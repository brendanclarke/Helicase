/*
 * Core/Hardware/SD/filesystem.c
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
 * filesystem.c - SD filesystem operation facade for LXR-02.
 *
 * This file is the boundary between application code and storage internals.
 * Clients should include filesystem.h, request typed operations, and call
 * filesystem_tick(). They should not include asyncfatfs.h, sd_routines.h,
 * spi_sd.h, or sdcard_lxr02.h directly.
 *
 * How to add a new file type:
 * 1. Add an FS_FILE_* enum in filesystem.h.
 * 2. Add a descriptor row in fs_file_descs[] below.
 * 3. Add an FS_INTERNAL_OP_* pair for load/save if the type needs payloads.
 * 4. Add a small streaming serializer here. Avoid staging whole files in RAM.
 * 5. Route filesystem_requestLoad/Save() to the new op(s).
 * 6. Add presetManager completion mapping and menu dispatch.
 * 7. Verify save, load, name browse, missing file, and busy request behavior.
 *
 * File payloads intentionally live here instead of kitBrowser.c. kitBrowser is
 * a kit-only slot scanner/display helper; typed load/save uses the descriptor
 * table and explicit payload serializers below.
 *
 * The filesystem pump remains single-context. afatfs_poll() is called only
 * from filesystem_tick(), including synchronous boot polling.
 */

#include "filesystem.h"
#include "asyncfatfs.h"
#include "fat_standard.h"
#include "storageTypes.h"
#include "SceneData.h"
#include "BankData.h"
#include "sd_routines.h"
#include "spi_sd.h"
#include "presetManager.h"
#include "ParameterArray.h"
#include "menu.h"
#include "SampleMemory.h"
#include "kitBrowser.h"
#include "sequencer.h"
#include "PatternData.h"
#include "MidiMessages.h"
#include "timebase.h"
#include "random.h"
#include <string.h>
#include <stdint.h>

#define FS_SECTOR_SIZE_BYTES 512u
#define FS_NUM_FATS_EXPECTED 2u
#define MBR_PARTITION_TYPE_EXFAT 0x07u
#define FS_CONTAINER_META_LEN 64u
#define FS_CONTAINER_KIT_LEN 512u
#define FS_CONTAINER_PAD_BYTE 0xffu
#define FS_KIT_LFN_MAX 80u
/*
 * Text line buffer for storageTypes schemas.
 *
 * Most files fit under 96 bytes, but the draft Scene/Bank pattern format writes
 * `trackN=<length>,<scale>,<128 on/off bits>`, which needs roughly 142 bytes
 * including newline/NUL. Keep this shared buffer large enough for that row
 * while still avoiding whole-file staging.
 */
#define FS_TEXT_LINE_MAX 160u
#define FS_INSTRUMENT_MAX_PER_TYPE 128u
#define FS_TEST_OBJECT_MAX 64u

/* -----------------------------------------------------------------------
** Operation types
** ----------------------------------------------------------------------- */
typedef enum {
    FS_INTERNAL_OP_NONE,
    FS_INTERNAL_OP_FLUSH_FINISH,
    FS_INTERNAL_OP_LOAD_KIT,
    FS_INTERNAL_OP_LOAD_KIT_MORPH,
    FS_INTERNAL_OP_LOAD_SCENE,
    FS_INTERNAL_OP_LOAD_BANK,
    FS_INTERNAL_OP_SAVE_KIT,
    FS_INTERNAL_OP_SAVE_SCENE,
    FS_INTERNAL_OP_SAVE_BANK,
    /*
     * Instrument Morph Save writes the root Instrument format with Morph Save
     * value projection. It is intentionally separate from the legacy flat .snd
     * morph load compatibility path.
     */
    FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH,
    FS_INTERNAL_OP_LOAD_MORPH,
    FS_INTERNAL_OP_LOAD_PATTERN,
    FS_INTERNAL_OP_SAVE_PATTERN,
    FS_INTERNAL_OP_LOAD_ALL,
    FS_INTERNAL_OP_SAVE_ALL,
    FS_INTERNAL_OP_LOAD_PERFORMANCE,
    FS_INTERNAL_OP_SAVE_PERFORMANCE,
    FS_INTERNAL_OP_LOAD_GLOBALS,
    FS_INTERNAL_OP_SAVE_GLOBALS,
    FS_INTERNAL_OP_SCAN_KITS,
    FS_INTERNAL_OP_SCAN_SCENES,
    FS_INTERNAL_OP_SCAN_BANKS,
    /*
     * Preview one selected Bank directory's 00..15 Scene children.
     *
     * Why: root Bank scanning only tells the browser which Bank slots exist;
     * Load:[Bank] LEDs also need the child Scene occupancy of the highlighted
     * slot. Inputs are op_slot and the root Bank scan cache. Outputs are the
     * operation-local op_bank_child_present_mask/name caches, exposed through
     * filesystem_bankChildSceneMask() after completion. This op never reads
     * bankset.bcg or changes BankData.
     */
    FS_INTERNAL_OP_SCAN_BANK_SCENES,
    FS_INTERNAL_OP_SCAN_INSTRUMENTS,
    FS_INTERNAL_OP_LOAD_INSTRUMENT,
    FS_INTERNAL_OP_SAVE_INSTRUMENT,
    FS_INTERNAL_OP_LOAD_NAME,
    FS_INTERNAL_OP_SCAN_TEST_FILES,
    FS_INTERNAL_OP_SCAN_TEST_DIRS,
    FS_INTERNAL_OP_LOAD_TEST_FILE,
    FS_INTERNAL_OP_LOAD_TEST_DIR,
    FS_INTERNAL_OP_SAVE_TEST_FILE,
    FS_INTERNAL_OP_SAVE_TEST_DIR,
    FS_INTERNAL_OP_SAVE_TEST_SIMPLE_DIR,
} fs_internal_op_t;

typedef struct {
    fs_file_type_t type;
    const char *extension;
    const char *literal_name;
    uint8_t numbered;
    uint8_t has_name_header;
    uint8_t supports_load;
    uint8_t supports_save;
} fs_file_desc_t;

static const fs_file_desc_t fs_file_descs[] = {
    { FS_FILE_KIT,         ".snd", NULL,      1, 1, 1, 0 },
    { FS_FILE_SCENE,       NULL,   NULL,      1, 0, 1, 0 },
    { FS_FILE_BANK,        NULL,   NULL,      1, 0, 1, 0 },
    { FS_FILE_MORPH,       ".snd", NULL,      1, 1, 1, 0 },
    { FS_FILE_PATTERN,     ".pat", NULL,      1, 1, 1, 1 },
    { FS_FILE_PERFORMANCE, ".prf", NULL,      1, 1, 1, 1 },
    { FS_FILE_ALL,         ".all", NULL,      1, 1, 1, 1 },
    { FS_FILE_SETTINGS,    NULL,   STORAGE_SETTINGS_FILENAME, 0, 0, 1, 1 },
    { FS_FILE_SAMPLES,     NULL,   NULL,      0, 0, 1, 0 },
};

/* -----------------------------------------------------------------------
** State
** ----------------------------------------------------------------------- */
static fs_internal_op_t current_op   = FS_INTERNAL_OP_NONE;
static fs_status_t      status       = FS_STATUS_IDLE;
static uint8_t          op_phase     = 0;
static char             fs_error_code[9];
/*
 * Deferred completion status used by FS_INTERNAL_OP_FLUSH_FINISH.
 *
 * Successful operations now finish in two steps: the individual state machine
 * closes its files, then the filesystem facade keeps status BUSY while
 * asyncfatfs drains dirty FAT, directory, and data sectors to the SD card. This
 * retained status is the value reported to Preset/Menu only after afatfs_flush()
 * confirms that no dirty or in-flight write remains. The affiliate boundary is
 * filesystem_finish(), filesystem_flushFinish_tick(), and completion_callback.
 */
static fs_status_t      op_flush_final_status = FS_STATUS_DONE;
/*
 * Current numbered operation slot.
 *
 * Kit/Scene directories now span user slots 000..999, so the common request
 * field must be wider than uint8_t even though older .snd/.pat/.prf payloads
 * still use only the low three decimal digits for their legacy filenames.
 */
static uint16_t         op_slot      = 0;
static fs_file_type_t   op_file_type = FS_FILE_KIT;
static afatfsFilePtr_t  op_file      = NULL;
static volatile bool    op_file_ready = false;
static uint32_t         op_bytes_done = 0;
static fs_status_t      op_close_status = FS_STATUS_DONE;
static fs_completion_cb_t completion_callback = NULL;

/* Staging buffer for bulk writes.
 * Kit save: 8 (name) + END_OF_SOUND_PARAMETERS bytes.
 * Globals save: NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS bytes.
 * Only one operation at a time, so one buffer suffices.
 * NUM_PARAMS is kept wide for descriptor-id compatibility; use 512 for margin. */
static uint8_t staging_buf[512];

/* Name buffer for load_name operation.
 *
 * loaded_name is the public result returned by filesystem_loadedName(). It is
 * separate from preset_currentName because name browsing should not overwrite
 * the currently-loaded kit/pattern until the caller explicitly loads it.
 */
static char loaded_name[9];

/* Phase 2 Kit/ directory scan and load state.
 *
 * Why it exists: the new on-card layout stores kits as Kit/NNN Name folders,
 * but existing preset/menu clients still ask for zero-based numeric kit slots.
 * The scan cache bridges those worlds. kit_slot_present says whether a slot
 * was found. kit_slot_name stores the eight-character display name used by the
 * LCD. kit_slot_open_name stores the FAT short alias needed by afatfs_fopen(),
 * because long filenames are useful for display but asyncfatfs opens by short
 * name in the current directory.
 *
 * op_kit_root_dir/op_kit_slot_dir/op_finder/op_lfn_* are private scratch for
 * the scan and directory-load state machines. op_line_buf/op_line_len stream
 * kitset.kcg and instrument text lines without staging whole files in RAM.
 * op_kitset/op_instrument_state/op_instrument_slot are the storageTypes parser
 * state. Accessors are filesystem_kitSlotExists() and filesystem_kitSlotName();
 * clients are menu.c, kitBrowser.c via filesystem_requestLoadName(), and
 * filesystem_loadKitDirectory_tick().
 */
static uint8_t kit_slot_present[STORAGE_KIT_MAX_SLOTS];
static char kit_slot_name[STORAGE_KIT_MAX_SLOTS][STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char kit_slot_open_name[STORAGE_KIT_MAX_SLOTS][STORAGE_KIT_FILENAME_MAX];
/*
 * Root Scene/ directory scan cache.
 *
 * Scene folders use the same numbered "NNN Name" convention as Kit folders,
 * but their occupancy and display names are independent library state. Keeping
 * a separate cache prevents a Scene slot from borrowing a Kit open alias or
 * showing a Kit name in Load:[Scene]. Each open name is the FAT short alias
 * that asyncfatfs can reopen after the scan; each display name is the actual
 * LFN-derived user label truncated/padded to the LCD field.
 */
static uint8_t scene_slot_present[STORAGE_SCENE_MAX_SLOTS];
static char scene_slot_name[STORAGE_SCENE_MAX_SLOTS]
                           [STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
static char scene_slot_open_name[STORAGE_SCENE_MAX_SLOTS]
                                [STORAGE_KIT_FILENAME_MAX];
/*
 * Root Bank/ directory scan cache.
 *
 * Root Banks are library slots named `NNN <bank name>` just like root Scenes,
 * but a Bank folder contains bankset.bcg plus Bank-local Scene children named
 * `00..15`. This cache stores only the root Bank identity used by Load/Save
 * browsing; child Scene names live in per-operation scratch while a selected
 * Bank is open.
 */
static uint8_t bank_slot_present[STORAGE_BANK_MAX_SLOTS];
static char bank_slot_name[STORAGE_BANK_MAX_SLOTS]
                          [STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char bank_slot_open_name[STORAGE_BANK_MAX_SLOTS]
                               [STORAGE_KIT_FILENAME_MAX];
static afatfsFilePtr_t op_kit_root_dir = NULL;
static afatfsFilePtr_t op_kit_slot_dir = NULL;
static afatfsFinder_t op_finder;
/*
 * LFN-aware object scan scratch for production Kit/Instrument browsers.
 *
 * Scene scan still uses the older raw finder until Scene is promoted in a
 * later pass. Kit and Instrument restoration should use asyncfatfs' object
 * iterator so their display names, short aliases, and file/directory kinds are
 * resolved by the same code proven by the File/Dir diagnostics.
 */
static afatfsObjectFinder_t op_object_finder;
static afatfsObjectInfo_t op_object;
static char op_root_open_name[AFATFS_SHORT_FILENAME_MAX];
static char op_lfn_name[FS_KIT_LFN_MAX];
static uint8_t op_lfn_valid = 0;
static char op_line_buf[FS_TEXT_LINE_MAX];
static uint8_t op_line_len = 0;
static storage_kitset_t op_kitset;
static storage_instrument_state_t op_instrument_state;
static uint8_t op_instrument_slot = 0;
static uint8_t op_remove_done = 0u;
/*
 * Shared streaming writer scratch.
 *
 * Kit Save, root Instrument Save, and future text-shaped saves do not allocate
 * whole files in RAM. storageTypes/filesystem_next* callbacks produce one
 * line at a time into op_write_line_buf, while op_write_line_offset tracks
 * partial asyncfatfs writes of that line. op_write_line_index is the logical
 * schema line number, not a byte offset.
 */
static char op_write_line_buf[FS_TEXT_LINE_MAX];
static uint16_t op_write_line_len = 0u;
static uint16_t op_write_line_offset = 0u;
static uint16_t op_write_line_index = 0u;
/*
 * Request-time Kit Save state.
 *
 * The source Scene, visible target folder name, visible member filenames, and
 * normal/Morph projection mode are captured when Preset posts the request.
 * Encoder/menu movement after acceptance must not retarget an in-flight save.
 * The open-name buffer stores asyncfatfs' returned 8.3 alias for reopening the
 * newly-created LFN directory.
 */
static char op_save_kit_dir_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_kit_member_display_file[STORAGE_KIT_SLOT_COUNT]
                                            [STORAGE_KIT_MEMBER_FILENAME_MAX];
static char op_save_kit_dir_open_name[AFATFS_SHORT_FILENAME_MAX];
static char op_save_scene_kit_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_scene_kit_open_name[AFATFS_SHORT_FILENAME_MAX];
static uint8_t op_kit_save_source_scene = 0u;
static storage_instrument_save_mode_t op_kit_save_mode =
    STORAGE_INSTRUMENT_SAVE_NORMAL;

#define FS_DELETE_DEPTH_MAX 8u

/*
 * Delete helpers are state machines because asyncfatfs exposes only
 * foreground-pumped operations. Slot delete scans /Kit/ for all physical
 * directories matching a numbered slot. Tree delete removes one visible
 * directory component recursively after the caller has entered its parent.
 */
typedef enum {
    FS_DELETE_SLOT_IDLE = 0u,
    FS_DELETE_SLOT_OPEN_SCAN,
    FS_DELETE_SLOT_WAIT_SCAN,
    FS_DELETE_SLOT_SCAN_NEXT,
    FS_DELETE_SLOT_CLOSE_SCAN,
    FS_DELETE_SLOT_WAIT_CLOSE_SCAN,
    FS_DELETE_SLOT_DELETE_MATCH,
    FS_DELETE_SLOT_DONE,
    FS_DELETE_SLOT_ERROR
} fs_delete_slot_phase_t;

typedef enum {
    FS_DELETE_TREE_IDLE = 0u,
    FS_DELETE_TREE_OPEN_TARGET,
    FS_DELETE_TREE_WAIT_TARGET,
    FS_DELETE_TREE_CLOSE_TARGET,
    FS_DELETE_TREE_OPEN_SCAN,
    FS_DELETE_TREE_WAIT_SCAN,
    FS_DELETE_TREE_SCAN_NEXT,
    FS_DELETE_TREE_CLOSE_SCAN_BEFORE_CHILD,
    FS_DELETE_TREE_HANDLE_CHILD,
    FS_DELETE_TREE_WAIT_FILE_REMOVE,
    FS_DELETE_TREE_WAIT_CHILD_DIR,
    FS_DELETE_TREE_CLOSE_CHILD_DIR,
    FS_DELETE_TREE_OPEN_PARENT,
    FS_DELETE_TREE_WAIT_PARENT,
    FS_DELETE_TREE_CLOSE_PARENT,
    FS_DELETE_TREE_REMOVE_EMPTY_DIR,
    FS_DELETE_TREE_WAIT_REMOVE_EMPTY_DIR,
    FS_DELETE_TREE_DONE,
    FS_DELETE_TREE_ERROR
} fs_delete_tree_phase_t;

static fs_delete_tree_phase_t op_delete_tree_phase = FS_DELETE_TREE_IDLE;
static uint8_t op_delete_tree_depth = 0u;
static char op_delete_tree_name_stack[FS_DELETE_DEPTH_MAX]
                                     [AFATFS_LONG_FILENAME_MAX + 1u];
static char op_delete_tree_open_name_stack[FS_DELETE_DEPTH_MAX]
                                           [AFATFS_SHORT_FILENAME_MAX];
static char op_delete_tree_child_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_delete_tree_child_open_name[AFATFS_SHORT_FILENAME_MAX];
static afatfsObjectKind_t op_delete_tree_child_kind = AFATFS_OBJECT_NONE;
static afatfsFilePtr_t op_delete_tree_dir = NULL;
static fs_delete_slot_phase_t op_delete_slot_phase = FS_DELETE_SLOT_IDLE;
static afatfsFilePtr_t op_delete_slot_dir = NULL;
static uint8_t op_delete_slot_allow_short_alias = 0u;
static uint8_t op_delete_slot_bank_scene = 0u;
static uint16_t op_delete_slot_number = 0u;
static afatfsObjectId_t op_delete_slot_target_id;
static bool op_delete_tree_done = false;
static afatfsResultCode_t op_delete_tree_result = AFATFS_RESULT_OK;

static void on_delete_tree_complete(afatfsResultCode_t result)
{
    op_delete_tree_done = true;
    op_delete_tree_result = result;
}
/*
 * Staged Kit payload and target mask for multi-Scene Kit Load.
 *
 * The directory state machine fills this isolated kit_t while every kitset and
 * instrument file is validated. Only after the final file succeeds is it
 * copied into the selected resident Scenes for normal Kit Load, preventing a
 * malformed Kit from leaving a partially replaced live Scene. KitMrp uses the
 * same staging image but skips the filesystem commit so Preset can copy only
 * same-type morphable endpoints into the current resident kits. The mask uses
 * Scene indices as bit positions; its callers are the normal Kit and KitMrp
 * request helpers.
 */
static kit_t op_staged_kit;
static uint16_t op_kit_load_scene_mask = 0u;
/*
 * Staged Scene payload and Scene-specific operation scratch.
 *
 * Scene Load validates every child before writing resident memory: sceneset,
 * embedded Kit, bridge Pattern, and placeholder Effect. The same staging Scene
 * is also reused by save helpers that need a source Scene pointer stable across
 * asynchronous phases. op_scene_display_name is the eight-character Scene
 * directory name captured from the root Scene scan cache. sceneset.scg never
 * stores or overrides it. op_scene_child_display_name captures the text after
 * "Kit " while scanning children so the embedded Kit's retained name survives
 * initial boot Scene Load and later seeds Save:[Kit] character entry.
 */
static scene_t op_staged_scene;
static uint16_t op_scene_load_scene_mask = 0u;
static char op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
static char op_scene_child_open_name[STORAGE_KIT_FILENAME_MAX];
static char op_scene_child_display_name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char op_scene_pattern_open_name[STORAGE_KIT_FILENAME_MAX];
static char op_scene_effect_open_name[STORAGE_KIT_FILENAME_MAX];
static storage_sceneset_t op_sceneset_state;
static storage_effect_state_t op_effect_state;
static storage_pattern_stub_state_t op_pattern_stub_state;
/*
 * Bank load/save scratch.
 *
 * Root Bank identity uses the scan cache above. While one Bank is selected,
 * op_bank_child_* caches the Bank-local two-digit Scene children discovered
 * under that folder. op_bank_payload_active lets the Bank state machine hand
 * control to the existing Scene payload reader/writer after it has positioned
 * asyncfatfs inside the selected Bank directory.
 */
static storage_bankset_t op_bankset_state;
static char op_bank_display_name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char op_save_bank_dir_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_bank_tmp_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_bank_old_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_bank_dir_open_name[AFATFS_SHORT_FILENAME_MAX];
static char op_save_bank_rename_open_name[AFATFS_SHORT_FILENAME_MAX];
static uint8_t op_save_bank_scratch_attempts = 0u;
static uint8_t op_save_bank_scratch_collision = 0u;
static uint8_t op_bank_child_present[STORAGE_BANK_SCENE_MAX_SLOTS];
static char op_bank_child_name[STORAGE_BANK_SCENE_MAX_SLOTS]
                              [STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
static char op_bank_child_open_name[STORAGE_BANK_SCENE_MAX_SLOTS]
                                   [STORAGE_KIT_FILENAME_MAX];
static uint16_t op_bank_child_present_mask = 0u;
static uint16_t op_bank_scene_load_mask = 0u;
static uint16_t op_bank_scene_save_mask = 0u;
static uint8_t op_bank_active_scene = 0u;
static uint8_t op_bank_child_cursor = 0u;
static uint8_t op_bank_loaded_scene = 0u;
static uint8_t op_bank_payload_active = 0u;
static uint8_t op_rename_done = 0u;
/*
 * Scene load helper prototypes.
 *
 * The Scene loader is kept beside the Kit directory loader, but a few generic
 * Scene-name helpers live with the save/scan helpers later in this file.
 * Declaring them here keeps the C translation unit explicit under gnu11 and
 * avoids implicit-function warnings when the state machine calls them.
 */
static void filesystem_initStagedScene(scene_t *scene);
static uint8_t filesystem_defaultVoiceAudioOut(uint8_t slot);
static uint8_t filesystem_nameStartsWithKitSpace(const char *name);
static uint8_t filesystem_nameHasExtension(const char *name,
                                           const char *extension);
static uint16_t filesystem_interpolateMorphEndpoint(uint16_t normal,
                                                    uint16_t morph,
                                                    uint8_t amount);
static void filesystem_saveKitDirectory_tick(void);
static void filesystem_saveSceneDirectory_tick(void);
static void filesystem_loadBankDirectory_tick(void);
static void filesystem_saveBankDirectory_tick(void);
static void filesystem_scanBanks_tick(void);
static void filesystem_scanBankScenes_tick(void);
static void filesystem_deleteTreeStartWithOpenName(const char *display_name,
                                                   const char *open_name);
static fs_status_t filesystem_deleteTree_tick(void);
static void filesystem_makeNumberedDir(char *dst,
                                       uint16_t slot,
                                       const char display[8]);
/*
 * Root Instrument Save state machine declaration.
 *
 * filesystem_tick() dispatches every asynchronous operation from one switch
 * near the public facade. The save implementation lives beside Instrument
 * Load and the text-line writer helpers later in this file, so this prototype
 * keeps the single dispatcher explicit without moving a large state machine
 * above the related parser/serializer context.
 */
static void filesystem_saveInstrument_tick(void);
typedef struct {
    storage_instrument_write_view_t view;
} filesystem_instrument_write_ctx_t;
/*
 * Shared text-writer helpers used by Kit Save and root Instrument Save.
 *
 * The Instrument Save state machine appears before the Kit Save helpers so it
 * can sit beside Instrument Load. These declarations make the dependency
 * explicit: storageTypes supplies one logical text line, while filesystem.c
 * owns asyncfatfs write offsets and foreground pacing.
 */
static uint8_t filesystem_nextInstrumentLine(char *dst, uint16_t cap,
                                             void *raw);
static uint8_t filesystem_nextKitsetLine(char *dst, uint16_t cap,
                                         void *raw);
static uint8_t filesystem_nextScenesetLine(char *dst, uint16_t cap,
                                           void *raw);
static uint8_t filesystem_nextEffectPlaceholderLine(char *dst, uint16_t cap,
                                                    void *raw);
static uint8_t filesystem_nextPatternStubLine(char *dst, uint16_t cap,
                                              void *raw);
static uint8_t filesystem_nextBanksetLine(char *dst, uint16_t cap,
                                          void *raw);
static uint8_t filesystem_appendChar(char *dst, uint16_t cap,
                                     uint16_t *pos, char c);
static uint8_t filesystem_appendText(char *dst, uint16_t cap,
                                     uint16_t *pos, const char *text);
static uint8_t filesystem_writeTextLine(uint8_t (*next_line)(char *, uint16_t,
                                                             void *),
                                        void *ctx);
/*
 * Root Instrument/ browser and single-load state.
 *
 * The Instrument pool is list-indexed by instrument type rather than numbered
 * Kit slot. The cache stores an asyncfatfs-openable short filename plus the
 * eight-character display stem for each discovered file. Counts are per
 * instrument type; the UI display number is derived from the sorted index and
 * visually saturates at 999.
 */
static uint8_t instrument_file_count[INSTRUMENT_TYPE_UNKNOWN];
static char instrument_file_name[INSTRUMENT_TYPE_UNKNOWN]
                                [FS_INSTRUMENT_MAX_PER_TYPE]
                                [STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char instrument_file_open_name[INSTRUMENT_TYPE_UNKNOWN]
                                     [FS_INSTRUMENT_MAX_PER_TYPE]
                                     [STORAGE_KIT_FILENAME_MAX];
static char instrument_file_stem[INSTRUMENT_TYPE_UNKNOWN]
                                [FS_INSTRUMENT_MAX_PER_TYPE]
                                [SCENE_INSTRUMENT_STEM_LEN + 1u];
static uint8_t op_instrument_load_destination_slot = 0u;
static uint8_t op_instrument_load_destination_scene = 0u;
static instrument_type_t op_instrument_load_type = INSTRUMENT_TYPE_UNKNOWN;
static uint8_t op_instrument_load_index = 0u;
/*
 * Root Instrument Save request scratch.
 *
 * These fields capture the source Scene/slot and the exact display filename at
 * request acceptance time. Menu may continue moving after the request starts,
 * but the writer must serialize the originally selected resident instrument
 * into the root Instrument/ pool.
 */
static uint8_t op_instrument_save_source_scene = 0u;
static uint8_t op_instrument_save_source_slot = 0u;
static instrument_type_t op_instrument_save_type = INSTRUMENT_TYPE_UNKNOWN;
static char op_instrument_save_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_instrument_save_open_name[AFATFS_SHORT_FILENAME_MAX];
static storage_instrument_save_mode_t op_instrument_save_mode =
    STORAGE_INSTRUMENT_SAVE_NORMAL;
/*
 * Validated one-Instrument staging payload.
 *
 * Filesystem owns these buffers from request start through completion. Parsing
 * writes only here, so audio cannot observe a half-read type or parameter
 * image. Preset reads the immutable result after FS_STATUS_DONE and performs
 * the ordered modulation-clear/Scene-commit/runtime-apply transaction.
 */
static kit_instrument_slot_t op_staged_instrument;
static char op_staged_instrument_display_name[9];
static char op_staged_instrument_stem[SCENE_INSTRUMENT_STEM_LEN + 1u];
static uint32_t op_stream_index = 0;
static uint8_t op_item_offset = 0;
static uint8_t op_loaded_active_pattern_running = 0;
static uint8_t op_file_version = 0;
static fs_mount_result_t fs_last_mount_result = FS_MOUNT_RESULT_UNKNOWN;
static uint8_t fs_boot_detected_unsupported_card = 0;
static fs_stale_warning_source_t fs_stale_warning_pending = FS_STALE_WARNING_NONE;
/*
 * Root-level File/Dir test caches for the asyncfatfs expansion menu.
 *
 * These are intentionally independent of Kit/Scene/Instrument caches. The
 * expansion tests need to prove generic LFN behavior at the filesystem layer:
 * scan every root file or directory, preserve exact case in displayName, sort
 * by that visible component, and reopen by exact case-sensitive display text.
 */
static char fs_test_file_name[FS_TEST_OBJECT_MAX][FS_TEST_NAME_MAX + 1u];
static char fs_test_dir_name[FS_TEST_OBJECT_MAX][FS_TEST_NAME_MAX + 1u];
static uint8_t fs_test_file_count = 0u;
static uint8_t fs_test_dir_count = 0u;
static char op_test_name[FS_TEST_NAME_MAX + 1u];
static char op_test_child_name[FS_TEST_NAME_MAX + 1u];
static char op_test_short_alias[AFATFS_SHORT_FILENAME_MAX];
static char op_test_parent_alias[AFATFS_SHORT_FILENAME_MAX];
static uint8_t op_test_bytes[FS_TEST_RESULT_BYTES];
static fs_test_result_kind_t op_test_result_kind = FS_TEST_RESULT_BYTES_READY;
static uint32_t fs_test_payload_counter = 0u;
static uint16_t fs_bank_scratch_counter = 0u;
static afatfsObjectFinder_t op_test_object_finder;
static afatfsObjectInfo_t op_test_object;
static afatfsObjectKind_t op_test_best_kind = AFATFS_OBJECT_NONE;
static afatfsFilePtr_t op_test_dir = NULL;
static uint8_t op_test_lookup_result = 0u;
static uint8_t op_test_verify_seen_alias = 0u;
static uint8_t op_test_verify_seen_fold = 0u;

#define FS_TEST_LOOKUP_ERROR 0u
#define FS_TEST_LOOKUP_OPEN_ALIAS 1u
#define FS_TEST_LOOKUP_CREATE 2u

#define FS_IDLE_POLL_MS 5u
/* .all still carries the old raw meta prefix until that container is rebuilt.
 * settings.cfg does not use this compatibility span: root settings now persist
 * as keyed text and the former raw globals filename is deliberately ignored. */
#define FS_GLOBALS_LEGACY_LEN_22  22u
static uint16_t fs_last_idle_poll_tick = 0;

/* Existing morph destination buffer owned by preset/sound code.
 *
 * Directory kit loading writes primary parameters into parameter_values[] and
 * optional/fallback morph parameters into parameters2[]. The legacy morph .SND
 * loader also uses parameters2[], so this remains an external affiliate rather
 * than storage owned by filesystem.c.
 */
extern uint8_t parameters2[END_OF_SOUND_PARAMETERS];

/* Compatibility bridge to the existing kitBrowser map.
 *
 * The Phase 2 scan now discovers Kit/ directories, but kitBrowser still expects
 * kb_map/kb_numKits to describe the slots that exist. filesystem_recordKit-
 * Directory() populates both the new scan cache and this legacy map until the
 * browser is rewritten around the new filesystem contract.
 */
extern uint16_t kb_map[];
extern uint16_t kb_numKits;

/* -----------------------------------------------------------------------
** fopen callback - asyncfatfs fires this when file open completes
** ----------------------------------------------------------------------- */
static void on_file_opened(afatfsFilePtr_t file)
{
    op_file = file;
    op_file_ready = true;
}

/* -----------------------------------------------------------------------
** fclose callback
** ----------------------------------------------------------------------- */
static volatile bool op_close_done = false;
static void on_file_closed(void)
{
    op_close_done = true;
}

static void on_remove_complete(void)
{
    /*
     * Mark completion of asyncfatfs overwrite preflight work.
     *
     * What: Latches that afatfs_removeObjects_lfn() has called back. The
     * following state-machine phase decides success by opening the expected
     * object with the target display name.
     *
     * Why: File overwrite is now a two-step operation: collapse same-casefold
     * file variants before writing, then create exactly one object with the
     * user's entered case. The filesystem pump needs this callback bridge
     * between asyncfatfs completion and the next phase.
     *
     * Affiliates/clients: filesystem_saveInstrument_tick() and
     * InstrumentMrp Save.
     */
    op_remove_done = 1u;
}

static void on_rename_complete(void)
{
    /*
     * Latch completion of an async directory rename.
     *
     * Inputs: asyncfatfs_renameObject_lfn() invokes this after it has either
     * rewritten the object name run or failed to find/rename the source.
     * Output: Bank Save promotion phases wake up and inspect the caller-owned
     * open-name buffer. asyncfatfs writes that buffer only on success, so an
     * empty buffer means "rename did not produce the requested object".
     */
    op_rename_done = 1u;
}

/* -----------------------------------------------------------------------
** Helpers
** ----------------------------------------------------------------------- */
static const fs_file_desc_t *filesystem_desc(fs_file_type_t type)
{
    uint8_t i;
    for (i = 0; i < (uint8_t)(sizeof(fs_file_descs) / sizeof(fs_file_descs[0])); i++) {
        if (fs_file_descs[i].type == type)
            return &fs_file_descs[i];
    }
    return NULL;
}

static uint8_t filesystem_isPowerOfTwoU8(uint8_t value)
{
    return (uint8_t)(value && ((value & (uint8_t)(value - 1u)) == 0u));
}

static void filesystem_copyTestName(char dst[FS_TEST_NAME_MAX + 1u],
                                    const char *src)
{
    uint8_t i = 0u;

    /*
     * Copy and normalize one single-component test name into bounded
     * filesystem state.
     *
     * Inputs come from asyncfatfs displayName or Menu's filename editor.
     * Output is NUL-terminated and never contains slash-separated paths. The
     * low-level LFN writer applies the same policy again, but doing it here
     * keeps compare/verification state identical to the component that
     * asyncfatfs will actually create. The Save menu editor is fixed-width and
     * space-padded, so trailing spaces and periods must be stripped before the
     * request records its target.
     */
    if (!src)
        src = "";
    while (i < FS_TEST_NAME_MAX && src[i] != '\0' && src[i] != '/' &&
           src[i] != '\\') {
        char c = src[i];
        dst[i] = fat_lfnCharAllowed(c) ? c : '_';
        i++;
    }
    while (i > 0u && (dst[i - 1u] == ' ' || dst[i - 1u] == '.'))
        i--;
    dst[i] = '\0';
}

static void filesystem_setTestDiag(const char *code)
{
    filesystem_copyTestName(op_test_child_name, code);
}

static void filesystem_insertTestName(char names[FS_TEST_OBJECT_MAX]
                                                [FS_TEST_NAME_MAX + 1u],
                                      uint8_t *count,
                                      const char *name)
{
    uint8_t pos = 0u;

    /*
     * Insert one scanned object into the root test cache in display order.
     *
     * The comparison is intentionally case-sensitive because these menus are
     * the proof surface for exact-case asyncfatfs lookup. When the cache is
     * full, names beyond the retained sorted window are dropped deterministically
     * rather than wrapping or corrupting earlier entries.
     */
    while (pos < *count &&
           fat_compareDisplayName(names[pos], name, true) <= 0)
        pos++;

    if (*count < FS_TEST_OBJECT_MAX) {
        for (uint8_t i = *count; i > pos; i--)
            filesystem_copyTestName(names[i], names[i - 1u]);
        filesystem_copyTestName(names[pos], name);
        (*count)++;
        return;
    }

    if (pos >= FS_TEST_OBJECT_MAX)
        return;
    for (uint8_t i = (uint8_t)(FS_TEST_OBJECT_MAX - 1u); i > pos; i--)
        filesystem_copyTestName(names[i], names[i - 1u]);
    filesystem_copyTestName(names[pos], name);
}

static void filesystem_makeTestBytes(void)
{
    uint32_t value;

    /*
     * Build a four-byte diagnostic payload without assuming back-to-back
     * RNG_DR reads are fresh.
     *
     * GetRngValue() reads the hardware RNG data register directly and may
     * return the same 16-bit word when called twice in the same foreground
     * pass. The File and Dir save tests need four visibly independent bytes so
     * the LCD result can prove byte order and file persistence. Seed a tiny
     * software mixer from one hardware sample, the system tick, and an
     * incrementing counter; this is a test payload, not cryptographic
     * randomness.
     */
    fs_test_payload_counter++;
    value = ((uint32_t)(uint16_t)GetRngValue() << 16) ^
            ((uint32_t)time_sysTick << 1) ^
            (fs_test_payload_counter * 0x9e3779b9u);
    /*
     * Xorshift32 diffusion spreads the 16-bit hardware sample, foreground
     * tick, and monotonically increasing counter across all four output bytes.
     * The arithmetic is intentionally fixed-width unsigned math so the same
     * byte order is written to the file and copied to the LCD result overlay.
     */
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    op_test_bytes[0] = (uint8_t)(value & 0xffu);
    op_test_bytes[1] = (uint8_t)((value >> 8) & 0xffu);
    op_test_bytes[2] = (uint8_t)((value >> 16) & 0xffu);
    op_test_bytes[3] = (uint8_t)((value >> 24) & 0xffu);
    op_test_result_kind = FS_TEST_RESULT_BYTES_READY;
}

static uint16_t filesystem_nextBankScratchNonce(void)
{
    uint16_t nonce;

    /*
     * Generate one short nonce for Bank Save scratch directory names.
     *
     * Inputs: hardware RNG, the 1ms service tick, and a firmware-local counter
     * that advances on every Save Bank request. Output: a 16-bit value encoded
     * by filesystem_makeBankScratchDir() into `tmpSSS-NNNN` and `oldSSS-NNNN`.
     *
     * Why: asyncfatfs mkdir is intentionally create-or-open. If a previous
     * failed save left a `tmp...` directory and a later save reused the exact
     * same component, the writer could merge into that stale temp tree. Mixing
     * three independent sources makes reuse across repeated hardware retests
     * and the 16-bit tick wrap much less likely without adding recursive
     * cleanup back into the foreground Save Bank path.
     */
    fs_bank_scratch_counter++;
    nonce = (uint16_t)GetRngValue();
    nonce ^= time_sysTick;
    nonce ^= (uint16_t)(fs_bank_scratch_counter * 0x9e37u);
    return nonce;
}

static uint8_t filesystem_readTestBytesTick(void)
{
    /*
     * Incrementally read up to four bytes from the current open file.
     *
     * Output: nonzero when the result buffer is complete or EOF has been
     * reached. Missing bytes remain zero so empty or short files produce a
     * deterministic display instead of stale data from a previous test.
     */
    while (op_item_offset < FS_TEST_RESULT_BYTES) {
        uint32_t n = afatfs_fread(op_file,
                                  op_test_bytes + op_item_offset,
                                  (uint32_t)(FS_TEST_RESULT_BYTES -
                                             op_item_offset));
        if (n != 0u) {
            op_item_offset = (uint8_t)(op_item_offset + n);
            op_bytes_done += n;
            continue;
        }
        if (afatfs_feof(op_file))
            return 1u;
        return 0u;
    }
    return 1u;
}

static uint8_t filesystem_writeTestBytesTick(void)
{
    /*
     * Incrementally write the four-byte test payload to the current file.
     *
     * afatfs_fwrite() may accept fewer bytes than requested when cache or SD
     * state is busy. The offset is therefore advanced only by the accepted
     * count and the state machine retries until all four bytes have been
     * handed to asyncfatfs.
     */
    while (op_item_offset < FS_TEST_RESULT_BYTES) {
        uint32_t n = afatfs_fwrite(op_file,
                                   op_test_bytes + op_item_offset,
                                   (uint32_t)(FS_TEST_RESULT_BYTES -
                                              op_item_offset));
        if (n == 0u)
            return 0u;
        op_item_offset = (uint8_t)(op_item_offset + n);
        op_bytes_done += n;
    }
    return 1u;
}

static uint8_t filesystem_hasFatBootSignature(const uint8_t *sector)
{
    return (uint8_t)(sector[FS_SECTOR_SIZE_BYTES - 2u] == FAT_VOLUME_ID_SIGNATURE_1 &&
                     sector[FS_SECTOR_SIZE_BYTES - 1u] == FAT_VOLUME_ID_SIGNATURE_2);
}

static uint8_t filesystem_isPlausibleFatVolume(const uint8_t *sector, uint32_t *num_clusters)
{
    const fatVolumeID_t *volume = (const fatVolumeID_t *)sector;
    uint32_t totalSectors;
    uint32_t fatSectors;
    uint32_t rootDirectorySectors;
    uint32_t usedSectors;
    uint32_t dataSectors;

    if (!filesystem_hasFatBootSignature(sector))
        return 0;
    if (volume->bytesPerSector != FS_SECTOR_SIZE_BYTES || volume->numFATs != FS_NUM_FATS_EXPECTED)
        return 0;
    if (volume->sectorsPerCluster < 1u || volume->sectorsPerCluster > 128u ||
        !filesystem_isPowerOfTwoU8(volume->sectorsPerCluster))
        return 0;

    fatSectors = (volume->FATSize16 != 0u) ? volume->FATSize16 : volume->fatDescriptor.fat32.FATSize32;
    if (fatSectors == 0u)
        return 0;

    totalSectors = (volume->totalSectors16 != 0u) ? volume->totalSectors16 : volume->totalSectors32;
    if (totalSectors == 0u)
        return 0;

    rootDirectorySectors = ((uint32_t)volume->rootEntryCount * FAT_DIRECTORY_ENTRY_SIZE +
                            (volume->bytesPerSector - 1u)) / volume->bytesPerSector;
    usedSectors = (uint32_t)volume->reservedSectorCount + (volume->numFATs * fatSectors) + rootDirectorySectors;
    if (totalSectors <= usedSectors)
        return 0;

    dataSectors = totalSectors - usedSectors;
    *num_clusters = dataSectors / volume->sectorsPerCluster;
    return 1;
}

static uint8_t filesystem_sectorLooksLikeExFat(const uint8_t *sector)
{
    static const char exfat_oem[8] = { 'E', 'X', 'F', 'A', 'T', ' ', ' ', ' ' };

    if (!filesystem_hasFatBootSignature(sector))
        return 0;
    return (uint8_t)(memcmp(&sector[3], exfat_oem, sizeof(exfat_oem)) == 0);
}

static uint8_t filesystem_metaPaddingIsFF(const uint8_t *meta, uint16_t start)
{
    for (uint16_t i = start; i < FS_CONTAINER_META_LEN; i++) {
        if (meta[i] != FS_CONTAINER_PAD_BYTE)
            return 0u;
    }
    return 1u;
}

static uint8_t filesystem_metaHasStoredGlobalsLen(const uint8_t *meta, uint16_t len)
{
    /* .all meta is always a 64-byte field. With no version byte, infer the
    ** stored globals length from the first 0xff-padded byte after real data.
    ** This lets us accept exactly legacy-22 or current-length globals while
    ** rejecting random/stale layouts instead of applying bad values. */
    if (len == 0u || len > FS_CONTAINER_META_LEN)
        return 0u;
    if (meta[len - 1u] == FS_CONTAINER_PAD_BYTE)
        return 0u;
    return filesystem_metaPaddingIsFF(meta, len);
}

static void filesystem_resetGlobalsToDefaults(void)
{
    /* Defaults first, then overlay only the bytes we trust. This prevents
    ** shorter/stale files from leaving zero-filled or shifted parameters live. */
    uint16_t globals_len = (uint16_t)(NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS);
    memset(parameter_values + PAR_BEGINNING_OF_GLOBALS, 0, globals_len);
    parameter_values[PAR_BPM] = 120u;
    parameter_values[PAR_OSC_WAVE_INTERP] = 0u;
}

static void filesystem_sanitizeLoadedGlobals(void)
{
    uint8_t ch = parameter_values[PAR_MIDI_CHAN_GLOBAL];
    if (ch == 0u || ch > 16u)
        parameter_values[PAR_MIDI_CHAN_GLOBAL] = 1u;
}

static void filesystem_applyGlobalsPrefix(const uint8_t *src, uint16_t src_len)
{
    uint16_t globals_len = (uint16_t)(NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS);
    uint16_t copy_len = (src_len < globals_len) ? src_len : globals_len;

    filesystem_resetGlobalsToDefaults();
    if (copy_len > 0u) {
        memcpy(parameter_values + PAR_BEGINNING_OF_GLOBALS, src, copy_len);
    }
    filesystem_sanitizeLoadedGlobals();
}

static void filesystem_applyLegacy22Globals(const uint8_t *src, uint16_t src_len)
{
    /* Legacy 22-byte globals are treated as a supported compatibility case:
    ** load all overlapping bytes silently, then force fields that moved or did
    ** not exist in the old file to safe/current defaults. */
    uint16_t copy_len = src_len;

    if (copy_len > FS_GLOBALS_LEGACY_LEN_22)
        copy_len = FS_GLOBALS_LEGACY_LEN_22;

    filesystem_applyGlobalsPrefix(src, copy_len);
    parameter_values[PAR_EXT_SYNC] = SEQ_EXT_SYNC_AUTO;
    parameter_values[PAR_OSC_WAVE_INTERP] = 1u;
}

static uint16_t filesystem_staleGlobalsPrefixLimit(void)
{
    return (uint16_t)(PAR_PRESCALER_CLOCK_OUT1 - PAR_BEGINNING_OF_GLOBALS + 1u);
}

static uint16_t filesystem_staleMetaPrefixLen(const uint8_t *meta)
{
    uint16_t limit = filesystem_staleGlobalsPrefixLimit();
    if (limit > FS_CONTAINER_META_LEN)
        limit = FS_CONTAINER_META_LEN;

    for (uint16_t i = 0u; i < limit; i++) {
        if (meta[i] == FS_CONTAINER_PAD_BYTE)
            return i;
    }
    return limit;
}

static void filesystem_applyStaleGlobalsFallback(const uint8_t *src, uint16_t src_len)
{
    /* Unknown globals lengths are stale, but still likely start with the old
    ** global ordering. Preserve the known prefix through clock-out prescaler,
    ** default everything else, and request the "check&save" warning. */
    uint16_t copy_limit = filesystem_staleGlobalsPrefixLimit();
    uint16_t copy_len = (src_len < copy_limit) ? src_len : copy_limit;

    filesystem_resetGlobalsToDefaults();
    if (copy_len > 0u) {
        memcpy(parameter_values + PAR_BEGINNING_OF_GLOBALS, src, copy_len);
    }

    parameter_values[PAR_EXT_SYNC] = SEQ_EXT_SYNC_AUTO;
    parameter_values[PAR_BAR_RESET_MODE] = 0u;
    parameter_values[PAR_MIDI_CHAN_GLOBAL] = 1u;
    parameter_values[PAR_OSC_WAVE_INTERP] = 1u;
    filesystem_sanitizeLoadedGlobals();
}

static void filesystem_resetSettingsToDefaults(void)
{
    /*
     * Reset only the persistent global settings owned by settings.cfg.
     *
     * Inputs: none. Outputs: the agreed global-menu fields receive safe
     * defaults before a settings file overlays any present keys. Scene-owned
     * Morph, voice morph, decimation, kit, and future effects values are not
     * touched here, because those now belong to Scene/Bank storage rather than
     * root machine settings.
     */
    parameter_values[PAR_BPM] = 120u;
    parameter_values[PAR_EXT_SYNC] = SEQ_EXT_SYNC_AUTO;
    parameter_values[PAR_QUANTISATION] = 0u;
    parameter_values[PAR_MIDI_CHAN_GLOBAL] = 1u;
    parameter_values[PAR_MIDI_FILT_TX] = 0u;
    parameter_values[PAR_MIDI_FILT_RX] = 0u;
    parameter_values[PAR_MIDI_ROUTING] = 0u;
    parameter_values[PAR_SCREENSAVER_ON_OFF] = 0u;
    parameter_values[PAR_BAR_RESET_MODE] = 0u;
    parameter_values[PAR_PRESCALER_CLOCK_IN] = 0u;
    parameter_values[PAR_PRESCALER_CLOCK_OUT1] = 0u;
    parameter_values[PAR_FOLLOW] = 0u;
    parameter_values[PAR_OSC_WAVE_INTERP] = 0u;
    bank_setRestoreBankSlot(0u);
}

static const char *filesystem_trimSettingsText(const char *text)
{
    while (*text == ' ' || *text == '\t')
        text++;
    return text;
}

static uint8_t filesystem_parseSettingsU16(const char *text, uint16_t *out)
{
    uint32_t value = 0u;
    uint8_t digits = 0u;

    /*
     * Parse decimal settings values without accepting partial text.
     *
     * Inputs: value text from `key=value`. Output: *out receives 0..65535 on
     * success. settings.cfg keeps active_bank decimal because it is a slot
     * number, while bit masks live in bankset.bcg and use the storageTypes hex
     * parser instead.
     */
    if (!text || !out)
        return 0u;
    text = filesystem_trimSettingsText(text);
    while (*text >= '0' && *text <= '9') {
        value = (value * 10u) + (uint8_t)(*text - '0');
        if (value > 65535u)
            return 0u;
        text++;
        digits++;
    }
    if (digits == 0u || *filesystem_trimSettingsText(text) != '\0')
        return 0u;
    *out = (uint16_t)value;
    return 1u;
}

static uint8_t filesystem_settingsParamForKey(const char *key,
                                              uint16_t *param)
{
    /*
     * Map settings.cfg keys to the small persistent global allowlist.
     *
     * Inputs: parsed key text. Output: nonzero plus a ParameterArray id when
     * the key is one of the current global-menu settings. The explicit table is
     * the guard that keeps Scene parameters such as Morph or voice morph from
     * leaking back into global persistence.
     */
    if (strcmp(key, "bpm") == 0) *param = PAR_BPM;
    else if (strcmp(key, "ext_sync") == 0) *param = PAR_EXT_SYNC;
    else if (strcmp(key, "quantisation") == 0) *param = PAR_QUANTISATION;
    else if (strcmp(key, "midi_chan_global") == 0) *param = PAR_MIDI_CHAN_GLOBAL;
    else if (strcmp(key, "midi_filt_tx") == 0) *param = PAR_MIDI_FILT_TX;
    else if (strcmp(key, "midi_filt_rx") == 0) *param = PAR_MIDI_FILT_RX;
    else if (strcmp(key, "midi_routing") == 0) *param = PAR_MIDI_ROUTING;
    else if (strcmp(key, "screensaver_on_off") == 0) *param = PAR_SCREENSAVER_ON_OFF;
    else if (strcmp(key, "bar_reset_mode") == 0) *param = PAR_BAR_RESET_MODE;
    else if (strcmp(key, "prescaler_clock_in") == 0) *param = PAR_PRESCALER_CLOCK_IN;
    else if (strcmp(key, "prescaler_clock_out1") == 0) *param = PAR_PRESCALER_CLOCK_OUT1;
    else if (strcmp(key, "follow") == 0) *param = PAR_FOLLOW;
    else if (strcmp(key, "osc_wave_interp") == 0) *param = PAR_OSC_WAVE_INTERP;
    else return 0u;
    return 1u;
}

static fs_status_t filesystem_parseSettingsLine(const char *line)
{
    char key[32];
    const char *eq;
    const char *value;
    uint8_t len = 0u;
    uint16_t parsed;
    uint16_t param;

    /*
     * Parse and apply one settings.cfg assignment.
     *
     * Inputs: one NUL-terminated text line read by filesystem_readTextLine().
     * Outputs: global parameter_values[] fields and BankData's restore slot
     * update immediately as lines arrive. Unknown keys are ignored so future
     * settings can be appended without breaking older firmware, but format and
     * version keys are strict guards for accidental non-settings files.
     */
    if (!line)
        return FS_STATUS_ERROR;
    line = filesystem_trimSettingsText(line);
    if (*line == '\0' || *line == '#')
        return FS_STATUS_DONE;
    eq = line;
    while (*eq != '\0' && *eq != '=')
        eq++;
    if (*eq != '=')
        return FS_STATUS_ERROR;
    while (line < eq && len < (uint8_t)(sizeof(key) - 1u)) {
        key[len++] = *line++;
    }
    key[len] = '\0';
    while (len > 0u && (key[len - 1u] == ' ' || key[len - 1u] == '\t'))
        key[--len] = '\0';
    value = filesystem_trimSettingsText(eq + 1u);
    if (strcmp(key, "format") == 0)
        return (strcmp(value, "helicase.settings") == 0)
            ? FS_STATUS_DONE
            : FS_STATUS_ERROR;
    if (strcmp(key, "version") == 0) {
        if (!filesystem_parseSettingsU16(value, &parsed) || parsed != 1u)
            return FS_STATUS_ERROR;
        return FS_STATUS_DONE;
    }
    if (strcmp(key, "active_bank") == 0) {
        if (!filesystem_parseSettingsU16(value, &parsed))
            return FS_STATUS_ERROR;
        bank_setRestoreBankSlot(parsed);
        return FS_STATUS_DONE;
    }
    if (filesystem_settingsParamForKey(key, &param)) {
        if (!filesystem_parseSettingsU16(value, &parsed) || parsed > 255u)
            return FS_STATUS_ERROR;
        parameter_values[param] = (uint8_t)parsed;
    }
    filesystem_sanitizeLoadedGlobals();
    return FS_STATUS_DONE;
}

static uint8_t filesystem_detectUnsupportedCardLayout(void)
{
    uint8_t sector0[FS_SECTOR_SIZE_BYTES];
    uint32_t numClusters = 0;
    uint32_t fatPartitionLba = 0;

    if (SD_readSingleBlockCustomBuffer(0, sector0) != 0u)
        return 0;

    if (filesystem_sectorLooksLikeExFat(sector0))
        return 1;

    if (filesystem_hasFatBootSignature(sector0)) {
        const mbrPartitionEntry_t *partition = (const mbrPartitionEntry_t *)(sector0 + 446);

        for (uint8_t i = 0; i < 4u; i++) {
            uint8_t type = partition[i].type;
            if (type == MBR_PARTITION_TYPE_EXFAT)
                return 1;

            if (fatPartitionLba == 0u &&
                partition[i].lbaBegin > 0u &&
                (type == MBR_PARTITION_TYPE_FAT32 ||
                 type == MBR_PARTITION_TYPE_FAT32_LBA ||
                 type == MBR_PARTITION_TYPE_FAT16 ||
                 type == MBR_PARTITION_TYPE_FAT16_LBA)) {
                fatPartitionLba = partition[i].lbaBegin;
            }
        }
    }

    if (fatPartitionLba > 0u) {
        uint8_t volumeSector[FS_SECTOR_SIZE_BYTES];
        if (SD_readSingleBlockCustomBuffer(fatPartitionLba, volumeSector) == 0u &&
            filesystem_isPlausibleFatVolume(volumeSector, &numClusters) &&
            numClusters <= FAT12_MAX_CLUSTERS) {
            return 1;
        }
        return 0;
    }

    if (filesystem_isPlausibleFatVolume(sector0, &numClusters) &&
        numClusters <= FAT12_MAX_CLUSTERS) {
        return 1;
    }

    return 0;
}

static bool filesystem_makeFilename(char *buf, fs_file_type_t type, uint16_t num)
{
    const fs_file_desc_t *desc = filesystem_desc(type);
    uint8_t i;

    if (desc == NULL)
        return false;

    if (!desc->numbered) {
        if (desc->literal_name == NULL)
            return false;
        for (i = 0; desc->literal_name[i] && i < 12u; i++)
            buf[i] = desc->literal_name[i];
        buf[i] = '\0';
        return true;
    }

    buf[0] = 'p';
    buf[1] = (char)('0' + ((num / 100u) % 10u));
    buf[2] = (char)('0' + ((num / 10u) % 10u));
    buf[3] = (char)('0' + (num % 10u));
    for (i = 0; desc->extension[i] && i < 4u; i++)
        buf[4u + i] = desc->extension[i];
    buf[4u + i] = '\0';
    return true;
}

/* Pattern files are large enough that they must be streamed. These helpers
 * define the on-card byte order explicitly:
 *   name[8]
 *   Step[track-major pattern-major step-major], each Step as 7 bytes
 *   main steps[pattern-major track-major], little-endian uint16_t
 *   pattern settings[pattern], nextPattern then changeBar
 *   track length bytes[pattern-major track-major], optional for old files
 *   track settings extension[pattern-major track-major], optional append-only:
 *     rotate, scale, midiChannel, midiNote
 *   track shuffle extension[pattern-major track-major], optional append-only:
 *     shuffle
 *
 * Phase 2 storage is intentionally not back-compatible with every intermediate
 * bridge experiment. The old single shuffle byte is ignored and no longer
 * written; external Python converters will own migration once the final storage
 * shape settles.
 */
#define FS_PATTERN_FILE_PATTERN_COUNT 1u
#define FS_PATTERN_STEP_COUNT     ((uint32_t)NUM_TRACKS * FS_PATTERN_FILE_PATTERN_COUNT * NUM_STEPS)
#define FS_PATTERN_MAIN_COUNT     ((uint32_t)FS_PATTERN_FILE_PATTERN_COUNT * NUM_TRACKS)
#define FS_PATTERN_SETTINGS_COUNT ((uint32_t)FS_PATTERN_FILE_PATTERN_COUNT)
#define FS_PATTERN_LENGTH_COUNT   ((uint32_t)FS_PATTERN_FILE_PATTERN_COUNT * NUM_TRACKS)
#define FS_PATTERN_STEP_SIZE      9u
#define FS_PATTERN_MAIN_SIZE      2u
#define FS_PATTERN_SETTING_SIZE   2u
#define FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE 2u
#define FS_PATTERN_TRACK_SHUFFLE_SIZE 1u
/*
 * Container version stays at 2 while Phase 2 storage is still provisional.
 * Inputs/clients: saveContainer writes the current bridge payload and optional
 * per-track extensions; loadContainer treats EOF before optional extensions as
 * a valid partial bridge file. Output: runtime code ignores the removed legacy
 * single shuffle byte instead of preserving transitional compatibility.
 */
#define FS_CONTAINER_VERSION      2u

static void filesystem_patternStepAddress(uint32_t step_index,
                                          uint8_t *pattern,
                                          uint8_t *track,
                                          uint8_t *step)
{
    uint32_t abs_pat = step_index / NUM_STEPS;

    *track = (uint8_t)(abs_pat / FS_PATTERN_FILE_PATTERN_COUNT);
    *pattern = (uint8_t)(abs_pat - ((uint32_t)*track * FS_PATTERN_FILE_PATTERN_COUNT));
    *step = (uint8_t)(step_index - (abs_pat * NUM_STEPS));
}

static void filesystem_patternTrackAddress(uint32_t index,
                                           uint8_t *pattern,
                                           uint8_t *track)
{
    *pattern = (uint8_t)(index / NUM_TRACKS);
    *track = (uint8_t)(index - ((uint32_t)*pattern * NUM_TRACKS));
}


/* Legacy pattern-file discard/blank records for bridge slots 1..7.
 *
 * The Phase 2 bridge has one live pattern (slot 0) but still streams the old
 * eight-slot file layout. Save paths read these zeroed records for slots 1..7;
 * load paths write into them so old files can be consumed without creating live
 * pattern slots that Phase 3 will delete. */
static Step filesystem_discardStep;
static uint16_t filesystem_discardMainSteps;
static PatternSetting filesystem_discardPatternSetting;
static LengthRotate filesystem_discardLengthRotate = {
    NUM_STEPS, 0u, TRACK_SCALE_OFF, 0u
};

static uint8_t __attribute__((unused)) filesystem_defaultTrackMidiChannel(
    uint8_t track)
{
    /*
     * Pattern files can omit the track-settings extension. In that case the
     * loader supplies a valid PatternData-owned channel in menu form so old
     * files cannot leave zero in a 1..16 channel field.
     */
    return (uint8_t)((track < 15u) ? (track + 1u) : 1u);
}

static void filesystem_defaultTrackSettings(LengthRotate *lr, uint8_t track)
{
    /*
     * Default all fields that were not present in the legacy one-byte length
     * stream. The following optional extensions may overwrite rotate, scale,
     * midiChannel, midiNote, and shuffle for new pattern/container saves.
     * Legacy single-shuffle storage is intentionally ignored in Phase 2, so a
     * missing per-track shuffle extension means shuffle defaults to off.
     */
    if (!lr)
        return;
    lr->rotate = 0u;
    lr->scale = TRACK_SCALE_OFF;
    lr->shuffle = 0u;
    (void)track;
}
static Step *filesystem_patternStepPtr(uint8_t pattern, uint8_t track, uint8_t step)
{
    /*
     * Returns the PatternData Step record used by pattern save/load streaming.
     *
     * Why this exists: filesystem serializes pattern files one logical record at
     * a time and should not know the internal array names. PatternData owns
     * storage, so filesystem reaches it through pat_stepPtr().
     *
     * Running-load rule: if the loaded pattern is the currently playing
     * seq_activePattern, writes go to PATTERNDATA_STAGING_PATTERN. Sequencer
     * later commits that staging buffer at a safe pattern boundary.
     *
     * Inputs: pattern/track/step coordinates from the file stream. Output:
     * mutable Step pointer or NULL when coordinates are invalid.
     *
     * Risk: save paths also use this helper. During an active-pattern load,
     * reading that same pattern would read staging data by design, so callers
     * must not overlap save/load operations.
     */
    if (pattern != 0u)
        return &filesystem_discardStep;
    return pat_stepPtr(scene_getActiveIndex(), track, step);
}

static uint16_t *filesystem_patternMainPtr(uint8_t pattern, uint8_t track)
{
    /*
     * Returns the PatternData main-step bitfield for pattern serialization.
     *
     * Main steps are Pattern-owned track data. The staging rule mirrors
     * filesystem_patternStepPtr() so active-pattern loads do not modify the
     * currently sounding pattern until Sequencer commits them.
     */
    if (pattern != 0u)
        return &filesystem_discardMainSteps;
    return pat_mainStepsPtr(scene_getActiveIndex(), track);
}

static PatternSetting *filesystem_patternSettingPtr(uint8_t pattern)
{
    /*
     * Returns PatternData's per-pattern settings record for serialization.
     *
     * Pattern settings include nextPattern/changeBar. They belong with Pattern
     * data rather than Sequencer globals because they are saved with .pat files.
     * Active-pattern loads use the staging pattern for boundary-safe commit.
     */
    if (pattern != 0u)
        return &filesystem_discardPatternSetting;
    return pat_patternSettingPtr(scene_getActiveIndex());
}

static LengthRotate *filesystem_patternLengthPtr(uint8_t pattern, uint8_t track)
{
    /*
     * Returns PatternData's per-pattern/per-track length/rotation record.
     *
     * Length/rotation moved under PatternData during FrontPanelParser removal.
     * Filesystem streams those bytes directly from the owner instead of asking
     * Sequencer/frontPanelParser for encoded values.
     */
    if (pattern != 0u)
        return &filesystem_discardLengthRotate;
    return pat_lengthRotatePtr(scene_getActiveIndex(), track);
}

static Step *filesystem_patternSetStepPtr(PatternSet *pattern_set,
                                          uint8_t pattern,
                                          uint8_t track,
                                          uint8_t step)
{
    /*
     * Borrow one Step from a staged Scene PatternSet.
     *
     * Scene Load must not write through live PatternData while it is still
     * validating sibling files. Inputs are the bridge file coordinates; output
     * is a mutable staged Step for pattern 0 or the discard record for legacy
     * non-live patterns. The bounds mirror PatternData's current one-pattern
     * bridge shape.
     */
    if (pattern != 0u)
        return &filesystem_discardStep;
    if (!pattern_set || track >= NUM_TRACKS || step >= NUM_STEPS)
        return NULL;
    return &pattern_set->pat_subStepPattern[track][step];
}

static uint16_t *filesystem_patternSetMainPtr(PatternSet *pattern_set,
                                              uint8_t pattern,
                                              uint8_t track)
{
    if (pattern != 0u)
        return &filesystem_discardMainSteps;
    if (!pattern_set || track >= NUM_TRACKS)
        return NULL;
    return &pattern_set->pat_mainSteps[track];
}

static PatternSetting *filesystem_patternSetSettingPtr(PatternSet *pattern_set,
                                                       uint8_t pattern)
{
    if (pattern != 0u)
        return &filesystem_discardPatternSetting;
    return pattern_set ? &pattern_set->pat_patternSettings : NULL;
}

static LengthRotate *filesystem_patternSetLengthPtr(PatternSet *pattern_set,
                                                    uint8_t pattern,
                                                    uint8_t track)
{
    if (pattern != 0u)
        return &filesystem_discardLengthRotate;
    if (!pattern_set || track >= NUM_TRACKS)
        return NULL;
    return &pattern_set->pat_patternLengthRotate[track];
}

static void filesystem_packStep(const Step *step, uint8_t *buf)
{
    buf[0] = step->volume;
    buf[1] = step->prob;
    buf[2] = step->note;
    buf[3] = (uint8_t)(step->param1Nr & 0xffu);
    buf[4] = (uint8_t)(step->param1Nr >> 8);
    buf[5] = step->param1Val;
    buf[6] = (uint8_t)(step->param2Nr & 0xffu);
    buf[7] = (uint8_t)(step->param2Nr >> 8);
    buf[8] = step->param2Val;
}

static void filesystem_unpackStep(Step *step, const uint8_t *buf)
{
    instrument_param_id_t param;

    /*
     * Unpack one stored Step and normalize legacy automation destinations.
     *
     * Inputs: on-card Step bytes. Outputs: Step fields are restored, but
     * automation destinations outside the legacy automationNode 1..254 range
     * collapse to NO_AUTOMATION. Why this must exist: current Step storage uses
     * a uint16_t field, and old/default/off values may be 0xffff; playback's
     * legacy automation bridge indexes a 255-entry MIDI CC history table and
     * must never receive those wide sentinels.
     */
    step->volume = buf[0];
    step->prob = buf[1];
    step->note = buf[2];
    param = (instrument_param_id_t)buf[3] |
            ((instrument_param_id_t)buf[4] << 8);
    step->param1Nr = (param > 0u && param < NO_AUTOMATION)
        ? param
        : NO_AUTOMATION;
    step->param1Val = buf[5];
    param = (instrument_param_id_t)buf[6] |
            ((instrument_param_id_t)buf[7] << 8);
    step->param2Nr = (param > 0u && param < NO_AUTOMATION)
        ? param
        : NO_AUTOMATION;
    step->param2Val = buf[8];
}

static uint32_t filesystem_writeStreamChunk(const uint8_t *buf, uint8_t len)
{
    uint32_t n = afatfs_fwrite(op_file, buf + op_item_offset,
                               (uint32_t)(len - op_item_offset));
    op_item_offset = (uint8_t)(op_item_offset + n);
    op_bytes_done += n;
    return n;
}

static uint32_t filesystem_readStreamChunk(uint8_t *buf, uint8_t len)
{
    uint32_t n = afatfs_fread(op_file, buf + op_item_offset,
                              (uint32_t)(len - op_item_offset));
    op_item_offset = (uint8_t)(op_item_offset + n);
    op_bytes_done += n;
    return n;
}

static const char *filesystem_errorPrefix(fs_internal_op_t op)
{
    switch (op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:          return "Flush";
    case FS_INTERNAL_OP_LOAD_KIT:              return "KitL";
    case FS_INTERNAL_OP_LOAD_KIT_MORPH:        return "KMrL";
    case FS_INTERNAL_OP_LOAD_SCENE:            return "ScnL";
    case FS_INTERNAL_OP_LOAD_BANK:             return "BnkL";
    case FS_INTERNAL_OP_SAVE_KIT:              return "KitS";
    case FS_INTERNAL_OP_SAVE_SCENE:            return "ScnS";
    case FS_INTERNAL_OP_SAVE_BANK:             return "BnkS";
    case FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH: return "IMrS";
    case FS_INTERNAL_OP_LOAD_MORPH:            return "MrpL";
    case FS_INTERNAL_OP_LOAD_PATTERN:          return "PatL";
    case FS_INTERNAL_OP_SAVE_PATTERN:          return "PatS";
    case FS_INTERNAL_OP_LOAD_ALL:              return "AllL";
    case FS_INTERNAL_OP_SAVE_ALL:              return "AllS";
    case FS_INTERNAL_OP_LOAD_PERFORMANCE:      return "PrfL";
    case FS_INTERNAL_OP_SAVE_PERFORMANCE:      return "PrfS";
    case FS_INTERNAL_OP_LOAD_GLOBALS:          return "GloL";
    case FS_INTERNAL_OP_SAVE_GLOBALS:          return "GloS";
    case FS_INTERNAL_OP_SCAN_KITS:             return "KitSc";
    case FS_INTERNAL_OP_SCAN_SCENES:           return "ScnSc";
    case FS_INTERNAL_OP_SCAN_BANKS:            return "BnkSc";
    case FS_INTERNAL_OP_SCAN_BANK_SCENES:      return "BScn";
    case FS_INTERNAL_OP_SCAN_INSTRUMENTS:      return "InsSc";
    case FS_INTERNAL_OP_LOAD_INSTRUMENT:       return "InsL";
    case FS_INTERNAL_OP_SAVE_INSTRUMENT:       return "InsS";
    case FS_INTERNAL_OP_LOAD_NAME:             return "NameL";
    case FS_INTERNAL_OP_SCAN_TEST_FILES:       return "TFiSc";
    case FS_INTERNAL_OP_SCAN_TEST_DIRS:        return "TDiSc";
    case FS_INTERNAL_OP_LOAD_TEST_FILE:        return "TFiL";
    case FS_INTERNAL_OP_LOAD_TEST_DIR:         return "TDiL";
    case FS_INTERNAL_OP_SAVE_TEST_FILE:        return "TFiS";
    case FS_INTERNAL_OP_SAVE_TEST_DIR:         return "TDiS";
    case FS_INTERNAL_OP_SAVE_TEST_SIMPLE_DIR:  return "TSdS";
    default:                                   return "Fs";
    }
}

static void filesystem_makeAutoErrorCode(fs_internal_op_t failed_op,
                                         uint8_t failed_phase)
{
    static const char hex[] = "0123456789ABCDEF";
    const char *prefix = filesystem_errorPrefix(failed_op);
    uint8_t i = 0u;

    while (i < 6u && prefix[i] != '\0') {
        fs_error_code[i] = prefix[i];
        i++;
    }
    fs_error_code[i++] = hex[(failed_phase >> 4) & 0x0fu];
    fs_error_code[i++] = hex[failed_phase & 0x0fu];
    fs_error_code[i] = '\0';
}

static void filesystem_makeNamedErrorCode(const char *prefix,
                                          uint8_t failed_phase)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t i = 0u;

    while (i < 6u && prefix && prefix[i] != '\0') {
        fs_error_code[i] = prefix[i];
        i++;
    }
    fs_error_code[i++] = hex[(failed_phase >> 4) & 0x0fu];
    fs_error_code[i++] = hex[failed_phase & 0x0fu];
    fs_error_code[i] = '\0';
}

static void filesystem_complete(fs_status_t final_status)
{
    if (final_status == FS_STATUS_ERROR && fs_error_code[0] == '\0')
        filesystem_makeAutoErrorCode(current_op, op_phase);
    status = final_status;
    current_op = FS_INTERNAL_OP_NONE;
    if (completion_callback) {
        fs_completion_cb_t cb = completion_callback;
        completion_callback = NULL;
        cb();
    }
}

static void filesystem_finish(fs_status_t final_status)
{
    uint8_t flush_before_complete = (uint8_t)(final_status == FS_STATUS_DONE);

    if (flush_before_complete) {
        /*
         * Do not publish a successful filesystem operation until asyncfatfs has
         * persisted every dirty sector the operation left behind.
         *
         * Inputs: final_status is the state-machine result after all files have
         * been closed. Output: status remains FS_STATUS_BUSY and the normal
         * completion callback is deferred. This prevents the Save UI from
         * resetting before the directory entry, FAT allocation, and data
         * sectors for a saved object are visible on a freshly-mounted SD card.
         */
        op_flush_final_status = final_status;
        current_op = FS_INTERNAL_OP_FLUSH_FINISH;
        op_phase = 0u;
        return;
    }

    filesystem_complete(final_status);
}

static void filesystem_flushFinish_tick(void)
{
    /*
     * Final save/load persistence gate.
     *
     * filesystem_tick() has already called afatfs_poll() for this foreground
     * pass. Calling afatfs_sync() here either starts/continues one bounded
     * cache write or confirms that no dirty sector and no in-flight sector
     * write remains. Only then is completion_callback invoked, so Preset/Menu
     * cannot display save completion while the removable card still lacks the
     * sectors that make the saved object discoverable.
     */
    if (!afatfs_sync())
        return;

    filesystem_complete(op_flush_final_status);
}

/* Copy display text into an exact eight-character preset/LCD field.
 *
 * Inputs: dst points to an eight-byte field, src is NUL-terminated text.
 * Output: dst receives printable ASCII padded with spaces; it is not
 * NUL-terminated. Clients: filesystem_setLoadedNameFromKitSlot() and invalid/
 * empty sentinel helpers.
 */
static void filesystem_copyEightCharName(char dst[8], const char *src)
{
    for (uint8_t i = 0u; i < 8u; i++) {
        char c = src[i];
        if (c == '\0')
            c = ' ';
        dst[i] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
    }
}

/* Fill loaded_name from the Kit/ scan cache for a load-name request.
 *
 * Input: zero-based slot. Output: loaded_name becomes the cached kit name, or
 * "Empty   " when the slot is absent. Client: filesystem_loadName_tick() for
 * FS_FILE_KIT, replacing the old behavior of opening Pxxx.SND headers.
 */
static void filesystem_setLoadedNameFromKitSlot(uint16_t slot)
{
    if (slot < STORAGE_KIT_MAX_SLOTS && kit_slot_present[slot]) {
        filesystem_copyEightCharName(loaded_name, kit_slot_name[slot]);
    } else {
        filesystem_copyEightCharName(loaded_name, "Empty   ");
    }
    loaded_name[8] = '\0';
}

/* Mark the active preset name as an intentionally empty kit slot.
 *
 * Input: none. Output: preset_currentName is "Empty   ". Clients: kit directory
 * load failures caused by a missing folder or missing Kit/ root. menu.c uses
 * this sentinel to leave the load page gracefully.
 */
static void filesystem_setPresetNameEmpty(void)
{
    memcpy(preset_currentName, "Empty   ", 8);
}

/* Mark the active preset name as a malformed/invalid storage object.
 *
 * Input: none. Output: preset_currentName becomes '-' followed by spaces.
 * Clients: kitset/instrument parse failures and impossible loader states, so
 * the display can distinguish "bad file" from "empty numbered slot."
 */
static void filesystem_setPresetNameInvalid(void)
{
    preset_currentName[0] = '-';
    memset(preset_currentName + 1, ' ', 7);
}

/* Clear the in-progress Kit/ long-filename accumulator.
 *
 * Input/output: resets op_lfn_name and op_lfn_valid. Client:
 * filesystem_scanKits_tick() between FAT directory entries so LFN fragments
 * from one entry cannot be applied to the next.
 */
static void filesystem_dirLfnReset(void)
{
    memset(op_lfn_name, 0, sizeof(op_lfn_name));
    op_lfn_valid = 0u;
}

/* Read one UTF-16 code unit from a FAT LFN directory entry.
 *
 * Inputs: raw 32-byte LFN entry and character index 0..12. Output: *out gets
 * the code unit; return is always nonzero for compatibility with the sample
 * LFN helper shape. Client: filesystem_dirLfnAppendEntry().
 */
static uint8_t filesystem_dirLfnCharAt(const uint8_t *entry, uint8_t index,
                                       uint16_t *out)
{
    static const uint8_t offsets[13] = {
        1u, 3u, 5u, 7u, 9u,
        14u, 16u, 18u, 20u, 22u, 24u,
        28u, 30u
    };
    uint8_t o = offsets[index];

    *out = (uint16_t)entry[o] | ((uint16_t)entry[o + 1u] << 8);
    return 1u;
}

/* Append one FAT LFN fragment while scanning Kit/.
 *
 * Input: directory entry whose attribute byte identifies it as an LFN entry.
 * Output: op_lfn_name/op_lfn_valid accumulate an ASCII-safe display name.
 * Non-ASCII code points are replaced with '_' because the kit folder parser and
 * LCD name fields are ASCII-only. Client: filesystem_scanKits_tick().
 */
static void filesystem_dirLfnAppendEntry(const fatDirectoryEntry_t *entry)
{
    const uint8_t *raw = (const uint8_t *)entry;
    uint8_t seq = raw[0] & 0x1fu;

    if (seq == 0u || seq > 6u) {
        filesystem_dirLfnReset();
        return;
    }

    if (raw[0] & 0x40u) {
        memset(op_lfn_name, 0, sizeof(op_lfn_name));
        op_lfn_valid = 1u;
    } else if (!op_lfn_valid) {
        return;
    }

    uint8_t pos = (uint8_t)((seq - 1u) * 13u);
    for (uint8_t i = 0u; i < 13u && pos < (FS_KIT_LFN_MAX - 1u); i++, pos++) {
        uint16_t ch;

        (void)filesystem_dirLfnCharAt(raw, i, &ch);
        if (ch == 0x0000u)
            break;
        if (ch == 0xffffu)
            continue;
        op_lfn_name[pos] = (ch < 0x80u) ? (char)ch : '_';
    }
}

static uint8_t filesystem_displayPrecedesCached(const char *candidate,
                                                const char *cached)
{
    /*
     * Keep the earliest display variant by product order.
     *
     * Slot number is the product identity, so externally-created duplicate
     * directories for the same `NNN` slot must not appear as separate products.
     * The casefold-first/raw-case tiebreak chooses a deterministic
     * representative with capital letters before lowercase, while later
     * duplicate directories are ignored until recursive delete exists.
     */
    return (uint8_t)(fat_compareDisplayNameCasefoldThenCase(candidate,
                                                            cached) < 0);
}

static void filesystem_noteKitBrowserSlot(uint16_t slot)
{
    uint16_t i;

    /*
     * Add one Kit slot to the legacy kitBrowser bridge at most once.
     *
     * The product scan cache is now slot-addressed, but kitBrowser still uses
     * a compact map of present slots. Duplicate same-slot folders must not
     * create duplicate browser rows while we are choosing a canonical cache
     * representative.
     */
    for (i = 0u; i < kb_numKits; i++) {
        if (kb_map[i] == slot)
            return;
    }
    if (kb_numKits < KITBROWSER_MAX_KITS)
        kb_map[kb_numKits++] = slot;
}

/* Record a kit directory when FAT only gives us the generated short alias.
 *
 * Why this exists: folder names with spaces depend on FAT long-filename entries
 * for their visible text. Some cards/formatters can still expose a short alias
 * such as 001SLA~1 to this scanner. That alias has the correct three-digit
 * slot prefix but no space/underscore separator, so the normal visible-name
 * parser must reject it. This fallback keeps the slot loadable by deriving the
 * slot from the first three digits and using the alias tail as a best-effort
 * display name. Kit names are owned by the numbered folder, not kitset.kcg.
 *
 * Inputs: open_name is the 8.3 alias used by afatfs_fopen(). Outputs: the scan
 * cache and kitBrowser compatibility map are populated if the alias starts with
 * a valid 000..999 slot number and has some non-empty tail. Slot 000 is a real
 * Kit slot, so the parsed number maps directly to the cache index. Client:
 * filesystem_recordKitDirectory() after normal LFN parsing fails.
 */
static void filesystem_recordKitShortAlias(const char *open_name)
{
    uint16_t number;
    uint16_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    if (open_name[0] < '0' || open_name[0] > '9' ||
        open_name[1] < '0' || open_name[1] > '9' ||
        open_name[2] < '0' || open_name[2] > '9') {
        return;
    }

    number = (uint16_t)((uint16_t)(open_name[0] - '0') * 100u +
                        (uint16_t)(open_name[1] - '0') * 10u +
                        (uint16_t)(open_name[2] - '0'));
    if (number >= STORAGE_KIT_MAX_SLOTS || open_name[3] == '\0') {
        return;
    }

    slot = number;
    storage_copyDisplayName(display, open_name + 3u);
    display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    if (kit_slot_present[slot] &&
        !filesystem_displayPrecedesCached(display, kit_slot_name[slot])) {
        return;
    }
    kit_slot_present[slot] = 1u;
    memcpy(kit_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    kit_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(kit_slot_open_name[slot], open_name);
    filesystem_noteKitBrowserSlot(slot);
}

/* Record one numbered kit directory discovered during a Kit/ scan.
 *
 * Inputs: display_name is the LFN or short name visible to the user; open_name
 * is the FAT short name that afatfs_fopen() can open later. Outputs: the new
 * scan cache is updated, and kb_map/kb_numKits are compatibility-populated for
 * existing kitBrowser clients. Invalid visible names outside the
 * NNN Name/NNN_Name format are ignored unless open_name is a FAT short alias
 * that begins with a valid three-digit slot prefix.
 */
static void filesystem_recordKitDirectory(const char *display_name,
                                          const char *open_name)
{
    uint16_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    if (!storage_parseNumberedFolder(display_name, &slot, display)) {
        filesystem_recordKitShortAlias(open_name);
        return;
    }
    /*
     * Terminate the fixed-width display scratch before duplicate ordering.
     *
     * What: storage_parseNumberedFolder() fills exactly eight display cells,
     * matching LCD and SceneData storage. filesystem_displayPrecedesCached()
     * compares C strings, so the local scratch gets a ninth byte here before
     * it participates in casefold/case-preserving sort order.
     *
     * Why: duplicate same-slot directories are unusual external-card damage,
     * but when they exist the product must hide every later casefold-equivalent
     * entry and show only the capital-first winner. That comparison must never
     * read past the eight display cells returned by storageTypes.
     *
     * Inputs: parsed folder display component and existing slot cache entry.
     * Outputs: safe transient C string; the stored cache remains exactly eight
     * display cells plus its own terminator.
     *
     * Affiliates/clients: filesystem_recordKitShortAlias(),
     * filesystem_recordSceneDirectory(), fat_compareDisplayNameCasefoldThenCase().
     */
    display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';

    if (kit_slot_present[slot] &&
        !filesystem_displayPrecedesCached(display, kit_slot_name[slot])) {
        return;
    }
    kit_slot_present[slot] = 1u;
    memcpy(kit_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    kit_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(kit_slot_open_name[slot], open_name);
    filesystem_noteKitBrowserSlot(slot);
}

static void filesystem_recordSavedKitDirectory(const char *display_name,
                                               const char *open_name)
{
    uint16_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Save is authoritative for its target slot.
     *
     * The scan-cache duplicate rule intentionally keeps the earliest
     * casefold/case-preserving name when the SD card already contains multiple
     * physical directories for one numbered slot. That rule is wrong after a
     * successful Save:[Kit]: the just-written directory is the only intended
     * resident slot identity and must replace any stale cached name.
     */
    if (!storage_parseNumberedFolder(display_name, &slot, display) ||
        slot >= STORAGE_KIT_MAX_SLOTS) {
        return;
    }
    display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    kit_slot_present[slot] = 1u;
    memcpy(kit_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    kit_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(kit_slot_open_name[slot], open_name);
    filesystem_noteKitBrowserSlot(slot);
}

static void filesystem_recordSceneShortAlias(const char *open_name)
{
    uint16_t number;
    uint16_t slot;
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

    /*
     * FAT short-alias fallback for Scene folders.
     *
     * Inputs: an asyncfatfs-openable 8.3 alias such as 001SLA~1. Output:
     * Scene scan cache entry when the alias begins with a valid 000..999
     * prefix. Slot 000 is real, so the parsed number maps directly to the
     * cache index. This mirrors the Kit fallback but deliberately avoids
     * kb_map because Scene and Kit library occupancy are independent.
     */
    if (open_name[0] < '0' || open_name[0] > '9' ||
        open_name[1] < '0' || open_name[1] > '9' ||
        open_name[2] < '0' || open_name[2] > '9') {
        return;
    }
    number = (uint16_t)((uint16_t)(open_name[0] - '0') * 100u +
                        (uint16_t)(open_name[1] - '0') * 10u +
                        (uint16_t)(open_name[2] - '0'));
    if (number >= STORAGE_SCENE_MAX_SLOTS || open_name[3] == '\0') {
        return;
    }
    slot = number;
    storage_copyDisplayName(display, open_name + 3u);
    display[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    if (scene_slot_present[slot] &&
        !filesystem_displayPrecedesCached(display, scene_slot_name[slot])) {
        return;
    }
    scene_slot_present[slot] = 1u;
    memcpy(scene_slot_name[slot], display, STORAGE_SCENE_DISPLAY_NAME_LEN);
    scene_slot_name[slot][STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(scene_slot_open_name[slot], open_name);
}

static void filesystem_recordSceneDirectory(const char *display_name,
                                            const char *open_name)
{
    uint16_t slot;
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

    /*
     * Record one root Scene/ numbered folder.
     *
     * Inputs: display_name from LFN when available and open_name as the FAT
     * alias. Output: Scene cache only. The display must come from actual FAT
     * directory data, not from a future sceneset.scg value, so the UI never
     * presents a filename register that lies about what is on the card.
     */
    if (!storage_parseNumberedFolder(display_name, &slot, display)) {
        filesystem_recordSceneShortAlias(open_name);
        return;
    }
    if (slot >= STORAGE_SCENE_MAX_SLOTS)
        return;
    /*
     * Terminate the fixed-width Scene display scratch before duplicate sorting.
     *
     * What: Converts the eight-byte storageTypes display field into a transient
     * C string solely for the capital-first duplicate-selection comparison.
     *
     * Why: Scene folders share the same case-insensitive/case-preserving
     * product rule as Kit folders. External duplicate Scene slot names must be
     * ignored after the first sorted winner without allowing the comparator to
     * scan beyond the parsed display buffer.
     *
     * Inputs: parsed Scene folder display text and existing Scene cache entry.
     * Outputs: safe transient C string; the Scene cache write below preserves
     * the fixed eight display cells used by the LCD.
     *
     * Affiliates/clients: filesystem_recordKitDirectory() and Scene library
     * load browsing.
     */
    display[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    if (scene_slot_present[slot] &&
        !filesystem_displayPrecedesCached(display, scene_slot_name[slot])) {
        return;
    }
    scene_slot_present[slot] = 1u;
    memcpy(scene_slot_name[slot], display, STORAGE_SCENE_DISPLAY_NAME_LEN);
    scene_slot_name[slot][STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(scene_slot_open_name[slot], open_name);
}

static void filesystem_recordSavedSceneDirectory(const char *display_name,
                                                 const char *open_name)
{
    uint16_t slot;
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

    /*
     * Save is authoritative for one root Scene slot.
     *
     * Inputs: the display folder name just written and asyncfatfs' returned
     * short open alias. Outputs: Scene browser cache for that slot is replaced
     * with the successful save identity. This mirrors Kit Save's cache update
     * but deliberately avoids kitBrowser because Scene slots are independent.
     */
    if (!storage_parseNumberedFolder(display_name, &slot, display) ||
        slot >= STORAGE_SCENE_MAX_SLOTS) {
        return;
    }
    display[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    scene_slot_present[slot] = 1u;
    memcpy(scene_slot_name[slot], display, STORAGE_SCENE_DISPLAY_NAME_LEN);
    scene_slot_name[slot][STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(scene_slot_open_name[slot], open_name);
}

static void filesystem_recordBankDirectory(const char *display_name,
                                           const char *open_name)
{
    uint16_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Record one root Bank/ numbered folder.
     *
     * Inputs: FAT display component and the component to use for later
     * afatfs_opendir_lfn() calls. Output: the root Bank browser cache only.
     * Bank-local Scene children are intentionally not cached here because
     * their two-digit namespace belongs to one selected Bank folder, not the
     * root library.
     *
     * Important: the open component must be the display component for LFN-aware
     * opens. A host-created folder such as `000 Slak` may have a generated SFN
     * alias, but afatfs_opendir_lfn() resolves read-only opens by comparing the
     * public display name. Caching the alias lists the Bank but fails at Bank
     * load phase 6.
     */
    if (!storage_parseNumberedFolder(display_name, &slot, display) ||
        slot >= STORAGE_BANK_MAX_SLOTS) {
        return;
    }
    display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    if (bank_slot_present[slot] &&
        !filesystem_displayPrecedesCached(display, bank_slot_name[slot])) {
        return;
    }
    bank_slot_present[slot] = 1u;
    memcpy(bank_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    bank_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(bank_slot_open_name[slot], open_name);
}

static void filesystem_recordSavedBankDirectory(const char *display_name,
                                                const char *open_name)
{
    uint16_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Save is authoritative for one root Bank slot.
     *
     * Inputs: the Bank folder that was successfully created/opened. Output:
     * root Bank scan cache for that slot. Names still come only from the
     * directory; bankset.bcg is never queried for identity. The open cache
     * intentionally keeps display_name, not the returned SFN alias, so the next
     * Bank Load uses the same LFN-aware matching path as root Bank scan.
     */
    (void)open_name;
    if (!storage_parseNumberedFolder(display_name, &slot, display) ||
        slot >= STORAGE_BANK_MAX_SLOTS) {
        return;
    }
    display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    bank_slot_present[slot] = 1u;
    memcpy(bank_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    bank_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(bank_slot_open_name[slot], display_name);
}

static int8_t filesystem_compareInstrumentDisplayName(const char *a,
                                                       const char *b)
{
    /*
     * Sort Instrument rows by product display order.
     *
     * Product identity is case-insensitive, but externally-created duplicate
     * case variants need a deterministic representative. The shared FAT helper
     * compares casefolded text first and raw ASCII second, so uppercase wins
     * before lowercase for the same folded character.
     */
    return fat_compareDisplayNameCasefoldThenCase(a, b);
}

static instrument_type_t filesystem_instrumentTypeFromFilename(
    const char *filename)
{
    uint8_t i;

    /*
     * Classify one Instrument/ file from its extension.
     *
     * Input: FAT display or short filename. Output: registered instrument type
     * or UNKNOWN. InstrumentManager owns extension matching, so filesystem does
     * not duplicate `.drm`/`.snr`/`.cym`/`.hat` knowledge.
     */
    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);
        if (entry &&
            instrumentManager_filenameMatchesType(filename, entry->type)) {
            return entry->type;
        }
    }
    return INSTRUMENT_TYPE_UNKNOWN;
}

static void filesystem_copyInstrumentStemDisplay(char dst[9],
                                                 const char *filename)
{
    char stem[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
    uint8_t i = 0u;

    /*
     * Build the Instrument Load display name from a filename stem.
     *
     * Input: LFN or short filename. Output: first eight printable stem
     * characters, padded and NUL-terminated. The extension is not displayed
     * because the top row already shows the selected instrument type.
     */
    while (i < STORAGE_KIT_DISPLAY_NAME_LEN &&
           filename[i] != '\0' &&
           filename[i] != '.') {
        stem[i] = filename[i];
        i++;
    }
    stem[i] = '\0';
    storage_copyDisplayName(dst, stem);
    dst[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
}

static void filesystem_copyInstrumentStem16(
    char dst[SCENE_INSTRUMENT_STEM_LEN + 1u],
    const char *filename)
{
    uint8_t i = 0u;

    /*
     * Retain the longer source stem used by later Kit Save.
     *
     * Input is the LFN display name when available, falling back to the short
     * open name. Output is the first 16 stem characters before the extension,
     * NUL-terminated, and paired with the eight-character Instrument browser
     * display cached beside it.
     */
    memset(dst, 0, SCENE_INSTRUMENT_STEM_LEN + 1u);
    while (filename &&
           filename[i] != '\0' &&
           filename[i] != '.' &&
           i < SCENE_INSTRUMENT_STEM_LEN) {
        char c = filename[i];
        dst[i] = (c >= 32 && c <= 126) ? c : '_';
        i++;
    }
    if (i == 0u)
        memcpy(dst, "inst", 5u);
}

static uint8_t filesystem_instrumentCacheStemMatches(
        instrument_type_t type,
        uint8_t index,
        const char *display_stem)
{
    /*
     * Test whether one cached Instrument row is the same product object.
     *
     * Inputs: type/index in the Instrument browser cache plus an eight-character
     * display stem. Output: nonzero when the cached row has the same folded
     * stem. Instrument type is part of identity because `.drm` and `.snr`
     * files with the same stem are different products.
     */
    return (uint8_t)(
        type < INSTRUMENT_TYPE_UNKNOWN &&
        index < instrument_file_count[type] &&
        fat_compareDisplayName(instrument_file_name[type][index],
                               display_stem,
                               false) == 0);
}

static void filesystem_recordInstrumentFile(const char *display_name,
                                            const char *open_name)
{
    instrument_type_t type =
        filesystem_instrumentTypeFromFilename(display_name);
    uint8_t count;
    uint8_t pos;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Insert one Instrument/ file into its per-type sorted cache.
     *
     * Inputs: display_name from asyncfatfs object metadata and open_name as the
     * asyncfatfs-openable short filename. Output: the per-type cache stores a
     * display stem and open name in alphanumeric order. Classification prefers
     * the visible filename first, because a host-created long file may have a
     * generated short alias that is less meaningful than its display extension;
     * the alias fallback keeps legacy alias-only media loadable.
     */
    if (type == INSTRUMENT_TYPE_UNKNOWN)
        type = filesystem_instrumentTypeFromFilename(open_name);
    if (type == INSTRUMENT_TYPE_UNKNOWN ||
        type >= INSTRUMENT_TYPE_UNKNOWN)
        return;
    filesystem_copyInstrumentStemDisplay(display, display_name);
    /*
     * Suppress same-casefold Instrument browser duplicates.
     *
     * What: Before inserting a scanned Instrument/ object, compare it against
     * existing cached rows for the same instrument type. If the display stem
     * matches case-insensitively, keep only the filename that sorts first by
     * folded text and raw ASCII case.
     *
     * Why: Externally edited FAT cards may contain names that differ only by
     * case. Product policy treats those as one object; later variants must not
     * appear in the Load UI even though asyncfatfs reports every physical FAT
     * object.
     *
     * Inputs: display_name and open_name come from afatfs_findNextObject().
     * Outputs: the per-type cache either keeps its existing representative,
     * replaces it with an earlier-sorting variant, or inserts a new product
     * object.
     *
     * Affiliates/clients: filesystem_requestScanInstruments(), nested
     * Instrument Load, root Instrument Save cache update,
     * fat_compareDisplayNameCasefoldThenCase().
     */
    for (pos = 0u; pos < instrument_file_count[type]; pos++) {
        if (!filesystem_instrumentCacheStemMatches(type, pos, display))
            continue;
        if (filesystem_compareInstrumentDisplayName(
                display,
                instrument_file_name[type][pos]) < 0) {
            memcpy(instrument_file_name[type][pos], display,
                   sizeof(instrument_file_name[type][pos]));
            storage_copyFilename(instrument_file_open_name[type][pos],
                                 open_name);
            filesystem_copyInstrumentStem16(instrument_file_stem[type][pos],
                                            display_name);
        }
        return;
    }

    count = instrument_file_count[type];
    if (count >= FS_INSTRUMENT_MAX_PER_TYPE)
        return;

    pos = count;
    while (pos > 0u &&
           filesystem_compareInstrumentDisplayName(
               instrument_file_name[type][pos - 1u], display) > 0) {
        memcpy(instrument_file_name[type][pos],
               instrument_file_name[type][pos - 1u],
               sizeof(instrument_file_name[type][pos]));
        memcpy(instrument_file_open_name[type][pos],
               instrument_file_open_name[type][pos - 1u],
               sizeof(instrument_file_open_name[type][pos]));
        memcpy(instrument_file_stem[type][pos],
               instrument_file_stem[type][pos - 1u],
               sizeof(instrument_file_stem[type][pos]));
        pos--;
    }
    memcpy(instrument_file_name[type][pos], display,
           sizeof(instrument_file_name[type][pos]));
    storage_copyFilename(instrument_file_open_name[type][pos], open_name);
    filesystem_copyInstrumentStem16(instrument_file_stem[type][pos],
                                    display_name);
    instrument_file_count[type] = (uint8_t)(count + 1u);
}

static void filesystem_updateInstrumentCacheAfterSave(const char *display_name,
                                                      const char *open_name)
{
    instrument_type_t type =
        filesystem_instrumentTypeFromFilename(display_name);
    uint8_t i;

    /*
     * Refresh browser cache after case-insensitive overwrite.
     *
     * What: Removes every cached row whose filename alias or display stem
     * matches the saved target under case-insensitive comparison, then inserts
     * the one returned by the completed save.
     *
     * Why: The SD card has already collapsed same-casefold physical files into
     * one visible object. The in-RAM browser cache must mirror that immediately
     * so the next nested load cannot select a stale duplicate alias.
     *
     * Inputs: display_name is the target case just written; open_name is the
     * short alias returned by asyncfatfs. Outputs: per-type Instrument cache
     * contains one row for the saved object.
     *
     * Affiliates/clients: filesystem_saveInstrument_tick(),
     * filesystem_recordInstrumentFile(), nested Instrument Load.
     */
    if (type == INSTRUMENT_TYPE_UNKNOWN)
        type = filesystem_instrumentTypeFromFilename(open_name);
    if (type == INSTRUMENT_TYPE_UNKNOWN || type >= INSTRUMENT_TYPE_UNKNOWN)
        return;

    for (i = 0u; i < instrument_file_count[type]; ) {
        char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
        uint8_t remove = 0u;

        filesystem_copyInstrumentStemDisplay(display, display_name);
        if (fat_compareDisplayName(instrument_file_open_name[type][i],
                                   open_name,
                                   false) == 0 ||
            fat_compareDisplayName(instrument_file_name[type][i],
                                   display,
                                   false) == 0) {
            remove = 1u;
        }

        if (remove) {
            uint8_t j;
            for (j = i; (uint8_t)(j + 1u) < instrument_file_count[type]; j++) {
                memcpy(instrument_file_name[type][j],
                       instrument_file_name[type][j + 1u],
                       sizeof(instrument_file_name[type][j]));
                memcpy(instrument_file_open_name[type][j],
                       instrument_file_open_name[type][j + 1u],
                       sizeof(instrument_file_open_name[type][j]));
                memcpy(instrument_file_stem[type][j],
                       instrument_file_stem[type][j + 1u],
                       sizeof(instrument_file_stem[type][j]));
            }
            instrument_file_count[type]--;
            continue;
        }
        i++;
    }

    filesystem_recordInstrumentFile(display_name, open_name);
}

/* Read one text line from an asyncfatfs file without blocking the pump.
 *
 * Inputs: open file handle, caller-owned line buffer, current length pointer,
 * capacity, and result flags. Outputs: line_ready with a NUL-terminated line,
 * eof when no more data remains, WAIT when the per-tick byte budget is spent,
 * or LINE_TOO_LONG for malformed text. Clients: the directory kit loader for
 * kitset.kcg and instrument files.
 */
static storage_status_t filesystem_readTextLine(afatfsFilePtr_t file,
                                                char *buf,
                                                uint8_t *len,
                                                uint8_t cap,
                                                uint8_t *line_ready,
                                                uint8_t *eof)
{
    uint8_t budget = 16u;

    *line_ready = 0u;
    *eof = 0u;

    while (budget-- > 0u) {
        uint8_t c;
        uint32_t n = afatfs_fread(file, &c, 1u);

        if (n == 0u) {
            if (afatfs_feof(file)) {
                if (*len > 0u) {
                    buf[*len] = '\0';
                    *len = 0u;
                    *line_ready = 1u;
                } else {
                    *eof = 1u;
                }
            }
            return STORAGE_STATUS_OK;
        }

        if (c == '\r')
            continue;
        if (c == '\n') {
            buf[*len] = '\0';
            *len = 0u;
            *line_ready = 1u;
            return STORAGE_STATUS_OK;
        }
        if (*len >= (uint8_t)(cap - 1u))
            return STORAGE_STATUS_LINE_TOO_LONG;
        buf[*len] = (char)c;
        *len = (uint8_t)(*len + 1u);
    }

    return STORAGE_STATUS_WAIT;
}

/* -----------------------------------------------------------------------
** LEGACY .SND LOAD state machine
**
** Why this still exists: normal kit loading now uses Kit/NNN Name directories,
** but morph loading is still backed by the original Pxxx.SND payload until the
** save/morph format pass moves morph data into instrument files. Inputs are
** op_file_type/op_slot/current_op set by filesystem_start(). Outputs are
** preset_currentName plus either parameter_values[] for legacy kit callers or
** parameters2[] when current_op is FS_INTERNAL_OP_LOAD_MORPH.
**
** Affiliates/clients: filesystem_tick() dispatches FS_INTERNAL_OP_LOAD_MORPH
** here; preset_loadDrumset(..., morph=1) is the menu-facing client. Normal
** FS_INTERNAL_OP_LOAD_KIT deliberately dispatches to
** filesystem_loadKitDirectory_tick() instead.
**
** Phases: 0=open, 1=wait_open, 2=read_name, 3=read_params, 4=close,
**         5=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_loadKit_tick(void)
{
    uint8_t *dest = (current_op == FS_INTERNAL_OP_LOAD_MORPH) ? parameters2 : parameter_values;

    switch (op_phase) {
    case 0: /* OPEN */
    {
        char fname[13];
        if (!filesystem_makeFilename(fname, op_file_type, op_slot)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(fname, "r", on_file_opened)) {
            /* File pool full - retry next tick */
            return;
        }
        op_phase = 1;
        return;
    }

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            /* File not found */
            memcpy(preset_currentName, "Empty   ", 8);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2;
        op_bytes_done = 0;
        return;

    case 2: /* READ_NAME */
    {
        uint32_t n = afatfs_fread(op_file,
                                  (uint8_t *)preset_currentName + op_bytes_done,
                                  8 - op_bytes_done);
        op_bytes_done += n;
        if (op_bytes_done >= 8) {
            /* Full name read — sanitize and proceed to param read. */
            uint8_t i;
            for (i = 0; i < 8; i++)
                if (preset_currentName[i] < 0x20 || preset_currentName[i] > 0x7E)
                    preset_currentName[i] = ' ';
            op_phase = 3;
            op_bytes_done = 0;
        } else if (n == 0 && afatfs_feof(op_file)) {
            /* EOF before 8 bytes — file is malformed; close and report error.
            ** Do not proceed to param read: parameter_values[] must not be
            ** clobbered with zeroes from an invalid file. */
            preset_currentName[0] = '-';
            memset(preset_currentName + 1, ' ', 7);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 4; /* jump straight to CLOSE */
        }
        return;
    }

    case 3: /* READ_PARAMS - write directly into active or morph buffer */
    {
        uint32_t remaining = END_OF_SOUND_PARAMETERS - op_bytes_done;
        uint32_t n = afatfs_fread(op_file,
                                  dest + op_bytes_done,
                                  remaining);
        op_bytes_done += n;
        if (op_bytes_done >= END_OF_SOUND_PARAMETERS
            || (n == 0 && afatfs_feof(op_file))) {
            if (op_bytes_done < END_OF_SOUND_PARAMETERS) {
                memset(dest + op_bytes_done, 0,
                       END_OF_SOUND_PARAMETERS - op_bytes_done);
            }
            op_phase = 4;
        }
        return;
    }

    case 4: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed)) {
            op_phase = 5;
        }
        /* If fclose returns false (busy), retry next tick */
        return;

    case 5: /* WAIT_CLOSE */
        if (!op_close_done) return;
        /* Data is in parameter_values[] or parameters2[]. Post-load work
        ** happens in the menu layer when it sees UPDATE_READY. */
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD DIRECTORY KIT state machine
**
** Why this exists: Phase 2 replaces root-level Pxxx.SND kit loads with
** Kit/NNN Name/kitset.kcg plus six instrument files. The loader validates the
** selected scan-cache slot, enters Kit/, enters the selected kit folder by its
** cached FAT short name, copies the display name from the folder scan, parses
** kitset.kcg, then opens each listed instrument file in order.
**
** Inputs: op_slot is the zero-based internal kit number; kit_slot_present and
** kit_slot_open_name must have been populated by filesystem_requestScanKits();
** storageTypes owns the text schema and ParameterArray maps. Outputs:
** op_staged_kit receives routing, endpoints, and source-name metadata until
** all files validate; then selected Scene kits receive that complete payload.
** preset_currentName receives the kit display name, "Empty   ", or "-       ".
**
** Affiliates/clients: preset_loadKitForScenes() requests
** FS_INTERNAL_OP_LOAD_KIT; filesystem_tick() dispatches that request here.
** menu.c initiates those requests from the Load page. asyncfatfs current
** directory is restored to root before finishing.
** ----------------------------------------------------------------------- */
static void filesystem_loadKitDirectory_tick(void)
{
    uint8_t line_ready;
    uint8_t eof;
    storage_status_t st;

    switch (op_phase) {
    case 0: /* VALIDATE CACHE + CHDIR ROOT */
        if (op_slot >= STORAGE_KIT_MAX_SLOTS || !kit_slot_present[op_slot]) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        memcpy(preset_currentName, kit_slot_name[op_slot], 8);
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Kit/ */
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_opendir_lfn(STORAGE_ROOT_KIT,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT Kit/ */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR Kit/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4;
        return;

    case 4: /* CLOSE Kit/ handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT CLOSE Kit/ */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 6;
        return;

    case 6: /* OPEN selected kit directory */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(kit_slot_open_name[op_slot], "r", on_file_opened))
            return;
        op_phase = 7;
        return;

    case 7: /* WAIT selected kit directory */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_setPresetNameEmpty();
            filesystem_makeNamedErrorCode("KDir", op_phase);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 28;
            return;
        }
        op_kit_slot_dir = op_file;
        op_phase = 8;
        return;

    case 8: /* CHDIR selected kit directory */
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 9;
        return;

    case 9: /* CLOSE selected kit directory handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed)) {
            /*
             * Case 9 only starts the async close. Case 10 must wait for
             * on_file_closed() to set op_close_done; staying in case 9 repeats
             * the close request forever and stalls the boot kit load loop.
             */
            op_phase = 10;
        }
        return;

    case 10: /* WAIT CLOSE selected kit directory */
        if (!op_close_done) return;
        op_kit_slot_dir = NULL;
        op_phase = 11;
        return;

    case 11: /* OPEN kitset.kcg */
        storage_kitsetInit(&op_kitset);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_KITSET_FILENAME, "r", on_file_opened))
            return;
        op_phase = 12;
        return;

    case 12: /* WAIT kitset.kcg */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_setPresetNameInvalid();
            filesystem_makeNamedErrorCode("KSet", op_phase);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 28;
            return;
        }
        op_phase = 13;
        return;

    case 13: /* READ kitset.kcg */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            filesystem_makeNamedErrorCode("KSet", op_phase);
            op_close_status = FS_STATUS_ERROR;
            /*
             * A read error can occur after kitset.kcg is open. Route through
             * case 14 so afatfs_fclose() is actually requested; case 15 only
             * waits for a close callback and would hang if entered directly.
             */
            op_phase = 14;
            return;
        }
        if (line_ready) {
            st = storage_kitsetParseLine(&op_kitset, op_line_buf,
                                          &op_staged_kit);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                filesystem_makeNamedErrorCode("KSet", op_phase);
                op_close_status = FS_STATUS_ERROR;
                /*
                 * Malformed kitset data still owns an open file handle. Close
                 * first, then let the wait-close phase surface FS_STATUS_ERROR.
                 */
                op_phase = 14;
            }
            return;
        }
        if (eof) {
            st = storage_kitsetFinalize(&op_kitset);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                filesystem_makeNamedErrorCode("KSet", op_phase);
                op_close_status = FS_STATUS_ERROR;
            } else {
                op_close_status = FS_STATUS_DONE;
            }
            op_phase = 14;
        }
        return;

    case 14: /* CLOSE kitset.kcg */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15;
        return;

    case 15: /* WAIT CLOSE kitset.kcg */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 28;
            return;
        }
        op_instrument_slot = 0u;
        op_phase = 16;
        return;

    case 16: /* PREPARE NEXT INSTRUMENT */
        if (op_instrument_slot >= STORAGE_KIT_SLOT_COUNT) {
            uint8_t scene_index;

            /*
             * Commit only normal Kit Load from filesystem.
             *
             * Inputs: op_staged_kit and the request-time Scene mask. Output:
             * normal Kit Load replaces selected resident Scene kits only after
             * all files have passed validation. KitMrp deliberately skips this
             * assignment and leaves op_staged_kit available through
             * filesystem_loadedKit(), because Preset must preserve destination
             * slot identity and copy only same-type morphable endpoint values.
             */
            if (current_op == FS_INTERNAL_OP_LOAD_KIT) {
                for (scene_index = 0u;
                     scene_index < SCENE_COUNT && scene_index < 16u;
                     scene_index++) {
                    if ((op_kit_load_scene_mask &
                         (uint16_t)(1u << scene_index)) != 0u) {
                        scene_t *target_scene = scene_get(scene_index);
                        if (target_scene) {
                            target_scene->kit = op_staged_kit;
                            /*
                             * Update retained Kit identity only for normal Kit
                             * Load.
                             *
                             * What: Copies the selected Kit folder display name
                             * into each destination resident Kit after the
                             * entire directory validates and commits.
                             *
                             * Why: KitMrp uses the same file parser but copies
                             * endpoint values into morph images only. Morph
                             * operations must not rename the resident Kit or
                             * its member Instruments.
                             *
                             * Inputs: the scan-cache name for op_slot and the
                             * destination Scene selected by the Load page.
                             * Outputs: target_scene->kit.display_name mirrors
                             * the on-card Kit folder identity now resident in
                             * RAM.
                             *
                             * Affiliates/clients: filesystem_loadKitDirectory_tick(),
                             * Preset KitMrp completion, Save editor seeding.
                             */
                            scene_setKitDisplayName(&target_scene->kit,
                                                    kit_slot_name[op_slot]);
                        }
                    }
                }
                memcpy(preset_currentName, kit_slot_name[op_slot], 8);
            }
            op_close_status = FS_STATUS_DONE;
            op_phase = 28;
            return;
        }
        storage_instrumentStateInit(&op_instrument_state,
                                    op_kitset.instrument_type[op_instrument_slot],
                                    (uint8_t)(op_instrument_slot + 1u));
        instrumentManager_resetSlot(
            &op_staged_kit.instruments[op_instrument_slot],
            op_kitset.instrument_type[op_instrument_slot]);
        op_line_len = 0u;
        op_phase = 17;
        return;

    case 17: /* OPEN INSTRUMENT */
        op_file_ready = false;
        op_file = NULL;
        /*
         * Open Kit member files by the kitset-visible display component.
         *
         * What: Resolves the `file=` value from kitset.kcg through the
         * LFN-aware open path instead of treating it as a short alias.
         *
         * Why: Kit member filenames carry the convention that stem character
         * eight is the one-based voice number. kitset.kcg now stores that
         * visible filename, while older 8.3 kitsets still load because
         * afatfs_fopen_lfn() also matches SFN display names.
         *
         * Inputs: op_kitset.instrument_file[op_instrument_slot] from the
         * parsed kitset. Output: op_file receives the matching Instrument file
         * handle in the selected Kit directory.
         *
         * Affiliates/clients: storage_kitsetParseLine() and
         * storage_makeSavedInstrumentDisplayFilename().
         */
        if (!afatfs_fopen_lfn(op_kitset.instrument_file[op_instrument_slot],
                              "r",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 18;
        return;

    case 18: /* WAIT INSTRUMENT */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_setPresetNameInvalid();
            filesystem_makeNamedErrorCode("KIns", op_phase);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 28;
            return;
        }
        op_phase = 19;
        return;

    case 19: /* READ INSTRUMENT */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            filesystem_makeNamedErrorCode("KIns", op_phase);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 20;
            return;
        }
        if (line_ready) {
            st = storage_instrumentParseLine(&op_instrument_state,
                                             op_line_buf,
                                             &op_staged_kit.instruments[
                                                 op_instrument_slot]);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                filesystem_makeNamedErrorCode("KIns", op_phase);
                op_close_status = FS_STATUS_ERROR;
                op_phase = 20;
            }
            return;
        }
        if (eof) {
            st = storage_instrumentFinalize(&op_instrument_state);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                filesystem_makeNamedErrorCode("KIns", op_phase);
                op_close_status = FS_STATUS_ERROR;
            } else {
                if (op_instrument_state.seen_morph_count == 0u) {
                    storage_instrumentCopyMainToMorphFallback(
                        op_kitset.instrument_type[op_instrument_slot],
                        (uint8_t)(op_instrument_slot + 1u),
                        &op_staged_kit.instruments[op_instrument_slot]);
                }
                op_close_status = FS_STATUS_DONE;
            }
            op_phase = 20;
        }
        return;

    case 20: /* CLOSE INSTRUMENT */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 21;
        return;

    case 21: /* WAIT CLOSE INSTRUMENT */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 28;
            return;
        }
        op_instrument_slot++;
        op_phase = 16;
        return;

    case 28: /* RETURN TO ROOT + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_setPresetNameInvalid();
        op_close_status = FS_STATUS_ERROR;
        op_phase = 28;
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD SCENE DIRECTORY state machine
**
** Scene folders are validated as a unit before resident SceneData changes.
** The loader enters Scene/<NNN Name>/ from the root Scene scan cache, discovers
** child filenames from actual FAT entries, parses sceneset.scg into
** op_staged_scene.settings, parses the first Kit* directory into
** op_staged_scene.kit, parses the first .pat bridge file into
** op_staged_scene.pattern, validates the first .fx placeholder, then copies
** the finished staged Scene to every destination bit in
** op_scene_load_scene_mask.
**
** Inputs: op_slot and op_scene_load_scene_mask are set by
** filesystem_requestLoadSceneForScenes(). Outputs: selected resident Scenes
** receive a full Scene image only after all required files validate. The
** displayed Scene name comes from the Scene folder scan/sceneset, while the
** embedded Kit name comes only from the "Kit <name>" child directory.
** ----------------------------------------------------------------------- */
static void filesystem_loadSceneDirectory_tick(void)
{
    uint8_t line_ready;
    uint8_t eof;
    storage_status_t st;

    switch (op_phase) {
    case 0: /* VALIDATE CACHE + INIT STAGING + CHDIR ROOT */
        if (op_slot >= STORAGE_SCENE_MAX_SLOTS ||
            !scene_slot_present[op_slot]) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        filesystem_initStagedScene(&op_staged_scene);
        memcpy(preset_currentName, scene_slot_name[op_slot], 8u);
        memcpy(op_scene_display_name, scene_slot_name[op_slot],
               STORAGE_SCENE_DISPLAY_NAME_LEN);
        op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
        memset(op_scene_child_open_name, 0, sizeof(op_scene_child_open_name));
        memset(op_scene_child_display_name, 0,
               sizeof(op_scene_child_display_name));
        memset(op_scene_pattern_open_name, 0, sizeof(op_scene_pattern_open_name));
        memset(op_scene_effect_open_name, 0, sizeof(op_scene_effect_open_name));
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN root Scene/ */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_ROOT_SCENE, "r", on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT root Scene/ */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR root Scene/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4;
        return;

    case 4: /* CLOSE root Scene/ handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT root Scene/ close */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 6;
        return;

    case 6: /* OPEN selected Scene directory */
        op_file_ready = false;
        op_file = NULL;
        filesystem_makeNumberedDir(op_root_open_name,
                                   op_slot,
                                   scene_slot_name[op_slot]);
        /*
         * Open root Scene folders by the visible numbered component.
         *
         * Inputs: op_slot plus scene_slot_name[] from the Scene browser cache.
         * Output: afatfs_opendir_lfn() resolves the same `NNN Name` component
         * that the user selected, and returns a directory handle for phase 8.
         *
         * Why: Scene Load is the source for Bank Save's resident Scene images.
         * Opening by cached 8.3 alias is fragile when the card has duplicate or
         * host-created names, and hardware retest showed a Scene 005 load could
         * leave the destination slot saving stale Slak data instead of the
         * selected `005 Slak2/Kit Forest` payload. LFN display open keeps root
         * Scene Load aligned with the hardened Bank child discovery path.
         */
        if (!afatfs_opendir_lfn(op_root_open_name,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                scene_slot_open_name[op_slot],
                                on_file_opened))
            return;
        op_phase = 7;
        return;

    case 7: /* WAIT selected Scene directory */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameEmpty();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_kit_slot_dir = op_file;
        op_phase = 8;
        return;

    case 8: /* CHDIR selected Scene + start child scan */
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        afatfs_findFirstObject(op_kit_slot_dir, &op_object_finder);
        op_phase = 9;
        return;

    case 9: /* SCAN first Kit*, .pat, and .fx child names */
    {
        afatfsOperationStatus_e ast =
            afatfs_findNextObject(op_kit_slot_dir,
                                  &op_object_finder,
                                  &op_object);
        if (ast == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (ast == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 10;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 10;
            return;
        }
        /*
         * Child discovery follows the user-editable Scene contract using the
         * LFN-aware object iterator.
         *
         * Inputs: op_object.id.displayName is checksum-verified VFAT text when
         * present, otherwise a case-preserved SFN display; op_object.id.shortName
         * is the asyncfatfs-openable alias. Outputs: the first `Kit <name>`
         * directory plus the first `.pat` and `.fx` files are staged for later
         * validation. This avoids the older raw-entry/LFN side state and keeps
         * root Scene Load behavior aligned with Bank-local Scene Load.
         */
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
            if (op_scene_child_open_name[0] == '\0' &&
                filesystem_nameStartsWithKitSpace(op_object.id.displayName)) {
                storage_copyFilename(op_scene_child_open_name,
                                     op_object.id.shortName);
                storage_copyDisplayName(op_scene_child_display_name,
                                        &op_object.id.displayName[4]);
            }
        } else if (op_object.id.kind == AFATFS_OBJECT_FILE) {
            if (op_scene_pattern_open_name[0] == '\0' &&
                filesystem_nameHasExtension(op_object.id.displayName, ".pat")) {
                storage_copyFilename(op_scene_pattern_open_name,
                                     op_object.id.shortName);
            } else if (op_scene_effect_open_name[0] == '\0' &&
                       filesystem_nameHasExtension(op_object.id.displayName,
                                                   ".fx")) {
                storage_copyFilename(op_scene_effect_open_name,
                                     op_object.id.shortName);
            }
        }
        return;
    }

    case 10: /* CLOSE selected Scene scan handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 11;
        return;

    case 11: /* WAIT selected Scene close */
        if (!op_close_done) return;
        op_kit_slot_dir = NULL;
        if (op_close_status != FS_STATUS_DONE ||
            op_scene_child_open_name[0] == '\0' ||
            op_scene_pattern_open_name[0] == '\0' ||
            op_scene_effect_open_name[0] == '\0') {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_phase = 12;
        return;

    case 12: /* OPEN sceneset.scg */
        storage_scenesetInit(&op_sceneset_state);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_SCENESET_FILENAME, "r", on_file_opened))
            return;
        op_phase = 13;
        return;

    case 13: /* WAIT sceneset.scg */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_phase = 14;
        return;

    case 14: /* READ sceneset.scg */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 15;
            return;
        }
        if (line_ready) {
            st = storage_scenesetParseLine(&op_sceneset_state,
                                           op_line_buf,
                                           &op_staged_scene,
                                           op_scene_display_name);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 15;
            }
            return;
        }
        if (eof) {
            st = storage_scenesetFinalize(&op_sceneset_state);
            op_close_status = (st == STORAGE_STATUS_OK)
                ? FS_STATUS_DONE
                : FS_STATUS_ERROR;
            if (st != STORAGE_STATUS_OK)
                filesystem_setPresetNameInvalid();
            op_phase = 15;
        }
        return;

    case 15: /* CLOSE sceneset.scg */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 16;
        return;

    case 16: /* WAIT sceneset.scg close */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 72;
            return;
        }
        op_phase = 17;
        return;

    case 17: /* OPEN embedded Kit directory */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(op_scene_child_open_name, "r", on_file_opened))
            return;
        op_phase = 18;
        return;

    case 18: /* WAIT embedded Kit directory */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_kit_slot_dir = op_file;
        op_phase = 19;
        return;

    case 19: /* CHDIR embedded Kit */
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 20;
        return;

    case 20: /* CLOSE embedded Kit handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 21;
        return;

    case 21: /* WAIT embedded Kit close */
        if (!op_close_done) return;
        op_kit_slot_dir = NULL;
        op_phase = 22;
        return;

    case 22: /* OPEN embedded kitset.kcg */
        storage_kitsetInit(&op_kitset);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_KITSET_FILENAME, "r", on_file_opened))
            return;
        op_phase = 23;
        return;

    case 23: /* WAIT embedded kitset.kcg */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_phase = 24;
        return;

    case 24: /* READ embedded kitset.kcg */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 25;
            return;
        }
        if (line_ready) {
            st = storage_kitsetParseLine(&op_kitset, op_line_buf,
                                          &op_staged_scene.kit);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 25;
            }
            return;
        }
        if (eof) {
            st = storage_kitsetFinalize(&op_kitset);
            if (st == STORAGE_STATUS_OK) {
                /*
                 * One-way compatibility bridge for old embedded Scene Kits.
                 *
                 * What: imports a complete six-value legacy kitset audio_out
                 * block into the staged Scene only when sceneset.scg did not
                 * already contain the new Scene-owned audio_out line.
                 *
                 * Why: early Scene fixtures stored mixer routing inside the
                 * embedded Kit. Root Kit Load must preserve current Scene
                 * routing, so this fallback intentionally lives only in Scene
                 * Load after both sceneset and embedded kitset parse state are
                 * available.
                 *
                 * Inputs: op_sceneset_state.seen_audio_out, the kitset parser's
                 * seen_audio_out_mask, and legacy_audio_out[] values. Output:
                 * op_staged_scene.settings.audio_out[] in persisted route
                 * domain 0..5. The per-slot loop clamps corrupt route bytes to
                 * the same defaults used by new-format scenes.
                 *
                 * Affiliates/clients: storage_kitsetHasCompleteLegacyAudioOut(),
                 * storage_kitsetLegacyAudioOut(), SceneData route accessors,
                 * and preset_applyKitAudioRouting().
                 */
                if (!op_sceneset_state.seen_audio_out &&
                    storage_kitsetHasCompleteLegacyAudioOut(&op_kitset)) {
                    const uint8_t *legacy =
                        storage_kitsetLegacyAudioOut(&op_kitset);
                    uint8_t slot;
                    for (slot = 0u;
                         slot < INSTRUMENT_SLOT_COUNT &&
                             slot < STORAGE_KIT_SLOT_COUNT;
                         slot++) {
                        uint8_t route = legacy[slot];
                        op_staged_scene.settings.audio_out[slot] =
                            (route <= 5u)
                                ? route
                                : filesystem_defaultVoiceAudioOut(slot);
                    }
                }
                /*
                 * Retain the embedded Kit directory name in staged SceneData.
                 *
                 * Inputs: op_scene_child_display_name was captured during the
                 * selected Scene directory scan, before sceneset.scg or
                 * kitset.kcg parsing. Output: op_staged_scene.kit.display_name
                 * is ready before the final resident Scene commit, so boot
                 * Scene Load and manual Scene Load both seed Save:[Kit] from
                 * the loaded "Kit <name>" directory.
                 */
                scene_setKitDisplayName(&op_staged_scene.kit,
                                        op_scene_child_display_name);
                op_close_status = FS_STATUS_DONE;
            } else {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
            }
            op_phase = 25;
        }
        return;

    case 25: /* CLOSE embedded kitset.kcg */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 26;
        return;

    case 26: /* WAIT embedded kitset.kcg close */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 72;
            return;
        }
        op_instrument_slot = 0u;
        op_phase = 27;
        return;

    case 27: /* PREPARE/OPEN next embedded instrument */
        if (op_instrument_slot >= STORAGE_KIT_SLOT_COUNT) {
            op_phase = 33;
            return;
        }
        storage_instrumentStateInit(&op_instrument_state,
                                    op_kitset.instrument_type[op_instrument_slot],
                                    (uint8_t)(op_instrument_slot + 1u));
        instrumentManager_resetSlot(
            &op_staged_scene.kit.instruments[op_instrument_slot],
            op_kitset.instrument_type[op_instrument_slot]);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        /*
         * Open embedded Scene Kit members through their visible kitset names.
         *
         * What: Uses the same LFN-aware member-file lookup as root Kit Load.
         *
         * Why: embedded Kits and root Kits share the Instrument-in-Kit naming
         * convention. The `file=` entry should point at the display filename
         * with the forced eighth-character voice number, not a collapsed short
         * alias.
         *
         * Inputs: parsed sceneset child Kit's kitset member filename. Output:
         * op_file receives the selected embedded Instrument file handle.
         *
         * Affiliates/clients: filesystem_loadKitDirectory_tick() and
         * storage_kitsetParseLine().
         */
        if (!afatfs_fopen_lfn(op_kitset.instrument_file[op_instrument_slot],
                              "r",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 28;
        return;

    case 28: /* WAIT embedded instrument */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_phase = 29;
        return;

    case 29: /* READ embedded instrument */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 30;
            return;
        }
        if (line_ready) {
            st = storage_instrumentParseLine(&op_instrument_state,
                                             op_line_buf,
                                             &op_staged_scene.kit.instruments[
                                                 op_instrument_slot]);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 30;
            }
            return;
        }
        if (eof) {
            st = storage_instrumentFinalize(&op_instrument_state);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
            } else {
                if (op_instrument_state.seen_morph_count == 0u) {
                    storage_instrumentCopyMainToMorphFallback(
                        op_kitset.instrument_type[op_instrument_slot],
                        (uint8_t)(op_instrument_slot + 1u),
                        &op_staged_scene.kit.instruments[op_instrument_slot]);
                }
                op_close_status = FS_STATUS_DONE;
            }
            op_phase = 30;
        }
        return;

    case 30: /* CLOSE embedded instrument */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 31;
        return;

    case 31: /* WAIT embedded instrument close */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 72;
            return;
        }
        op_instrument_slot++;
        op_phase = 27;
        return;

    case 33: /* REENTER selected Scene before pattern/effect files */
        if (current_op == FS_INTERNAL_OP_LOAD_BANK) {
            afatfsOperationStatus_e ast = afatfs_chdirParent();

            /*
             * Bank-local Scene payloads are already inside
             * `Bank/NNN Name/SS Scene/Kit Name/` after the embedded Kit and
             * Instrument files load.
             *
             * Inputs: current directory is the embedded Kit child. Output:
             * one parent step returns to the Bank-local Scene folder so the
             * already-scanned `pattern.pat` and `effects.fx` open relative to
             * `SS Scene/`. The root Scene path below must not run here,
             * because it would leave the Bank and reopen `/Scene/NNN`, which is
             * a different library namespace.
             */
            if (ast == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (ast == AFATFS_OPERATION_FAILURE) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 72;
                return;
            }
            op_phase = 44;
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 34;
        return;

    case 34: /* OPEN root Scene/ again */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_ROOT_SCENE, "r", on_file_opened))
            return;
        op_phase = 35;
        return;

    case 35: /* WAIT root Scene/ reopen */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 36;
        return;

    case 36: /* CHDIR root Scene/ again */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 37;
        return;

    case 37: /* CLOSE root Scene/ again */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 38;
        return;

    case 38: /* WAIT root Scene/ close again */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 39;
        return;

    case 39: /* OPEN selected Scene again */
        op_file_ready = false;
        op_file = NULL;
        filesystem_makeNumberedDir(op_root_open_name,
                                   op_slot,
                                   scene_slot_name[op_slot]);
        /*
         * Reopen the selected Scene by the same visible component used at
         * phase 6.
         *
         * Inputs: after embedded Kit parsing the current directory is back at
         * root `Scene/`; phase 39 needs the original selected Scene folder so
         * the pattern/effect children are opened from the same payload that
         * supplied sceneset.scg and Kit. Output: op_kit_slot_dir points at that
         * exact `NNN Name` directory again.
         */
        if (!afatfs_opendir_lfn(op_root_open_name,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                scene_slot_open_name[op_slot],
                                on_file_opened))
            return;
        op_phase = 40;
        return;

    case 40: /* WAIT selected Scene reopen */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_kit_slot_dir = op_file;
        op_phase = 41;
        return;

    case 41: /* CHDIR selected Scene again */
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 42;
        return;

    case 42: /* CLOSE selected Scene handle again */
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 43;
        return;

    case 43: /* WAIT selected Scene close again */
        if (!op_close_done) return;
        op_kit_slot_dir = NULL;
        op_phase = 44;
        return;

    case 44: /* OPEN bridge pattern */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(op_scene_pattern_open_name, "r", on_file_opened))
            return;
        op_phase = 45;
        return;

    case 45: /* WAIT bridge pattern */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_stream_index = 0u;
        op_item_offset = 0u;
        op_phase = 46;
        return;

    case 46: /* PROBE pattern file, then READ legacy binary name/header */
    {
        uint32_t n;

        /*
         * Scene patterns now have two accepted wire shapes.
         *
         * New Scene/Bank-local Scene Save writes text beginning with "format=".
         * Version 1 text is the old placeholder and leaves the initialized
         * PatternSet alone; version 2 carries the draft 128x7 active-step
         * bitmap plus per-track length/scale. Older fixtures and pattern files
         * are binary and begin with the legacy eight-byte pattern name/header.
         * The probe reads seven bytes first: if they match the text prefix,
         * those bytes become the already-buffered start of the first text line;
         * otherwise they remain bytes 0..6 of the binary header and the old
         * reader pulls byte 7 before continuing to the step stream.
         */
        if (op_item_offset < 7u) {
            n = filesystem_readStreamChunk(staging_buf, 7u);
            if (op_item_offset < 7u) {
                if (n == 0u && afatfs_feof(op_file)) {
                    filesystem_setPresetNameInvalid();
                    op_close_status = FS_STATUS_ERROR;
                    op_phase = 54;
                }
                return;
            }
        }
        if (op_item_offset == 7u &&
            staging_buf[0] == 'f' && staging_buf[1] == 'o' &&
            staging_buf[2] == 'r' && staging_buf[3] == 'm' &&
            staging_buf[4] == 'a' && staging_buf[5] == 't' &&
            staging_buf[6] == '=') {
            storage_patternStubStateInit(&op_pattern_stub_state);
            memcpy(op_line_buf, "format=", 7u);
            op_line_len = 7u;
            op_phase = 53;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, 8u);
        if (op_item_offset >= 8u) {
            op_item_offset = 0u;
            op_stream_index = 0u;
            op_phase = 47;
        } else if (n == 0u && afatfs_feof(op_file)) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 54;
        }
        return;
    }

    case 47: /* READ pattern steps */
    {
        uint8_t pattern, track, step_nr;
        Step *step;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_STEP_COUNT) {
            op_stream_index = 0u;
            op_item_offset = 0u;
            op_phase = 48;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_STEP_SIZE);
        if (op_item_offset >= FS_PATTERN_STEP_SIZE) {
            filesystem_patternStepAddress(op_stream_index, &pattern, &track,
                                          &step_nr);
            step = filesystem_patternSetStepPtr(&op_staged_scene.pattern,
                                                pattern, track, step_nr);
            if (!step) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
                return;
            }
            filesystem_unpackStep(step, staging_buf);
            op_item_offset = 0u;
            op_stream_index++;
        } else if (n == 0u && afatfs_feof(op_file)) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 54;
        }
        return;
    }

    case 48: /* READ pattern main steps */
    {
        uint8_t pattern, track;
        uint16_t *main_steps;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_MAIN_COUNT) {
            op_stream_index = 0u;
            op_item_offset = 0u;
            op_phase = 49;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_MAIN_SIZE);
        if (op_item_offset >= FS_PATTERN_MAIN_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            main_steps = filesystem_patternSetMainPtr(&op_staged_scene.pattern,
                                                      pattern, track);
            if (!main_steps) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
                return;
            }
            *main_steps = (uint16_t)staging_buf[0] |
                          ((uint16_t)staging_buf[1] << 8);
            op_item_offset = 0u;
            op_stream_index++;
        } else if (n == 0u && afatfs_feof(op_file)) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 54;
        }
        return;
    }

    case 49: /* READ pattern settings */
    {
        PatternSetting *setting;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_SETTINGS_COUNT) {
            op_stream_index = 0u;
            op_item_offset = 0u;
            op_phase = 50;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_SETTING_SIZE);
        if (op_item_offset >= FS_PATTERN_SETTING_SIZE) {
            setting = filesystem_patternSetSettingPtr(&op_staged_scene.pattern,
                                                      (uint8_t)op_stream_index);
            if (!setting) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
                return;
            }
            setting->nextPattern = staging_buf[0];
            setting->changeBar = staging_buf[1];
            op_item_offset = 0u;
            op_stream_index++;
        } else if (n == 0u && afatfs_feof(op_file)) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 54;
        }
        return;
    }

    case 50: /* READ pattern lengths */
    {
        uint8_t pattern, track;
        LengthRotate *lr;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0u;
            op_item_offset = 0u;
            op_phase = 51;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, 1u);
        if (op_item_offset >= 1u || (n == 0u && afatfs_feof(op_file))) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            lr = filesystem_patternSetLengthPtr(&op_staged_scene.pattern,
                                                pattern, track);
            if (!lr) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
                return;
            }
            lr->length = (op_item_offset >= 1u) ? staging_buf[0] : 0u;
            filesystem_defaultTrackSettings(lr, track);
            op_item_offset = 0u;
            op_stream_index++;
        }
        return;
    }

    case 51: /* READ optional rotate/scale extension */
    {
        uint8_t pattern, track;
        LengthRotate *lr;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0u;
            op_item_offset = 0u;
            op_phase = 52;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf,
                                       FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            lr = filesystem_patternSetLengthPtr(&op_staged_scene.pattern,
                                                pattern, track);
            if (!lr) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
                return;
            }
            lr->rotate = staging_buf[0];
            lr->scale = (staging_buf[1] < TRACK_SCALE_COUNT)
                ? staging_buf[1]
                : TRACK_SCALE_OFF;
            op_item_offset = 0u;
            op_stream_index++;
        } else if (n == 0u && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 54;
        }
        return;
    }

    case 52: /* READ optional shuffle extension */
    {
        uint8_t pattern, track;
        LengthRotate *lr;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 54;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_TRACK_SHUFFLE_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SHUFFLE_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            lr = filesystem_patternSetLengthPtr(&op_staged_scene.pattern,
                                                pattern, track);
            if (!lr) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
                return;
            }
            lr->shuffle = (staging_buf[0] <= 127u) ? staging_buf[0] : 0u;
            op_item_offset = 0u;
            op_stream_index++;
        } else if (n == 0u && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 54;
        }
        return;
    }

    case 53: /* READ text pattern placeholder/draft */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 54;
            return;
        }
        if (line_ready) {
            st = storage_patternStubParseLine(&op_pattern_stub_state,
                                              op_line_buf,
                                              &op_staged_scene.pattern);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 54;
            }
            return;
        }
        if (eof) {
            /*
             * Text pattern completion.
             *
             * Inputs: parser seen bits plus any v2 track rows already applied
             * into op_staged_scene.pattern. Output: success accepts either a
             * guarded v1 placeholder or a complete seven-track draft payload;
             * failure rejects the whole Scene before resident memory is
             * touched.
             */
            st = storage_patternStubFinalize(&op_pattern_stub_state);
            op_close_status = (st == STORAGE_STATUS_OK)
                ? FS_STATUS_DONE
                : FS_STATUS_ERROR;
            if (st != STORAGE_STATUS_OK)
                filesystem_setPresetNameInvalid();
            op_phase = 54;
        }
        return;

    case 54: /* CLOSE bridge pattern */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 55;
        return;

    case 55: /* WAIT bridge pattern close */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 72;
            return;
        }
        op_phase = 56;
        return;

    case 56: /* OPEN effect placeholder */
        storage_effectStateInit(&op_effect_state);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(op_scene_effect_open_name, "r", on_file_opened))
            return;
        op_phase = 57;
        return;

    case 57: /* WAIT effect placeholder */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 72;
            return;
        }
        op_phase = 58;
        return;

    case 58: /* READ effect placeholder */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 59;
            return;
        }
        if (line_ready) {
            st = storage_effectParseLine(&op_effect_state, op_line_buf);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 59;
            }
            return;
        }
        if (eof) {
            st = storage_effectFinalize(&op_effect_state);
            op_close_status = (st == STORAGE_STATUS_OK)
                ? FS_STATUS_DONE
                : FS_STATUS_ERROR;
            if (st != STORAGE_STATUS_OK)
                filesystem_setPresetNameInvalid();
            op_phase = 59;
        }
        return;

    case 59: /* CLOSE effect placeholder */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 60;
        return;

    case 60: /* WAIT effect placeholder close */
        if (!op_close_done) return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_phase = 72;
            return;
        }
        op_phase = 61;
        return;

    case 61: /* COMMIT staged Scene to selected resident slots */
    {
        uint8_t scene_index;

        /*
         * Atomic resident apply point.
         *
         * The loop is the only place this loader mutates SceneData. Inputs are
         * the validated op_staged_scene image and request-time destination
         * mask. Output replaces every selected resident Scene with identical
         * settings, pattern, and kit content. The active Scene runtime apply is
         * handled after filesystem completion by Preset/Menu, matching Kit
         * Load's owner boundary.
         *
         * Name retention happens immediately before the copy because
         * sceneset.scg never stores its own name. Inputs: op_scene_display_name
         * was captured from the selected root Scene directory, and
         * op_scene_child_display_name was already copied into
         * op_staged_scene.kit.display_name after embedded kitset validation.
         * Output: boot Scene Load preserves the resident Scene display name so
         * Save:[Scene] character entry can seed from "000 Slak" even when the
         * target save slot currently displays Empty.
         */
        memcpy(op_staged_scene.display_name,
               op_scene_display_name,
               STORAGE_SCENE_DISPLAY_NAME_LEN);
        op_staged_scene.display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((op_scene_load_scene_mask &
                 (uint16_t)(1u << scene_index)) != 0u) {
                scene_t *target = scene_get(scene_index);
                if (target)
                    *target = op_staged_scene;
            }
        }
        memcpy(preset_currentName, op_scene_display_name, 8u);
        if (current_op == FS_INTERNAL_OP_LOAD_BANK) {
            /*
             * Mark one Bank-local child Scene payload as loaded.
             *
             * Inputs: validated child Scene data and the one-bit destination
             * mask installed by the Bank loader before delegating here. Output:
             * only op_bank_loaded_scene changes. BankData identity, active
             * Scene, present mask, restore slot, and scene_mask_voice_edit are
             * committed by the Bank loader after every selected child has
             * finished, so a later child failure cannot leave partially updated
             * Bank metadata.
             */
            op_bank_loaded_scene = 1u;
        }
        op_close_status = FS_STATUS_DONE;
        op_phase = 72;
        return;
    }

    case 72: /* RETURN ROOT + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        if (current_op == FS_INTERNAL_OP_LOAD_BANK &&
            op_bank_payload_active) {
            /*
             * Return control to the Bank child loop instead of completing the
             * public operation.
             *
             * The Scene loader has restored the filesystem root after one
             * child payload. The Bank loader will reopen the selected Bank
             * folder, advance op_bank_child_cursor, and either load the next
             * selected child or atomically commit BankData once the mask is
             * exhausted.
             */
            op_bank_payload_active = 0u;
            op_phase = 20u;
            return;
        }
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_setPresetNameInvalid();
        op_close_status = FS_STATUS_ERROR;
        op_phase = 72;
        return;
    }
}

static void filesystem_loadBankDirectory_tick(void)
{
    uint8_t line_ready;
    uint8_t eof;
    storage_status_t st;

    if (op_bank_payload_active) {
        filesystem_loadSceneDirectory_tick();
        return;
    }

    /*
     * Load one root Bank, then optionally one Bank-local Scene.
     *
     * Phases 0..18 validate and inspect the Bank container. When a child Scene
     * is selected, the state machine opens that two-digit child and jumps into
     * filesystem_loadSceneDirectory_tick() at Scene phase 8. From there the
     * existing Scene payload reader handles sceneset.scg, embedded Kit, pattern,
     * effect, and atomic resident Scene commit.
     */
    switch (op_phase) {
    case 0:
        if (op_slot >= STORAGE_BANK_MAX_SLOTS ||
            !bank_slot_present[op_slot]) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_BANK,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3u;
        return;

    case 3:
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4u;
        return;

    case 4:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5u;
        return;

    case 5:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(bank_slot_open_name[op_slot],
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 6u;
        return;

    case 6:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        op_phase = 7u;
        return;

    case 7:
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 8u;
        return;

    case 8:
        storage_banksetInit(&op_bankset_state);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_BANKSET_FILENAME, "r", on_file_opened))
            return;
        op_phase = 9u;
        return;

    case 9:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 10u;
        return;

    case 10:
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            filesystem_setPresetNameInvalid();
            op_close_status = FS_STATUS_ERROR;
            op_phase = 11u;
            return;
        }
        if (line_ready) {
            st = storage_banksetParseLine(&op_bankset_state, op_line_buf);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 11u;
            }
            return;
        }
        if (eof) {
            st = storage_banksetFinalize(&op_bankset_state);
            op_close_status = (st == STORAGE_STATUS_OK)
                ? FS_STATUS_DONE
                : FS_STATUS_ERROR;
            if (st != STORAGE_STATUS_OK)
                filesystem_setPresetNameInvalid();
            op_phase = 11u;
        }
        return;

    case 11:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 12u;
        return;

    case 12:
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            op_close_done = false;
            if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
                op_phase = 19u;
            return;
        }
        op_bank_active_scene = op_bankset_state.active_scene;
        /*
         * Scan Bank-local Scene children using the still-open selected Bank
         * directory handle.
         *
         * We are already chdir'd into the selected Bank. Reopening the Bank by
         * its root alias here would incorrectly look for a child with the same
         * name inside itself. Keeping op_kit_slot_dir open mirrors Scene Load's
         * child scan and makes the relative directory context explicit.
         */
        afatfs_findFirstObject(op_kit_slot_dir, &op_object_finder);
        op_phase = 15u;
        return;

    case 15:
    {
        afatfsOperationStatus_e ast =
            afatfs_findNextObject(op_kit_slot_dir,
                                  &op_object_finder,
                                  &op_object);
        if (ast == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (ast == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 16u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 16u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
            uint8_t child_slot;
            char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

            /*
             * Bank child discovery uses the two-digit parser only.
             *
             * Inputs: public child directory names inside the selected Bank.
             * Output: operation-local cache for slots 00..15. The duplicate
             * rule mirrors root scans, but these names never populate the root
             * Scene library cache because Bank-local Scenes are a different
             * namespace.
             */
            if (storage_parseBankSceneFolder(op_object.id.displayName,
                                             &child_slot,
                                             display)) {
                display[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
                if (!op_bank_child_present[child_slot] ||
                    filesystem_displayPrecedesCached(
                        display,
                        op_bank_child_name[child_slot])) {
                    op_bank_child_present[child_slot] = 1u;
                    op_bank_child_present_mask =
                        (uint16_t)(op_bank_child_present_mask |
                                   (uint16_t)(1u << child_slot));
                    memcpy(op_bank_child_name[child_slot], display,
                           STORAGE_SCENE_DISPLAY_NAME_LEN);
                    op_bank_child_name[child_slot]
                                      [STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
                    /*
                     * Cache the child display component for later LFN open.
                     *
                     * `00 Slak` is the product identity inside the Bank. The
                     * SFN alias is still useful for legacy fopen paths, but
                     * Bank child Scene directories are opened through
                     * afatfs_opendir_lfn(), which matches the display name
                     * returned by this same object iterator.
                     */
                    storage_copyFilename(
                        op_bank_child_open_name[child_slot],
                        op_object.id.displayName);
                }
            }
        }
        return;
    }

    case 16:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 17u;
        return;

    case 17:
    {
        uint8_t child_slot = 0u;
        uint8_t found = 0u;
        uint16_t active_bit;

        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_bank_scene_load_mask =
            (uint16_t)(op_bank_scene_load_mask & op_bank_child_present_mask);
        if (op_bank_scene_load_mask == 0u && op_bank_child_present_mask != 0u)
            op_bank_scene_load_mask = op_bank_child_present_mask;
        active_bit = (op_bank_active_scene < STORAGE_BANK_SCENE_MAX_SLOTS)
            ? (uint16_t)(1u << op_bank_active_scene)
            : 0u;
        if ((op_bank_scene_load_mask & active_bit) == 0u) {
            for (child_slot = 0u;
                 child_slot < STORAGE_BANK_SCENE_MAX_SLOTS;
                 child_slot++) {
                if ((op_bank_scene_load_mask &
                     (uint16_t)(1u << child_slot)) != 0u) {
                    op_bank_active_scene = child_slot;
                    active_bit = (uint16_t)(1u << child_slot);
                    break;
                }
            }
        }
        if (op_bank_scene_load_mask == 0u) {
            /*
             * Empty Banks are valid.
             *
             * Output: BankData keeps the loaded Bank identity and child
             * presence map, but op_bank_loaded_scene stays zero so Preset/Menu
             * can initialize audible Scene data from root Scene, root Kit, or
             * SRAM defaults. The restore slot is still updated because the
             * Bank itself was successfully loaded.
             */
            bank_setDisplayName(op_bank_display_name);
            bank_setScenePresentMask(0u);
            bank_selectActiveSceneForEditMask(op_bank_active_scene);
            bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
            bank_setRestoreBankSlot(op_slot);
            bank_setHasResidentBank(1u);
            memcpy(preset_currentName, op_bank_display_name, 8u);
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        for (child_slot = 0u;
             child_slot < STORAGE_BANK_SCENE_MAX_SLOTS;
             child_slot++) {
            if ((op_bank_scene_load_mask &
                 (uint16_t)(1u << child_slot)) != 0u) {
                found = 1u;
                break;
            }
        }
        if (!found) {
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_bank_child_cursor = child_slot;
        op_scene_load_scene_mask = (uint16_t)(1u << child_slot);
        filesystem_initStagedScene(&op_staged_scene);
        memcpy(op_scene_display_name, op_bank_child_name[child_slot],
               STORAGE_SCENE_DISPLAY_NAME_LEN);
        op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(op_bank_child_open_name[child_slot],
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 18u;
        return;
    }

    case 18:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setPresetNameInvalid();
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        op_bank_loaded_scene = 1u;
        op_bank_payload_active = 1u;
        op_phase = 8u;
        return;

    case 20:
    {
        uint8_t child_slot;

        if (op_close_status != FS_STATUS_DONE) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        for (child_slot = (uint8_t)(op_bank_child_cursor + 1u);
             child_slot < STORAGE_BANK_SCENE_MAX_SLOTS;
             child_slot++) {
            if ((op_bank_scene_load_mask &
                 (uint16_t)(1u << child_slot)) != 0u) {
                op_bank_child_cursor = child_slot;
                op_phase = 21u;
                return;
            }
        }
        /*
         * Commit resident Bank metadata after every selected child has loaded.
         *
         * Inputs: validated Bank directory name, parsed bankset.bcg values,
         * discovered child-present mask, and the selected child loop result.
         * Outputs: BankData becomes authoritative for Save:[Bank], boot
         * restore, active Scene, edit fan-out, and Scene availability LEDs.
         * The active Scene was chosen from the loaded mask above, so it is
         * always one of the resident payloads just committed.
         */
        bank_setDisplayName(op_bank_display_name);
        bank_setScenePresentMask(op_bank_scene_load_mask);
        bank_selectActiveSceneForEditMask(op_bank_active_scene);
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
        bank_setRestoreBankSlot(op_slot);
        bank_setHasResidentBank(1u);
        scene_selectActive(op_bank_active_scene);
        memcpy(preset_currentName, op_bank_display_name, 8u);
        filesystem_finish(FS_STATUS_DONE);
        return;
    }

    case 21:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_BANK,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 22u;
        return;

    case 22:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 23u;
        return;

    case 23:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 24u;
        return;

    case 24:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(bank_slot_open_name[op_slot],
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 25u;
        return;

    case 25:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 26u;
        return;

    case 26:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 27u;
        return;

    case 27:
    {
        uint8_t child_slot = op_bank_child_cursor;

        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        op_scene_load_scene_mask = (uint16_t)(1u << child_slot);
        filesystem_initStagedScene(&op_staged_scene);
        memcpy(op_scene_display_name, op_bank_child_name[child_slot],
               STORAGE_SCENE_DISPLAY_NAME_LEN);
        op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(op_bank_child_open_name[child_slot],
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 18u;
        return;
    }

    case 19:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_ERROR);
        return;

    default:
        filesystem_setPresetNameInvalid();
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_scanBankScenes_tick(void)
{
    /*
     * Preview-scan the two-digit Scene children inside one root Bank slot.
     *
     * State machine inputs are op_slot and the root Bank scan cache. Outputs
     * are op_bank_child_present_mask/name/open_name, the same child caches used
     * by filesystem_loadBankDirectory_tick(). This read-only operation exists
     * so Menu can light Load:[Bank] LEDs for the highlighted Bank before the
     * user presses OK; it intentionally does not parse bankset.bcg, load Scene
     * payloads, or write BankData.
     */
    switch (op_phase) {
    case 0:
        if (op_slot >= STORAGE_BANK_MAX_SLOTS ||
            !bank_slot_present[op_slot]) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_BANK,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3u;
        return;

    case 3:
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4u;
        return;

    case 4:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5u;
        return;

    case 5:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(bank_slot_open_name[op_slot],
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 6u;
        return;

    case 6:
        if (!op_file_ready)
            return;
        if (!op_file) {
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_slot_dir = op_file;
        op_phase = 7u;
        return;

    case 7:
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        /*
         * Start iteration from inside the selected Bank directory.
         *
         * The open handle stays alive for findNextObject(), matching the real
         * Bank Load scan. The loop below only accepts names parsed by
         * storage_parseBankSceneFolder(), so arbitrary files and non-Scene
         * folders do not light LEDs.
         */
        afatfs_findFirstObject(op_kit_slot_dir, &op_object_finder);
        op_phase = 8u;
        return;

    case 8:
    {
        afatfsOperationStatus_e ast =
            afatfs_findNextObject(op_kit_slot_dir,
                                  &op_object_finder,
                                  &op_object);
        if (ast == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (ast == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 9u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
            uint8_t child_slot;
            char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

            /*
             * Convert one directory entry to a child Scene bit.
             *
             * Inputs: FAT display name from the selected Bank folder. Output:
             * child_slot sets one bit in op_bank_child_present_mask and stores
             * the display/open name for possible later Bank Load reuse. If two
             * entries claim the same two-digit slot, lexical display ordering
             * matches the root browser duplicate policy.
             */
            if (storage_parseBankSceneFolder(op_object.id.displayName,
                                             &child_slot,
                                             display)) {
                display[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
                if (!op_bank_child_present[child_slot] ||
                    filesystem_displayPrecedesCached(
                        display,
                        op_bank_child_name[child_slot])) {
                    op_bank_child_present[child_slot] = 1u;
                    op_bank_child_present_mask =
                        (uint16_t)(op_bank_child_present_mask |
                                   (uint16_t)(1u << child_slot));
                    memcpy(op_bank_child_name[child_slot], display,
                           STORAGE_SCENE_DISPLAY_NAME_LEN);
                    op_bank_child_name[child_slot]
                                      [STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
                    storage_copyFilename(op_bank_child_open_name[child_slot],
                                         op_object.id.displayName);
                }
            }
        }
        return;
    }

    case 9:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 10u;
        return;

    case 10:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD ONE ROOT INSTRUMENT state machine
**
** Inputs: destination Scene/slot, instrument type, and per-type browser index
** from filesystem_requestLoadInstrument(). Outputs: only the selected Scene
** staging slot is reset and populated from Instrument/<file>. Live SceneData,
** kit settings, audio routing, and all runtime objects remain unchanged until
** Preset commits the validated staging payload after completion.
** ----------------------------------------------------------------------- */
static void filesystem_loadInstrument_tick(void)
{
    uint8_t line_ready;
    uint8_t eof;
    storage_status_t st;

    switch (op_phase) {
    case 0: /* VALIDATE + CHDIR ROOT */
        if (op_instrument_load_destination_slot >= STORAGE_KIT_SLOT_COUNT ||
            op_instrument_load_type >= INSTRUMENT_TYPE_UNKNOWN ||
            op_instrument_load_index >=
                instrument_file_count[op_instrument_load_type]) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Instrument/ */
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT,
                                AFATFS_MATCH_CASE_SENSITIVE,
                                op_root_open_name,
                                on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT Instrument/ */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR Instrument/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4;
        return;

    case 4: /* CLOSE Instrument/ handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT CLOSE Instrument/ */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 6;
        return;

    case 6: /* OPEN selected instrument */
        storage_instrumentStateInit(&op_instrument_state,
                                    op_instrument_load_type,
                                    (uint8_t)(op_instrument_load_destination_slot + 1u));
        instrumentManager_resetSlot(
            &op_staged_instrument,
            op_instrument_load_type);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        /*
         * Open the selected Instrument file by scan-cache identity.
         *
         * Menu selects a typed cache index, not a freshly-entered filename.
         * The cached short alias belongs to the exact object returned by the
         * Instrument/ scan, so the open cannot drift to another same-display
         * candidate while the load is in flight. Display/stem metadata is
         * committed only after the parser validates the file.
         */
        if (!afatfs_fopen(
                instrument_file_open_name[op_instrument_load_type]
                                         [op_instrument_load_index],
                "r",
                on_file_opened)) {
            return;
        }
        op_phase = 7;
        return;

    case 7: /* WAIT selected instrument */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 11;
            return;
        }
        op_phase = 8;
        return;

    case 8: /* READ selected instrument */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9;
            return;
        }
        if (line_ready) {
            st = storage_instrumentParseLine(
                &op_instrument_state,
                op_line_buf,
                &op_staged_instrument);
            if (st != STORAGE_STATUS_OK) {
                op_close_status = FS_STATUS_ERROR;
                op_phase = 9;
            }
            return;
        }
        if (eof) {
            st = storage_instrumentFinalize(&op_instrument_state);
            if (st != STORAGE_STATUS_OK) {
                op_close_status = FS_STATUS_ERROR;
            } else {
                if (op_instrument_state.seen_morph_count == 0u) {
                    storage_instrumentCopyMainToMorphFallback(
                        op_instrument_load_type,
                        (uint8_t)(op_instrument_load_destination_slot + 1u),
                        &op_staged_instrument);
                }
                /*
                 * Stage the browser-normalized file stem beside the payload.
                 *
                 * Input: immutable per-type scan-cache entry selected by this
                 * operation. Output: a nine-byte NUL-terminated name that
                 * Preset commits with the same slot image. Keeping both staged
                 * prevents the LCD from claiming a file belongs to a slot when
                 * parsing or runtime commit has not completed.
                 */
                memcpy(op_staged_instrument_display_name,
                       instrument_file_name[op_instrument_load_type]
                                           [op_instrument_load_index], 9u);
                memcpy(op_staged_instrument_stem,
                       instrument_file_stem[op_instrument_load_type]
                                           [op_instrument_load_index],
                       sizeof(op_staged_instrument_stem));
                op_close_status = FS_STATUS_DONE;
            }
            op_phase = 9;
        }
        return;

    case 9: /* CLOSE selected instrument */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 10;
        return;

    case 10: /* WAIT CLOSE selected instrument */
        if (!op_close_done) return;
        op_file = NULL;
        op_phase = 11;
        return;

    case 11: /* RETURN ROOT + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        op_close_status = FS_STATUS_ERROR;
        op_phase = 11;
        return;
    }
}

/* -----------------------------------------------------------------------
** SAVE ONE ROOT INSTRUMENT state machine
**
** Inputs: source Scene/slot, captured instrument type, and exact visible
** filename from filesystem_requestSaveInstrument(). Output: creates/opens
** Instrument/<filename> with case-preserving LFN support, streams the source
** resident instrument text into that file, updates the Instrument browser
** cache with the returned short alias, and finishes only after the write has
** been closed and sync-flushed by filesystem_finish().
** ----------------------------------------------------------------------- */
static void filesystem_saveInstrument_tick(void)
{
    const scene_t *scene = scene_getConst(op_instrument_save_source_scene);
    const kit_instrument_slot_t *instrument =
        (scene && op_instrument_save_source_slot < STORAGE_KIT_SLOT_COUNT)
            ? &scene->kit.instruments[op_instrument_save_source_slot]
            : NULL;
    uint8_t morph_save =
        (uint8_t)(op_instrument_save_mode == STORAGE_INSTRUMENT_SAVE_MORPH);

    switch (op_phase) {
    case 0: /* VALIDATE + CHDIR ROOT */
        /*
         * Revalidate the captured source before opening any filesystem object.
         *
         * Inputs were copied at request acceptance time, but SceneData is live
         * RAM. The type comparison prevents a delayed save from serializing a
         * different instrument type under the filename/extension chosen for the
         * original source slot.
         */
        if (!instrument ||
            op_instrument_save_source_slot >= STORAGE_KIT_SLOT_COUNT ||
            op_instrument_save_type >= INSTRUMENT_TYPE_UNKNOWN ||
            instrument->type != op_instrument_save_type ||
            op_instrument_save_display_name[0] == '\0') {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* SCAN root for existing Instrument/ */
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        /*
         * Instrument is a long root component, so do not start with
         * mkdir_lfn("Instrument").
         *
         * A create-capable LFN lookup that misses an existing root LFN can
         * allocate a second physical Instrument directory. Saving a root
         * Instrument must first prove whether one already exists by scanning
         * the root object list and opening the matching directory by its exact
         * short alias. Only a scan miss creates a fresh Instrument/.
         */
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT root scan */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
        op_phase = 3;
        return;

    case 3: /* FIND Instrument/ in root */
    {
        afatfsOperationStatus_e st =
            afatfs_findNextObject(op_kit_root_dir,
                                  &op_object_finder,
                                  &op_object);
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_phase = 4;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY &&
            fat_compareDisplayName(op_object.id.displayName,
                                   STORAGE_ROOT_INSTRUMENT,
                                   false) == 0) {
            storage_copyFilename(op_root_open_name, op_object.id.shortName);
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_phase = 4;
        }
        return;
    }

    case 4: /* CLOSE root scan */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT root scan close + OPEN/CREATE Instrument/ */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (op_root_open_name[0] != '\0') {
            if (!afatfs_opendir(op_root_open_name, on_file_opened))
                return;
        } else {
            if (!afatfs_mkdir_lfn(STORAGE_ROOT_INSTRUMENT,
                                  AFATFS_MATCH_CASE_INSENSITIVE,
                                  op_root_open_name,
                                  on_file_opened)) {
                return;
            }
        }
        op_phase = 6;
        return;

    case 6: /* WAIT Instrument/ */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 7;
        return;

    case 7: /* CHDIR Instrument/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 8;
        return;

    case 8: /* CLOSE Instrument/ handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 9;
        return;

    case 9: /* WAIT CLOSE Instrument/ */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 10;
        return;

    case 10: /* REMOVE target instrument variants */
        /*
         * Remove case-variant Instrument files before saving one root
         * Instrument.
         *
         * What: In Instrument/, removes every file whose display component
         * matches the requested target under case-insensitive comparison.
         *
         * Why: The root Instrument pool is user-copyable from desktop
         * filesystems. If a card contains both `fiRstfile.snr` and
         * `firStfile.snr`, the browser exposes only the capital-first winner,
         * and overwrite must collapse all physical variants into the newly
         * entered case.
         *
         * Inputs: op_instrument_save_display_name is the captured target
         * filename from Menu after extension/type construction. Output: the
         * following fopen_lfn() writes one replacement file and returns its
         * short alias for cache update.
         *
         * Affiliates/clients: filesystem_updateInstrumentCacheAfterSave(),
         * afatfs_removeObjects_lfn(), afatfs_fopen_lfn(), nested Instrument
         * Save UI.
         */
        op_remove_done = 0u;
        if (!afatfs_removeObjects_lfn(op_instrument_save_display_name,
                                      AFATFS_MATCH_CASE_INSENSITIVE,
                                      AFATFS_REMOVE_FILES_ONLY,
                                      on_remove_complete)) {
            return;
        }
        op_phase = 11;
        return;

    case 11: /* WAIT remove + OPEN target instrument file */
        if (!op_remove_done)
            return;
        op_file_ready = false;
        op_file = NULL;
        memset(op_instrument_save_open_name, 0,
               sizeof(op_instrument_save_open_name));
        /*
         * Open the exact visible Instrument filename with write semantics.
         *
         * fopen_lfn() is required here rather than fopen(): the user-facing
         * Instrument pool may contain mixed case, spaces, and long stems. The
         * returned short alias is retained so the browser cache can reopen the
         * same physical object after this save without requiring a rescan.
         */
        if (!afatfs_fopen_lfn(op_instrument_save_display_name,
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_instrument_save_open_name,
                              on_file_opened))
            return;
        op_phase = 12;
        return;

    case 12: /* WAIT target instrument file */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 13;
        return;

    case 13: /* WRITE complete instrument text */
    {
        filesystem_instrument_write_ctx_t ctx = {{
            instrument,
            op_instrument_save_type,
            (uint8_t)(op_instrument_save_source_slot + 1u),
            scene ? scene->settings.voice_morph_amount[
                        op_instrument_save_source_slot] : 0u,
            op_instrument_save_mode
        }};
        /*
         * Build the root Instrument save view.
         *
         * What: Captures the source slot image, file voice coordinate,
         * retained per-slot Morph amount, and normal-vs-Morph save mode for
         * the formatter.
         *
         * Why: normal Instrument Save and InstrumentMrp Save share all
         * asyncfatfs overwrite behavior. Their only storage difference is the
         * endpoint projection inside the Instrument text file.
         *
         * Inputs: request-time source Scene/slot and captured save projection
         * mode. Output: one context consumed by
         * filesystem_nextInstrumentLine().
         *
         * Affiliates/clients: storage_formatInstrumentLineView(),
         * filesystem_requestSaveInstrumentMode(), nested Instrument Save UI.
         */
        if (filesystem_writeTextLine(filesystem_nextInstrumentLine, &ctx))
            return;
        op_phase = 14;
        return;
    }

    case 14: /* CLOSE target instrument file */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15;
        return;

    case 15: /* WAIT CLOSE target instrument file */
        if (!op_close_done) return;
        op_file = NULL;
        op_phase = 16;
        return;

    case 16: /* RETURN ROOT + UPDATE CACHE */
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Make the saved file immediately visible to nested Instrument Load.
         *
         * Inputs are the exact display filename and returned asyncfatfs short
         * alias. The helper removes older matching cache entries before adding
         * this one, so re-saving the same visible Instrument updates selection
         * identity instead of showing duplicate browser rows.
         */
        filesystem_updateInstrumentCacheAfterSave(
            op_instrument_save_display_name,
            op_instrument_save_open_name);
        if (!morph_save) {
            /*
             * Retain Instrument source name only for normal Instrument Save.
             *
             * What: Skips resident source-name mutation when this writer was
             * entered for InstrumentMrp Save.
             *
             * Why: Morph Save writes a transformed file while preserving the
             * currently loaded Instrument identity. Updating the retained stem
             * would make later normal Kit/Instrument Save pretend the
             * morph-export filename is the loaded source.
             *
             * Inputs: morph_save flag plus request-time source Scene/slot.
             * Outputs: SceneData source-name metadata for normal save only.
             *
             * Affiliates/clients: menu_instrumentSaveSeedName(), normal root
             * Instrument Save, InstrumentMrp Save no-name-change tests.
             */
            scene_setInstrumentSourceName(op_instrument_save_source_scene,
                                          op_instrument_save_source_slot,
                                          op_instrument_save_display_name);
        }
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static uint8_t filesystem_writeTextLine(uint8_t (*next_line)(char *, uint16_t,
                                                             void *),
                                        void *ctx)
{
    /*
     * Stream text save files one line at a time.
     *
     * Kit instruments can be larger than the generic 512-byte staging buffer,
     * and save must keep foreground work bounded like the existing pattern
     * writer. The filesystem state machine owns write offsets while
     * storageTypes owns line contents.
     */
    if (op_write_line_len == 0u) {
        op_write_line_len = next_line(op_write_line_buf,
                                      sizeof(op_write_line_buf),
                                      ctx);
        op_write_line_offset = 0u;
        if (op_write_line_len == 0u)
            return 0u;
    }
    op_write_line_offset = (uint16_t)(op_write_line_offset +
        afatfs_fwrite(op_file,
                      (const uint8_t *)op_write_line_buf +
                          op_write_line_offset,
                      op_write_line_len - op_write_line_offset));
    if (op_write_line_offset >= op_write_line_len) {
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_write_line_index++;
    }
    return 1u;
}

static void filesystem_initStagedScene(scene_t *scene)
{
    static const instrument_type_t initial_types[INSTRUMENT_SLOT_COUNT] = {
        INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM,
        INSTRUMENT_TYPE_SNR, INSTRUMENT_TYPE_CYM, INSTRUMENT_TYPE_HAT
    };
    uint8_t slot;
    uint8_t track;

    /*
     * Build safe defaults for a filesystem-owned staged Scene.
     *
     * SceneData's scene_initAll() initializes resident scenes[] directly. Scene
     * Load needs the same style of defaults in private staging memory so
     * optional sceneset keys can be absent without leaving random bytes, and so
     * a failed child file never mutates resident Scene state.
     */
    if (!scene)
        return;
    memset(scene, 0, sizeof(*scene));
    /*
     * PatternData owns the default bridge pattern shape.
     *
     * Inputs: staged PatternSet pointer plus nextPattern 0 for a neutral Scene
     * payload. Output: the private staging pattern is valid before the loader
     * encounters either a thin text placeholder or an older binary pattern
     * payload. This call must happen after memset() and before child file parse
     * phases so missing optional pattern details cannot leave zero-length track
     * settings.
     */
    pat_initPatternSet(&scene->pattern, 0u);
    scene->settings.voice_decimation_all = 127u;
    for (track = 0u; track < NUM_TRACKS; track++) {
        scene->settings.midi_channel[track] = (uint8_t)(track + 1u);
        scene->settings.midi_note[track] = PAT_DEFAULT_NOTE;
    }
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        char fallback[9] = { 'i', 'n', 's', 't', '_', 'v', 'o',
                             (char)('1' + slot), '\0' };
        /*
         * Scene-owned voice mix defaults mirror SceneData's resident init.
         *
         * Inputs: the zero-based instrument slot. Outputs: staged Scene route,
         * FX send, and fader mode bytes before any optional sceneset.scg lines
         * are parsed. The loop is deliberately per-slot, not per-track, because
         * audio routing is six-voice mixer state while MIDI defaults above are
         * seven-track sequencer state.
         */
        scene->settings.audio_out[slot] = filesystem_defaultVoiceAudioOut(slot);
        scene->settings.fx_send_amount[slot] = 0u;
        scene->settings.fader_setting[slot] = 0u;
        instrumentManager_resetSlot(&scene->kit.instruments[slot],
                                    initial_types[slot]);
        scene_setKitInstrumentSourceName(&scene->kit, slot, fallback);
    }
}

static uint8_t filesystem_defaultVoiceAudioOut(uint8_t slot)
{
    /*
     * Local copy of the SceneData route default for filesystem staging.
     *
     * What: returns the canonical mixer route for a missing Scene audio_out
     * value: slot 1/voice 1 uses stereo DAC2, slot 6/voice 6 uses DAC1 right,
     * and all other voices use stereo DAC1.
     *
     * Why: filesystem.c initializes off-scene staging memory and cannot route
     * through scene_setVoiceAudioOut(), which writes resident SceneData by
     * index. Keeping the arithmetic here byte-for-byte simple also makes the
     * legacy kitset import fallback below explicit.
     *
     * Inputs: zero-based voice slot. Output: persisted Scene route domain
     * 0..5. Affiliates: scene_defaultVoiceAudioOut() in SceneData.c and
     * preset_applyKitAudioRouting().
     */
    if (slot == 0u)
        return 2u;
    if (slot == 5u)
        return 1u;
    return 0u;
}

static uint8_t filesystem_nameStartsWithKitSpace(const char *name)
{
    /*
     * Identify embedded Scene Kit directories.
     *
     * Inputs: display/LFN or short alias text from FAT. Output: nonzero only
     * for names beginning with "Kit ". The text after that space is the loaded
     * Kit name; sceneset.scg must never store or override it.
     */
    return (uint8_t)(name &&
                     name[0] == 'K' && name[1] == 'i' &&
                     name[2] == 't' && name[3] == ' ');
}

static char filesystem_asciiLower(char c)
{
    /*
     * Fold one ASCII byte for local extension checks.
     *
     * This helper is not used for product identity sorting; those paths use
     * fat_compareDisplayName* so duplicate case policy stays centralized in
     * the FAT display-name helpers.
     */
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static uint8_t filesystem_nameHasExtension(const char *name,
                                           const char *extension)
{
    uint8_t dot = 0xffu;
    uint8_t i = 0u;

    /*
     * Case-insensitive filename extension check for Scene child discovery.
     *
     * Users may rename/move .pat and .fx files between Scenes. The loader scans
     * directory entries and accepts the first matching extension, then validates
     * the file contents before committing the staged Scene.
     */
    if (!name || !extension)
        return 0u;
    while (name[i] != '\0') {
        if (name[i] == '.')
            dot = i;
        i++;
    }
    if (dot == 0xffu)
        return 0u;
    i = 0u;
    while (extension[i] != '\0') {
        char a = filesystem_asciiLower(name[dot + i]);
        char b = filesystem_asciiLower(extension[i]);
        if (a != b)
            return 0u;
        i++;
    }
    return (uint8_t)(name[dot + i] == '\0');
}

static uint8_t filesystem_nextInstrumentLine(char *dst, uint16_t cap,
                                             void *raw)
{
    filesystem_instrument_write_ctx_t *ctx =
        (filesystem_instrument_write_ctx_t *)raw;
    /*
     * Stream one Instrument line through a storage-owned save view.
     *
     * What: Adapts filesystem's generic line writer to storageTypes'
     * normal-vs-Morph Instrument formatter.
     *
     * Why: filesystem.c owns asynchronous file sequencing and foreground write
     * pacing. storageTypes owns descriptor iteration, [params]/[morph] section
     * rules, and Morph Save endpoint projection.
     *
     * Inputs: opaque context built in the active save phase and
     * op_write_line_index advanced by filesystem_writeTextLine(). Output: one
     * formatted line or zero at schema completion.
     *
     * Affiliates/clients: filesystem_saveInstrument_tick() and
     * storage_formatInstrumentLineView().
     */
    return storage_formatInstrumentLineView(dst, cap, &ctx->view,
                                            op_write_line_index);
}

static void filesystem_copyLongComponent(char *dst, uint16_t cap,
                                         const char *src)
{
    uint16_t i = 0u;

    if (!dst || cap == 0u)
        return;
    if (!src)
        src = "";
    while (i + 1u < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void filesystem_makeNumberedDir(char *dst,
                                       uint16_t slot,
                                       const char display[8])
{
    /*
     * Format one root numbered library directory.
     *
     * Inputs: direct slot 000..999 and eight display cells. Output:
     * `NNN <name>`. The hundreds/tens/ones arithmetic is root-library only:
     * Bank-local Scene children use storage_formatBankSceneDir() and two
     * digits, so callers should not reuse this helper for `00..15` children.
     */
    dst[0] = (char)('0' + ((slot / 100u) % 10u));
    dst[1] = (char)('0' + ((slot / 10u) % 10u));
    dst[2] = (char)('0' + (slot % 10u));
    dst[3] = ' ';
    for (uint8_t i = 0u; i < STORAGE_KIT_DISPLAY_NAME_LEN; i++)
        dst[4u + i] = display ? display[i] : ' ';
    dst[4u + STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
}

static void filesystem_makeBankScratchDir(char *dst,
                                          uint16_t cap,
                                          const char prefix[3],
                                          uint16_t slot,
                                          uint16_t nonce)
{
    static const char hex[] = "0123456789abcdef";
    uint8_t pos = 0u;

    /*
     * Format a non-slot Bank staging directory name.
     *
     * Inputs: three-letter prefix (`tmp` for the newly written payload or
     * `old` for the displaced previous Bank), root Bank slot, and a mixed
     * request nonce. Output: a single FAT component like
     * `tmp000-4a3f` that never begins with a digit. Root Bank scanning accepts
     * only `NNN Name`, so these scratch directories are ignored by Bank Load if
     * power is lost during promotion.
     *
     * Why: Bank Save must stop mutating `000 Slak/` in place. Writing a
     * complete non-numbered temp tree first prevents stale child Scenes and
     * duplicate Instrument files from being merged into the loadable Bank
     * namespace. The nonce makes repeated failed saves unlikely to reopen a
     * stale temp folder if a previous promotion was interrupted.
     */
    if (!dst || cap < 12u || !prefix)
        return;
    dst[pos++] = prefix[0];
    dst[pos++] = prefix[1];
    dst[pos++] = prefix[2];
    dst[pos++] = (char)('0' + ((slot / 100u) % 10u));
    dst[pos++] = (char)('0' + ((slot / 10u) % 10u));
    dst[pos++] = (char)('0' + (slot % 10u));
    dst[pos++] = '-';
    dst[pos++] = hex[(nonce >> 12) & 0x0fu];
    dst[pos++] = hex[(nonce >> 8) & 0x0fu];
    dst[pos++] = hex[(nonce >> 4) & 0x0fu];
    dst[pos++] = hex[nonce & 0x0fu];
    dst[pos] = '\0';
}

static void filesystem_prepareBankScratchDirs(uint16_t nonce)
{
    /*
     * Build the paired scratch names for one Bank Save attempt.
     *
     * Inputs: op_slot is the root Bank slot being saved and nonce is already
     * mixed by filesystem_nextBankScratchNonce(). Outputs: temp and displaced
     * old directory names share the same suffix, e.g. `tmp000-4a3f` and
     * `old000-4a3f`, so a card directory listing shows which old Bank was
     * moved aside by which temp publish attempt.
     */
    filesystem_makeBankScratchDir(op_save_bank_tmp_display_name,
                                  sizeof(op_save_bank_tmp_display_name),
                                  "tmp",
                                  op_slot,
                                  nonce);
    filesystem_makeBankScratchDir(op_save_bank_old_display_name,
                                  sizeof(op_save_bank_old_display_name),
                                  "old",
                                  op_slot,
                                  nonce);
}

static uint8_t filesystem_bankScratchNameCollides(
    const afatfsObjectInfo_t *object)
{
    /*
     * Check whether one existing /Bank child already owns the chosen scratch
     * display name.
     *
     * Inputs: LFN-aware object metadata from the /Bank preflight scan. Output:
     * nonzero when either paired scratch component is already present. This is
     * deliberately a display-name check, matching the later mkdir_lfn() open
     * rule. The object type does not matter: a stale file named like the temp
     * directory should also force a different scratch component.
     *
     * Why: asyncfatfs mkdir_lfn() is create-or-open. If Save Bank reuses an
     * old `tmp...` component, the payload writer merges new Scenes into that
     * stale temp tree and can preserve obsolete embedded Kit directories such
     * as the extra `Kit Slak` observed in `000 SlakBad4/01 Slak2/`.
     */
    if (!object || object->id.kind == AFATFS_OBJECT_NONE)
        return 0u;
    if (fat_compareDisplayName(object->id.displayName,
                               op_save_bank_tmp_display_name,
                               false) == 0) {
        return 1u;
    }
    if (fat_compareDisplayName(object->id.displayName,
                               op_save_bank_old_display_name,
                               false) == 0) {
        return 1u;
    }
    return 0u;
}

static void filesystem_makeSceneEmbeddedKitDir(char *dst,
                                               uint16_t cap,
                                               const char display[8])
{
    uint16_t pos = 0u;
    int8_t end = (int8_t)(STORAGE_KIT_DISPLAY_NAME_LEN - 1u);

    /*
     * Build the visible "Kit <name>" child directory for Scene Save.
     *
     * Inputs: resident Kit display field, exactly eight cells. Output: a
     * NUL-terminated FAT display component. The reverse loop trims trailing
     * spaces because host filesystems often dislike names that visually depend
     * on suffix blanks. If every display cell is blank, "none" is used because
     * uninitialized resident object names are product data too; they should
     * serialize through the same visible default instead of inventing
     * "Untitled" in the filesystem layer.
     */
    if (!dst || cap == 0u)
        return;
    dst[0] = '\0';
    while (end >= 0 && (!display || display[(uint8_t)end] == ' '))
        end--;
    if (!filesystem_appendText(dst, cap, &pos, "Kit "))
        return;
    if (end < 0) {
        (void)filesystem_appendText(dst, cap, &pos, "none");
        return;
    }
    for (uint8_t i = 0u; i <= (uint8_t)end; i++) {
        char c = display[i];
        if (c < 0x20 || c > 0x7e)
            c = ' ';
        if (!filesystem_appendChar(dst, cap, &pos, c))
            return;
    }
}

static uint8_t filesystem_prepareBankSceneSaveSource(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t voice;

    /*
     * Prepare all per-child Scene Save scratch for one Bank-local child.
     *
     * Inputs: resident Scene index selected by op_bank_scene_save_mask.
     * Outputs: op_kit_save_source_scene, the Bank-local `SS Name` directory,
     * Scene display name, embedded `Kit <name>` child, and six Instrument file
     * names are rebuilt from that same source Scene. The Bank Save loop calls
     * this before delegating to filesystem_saveSceneDirectory_tick(); doing the
     * setup per child prevents Scene 03 from accidentally saving Scene 00's
     * kit identity or member filenames.
     */
    if (!scene || scene_index >= SCENE_COUNT ||
        scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS) {
        return 0u;
    }
    op_kit_save_source_scene = scene_index;
    op_kit_save_mode = STORAGE_INSTRUMENT_SAVE_NORMAL;
    storage_formatBankSceneDir(op_save_kit_dir_display_name,
                               sizeof(op_save_kit_dir_display_name),
                               scene_index,
                               scene->display_name);
    memcpy(op_scene_display_name, scene->display_name,
           STORAGE_SCENE_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    filesystem_makeSceneEmbeddedKitDir(
        op_save_scene_kit_display_name,
        sizeof(op_save_scene_kit_display_name),
        scene->kit.display_name);
    for (voice = 0u; voice < STORAGE_KIT_SLOT_COUNT; voice++) {
        storage_makeSavedInstrumentDisplayFilename(
            op_save_kit_member_display_file[voice],
            sizeof(op_save_kit_member_display_file[voice]),
            scene->kit.instrument_stem[voice],
            scene->kit.instruments[voice].type,
            (uint8_t)(voice + 1u),
            1u);
    }
    return 1u;
}

static uint8_t filesystem_appendChar(char *dst, uint16_t cap,
                                     uint16_t *pos, char c)
{
    if (!dst || !pos || *pos + 1u >= cap)
        return 0u;
    dst[(*pos)++] = c;
    dst[*pos] = '\0';
    return 1u;
}

static uint8_t filesystem_appendText(char *dst, uint16_t cap,
                                     uint16_t *pos, const char *text)
{
    if (!text)
        return 0u;
    while (*text != '\0') {
        if (!filesystem_appendChar(dst, cap, pos, *text++))
            return 0u;
    }
    return 1u;
}

static uint8_t filesystem_appendU16(char *dst, uint16_t cap,
                                    uint16_t *pos, uint16_t value)
{
    char digits[5];
    uint8_t count = 0u;

    do {
        digits[count++] = (char)('0' + (value % 10u));
        value = (uint16_t)(value / 10u);
    } while (value != 0u && count < sizeof(digits));
    while (count > 0u) {
        if (!filesystem_appendChar(dst, cap, pos, digits[--count]))
            return 0u;
    }
    return 1u;
}

static uint8_t filesystem_formatAssignmentTextLine(char *dst, uint16_t cap,
                                                   const char *key,
                                                   const char *value)
{
    uint16_t pos = 0u;

    if (cap == 0u)
        return 0u;
    dst[0] = '\0';
    if (!filesystem_appendText(dst, cap, &pos, key) ||
        !filesystem_appendChar(dst, cap, &pos, '=') ||
        !filesystem_appendText(dst, cap, &pos, value) ||
        !filesystem_appendChar(dst, cap, &pos, '\n')) {
        return 0u;
    }
    return (uint8_t)pos;
}

static uint8_t filesystem_formatAssignmentU16Line(char *dst, uint16_t cap,
                                                  const char *key,
                                                  uint16_t value)
{
    uint16_t pos = 0u;

    if (cap == 0u)
        return 0u;
    dst[0] = '\0';
    if (!filesystem_appendText(dst, cap, &pos, key) ||
        !filesystem_appendChar(dst, cap, &pos, '=') ||
        !filesystem_appendU16(dst, cap, &pos, value) ||
        !filesystem_appendChar(dst, cap, &pos, '\n')) {
        return 0u;
    }
    return (uint8_t)pos;
}

static uint8_t filesystem_formatAssignmentCsvU8Line(char *dst,
                                                    uint16_t cap,
                                                    const char *key,
                                                    const uint8_t *values,
                                                    uint8_t count)
{
    uint16_t pos = 0u;
    uint8_t i;

    /*
     * Format "key=a,b,c\n" for fixed-size Scene setting vectors.
     *
     * Inputs: schema key, value array, and explicit count. Output: one complete
     * text line or zero on buffer exhaustion. The loop emits a comma before
     * every value except index zero; values are promoted to uint16_t only
     * because filesystem_appendU16() is the shared decimal writer.
     */
    if (cap == 0u || !key || !values)
        return 0u;
    dst[0] = '\0';
    if (!filesystem_appendText(dst, cap, &pos, key) ||
        !filesystem_appendChar(dst, cap, &pos, '=')) {
        return 0u;
    }
    for (i = 0u; i < count; i++) {
        if (i > 0u && !filesystem_appendChar(dst, cap, &pos, ','))
            return 0u;
        if (!filesystem_appendU16(dst, cap, &pos, values[i]))
            return 0u;
    }
    if (!filesystem_appendChar(dst, cap, &pos, '\n'))
        return 0u;
    return (uint8_t)pos;
}

static uint8_t filesystem_formatLiteralLine(char *dst, uint16_t cap,
                                            const char *text)
{
    uint16_t pos = 0u;

    if (cap == 0u)
        return 0u;
    dst[0] = '\0';
    if (!filesystem_appendText(dst, cap, &pos, text))
        return 0u;
    return (uint8_t)pos;
}

static uint8_t filesystem_nextScenesetLine(char *dst, uint16_t cap,
                                           void *raw)
{
    const scene_t *scene = (const scene_t *)raw;

    /*
     * Stream one sceneset.scg line for Scene Save.
     *
     * Inputs: source Scene and op_write_line_index from the generic text
     * writer. Output: one schema line or zero after the final retained setting.
     * The switch is intentionally flat so adding future Scene-level fields
     * requires one explicit line number and parser counterpart.
     *
     * Naming rule: no name= line is emitted. Scene identity is the enclosing
     * Scene directory, and embedded Kit identity is the "Kit <name>" directory.
     * This writer therefore serializes only Scene-level settings.
     */
    if (!scene)
        return 0u;
    switch (op_write_line_index) {
    case 0u:
        return filesystem_formatLiteralLine(dst, cap,
                                            "format=helicase.sceneset\n");
    case 1u:
        return filesystem_formatLiteralLine(dst, cap, "version=1\n");
    case 2u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "morph_amount", scene->settings.morph_amount);
    case 3u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "voice_morph_amount",
            scene->settings.voice_morph_amount, INSTRUMENT_SLOT_COUNT);
    case 4u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "voice_decimation_all",
            scene->settings.voice_decimation_all);
    case 5u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "midi_channel", scene->settings.midi_channel,
            NUM_TRACKS);
    case 6u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "midi_note", scene->settings.midi_note, NUM_TRACKS);
    case 7u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "audio_out", scene->settings.audio_out,
            INSTRUMENT_SLOT_COUNT);
    case 8u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "fx_send_amount", scene->settings.fx_send_amount,
            INSTRUMENT_SLOT_COUNT);
    case 9u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "fader_setting", scene->settings.fader_setting,
            INSTRUMENT_SLOT_COUNT);
    default:
        return 0u;
    }
}

static uint8_t filesystem_nextEffectPlaceholderLine(char *dst, uint16_t cap,
                                                    void *raw)
{
    /*
     * Adapt storageTypes' effect placeholder writer to filesystem_writeTextLine.
     *
     * The raw context is unused because effects.fx currently has no runtime
     * payload. op_write_line_index is the only input, and storageTypes owns the
     * exact emitted schema.
     */
    (void)raw;
    return storage_formatEffectPlaceholderLine(dst, cap, op_write_line_index);
}

static uint8_t filesystem_nextPatternStubLine(char *dst, uint16_t cap,
                                              void *raw)
{
    const PatternSet *pattern = (const PatternSet *)raw;

    /*
     * Adapt storageTypes' draft pattern writer to filesystem_writeTextLine.
     *
     * Inputs: raw is the Scene being saved's PatternSet. Output: one v2 draft
     * pattern.pat row per call. The writer stores only Step active bits plus
     * length/scale; non-stored fields are recreated from PatternData defaults
     * by the loader.
     */
    return storage_formatPatternStubLine(dst, cap, pattern,
                                         op_write_line_index);
}

static uint8_t filesystem_nextBanksetLine(char *dst, uint16_t cap,
                                          void *raw)
{
    /*
     * Adapt storageTypes' Bank config writer to filesystem_writeTextLine.
     *
     * Inputs: op_bankset_state.active_scene and the generic line index.
     * Output: one bankset.bcg line. The writer intentionally has no Bank name
     * access because the enclosing Bank directory is the sole identity owner.
     */
    (void)raw;
    return storage_formatBanksetLine(dst, cap, &op_bankset_state,
                                     op_write_line_index);
}

static uint8_t filesystem_nextSettingsLine(char *dst, uint16_t cap,
                                           void *raw)
{
    (void)raw;

    /*
     * Stream one settings.cfg line.
     *
     * Inputs: op_write_line_index and the live global settings/Bank restore
     * slot. Output: a complete keyed text line or zero after the schema ends.
     * The switch is deliberately an allowlist: Scene-owned Morph and voice
     * values are absent even though they still have ParameterArray ids.
     */
    switch (op_write_line_index) {
    case 0u:
        return filesystem_formatLiteralLine(dst, cap,
                                            "format=helicase.settings\n");
    case 1u:
        return filesystem_formatLiteralLine(dst, cap, "version=1\n");
    case 2u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "active_bank", bank_restoreBankSlot());
    case 3u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "bpm", parameter_values[PAR_BPM]);
    case 4u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "ext_sync", parameter_values[PAR_EXT_SYNC]);
    case 5u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "quantisation", parameter_values[PAR_QUANTISATION]);
    case 6u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "midi_chan_global",
            parameter_values[PAR_MIDI_CHAN_GLOBAL]);
    case 7u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "midi_filt_tx", parameter_values[PAR_MIDI_FILT_TX]);
    case 8u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "midi_filt_rx", parameter_values[PAR_MIDI_FILT_RX]);
    case 9u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "midi_routing", parameter_values[PAR_MIDI_ROUTING]);
    case 10u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "screensaver_on_off",
            parameter_values[PAR_SCREENSAVER_ON_OFF]);
    case 11u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "bar_reset_mode", parameter_values[PAR_BAR_RESET_MODE]);
    case 12u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "prescaler_clock_in",
            parameter_values[PAR_PRESCALER_CLOCK_IN]);
    case 13u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "prescaler_clock_out1",
            parameter_values[PAR_PRESCALER_CLOCK_OUT1]);
    case 14u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "follow", parameter_values[PAR_FOLLOW]);
    case 15u:
        return filesystem_formatAssignmentU16Line(
            dst, cap, "osc_wave_interp",
            parameter_values[PAR_OSC_WAVE_INTERP]);
    default:
        return 0u;
    }
}

static uint8_t filesystem_nextKitsetLine(char *dst, uint16_t cap,
                                         void *raw)
{
    const kit_t *kit = (const kit_t *)raw;
    const scene_t *source_scene = scene_getConst(op_kit_save_source_scene);
    uint16_t index = op_write_line_index;
    uint8_t slot;
    uint8_t field;

    if (!kit)
        return 0u;
    if (index == 0u)
        return filesystem_formatLiteralLine(dst, cap,
                                            "format=helicase.kitset\n");
    if (index == 1u)
        return filesystem_formatLiteralLine(dst, cap, "version=1\n");
    if (index == 2u)
        return filesystem_formatAssignmentU16Line(
            dst, cap, "slot6_track7_amp_envelope_decay",
            (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_MORPH &&
             op_kit_save_source_scene < SCENE_COUNT)
                ? filesystem_interpolateMorphEndpoint(
                      kit->settings.slot6_track7_amp_envelope_decay,
                      kit->settings.slot6_track7_morph_amp_envelope_decay,
                      source_scene
                          ? source_scene->settings.voice_morph_amount[5u]
                          : 0u)
                : kit->settings.slot6_track7_amp_envelope_decay);
    if (index == 3u)
        return filesystem_formatAssignmentU16Line(
            dst, cap, "slot6_track7_morph_amp_envelope_decay",
            (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_MORPH)
                ? filesystem_interpolateMorphEndpoint(
                      kit->settings.slot6_track7_amp_envelope_decay,
                      kit->settings.slot6_track7_morph_amp_envelope_decay,
                      source_scene
                          ? source_scene->settings.voice_morph_amount[5u]
                          : 0u)
                : kit->settings.slot6_track7_morph_amp_envelope_decay);
    if (index == 4u)
        return filesystem_formatLiteralLine(dst, cap, "\n");

    /*
     * Each slot now serializes four logical lines: header, type, file, and a
     * blank separator. audio_out used to be a fifth per-slot line, but routing
     * belongs to sceneset.scg. The integer division maps a monotonically
     * increasing line index onto the source instrument slot; the modulus picks
     * the field inside that four-line slot block.
     */
    index = (uint16_t)(index - 5u);
    slot = (uint8_t)(index / 4u);
    field = (uint8_t)(index % 4u);
    if (slot >= STORAGE_KIT_SLOT_COUNT)
        return 0u;

    switch (field) {
    case 0u:
        if (cap < 9u)
            return 0u;
        dst[0] = '[';
        dst[1] = 's';
        dst[2] = 'l';
        dst[3] = 'o';
        dst[4] = 't';
        dst[5] = (char)('1' + slot);
        dst[6] = ']';
        dst[7] = '\n';
        dst[8] = '\0';
        return 8u;
    case 1u:
        return filesystem_formatAssignmentTextLine(
            dst, cap, "type",
            storage_instrumentTypeToText(kit->instruments[slot].type));
    case 2u:
        return filesystem_formatAssignmentTextLine(
            dst, cap, "file", op_save_kit_member_display_file[slot]);
    default:
        /*
         * Blank separator after each slot. New Kit writers intentionally stop
         * here; legacy audio_out values remain parse-only compatibility data
         * and are never emitted from a root or embedded Kit Save.
         */
        return filesystem_formatLiteralLine(dst, cap, "\n");
    }
}

/*
 * Recursive directory deletion helper for directory-shaped saves.
 *
 * Contract:
 * - The caller must have already chdir'd into the parent directory that owns
 *   display_name.
 * - display_name is one visible LFN component, not a slash-separated path.
 * - Missing targets are treated as successful no-ops, so save paths can call
 *   this unconditionally before recreating a directory.
 * - On success, the asyncfatfs current directory is back at the original
 *   parent directory. On error, the caller must finish the filesystem op and
 *   rely on filesystem_start()/future ops to return to a known root.
 *
 * How it works:
 * - Open the target by LFN, chdir into it, and scan one object at a time.
 * - Files are removed with AFATFS_REMOVE_FILES_ONLY.
 * - Subdirectories are opened by their short alias and processed depth-first.
 * - Once a directory scan is empty, chdir to its parent and remove that now
 *   empty directory with AFATFS_REMOVE_EMPTY_DIRECTORIES.
 *
 * Why this lives in filesystem.c:
 * asyncfatfs exposes single-directory primitives only. It can remove an empty
 * directory entry and free its cluster chain, but it intentionally does not
 * recurse through children. Directory-shaped product saves, including Kit Save
 * today and Scene/Morph-style writers later, need the higher-level overwrite
 * policy: delete the whole old tree, then write one clean replacement tree.
 *
 * Limits:
 * FS_DELETE_DEPTH_MAX bounds recursion so corrupted or host-created deep trees
 * cannot consume unbounded firmware state. Kit saves should only need depth 2
 * (/Kit/NNN Name/member files); deeper user-created contents are deleted up to
 * this bound and otherwise turn into a filesystem error screen.
 *
 * Affiliates/clients: filesystem_saveKitDirectory_tick(), future directory
 * save operations, afatfs_findNextObject(), afatfs_removeObjects_lfn(), and
 * asyncfatfs AFATFS_REMOVE_EMPTY_DIRECTORIES.
 */
static void filesystem_deleteTreeStartWithOpenName(const char *display_name,
                                                   const char *open_name)
{
    memset(op_delete_tree_name_stack, 0, sizeof(op_delete_tree_name_stack));
    memset(op_delete_tree_open_name_stack, 0,
           sizeof(op_delete_tree_open_name_stack));
    memset(op_delete_tree_child_name, 0, sizeof(op_delete_tree_child_name));
    memset(op_delete_tree_child_open_name, 0,
           sizeof(op_delete_tree_child_open_name));
    op_delete_tree_depth = 0u;
    op_delete_tree_child_kind = AFATFS_OBJECT_NONE;
    op_delete_tree_dir = NULL;
    filesystem_copyLongComponent(op_delete_tree_name_stack[0],
                                 sizeof(op_delete_tree_name_stack[0]),
                                 display_name);
    /*
     * Preserve the exact top-level SFN alias when the caller discovered one.
     *
     * Inputs: display_name is the visible name used for diagnostics and legacy
     * LFN fallback. open_name is optional; slot cleanup passes the shortName
     * from the afatfsObjectInfo_t it just matched. Output: depth zero carries
     * both names, so OPEN_TARGET can enter that physical directory by alias and
     * REMOVE_EMPTY_DIR can later retire the same alias. This is what prevents a
     * damaged Bank folder containing duplicate `SS Name` children from being
     * re-resolved by display name and leaving or deleting the wrong sibling.
     */
    filesystem_copyLongComponent(op_delete_tree_open_name_stack[0],
                                 sizeof(op_delete_tree_open_name_stack[0]),
                                 open_name);
    op_delete_tree_phase = FS_DELETE_TREE_OPEN_TARGET;
}

static uint8_t filesystem_directoryObjectMatchesSlot(
        const afatfsObjectInfo_t *object,
        uint16_t slot,
        uint8_t allow_short_alias,
        uint8_t bank_scene_namespace)
{
    uint16_t parsed_slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Decide whether one child directory is eligible for slot replacement.
     *
     * Inputs: asyncfatfs object info from the current parent directory, the
     * requested slot, whether old compact 8.3 aliases may match by their first
     * three short-name digits, and whether the parent is a Bank-local Scene
     * namespace. Output: nonzero only for directories that belong to the exact
     * requested numeric slot in the correct namespace.
     *
     * Critical distinction: root Kit/Scene/Bank folders use `NNN Name`, but
     * Bank-local Scene folders use `NN Name`. Using the root parser for Bank
     * children misses `01 Slak2`, leaves the old directory alive, and lets a
     * later create path produce duplicate visible child folders on FAT cards.
     *
     * Why the short-alias flag exists: root Kit Save historically had to clean
     * host/firmware aliases such as "001SLA~1" whose visible component may not
     * satisfy the preferred "001 Slak" grammar. Scene Save is newer and more
     * dangerous because a Scene tree contains nested Kit/Pattern/Effect
     * directories; it therefore passes allow_short_alias=0 and deletes only
     * visible names that parse as numbered Scene folders. The safe failure mode
     * for Scene is leaving an odd alias behind, never recursing into a
     * directory that was not visibly a same-slot Scene.
     */
    if (!object || object->id.kind != AFATFS_OBJECT_DIRECTORY)
        return 0u;
    if (bank_scene_namespace) {
        uint8_t bank_slot;

        /*
         * Match Bank child Scene folders with the two-digit parser only.
         *
         * Inputs: object display component from inside `/Bank/NNN Name/`.
         * Output: a match only when it parses as `SS Name` and SS equals the
         * requested Bank-local Scene slot. Short-alias fallback is deliberately
         * ignored here because a two-digit prefix is too broad for safe deletion
         * in a mixed user-created directory.
         */
        if (storage_parseBankSceneFolder(object->id.displayName,
                                         &bank_slot,
                                         display) &&
            bank_slot == (uint8_t)slot) {
            return 1u;
        }
        return 0u;
    }
    if (storage_parseNumberedFolder(object->id.displayName,
                                    &parsed_slot,
                                    display) &&
        parsed_slot == slot) {
        return 1u;
    }
    if (!allow_short_alias)
        return 0u;
    if (object->id.shortName[0] >= '0' && object->id.shortName[0] <= '9' &&
        object->id.shortName[1] >= '0' && object->id.shortName[1] <= '9' &&
        object->id.shortName[2] >= '0' && object->id.shortName[2] <= '9') {
        parsed_slot = (uint16_t)(
            (uint16_t)(object->id.shortName[0] - '0') * 100u +
            (uint16_t)(object->id.shortName[1] - '0') * 10u +
            (uint16_t)(object->id.shortName[2] - '0'));
        return (uint8_t)(parsed_slot == slot);
    }
    return 0u;
}

static uint16_t filesystem_interpolateMorphEndpoint(uint16_t normal,
                                                    uint16_t morph,
                                                    uint8_t amount)
{
    int32_t numerator;

    /*
     * Match storageTypes' Morph Save endpoint interpolation for kitset fields.
     *
     * Kit member Instrument files delegate Morph Save projection to
     * storage_formatInstrumentLineView(). The generated slot-6/track-7 bridge
     * lives in kitset.kcg instead of an Instrument descriptor table, so it
     * needs the same tiny endpoint interpolation here.
     */
    if (amount == 0u)
        return normal;
    if (amount == 255u)
        return morph;
    numerator = (int32_t)normal * 255 +
                ((int32_t)morph - (int32_t)normal) * amount;
    numerator += 127;
    if (numerator < 0)
        return 0u;
    return (uint16_t)(numerator / 255);
}

static void filesystem_deleteSlotDirectoriesStart(uint16_t slot,
                                                  uint8_t allow_short_alias,
                                                  uint8_t bank_scene_namespace)
{
    /*
     * Start same-slot directory cleanup in the current parent directory.
     *
     * Inputs: caller must already be chdir'd into the parent directory that
     * owns the numbered children; slot is the exact 000..999 root slot or
     * Bank-local 00..15 child number to replace. allow_short_alias controls
     * whether the scan may match legacy compact 8.3 aliases by short-name
     * digits. bank_scene_namespace selects the two-digit Bank child parser.
     * Output: the delete-slot state machine is reset and will repeatedly scan
     * the current parent, recursively delete one matching child, return to this
     * parent, and rescan until no same-slot directory remains.
     *
     * Safety: this function never receives a path to delete. It only deletes
     * children discovered by filesystem_directoryObjectMatchesSlot(), so Scene
     * Save can opt out of short-alias matching and avoid deleting anything
     * except visibly numbered Scene folders for the target slot.
     */
    op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;
    op_delete_slot_dir = NULL;
    op_delete_slot_number = slot;
    op_delete_slot_allow_short_alias = allow_short_alias;
    op_delete_slot_bank_scene = bank_scene_namespace;
    op_delete_slot_phase = FS_DELETE_SLOT_OPEN_SCAN;
}

static void filesystem_deleteKitSlotDirectoriesStart(void)
{
    /*
     * Kit Save cleanup allows short-alias fallback.
     *
     * Older Kit cards may contain same-slot directories whose visible name is
     * only an 8.3 alias such as 001SLA~1. Matching those by their first three
     * digits is acceptable inside /Kit because the product tree contains only
     * Kit directories and member files.
     */
    filesystem_deleteSlotDirectoriesStart(op_slot, 1u, 0u);
}

static void filesystem_deleteSceneSlotDirectoriesStart(void)
{
    /*
     * Scene Save cleanup forbids short-alias fallback.
     *
     * The current parent must be /Scene. Only children whose visible display
     * name parses as "NNN Name" for op_slot are recursively deleted. This keeps
     * replacement scoped to the requested Scene slot no matter how many nested
     * directories exist inside other Scene folders.
     */
    filesystem_deleteSlotDirectoriesStart(op_slot, 0u, 0u);
}

static uint32_t op_delete_slot_timeout_ticks = 0;
static uint8_t op_delete_slot_last_phase = 0;

static fs_status_t filesystem_deleteKitSlotDirectories_tick(void)
{
    fs_status_t delete_status;

    if (op_delete_slot_phase != op_delete_slot_last_phase) {
        op_delete_slot_last_phase = op_delete_slot_phase;
        op_delete_slot_timeout_ticks = 0;
    }
    op_delete_slot_timeout_ticks++;
    if (op_delete_slot_timeout_ticks > 50000) {
        uint8_t subphase = afatfs_getDeleteTreePhase();
        if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH && subphase != 0xFF) {
            filesystem_makeNamedErrorCode("TDel", subphase);
        } else {
            filesystem_makeNamedErrorCode("TOut", (uint8_t)op_delete_slot_phase);
        }
        op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
        return FS_STATUS_ERROR;
    }

    for (;;) {
        switch (op_delete_slot_phase) {
        case FS_DELETE_SLOT_IDLE:
        case FS_DELETE_SLOT_DONE:
            return FS_STATUS_DONE;
        case FS_DELETE_SLOT_ERROR:
            return FS_STATUS_ERROR;

        case FS_DELETE_SLOT_OPEN_SCAN:
            op_file_ready = false;
            op_file = NULL;
            if (!afatfs_fopen(".", "r", on_file_opened))
                return FS_STATUS_BUSY;
            op_delete_slot_phase = FS_DELETE_SLOT_WAIT_SCAN;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_WAIT_SCAN:
            if (!op_file_ready)
                return FS_STATUS_BUSY;
            if (!op_file) {
                filesystem_makeNamedErrorCode(
                    "KDel", (uint8_t)op_delete_slot_phase);
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_dir = op_file;
            afatfs_findFirstObject(op_delete_slot_dir, &op_object_finder);
            op_delete_slot_phase = FS_DELETE_SLOT_SCAN_NEXT;
            break;

        case FS_DELETE_SLOT_SCAN_NEXT:
        {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_delete_slot_dir,
                                      &op_object_finder,
                                      &op_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return FS_STATUS_BUSY;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                filesystem_makeNamedErrorCode(
                    "KDel", (uint8_t)op_delete_slot_phase);
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            if (op_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            if (filesystem_directoryObjectMatchesSlot(
                    &op_object,
                    op_delete_slot_number,
                    op_delete_slot_allow_short_alias,
                    op_delete_slot_bank_scene)) {
                op_delete_slot_target_id = op_object.id;
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            return FS_STATUS_BUSY;
        }

        case FS_DELETE_SLOT_CLOSE_SCAN:
            op_close_done = false;
            if (!afatfs_fclose(op_delete_slot_dir, on_file_closed))
                return FS_STATUS_BUSY;
            op_delete_slot_phase = FS_DELETE_SLOT_WAIT_CLOSE_SCAN;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_WAIT_CLOSE_SCAN:
            if (!op_close_done)
                return FS_STATUS_BUSY;
            op_delete_slot_dir = NULL;
            if (op_delete_slot_target_id.kind == AFATFS_OBJECT_NONE) {
                op_delete_slot_phase = FS_DELETE_SLOT_DONE;
                return FS_STATUS_DONE;
            }
            /*
             * Delete the concrete directory object discovered by the scan.
             *
             * Inputs: op_delete_slot_target_name is the visible name, while
             * op_delete_slot_target_open_name is the matching object's exact
             * short alias captured from afatfsObjectInfo_t. Output: recursive
             * deletion opens and finally removes that physical alias. This is
             * essential when repairing a Bank folder that already has duplicate
             * `SS Name` children from an interrupted or pre-fix save.
             */
            op_delete_tree_done = false;
            if (!afatfs_deleteTree(&op_delete_slot_target_id, on_delete_tree_complete)) {
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_phase = FS_DELETE_SLOT_DELETE_MATCH;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_DELETE_MATCH:
            if (!op_delete_tree_done)
                return FS_STATUS_BUSY;
            if (op_delete_tree_result != AFATFS_RESULT_OK) {
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;
            op_delete_slot_phase = FS_DELETE_SLOT_OPEN_SCAN;
            break;
        }
    }
}

static fs_status_t filesystem_deleteTree_tick(void)
{
    for (;;) {
        switch (op_delete_tree_phase) {
        case FS_DELETE_TREE_IDLE:
        case FS_DELETE_TREE_DONE:
            return FS_STATUS_DONE;
        case FS_DELETE_TREE_ERROR:
            return FS_STATUS_ERROR;

        case FS_DELETE_TREE_OPEN_TARGET:
            op_file_ready = false;
            op_file = NULL;
            if (op_delete_tree_open_name_stack[0][0] != '\0') {
                /*
                 * Open by the exact SFN alias when the caller supplied one.
                 *
                 * Input: slot cleanup captured op_object.id.shortName for the
                 * same directory it matched. Output: the recursive deleter
                 * enters that physical directory instead of asking the LFN
                 * matcher to choose among duplicate visible names.
                 */
                if (!afatfs_opendir(op_delete_tree_open_name_stack[0],
                                    on_file_opened)) {
                    return FS_STATUS_BUSY;
                }
            } else {
                if (!afatfs_opendir_lfn(op_delete_tree_name_stack[0],
                                        AFATFS_MATCH_CASE_INSENSITIVE,
                                        op_delete_tree_child_open_name,
                                        on_file_opened)) {
                    return FS_STATUS_BUSY;
                }
            }
            op_delete_tree_phase = FS_DELETE_TREE_WAIT_TARGET;
            return FS_STATUS_BUSY;

        case FS_DELETE_TREE_WAIT_TARGET:
            if (!op_file_ready)
                return FS_STATUS_BUSY;
            if (!op_file) {
                op_delete_tree_phase = FS_DELETE_TREE_DONE;
                return FS_STATUS_DONE;
            }
            if (op_delete_tree_open_name_stack[0][0] == '\0') {
                /*
                 * LFN-open callers did not know the exact alias up front.
                 *
                 * afatfs_opendir_lfn() returned the concrete open component it
                 * selected. Store it now so REMOVE_EMPTY_DIR still retires the
                 * same entry that this delete tree entered.
                 */
                filesystem_copyLongComponent(
                    op_delete_tree_open_name_stack[0],
                    sizeof(op_delete_tree_open_name_stack[0]),
                    op_delete_tree_child_open_name);
            }
            op_delete_tree_dir = op_file;
            if (!afatfs_chdir(op_delete_tree_dir))
                return FS_STATUS_BUSY;
            op_delete_tree_phase = FS_DELETE_TREE_CLOSE_TARGET;
            break;

        case FS_DELETE_TREE_CLOSE_TARGET:
            op_close_done = false;
            if (!afatfs_fclose(op_delete_tree_dir, on_file_closed))
                return FS_STATUS_BUSY;
            op_delete_tree_phase = FS_DELETE_TREE_CLOSE_CHILD_DIR;
            return FS_STATUS_BUSY;

        case FS_DELETE_TREE_CLOSE_CHILD_DIR:
            if (!op_close_done)
                return FS_STATUS_BUSY;
            op_delete_tree_dir = NULL;
            op_delete_tree_phase = FS_DELETE_TREE_OPEN_SCAN;
            break;

        case FS_DELETE_TREE_OPEN_SCAN:
            op_file_ready = false;
            op_file = NULL;
            if (!afatfs_fopen(".", "r", on_file_opened))
                return FS_STATUS_BUSY;
            op_delete_tree_phase = FS_DELETE_TREE_WAIT_SCAN;
            return FS_STATUS_BUSY;

        case FS_DELETE_TREE_WAIT_SCAN:
            if (!op_file_ready)
                return FS_STATUS_BUSY;
            if (!op_file) {
                filesystem_makeNamedErrorCode(
                    "Del", (uint8_t)op_delete_tree_phase);
                op_delete_tree_phase = FS_DELETE_TREE_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_tree_dir = op_file;
            afatfs_findFirstObject(op_delete_tree_dir, &op_object_finder);
            op_delete_tree_phase = FS_DELETE_TREE_SCAN_NEXT;
            break;

        case FS_DELETE_TREE_SCAN_NEXT:
        {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_delete_tree_dir,
                                      &op_object_finder,
                                      &op_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return FS_STATUS_BUSY;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_delete_tree_dir, &op_object_finder);
                filesystem_makeNamedErrorCode(
                    "Del", (uint8_t)op_delete_tree_phase);
                op_delete_tree_phase = FS_DELETE_TREE_ERROR;
                return FS_STATUS_ERROR;
            }
            if (op_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_delete_tree_dir, &op_object_finder);
                op_delete_tree_child_kind = AFATFS_OBJECT_NONE;
            } else {
                filesystem_copyLongComponent(op_delete_tree_child_name,
                                             sizeof(op_delete_tree_child_name),
                                             op_object.id.displayName);
                filesystem_copyLongComponent(
                    op_delete_tree_child_open_name,
                    sizeof(op_delete_tree_child_open_name),
                    op_object.id.shortName);
                op_delete_tree_child_kind = op_object.id.kind;
                afatfs_findLastObject(op_delete_tree_dir, &op_object_finder);
            }
            op_delete_tree_phase = FS_DELETE_TREE_CLOSE_SCAN_BEFORE_CHILD;
            break;
        }

        case FS_DELETE_TREE_CLOSE_SCAN_BEFORE_CHILD:
            op_close_done = false;
            if (!afatfs_fclose(op_delete_tree_dir, on_file_closed))
                return FS_STATUS_BUSY;
            op_delete_tree_phase = FS_DELETE_TREE_HANDLE_CHILD;
            return FS_STATUS_BUSY;

        case FS_DELETE_TREE_HANDLE_CHILD:
            if (!op_close_done)
                return FS_STATUS_BUSY;
            op_delete_tree_dir = NULL;
            if (op_delete_tree_child_kind == AFATFS_OBJECT_FILE) {
                op_remove_done = 0u;
                if (!afatfs_removeObjects_lfn(op_delete_tree_child_name,
                                              AFATFS_MATCH_CASE_INSENSITIVE,
                                              AFATFS_REMOVE_FILES_ONLY,
                                              on_remove_complete)) {
                    return FS_STATUS_BUSY;
                }
                op_delete_tree_phase = FS_DELETE_TREE_WAIT_FILE_REMOVE;
                return FS_STATUS_BUSY;
            }
            if (op_delete_tree_child_kind == AFATFS_OBJECT_DIRECTORY) {
                if (op_delete_tree_depth + 1u >= FS_DELETE_DEPTH_MAX) {
                    filesystem_makeNamedErrorCode(
                        "Del", (uint8_t)op_delete_tree_phase);
                    op_delete_tree_phase = FS_DELETE_TREE_ERROR;
                    return FS_STATUS_ERROR;
                }
                op_file_ready = false;
                op_file = NULL;
                if (!afatfs_opendir(op_delete_tree_child_open_name,
                                    on_file_opened)) {
                    return FS_STATUS_BUSY;
                }
                op_delete_tree_phase = FS_DELETE_TREE_WAIT_CHILD_DIR;
                return FS_STATUS_BUSY;
            }
            op_delete_tree_phase = FS_DELETE_TREE_OPEN_PARENT;
            break;

        case FS_DELETE_TREE_WAIT_FILE_REMOVE:
            if (!op_remove_done)
                return FS_STATUS_BUSY;
            op_delete_tree_phase = FS_DELETE_TREE_OPEN_SCAN;
            break;

        case FS_DELETE_TREE_WAIT_CHILD_DIR:
            if (!op_file_ready)
                return FS_STATUS_BUSY;
            if (!op_file) {
                filesystem_makeNamedErrorCode(
                    "Del", (uint8_t)op_delete_tree_phase);
                op_delete_tree_phase = FS_DELETE_TREE_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_tree_dir = op_file;
            if (!afatfs_chdir(op_delete_tree_dir))
                return FS_STATUS_BUSY;
            op_delete_tree_depth++;
            filesystem_copyLongComponent(
                op_delete_tree_name_stack[op_delete_tree_depth],
                sizeof(op_delete_tree_name_stack[op_delete_tree_depth]),
                op_delete_tree_child_name);
            /*
             * Keep the nested directory's exact alias beside its display name.
             *
             * Input: SCAN_NEXT copied the child afatfsObjectInfo_t.shortName
             * before opening it. Output: after children are removed and the
             * state climbs back to the parent, REMOVE_EMPTY_DIR can delete the
             * exact nested directory that was just emptied.
             */
            filesystem_copyLongComponent(
                op_delete_tree_open_name_stack[op_delete_tree_depth],
                sizeof(op_delete_tree_open_name_stack[op_delete_tree_depth]),
                op_delete_tree_child_open_name);
            op_delete_tree_phase = FS_DELETE_TREE_CLOSE_CHILD_DIR;
            break;

        case FS_DELETE_TREE_OPEN_PARENT:
        {
            afatfsOperationStatus_e st = afatfs_chdirParent();
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return FS_STATUS_BUSY;
            if (st == AFATFS_OPERATION_FAILURE) {
                filesystem_makeNamedErrorCode(
                    "Del", (uint8_t)op_delete_tree_phase);
                op_delete_tree_phase = FS_DELETE_TREE_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_tree_phase = FS_DELETE_TREE_REMOVE_EMPTY_DIR;
            break;
        }

        case FS_DELETE_TREE_WAIT_PARENT:
        case FS_DELETE_TREE_CLOSE_PARENT:
            op_delete_tree_phase = FS_DELETE_TREE_ERROR;
            return FS_STATUS_ERROR;

        case FS_DELETE_TREE_REMOVE_EMPTY_DIR:
            op_delete_tree_dir = NULL;
            op_remove_done = 0u;
            if (op_delete_tree_open_name_stack[op_delete_tree_depth][0] !=
                '\0') {
                /*
                 * Retire the exact directory entry that this recursion level
                 * opened.
                 *
                 * Inputs: current directory is the parent, and the stack entry
                 * is the printable SFN alias for the emptied child. Output:
                 * asyncfatfs removes only that physical object. This is the
                 * duplicate-LFN guard needed for Bank overwrite recovery: a
                 * damaged `01 Slak2` sibling cannot intercept the removal.
                 */
                if (!afatfs_removeObject(
                        op_delete_tree_open_name_stack[op_delete_tree_depth],
                        AFATFS_REMOVE_EMPTY_DIRECTORIES,
                        on_remove_complete)) {
                    return FS_STATUS_BUSY;
                }
            } else {
                if (!afatfs_removeObjects_lfn(
                        op_delete_tree_name_stack[op_delete_tree_depth],
                        AFATFS_MATCH_CASE_INSENSITIVE,
                        AFATFS_REMOVE_EMPTY_DIRECTORIES,
                        on_remove_complete)) {
                    return FS_STATUS_BUSY;
                }
            }
            op_delete_tree_phase = FS_DELETE_TREE_WAIT_REMOVE_EMPTY_DIR;
            return FS_STATUS_BUSY;

        case FS_DELETE_TREE_WAIT_REMOVE_EMPTY_DIR:
            if (!op_remove_done)
                return FS_STATUS_BUSY;
            if (op_delete_tree_depth == 0u) {
                op_delete_tree_phase = FS_DELETE_TREE_DONE;
                return FS_STATUS_DONE;
            }
            op_delete_tree_depth--;
            op_delete_tree_phase = FS_DELETE_TREE_OPEN_SCAN;
            break;
        }
    }
}

static void filesystem_saveKitDirectory_tick(void)
{
    const scene_t *scene = scene_getConst(op_kit_save_source_scene);
    const kit_t *kit = scene ? &scene->kit : NULL;
    fs_status_t delete_status;

    switch (op_phase) {
    case 0:
        if (!kit || op_slot >= STORAGE_KIT_MAX_SLOTS ||
            op_save_kit_dir_display_name[0] == '\0') {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_mkdir_lfn(STORAGE_ROOT_KIT,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 3u;
        return;

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        /*
         * Remove every physical directory for this numbered Kit slot before
         * creating the fresh one.
         *
         * This is stronger than deleting a cached old name plus the requested
         * new name. A card can contain duplicate same-slot folders such as
         * "003 RedSnap" and "003 Slak"; the scan cache will expose only one,
         * so overwrite must discover and delete all matching numbered folders
         * directly from /Kit/.
         */
        filesystem_deleteKitSlotDirectoriesStart();
        op_phase = 5u;
        return;

    case 5:
        delete_status = filesystem_deleteKitSlotDirectories_tick();
        if (delete_status == FS_STATUS_BUSY)
            return;
        if (delete_status == FS_STATUS_ERROR) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 8u;
        return;

    case 8:
        op_file_ready = false;
        op_file = NULL;
        memset(op_save_kit_dir_open_name, 0, sizeof(op_save_kit_dir_open_name));
        if (!afatfs_mkdir_lfn(op_save_kit_dir_display_name,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_save_kit_dir_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 9u;
        return;

    case 9:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 10u;
        return;

    case 10:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 11u;
        return;

    case 11:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(STORAGE_KITSET_FILENAME,
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 12u;
        return;

    case 12:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 13u;
        return;

    case 13:
        if (filesystem_writeTextLine(filesystem_nextKitsetLine, (void *)kit))
            return;
        op_phase = 14u;
        return;

    case 14:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15u;
        return;

    case 15:
        if (!op_close_done)
            return;
        op_file = NULL;
        op_instrument_slot = 0u;
        op_phase = 16u;
        return;

    case 16:
        if (op_instrument_slot >= STORAGE_KIT_SLOT_COUNT) {
            op_phase = 21u;
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_fopen_lfn(
                op_save_kit_member_display_file[op_instrument_slot],
                "w",
                AFATFS_MATCH_CASE_INSENSITIVE,
                op_root_open_name,
                on_file_opened)) {
            return;
        }
        op_phase = 17u;
        return;

    case 17:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 18u;
        return;

    case 18:
    {
        filesystem_instrument_write_ctx_t ctx = {{
            &kit->instruments[op_instrument_slot],
            kit->instruments[op_instrument_slot].type,
            (uint8_t)(op_instrument_slot + 1u),
            scene ? scene->settings.voice_morph_amount[op_instrument_slot] : 0u,
            op_kit_save_mode
        }};
        if (filesystem_writeTextLine(filesystem_nextInstrumentLine, &ctx))
            return;
        op_phase = 19u;
        return;
    }

    case 19:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 20u;
        return;

    case 20:
        if (!op_close_done)
            return;
        op_file = NULL;
        op_instrument_slot++;
        op_phase = 16u;
        return;

    case 21:
        if (!afatfs_chdir(NULL))
            return;
        filesystem_recordSavedKitDirectory(op_save_kit_dir_display_name,
                                           op_save_kit_dir_open_name);
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL) {
            scene_setResidentKitDisplayName(op_kit_save_source_scene,
                                            preset_currentName);
        }
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_saveBankDirectory_tick(void)
{
    if (op_bank_payload_active) {
        filesystem_saveSceneDirectory_tick();
        return;
    }

    /*
     * Save one root Bank in the initial one-Scene bridge form.
     *
     * Phases 0..12 create/open root Bank containers and write bankset.bcg.
     * Then the state jumps into the existing Scene payload writer at phase 8,
     * while still chdir'd inside `Bank/NNN Name/`. That writer creates the
     * Bank-local child `00 <scene>/` and writes sceneset, embedded Kit,
     * pattern, and effects. No phase here deletes root Bank or untoggled child
     * Scenes.
     */
    switch (op_phase) {
    case 0:
        if (!scene_getConst(op_kit_save_source_scene) ||
            op_slot >= STORAGE_BANK_MAX_SLOTS ||
            op_save_bank_dir_display_name[0] == '\0') {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_mkdir_lfn(STORAGE_ROOT_BANK,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 3u;
        return;

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        /*
         * Preflight the chosen scratch names before mkdir_lfn().
         *
         * Inputs: current directory is `/Bank/`; the request path already
         * generated paired temp/old components. Output: phases 46..48 scan
         * existing children and either prove both scratch names are absent or
         * generate a different pair. This must happen before mkdir_lfn()
         * because that API intentionally opens an existing directory instead
         * of failing on same-name collision.
         */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 46u;
        return;

    case 46:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        afatfs_findFirstObject(op_file, &op_object_finder);
        op_save_bank_scratch_collision = 0u;
        op_phase = 47u;
        return;

    case 47:
    {
        afatfsOperationStatus_e ast =
            afatfs_findNextObject(op_file, &op_object_finder, &op_object);
        if (ast == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (ast == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_file, &op_object_finder);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_file, &op_object_finder);
            op_save_bank_scratch_collision = 0u;
            op_phase = 48u;
            return;
        }
        if (filesystem_bankScratchNameCollides(&op_object)) {
            /*
             * Found a stale scratch component.
             *
             * Inputs: the currently selected temp or old name is already a
             * visible /Bank child. Output: close the scan handle and let phase
             * 48 generate a new pair. The scan stops immediately because one
             * collision is enough to make this pair unsafe for create-or-open.
             */
            afatfs_findLastObject(op_file, &op_object_finder);
            op_save_bank_scratch_collision = 1u;
            op_phase = 48u;
            return;
        }
        return;
    }

    case 48:
        /*
         * Close the scratch preflight scan handle.
         *
         * Inputs: op_file is the temporary "." directory handle used only for
         * /Bank enumeration. Output: phase 50 can safely inspect the latched
         * collision flag after asyncfatfs has released the handle.
         */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 50u;
        return;

    case 50:
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_save_bank_scratch_collision) {
            uint16_t scratch_nonce;

            if (op_save_bank_scratch_attempts++ >= 16u) {
                filesystem_makeNamedErrorCode("BTmp", 48u);
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * Retry with a different paired scratch suffix.
             *
             * Inputs: a real /Bank child collided with either the temp or old
             * scratch component. Output: both names are regenerated together
             * and phase 4 starts a fresh scan from the top of /Bank. The retry
             * loop avoids recursive deletion while still guaranteeing that the
             * writer never opens a stale temp folder.
             */
            scratch_nonce = filesystem_nextBankScratchNonce();
            filesystem_prepareBankScratchDirs(scratch_nonce);
            op_phase = 4u;
            return;
        }
        op_phase = 49u;
        return;

    case 49:
        /*
         * Create a non-numbered staging Bank directory.
         *
         * Inputs: current directory is `/Bank/`; op_save_bank_tmp_display_name
         * is a request-unique name such as `tmp000-4a3f`, not a loadable
         * `NNN Name` Bank slot. Output: phases 5..12 write bankset.bcg and all
         * selected Bank-local Scenes into this temp folder first.
         *
         * Why: in-place Bank overwrite has repeatedly merged stale child
         * Scenes and duplicate Instrument files into the numbered Bank folder.
         * The numbered `000 Slak` slot should appear only after a complete temp
         * payload exists. Promotion below renames the old numbered Bank aside
         * and renames this temp folder into the final numbered slot.
         *
         * Accessing code: filesystem_requestSaveBank() captures final/temp/old
         * names, filesystem_saveSceneDirectory_tick() writes child payloads
         * under the temp folder, and promotion phases 39..45 publish it.
         */
        op_file_ready = false;
        op_file = NULL;
        memset(op_save_bank_dir_open_name, 0,
               sizeof(op_save_bank_dir_open_name));
        if (!afatfs_mkdir_lfn(op_save_bank_tmp_display_name,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_save_bank_dir_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 5u;
        return;

    case 5:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 6u;
        return;

    case 6:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 7u;
        return;

    case 7:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(STORAGE_BANKSET_FILENAME,
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 8u;
        return;

    case 8:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 9u;
        return;

    case 9:
        if (filesystem_writeTextLine(filesystem_nextBanksetLine, NULL))
            return;
        op_phase = 10u;
        return;

    case 10:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 11u;
        return;

    case 11:
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_bank_scene_save_mask == 0u) {
            if (!afatfs_chdir(NULL))
                return;
            op_phase = 39u;
            return;
        }
        for (op_bank_child_cursor = 0u;
             op_bank_child_cursor < STORAGE_BANK_SCENE_MAX_SLOTS;
             op_bank_child_cursor++) {
            if ((op_bank_scene_save_mask &
                 (uint16_t)(1u << op_bank_child_cursor)) != 0u) {
                /*
                 * Write the selected Scene into the temp Bank folder.
                 *
                 * Input: op_bank_child_cursor is both resident Scene index and
                 * two-digit Bank-local child number. Output: phase 20 prepares
                 * the per-child Scene writer and delegates to the existing
                 * Scene payload save. No recursive child cleanup is needed
                 * here because the parent is a unique non-numbered temp folder.
                 */
                op_phase = 20u;
                return;
            }
        }
        filesystem_finish(FS_STATUS_ERROR);
        return;

    case 12:
    {
        uint8_t child_slot;

        for (child_slot = (uint8_t)(op_bank_child_cursor + 1u);
             child_slot < STORAGE_BANK_SCENE_MAX_SLOTS;
             child_slot++) {
            if ((op_bank_scene_save_mask &
                 (uint16_t)(1u << child_slot)) != 0u) {
                op_bank_child_cursor = child_slot;
                op_phase = 13u;
                return;
            }
        }
        op_phase = 39u;
        return;
    }

    case 13:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_BANK,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 14u;
        return;

    case 14:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 15u;
        return;

    case 15:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 16u;
        return;

    case 16:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        /*
         * Reopen the just-created Bank directory by its short open component.
         *
         * Inputs: op_save_bank_dir_open_name was captured from mkdir_lfn() when
         * the temp Bank folder was created/opened. Output: the temp Bank
         * directory handle is restored after a child Scene payload returned to
         * root.
         * This must use afatfs_opendir(), not afatfs_opendir_lfn(): the LFN
         * opener compares display names, while this scratch value is the 8.3
         * open alias. Using the LFN opener here produced ERR BnkS11 after the
         * first child Scene save.
         */
        if (!afatfs_opendir(op_save_bank_dir_open_name,
                            on_file_opened)) {
            return;
        }
        op_phase = 17u;
        return;

    case 17:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 18u;
        return;

    case 18:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 19u;
        return;

    case 19:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        op_phase = 20u;
        return;

    case 20:
    {
        /*
         * Prepare and write one Bank-local child into the temp Bank folder.
         *
         * Inputs: current directory is the unique temp Bank directory and
         * op_bank_child_cursor selects the resident Scene slot. Output: the
         * existing Scene writer starts at phase 8 and creates `SS Name/` with
         * sceneset, embedded Kit, pattern, and effects. Because the temp parent
         * is new, this path deliberately avoids recursive delete and cannot
         * merge stale Instrument files into the child.
         */
        if (!filesystem_prepareBankSceneSaveSource(op_bank_child_cursor)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_bank_payload_active = 1u;
        op_phase = 8u;
        return;
    }

    case 39:
        /*
         * Begin temp Bank promotion from filesystem root.
         *
         * Inputs: a complete temp Bank tree has been written under `/Bank/`
         * using op_save_bank_tmp_display_name. Output: phase 40 opens `/Bank/`
         * so the following rename operations happen among root Bank children.
         */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 40u;
        return;

    case 40:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_BANK,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 41u;
        return;

    case 41:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 42u;
        return;

    case 42:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 43u;
        return;

    case 43:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        /*
         * Move the old numbered Bank out of the loadable namespace.
         *
         * Inputs: current directory is `/Bank/`; op_save_bank_dir_display_name
         * is the final `NNN Name` slot and op_save_bank_old_display_name is a
         * non-numbered `old...` component. Output: if the old Bank exists, it
         * is renamed out of the numbered namespace without recursing through
         * its children. If it does not exist, asyncfatfs completes with an
         * empty open-name buffer and phase 45 still attempts temp promotion.
         *
         * Why: deleting a damaged old Bank tree recursively has been the
         * failure point. Renaming the old directory preserves its contents for
         * manual cleanup while making it invisible to root Bank scans.
         */
        op_rename_done = 0u;
        memset(op_save_bank_rename_open_name, 0,
               sizeof(op_save_bank_rename_open_name));
        if (!afatfs_renameObject_lfn(op_save_bank_dir_display_name,
                                     op_save_bank_old_display_name,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     op_save_bank_rename_open_name,
                                     on_rename_complete)) {
            return;
        }
        op_phase = 44u;
        return;

    case 44:
        if (!op_rename_done)
            return;
        /*
         * Publish the complete temp Bank as the numbered slot.
         *
         * Inputs: temp tree name from filesystem_requestSaveBank() and final
         * `NNN Name` display name. Output: op_save_bank_rename_open_name
         * receives the final asyncfatfs-openable alias on success. If this
         * buffer stays empty, the promotion did not happen and the numbered
         * Bank is not trustworthy, so the save reports an error instead of
         * silently leaving the menu on a broken slot.
         */
        op_rename_done = 0u;
        memset(op_save_bank_rename_open_name, 0,
               sizeof(op_save_bank_rename_open_name));
        if (!afatfs_renameObject_lfn(op_save_bank_tmp_display_name,
                                     op_save_bank_dir_display_name,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     op_save_bank_rename_open_name,
                                     on_rename_complete)) {
            return;
        }
        op_phase = 45u;
        return;

    case 45:
        if (!op_rename_done)
            return;
        if (op_save_bank_rename_open_name[0] == '\0') {
            filesystem_makeNamedErrorCode("BProm", 45u);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        storage_copyFilename(op_save_bank_dir_open_name,
                             op_save_bank_rename_open_name);
        if (!afatfs_chdir(NULL))
            return;
        filesystem_recordSavedBankDirectory(op_save_bank_dir_display_name,
                                            op_save_bank_dir_open_name);
        bank_setDisplayName(op_bank_display_name);
        bank_setScenePresentMask(op_bank_scene_save_mask);
        bank_selectActiveSceneForEditMask(op_bank_active_scene);
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
        bank_setRestoreBankSlot(op_slot);
        bank_setHasResidentBank(1u);
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_saveSceneDirectory_tick(void)
{
    const scene_t *scene = scene_getConst(op_kit_save_source_scene);
    const kit_t *kit = scene ? &scene->kit : NULL;
    fs_status_t delete_status;

    /*
     * Save one complete root Scene directory.
     *
     * Inputs were captured by filesystem_requestSaveSceneDirectory(): target
     * root Scene slot, source resident Scene, display Scene name, embedded Kit
     * directory name, and six generated member filenames. Outputs are a clean
     * Scene/<NNN Name>/ tree containing sceneset.scg, Kit <name>/kitset.kcg,
     * six Instrument files, pattern.pat, and effects.fx.
     *
     * The state machine intentionally mirrors Kit Save where possible. The
     * important extra loop is the child-file sequence after sceneset.scg: write
     * the embedded Kit while chdir'd into its directory, climb back to the
     * Scene directory, then write the two placeholder files.
     */
    switch (op_phase) {
    case 0:
        if (!scene || !kit || op_slot >= STORAGE_SCENE_MAX_SLOTS ||
            op_save_kit_dir_display_name[0] == '\0' ||
            op_save_scene_kit_display_name[0] == '\0') {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_mkdir_lfn(STORAGE_ROOT_SCENE,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 3u;
        return;

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        /*
         * Remove every same-number Scene directory in /Scene/.
         *
         * The start wrapper is Scene-specific even though the tick worker is
         * shared with Kit Save. It disables 8.3 short-alias fallback and
         * accepts only visible child names parsed by storage_parseNumberedFolder()
         * as the exact requested op_slot. Output: recursive deletion is scoped
         * to same-slot Scene directories under /Scene/, never to /Scene itself
         * or to other numbered Scene children, regardless of nested Kit,
         * pattern, or effect subdirectories inside those other Scenes.
         */
        filesystem_deleteSceneSlotDirectoriesStart();
        op_phase = 5u;
        return;

    case 5:
        delete_status = filesystem_deleteKitSlotDirectories_tick();
        if (delete_status == FS_STATUS_BUSY)
            return;
        if (delete_status == FS_STATUS_ERROR) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 8u;
        return;

    case 8:
        op_file_ready = false;
        op_file = NULL;
        memset(op_save_kit_dir_open_name, 0, sizeof(op_save_kit_dir_open_name));
        if (!afatfs_mkdir_lfn(op_save_kit_dir_display_name,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_save_kit_dir_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 9u;
        return;

    case 9:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 10u;
        return;

    case 10:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 11u;
        return;

    case 11:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(STORAGE_SCENESET_FILENAME,
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 12u;
        return;

    case 12:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 13u;
        return;

    case 13:
        if (filesystem_writeTextLine(filesystem_nextScenesetLine,
                                     (void *)scene))
            return;
        op_phase = 14u;
        return;

    case 14:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15u;
        return;

    case 15:
        if (!op_close_done)
            return;
        op_file = NULL;
        op_file_ready = false;
        op_file = NULL;
        memset(op_save_scene_kit_open_name, 0,
               sizeof(op_save_scene_kit_open_name));
        if (!afatfs_mkdir_lfn(op_save_scene_kit_display_name,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_save_scene_kit_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 16u;
        return;

    case 16:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_slot_dir = op_file;
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 17u;
        return;

    case 17:
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 18u;
        return;

    case 18:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(STORAGE_KITSET_FILENAME,
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 19u;
        return;

    case 19:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 20u;
        return;

    case 20:
        if (filesystem_writeTextLine(filesystem_nextKitsetLine, (void *)kit))
            return;
        op_phase = 21u;
        return;

    case 21:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 22u;
        return;

    case 22:
        if (!op_close_done)
            return;
        op_file = NULL;
        op_instrument_slot = 0u;
        op_phase = 23u;
        return;

    case 23:
        if (op_instrument_slot >= STORAGE_KIT_SLOT_COUNT) {
            op_phase = 28u;
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_fopen_lfn(
                op_save_kit_member_display_file[op_instrument_slot],
                "w",
                AFATFS_MATCH_CASE_INSENSITIVE,
                op_root_open_name,
                on_file_opened)) {
            return;
        }
        op_phase = 24u;
        return;

    case 24:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 25u;
        return;

    case 25:
    {
        filesystem_instrument_write_ctx_t ctx = {{
            &kit->instruments[op_instrument_slot],
            kit->instruments[op_instrument_slot].type,
            (uint8_t)(op_instrument_slot + 1u),
            scene->settings.voice_morph_amount[op_instrument_slot],
            STORAGE_INSTRUMENT_SAVE_NORMAL
        }};
        if (filesystem_writeTextLine(filesystem_nextInstrumentLine, &ctx))
            return;
        op_phase = 26u;
        return;
    }

    case 26:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 27u;
        return;

    case 27:
        if (!op_close_done)
            return;
        op_file = NULL;
        op_instrument_slot++;
        op_phase = 23u;
        return;

    case 28:
    {
        afatfsOperationStatus_e st = afatfs_chdirParent();
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 29u;
        return;
    }

    case 29:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn("pattern.pat",
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 30u;
        return;

    case 30:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 31u;
        return;

    case 31:
        if (filesystem_writeTextLine(filesystem_nextPatternStubLine,
                                     (void *)&scene->pattern))
            return;
        op_phase = 32u;
        return;

    case 32:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 33u;
        return;

    case 33:
        if (!op_close_done)
            return;
        op_file = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn("effects.fx",
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 34u;
        return;

    case 34:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 35u;
        return;

    case 35:
        if (filesystem_writeTextLine(filesystem_nextEffectPlaceholderLine, NULL))
            return;
        op_phase = 36u;
        return;

    case 36:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 37u;
        return;

    case 37:
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        if (current_op == FS_INTERNAL_OP_SAVE_BANK) {
            /*
             * Return one completed Bank-local Scene payload to the Bank loop.
             *
             * Inputs: we just wrote `Bank/NNN Name/SS Scene/...` for the
             * current op_bank_child_cursor. Output: the root filesystem is
             * restored, op_bank_payload_active is cleared, and op_phase moves
             * to the Bank save cursor. Bank cache/identity are committed only
             * after every selected child has succeeded.
             */
            op_bank_payload_active = 0u;
            op_phase = 12u;
            return;
        } else {
            filesystem_recordSavedSceneDirectory(op_save_kit_dir_display_name,
                                                 op_save_kit_dir_open_name);
            scene_setSceneDisplayName(op_kit_save_source_scene,
                                      op_scene_display_name);
        }
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** SAVE PATTERN state machine
**
** The `.pat` payload mirrors the original AVR file format, but the data is
** read through PatternData instead of requesting it over the old
** AVR/STM32 pseudo-sysex link. Each tick writes at most one logical record,
** so audio-time callers can keep pumping filesystem_tick().
**
** Phases: 0=open, 1=wait_open, 2=name, 3=steps, 4=main steps,
**         5=settings, 6=lengths, 7=track settings extension,
**         8=track shuffle extension, 9=close, 10=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_savePattern_tick(void)
{
    switch (op_phase) {
    case 0: /* OPEN */
    {
        char fname[13];
        if (!filesystem_makeFilename(fname, op_file_type, op_slot)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(fname, "w", on_file_opened))
            return;
        op_phase = 1;
        return;
    }

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2;
        op_stream_index = 0;
        op_item_offset = 0;
        op_bytes_done = 0;
        return;

    case 2: /* NAME */
        filesystem_writeStreamChunk((const uint8_t *)preset_currentName, 8);
        if (op_item_offset >= 8u) {
            op_item_offset = 0;
            op_stream_index = 0;
            op_phase = 3;
        }
        return;

    case 3: /* STEPS */
    {
        uint8_t pattern, track, step_nr;
        Step *step;

        if (op_stream_index >= FS_PATTERN_STEP_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 4;
            return;
        }

        filesystem_patternStepAddress(op_stream_index, &pattern, &track, &step_nr);
        step = filesystem_patternStepPtr(pattern, track, step_nr);
        if (!step) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        filesystem_packStep(step, staging_buf);
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_STEP_SIZE);
        if (op_item_offset >= FS_PATTERN_STEP_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 4: /* MAIN STEPS */
    {
        uint8_t pattern, track;
        uint16_t main_steps;

        if (op_stream_index >= FS_PATTERN_MAIN_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 5;
            return;
        }

        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            uint16_t *mainPtr = filesystem_patternMainPtr(pattern, track);
            if (!mainPtr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            main_steps = *mainPtr;
        }
        staging_buf[0] = (uint8_t)(main_steps & 0xffu);
        staging_buf[1] = (uint8_t)(main_steps >> 8);
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_MAIN_SIZE);
        if (op_item_offset >= FS_PATTERN_MAIN_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 5: /* PATTERN SETTINGS */
    {
        PatternSetting *setting;

        if (op_stream_index >= FS_PATTERN_SETTINGS_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 6;
            return;
        }

        setting = filesystem_patternSettingPtr((uint8_t)op_stream_index);
        if (!setting) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        staging_buf[0] = setting->nextPattern;
        staging_buf[1] = setting->changeBar;
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_SETTING_SIZE);
        if (op_item_offset >= FS_PATTERN_SETTING_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 6: /* TRACK LENGTHS */
    {
        uint8_t pattern, track;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 7;
            return;
        }

        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            LengthRotate *lr = filesystem_patternLengthPtr(pattern, track);
            if (!lr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            staging_buf[0] = lr->length;
        }
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 7: /* TRACK SETTINGS EXTENSION */
    {
        uint8_t pattern, track;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_close_status = FS_STATUS_DONE;
            op_phase = 8;
            return;
        }

        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            LengthRotate *lr = filesystem_patternLengthPtr(pattern, track);
            if (!lr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * New pattern saves append the fields that make the STEP front page
             * PatternData-owned. Length remains in the legacy block above so old
             * files and old tooling still see the expected byte stream prefix.
             * Shuffle deliberately does not widen this record; it has its own
             * following extension so files saved by the earlier four-byte
             * extension build remain unambiguous.
             */
            staging_buf[0] = lr->rotate;
            staging_buf[1] = lr->scale;
        }
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 8: /* TRACK SHUFFLE EXTENSION */
    {
        uint8_t pattern, track;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 9;
            return;
        }

        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            LengthRotate *lr = filesystem_patternLengthPtr(pattern, track);
            if (!lr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * Per-track shuffle is persisted in a standalone append-only
             * extension. Input is the PatternData LengthRotate owner for the
             * current file coordinate; output is one 0..127 shuffle byte.
             * Clients are newer pattern/container loaders. Older firmware stops
             * before this block because all earlier bytes keep their original
             * size and order.
             */
            staging_buf[0] = lr->shuffle;
        }
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_TRACK_SHUFFLE_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SHUFFLE_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 9: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed)) {
            /*
             * Pattern save follows the same close/request then wait/finish
             * contract. Advancing to case 10 lets the callback complete the
             * operation; re-entering case 9 would hang after the file payload.
             */
            op_phase = 10;
        }
        return;

    case 10: /* WAIT_CLOSE */
        if (!op_close_done) return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD PATTERN state machine
**
** Required sections (name, steps, main steps, settings, shuffle) fail on EOF.
** The final length block is optional for old `.pat` files; missing lengths
** are set to 0, which PatternData normalizes to the 128-step default. New
** files may append track-settings and track-shuffle extensions after the
** legacy length block.
**
** If the file contains the currently playing pattern while the sequencer is
** running, that pattern is loaded into PatternData's temporary buffer. At completion,
** seq_newPatternAvailable plus seq_armActivePatternReload() arms the existing
** sequencer boundary-swap path without replacing a queued pattern change.
**
** Phases: 0=open, 1=wait_open, 2=name, 3=steps, 4=main steps,
**         5=settings, 6=lengths, 7=settings extension,
**         8=shuffle extension, 9=close, 10=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_loadPattern_tick(void)
{
    switch (op_phase) {
    case 0: /* OPEN */
    {
        char fname[13];
        if (!filesystem_makeFilename(fname, op_file_type, op_slot)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(fname, "r", on_file_opened))
            return;
        op_phase = 1;
        return;
    }

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            memcpy(preset_currentName, "Empty   ", 8);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /*
         * TempPattern no longer exists. A one-Scene build cannot replace the
         * playing Pattern safely, so running loads fail explicitly instead of
         * writing through a hidden staging shape.
         */
        if (seq_isRunning()) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9;
            return;
        }
        op_loaded_active_pattern_running = 0u;
        op_phase = 2;
        op_stream_index = 0;
        op_item_offset = 0;
        op_bytes_done = 0;
        return;

    case 2: /* NAME */
    {
        uint32_t n = filesystem_readStreamChunk((uint8_t *)preset_currentName, 8);
        if (op_item_offset >= 8u) {
            uint8_t i;
            for (i = 0; i < 8; i++)
                if (preset_currentName[i] < 0x20 || preset_currentName[i] > 0x7E)
                    preset_currentName[i] = ' ';
            op_item_offset = 0;
            op_stream_index = 0;
            op_phase = 3;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9;
        }
        return;
    }

    case 3: /* STEPS */
    {
        uint8_t pattern, track, step_nr;
        Step *step;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_STEP_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 4;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_STEP_SIZE);
        if (op_item_offset >= FS_PATTERN_STEP_SIZE) {
            filesystem_patternStepAddress(op_stream_index, &pattern, &track, &step_nr);
            step = filesystem_patternStepPtr(pattern, track, step_nr);
            filesystem_unpackStep(step, staging_buf);
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9;
        }
        return;
    }

    case 4: /* MAIN STEPS */
    {
        uint8_t pattern, track;
        uint16_t *main_steps;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_MAIN_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 5;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_MAIN_SIZE);
        if (op_item_offset >= FS_PATTERN_MAIN_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            main_steps = filesystem_patternMainPtr(pattern, track);
            *main_steps = (uint16_t)staging_buf[0] | ((uint16_t)staging_buf[1] << 8);
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9;
        }
        return;
    }

    case 5: /* PATTERN SETTINGS */
    {
        PatternSetting *setting;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_SETTINGS_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 6;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_SETTING_SIZE);
        if (op_item_offset >= FS_PATTERN_SETTING_SIZE) {
            setting = filesystem_patternSettingPtr((uint8_t)op_stream_index);
            setting->nextPattern = staging_buf[0];
            setting->changeBar = staging_buf[1];
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 9;
        }
        return;
    }

    case 6: /* TRACK LENGTHS */
    {
        uint8_t pattern, track;
        LengthRotate *length_rotate;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 7;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u || (n == 0 && afatfs_feof(op_file))) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            length_rotate = filesystem_patternLengthPtr(pattern, track);
            length_rotate->length = (op_item_offset >= 1u) ? staging_buf[0] : 0;
            filesystem_defaultTrackSettings(length_rotate, track);
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 7: /* TRACK SETTINGS EXTENSION */
    {
        uint8_t pattern, track;
        LengthRotate *length_rotate;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 8;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            length_rotate = filesystem_patternLengthPtr(pattern, track);
            /*
             * Optional four-byte track-settings extension for new pattern
             * saves. Legacy files hit EOF before this block and keep the
             * defaults assigned while reading the length block above. Per-track
             * shuffle is intentionally a following extension so the four-byte
             * record size remains compatible with earlier Phase 2 saves.
             */
            length_rotate->rotate = staging_buf[0];
            length_rotate->scale = (staging_buf[1] < TRACK_SCALE_COUNT)
                ? staging_buf[1]
                : TRACK_SCALE_OFF;
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_item_offset = 0;
            op_close_status = FS_STATUS_DONE;
            op_phase = 9;
        }
        return;
    }

    case 8: /* TRACK SHUFFLE EXTENSION */
    {
        uint8_t pattern, track;
        LengthRotate *length_rotate;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 9;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_TRACK_SHUFFLE_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SHUFFLE_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            length_rotate = filesystem_patternLengthPtr(pattern, track);
            /*
             * Optional per-track shuffle extension. Input is one stored 0..127
             * byte for the current PatternData track; output updates
             * LengthRotate.shuffle. If EOF arrives before this block exists,
             * shuffle remains at the default-off value assigned with the track
             * length/settings defaults.
             * Clients: PatternData accessors and the per-track sequencer timing
             * scheduler.
             */
            length_rotate->shuffle = (staging_buf[0] <= 127u) ? staging_buf[0] : 0u;
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 9;
        }
        return;
    }

    case 9: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 10;
        return;

    case 10: /* WAIT_CLOSE */
        if (!op_close_done) return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** SAVE ALL / PERFORMANCE state machine
**
** Container format matches the reference:
**   name[8], version[1], meta area[64], kit area[512], pattern payload
**
** ALL meta area stores all globals, then 0xff padding to 64 bytes.
** PERFORMANCE meta area stores BPM and bar-reset mode, then 0xff padding.
** Kit area stores active kit bytes without a name, then 0xff padding.
** Pattern payload is the same as `.pat` after its 8-byte name header.
**
** Pattern payload phases: 8=steps, 9=main steps, 10=settings,
** 11=lengths, 12=track settings extension, 13=track shuffle extension,
** 14=close, 15=wait_close.
** ----------------------------------------------------------------------- */
static void filesystem_saveContainer_tick(void)
{
    uint8_t is_all = (current_op == FS_INTERNAL_OP_SAVE_ALL);

    switch (op_phase) {
    case 0: /* OPEN */
    {
        char fname[13];
        if (!filesystem_makeFilename(fname, op_file_type, op_slot)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(fname, "w", on_file_opened))
            return;
        op_phase = 1;
        return;
    }

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2;
        op_stream_index = 0;
        op_item_offset = 0;
        op_bytes_done = 0;
        return;

    case 2: /* NAME */
        filesystem_writeStreamChunk((const uint8_t *)preset_currentName, 8);
        if (op_item_offset >= 8u) {
            op_item_offset = 0;
            op_phase = 3;
        }
        return;

    case 3: /* VERSION */
        staging_buf[0] = FS_CONTAINER_VERSION;
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index = 0;
            op_phase = 4;
        }
        return;

    case 4: /* META */
    {
        uint16_t meta_len = is_all ? (NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS) : 2u;

        if (op_stream_index >= meta_len) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 5;
            return;
        }

        if (is_all) {
            staging_buf[0] = parameter_values[PAR_BEGINNING_OF_GLOBALS + op_stream_index];
        } else {
            staging_buf[0] = (op_stream_index == 0u)
                ? parameter_values[PAR_BPM]
                : parameter_values[PAR_BAR_RESET_MODE];
        }
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 5: /* META PADDING */
    {
        uint16_t meta_len = is_all ? (NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS) : 2u;
        uint16_t pad_len = FS_CONTAINER_META_LEN - meta_len;

        if (op_stream_index >= pad_len) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 6;
            return;
        }

        staging_buf[0] = FS_CONTAINER_PAD_BYTE;
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 6: /* KIT DATA */
        if (op_stream_index >= END_OF_SOUND_PARAMETERS) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 7;
            return;
        }
        staging_buf[0] = parameter_values[op_stream_index];
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;

    case 7: /* KIT PADDING */
    {
        uint16_t pad_len = FS_CONTAINER_KIT_LEN - END_OF_SOUND_PARAMETERS;
        if (op_stream_index >= pad_len) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 8;
            return;
        }
        staging_buf[0] = FS_CONTAINER_PAD_BYTE;
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 8: /* PATTERN STEPS */
    {
        uint8_t pattern, track, step_nr;
        Step *step;
        if (op_stream_index >= FS_PATTERN_STEP_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 9;
            return;
        }
        filesystem_patternStepAddress(op_stream_index, &pattern, &track, &step_nr);
        step = filesystem_patternStepPtr(pattern, track, step_nr);
        if (!step) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        filesystem_packStep(step, staging_buf);
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_STEP_SIZE);
        if (op_item_offset >= FS_PATTERN_STEP_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 9: /* PATTERN MAIN STEPS */
    {
        uint8_t pattern, track;
        uint16_t main_steps;
        if (op_stream_index >= FS_PATTERN_MAIN_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 10;
            return;
        }
        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            uint16_t *mainPtr = filesystem_patternMainPtr(pattern, track);
            if (!mainPtr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            main_steps = *mainPtr;
        }
        staging_buf[0] = (uint8_t)(main_steps & 0xffu);
        staging_buf[1] = (uint8_t)(main_steps >> 8);
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_MAIN_SIZE);
        if (op_item_offset >= FS_PATTERN_MAIN_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 10: /* PATTERN SETTINGS */
    {
        PatternSetting *setting;
        if (op_stream_index >= FS_PATTERN_SETTINGS_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 11;
            return;
        }
        setting = filesystem_patternSettingPtr((uint8_t)op_stream_index);
        if (!setting) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        staging_buf[0] = setting->nextPattern;
        staging_buf[1] = setting->changeBar;
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_SETTING_SIZE);
        if (op_item_offset >= FS_PATTERN_SETTING_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 11: /* PATTERN LENGTHS */
    {
        uint8_t pattern, track;
        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 12;
            return;
        }
        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            LengthRotate *lr = filesystem_patternLengthPtr(pattern, track);
            if (!lr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            staging_buf[0] = lr->length;
        }
        filesystem_writeStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 12: /* PATTERN SETTINGS EXTENSION */
    {
        uint8_t pattern, track;
        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_close_status = FS_STATUS_DONE;
            op_phase = 13;
            return;
        }
        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            LengthRotate *lr = filesystem_patternLengthPtr(pattern, track);
            if (!lr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * Container pattern payload mirrors standalone .pat: write the
             * current track length block, then append PatternData-owned track
             * settings for new firmware. Shuffle is not packed into this
             * four-byte record; it has a separate extension immediately after
             * this block.
             */
            staging_buf[0] = lr->rotate;
            staging_buf[1] = lr->scale;
        }
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 13: /* TRACK SHUFFLE EXTENSION */
    {
        uint8_t pattern, track;
        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 14;
            return;
        }
        filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
        {
            LengthRotate *lr = filesystem_patternLengthPtr(pattern, track);
            if (!lr) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * Append per-track shuffle after the track-settings
             * extension. Input is PatternData's stored 0..127 timing amount;
             * output is one byte consumed by newer container/pattern loaders.
             */
            staging_buf[0] = lr->shuffle;
        }
        filesystem_writeStreamChunk(staging_buf, FS_PATTERN_TRACK_SHUFFLE_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SHUFFLE_SIZE) {
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 14: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15;
        return;

    case 15: /* WAIT_CLOSE */
        if (!op_close_done) return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD ALL / PERFORMANCE state machine
**
** Pattern payload phases mirror saveContainer(): 8=steps, 9=main steps,
** 10=settings, 11=lengths, 12=track settings extension,
** 13=optional track shuffle extension, 14=close, 15=wait_close. Required
** sections close with error on EOF; optional extensions close with done.
** ----------------------------------------------------------------------- */
static void filesystem_loadContainer_tick(void)
{
    uint8_t is_all = (current_op == FS_INTERNAL_OP_LOAD_ALL);

    switch (op_phase) {
    case 0: /* OPEN */
    {
        char fname[13];
        if (!filesystem_makeFilename(fname, op_file_type, op_slot)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (is_all)
            fs_stale_warning_pending = FS_STALE_WARNING_NONE;
        if (!afatfs_fopen(fname, "r", on_file_opened))
            return;
        op_phase = 1;
        return;
    }

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            memcpy(preset_currentName, "Empty   ", 8);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (seq_isRunning()) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
            return;
        }
        op_loaded_active_pattern_running = 0u;
        op_phase = 2;
        op_stream_index = 0;
        op_item_offset = 0;
        op_bytes_done = 0;
        return;

    case 2: /* NAME */
    {
        uint32_t n = filesystem_readStreamChunk((uint8_t *)preset_currentName, 8);
        if (op_item_offset >= 8u) {
            uint8_t i;
            for (i = 0; i < 8; i++)
                if (preset_currentName[i] < 0x20 || preset_currentName[i] > 0x7E)
                    preset_currentName[i] = ' ';
            op_item_offset = 0;
            op_phase = 3;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 3: /* VERSION */
    {
        uint32_t n = filesystem_readStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_file_version = staging_buf[0];
            op_item_offset = 0;
            op_stream_index = 0;
            if (op_file_version > FS_CONTAINER_VERSION) {
                op_close_status = FS_STATUS_ERROR;
                op_phase = 14;
            } else {
                op_phase = 4;
            }
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 4: /* META */
    {
        uint32_t n;

        if (is_all) {
            uint16_t globals_len = (uint16_t)(NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS);

            /* Read the complete 64-byte .all meta field before deciding how
            ** many globals it actually contains. This preserves the fixed file
            ** offset regardless of whether globals are current, legacy, or
            ** stale. Unknown layouts are safely defaulted and warned later. */
            n = filesystem_readStreamChunk(staging_buf, FS_CONTAINER_META_LEN);
            if (op_item_offset >= FS_CONTAINER_META_LEN) {
                if (globals_len <= FS_CONTAINER_META_LEN &&
                    filesystem_metaHasStoredGlobalsLen(staging_buf, globals_len)) {
                    filesystem_applyGlobalsPrefix(staging_buf, globals_len);
                } else if (filesystem_metaHasStoredGlobalsLen(staging_buf,
                                                             FS_GLOBALS_LEGACY_LEN_22)) {
                    /* Legacy 22-byte globals: keep values, force known-safe
                    ** settings for fields that shifted/weren't present. */
                    filesystem_applyLegacy22Globals(staging_buf, FS_GLOBALS_LEGACY_LEN_22);
                } else {
                    filesystem_applyStaleGlobalsFallback(staging_buf,
                                                         filesystem_staleMetaPrefixLen(staging_buf));
                    fs_stale_warning_pending = FS_STALE_WARNING_ALL;
                }
                op_item_offset = 0;
                op_stream_index = 0;
                op_phase = 6;
            } else if (n == 0 && afatfs_feof(op_file)) {
                op_close_status = FS_STATUS_ERROR;
                op_phase = 14;
            }
            return;
        }

        {
            uint16_t meta_len = (op_file_version > 1u ? 2u : 1u);

            if (op_stream_index >= meta_len) {
                op_stream_index = 0;
                op_item_offset = 0;
                op_phase = 5;
                return;
            }

            n = filesystem_readStreamChunk(staging_buf, 1);
            if (op_item_offset >= 1u) {
                if (op_stream_index == 0u) {
                    parameter_values[PAR_BPM] = staging_buf[0];
                } else {
                    parameter_values[PAR_BAR_RESET_MODE] = staging_buf[0];
                }
                op_item_offset = 0;
                op_stream_index++;
            } else if (n == 0 && afatfs_feof(op_file)) {
                op_close_status = FS_STATUS_ERROR;
                op_phase = 14;
            }
        }
        return;
    }

    case 5: /* META PADDING */
    {
        uint16_t meta_len = (op_file_version > 1u ? 2u : FS_CONTAINER_META_LEN);
        uint16_t pad_len = FS_CONTAINER_META_LEN - meta_len;
        uint32_t n;

        if (op_stream_index >= pad_len) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 6;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 6: /* KIT DATA */
    {
        uint32_t n;
        if (op_stream_index >= END_OF_SOUND_PARAMETERS) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 7;
            return;
        }
        n = filesystem_readStreamChunk(parameter_values + op_stream_index, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            memset(parameter_values + op_stream_index, 0,
                   END_OF_SOUND_PARAMETERS - op_stream_index);
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 7;
        }
        return;
    }

    case 7: /* KIT PADDING */
    {
        uint16_t pad_len = FS_CONTAINER_KIT_LEN - END_OF_SOUND_PARAMETERS;
        uint32_t n;
        if (op_stream_index >= pad_len) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 8;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u) {
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 8: /* PATTERN STEPS */
    {
        uint8_t pattern, track, step_nr;
        Step *step;
        uint32_t n;
        if (op_stream_index >= FS_PATTERN_STEP_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 9;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_STEP_SIZE);
        if (op_item_offset >= FS_PATTERN_STEP_SIZE) {
            filesystem_patternStepAddress(op_stream_index, &pattern, &track, &step_nr);
            step = filesystem_patternStepPtr(pattern, track, step_nr);
            filesystem_unpackStep(step, staging_buf);
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 9: /* PATTERN MAIN STEPS */
    {
        uint8_t pattern, track;
        uint16_t *main_steps;
        uint32_t n;
        if (op_stream_index >= FS_PATTERN_MAIN_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 10;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_MAIN_SIZE);
        if (op_item_offset >= FS_PATTERN_MAIN_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            main_steps = filesystem_patternMainPtr(pattern, track);
            *main_steps = (uint16_t)staging_buf[0] | ((uint16_t)staging_buf[1] << 8);
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 10: /* PATTERN SETTINGS */
    {
        PatternSetting *setting;
        uint32_t n;
        if (op_stream_index >= FS_PATTERN_SETTINGS_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 11;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_SETTING_SIZE);
        if (op_item_offset >= FS_PATTERN_SETTING_SIZE) {
            setting = filesystem_patternSettingPtr((uint8_t)op_stream_index);
            setting->nextPattern = staging_buf[0];
            setting->changeBar = staging_buf[1];
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
        }
        return;
    }

    case 11: /* PATTERN LENGTHS */
    {
        uint8_t pattern, track;
        LengthRotate *length_rotate;
        uint32_t n;
        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 12;
            return;
        }
        n = filesystem_readStreamChunk(staging_buf, 1);
        if (op_item_offset >= 1u || (n == 0 && afatfs_feof(op_file))) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            length_rotate = filesystem_patternLengthPtr(pattern, track);
            length_rotate->length = (op_item_offset >= 1u) ? staging_buf[0] : 0;
            filesystem_defaultTrackSettings(length_rotate, track);
            op_item_offset = 0;
            op_stream_index++;
        }
        return;
    }

    case 12: /* PATTERN SETTINGS EXTENSION */
    {
        uint8_t pattern, track;
        LengthRotate *length_rotate;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_stream_index = 0;
            op_item_offset = 0;
            op_phase = 13;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SETTINGS_EXTRA_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            length_rotate = filesystem_patternLengthPtr(pattern, track);
            /*
             * Optional four-byte track-settings extension inside .all/.prf
             * containers. Inputs are the append-only bytes after the current
             * length block; outputs update PatternData's track settings record.
             * Shuffle is intentionally not read here because it belongs to the
             * following one-byte-per-track extension, keeping this record
             * compatible with earlier four-byte saves.
             */
            length_rotate->rotate = staging_buf[0];
            length_rotate->scale = (staging_buf[1] < TRACK_SCALE_COUNT)
                ? staging_buf[1]
                : TRACK_SCALE_OFF;
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_item_offset = 0;
            op_close_status = FS_STATUS_DONE;
            op_phase = 14;
        }
        return;
    }

    case 13: /* TRACK SHUFFLE EXTENSION */
    {
        uint8_t pattern, track;
        LengthRotate *length_rotate;
        uint32_t n;

        if (op_stream_index >= FS_PATTERN_LENGTH_COUNT) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 14;
            return;
        }

        n = filesystem_readStreamChunk(staging_buf, FS_PATTERN_TRACK_SHUFFLE_SIZE);
        if (op_item_offset >= FS_PATTERN_TRACK_SHUFFLE_SIZE) {
            filesystem_patternTrackAddress(op_stream_index, &pattern, &track);
            length_rotate = filesystem_patternLengthPtr(pattern, track);
            /*
             * Optional per-track shuffle extension for container payloads.
             * Input is one stored 0..127 byte for the current PatternData
             * track; output updates LengthRotate.shuffle. If a provisional file
             * ends before this block, shuffle remains at the default-off value.
             */
            length_rotate->shuffle = (staging_buf[0] <= 127u) ? staging_buf[0] : 0u;
            op_item_offset = 0;
            op_stream_index++;
        } else if (n == 0 && afatfs_feof(op_file)) {
            op_close_status = FS_STATUS_DONE;
            op_phase = 14;
        }
        return;
    }

    case 14: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15;
        return;

    case 15: /* WAIT_CLOSE */
        if (!op_close_done) return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD SETTINGS state machine
**
** Phases: 0=defaults+open, 1=wait_open, 2=read lines, 3=close, 4=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_loadGlobals_tick(void)
{
    uint8_t line_ready;
    uint8_t eof;
    storage_status_t st;

    switch (op_phase) {
    case 0: /* DEFAULTS + OPEN */
        op_file_ready = false;
        op_file = NULL;
        fs_stale_warning_pending = FS_STALE_WARNING_NONE;
        filesystem_resetSettingsToDefaults();
        op_close_status = FS_STATUS_DONE;
        op_line_len = 0u;
        if (!afatfs_fopen(STORAGE_SETTINGS_FILENAME, "r", on_file_opened))
            return;
        op_phase = 1;
        return;

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            /*
             * No settings file is valid first-boot/card state.
             *
             * Output: defaults from phase 0 remain live. No former raw globals
             * fallback is attempted because that filename is retired and
             * should not be recognized by current firmware.
             */
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_phase = 2;
        return;

    case 2: /* READ LINES */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 3;
            return;
        }
        if (line_ready) {
            op_close_status = filesystem_parseSettingsLine(op_line_buf);
            if (op_close_status != FS_STATUS_DONE)
                op_phase = 3;
            return;
        }
        if (eof) {
            filesystem_sanitizeLoadedGlobals();
            op_close_status = FS_STATUS_DONE;
            op_phase = 3;
        }
        return;

    case 3: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4;
        return;

    case 4: /* WAIT_CLOSE */
        if (!op_close_done) return;
        /*
         * Data is in parameter_values[] and BankData. Global runtime apply
         * happens in Menu when it sees the Preset completion, matching the old
         * deferred apply boundary.
         */
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** SAVE SETTINGS state machine
**
** Phases: 0=open, 1=wait_open, 2=write lines, 3=close, 4=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_saveGlobals_tick(void)
{
    switch (op_phase) {
    case 0: /* OPEN */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_SETTINGS_FILENAME, "w", on_file_opened))
            return;
        op_phase = 1;
        return;

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 2;
        return;

    case 2: /* WRITE LINES */
        if (filesystem_writeTextLine(filesystem_nextSettingsLine, NULL))
            return;
        op_phase = 3;
        return;

    case 3: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4;
        return;

    case 4: /* WAIT_CLOSE */
        if (!op_close_done) return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** SCAN PHASE 2 KIT DIRECTORIES state machine
**
** Why this exists: the menu still browses fixed numeric slots, but the card now
** stores kits as folders named 001 Name, 002 Name, etc. This scanner opens the
** root Kit/ directory by exact display name, walks asyncfatfs-resolved objects,
** accepts only numbered directories via storage_parseNumberedFolder(), and
** caches both the user display name and short open name.
**
** Inputs: no slot input; filesystem_requestScanKits() clears the cache and
** starts this op. Outputs: kit_slot_present/name/open_name and the legacy
** kb_map/kb_numKits compatibility map. Missing Kit/ is treated as a successful
** empty scan so boot/menu can show Empty slots instead of a filesystem error.
**
** Affiliates/clients: main startup calls filesystem_requestScanKits() before
** preset_loadDrumset(0,0); kitBrowser.c still consumes kb_map; menu.c consumes
** filesystem_kitSlotName() for direct Load-page display.
** ----------------------------------------------------------------------- */
static void filesystem_scanKits_tick(void)
{
    switch (op_phase) {
    case 0: /* CHDIR root */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Kit/ */
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        /*
         * Open the root Kit directory through the LFN-aware directory path.
         *
         * "Kit" is a short display component today, but using opendir_lfn keeps
         * production scans on the same case-preserving lookup path as long
         * Kit folders and prevents future root-name changes from falling back
         * to raw uppercase SFN behavior.
         */
        if (!afatfs_opendir_lfn(STORAGE_ROOT_KIT,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR Kit/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
        op_phase = 4;
        return;

    case 4: /* FIND_NEXT */
    {
        /*
         * One public object may consume several raw FAT entries.
         *
         * The iterator can return IN_PROGRESS while cache sectors are
         * unavailable, so this phase advances only after SUCCESS. A NONE
         * object means end of directory, not an empty Kit slot; missing slot
         * semantics are handled by the numbered-folder cache after scanning.
         */
        afatfsOperationStatus_e st =
            afatfs_findNextObject(op_kit_root_dir,
                                  &op_object_finder,
                                  &op_object);
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 5;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 5;
            return;
        }
        /*
         * Scan Kit/ through the asyncfatfs object iterator.
         *
         * Inputs are concrete objects with displayName already resolved from
         * VFAT LFN fragments or SFN case bits. Output: only numbered directory
         * components become Kit browser slots. The product-level numbered
         * folder parser is the filter; asyncfatfs has already handled
         * structural FAT records, and it intentionally does not hide ordinary
         * dot-prefixed names.
         */
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
            filesystem_recordKitDirectory(op_object.id.displayName,
                                          op_object.id.shortName);
        }
        return;
    }

    case 5: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 6;
        return;

    case 6: /* WAIT_CLOSE */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 7;
        return;

    case 7: /* CHDIR root + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** SCAN SCENE DIRECTORIES state machine
**
** Inputs: filesystem_requestScanScenes() clears the Scene slot cache and
** starts this operation. Output: Scene/NNN Name folders populate
** scene_slot_present/name/open_name. Missing Scene/ is a successful empty
** scan, matching Kit/ behavior.
**
** The long-filename loop is intentionally duplicated from Kit scan instead of
** reusing Kit recording callbacks: Scene slots and Kit slots are separate root
** libraries and must never share occupancy or display-name state.
** ----------------------------------------------------------------------- */
static void filesystem_scanScenes_tick(void)
{
    switch (op_phase) {
    case 0: /* CHDIR root */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Scene/ */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_ROOT_SCENE, "r", on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR Scene/ and begin directory iteration */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        afatfs_findFirst(op_kit_root_dir, &op_finder);
        filesystem_dirLfnReset();
        op_phase = 4;
        return;

    case 4: /* FIND_NEXT */
    {
        fatDirectoryEntry_t *entry = NULL;
        afatfsOperationStatus_e st = afatfs_findNext(op_kit_root_dir,
                                                     &op_finder,
                                                     &entry);
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLast(op_kit_root_dir);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 5;
            return;
        }
        if (entry == NULL || fat_isDirectoryEntryTerminator(entry)) {
            afatfs_findLast(op_kit_root_dir);
            op_close_status = FS_STATUS_DONE;
            op_phase = 5;
            return;
        }
        if (fat_isDirectoryEntryEmpty(entry)) {
            filesystem_dirLfnReset();
            return;
        }
        if ((entry->attrib & 0x0fu) == 0x0fu) {
            filesystem_dirLfnAppendEntry(entry);
            return;
        }
        if ((entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) &&
            !(entry->attrib & FAT_FILE_ATTRIBUTE_VOLUME_ID)) {
            char short_name[STORAGE_KIT_FILENAME_MAX];
            const char *display_name;

            memset(short_name, 0, sizeof(short_name));
            fat_convertFATStyleToFilename(entry->filename, short_name);
            fat_applyFilenameCaseFlags(short_name, entry->ntReserved);

            display_name = op_lfn_valid ? op_lfn_name : short_name;
            filesystem_recordSceneDirectory(display_name, short_name);
        }
        filesystem_dirLfnReset();
        return;
    }

    case 5: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 6;
        return;

    case 6: /* WAIT_CLOSE */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 7;
        return;

    case 7: /* CHDIR root + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        op_close_status = FS_STATUS_ERROR;
        op_phase = 7;
        return;
    }
}

/* -----------------------------------------------------------------------
** SCAN BANK DIRECTORIES state machine
**
** Inputs: filesystem_requestScanBanks() clears the root Bank slot cache and
** starts this operation. Output: Bank/NNN Name folders populate
** bank_slot_present/name/open_name. Missing Bank/ is a successful empty scan
** so boot can continue into the root Scene/Kit fallback chain.
**
** Bank-local Scene children are not scanned here. They are two-digit 00..15
** folders discovered only while a selected Bank is open for load/save.
** ----------------------------------------------------------------------- */
static void filesystem_scanBanks_tick(void)
{
    switch (op_phase) {
    case 0: /* CHDIR root */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Bank/ */
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        if (!afatfs_opendir_lfn(STORAGE_ROOT_BANK,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 2;
        return;

    case 2: /* WAIT_OPEN */
        if (!op_file_ready)
            return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR Bank/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
        op_phase = 4;
        return;

    case 4: /* FIND_NEXT */
    {
        afatfsOperationStatus_e st =
            afatfs_findNextObject(op_kit_root_dir,
                                  &op_object_finder,
                                  &op_object);
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 5;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 5;
            return;
        }
        /*
         * Record only root Bank folders.
         *
         * Inputs: asyncfatfs has already resolved the public display name and
         * short alias. Output: one root 000..999 Bank cache entry per accepted
         * directory. Files and dot backers are ignored until future autosave
         * support gives them product meaning.
         */
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
            /*
             * Cache displayName as both browser text and future open key.
             *
             * afatfs_opendir_lfn() performs display-component matching for
             * read-only opens. Passing the generated SFN alias here makes
             * host-created LFN Banks list correctly but fail to open later.
             */
            filesystem_recordBankDirectory(op_object.id.displayName,
                                           op_object.id.displayName);
        }
        return;
    }

    case 5: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 6;
        return;

    case 6: /* WAIT_CLOSE */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_phase = 7;
        return;

    case 7: /* CHDIR root + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        op_close_status = FS_STATUS_ERROR;
        op_phase = 7;
        return;
    }
}

/* -----------------------------------------------------------------------
** SCAN ROOT INSTRUMENT FILES state machine
**
** Inputs: filesystem_requestScanInstruments() clears the typed cache and
** starts this operation. Outputs: per-type sorted Instrument/ cache populated
** from files whose extensions match the instrument registry. Missing
** Instrument/ is a successful empty scan so the Load Instrument UI can show an
** empty list instead of a filesystem error.
** ----------------------------------------------------------------------- */
static void filesystem_scanInstruments_tick(void)
{
    switch (op_phase) {
    case 0: /* CHDIR root */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Instrument/ */
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        /*
         * Open the root Instrument directory by exact display component.
         *
         * Older code used the generated alias INSTRU~1. The restored
         * production browser must instead depend on asyncfatfs' case-preserved
         * LFN/SFN lookup so files saved under Instrument/ are scanned from the
         * directory the user sees on the card.
         */
        if (!afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT,
                                AFATFS_MATCH_CASE_SENSITIVE,
                                op_root_open_name,
                                on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3;
        return;

    case 3: /* CHDIR Instrument/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
        op_phase = 4;
        return;

    case 4: /* FIND_NEXT */
    {
        afatfsOperationStatus_e st =
            afatfs_findNextObject(op_kit_root_dir,
                                  &op_object_finder,
                                  &op_object);
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 5;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 5;
            return;
        }
        /*
         * Keep all ordinary files visible to the product classifier.
         *
         * Dot-prefixed files are not hidden here by filesystem policy. They
         * simply fail the registered instrument extension/type check unless
         * they are genuine instrument files. displayName is the user-facing LFN
         * or case-preserved SFN spelling; shortName is the identity alias used
         * by the current file-open path after selection.
         */
        if (op_object.id.kind == AFATFS_OBJECT_FILE) {
            filesystem_recordInstrumentFile(op_object.id.displayName,
                                            op_object.id.shortName);
        }
        return;
    }

    case 5: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 6;
        return;

    case 6: /* WAIT_CLOSE */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 7;
        return;

    case 7: /* CHDIR root + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** LOAD NAME state machine
**
** Why this exists: browsing code needs names without loading payloads. Legacy
** file types still read the first eight bytes of their numbered file header.
** FS_FILE_KIT is now special: kit names are already known from the Kit/ scan
** cache, so the function returns the cached name directly and does not open a
** .SND file.
**
** Inputs: op_file_type/op_slot from filesystem_requestLoadName(). Outputs:
** loaded_name is filled with a display name, "Empty   ", spaces for unsupported
** types, or "-       " for malformed short files. Clients: preset_loadName(),
** kitBrowser.c, and menu_requestCurrentLoadSaveSelection().
**
** Phases for legacy file-header reads: 0=open, 1=wait_open, 2=read,
** 3=close, 4=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_loadName_tick(void)
{
    switch (op_phase) {
    case 0: /* OPEN */
    {
        char fname[13];
        const fs_file_desc_t *desc = filesystem_desc(op_file_type);
        /* Directory kits do not have an eight-byte .SND header. The scan cache
        ** is the name source, and using it avoids conflicting with an active
        ** single-operation kit load. */
        if (op_file_type == FS_FILE_KIT) {
            filesystem_setLoadedNameFromKitSlot(op_slot);
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        if (desc == NULL || !desc->has_name_header ||
            !filesystem_makeFilename(fname, op_file_type, op_slot)) {
            memcpy(loaded_name, "        ", 8);
            loaded_name[8] = '\0';
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(fname, "r", on_file_opened))
            return;
        op_phase = 1;
        return;
    }

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            memcpy(loaded_name, "Empty   ", 8);
            loaded_name[8] = '\0';
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_phase = 2;
        op_bytes_done = 0;
        return;

    case 2: /* READ */
    {
        uint32_t n = afatfs_fread(op_file,
                                  (uint8_t *)loaded_name + op_bytes_done,
                                  8 - op_bytes_done);
        op_bytes_done += n;
        if (op_bytes_done >= 8) {
            /* Full name read — sanitize and proceed. */
            loaded_name[8] = '\0';
            uint8_t i;
            for (i = 0; i < 8; i++)
                if (loaded_name[i] < 0x20 || loaded_name[i] > 0x7E)
                    loaded_name[i] = ' ';
            op_phase = 3;
        } else if (n == 0 && afatfs_feof(op_file)) {
            /* EOF before 8 bytes (including zero-byte file) — malformed.
            ** Show '-' so the display distinguishes this from an empty slot. */
            loaded_name[0] = '-';
            memset(loaded_name + 1, ' ', 7);
            loaded_name[8] = '\0';
            op_phase = 3;
        }
        /* else: async buffer not ready yet — wait for more data next tick. */
        return;
    }

    case 3: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4;
        return;

    case 4: /* WAIT_CLOSE */
        if (!op_close_done) return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** MODAL SAMPLE INSTALL
**
** This path intentionally blocks. The caller must suspend audio before
** invoking it. asyncfatfs still does the SD byte pumping; we simply poll it
** synchronously until each open/read/seek/close step completes.
** ----------------------------------------------------------------------- */
#define FS_SAMPLE_MANIFEST_MAX SAMPLE_MAX_COUNT
#define FS_SAMPLE_BUFFER_SIZE  512u
#define FS_SAMPLE_LFN_MAX      80u
#define FS_SAMPLE_ELLIPSIS     ((char)0x00)

typedef struct {
    char filename[13];
    char sort_name[FS_SAMPLE_LFN_MAX];
    char name[3];
    char display_name[SAMPLE_DISPLAY_NAME_LEN];
    uint32_t data_offset;
    uint32_t data_bytes;
} fs_sample_manifest_t;

static fs_sample_manifest_t sample_manifest[FS_SAMPLE_MANIFEST_MAX];
static uint8_t sample_manifest_count = 0;
static uint8_t sample_io_buf[FS_SAMPLE_BUFFER_SIZE + 4u];

static volatile uint8_t block_file_ready = 0;
static afatfsFilePtr_t block_file = NULL;
static volatile uint8_t block_close_done = 0;

static void filesystem_blockOpenCb(afatfsFilePtr_t file)
{
    block_file = file;
    block_file_ready = 1;
}

static void filesystem_blockCloseCb(void)
{
    block_close_done = 1;
}

static uint8_t filesystem_blockFsOk(void)
{
    return (uint8_t)(afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_READY);
}

static void filesystem_blockPoll(void)
{
    afatfs_poll();
}

static afatfsFilePtr_t filesystem_blockOpen(const char *filename)
{
    block_file_ready = 0;
    block_file = NULL;

    while (!afatfs_fopen(filename, "r", filesystem_blockOpenCb)) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return NULL;
    }

    while (!block_file_ready) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return NULL;
    }

    return block_file;
}

static uint8_t filesystem_blockClose(afatfsFilePtr_t file)
{
    block_close_done = 0;
    while (!afatfs_fclose(file, filesystem_blockCloseCb)) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0;
    }
    while (!block_close_done) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0;
    }
    return 1;
}

static uint8_t filesystem_blockChdir(afatfsFilePtr_t dir)
{
    while (!afatfs_chdir(dir)) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0;
    }
    return 1;
}

static uint8_t filesystem_blockSeek(afatfsFilePtr_t file, uint32_t offset)
{
    uint32_t pos = 0;
    afatfsOperationStatus_e st = afatfs_fseek(file, (int32_t)offset, AFATFS_SEEK_SET);

    if (st == AFATFS_OPERATION_FAILURE)
        return 0;
    while (!afatfs_ftell(file, &pos)) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0;
    }
    return (uint8_t)(pos == offset);
}

static uint8_t filesystem_blockTell(afatfsFilePtr_t file, uint32_t *position)
{
    while (!afatfs_ftell(file, position)) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0;
    }
    return 1;
}

static uint32_t filesystem_blockRead(afatfsFilePtr_t file, uint8_t *buf, uint32_t len)
{
    uint32_t done = 0;

    while (done < len) {
        uint32_t n = afatfs_fread(file, buf + done, len - done);
        if (n != 0u) {
            done += n;
            continue;
        }
        if (afatfs_feof(file))
            break;
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            break;
    }

    return done;
}

static uint16_t filesystem_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t filesystem_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t filesystem_chunkIdEquals(const uint8_t *p, const char *id)
{
    return (uint8_t)(p[0] == (uint8_t)id[0] && p[1] == (uint8_t)id[1] &&
                     p[2] == (uint8_t)id[2] && p[3] == (uint8_t)id[3]);
}

static uint8_t filesystem_isWavName(const char *filename)
{
    uint8_t i = 0;
    while (filename[i] != '\0' && i < (FS_SAMPLE_LFN_MAX - 1u))
        i++;
    if (i < 5u)
        return 0;
    return (uint8_t)(filename[i - 4u] == '.' &&
        (filename[i - 3u] == 'W' || filename[i - 3u] == 'w') &&
        (filename[i - 2u] == 'A' || filename[i - 2u] == 'a') &&
        (filename[i - 1u] == 'V' || filename[i - 1u] == 'v'));
}

static void filesystem_sampleNameFromFilename(char dst[3], const char *filename)
{
    for (uint8_t i = 0; i < 3u; i++) {
        char c = filename[i];
        dst[i] = (c == '\0' || c == '.') ? ' ' : c;
    }
}

static void filesystem_displayNameFromFilename(char dst[SAMPLE_DISPLAY_NAME_LEN],
                                               const char *filename)
{
    uint8_t len = 0;
    uint8_t stem_len = 0;

    while (filename[len] != '\0' && len < (FS_SAMPLE_LFN_MAX - 1u)) {
        if (filename[len] == '.')
            stem_len = len;
        len++;
    }
    if (stem_len == 0u)
        stem_len = len;

    if (stem_len > SAMPLE_DISPLAY_NAME_LEN) {
        dst[0] = filename[0];
        dst[1] = filename[1];
        dst[2] = filename[2];
        dst[3] = filename[3];
        dst[4] = FS_SAMPLE_ELLIPSIS;
        dst[5] = filename[stem_len - 3u];
        dst[6] = filename[stem_len - 2u];
        dst[7] = filename[stem_len - 1u];
        return;
    }

    for (uint8_t i = 0; i < SAMPLE_DISPLAY_NAME_LEN; i++) {
        if (i >= stem_len)
            break;
        dst[i] = filename[i];
    }
    for (uint8_t i = stem_len; i < SAMPLE_DISPLAY_NAME_LEN; i++)
        dst[i] = ' ';
}

static void filesystem_copySampleSortName(char dst[FS_SAMPLE_LFN_MAX],
                                          const char *filename)
{
    uint8_t i;

    for (i = 0; i < (FS_SAMPLE_LFN_MAX - 1u) && filename[i] != '\0'; i++)
        dst[i] = filename[i];
    dst[i] = '\0';
}

static uint8_t filesystem_lfnCharAt(const uint8_t *entry, uint8_t index,
                                    uint16_t *out)
{
    static const uint8_t offsets[13] = {
        1u, 3u, 5u, 7u, 9u,
        14u, 16u, 18u, 20u, 22u, 24u,
        28u, 30u
    };
    uint8_t o = offsets[index];

    *out = (uint16_t)entry[o] | ((uint16_t)entry[o + 1u] << 8);
    return 1;
}

static void filesystem_lfnReset(char *lfn, uint8_t *valid)
{
    memset(lfn, 0, FS_SAMPLE_LFN_MAX);
    *valid = 0;
}

static void filesystem_lfnAppendEntry(const fatDirectoryEntry_t *entry,
                                      char *lfn, uint8_t *valid)
{
    const uint8_t *raw = (const uint8_t *)entry;
    uint8_t seq = raw[0] & 0x1fu;

    if (seq == 0u || seq > 6u) {
        filesystem_lfnReset(lfn, valid);
        return;
    }

    if (raw[0] & 0x40u) {
        memset(lfn, 0, FS_SAMPLE_LFN_MAX);
        *valid = 1;
    } else if (!*valid) {
        return;
    }

    uint8_t pos = (uint8_t)((seq - 1u) * 13u);
    for (uint8_t i = 0; i < 13u && pos < (FS_SAMPLE_LFN_MAX - 1u); i++, pos++) {
        uint16_t ch;

        (void)filesystem_lfnCharAt(raw, i, &ch);
        if (ch == 0x0000u)
            break;
        if (ch == 0xffffu)
            continue;
        lfn[pos] = (ch < 0x80u) ? (char)ch : '_';
    }
}

static char filesystem_sortLower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

static int8_t filesystem_compareSampleNameStrict(const char *a, const char *b)
{
    for (uint8_t i = 0; i < (FS_SAMPLE_LFN_MAX - 1u); i++) {
        char ca = filesystem_sortLower(a[i]);
        char cb = filesystem_sortLower(b[i]);

        if (ca != cb)
            return (ca < cb) ? -1 : 1;
        if (ca == '\0')
            return 0;
    }
    return 0;
}

static void filesystem_insertSampleManifest(const fs_sample_manifest_t *manifest)
{
    uint8_t pos = 0;

    while (pos < sample_manifest_count &&
           filesystem_compareSampleNameStrict(sample_manifest[pos].sort_name,
                                              manifest->sort_name) <= 0)
        pos++;

    if (sample_manifest_count < FS_SAMPLE_MANIFEST_MAX) {
        for (uint8_t i = sample_manifest_count; i > pos; i--)
            sample_manifest[i] = sample_manifest[i - 1u];
        sample_manifest[pos] = *manifest;
        sample_manifest_count++;
        return;
    }

    if (pos >= FS_SAMPLE_MANIFEST_MAX)
        return;

    for (uint8_t i = (uint8_t)(FS_SAMPLE_MANIFEST_MAX - 1u); i > pos; i--)
        sample_manifest[i] = sample_manifest[i - 1u];
    sample_manifest[pos] = *manifest;
}

static uint8_t filesystem_parseWav(const char *filename, fs_sample_manifest_t *out)
{
    afatfsFilePtr_t file = filesystem_blockOpen(filename);
    uint8_t hdr[16];
    uint8_t fmt_ok = 0;
    uint8_t data_ok = 0;
    uint32_t data_offset = 0;
    uint32_t data_bytes = 0;

    if (file == NULL)
        return 0;

    if (filesystem_blockRead(file, hdr, 12u) != 12u)
        goto fail;
    if (!filesystem_chunkIdEquals(hdr, "RIFF") ||
        !filesystem_chunkIdEquals(hdr + 8u, "WAVE"))
        goto fail;

    while (!afatfs_feof(file)) {
        uint32_t chunk_data_pos;
        uint32_t chunk_size;
        uint32_t next_chunk_pos;

        if (filesystem_blockRead(file, hdr, 8u) != 8u)
            break;
        chunk_size = filesystem_le32(hdr + 4u);
        if (!filesystem_blockTell(file, &chunk_data_pos))
            goto fail;

        next_chunk_pos = chunk_data_pos + chunk_size + (chunk_size & 1u);
        if (next_chunk_pos < chunk_data_pos)
            goto fail;

        if (filesystem_chunkIdEquals(hdr, "fmt ")) {
            if (chunk_size < 16u)
                goto fail;
            if (filesystem_blockRead(file, hdr, 16u) != 16u)
                goto fail;

            fmt_ok = (uint8_t)(filesystem_le16(hdr + 0u) == 1u &&
                               filesystem_le16(hdr + 2u) == 1u &&
                               filesystem_le32(hdr + 4u) == 44100u &&
                               filesystem_le16(hdr + 14u) == 16u);
            if (!fmt_ok)
                goto fail;
        } else if (filesystem_chunkIdEquals(hdr, "data")) {
            data_offset = chunk_data_pos;
            data_bytes = chunk_size;
            data_ok = (uint8_t)(data_bytes != 0u && ((data_bytes & 1u) == 0u));
        }

        if (!filesystem_blockSeek(file, next_chunk_pos))
            goto fail;
        if (fmt_ok && data_ok)
            break;
    }

    if (!fmt_ok || !data_ok)
        goto fail;

    memset(out, 0, sizeof(*out));
    for (uint8_t i = 0; i < 12u && filename[i] != '\0'; i++)
        out->filename[i] = filename[i];
    filesystem_sampleNameFromFilename(out->name, filename);
    filesystem_displayNameFromFilename(out->display_name, filename);
    out->data_offset = data_offset;
    out->data_bytes = data_bytes;

    (void)filesystem_blockClose(file);
    return 1;

fail:
    (void)filesystem_blockClose(file);
    return 0;
}

static uint8_t filesystem_manifestCanFit(uint32_t *planned_addr, uint32_t data_bytes)
{
    uint32_t needed = (data_bytes + 3u) & ~3u;
    needed += 4u;
    if (*planned_addr > SAMPLE_INFO_START_ADDRESS)
        return 0;
    if (needed > (SAMPLE_INFO_START_ADDRESS - *planned_addr))
        return 0;
    *planned_addr += needed;
    return 1;
}

static uint8_t filesystem_scanSamples(afatfsFilePtr_t dir, uint32_t planned_addr,
                                      uint8_t max_new_samples)
{
    afatfsFinder_t finder;
    char lfn_name[FS_SAMPLE_LFN_MAX];
    uint8_t lfn_valid = 0;

    sample_manifest_count = 0;
    filesystem_lfnReset(lfn_name, &lfn_valid);
    afatfs_findFirst(dir, &finder);

    for (;;) {
        fatDirectoryEntry_t *entry = NULL;
        afatfsOperationStatus_e st;
        char filename[13];
        fs_sample_manifest_t manifest;

        do {
            st = afatfs_findNext(dir, &finder, &entry);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                filesystem_blockPoll();
            if (!filesystem_blockFsOk()) {
                afatfs_findLast(dir);
                return 0;
            }
        } while (st == AFATFS_OPERATION_IN_PROGRESS);

        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLast(dir);
            return 0;
        }
        if (entry == NULL || fat_isDirectoryEntryTerminator(entry))
            break;
        if (fat_isDirectoryEntryEmpty(entry)) {
            filesystem_lfnReset(lfn_name, &lfn_valid);
            continue;
        }
        if ((entry->attrib & 0x0fu) == 0x0fu) {
            filesystem_lfnAppendEntry(entry, lfn_name, &lfn_valid);
            continue;
        }
        if ((entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) ||
            (entry->attrib & FAT_FILE_ATTRIBUTE_VOLUME_ID)) {
            filesystem_lfnReset(lfn_name, &lfn_valid);
            continue;
        }

        memset(filename, 0, sizeof(filename));
        fat_convertFATStyleToFilename(entry->filename, filename);
        fat_applyFilenameCaseFlags(filename, entry->ntReserved);
        const char *display_filename =
            (lfn_valid && filesystem_isWavName(lfn_name)) ? lfn_name : filename;

        if (!filesystem_isWavName(filename)) {
            filesystem_lfnReset(lfn_name, &lfn_valid);
            continue;
        }

        afatfs_findLast(dir);
        if (!filesystem_parseWav(filename, &manifest))
            goto scan_next_entry;
        filesystem_displayNameFromFilename(manifest.display_name, display_filename);
        filesystem_copySampleSortName(manifest.sort_name, display_filename);
        filesystem_insertSampleManifest(&manifest);

scan_next_entry:
        filesystem_lfnReset(lfn_name, &lfn_valid);
    }

    afatfs_findLast(dir);

    uint8_t out = 0;
    for (uint8_t i = 0; i < sample_manifest_count; i++) {
        if (out >= max_new_samples)
            break;
        if (!filesystem_manifestCanFit(&planned_addr, sample_manifest[i].data_bytes))
            continue;
        if (out != i)
            sample_manifest[out] = sample_manifest[i];
        out++;
    }
    sample_manifest_count = out;
    return 1;
}

static uint8_t filesystem_installOneSample(const fs_sample_manifest_t *manifest,
                                           uint8_t looped)
{
    afatfsFilePtr_t file = filesystem_blockOpen(manifest->filename);
    uint32_t remaining = manifest->data_bytes;
    int rc;

    if (file == NULL)
        return 0;
    if (!filesystem_blockSeek(file, manifest->data_offset))
        goto fail;

    rc = sampleMemory_installStartSample(manifest->name, manifest->display_name,
                                         looped,
                                         manifest->data_bytes);
    if (rc != 0)
        goto fail;

    while (remaining > 0u) {
        uint32_t to_read = remaining;
        uint32_t to_write;

        if (to_read > FS_SAMPLE_BUFFER_SIZE)
            to_read = FS_SAMPLE_BUFFER_SIZE;

        if (filesystem_blockRead(file, sample_io_buf, to_read) != to_read)
            goto fail;

        remaining -= to_read;
        to_write = to_read;
        if (remaining == 0u) {
            uint32_t padded = (to_write + 3u) & ~3u;
            while (to_write < padded)
                sample_io_buf[to_write++] = 0;
        }

        if (sampleMemory_installWriteData(sample_io_buf, to_write) != 0)
            goto fail;
    }

    if (sampleMemory_installFinishSample() != 0)
        goto fail;

    (void)filesystem_blockClose(file);
    return 1;

fail:
    (void)filesystem_blockClose(file);
    return 0;
}

static uint8_t filesystem_installSampleFolderBlocking(const char *folder,
                                                      uint8_t append,
                                                      uint8_t looped)
{
    afatfsFilePtr_t sample_dir;
    uint8_t ok = 0;
    uint32_t planned_addr;
    uint8_t max_new_samples = SAMPLE_MAX_COUNT;

    if (status == FS_STATUS_BUSY)
        return 0;
    if (!filesystem_blockFsOk())
        return 0;

    status = FS_STATUS_BUSY;
    current_op = FS_INTERNAL_OP_NONE;

    if (append) {
        if (sampleMemory_installAppendBegin() != 0)
            goto done;
        planned_addr = SAMPLE_INFO_START_ADDRESS - sampleMemory_installBytesFree();
        max_new_samples = (uint8_t)(SAMPLE_MAX_COUNT - sampleMemory_getNumSamples());
    } else {
        planned_addr = SAMPLE_ROM_START_ADDRESS + 4u;
    }

    sample_dir = filesystem_blockOpen(folder);
    if (sample_dir == NULL)
        goto done;

    if (!filesystem_blockChdir(sample_dir))
        goto close_dir;

    if (!filesystem_scanSamples(sample_dir, planned_addr, max_new_samples))
        goto root_and_close;

    if (!append) {
        if (sampleMemory_installBegin() != 0)
            goto root_and_close;
    }

    ok = 1;
    for (uint8_t i = 0; i < sample_manifest_count; i++) {
        if (!filesystem_installOneSample(&sample_manifest[i], looped)) {
            ok = 0;
            break;
        }
    }

    if (ok && sampleMemory_installCommit() != 0)
        ok = 0;

root_and_close:
    (void)filesystem_blockChdir(NULL);
close_dir:
    (void)filesystem_blockClose(sample_dir);
done:
    current_op = FS_INTERNAL_OP_NONE;
    status = FS_STATUS_IDLE;
    return ok;
}

uint8_t filesystem_installSamplesBlocking(void)
{
    return filesystem_installSampleFolderBlocking("samples", 0, 0);
}

uint8_t filesystem_installLoopsBlocking(void)
{
    return filesystem_installSampleFolderBlocking("loops", 1, 1);
}

/* -----------------------------------------------------------------------
** Generic asyncfatfs File/Dir test operations
** ----------------------------------------------------------------------- */
static void filesystem_scanTestObjects_tick(uint8_t want_dirs)
{
    switch (op_phase) {
    case 0:
        /*
         * Always scan from the volume root.
         *
         * These menus are deliberately generic asyncfatfs tests, not Kit/Scene
         * browsers. Resetting CWD to root makes the visible list independent
         * of whichever older storage state machine last entered a directory.
         */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        afatfs_findFirstObject(op_test_dir, &op_test_object_finder);
        op_phase = 2u;
        return;

    case 2:
        for (;;) {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_test_dir,
                                      &op_test_object_finder,
                                      &op_test_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                op_phase = 5u;
                return;
            }
            if (op_test_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                op_phase = 3u;
                return;
            }
            /*
             * Insert only the requested object class. The iterator has already
             * hidden VFAT fragments, deleted entries, labels, and structural
             * dot entries. It intentionally does not hide ordinary names that
             * begin with '.', because dot-prefixed files and directories are
             * legal FAT objects and the diagnostic browser must reflect the
             * filesystem exactly.
             */
            if (!want_dirs && op_test_object.id.kind == AFATFS_OBJECT_FILE) {
                filesystem_insertTestName(fs_test_file_name,
                                          &fs_test_file_count,
                                          op_test_object.id.displayName);
            } else if (want_dirs &&
                       op_test_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                filesystem_insertTestName(fs_test_dir_name,
                                          &fs_test_dir_count,
                                          op_test_object.id.displayName);
            }
        }

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        filesystem_finish(FS_STATUS_DONE);
        return;

    case 5:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 6u;
        return;

    case 6:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_loadTestFile_tick(void)
{
    switch (op_phase) {
    case 0:
        memset(op_test_bytes, 0, sizeof(op_test_bytes));
        op_test_result_kind = FS_TEST_RESULT_BYTES_READY;
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(op_test_name, "r",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              op_test_short_alias,
                              on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_phase = 2u;
        return;

    case 2:
        if (!filesystem_readTestBytesTick())
            return;
        op_phase = 3u;
        return;

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_file = NULL;
        filesystem_finish(FS_STATUS_DONE);
        return;
    }
}

static void filesystem_loadTestDir_tick(void)
{
    switch (op_phase) {
    case 0:
        memset(op_test_bytes, 0, sizeof(op_test_bytes));
        memset(op_test_child_name, 0, sizeof(op_test_child_name));
        op_test_best_kind = AFATFS_OBJECT_NONE;
        op_test_result_kind = FS_TEST_RESULT_BYTES_READY;
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(op_test_name,
                                AFATFS_MATCH_CASE_SENSITIVE,
                                op_test_short_alias,
                                on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        if (!afatfs_chdir(op_test_dir))
            return;
        afatfs_findFirstObject(op_test_dir, &op_test_object_finder);
        op_phase = 2u;
        return;

    case 2:
        for (;;) {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_test_dir,
                                      &op_test_object_finder,
                                      &op_test_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                op_phase = 10u;
                return;
            }
            if (op_test_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                op_phase = 3u;
                return;
            }
            /*
             * Retain the first alphanumerically-sorted child by exact display
             * case. This avoids depending on raw directory order, which can
             * vary after deletes or host-side file creation. Dot-prefixed
             * names remain eligible here: they are real filesystem entries,
             * and this diagnostic path must not apply hidden-file policy above
             * asyncfatfs.
             */
            if (op_test_best_kind == AFATFS_OBJECT_NONE ||
                fat_compareDisplayName(op_test_object.id.displayName,
                                       op_test_child_name,
                                       true) < 0) {
                op_test_best_kind = op_test_object.id.kind;
                filesystem_copyTestName(op_test_child_name,
                                        op_test_object.id.displayName);
            }
        }

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        if (op_test_best_kind == AFATFS_OBJECT_NONE) {
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (op_test_best_kind == AFATFS_OBJECT_DIRECTORY) {
            op_test_result_kind = FS_TEST_RESULT_DIRECTORY;
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(op_test_child_name, "r",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              op_test_short_alias,
                              on_file_opened))
            return;
        op_phase = 5u;
        return;

    case 5:
        if (!op_file_ready)
            return;
        if (!op_file) {
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_phase = 6u;
        return;

    case 6:
        if (!filesystem_readTestBytesTick())
            return;
        op_phase = 7u;
        return;

    case 7:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 8u;
        return;

    case 8:
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    case 10:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 11u;
        return;

    case 11:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        (void)afatfs_chdir(NULL);
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_saveTestFile_tick(void)
{
    switch (op_phase) {
    case 0:
        filesystem_makeTestBytes();
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(op_test_name, "w",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              op_test_short_alias,
                              on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_phase = 2u;
        return;

    case 2:
        if (!filesystem_writeTestBytesTick())
            return;
        op_phase = 3u;
        return;

    case 3:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4u;
        return;

    case 4:
        if (!op_close_done)
            return;
        op_file = NULL;
        filesystem_finish(FS_STATUS_DONE);
        return;
    }
}

static void filesystem_saveTestDir_tick(void)
{
    switch (op_phase) {
    case 0:
        filesystem_makeTestBytes();
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        memset(op_root_open_name, 0, sizeof(op_root_open_name));
        /*
         * Save:[Dir] now targets /Kit/<name>/ instead of /<name>/.
         *
         * The root Kit directory is created on demand so a clean card can run
         * this diagnostic without a separate setup step. The user-entered
         * component remains exact-case LFN data for the child directory below.
         */
        if (!afatfs_mkdir_lfn(STORAGE_ROOT_KIT,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_root_open_name,
                              on_file_opened))
            return;
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setTestDiag("RootOpn");
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3u;
        return;

    case 3:
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4u;
        return;

    case 4:
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5u;
        return;

    case 5:
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_phase = 6u;
        return;

    case 6:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_mkdir_lfn(op_test_name,
                              AFATFS_MATCH_CASE_SENSITIVE,
                              op_test_short_alias,
                              on_file_opened))
            return;
        op_phase = 7u;
        return;

    case 7:
        if (!op_file_ready)
            return;
        if (!op_file) {
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        if (!afatfs_chdir(op_test_dir))
            return;
        op_phase = 8u;
        return;

    case 8:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 9u;
        return;

    case 9:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        /*
         * Save:[Dir] writes a child file with the same exact display name as
         * the directory, proving both LFN directory creation and LFN file
         * creation inside that new current directory.
         */
        if (!afatfs_fopen_lfn(op_test_name, "w",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              op_test_short_alias,
                              on_file_opened))
            return;
        op_phase = 10u;
        return;

    case 10:
        if (!op_file_ready)
            return;
        if (!op_file) {
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_phase = 11u;
        return;

    case 11:
        if (!filesystem_writeTestBytesTick())
            return;
        op_phase = 12u;
        return;

    case 12:
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 13u;
        return;

    case 13:
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
        return;
    }
}

static void filesystem_saveTestSimpleDir_tick(void)
{
    switch (op_phase) {
    case 0:
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 2u;
        return;

    case 2:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        afatfs_findFirstObject(op_test_dir, &op_test_object_finder);
        op_phase = 3u;
        return;

    case 3:
        for (;;) {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_test_dir,
                                      &op_test_object_finder,
                                      &op_test_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                filesystem_setTestDiag("RootScn");
                op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                op_phase = 4u;
                return;
            }
            if (op_test_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                op_test_lookup_result = FS_TEST_LOOKUP_CREATE;
                op_phase = 4u;
                return;
            }
            if (fat_compareDisplayName(op_test_object.id.displayName,
                                       op_test_name,
                                       true) == 0) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                if (op_test_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                    memcpy(op_test_parent_alias,
                           op_test_object.id.shortName,
                           sizeof(op_test_parent_alias));
                    op_test_lookup_result = FS_TEST_LOOKUP_OPEN_ALIAS;
                } else {
                    filesystem_setTestDiag("RootFil");
                    op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                }
                op_phase = 4u;
                return;
            }
        }

    case 4:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 5u;
        return;

    case 5:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (op_test_lookup_result == FS_TEST_LOOKUP_OPEN_ALIAS) {
            if (!afatfs_opendir(op_test_parent_alias, on_file_opened)) {
                filesystem_setTestDiag("RootQue");
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
        } else if (op_test_lookup_result == FS_TEST_LOOKUP_CREATE) {
            if (!afatfs_mkdir_lfn(op_test_name,
                                  AFATFS_MATCH_CASE_SENSITIVE,
                                  op_test_parent_alias,
                                  on_file_opened)) {
                filesystem_setTestDiag("RootQue");
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
        } else {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 6u;
        return;

    case 6:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setTestDiag("RootCb");
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        if (!afatfs_chdir(op_test_dir))
            return;
        op_phase = 7u;
        return;

    case 7:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 8u;
        return;

    case 8:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        op_phase = 9u;
        return;

    case 9:
        if (!afatfs_sync())
            return;
        op_phase = 10u;
        return;

    case 10:
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 11u;
        return;

    case 11:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setTestDiag("ParOpn");
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        afatfs_findFirstObject(op_test_dir, &op_test_object_finder);
        op_phase = 12u;
        return;

    case 12:
        for (;;) {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_test_dir,
                                      &op_test_object_finder,
                                      &op_test_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                filesystem_setTestDiag("ParScn");
                op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                op_phase = 13u;
                return;
            }
            if (op_test_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                op_test_lookup_result = FS_TEST_LOOKUP_CREATE;
                op_phase = 13u;
                return;
            }
            if (fat_compareDisplayName(op_test_object.id.displayName,
                                       op_test_name,
                                       true) == 0) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                if (op_test_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                    memcpy(op_test_short_alias,
                           op_test_object.id.shortName,
                           sizeof(op_test_short_alias));
                    op_test_lookup_result = FS_TEST_LOOKUP_OPEN_ALIAS;
                } else {
                    filesystem_setTestDiag("ParFile");
                    op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                }
                op_phase = 13u;
                return;
            }
        }

    case 13:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 14u;
        return;

    case 14:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (op_test_lookup_result == FS_TEST_LOOKUP_OPEN_ALIAS) {
            if (!afatfs_opendir(op_test_short_alias, on_file_opened)) {
                filesystem_setTestDiag("ChdQue");
                (void)afatfs_chdir(NULL);
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
        } else if (op_test_lookup_result == FS_TEST_LOOKUP_CREATE) {
            if (!afatfs_mkdir_lfn(op_test_name,
                                  AFATFS_MATCH_CASE_SENSITIVE,
                                  op_test_short_alias,
                                  on_file_opened)) {
                filesystem_setTestDiag("ChdQue");
                (void)afatfs_chdir(NULL);
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
        } else {
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 15u;
        return;

    case 15:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setTestDiag("ChdCb");
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        op_phase = 16u;
        return;

    case 16:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 17u;
        return;

    case 17:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        if (!afatfs_opendir(op_test_parent_alias, on_file_opened)) {
            filesystem_setTestDiag("VerQue");
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 18u;
        return;

    case 18:
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_setTestDiag("VerOpn");
            (void)afatfs_chdir(NULL);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_test_dir = op_file;
        afatfs_findFirstObject(op_test_dir, &op_test_object_finder);
        op_test_verify_seen_alias = 0u;
        op_test_verify_seen_fold = 0u;
        op_phase = 19u;
        return;

    case 19:
        for (;;) {
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_test_dir,
                                      &op_test_object_finder,
                                      &op_test_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (st == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                filesystem_setTestDiag("VerScn");
                op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                op_phase = 20u;
                return;
            }
            if (op_test_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                if (op_test_verify_seen_alias)
                    filesystem_setTestDiag("VerSfn");
                else if (op_test_verify_seen_fold)
                    filesystem_setTestDiag("VerFold");
                else
                    filesystem_setTestDiag("VerNone");
                op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                op_phase = 20u;
                return;
            }
            if (fat_compareDisplayName(op_test_object.id.displayName,
                                       op_test_name,
                                       false) == 0) {
                op_test_verify_seen_fold = 1u;
            }
            if (op_test_short_alias[0] != '\0' &&
                fat_compareDisplayName(op_test_object.id.shortName,
                                       op_test_short_alias,
                                       false) == 0) {
                op_test_verify_seen_alias = 1u;
            }
            if (fat_compareDisplayName(op_test_object.id.displayName,
                                       op_test_name,
                                       true) == 0) {
                if (op_test_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                    afatfs_findLastObject(op_test_dir,
                                          &op_test_object_finder);
                    filesystem_copyTestName(op_test_child_name, op_test_name);
                    op_test_result_kind = FS_TEST_RESULT_DIRECTORY;
                    op_test_lookup_result = FS_TEST_LOOKUP_OPEN_ALIAS;
                    op_phase = 20u;
                    return;
                }
                afatfs_findLastObject(op_test_dir, &op_test_object_finder);
                filesystem_setTestDiag("VerFile");
                op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
                op_phase = 20u;
                return;
            }
        }

    case 20:
        op_close_done = false;
        if (afatfs_fclose(op_test_dir, on_file_closed))
            op_phase = 21u;
        return;

    case 21:
        if (!op_close_done)
            return;
        op_test_dir = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(op_test_lookup_result == FS_TEST_LOOKUP_OPEN_ALIAS
                          ? FS_STATUS_DONE
                          : FS_STATUS_ERROR);
        return;
    }
}

/* =======================================================================
** Public API
** ======================================================================= */

void filesystem_initAfterCardReady(void)
{
    current_op = FS_INTERNAL_OP_NONE;
    status = FS_STATUS_IDLE;
    memset(fs_error_code, 0, sizeof(fs_error_code));
    afatfs_init();
}

uint8_t filesystem_initCardAndMountBlocking(void)
{
    uint8_t sd_init_result;

    fs_boot_detected_unsupported_card = 0;
    fs_last_mount_result = FS_MOUNT_RESULT_UNKNOWN;
    spi_sd_set_slow();
    sd_init_result = SD_init();
    if (sd_init_result != 0u) {
        fs_last_mount_result = (sd_init_result == 1u) ?
            FS_MOUNT_RESULT_NO_CARD : FS_MOUNT_RESULT_CARD_INIT_FAILED;
        return 0;
    }

    spi_sd_set_fast();
    filesystem_initAfterCardReady();
    while (afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_INITIALIZATION)
        filesystem_tick();

    if (afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_READY) {
        fs_last_mount_result = FS_MOUNT_RESULT_READY;
        return 1;
    }

    if (afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_FATAL &&
        filesystem_detectUnsupportedCardLayout()) {
        fs_boot_detected_unsupported_card = 1;
        fs_last_mount_result = FS_MOUNT_RESULT_UNSUPPORTED_CARD;
    } else {
        fs_last_mount_result = FS_MOUNT_RESULT_MOUNT_FAILED;
    }

    return 0;
}

void filesystem_tick(void)
{
    /* Busy operations poll asyncfatfs every pass so reads/writes make progress
    ** whenever the main loop has slack. When the public filesystem facade is
    ** idle, poll only every FS_IDLE_POLL_MS; that removes steady background SD
    ** housekeeping from the audio foreground budget without changing active
    ** transfer behavior. asyncfatfs must still be polled from one context only. */
    if (status == FS_STATUS_BUSY ||
        afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY) {
        afatfs_poll();
    } else {
        uint16_t now = time_sysTick;
        if ((uint16_t)(now - fs_last_idle_poll_tick) >= FS_IDLE_POLL_MS) {
            fs_last_idle_poll_tick = now;
            afatfs_poll();
        }
    }

    if (status != FS_STATUS_BUSY) return;

    switch (current_op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:
        filesystem_flushFinish_tick();
        break;
    case FS_INTERNAL_OP_LOAD_KIT:
    case FS_INTERNAL_OP_LOAD_KIT_MORPH:
        filesystem_loadKitDirectory_tick();
        break;
    case FS_INTERNAL_OP_LOAD_SCENE:
        filesystem_loadSceneDirectory_tick();
        break;
    case FS_INTERNAL_OP_LOAD_BANK:
        filesystem_loadBankDirectory_tick();
        break;
    case FS_INTERNAL_OP_SAVE_KIT:
        filesystem_saveKitDirectory_tick();
        break;
    case FS_INTERNAL_OP_SAVE_SCENE:
        filesystem_saveSceneDirectory_tick();
        break;
    case FS_INTERNAL_OP_SAVE_BANK:
        filesystem_saveBankDirectory_tick();
        break;
    case FS_INTERNAL_OP_LOAD_INSTRUMENT:
        filesystem_loadInstrument_tick();
        break;
    case FS_INTERNAL_OP_SAVE_INSTRUMENT:
    case FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH:
        filesystem_saveInstrument_tick();
        break;
    case FS_INTERNAL_OP_LOAD_MORPH:
        filesystem_loadKit_tick();
        break;
    case FS_INTERNAL_OP_LOAD_PATTERN:
        filesystem_loadPattern_tick();
        break;
    case FS_INTERNAL_OP_SAVE_PATTERN:
        filesystem_savePattern_tick();
        break;
    case FS_INTERNAL_OP_LOAD_ALL:
    case FS_INTERNAL_OP_LOAD_PERFORMANCE:
        filesystem_loadContainer_tick();
        break;
    case FS_INTERNAL_OP_SAVE_ALL:
    case FS_INTERNAL_OP_SAVE_PERFORMANCE:
        filesystem_saveContainer_tick();
        break;
    case FS_INTERNAL_OP_LOAD_GLOBALS:
        filesystem_loadGlobals_tick();
        break;
    case FS_INTERNAL_OP_SAVE_GLOBALS:
        filesystem_saveGlobals_tick();
        break;
    case FS_INTERNAL_OP_SCAN_KITS:
        filesystem_scanKits_tick();
        break;
    case FS_INTERNAL_OP_SCAN_SCENES:
        filesystem_scanScenes_tick();
        break;
    case FS_INTERNAL_OP_SCAN_BANKS:
        filesystem_scanBanks_tick();
        break;
    case FS_INTERNAL_OP_SCAN_BANK_SCENES:
        filesystem_scanBankScenes_tick();
        break;
    case FS_INTERNAL_OP_SCAN_INSTRUMENTS:
        filesystem_scanInstruments_tick();
        break;
    case FS_INTERNAL_OP_LOAD_NAME:
        filesystem_loadName_tick();
        break;
    case FS_INTERNAL_OP_SCAN_TEST_FILES:
        filesystem_scanTestObjects_tick(0u);
        break;
    case FS_INTERNAL_OP_SCAN_TEST_DIRS:
        filesystem_scanTestObjects_tick(1u);
        break;
    case FS_INTERNAL_OP_LOAD_TEST_FILE:
        filesystem_loadTestFile_tick();
        break;
    case FS_INTERNAL_OP_LOAD_TEST_DIR:
        filesystem_loadTestDir_tick();
        break;
    case FS_INTERNAL_OP_SAVE_TEST_FILE:
        filesystem_saveTestFile_tick();
        break;
    case FS_INTERNAL_OP_SAVE_TEST_DIR:
        filesystem_saveTestDir_tick();
        break;
    case FS_INTERNAL_OP_SAVE_TEST_SIMPLE_DIR:
        filesystem_saveTestSimpleDir_tick();
        break;
    default: break;
    }
}

fs_status_t filesystem_status(void)
{
    return status;
}

const char *filesystem_errorCode(void)
{
    return fs_error_code;
}

void filesystem_ack(void)
{
    if (status == FS_STATUS_DONE || status == FS_STATUS_ERROR) {
        status = FS_STATUS_IDLE;
        current_op = FS_INTERNAL_OP_NONE;
    }
}

/* -----------------------------------------------------------------------
** Request functions
** ----------------------------------------------------------------------- */
static bool filesystem_start(fs_internal_op_t op, fs_file_type_t type,
                             uint16_t slot, fs_completion_cb_t cb)
{
    if (status == FS_STATUS_BUSY) return false;
    status = FS_STATUS_BUSY;
    current_op = op;
    op_phase = 0;
    op_slot = slot;
    op_file_type = type;
    memset(fs_error_code, 0, sizeof(fs_error_code));
    op_file = NULL;
    op_file_ready = false;
    op_close_done = false;
    op_close_status = FS_STATUS_DONE;
    op_test_lookup_result = FS_TEST_LOOKUP_ERROR;
    op_flush_final_status = FS_STATUS_DONE;
    op_bytes_done = 0;
    /*
     * Reset the generic File/Dir test scratch for every filesystem request.
     *
     * The test result is shared by all four new menu operations, so it must be
     * cleared even when the next request is an old Kit/Scene operation. That
     * prevents a later PRESET_OP_TEST_* completion from displaying bytes or a
     * child-directory label that belonged to an earlier save/load attempt.
     */
    memset(op_test_name, 0, sizeof(op_test_name));
    memset(op_test_child_name, 0, sizeof(op_test_child_name));
    memset(op_test_short_alias, 0, sizeof(op_test_short_alias));
    memset(op_test_parent_alias, 0, sizeof(op_test_parent_alias));
    memset(op_test_bytes, 0, sizeof(op_test_bytes));
    op_test_result_kind = FS_TEST_RESULT_BYTES_READY;
    op_test_best_kind = AFATFS_OBJECT_NONE;
    op_test_dir = NULL;
    op_test_verify_seen_alias = 0u;
    op_test_verify_seen_fold = 0u;
    op_stream_index = 0;
    op_item_offset = 0;
    op_loaded_active_pattern_running = 0;
    op_file_version = 0;
    op_write_line_len = 0u;
    op_write_line_offset = 0u;
    op_write_line_index = 0u;
    op_remove_done = 0u;
    op_kit_save_source_scene = 0u;
    op_kit_save_mode = STORAGE_INSTRUMENT_SAVE_NORMAL;
    memset(op_save_kit_dir_display_name, 0,
           sizeof(op_save_kit_dir_display_name));
    memset(op_save_kit_member_display_file, 0,
           sizeof(op_save_kit_member_display_file));
    memset(op_save_kit_dir_open_name, 0, sizeof(op_save_kit_dir_open_name));
    memset(op_save_scene_kit_display_name, 0,
           sizeof(op_save_scene_kit_display_name));
    memset(op_save_scene_kit_open_name, 0,
           sizeof(op_save_scene_kit_open_name));
    op_delete_slot_phase = FS_DELETE_SLOT_IDLE;
    op_delete_slot_dir = NULL;
    op_delete_slot_allow_short_alias = 0u;
    op_delete_slot_bank_scene = 0u;
    op_delete_slot_number = 0u;
    op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;
    op_delete_tree_phase = FS_DELETE_TREE_IDLE;
    op_delete_tree_depth = 0u;
    op_delete_tree_dir = NULL;
    memset(op_delete_tree_name_stack, 0, sizeof(op_delete_tree_name_stack));
    memset(op_delete_tree_open_name_stack, 0,
           sizeof(op_delete_tree_open_name_stack));
    memset(op_delete_tree_child_name, 0, sizeof(op_delete_tree_child_name));
    memset(op_delete_tree_child_open_name, 0,
           sizeof(op_delete_tree_child_open_name));
    memset(&op_staged_scene, 0, sizeof(op_staged_scene));
    op_scene_load_scene_mask = 0u;
    memset(op_scene_display_name, 0, sizeof(op_scene_display_name));
    memset(op_scene_child_open_name, 0, sizeof(op_scene_child_open_name));
    memset(op_scene_child_display_name, 0,
           sizeof(op_scene_child_display_name));
    memset(op_scene_pattern_open_name, 0, sizeof(op_scene_pattern_open_name));
    memset(op_scene_effect_open_name, 0, sizeof(op_scene_effect_open_name));
    memset(&op_bankset_state, 0, sizeof(op_bankset_state));
    memset(op_bank_display_name, 0, sizeof(op_bank_display_name));
    memset(op_save_bank_dir_display_name, 0,
           sizeof(op_save_bank_dir_display_name));
    memset(op_save_bank_tmp_display_name, 0,
           sizeof(op_save_bank_tmp_display_name));
    memset(op_save_bank_old_display_name, 0,
           sizeof(op_save_bank_old_display_name));
    memset(op_save_bank_dir_open_name, 0, sizeof(op_save_bank_dir_open_name));
    memset(op_save_bank_rename_open_name, 0,
           sizeof(op_save_bank_rename_open_name));
    op_save_bank_scratch_attempts = 0u;
    op_save_bank_scratch_collision = 0u;
    memset(op_bank_child_present, 0, sizeof(op_bank_child_present));
    memset(op_bank_child_name, 0, sizeof(op_bank_child_name));
    memset(op_bank_child_open_name, 0, sizeof(op_bank_child_open_name));
    op_bank_child_present_mask = 0u;
    op_bank_scene_load_mask = 0u;
    op_bank_scene_save_mask = 0u;
    op_bank_active_scene = 0u;
    op_bank_child_cursor = 0u;
    op_bank_loaded_scene = 0u;
    op_bank_payload_active = 0u;
    op_rename_done = 0u;
    op_kit_root_dir = NULL;
    op_kit_slot_dir = NULL;
    op_lfn_valid = 0;
    op_line_len = 0;
    op_instrument_slot = 0;
    op_kit_load_scene_mask = 0u;
    op_instrument_load_destination_slot = 0u;
    op_instrument_load_destination_scene = 0u;
    op_instrument_load_type = INSTRUMENT_TYPE_UNKNOWN;
    op_instrument_load_index = 0u;
    op_instrument_save_source_scene = 0u;
    op_instrument_save_source_slot = 0u;
    op_instrument_save_type = INSTRUMENT_TYPE_UNKNOWN;
    op_instrument_save_mode = STORAGE_INSTRUMENT_SAVE_NORMAL;
    memset(op_instrument_save_display_name, 0,
           sizeof(op_instrument_save_display_name));
    memset(op_instrument_save_open_name, 0,
           sizeof(op_instrument_save_open_name));
    memset(&op_staged_instrument, 0, sizeof(op_staged_instrument));
    memset(op_staged_instrument_display_name, 0,
           sizeof(op_staged_instrument_display_name));
    memset(op_staged_instrument_stem, 0, sizeof(op_staged_instrument_stem));
    completion_callback = cb;
    return true;
}

bool filesystem_requestLoad(fs_file_type_t type, uint16_t slot, fs_completion_cb_t cb)
{
    const fs_file_desc_t *desc = filesystem_desc(type);
    if (desc == NULL || !desc->supports_load)
        return false;

    switch (type) {
    case FS_FILE_KIT:
        return filesystem_requestLoadKitForScenes(
            slot, (uint16_t)(1u << scene_getActiveIndex()), cb);
    case FS_FILE_SCENE:
        return filesystem_requestLoadSceneForScenes(
            slot, (uint16_t)(1u << scene_getActiveIndex()), cb);
    case FS_FILE_BANK:
        return filesystem_requestLoadBank(
            slot, (uint16_t)(1u << scene_getActiveIndex()), cb);
    case FS_FILE_MORPH:
        return filesystem_start(FS_INTERNAL_OP_LOAD_MORPH, type, slot, cb);
    case FS_FILE_PATTERN:
        return filesystem_start(FS_INTERNAL_OP_LOAD_PATTERN, type, slot, cb);
    case FS_FILE_ALL:
        return filesystem_start(FS_INTERNAL_OP_LOAD_ALL, type, slot, cb);
    case FS_FILE_PERFORMANCE:
        return filesystem_start(FS_INTERNAL_OP_LOAD_PERFORMANCE, type, slot, cb);
    case FS_FILE_SETTINGS:
        return filesystem_start(FS_INTERNAL_OP_LOAD_GLOBALS, type, 0, cb);
    default:
        return false;
    }
}

bool filesystem_requestLoadKitForScenes(uint16_t slot, uint16_t scene_mask,
                                        fs_completion_cb_t cb)
{
    uint8_t scene_index;
    uint16_t valid_mask = 0u;

    /*
     * Validate and start a staged multi-Scene Kit directory load.
     *
     * Inputs: Kit scan-cache slot and requested Scene bitmask. Output: one
     * asynchronous load operation, or false without changing live Scene data.
     * The validity loop deliberately filters mask bits against the resident
     * allocation so a future SCENE_COUNT increase remains bounded by the
     * current 16 physical SEQ buttons. Filesystem owns the staging lifecycle;
     * callers receive completion only after the eventual atomic commit.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((scene_mask & (uint16_t)(1u << scene_index)) != 0u)
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
    }
    if (valid_mask == 0u || slot >= STORAGE_KIT_MAX_SLOTS)
        return false;
    if (!kit_slot_present[slot]) {
        filesystem_setPresetNameEmpty();
        return false;
    }
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_KIT, FS_FILE_KIT, slot, cb))
        return false;
    op_kit_load_scene_mask = valid_mask;
    memset(&op_staged_kit, 0, sizeof(op_staged_kit));
    for (scene_index = 0u; scene_index < INSTRUMENT_SLOT_COUNT; scene_index++)
        memcpy(op_staged_kit.instrument_display_name[scene_index], "Empty   ", 9u);
    return true;
}

bool filesystem_requestSaveKitDirectory(uint16_t slot,
                                        uint8_t source_scene,
                                        const char display_name[8],
                                        uint8_t morph_projection,
                                        fs_completion_cb_t cb)
{
    const scene_t *scene = scene_getConst(source_scene);
    uint8_t voice;

    if (!scene || !display_name || slot >= STORAGE_KIT_MAX_SLOTS)
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_SAVE_KIT, FS_FILE_KIT, slot, cb))
        return false;

    op_kit_save_source_scene = source_scene;
    op_kit_save_mode = morph_projection ? STORAGE_INSTRUMENT_SAVE_MORPH
                                        : STORAGE_INSTRUMENT_SAVE_NORMAL;
    filesystem_makeNumberedDir(op_save_kit_dir_display_name,
                               slot,
                               display_name);

    for (voice = 0u; voice < STORAGE_KIT_SLOT_COUNT; voice++) {
        storage_makeSavedInstrumentDisplayFilename(
            op_save_kit_member_display_file[voice],
            sizeof(op_save_kit_member_display_file[voice]),
            scene->kit.instrument_stem[voice],
            scene->kit.instruments[voice].type,
            (uint8_t)(voice + 1u),
            1u);
    }
    return true;
}

bool filesystem_requestSaveSceneDirectory(uint16_t slot,
                                          uint8_t source_scene,
                                          const char display_name[8],
                                          fs_completion_cb_t cb)
{
    const scene_t *scene = scene_getConst(source_scene);
    uint8_t voice;

    /*
     * Capture all request-time Scene Save coordinates.
     *
     * Inputs: root Scene slot, source resident Scene, and edited display name.
     * Outputs: filesystem operation scratch now contains the target Scene
     * directory display name, embedded Kit directory display, and per-voice
     * member filenames. That Scene display name is used for root directory
     * creation/cache only; sceneset.scg does not write a self-name. Menu
     * movement after OK cannot retarget an in-flight save.
     */
    if (!scene || !display_name || slot >= STORAGE_SCENE_MAX_SLOTS)
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_SAVE_SCENE,
                          FS_FILE_SCENE,
                          slot,
                          cb)) {
        return false;
    }

    op_kit_save_source_scene = source_scene;
    op_kit_save_mode = STORAGE_INSTRUMENT_SAVE_NORMAL;
    filesystem_makeNumberedDir(op_save_kit_dir_display_name,
                               slot,
                               display_name);
    memcpy(op_scene_display_name, display_name, STORAGE_SCENE_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    filesystem_makeSceneEmbeddedKitDir(
        op_save_scene_kit_display_name,
        sizeof(op_save_scene_kit_display_name),
        scene->kit.display_name);

    for (voice = 0u; voice < STORAGE_KIT_SLOT_COUNT; voice++) {
        storage_makeSavedInstrumentDisplayFilename(
            op_save_kit_member_display_file[voice],
            sizeof(op_save_kit_member_display_file[voice]),
            scene->kit.instrument_stem[voice],
            scene->kit.instruments[voice].type,
            (uint8_t)(voice + 1u),
            1u);
    }
    return true;
}

bool filesystem_requestLoadKitMorphForScenes(uint16_t slot,
                                             uint16_t scene_mask,
                                             fs_completion_cb_t cb)
{
    uint8_t scene_index;
    uint16_t valid_mask = 0u;

    /*
     * Validate and start a staged KitMrp directory load.
     *
     * Inputs mirror normal Kit Load so the browser, missing-slot behavior, and
     * Scene selection mask stay identical. Output differs at the final state
     * machine phase: filesystem validates every kit file into op_staged_kit but
     * does not replace any live Scene. Preset owns the later same-type morph
     * endpoint copy because only Preset can preserve current slot identity and
     * refresh the Morph worker without reapplying routing.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((scene_mask & (uint16_t)(1u << scene_index)) != 0u)
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
    }
    if (valid_mask == 0u || slot >= STORAGE_KIT_MAX_SLOTS)
        return false;
    if (!kit_slot_present[slot]) {
        filesystem_setPresetNameEmpty();
        return false;
    }
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_KIT_MORPH, FS_FILE_KIT, slot, cb))
        return false;
    op_kit_load_scene_mask = valid_mask;
    memset(&op_staged_kit, 0, sizeof(op_staged_kit));
    for (scene_index = 0u; scene_index < INSTRUMENT_SLOT_COUNT; scene_index++)
        memcpy(op_staged_kit.instrument_display_name[scene_index], "Empty   ", 9u);
    return true;
}

bool filesystem_requestSave(fs_file_type_t type, uint16_t slot, fs_completion_cb_t cb)
{
    const fs_file_desc_t *desc = filesystem_desc(type);
    if (desc == NULL || !desc->supports_save)
        return false;

    switch (type) {
    case FS_FILE_KIT:
        return false;
    case FS_FILE_SCENE:
        return false;
    case FS_FILE_MORPH:
        return false;
    case FS_FILE_PATTERN:
        return filesystem_start(FS_INTERNAL_OP_SAVE_PATTERN, type, slot, cb);
    case FS_FILE_ALL:
        return filesystem_start(FS_INTERNAL_OP_SAVE_ALL, type, slot, cb);
    case FS_FILE_PERFORMANCE:
        return filesystem_start(FS_INTERNAL_OP_SAVE_PERFORMANCE, type, slot, cb);
    case FS_FILE_SETTINGS:
        return filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS, type, 0, cb);
    default:
        return false;
    }
}

bool filesystem_requestLoadSceneForScenes(uint16_t slot,
                                          uint16_t scene_mask,
                                          fs_completion_cb_t cb)
{
    uint8_t scene_index;
    uint16_t valid_mask = 0u;

    /*
     * Validate and start a staged Scene directory load.
     *
     * Inputs mirror Kit Load: root Scene library slot plus destination Scene
     * mask. Output is one asynchronous operation that parses the Scene folder
     * into op_staged_scene and commits it only if sceneset.scg, the embedded
     * Kit, the bridge pattern, and placeholder effect all validate. The mask
     * filtering is deliberately shared with Kit Load so future 16-Scene banks
     * can call this same public boundary.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((scene_mask & (uint16_t)(1u << scene_index)) != 0u)
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
    }
    if (valid_mask == 0u || slot >= STORAGE_SCENE_MAX_SLOTS ||
        !scene_slot_present[slot])
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_SCENE, FS_FILE_SCENE, slot, cb))
        return false;
    op_scene_load_scene_mask = valid_mask;
    filesystem_initStagedScene(&op_staged_scene);
    return true;
}

bool filesystem_requestLoadBank(uint16_t slot,
                                uint16_t scene_mask,
                                fs_completion_cb_t cb)
{
    uint8_t scene_index;
    uint16_t valid_mask = 0u;

    /*
     * Validate and start a root Bank Load.
     *
     * Inputs: root Bank slot and resident Scene destination mask. Output:
     * Bank-level operation state plus a later Scene payload load if a
     * Bank-local 00..15 child exists. The mask loop matches Kit/Scene request
     * filtering so future SCENE_COUNT growth remains bounded by the 16 physical
     * Bank Scene buttons.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((scene_mask & (uint16_t)(1u << scene_index)) != 0u)
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
    }
    if (valid_mask == 0u || slot >= STORAGE_BANK_MAX_SLOTS ||
        !bank_slot_present[slot]) {
        return false;
    }
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_BANK, FS_FILE_BANK, slot, cb))
        return false;
    op_bank_scene_load_mask = valid_mask;
    op_scene_load_scene_mask = valid_mask;
    filesystem_initStagedScene(&op_staged_scene);
    memcpy(op_bank_display_name, bank_slot_name[slot], STORAGE_KIT_DISPLAY_NAME_LEN);
    op_bank_display_name[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    memcpy(preset_currentName, op_bank_display_name, 8u);
    return true;
}

bool filesystem_requestSaveBank(uint16_t slot,
                                uint8_t source_scene,
                                const char display_name[8],
                                uint16_t bank_scene_save_mask,
                                fs_completion_cb_t cb)
{
    uint16_t scratch_nonce;

    /*
     * Capture one Bank Save request.
     *
     * Inputs: root Bank slot/name, UI-selected source hint retained for API
     * compatibility, and 16-bit Bank-local Scene save mask. Output:
     * asynchronous Bank writer state. The mask drives the actual child loop;
     * each selected bit saves that same resident Scene index into the matching
     * Bank-local `SS Name` child folder. The source_scene argument is no longer
     * the only saved child, but it remains validated so older callers cannot
     * submit an out-of-range coordinate unnoticed.
     */
    if (!scene_getConst(source_scene) || !display_name ||
        slot >= STORAGE_BANK_MAX_SLOTS)
        return false;
    bank_scene_save_mask =
        (uint16_t)(bank_scene_save_mask &
                   (uint16_t)((1u << STORAGE_BANK_SCENE_MAX_SLOTS) - 1u));
    if (!filesystem_start(FS_INTERNAL_OP_SAVE_BANK, FS_FILE_BANK, slot, cb))
        return false;

    op_kit_save_source_scene = source_scene;
    op_kit_save_mode = STORAGE_INSTRUMENT_SAVE_NORMAL;
    op_bank_scene_save_mask = bank_scene_save_mask;
    op_bank_active_scene = bank_activeSceneSlot();
    if (op_bank_scene_save_mask != 0u &&
        (op_bank_scene_save_mask &
         (uint16_t)(1u << op_bank_active_scene)) == 0u) {
        uint8_t scene_index;

        /*
         * Keep saved Bank metadata internally reachable.
         *
         * Inputs: Save:[Bank] may intentionally save only a subset of resident
         * Scenes. Output: active_scene is moved to the first saved child when
         * the current active Scene is outside that subset. Without this guard,
         * bankset.bcg could point boot/load at an unsaved child directory.
         */
        for (scene_index = 0u;
             scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
             scene_index++) {
            if ((op_bank_scene_save_mask &
                 (uint16_t)(1u << scene_index)) != 0u) {
                op_bank_active_scene = scene_index;
                break;
            }
        }
    }
    op_bankset_state.active_scene = op_bank_active_scene;
    op_bankset_state.scene_mask_voice_edit = bank_sceneMaskVoiceEdit();
    op_bankset_state.seen_format = 1u;
    op_bankset_state.seen_version = 1u;
    op_bankset_state.seen_active_scene = 1u;
    op_bankset_state.seen_scene_mask_voice_edit = 1u;
    filesystem_makeNumberedDir(op_save_bank_dir_display_name,
                               slot,
                               display_name);
    scratch_nonce = filesystem_nextBankScratchNonce();
    filesystem_prepareBankScratchDirs(scratch_nonce);
    memcpy(op_bank_display_name, display_name, STORAGE_KIT_DISPLAY_NAME_LEN);
    op_bank_display_name[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    return true;
}

bool filesystem_requestScanKits(fs_completion_cb_t cb)
{
    if (status == FS_STATUS_BUSY) return false;
    /* The scan cache is authoritative for directory kits. Clear it before
    ** starting so a failed or missing Kit/ folder cannot leave stale kit names
    ** visible in the Load page. kb_numKits is cleared for the legacy
    ** kitBrowser compatibility map populated by filesystem_recordKitDirectory().
    */
    kb_numKits = 0;
    memset(kit_slot_present, 0, sizeof(kit_slot_present));
    memset(kit_slot_name, 0, sizeof(kit_slot_name));
    memset(kit_slot_open_name, 0, sizeof(kit_slot_open_name));
    return filesystem_start(FS_INTERNAL_OP_SCAN_KITS, FS_FILE_KIT, 0, cb);
}

bool filesystem_requestScanScenes(fs_completion_cb_t cb)
{
    /*
     * Start a root Scene/ numbered-folder scan.
     *
     * Inputs: completion callback. Output: the Scene library cache is cleared
     * immediately and then repopulated from actual FAT directory entries. This
     * mirrors Kit scan but intentionally has no kitBrowser compatibility map.
     */
    if (status == FS_STATUS_BUSY) return false;
    memset(scene_slot_present, 0, sizeof(scene_slot_present));
    memset(scene_slot_name, 0, sizeof(scene_slot_name));
    memset(scene_slot_open_name, 0, sizeof(scene_slot_open_name));
    return filesystem_start(FS_INTERNAL_OP_SCAN_SCENES, FS_FILE_SCENE, 0, cb);
}

bool filesystem_requestScanBanks(fs_completion_cb_t cb)
{
    /*
     * Start a root Bank/ numbered-folder scan.
     *
     * Inputs: completion callback. Output: the root Bank browser cache is
     * cleared immediately and repopulated from actual FAT directory entries.
     * Missing Bank/ is a successful empty scan so boot can fall back to root
     * Scene/Kit/defaults without treating a pre-Bank card as corrupt.
     */
    if (status == FS_STATUS_BUSY) return false;
    memset(bank_slot_present, 0, sizeof(bank_slot_present));
    memset(bank_slot_name, 0, sizeof(bank_slot_name));
    memset(bank_slot_open_name, 0, sizeof(bank_slot_open_name));
    return filesystem_start(FS_INTERNAL_OP_SCAN_BANKS, FS_FILE_BANK, 0, cb);
}

bool filesystem_requestScanBankScenes(uint16_t slot, fs_completion_cb_t cb)
{
    /*
     * Start a child-Scene preview scan for one highlighted Bank.
     *
     * Inputs: zero-based Bank slot from Menu's browser. Output: the shared
     * operation reset clears old child bits, then filesystem_scanBankScenes_tick()
     * repopulates op_bank_child_present_mask from actual 00..15 child
     * directories. Returning false means the filesystem is busy or the root
     * Bank slot is not present, so callers should keep/defer their current LED
     * state rather than trust a stale child mask.
     */
    if (status == FS_STATUS_BUSY || slot >= STORAGE_BANK_MAX_SLOTS ||
        !bank_slot_present[slot]) {
        return false;
    }
    return filesystem_start(FS_INTERNAL_OP_SCAN_BANK_SCENES,
                            FS_FILE_BANK,
                            slot,
                            cb);
}

uint16_t filesystem_bankChildSceneMask(void)
{
    /*
     * Return the most recently completed Bank child preview mask.
     *
     * Output is a 16-bit map where bit N means the selected Bank folder
     * contained a parseable child Scene directory `NN Name`. Clients:
     * Load:[Bank] LED preview after filesystem_requestScanBankScenes()
     * completes. Risk: this is operation-local cache state; Menu must compare
     * the scanned slot it requested with the slot still on screen before using
     * this value.
     */
    return op_bank_child_present_mask;
}

bool filesystem_requestScanInstruments(fs_completion_cb_t cb)
{
    /*
     * Start a root Instrument/ scan.
     *
     * Inputs: completion callback. Output: typed Instrument browser cache is
     * cleared immediately and repopulated asynchronously from Instrument/.
     * Missing Instrument/ is handled inside the state machine as an empty
     * successful scan, mirroring Kit/ scan behavior.
     */
    if (status == FS_STATUS_BUSY) return false;
    memset(instrument_file_count, 0, sizeof(instrument_file_count));
    memset(instrument_file_name, 0, sizeof(instrument_file_name));
    memset(instrument_file_open_name, 0, sizeof(instrument_file_open_name));
    memset(instrument_file_stem, 0, sizeof(instrument_file_stem));
    return filesystem_start(FS_INTERNAL_OP_SCAN_INSTRUMENTS, FS_FILE_KIT, 0,
                            cb);
}

bool filesystem_requestScanTestFiles(fs_completion_cb_t cb)
{
    /*
     * Start a root file scan for Load:[File].
     *
     * Output: fs_test_file_name/count are cleared immediately and rebuilt from
     * asyncfatfs object metadata. Directories are ignored here so the menu can
     * prove file enumeration without Scene/Kit folder assumptions.
     */
    if (status == FS_STATUS_BUSY)
        return false;
    fs_test_file_count = 0u;
    memset(fs_test_file_name, 0, sizeof(fs_test_file_name));
    return filesystem_start(FS_INTERNAL_OP_SCAN_TEST_FILES, FS_FILE_KIT, 0, cb);
}

bool filesystem_requestScanTestDirs(fs_completion_cb_t cb)
{
    /*
     * Start a root directory scan for Load:[Dir].
     *
     * Output: fs_test_dir_name/count are cleared immediately and rebuilt from
     * exact-case asyncfatfs display components. Files are ignored so the menu
     * can test directory creation/opening independently of file listing.
     */
    if (status == FS_STATUS_BUSY)
        return false;
    fs_test_dir_count = 0u;
    memset(fs_test_dir_name, 0, sizeof(fs_test_dir_name));
    return filesystem_start(FS_INTERNAL_OP_SCAN_TEST_DIRS, FS_FILE_KIT, 0, cb);
}

uint8_t filesystem_testFileCount(void)
{
    return fs_test_file_count;
}

uint8_t filesystem_testDirCount(void)
{
    return fs_test_dir_count;
}

const char *filesystem_testFileName(uint8_t index)
{
    if (index >= fs_test_file_count)
        return "";
    return fs_test_file_name[index];
}

const char *filesystem_testDirName(uint8_t index)
{
    if (index >= fs_test_dir_count)
        return "";
    return fs_test_dir_name[index];
}

bool filesystem_requestLoadTestFile(const char *display_name,
                                    fs_completion_cb_t cb)
{
    char name[FS_TEST_NAME_MAX + 1u];

    /*
     * Load the first four bytes from an exact root file display name.
     *
     * Inputs are copied at request time so later menu edits cannot retarget an
     * in-flight open. Output is filesystem_testResultBytes() after completion.
     */
    filesystem_copyTestName(name, display_name);
    if (name[0] == '\0')
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_TEST_FILE, FS_FILE_KIT, 0, cb))
        return false;
    filesystem_copyTestName(op_test_name, name);
    return true;
}

bool filesystem_requestLoadTestDir(const char *display_name,
                                   fs_completion_cb_t cb)
{
    char name[FS_TEST_NAME_MAX + 1u];

    /*
     * Load the first sorted child from an exact root directory display name.
     *
     * Output is either four bytes from the first child file or a directory name
     * in filesystem_testResultName() when the first sorted child is a
     * subdirectory. Empty directories complete as errors because there is no
     * data or child label to display.
     */
    filesystem_copyTestName(name, display_name);
    if (name[0] == '\0')
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_TEST_DIR, FS_FILE_KIT, 0, cb))
        return false;
    filesystem_copyTestName(op_test_name, name);
    return true;
}

bool filesystem_requestSaveTestFile(const char *display_name,
                                    fs_completion_cb_t cb)
{
    char name[FS_TEST_NAME_MAX + 1u];

    /*
     * Save four random bytes to an exact root file display name.
     *
     * Existing files are overwritten by afatfs_fopen_lfn(..., "w", exact).
     * Completion exposes the bytes that were written so Menu can display the
     * same result that should be visible to a desktop reader.
     */
    filesystem_copyTestName(name, display_name);
    if (name[0] == '\0')
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_SAVE_TEST_FILE, FS_FILE_KIT, 0, cb))
        return false;
    filesystem_copyTestName(op_test_name, name);
    return true;
}

bool filesystem_requestSaveTestDir(const char *display_name,
                                   fs_completion_cb_t cb)
{
    char name[FS_TEST_NAME_MAX + 1u];

    /*
     * Create/open an exact directory under /Kit and write a same-name child
     * file.
     *
     * This proves long-name directory creation and long-name file creation
     * inside the entered directory in one operation, while exercising the Kit
     * parent directory path. The child filename equals the directory display
     * component byte-for-byte after bounded copy.
     */
    filesystem_copyTestName(name, display_name);
    if (name[0] == '\0')
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_SAVE_TEST_DIR, FS_FILE_KIT, 0, cb))
        return false;
    filesystem_copyTestName(op_test_name, name);
    return true;
}

bool filesystem_requestSaveTestSimpleDir(const char *display_name,
                                         fs_completion_cb_t cb)
{
    char name[FS_TEST_NAME_MAX + 1u];

    filesystem_copyTestName(name, display_name);
    if (name[0] == '\0')
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_SAVE_TEST_SIMPLE_DIR,
                          FS_FILE_KIT,
                          0,
                          cb)) {
        return false;
    }
    filesystem_copyTestName(op_test_name, name);
    return true;
}

fs_test_result_kind_t filesystem_testResultKind(void)
{
    return op_test_result_kind;
}

const uint8_t *filesystem_testResultBytes(void)
{
    return op_test_bytes;
}

const char *filesystem_testResultName(void)
{
    return op_test_child_name;
}

bool filesystem_requestLoadInstrument(uint8_t destination_scene,
                                      uint8_t destination_slot,
                                      instrument_type_t type,
                                      uint8_t browser_index,
                                      fs_completion_cb_t cb)
{
    /*
     * Start a single Instrument/ file load into one kit slot.
     *
     * Inputs: resident destination Scene/slot, instrument type, and per-type
     * browser index from the most recent Instrument/ scan. Output: an async
     * operation that validates one file into staging without changing that
     * Scene kit slot. This request is separate from filesystem_requestLoad()
     * because its generic signature has no Scene/type/cache-index coordinates,
     * while Instrument Load needs all three as immutable completion context.
     */
    if (!scene_indexValid(destination_scene) ||
        type >= INSTRUMENT_TYPE_UNKNOWN ||
        destination_slot >= STORAGE_KIT_SLOT_COUNT ||
        browser_index >= instrument_file_count[type])
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_INSTRUMENT, FS_FILE_KIT, 0, cb))
        return false;
    op_instrument_load_destination_slot = destination_slot;
    op_instrument_load_destination_scene = destination_scene;
    op_instrument_load_type = type;
    op_instrument_load_index = browser_index;
    return true;
}

static bool filesystem_requestSaveInstrumentMode(fs_internal_op_t op,
                                                 uint8_t source_scene,
                                                 uint8_t source_slot,
                                                 const char *display_name,
                                                 fs_completion_cb_t cb)
{
    const scene_t *scene = scene_getConst(source_scene);
    const kit_instrument_slot_t *instrument =
        (scene && source_slot < STORAGE_KIT_SLOT_COUNT)
            ? &scene->kit.instruments[source_slot]
            : NULL;
    char display[AFATFS_LONG_FILENAME_MAX + 1u];

    /*
     * Capture one root Instrument Save request.
     *
     * What: Converts a resident source Scene/voice plus an edited stem into the
     * exact visible Instrument/<stem.ext> target and records immutable source
     * coordinates for the asynchronous writer.
     *
     * Why: normal Instrument Save and InstrumentMrp Save share filename/type
     * construction, overwrite behavior, and source validation. Their only
     * difference is the storage write view and whether retained source-name
     * metadata is updated after success.
     *
     * Inputs: internal save op, source Scene/slot, edited display stem, and
     * completion callback. Outputs: op_instrument_save_* scratch and filesystem
     * busy state.
     *
     * Affiliates/clients: filesystem_requestSaveInstrument(),
     * filesystem_requestSaveInstrumentMorph(), filesystem_saveInstrument_tick().
     */
    if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT &&
        op != FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH) {
        return false;
    }
    if (!instrument ||
        source_slot >= STORAGE_KIT_SLOT_COUNT ||
        instrument->type >= INSTRUMENT_TYPE_UNKNOWN)
        return false;
    storage_makeSavedInstrumentDisplayFilename(display,
                                               sizeof(display),
                                               display_name,
                                               instrument->type,
                                               (uint8_t)(source_slot + 1u),
                                               0u);
    if (display[0] == '\0')
        return false;
    if (!filesystem_start(op, FS_FILE_KIT, 0u, cb))
        return false;
    /*
     * Save the immutable request coordinates after filesystem_start() clears
     * operation scratch. The captured type protects the writer from serializing
     * a later slot replacement under the wrong file extension; the captured
     * display component protects the write target from encoder movement while
     * asyncfatfs is busy.
     */
    op_instrument_save_source_scene = source_scene;
    op_instrument_save_source_slot = source_slot;
    op_instrument_save_type = instrument->type;
    /*
     * Capture the root Instrument save projection with the accepted request.
     *
     * What: Stores whether this request is normal Instrument Save or
     * InstrumentMrp Save after filesystem_start() clears shared operation
     * scratch.
     *
     * Why: the writer streams many async ticks after the menu click. Its value
     * projection should be request-owned state, not a fresh inference from
     * generic operation plumbing at each write phase.
     *
     * Inputs: internal save op accepted above. Output:
     * op_instrument_save_mode consumed by filesystem_saveInstrument_tick() and
     * storage_formatInstrumentLineView().
     *
     * Affiliates/clients: preset_saveInstrumentMorph(), nested Instrument Save
     * type row, STORAGE_INSTRUMENT_SAVE_MORPH endpoint projection.
     */
    op_instrument_save_mode =
        (op == FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH)
            ? STORAGE_INSTRUMENT_SAVE_MORPH
            : STORAGE_INSTRUMENT_SAVE_NORMAL;
    strncpy(op_instrument_save_display_name, display,
            sizeof(op_instrument_save_display_name) - 1u);
    op_instrument_save_display_name[
        sizeof(op_instrument_save_display_name) - 1u] = '\0';
    return true;
}

bool filesystem_requestSaveInstrument(uint8_t source_scene,
                                      uint8_t source_slot,
                                      const char *display_name,
                                      fs_completion_cb_t cb)
{
    /*
     * Start normal root Instrument Save.
     *
     * What: Routes the public normal-save API through the shared request
     * capture helper with the normal Instrument save operation tag.
     *
     * Why: normal and Morph Instrument saves must construct identical target
     * filenames for the same source/stem; only the later writer projection and
     * retained-name completion policy differ.
     *
     * Affiliates/clients: preset_saveInstrument(),
     * filesystem_requestSaveInstrumentMode().
     */
    return filesystem_requestSaveInstrumentMode(FS_INTERNAL_OP_SAVE_INSTRUMENT,
                                                source_scene,
                                                source_slot,
                                                display_name,
                                                cb);
}

bool filesystem_requestSaveInstrumentMorph(uint8_t source_scene,
                                           uint8_t source_slot,
                                           const char *display_name,
                                           fs_completion_cb_t cb)
{
    /*
     * Start root Instrument Morph Save.
     *
     * What: Routes InstrumentMrp Save through the shared request capture helper
     * with the Morph Instrument save operation tag.
     *
     * Why: InstrumentMrp Save uses the same target filename and overwrite
     * identity as normal Instrument Save, but filesystem_saveInstrument_tick()
     * will choose a Morph Save view and skip retained-name mutation.
     *
     * Affiliates/clients: preset_saveInstrumentMorph(),
     * filesystem_saveInstrument_tick().
     */
    return filesystem_requestSaveInstrumentMode(
        FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH,
        source_scene,
        source_slot,
        display_name,
        cb);
}

bool filesystem_requestLoadName(fs_file_type_t type, uint16_t slot, fs_completion_cb_t cb)
{
    const fs_file_desc_t *desc = filesystem_desc(type);
    if (desc == NULL || !desc->has_name_header)
        return false;
    return filesystem_start(FS_INTERNAL_OP_LOAD_NAME, type, slot, cb);
}

const char *filesystem_loadedName(void)
{
    return loaded_name;
}

const kit_t *filesystem_loadedKit(void)
{
    /*
     * Borrow the validated Kit directory staging image.
     *
     * Output is read-only filesystem storage consumed by Preset immediately
     * after a matching KitMrp completion callback. Normal Kit Load still keeps
     * its historical atomic commit inside filesystem; this accessor exists only
     * for morph-load commit policy, where Preset must copy endpoint values into
     * the current resident kit without replacing types, routing, or names.
     */
    return &op_staged_kit;
}

const struct kit_instrument_slot *filesystem_loadedInstrumentSlot(void)
{
    /*
     * Borrow the validated one-Instrument staging image.
     *
     * Output is read-only filesystem storage consumed by Preset immediately
     * after the matching completion callback. No copy or commit occurs here;
     * the caller must preserve the outgoing runtime identity until modulation
     * targets have been cleared.
     */
    return &op_staged_instrument;
}

const char *filesystem_loadedInstrumentDisplayName(void)
{
    /*
     * Borrow the display stem paired with the staged Instrument image.
     *
     * Output is eight printable cache characters plus NUL. Preset copies this
     * only when it commits the associated slot, so file identity and parameter
     * identity cannot diverge on a failed or incomplete load.
     */
    return op_staged_instrument_display_name;
}

const char *filesystem_loadedInstrumentStem(void)
{
    /*
     * Borrow the retained source stem paired with a staged Instrument image.
     *
     * Output remains valid until the next filesystem operation starts. Preset
     * commits this only with the matching staged payload so Kit Save metadata
     * cannot claim a filename for a slot that failed to load.
     */
    return op_staged_instrument_stem;
}

/* Query whether the most recent Kit/ scan found a numbered folder.
 *
 * Input: zero-based slot used by menu/preset code. Output: nonzero if a
 * Kit/NNN Name folder exists. Clients: filesystem_kitSlotName() and future UI
 * code that wants to distinguish absent slots from malformed present kits.
 */
uint8_t filesystem_kitSlotExists(uint16_t zero_based_slot)
{
    if (zero_based_slot >= STORAGE_KIT_MAX_SLOTS)
        return 0u;
    return kit_slot_present[zero_based_slot];
}

/* Return a display name from the Kit/ scan cache.
 *
 * Input: zero-based slot. Output: NUL-terminated eight-character cached name,
 * or "Empty   " for absent/out-of-range slots. Client: menu.c's Load page.
 */
const char *filesystem_kitSlotName(uint16_t zero_based_slot)
{
    if (!filesystem_kitSlotExists(zero_based_slot))
        return "Empty   ";
    return kit_slot_name[zero_based_slot];
}

uint8_t filesystem_sceneSlotExists(uint16_t zero_based_slot)
{
    /*
     * Query the root Scene/ scan cache.
     *
     * Input: zero-based library slot. Output: nonzero only when the latest
     * Scene scan found a numbered Scene folder at that slot.
     */
    if (zero_based_slot >= STORAGE_SCENE_MAX_SLOTS)
        return 0u;
    return scene_slot_present[zero_based_slot];
}

const char *filesystem_sceneSlotName(uint16_t zero_based_slot)
{
    /*
     * Return an eight-character root Scene library display name.
     *
     * Input: zero-based library slot. Output: cached display name for existing
     * Scenes, or "Empty   " for missing/out-of-range slots. Menu uses this
     * directly for Load:[Scene] and Save overwrite planning.
     */
    if (zero_based_slot >= STORAGE_SCENE_MAX_SLOTS ||
        !scene_slot_present[zero_based_slot]) {
        return "Empty   ";
    }
    return scene_slot_name[zero_based_slot];
}

uint8_t filesystem_bankSlotExists(uint16_t zero_based_slot)
{
    /*
     * Query the root Bank/ scan cache.
     *
     * Input: zero-based root Bank library slot 000..999. Output: nonzero only
     * when the latest Bank scan found a matching Bank directory. Bank-local
     * Scene children are intentionally outside this API.
     */
    if (zero_based_slot >= STORAGE_BANK_MAX_SLOTS)
        return 0u;
    return bank_slot_present[zero_based_slot];
}

const char *filesystem_bankSlotName(uint16_t zero_based_slot)
{
    /*
     * Return an eight-character root Bank display name.
     *
     * Input: root Bank library slot. Output: cached directory-derived display
     * name or "Empty   ". bankset.bcg is not consulted because files never
     * store their own object names.
     */
    if (zero_based_slot >= STORAGE_BANK_MAX_SLOTS ||
        !bank_slot_present[zero_based_slot]) {
        return "Empty   ";
    }
    return bank_slot_name[zero_based_slot];
}

uint16_t filesystem_firstKitSlot(void)
{
    uint16_t slot;

    /*
     * Find the lowest present Kit slot for fallback loading.
     *
     * The loop scans direct root slots 000..999 and returns the array count as
     * the absent sentinel. Callers compare against STORAGE_KIT_MAX_SLOTS rather
     * than assuming 0 is empty, because slot 000 is a real user slot.
     */
    for (slot = 0u; slot < STORAGE_KIT_MAX_SLOTS; slot++) {
        if (kit_slot_present[slot])
            return slot;
    }
    return STORAGE_KIT_MAX_SLOTS;
}

uint16_t filesystem_firstSceneSlot(void)
{
    uint16_t slot;

    /*
     * Find the lowest root Scene slot for Bank-empty fallback.
     *
     * The loop walks the direct root Scene/ cache only. Bank-local two-digit
     * child Scenes are deliberately excluded because fallback must initialize
     * from the public Scene library before trying Kit/defaults.
     */
    for (slot = 0u; slot < STORAGE_SCENE_MAX_SLOTS; slot++) {
        if (scene_slot_present[slot])
            return slot;
    }
    return STORAGE_SCENE_MAX_SLOTS;
}

uint16_t filesystem_firstBankSlot(void)
{
    uint16_t slot;

    /*
     * Find the lowest root Bank slot for boot.
     *
     * The return sentinel is STORAGE_BANK_MAX_SLOTS, not zero, because Bank
     * slot 000 is the intended default. Boot and Menu callers therefore ask
     * filesystem_bankSlotExists() or compare against the max before loading.
     */
    for (slot = 0u; slot < STORAGE_BANK_MAX_SLOTS; slot++) {
        if (bank_slot_present[slot])
            return slot;
    }
    return STORAGE_BANK_MAX_SLOTS;
}

uint8_t filesystem_lastBankLoadLoadedScene(void)
{
    /*
     * Report whether the most recent Bank Load supplied a child Scene payload.
     *
     * Empty Banks are successful Bank loads but leave Scene initialization to
     * the Preset/Menu fallback chain. This bit separates that valid empty-Bank
     * outcome from a filesystem error.
     */
    return op_bank_loaded_scene;
}

uint8_t filesystem_instrumentTargetExists(instrument_type_t type,
                                          const char *display_stem)
{
    char display_file[AFATFS_LONG_FILENAME_MAX + 1u];
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Query whether a root Instrument save target already exists.
     *
     * What: Builds the same visible `stem.ext` component that root Instrument
     * Save will write, then checks the current Instrument/ scan cache for a
     * case-insensitive match of the same instrument type.
     *
     * Why: Menu must render persistent `OW` before the user confirms Save.
     * Numbered slots can answer from occupancy caches, but root Instrument Save
     * is filename-based and needs the extension/type rule owned by filesystem.
     *
     * Inputs: resident instrument type and the eight-character Save editor
     * stem. Outputs: nonzero when confirming would overwrite at least one
     * on-card same-casefold Instrument file.
     *
     * Affiliates/clients: menu_currentSaveWouldOverwrite(), root Instrument
     * Save, Instrument browser duplicate suppression.
     */
    if (type >= INSTRUMENT_TYPE_UNKNOWN || !display_stem)
        return 0u;
    storage_makeSavedInstrumentDisplayFilename(display_file,
                                               sizeof(display_file),
                                               display_stem,
                                               type,
                                               0u,
                                               0u);
    filesystem_copyInstrumentStemDisplay(display, display_file);
    for (uint8_t i = 0u; i < instrument_file_count[type]; i++) {
        if (filesystem_instrumentCacheStemMatches(type, i, display))
            return 1u;
    }
    return 0u;
}

uint8_t filesystem_instrumentCount(instrument_type_t type)
{
    /*
     * Return the cached Instrument/ count for one type.
     *
     * Input: instrument type. Output: number of sorted files found by the most
     * recent scan, capped by FS_INSTRUMENT_MAX_PER_TYPE. Menu uses this to
     * bound browser indices without accessing filesystem static arrays.
     */
    if (type >= INSTRUMENT_TYPE_UNKNOWN)
        return 0u;
    return instrument_file_count[type];
}

const char *filesystem_instrumentName(instrument_type_t type,
                                      uint8_t browser_index)
{
    /*
     * Return one cached Instrument/ display name.
     *
     * Inputs: instrument type and zero-based browser index. Output: an
     * eight-character NUL-terminated stem, or "Empty   " for invalid/empty
     * selections. The returned pointer is filesystem-owned cache storage.
     */
    if (type >= INSTRUMENT_TYPE_UNKNOWN ||
        browser_index >= instrument_file_count[type])
        return "Empty   ";
    return instrument_file_name[type][browser_index];
}

uint16_t filesystem_instrumentDisplayIndex(instrument_type_t type,
                                           uint8_t browser_index)
{
    uint16_t display_index;

    /*
     * Convert a cached Instrument/ index into the visible one-based counter.
     *
     * Inputs: type and zero-based browser index. Output: one-based list
     * position, visually saturated at 999. The current cache cannot exceed 128
     * entries per type, but the saturation lives here so the UI rule stays with
     * the browser data source if the cache grows later.
     */
    if (type >= INSTRUMENT_TYPE_UNKNOWN ||
        browser_index >= instrument_file_count[type])
        return 0u;
    display_index = (uint16_t)browser_index + 1u;
    return (display_index > 999u) ? 999u : display_index;
}

uint8_t filesystem_diagOp(void)
{
    return (uint8_t)current_op;
}

uint8_t filesystem_diagPhase(void)
{
    return op_phase;
}

uint32_t filesystem_diagBytesDone(void)
{
    return op_bytes_done;
}

fs_mount_result_t filesystem_lastMountResult(void)
{
    return fs_last_mount_result;
}

uint8_t filesystem_bootDetectedUnsupportedCard(void)
{
    return fs_boot_detected_unsupported_card;
}

fs_stale_warning_source_t filesystem_takeStaleGlobalsWarning(void)
{
    fs_stale_warning_source_t result = fs_stale_warning_pending;
    fs_stale_warning_pending = FS_STALE_WARNING_NONE;
    return result;
}

#if FILESYSTEM_DIAGNOSTICS
uint8_t filesystem_diagRawCmd0(void)
{
    uint8_t i, resp = 0xFF;

    spi_sd_set_slow();
    SD_CS_DEASSERT;
    for (i = 0; i < 10; i++) SPI_transmit(0xFF);

    SD_CS_ASSERT;
    SPI_transmit(0x40);
    SPI_transmit(0x00); SPI_transmit(0x00);
    SPI_transmit(0x00); SPI_transmit(0x00);
    SPI_transmit(0x95);

    for (i = 0; i < 8; i++) {
        resp = SPI_receive();
        if (resp != 0xFF) break;
    }

    SD_CS_DEASSERT;
    SPI_transmit(0xFF);
    return resp;
}
#endif
