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
 * File payloads intentionally live here, behind the typed filesystem facade.
 * Typed load/save uses the descriptor table and explicit payload serializers
 * below, while Menu consumes only filesystem-owned scan/index accessors.
 *
 * The filesystem pump remains single-context. afatfs_poll() is called only
 * from filesystem_tick(), including synchronous boot polling.
 */

#include "filesystem.h"
#include "config.h"
#include "asyncfatfs.h"
#include "fat_standard.h"
#include "storageTypes.h"
#include "SceneData.h"
#include "BankData.h"
#include "Autosave.h"
#include "AutosaveTrace.h"
#include "sd_routines.h"
#include "spi_sd.h"
#include "sdcard_lxr02.h"
#include "presetManager.h"
#include "ParameterArray.h"
#include "menu.h"
#include "SampleMemory.h"
#include "sequencer.h"
#include "PatternData.h"
#include "MidiNoteNumbers.h"
#include "MidiMessages.h"
#include "timebase.h"
#include "random.h"
#include <string.h>
#include <stdint.h>

#define FS_SECTOR_SIZE_BYTES 512u
#define FS_NUM_FATS_EXPECTED 2u
/*
 * Warm-reset SD power/readiness hold.
 *
 * The MCU can restart while the card remains powered or is still completing
 * an internal operation. After spi_sd_set_slow() has made CS high and all bus
 * lines idle, hold this long before CMD0. Development LCD diagnostics
 * previously supplied incidental latency at this boundary. The explicit
 * constant makes production behavior deterministic without affecting runtime.
 */
#define FS_SD_PREINIT_SETTLE_MS 250u
#define MBR_PARTITION_TYPE_EXFAT 0x07u
#define FS_CONTAINER_META_LEN 64u
#define FS_CONTAINER_KIT_LEN 512u
#define FS_CONTAINER_PAD_BYTE 0xffu
#define FS_KIT_LFN_MAX 80u
#define FS_RESIDENT_NAMES_FILENAME ".hcnames"
/*
 * `/.hcnames` is one firmware-owned root singleton in FAT's case-insensitive
 * namespace. Every read, rewrite, and first-use creation must therefore use
 * folded display-name lookup: a host-created case variant is the same register
 * to preserve, not evidence that the canonical name is absent. This prevents
 * a later write from allocating a second visible HCNAMES object solely because
 * its display case differs. It does not classify an I/O open failure as absent;
 * callers that bootstrap after a NULL open still require a separate
 * failure-versus-absence remedy.
 */
#define FS_RESIDENT_NAMES_MATCH_MODE AFATFS_MATCH_CASE_INSENSITIVE
/*
 * Fixed logical row coordinates inside the variable-length `/.hcnames` file.
 *
 * What: the register contains one Bank row, sixteen Scene rows, sixteen Kit
 * rows, and then six Instrument rows for each of sixteen resident Scenes.
 * Why: runtime Instrument lookup/update must compute one stable Scene/slot
 * coordinate without retaining another mapping table. The physical text rows
 * remain trimmed and variable length; these constants describe row identity,
 * not byte offsets.
 */
#define FS_RESIDENT_NAMES_INSTRUMENT_BASE \
    (1u + STORAGE_BANK_SCENE_MAX_SLOTS + STORAGE_BANK_SCENE_MAX_SLOTS)
#define FS_RESIDENT_NAMES_KIT_BASE \
    (1u + STORAGE_BANK_SCENE_MAX_SLOTS)
#define FS_RESIDENT_NAMES_ROW_COUNT \
    (FS_RESIDENT_NAMES_INSTRUMENT_BASE + \
     (STORAGE_BANK_SCENE_MAX_SLOTS * STORAGE_KIT_SLOT_COUNT))
/*
 * Text line buffer for storageTypes schemas.
 *
 * Most files fit under 96 bytes, but the draft Scene/Bank pattern format writes
 * `trackN=<length>,<scale>,<128 on/off bits>`, which needs roughly 142 bytes
 * including newline/NUL. Keep this shared buffer large enough for that row
 * while still avoiding whole-file staging.
 */
#define FS_TEXT_LINE_MAX 160u
/* The shared cache is the complete Instrument browser capacity. */
#define FS_LIBRARY_NAME_CACHE_MAX 1000u
#define FS_TEST_OBJECT_MAX 64u

/*
 * The generalized browser cache has one physical name array for every
 * numbered or typed library. Kit, root Scene, and root Bank indexes use the
 * slot number as the array index, while an Instrument index uses the first N
 * sorted rows. Runtime HCNAMES access temporarily borrows the first 129 rows;
 * Menu copies its selected resident name before the typed index replaces them.
 * Keeping this maximum at the largest numbered library lets the same SRAM
 * object be disposed and reused instead of allocating one name array per
 * library or per Instrument type.
 */
typedef enum {
    FS_NAME_CACHE_NONE = 0u,
    FS_NAME_CACHE_INSTRUMENT,
    FS_NAME_CACHE_KIT,
    FS_NAME_CACHE_SCENE,
    FS_NAME_CACHE_BANK,
    /* Temporary 129-row root register image; never a second allocation. */
    FS_NAME_CACHE_HCNAMES,
} fs_name_cache_kind_t;

/* -----------------------------------------------------------------------
** Operation types
** ----------------------------------------------------------------------- */
typedef enum {
    FS_INTERNAL_OP_NONE,
    FS_INTERNAL_OP_FLUSH_FINISH,
    FS_INTERNAL_OP_CREATE_BOOT_INDEX,
    /* Boot/runtime writer for slot-ordered Kit/Scene/Bank `.hcindex` rows. */
    FS_INTERNAL_OP_CREATE_LIBRARY_INDEX,
    /*
     * Boot writer for root `/.hcnames`.
     *
     * This intentionally uses the same foreground-pumped open/write/close
     * pattern as `.hcindex`: boot may wait for completion, but SD progress is
     * still made by filesystem_tick() and the normal finish/flush gate.
    */
    FS_INTERNAL_OP_WRITE_HCNAMES,
    /*
     * Logging recovery writer for the root `/bootlog.bin` file.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Inputs: the eight-byte operation code preserved outside generic
     * operation scratch. Output: exactly those eight bytes, closed and passed
     * through the normal sync gate. Why: main must not issue ad-hoc FAT writes
     * while the filesystem facade owns all async progress. Affiliates:
     * filesystem_writeBootFailureLogBlocking() and filesystem_tick().
     */
    FS_INTERNAL_OP_WRITE_BOOT_LOG,
    /*
     * Boot-only creation of the two fixed-size working-Bank registers.
     *
     * The operation reads HCNAMES once, scans each root target before opening
     * it for write, and creates only proven-missing files.  It never performs
     * a parameter overlay, dirty mark, record selection, or existing-file
     * rewrite; those are separate future autosave milestones.
     */
    FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES,
    /*
     * Runtime, background-only A/B parameter drain.
     *
     * Inputs: newest valid record, its on-card mutation mask, and the resident
     * Bank/Scene parameter owners. Output: a bounded set of live bytes and an
     * updated mask are copy-forwarded into the inactive peer before the normal
     * CRC/final-commit transaction. Affiliates: Autosave.c's explicit payload
     * map and the dedicated non-name cache below.
     */
    FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN,
    /*
     * Lowest-priority diagnostic append of already-captured autosave stages.
     *
     * Inputs are a bounded RAM-ring snapshot; output is one root
     * `asavetrc.bin` append which advances the ring only after a sync gate.
     * Why: trace persistence must share the facade's one AsyncFATFS owner and
     * must never be confused with, or allowed to preempt, parameter draining.
     */
    FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
    /*
     * Runtime reader/update operations borrow the generalized name cache.
     * Instrument operations address one voice row per selected Scene. Kit
     * operations address the Kit row plus all six Instrument rows because a
     * full Kit replacement changes that complete resident identity block.
     * Scene operations borrow/update exactly one Scene row or mask of rows;
     * they exist because Scene identity no longer lives in scene_t.
     */
    FS_INTERNAL_OP_LOAD_HCNAMES_INSTRUMENT,
    FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT,
    FS_INTERNAL_OP_LOAD_HCNAMES_KIT,
    FS_INTERNAL_OP_UPDATE_HCNAMES_KIT,
    FS_INTERNAL_OP_LOAD_HCNAMES_SCENE,
    FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE,
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
     * operation-local op_bank_child_present_mask, exposed through
     * filesystem_bankChildSceneMask() after completion. This op never reads
     * bankset.bcg or changes BankData. It deliberately never retains child
     * names or aliases: a real Bank Load rescans one selected child at a time.
     */
    FS_INTERNAL_OP_SCAN_BANK_SCENES,
    FS_INTERNAL_OP_SCAN_INSTRUMENTS,
    /*
     * Name repair is a physical-card maintenance operation for index
     * generation and the immediate child-name handoff used by Bank Load. It
     * walks one parent directory at a time, renames one object, flushes, and
     * then resumes scanning. Bank Load uses only that bounded child-directory
     * repair; selected Scene payload validation remains in the common reader.
     * It must never populate a second 1,000-entry cache; normal scan/index
     * passes publish browser rows after repair.
     */
    FS_INTERNAL_OP_REPAIR_NAMES,
    FS_INTERNAL_OP_LOAD_INSTRUMENT,
    FS_INTERNAL_OP_SAVE_INSTRUMENT,
    /* Temporary `kit` row file; it never updates HCNAMES or `.hcindex`. */
    FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP,
    FS_INTERNAL_OP_LOAD_NAME,
    FS_INTERNAL_OP_LOAD_INDEX,
    /* Async reader that replaces the shared cache from a root `.hcindex`. */
    FS_INTERNAL_OP_LOAD_LIBRARY_INDEX,
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
 * DEV_MODE_LOGGING boot watchdog record.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: retains one exact eight-byte operation code, a wrapping millisecond
 * deadline, and separate recovery state outside all generic operation scratch.
 * Why: the code must survive dirty asyncfatfs destruction after the operation
 * that owned that scratch has timed out. Inputs are filesystem_start(), mount,
 * flush, and raw blocking-operation boundaries; outputs are consumed by both
 * polling paths and the root bootlog writer. `armed` prevents idle gaps between
 * completed boot operations from expiring. Affiliates: time_sysTick,
 * filesystem_bootLoggingPollDeadline(), and main.c's pre-audio window.
 */
#if DEV_MODE_LOGGING
static uint8_t          fs_boot_logging_active = 0u;
static uint8_t          fs_boot_logging_armed = 0u;
static uint8_t          fs_boot_logging_timed_out = 0u;
static uint8_t          fs_boot_logging_recovery = 0u;
static uint8_t          fs_boot_logging_recovery_failed = 0u;
static uint16_t         fs_boot_logging_started_tick = 0u;
static uint16_t         fs_boot_logging_recovery_started_tick = 0u;
static uint8_t          fs_boot_logging_code[8];
#endif
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
 * The shared cache bridges those worlds. For Kit and root Scene, a non-blank
 * row in the active slot-ordered cache is both the eight-character display
 * name and the authoritative occupancy bit. No per-slot presence or FAT
 * alias arrays are retained: the cache is disposed and repopulated whenever
 * the active library changes, so a stale library cannot remain usable.
 *
 * op_kit_root_dir/op_kit_slot_dir/op_finder/op_lfn_* are private scratch for
 * the scan and directory-load state machines. op_line_buf/op_line_len stream
 * kitset.kcg and instrument text lines without staging whole files in RAM.
 * op_kitset/op_instrument_state/op_instrument_slot are the storageTypes parser
 * state. Accessors are filesystem_kitSlotExists() and filesystem_kitSlotName();
 * clients are menu.c, legacy preset name browsing via
 * filesystem_requestLoadName(), and filesystem_loadKitDirectory_tick().
 */
/*
 * Root Scene/ directory scan cache.
 *
 * Scene folders use the same numbered "NNN Name" convention as Kit folders,
 * but their occupancy is independent while this shared cache is active.
 * Display names and occupancy are therefore owned entirely by the one
 * generalized cache tagged FS_NAME_CACHE_SCENE. Any concrete FAT alias needed
 * by asyncfatfs is kept only in operation-local scratch for that one reopen.
 */
/*
 * Root Bank/ directory identity.
 *
 * Root Banks are library slots named `NNN <bank name>` just like root Scenes,
 * but a Bank folder contains bankset.bcg plus Bank-local Scene children named
 * `00..15`. This cache stores only the root Bank identity used by Load/Save
 * browsing; child Scene names live in per-operation scratch while a selected
 * Bank is open. Root Bank names and occupancy are held by the generalized
 * slot-ordered cache tagged FS_NAME_CACHE_BANK. Bank-local child names remain
 * operation scratch because they belong to the selected Bank, not the root
 * library.
 */
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
/*
 * One bounded `/.hcnames` absence-proof scan borrows the facade's normal file
 * handle and object-finder scratch between a failed read and a possible
 * bootstrap write.  These two bytes retain only the live scan phase and a
 * saturated 0/1/2-or-more match count; they are not an HCNAMES cache and are
 * reset for every facade request.  Keeping the proof inside the owning
 * operation prevents a NULL open callback (which can also mean I/O failure)
 * from authorizing a root-file creation.
 */
typedef enum {
    FS_HCNAMES_PROBE_IDLE = 0u,
    FS_HCNAMES_PROBE_OPEN_ROOT,
    FS_HCNAMES_PROBE_WAIT_ROOT,
    FS_HCNAMES_PROBE_SCAN,
    FS_HCNAMES_PROBE_CLOSE_ROOT,
    FS_HCNAMES_PROBE_WAIT_CLOSE
} fs_hcnames_probe_state_t;

typedef enum {
    FS_HCNAMES_PROBE_IN_PROGRESS = 0u,
    FS_HCNAMES_PROBE_ABSENT,
    FS_HCNAMES_PROBE_PRESENT,
    FS_HCNAMES_PROBE_DUPLICATE,
    FS_HCNAMES_PROBE_ERROR
} fs_hcnames_probe_result_t;

static fs_hcnames_probe_state_t op_hcnames_probe_state =
    FS_HCNAMES_PROBE_IDLE;
static uint8_t op_hcnames_probe_matches = 0u;
static char op_root_open_name[AFATFS_SHORT_FILENAME_MAX];
/*
 * One-operation Scene directory alias returned by asyncfatfs.
 *
 * What: receives the concrete short alias while a root Scene directory is
 * opened by its visible `NNN Name` key, then remains available for the second
 * open later in the same Scene load state machine. Why: the asyncfatfs LFN
 * helper can return an alias needed for an exact reopen, but retaining one
 * alias array for all 1,000 slots would duplicate the general name cache and
 * waste SRAM. This scratch is invalid outside the active Scene load.
 */
static char op_scene_root_open_name[AFATFS_SHORT_FILENAME_MAX];
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
/*
 * One transient derived filename component.
 *
 * Why: Kit/Scene writers open and serialize one Instrument file at a time, so
 * six retained member keys were duplicate SRAM state. Inputs: active source
 * Scene, voice, identity name, and type. Output: valid until the following
 * async FAT open has accepted it. Affiliates: kitset writer and member writer.
 */
static char op_filename_component[STORAGE_KIT_MEMBER_FILENAME_MAX];
static char op_save_kit_dir_open_name[AFATFS_SHORT_FILENAME_MAX];
static char op_save_scene_kit_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_scene_kit_open_name[AFATFS_SHORT_FILENAME_MAX];
static uint8_t op_kit_save_source_scene = 0u;
static storage_instrument_save_mode_t op_kit_save_mode =
    STORAGE_INSTRUMENT_SAVE_NORMAL;

static const char *filesystem_memberFilename(uint8_t slot)
{
    const scene_t *scene = scene_getConst(op_kit_save_source_scene);

    /*
     * Derive one Kit member leaf immediately before use.
     *
     * Why: the HCNAMES row, voice/type, and extension are sufficient; storing
     * six preformatted keys is neither authoritative nor needed concurrently.
     * Inputs: zero-based voice and request-stable source Scene. Output: one
     * 49-byte component for the immediate kitset/open operation, or blank.
     * Affiliates: filesystem_nextKitsetLine() and Kit/Scene save phases.
     */
    if (!scene || slot >= STORAGE_KIT_SLOT_COUNT) {
        op_filename_component[0] = '\0';
        return op_filename_component;
    }
    storage_makeSavedInstrumentDisplayFilename(
        op_filename_component, sizeof(op_filename_component),
        filesystem_identityName((uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 + slot)),
        scene->kit.instruments[slot].type, (uint8_t)(slot + 1u), 1u);
    return op_filename_component;
}

/*
 * Slot delete is a state machine because asyncfatfs exposes only
 * foreground-pumped operations. It scans /Kit/ for all physical directories
 * matching a numbered slot. Recursive generic delete was removed because the
 * product uses asyncfatfs' maintained tree deletion; keeping a second unused
 * implementation retained obsolete name and alias stacks in SRAM.
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

static fs_delete_slot_phase_t op_delete_slot_phase = FS_DELETE_SLOT_IDLE;
static afatfsFilePtr_t op_delete_slot_dir = NULL;
static uint8_t op_delete_slot_allow_short_alias = 0u;
static uint8_t op_delete_slot_bank_scene = 0u;
static uint16_t op_delete_slot_number = 0u;
static afatfsObjectId_t op_delete_slot_target_id;
/*
 * Completion latch for asyncfatfs' maintained recursive deleter.
 *
 * Inputs: afatfs_deleteTree() invokes the callback for the concrete object
 * captured by the slot scanner. Output: the slot-delete state machine learns
 * that the one foreground-pumped delete finished and whether it succeeded.
 * This is intentionally only a result latch, not the former firmware-owned
 * recursive name/alias stack; asyncfatfs owns that traversal now.
 */
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
/*
 * Non-Pattern Scene stage shape.
 *
 * What: one load-time Scene settings image plus its embedded Kit; PatternSet
 * is deliberately absent. Why: Scene settings/Kit validate atomically before
 * Pattern streams directly to final Scene SRAM under the agreed non-atomic
 * Pattern policy. Inputs: sceneset, kitset, and Instrument file parsers.
 * Outputs: filesystem_commitSceneStage() copies the validated image to the
 * selected resident Scene(s). Affiliates: Kit/Instrument stage members below,
 * Pattern loader phases, and the later Effect payload design.
 */
typedef struct {
    scene_settings_t settings;
    kit_t kit;
} filesystem_scene_stage_t;

/*
 * Runtime autosave parameter-drain state borrowed from the existing 2 KB
 * operation stage.
 *
 * The filesystem facade permits only one operation at a time, so this scalar
 * state cannot overlap Kit/Scene/Instrument parser staging. The full
 * 34,768-byte record remains streamed through staging_buf; only stable
 * transaction-local payload patches live in the separate bounded cache below.
 * The persistent 3,856-byte dirty record is owned by Autosave.c and therefore
 * survives reuse of this operation union.
 */
typedef struct {
    autosave_stream_validation_t validation;
    afatfsFilePtr_t target_file;
    uint32_t winner_generation;
    uint32_t target_crc32c;
    uint32_t stream_offset;
    uint32_t seek_position;
    uint16_t chunk_bytes;
    uint16_t chunk_written;
    uint16_t mask_bytes_read;
    uint16_t payload_scan_offset;
    uint16_t patch_count;
    uint16_t patch_cursor;
    uint8_t winner_index;
    uint8_t winner_probe;
    uint8_t candidate_index;
    uint8_t have_winner;
    uint8_t candidate_valid;
    uint8_t target_ready;
    uint8_t recovery_target_index;
    uint8_t recovery_using_names;
} filesystem_autosave_writer_state_t;

/*
 * Dedicated stable live-byte patch cache.
 *
 * What: holds at most the configured number of sorted uint16 payload offsets
 * and sampled byte values. Why: captured values must remain stable while the
 * one transformed source-to-target stream spans foreground ticks; the
 * canonical dirty record itself remains in Autosave.c. Inputs are bounded live
 * gets; outputs are immutable payload substitutions and error-rollback offsets.
 * Affiliates: filesystem_autosaveParameterDrain_tick(), Autosave.c's
 * transform, and filesystem_autosaveWriterFinishErrorNow().
 *
 * This allocation is intentionally independent from fs_list_cache_name. The
 * eventual dual-use optimization is deferred until hardware behavior is
 * confirmed, so Load/Save name-cache ownership cannot alias this test.
 */
typedef struct {
    uint16_t payload_offsets[AUTOSAVE_PARAMETER_GETS_PER_WRITE];
    uint8_t payload_values[AUTOSAVE_PARAMETER_GETS_PER_WRITE];
} filesystem_autosave_parameter_cache_t;

/*
 * Separate fixed-size non-Pattern payload stage.
 *
 * What: 2,048 bytes of aligned SRAM for one Kit, one Instrument, or one
 * Scene-with-Kit staging image. Why: the 9,000-byte name cache must remain a
 * cache only; sharing it with parser staging erased active `.hcindex` rows
 * during Load scrolling. 512 parameter cells require three endpoint images
 * (main, morph, interpolation), or 1,536 bytes because values are uint8_t.
 * The remaining budget covers current Scene/Kit metadata and reserves 384
 * bytes for a future non-Pattern Effect stage. Inputs: mutually exclusive
 * typed parsers. Outputs: one validated payload for commit; Pattern is never
 * placed here. Affiliates: filesystem_loadKitDirectory_tick(),
 * filesystem_loadInstrument_tick(), filesystem_loadSceneDirectory_tick(),
 * and InstrumentManager's 64-cells-per-voice contract.
 */
#define FS_STAGE_PARAMETER_CAPACITY   512u
#define FS_STAGE_PARAMETER_IMAGE_COUNT 3u
#define FS_STAGE_EFFECT_RESERVE_BYTES 384u
#define FS_STAGE_CACHE_BYTES          2048u

typedef union {
    uint8_t raw[FS_STAGE_CACHE_BYTES];
    kit_t kit_stage;
    /* One parsed Instrument candidate; its original `kit` source is now an
     * on-card `.hctmp.<ext>` file, so staging never owns a second image. */
    kit_instrument_slot_t instrument_stage;
    filesystem_scene_stage_t scene_stage;
    filesystem_autosave_writer_state_t autosave_writer;
} filesystem_stage_workspace_t;

/*
 * The 1,000-row index/HCNAMES cache is intentionally independent from staging.
 *
 * Inputs: scans, index readers, and HCNAMES transactions. Outputs: browser
 * names and slot occupancy. Why: a staged payload may now be prepared without
 * invalidating the selected index row used by scrolling and later opens.
 * Affiliates: filesystem_prepareLibraryNameCache(), filesystem_residentNames,
 * and the separate filesystem_stage_workspace_t above.
 */
static char fs_list_cache_name[FS_LIBRARY_NAME_CACHE_MAX]
                              [STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static filesystem_stage_workspace_t fs_stage_workspace;
static filesystem_autosave_parameter_cache_t fs_autosave_parameter_cache;

/*
 * The only operation/menu identity strings retained outside the name cache.
 *
 * Why: loading `.hcindex` necessarily reuses the name cache after HCNAMES has
 * been read. The active Bank is already retained once by BankData, so this
 * physical array preserves only Scene, Kit, and six Instrument identities;
 * together those logical nine rows replace SceneData fields and former Menu
 * scratch allocations without duplicating the Bank name.
 *
 * Inputs: Menu copies HCNAMES rows here before index traversal, and completed
 * loads/saves refresh their affected rows. Outputs: targeted HCNAMES writers
 * and filename formatters borrow the rows until the menu session ends.
 * Affiliates: filesystem_identityName(), menu.c, and the HCNAMES state machine.
 */
static char fs_identity_name[FS_IDENTITY_ROW_COUNT - 1u]
                            [STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static uint16_t fs_identity_valid_mask = 0u;

_Static_assert(sizeof(fs_list_cache_name) ==
                   (FS_LIBRARY_NAME_CACHE_MAX *
                    (STORAGE_KIT_DISPLAY_NAME_LEN + 1u)),
               "the index/HCNAMES cache must remain exactly 9000 bytes");
_Static_assert(sizeof(fs_identity_name) + BANK_DISPLAY_NAME_LEN + 1u == 81u,
               "one Bank plus one Scene, Kit, and six Instrument names is 81 bytes");
_Static_assert(INSTRUMENT_SLOT_COUNT * INSTRUMENT_PARAM_COUNT <=
                   FS_STAGE_PARAMETER_CAPACITY,
               "stage parameter capacity must cover all current kit voices");
_Static_assert(FS_STAGE_CACHE_BYTES >=
                   ((FS_STAGE_PARAMETER_CAPACITY *
                     FS_STAGE_PARAMETER_IMAGE_COUNT *
                     sizeof(instrument_param_value_t)) +
                    sizeof(scene_settings_t) + sizeof(kit_settings_t) +
                    (INSTRUMENT_SLOT_COUNT * sizeof(instrument_type_t)) +
                    FS_STAGE_EFFECT_RESERVE_BYTES),
               "stage cache must cover 512 parameter cells per image plus effect reserve");
_Static_assert(sizeof(filesystem_stage_workspace_t) <= FS_STAGE_CACHE_BYTES,
               "typed non-Pattern stage exceeds its fixed SRAM budget");
_Static_assert(_Alignof(filesystem_stage_workspace_t) >= _Alignof(kit_t),
               "typed stage must align Kit staging");
_Static_assert(
    sizeof(fs_autosave_parameter_cache.payload_offsets) /
            sizeof(fs_autosave_parameter_cache.payload_offsets[0]) ==
        AUTOSAVE_PARAMETER_GETS_PER_WRITE,
    "autosave offset cache must match the configured get cap");
_Static_assert(
    sizeof(fs_autosave_parameter_cache.payload_values) /
            sizeof(fs_autosave_parameter_cache.payload_values[0]) ==
        AUTOSAVE_PARAMETER_GETS_PER_WRITE,
    "autosave value cache must match the configured get cap");
_Static_assert(sizeof(filesystem_autosave_parameter_cache_t) == 4608u,
               "autosave patch cache must remain exactly 4608 bytes");
_Static_assert(sizeof(filesystem_autosave_parameter_cache_t) <= 9000u,
               "dedicated autosave cache must stay within its 9 KB ceiling");
_Static_assert(AUTOSAVE_WRITER_INTERVAL_MS < 0x8000u,
               "autosave debounce must fit the wrapping scheduler comparison");
_Static_assert(AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS < 0x8000u,
               "autosave continuation must fit the wrapping scheduler comparison");
_Static_assert(SETTINGS_AUTOWRITE_DEBOUNCE_MS > 0u &&
               SETTINGS_AUTOWRITE_DEBOUNCE_MS < 0x8000u,
               "settings debounce must fit the wrapping scheduler comparison");

#define op_staged_kit        (fs_stage_workspace.kit_stage)
#define op_staged_instrument (fs_stage_workspace.instrument_stage)
#define op_autosave_writer   (fs_stage_workspace.autosave_writer)

void filesystem_clearIdentityNames(void)
{
    /*
     * Invalidate the one operation-scoped identity block.
     *
     * Inputs: none. Output: Scene/Kit/Instrument LCD/name clients observe
     * blank rows until the next HCNAMES entry fetch. The Bank row is not
     * cleared because BankData is the one resident Bank-name owner; clearing
     * it here would either duplicate or lose the current Bank identity.
     * Affiliates: menu session exit, BankData, and filesystem operation reset.
     */
    memset(fs_identity_name, 0, sizeof(fs_identity_name));
    fs_identity_valid_mask = 0u;
}

void filesystem_setIdentityName(uint8_t row, const char name[8])
{
    uint8_t i;

    /*
     * Copy exactly one HCNAMES-style display row into the identity block.
     *
     * Inputs: logical row selector and fixed eight-cell source. Output: the
     * Bank row routes to BankData's existing sole name; other rows become
     * printable, NUL-terminated identity text plus a valid-row bit. Why: never
     * retain a filename/stem or a duplicate Bank/menu name copy. Affiliates:
     * menu HCNAMES completion, BankData, and targeted update helpers.
     */
    if (row >= FS_IDENTITY_ROW_COUNT)
        return;
    if (row == FS_IDENTITY_BANK_ROW) {
        if (name)
            bank_setDisplayName(name);
        return;
    }
    row--;
    memset(fs_identity_name[row], 0, sizeof(fs_identity_name[row]));
    for (i = 0u; name && i < STORAGE_KIT_DISPLAY_NAME_LEN; i++) {
        char c = name[i];
        fs_identity_name[row][i] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
    }
    fs_identity_name[row][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    if (name)
        fs_identity_valid_mask = (uint16_t)(fs_identity_valid_mask |
                                             (uint16_t)(1u << (row + 1u)));
    else
        fs_identity_valid_mask = (uint16_t)(fs_identity_valid_mask &
                                             (uint16_t)~(1u << (row + 1u)));
}

const char *filesystem_identityName(uint8_t row)
{
    /*
     * Borrow one active identity row without allocating a second cache.
     *
     * Inputs: logical row 0..8. Output: BankData's one Bank row for row zero,
     * otherwise fixed-width NUL-terminated identity text or blanks. This
     * mapping keeps the public nine-row interface while physically storing no
     * duplicate Bank string. Affiliates: menu LCD, HCNAMES writers, BankData,
     * and on-demand filename construction.
     */
    if (row == FS_IDENTITY_BANK_ROW)
        return bank_displayName();
    if (row >= FS_IDENTITY_ROW_COUNT ||
        (fs_identity_valid_mask & (uint16_t)(1u << row)) == 0u) {
        return "        ";
    }
    return fs_identity_name[row - 1u];
}

char *filesystem_identityNameMutable(uint8_t row)
{
    /*
     * Provide the Menu editor direct access to its one identity row.
     *
     * Inputs: logical non-Bank row selected by the Kit/Instrument editor.
     * Output: the canonical 9-byte operation string, or NULL for Bank/bad
     * input. Bank is intentionally immutable here because BankData owns its
     * sole row; a temporary editor string would duplicate that identity.
     * Affiliates: menu.c Save editing, BankData, and filesystem_setIdentityName.
     */
    if (row == FS_IDENTITY_BANK_ROW || row >= FS_IDENTITY_ROW_COUNT)
        return NULL;
    fs_identity_valid_mask = (uint16_t)(fs_identity_valid_mask |
                                         (uint16_t)(1u << row));
    return fs_identity_name[row - 1u];
}
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
 * op_bank_child_present_mask records only 00..15 occupancy for LEDs and mask
 * intersection. A selected child is rescanned from disk immediately before it
 * is opened, using the existing one-Scene stage and operation name scratch;
 * no Bank-local name, alias, or per-child key cache exists. The
 * op_bank_payload_active flag hands control to the shared Scene payload
 * reader/writer after that single child has been positioned.
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
static uint16_t op_bank_child_present_mask = 0u;
static uint16_t op_bank_scene_load_mask = 0u;
static uint16_t op_bank_scene_save_mask = 0u;
static uint8_t op_bank_active_scene = 0u;
static uint8_t op_bank_child_cursor = 0u;
static uint8_t op_bank_loaded_scene = 0u;
static uint8_t op_bank_payload_active = 0u;
static uint8_t op_rename_done = 0u;
typedef enum {
    FS_REPAIR_SCOPE_NONE = 0u,
    FS_REPAIR_SCOPE_LIBRARY,
    FS_REPAIR_SCOPE_INSTRUMENT,
    FS_REPAIR_SCOPE_BANK_LOAD,
} fs_repair_scope_t;

/*
 * Operation-local name-repair scratch.
 *
 * The sanitizer is deliberately not a browser cache. It remembers only the
 * parent namespace currently being scanned, one candidate old/new component,
 * and the suffix retry needed when asyncfatfs reports a target collision.
 * After each successful rename the parent is rescanned from disk, so no SRAM
 * occupancy or alias table can diverge from the physical card.
 */
static fs_repair_scope_t op_repair_scope = FS_REPAIR_SCOPE_NONE;
static fs_name_cache_kind_t op_repair_library_kind = FS_NAME_CACHE_NONE;
static instrument_type_t op_repair_instrument_type = INSTRUMENT_TYPE_UNKNOWN;
static uint8_t op_repair_registry_index = 0u;
static uint16_t op_repair_bank_slot = 0u;
static uint16_t op_repair_suffix = 0u;
static uint8_t op_repair_retry = 0u;
static char op_repair_old_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_repair_new_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_repair_base_display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
static char op_repair_rename_open_name[AFATFS_SHORT_FILENAME_MAX];
/*
 * Open-before-create guard for index directory setup.
 *
 * The `.hcindex` writers need parent directories to exist, but a direct
 * create-capable LFN open can allocate a duplicate directory when a host
 * already created an LFN variant. Index generation therefore first tries a
 * strict open; this flag records that only one fallback mkdir attempt is
 * allowed after a proven miss.
 */
static uint8_t op_create_dir_retry = 0u;
/*
 * Scene load helper prototypes.
 *
 * The Scene loader is kept beside the Kit directory loader, but a few generic
 * Scene-name helpers live with the save/scan helpers later in this file.
 * Declaring them here keeps the C translation unit explicit under gnu11 and
 * avoids implicit-function warnings when the state machine calls them.
 */
static void filesystem_initSceneStage(filesystem_scene_stage_t *stage);
static void filesystem_commitSceneStage(void);
static PatternSet *filesystem_directPatternTarget(void);
static void filesystem_resetSceneLoadChildDiscovery(void);
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
static void filesystem_createBootIndex_tick(void);
static void filesystem_createLibraryIndex_tick(void);
static void filesystem_writeResidentNames_tick(void);
static void filesystem_writeBootLog_tick(void);
static void filesystem_residentNames_tick(void);
static void filesystem_ensureAutosaveFiles_tick(void);
static void filesystem_autosaveParameterDrain_tick(void);
static void filesystem_autosaveTraceFlush_tick(void);
static void filesystem_settingsWriterSchedule_tick(void);
static void filesystem_settingsWriterCompleted(void);
static void filesystem_autosaveWriterSchedule_tick(void);
static void filesystem_autosaveWriterCompleted(void);
static void filesystem_autosaveTraceFlushSchedule_tick(void);
static void filesystem_autosaveTraceFlushCompleted(void);
static void filesystem_autosaveTraceCaptured(uint8_t budget_exhausted);
static void filesystem_autosaveSetupCompleted(void);
static uint8_t filesystem_residentNameIsBlank(const char *name);
static uint8_t filesystem_formatResidentNameLine(char *dst,
                                                 uint16_t cap,
                                                 const char *name,
                                                 uint8_t present);
static uint8_t filesystem_nextResidentNameLine(char *dst,
                                               uint16_t cap,
                                               uint16_t row);
static void filesystem_repairNames_tick(void);
static uint8_t filesystem_repairBuildCandidate(void);
typedef enum {
    FS_KIT_VALIDATION_VALID = 0u,
    FS_KIT_VALIDATION_INVALID_CONTENT,
    FS_KIT_VALIDATION_IO_ABORT,
} fs_kit_validation_result_t;

typedef enum {
    FS_KIT_QUARANTINE_OK = 0u,
    FS_KIT_QUARANTINE_IO_ABORT,
} fs_kit_quarantine_result_t;

static fs_kit_quarantine_result_t filesystem_quarantineKitLibraryBlocking(void);
static uint8_t filesystem_kitMemberNameIsCanonical(const char *name);
static void filesystem_bootLoggingSetBankDetail(const char suffix[3]);
static void filesystem_bootLoggingSetBankSceneDetail(char family);
static uint8_t filesystem_bankPayloadDetailActive(void);
static bool filesystem_startRepairLibraryNames(fs_library_index_kind_t kind,
                                               fs_completion_cb_t cb);
static bool filesystem_startRepairInstrumentNames(instrument_type_t type,
                                                  fs_completion_cb_t cb);
static bool filesystem_startRepairBankNames(uint16_t slot,
                                            fs_completion_cb_t cb);
static bool filesystem_start(fs_internal_op_t op, fs_file_type_t type,
                             uint16_t slot, fs_completion_cb_t cb);
static uint8_t filesystem_displayNameMatchesCaseInsensitive(
    const char *candidate, const char *target);
static void filesystem_hcnamesProbeBegin(void);
static fs_hcnames_probe_result_t filesystem_hcnamesProbe_tick(void);
static uint8_t filesystem_hcnamesProbeIsScanning(void);
static void filesystem_libraryIndexRebuildScanComplete(void);
static void filesystem_libraryIndexRebuildWriteComplete(void);
static void filesystem_loadInstrumentIndex_tick(void);
static void filesystem_loadLibraryIndex_tick(void);
static uint16_t filesystem_cachedInstrumentCount(instrument_type_t type);
static const char *filesystem_cachedInstrumentName(instrument_type_t type,
                                                   uint16_t index);
static const char *filesystem_cachedLibraryName(fs_name_cache_kind_t kind,
                                                uint16_t slot);
static uint8_t filesystem_librarySlotExists(fs_name_cache_kind_t kind,
                                             uint16_t slot);
static void filesystem_prepareLibraryNameCache(fs_name_cache_kind_t kind);
static storage_status_t filesystem_readTextLine(afatfsFilePtr_t file,
                                                char *buffer,
                                                uint8_t *len,
                                                uint8_t limit,
                                                uint8_t *line_ready,
                                                uint8_t *eof);
static void filesystem_scanBanks_tick(void);
static void filesystem_scanBankScenes_tick(void);
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
 * Generalized name-index cache.
 *
 * What: stores the currently active library's eight-character display names.
 * Instrument rows occupy the first sorted entries, up to all 1,000 rows;
 * Kit, root Scene, and root Bank rows occupy their direct 000..999 slot
 * positions so an index line can be turned back into `NNN ` + name without
 * sorting. HCNAMES temporarily occupies its fixed 129 logical rows during
 * Instrument menu entry or targeted post-action update.
 * Why: this is the one SRAM name cache. A type/library transition disposes it
 * and the newly selected `.hcindex` repopulates it, so no per-Instrument,
 * per-Kit, per-Scene, or per-Bank display-name arrays can retain stale browser
 * text.
 * Open aliases and longer source stems are operation-local values; they are
 * not browser-cache identity and therefore are not retained in this array.
 * Inputs/outputs: filesystem scan/index/save state machines write these cells;
 * Menu and Preset access them only through filesystem accessors.
 */
static fs_name_cache_kind_t fs_list_cache_kind = FS_NAME_CACHE_NONE;
static instrument_type_t fs_list_cache_type = INSTRUMENT_TYPE_UNKNOWN;
static uint16_t fs_list_cache_count;
static uint8_t op_instrument_load_destination_slot = 0u;
static uint8_t op_instrument_load_destination_scene = 0u;
static instrument_type_t op_instrument_load_type = INSTRUMENT_TYPE_UNKNOWN;
/* True only while loading `.hctmp.<ext>` back into the `kit` menu row. */
static uint8_t op_instrument_load_temporary = 0u;
/* A browser index addresses any row in the shared 1,000-entry cache. */
static uint16_t op_instrument_load_index = 0u;
/* Registry indices, rather than enum values, drive folder iteration. */
static uint8_t op_instrument_scan_registry_index = 0u;
static instrument_type_t op_instrument_index_type = INSTRUMENT_TYPE_UNKNOWN;
static uint8_t op_instrument_scan_one_type = 0u;
static fs_name_cache_kind_t op_library_index_kind = FS_NAME_CACHE_NONE;
/*
 * Durable library-index rebuild chain.
 *
 * What: holds the original completion callback while a physical Kit/Scene/Bank
 * directory scan and its complete slot-ordered `.hcindex` rewrite run after a
 * flush. Why: a successful numbered-root Save can create, rename, or remove a
 * directory and must not publish completion while `.hcindex` still describes
 * the preceding namespace. Pure Loads never enter this chain: after DSP apply
 * Menu performs a read-only reload of the unchanged index. Inputs: rebuild
 * kind plus the original Save callback. Output: exactly one original callback
 * after the scan/index chain is durable; no new retained storage is allocated.
 */
static uint8_t op_library_index_rebuild_pending = 0u;
static fs_name_cache_kind_t op_library_index_rebuild_kind = FS_NAME_CACHE_NONE;
static fs_completion_cb_t op_library_index_rebuild_callback = NULL;
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
/* True only for the hidden reversible-load save; skip cache/name publication. */
static uint8_t op_instrument_save_temporary = 0u;

/*
 * Dispose the only physical browser-name array.
 *
 * What: clears the active-domain tag, typed Instrument tag, row count, and
 * all 1,000 fixed-width name cells. Why: a cache transition must invalidate
 * both names and occupancy together; otherwise a subsequent Kit, Scene, Bank,
 * or Instrument request could validate a slot against stale SRAM text.
 * Inputs/outputs: none; callers choose and repopulate the next domain after
 * this function returns. This is the private storage primitive behind the
 * public menu lifecycle APIs.
 */
static void filesystem_clearNameCacheStorage(void)
{
    /*
     * Reset the dedicated name-cache storage for a cache transition.
     *
     * Inputs: callers select a new cache domain. Output: index/HCNAMES rows
     * are cleared without modifying the separate typed stage. Affiliates:
     * HCNAMES, `.hcindex`, menu browse state machines, and typed load stages.
     */
    fs_list_cache_kind = FS_NAME_CACHE_NONE;
    fs_list_cache_type = INSTRUMENT_TYPE_UNKNOWN;
    fs_list_cache_count = 0u;
    memset(fs_list_cache_name, 0, sizeof(fs_list_cache_name));
}

/*
 * Keep the old private helper name as a migration shim for Instrument paths.
 * What: clears the same generalized cache used by Kit and Scene. Why: the
 * earlier Instrument-only implementation has many carefully audited callers;
 * routing them through this shim prevents one caller from accidentally
 * clearing only an obsolete typed sub-cache after the generalization.
 */
static void filesystem_clearInstrumentCacheStorage(void)
{
    filesystem_clearNameCacheStorage();
}

/*
 * Select a numbered root-library view in the shared cache.
 *
 * What: disposes the previous domain, tags the requested Kit, root Scene, or
 * root Bank domain, and exposes its 000..999 row count. Why: numbered library
 * rows must remain at their physical slot index, including blank slots, so an
 * index reader or physical scan cannot compact them into a second structure.
 * Instrument callers use their separate typed preparation path because their
 * rows are sorted file names rather than direct slot coordinates.
 */
static void filesystem_prepareLibraryNameCache(fs_name_cache_kind_t kind)
{
    filesystem_clearNameCacheStorage();
    fs_list_cache_kind = kind;
    fs_list_cache_count = (kind == FS_NAME_CACHE_KIT)
        ? STORAGE_KIT_MAX_SLOTS
        : (kind == FS_NAME_CACHE_SCENE)
            ? STORAGE_SCENE_MAX_SLOTS
        : (kind == FS_NAME_CACHE_BANK)
            ? STORAGE_BANK_MAX_SLOTS
            : 0u;
}

/*
 * Borrow one name from the active numbered-library cache.
 *
 * What: returns a direct 000..999 row only when both the requested domain and
 * slot are active. Why: a name from another library is not a valid open key or
 * occupancy record, and returning NULL on a domain mismatch prevents callers
 * from accidentally using stale rows after a menu transition. The returned
 * pointer remains valid until the next cache disposal or replacement.
 */
static const char *filesystem_cachedLibraryName(fs_name_cache_kind_t kind,
                                                uint16_t slot)
{
    if (fs_list_cache_kind != kind || slot >= fs_list_cache_count)
        return NULL;
    return fs_list_cache_name[slot];
}

/* Query occupancy without introducing a second per-slot bitmap.
 *
 * What: treats a non-empty row in the active slot-ordered Kit/Scene/Bank index as
 * the complete existence record for that slot. Why: the shared 1,000-entry
 * cache already has to retain the display name used to reconstruct `NNN
 * Name`; a separate presence array would consume SRAM and could disagree with
 * the name after cache disposal or index reload. Inputs are the active cache
 * domain and zero-based slot. Output is nonzero only for a valid, non-blank
 * cached name. Clients: Kit/Scene/Bank scan, load validation, menu accessors, and
 * first-slot fallback selection.
 */
static uint8_t filesystem_librarySlotExists(fs_name_cache_kind_t kind,
                                             uint16_t slot)
{
    const char *name = filesystem_cachedLibraryName(kind, slot);

    return (uint8_t)(name != NULL && name[0] != '\0');
}
/*
 * Validated one-Instrument staging payload.
 *
 * Filesystem owns these buffers from request start through completion. Parsing
 * writes only here, so audio cannot observe a half-read type or parameter
 * image. Preset reads the immutable result after FS_STATUS_DONE and performs
 * the ordered modulation-clear/Scene-commit/runtime-apply transaction.
 */
/*
 * The validated Instrument payload is `op_staged_instrument`, the union alias
 * above.  Its display/key metadata is intentionally absent: successful
 * operations publish the authoritative HCNAMES row, and future opens derive
 * the eight-character leaf plus type extension on demand.
 */
static uint32_t op_stream_index = 0;
/* Also indexes 000..999 `.hcindex` rows, so this must not wrap at 255. */
static uint16_t op_item_offset = 0;
static uint8_t op_loaded_active_pattern_running = 0;
static uint8_t op_file_version = 0;
static fs_mount_result_t fs_last_mount_result = FS_MOUNT_RESULT_UNKNOWN;
static uint8_t fs_boot_detected_unsupported_card = 0;
static fs_stale_warning_source_t fs_stale_warning_pending = FS_STALE_WARNING_NONE;
/*
 * Retired File/Dir diagnostics.
 *
 * The generic asyncfatfs test UI is no longer a product surface. Keeping its
 * 64 x 49-byte file and directory lists would reserve 6,240 bytes outside the
 * agreed name/staging contract, so the entire diagnostic state machine is
 * excluded rather than silently sharing or repurposing its cache.
 */
#if 0
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
#endif

/*
 * Bank Save scratch-name nonce counter.
 *
 * This is independent of the retired File/Dir diagnostic state above. Inputs:
 * each Bank Save scratch-directory attempt. Output: one incrementing value
 * mixed with RNG/tick by filesystem_nextBankScratchNonce(), avoiding stale
 * temporary-tree collisions without retaining any name cache.
 */
static uint16_t fs_bank_scratch_counter = 0u;

#define FS_IDLE_POLL_MS 5u
/* .all still carries the old raw meta prefix until that container is rebuilt.
 * settings.cfg does not use this compatibility span: root settings now persist
 * as keyed text and the former raw globals filename is deliberately ignored. */
#define FS_GLOBALS_LEGACY_LEN_22  22u
static uint16_t fs_last_idle_poll_tick = 0;
/*
 * Debounced settings.cfg persistence state.
 *
 * What: retains one pending flag/deadline, a boot/runtime authorization gate,
 * and live/captured revisions for the streaming writer. Why: Global/source
 * bursts must coalesce, while a change occurring after its line was emitted
 * must survive for a complete follow-up file write. Inputs are Menu/Preset
 * dirty notifications and time_sysTick. Outputs are autonomous reuse of the
 * existing SAVE_GLOBALS operation only while the facade is idle. Affiliates:
 * filesystem_saveGlobals_tick(), filesystem_complete(), and the scheduler.
 */
static uint16_t fs_settings_next_due_tick = 0u;
static uint32_t fs_settings_change_revision = 0u;
static uint32_t op_settings_change_revision = 0u;
static uint8_t fs_settings_dirty = 0u;
static uint8_t fs_settings_runtime_ready = 0u;
static uint8_t op_settings_write_active = 0u;
/*
 * Background autosave cadence state.
 *
 * This is deliberately separate from fs_last_idle_poll_tick: the latter owns
 * 5 ms idle SD polling, while these fields retain the next absolute autosave
 * deadline selected by the completion callback. Inputs are the ordinary
 * five-second cadence or the short durable-backlog continuation cadence;
 * output is one wrapping deadline that survives after the operation stage is
 * reused. Affiliates: filesystem_autosaveWriterCompleted() and
 * filesystem_autosaveWriterSchedule_tick().
 */
static uint16_t fs_autosave_next_due_tick = 0u;
static uint8_t fs_autosave_writer_armed = 0u;
/*
 * Logging-only trace cadence state.
 *
 * Input is the next wrapping millisecond deadline selected after a trace
 * append; output prevents diagnostic appends from repeatedly claiming an idle
 * facade. This extra two-byte allocation is excluded with the trace module in
 * production, so DEV_MODE_LOGGING 0 has neither trace SRAM nor trace I/O.
 */
#if DEV_MODE_LOGGING
static uint16_t fs_autosave_trace_next_due_tick = 0u;
#endif
/*
 * Pre-audio boot owns first creation of the hidden record pair. This gate
 * remains clear from reset until that create-only transaction has completed,
 * preventing the runtime recovery writer from racing the Bank/globals boot
 * ladder when a card starts with no autosave files.
 */
static uint8_t fs_autosave_writer_boot_ready = 0u;
/*
 * One-time runtime import of the valid winner's file-carried dirty mask.
 *
 * What: successful boot pair setup sets this flag even when SRAM is clean;
 * successful writer validation clears it. Why: file masks recover interrupted
 * work once, after which an empty canonical mask must cause no filesystem
 * operation. Inputs/outputs: boot ensure and writer completion own the flag;
 * the idle scheduler treats it as work independent of current dirty bits.
 * Affiliate: Autosave's tracking gate and maskMergeChunk().
 */
static uint8_t fs_autosave_recovery_pending = 0u;
/*
 * User policy and runtime autosave lifecycle state.
 *
 * Inputs: settings/menu AutoSave preference, the post-boot runtime gate, and
 * autonomous operation callbacks. Outputs: setup is queued only for an enabled
 * resident Bank; OFF suppresses all new hidden-file operations and may defer a
 * canonical-mask discard until an active transform completes. Why: the
 * scheduler, ensure wrapper, and completion callbacks all need the same
 * defense rather than trusting one Menu hook. Affiliates:
 * filesystem_setAutosaveEnabled(), main.c, and Autosave's producer/mask APIs.
 */
static uint8_t fs_autosave_enabled = 1u;
static uint8_t fs_autosave_runtime_ready = 0u;
static uint8_t fs_autosave_setup_pending = 0u;
static uint8_t fs_autosave_setup_failed = 0u;
static uint8_t fs_autosave_transaction_active = 0u;
static uint8_t fs_autosave_discard_pending = 0u;

/* Existing morph destination buffer owned by preset/sound code.
 *
 * Directory kit loading writes primary parameters into parameter_values[] and
 * optional/fallback morph parameters into parameters2[]. The legacy morph .SND
 * loader also uses parameters2[], so this remains an external affiliate rather
 * than storage owned by filesystem.c.
 */
extern uint8_t parameters2[END_OF_SOUND_PARAMETERS];

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

static void on_autosave_target_opened(afatfsFilePtr_t file)
{
    /*
     * The copy-forward phase keeps op_file as its read handle. AsyncFATFS
     * returns the concurrently opened inactive target through this dedicated
     * callback into the existing operation stage rather than overwriting the
     * reader handle or allocating another global file pointer.
     */
    op_autosave_writer.target_file = file;
    op_autosave_writer.target_ready = 1u;
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
     * Affiliates/clients: filesystem_saveInstrument_tick(), InstrumentMrp
     * Save, and the autosave writer's inactive-record deduplication phases.
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

/* Retired File/Dir diagnostic helpers; see the disabled state block above. */
#if 0
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
#endif

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

/* Retired File/Dir diagnostic stream helpers; no product operation calls them. */
#if 0
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
#endif

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
    /*
     * Normalize the persistent AutoSave preference at the common Global-load
     * boundary. Input is any flat/global compatibility image. Output is one
     * strict boolean, with nonzero legacy bytes interpreted as ON. Why: keyed
     * settings parsing is not the only caller of this sanitizer. Affiliates:
     * menu_sendAllGlobals() and filesystem_setAutosaveEnabled().
     */
    parameter_values[PAR_AUTOSAVE_ENABLED] =
        parameter_values[PAR_AUTOSAVE_ENABLED] ? 1u : 0u;
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
    /*
     * New settings fields retain backward-compatible defaults under version 1.
     *
     * Input: every keyed settings load before file overlay. Outputs: AutoSave
     * starts ON and all sixteen Scene sources become explicitly unknown. Why:
     * missing files/keys and later manual Settings Loads must not retain stale
     * provenance. Affiliates: SceneData's exact 32-byte source owner and the
     * parser/writer below.
     */
    parameter_values[PAR_AUTOSAVE_ENABLED] = 1u;
    scene_resetSources();
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
    else if (strcmp(key, "autosave") == 0) *param = PAR_AUTOSAVE_ENABLED;
    else return 0u;
    return 1u;
}

static uint8_t filesystem_settingsSceneSourceIndex(const char *key,
                                                   uint8_t *scene_index)
{
    static const char prefix[] = "scene_source_";
    uint8_t tens;
    uint8_t ones;
    uint8_t parsed_index;

    /*
     * Recognize one exact scene_source_00..15 key with a tri-state result.
     *
     * Inputs: trimmed settings key and output index. Output: zero means an
     * unrelated forward-compatible key, one means a valid Scene-source key,
     * and two means the reserved prefix was present but malformed. Why: the
     * parser must ignore unrelated future keys while failing closed for a
     * misspelled source assignment. Affiliates: filesystem_parseSettingsLine()
     * and SceneData's bounded encoded setter.
     */
    if (!key || strncmp(key, prefix, sizeof(prefix) - 1u) != 0)
        return 0u;
    key += sizeof(prefix) - 1u;
    if (key[0] < '0' || key[0] > '9' ||
        key[1] < '0' || key[1] > '9' || key[2] != '\0') {
        return 2u;
    }
    tens = (uint8_t)(key[0] - '0');
    ones = (uint8_t)(key[1] - '0');
    parsed_index = (uint8_t)(tens * 10u + ones);
    if (!scene_indexValid(parsed_index))
        return 2u;
    if (scene_index)
        *scene_index = parsed_index;
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
    uint8_t source_key_state;
    uint8_t source_scene;

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
    source_key_state = filesystem_settingsSceneSourceIndex(
        key, &source_scene);
    if (source_key_state != 0u) {
        /*
         * Restore one two-byte Scene provenance value from keyed text.
         *
         * Inputs: exact scene_source_NN key and decimal U16 text. Output: the
         * retained source receives 0..1999 or UINT16_MAX; malformed reserved
         * keys/values fail the settings operation. Why: 2000..65534 have no
         * defined source meaning. Affiliates: SceneData's 32-byte owner and
         * filesystem_nextSettingsLine().
         */
        if (source_key_state != 1u ||
            !filesystem_parseSettingsU16(value, &parsed) ||
            !scene_setSourceEncoded(source_scene, parsed)) {
            return FS_STATUS_ERROR;
        }
        return FS_STATUS_DONE;
    }
    if (filesystem_settingsParamForKey(key, &param)) {
        if (!filesystem_parseSettingsU16(value, &parsed) || parsed > 255u ||
            (param == PAR_AUTOSAVE_ENABLED && parsed > 1u)) {
            return FS_STATUS_ERROR;
        }
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

#if 0 /* Retired binary Step/Pattern/All/Performance bridge; v3 text is below. */
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
#endif

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

/*
 * Convert private filesystem ownership into the stable eight-byte boot codes.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: classifies every operation reachable from the current pre-audio
 * ladder, using file type only where the shared library-index reader needs it.
 * Why: internal enum values and phase numbers are unstable implementation
 * details, while bootlog.bin must remain human-readable across builds. Input
 * is one operation plus its typed file domain; output is an eight-byte literal
 * or NULL for runtime-only/unclassified work. Affiliates: filesystem_start(),
 * explicit mount/quarantine/handoff arms, and BOOT_LOGGING.md.
 */
static const char *filesystem_bootLogCodeForOperation(
    fs_internal_op_t op, fs_file_type_t type)
{
    switch (op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:         return "FSFLUSH ";
    case FS_INTERNAL_OP_CREATE_BOOT_INDEX:    return "INSINDEX";
    case FS_INTERNAL_OP_CREATE_LIBRARY_INDEX: return "LIBINDEX";
    case FS_INTERNAL_OP_WRITE_HCNAMES:        return "HCNAMES ";
    case FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES:return "ASENSURE";
    case FS_INTERNAL_OP_LOAD_KIT:
    case FS_INTERNAL_OP_LOAD_KIT_MORPH:       return "KITLOAD ";
    case FS_INTERNAL_OP_LOAD_SCENE:           return "SCNELOAD";
    case FS_INTERNAL_OP_LOAD_BANK:            return "BANKLOAD";
    case FS_INTERNAL_OP_LOAD_GLOBALS:         return "GLOBLOAD";
    case FS_INTERNAL_OP_SCAN_KITS:            return "KITSCAN ";
    case FS_INTERNAL_OP_SCAN_SCENES:          return "SCNSCAN ";
    case FS_INTERNAL_OP_SCAN_BANKS:           return "BNKSCAN ";
    case FS_INTERNAL_OP_SCAN_INSTRUMENTS:     return "INSSCAN ";
    case FS_INTERNAL_OP_REPAIR_NAMES:         return "NAMEREPR";
    case FS_INTERNAL_OP_LOAD_LIBRARY_INDEX:
        if (type == FS_FILE_BANK)
            return "BIDXLOAD";
        if (type == FS_FILE_SCENE)
            return "SIDXLOAD";
        if (type == FS_FILE_KIT)
            return "KIDXLOAD";
        return NULL;
    default:
        return NULL;
    }
}

/*
 * Poll the cooperative boot deadline from either filesystem progress path.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: compares the wrapping 16-bit millisecond tick against the currently
 * armed normal or recovery start time. Why: facade state machines advance via
 * filesystem_tick(), but validation/quarantine helpers call afatfs_poll()
 * directly, so both must share one latch. Inputs are logging flags and
 * time_sysTick; output is nonzero after expiry and FS_STATUS_ERROR is published
 * to unwind facade-owned busy loops. It deliberately does not finish, close,
 * flush, or invoke callbacks for the abandoned operation. A C/driver call that
 * never returns cannot be preempted by this cooperative check; SD command code
 * retains its own bounded loops. Affiliates: filesystem_tick(),
 * filesystem_blockPoll(), and the bounded recovery writer.
 */
static uint8_t filesystem_bootLoggingPollDeadline(void)
{
#if DEV_MODE_LOGGING
    uint16_t started;

    if (!fs_boot_logging_active)
        return 0u;
    if (fs_boot_logging_recovery) {
        if (fs_boot_logging_recovery_failed)
            return 1u;
        started = fs_boot_logging_recovery_started_tick;
        if ((uint16_t)(time_sysTick - started) <
            BOOT_FILESYSTEM_TIMEOUT_MS)
            return 0u;
        fs_boot_logging_recovery_failed = 1u;
        status = FS_STATUS_ERROR;
        return 1u;
    }
    if (!fs_boot_logging_armed)
        return fs_boot_logging_timed_out;
    if ((uint16_t)(time_sysTick - fs_boot_logging_started_tick) <
        BOOT_FILESYSTEM_TIMEOUT_MS)
        return 0u;
    fs_boot_logging_timed_out = 1u;
    fs_boot_logging_armed = 0u;
    status = FS_STATUS_ERROR;
    return 1u;
#else
    return 0u;
#endif
}

/*
 * End only the current normal boot-operation deadline.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: disarms the timer after an operation/mount reaches a terminal result
 * without closing the wider logging window. Why: CPU-only boot work between
 * filesystem requests must not be attributed to the operation that just
 * completed. Inputs are completion and mount results; output is an unarmed
 * logger that preserves its last code. Recovery keeps its independent total
 * deadline. Affiliates: filesystem_complete() and the mount wrapper.
 */
static void filesystem_bootLoggingOperationDone(void)
{
#if DEV_MODE_LOGGING
    if (!fs_boot_logging_recovery)
        fs_boot_logging_armed = 0u;
#endif
}

void filesystem_bootLoggingBegin(void)
{
    /*
     * Open the logging window immediately before the pre-audio mount.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Inputs: an idle boot filesystem context and the live millisecond tick.
     * Outputs: clears old timeout/recovery latches, initializes the retained
     * code to spaces, and waits for the first explicit Arm. Why: runtime work
     * must never inherit a prior reset's watchdog state. Affiliates: main.c and
     * filesystem_bootLoggingEnd().
     */
#if DEV_MODE_LOGGING
    fs_boot_logging_active = 1u;
    fs_boot_logging_armed = 0u;
    fs_boot_logging_timed_out = 0u;
    fs_boot_logging_recovery = 0u;
    fs_boot_logging_recovery_failed = 0u;
    fs_boot_logging_started_tick = time_sysTick;
    fs_boot_logging_recovery_started_tick = time_sysTick;
    memset(fs_boot_logging_code, ' ', sizeof(fs_boot_logging_code));
#endif
}

void filesystem_bootLoggingArm(const char code[8])
{
    /*
     * Capture one operation boundary and start its fresh ten-second deadline.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Input is exactly eight bytes and need not be NUL-terminated. Output is a
     * byte-for-byte SRAM copy plus the current 1 kHz tick. Why: strlen() would
     * make trailing spaces unreliable and the recovery remount must preserve
     * the culprit code. Arms are ignored outside boot and during recovery so
     * `BOOTLOG ` can never replace the failed operation. Affiliates:
     * filesystem_start(), mount, flush, Kit quarantine, and internal handoffs.
     */
#if DEV_MODE_LOGGING
    if (!fs_boot_logging_active || fs_boot_logging_recovery ||
        fs_boot_logging_timed_out || !code)
        return;
    memcpy(fs_boot_logging_code, code, sizeof(fs_boot_logging_code));
    fs_boot_logging_started_tick = time_sysTick;
    fs_boot_logging_armed = 1u;
#else
    (void)code;
#endif
}

/*
 * Update the retained substep label without changing the enclosing
 * boot-operation deadline.
 *
 * Inputs: exactly eight bytes and an already armed normal boot logger. Output:
 * only fs_boot_logging_code changes. Why: timeout recovery needs the last
 * wait-capable primitive, while every successful substep must remain inside
 * the enclosing operation's original ten-second budget. This helper does not
 * arm, disarm, reset a tick, poll, allocate, call FAT, or write a file.
 */
static void filesystem_bootLoggingSetDetail(const char code[8])
{
#if DEV_MODE_LOGGING
    if (!fs_boot_logging_active || !fs_boot_logging_armed ||
        fs_boot_logging_recovery || fs_boot_logging_timed_out || !code) {
        return;
    }
    memcpy(fs_boot_logging_code, code, sizeof(fs_boot_logging_code));
#else
    (void)code;
#endif
}

uint8_t filesystem_bootLoggingTimedOut(void)
{
    /*
     * Observe the primary timeout without advancing filesystem state.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Input is the logger latch; output is zero/nonzero. Why: main-owned
     * Preset waits do not use facade status and need an explicit exit test.
     * Affiliates: the boot timeout cleanup label in main.c.
     */
#if DEV_MODE_LOGGING
    return fs_boot_logging_timed_out;
#else
    return 0u;
#endif
}

const uint8_t *filesystem_bootLoggingCode(void)
{
    /*
     * Return the retained fixed-width code for logging only.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Output remains valid through dirty teardown and recovery because it is
     * static logger state, not generic operation scratch. The caller must read
     * exactly eight bytes. Affiliates: the internal bootlog writer.
     */
#if DEV_MODE_LOGGING
    return fs_boot_logging_code;
#else
    static const uint8_t disabled_code[8] = { 0u };
    return disabled_code;
#endif
}

void filesystem_bootLoggingEnd(void)
{
    /*
     * Close the logging window before audio/runtime filesystem service.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Inputs: any normal, no-card, timed-out, or recovery-complete boot path.
     * Outputs: all watchdog/recovery flags are disabled while the last code is
     * left untouched. Why: Menu, Preset, and autosave operations must retain
     * their ordinary non-destructive runtime semantics. Affiliates: main.c's
     * stage-14 boundary and filesystem_start().
     */
#if DEV_MODE_LOGGING
    fs_boot_logging_active = 0u;
    fs_boot_logging_armed = 0u;
    fs_boot_logging_timed_out = 0u;
    fs_boot_logging_recovery = 0u;
    fs_boot_logging_recovery_failed = 0u;
#endif
}

static const char *filesystem_errorPrefix(fs_internal_op_t op)
{
    switch (op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:          return "Flush";
    case FS_INTERNAL_OP_CREATE_BOOT_INDEX:     return "HIdx";
    case FS_INTERNAL_OP_CREATE_LIBRARY_INDEX:  return "LIdx";
    case FS_INTERNAL_OP_WRITE_HCNAMES:         return "HNam";
    case FS_INTERNAL_OP_WRITE_BOOT_LOG:        return "BLog";
    case FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN: return "ASv";
    case FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH:  return "AST";
    case FS_INTERNAL_OP_LOAD_HCNAMES_INSTRUMENT:return "HNrL";
    case FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT:return "HNrU";
    case FS_INTERNAL_OP_LOAD_HCNAMES_KIT:      return "HNkL";
    case FS_INTERNAL_OP_UPDATE_HCNAMES_KIT:    return "HNkU";
    case FS_INTERNAL_OP_LOAD_HCNAMES_SCENE:    return "HNsL";
    case FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE:  return "HNsU";
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
    case FS_INTERNAL_OP_REPAIR_NAMES:          return "NameR";
    case FS_INTERNAL_OP_LOAD_INSTRUMENT:       return "InsL";
    case FS_INTERNAL_OP_SAVE_INSTRUMENT:       return "InsS";
    case FS_INTERNAL_OP_LOAD_NAME:             return "NameL";
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

/*
 * Compare one AsyncFATFS display name against a firmware-owned FAT component.
 *
 * FAT treats ASCII case variants as one namespace entry, while displayName
 * deliberately preserves host-selected case.  The root HCNAMES probe and the
 * autosave no-overwrite scan therefore use this local folded comparison rather
 * than trusting a case-sensitive C string comparison to prove absence.
 */
static uint8_t filesystem_displayNameMatchesCaseInsensitive(
    const char *candidate, const char *target)
{
    if (!candidate || !target)
        return 0u;
    while (*candidate != '\0' && *target != '\0') {
        char left = *candidate++;
        char right = *target++;

        if (left >= 'A' && left <= 'Z')
            left = (char)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z')
            right = (char)(right + ('a' - 'A'));
        if (left != right)
            return 0u;
    }
    return (uint8_t)(*candidate == '\0' && *target == '\0');
}

/*
 * Start the read-only proof required before any first-use HCNAMES creation.
 *
 * Inputs: the caller has received a completed NULL callback from a folded
 * `/.hcnames` read and owns no other open facade handle.  Output: a fresh
 * root-directory scan using the shared handle/finder scratch.  No deadline is
 * armed or extended here: a Bank caller remains inside its original BANKLOAD
 * ten-second operation budget while runtime callers retain normal async error
 * handling.
 */
static void filesystem_hcnamesProbeBegin(void)
{
    op_hcnames_probe_matches = 0u;
    op_hcnames_probe_state = FS_HCNAMES_PROBE_OPEN_ROOT;
}

/*
 * Progress one read-only root scan and classify HCNAMES without creating it.
 *
 * A NULL direct read cannot distinguish a genuinely absent file from a failed
 * lookup, damaged entry chain, wrong object kind, or device failure.  This
 * helper enumerates every root object, counts folded `.hcnames` display-name
 * matches through the ordinary callback/close path, and returns ABSENT only
 * after that enumeration closes successfully.  One match is retried by the
 * caller's existing read path; two or more matches and every scan/close error
 * are terminal FS_STATUS_ERROR cases, preserving all card evidence.
 */
static fs_hcnames_probe_result_t filesystem_hcnamesProbe_tick(void)
{
    afatfsOperationStatus_e scan_status;

    switch (op_hcnames_probe_state) {
    case FS_HCNAMES_PROBE_OPEN_ROOT:
        if (!afatfs_chdir(NULL))
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        op_hcnames_probe_state = FS_HCNAMES_PROBE_WAIT_ROOT;
        return FS_HCNAMES_PROBE_IN_PROGRESS;

    case FS_HCNAMES_PROBE_WAIT_ROOT:
        if (!op_file_ready)
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        if (!op_file) {
            op_hcnames_probe_state = FS_HCNAMES_PROBE_IDLE;
            return FS_HCNAMES_PROBE_ERROR;
        }
        afatfs_findFirstObject(op_file, &op_object_finder);
        op_hcnames_probe_state = FS_HCNAMES_PROBE_SCAN;
        return FS_HCNAMES_PROBE_IN_PROGRESS;

    case FS_HCNAMES_PROBE_SCAN:
        scan_status = afatfs_findNextObject(op_file, &op_object_finder,
                                            &op_object);
        if (scan_status == AFATFS_OPERATION_IN_PROGRESS)
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        if (scan_status == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_file, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_hcnames_probe_state = FS_HCNAMES_PROBE_CLOSE_ROOT;
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_file, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_hcnames_probe_state = FS_HCNAMES_PROBE_CLOSE_ROOT;
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        }
        if (filesystem_displayNameMatchesCaseInsensitive(
                op_object.id.displayName, FS_RESIDENT_NAMES_FILENAME) &&
            op_hcnames_probe_matches < 2u) {
            /* Saturation distinguishes 0, one, and duplicate-or-more without
             * retaining object identities or introducing a second name cache. */
            op_hcnames_probe_matches++;
        }
        return FS_HCNAMES_PROBE_IN_PROGRESS;

    case FS_HCNAMES_PROBE_CLOSE_ROOT:
        op_close_done = false;
        if (!afatfs_fclose(op_file, on_file_closed))
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        op_hcnames_probe_state = FS_HCNAMES_PROBE_WAIT_CLOSE;
        return FS_HCNAMES_PROBE_IN_PROGRESS;

    case FS_HCNAMES_PROBE_WAIT_CLOSE:
        if (!op_close_done)
            return FS_HCNAMES_PROBE_IN_PROGRESS;
        op_file = NULL;
        op_hcnames_probe_state = FS_HCNAMES_PROBE_IDLE;
        if (op_close_status != FS_STATUS_DONE)
            return FS_HCNAMES_PROBE_ERROR;
        if (op_hcnames_probe_matches == 0u)
            return FS_HCNAMES_PROBE_ABSENT;
        if (op_hcnames_probe_matches == 1u)
            return FS_HCNAMES_PROBE_PRESENT;
        return FS_HCNAMES_PROBE_DUPLICATE;

    case FS_HCNAMES_PROBE_IDLE:
    default:
        return FS_HCNAMES_PROBE_ERROR;
    }
}

/*
 * Expose only the probe's wait-capable scan boundary to the Bank boot logger.
 *
 * The Bank caller records `BKHCSCAN` after root open has completed so a later
 * timeout distinguishes directory enumeration from the preceding HCNAMES
 * read.  This query has no I/O or clock side effect and cannot rearm BANKLOAD.
 */
static uint8_t filesystem_hcnamesProbeIsScanning(void)
{
    return (uint8_t)(op_hcnames_probe_state == FS_HCNAMES_PROBE_SCAN ||
                     op_hcnames_probe_state == FS_HCNAMES_PROBE_CLOSE_ROOT ||
                     op_hcnames_probe_state == FS_HCNAMES_PROBE_WAIT_CLOSE);
}

static void filesystem_complete(fs_status_t final_status)
{
    /*
     * Acknowledge a complete settings snapshot only after the shared flush.
     *
     * Inputs: terminal status, the retained settings-write marker, and the
     * revision captured when its text stream opened. Output: dirty clears only
     * when the durable file contains the latest revision; an edit during
     * streaming or flush remains queued. Why: current_op becomes the shared
     * flush-finisher before completion, while the retained marker preserves
     * SAVE_GLOBALS ownership through that final boundary. Affiliates: explicit
     * and autonomous settings saves plus filesystem_markSettingsDirty().
     */
    if (op_settings_write_active &&
        final_status == FS_STATUS_DONE &&
        op_settings_change_revision == fs_settings_change_revision) {
        fs_settings_dirty = 0u;
    }
    op_settings_write_active = 0u;
    if (final_status == FS_STATUS_ERROR && fs_error_code[0] == '\0')
        filesystem_makeAutoErrorCode(current_op, op_phase);
    status = final_status;
    current_op = FS_INTERNAL_OP_NONE;
    filesystem_bootLoggingOperationDone();
    if (completion_callback) {
        fs_completion_cb_t cb = completion_callback;
        completion_callback = NULL;
        cb();
    }
}

static void filesystem_finish(fs_status_t final_status)
{
    uint8_t flush_before_complete = (uint8_t)(final_status == FS_STATUS_DONE);

    if (!flush_before_complete && op_library_index_rebuild_pending) {
        /*
         * Cancel a Save-owned rebuild when its preceding transaction fails.
         *
         * Inputs: Scene Save declares its namespace rebuild before entering the
         * shared HCNAMES writer, while Kit/Bank Save declare theirs only at
         * their successful terminal phases. Output: an HCNAMES or other
         * pre-rebuild error clears the pending kind before publishing the
         * original failure, so a later unrelated successful operation cannot
         * inherit and run a stale scan/index rewrite. Once a rebuild has
         * actually started, pending is already zero and its parked callback
         * retains ownership of rebuild-error completion.
         */
        op_library_index_rebuild_pending = 0u;
        op_library_index_rebuild_kind = FS_NAME_CACHE_NONE;
    }

    if (flush_before_complete) {
        fs_internal_op_t finished_op = current_op;

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
        /*
         * DEV_MODE_LOGGING writes operation codes to file for use in debugging.
         * It must never print anything to the screen or otherwise delay
         * operations unnecessarily since logging may be used to assess timing
         * failures in other modules that might otherwise be obscured by screen
         * write delays.
         */
        /*
         * Preserve Bank-load provenance across the mandatory final flush.
         *
         * Inputs: the operation which completed logical work. Output: a fresh
         * BKFLUSH deadline only for Load Bank; every other owner retains
         * FSFLUSH. Why: this selects a diagnostic label only, so a stalled
         * sync still times out and an error never enters this DONE-only path.
         */
        filesystem_bootLoggingArm(
            finished_op == FS_INTERNAL_OP_LOAD_BANK ? "BKFLUSH " : "FSFLUSH ");
        current_op = FS_INTERNAL_OP_FLUSH_FINISH;
        op_phase = 0u;
        return;
    }

    filesystem_complete(final_status);
}

/* Complete the original save after its scan/index rebuild chain.
 *
 * What: publishes the final status and invokes the callback captured when the
 * Kit/Scene save was requested. Why: the save callback must not run between
 * writing the directory and rewriting `.hcindex`, otherwise Menu can dispose
 * the cache or allow another operation while the index still describes the
 * old directory set. Inputs: final chain status. Output: the original Preset
 * completion callback runs exactly once, after the physical folder and index
 * have both passed their FAT flush gates.
 */
static void filesystem_completeLibraryIndexRebuild(fs_status_t final_status)
{
    fs_completion_cb_t cb = op_library_index_rebuild_callback;

    op_library_index_rebuild_callback = NULL;
    op_library_index_rebuild_kind = FS_NAME_CACHE_NONE;
    op_library_index_rebuild_pending = 0u;
    status = final_status;
    current_op = FS_INTERNAL_OP_NONE;
    if (cb)
        cb();
}

/* Start the boot-equivalent physical rescan after a successful save flush.
 *
 * What: replaces the old cache-row-only save update with a real Kit/Scene/Bank
 * directory scan. Why: a save can create, rename, or remove a numbered folder;
 * only scanning the parent directory observes the complete resulting set.
 * Inputs: op_library_index_rebuild_kind and the parked original callback.
 * Output: the shared cache is rebuilt and the scan callback starts the full
 * 1,000-row `.hcindex` writer.
 */
static void filesystem_startLibraryIndexRebuild(void)
{
    fs_name_cache_kind_t kind = op_library_index_rebuild_kind;
    bool started;

    op_library_index_rebuild_pending = 0u;
    op_library_index_rebuild_callback = completion_callback;
    completion_callback = NULL;
    op_library_index_kind = kind;
    status = FS_STATUS_IDLE;
    current_op = FS_INTERNAL_OP_NONE;

    started = (kind == FS_NAME_CACHE_KIT)
        ? filesystem_requestScanKits(filesystem_libraryIndexRebuildScanComplete)
        : (kind == FS_NAME_CACHE_SCENE)
            ? filesystem_requestScanScenes(filesystem_libraryIndexRebuildScanComplete)
            : (kind == FS_NAME_CACHE_BANK)
                ? filesystem_requestScanBanks(filesystem_libraryIndexRebuildScanComplete)
                : false;
    if (!started) {
        filesystem_makeNamedErrorCode("Idx", 0u);
        filesystem_completeLibraryIndexRebuild(FS_STATUS_ERROR);
    }
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

    if (op_library_index_rebuild_pending) {
        filesystem_startLibraryIndexRebuild();
        return;
    }

    filesystem_complete(op_flush_final_status);
}

/* -----------------------------------------------------------------------
** INSTRUMENT INDEX CREATE state machine
**
** Creates/truncates one `.hcindex` file per registry-defined Instrument folder
** and writes the current typed name cache as newline-separated display stems.
** Boot requests run this state machine for every registry row; a completed
** Instrument Save selects only its own registry row for a bounded refresh.
** All directory/file work remains foreground-pumped and asynchronous after
** the initial request, with filesystem_finish() providing the final flush gate.
** Parent directories are opened read-only during boot indexing. Missing
** Instrument roots or type folders are treated as empty namespaces; save paths
** are responsible for creating directories when the user actually writes data.
**
** Phases: 0..3 enter /Instrument/, 4 selects a registry row, 5..8 enter its
** subdirectory, 9..12 write one `.hcindex`, and 14 returns to root and
** completes. Phase 13 is intentionally unused so the close/parent transition
** remains distinct from the line-reader phase numbers used by index loading.
** ----------------------------------------------------------------------- */
static void filesystem_createBootIndex_tick(void)
{
    const instrument_registry_entry_t *entry;

    switch (op_phase) {
    case 0: /* RETURN TO ROOT + OPEN Instrument/ */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1: /* WAIT Instrument/ */
        if (!op_file_ready)
            return;
        if (op_file == NULL) {
            /*
             * Do not create Instrument/ during boot index generation.
             *
             * A missing root means there are no Instrument indexes to refresh.
             * Creating folders during a cache rebuild can duplicate damaged
             * host-created LFNs; Instrument Save owns the directory-creation
             * path because it has a concrete file to write.
             */
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 2u;
        return;

    case 2: /* CHDIR Instrument/ */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 3u;
        return;

    case 3: /* CLOSE Instrument/ handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4: /* WAIT CLOSE Instrument/ */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        if (op_instrument_index_type == INSTRUMENT_TYPE_UNKNOWN) {
            op_instrument_scan_registry_index = 0u;
        } else {
            uint8_t i;
            op_instrument_scan_registry_index =
                instrumentManager_registryCount();
            for (i = 0u; i < instrumentManager_registryCount(); i++) {
                entry = instrumentManager_registryEntryAt(i);
                if (entry && entry->type == op_instrument_index_type) {
                    op_instrument_scan_registry_index = i;
                    break;
                }
            }
            if (op_instrument_scan_registry_index >=
                instrumentManager_registryCount()) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
        }
        op_phase = 5u;
        return;

    case 5: /* OPEN registry-defined Instrument subdirectory */
        entry = instrumentManager_registryEntryAt(
            op_instrument_scan_registry_index);
        if (!entry || !entry->storage_directory) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(entry->storage_directory,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 6u;
        return;

    case 6: /* WAIT subdirectory */
        if (!op_file_ready)
            return;
        if (op_file == NULL) {
            /*
             * Do not create type folders while writing `.hcindex`.
             *
             * Boot indexing is a publication pass over existing physical
             * Instrument directories. If `Drum/` or `Snare/` is absent, the
             * matching typed cache is empty and no index file is needed. This
             * also prevents duplicate type directories on cards with damaged
             * or host-created LFN entries.
             */
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 7u;
        return;

    case 7: /* CHDIR subdirectory */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 8u;
        return;

    case 8: /* CLOSE subdirectory handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 9u;
        return;

    case 9: /* WAIT CLOSE subdirectory + OPEN .hcindex */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        op_item_offset = 0u;
        op_bytes_done = 0;
        if (!afatfs_fopen_lfn(".hcindex",
                              "w",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 10u;
        return;

    case 10: /* WAIT .hcindex open */
        if (!op_file_ready)
            return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 11u;
        return;

    case 11: /* WRITE typed cache strings */
        entry = instrumentManager_registryEntryAt(
            op_instrument_scan_registry_index);
        if (!entry) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (op_item_offset <
            filesystem_cachedInstrumentCount(entry->type)) {
            if (op_bytes_done == 0) {
                strcpy(op_line_buf,
                       filesystem_cachedInstrumentName(entry->type,
                                                       op_item_offset));
                strcat(op_line_buf, "\n");
                op_line_len = (uint8_t)strlen(op_line_buf);
            }
            
            uint32_t written = afatfs_fwrite(
                op_file,
                (const uint8_t *)op_line_buf + op_bytes_done,
                op_line_len - op_bytes_done);
            
            op_bytes_done += written;
            
            if (op_bytes_done == op_line_len) {
                op_item_offset++;
                op_bytes_done = 0;
            }
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 12u;
        return;

    case 12: /* WAIT .hcindex close + RETURN to /Instrument/ */
        if (!op_close_done)
            return;
        op_file = NULL;
        {
            afatfsOperationStatus_e st = afatfs_chdirParent();
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (st == AFATFS_OPERATION_FAILURE) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
        }
        if (op_instrument_index_type != INSTRUMENT_TYPE_UNKNOWN ||
            (uint8_t)(op_instrument_scan_registry_index + 1u) >=
                instrumentManager_registryCount()) {
            op_phase = 14u;
        } else {
            op_instrument_scan_registry_index++;
            op_phase = 5u;
        }
        return;

    case 14: /* RETURN ROOT + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* Load one registry-defined Instrument `.hcindex` into the typed name cache.
 *
 * Inputs: op_instrument_index_type captured by
 * filesystem_requestLoadInstrumentIndex(). Outputs: only that type's general
 * name/count cache is replaced; all Scene/DSP state and other Instrument lists
 * remain untouched. Clients: Menu requests this when entering either nested
 * Instrument Load or Instrument Save for a selected type.
 */
static void filesystem_loadInstrumentIndex_tick(void)
{
    const char *directory = instrumentManager_storageDirectory(
        op_instrument_index_type);

    switch (op_phase) {
    case 0: /* CHDIR ROOT */
        if (!afatfs_chdir(NULL)) return;
        op_phase = 1u;
        return;

    case 1: /* OPEN Instrument/ */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 2u;
        return;

    case 2: /* WAIT Instrument/ */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3u;
        return;

    case 3: /* CHDIR Instrument/ */
        if (!afatfs_chdir(op_kit_root_dir)) return;
        op_phase = 4u;
        return;

    case 4: /* CLOSE Instrument/ handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5u;
        return;

    case 5: /* WAIT CLOSE Instrument/ */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 6u;
        return;

    case 6: /* OPEN the registry-defined type directory */
        op_file_ready = false;
        op_file = NULL;
        if (!directory ||
            !afatfs_opendir_lfn(directory,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 7u;
        return;

    case 7: /* WAIT type directory */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 8u;
        return;

    case 8: /* CHDIR type directory */
        if (!afatfs_chdir(op_kit_root_dir)) return;
        op_phase = 9u;
        return;

    case 9: /* CLOSE type-directory handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 10u;
        return;

    case 10: /* WAIT CLOSE type-directory handle */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 11u;
        return;

    case 11: /* OPEN .hcindex */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(".hcindex", "r", AFATFS_MATCH_CASE_SENSITIVE, NULL, on_file_opened))
            return;
        op_phase = 12u;
        return;

    case 12: /* WAIT OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        fs_list_cache_count = 0u;
        op_line_len = 0u;
        op_phase = 13u;
        return;

    case 13: /* READ LINES */
    {
        uint8_t line_ready = 0, eof = 0;
        storage_status_t st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len, sizeof(op_line_buf), &line_ready, &eof);

        if (st != STORAGE_STATUS_OK && st != STORAGE_STATUS_WAIT) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 14u;
            return;
        }

        if (line_ready) {
            uint8_t actual_len = (uint8_t)strlen(op_line_buf);
            uint8_t field_len = 0u;
            while (field_len < actual_len && op_line_buf[field_len] != ',')
                field_len++;
            if (field_len > 0u &&
                op_item_offset < FS_LIBRARY_NAME_CACHE_MAX) {
                op_line_buf[field_len] = '\0';
                storage_copyDisplayName(
                    fs_list_cache_name[op_item_offset],
                    op_line_buf);
                fs_list_cache_name[op_item_offset]
                    [STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';

                fs_list_cache_count++;
                op_item_offset++;
            }
            op_line_len = 0u;
        }

        if (eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 14u;
        }
        return;
    }

    case 14: /* WAIT CLOSE */
        if (!op_close_done) return;
        op_file = NULL;
        op_phase = 15u;
        return;

    case 15: /* RETURN ROOT */
        if (!afatfs_chdir(NULL)) return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/*
 * Create one slot-ordered root Kit, Scene, or Bank `.hcindex`.
 *
 * What: writes exactly one newline-delimited row for every 000..999 slot;
 * existing rows contain only the eight-character name and empty slots contain
 * an empty row. Why: the row number is the library slot, so alphabetic sorting
 * would destroy the identity needed to regenerate `NNN Name` later. The
 * operation uses the same single name cache that the menu reads, then returns
 * to root and passes through filesystem_finish() for the normal FAT flush.
 * Inputs: op_library_index_kind and its already-populated shared cache.
 * Clients: boot index refresh and successful Kit/Scene/Bank saves.
 * The root directory is opened before any mkdir fallback so index generation
 * cannot duplicate a host-created LFN root component.
 */
static void filesystem_createLibraryIndex_tick(void)
{
    const char *root = (op_library_index_kind == FS_NAME_CACHE_KIT)
        ? STORAGE_ROOT_KIT
        : (op_library_index_kind == FS_NAME_CACHE_SCENE)
            ? STORAGE_ROOT_SCENE
            : (op_library_index_kind == FS_NAME_CACHE_BANK)
                ? STORAGE_ROOT_BANK
            : NULL;

    switch (op_phase) {
    case 0: /* RETURN ROOT + OPEN LIBRARY DIRECTORY */
        if (!root || !afatfs_chdir(NULL)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        op_create_dir_retry = 0u;
        if (!afatfs_opendir_lfn(root,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1: /* WAIT ROOT DIRECTORY */
        if (!op_file_ready)
            return;
        if (!op_file) {
            if (op_create_dir_retry != 0u) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * Create the root library only after strict open missed.
             *
             * The index writer should make missing Kit/Scene/Bank roots for a
             * fresh card, but it must not call a create-capable LFN path before
             * proving an existing visible root is absent.
             */
            op_create_dir_retry = 1u;
            op_file_ready = false;
            if (!afatfs_mkdir_lfn(root,
                                  AFATFS_MATCH_CASE_INSENSITIVE,
                                  NULL,
                                  on_file_opened))
                return;
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 2u;
        return;

    case 2: /* ENTER ROOT DIRECTORY */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 3u;
        return;

    case 3: /* CLOSE ROOT DIRECTORY HANDLE */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4: /* WAIT ROOT CLOSE + OPEN INDEX */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        op_item_offset = 0u;
        op_bytes_done = 0u;
        if (!afatfs_fopen_lfn(".hcindex",
                              "w",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 5u;
        return;

    case 5: /* WAIT INDEX OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 6u;
        return;

    case 6: /* WRITE SLOT-ORDERED ROWS */
        if (op_item_offset < fs_list_cache_count) {
            if (op_bytes_done == 0u) {
                const char *name = fs_list_cache_name[op_item_offset];
                strcpy(op_line_buf, name);
                strcat(op_line_buf, "\n");
                op_line_len = (uint8_t)strlen(op_line_buf);
            }
            op_bytes_done += afatfs_fwrite(
                op_file,
                (const uint8_t *)op_line_buf + op_bytes_done,
                op_line_len - op_bytes_done);
            if (op_bytes_done >= op_line_len) {
                op_item_offset++;
                op_bytes_done = 0u;
            }
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 7u;
        return;

    case 7: /* WAIT INDEX CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_writeResidentNames_tick(void)
{
    fs_hcnames_probe_result_t probe_result;

    /*
     * Write root `/.hcnames` using the `.hcindex` writer pattern.
     *
     * Inputs: resident BankData/SceneData after the initial load/fallback
     * chain. Output: one root file whose row order is the resident-name
     * register contract. Its first phase now proves whether the singleton is
     * absent before allowing a create-capable writer: a completed NULL open is
     * not sufficient evidence. The proof borrows only operation-local finder
     * scratch; the writer still serializes the existing SRAM fields and
     * finishes through filesystem_finish() so asyncfatfs flushes before boot
     * continues.
     */
    switch (op_phase) {
    case 0: /* PROVE ROOT HCNAMES ABSENT/UNIQUE, THEN OPEN FOR WRITE */
        if (op_hcnames_probe_state == FS_HCNAMES_PROBE_IDLE)
            filesystem_hcnamesProbeBegin();
        probe_result = filesystem_hcnamesProbe_tick();
        if (probe_result == FS_HCNAMES_PROBE_IN_PROGRESS)
            return;
        if (probe_result == FS_HCNAMES_PROBE_ERROR) {
            /* A failed proof never falls through to a create-capable open;
             * preserve the storage failure for the caller's normal error UI. */
            filesystem_makeNamedErrorCode("HNPrb", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (probe_result == FS_HCNAMES_PROBE_DUPLICATE) {
            /* Multiple folded matches are evidence for manual recovery, not a
             * license to choose, overwrite, rename, or merge either entry. */
            filesystem_bootLoggingSetDetail("HNDUP   ");
            filesystem_makeNamedErrorCode("HNDup", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /* ABSENT authorizes initial creation; PRESENT authorizes this legacy
         * refresh writer to reopen the one proven existing register. */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        op_item_offset = 0u;
        op_bytes_done = 0u;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "w",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1: /* WAIT .hcnames OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2u;
        return;

    case 2: /* WRITE FIXED-ORDER RESIDENT NAME ROWS */
        if (op_item_offset < FS_RESIDENT_NAMES_ROW_COUNT) {
            if (op_bytes_done == 0u) {
                op_line_len = filesystem_nextResidentNameLine(
                    op_line_buf, sizeof(op_line_buf), op_item_offset);
                if (op_line_len == 0u) {
                    filesystem_finish(FS_STATUS_ERROR);
                    return;
                }
            }
            op_bytes_done += afatfs_fwrite(
                op_file,
                (const uint8_t *)op_line_buf + op_bytes_done,
                op_line_len - op_bytes_done);
            if (op_bytes_done >= op_line_len) {
                op_item_offset++;
                op_bytes_done = 0u;
            }
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 3u;
        return;

    case 3: /* WAIT CLOSE + FINISH THROUGH FLUSH GATE */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/*
 * Stream the captured operation code to root `/bootlog.bin`.
 *
 * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
 * must never print anything to the screen or otherwise delay operations
 * unnecessarily since logging may be used to assess timing failures in other
 * modules that might otherwise be obscured by screen write delays.
 *
 * What: returns to root, opens the file with replacement semantics, writes
 * exactly eight bytes with partial-write tracking, closes it, and enters the
 * ordinary sync finish gate. Why: the diagnostic recovery must obey the same
 * single-owner asyncfatfs rules as every other facade write and must not claim
 * durability after only updating a cache. Input is fs_boot_logging_code, which
 * survives dirty recovery; output is an eight-byte file with no NUL/newline.
 * Affiliates: filesystem_writeBootFailureLogBlocking(), on_file_opened(),
 * on_file_closed(), filesystem_finish(), and the tick dispatcher.
 */
static void filesystem_writeBootLog_tick(void)
{
    switch (op_phase) {
    case 0u: /* RETURN ROOT + OPEN/TRUNCATE bootlog.bin */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        op_bytes_done = 0u;
        if (!afatfs_fopen_lfn("bootlog.bin",
                              "w",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 1u;
        return;

    case 1u: /* WAIT OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2u;
        return;

    case 2u: /* WRITE EXACTLY EIGHT BYTES + QUEUE CLOSE */
#if DEV_MODE_LOGGING
        if (op_bytes_done < 8u) {
            uint32_t n = afatfs_fwrite(
                op_file,
                fs_boot_logging_code + op_bytes_done,
                8u - op_bytes_done);
            op_bytes_done += n;
            if (n == 0u && afatfs_isFull()) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            return;
        }
#else
        filesystem_finish(FS_STATUS_ERROR);
        return;
#endif
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 3u;
        return;

    case 3u: /* WAIT CLOSE + SYNC */
        if (!op_close_done)
            return;
        op_file = NULL;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/*
 * Serialize the pending AutosaveTrace prefix into the shared 512-byte buffer.
 *
 * Inputs: a pending-count snapshot capped by the 64-record ring. Output:
 * `byte_count` receives its exact eight-byte-record length and staging_buf
 * contains records in oldest-first order. Why: trace owns no filesystem
 * buffer, while exactly 64 * 8 bytes fits the existing one-operation staging
 * buffer without introducing another retained allocation. Affiliate:
 * filesystem_autosaveTraceFlush_tick().
 */
static void filesystem_autosaveTraceSerialize(uint16_t record_count,
                                              uint16_t *byte_count)
{
    uint16_t i;

    for (i = 0u; i < record_count; i++) {
        (void)autosaveTrace_peekRecord(
            i, staging_buf + (i * AUTOSAVE_TRACE_RECORD_BYTES));
    }
    *byte_count = (uint16_t)(record_count * AUTOSAVE_TRACE_RECORD_BYTES);
}

/*
 * Append one bounded RAM trace batch to root `asavetrc.bin`.
 *
 * Inputs: AutosaveTrace's pending records. Output: an append-mode file write
 * that advances the trace flush cursor only after file close and the shared
 * filesystem sync gate. Why: acknowledging before sync could silently lose
 * the only evidence of an autosave failure during a power cut. This state
 * machine is never scheduled in DEV_MODE_LOGGING 0; its explicit disabled
 * branch also guarantees that an accidental direct start cannot write a trace
 * file in a production build. Affiliates: AutosaveTrace.c, the trace scheduler,
 * filesystem_finish(), and on_file_opened()/on_file_closed().
 */
static void filesystem_autosaveTraceFlush_tick(void)
{
#if DEV_MODE_LOGGING
    switch (op_phase) {
    case 0u: /* RETURN ROOT + SNAPSHOT/OPEN APPEND TRACE FILE */
        if (!afatfs_chdir(NULL))
            return;
        op_stream_index = autosaveTrace_pendingCount();
        if (op_stream_index > AUTOSAVE_TRACE_RECORD_COUNT)
            op_stream_index = AUTOSAVE_TRACE_RECORD_COUNT;
        if (op_stream_index == 0u) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        filesystem_autosaveTraceSerialize((uint16_t)op_stream_index,
                                          &op_write_line_len);
        op_file_ready = false;
        op_file = NULL;
        op_bytes_done = 0u;
        if (!afatfs_fopen_lfn(AUTOSAVE_TRACE_FILENAME, "a",
                              AFATFS_MATCH_CASE_INSENSITIVE, NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 1u;
        return;

    case 1u: /* WAIT FOR APPEND FILE OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 2u;
        return;

    case 2u: /* STREAM THE SNAPSHOT, RETRYING ASYNC BACK-PRESSURE */
    {
        uint32_t written;

        if (op_bytes_done >= op_write_line_len) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 3u;
            return;
        }
        written = afatfs_fwrite(op_file, staging_buf + op_bytes_done,
                                op_write_line_len - op_bytes_done);
        op_bytes_done += written;
        if (written == 0u && afatfs_isFull()) {
            /*
             * Full media is a real error, but the append handle still belongs
             * to this operation. Close it before publishing ERROR so a later
             * foreground load/save cannot inherit an orphaned AsyncFATFS owner
             * or mistake this diagnostic failure for successful trace loss.
             */
            op_close_status = FS_STATUS_ERROR;
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 4u;
        }
        return;
    }

    case 3u: /* WAIT CLOSE + DURABLE SYNC BEFORE ACKNOWLEDGING THE RING */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_sync())
            return;
        /*
         * op_stream_index is the phase-0 record-count snapshot. Advancing
         * only here preserves every still-pending trace record across failed
         * opens, partial writes, closes, and syncs; ring overflow is exposed
         * separately by autosaveTrace_droppedCount(), never hidden.
         */
        autosaveTrace_advanceFlushCursor((uint16_t)op_stream_index);
        filesystem_finish(FS_STATUS_DONE);
        return;

    case 4u: /* WAIT ERROR-HANDLE CLOSE BEFORE PUBLISHING TRACE FAILURE */
        if (!op_close_done)
            return;
        op_file = NULL;
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
#else
    filesystem_finish(FS_STATUS_DONE);
#endif
}

static uint16_t filesystem_residentInstrumentRow(uint8_t scene_index,
                                                 uint8_t instrument_slot)
{
    /*
     * Convert one resident Instrument coordinate into its `.hcnames` row.
     *
     * Inputs: zero-based resident Scene and six-voice Kit slot. Output: the
     * zero-based text row, or FS_RESIDENT_NAMES_ROW_COUNT as an invalid
     * sentinel. Keeping this arithmetic in one helper prevents Menu entry,
     * post-Load refresh, and post-Save refresh from drifting to different rows.
     */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS ||
        instrument_slot >= STORAGE_KIT_SLOT_COUNT) {
        return FS_RESIDENT_NAMES_ROW_COUNT;
    }
    return (uint16_t)(FS_RESIDENT_NAMES_INSTRUMENT_BASE +
                      ((uint16_t)scene_index * STORAGE_KIT_SLOT_COUNT) +
                      instrument_slot);
}

static uint16_t filesystem_residentKitRow(uint8_t scene_index)
{
    /*
     * Convert one resident Scene coordinate into its `.hcnames` Kit row.
     *
     * Input: a zero-based resident Scene. Output: the matching row in the
     * sixteen-row Kit block, or the common row-count sentinel when invalid.
     * Keeping this beside the Instrument-row helper makes Kit entry and the
     * seven-row post-action update share the exact same fixed-row contract.
     */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return FS_RESIDENT_NAMES_ROW_COUNT;
    return (uint16_t)(FS_RESIDENT_NAMES_KIT_BASE + scene_index);
}

static uint16_t filesystem_residentSceneRow(uint8_t scene_index)
{
    /*
     * Convert one resident Scene coordinate into its root HCNAMES row.
     *
     * Input: zero-based resident Scene index. Output: fixed row 1..16, or the
     * common row-count sentinel for an invalid coordinate. Scene identity is
     * intentionally stored only in this register, not mirrored in scene_t;
     * keeping the mapping beside the Kit/Instrument helpers prevents Bank
     * masked-load code from using an accidental Kit-row offset.
     *
     * Affiliates: Scene menu single-row borrowing, root Scene Load/Save name
     * publication, and Bank Load's selected-row-only overlay.
     */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return FS_RESIDENT_NAMES_ROW_COUNT;
    return (uint16_t)(1u + scene_index);
}

static const char *filesystem_cachedResidentName(uint16_t row)
{
    /*
     * Borrow one HCNAMES cell while the general cache is in register mode.
     *
     * Inputs: fixed resident row. Output: a NUL-terminated eight-cell cache
     * value, or eight spaces when the row/cache domain is unavailable. Bank
     * Save uses this to write Scene and Kit directory display components from
     * the card authority, rather than from a duplicate SceneData Scene name.
     * The returned pointer is cache-owned and only valid until the next cache
     * transition. Affiliates: filesystem_prepareBankSceneSaveSource().
     */
    if (fs_list_cache_kind != FS_NAME_CACHE_HCNAMES ||
        row >= FS_RESIDENT_NAMES_ROW_COUNT)
        return "        ";
    return fs_list_cache_name[row];
}

static void filesystem_prepareResidentNamesCache(void)
{
    /*
     * Borrow the existing generalized cache for the complete root register.
     *
     * What: clears the previous `.hcindex` view, tags the same physical
     * fs_list_cache_name allocation as HCNAMES, and exposes exactly 129 rows.
     * Why: variable-length lines prevent safe in-place growth of one row, so a
     * targeted update must preserve the other rows while rewriting the text
     * file. Reusing the 9,000-byte cache adds no SRAM allocation; Menu copies
     * its selected eight cells before the cache is replaced by `.hcindex`.
     */
    filesystem_clearNameCacheStorage();
    fs_list_cache_kind = FS_NAME_CACHE_HCNAMES;
    fs_list_cache_count = FS_RESIDENT_NAMES_ROW_COUNT;
}

static void filesystem_cacheResidentName(uint16_t row, const char *line)
{
    /*
     * Normalize one parsed `.hcnames` line into the shared eight-cell format.
     *
     * Inputs: physical line number and its newline-free text. Output: only the
     * matching cache row is changed, printable text is space padded, and blank
     * lines remain blank occupancy. The ninth byte stays NUL so Menu may copy
     * or inspect the cell before the next cache-domain transition.
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        fs_list_cache_kind != FS_NAME_CACHE_HCNAMES) {
        return;
    }
    memset(fs_list_cache_name[row], 0,
           sizeof(fs_list_cache_name[row]));
    if (line && line[0] != '\0')
        storage_copyDisplayName(fs_list_cache_name[row], line);
    fs_list_cache_name[row][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
}

static void filesystem_cacheCurrentResidentInstrumentNames(void)
{
    uint8_t scene_index;

    /*
     * Replace only the Instrument rows affected by the completed action.
     *
     * Inputs: op_kit_load_scene_mask and op_slot captured by the public update
     * request, plus the now-committed resident names. Output: matching rows in
     * the borrowed HCNAMES cache are refreshed; Bank, Scene, Kit, and every
     * unrelated Instrument row remain byte-for-byte logical copies of the file
     * that was read. A multi-Scene Instrument Load updates each destination
     * because those are separate resident slots changed by the same action.
     */
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t row;

        if ((op_kit_load_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u) {
            continue;
        }
        row = filesystem_residentInstrumentRow(scene_index,
                                               (uint8_t)op_slot);
        if (row >= FS_RESIDENT_NAMES_ROW_COUNT)
            continue;
        /*
         * The committed Instrument payload intentionally has no source name.
         * Use the one active identity row selected by the Instrument menu for
         * every destination in this normal multi-Scene operation.
         */
        filesystem_cacheResidentName(
            row,
            filesystem_identityName((uint8_t)(
                FS_IDENTITY_INSTRUMENT_ROW_0 + op_slot)));
    }
}

static void filesystem_cacheCurrentResidentKitNames(void)
{
    uint8_t scene_index;

    /*
     * Replace the complete resident-name block changed by a full Kit action.
     *
     * Inputs: op_kit_load_scene_mask captured by the public update request and
     * committed SceneData names after normal Kit Load or Kit Save. Output: for
     * every selected Scene, only its one Kit row and six Instrument rows in the
     * borrowed HCNAMES cache are replaced. These seven request-owned rows use
     * the successful action itself as their presence predicate: Kit Save may
     * originate from a valid resident Scene whose Bank present bit is clear,
     * and applying the general boot serializer's present-mask gate here would
     * incorrectly turn its newly saved Kit name into a blank line. Bank, Scene,
     * and all unselected Kit blocks remain copies of the file that was read.
     * The shared line buffer formats each row, so this adds no Kit-sized scratch
     * or persistent SRAM.
     */
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t row;
        uint8_t slot;
        if ((op_kit_load_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u) {
            continue;
        }

        row = filesystem_residentKitRow(scene_index);
        if (row < FS_RESIDENT_NAMES_ROW_COUNT) {
            filesystem_cacheResidentName(row,
                                         filesystem_identityName(
                                             FS_IDENTITY_KIT_ROW));
        }

        for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
            row = filesystem_residentInstrumentRow(scene_index, slot);
            if (row >= FS_RESIDENT_NAMES_ROW_COUNT)
                continue;
            filesystem_cacheResidentName(
                row,
                filesystem_identityName((uint8_t)(
                    FS_IDENTITY_INSTRUMENT_ROW_0 + slot)));
        }
    }
}

static void filesystem_cacheCurrentResidentSceneNames(void)
{
    uint8_t scene_index;

    /*
     * Replace only Scene register rows changed by one successful Scene action.
     *
     * Inputs: op_scene_load_scene_mask and op_scene_display_name captured at
     * request/parse time. Output: each selected row receives the same loaded
     * or saved eight-cell name while Bank, Kit, Instrument, and unselected
     * Scene rows remain the values read from `/.hcnames`. A normal root Scene
     * load may target several resident Scenes; none need a retained name field
     * in SceneData. Affiliates: filesystem_residentNames_tick() and Menu's
     * one-name Scene entry scratch.
     */
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t row;

        if ((op_scene_load_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u) {
            continue;
        }
        row = filesystem_residentSceneRow(scene_index);
        if (row < FS_RESIDENT_NAMES_ROW_COUNT)
            filesystem_cacheResidentName(row, op_scene_display_name);
    }
}

static void filesystem_cacheCurrentBankSceneNameBlock(uint8_t scene_index)
{
    uint8_t slot;

    /*
     * Overlay one successfully committed Bank child onto the HCNAMES register.
     *
     * Inputs: the Bank loader's one-bit child cursor, its parsed Scene display
     * name in op_scene_display_name, and the resident Scene just atomically
     * committed by the shared Scene loader. Output: only that Scene row, its
     * Kit row, and its six Instrument rows change in the borrowed cache.
     * Unmasked resident Scenes are deliberately never touched, preserving both
     * their payload/name pairing during every mask-selective Bank Load.
     * Affiliates: filesystem_loadSceneDirectory_tick() commit phase and the
     * final Bank HCNAMES writer.
     */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return;
    filesystem_cacheResidentName(filesystem_residentSceneRow(scene_index),
                                 op_scene_display_name);
    filesystem_cacheResidentName(filesystem_residentKitRow(scene_index),
                                 filesystem_identityName(FS_IDENTITY_KIT_ROW));
    for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
        filesystem_cacheResidentName(
            filesystem_residentInstrumentRow(scene_index, slot),
            filesystem_identityName((uint8_t)(
                FS_IDENTITY_INSTRUMENT_ROW_0 + slot)));
    }
}

static void filesystem_residentNames_tick(void)
{
    const uint8_t update = (uint8_t)(
        current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT ||
        current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_KIT ||
        current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE);
    fs_hcnames_probe_result_t probe_result;

    /*
     * Read `/.hcnames`, optionally replace affected Instrument or Kit rows,
     * and write the variable-length register through the normal flush gate.
     *
     * Load mode is used on Instrument or Kit menu entry: it fills the existing
     * generalized cache and lets Menu copy one selected row before requesting
     * the appropriate `.hcindex`. Update mode first performs that same read,
     * changes only rows identified by the completed action, then truncates and
     * streams all cached rows. A full Kit update replaces one Kit row and its
     * six Instrument rows per selected Scene; a nested Instrument update still
     * replaces only its selected voice row(s). Rewriting is necessary because
     * trimmed row lengths can change; untouched logical rows come from the
     * file, never from resident SRAM. The operation owns no new persistent
     * buffer or handle array.
     */
    switch (op_phase) {
    case 0: /* RETURN ROOT + OPEN EXISTING REGISTER */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 1u;
        return;

    case 1: /* WAIT OPEN + INITIALIZE STREAM */
        if (!op_file_ready)
            return;
        if (!op_file) {
            if (update) {
                /*
                 * A failed read is not yet proof that HCNAMES is missing.
                 *
                 * The AsyncFATFS callback has no reason code, so NULL can mean
                 * a missing entry or an I/O/lookup failure.  Delay both cache
                 * mutation and the write-capable bootstrap until the bounded
                 * root scan proves absence; a present object instead receives
                 * exactly one folded read retry and every duplicate/error is
                 * exposed through FS_STATUS_ERROR without writing the card.
                 */
                filesystem_hcnamesProbeBegin();
                op_phase = 7u;
                return;
            }
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 2u;
        return;

    case 2: /* READ EVERY LOGICAL ROW INTO THE SHARED CACHE */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t st = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (st != STORAGE_STATUS_OK && st != STORAGE_STATUS_WAIT) {
            op_close_status = FS_STATUS_ERROR;
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 3u;
            return;
        }
        if (line_ready) {
            filesystem_cacheResidentName(op_item_offset, op_line_buf);
            if (op_item_offset < UINT16_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if (eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 3u;
        }
        return;
    }

    case 3: /* WAIT SOURCE CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE || !update) {
            filesystem_finish(op_close_status);
            return;
        }
        if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_KIT)
            filesystem_cacheCurrentResidentKitNames();
        else if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE)
            filesystem_cacheCurrentResidentSceneNames();
        else
            filesystem_cacheCurrentResidentInstrumentNames();
        op_file_ready = false;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "w",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 4u;
        return;

    case 4: /* WAIT DESTINATION OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_bytes_done = 0u;
        op_phase = 5u;
        return;

    case 5: /* STREAM THE PRESERVED REGISTER */
        if (op_item_offset < FS_RESIDENT_NAMES_ROW_COUNT) {
            if (op_bytes_done == 0u) {
                const char *name = fs_list_cache_name[op_item_offset];
                op_line_len = filesystem_formatResidentNameLine(
                    op_line_buf,
                    sizeof(op_line_buf),
                    name,
                    (uint8_t)!filesystem_residentNameIsBlank(name));
                if (op_line_len == 0u) {
                    filesystem_finish(FS_STATUS_ERROR);
                    return;
                }
            }
            op_bytes_done += afatfs_fwrite(
                op_file,
                (const uint8_t *)op_line_buf + op_bytes_done,
                op_line_len - op_bytes_done);
            if (op_bytes_done >= op_line_len) {
                op_item_offset++;
                op_bytes_done = 0u;
            }
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 6u;
        return;

    case 6: /* WAIT DESTINATION CLOSE + FLUSH */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Finish only the requested resident-name transaction.
         *
         * Inputs: the complete HCNAMES cache and a closed root file. Output:
         * HCNAMES is flushed without inferring whether `/Scene/` changed.
         * Scene Save explicitly owns the namespace-rebuild flag before it
         * enters this shared writer; pure Scene Load instead lets Menu reload
         * the unchanged `.hcindex` after DSP apply. This keeps a metadata
         * writer from selecting either caller's browser policy.
         */
        filesystem_finish(FS_STATUS_DONE);
        return;

    case 7: /* PROVE FAILED-READ REGISTER IS ABSENT BEFORE BOOTSTRAP */
        probe_result = filesystem_hcnamesProbe_tick();
        if (probe_result == FS_HCNAMES_PROBE_IN_PROGRESS)
            return;
        if (probe_result == FS_HCNAMES_PROBE_ERROR) {
            /* A root scan or close failure must retain the failed read as an
             * error; creating a new root register here would hide that fault. */
            filesystem_makeNamedErrorCode("HNPrb", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (probe_result == FS_HCNAMES_PROBE_DUPLICATE) {
            /* Do not select one duplicate or repair either entry implicitly.
             * The named error is visible to runtime callers and boot logging
             * retains the condition if this update occurs during boot. */
            filesystem_bootLoggingSetDetail("HNDUP   ");
            filesystem_makeNamedErrorCode("HNDup", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (probe_result == FS_HCNAMES_PROBE_PRESENT) {
            /* The scan found exactly one folded match.  Retry its normal read
             * once; a second NULL is an error, never a retry/create loop. */
            op_phase = 8u;
            return;
        }
        /* Only an empty, successfully closed root scan reaches this create.
         * Start with blank preserved rows, then publish the completed action's
         * identity block as the first authoritative HCNAMES content. */
        if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_KIT)
            filesystem_cacheCurrentResidentKitNames();
        else if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE)
            filesystem_cacheCurrentResidentSceneNames();
        else
            filesystem_cacheCurrentResidentInstrumentNames();
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "w",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 4u;
        return;

    case 8: /* RETURN ROOT + RETRY THE ONE SCANNED EXISTING REGISTER */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 9u;
        return;

    case 9: /* WAIT ONE RETRY; DO NOT TURN A SECOND FAILURE INTO CREATE */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_makeNamedErrorCode("HNRtry", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 2u;
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/*
 * Return one requested root component without creating a second filename
 * buffer.  op_stream_index is otherwise idle for this operation and is the
 * zero-based A/B target selector; its normal request initializer resets it.
 */
static const char *filesystem_autosaveTargetName(void)
{
    return (op_stream_index == 0u)
        ? AUTOSAVE_RECORD_A_FILENAME
        : AUTOSAVE_RECORD_B_FILENAME;
}

static const char *filesystem_autosaveFilenameForIndex(uint8_t index)
{
    /*
     * Keep boot creation and runtime writer bound to the same A/B display
     * names. Every autosave LFN open uses case-insensitive matching: FAT has
     * one case-folded filename namespace, so exact-case lookup could miss an
     * existing host-cased entry and create a visible duplicate.
     */
    return (index == 0u) ? AUTOSAVE_RECORD_A_FILENAME
                         : AUTOSAVE_RECORD_B_FILENAME;
}

static uint32_t filesystem_autosaveCreatedTargetGeneration(void)
{
    /*
     * Once a missing target is open, op_stream_index is borrowed for the
     * already-calculated CRC32C. op_file_version is no longer needed as the
     * root-scan absent flag, so it retains that target's A/B index until the
     * close callback restores normal target selection.
     */
    return (op_file_version == 0u) ? 1u : 0u;
}

static uint8_t filesystem_autosaveTargetMatches(const char *candidate,
                                                const char *target)
{
    /*
     * Autosave's creation-only pass shares the HCNAMES probe's FAT-folded
     * comparison.  A host case variant is an existing record, never a reason
     * to open the writer as though the root target were absent.
     */
    return filesystem_displayNameMatchesCaseInsensitive(candidate, target);
}

static void filesystem_autosaveAdvanceTarget(void)
{
    /*
     * A target has either been proven existing or its new file has closed.
     * Both outcomes intentionally take the same next step.  There is no
     * active-record flag yet: this first pass only ensures two baseline files.
     */
    if (op_stream_index + 1u < AUTOSAVE_RECORD_FILE_COUNT) {
        op_stream_index++;
        op_phase = 4u;
        return;
    }
    if (!afatfs_chdir(NULL))
        return;
    filesystem_finish(FS_STATUS_DONE);
}

/* -----------------------------------------------------------------------
** BOOT AUTOSAVE-REGISTER ENSURE state machine
**
** Inputs: a successfully loaded Bank, its BankData name, and root `/.hcnames`.
** Outputs: only missing `/.hcprms1` and `/.hcprms2` files receive one
** 34,768-byte slot/name baseline with zero mask/parameters and a valid CRC32C
** control header. Existing files are first found by the LFN-aware root iterator
** and then left unopened for write. The state machine owns no new retained
** memory: it reuses the normal HCNAMES cache, staging_buf, operation cursors,
** and one file handle.
** ----------------------------------------------------------------------- */
static void filesystem_ensureAutosaveFiles_tick(void)
{
    switch (op_phase) {
    case 0: /* RETURN ROOT + OPEN AUTHORITATIVE HCNAMES */
        if (!bank_hasResidentBank()) {
            /* Defensive duplicate of the public wrapper's no-Bank gate. */
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        filesystem_prepareResidentNamesCache();
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME, "r",
                              FS_RESIDENT_NAMES_MATCH_MODE, NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 1u;
        return;

    case 1: /* WAIT HCNAMES OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            /* Correct complete name population is impossible without HCNAMES. */
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 2u;
        return;

    case 2: /* STREAM ALL HCNAMES ROWS INTO THE EXISTING SHARED CACHE */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t st = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (st != STORAGE_STATUS_OK && st != STORAGE_STATUS_WAIT) {
            op_close_status = FS_STATUS_ERROR;
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 3u;
            return;
        }
        if (line_ready) {
            filesystem_cacheResidentName(op_item_offset, op_line_buf);
            if (op_item_offset < UINT16_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if (eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 3u;
        }
        return;
    }

    case 3: /* WAIT HCNAMES CLOSE, THEN BEGIN RECORD A ROOT SCAN */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_finish(op_close_status);
            return;
        }
        op_stream_index = 0u;
        op_phase = 4u;
        return;

    case 4: /* OPEN ROOT AS A DIRECTORY FOR A NO-OVERWRITE EXISTENCE SCAN */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 5u;
        return;

    case 5: /* WAIT ROOT DIRECTORY OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /* op_file_version is existing generic operation scratch; here 1=absent. */
        op_file_version = 1u;
        afatfs_findFirstObject(op_file, &op_object_finder);
        op_phase = 6u;
        return;

    case 6: /* FIND TARGET OR ROOT END */
    {
        afatfsOperationStatus_e st = afatfs_findNextObject(
            op_file, &op_object_finder, &op_object);

        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op_file, &op_object_finder);
            op_close_status = FS_STATUS_ERROR;
            op_phase = 7u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_file, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 7u;
            return;
        }
        if (filesystem_autosaveTargetMatches(op_object.id.displayName,
                                             filesystem_autosaveTargetName())) {
            /* Any matching object, file or directory, is preserved untouched. */
            op_file_version = 0u;
            afatfs_findLastObject(op_file, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 7u;
        }
        return;
    }

    case 7: /* CLOSE ROOT SCAN HANDLE BEFORE EITHER ADVANCE OR CREATE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 8u;
        return;

    case 8: /* WAIT ROOT SCAN CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_finish(op_close_status);
            return;
        }
        if (op_file_version == 0u) {
            filesystem_autosaveAdvanceTarget();
            return;
        }
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        if (!afatfs_fopen_lfn(filesystem_autosaveTargetName(), "w",
                              AFATFS_MATCH_CASE_INSENSITIVE, NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 9u;
        return;

    case 9: /* WAIT NEW TARGET OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /*
         * Compute the deterministic whole-record CRC once per newly created
         * target. The existing fields change roles only after the root scan:
         * op_file_version retains A/B and op_stream_index now holds CRC32C.
         */
        op_file_version = (uint8_t)op_stream_index;
        op_stream_index = autosave_initialRecordCrc(
            filesystem_autosaveCreatedTargetGeneration(),
            bank_restoreBankSlot(), bank_displayName(),
            (const char (*)[AUTOSAVE_HCNAMES_ROW_BYTES])fs_list_cache_name);
        op_bytes_done = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 10u;
        return;

    case 10: /* STREAM ONE COMPLETE HEADER/MASK/NAME CHUNK AT A TIME */
    {
        uint32_t written;

        if (op_bytes_done >= AUTOSAVE_RECORD_BYTES) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 11u;
            return;
        }
        if (op_write_line_len == 0u) {
            uint32_t remaining = AUTOSAVE_RECORD_BYTES - op_bytes_done;

            op_write_line_len = (remaining > sizeof(staging_buf))
                ? sizeof(staging_buf) : (uint16_t)remaining;
            op_write_line_offset = 0u;
            autosave_formatInitialChunk(
                staging_buf, op_bytes_done, op_write_line_len,
                filesystem_autosaveCreatedTargetGeneration(),
                bank_restoreBankSlot(), op_stream_index, bank_displayName(),
                (const char (*)[AUTOSAVE_HCNAMES_ROW_BYTES])fs_list_cache_name);
        }
        written = afatfs_fwrite(
            op_file, staging_buf + op_write_line_offset,
            op_write_line_len - op_write_line_offset);
        op_write_line_offset =
            (uint16_t)(op_write_line_offset + written);
        /*
         * A zero-byte asynchronous write is normally back-pressure and must be
         * retried. Once AsyncFATFS has positively declared its regular cluster
         * pool exhausted, however, retry can never progress: its full flag is
         * intentionally sticky until remount. Close the partial file and
         * return an error so this boot-only wrapper cannot strand the device
         * in its pre-audio polling loop.
         *
         * The cluster allocator now searches both sides of its allocation
         * hint before setting this flag, so this branch represents genuine
         * exhaustion rather than the former false end-of-volume result.
         */
        if (written == 0u && afatfs_isFull()) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 12u;
            return;
        }
        if (op_write_line_offset >= op_write_line_len) {
            op_bytes_done += op_write_line_len;
            op_write_line_len = 0u;
            op_write_line_offset = 0u;
        }
        return;
    }

    case 11: /* WAIT NEW TARGET CLOSE, THEN HANDLE THE OTHER TARGET */
        if (!op_close_done)
            return;
        op_file = NULL;
        /* Restore op_stream_index's ordinary A/B selector before advancing. */
        op_stream_index = op_file_version;
        filesystem_autosaveAdvanceTarget();
        return;

    case 12: /* WAIT FAILED PARTIAL-FILE CLOSE, THEN RELEASE BOOT */
        /*
         * The caller only authorizes the runtime writer after a DONE result.
         * Returning ERROR here therefore leaves autosave disabled while still
         * allowing the rest of boot to reach audio and UI. The partial record
         * is harmless: a later boot can create the missing peer, after which
         * normal validation/recovery can replace any invalid record.
         */
        if (!op_close_done)
            return;
        op_file = NULL;
        filesystem_finish(FS_STATUS_ERROR);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

static void filesystem_autosaveWriterFinishErrorNow(void)
{
    /*
     * Recovery is the only writer path that borrows HCNAMES. Dispose that
     * temporary cache domain on every error before publishing the normal
     * filesystem failure; ordinary copy-forward never touches name rows.
     */
    if (op_autosave_writer.recovery_using_names) {
        filesystem_clearNameCacheStorage();
        op_autosave_writer.recovery_using_names = 0u;
    }
    /*
     * Restore every live value captured before an unsuccessful target commit.
     *
     * Inputs: the transaction-local sorted offset list and patch_count; before
     * classification or on recovery errors the count is zero. Output: those
     * offsets are dirty again in Autosave.c's canonical record. Why: phase 56
     * clears a bit after capturing its stable value, but a failed SD operation
     * must not discard work that may exist only in SRAM. Setting is idempotent,
     * so file-carried or concurrently re-dirtied positions remain correct.
     */
    autosave_maskRestoreCaptured(
        fs_autosave_parameter_cache.payload_offsets,
        op_autosave_writer.patch_count);
    filesystem_finish(FS_STATUS_ERROR);
}

static void filesystem_autosaveWriterFinishError(void)
{
    /*
     * Every normal writer close is asynchronous, so an error may arrive while
     * the source and/or inactive target is still open. Route it through the
     * dedicated close-down phases instead of handing a live handle back to a
     * later filesystem operation. The phases retry a close only until it is
     * accepted, then wait for its callback before publishing ERROR.
     */
    if (op_file) {
        op_phase = 40u;
        return;
    }
    if (op_autosave_writer.target_file) {
        op_phase = 42u;
        return;
    }
    filesystem_autosaveWriterFinishErrorNow();
}

static uint32_t filesystem_autosaveRecoveryGeneration(void)
{
    /* Recovery writes B=0 first, then A=1, restoring the documented baseline. */
    return (op_autosave_writer.recovery_target_index == 0u) ? 1u : 0u;
}

/* -----------------------------------------------------------------------
** RUNTIME AUTOSAVE PARAMETER-DRAIN state machine
**
** Inputs: a resident Bank, two root records, the canonical SRAM dirty record,
** the winner's file-carried completeness mask, and
** the ordinary foreground filesystem pump. Outputs: at most the configured
** number of stable live payload bytes are captured, their bits plus every
** classified nonexistent bit are cleared in the canonical record, and the
** current canonical mask/value image is copy-forwarded into the inactive peer.
** Remaining dirty bits stay in SRAM and are also carried by any target mask
** chunk staged after they were set. If neither record validates for the Bank
** identity, both are asynchronously regenerated B then A from HCNAMES with a
** zero mask. No phase blocks on AsyncFATFS or borrows the shared name cache
** except that existing recovery path. The file mask is ORed into Autosave.c's
** single persistent record before classification; an empty canonical mask
** completes read-only and never creates another empty generation. One
** transformed copy calculates CRC from the exact staged bytes, then publishes
** durable CRC and the final commit marker in separate post-copy steps.
** ----------------------------------------------------------------------- */
static void filesystem_autosaveParameterDrain_tick(void)
{
    switch (op_phase) {
    case 0: /* INITIALIZE ONE OPERATION-LOCAL A/B VALIDATION PASS */
        /*
         * Reset operation progress and transaction-local patches only.
         *
         * Inputs: a newly accepted private filesystem operation. Outputs: no
         * stale patch offset or captured value can leak from a prior
         * generation. Autosave.c's canonical mask is deliberately untouched:
         * starting a filesystem transaction is not a dirty-record reset. The
         * name cache also remains untouched by this ordinary path.
         */
        memset(&op_autosave_writer, 0, sizeof(op_autosave_writer));
        memset(&fs_autosave_parameter_cache, 0,
               sizeof(fs_autosave_parameter_cache));
        op_autosave_writer.candidate_index = 0u;
        op_phase = 1u;
        return;

    case 1: /* OPEN CURRENT A/B CANDIDATE FOR A STREAMING VALIDATION READ */
        if (!afatfs_chdir(NULL))
            return;
        autosave_streamValidationBegin(&op_autosave_writer.validation);
        op_autosave_writer.candidate_valid = 0u;
        op_bytes_done = 0u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    op_autosave_writer.candidate_index),
                "r", AFATFS_MATCH_CASE_INSENSITIVE, NULL, on_file_opened)) {
            /* No handle available/missing candidate is simply invalid. */
            op_phase = 5u;
            return;
        }
        op_phase = 2u;
        return;

    case 2: /* WAIT CANDIDATE OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            op_phase = 5u;
            return;
        }
        op_phase = 3u;
        return;

    case 3: /* STREAM ONE CANDIDATE THROUGH CRC/HEADER/BANK-NAME VALIDATION */
    {
        uint32_t n = afatfs_fread(op_file, staging_buf, sizeof(staging_buf));

        if (n != 0u) {
            autosave_streamValidationUpdate(&op_autosave_writer.validation,
                                            op_bytes_done, staging_buf,
                                            (uint16_t)n);
            op_bytes_done += n;
            return;
        }
        if (!afatfs_feof(op_file))
            return;
        op_autosave_writer.candidate_valid = (uint8_t)(
            autosave_streamValidationFinish(&op_autosave_writer.validation) &&
            autosave_streamValidationMatchesBank(
                &op_autosave_writer.validation, bank_restoreBankSlot(),
                bank_displayName()));
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4u;
        return;
    }

    case 4: /* WAIT CANDIDATE CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        op_phase = 5u;
        return;

    case 5: /* RECORD BEST VALID CANDIDATE, THEN ADVANCE A -> B */
        if (op_autosave_writer.candidate_valid &&
            (!op_autosave_writer.have_winner ||
             autosave_generationIsNewer(
                 op_autosave_writer.validation.generation,
                 op_autosave_writer.winner_generation))) {
            /* A remains the deterministic winner on equal generations. */
            op_autosave_writer.have_winner = 1u;
            op_autosave_writer.winner_index =
                op_autosave_writer.candidate_index;
            op_autosave_writer.winner_generation =
                op_autosave_writer.validation.generation;
            op_autosave_writer.winner_probe =
                op_autosave_writer.validation.probe_counter;
        }
        if (op_autosave_writer.candidate_index + 1u <
            AUTOSAVE_RECORD_FILE_COUNT) {
            op_autosave_writer.candidate_index++;
            op_phase = 1u;
            return;
        }
        /*
         * VALIDATED marks the complete two-candidate decision before either
         * recovery or copy-forward work begins. flags bit 0 says a winner
         * exists; bit 1 is its A/B index when present; value is its generation
         * (zero without a winner). This preserves validation failure evidence
         * rather than inferring it later from a recovery path.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_VALIDATED,
            (uint8_t)((op_autosave_writer.have_winner ? 1u : 0u) |
                      (op_autosave_writer.have_winner
                           ? (uint8_t)(op_autosave_writer.winner_index << 1u)
                           : 0u)),
            op_autosave_writer.have_winner
                ? op_autosave_writer.winner_generation : 0u);
        op_phase = op_autosave_writer.have_winner ? 50u : 30u;
        return;

    case 50: /* REOPEN WINNER TO LOAD ITS COMPLETE MUTATION MASK */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    op_autosave_writer.winner_index),
                "r", AFATFS_MATCH_CASE_INSENSITIVE, NULL, on_file_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 51u;
        return;

    case 51: /* WAIT WINNER MASK-READER OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 52u;
        return;

    case 52: /* START/RETRY ASYNCHRONOUS SEEK TO MASK OFFSET */
    {
        afatfsOperationStatus_e seek = afatfs_fseek(
            op_file, AUTOSAVE_MASK_OFFSET, AFATFS_SEEK_SET);

        /*
         * AsyncFATFS seek may complete immediately or queue cluster traversal.
         *
         * Output: an immediate success can read the mask next; queued progress
         * is verified through ftell before any cache byte is accepted. FAILURE
         * represents temporary file busy state and is retried next tick, as in
         * the existing final-commit seek.
         */
        if (seek == AFATFS_OPERATION_SUCCESS) {
            op_phase = 54u;
        } else if (seek == AFATFS_OPERATION_IN_PROGRESS) {
            op_phase = 53u;
        }
        return;
    }

    case 53: /* WAIT QUEUED MASK SEEK BY OBSERVING THE FILE CURSOR */
        if (!afatfs_ftell(op_file, &op_autosave_writer.seek_position))
            return;
        if (op_autosave_writer.seek_position != AUTOSAVE_MASK_OFFSET) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 54u;
        return;

    case 54: /* OR THE WINNER'S 3,856-BYTE MASK INTO CANONICAL SRAM */
    {
        uint32_t remaining =
            AUTOSAVE_MASK_BYTES - op_autosave_writer.mask_bytes_read;
        uint32_t n;

        if (remaining == 0u) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 55u;
            return;
        }
        n = afatfs_fread(op_file, staging_buf,
                         (remaining > sizeof(staging_buf))
                             ? sizeof(staging_buf) : remaining);
        if (n != 0u) {
            /*
             * Merge accepted file bits before advancing the stream cursor.
             *
             * Inputs: one winner-mask interval in the existing 512-byte
             * staging buffer plus its mask-relative cursor. Output: set file
             * bits are ORed into Autosave.c's canonical record; pre-existing
             * SRAM bits can never be overwritten by a file read. Why: the file
             * carries incomplete work across power loss but is not the live
             * owner. Affiliates: autosave_maskMergeChunk() and phase 55.
             */
            autosave_maskMergeChunk(
                op_autosave_writer.mask_bytes_read,
                staging_buf, (uint16_t)n);
            op_autosave_writer.mask_bytes_read = (uint16_t)(
                op_autosave_writer.mask_bytes_read + n);
            return;
        }
        if (afatfs_feof(op_file))
            filesystem_autosaveWriterFinishError();
        return;
    }

    case 55: /* CLOSE COMPLETE: FALL THROUGH IF CANONICAL SRAM IS EMPTY */
    {
        uint8_t has_dirty;

        if (!op_close_done)
            return;
        op_file = NULL;
        /*
         * Stop at the first safe no-write boundary after loading the mask.
         *
         * Inputs: the selected winner has validated, its complete on-file mask
         * has been ORed into Autosave.c's persistent record, and the read
         * handle is closed. Output: an empty canonical mask completes directly
         * without removing the peer, advancing generation/probe, writing, or
         * flushing. Why: the file mask records completeness; it is not the
         * owner or a request to manufacture another empty generation. A
         * nonempty record continues into bounded classification. Affiliates:
         * autosave_maskHasDirty() and filesystem_autosaveWriterCompleted().
         */
        has_dirty = autosave_maskHasDirty();
        /*
         * MASK_MERGED distinguishes a clean read-only recovery from work that
         * will enter live-byte capture. flags is the post-merge dirty result;
         * value is the exact on-file mask bytes accepted before the handle
         * closed. This records the canonical-owner boundary without changing it.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_MASK_MERGED, has_dirty,
                             (uint32_t)op_autosave_writer.mask_bytes_read);
        if (!has_dirty) {
            filesystem_complete(FS_STATUS_DONE);
            return;
        }
        op_autosave_writer.payload_scan_offset = 0u;
        op_autosave_writer.patch_count = 0u;
        op_phase = 56u;
        return;
    }

    case 56: /* CLASSIFY/CAPTURE A BOUNDED NUMBER OF MASK POSITIONS */
    {
        uint16_t examined = 0u;

        /*
         * Build one stable sorted patch list without monopolizing a main-loop
         * pass.
         *
         * Inputs: canonical mask and retained payload cursor. Outputs: an
         * atomic take claims each available bit before its live get; existing
         * bytes are captured in the transaction cache, nonexistent cells use no
         * patch, and later cells remain untouched when either bound is reached.
         * Why: a timer-side mutation after take re-dirties the bit for the next
         * pass instead of being erased by a later foreground clear.
         */
        while (op_autosave_writer.payload_scan_offset <
                   AUTOSAVE_PAYLOAD_BYTES &&
               examined < AUTOSAVE_MASK_BITS_PER_TICK) {
            uint16_t payload_offset =
                op_autosave_writer.payload_scan_offset;

            if (op_autosave_writer.patch_count >=
                AUTOSAVE_PARAMETER_GETS_PER_WRITE) {
                filesystem_autosaveTraceCaptured(1u);
                op_phase = 10u;
                return;
            }
            op_autosave_writer.payload_scan_offset++;
            examined++;
            if (!autosave_maskBitTake(payload_offset)) {
                continue;
            }
            if (autosave_getLivePayloadByte(
                    payload_offset,
                    &fs_autosave_parameter_cache.payload_values[
                        op_autosave_writer.patch_count])) {
                fs_autosave_parameter_cache.payload_offsets[
                    op_autosave_writer.patch_count] = payload_offset;
                op_autosave_writer.patch_count++;
            }
            /*
             * Take already closed the claimed bit atomically. A successful get
             * is represented by a stable patch; a failed get proves the format
             * cell has no current owner. Only successful gets consume budget,
             * while any later producer remains set for continuation.
             */
        }
        if (op_autosave_writer.payload_scan_offset >=
            AUTOSAVE_PAYLOAD_BYTES) {
            filesystem_autosaveTraceCaptured(0u);
            op_phase = 10u;
        }
        return;
    }

    case 10: /* OPEN WINNER FOR THE SINGLE COPY/CRC SOURCE STREAM */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    op_autosave_writer.winner_index),
                "r", AFATFS_MATCH_CASE_INSENSITIVE, NULL, on_file_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 11u;
        return;

    case 11: /* WAIT COPY SOURCE OPEN, THEN REMOVE EVERY INACTIVE VARIANT */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        /*
         * FAT display names are case-insensitive and a damaged/old card can
         * already contain multiple LFN entries for one hidden register. The
         * selected winner remains open and untouched, so retire every file
         * variant of only the inactive target before creating its one new
         * canonical entry. This is the duplicate-prevention boundary: a
         * write-open alone can otherwise select one variant and leave siblings
         * visible to a desktop filesystem.
         */
        op_remove_done = 0u;
        if (!afatfs_removeObjects_lfn(
                filesystem_autosaveFilenameForIndex(
                    (uint8_t)(op_autosave_writer.winner_index ^ 1u)),
                AFATFS_MATCH_CASE_INSENSITIVE,
                AFATFS_REMOVE_FILES_ONLY, on_remove_complete)) {
            return;
        }
        op_phase = 24u;
        return;

    case 24: /* WAIT TARGET-VARIANT RETIREMENT, THEN CREATE ONE TARGET */
        if (!op_remove_done)
            return;
        op_autosave_writer.target_file = NULL;
        op_autosave_writer.target_ready = 0u;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    (uint8_t)(op_autosave_writer.winner_index ^ 1u)),
                "w", AFATFS_MATCH_CASE_INSENSITIVE, NULL,
                on_autosave_target_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 12u;
        return;

    case 12: /* WAIT INACTIVE TARGET OPEN */
        if (!op_autosave_writer.target_ready)
            return;
        if (!op_autosave_writer.target_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_autosave_writer.stream_offset = 0u;
        op_autosave_writer.chunk_bytes = 0u;
        op_autosave_writer.chunk_written = 0u;
        /*
         * Start one transformed copy and its matching CRC accumulator.
         *
         * Inputs are the canonical mask and immutable captured patch list.
         * Output is one monotonic patch cursor plus a fresh CRC32C accumulator;
         * every transformed chunk is checksummed once and then written, so no
         * earlier projected record can diverge from the physical copy.
         */
        op_autosave_writer.patch_cursor = 0u;
        op_autosave_writer.target_crc32c = autosave_recordCrcBegin();
        op_phase = 13u;
        return;

    case 13: /* TRANSFORM, CRC, AND COPY ONE UNPUBLISHED TARGET CHUNK */
    {
        uint32_t n;

        if (op_autosave_writer.chunk_written <
            op_autosave_writer.chunk_bytes) {
            n = afatfs_fwrite(
                op_autosave_writer.target_file,
                staging_buf + op_autosave_writer.chunk_written,
                op_autosave_writer.chunk_bytes -
                    op_autosave_writer.chunk_written);
            op_autosave_writer.chunk_written = (uint16_t)(
                op_autosave_writer.chunk_written + n);
            /*
             * Zero normally means asynchronous back-pressure. A sticky full
             * result is terminal, so enter the existing two-handle close path
             * rather than occupying the one filesystem operation forever.
             */
            if (n == 0u && afatfs_isFull())
                filesystem_autosaveWriterFinishError();
            return;
        }
        if (op_autosave_writer.stream_offset >= AUTOSAVE_RECORD_BYTES) {
            /*
             * Finalize exactly once after the last checksummed chunk has also
             * passed the partial-fwrite gate above. Output is the four-byte
             * value published after the invalid target copy becomes durable;
             * a separate phase retries source close without complementing the
             * accumulator again if AsyncFATFS is temporarily busy.
             */
            op_autosave_writer.target_crc32c = autosave_recordCrcFinish(
                op_autosave_writer.target_crc32c);
            op_phase = 67u;
            return;
        }
        n = afatfs_fread(
            op_file, staging_buf,
            ((AUTOSAVE_RECORD_BYTES - op_autosave_writer.stream_offset) >
             sizeof(staging_buf))
                ? sizeof(staging_buf)
                : (AUTOSAVE_RECORD_BYTES - op_autosave_writer.stream_offset));
        if (n == 0u) {
            if (afatfs_feof(op_file))
                filesystem_autosaveWriterFinishError();
            return;
        }
        autosave_transformDrainChunk(
            staging_buf, op_autosave_writer.stream_offset, (uint16_t)n,
            op_autosave_writer.winner_generation + 1u,
            (uint8_t)(op_autosave_writer.winner_probe + 1u),
            fs_autosave_parameter_cache.payload_offsets,
            fs_autosave_parameter_cache.payload_values,
            op_autosave_writer.patch_count,
            &op_autosave_writer.patch_cursor);
        /*
         * Checksum the prospective final bytes, then keep the physical target
         * invalid while it is under construction.
         *
         * Input is the transformed chunk containing final generation, probe,
         * canonical mask, payload patches, zero CRC field, and logical A5
         * commit. Output advances CRC from those exact bytes. If this interval
         * contains the commit cell, only its staged disk value is then cleared;
         * the calculated CRC continues to describe the later committed image.
         * Affiliates: post-copy CRC phases and final commit publication.
         */
        op_autosave_writer.target_crc32c = autosave_recordCrcUpdate(
            op_autosave_writer.target_crc32c,
            op_autosave_writer.stream_offset,
            staging_buf, (uint16_t)n);
        if (op_autosave_writer.stream_offset <=
                AUTOSAVE_HEADER_COMMIT_OFFSET &&
            op_autosave_writer.stream_offset + n >
                AUTOSAVE_HEADER_COMMIT_OFFSET) {
            staging_buf[AUTOSAVE_HEADER_COMMIT_OFFSET -
                        op_autosave_writer.stream_offset] = 0u;
        }
        op_autosave_writer.stream_offset += n;
        op_autosave_writer.chunk_bytes = (uint16_t)n;
        op_autosave_writer.chunk_written = 0u;
        return;
    }

    case 67: /* QUEUE/RETRY SINGLE COPY-SOURCE CLOSE */
        /*
         * Close the winner reader after CRC finalization without re-entering
         * the finalization branch. Input is the exhausted source handle;
         * output proceeds only when AsyncFATFS accepts the asynchronous close.
         */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 14u;
        return;

    case 14: /* WAIT SINGLE COPY-SOURCE CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        op_phase = 66u;
        return;

    case 66: /* QUEUE/RETRY CRC/COMMIT-ZERO TARGET CLOSE */
        /*
         * Close the concurrently written inactive target in its own retry
         * phase. Input is target_file after the source close callback. Output
         * advances only after AsyncFATFS accepts the target close; separating
         * queue from wait prevents a rejected close from leaving the state
         * machine waiting for a callback that was never scheduled.
         */
        op_close_done = false;
        if (afatfs_fclose(op_autosave_writer.target_file, on_file_closed))
            op_phase = 15u;
        return;

    case 15: /* WAIT INVALID TARGET CLOSE, THEN ADVANCE TO DATA SYNC */
        if (!op_close_done)
            return;
        op_autosave_writer.target_file = NULL;
        op_phase = 16u;
        return;

    case 16: /* MAKE THE CRC/COMMIT-ZERO TARGET COPY DURABLE */
        /*
         * Persist every checksummed data byte before publishing its checksum.
         *
         * Input is the closed full-size target containing transformed payload,
         * canonical mask bytes, CRC zero, and commit zero. Output advances only
         * after AsyncFATFS reports those data/FAT sectors durable. Why: the
         * later CRC must never describe a copy still pending in cache.
         */
        if (!afatfs_sync())
            return;
        op_phase = 17u;
        return;

    case 17: /* REOPEN DURABLE TARGET WITHOUT TRUNCATION FOR CRC */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    (uint8_t)(op_autosave_writer.winner_index ^ 1u)),
                "r+", AFATFS_MATCH_CASE_INSENSITIVE, NULL, on_file_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 18u;
        return;

    case 18: /* WAIT CRC-PUBLICATION TARGET OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_autosave_writer.chunk_written = 0u;
        op_phase = 19u;
        return;

    case 19: /* START/RETRY ASYNCHRONOUS SEEK TO HEADER CRC OFFSET */
    {
        afatfsOperationStatus_e seek = afatfs_fseek(
            op_file, AUTOSAVE_HEADER_CRC32C_OFFSET, AFATFS_SEEK_SET);

        if (seek == AFATFS_OPERATION_SUCCESS) {
            op_phase = 21u;
        } else if (seek == AFATFS_OPERATION_IN_PROGRESS) {
            op_phase = 20u;
        }
        /* FAILURE means the file is still busy; retry this phase next tick. */
        return;
    }

    case 20: /* WAIT QUEUED CRC SEEK BY OBSERVING THE FILE CURSOR */
        if (!afatfs_ftell(op_file, &op_autosave_writer.seek_position))
            return;
        if (op_autosave_writer.seek_position != AUTOSAVE_HEADER_CRC32C_OFFSET) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 21u;
        return;

    case 21: /* WRITE THE CALCULATED LITTLE-ENDIAN CRC AFTER THE COPY */
    {
        uint32_t n;

        /*
         * Publish the checksum only after the data it covers is durable.
         *
         * Input is target_crc32c finalized at the end of the one copy stream.
         * Output writes exactly four little-endian bytes at header offset 12,
         * retaining a byte cursor across AsyncFATFS partial writes. The target
         * remains invalid because its commit byte is still zero. Affiliates:
         * phases 16/57-59 and autosave_recordCrcUpdate().
         */
        staging_buf[0] = (uint8_t)op_autosave_writer.target_crc32c;
        staging_buf[1] = (uint8_t)(op_autosave_writer.target_crc32c >> 8u);
        staging_buf[2] = (uint8_t)(op_autosave_writer.target_crc32c >> 16u);
        staging_buf[3] = (uint8_t)(op_autosave_writer.target_crc32c >> 24u);
        n = afatfs_fwrite(op_file,
                          staging_buf + op_autosave_writer.chunk_written,
                          4u - op_autosave_writer.chunk_written);
        op_autosave_writer.chunk_written = (uint16_t)(
            op_autosave_writer.chunk_written + n);
        if (n == 0u && afatfs_isFull()) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        if (op_autosave_writer.chunk_written < 4u)
            return;
        op_phase = 57u;
        return;
    }

    case 57: /* QUEUE/RETRY CRC-PUBLICATION HANDLE CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 58u;
        return;

    case 58: /* WAIT CRC-PUBLICATION HANDLE CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        op_phase = 59u;
        return;

    case 59: /* MAKE THE POST-COPY CRC DURABLE BEFORE VALID COMMIT */
        /*
         * Persist the CRC independently while commit remains zero.
         *
         * Input is a closed target with durable data and a newly written CRC.
         * Output permits commit publication only after the checksum sector is
         * durable. Why: combining CRC and commit without this gate could expose
         * A5 before the checksum it validates has reached the card.
         */
        if (!afatfs_sync())
            return;
        op_phase = 60u;
        return;

    case 60: /* REOPEN CRC-COMPLETE TARGET FOR THE FINAL COMMIT BYTE */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    (uint8_t)(op_autosave_writer.winner_index ^ 1u)),
                "r+", AFATFS_MATCH_CASE_INSENSITIVE, NULL, on_file_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 61u;
        return;

    case 61: /* WAIT FINAL-COMMIT TARGET OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 62u;
        return;

    case 62: /* START/RETRY ASYNCHRONOUS SEEK TO HEADER COMMIT OFFSET */
    {
        afatfsOperationStatus_e seek = afatfs_fseek(
            op_file, AUTOSAVE_HEADER_COMMIT_OFFSET, AFATFS_SEEK_SET);

        if (seek == AFATFS_OPERATION_SUCCESS) {
            op_phase = 64u;
        } else if (seek == AFATFS_OPERATION_IN_PROGRESS) {
            op_phase = 63u;
        }
        /* FAILURE means the file is still busy; retry this phase next tick. */
        return;
    }

    case 63: /* WAIT QUEUED COMMIT SEEK BY OBSERVING THE FILE CURSOR */
        if (!afatfs_ftell(op_file, &op_autosave_writer.seek_position))
            return;
        if (op_autosave_writer.seek_position != AUTOSAVE_HEADER_COMMIT_OFFSET) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 64u;
        return;

    case 64: /* WRITE THE ONE VALID MARKER BYTE LAST */
    {
        uint32_t n;

        /*
         * Publish validity only after data and CRC have separate durable gates.
         * Input is a CRC-complete target with commit zero. Output changes only
         * header byte 5 to A5, making the new generation eligible after the
         * existing final filesystem flush. Affiliates: stream validation and
         * A/B generation selection.
         */
        staging_buf[0] = AUTOSAVE_HEADER_COMMIT_VALID;
        n = afatfs_fwrite(op_file, staging_buf, 1u);
        if (n == 0u) {
            if (afatfs_isFull())
                filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 65u;
        return;
    }

    case 65: /* QUEUE/RETRY FINAL-COMMIT HANDLE CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 22u;
        return;

    case 22: /* WAIT COMMIT CLOSE, THEN ENTER THE EXISTING FINAL SYNC */
        if (!op_close_done)
            return;
        op_file = NULL;
        /*
         * PUBLISHED fires after the final A5 handle is closed but before the
         * generic sync owner replaces this autosave operation. flags is the
         * newly active target index; value is its new generation. The generic
         * final sync remains the durability authority, so this hook cannot
         * claim a hidden write succeeded if that shared gate later fails.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_PUBLISHED,
            (uint8_t)(op_autosave_writer.winner_index ^ 1u),
            op_autosave_writer.winner_generation + 1u);
        /*
         * Hand the committed target to the shared success flush unchanged.
         *
         * Input is the closed A5 target whose data and CRC were already synced.
         * Output defers operation completion until the shared final sync makes
         * the one-byte validity publication durable. Canonical mask ownership
         * remains in Autosave.c; the completion callback reads it directly and
         * no transaction-local scalar replaces or clears it.
         */
        filesystem_finish(FS_STATUS_DONE);
        return;

    case 30: /* RECOVERY: LOAD HCNAMES BEFORE OVERWRITING EITHER INVALID FILE */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_prepareResidentNamesCache();
        op_autosave_writer.recovery_using_names = 1u;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME, "r",
                              FS_RESIDENT_NAMES_MATCH_MODE, NULL,
                              on_file_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 31u;
        return;

    case 31: /* WAIT RECOVERY HCNAMES OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 32u;
        return;

    case 32: /* STREAM HCNAMES INTO THE EXISTING TEMPORARY CACHE */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t read_status = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            op_close_status = FS_STATUS_ERROR;
        }
        if (line_ready) {
            filesystem_cacheResidentName(op_item_offset, op_line_buf);
            if (op_item_offset < UINT16_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if ((read_status != STORAGE_STATUS_OK &&
             read_status != STORAGE_STATUS_WAIT) || eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 33u;
        }
        return;
    }

    case 33: /* WAIT HCNAMES CLOSE, THEN REGENERATE B BEFORE A */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_autosave_writer.recovery_target_index = 1u;
        op_phase = 34u;
        return;

    case 34: /* REMOVE ALL CORRUPT TARGET VARIANTS BEFORE RECOVERY CREATE */
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Neither record passed validation, so recovery may safely collapse
         * all file variants for this one A/B target before rebuilding it.
         * The same case-folded removal used by normal copy-forward guarantees
         * a repaired card returns with exactly one .hcprms1 and one .hcprms2.
         */
        op_remove_done = 0u;
        if (!afatfs_removeObjects_lfn(
                filesystem_autosaveFilenameForIndex(
                    op_autosave_writer.recovery_target_index),
                AFATFS_MATCH_CASE_INSENSITIVE,
                AFATFS_REMOVE_FILES_ONLY, on_remove_complete)) {
            return;
        }
        op_phase = 25u;
        return;

    case 25: /* WAIT RECOVERY TARGET-VARIANT RETIREMENT, THEN OPEN ONE FILE */
        if (!op_remove_done)
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(
                    op_autosave_writer.recovery_target_index),
                "w", AFATFS_MATCH_CASE_INSENSITIVE, NULL, on_file_opened)) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_phase = 35u;
        return;

    case 35: /* WAIT RECOVERY TARGET OPEN AND PREPARE ITS FIXED CRC */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_autosave_writer.target_crc32c = autosave_initialRecordCrc(
            filesystem_autosaveRecoveryGeneration(),
            bank_restoreBankSlot(), bank_displayName(),
            (const char (*)[AUTOSAVE_HCNAMES_ROW_BYTES])fs_list_cache_name);
        op_autosave_writer.stream_offset = 0u;
        op_autosave_writer.chunk_bytes = 0u;
        op_autosave_writer.chunk_written = 0u;
        op_phase = 36u;
        return;

    case 36: /* STREAM ONE RECOVERY BASELINE CHUNK WITHOUT A RECORD BUFFER */
    {
        uint32_t n;

        if (op_autosave_writer.chunk_written <
            op_autosave_writer.chunk_bytes) {
            n = afatfs_fwrite(op_file,
                              staging_buf + op_autosave_writer.chunk_written,
                              op_autosave_writer.chunk_bytes -
                                  op_autosave_writer.chunk_written);
            op_autosave_writer.chunk_written = (uint16_t)(
                op_autosave_writer.chunk_written + n);
            /*
             * Recovery uses the same cluster-growth path as boot creation.
             * Preserve ordinary zero-byte retries, but close and release the
             * background operation after a proven full-volume result.
             */
            if (n == 0u && afatfs_isFull())
                filesystem_autosaveWriterFinishError();
            return;
        }
        if (op_autosave_writer.stream_offset >= AUTOSAVE_RECORD_BYTES) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 37u;
            return;
        }
        op_autosave_writer.chunk_bytes = (uint16_t)(
            (AUTOSAVE_RECORD_BYTES - op_autosave_writer.stream_offset) >
            sizeof(staging_buf)
                ? sizeof(staging_buf)
                : (AUTOSAVE_RECORD_BYTES - op_autosave_writer.stream_offset));
        autosave_formatInitialChunk(
            staging_buf, op_autosave_writer.stream_offset,
            op_autosave_writer.chunk_bytes,
            filesystem_autosaveRecoveryGeneration(),
            bank_restoreBankSlot(), op_autosave_writer.target_crc32c,
            bank_displayName(),
            (const char (*)[AUTOSAVE_HCNAMES_ROW_BYTES])fs_list_cache_name);
        op_autosave_writer.stream_offset += op_autosave_writer.chunk_bytes;
        op_autosave_writer.chunk_written = 0u;
        return;
    }

    case 37: /* WAIT RECOVERY TARGET CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        op_phase = 38u;
        return;

    case 38: /* FLUSH B BEFORE A, THEN USE THE NORMAL FINAL FLUSH FOR A */
        if (op_autosave_writer.recovery_target_index == 1u) {
            if (!afatfs_sync())
                return;
            op_autosave_writer.recovery_target_index = 0u;
            op_phase = 34u;
            return;
        }
        filesystem_clearNameCacheStorage();
        op_autosave_writer.recovery_using_names = 0u;
        filesystem_finish(FS_STATUS_DONE);
        return;

    case 40: /* ERROR CLEANUP: QUEUE/RETRY SOURCE-HANDLE CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 41u;
        return;

    case 41: /* WAIT SOURCE CLOSE, THEN DISCARD ANY OPEN INACTIVE TARGET */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_autosave_writer.target_file) {
            op_phase = 42u;
            return;
        }
        filesystem_autosaveWriterFinishErrorNow();
        return;

    case 42: /* ERROR CLEANUP: QUEUE/RETRY INACTIVE-TARGET CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_autosave_writer.target_file, on_file_closed))
            op_phase = 43u;
        return;

    case 43: /* WAIT INACTIVE TARGET CLOSE, THEN PUBLISH ERROR */
        if (!op_close_done)
            return;
        op_autosave_writer.target_file = NULL;
        filesystem_autosaveWriterFinishErrorNow();
        return;

    default:
        filesystem_autosaveWriterFinishError();
        return;
    }
}

static const char *filesystem_repairLibraryRoot(void)
{
    /*
     * Resolve the root directory for the active library repair.
     *
     * Inputs: op_repair_library_kind set by the public repair wrapper. Output:
     * product root component used by asyncfatfs LFN open. Keeping this mapping
     * in one helper prevents the sanitizer from accidentally repairing a
     * different namespace than the later `.hcindex` writer.
     */
    return (op_repair_library_kind == FS_NAME_CACHE_KIT)
        ? STORAGE_ROOT_KIT
        : (op_repair_library_kind == FS_NAME_CACHE_SCENE)
            ? STORAGE_ROOT_SCENE
            : (op_repair_library_kind == FS_NAME_CACHE_BANK)
                ? STORAGE_ROOT_BANK
                : NULL;
}

static const char *filesystem_repairInstrumentRoot(void)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntryAt(op_repair_registry_index);

    /*
     * Resolve one registered Instrument subtype directory.
     *
     * Inputs: op_repair_registry_index/type from the blocking Instrument repair
     * loop. Output: the storage directory that owns files for exactly that
     * descriptor type. The sanitizer must never repair all Instrument files in
     * one mixed pass because duplicate stems are isolated by type extension.
     */
    if (!entry || entry->type != op_repair_instrument_type)
        return NULL;
    return entry->storage_directory;
}

static void filesystem_repairNames_tick(void)
{
    const char *name;

    /*
     * Repair one product namespace with bounded SRAM.
     *
     * Phases 0..8 enter the requested parent directory. Phase 20 scans objects
     * until a non-canonical component is found. Phases 30..34 close the scan
     * handle, rename the selected component, sync dirty FAT sectors, and then
     * restart from phase 0 so duplicate resolution observes the physical card.
     * A clean scan closes the parent and finishes successfully.
     */
    switch (op_phase) {
    case 0: /* CHDIR root */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1u;
        return;

    case 1: /* OPEN top-level parent */
        op_file_ready = false;
        op_file = NULL;
        name = (op_repair_scope == FS_REPAIR_SCOPE_LIBRARY)
            ? filesystem_repairLibraryRoot()
            : (op_repair_scope == FS_REPAIR_SCOPE_INSTRUMENT ||
               op_repair_scope == FS_REPAIR_SCOPE_BANK_LOAD)
                ? ((op_repair_scope == FS_REPAIR_SCOPE_INSTRUMENT)
                    ? STORAGE_ROOT_INSTRUMENT : STORAGE_ROOT_BANK)
                : NULL;
        if (!name) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_opendir_lfn(name,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened))
            return;
        op_phase = 2u;
        return;

    case 2: /* WAIT top-level parent */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3u;
        return;

    case 3: /* ENTER top-level parent */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        if (op_repair_scope == FS_REPAIR_SCOPE_LIBRARY) {
            afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
            op_phase = 20u;
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4: /* WAIT top-level close before opening child parent */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (op_repair_scope == FS_REPAIR_SCOPE_INSTRUMENT) {
            name = filesystem_repairInstrumentRoot();
        } else {
            filesystem_makeNumberedDir(
                op_root_open_name,
                op_repair_bank_slot,
                filesystem_cachedLibraryName(FS_NAME_CACHE_BANK,
                                             op_repair_bank_slot));
            name = op_root_open_name;
        }
        if (!name) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (!afatfs_opendir_lfn(name,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened))
            return;
        op_phase = 5u;
        return;

    case 5: /* WAIT child parent */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 6u;
        return;

    case 6: /* ENTER child parent and begin scan */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
        op_phase = 20u;
        return;

    case 20: /* FIND repair candidate */
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
            op_phase = 40u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_close_status = FS_STATUS_DONE;
            op_phase = 40u;
            return;
        }
        op_repair_retry = 0u;
        op_repair_suffix = 0u;
        if (filesystem_repairBuildCandidate()) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_phase = 30u;
        }
        return;
    }

    case 30: /* CLOSE scan handle before rename mutates current directory */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 31u;
        return;

    case 31: /* WAIT scan close */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_phase = 32u;
        return;

    case 32: /* START rename */
        op_rename_done = 0u;
        memset(op_repair_rename_open_name, 0,
               sizeof(op_repair_rename_open_name));
        if (!afatfs_renameObject_lfn(op_repair_old_name,
                                     op_repair_new_name,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     op_repair_rename_open_name,
                                     on_rename_complete))
            return;
        op_phase = 33u;
        return;

    case 33: /* WAIT rename */
        if (!op_rename_done)
            return;
        if (op_repair_rename_open_name[0] == '\0') {
            if (op_repair_retry) {
                if (op_repair_suffix >= 999u) {
                    filesystem_finish(FS_STATUS_ERROR);
                    return;
                }
                op_repair_suffix++;
            } else {
                op_repair_retry = 1u;
                op_repair_suffix = 0u;
            }
            if (op_repair_suffix >= 999u) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            (void)filesystem_repairBuildCandidate();
            op_phase = 32u;
            return;
        }
        op_phase = 34u;
        return;

    case 34: /* SYNC rename before scanning again */
        if (!afatfs_sync())
            return;
        op_phase = 0u;
        return;

    case 40: /* CLOSE clean/error scan */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 41u;
        return;

    case 41: /* WAIT clean/error close */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_phase = 42u;
        return;

    case 42: /* RETURN root before final repair action */
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 43u;
        return;

    case 43: /* HAND OFF repaired Bank child names to the Bank payload reader */
        if (op_close_status == FS_STATUS_DONE &&
            op_repair_scope == FS_REPAIR_SCOPE_BANK_LOAD) {
            /*
             * Continue one Bank Load through its original callback.
             *
             * Inputs: a repaired selected Bank directory and request state
             * captured before the repair. Output: Bank phase zero resumes
             * without publishing an intermediate completion. The former
             * blocking embedded-Kit quarantine is intentionally absent: each
             * selected child validates in the shared Scene reader instead.
             */
            /*
             * Input is the successfully completed Bank-name repair; output is
             * a fresh BANKLOAD deadline before the payload reader becomes the
             * private current operation. Why: phase 43 bypasses the generic
             * filesystem_start() arm. Affiliates: Bank boot load and the
             * repair-name state machine.
             */
            /*
             * DEV_MODE_LOGGING writes operation codes to file for use in
             * debugging. It must never print anything to the screen or
             * otherwise delay operations unnecessarily since logging may be
             * used to assess timing failures in other modules that might
             * otherwise be obscured by screen write delays.
             */
            filesystem_bootLoggingArm("BANKLOAD");
            current_op = FS_INTERNAL_OP_LOAD_BANK;
            op_phase = 0u;
            op_close_status = FS_STATUS_DONE;
            op_file = NULL;
            op_file_ready = false;
            op_close_done = false;
            return;
        }
        filesystem_finish(op_close_status);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* Continue a save's rebuild chain after the physical directory scan.
 *
 * What: starts the same complete slot-ordered index writer used by boot.
 * Why: the scan has now rebuilt every cache row from actual FAT directory
 * entries, so the writer can publish a correct index instead of preserving
 * stale rows from before the save. Inputs: status from the finished scan and
 * op_library_index_rebuild_kind. Output: the original callback remains
 * parked until the index writer and its final flush complete.
 */
static void filesystem_libraryIndexRebuildScanComplete(void)
{
    fs_status_t scan_status = status;
    fs_file_type_t file_type = (op_library_index_rebuild_kind == FS_NAME_CACHE_KIT)
        ? FS_FILE_KIT
        : (op_library_index_rebuild_kind == FS_NAME_CACHE_SCENE)
            ? FS_FILE_SCENE : FS_FILE_BANK;

    if (scan_status != FS_STATUS_DONE) {
        filesystem_completeLibraryIndexRebuild(scan_status);
        return;
    }
    if (!filesystem_start(FS_INTERNAL_OP_CREATE_LIBRARY_INDEX,
                          file_type,
                          0u,
                          filesystem_libraryIndexRebuildWriteComplete)) {
        filesystem_makeNamedErrorCode("Idx", 1u);
        filesystem_completeLibraryIndexRebuild(FS_STATUS_ERROR);
    }
}

/* Publish the original save result after `.hcindex` is durable. */
static void filesystem_libraryIndexRebuildWriteComplete(void)
{
    filesystem_completeLibraryIndexRebuild(status);
}

/*
 * Load one slot-ordered Kit, root Scene, or root Bank `.hcindex` into the
 * shared cache.
 *
 * What: treats each physical line as the matching slot, including blank lines,
 * and copies only the name portion into fs_list_cache_name[slot]. Why: Kit,
 * Scene, and Bank names intentionally exclude their three-digit folder prefix;
 * the loader must preserve empty slots and never compact rows or sort them. The
 * occupancy arrays are rebuilt from non-empty rows so payload loaders can
 * validate a selection without retaining another display-name array.
 * Inputs: op_library_index_kind captured by the public request. Clients: the
 * top-level Load/Save menu after entering Kit, KitMrp, root Scene, or Bank.
 */
static void filesystem_loadLibraryIndex_tick(void)
{
    const char *root = (op_library_index_kind == FS_NAME_CACHE_KIT)
        ? STORAGE_ROOT_KIT
        : (op_library_index_kind == FS_NAME_CACHE_SCENE)
            ? STORAGE_ROOT_SCENE
            : (op_library_index_kind == FS_NAME_CACHE_BANK)
                ? STORAGE_ROOT_BANK
                : NULL;

    switch (op_phase) {
    case 0: /* CHDIR ROOT */
        if (!root || !afatfs_chdir(NULL)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 1u;
        return;

    case 1: /* OPEN ROOT DIRECTORY */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(root,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 2u;
        return;

    case 2: /* WAIT ROOT DIRECTORY */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 3u;
        return;

    case 3: /* ENTER ROOT DIRECTORY */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4u;
        return;

    case 4: /* CLOSE ROOT HANDLE */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5u;
        return;

    case 5: /* WAIT ROOT CLOSE + OPEN INDEX */
        if (!op_close_done)
            return;
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(".hcindex",
                              "r",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 6u;
        return;

    case 6: /* WAIT INDEX OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_phase = 7u;
        return;

    case 7: /* READ ONE ROW PER SLOT */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t st = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (st != STORAGE_STATUS_OK && st != STORAGE_STATUS_WAIT) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 8u;
            return;
        }
        if (line_ready) {
            if (op_item_offset < fs_list_cache_count) {
                memset(fs_list_cache_name[op_item_offset], 0,
                       sizeof(fs_list_cache_name[op_item_offset]));
                if (op_line_buf[0] != '\0')
                    storage_copyDisplayName(fs_list_cache_name[op_item_offset],
                                            op_line_buf);
            }
            if (op_item_offset < FS_LIBRARY_NAME_CACHE_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if (eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 8u;
        }
        return;
    }

    case 8: /* WAIT INDEX CLOSE */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
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
    const char *name = filesystem_librarySlotExists(FS_NAME_CACHE_KIT, slot)
        ? filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, slot) : NULL;

    if (name) {
        filesystem_copyEightCharName(
            loaded_name,
            name);
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
 * Inputs: open_name is the 8.3 alias used by afatfs_fopen(). Outputs: the
 * slot-ordered Kit cache is populated if the alias starts with a valid
 * 000..999 slot number and has some non-empty tail. Slot 000 is a real Kit
 * slot, so the parsed number maps directly to the cache index. Client:
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
    if (filesystem_librarySlotExists(FS_NAME_CACHE_KIT, slot) &&
        !filesystem_displayPrecedesCached(
            display,
            filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, slot))) {
        return;
    }
    memcpy(fs_list_cache_name[slot], display,
           STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
}

/* Record one numbered kit directory discovered during a Kit/ scan.
 *
 * Inputs: display_name is the LFN or short name visible to the user; open_name
 * is the FAT short name that afatfs_fopen() can open later. Outputs: the
 * slot-ordered Kit cache is updated. Invalid visible names outside the
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

    if (filesystem_librarySlotExists(FS_NAME_CACHE_KIT, slot) &&
        !filesystem_displayPrecedesCached(
            display,
            filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, slot))) {
        return;
    }
    memcpy(fs_list_cache_name[slot], display,
           STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
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
     * cache index. This mirrors the Kit fallback; Scene and Kit library
     * occupancy are independent.
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
    if (filesystem_librarySlotExists(FS_NAME_CACHE_SCENE, slot) &&
        !filesystem_displayPrecedesCached(
            display,
            filesystem_cachedLibraryName(FS_NAME_CACHE_SCENE, slot))) {
        return;
    }
    memcpy(fs_list_cache_name[slot], display,
           STORAGE_SCENE_DISPLAY_NAME_LEN + 1u);
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
    if (filesystem_librarySlotExists(FS_NAME_CACHE_SCENE, slot) &&
        !filesystem_displayPrecedesCached(
            display,
            filesystem_cachedLibraryName(FS_NAME_CACHE_SCENE, slot))) {
        return;
    }
    memcpy(fs_list_cache_name[slot], display,
           STORAGE_SCENE_DISPLAY_NAME_LEN + 1u);
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
     * afatfs_opendir_lfn() calls. Output: the shared root-Bank cache row only.
     * Bank-local Scene children are intentionally not cached here because
     * their two-digit namespace belongs to one selected Bank folder, not the
     * root library. A non-blank shared row is the complete occupancy record.
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
    if (filesystem_librarySlotExists(FS_NAME_CACHE_BANK, slot) &&
        !filesystem_displayPrecedesCached(
            display,
            filesystem_cachedLibraryName(FS_NAME_CACHE_BANK, slot))) {
        return;
    }
    memcpy(fs_list_cache_name[slot], display,
           STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
    (void)open_name;
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
     * shared root-Bank cache row for that slot. Names still come only from the
     * directory; bankset.bcg is never queried for identity. The returned alias
     * is deliberately discarded because the shared cache stores only display
     * names; Bank Load reconstructs the visible `NNN Name` key when needed.
     */
    (void)open_name;
    if (!storage_parseNumberedFolder(display_name, &slot, display) ||
        slot >= STORAGE_BANK_MAX_SLOTS) {
        return;
    }
    display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    memcpy(fs_list_cache_name[slot], display,
           STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
    (void)open_name;
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

/* Return the active browser count for one Instrument type.
 *
 * Inputs: registered Instrument type. Output: the count owned by the typed
 * general name-index cache that currently supplies the Menu browser. Clients:
 * request validation, index creation, and every loader path that must agree
 * with filesystem_instrumentCount().
 */
static uint16_t filesystem_cachedInstrumentCount(instrument_type_t type)
{
    if (fs_list_cache_kind != FS_NAME_CACHE_INSTRUMENT ||
        type >= INSTRUMENT_TYPE_UNKNOWN || type != fs_list_cache_type)
        return 0u;
    return (fs_list_cache_count > FS_LIBRARY_NAME_CACHE_MAX)
        ? FS_LIBRARY_NAME_CACHE_MAX
        : fs_list_cache_count;
}

/* Return the display name paired with one active browser entry.
 *
 * Inputs: registered type and zero-based browser index. Output: the
 * filesystem-owned display stem for that cache entry, or NULL when the index
 * is outside the active cache. Clients: Instrument Load filename construction
 * and staged completion metadata. Keeping this selection in one helper avoids
 * accepting an index from one cache and reading its name from another.
 */
static const char *filesystem_cachedInstrumentName(instrument_type_t type,
                                                   uint16_t index)
{
    if (index >= filesystem_cachedInstrumentCount(type))
        return NULL;
    return fs_list_cache_name[index];
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

static void filesystem_makeInstrumentTemporaryFilename(
    char destination[AFATFS_LONG_FILENAME_MAX + 1u],
    instrument_type_t type)
{
    const char *extension = storage_instrumentTypeExtension(type);
    uint8_t pos = 0u;

    /*
     * Build the hidden reversible Instrument Load filename.
     *
     * Inputs: the active typed Instrument family. Output: `.hctmp.<ext>` in
     * the existing operation filename scratch. Why: this file is the durable
     * `kit` source while a user browses other files, replacing the former
     * second Instrument image in staging. The dot-prefixed component is
     * explicitly excluded from typed `.hcindex` scans below.
     * Affiliates: temporary save/load requests and filesystem_recordInstrumentFile().
     */
    if (!extension)
        extension = "drm";
    memset(destination, 0, AFATFS_LONG_FILENAME_MAX + 1u);
    memcpy(destination, ".hctmp.", 7u);
    pos = 7u;
    while (*extension != '\0' &&
           pos + 1u < AFATFS_LONG_FILENAME_MAX + 1u) {
        destination[pos++] = *extension++;
    }
    destination[pos] = '\0';
}

static uint8_t filesystem_instrumentCacheStemMatches(
        instrument_type_t type,
        uint16_t index,
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
        fs_list_cache_kind == FS_NAME_CACHE_INSTRUMENT &&
        type < INSTRUMENT_TYPE_UNKNOWN &&
        type == fs_list_cache_type &&
        index < fs_list_cache_count &&
        fat_compareDisplayName(fs_list_cache_name[index],
                               display_stem,
                               false) == 0);
}

static void filesystem_recordInstrumentFile(const char *display_name,
                                            const char *open_name)
{
    instrument_type_t type =
        filesystem_instrumentTypeFromFilename(display_name);
    uint16_t count;
    uint16_t pos;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Insert one Instrument/ file into the shared cache for its active type.
     *
     * Inputs: display_name from asyncfatfs object metadata and open_name as the
     * asyncfatfs-openable short filename. Output: the general typed cache stores
     * the display stem in alphanumeric order. Classification prefers the visible
     * filename first; the alias fallback keeps legacy alias-only media loadable,
     * but the alias is not retained after classification.
     */
    if (type == INSTRUMENT_TYPE_UNKNOWN)
        type = filesystem_instrumentTypeFromFilename(open_name);
    if (type == INSTRUMENT_TYPE_UNKNOWN ||
        type >= INSTRUMENT_TYPE_UNKNOWN)
        return;
    /*
     * Hide the reversible Instrument Load file from every typed browser index.
     *
     * Inputs: one on-card Instrument subdirectory entry. Output: a hidden
     * `.hctmp.<ext>` is ignored before its stem could consume a pool row.
     * Why: the file is an implementation-owned `kit` restore source, never a
     * user-selectable Instrument. Affiliates: temporary save/load requests and
     * Menu's three-digit pool cursor.
     */
    if (strncmp(display_name, ".hctmp.", 7u) == 0)
        return;
    if (fs_list_cache_kind != FS_NAME_CACHE_INSTRUMENT ||
        fs_list_cache_type != type)
        filesystem_clearNameCacheStorage();
    fs_list_cache_kind = FS_NAME_CACHE_INSTRUMENT;
    fs_list_cache_type = type;
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
     * Outputs: the shared cache either keeps its existing representative,
     * replaces it with an earlier-sorting variant, or inserts a new product
     * object.
     *
     * Affiliates/clients: filesystem_requestScanInstruments(), nested
     * Instrument Load, root Instrument Save cache update,
     * fat_compareDisplayNameCasefoldThenCase().
     */
    for (pos = 0u; pos < fs_list_cache_count; pos++) {
        if (!filesystem_instrumentCacheStemMatches(type, pos, display))
            continue;
        if (filesystem_compareInstrumentDisplayName(
                display,
                fs_list_cache_name[pos]) < 0) {
            memcpy(fs_list_cache_name[pos], display,
                   sizeof(fs_list_cache_name[pos]));
        }
        return;
    }

    count = fs_list_cache_count;
    if (count >= FS_LIBRARY_NAME_CACHE_MAX)
        return;

    pos = count;
    while (pos > 0u &&
           filesystem_compareInstrumentDisplayName(
               fs_list_cache_name[pos - 1u], display) > 0) {
        memcpy(fs_list_cache_name[pos],
               fs_list_cache_name[pos - 1u],
               sizeof(fs_list_cache_name[pos]));
        pos--;
    }
    memcpy(fs_list_cache_name[pos], display,
           sizeof(fs_list_cache_name[pos]));
    fs_list_cache_count = (uint16_t)(count + 1u);
}

static uint8_t filesystem_repairDisplayExact(const char *a, const char *b)
{
    /*
     * Compare concrete FAT display components, not product identities.
     *
     * The sanitizer only skips a rename when the physical component already
     * equals the canonical component byte-for-byte. Casefolded equality is not
     * enough here because the repair pass is also responsible for publishing
     * the firmware's exact canonical spelling before `.hcindex` is written.
     */
    return (uint8_t)(a && b && strcmp(a, b) == 0);
}

static void filesystem_makeSuffixedDisplay(
    char dst[STORAGE_KIT_DISPLAY_NAME_LEN + 1u],
    const char base[STORAGE_KIT_DISPLAY_NAME_LEN + 1u],
    uint8_t use_suffix,
    uint16_t suffix)
{
    uint8_t width = 1u;
    uint16_t n = suffix;

    /*
     * Generate the duplicate-resolved eight-cell display stem.
     *
     * Inputs: the unsuffixed truncated display and a decimal suffix candidate.
     * Output: eight display cells plus NUL. The suffix overwrites the shortest
     * possible tail, so "SnareDru" collides into "SnareDr0", then "SnareDr1",
     * and eventually wider tails such as "SnareD10". This keeps product names
     * bounded without needing a directory-wide SRAM duplicate table.
     */
    memcpy(dst, base, STORAGE_KIT_DISPLAY_NAME_LEN);
    dst[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    if (!use_suffix)
        return;
    while (n >= 10u && width < STORAGE_KIT_DISPLAY_NAME_LEN) {
        n = (uint16_t)(n / 10u);
        width++;
    }
    if (width > STORAGE_KIT_DISPLAY_NAME_LEN)
        width = STORAGE_KIT_DISPLAY_NAME_LEN;
    n = suffix;
    for (uint8_t i = 0u; i < width; i++) {
        uint8_t pos = (uint8_t)(STORAGE_KIT_DISPLAY_NAME_LEN - 1u - i);
        dst[pos] = (char)('0' + (n % 10u));
        n = (uint16_t)(n / 10u);
    }
}

static void filesystem_makeCanonicalInstrumentName(char *dst,
                                                   uint16_t capacity,
                                                   const char display[9],
                                                   instrument_type_t type)
{
    const char *ext = storage_instrumentTypeExtension(
        (storage_instrument_type_t)type);
    int8_t last = -1;
    uint8_t out = 0u;

    /*
     * Format the canonical root Instrument filename.
     *
     * Inputs: eight display cells and the registered type. Output:
     * `<stem>.<ext>` with trailing display spaces trimmed. This is the key
     * shape that lets future resident state keep one display name instead of a
     * separate long filename stem.
     */
    if (!dst || capacity == 0u)
        return;
    dst[0] = '\0';
    if (!ext)
        ext = "drm";
    for (uint8_t i = 0u; i < STORAGE_KIT_DISPLAY_NAME_LEN; i++) {
        if (display[i] != ' ' && display[i] != '\0')
            last = (int8_t)i;
    }
    if (last < 0) {
        if (capacity > 5u)
            strcpy(dst, "inst");
        out = 4u;
    } else {
        while (out <= (uint8_t)last && out < (capacity - 1u)) {
            dst[out] = display[out];
            out++;
        }
        dst[out] = '\0';
    }
    if ((uint16_t)(out + 1u) < capacity)
        dst[out++] = '.';
    while (*ext && (uint16_t)(out + 1u) < capacity)
        dst[out++] = *ext++;
    dst[out] = '\0';
}

static void filesystem_appendTrimmedDisplay(char *dst,
                                            uint16_t capacity,
                                            uint8_t *offset,
                                            const char display[9])
{
    int8_t last = -1;

    /*
     * Append display cells as a FAT component, not as an LCD field.
     *
     * The resident/browser display strings are fixed eight-cell, space-padded
     * values. FAT directory components are not: asyncfatfs reports normal host
     * names without trailing spaces. Repair comparisons must therefore format
     * canonical directory names with trailing display spaces trimmed, or the
     * boot sanitizer can keep renaming an already-valid object forever.
     */
    if (!dst || !offset || capacity == 0u)
        return;
    for (uint8_t i = 0u; i < STORAGE_KIT_DISPLAY_NAME_LEN; i++) {
        if (display[i] != ' ' && display[i] != '\0')
            last = (int8_t)i;
    }
    if (last < 0)
        return;
    for (uint8_t i = 0u;
         i <= (uint8_t)last && (uint16_t)(*offset + 1u) < capacity;
         i++) {
        dst[*offset] = display[i];
        *offset = (uint8_t)(*offset + 1u);
    }
    dst[*offset] = '\0';
}

static void filesystem_makeCanonicalNumberedDir(char *dst,
                                                uint16_t capacity,
                                                uint16_t slot,
                                                const char display[9])
{
    uint8_t offset = 4u;

    /*
     * Format a root numbered directory for repair comparisons.
     *
     * Inputs: direct slot and eight display cells. Output: `NNN Name` with the
     * display tail trimmed for FAT. The normal save helper keeps eight cells
     * because it is fed by UI storage; the sanitizer needs a physical component
     * that will compare equal to what asyncfatfs reports after the rename.
     */
    if (!dst || capacity < 5u)
        return;
    dst[0] = (char)('0' + ((slot / 100u) % 10u));
    dst[1] = (char)('0' + ((slot / 10u) % 10u));
    dst[2] = (char)('0' + (slot % 10u));
    dst[3] = ' ';
    dst[4] = '\0';
    filesystem_appendTrimmedDisplay(dst, capacity, &offset, display);
}

static void filesystem_makeCanonicalBankSceneDir(char *dst,
                                                 uint16_t capacity,
                                                 uint8_t slot,
                                                 const char display[9])
{
    uint8_t offset = 3u;

    /*
     * Format a Bank-local Scene child for repair comparisons.
     *
     * Inputs: direct child slot and eight display cells. Output: `SS Name`
     * with trailing display spaces trimmed. This mirrors the two-digit product
     * namespace while avoiding padded FAT names that would never compare equal
     * on the next physical scan.
     */
    if (!dst || capacity < 4u)
        return;
    dst[0] = (char)('0' + (slot / 10u));
    dst[1] = (char)('0' + (slot % 10u));
    dst[2] = ' ';
    dst[3] = '\0';
    filesystem_appendTrimmedDisplay(dst, capacity, &offset, display);
}

static uint8_t filesystem_repairBuildCandidate(void)
{
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
    char suffixed[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
    uint16_t slot;
    uint8_t child_slot;
    instrument_type_t type;

    /*
     * Classify one scanned object and, when needed, prepare one rename.
     *
     * Inputs: op_object from the current parent iterator plus the active repair
     * scope. Outputs: op_repair_old_name/op_repair_new_name hold exactly one
     * old/new display component. The helper does not mutate FAT and does not
     * remember any other directory entries; failed duplicate targets are
     * handled by retrying the rename with a suffixed display stem.
     */
    op_repair_old_name[0] = '\0';
    op_repair_new_name[0] = '\0';
    if (op_repair_scope == FS_REPAIR_SCOPE_INSTRUMENT) {
        if (op_object.id.kind != AFATFS_OBJECT_FILE)
            return 0u;
        /*
         * Reserve the hidden reversible Load source before filename repair.
         *
         * Input: one file discovered in an Instrument type directory during
         * boot's canonical-name pass. Output: `.hctmp.<ext>` produces no
         * rename candidate. Why: this product-owned file is deliberately
         * dot-prefixed and therefore cannot satisfy the normal eight-character
         * stem policy; repairing it would rename or repeatedly collide with a
         * user Instrument before boot can proceed. Affiliates: the matching
         * typed-index exclusion in filesystem_recordInstrumentFile() and Menu
         * temp save/load requests.
         */
        if (strncmp(op_object.id.displayName, ".hctmp.", 7u) == 0)
            return 0u;
        type = filesystem_instrumentTypeFromFilename(op_object.id.displayName);
        if (type == INSTRUMENT_TYPE_UNKNOWN)
            type = filesystem_instrumentTypeFromFilename(op_object.id.shortName);
        if (type != op_repair_instrument_type)
            return 0u;
        filesystem_copyInstrumentStemDisplay(op_repair_base_display,
                                             op_object.id.displayName);
        filesystem_makeSuffixedDisplay(suffixed,
                                       op_repair_base_display,
                                       op_repair_retry,
                                       op_repair_suffix);
        filesystem_makeCanonicalInstrumentName(op_repair_new_name,
                                               sizeof(op_repair_new_name),
                                               suffixed,
                                               type);
    } else if (op_repair_scope == FS_REPAIR_SCOPE_BANK_LOAD) {
        if (op_object.id.kind != AFATFS_OBJECT_DIRECTORY ||
            !storage_parseBankSceneFolder(op_object.id.displayName,
                                          &child_slot,
                                          display)) {
            return 0u;
        }
        display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
        memcpy(op_repair_base_display, display,
               STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
        filesystem_makeSuffixedDisplay(suffixed,
                                       op_repair_base_display,
                                       op_repair_retry,
                                       op_repair_suffix);
        filesystem_makeCanonicalBankSceneDir(op_repair_new_name,
                                             sizeof(op_repair_new_name),
                                             child_slot,
                                             suffixed);
    } else {
        if (op_object.id.kind != AFATFS_OBJECT_DIRECTORY ||
            !storage_parseNumberedFolder(op_object.id.displayName,
                                         &slot,
                                         display)) {
            return 0u;
        }
        if ((op_repair_library_kind == FS_NAME_CACHE_KIT &&
             slot >= STORAGE_KIT_MAX_SLOTS) ||
            (op_repair_library_kind == FS_NAME_CACHE_SCENE &&
             slot >= STORAGE_SCENE_MAX_SLOTS) ||
            (op_repair_library_kind == FS_NAME_CACHE_BANK &&
             slot >= STORAGE_BANK_MAX_SLOTS)) {
            return 0u;
        }
        display[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
        memcpy(op_repair_base_display, display,
               STORAGE_KIT_DISPLAY_NAME_LEN + 1u);
        filesystem_makeSuffixedDisplay(suffixed,
                                       op_repair_base_display,
                                       op_repair_retry,
                                       op_repair_suffix);
        filesystem_makeCanonicalNumberedDir(op_repair_new_name,
                                            sizeof(op_repair_new_name),
                                            slot,
                                            suffixed);
    }
    if (filesystem_repairDisplayExact(op_object.id.displayName,
                                      op_repair_new_name) &&
        !op_repair_retry) {
        op_repair_new_name[0] = '\0';
        return 0u;
    }
    strncpy(op_repair_old_name,
            op_object.id.displayName,
            sizeof(op_repair_old_name) - 1u);
    op_repair_old_name[sizeof(op_repair_old_name) - 1u] = '\0';
    return 1u;
}

static void filesystem_updateInstrumentCacheAfterSave(const char *display_name,
                                                      const char *open_name)
{
    instrument_type_t type =
        filesystem_instrumentTypeFromFilename(display_name);
    uint16_t i;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Refresh browser cache after case-insensitive overwrite.
     *
     * What: Removes every general-cache row whose display stem matches the
     * saved target under case-insensitive comparison, then inserts the one
     * returned by the completed save.
     *
     * Why: The SD card has already collapsed same-casefold physical files into
     * one visible object. The in-RAM browser cache must mirror that immediately
     * so the next nested load cannot select a stale duplicate alias.
     *
     * Inputs: display_name is the target case just written; open_name is used
     * only as a fallback for classifying legacy alias-only metadata. Outputs:
     * the general typed Instrument cache contains one row for the saved object.
     *
     * Affiliates/clients: filesystem_saveInstrument_tick(),
     * filesystem_recordInstrumentFile(), nested Instrument Load.
     */
    if (type == INSTRUMENT_TYPE_UNKNOWN)
        type = filesystem_instrumentTypeFromFilename(open_name);
    if (type == INSTRUMENT_TYPE_UNKNOWN || type >= INSTRUMENT_TYPE_UNKNOWN)
        return;
    if (fs_list_cache_kind != FS_NAME_CACHE_INSTRUMENT ||
        fs_list_cache_type != type)
        filesystem_clearNameCacheStorage();
    fs_list_cache_kind = FS_NAME_CACHE_INSTRUMENT;
    fs_list_cache_type = type;

    filesystem_copyInstrumentStemDisplay(display, display_name);

    for (i = 0u; i < fs_list_cache_count; ) {
        uint8_t remove = 0u;

        if (fat_compareDisplayName(fs_list_cache_name[i],
                                   display,
                                   false) == 0) {
            remove = 1u;
        }

        if (remove) {
            uint16_t j;
            for (j = i; (uint16_t)(j + 1u) < fs_list_cache_count; j++) {
                memcpy(fs_list_cache_name[j],
                       fs_list_cache_name[j + 1u],
                       sizeof(fs_list_cache_name[j]));
            }
            fs_list_cache_count--;
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
** Inputs: op_slot is the zero-based internal kit number; the active shared
** Kit name cache must have been populated by the `.hcindex` load request;
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
    case 0: /* VALIDATE CAPTURED KEY + CHDIR ROOT */
        if (op_slot >= STORAGE_KIT_MAX_SLOTS ||
            op_scene_display_name[0] == '\0') {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /*
         * Use the request-time Kit display key, not the index cache.
         *
         * Why: staging is independent from the cache, but the captured key
         * makes this operation immutable despite later cache transitions.
         * Inputs: the validated cache row copied into
         * op_scene_display_name by the request helper. Output: the visible
         * preset name and later `NNN Name` directory key remain stable through
         * staging. Affiliates: filesystem_requestLoadKitForScenes(),
         * filesystem_requestLoadKitMorphForScenes(), and phase 6 below.
         */
        memcpy(preset_currentName,
               op_scene_display_name, 8u);
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
        filesystem_makeNumberedDir(
            op_root_open_name,
            op_slot,
            op_scene_display_name);
        /*
         * The selected Kit identity was captured from the slot-ordered
         * `.hcindex` before its union storage became the Kit stage. Open the
         * generated visible `NNN Name` component so a menu entered from an
         * index load does not depend on a stale or missing per-slot short-alias
         * cache. asyncfatfs returns the physical alias internally if a later
         * phase needs it, but the browser contract is the LFN key.
         */
        if (!afatfs_opendir_lfn(op_root_open_name,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
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
                uint8_t identity_slot;

                /*
                 * Publish the validated Kit directory/member identities into
                 * the one operation-scoped block before resident audio commit.
                 *
                 * Inputs: request-stable captured Kit name and transient
                 * kitset `file=` fields. Outputs: HCNAMES update rows; no name
                 * enters kit_t. The index cache cannot be consulted here
                 * because it was deliberately reused as this Kit stage.
                 * Affiliates: filesystem_cacheCurrentResidentKitNames() and
                 * Menu's deferred Kit/Instrument HCNAMES flush.
                 */
                filesystem_setIdentityName(
                    FS_IDENTITY_KIT_ROW,
                    op_scene_display_name);
                for (identity_slot = 0u;
                     identity_slot < STORAGE_KIT_SLOT_COUNT;
                     identity_slot++) {
                    filesystem_setIdentityName(
                        (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 + identity_slot),
                        op_kitset.instrument_file[identity_slot]);
                }
                for (scene_index = 0u;
                     scene_index < SCENE_COUNT && scene_index < 16u;
                     scene_index++) {
                    if ((op_kit_load_scene_mask &
                         (uint16_t)(1u << scene_index)) != 0u) {
                        scene_t *target_scene = scene_get(scene_index);
                        if (target_scene) {
                            target_scene->kit = op_staged_kit;
                        }
                    }
                }
                memcpy(preset_currentName,
                       op_scene_display_name, 8u);
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
** the separate Scene settings/Kit stage, validates that non-Pattern
** payload, commits it to final Scene SRAM, then parses the `.pat` bridge
** directly into the final PatternSet. The first direct destination is mirrored
** to any other selected destination after a successful Pattern read.
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
    case 0: /* VALIDATE CAPTURED KEY + INIT STAGING + CHDIR ROOT */
        if (op_slot >= STORAGE_SCENE_MAX_SLOTS ||
            op_scene_display_name[0] == '\0') {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        filesystem_initSceneStage(&fs_stage_workspace.scene_stage);
        memcpy(preset_currentName,
               op_scene_display_name, 8u);
        /*
         * The selected Scene key is retained in existing operation scratch
         * while the dedicated stage is initialized above. Inputs: request-time
         * slot/name capture. Output: identity and both visible directory opens
         * use the same selected row without a second name copy.
         * Affiliates: filesystem_requestLoadSceneForScenes(), phases 6/39,
         * and the targeted Scene HCNAMES update after commit.
         */
        filesystem_setIdentityName(FS_IDENTITY_SCENE_ROW,
                                   op_scene_display_name);
        /*
         * A root Scene request owns exactly one child scan, but it shares the
         * same scratch fields as Bank Load. Reset them through the common
         * helper so root and Bank-local Scene payloads have identical discovery
         * semantics and later additions cannot clear only one of the fields.
         */
        filesystem_resetSceneLoadChildDiscovery();
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
                                   op_scene_display_name);
        /*
         * Open root Scene folders by the visible numbered component.
         *
         * Inputs: op_slot plus the request-captured Scene row.
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
                                op_scene_root_open_name,
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
        if (filesystem_bankPayloadDetailActive()) {
            /* This shared loader is serving a selected Bank child; record the
             * metadata/Kit file family without changing root Scene behaviour. */
            filesystem_bootLoggingSetBankSceneDetail('K');
        }
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
                                           &fs_stage_workspace.scene_stage.settings,
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('K');
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('K');
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('K');
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
                                          &fs_stage_workspace.scene_stage.kit);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 25;
            }
            return;
        }
        if (eof) {
            st = storage_kitsetFinalize(&op_kitset);
            if (st == STORAGE_STATUS_OK &&
                current_op == FS_INTERNAL_OP_LOAD_BANK) {
                uint8_t member_slot;

                /*
                 * Validate Bank-member provenance before any child commit.
                 *
                 * Inputs: the six declared `file=` names from the shared
                 * kitset parser. Output: a Bank child with a basename that
                 * cannot be reconstructed from the eight-cell resident-name
                 * register fails through the normal foreground state machine.
                 * Why: this replaces the former blocking all-Bank quarantine;
                 * only the selected child is examined, declared instrument
                 * types remain fully flexible, and no runtime load mutates
                 * unrelated files on the card.
                 */
                for (member_slot = 0u;
                     member_slot < STORAGE_KIT_SLOT_COUNT;
                     member_slot++) {
                    if (!filesystem_kitMemberNameIsCanonical(
                            op_kitset.instrument_file[member_slot])) {
                        st = STORAGE_STATUS_INVALID_FORMAT;
                        break;
                    }
                }
            }
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
                 * shared scene-stage settings.audio_out[] in persisted route
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
                        fs_stage_workspace.scene_stage.settings.audio_out[slot] =
                            (route <= 5u)
                                ? route
                                : filesystem_defaultVoiceAudioOut(slot);
                    }
                }
                /*
                 * Publish embedded Kit/member identities outside the staged
                 * audio image.
                 *
                 * Why: scene_t/kit_t intentionally contain no name or key
                 * fields. Inputs: discovered `Kit <name>` component and the
                 * transient kitset file rows. Outputs: one identity block used
                 * by the later targeted HCNAMES write and save key formatter.
                 * Affiliates: Scene commit, menu session, HCNAMES writer.
                 */
                {
                    uint8_t identity_slot;
                    filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                               op_scene_child_display_name);
                    for (identity_slot = 0u;
                         identity_slot < STORAGE_KIT_SLOT_COUNT;
                         identity_slot++) {
                        filesystem_setIdentityName(
                            (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 +
                                      identity_slot),
                            op_kitset.instrument_file[identity_slot]);
                    }
                }
                op_close_status = FS_STATUS_DONE;
            } else {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
            }
            op_phase = 25;
        }
        return;

    case 25: /* CLOSE embedded kitset.kcg */
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('K');
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
            &fs_stage_workspace.scene_stage.kit.instruments[op_instrument_slot],
            op_kitset.instrument_type[op_instrument_slot]);
        op_line_len = 0u;
        if (filesystem_bankPayloadDetailActive()) {
            /* Record the selected child's Instrument sequence before starting
             * its open; logging cannot advance the slot or commit staging. */
            filesystem_bootLoggingSetBankSceneDetail('I');
        }
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
                                             &fs_stage_workspace.scene_stage.kit.instruments[
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
                        &fs_stage_workspace.scene_stage.kit.instruments[op_instrument_slot]);
                }
                op_close_status = FS_STATUS_DONE;
            }
            op_phase = 30;
        }
        return;

    case 30: /* CLOSE embedded instrument */
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('I');
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

    case 33: /* COMMIT validated non-Pattern Scene, then reenter for Pattern */
        /*
         * The non-Pattern payload has now passed sceneset, Kit, and Instrument
         * validation. Commit it before Pattern I/O by design: Pattern is the
         * separately redesigned, non-atomic phase and is read directly into
         * final Scene SRAM below.
         */
        filesystem_commitSceneStage();
        if (current_op == FS_INTERNAL_OP_LOAD_BANK) {
            afatfsOperationStatus_e ast;

            if (op_bank_payload_active) {
                /* Pattern/Effects and the return to Bank are one final
                 * per-child I/O family; this label has no commit side effect. */
                filesystem_bootLoggingSetBankSceneDetail('P');
            }
            ast = afatfs_chdirParent();

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
                                   op_scene_display_name);
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
                                op_scene_root_open_name,
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('P');
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

    case 46: /* PROBE text-only v3/v2/v1 pattern file */
    {
        uint32_t n;

        /*
         * Scene patterns now have two accepted wire shapes.
         *
         * New Scene/Bank-local Scene Save writes text beginning with "format=".
         * Version 1 is an empty placeholder, v2 imports active bits only, and
         * v3 is the emitted hex bitmap. Binary Step streams are rejected:
         * accepting them would require retired Step/length/automation storage.
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
        (void)n;
        filesystem_setPresetNameInvalid();
        op_close_status = FS_STATUS_ERROR;
        op_phase = 54;
        return;
    }

#if 0 /* Retired binary Step reader phases. */
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
            step = filesystem_patternSetStepPtr(filesystem_directPatternTarget(),
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
            main_steps = filesystem_patternSetMainPtr(filesystem_directPatternTarget(),
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
            setting = filesystem_patternSetSettingPtr(filesystem_directPatternTarget(),
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
            lr = filesystem_patternSetLengthPtr(filesystem_directPatternTarget(),
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
            lr = filesystem_patternSetLengthPtr(filesystem_directPatternTarget(),
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
            lr = filesystem_patternSetLengthPtr(filesystem_directPatternTarget(),
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

#endif
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
                                              filesystem_directPatternTarget());
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
             * into the direct final Scene PatternSet. Output: success accepts either a
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('P');
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('P');
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
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('P');
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

    case 61: /* MIRROR direct Pattern to additional selected Scene slots */
    {
        uint8_t scene_index;

        /*
         * Complete the direct Pattern fan-out without staging PatternSet.
         *
         * Inputs are the directly parsed first destination PatternSet and the
         * request-time destination mask. General settings and Kit were already
         * committed in phase 33. Output mirrors only Pattern data to additional
         * destinations; no Pattern payload has occupied the typed stage.
         *
         * Scene identity remains outside the copied payload because sceneset.scg
         * never stores its own name. Inputs: op_scene_display_name was captured
         * from the selected root/Bank directory, while
         * op_scene_child_display_name was copied into the staged embedded Kit
         * after validation. Output: SceneData receives only playable data;
         * root Scene Load later publishes the one directory name to selected
         * HCNAMES rows, and Bank Load overlays only its selected child block.
         * This keeps mask-unselected Scene data and names paired and unchanged.
         */
        PatternSet *direct = filesystem_directPatternTarget();
        for (scene_index = 0u;
             scene_index < SCENE_COUNT && scene_index < 16u;
             scene_index++) {
            if ((op_scene_load_scene_mask &
                 (uint16_t)(1u << scene_index)) != 0u) {
                scene_t *target = scene_get(scene_index);
                if (target && direct && &target->pattern != direct)
                    target->pattern = *direct;
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
            filesystem_cacheCurrentBankSceneNameBlock(op_bank_child_cursor);
        }
        op_close_status = FS_STATUS_DONE;
        op_phase = 72;
        return;
    }

    case 72: /* RETURN ROOT + FINISH */
        if (filesystem_bankPayloadDetailActive())
            filesystem_bootLoggingSetBankSceneDetail('P');
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
        if (op_close_status == FS_STATUS_DONE &&
            current_op == FS_INTERNAL_OP_LOAD_SCENE) {
            /*
             * Continue a successful root Scene Load as its targeted HCNAMES
             * publication. Inputs: op_scene_load_scene_mask and the parsed
             * root directory display name. Output: the generic register
             * updater preserves every unrelated row, replaces only destination
             * Scene rows, and flushes HCNAMES before the original Preset
             * callback. It does not scan or rewrite the unchanged Scene
             * namespace; an explicit runtime command reloads `/Scene/.hcindex`
             * only after the shared DSP apply. Bank delegation deliberately
             * skips this branch because Bank owns its selected-row overlay and
             * one final register write.
             */
            filesystem_prepareResidentNamesCache();
            /*
             * This internal handoff bypasses filesystem_start().
             *
             * Input is a successfully loaded root Scene; output is a fresh
             * HCNAMES diagnostic deadline before the register update owns the
             * facade. Why: otherwise a stall would retain SCNELOAD even though
             * payload loading had completed. Affiliate: the shared resident
             * names state machine.
             */
            /*
             * DEV_MODE_LOGGING writes operation codes to file for use in
             * debugging. It must never print anything to the screen or
             * otherwise delay operations unnecessarily since logging may be
             * used to assess timing failures in other modules that might
             * otherwise be obscured by screen write delays.
             */
            filesystem_bootLoggingArm("HCNAMES ");
            current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE;
            op_phase = 0u;
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
    fs_hcnames_probe_result_t probe_result;

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
            !filesystem_librarySlotExists(FS_NAME_CACHE_BANK, op_slot)) {
            filesystem_setPresetNameEmpty();
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        /*
         * Replace the Bank browser cache with the complete resident register
         * only after request validation and Bank-name repair have consumed the
         * browser row. Inputs: selected Bank display retained in
         * op_bank_display_name and mask captured by filesystem_requestLoadBank.
         * Output: a 129-row HCNAMES image whose unselected Scene blocks remain
         * untouched while selected Bank children overlay their rows at commit.
         * The final writer restores `/Bank/.hcindex`, so this cache borrowing
         * adds no persistent SRAM allocation or browser-state ambiguity.
         */
        filesystem_prepareResidentNamesCache();
        /* Record the HCNAMES preload before its wait-capable root/open path.
         * This changes only the retained detail label; it never restarts the
         * enclosing BANKLOAD deadline or changes missing-register semantics. */
        filesystem_bootLoggingSetDetail("BKHCREAD");
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 80u;
        return;

    case 80: /* WAIT Bank-load HCNAMES OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            /*
             * A NULL HCNAMES read is ambiguous: it may describe a first-use
             * card, but it may also be a failed lookup or device operation.
             * Before Bank can continue with its blank cache and eventually
             * open a create-capable writer, prove root absence with the
             * bounded read-only scan.  The new detail identifies the scan's
             * root-open boundary without rearming or extending BANKLOAD.
             */
            filesystem_bootLoggingSetDetail("BKHCROOT");
            filesystem_hcnamesProbeBegin();
            op_phase = 87u;
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        filesystem_bootLoggingSetDetail("BKHCREAD");
        op_phase = 81u;
        return;

    case 81: /* READ HCNAMES REGISTER BEFORE MASKED OVERLAY */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t read_status = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            op_close_status = FS_STATUS_ERROR;
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 82u;
            return;
        }
        if (line_ready) {
            filesystem_cacheResidentName(op_item_offset, op_line_buf);
            if (op_item_offset < UINT16_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if (eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 82u;
        }
        return;
    }

    case 82: /* CLOSE PRELOAD, THEN START NORMAL BANK READER */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 1u;
        return;

    case 87: /* READ-ONLY ROOT PROOF AFTER A NULL HCNAMES READ */
        /* Once the helper owns an established root iterator, distinguish a
         * scan stall from the preceding direct HCNAMES read in bootlog.bin.
         * SetDetail() only replaces the retained eight-byte code; it cannot
         * reset the enclosing BANKLOAD deadline. */
        if (filesystem_hcnamesProbeIsScanning())
            filesystem_bootLoggingSetDetail("BKHCSCAN");
        probe_result = filesystem_hcnamesProbe_tick();
        if (probe_result == FS_HCNAMES_PROBE_IN_PROGRESS)
            return;
        if (probe_result == FS_HCNAMES_PROBE_ERROR) {
            /* Finder, root-open, or close failure is not file absence.  Stop
             * before Bank's final HCNAMES writer can allocate a second entry. */
            filesystem_makeNamedErrorCode("BKHprb", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (probe_result == FS_HCNAMES_PROBE_DUPLICATE) {
            /* Preserve both physical entries for forensic recovery.  No boot
             * policy chooses a winner, rewrites one, or removes either copy. */
            filesystem_bootLoggingSetDetail("BKHCDUP ");
            filesystem_makeNamedErrorCode("BKHdup", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        if (probe_result == FS_HCNAMES_PROBE_PRESENT) {
            /* A single folded match must be opened once more through the
             * ordinary reader.  A second NULL fails in phase 89; this cannot
             * cycle back into the absence proof or authorize creation. */
            op_phase = 88u;
            return;
        }
        /* A complete root scan proved no matching object.  The existing blank
         * cache path is now safe: selected children will populate it before
         * Bank's final writer creates the first authoritative register. */
        op_phase = 1u;
        return;

    case 88: /* RETURN ROOT + RETRY THE ONE SCANNED EXISTING REGISTER */
        filesystem_bootLoggingSetDetail("BKHCREAD");
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 89u;
        return;

    case 89: /* WAIT ONE RETRY; NEVER CONVERT A SECOND NULL INTO CREATE */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_makeNamedErrorCode("BKHtry", op_phase);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        filesystem_bootLoggingSetDetail("BKHCREAD");
        op_phase = 81u;
        return;

    case 1:
        /* Record the root Bank directory boundary before its asynchronous
         * open. The label remains until a later Bank sub-operation starts. */
        filesystem_bootLoggingSetDetail("BKROOT  ");
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
        filesystem_bootLoggingSetDetail("BKROOT  ");
        op_phase = 3u;
        return;

    case 3:
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 4u;
        return;

    case 4:
        filesystem_bootLoggingSetDetail("BKROOT  ");
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
        /* Rebuild the visible root key from the shared cache row. */
        filesystem_makeNumberedDir(
            op_root_open_name,
            op_slot,
            op_bank_display_name);
        /* The selected Bank directory is now the next wait-capable boundary;
         * identifying it must not mutate the cached key or BankData. */
        filesystem_bootLoggingSetBankDetail("DIR");
        if (!afatfs_opendir_lfn(op_root_open_name,
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
        filesystem_bootLoggingSetBankDetail("DIR");
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
        /* Bankset diagnostics name file-family work only; malformed content
         * still reaches the existing FS_STATUS_ERROR path. */
        filesystem_bootLoggingSetBankDetail("SET");
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
        filesystem_bootLoggingSetBankDetail("SET");
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
        filesystem_bootLoggingSetBankDetail("SCN");
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
             * Bank child discovery records occupancy only.
             *
             * Inputs: public child directory names inside the selected Bank.
             * Output: one bit in op_bank_child_present_mask for each valid
             * 00..15 child. Why: retaining all display names and aliases here
             * consumed a non-authoritative 368-byte Bank-local cache. The
             * selected child is rescanned below immediately before opening,
             * so duplicate ordering and an open key need exist for one child
             * only in existing operation scratch.
             */
            if (storage_parseBankSceneFolder(op_object.id.displayName,
                                             &child_slot,
                                             display)) {
                op_bank_child_present_mask =
                    (uint16_t)(op_bank_child_present_mask |
                               (uint16_t)(1u << child_slot));
            }
        }
        return;
    }

    case 16:
        filesystem_bootLoggingSetBankDetail("SCN");
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
        /*
         * Bank Load is strictly mask-selective.
         *
         * Inputs: caller's requested resident Scene bits and the discovered
         * Bank-child presence mask. Output: only their intersection can enter
         * the payload loop. The former empty-intersection fallback loaded every
         * present child, overwriting resident Scenes the caller had explicitly
         * left outside the mask and publishing their names as collateral.
         * Affiliates: the selected-child HCNAMES overlay and final BankData
         * presence merge below; neither may expand this request implicitly.
         */
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
            /*
             * No requested child exists in this Bank. Preserve the existing
             * resident Scene availability rather than clearing it: the caller
             * asked for a selective Bank identity/load operation, not a reset
             * of every unselected playable Scene. HCNAMES rows remain the
             * preloaded register values for the same reason.
             */
            bank_setScenePresentMask(bank_scenePresentMask());
            bank_selectActiveSceneForEditMask(op_bank_active_scene);
            bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
            bank_setRestoreBankSlot(op_slot);
            bank_setHasResidentBank(1u);
            memcpy(preset_currentName, op_bank_display_name, 8u);
            if (!afatfs_chdir(NULL))
                return;
            filesystem_cacheResidentName(0u, op_bank_display_name);
            filesystem_bootLoggingSetDetail("BKHCWRIT");
            op_phase = 83u;
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
        /*
         * The first selected child can be rediscovered immediately because
         * the parent Bank directory remains the current directory after phase
         * 16 closes its scan handle. Inputs: selected slot bit only. Output:
         * phases 27..31 retain one display name long enough to rebuild/open
         * that child; the shared Scene stage is initialized only after the
         * matching on-card directory has been found.
         */
        op_phase = 27u;
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
        /*
         * Opening a selected Bank child is not a successful payload result.
         *
         * Input: an existing child directory. Output: only the shared Scene
         * parser is armed. op_bank_loaded_scene remains false until that parser
         * validates and commits the child, so Menu never applies data merely
         * because a directory could be opened.
         */
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
        /*
         * Retain availability for resident Scenes outside this Bank request.
         *
         * Inputs: the effective selected-child mask and the pre-commit Bank
         * present mask, which is still intact until this one metadata commit.
         * Output: selected child slots become available while pre-existing
         * unselected slots remain available, matching the data and HCNAMES
         * preservation rules. This is a metadata merge only; the payload loop
         * above remains the sole writer of selected SceneData.
         */
        bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                            op_bank_scene_load_mask));
        bank_selectActiveSceneForEditMask(op_bank_active_scene);
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
        bank_setRestoreBankSlot(op_slot);
        bank_setHasResidentBank(1u);
        scene_selectActive(op_bank_active_scene);
        memcpy(preset_currentName, op_bank_display_name, 8u);
        filesystem_cacheResidentName(0u, op_bank_display_name);
        filesystem_bootLoggingSetDetail("BKHCWRIT");
        op_phase = 83u;
        return;
    }

    case 21:
        filesystem_bootLoggingSetBankSceneDetail('O');
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
        filesystem_bootLoggingSetBankSceneDetail('O');
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 23u;
        return;

    case 23:
        filesystem_bootLoggingSetBankSceneDetail('O');
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
        filesystem_makeNumberedDir(
            op_root_open_name,
            op_slot,
            op_bank_display_name);
        filesystem_bootLoggingSetBankSceneDetail('O');
        if (!afatfs_opendir_lfn(op_root_open_name,
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
        filesystem_bootLoggingSetBankSceneDetail('O');
        if (!afatfs_chdir(op_kit_slot_dir))
            return;
        op_phase = 26u;
        return;

    case 26:
        filesystem_bootLoggingSetBankSceneDetail('O');
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 27u;
        return;

    case 27:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        /*
         * Rescan the current selected Bank parent for just one child slot.
         *
         * Inputs: op_bank_child_cursor and the parent CWD restored by phases
         * 21..26 (or retained after phase 16 for the first child). Output:
         * op_scene_display_name is the sole transient child name. Opening
         * `.` gives findNextObject() a handle without allocating the former
         * 16-name/16-alias Bank cache; phase 31 turns that one name into the
         * exact directory component used by the shared Scene loader.
         */
        op_scene_display_name[0] = '\0';
        filesystem_bootLoggingSetBankSceneDetail('O');
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(".", "r", on_file_opened))
            return;
        op_phase = 28u;
        return;

    case 28:
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
        afatfs_findFirstObject(op_kit_slot_dir, &op_object_finder);
        op_phase = 29u;
        return;

    case 29:
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
            op_phase = 30u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_slot_dir, &op_object_finder);
            op_close_status = (op_scene_display_name[0] != '\0')
                ? FS_STATUS_DONE
                : FS_STATUS_ERROR;
            op_phase = 30u;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_DIRECTORY) {
            uint8_t child_slot;
            char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

            /*
             * Retain the lexical winner for this one requested child slot.
             *
             * Inputs: one directory object and op_bank_child_cursor. Output:
             * the pre-existing nine-byte op_scene_display_name scratch. This
             * preserves the old duplicate policy while replacing the former
             * sixteen-entry name/alias arrays; no filename key is stored,
             * because storage_formatBankSceneDir() derives it in phase 31.
             */
            if (storage_parseBankSceneFolder(op_object.id.displayName,
                                             &child_slot,
                                             display) &&
                child_slot == op_bank_child_cursor) {
                display[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
                if (op_scene_display_name[0] == '\0' ||
                    filesystem_displayPrecedesCached(display,
                                                     op_scene_display_name)) {
                    memcpy(op_scene_display_name, display,
                           STORAGE_SCENE_DISPLAY_NAME_LEN + 1u);
                }
            }
        }
        return;
    }

    case 30:
        filesystem_bootLoggingSetBankSceneDetail('O');
        op_close_done = false;
        if (afatfs_fclose(op_kit_slot_dir, on_file_closed))
            op_phase = 31u;
        return;

    case 31:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_setPresetNameInvalid();
            if (!afatfs_chdir(NULL))
                return;
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_scene_load_scene_mask = (uint16_t)(1u << op_bank_child_cursor);
        filesystem_initSceneStage(&fs_stage_workspace.scene_stage);
        /*
         * Reset payload discovery and derive the selected directory on demand.
         *
         * Inputs: one validated `SS Name` display suffix in
         * op_scene_display_name. Output: op_root_open_name receives the exact
         * Bank-child component and asyncfatfs returns its short alias only for
         * this immediate open. Affiliates: filesystem_loadSceneDirectory_tick
         * consumes the resulting handle at phase 18; its stage remains the
         * one shared Scene/Kit staging workspace, not a name cache.
         */
        filesystem_resetSceneLoadChildDiscovery();
        storage_formatBankSceneDir(op_root_open_name,
                                   sizeof(op_root_open_name),
                                   op_bank_child_cursor,
                                   op_scene_display_name);
        filesystem_bootLoggingSetBankSceneDetail('O');
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_opendir_lfn(op_root_open_name,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                op_root_open_name,
                                on_file_opened)) {
            return;
        }
        op_phase = 18u;
        return;

    case 19:
        if (!op_close_done)
            return;
        op_kit_slot_dir = NULL;
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_ERROR);
        return;

    case 83: /* OPEN HCNAMES DESTINATION AFTER BANK METADATA COMMIT */
        /* The final merged register is Bank-owned; retain this label across
         * open, streaming, close, and root return without changing errors. */
        filesystem_bootLoggingSetDetail("BKHCWRIT");
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "w",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 84u;
        return;

    case 84: /* WAIT HCNAMES DESTINATION */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_bytes_done = 0u;
        filesystem_bootLoggingSetDetail("BKHCWRIT");
        op_phase = 85u;
        return;

    case 85: /* STREAM PRESERVED + SELECTED-BANK HCNAMES ROWS */
        if (op_item_offset < FS_RESIDENT_NAMES_ROW_COUNT) {
            if (op_bytes_done == 0u) {
                op_line_len = filesystem_formatResidentNameLine(
                    op_line_buf, sizeof(op_line_buf),
                    fs_list_cache_name[op_item_offset],
                    (uint8_t)!filesystem_residentNameIsBlank(
                        fs_list_cache_name[op_item_offset]));
                if (op_line_len == 0u) {
                    filesystem_finish(FS_STATUS_ERROR);
                    return;
                }
            }
            op_bytes_done += afatfs_fwrite(
                op_file, (const uint8_t *)op_line_buf + op_bytes_done,
                op_line_len - op_bytes_done);
            if (op_bytes_done >= op_line_len) {
                op_item_offset++;
                op_bytes_done = 0u;
            }
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 86u;
        return;

    case 86: /* CLOSE AND FLUSH HCNAMES */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Complete Bank Load after its resident identity is durable.
         *
         * Inputs: committed Bank/Scene payload and closed HCNAMES file. Output:
         * the original Preset callback can consume op_bank_loaded_scene before
         * any new filesystem request resets operation scratch. Load does not
         * mutate the root Bank namespace, so it neither scans `/Bank/` nor
         * rewrites `.hcindex`; Menu reloads that existing index only after the
         * active Scene has passed through the shared DSP apply worker.
         */
        filesystem_finish(FS_STATUS_DONE);
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
     * is op_bank_child_present_mask only. This read-only operation exists
     * so Menu can light Load:[Bank] LEDs for the highlighted Bank before the
     * user presses OK; it intentionally does not parse bankset.bcg, load Scene
     * payloads, or write BankData.
     */
    switch (op_phase) {
    case 0:
        if (op_slot >= STORAGE_BANK_MAX_SLOTS ||
            !filesystem_librarySlotExists(FS_NAME_CACHE_BANK, op_slot)) {
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
        /* Reconstruct the selected root key from the shared Bank row. */
        filesystem_makeNumberedDir(
            op_root_open_name,
            op_slot,
            filesystem_cachedLibraryName(FS_NAME_CACHE_BANK, op_slot));
        if (!afatfs_opendir_lfn(op_root_open_name,
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
             * child_slot sets one bit in op_bank_child_present_mask. Why: LED
             * preview needs occupancy, not child identities; a real Bank Load
             * rescans its requested child and chooses its lexical winner using
             * the existing one-name operation scratch.
             */
            if (storage_parseBankSceneFolder(op_object.id.displayName,
                                             &child_slot,
                                             display)) {
                op_bank_child_present_mask =
                    (uint16_t)(op_bank_child_present_mask |
                               (uint16_t)(1u << child_slot));
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
    const char *directory = instrumentManager_storageDirectory(
        op_instrument_load_type);

    switch (op_phase) {
    case 0: /* VALIDATE CAPTURED KEY + CHDIR ROOT */
        if (op_instrument_load_destination_slot >= STORAGE_KIT_SLOT_COUNT ||
            op_instrument_load_type >= INSTRUMENT_TYPE_UNKNOWN ||
            op_instrument_save_display_name[0] == '\0') {
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

    case 6: /* OPEN subdir */
        op_file_ready = false;
        op_file = NULL;
        
        /*
         * We use AFATFS_MATCH_CASE_INSENSITIVE here to match user-created directories seamlessly,
         * ensuring that even if the directory on the card is named "drum" instead of "Drum",
         * it will be opened successfully.
         */
        if (!directory ||
            !afatfs_opendir_lfn(directory,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 7;
        return;

    case 7: /* WAIT subdir */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 8;
        return;

    case 8: /* CHDIR subdir */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 9;
        return;

    case 9: /* CLOSE subdir */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 10;
        return;

    case 10: /* WAIT CLOSE subdir */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 11;
        return;

    case 11: /* OPEN selected instrument */
        storage_instrumentStateInit(&op_instrument_state,
                                    op_instrument_load_type,
                                    (uint8_t)(op_instrument_load_destination_slot + 1u));
        instrumentManager_resetSlot(
            &op_staged_instrument,
            op_instrument_load_type);
        op_line_len = 0u;
        op_file_ready = false;
        op_file = NULL;
        
        {
            char lfn[STORAGE_KIT_FILENAME_MAX];
            const char *display_name = op_instrument_save_display_name;

            /*
             * Derive the one immediate LFN open key after staging has claimed
             * the separate typed stage.
             *
             * Normal-input path: a captured pool display stem plus type is
             * formatted into its `<stem>.<ext>` leaf. Temporary-input path:
             * the request already contains the complete reserved
             * `.hctmp.<ext>` component, so it must be copied verbatim. Why:
             * storage_makeSavedInstrumentDisplayFilename() correctly treats a
             * leading dot as an empty normal stem and substitutes `none`; using
             * it for the hidden name therefore attempted to open `none.<ext>`
             * and made the direct `kit` row fail as `InsL00`. Output: `lfn` is
             * the exact selected pool leaf or the exact temporary leaf for the
             * following asyncfatfs open. Affiliates:
             * filesystem_requestLoadInstrument(),
             * filesystem_requestLoadInstrumentTemp(), and
             * filesystem_makeInstrumentTemporaryFilename().
             */
            if (op_instrument_load_temporary) {
                strncpy(lfn, display_name, sizeof(lfn) - 1u);
                lfn[sizeof(lfn) - 1u] = '\0';
            } else {
                storage_makeSavedInstrumentDisplayFilename(
                    lfn, sizeof(lfn), display_name,
                    op_instrument_load_type, 0u, 0u);
            }

            if (!afatfs_fopen_lfn(lfn, "r", AFATFS_MATCH_CASE_INSENSITIVE,
                                  NULL, on_file_opened))
                return;
        }
        op_phase = 12;
        return;

    case 12: /* WAIT selected instrument */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 16;
            return;
        }
        op_phase = 13;
        return;

    case 13: /* READ selected instrument */
        st = filesystem_readTextLine(op_file, op_line_buf, &op_line_len,
                                     sizeof(op_line_buf), &line_ready, &eof);
        if (st == STORAGE_STATUS_WAIT)
            return;
        if (st != STORAGE_STATUS_OK) {
            op_close_status = FS_STATUS_ERROR;
            op_phase = 14;
            return;
        }
        if (line_ready) {
            st = storage_instrumentParseLine(
                &op_instrument_state,
                op_line_buf,
                &op_staged_instrument);
            if (st != STORAGE_STATUS_OK) {
                op_close_status = FS_STATUS_ERROR;
                op_phase = 14;
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
                 * Pair the validated Instrument with its one identity row.
                 *
                 * Why: the payload union holds descriptor data only; the
                 * captured request key remains valid after its typed index was
                 * reused as staging. Inputs: selected display name and
                 * destination voice. Output: HCNAMES update source.
                 * Affiliates: preset completion and Instrument menu exit.
                 */
                /*
                 * Publish a normal pool load name, never the hidden temp name.
                 *
                 * Inputs: validated selected filename and request-time hidden
                 * temporary flag. Output: ordinary pool loads update one
                 * identity row immediately; `.hctmp.<ext>` restores parameters
                 * only, leaving Menu's separate nine-byte `kit` label intact.
                 * Affiliates: filesystem_requestLoadInstrumentTemp(), Menu's
                 * session invalidation, and the exit-time HCNAMES writer.
                 */
                if (!op_instrument_load_temporary) {
                    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
                    filesystem_copyInstrumentStemDisplay(
                        display, op_instrument_save_display_name);
                    filesystem_setIdentityName(
                        (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 +
                                  op_instrument_load_destination_slot),
                        display);
                }
                op_close_status = FS_STATUS_DONE;
            }
            op_phase = 14;
        }
        return;

    case 14: /* CLOSE selected instrument */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 15;
        return;

    case 15: /* WAIT CLOSE selected instrument */
        if (!op_close_done) return;
        op_file = NULL;
        op_phase = 16;
        return;

    case 16: /* RETURN ROOT + FINISH */
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

    case 10: /* OPEN subdir before any create fallback */
        op_file_ready = false;
        op_file = NULL;
        op_create_dir_retry = 0u;
        {
            const char *directory = instrumentManager_storageDirectory(
                op_instrument_save_type);
            if (!directory ||
                !afatfs_opendir_lfn(directory,
                                    AFATFS_MATCH_CASE_INSENSITIVE,
                                    NULL,
                                    on_file_opened)) {
                return;
            }
        }
        op_phase = 11;
        return;

    case 11: /* WAIT subdir */
        if (!op_file_ready) return;
        if (!op_file) {
            const char *directory = instrumentManager_storageDirectory(
                op_instrument_save_type);
            if (op_create_dir_retry != 0u || !directory) {
                filesystem_finish(FS_STATUS_ERROR);
                return;
            }
            /*
             * Create an Instrument type directory only after strict open miss.
             *
             * This mirrors `.hcindex` generation: save may create `Drum/` on a
             * fresh card, but it must not use create-capable LFN lookup before
             * proving that a visible `Drum/` directory is absent.
             */
            op_create_dir_retry = 1u;
            op_file_ready = false;
            if (!afatfs_mkdir_lfn(directory,
                                  AFATFS_MATCH_CASE_INSENSITIVE,
                                  NULL,
                                  on_file_opened))
                return;
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 12;
        return;

    case 12: /* CHDIR subdir */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 13;
        return;

    case 13: /* CLOSE subdir handle */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 14;
        return;

    case 14: /* WAIT CLOSE subdir */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 15;
        return;

    case 15: /* REMOVE target instrument variants */
        op_remove_done = 0u;
        if (!afatfs_removeObjects_lfn(op_instrument_save_display_name,
                                      AFATFS_MATCH_CASE_INSENSITIVE,
                                      AFATFS_REMOVE_FILES_ONLY,
                                      on_remove_complete)) {
            return;
        }
        op_phase = 16;
        return;

    case 16: /* WAIT remove + OPEN target instrument file */
        if (!op_remove_done)
            return;
        op_file_ready = false;
        op_file = NULL;
        memset(op_instrument_save_open_name, 0,
               sizeof(op_instrument_save_open_name));
        if (!afatfs_fopen_lfn(op_instrument_save_display_name,
                              "w",
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_instrument_save_open_name,
                              on_file_opened))
            return;
        op_phase = 17;
        return;

    case 17: /* WAIT target instrument file */
        if (!op_file_ready) return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_phase = 18;
        return;

    case 18: /* WRITE complete instrument text */
    {
        filesystem_instrument_write_ctx_t ctx = {{
            instrument,
            op_instrument_save_type,
            (uint8_t)(op_instrument_save_source_slot + 1u),
            scene ? scene->settings.voice_morph_amount[
                        op_instrument_save_source_slot] : 0u,
            op_instrument_save_mode
        }};
        if (filesystem_writeTextLine(filesystem_nextInstrumentLine, &ctx))
            return;
        op_phase = 19;
        return;
    }

    case 19: /* CLOSE target instrument file */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 20;
        return;

    case 20: /* WAIT CLOSE target instrument file */
        if (!op_close_done) return;
        op_file = NULL;
        op_phase = 21;
        return;

    case 21: /* RETURN ROOT + UPDATE CACHE */
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Finish a hidden `kit` snapshot without publishing library state.
         *
         * Inputs: `.hctmp.<ext>` has been written in the current type folder.
         * Output: normal filesystem completion only. Why: the temporary file
         * must not rename the live Instrument identity or alter `.hcindex`;
         * it is excluded from future scans and is read only by the direct
         * `kit` row. Affiliates: Menu entry sequencing and temp load request.
         */
        if (op_instrument_save_temporary) {
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        filesystem_updateInstrumentCacheAfterSave(
            op_instrument_save_display_name,
            op_instrument_save_open_name);
        if (!morph_save) {
            char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

            /*
             * Publish only the saved Instrument stem to the resident identity.
             *
             * Inputs: completed `stem.ext` write target and source voice.
             * Output: the one HCNAMES/LCD identity row receives the eight-cell
             * stem, never the raw filename whose extension would be visible
             * whenever the stem is shorter than eight characters. The complete
             * filename remains operation-local and is still used for cache/open
             * keys above. Affiliates: nested Instrument Save and Menu's single
             * HCNAMES session flush.
             */
            filesystem_copyInstrumentStemDisplay(
                display, op_instrument_save_display_name);
            filesystem_setIdentityName(
                (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 +
                          op_instrument_save_source_slot),
                display);
        }
        
        /*
         * Refresh the saved type's registry-owned `.hcindex` before publishing
         * Save completion. The single shared cache has already incorporated
         * the saved display stem, so the refresh works identically for Drum,
         * Snare, Cymbal, and HiHat and leaves other type indexes untouched.
         */
        op_instrument_index_type = op_instrument_save_type;
        /*
         * Arm the new owner because this save-to-index handoff does not call
         * filesystem_start(). Input is the completed Instrument save cache;
         * output is an INSINDEX deadline for its registry index rewrite. This
         * is harmless at runtime because logging is inactive there. Affiliate:
         * filesystem_createBootIndex_tick().
         */
        /*
         * DEV_MODE_LOGGING writes operation codes to file for use in debugging.
         * It must never print anything to the screen or otherwise delay
         * operations unnecessarily since logging may be used to assess timing
         * failures in other modules that might otherwise be obscured by screen
         * write delays.
         */
        filesystem_bootLoggingArm("INSINDEX");
        current_op = FS_INTERNAL_OP_CREATE_BOOT_INDEX;
        op_phase = 0u;
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

static void filesystem_initSceneStage(filesystem_scene_stage_t *stage)
{
    static const instrument_type_t initial_types[INSTRUMENT_SLOT_COUNT] = {
        INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM,
        INSTRUMENT_TYPE_SNR, INSTRUMENT_TYPE_CYM, INSTRUMENT_TYPE_HAT
    };
    uint8_t slot;
    uint8_t track;

    /*
     * Build safe defaults for the separate non-Pattern Scene stage.
     *
     * SceneData's scene_initAll() initializes resident scenes[] directly. Scene
     * Load needs the same style of defaults in dedicated staging SRAM so
     * optional sceneset keys can be absent without leaving random bytes, and so
     * a failed child file never mutates resident Scene state.
     */
    if (!stage)
        return;
    memset(stage, 0, sizeof(*stage));
    stage->settings.voice_decimation_all = 127u;
    for (track = 0u; track < NUM_TRACKS; track++) {
        stage->settings.midi_channel[track] = (uint8_t)(track + 1u);
        stage->settings.midi_note[track] = MIDI_DEFAULT_TRIGGER_NOTE;
    }
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        /*
         * Scene-owned voice mix defaults mirror SceneData's resident init.
         *
         * Inputs: the zero-based instrument slot. Outputs: staged Scene route,
         * FX send, and fader mode bytes before any optional sceneset.scg lines
         * are parsed. The loop is deliberately per-slot, not per-track, because
         * audio routing is six-voice mixer state while MIDI defaults above are
         * seven-track sequencer state.
         */
        stage->settings.audio_out[slot] = filesystem_defaultVoiceAudioOut(slot);
        stage->settings.fx_send_amount[slot] = 0u;
        stage->settings.fader_setting[slot] = 0u;
        instrumentManager_resetSlot(&stage->kit.instruments[slot],
                                    initial_types[slot]);
    }
}

static void filesystem_commitSceneStage(void)
{
    uint8_t scene_index;

    /*
     * Commit validated general Scene settings and embedded Kit before Pattern
     * I/O starts.
     *
     * Why: Pattern is intentionally non-atomic for the current format work;
     * excluding PatternSet from staging keeps validation inside the separate
     * typed stage. Inputs: fully parsed stage image and the
     * immutable destination mask. Outputs: final Scene settings/Kit plus a
     * default final PatternSet ready for direct streaming.
     *
     * Affiliates: filesystem_directPatternTarget(), Scene Pattern phases, and
     * the later Pattern transactional redesign.
     */
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        scene_t *target;
        if ((op_scene_load_scene_mask & (uint16_t)(1u << scene_index)) == 0u)
            continue;
        target = scene_get(scene_index);
        if (!target)
            continue;
        target->settings = fs_stage_workspace.scene_stage.settings;
        target->kit = fs_stage_workspace.scene_stage.kit;
        pat_initPatternSet(&target->pattern);
    }
}

static PatternSet *filesystem_directPatternTarget(void)
{
    uint8_t scene_index;

    /*
     * Choose the first committed destination as the direct Pattern parse sink.
     *
     * Inputs: current Scene destination mask. Output: final Scene PatternSet
     * or NULL. The final commit phase mirrors this completed PatternSet to any
     * additional selected destinations without ever allocating a Pattern stage.
     * Affiliates: every binary/text Pattern parser phase and Scene phase 61.
     */
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((op_scene_load_scene_mask & (uint16_t)(1u << scene_index)) != 0u) {
            scene_t *target = scene_get(scene_index);
            if (target)
                return &target->pattern;
        }
    }
    return NULL;
}

static void filesystem_resetSceneLoadChildDiscovery(void)
{
    /*
     * Clear Scene-loader object-discovery scratch before scanning one payload.
     *
     * Inputs: none. Outputs: the names used by Scene-load phase 9 to remember
     * the first embedded `Kit <name>` directory, `.pat` file, and `.fx` file
     * are all empty. The next scan may therefore populate them only from the
     * directory it has just entered.
     *
     * Root Scene Load calls this once. Bank Load calls it before its first
     * child and each later child because it reuses the same Scene state machine
     * within one public request. Without this reset, loading `00 Slak/Kit
     * Brezel` followed by `01 Slak/Kit Forest` leaves `Kit Brezel` cached;
     * child 01 skips discovery and fails trying to open that absent directory.
     * The Bank wrapper later reports that delegated-child failure as BnkL14
     * (decimal Bank phase 20 represented in hexadecimal), obscuring the real
     * stale-name cause.
     */
    memset(op_scene_child_open_name, 0, sizeof(op_scene_child_open_name));
    memset(op_scene_child_display_name, 0,
           sizeof(op_scene_child_display_name));
    memset(op_scene_pattern_open_name, 0, sizeof(op_scene_pattern_open_name));
    memset(op_scene_effect_open_name, 0, sizeof(op_scene_effect_open_name));
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
                               filesystem_cachedResidentName(
                                   filesystem_residentSceneRow(scene_index)));
    memcpy(op_scene_display_name,
           filesystem_cachedResidentName(
               filesystem_residentSceneRow(scene_index)),
           STORAGE_SCENE_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    filesystem_makeSceneEmbeddedKitDir(
        op_save_scene_kit_display_name,
        sizeof(op_save_scene_kit_display_name),
        filesystem_cachedResidentName(
            filesystem_residentKitRow(scene_index)));
    for (voice = 0u; voice < STORAGE_KIT_SLOT_COUNT; voice++)
        filesystem_setIdentityName(
            (uint8_t)(FS_IDENTITY_INSTRUMENT_ROW_0 + voice),
            filesystem_cachedResidentName(
                filesystem_residentInstrumentRow(scene_index, voice)));
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
    case 16u:
        /*
         * Persist the user-facing AutoSave policy independently of hidden-file
         * activity. Input is the normalized Global byte. Output is one keyed
         * 0/1 line; writing settings.cfg remains active even while AutoSave is
         * OFF. Affiliates: PAR_AUTOSAVE_ENABLED and the filesystem policy gate.
         */
        return filesystem_formatAssignmentU16Line(
            dst, cap, "autosave", parameter_values[PAR_AUTOSAVE_ENABLED]);
    default:
        if (op_write_line_index >= 17u && op_write_line_index < 33u) {
            char key[] = "scene_source_00";
            uint8_t scene_index = (uint8_t)(op_write_line_index - 17u);

            /*
             * Stream one resident Scene source without a sixteen-string table.
             *
             * Inputs: line index 17..32 and SceneData's encoded uint16_t.
             * Output: scene_source_00..15 in stable resident order. Why: one
             * generated key keeps parser/writer numbering visibly paired and
             * retains the exact 32-byte SRAM design. Affiliates:
             * filesystem_settingsSceneSourceIndex() and scene_sourceValue().
             */
            key[13] = (char)('0' + (scene_index / 10u));
            key[14] = (char)('0' + (scene_index % 10u));
            return filesystem_formatAssignmentU16Line(
                dst, cap, key, scene_sourceValue(scene_index));
        }
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
            dst, cap, "file", filesystem_memberFilename(slot));
    default:
        /*
         * Blank separator after each slot. New Kit writers intentionally stop
         * here; legacy audio_out values remain parse-only compatibility data
         * and are never emitted from a root or embedded Kit Save.
         */
        return filesystem_formatLiteralLine(dst, cap, "\n");
    }
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

/*
 * Retired firmware-owned recursive deleter.
 *
 * asyncfatfs_deleteTree() now owns recursive traversal for the concrete
 * object captured by filesystem_deleteKitSlotDirectories_tick(). Keeping this
 * historical implementation disabled ensures its 558-byte name/alias stack
 * cannot return to SRAM while preserving the old algorithm only as temporary
 * source archaeology until the next broad storage cleanup removes it entirely.
 */
#if 0
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

#endif

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
                filesystem_memberFilename(op_instrument_slot),
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
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL)
            filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                       preset_currentName);
        /*
         * Defer successful completion until the boot-equivalent Kit rebuild
         * chain has finished. The directory is now written, but the active
         * cache may still describe the pre-save card contents. The final save
         * flush starts a physical Kit/ scan; that scan then starts the complete
         * 000..999 `.hcindex` writer before the original callback is released.
         */
        op_library_index_rebuild_kind = FS_NAME_CACHE_KIT;
        op_library_index_rebuild_pending = 1u;
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
        /*
         * Borrow the full resident name register before constructing the Bank
         * tree. Inputs: accepted Save request and current root `/.hcnames`.
         * Output: Scene/Kit directory display components come from the cache
         * while Instrument member filenames retain their required 16-character
         * source stems from SceneData. This replaces no new SRAM: the 129 rows
         * occupy the already-existing generalized cache until Bank index
         * restoration after promotion. Affiliates: prepareBankSceneSaveSource
         * and final Bank HCNAMES writer.
         */
        filesystem_prepareResidentNamesCache();
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 80u;
        return;

    case 80: /* WAIT Bank-save HCNAMES OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 81u;
        return;

    case 81: /* READ SAVED-IDENTITY REGISTER */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t read_status = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            op_close_status = FS_STATUS_ERROR;
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 82u;
            return;
        }
        if (line_ready) {
            filesystem_cacheResidentName(op_item_offset, op_line_buf);
            if (op_item_offset < UINT16_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if (eof) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 82u;
        }
        return;
    }

    case 82: /* CLOSE PRELOAD, THEN CREATE THE TEMP BANK */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
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
        /*
         * The promoted root folder is the authoritative new Bank identity.
         * Update only row zero in the already-read register; selected Scene,
         * Kit, and Instrument rows were copied from HCNAMES into the just
         * written Bank tree and remain unchanged in resident memory.
         */
        filesystem_cacheResidentName(0u, op_bank_display_name);
        op_phase = 83u;
        return;

    case 83: /* OPEN ROOT HCNAMES FOR POST-PROMOTION REGISTER WRITE */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "w",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 84u;
        return;

    case 84: /* WAIT HCNAMES WRITE DESTINATION */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_item_offset = 0u;
        op_bytes_done = 0u;
        op_phase = 85u;
        return;

    case 85: /* STREAM BANK-SAVE HCNAMES REGISTER */
        if (op_item_offset < FS_RESIDENT_NAMES_ROW_COUNT) {
            if (op_bytes_done == 0u) {
                op_line_len = filesystem_formatResidentNameLine(
                    op_line_buf, sizeof(op_line_buf),
                    fs_list_cache_name[op_item_offset],
                    (uint8_t)!filesystem_residentNameIsBlank(
                        fs_list_cache_name[op_item_offset]));
                if (op_line_len == 0u) {
                    filesystem_finish(FS_STATUS_ERROR);
                    return;
                }
            }
            op_bytes_done += afatfs_fwrite(
                op_file, (const uint8_t *)op_line_buf + op_bytes_done,
                op_line_len - op_bytes_done);
            if (op_bytes_done >= op_line_len) {
                op_item_offset++;
                op_bytes_done = 0u;
            }
            return;
        }
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 86u;
        return;

    case 86: /* CLOSE REGISTER, THEN RESTORE ROOT BANK INDEX */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Bank Save has now promoted its complete temporary tree. Park the
         * original callback and run the same boot-equivalent Bank rescan plus
         * `/Bank/.hcindex` rewrite used by Kit, root Scene, and Bank. This is required
         * for newly-created, renamed, or removed root Bank folders to become
         * visible immediately without a restart.
         */
        op_library_index_rebuild_kind = FS_NAME_CACHE_BANK;
        op_library_index_rebuild_pending = 1u;
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
                filesystem_memberFilename(op_instrument_slot),
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
            /*
             * Publish the one renamed/saved resident Scene through HCNAMES
             * before rebuilding `/Scene/.hcindex`.
             *
             * Inputs: source Scene coordinate and op_scene_display_name
             * captured at Save acceptance. Output: the generic register writer
             * preserves every other name row. Scene Save owns the rebuild flag
             * because it alone promoted a possibly created or renamed root
             * directory; the shared HCNAMES close does not infer that policy.
             * The original callback remains parked until the physical Scene
             * scan, complete index rewrite, and final flush are durable. This
             * replaces the removed scene_t display-name mirror.
             */
            op_scene_load_scene_mask =
                (uint16_t)(1u << op_kit_save_source_scene);
            op_library_index_rebuild_kind = FS_NAME_CACHE_SCENE;
            op_library_index_rebuild_pending = 1u;
            filesystem_prepareResidentNamesCache();
            /*
             * Scene Save hands directly to HCNAMES without generic start.
             * Input is the saved root Scene identity; output is a separately
             * timed HCNAMES owner. Why: a logging build must report the actual
             * stalled register update, while runtime behavior is unchanged
             * after filesystem_bootLoggingEnd(). Affiliate: resident names.
             */
            /*
             * DEV_MODE_LOGGING writes operation codes to file for use in
             * debugging. It must never print anything to the screen or
             * otherwise delay operations unnecessarily since logging may be
             * used to assess timing failures in other modules that might
             * otherwise be obscured by screen write delays.
             */
            filesystem_bootLoggingArm("HCNAMES ");
            current_op = FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE;
            op_phase = 0u;
            return;
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
#if 0 /* Retired generic binary pattern/container stream state machines. */
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

#endif
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
        /*
         * Snapshot the settings revision before the first line is emitted.
         * Input: live dirty revision. Output: the final flush may clear dirty
         * only if no later Global/provenance event advanced it. Why: line-by-
         * line serialization is not an atomic SRAM snapshot. Affiliates:
         * filesystem_complete() and the background settings scheduler.
         */
        op_settings_change_revision = fs_settings_change_revision;
        op_settings_write_active = 1u;
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
** populates the shared slot-indexed name cache. A non-blank cache row is the
** complete Kit slot-existence record; no per-slot alias or presence table is
** retained.
**
** Inputs: no slot input; filesystem_requestScanKits() clears the cache and
** starts this op. Outputs: shared slot-indexed names. Missing Kit/ is treated
** as a successful empty scan so boot/menu can show Empty slots instead of a
** filesystem error.
**
** Affiliates/clients: main startup calls filesystem_requestScanKits() before
** preset_loadDrumset(0,0); menu.c consumes filesystem_kitSlotName() for direct
** Load-page display.
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
** Inputs: filesystem_requestScanScenes() clears the shared Scene cache and
** starts this operation. Output: Scene/NNN Name folders populate that cache;
** a non-blank row is the sole slot-existence record. Missing Scene/ is a
** successful empty scan, matching Kit/ behavior.
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
** Inputs: filesystem_requestScanBanks() clears the shared root-Bank cache and
** starts this operation. Output: Bank/NNN Name folders populate the
** FS_NAME_CACHE_BANK rows. Missing Bank/ is a successful empty scan
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
** Inputs: filesystem_requestScanInstruments() clears the shared cache and
** starts this operation. Outputs: one sorted Instrument cache populated for
** the active registry type from files whose extensions match the registry. Missing
** Instrument/ is a successful empty scan so the Load Instrument UI can show an
** empty list instead of a filesystem error.
** ----------------------------------------------------------------------- */
static void filesystem_scanInstruments_tick(void)
{
    switch (op_phase) {
    case 0: /* CHDIR root */
        if (!afatfs_chdir(NULL))
            return;
        /* Registry order owns the set of Instrument folders to scan. */
        if (!op_instrument_scan_one_type)
            op_instrument_scan_registry_index = 0u;
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

    case 2: /* WAIT_OPEN Instrument/ */
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
        op_phase = 4;
        return;

    case 4: /* CLOSE Instrument/ */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT_CLOSE Instrument/ */
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        op_phase = 6;
        return;

    case 6: /* NEXT SUBDIRECTORY LOOP */
    {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(
                op_instrument_scan_registry_index);
        if (!entry) {
            op_phase = 12; // all types scanned
            return;
        }
        if (fs_list_cache_kind != FS_NAME_CACHE_INSTRUMENT ||
            fs_list_cache_type != entry->type) {
            filesystem_clearNameCacheStorage();
            fs_list_cache_kind = FS_NAME_CACHE_INSTRUMENT;
            fs_list_cache_type = entry->type;
        }
        op_file_ready = false;
        op_file = NULL;
        
        /*
         * We use AFATFS_MATCH_CASE_INSENSITIVE here when opening subdirectories because users 
         * frequently copy/create these folders manually on their computers, which may result
         * in casing variations like "drum", "Drum", or "DRUM". FAT is fundamentally case-insensitive,
         * so this prevents user-created folders from becoming "invisible" to the scan.
         */
        if (!entry->storage_directory ||
            !afatfs_opendir_lfn(entry->storage_directory,
                                AFATFS_MATCH_CASE_INSENSITIVE,
                                NULL,
                                on_file_opened))
            return;
        op_phase = 7;
        return;
    }

    case 7: /* WAIT_OPEN SUBDIRECTORY */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            /* Missing subdir, skip to next type */
            if (op_instrument_scan_one_type) {
                op_phase = 12;
            } else {
                op_instrument_scan_registry_index++;
                op_phase = 6;
            }
            return;
        }
        op_kit_root_dir = op_file;
        op_phase = 8;
        return;

    case 8: /* CHDIR SUBDIRECTORY */
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        afatfs_findFirstObject(op_kit_root_dir, &op_object_finder);
        op_phase = 9;
        return;

    case 9: /* FIND_NEXT */
    {
        afatfsOperationStatus_e st =
            afatfs_findNextObject(op_kit_root_dir,
                                  &op_object_finder,
                                  &op_object);
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE || op_object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op_kit_root_dir, &op_object_finder);
            op_phase = 10;
            return;
        }
        if (op_object.id.kind == AFATFS_OBJECT_FILE) {
            filesystem_recordInstrumentFile(op_object.id.displayName,
                                            op_object.id.shortName);
        }
        return;
    }

    case 10: /* CLOSE SUBDIRECTORY */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 11;
        return;

    case 11: /* WAIT_CLOSE SUBDIRECTORY + CHDIR PARENT */
    {
        if (!op_close_done) return;
        op_kit_root_dir = NULL;
        
        /*
         * WARNING: afatfs_chdirParent returns an afatfsOperationStatus_e enum, NOT a boolean.
         * 
         * AFATFS_OPERATION_SUCCESS (0): The parent directory was successfully resolved and opened.
         * AFATFS_OPERATION_IN_PROGRESS (1): The sector load is still asynchronous, wait and retry.
         * AFATFS_OPERATION_FAILURE (2): The parent directory cannot be resolved.
         * 
         * Using `if (!afatfs_chdirParent())` is catastrophic because SUCCESS (0) evaluates to true
         * and IN_PROGRESS (1) evaluates to false. This will trap the state machine in this phase
         * forever on success!
         */
        afatfsOperationStatus_e st = afatfs_chdirParent();
        if (st == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (st == AFATFS_OPERATION_FAILURE) {
            op_phase = 12; // Force exit on severe filesystem error
            return;
        }
        
        /* Success: advance to the next registry-defined subdirectory unless
         * this boot pass intentionally scans only one type for the shared
         * cache/index pair. */
        if (op_instrument_scan_one_type) {
            op_phase = 12;
        } else {
            op_instrument_scan_registry_index++;
            op_phase = 6;
        }
        return;
    }

    case 12: /* CHDIR root + FINISH */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_finish(FS_STATUS_DONE);
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
** types, or "-       " for malformed short files. Clients: preset_loadName()
** and menu_requestCurrentLoadSaveSelection().
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
static fs_boot_substep_diag_cb_t fs_boot_substep_diagnostic = NULL;

static void filesystem_reportBootSubstep(uint8_t substep)
{
    /*
     * Notify the temporary front-panel observer before one blocking phase-43
     * component call starts. Input is the stable FSub code documented in
     * HCNAMES_IMPLEMENTATION.md. Output is diagnostic-only; the callback may
     * update the boot OLED but cannot mutate filesystem ownership or progress.
     *
     * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the
     * user to assess how operations are proceeding. It does not and should not
     * ever add additional file interaction steps, since the diagnostic may be
     * used to assess in-situ file procedures.
     */
#if DEV_MODE_DIAGNOSTIC
    if (fs_boot_substep_diagnostic)
        fs_boot_substep_diagnostic(substep);
#else
    (void)substep;
#endif
}

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
#if DEV_MODE_LOGGING
    /*
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * Raw blocking helpers must unwind after either primary or recovery
     * expiry. Input is the shared watchdog latch plus asyncfatfs mount state;
     * output is false before another open/read/rename loop iteration. Why:
     * these helpers do not inspect facade status. Affiliates:
     * filesystem_blockPoll() and Kit quarantine/validation.
     */
    if (fs_boot_logging_timed_out || fs_boot_logging_recovery_failed)
        return 0u;
#endif
    return (uint8_t)(afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_READY);
}

static void filesystem_blockPoll(void)
{
    /*
     * Apply the same cooperative deadline before direct asyncfatfs progress.
     * Input is the currently armed boot operation; output is either one normal
     * FAT poll or an immediate return after timeout. Why: private blocking
     * helpers bypass filesystem_tick(). Affiliate: filesystem_blockFsOk().
     */
    if (filesystem_bootLoggingPollDeadline())
        return;
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

static afatfsFilePtr_t filesystem_blockOpenLfn(const char *filename)
{
    block_file_ready = 0;
    block_file = NULL;

    while (!afatfs_fopen_lfn(filename,
                             "r",
                             AFATFS_MATCH_CASE_INSENSITIVE,
                             NULL,
                             filesystem_blockOpenCb)) {
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

static afatfsFilePtr_t filesystem_blockOpenDirLfn(const char *filename)
{
    block_file_ready = 0;
    block_file = NULL;

    while (!afatfs_opendir_lfn(filename,
                               AFATFS_MATCH_CASE_INSENSITIVE,
                               NULL,
                               filesystem_blockOpenCb)) {
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

static uint8_t filesystem_blockRename(const char *old_name,
                                      const char *new_name)
{
    char open_name[AFATFS_SHORT_FILENAME_MAX];

    /*
     * Blocking rename for boot/pre-load quarantine.
     *
     * Inputs: current directory is the parent containing old_name. Output:
     * nonzero only when asyncfatfs reports the renamed object's open alias and
     * the dirty FAT sectors have accepted a sync request. This is deliberately
     * used for quarantine names only; canonical name repair remains in the
     * normal foreground-pumped repair op.
     */
    memset(open_name, 0, sizeof(open_name));
    op_rename_done = 0u;
    while (!afatfs_renameObject_lfn(old_name,
                                    new_name,
                                    AFATFS_MATCH_CASE_INSENSITIVE,
                                    open_name,
                                    on_rename_complete)) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0u;
    }
    while (!op_rename_done) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0u;
    }
    if (open_name[0] == '\0')
        return 0u;
    while (!afatfs_sync()) {
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0u;
    }
    return 1u;
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

/*
 * Format one Kit-quarantine detail label without retaining diagnostic state.
 *
 * Inputs: the already parsed Kit slot and three fixed suffix bytes. Output:
 * one caller-local eight-byte code is passed to the private boot logger. Why:
 * timeout recovery must identify the concrete Kit and pending file family,
 * while quarantine remains a single ten-second operation and adds no SRAM.
 */
static void filesystem_bootLoggingSetKitDetail(uint16_t slot,
                                               const char suffix[3])
{
    char code[8] = { 'K', 'Q', '0', '0', '0', ' ', ' ', ' ' };

    code[2] = (char)('0' + ((slot / 100u) % 10u));
    code[3] = (char)('0' + ((slot / 10u) % 10u));
    code[4] = (char)('0' + (slot % 10u));
    if (suffix) {
        code[5] = suffix[0];
        code[6] = suffix[1];
        code[7] = suffix[2];
    }
    filesystem_bootLoggingSetDetail(code);
}

/*
 * Format one selected-Bank detail label without retaining diagnostic state.
 *
 * Inputs: the active root Bank slot and three suffix bytes. Output: one
 * caller-local `BKnnn...` label is passed to the private logger. Why: the
 * timeout record must identify Bank container work without changing its
 * deadline, masks, or persistent SRAM allocation.
 */
static void filesystem_bootLoggingSetBankDetail(const char suffix[3])
{
    char code[8] = { 'B', 'K', '0', '0', '0', ' ', ' ', ' ' };

    code[2] = (char)('0' + ((op_slot / 100u) % 10u));
    code[3] = (char)('0' + ((op_slot / 10u) % 10u));
    code[4] = (char)('0' + (op_slot % 10u));
    if (suffix) {
        code[5] = suffix[0];
        code[6] = suffix[1];
        code[7] = suffix[2];
    }
    filesystem_bootLoggingSetDetail(code);
}

/*
 * Format one Bank-local Scene detail label without retaining diagnostic state.
 *
 * Inputs: active Bank/child coordinates and one file-family letter. Output:
 * caller-local `BnnnSssX` is copied to the retained boot code only. Why: a
 * timeout can identify one selected child without changing Scene ownership,
 * parser progress, the Bank mask, or the enclosing BANKLOAD deadline.
 */
static void filesystem_bootLoggingSetBankSceneDetail(char family)
{
    char code[8] = { 'B', '0', '0', '0', 'S', '0', '0', family };

    code[1] = (char)('0' + ((op_slot / 100u) % 10u));
    code[2] = (char)('0' + ((op_slot / 10u) % 10u));
    code[3] = (char)('0' + (op_slot % 10u));
    code[5] = (char)('0' + ((op_bank_child_cursor / 10u) % 10u));
    code[6] = (char)('0' + (op_bank_child_cursor % 10u));
    filesystem_bootLoggingSetDetail(code);
}

/* Return nonzero only while the shared Scene loader owns a selected Bank child.
 * Why: root Scene Load shares these phases but must retain its existing
 * SCNELOAD diagnostic taxonomy and behaviour. */
static uint8_t filesystem_bankPayloadDetailActive(void)
{
    return (uint8_t)(current_op == FS_INTERNAL_OP_LOAD_BANK &&
                     op_bank_payload_active);
}

static uint8_t filesystem_kitMemberNameIsCanonical(const char *name)
{
    uint8_t stem_len = 0u;

    /*
     * Validate a Kit member filename before trusting kitset.kcg.
     *
     * Inputs: one `file=` component from kitset.kcg. Output: nonzero only
     * when the basename is present and fits the eight-character product
     * contract. The type extension is checked separately by storageTypes'
     * kitset finalizer, so this helper only rejects the host-edit case where a
     * long member name would force resident state to keep a separate FAT key.
     */
    if (!name || name[0] == '\0' || name[0] == '.')
        return 0u;
    while (name[stem_len] != '\0' && name[stem_len] != '.') {
        stem_len++;
        if (stem_len > STORAGE_KIT_DISPLAY_NAME_LEN)
            return 0u;
    }
    return (uint8_t)(stem_len > 0u && name[stem_len] == '.');
}

/*
 * Validate one current Kit directory without confusing content faults with
 * interrupted filesystem work.
 *
 * Inputs: currentDirectory is the selected Kit and kit_slot is its already
 * parsed numbered coordinate. Output: INVALID_CONTENT permits the established
 * err... quarantine; IO_ABORT stops traversal because no parser conclusion was
 * reached. Why: a timeout or lost FAT-ready state is not evidence that user
 * data is malformed and must never authorize a rename.
 */
static fs_kit_validation_result_t filesystem_validateCurrentKitBlocking(
    uint16_t kit_slot)
{
    storage_kitset_t kitset;
    kit_t scratch_kit;
    afatfsFilePtr_t file;
    char line[128];
    uint16_t line_len = 0u;
    uint8_t byte;
    uint8_t line_too_long = 0u;
    uint8_t saw_line_too_long = 0u;
    storage_status_t st;

    memset(&scratch_kit, 0, sizeof(scratch_kit));
    storage_kitsetInit(&kitset);
    filesystem_bootLoggingSetKitDetail(kit_slot, "KST");
    filesystem_reportBootSubstep(30u); /* open kitset.kcg */
    file = filesystem_blockOpen(STORAGE_KITSET_FILENAME);
    if (!file) {
        return filesystem_blockFsOk()
            ? FS_KIT_VALIDATION_INVALID_CONTENT
            : FS_KIT_VALIDATION_IO_ABORT;
    }
    filesystem_bootLoggingSetKitDetail(kit_slot, "KST");
    filesystem_reportBootSubstep(31u); /* stream/parse kitset.kcg */
    while (filesystem_blockRead(file, &byte, 1u) == 1u) {
        if (byte == '\r')
            continue;
        if (byte == '\n') {
            line[line_len] = '\0';
            if (!line_too_long) {
                st = storage_kitsetParseLine(&kitset, line, &scratch_kit);
                if (st != STORAGE_STATUS_OK) {
                    filesystem_bootLoggingSetKitDetail(kit_slot, "KST");
                    if (!filesystem_blockClose(file))
                        return FS_KIT_VALIDATION_IO_ABORT;
                    return FS_KIT_VALIDATION_INVALID_CONTENT;
                }
            }
            line_len = 0u;
            line_too_long = 0u;
            continue;
        }
        if (line_len + 1u < sizeof(line)) {
            line[line_len++] = (char)byte;
        } else {
            line_too_long = 1u;
            saw_line_too_long = 1u;
        }
    }
    if (!afatfs_feof(file)) {
        filesystem_bootLoggingSetKitDetail(kit_slot, "KST");
        (void)filesystem_blockClose(file);
        return FS_KIT_VALIDATION_IO_ABORT;
    }
    if (line_len != 0u && !line_too_long) {
        line[line_len] = '\0';
        st = storage_kitsetParseLine(&kitset, line, &scratch_kit);
        if (st != STORAGE_STATUS_OK) {
            filesystem_bootLoggingSetKitDetail(kit_slot, "KST");
            if (!filesystem_blockClose(file))
                return FS_KIT_VALIDATION_IO_ABORT;
            return FS_KIT_VALIDATION_INVALID_CONTENT;
        }
    }
    filesystem_bootLoggingSetKitDetail(kit_slot, "KST");
    filesystem_reportBootSubstep(32u); /* close kitset.kcg */
    if (!filesystem_blockClose(file))
        return FS_KIT_VALIDATION_IO_ABORT;
    if (line_too_long ||
        saw_line_too_long ||
        storage_kitsetFinalize(&kitset) != STORAGE_STATUS_OK)
        return FS_KIT_VALIDATION_INVALID_CONTENT;
    for (uint8_t i = 0u; i < STORAGE_KIT_SLOT_COUNT; i++) {
        if (!filesystem_kitMemberNameIsCanonical(kitset.instrument_file[i]))
            return FS_KIT_VALIDATION_INVALID_CONTENT;
        {
            char instrument_suffix[3] = {
                'I', '0', (char)('0' + i)
            };

            filesystem_bootLoggingSetKitDetail(kit_slot, instrument_suffix);
        }
        filesystem_reportBootSubstep((uint8_t)(40u + i));
        file = filesystem_blockOpenLfn(kitset.instrument_file[i]);
        if (!file) {
            return filesystem_blockFsOk()
                ? FS_KIT_VALIDATION_INVALID_CONTENT
                : FS_KIT_VALIDATION_IO_ABORT;
        }
        {
            char instrument_suffix[3] = {
                'I', '0', (char)('0' + i)
            };

            filesystem_bootLoggingSetKitDetail(kit_slot, instrument_suffix);
        }
        filesystem_reportBootSubstep((uint8_t)(50u + i));
        if (!filesystem_blockClose(file))
            return FS_KIT_VALIDATION_IO_ABORT;
    }
    return FS_KIT_VALIDATION_VALID;
}

static void filesystem_makeQuarantineName(char *dst,
                                          uint16_t capacity,
                                          const char *old_name)
{
    uint16_t out = 0u;
    const char prefix[] = "err";

    /*
     * Rename bad user-edited objects out of every loadable namespace.
     *
     * Inputs: the original visible FAT component. Output: `err...` plus as
     * much of the original component as fits. Product scanners reject this
     * prefix, so a bad Kit or owning Scene disappears from `.hcindex` while
     * its payload remains on the card for host-side diagnosis.
     */
    if (!dst || capacity == 0u)
        return;
    for (uint8_t i = 0u; prefix[i] != '\0' && out + 1u < capacity; i++)
        dst[out++] = prefix[i];
    if (old_name && old_name[0] != '\0') {
        uint16_t i = 0u;
        while (old_name[i] != '\0' && out + 1u < capacity)
            dst[out++] = old_name[i++];
    }
    dst[out] = '\0';
}

static afatfsOperationStatus_e filesystem_blockFindNextObject(
    afatfsFilePtr_t directory,
    afatfsObjectFinder_t *finder,
    afatfsObjectInfo_t *object)
{
    afatfsOperationStatus_e st;

    while (1) {
        st = afatfs_findNextObject(directory, finder, object);
        if (st != AFATFS_OPERATION_IN_PROGRESS)
            return st;
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return AFATFS_OPERATION_FAILURE;
    }
}

static uint8_t filesystem_residentNameIsBlank(const char *name)
{
    /*
     * Decide whether a resident display field should serialize as blank.
     *
     * Inputs: eight-cell Scene/Kit/Bank/Instrument display storage, or NULL.
     * Output: nonzero for absent names and the universal initialized
     * `none    ` placeholder. This prevents SRAM defaults from being mistaken
     * for loaded object provenance in the first-pass `/.hcnames` register.
     */
    if (!name)
        return 1u;
    if (name[0] == 'n' && name[1] == 'o' && name[2] == 'n' &&
        name[3] == 'e' &&
        (name[4] == ' ' || name[4] == '\0') &&
        (name[5] == ' ' || name[5] == '\0') &&
        (name[6] == ' ' || name[6] == '\0') &&
        (name[7] == ' ' || name[7] == '\0')) {
        return 1u;
    }
    for (uint8_t i = 0u; i < SCENE_OBJECT_DISPLAY_NAME_LEN; i++) {
        if (name[i] != ' ' && name[i] != '\0')
            return 0u;
    }
    return 1u;
}

static uint8_t filesystem_formatResidentNameLine(char *dst,
                                                 uint16_t cap,
                                                 const char *name,
                                                 uint8_t present)
{
    uint8_t len = 0u;

    /*
     * Format one fixed-order name-register row.
     *
     * Inputs: a resident eight-cell display field plus the caller's loaded
     * object predicate. Output: either the trimmed visible name and newline,
     * or a bare blank line when that register slot has no loaded object.
     * Trimming keeps root `/.hcnames` human-readable while the row number
     * preserves the exact Bank/Scene/Kit/Instrument coordinate.
     */
    if (!dst || cap < 2u)
        return 0u;
    dst[0] = '\0';
    if (present && !filesystem_residentNameIsBlank(name)) {
        int8_t last = -1;
        for (uint8_t i = 0u; i < SCENE_OBJECT_DISPLAY_NAME_LEN; i++) {
            if (name[i] != ' ' && name[i] != '\0')
                last = (int8_t)i;
        }
        if (last >= 0) {
            while (len <= (uint8_t)last &&
                   len + 1u < cap &&
                   len < SCENE_OBJECT_DISPLAY_NAME_LEN) {
                char c = name[len];
                dst[len] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
                len++;
            }
        }
    }
    if (len + 1u >= cap)
        return 0u;
    dst[len++] = '\n';
    dst[len] = '\0';
    return len;
}

static uint8_t filesystem_nextResidentNameLine(char *dst,
                                               uint16_t cap,
                                               uint16_t row)
{

    /*
     * Select one `/.hcnames` row from resident SRAM.
     *
     * Row order is the file contract: Bank, sixteen Scene names, sixteen Kit
     * names, then sixteen groups of six Instrument names. The selection uses
     * BankData's resident Scene-present mask so SRAM defaults and unloaded
     * Scene slots become blank rows instead of false provenance.
     */
    if (row == 0u) {
        return filesystem_formatResidentNameLine(dst, cap,
                                                 bank_displayName(),
                                                 bank_hasResidentBank());
    }
    row--;
    if (row < STORAGE_BANK_SCENE_MAX_SLOTS) {
        /*
         * Scene rows are no longer regenerated from SceneData.
         *
         * What: the boot-only generic writer emits an empty row for each
         * Scene it cannot identify from a card register. Why: scene_t no
         * longer duplicates root HCNAMES identity. Runtime Scene and Bank
         * operations preserve/update those rows through the shared cache,
         * which is the only path with the authoritative card name. Inputs:
         * current row/presence mask. Output: blank placeholder row. Affiliates:
         * first-card bootstrap and later targeted HCNAMES update state.
         */
        return filesystem_formatResidentNameLine(dst, cap, NULL, 0u);
    }
    /*
     * The generic boot serializer cannot reconstruct Kit/Instrument identity
     * from audio SRAM any more. Runtime targeted HCNAMES updates preserve and
     * replace those authoritative rows from the identity block instead.
     */
    (void)row;
    return filesystem_formatResidentNameLine(dst, cap, NULL, 0u);
}

/*
 * Quarantine only Kits proven invalid by the selected-content validator.
 *
 * Inputs: mounted root Kit namespace. Output: OK after a complete traversal or
 * IO_ABORT after any interrupted FAT operation. Why: an I/O abort cannot prove
 * a Kit is malformed, so it must leave the original directory intact rather
 * than reaching the err... rename path.
 */
static fs_kit_quarantine_result_t filesystem_quarantineKitLibraryBlocking(void)
{
    afatfsFilePtr_t root;
    afatfsFilePtr_t kit_dir;
    afatfsObjectFinder_t finder;
    afatfsObjectInfo_t object;
    afatfsOperationStatus_e st;
    char old_name[AFATFS_LONG_FILENAME_MAX + 1u];
    char err_name[AFATFS_LONG_FILENAME_MAX + 1u];
    uint16_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
    fs_kit_validation_result_t validation;

restart:
    filesystem_bootLoggingSetDetail("KQROOT  ");
    if (!filesystem_blockChdir(NULL))
        return FS_KIT_QUARANTINE_IO_ABORT;
    filesystem_bootLoggingSetDetail("KQROOT  ");
    root = filesystem_blockOpenDirLfn(STORAGE_ROOT_KIT);
    if (!root) {
        return filesystem_blockFsOk()
            ? FS_KIT_QUARANTINE_OK
            : FS_KIT_QUARANTINE_IO_ABORT;
    }
    filesystem_bootLoggingSetDetail("KQROOT  ");
    if (!filesystem_blockChdir(root)) {
        (void)filesystem_blockClose(root);
        return FS_KIT_QUARANTINE_IO_ABORT;
    }
    afatfs_findFirstObject(root, &finder);
    while (1) {
        filesystem_bootLoggingSetDetail("KQSCAN  ");
        st = filesystem_blockFindNextObject(root, &finder, &object);
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(root, &finder);
            (void)filesystem_blockClose(root);
            return FS_KIT_QUARANTINE_IO_ABORT;
        }
        if (object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(root, &finder);
            break;
        }
        if (object.id.kind != AFATFS_OBJECT_DIRECTORY ||
            !storage_parseNumberedFolder(object.id.displayName,
                                         &slot,
                                         display) ||
            slot >= STORAGE_KIT_MAX_SLOTS) {
            continue;
        }
        filesystem_copyLongComponent(old_name, sizeof(old_name),
                                     object.id.displayName);
        filesystem_bootLoggingSetKitDetail(slot, "DIR");
        kit_dir = filesystem_blockOpenDirLfn(old_name);
        if (!kit_dir) {
            afatfs_findLastObject(root, &finder);
            (void)filesystem_blockClose(root);
            return FS_KIT_QUARANTINE_IO_ABORT;
        }
        filesystem_bootLoggingSetKitDetail(slot, "DIR");
        if (!filesystem_blockChdir(kit_dir)) {
            (void)filesystem_blockClose(kit_dir);
            afatfs_findLastObject(root, &finder);
            (void)filesystem_blockClose(root);
            return FS_KIT_QUARANTINE_IO_ABORT;
        }
        validation = filesystem_validateCurrentKitBlocking(slot);
        if (validation == FS_KIT_VALIDATION_IO_ABORT) {
            afatfs_findLastObject(root, &finder);
            (void)filesystem_blockClose(kit_dir);
            (void)filesystem_blockClose(root);
            return FS_KIT_QUARANTINE_IO_ABORT;
        }
        if (validation == FS_KIT_VALIDATION_INVALID_CONTENT) {
            /*
             * Quarantine root Kit folders before `.hcindex` publication.
             *
             * Inputs: a numbered Kit directory whose kitset references a long
             * or missing member filename. Output: the folder is renamed to an
             * `errNNN ...` component in Kit/, so the normal slot scanner will
             * not publish a loadable row for data the firmware cannot
             * reconstruct from bounded resident names.
             */
            afatfs_findLastObject(root, &finder);
            filesystem_bootLoggingSetKitDetail(slot, "REN");
            if (!filesystem_blockChdir(root)) {
                (void)filesystem_blockClose(kit_dir);
                (void)filesystem_blockClose(root);
                return FS_KIT_QUARANTINE_IO_ABORT;
            }
            filesystem_bootLoggingSetKitDetail(slot, "REN");
            if (!filesystem_blockClose(kit_dir)) {
                (void)filesystem_blockClose(root);
                return FS_KIT_QUARANTINE_IO_ABORT;
            }
            if (!filesystem_blockClose(root))
                return FS_KIT_QUARANTINE_IO_ABORT;
            filesystem_makeQuarantineName(err_name, sizeof(err_name), old_name);
            filesystem_bootLoggingSetKitDetail(slot, "REN");
            if (!filesystem_blockRename(old_name, err_name))
                return FS_KIT_QUARANTINE_IO_ABORT;
            goto restart;
        }
        filesystem_bootLoggingSetKitDetail(slot, "DIR");
        if (!filesystem_blockChdir(root)) {
            afatfs_findLastObject(root, &finder);
            (void)filesystem_blockClose(kit_dir);
            (void)filesystem_blockClose(root);
            return FS_KIT_QUARANTINE_IO_ABORT;
        }
        filesystem_bootLoggingSetKitDetail(slot, "DIR");
        if (!filesystem_blockClose(kit_dir)) {
            afatfs_findLastObject(root, &finder);
            (void)filesystem_blockClose(root);
            return FS_KIT_QUARANTINE_IO_ABORT;
        }
    }
    filesystem_bootLoggingSetDetail("KQROOT  ");
    if (!filesystem_blockClose(root))
        return FS_KIT_QUARANTINE_IO_ABORT;
    filesystem_bootLoggingSetDetail("KQROOT  ");
    return filesystem_blockChdir(NULL)
        ? FS_KIT_QUARANTINE_OK
        : FS_KIT_QUARANTINE_IO_ABORT;
}

#if 0
/*
 * Retired runtime Bank-tree quarantine implementation.
 *
 * It remains here temporarily as an inactive historical reference for the
 * boot-only Kit quarantine helpers immediately above. Bank Load now validates
 * only selected children through the foreground-pumped shared Scene reader;
 * compiling this recursive blocking traversal would reintroduce the UI/audio
 * stall fixed by the Bank Load operation contract.
 */
static uint8_t filesystem_quarantineEmbeddedKitBlocking(
    const char *kit_name,
    const char *scene_name,
    afatfsFilePtr_t scene_dir,
    afatfsFilePtr_t scene_parent,
    uint8_t quarantine_scene)
{
    afatfsFilePtr_t kit_dir;
    char err_name[AFATFS_LONG_FILENAME_MAX + 1u];

    /*
     * Validate and quarantine one embedded `Kit ...` child.
     *
     * Inputs: current directory is the owning Scene; scene_dir is that exact
     * directory handle, and scene_parent contains the Scene when the Scene
     * itself also needs quarantine. Output: the bad Kit is renamed out of the
     * `Kit ` namespace, and optionally the owning Scene is renamed out of its
     * numbered namespace.
     */
    filesystem_reportBootSubstep(20u); /* open embedded Kit directory */
    kit_dir = filesystem_blockOpenDirLfn(kit_name);
    if (!kit_dir)
        return 0u;
    filesystem_reportBootSubstep(21u); /* enter embedded Kit directory */
    if (!filesystem_blockChdir(kit_dir)) {
        (void)filesystem_blockClose(kit_dir);
        return 0u;
    }
    /*
     * chdir copied the Kit directory into asyncfatfs.currentDirectory, so the
     * explicit Kit handle is no longer needed for validation. Release it
     * before opening kitset.kcg or an Instrument member; otherwise selected
     * Bank + Scene + Kit already consume all three pool handles and the first
     * payload-file open repeats the same impossible fourth-handle wait that
     * previously occurred at the Kit-directory open.
     */
    filesystem_reportBootSubstep(24u); /* release entered Kit handle */
    if (!filesystem_blockClose(kit_dir))
        return 0u;
    kit_dir = NULL;
    filesystem_reportBootSubstep(22u); /* validate embedded Kit payload */
    if (filesystem_validateCurrentKitBlocking()) {
        filesystem_reportBootSubstep(23u); /* return to owning Scene */
        if (!filesystem_blockChdir(scene_dir))
            return 0u;
        return 1u;
    }
    filesystem_makeQuarantineName(err_name, sizeof(err_name), kit_name);
    filesystem_reportBootSubstep(60u); /* return to Scene for quarantine */
    if (!filesystem_blockChdir(scene_dir))
        return 0u;
    filesystem_reportBootSubstep(62u); /* rename invalid Kit */
    if (!filesystem_blockRename(kit_name, err_name))
        return 0u;
    if (quarantine_scene) {
        filesystem_makeQuarantineName(err_name, sizeof(err_name), scene_name);
        filesystem_reportBootSubstep(63u); /* return root before Scene rename */
        if (!filesystem_blockChdir(NULL))
            return 0u;
        filesystem_reportBootSubstep(64u); /* re-enter Scene parent */
        if (!filesystem_blockChdir(scene_parent))
            return 0u;
        filesystem_reportBootSubstep(65u); /* rename owning Scene */
        if (!filesystem_blockRename(scene_name, err_name))
            return 0u;
    }
    return 0u;
}

static uint8_t filesystem_quarantineScenesInParentBlocking(
    afatfsFilePtr_t parent,
    uint8_t bank_local)
{
    afatfsFilePtr_t scene_dir;
    afatfsObjectFinder_t finder;
    afatfsObjectInfo_t object;
    afatfsOperationStatus_e st;
    char scene_name[AFATFS_LONG_FILENAME_MAX + 1u];
    char kit_name[AFATFS_LONG_FILENAME_MAX + 1u];
    uint16_t slot;
    uint8_t child_slot;
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];

restart:
    filesystem_reportBootSubstep(10u); /* enter/rescan selected Bank */
    if (!filesystem_blockChdir(parent))
        return 0u;
    afatfs_findFirstObject(parent, &finder);
    while (1) {
        filesystem_reportBootSubstep(11u); /* scan next Bank-local Scene */
        st = filesystem_blockFindNextObject(parent, &finder, &object);
        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(parent, &finder);
            return 0u;
        }
        if (object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(parent, &finder);
            return 1u;
        }
        if (object.id.kind != AFATFS_OBJECT_DIRECTORY)
            continue;
        if (bank_local) {
            if (!storage_parseBankSceneFolder(object.id.displayName,
                                              &child_slot,
                                              display))
                continue;
        } else if (!storage_parseNumberedFolder(object.id.displayName,
                                                &slot,
                                                display) ||
                   slot >= STORAGE_SCENE_MAX_SLOTS) {
            continue;
        }
        filesystem_copyLongComponent(scene_name, sizeof(scene_name),
                                     object.id.displayName);
        filesystem_reportBootSubstep(12u); /* open Bank-local Scene */
        scene_dir = filesystem_blockOpenDirLfn(scene_name);
        if (!scene_dir) {
            afatfs_findLastObject(parent, &finder);
            return 0u;
        }
        filesystem_reportBootSubstep(13u); /* enter Bank-local Scene */
        if (!filesystem_blockChdir(scene_dir)) {
            (void)filesystem_blockClose(scene_dir);
            afatfs_findLastObject(parent, &finder);
            return 0u;
        }
        kit_name[0] = '\0';
        {
            afatfsObjectFinder_t child_finder;
            afatfsObjectInfo_t child;
            afatfs_findFirstObject(scene_dir, &child_finder);
            while (1) {
                filesystem_reportBootSubstep(14u); /* scan Scene child */
                st = filesystem_blockFindNextObject(scene_dir,
                                                    &child_finder,
                                                    &child);
                if (st == AFATFS_OPERATION_FAILURE) {
                    afatfs_findLastObject(scene_dir, &child_finder);
                    (void)filesystem_blockClose(scene_dir);
                    afatfs_findLastObject(parent, &finder);
                    return 0u;
                }
                if (child.id.kind == AFATFS_OBJECT_NONE) {
                    afatfs_findLastObject(scene_dir, &child_finder);
                    break;
                }
                if (child.id.kind == AFATFS_OBJECT_DIRECTORY &&
                    filesystem_nameStartsWithKitSpace(child.id.displayName)) {
                    filesystem_copyLongComponent(kit_name, sizeof(kit_name),
                                                 child.id.displayName);
                    afatfs_findLastObject(scene_dir, &child_finder);
                    break;
                }
            }
        }
        if (kit_name[0] != '\0') {
            filesystem_reportBootSubstep(15u); /* process embedded Kit */
            if (!filesystem_quarantineEmbeddedKitBlocking(kit_name,
                                                           scene_name,
                                                           scene_dir,
                                                           parent,
                                                           1u)) {
                filesystem_reportBootSubstep(71u);
                (void)filesystem_blockClose(scene_dir);
                afatfs_findLastObject(parent, &finder);
                goto restart;
            }
        }
        filesystem_reportBootSubstep(72u); /* close valid Scene handle */
        if (!filesystem_blockClose(scene_dir)) {
            afatfs_findLastObject(parent, &finder);
            return 0u;
        }
        filesystem_reportBootSubstep(73u); /* return to selected Bank */
        if (!filesystem_blockChdir(parent)) {
            afatfs_findLastObject(parent, &finder);
            return 0u;
        }
    }
}

static uint8_t filesystem_quarantineBankKitsBlocking(uint16_t slot)
{
    afatfsFilePtr_t bank_root;
    afatfsFilePtr_t bank_dir;
    uint8_t ok;

    filesystem_reportBootSubstep(1u); /* return root before Bank reopen */
    if (!filesystem_blockChdir(NULL))
        return 0u;
    filesystem_reportBootSubstep(2u); /* open Bank root */
    bank_root = filesystem_blockOpenDirLfn(STORAGE_ROOT_BANK);
    if (!bank_root)
        return 1u;
    filesystem_reportBootSubstep(3u); /* enter Bank root */
    if (!filesystem_blockChdir(bank_root)) {
        (void)filesystem_blockClose(bank_root);
        return 0u;
    }
    filesystem_makeCanonicalNumberedDir(
        op_root_open_name,
        sizeof(op_root_open_name),
        slot,
        filesystem_cachedLibraryName(FS_NAME_CACHE_BANK, slot));
    filesystem_reportBootSubstep(4u); /* open selected Bank */
    bank_dir = filesystem_blockOpenDirLfn(op_root_open_name);
    if (!bank_dir) {
        (void)filesystem_blockClose(bank_root);
        return 1u;
    }
    /*
     * Release the top-level Bank handle before descending into the selected
     * Bank's Scene and embedded Kit hierarchy.
     *
     * asyncfatfs has exactly three reusable open-file handles. At this point
     * bank_root and bank_dir occupied two; opening a Scene occupied the third,
     * so the subsequent embedded-Kit open waited forever for a fourth handle
     * that could not be released while the same blocking call was active.
     * afatfs_chdir(bank_root) already copied the directory state, and bank_dir
     * now identifies the selected child independently, so bank_root has no
     * remaining ownership role. Closing it here leaves the required three-slot
     * descent budget: selected Bank, Scene, then Kit.
     */
    filesystem_reportBootSubstep(6u); /* release Bank-root handle budget */
    if (!filesystem_blockClose(bank_root)) {
        (void)filesystem_blockClose(bank_dir);
        return 0u;
    }
    bank_root = NULL;
    filesystem_reportBootSubstep(5u); /* scan/validate selected Bank */
    ok = filesystem_quarantineScenesInParentBlocking(bank_dir, 1u);
    filesystem_reportBootSubstep(80u); /* close selected Bank handle */
    if (!filesystem_blockClose(bank_dir)) {
        return 0u;
    }
    filesystem_reportBootSubstep(82u); /* final return to root */
    return (uint8_t)(ok && filesystem_blockChdir(NULL));
}
#endif

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
** Retired generic asyncfatfs File/Dir test operations
**
** Kept out of the build with their former cache state. These were diagnostic
** menu paths only; musical library operations use the dedicated 1,000-row
** `.hcindex` cache and staging workspace instead.
** ----------------------------------------------------------------------- */
#if 0
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

#endif

/* =======================================================================
** Public API
** ======================================================================= */

/*
 * Discard facade ownership after the diagnostic transport/filesystem reset.
 *
 * What: clears the generic owner, callbacks, open-handle coordinates, deferred
 * rebuild state, autonomous-writer authorization/recovery, and retained
 * mutation production without touching the preserved eight-byte logger record.
 * Why: afatfs_destroy(true) invalidates
 * every handle/callback target owned by the timed-out operation, so none may
 * leak into the remount or root log write. Inputs are abandoned boot facade
 * state; output is an idle facade suitable for afatfs_init() and one new
 * operation. Affiliates: sdcard_abortTransferForBootLog(),
 * filesystem_writeBootFailureLogBlocking(), and filesystem_start().
 */
static void filesystem_resetFacadeForBootLogRecovery(void)
{
    current_op = FS_INTERNAL_OP_NONE;
    status = FS_STATUS_IDLE;
    op_phase = 0u;
    completion_callback = NULL;
    op_file = NULL;
    op_file_ready = false;
    op_close_done = false;
    op_kit_root_dir = NULL;
    op_kit_slot_dir = NULL;
    op_library_index_rebuild_pending = 0u;
    op_library_index_rebuild_kind = FS_NAME_CACHE_NONE;
    op_library_index_rebuild_callback = NULL;
    op_autosave_writer.target_file = NULL;
    op_autosave_writer.target_ready = 0u;
    fs_autosave_writer_armed = 0u;
    fs_autosave_writer_boot_ready = 0u;
    fs_autosave_recovery_pending = 0u;
    fs_autosave_enabled = 1u;
    fs_autosave_runtime_ready = 0u;
    fs_autosave_setup_pending = 0u;
    fs_autosave_setup_failed = 0u;
    fs_autosave_transaction_active = 0u;
    fs_autosave_discard_pending = 0u;
    autosave_setMutationTrackingEnabled(0u);
    /*
     * Abandon autonomous settings scheduling with the destroyed FAT facade.
     * Input: boot timeout recovery invalidated every outstanding handle.
     * Output: no stale runtime writer can start on the remount; a later real
     * settings load reconstructs data. Affiliates: filesystem_initAfterCardReady().
     */
    fs_settings_dirty = 0u;
    fs_settings_runtime_ready = 0u;
    fs_settings_next_due_tick = 0u;
    fs_settings_change_revision = 0u;
    op_settings_change_revision = 0u;
    op_settings_write_active = 0u;
    memset(fs_error_code, 0, sizeof(fs_error_code));
}

void filesystem_initAfterCardReady(void)
{
    current_op = FS_INTERNAL_OP_NONE;
    status = FS_STATUS_IDLE;
    memset(fs_error_code, 0, sizeof(fs_error_code));
    /*
     * Initialize autonomous settings ownership for this mounted-card session.
     *
     * Input: a fresh AsyncFATFS facade before settings load. Output: no dirty
     * work and no runtime authorization until main.c finishes boot. Why: prior
     * remount/recovery state must not leak a background write into scans or
     * initial Bank loading. Affiliates: filesystem_enableRuntimeSettingsWrites().
     */
    fs_settings_dirty = 0u;
    fs_settings_runtime_ready = 0u;
    fs_settings_next_due_tick = 0u;
    fs_settings_change_revision = 0u;
    op_settings_change_revision = 0u;
    op_settings_write_active = 0u;
    /*
     * Reset hidden-writer policy to its documented cold default.
     *
     * Input: fresh mounted facade before settings.cfg overlay. Output: ON is
     * retained as a preference but runtime/setup authorization remains closed.
     * Why: main.c must apply the loaded preference before any autosave ensure,
     * and no stale transaction flag may survive a remount. Affiliates:
     * filesystem_setAutosaveEnabled() and the boot wrapper.
     */
    fs_autosave_enabled = 1u;
    fs_autosave_runtime_ready = 0u;
    fs_autosave_setup_pending = 0u;
    fs_autosave_setup_failed = 0u;
    fs_autosave_transaction_active = 0u;
    fs_autosave_discard_pending = 0u;
    fs_autosave_writer_boot_ready = 0u;
    fs_autosave_writer_armed = 0u;
    fs_autosave_recovery_pending = 0u;
    autosave_setMutationTrackingEnabled(0u);
    afatfs_init();
}

void filesystem_markSettingsDirty(void)
{
    /*
     * Record one settings/provenance change and restart trailing debounce.
     *
     * Inputs: live time_sysTick after a changed Global Menu byte or successful
     * Bank/Scene completion. Outputs: dirty/revision state only; no file is
     * opened or polled. Why: all runtime SD work must remain asynchronous and
     * multiple changes inside one second should coalesce. Affiliates:
     * SETTINGS_AUTOWRITE_DEBOUNCE_MS and the idle scheduler below.
     */
    fs_settings_change_revision++;
    fs_settings_dirty = 1u;
    fs_settings_next_due_tick = (uint16_t)(
        time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
}

void filesystem_enableRuntimeSettingsWrites(void)
{
    /*
     * Open the autonomous settings gate after all pre-audio filesystem work.
     *
     * Input: main.c's mounted-card boot completion boundary. Output: pending
     * boot provenance receives a fresh full debounce; clean state performs no
     * work. Why: a successful boot Bank Load must eventually update sources,
     * but it must not inject an invisible writer into scan/load ownership.
     * Affiliates: filesystem_markSettingsDirty() and main.c.
     */
    fs_settings_runtime_ready = 1u;
    /*
     * The same boot exit authorizes asynchronous AutoSave setup scheduling.
     *
     * Input: all blocking Bank/fallback/optional ensure work has released the
     * facade. Output: an enabled resident Bank lacking setup may queue ensure
     * on a later idle tick; OFF remains inert. Why: runtime re-enable and a
     * later Bank after fallback must never call the blocking boot wrapper.
     * Affiliates: filesystem_autosaveWriterSchedule_tick().
     */
    fs_autosave_runtime_ready = 1u;
    if (fs_autosave_enabled && bank_hasResidentBank() &&
        !fs_autosave_writer_boot_ready && !fs_autosave_setup_failed) {
        fs_autosave_setup_pending = 1u;
    }
    if (fs_settings_dirty) {
        fs_settings_next_due_tick = (uint16_t)(
            time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
    }
}

uint8_t filesystem_autosaveEnabled(void)
{
    /*
     * Report policy state without initiating hidden-record filesystem work.
     *
     * Input: none. Output: the normalized OFF/ON in-memory preference. Why:
     * boot can decide whether its optional ensure is authorized without making
     * a getter perform validation, creation, or scheduling. Affiliates:
     * filesystem_setAutosaveEnabled() and main.c's pre-audio setup ladder.
     */
    return fs_autosave_enabled;
}

void filesystem_setAutosaveEnabled(uint8_t enabled)
{
    enabled = enabled ? 1u : 0u;

    /*
     * Apply one AutoSave preference transition without synchronous card I/O.
     *
     * Inputs: normalized setting, current autonomous transaction state, and
     * resident/runtime lifecycle. Outputs: OFF revokes producer/scheduler
     * authorization and clears SRAM work now or after an active transform;
     * ON queues runtime setup only when a Bank exists. Hidden files are never
     * opened/deleted by this call. Why: Menu commits and boot settings need one
     * defensive policy boundary. Affiliates: idle setup/drain scheduler and
     * filesystem_autosaveWriterCompleted().
     */
    if (enabled == fs_autosave_enabled) {
        if (enabled && fs_autosave_runtime_ready &&
            bank_hasResidentBank() && !fs_autosave_writer_boot_ready &&
            !fs_autosave_setup_failed) {
            fs_autosave_setup_pending = 1u;
        }
        return;
    }
    fs_autosave_enabled = enabled;
    if (!enabled) {
        autosave_setMutationTrackingEnabled(0u);
        fs_autosave_setup_pending = 0u;
        fs_autosave_setup_failed = 0u;
        fs_autosave_recovery_pending = 0u;
        fs_autosave_writer_armed = 0u;
        fs_autosave_writer_boot_ready = 0u;
        if (fs_autosave_transaction_active) {
            fs_autosave_discard_pending = 1u;
        } else {
            autosave_discardDirtyMask();
            fs_autosave_discard_pending = 0u;
        }
        return;
    }
    fs_autosave_discard_pending = 0u;
    fs_autosave_setup_failed = 0u;
    if (fs_autosave_runtime_ready && bank_hasResidentBank())
        fs_autosave_setup_pending = 1u;
}

uint8_t filesystem_initCardAndMountBlocking(void)
{
    uint8_t sd_init_result;

    /*
     * Mount precedes every facade operation, so it owns an explicit code.
     *
     * Inputs: the active pre-audio logging window. Output: MOUNTSD plus a
     * fresh deadline covering settle, card initialization, and asyncfatfs
     * mount polling. Why: filesystem_start() cannot arm work before the FAT
     * facade exists. Affiliates: filesystem_bootLoggingBegin() and the mount
     * initialization loop below.
     */
    /*
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     */
    filesystem_bootLoggingArm("MOUNTSD ");
    fs_boot_detected_unsupported_card = 0;
    fs_last_mount_result = FS_MOUNT_RESULT_UNKNOWN;
    spi_sd_set_slow();
    /*
     * Let a still-powered SD controller settle with a valid idle bus.
     *
     * Inputs: slow SPI GPIO setup has deasserted CS, driven clock low, and
     * driven MOSI high; TIM6 is already running at 1 kHz. Output: a fixed
     * 250 ms pre-CMD0 interval before SD_init() supplies the required idle
     * clocks. This targets intermittent rapid-restart hangs that disappear
     * when diagnostic LCD drains or a longer power-off adds the same latency.
     * It is boot-only and occurs before audio initialization.
     */
    timebase_holdPreAudioMs(FS_SD_PREINIT_SETTLE_MS);
    sd_init_result = SD_init();
    /*
     * SD_init() owns bounded command loops rather than cooperative FAT polls.
     * Input is its returned result plus elapsed boot time; output latches a
     * timeout before any mount/failure branch continues. Why: the watchdog
     * cannot preempt a C call, but it must still classify an over-deadline
     * return as MOUNTSD. Affiliate: filesystem_bootLoggingPollDeadline().
     */
    if (filesystem_bootLoggingPollDeadline()) {
        fs_last_mount_result = FS_MOUNT_RESULT_CARD_INIT_FAILED;
        filesystem_bootLoggingOperationDone();
        return 0u;
    }
    if (sd_init_result != 0u) {
        fs_last_mount_result = (sd_init_result == 1u) ?
            FS_MOUNT_RESULT_NO_CARD : FS_MOUNT_RESULT_CARD_INIT_FAILED;
        filesystem_bootLoggingOperationDone();
        return 0;
    }

    spi_sd_set_fast();
    filesystem_initAfterCardReady();
    while (afatfs_getFilesystemState() ==
               AFATFS_FILESYSTEM_STATE_INITIALIZATION &&
           !filesystem_bootLoggingPollDeadline())
        filesystem_tick();

    if (afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_READY) {
        fs_last_mount_result = FS_MOUNT_RESULT_READY;
        filesystem_bootLoggingOperationDone();
        return 1;
    }

    if (afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_FATAL &&
        filesystem_detectUnsupportedCardLayout()) {
        fs_boot_detected_unsupported_card = 1;
        fs_last_mount_result = FS_MOUNT_RESULT_UNSUPPORTED_CARD;
    } else {
        fs_last_mount_result = FS_MOUNT_RESULT_MOUNT_FAILED;
    }

    /*
     * Unsupported-layout inspection is synchronous but bounded internally.
     * Input is its returned mount classification plus current elapsed time;
     * output upgrades an over-deadline return to the same MOUNTSD timeout.
     * Why: every returned component of mount must honor the operation budget
     * even when it did not call the cooperative FAT poll. Affiliate:
     * filesystem_detectUnsupportedCardLayout().
     */
    (void)filesystem_bootLoggingPollDeadline();
    filesystem_bootLoggingOperationDone();
    return 0;
}

uint8_t filesystem_writeBootFailureLogBlocking(void)
{
    /*
     * Make one bounded, best-effort durable report after confirmed boot failure.
     *
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging. It
     * must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     *
     * What: abandons the active or failed SD state, destroys dirty asyncfatfs
     * state, resets facade ownership, reinitializes/remounts the card, and
     * runs the ordinary eight-byte writer plus sync gate. Why: a caller has
     * already confirmed a timeout or boot filesystem failure, so it must not
     * be silently acknowledged merely because the watchdog did not expire.
     * Inputs are the retained detail code and active boot logging window;
     * output is nonzero only after bootlog.bin closes and syncs. Dirty abandon
     * can leave failed work partially represented on card and is acceptable
     * only in this DEV_MODE_LOGGING build. The recovery has one ten-second
     * ceiling, never retries, never invokes an abandoned callback, and failure
     * must not prevent main.c from continuing to audio. Affiliates:
     * sdcard_abortTransferForBootLog(), afatfs_destroy(true), SD_init(),
     * filesystem_writeBootLog_tick(), and filesystem_bootLoggingEnd().
     */
#if DEV_MODE_LOGGING
    uint8_t sd_init_result;
    uint8_t write_ok = 0u;

    if (!fs_boot_logging_active || fs_boot_logging_recovery)
        return 0u;

    fs_boot_logging_recovery = 1u;
    fs_boot_logging_recovery_failed = 0u;
    fs_boot_logging_recovery_started_tick = time_sysTick;

    sdcard_abortTransferForBootLog();
    (void)afatfs_destroy(true);
    filesystem_resetFacadeForBootLogRecovery();

    spi_sd_set_slow();
    timebase_holdPreAudioMs(FS_SD_PREINIT_SETTLE_MS);
    if (filesystem_bootLoggingPollDeadline())
        goto recovery_failed;
    sd_init_result = SD_init();
    if (sd_init_result != 0u ||
        filesystem_bootLoggingPollDeadline())
        goto recovery_failed;

    spi_sd_set_fast();
    filesystem_initAfterCardReady();
    while (afatfs_getFilesystemState() ==
               AFATFS_FILESYSTEM_STATE_INITIALIZATION &&
           !fs_boot_logging_recovery_failed) {
        filesystem_tick();
    }
    if (fs_boot_logging_recovery_failed ||
        afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
        goto recovery_failed;

    if (!filesystem_start(FS_INTERNAL_OP_WRITE_BOOT_LOG,
                          FS_FILE_SETTINGS, 0u, NULL))
        goto recovery_failed;
    while (status == FS_STATUS_BUSY &&
           !fs_boot_logging_recovery_failed) {
        filesystem_tick();
    }
    write_ok = (uint8_t)(
        !fs_boot_logging_recovery_failed && status == FS_STATUS_DONE);
    if (write_ok)
        filesystem_ack();
    else
        goto recovery_failed;

    fs_boot_logging_recovery = 0u;
    fs_boot_logging_recovery_failed = 0u;
    return 1u;

recovery_failed:
    /*
     * Bound failure cleanup without trying to close/flush another stuck owner.
     * Inputs are any remount/write failure or recovery expiry; output is an
     * idle, unmounted facade and zero. Why: a retry could reproduce the splash
     * hang this feature exists to escape. Affiliates: main.c's common timeout
     * continuation and filesystem_bootLoggingEnd().
     */
    sdcard_abortTransferForBootLog();
    (void)afatfs_destroy(true);
    filesystem_resetFacadeForBootLogRecovery();
    fs_boot_logging_recovery = 0u;
    return 0u;
#else
    return 0u;
#endif
}

static void filesystem_settingsWriterCompleted(void)
{
    /*
     * Complete an invisible settings.cfg write without involving Preset/Menu.
     *
     * Inputs: terminal SAVE_GLOBALS status after filesystem_complete() has
     * conditionally acknowledged the captured revision. Outputs: DONE simply
     * acknowledges; ERROR preserves/creates dirty work and restarts the
     * one-second retry deadline before acknowledging. Why: an autonomous
     * callback must not leave DONE/ERROR for an unrelated foreground caller or
     * tight-loop on media failure. Affiliates: filesystem_settingsWriterSchedule_tick().
     */
    if (status != FS_STATUS_DONE) {
        fs_settings_dirty = 1u;
        fs_settings_next_due_tick = (uint16_t)(
            time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
    }
    filesystem_ack();
}

static void filesystem_settingsWriterSchedule_tick(void)
{
    uint16_t now;

    /*
     * Start one debounced settings stream only from an idle mounted facade.
     *
     * Inputs: runtime gate, dirty/deadline state, and ready AsyncFATFS volume.
     * Output: one existing FS_INTERNAL_OP_SAVE_GLOBALS operation with a private
     * completion callback. Why: settings persistence neither borrows HCNAMES
     * nor needs a Load/Save-page pause, but it must never steal an accepted
     * foreground operation. Affiliates: filesystem_tick() and the autosave
     * scheduler, which receives the remaining idle opportunity afterwards.
     */
    if (!fs_settings_runtime_ready || !fs_settings_dirty ||
        afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY) {
        return;
    }
    now = time_sysTick;
    if ((uint16_t)(now - fs_settings_next_due_tick) >= 0x8000u)
        return;
    (void)filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS,
                           FS_FILE_SETTINGS, 0u,
                           filesystem_settingsWriterCompleted);
}

static void filesystem_autosaveWriterCompleted(void)
{
    /*
     * This callback belongs only to the autonomous writer. filesystem_complete
     * has already published its terminal status, so acknowledge it here rather
     * than leaving DONE/ERROR for a Menu/Preset caller that never requested it.
     * Input is terminal status plus Autosave.c's persistent canonical mask after
     * the final flush. Output: successful initial validation clears recovery;
     * remaining dirtiness arms the 250 ms continuation, a successful clean mask
     * disarms entirely, and errors retry after five seconds while preserving
     * recovery/rollback work. Why: clean state must perform no periodic file
     * operations. Affiliates: drain phase 22 and the scheduler below.
     */
    /*
     * TERMINAL is recorded before this callback resets lifecycle ownership.
     * flags bit 0 is the actual shared-facade terminal status; value is unused.
     * Why: an I/O error, a failed final sync, or a later policy transition must
     * remain distinguishable from a successful published generation in the
     * trace, never be masked by scheduler acknowledgement below.
     */
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_TERMINAL,
                         (uint8_t)(status == FS_STATUS_DONE ? 1u : 0u), 0u);
    fs_autosave_transaction_active = 0u;
    /*
     * OFF takes ownership at the first safe post-transform boundary.
     *
     * Inputs: an operation that was already active when policy changed and the
     * deferred-discard latch. Outputs: canonical SRAM work is cleared only now
     * that no CRC/copy consumes it; every scheduler flag remains disabled and
     * terminal status is acknowledged. Why: clearing mid-stream would change
     * CRC-covered mask bytes. Affiliates: filesystem_setAutosaveEnabled().
     */
    if (!fs_autosave_enabled) {
        if (fs_autosave_discard_pending) {
            autosave_discardDirtyMask();
            fs_autosave_discard_pending = 0u;
        }
        fs_autosave_writer_armed = 0u;
        fs_autosave_recovery_pending = 0u;
        fs_autosave_writer_boot_ready = 0u;
        filesystem_ack();
        return;
    }
    if (status == FS_STATUS_DONE) {
        fs_autosave_recovery_pending = 0u;
        if (autosave_maskHasDirty()) {
            fs_autosave_next_due_tick = (uint16_t)(
                time_sysTick + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS);
            fs_autosave_writer_armed = 1u;
        } else {
            fs_autosave_writer_armed = 0u;
        }
    } else {
        fs_autosave_next_due_tick = (uint16_t)(
            time_sysTick + AUTOSAVE_WRITER_INTERVAL_MS);
        fs_autosave_writer_armed = 1u;
    }
    filesystem_ack();
}

static void filesystem_autosaveSetupCompleted(void)
{
    uint8_t setup_ok = (uint8_t)(status == FS_STATUS_DONE);

    /*
     * Publish one asynchronous runtime ensure only after its durable flush.
     *
     * Inputs: terminal ensure status, current policy, and resident Bank state.
     * Outputs: success acknowledges then enables recovery/tracking and marks a
     * complete live Bank snapshot; failure/OFF leaves authorization revoked.
     * Why: runtime re-enable must use the foreground-pumped ensure state machine
     * without exposing partially created files to the drain scheduler.
     * Affiliates: filesystem_setAutosaveEnabled(), Autosave whole-Bank marker,
     * and filesystem_autosaveWriterSchedule_tick().
     */
    filesystem_ack();
    if (!setup_ok || !fs_autosave_enabled || !bank_hasResidentBank()) {
        /* A mounted/resident failure waits for an explicit OFF/ON transition
         * or a later no-Bank/new-Bank lifecycle instead of retrying every idle
         * tick. Inputs are terminal setup conditions; output is the retry
         * suppression latch. Affiliate: scheduler condition below. */
        fs_autosave_setup_failed = (uint8_t)(
            fs_autosave_enabled && bank_hasResidentBank());
        fs_autosave_writer_boot_ready = 0u;
        fs_autosave_recovery_pending = 0u;
        fs_autosave_writer_armed = 0u;
        autosave_setMutationTrackingEnabled(0u);
        return;
    }
    fs_autosave_writer_boot_ready = 1u;
    fs_autosave_setup_failed = 0u;
    fs_autosave_recovery_pending = 1u;
    fs_autosave_writer_armed = 0u;
    autosave_setMutationTrackingEnabled(1u);
    autosave_markResidentBankDirty();
}

static void filesystem_autosaveWriterSchedule_tick(void)
{
    uint16_t now;

    /*
     * This check is reached only while the facade is idle. Load/Save pages own
     * the library-name cache by contract, so they suppress new starts; an
     * already-running writer is deliberately never preempted mid-commit.
    */
    if (!fs_autosave_enabled) {
        /*
         * Defensive policy fall-through before every hidden-file start.
         * Input: OFF from settings/Menu. Output: no ensure, validation, read,
         * or write can begin even if stale ready/recovery flags exist. An
         * active transform is handled by its callback, not this idle path.
         * Affiliates: filesystem_setAutosaveEnabled().
         */
        autosave_setMutationTrackingEnabled(0u);
        fs_autosave_setup_pending = 0u;
        fs_autosave_setup_failed = 0u;
        fs_autosave_writer_armed = 0u;
        fs_autosave_recovery_pending = 0u;
        fs_autosave_writer_boot_ready = 0u;
        if (!fs_autosave_transaction_active && fs_autosave_discard_pending) {
            autosave_discardDirtyMask();
            fs_autosave_discard_pending = 0u;
        }
        return;
    }
    if (!bank_hasResidentBank()) {
        /*
         * Revoke a formerly authorized producer once on Bank-session loss.
         *
         * Input: no resident Bank plus any prior ready/recovery/armed state.
         * Output: tracking and all scheduler flags clear; subsequent idle ticks
         * perform no critical section. Why: dirty bits have no valid Bank owner
         * in fallback state. Affiliates: BankData's lifecycle flag and boot
         * ensure, which is the only path that can authorize a new session.
         */
        if (fs_autosave_writer_boot_ready ||
            fs_autosave_recovery_pending || fs_autosave_writer_armed) {
            autosave_setMutationTrackingEnabled(0u);
        }
        fs_autosave_writer_armed = 0u;
        fs_autosave_recovery_pending = 0u;
        fs_autosave_writer_boot_ready = 0u;
        fs_autosave_setup_pending = 0u;
        fs_autosave_setup_failed = 0u;
        if (!fs_autosave_transaction_active)
            autosave_discardDirtyMask();
        return;
    }
    /*
     * Runtime ON with a resident Bank but no authorized pair queues the
     * existing create-only ensure asynchronously. Inputs: post-boot runtime
     * gate and lifecycle flags. Output: no blocking call; setup starts below
     * only when facade/menu/card conditions are safe. Affiliates: runtime
     * re-enable and later Bank-after-fallback sessions.
     */
    if (fs_autosave_runtime_ready && !fs_autosave_writer_boot_ready &&
        !fs_autosave_setup_failed)
        fs_autosave_setup_pending = 1u;
    if (fs_autosave_setup_pending) {
        if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY ||
            menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
            return;
        }
        if (filesystem_start(FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES,
                             FS_FILE_SETTINGS, 0u,
                             filesystem_autosaveSetupCompleted)) {
            fs_autosave_setup_pending = 0u;
        }
        return;
    }
    /*
     * A resident Bank alone is insufficient during boot: the blocking
     * create-only pass must establish A/B before this runtime state machine
     * can validate, copy, or recover either record. A failed create keeps the
     * writer inactive instead of running an autonomous transaction mid-boot.
     */
    if (!fs_autosave_writer_boot_ready)
        return;
    /* Do not queue an LFN operation until the existing idle poll has mounted. */
    if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
        return;
    /*
     * Fall through before arming when neither boot recovery nor SRAM work
     * exists. Inputs are the one-time recovery flag and canonical mask. Output:
     * a clean post-recovery writer remains disarmed and never calls
     * filesystem_start(). Why: periodic generation/CRC/file traffic is not an
     * autosave operation. A later scalar mutation is observed on a later tick
     * and receives the normal five-second debounce.
     */
    if (!fs_autosave_recovery_pending && !autosave_maskHasDirty()) {
        fs_autosave_writer_armed = 0u;
        return;
    }
    now = time_sysTick;
    if (!fs_autosave_writer_armed) {
        fs_autosave_next_due_tick = (uint16_t)(
            now + AUTOSAVE_WRITER_INTERVAL_MS);
        fs_autosave_writer_armed = 1u;
        /*
         * SCHEDULED records the one 0-to-1 armed edge, separate from eventual
         * facade admission. value is the deadline selected for this debounce;
         * keeping it distinct proves whether a later missing drain failed in
         * scheduling, menu/deadline gating, or filesystem_start().
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_SCHEDULED, 0u,
                             (uint32_t)fs_autosave_next_due_tick);
        return;
    }
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE ||
        (uint16_t)(now - fs_autosave_next_due_tick) >= 0x8000u) {
        return;
    }
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveWriterCompleted)) {
        /*
         * Retain active transform ownership across the later generic FLUSH op.
         * Input: accepted drain start. Output: OFF defers mask discard until
         * the private completion callback, even after current_op becomes
         * FS_INTERNAL_OP_FLUSH_FINISH. Affiliate: policy transition.
         */
        fs_autosave_transaction_active = 1u;
        /*
         * ADMITTED records the exact point at which this private drain owns
         * the facade. This intentionally follows the active-transaction flag,
         * so policy code and the trace agree that a transform has begun.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_ADMITTED, 0u, 0u);
    }
}

/*
 * Record whether classification reached the source-copy boundary.
 *
 * Input is a boolean saying the live-byte patch budget ended this scan pass.
 * Output is one CAPTURED stage whose value is the immutable patch count used
 * by the next copy. Why: it proves a later failure occurred after capture and
 * exposes the expected bounded-continuation condition without altering either
 * mask ownership or the existing state-machine transitions. Affiliate: drain
 * phase 56's two phase-10 exits.
 */
static void filesystem_autosaveTraceCaptured(uint8_t budget_exhausted)
{
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_CAPTURED,
                         budget_exhausted ? 1u : 0u,
                         (uint32_t)op_autosave_writer.patch_count);
}

/*
 * Release the autonomous trace append's terminal facade status.
 *
 * Input is the DONE or ERROR status published after the diagnostic append
 * closes and syncs. Output is FS_STATUS_IDLE, so the ordinary settings and
 * autosave schedulers may run on their next tick. Why: unlike a foreground
 * filesystem request, this best-effort logger has no caller to observe and
 * acknowledge its terminal state; leaving it at DONE/ERROR would permanently
 * prevent every idle-only scheduler from claiming the facade. An ERROR does
 * not hide a trace failure: the trace flush cursor advanced only after sync,
 * so the next low-priority cadence retains and retries the pending records.
 * Affiliate: filesystem_autosaveTraceFlushSchedule_tick().
 */
static void filesystem_autosaveTraceFlushCompleted(void)
{
    filesystem_ack();
}

/*
 * Give durable trace flushing the last claim on an otherwise idle facade.
 *
 * Inputs are the ring's pending count and a wrapping cadence deadline. Output
 * is at most one append operation per interval, after settings persistence and
 * autosave writer scheduling declined this same tick. Why: diagnostics must
 * never delay the work they diagnose. A zero deadline is a reset sentinel: it
 * is initialized from the current tick before unsigned deadline comparison so
 * a first edit long after boot cannot wait for the 16-bit clock to wrap.
 */
static void filesystem_autosaveTraceFlushSchedule_tick(void)
{
#if DEV_MODE_LOGGING
    uint16_t now;

    if (autosaveTrace_pendingCount() == 0u)
        return;
    now = time_sysTick;
    if (fs_autosave_trace_next_due_tick == 0u)
        fs_autosave_trace_next_due_tick = now;
    if ((uint16_t)(now - fs_autosave_trace_next_due_tick) >= 0x8000u)
        return;
    /*
     * The background trace append owns no foreground caller, so its completion
     * callback must return the terminal facade state to IDLE. This prevents an
     * optional successful (or failed) diagnostic flush from stalling settings
     * persistence or the autosave writer after its first trace batch.
     */
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveTraceFlushCompleted)) {
        fs_autosave_trace_next_due_tick = (uint16_t)(
            now + AUTOSAVE_TRACE_FLUSH_INTERVAL_MS);
    }
#endif
}

void filesystem_tick(void)
{
    /*
     * Timeout is checked before another SD/FAT step can extend the failed
     * transaction. Input is the armed pre-audio or recovery deadline; output
     * is immediate cooperative unwind with no callback, close, or flush.
     * Why: the diagnostic path must abandon the owner that failed to make
     * progress, not enter its ordinary completion path. Affiliates: all
     * facade-owned blocking wrappers and bootlog recovery.
     */
    if (filesystem_bootLoggingPollDeadline())
        return;

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

    /*
     * Give the one-second metadata writer first use of an idle facade.
     *
     * Input: no current filesystem owner. Output: settings may start; autosave
     * scheduling runs only if the facade remains idle. Why: short source/
     * Global persistence should not be starved by 250 ms autosave backlog
     * continuations. Affiliates: both autonomous completion callbacks.
     */
    if (status == FS_STATUS_IDLE)
        filesystem_settingsWriterSchedule_tick();
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveWriterSchedule_tick();
    /* Trace persistence is deliberately last: it is diagnostic, not durable
     * user work, and may start only after both autonomous writers declined. */
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveTraceFlushSchedule_tick();
    if (status != FS_STATUS_BUSY) return;

    switch (current_op) {
    case FS_INTERNAL_OP_FLUSH_FINISH:
        filesystem_flushFinish_tick();
        break;
    case FS_INTERNAL_OP_CREATE_BOOT_INDEX:
        filesystem_createBootIndex_tick();
        break;
    case FS_INTERNAL_OP_CREATE_LIBRARY_INDEX:
        filesystem_createLibraryIndex_tick();
        break;
    case FS_INTERNAL_OP_WRITE_HCNAMES:
        filesystem_writeResidentNames_tick();
        break;
    case FS_INTERNAL_OP_WRITE_BOOT_LOG:
        filesystem_writeBootLog_tick();
        break;
    case FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES:
        filesystem_ensureAutosaveFiles_tick();
        break;
    case FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN:
        filesystem_autosaveParameterDrain_tick();
        break;
    case FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH:
        filesystem_autosaveTraceFlush_tick();
        break;
    case FS_INTERNAL_OP_LOAD_HCNAMES_INSTRUMENT:
    case FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT:
    case FS_INTERNAL_OP_LOAD_HCNAMES_KIT:
    case FS_INTERNAL_OP_UPDATE_HCNAMES_KIT:
    case FS_INTERNAL_OP_LOAD_HCNAMES_SCENE:
    case FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE:
        filesystem_residentNames_tick();
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
    case FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP:
        filesystem_saveInstrument_tick();
        break;
    case FS_INTERNAL_OP_LOAD_MORPH:
        filesystem_loadKit_tick();
        break;
    case FS_INTERNAL_OP_LOAD_PATTERN:
    case FS_INTERNAL_OP_SAVE_PATTERN:
    case FS_INTERNAL_OP_LOAD_ALL:
    case FS_INTERNAL_OP_LOAD_PERFORMANCE:
    case FS_INTERNAL_OP_SAVE_ALL:
    case FS_INTERNAL_OP_SAVE_PERFORMANCE:
        /*
         * Generic binary pattern containers are retired with Step storage.
         * Inputs: a stale request operation. Output: explicit error completion;
         * Scene/Bank directory persistence remains the supported bitmap path.
         */
        filesystem_finish(FS_STATUS_ERROR);
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
    case FS_INTERNAL_OP_REPAIR_NAMES:
        filesystem_repairNames_tick();
        break;
    case FS_INTERNAL_OP_LOAD_INDEX:
        filesystem_loadInstrumentIndex_tick();
        break;
    case FS_INTERNAL_OP_LOAD_LIBRARY_INDEX:
        filesystem_loadLibraryIndex_tick();
        break;
    case FS_INTERNAL_OP_LOAD_NAME:
        filesystem_loadName_tick();
        break;
    default: break;
    }
}

fs_status_t filesystem_status(void)
{
    return status;
}

void filesystem_getBootDiagnostic(uint8_t *op, uint8_t *phase)
{
    uint8_t public_op = FS_BOOT_DIAG_OTHER;

    /*
     * Translate only the operations reachable from boot stage 12.
     *
     * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the
     * user to assess how operations are proceeding. It does not and should not
     * ever add additional file interaction steps, since the diagnostic may be
     * used to assess in-situ file procedures.
     *
     * Input: current_op/op_phase owned by the single filesystem facade.
     * Output: stable diagnostic codes suitable for the front-panel display.
     * The mapping deliberately does not expose fs_internal_op_t values because
     * those values change whenever unrelated operations are inserted into the
     * private enum. Reading these fields is observational and cannot advance
     * or otherwise perturb the active state machine.
     */
#if DEV_MODE_DIAGNOSTIC
    switch (current_op) {
    case FS_INTERNAL_OP_REPAIR_NAMES:
        public_op = FS_BOOT_DIAG_REPAIR_NAMES;
        break;
    case FS_INTERNAL_OP_LOAD_BANK:
        public_op = FS_BOOT_DIAG_LOAD_BANK;
        break;
    case FS_INTERNAL_OP_LOAD_SCENE:
        public_op = FS_BOOT_DIAG_LOAD_SCENE;
        break;
    case FS_INTERNAL_OP_LOAD_KIT:
    case FS_INTERNAL_OP_LOAD_KIT_MORPH:
        public_op = FS_BOOT_DIAG_LOAD_KIT;
        break;
    case FS_INTERNAL_OP_FLUSH_FINISH:
        public_op = FS_BOOT_DIAG_FLUSH;
        break;
    default:
        break;
    }
#endif

    if (op)
        *op = public_op;
    if (phase) {
#if DEV_MODE_DIAGNOSTIC
        *phase = op_phase;
#else
        *phase = 0u;
#endif
    }
}

void filesystem_setBootSubstepDiagnostic(fs_boot_substep_diag_cb_t cb)
{
    /*
     * Register the temporary observer used only around initial payload load.
     *
     * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the
     * user to assess how operations are proceeding. It does not and should not
     * ever add additional file interaction steps, since the diagnostic may be
     * used to assess in-situ file procedures.
     *
     * Input: callback to invoke before phase-43 blocking component calls, or
     * NULL to disable observation. Output: callback ownership changes only;
     * current_op, op_phase, FAT state, and asyncfatfs pumping are untouched.
     */
#if DEV_MODE_DIAGNOSTIC
    fs_boot_substep_diagnostic = cb;
#else
    (void)cb;
    fs_boot_substep_diagnostic = NULL;
#endif
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
    const char *boot_code;

    if (status == FS_STATUS_BUSY) return false;
    /*
     * Arm before publishing BUSY so the complete operation owns its code.
     *
     * Inputs: private operation and typed domain. Output: a fresh diagnostic
     * deadline only when the pre-audio window is active and the operation has
     * a stable boot classification. Why: blocking wrappers contain nested
     * starts that main.c cannot see. Recovery and runtime starts are ignored
     * by the logger API. Affiliate: filesystem_bootLogCodeForOperation().
     */
    boot_code = filesystem_bootLogCodeForOperation(op, type);
    if (boot_code) {
        /*
         * DEV_MODE_LOGGING writes operation codes to file for use in debugging.
         * It must never print anything to the screen or otherwise delay
         * operations unnecessarily since logging may be used to assess timing
         * failures in other modules that might otherwise be obscured by screen
         * write delays.
         */
        filesystem_bootLoggingArm(boot_code);
    }
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
    /* The HCNAMES absence proof borrows generic handle/finder scratch.  Reset
     * its two operation-local markers here so a cancelled/failed predecessor
     * can never donate a stale scan result to a later create-capable request. */
    op_hcnames_probe_state = FS_HCNAMES_PROBE_IDLE;
    op_hcnames_probe_matches = 0u;
    op_flush_final_status = FS_STATUS_DONE;
    op_bytes_done = 0;
    op_stream_index = 0;
    op_item_offset = 0;
    op_loaded_active_pattern_running = 0;
    op_file_version = 0;
    op_write_line_len = 0u;
    op_write_line_offset = 0u;
    op_write_line_index = 0u;
    op_remove_done = 0u;
    /* A new request is ordinary until its dedicated temp request opts in. */
    op_instrument_load_temporary = 0u;
    op_instrument_save_temporary = 0u;
    op_kit_save_source_scene = 0u;
    op_kit_save_mode = STORAGE_INSTRUMENT_SAVE_NORMAL;
    memset(op_save_kit_dir_display_name, 0,
           sizeof(op_save_kit_dir_display_name));
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
    /*
     * Do not clear either dedicated cache or stage in generic request setup.
     *
     * Why: scan completion starts the `.hcindex` writer through
     * filesystem_start() while its dedicated name cache is still required
     * input. Typed requests likewise initialize their stage only after the
     * request is accepted. Generic reset must never discard either lifetime.
     *
     * Inputs: a new asynchronous request. Output: cache and stage remain
     * intact until their explicit cache transition or typed initializer.
     * Affiliates:
     * filesystem_createLibraryIndex_tick(), HCNAMES transactions, and all
     * typed load request initializers.
     */
    op_scene_load_scene_mask = 0u;
    memset(op_scene_display_name, 0, sizeof(op_scene_display_name));
    memset(op_scene_root_open_name, 0, sizeof(op_scene_root_open_name));
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
    op_bank_child_present_mask = 0u;
    op_bank_scene_load_mask = 0u;
    op_bank_scene_save_mask = 0u;
    op_bank_active_scene = 0u;
    op_bank_child_cursor = 0u;
    op_bank_loaded_scene = 0u;
    op_bank_payload_active = 0u;
    op_rename_done = 0u;
    op_repair_scope = FS_REPAIR_SCOPE_NONE;
    op_repair_library_kind = FS_NAME_CACHE_NONE;
    op_repair_instrument_type = INSTRUMENT_TYPE_UNKNOWN;
    op_repair_registry_index = 0u;
    op_repair_bank_slot = 0u;
    op_repair_suffix = 0u;
    op_repair_retry = 0u;
    memset(op_repair_old_name, 0, sizeof(op_repair_old_name));
    memset(op_repair_new_name, 0, sizeof(op_repair_new_name));
    memset(op_repair_base_display, 0, sizeof(op_repair_base_display));
    memset(op_repair_rename_open_name, 0,
           sizeof(op_repair_rename_open_name));
    op_create_dir_retry = 0u;
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
    completion_callback = cb;
    return true;
}

uint8_t filesystem_createBootIndexBlocking(void)
{
    /*
     * Boot-only synchronous wrapper around one shared-cache scan/index passes.
     * Each registry type is scanned into the single cache, its `.hcindex` is
     * written immediately, and the cache is then disposed before the next type
     * begins. Inputs: a mounted, ready asyncfatfs volume. Output: one refreshed
     * index in every registry-owned Instrument subdirectory, or zero if any
     * directory/file operation fails. This is intentionally called before
     * audioCodec_init(); runtime SD work remains asynchronous through
     * filesystem_tick().
     */
    uint8_t i;

    /*
     * Instrument repair runs before the typed scans.
     *
     * The Instrument browser is alphabetically sorted, but saved Kit
     * membership should only need the eight-character stem plus the descriptor
     * extension. Host-created long stems are renamed into deterministic
     * eight-character stems before the cache is populated, so `.hcindex`,
     * Instrument Load, and later Kit Save all see the same canonical key.
     */
    if (!filesystem_repairInstrumentNamesBlocking())
        return 0u;

    op_instrument_scan_one_type = 1u;
    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);

        if (!entry) {
            op_instrument_scan_one_type = 0u;
            filesystem_clearInstrumentCacheStorage();
            return 0u;
        }

        op_instrument_scan_registry_index = i;
        op_instrument_index_type = entry->type;
        filesystem_clearInstrumentCacheStorage();
        fs_list_cache_type = entry->type;
        if (!filesystem_start(FS_INTERNAL_OP_SCAN_INSTRUMENTS,
                              FS_FILE_SETTINGS,
                              0u,
                              NULL)) {
            op_instrument_scan_one_type = 0u;
            filesystem_clearInstrumentCacheStorage();
            return 0u;
        }
        while (status == FS_STATUS_BUSY)
            filesystem_tick();
        if (status != FS_STATUS_DONE) {
            filesystem_ack();
            op_instrument_scan_one_type = 0u;
            filesystem_clearInstrumentCacheStorage();
            return 0u;
        }
        filesystem_ack();

        if (!filesystem_start(FS_INTERNAL_OP_CREATE_BOOT_INDEX,
                              FS_FILE_SETTINGS,
                              0u,
                              NULL)) {
            op_instrument_scan_one_type = 0u;
            filesystem_clearInstrumentCacheStorage();
            return 0u;
        }
        while (status == FS_STATUS_BUSY)
            filesystem_tick();
        if (status != FS_STATUS_DONE) {
            filesystem_ack();
            op_instrument_scan_one_type = 0u;
            filesystem_clearInstrumentCacheStorage();
            return 0u;
        }
        filesystem_ack();
    }

    op_instrument_scan_one_type = 0u;
    op_instrument_index_type = INSTRUMENT_TYPE_UNKNOWN;
    filesystem_clearInstrumentCacheStorage();
    return 1u;
}

/*
 * Pump one trace append to its durable terminal boundary for a bench harness.
 *
 * Inputs: an idle facade and any current ring tail. Output: success if nothing
 * is pending or the private append operation completes successfully; busy or a
 * real I/O error returns zero. Why: this supplies an explicit pre-power-cycle
 * evidence boundary without making normal runtime code block for diagnostics.
 * Terminal status is acknowledged before return so this optional helper cannot
 * leave the autonomous schedulers disabled behind a stale DONE/ERROR state.
 */
uint8_t filesystem_autosaveTraceFlushBlocking(void)
{
    uint8_t trace_flushed;

    if (autosaveTrace_pendingCount() == 0u)
        return 1u;
    /*
     * The autonomous scheduler supplies a self-acknowledging callback because
     * it has no result consumer. This explicit bench helper instead needs to
     * inspect the terminal result below, so it deliberately owns the matching
     * filesystem_ack() after its local pump completes.
     */
    if (status == FS_STATUS_BUSY ||
        !filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                          FS_FILE_SETTINGS, 0u, NULL)) {
        return 0u;
    }
    while (status == FS_STATUS_BUSY)
        filesystem_tick();
    trace_flushed = (uint8_t)(status == FS_STATUS_DONE);
    /* See the function contract: return the outcome, but restore idle ownership. */
    filesystem_ack();
    return trace_flushed;
}

uint8_t filesystem_ensureAutosaveFilesBlocking(void)
{
    /*
     * Boot-only wrapper for the fixed-register creation state machine.
     *
     * Inputs: normal pre-audio filesystem ownership and BankData's completed
     * load result. Output: no card operation at all for a fallback with no
     * resident Bank; otherwise completion only after the normal foreground
     * pump has closed every created file and filesystem_finish() has passed the
     * shared asyncfatfs sync gate. The wrapper performs no autosave write, but
     * successful completion is the ownership boundary that enables retained
     * mutation production and one delayed runtime recovery attempt.
     */
    /*
     * Begin every boot setup with retained mutation production revoked.
     *
     * Inputs: current Bank boot result and any stale facade flags. Output:
     * tracking, runtime authorization, scheduling, and recovery are all clear
     * before create-only work or no-Bank fallback. Why: initialization setters
     * must never become autosave mutations. Affiliates: Autosave producer gate
     * and the idle scheduler.
     */
    autosave_setMutationTrackingEnabled(0u);
    fs_autosave_writer_boot_ready = 0u;
    fs_autosave_writer_armed = 0u;
    fs_autosave_recovery_pending = 0u;
    fs_autosave_setup_pending = 0u;
    fs_autosave_setup_failed = 0u;
    fs_autosave_transaction_active = 0u;
    fs_autosave_discard_pending = 0u;
    /*
     * Honor loaded AutoSave OFF before any HCNAMES/hidden-file access.
     *
     * Input: policy applied by main.c immediately after settings load. Output:
     * successful no-op with producer/scheduler authorization clear and SRAM
     * work discarded; neither hidden filename is scanned or opened. Why: a
     * caller-side condition alone is not sufficient defense for this public
     * wrapper. Affiliates: filesystem_setAutosaveEnabled() and main.c.
     */
    if (!fs_autosave_enabled) {
        autosave_discardDirtyMask();
        return 1u;
    }
    if (!bank_hasResidentBank())
        return 1u;
    /*
     * Start the create-only operation while runtime authorization remains clear.
     *
     * Inputs: idle mounted facade and resident Bank. Output: synchronous
     * foreground pumping until both files exist durably or setup fails. Why:
     * the scheduler cannot begin recovery while this wrapper owns the facade;
     * authorization is published only after acknowledgement below.
     */
    if (status == FS_STATUS_BUSY ||
        !filesystem_start(FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES,
                          FS_FILE_SETTINGS, 0u, NULL)) {
        fs_autosave_setup_failed = 1u;
        return 0u;
    }
    while (status == FS_STATUS_BUSY)
        filesystem_tick();
    if (status != FS_STATUS_DONE) {
        filesystem_ack();
        fs_autosave_setup_failed = 1u;
        return 0u;
    }
    filesystem_ack();
    /*
     * Enable only after the create-only pair is durable and its terminal
     * status has been acknowledged. Output: owner setters may now produce
     * mutations, and one recovery-pending attempt is armed lazily by the idle
     * scheduler after the full configured interval even when SRAM starts clean.
     * That attempt validates the winner and OR-merges interrupted file work;
     * successful clean completion then disarms all recurring file activity.
     */
    fs_autosave_writer_armed = 0u;
    fs_autosave_setup_failed = 0u;
    fs_autosave_recovery_pending = 1u;
    fs_autosave_writer_boot_ready = 1u;
    autosave_setMutationTrackingEnabled(1u);
    return 1u;
}

static bool filesystem_startRepairLibraryNames(fs_library_index_kind_t kind,
                                               fs_completion_cb_t cb)
{
    fs_name_cache_kind_t internal_kind;

    /*
     * Start root Kit/Scene/Bank name repair.
     *
     * Inputs: public library index kind. Output: one foreground-pumped repair
     * operation that mutates only that root directory namespace. The repair
     * fields are assigned after filesystem_start() because the generic request
     * initializer clears all operation-local scratch.
     */
    if (kind == FS_LIBRARY_INDEX_KIT)
        internal_kind = FS_NAME_CACHE_KIT;
    else if (kind == FS_LIBRARY_INDEX_SCENE)
        internal_kind = FS_NAME_CACHE_SCENE;
    else if (kind == FS_LIBRARY_INDEX_BANK)
        internal_kind = FS_NAME_CACHE_BANK;
    else
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_REPAIR_NAMES, FS_FILE_SETTINGS, 0u, cb))
        return false;
    op_repair_scope = FS_REPAIR_SCOPE_LIBRARY;
    op_repair_library_kind = internal_kind;
    return true;
}

static bool filesystem_startRepairInstrumentNames(instrument_type_t type,
                                                  fs_completion_cb_t cb)
{
    uint8_t i;

    /*
     * Start repair for one registered Instrument subtype directory.
     *
     * Inputs: descriptor type from InstrumentManager. Output: only that
     * `Instrument/<Type>/` folder is scanned and renamed. Type isolation is
     * required because the same eight-character stem is legal once per
     * extension family.
     */
    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);
        if (entry && entry->type == type) {
            if (!filesystem_start(FS_INTERNAL_OP_REPAIR_NAMES,
                                  FS_FILE_SETTINGS,
                                  0u,
                                  cb))
                return false;
            op_repair_scope = FS_REPAIR_SCOPE_INSTRUMENT;
            op_repair_registry_index = i;
            op_repair_instrument_type = type;
            return true;
        }
    }
    return false;
}

static bool filesystem_startRepairBankNames(uint16_t slot,
                                            fs_completion_cb_t cb)
{
    /*
     * Start selected-Bank child repair.
     *
     * Inputs: root Bank slot from the active Bank cache. Output: only that
     * Bank folder's immediate `00..15` children are canonicalized. Root Bank
     * names are repaired during boot/index maintenance; this preflight exists
     * so a Bank loaded after host edits cannot capture long child names into
     * resident provenance.
     */
    if (slot >= STORAGE_BANK_MAX_SLOTS ||
        !filesystem_librarySlotExists(FS_NAME_CACHE_BANK, slot))
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_REPAIR_NAMES, FS_FILE_BANK, slot, cb))
        return false;
    op_repair_scope = FS_REPAIR_SCOPE_BANK_LOAD;
    op_repair_bank_slot = slot;
    return true;
}

uint8_t filesystem_repairLibraryNamesBlocking(fs_library_index_kind_t kind)
{
    /*
     * Boot-only synchronous wrapper for root library repair.
     *
     * Inputs: mounted card before audio starts. Output: nonzero after the
     * namespace is canonical and all rename writes have passed the flush gate.
     * Runtime callers must use asynchronous repair or normal save refresh
     * chains instead of blocking the foreground.
     */
    if (!filesystem_startRepairLibraryNames(kind, NULL))
        return 0u;
    while (status == FS_STATUS_BUSY)
        filesystem_tick();
    if (status != FS_STATUS_DONE) {
        filesystem_ack();
        return 0u;
    }
    filesystem_ack();
    return 1u;
}

uint8_t filesystem_repairInstrumentNamesBlocking(void)
{
    uint8_t i;

    /*
     * Boot-only repair for every registered Instrument subtype.
     *
     * Inputs: mounted card before audio starts. Output: every
     * `Instrument/<Type>/` directory has been repaired before typed scans write
     * `.hcindex`. This keeps Instrument Load and later Kit Save aligned on the
     * same eight-character filename stem.
     */
    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);
        if (!entry ||
            !filesystem_startRepairInstrumentNames(entry->type, NULL))
            return 0u;
        while (status == FS_STATUS_BUSY)
            filesystem_tick();
        if (status != FS_STATUS_DONE) {
            filesystem_ack();
            return 0u;
        }
        filesystem_ack();
    }
    return 1u;
}

bool filesystem_requestRepairBankNames(uint16_t slot, fs_completion_cb_t cb)
{
    return filesystem_startRepairBankNames(slot, cb);
}

uint8_t filesystem_writeResidentNamesBlocking(
    fs_hcnames_diag_cb_t diagnostic_cb)
{
    /*
     * Boot-only wrapper for root `/.hcnames` generation.
     *
     * Inputs: filesystem idle after the initial resident load/fallback chain.
     * Output: nonzero only after the foreground-pumped writer has closed the
     * file and passed through filesystem_finish()'s normal flush gate. This is
     * intentionally a thin wrapper around FS_INTERNAL_OP_WRITE_HCNAMES, not a
     * private blocking file writer, so it has the same progress semantics as
     * `.hcindex` generation. diagnostic_cb is a temporary read-only hardware
     * hook: every blocking pump reports the live phase and next SRAM row so a
     * non-advancing state remains visible on the front-panel display.
     */
#if !DEV_MODE_DIAGNOSTIC
    /*
     * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the
     * user to assess how operations are proceeding. It does not and should not
     * ever add additional file interaction steps, since the diagnostic may be
     * used to assess in-situ file procedures.
     *
     * Discard callbacks in non-screen builds so this filesystem entry point
     * cannot expose operations merely because an obsolete caller supplied one.
     */
    diagnostic_cb = NULL;
#endif
    if (status == FS_STATUS_BUSY)
        return 0u;
    if (!filesystem_start(FS_INTERNAL_OP_WRITE_HCNAMES,
                          FS_FILE_SETTINGS,
                          0u,
                          NULL))
        return 0u;
    while (status == FS_STATUS_BUSY) {
        if (diagnostic_cb) {
            /*
             * DEV_MODE_DIAGNOSTIC displays runtime information on the screen
             * for the user to assess how operations are proceeding. It does
             * not and should not ever add additional file interaction steps,
             * since the diagnostic may be used to assess in-situ file
             * procedures.
             *
             * filesystem_finish(DONE) changes current_op to the shared flush
             * operation and resets op_phase to zero. Translate that internal
             * handoff to diagnostic phase 4; otherwise the screen would
             * misleadingly report that HCNAMES had returned to its open phase.
             * op_item_offset remains the next fixed-order SRAM row throughout
             * the writer and is safe to observe without changing ownership.
             */
            uint8_t diagnostic_phase =
                (current_op == FS_INTERNAL_OP_FLUSH_FINISH) ? 4u : op_phase;
            diagnostic_cb(diagnostic_phase, op_item_offset);
        }
        filesystem_tick();
    }
    if (status != FS_STATUS_DONE) {
        /*
         * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for
         * the user to assess how operations are proceeding. It does not and
         * should not ever add additional file interaction steps, since the
         * diagnostic may be used to assess in-situ file procedures.
         */
        if (diagnostic_cb)
            diagnostic_cb(6u, op_item_offset);
        filesystem_ack();
        return 0u;
    }
    /*
     * DEV_MODE_DIAGNOSTIC displays runtime information on the screen for the
     * user to assess how operations are proceeding. It does not and should not
     * ever add additional file interaction steps, since the diagnostic may be
     * used to assess in-situ file procedures.
     */
    if (diagnostic_cb)
        diagnostic_cb(5u, op_item_offset);
    filesystem_ack();
    return 1u;
}

uint8_t filesystem_createLibraryIndexBlocking(fs_library_index_kind_t kind)
{
    /*
     * Persist one numbered-library cache during pre-audio boot.
     *
     * Inputs: a mounted card and the requested Kit, root Scene, or root Bank
     * domain. Output: a slot-ordered `.hcindex`, including blank rows for
     * absent slots, after the normal asyncfatfs flush boundary. The wrapper
     * normally receives a cache populated by the preceding boot scan, but it
     * deliberately re-scans when the active shared cache tag does not match.
     * Why: the cache is intentionally disposable and a missed/failed caller
     * scan must never silently skip creation of the requested root index.
     * This wrapper exists only for boot; runtime saves chain the same state
     * machine without blocking audio.
     */
    fs_name_cache_kind_t internal_kind;
    bool scan_started = false;

    if (kind == FS_LIBRARY_INDEX_KIT)
        internal_kind = FS_NAME_CACHE_KIT;
    else if (kind == FS_LIBRARY_INDEX_SCENE)
        internal_kind = FS_NAME_CACHE_SCENE;
    else if (kind == FS_LIBRARY_INDEX_BANK)
        internal_kind = FS_NAME_CACHE_BANK;
    else
        return 0u;
    /* A completed error from an immediately preceding boot step is no longer
     * actionable; acknowledge it before deciding whether this request can
     * start. A live operation is still a hard serialization boundary. */
    if (status == FS_STATUS_DONE || status == FS_STATUS_ERROR)
        filesystem_ack();
    if (status == FS_STATUS_BUSY)
        return 0u;

    /*
     * Repair precedes scan because the scan cache is the index source.
     *
     * A long or duplicate host-created directory can parse into a valid slot
     * but cannot be regenerated later from an eight-character resident display
     * field. Repairing first lets the existing scan choose canonical physical
     * names and keeps `.hcindex` a faithful cache of the post-repair card.
     */
    if (!filesystem_repairLibraryNamesBlocking(kind))
        return 0u;
    if (kind == FS_LIBRARY_INDEX_KIT) {
        fs_kit_quarantine_result_t quarantine_result;

        /*
         * Kit quarantine uses raw blocking FAT helpers after name repair has
         * completed, so no filesystem_start() boundary exists to classify it.
         * Input is the mounted `/Kit` namespace; output is a KITQUAR deadline
         * covering validation/quarantine. Why: without this explicit arm the
         * completed NAMEREPR code could be blamed for a later raw-loop stall.
         * Affiliate: filesystem_blockPoll().
         */
        /*
         * DEV_MODE_LOGGING writes operation codes to file for use in debugging.
         * It must never print anything to the screen or otherwise delay
         * operations unnecessarily since logging may be used to assess timing
         * failures in other modules that might otherwise be obscured by screen
         * write delays.
         */
        filesystem_bootLoggingArm("KITQUAR ");
        quarantine_result = filesystem_quarantineKitLibraryBlocking();
        filesystem_bootLoggingOperationDone();
        if (quarantine_result != FS_KIT_QUARANTINE_OK) {
            /*
             * A failed Kit quarantine cannot be represented as a valid empty
             * library.
             *
             * Inputs: a retained KITQUAR detail label and an interrupted raw
             * FAT traversal. Output: this wrapper returns failure before cache
             * publication. Why: the boot caller must not mistake partial
             * traversal for a completed index or erase the evidence by moving
             * on to later scans.
             */
            return 0u;
        }
    }
    filesystem_clearNameCacheStorage();

    if (fs_list_cache_kind != internal_kind) {
        /* Rebuild the requested domain rather than writing another library's
         * rows under this root. This keeps the one-cache invariant explicit. */
        if (internal_kind == FS_NAME_CACHE_KIT)
            scan_started = filesystem_requestScanKits(NULL);
        else if (internal_kind == FS_NAME_CACHE_SCENE)
            scan_started = filesystem_requestScanScenes(NULL);
        else
            scan_started = filesystem_requestScanBanks(NULL);
        if (!scan_started)
            return 0u;
        while (status == FS_STATUS_BUSY)
            filesystem_tick();
        if (status != FS_STATUS_DONE) {
            filesystem_ack();
            return 0u;
        }
        filesystem_ack();
    }
    op_library_index_kind = internal_kind;
    if (!filesystem_start(FS_INTERNAL_OP_CREATE_LIBRARY_INDEX,
                          (kind == FS_LIBRARY_INDEX_KIT)
                              ? FS_FILE_KIT
                              : (kind == FS_LIBRARY_INDEX_SCENE)
                                  ? FS_FILE_SCENE : FS_FILE_BANK,
                          0u,
                          NULL))
        return 0u;
    while (status == FS_STATUS_BUSY)
        filesystem_tick();
    if (status != FS_STATUS_DONE) {
        filesystem_ack();
        return 0u;
    }
    filesystem_ack();
    return 1u;
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
    if (!filesystem_librarySlotExists(FS_NAME_CACHE_KIT, slot)) {
        filesystem_setPresetNameEmpty();
        return false;
    }
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_KIT, FS_FILE_KIT, slot, cb))
        return false;
    /*
     * Capture the selected Kit key before typed staging begins.
     *
     * Why: this keeps the request immutable even though the independent cache
     * may later be reused. Inputs: already-validated selected
     * cache row. Output: the existing nine-byte operation scratch is the sole
     * stable directory key through this request; no SRAM is allocated.
     * Affiliates: filesystem_loadKitDirectory_tick() phases 0/6/16 and the
     * Kit HCNAMES publication following a successful normal load.
     */
    memcpy(op_scene_display_name,
           filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, slot),
           STORAGE_KIT_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    op_kit_load_scene_mask = valid_mask;
    memset(&op_staged_kit, 0, sizeof(op_staged_kit));
    return true;
}

bool filesystem_requestSaveKitDirectory(uint16_t slot,
                                        uint8_t source_scene,
                                        const char display_name[8],
                                        uint8_t morph_projection,
                                        fs_completion_cb_t cb)
{
    const scene_t *scene = scene_getConst(source_scene);

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

    return true;
}

bool filesystem_requestSaveSceneDirectory(uint16_t slot,
                                          uint8_t source_scene,
                                          const char display_name[8],
                                          fs_completion_cb_t cb)
{
    const scene_t *scene = scene_getConst(source_scene);

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
    /*
     * Retain the one resident Scene row that this Save will publish after the
     * directory is durable. Input: source_scene selected by Menu. Output:
     * later HCNAMES update uses this one-bit mask without a SceneData name
     * field or a 16-name Menu mirror. Affiliates: saveSceneDirectory phase 37
     * and filesystem_cacheCurrentResidentSceneNames().
     */
    op_scene_load_scene_mask = (uint16_t)(1u << source_scene);
    filesystem_makeNumberedDir(op_save_kit_dir_display_name,
                               slot,
                               display_name);
    memcpy(op_scene_display_name, display_name, STORAGE_SCENE_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    /*
     * Capture the edited Scene row in the sole identity block before the
     * asynchronous directory writer starts. Inputs: accepted Save text.
     * Output: post-save targeted HCNAMES update source; no SceneData mirror.
     * Affiliates: filesystem_cacheCurrentResidentSceneNames() and Menu exit.
     */
    filesystem_setIdentityName(FS_IDENTITY_SCENE_ROW, display_name);
    filesystem_makeSceneEmbeddedKitDir(
        op_save_scene_kit_display_name,
        sizeof(op_save_scene_kit_display_name),
        filesystem_identityName(FS_IDENTITY_KIT_ROW));

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
    if (!filesystem_librarySlotExists(FS_NAME_CACHE_KIT, slot)) {
        filesystem_setPresetNameEmpty();
        return false;
    }
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_KIT_MORPH, FS_FILE_KIT, slot, cb))
        return false;
    /*
     * Preserve the selected source key in the existing operation scratch.
     *
     * Why: KitMrp uses the same typed stage as normal Kit Load, so its source
     * key must remain immutable. Inputs: validated selected index
     * row. Output: one request-stable `NNN Name` formatter input; resident
     * HCNAMES identity is deliberately unchanged by morph projection.
     * Affiliates: filesystem_loadKitDirectory_tick() phases 0/6 and Preset's
     * later morph-only endpoint commit.
     */
    memcpy(op_scene_display_name,
           filesystem_cachedLibraryName(FS_NAME_CACHE_KIT, slot),
           STORAGE_KIT_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    op_kit_load_scene_mask = valid_mask;
    memset(&op_staged_kit, 0, sizeof(op_staged_kit));
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
     * into the shared non-Pattern workspace and commits settings/Kit after
     * those inputs validate. Pattern then streams directly into final SRAM and
     * remains deliberately non-atomic; the mask
     * filtering is deliberately shared with Kit Load so future 16-Scene banks
     * can call this same public boundary.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((scene_mask & (uint16_t)(1u << scene_index)) != 0u)
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
    }
    if (valid_mask == 0u || slot >= STORAGE_SCENE_MAX_SLOTS ||
        !filesystem_librarySlotExists(FS_NAME_CACHE_SCENE, slot))
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_SCENE, FS_FILE_SCENE, slot, cb))
        return false;
    /*
     * Preserve the selected Scene key before Scene parsing starts.
     *
     * Why: phases 6 and 39 must reopen the selected root folder after parsing
     * begins, even if a later name-cache operation occurs.
     * Inputs: validated Scene cache row. Output: existing operation scratch
     * supplies both opens and the later HCNAMES row; no extra name cache is
     * retained. Affiliates: filesystem_loadSceneDirectory_tick() and the
     * root Scene targeted HCNAMES transaction.
     */
    memcpy(op_scene_display_name,
           filesystem_cachedLibraryName(FS_NAME_CACHE_SCENE, slot),
           STORAGE_SCENE_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    op_scene_load_scene_mask = valid_mask;
    filesystem_initSceneStage(&fs_stage_workspace.scene_stage);
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
        !filesystem_librarySlotExists(FS_NAME_CACHE_BANK, slot)) {
        return false;
    }
    /*
     * Repair selected Bank child names before capturing resident provenance.
     *
     * Inputs: selected root Bank slot and callback. Output: the asynchronous
     * repair handoff retains the root cache row until it has canonicalized the
     * immediate `00..15` child components, then starts the Bank payload reader
     * under the same callback. The repair no longer validates every embedded
     * Kit: selected children are validated by the shared Scene reader.
     */
    if (!filesystem_startRepairBankNames(slot, cb))
        return false;
    op_bank_scene_load_mask = valid_mask;
    op_scene_load_scene_mask = valid_mask;
    /*
     * Keep the root Bank display key intact while repair reads its cache row.
     *
     * Inputs: validated root Bank cache row. Output: repair can rebuild the
     * selected `NNN Name` component, after which the Bank reader reuses this
     * existing scratch while it opens selected child Scenes. No extra cache or
     * staging allocation exists here.
     */
    memcpy(op_bank_display_name,
           filesystem_cachedLibraryName(FS_NAME_CACHE_BANK, slot),
           STORAGE_KIT_DISPLAY_NAME_LEN);
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
    ** visible in the Load page.
    */
    filesystem_prepareLibraryNameCache(FS_NAME_CACHE_KIT);
    return filesystem_start(FS_INTERNAL_OP_SCAN_KITS, FS_FILE_KIT, 0, cb);
}

bool filesystem_requestScanScenes(fs_completion_cb_t cb)
{
    /*
     * Start a root Scene/ numbered-folder scan.
     *
     * Inputs: completion callback. Output: the Scene library cache is cleared
     * immediately and then repopulated from actual FAT directory entries. This
     * mirrors Kit scan while keeping Scene and Kit occupancy independent.
     */
    if (status == FS_STATUS_BUSY) return false;
    filesystem_prepareLibraryNameCache(FS_NAME_CACHE_SCENE);
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
    /* Bank names and occupancy now come from the single slot-ordered cache. */
    filesystem_prepareLibraryNameCache(FS_NAME_CACHE_BANK);
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
        !filesystem_librarySlotExists(FS_NAME_CACHE_BANK, slot)) {
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

bool filesystem_requestLoadResidentInstrumentName(uint8_t scene_index,
                                                  uint8_t instrument_slot,
                                                  fs_completion_cb_t cb)
{
    uint16_t row = filesystem_residentInstrumentRow(scene_index,
                                                    instrument_slot);

    /*
     * Load the root resident-name register for one Instrument menu entry.
     *
     * Inputs: the exact resident Scene/voice whose name Menu will display and
     * an optional completion callback. Output: the existing generalized cache
     * temporarily owns all HCNAMES rows so the selected row can be borrowed
     * through filesystem_residentInstrumentName(). The caller must copy those
     * eight cells before starting a `.hcindex` request, which intentionally
     * reuses and replaces the same cache. No SceneData name is consulted here.
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT || status == FS_STATUS_BUSY)
        return false;
    filesystem_prepareResidentNamesCache();
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_HCNAMES_INSTRUMENT,
                          FS_FILE_SETTINGS,
                          instrument_slot,
                          cb)) {
        filesystem_clearNameCacheStorage();
        return false;
    }
    op_kit_load_scene_mask = (uint16_t)(1u << scene_index);
    return true;
}

bool filesystem_requestUpdateResidentInstrumentNames(
    uint16_t scene_mask,
    uint8_t instrument_slot,
    fs_completion_cb_t cb)
{
    uint16_t valid_mask = 0u;
    uint8_t scene_index;

    /*
     * Refresh only resident Instrument rows changed by one Load/Save action.
     *
     * Inputs: destination/source Scene mask, one zero-based voice slot, and
     * optional completion callback. Output: `/.hcnames` is first read into the
     * existing generalized cache, only matching Instrument row(s) are replaced
     * from committed resident state, and the variable-length file is streamed
     * back through the normal close/flush gate. Other rows are preserved from
     * the file rather than regenerated from SRAM. Multiple bits are accepted
     * because one normal Instrument Load can target several Scenes; a Save
     * supplies exactly one bit. No second cache or persistent scratch is added.
     */
    if (instrument_slot >= STORAGE_KIT_SLOT_COUNT ||
        status == FS_STATUS_BUSY) {
        return false;
    }
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        if ((scene_mask & bit) != 0u)
            valid_mask = (uint16_t)(valid_mask | bit);
    }
    if (valid_mask == 0u)
        return false;
    filesystem_prepareResidentNamesCache();
    if (!filesystem_start(FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT,
                          FS_FILE_SETTINGS,
                          instrument_slot,
                          cb)) {
        filesystem_clearNameCacheStorage();
        return false;
    }
    op_kit_load_scene_mask = valid_mask;
    return true;
}

bool filesystem_requestLoadResidentKitName(uint8_t scene_index,
                                           fs_completion_cb_t cb)
{
    uint16_t row = filesystem_residentKitRow(scene_index);

    /*
     * Load the root resident-name register for one top-level Kit menu entry.
     *
     * Inputs: the resident source Scene selected by Load/Save and an optional
     * completion callback. Output: the existing generalized cache temporarily
     * owns all HCNAMES rows so Menu can copy the selected Kit row through
     * filesystem_residentKitName() before `/Kit/.hcindex` replaces the cache.
     * No SceneData name is read by the Menu entry path and no second name cache
     * or Kit-sized buffer is allocated.
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT || status == FS_STATUS_BUSY)
        return false;
    filesystem_prepareResidentNamesCache();
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_HCNAMES_KIT,
                          FS_FILE_SETTINGS,
                          scene_index,
                          cb)) {
        filesystem_clearNameCacheStorage();
        return false;
    }
    op_kit_load_scene_mask = (uint16_t)(1u << scene_index);
    return true;
}

bool filesystem_requestUpdateResidentKitNames(uint16_t scene_mask,
                                              fs_completion_cb_t cb)
{
    uint16_t valid_mask = 0u;
    uint8_t scene_index;

    /*
     * Refresh one full resident Kit identity block per selected Scene.
     *
     * Inputs: Menu's accumulated dirty-Scene mask at the combined
     * Kit/Instrument family exit, plus an optional callback. Individual
     * scroll, load, and save operations only update committed SceneData and
     * Menu scratch; they do not call this updater.
     * Output: `/.hcnames` is read into the existing generalized cache, exactly
     * one Kit row and six Instrument rows per selected Scene are replaced from
     * committed resident state, and the variable-length file is rewritten
     * through the normal close/flush gate. Every unrelated logical row is
     * preserved from the file. One exit request can therefore commit several
     * actions and several Scenes without retaining a 16-by-7 name array. The
     * request reuses the existing operation mask, line buffer, and general
     * cache, so it adds no persistent SRAM storage.
     */
    if (status == FS_STATUS_BUSY)
        return false;
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        if ((scene_mask & bit) != 0u)
            valid_mask = (uint16_t)(valid_mask | bit);
    }
    if (valid_mask == 0u)
        return false;
    filesystem_prepareResidentNamesCache();
    if (!filesystem_start(FS_INTERNAL_OP_UPDATE_HCNAMES_KIT,
                          FS_FILE_SETTINGS,
                          0u,
                          cb)) {
        filesystem_clearNameCacheStorage();
        return false;
    }
    op_kit_load_scene_mask = valid_mask;
    return true;
}

bool filesystem_requestLoadResidentSceneName(uint8_t scene_index,
                                             fs_completion_cb_t cb)
{
    uint16_t row = filesystem_residentSceneRow(scene_index);

    /*
     * Read the one Scene name needed by a Scene-menu operation.
     *
     * Inputs: resident Scene coordinate and optional completion callback.
     * Output: the existing general-purpose cache temporarily contains HCNAMES
     * so Menu can copy exactly this row before it reloads `/Scene/.hcindex`.
     * This intentionally reads the variable-length file through the common
     * full-register reader, but it retains only Menu's one nine-byte scratch;
     * SceneData receives no per-Scene name field. Affiliates: Menu's Scene
     * Save entry and filesystem_residentSceneName().
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT || status == FS_STATUS_BUSY)
        return false;
    filesystem_prepareResidentNamesCache();
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_HCNAMES_SCENE,
                          FS_FILE_SETTINGS, scene_index, cb)) {
        filesystem_clearNameCacheStorage();
        return false;
    }
    return true;
}

bool filesystem_requestUpdateResidentSceneNames(
    uint16_t scene_mask,
    const char name[8],
    fs_completion_cb_t cb)
{
    uint16_t valid_mask = 0u;
    uint8_t scene_index;

    /*
     * Publish a successful root Scene Load/Save without a SceneData mirror.
     *
     * Inputs: one or more destination Scene bits, the captured directory/save
     * name, and callback. Output: the reader preserves every unrelated
     * HCNAMES row, then replaces only selected Scene rows and rewrites the
     * variable-length register through its normal flush gate. A multi-target
     * root Scene Load legitimately assigns one source name to all destinations.
     * No additional SRAM array is allocated; the supplied name uses existing
     * operation scratch and the file uses fs_list_cache_name temporarily.
     */
    if (!name || status == FS_STATUS_BUSY)
        return false;
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        uint16_t bit = (uint16_t)(1u << scene_index);
        if ((scene_mask & bit) != 0u)
            valid_mask = (uint16_t)(valid_mask | bit);
    }
    if (valid_mask == 0u)
        return false;
    filesystem_prepareResidentNamesCache();
    if (!filesystem_start(FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE,
                          FS_FILE_SETTINGS, 0u, cb)) {
        filesystem_clearNameCacheStorage();
        return false;
    }
    op_scene_load_scene_mask = valid_mask;
    memcpy(op_scene_display_name, name, STORAGE_SCENE_DISPLAY_NAME_LEN);
    op_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
    return true;
}

bool filesystem_requestLoadInstrumentIndex(instrument_type_t type,
                                           fs_completion_cb_t cb)
{
    /*
     * Start loading one registry-owned Instrument index from the SD card.
     *
     * Inputs: registered type and optional completion callback. Outputs: the
     * one shared cache is cleared immediately, tagged with the selected type,
     * then repopulated by the foreground-pumped `.hcindex` reader. Keeping the
     * clear at request time prevents the Menu from displaying stale names
     * while the new index is in flight; invalid types and a busy queue fail
     * before any filesystem state is changed.
     */
    if (type >= INSTRUMENT_TYPE_UNKNOWN ||
        !instrumentManager_storageDirectory(type) ||
        status == FS_STATUS_BUSY)
        return false;
    filesystem_clearInstrumentCacheStorage();
    fs_list_cache_kind = FS_NAME_CACHE_INSTRUMENT;
    fs_list_cache_type = type;
    op_instrument_index_type = type;
    return filesystem_start(FS_INTERNAL_OP_LOAD_INDEX, FS_FILE_KIT, 0u, cb);
}

bool filesystem_requestReloadLibraryIndex(fs_library_index_kind_t kind,
                                          fs_completion_cb_t cb)
{
    fs_name_cache_kind_t cache_kind;

    /*
     * Read one existing numbered-root index into the shared browser cache.
     *
     * Inputs: public Kit, Scene, or Bank library kind plus an optional
     * callback. Output: the single shared cache is disposed immediately and
     * asynchronously repopulated from that domain's slot-preserving
     * `.hcindex`; a non-blank row is its occupancy record. This operation is
     * deliberately read-only: it performs no physical directory scan and no
     * index write. Browser entry and post-DSP Load restoration share it, while
     * namespace-mutating Saves use the separate rebuild chain.
     */
    if (status == FS_STATUS_BUSY)
        return false;

    if (kind == FS_LIBRARY_INDEX_KIT)
        cache_kind = FS_NAME_CACHE_KIT;
    else if (kind == FS_LIBRARY_INDEX_SCENE)
        cache_kind = FS_NAME_CACHE_SCENE;
    else if (kind == FS_LIBRARY_INDEX_BANK)
        cache_kind = FS_NAME_CACHE_BANK;
    else
        return false;

    filesystem_prepareLibraryNameCache(cache_kind);
    op_library_index_kind = cache_kind;
    return filesystem_start(FS_INTERNAL_OP_LOAD_LIBRARY_INDEX,
                            (cache_kind == FS_NAME_CACHE_KIT)
                                ? FS_FILE_KIT
                                : (cache_kind == FS_NAME_CACHE_SCENE)
                                    ? FS_FILE_SCENE : FS_FILE_BANK,
                            0u,
                            cb);
}

bool filesystem_requestLoadKitIndex(fs_completion_cb_t cb)
{
    /*
     * Preserve the domain-specific Kit entry point for boot/Menu affiliates.
     * Input/output are exactly the generic read-only reload contract above;
     * this wrapper adds no scan, write, cache, or retained operation state.
     */
    return filesystem_requestReloadLibraryIndex(FS_LIBRARY_INDEX_KIT, cb);
}

bool filesystem_requestLoadSceneIndex(fs_completion_cb_t cb)
{
    /*
     * Preserve the domain-specific Scene entry point for existing callers.
     * It delegates only to the slot-ordered reader; Scene Save's physical
     * namespace rebuild remains on the separate Save-owned chain.
     */
    return filesystem_requestReloadLibraryIndex(FS_LIBRARY_INDEX_SCENE, cb);
}

bool filesystem_requestLoadBankIndex(fs_completion_cb_t cb)
{
    /*
     * Bank enters the same read-only slot-ordered loader as Kit/root Scene.
     * Directory scans and index writes remain Save-owned rebuild operations.
     */
    return filesystem_requestReloadLibraryIndex(FS_LIBRARY_INDEX_BANK, cb);
}

bool filesystem_libraryNameCacheLoaded(fs_library_index_kind_t kind)
{
    /*
     * Report whether the requested root library is the active cache domain.
     * The cache is slot-sized even when every slot is empty, so readiness is a
     * domain identity question rather than a nonzero-name-count question.
     */
    if (kind == FS_LIBRARY_INDEX_KIT)
        return fs_list_cache_kind == FS_NAME_CACHE_KIT;
    if (kind == FS_LIBRARY_INDEX_SCENE)
        return fs_list_cache_kind == FS_NAME_CACHE_SCENE;
    if (kind == FS_LIBRARY_INDEX_BANK)
        return fs_list_cache_kind == FS_NAME_CACHE_BANK;
    return false;
}

void filesystem_clearInstrumentCache(void)
{
    /*
     * Dispose the one shared browser cache through the legacy Instrument name.
     *
     * Inputs: none. Outputs: no Instrument type, Kit library, or root Scene
     * library is considered loaded and all cached browser names are erased.
     * Existing Instrument callers retain this spelling for source compatibility;
     * new cross-library lifecycle code uses filesystem_clearNameCache().
     */
    filesystem_clearNameCacheStorage();
}

void filesystem_clearNameCache(void)
{
    /*
     * Dispose the one shared browser-name cache for every library.
     *
     * Inputs: none. Output: no Instrument, Kit, root Scene, root Bank, or
     * temporary HCNAMES view remains readable. Menu calls this on Load/Save
     * exit and type changes; retaining occupancy/open metadata here would make
     * a later payload operation appear valid without a corresponding display-
     * name index, so the public clear is intentionally limited to the shared
     * name cache itself and index loads clear their matching occupancy maps
     * before repopulating them.
     */
    filesystem_clearNameCacheStorage();
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
    op_instrument_scan_one_type = 0u;
    filesystem_clearInstrumentCacheStorage();
    return filesystem_start(FS_INTERNAL_OP_SCAN_INSTRUMENTS, FS_FILE_KIT, 0,
                            cb);
}

/*
 * Retired public File/Dir diagnostics.
 *
 * These request/accessor APIs are excluded with their backing caches. No
 * musical menu reaches them after the File/Dir types were removed from the
 * chooser, preventing a dangling public path from rebuilding diagnostic SRAM.
 */
#if 0
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

#endif

/*
 * Compatibility stubs for retired diagnostic callers.
 *
 * The menu no longer offers File/Dir diagnostics, so these return no work and
 * retain no names, aliases, result bytes, or filesystem state. The symbols
 * remain temporarily while upper layers are simplified, preventing a stale
 * developer-only call site from becoming an implicit declaration or from
 * reintroducing the former 6,240-byte lists.
 */
bool filesystem_requestScanTestFiles(fs_completion_cb_t cb)
{
    (void)cb;
    return false;
}

bool filesystem_requestScanTestDirs(fs_completion_cb_t cb)
{
    (void)cb;
    return false;
}

uint8_t filesystem_testFileCount(void) { return 0u; }
uint8_t filesystem_testDirCount(void) { return 0u; }
const char *filesystem_testFileName(uint8_t index) { (void)index; return ""; }
const char *filesystem_testDirName(uint8_t index) { (void)index; return ""; }
bool filesystem_requestLoadTestFile(const char *name, fs_completion_cb_t cb)
{ (void)name; (void)cb; return false; }
bool filesystem_requestLoadTestDir(const char *name, fs_completion_cb_t cb)
{ (void)name; (void)cb; return false; }
bool filesystem_requestSaveTestFile(const char *name, fs_completion_cb_t cb)
{ (void)name; (void)cb; return false; }
bool filesystem_requestSaveTestDir(const char *name, fs_completion_cb_t cb)
{ (void)name; (void)cb; return false; }
bool filesystem_requestSaveTestSimpleDir(const char *name, fs_completion_cb_t cb)
{ (void)name; (void)cb; return false; }
fs_test_result_kind_t filesystem_testResultKind(void)
{ return FS_TEST_RESULT_BYTES_READY; }
const uint8_t *filesystem_testResultBytes(void)
{ static const uint8_t empty[FS_TEST_RESULT_BYTES] = { 0u }; return empty; }
const char *filesystem_testResultName(void) { return ""; }

bool filesystem_requestLoadInstrument(uint8_t destination_scene,
                                      uint8_t destination_slot,
                                      instrument_type_t type,
                                      uint16_t browser_index,
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
        browser_index >= filesystem_cachedInstrumentCount(type))
        return false;
    if (!filesystem_start(FS_INTERNAL_OP_LOAD_INSTRUMENT, FS_FILE_KIT, 0, cb))
        return false;
    /*
     * Copy the typed-index selection before Instrument staging reuses its union.
     *
     * Why: the stage reset occurs at loader phase 11 and invalidates every
     * typed cache row, including browser_index. Inputs: validated type/index
     * name. Output: existing save-name request scratch becomes the one stable
     * filename/HCNAMES source for this load; it adds no SRAM allocation.
     * Affiliates: filesystem_loadInstrument_tick() phases 0/11/13 and Menu's
     * deferred Instrument HCNAMES flush.
     */
    storage_copyFilename(op_instrument_save_display_name,
                         filesystem_cachedInstrumentName(type, browser_index));
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
        op != FS_INTERNAL_OP_SAVE_INSTRUMENT_MORPH &&
        op != FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {
        return false;
    }
    if (!instrument ||
        source_slot >= STORAGE_KIT_SLOT_COUNT ||
        instrument->type >= INSTRUMENT_TYPE_UNKNOWN)
        return false;
    if (op == FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {
        filesystem_makeInstrumentTemporaryFilename(display, instrument->type);
    } else {
        storage_makeSavedInstrumentDisplayFilename(display,
                                                   sizeof(display),
                                                   display_name,
                                                   instrument->type,
                                                   (uint8_t)(source_slot + 1u),
                                                   0u);
    }
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
    /* This flag is consumed by save phase 21 to suppress cache/name changes. */
    op_instrument_save_temporary =
        (uint8_t)(op == FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP);
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

bool filesystem_requestSaveInstrumentTemp(uint8_t source_scene,
                                          uint8_t source_slot,
                                          fs_completion_cb_t cb)
{
    /*
     * Write one current voice as its hidden reversible Load source.
     *
     * Inputs: active Scene/voice and completion callback captured by Preset.
     * Output: `Instrument/<type>/.hctmp.<ext>` is atomically rewritten through
     * the normal Instrument serializer, but never enters `.hcindex` or
     * HCNAMES. Affiliates: Menu's `kit` row and the matching temp-load API.
     */
    return filesystem_requestSaveInstrumentMode(
        FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP, source_scene, source_slot, NULL, cb);
}

bool filesystem_requestLoadInstrumentTemp(uint8_t destination_scene,
                                          uint8_t destination_slot,
                                          instrument_type_t type,
                                          fs_completion_cb_t cb)
{
    /*
     * Load the hidden `kit` source without touching its display identity.
     *
     * Inputs: the same Scene/voice/type that created the temporary file.
     * Output: the regular Instrument parser populates the one candidate stage;
     * its normal completion applies parameters, while its temp flag suppresses
     * HCNAMES publication. Affiliates: Menu lower-row decrement and save API.
     */
    if (!scene_indexValid(destination_scene) ||
        destination_slot >= STORAGE_KIT_SLOT_COUNT ||
        type >= INSTRUMENT_TYPE_UNKNOWN ||
        !filesystem_start(FS_INTERNAL_OP_LOAD_INSTRUMENT, FS_FILE_KIT, 0u, cb))
        return false;
    filesystem_makeInstrumentTemporaryFilename(op_instrument_save_display_name,
                                               type);
    op_instrument_load_destination_scene = destination_scene;
    op_instrument_load_destination_slot = destination_slot;
    op_instrument_load_type = type;
    op_instrument_load_index = 0u;
    op_instrument_load_temporary = 1u;
    return true;
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

/* Query whether the active Kit name cache contains a numbered folder.
 *
 * Input: zero-based slot used by menu/preset code. Output: nonzero if the
 * active shared Kit `.hcindex` cache contains a non-blank row. Clients:
 * filesystem_kitSlotName() and future UI code that distinguishes absent slots
 * from malformed present kits. The cache row itself is the only occupancy
 * record; no parallel Kit bitmap is retained.
 */
uint8_t filesystem_kitSlotExists(uint16_t zero_based_slot)
{
    if (zero_based_slot >= STORAGE_KIT_MAX_SLOTS)
        return 0u;
    return filesystem_librarySlotExists(FS_NAME_CACHE_KIT, zero_based_slot);
}

/* Return a display name from the active slot-ordered Kit cache.
 *
 * Input: zero-based slot. Output: NUL-terminated eight-character cached name,
 * or "Empty   " for absent/out-of-range slots. Client: menu.c's Load page.
 */
const char *filesystem_kitSlotName(uint16_t zero_based_slot)
{
    const char *name;

    if (!filesystem_kitSlotExists(zero_based_slot))
        return "Empty   ";
    name = filesystem_cachedLibraryName(FS_NAME_CACHE_KIT,
                                        zero_based_slot);
    return name ? name : "Empty   ";
}

uint8_t filesystem_sceneSlotExists(uint16_t zero_based_slot)
{
    /*
     * Query the active root Scene/ name cache.
     *
     * Input: zero-based library slot. Output: nonzero only when the active
     * shared Scene `.hcindex` cache contains a non-blank row at that slot.
     * The cache row is the sole occupancy record, so this accessor cannot
     * disagree with filesystem_sceneSlotName().
     */
    if (zero_based_slot >= STORAGE_SCENE_MAX_SLOTS)
        return 0u;
    return filesystem_librarySlotExists(FS_NAME_CACHE_SCENE,
                                        zero_based_slot);
}

const char *filesystem_sceneSlotName(uint16_t zero_based_slot)
{
    const char *name;

    /*
     * Return an eight-character root Scene library display name.
     *
     * Input: zero-based library slot. Output: cached display name for existing
     * Scenes, or "Empty   " for missing/out-of-range slots. Menu uses this
     * directly for Load:[Scene] and Save overwrite planning.
     */
    if (zero_based_slot >= STORAGE_SCENE_MAX_SLOTS ||
        !filesystem_librarySlotExists(FS_NAME_CACHE_SCENE,
                                       zero_based_slot)) {
        return "Empty   ";
    }
    name = filesystem_cachedLibraryName(FS_NAME_CACHE_SCENE,
                                        zero_based_slot);
    return name ? name : "Empty   ";
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
    return filesystem_librarySlotExists(FS_NAME_CACHE_BANK,
                                        zero_based_slot);
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
        !filesystem_librarySlotExists(FS_NAME_CACHE_BANK,
                                       zero_based_slot)) {
        return "Empty   ";
    }
    return filesystem_cachedLibraryName(FS_NAME_CACHE_BANK,
                                        zero_based_slot);
}

uint16_t filesystem_firstKitSlot(void)
{
    uint16_t slot;

    /*
     * Find the lowest present Kit slot for fallback loading.
     *
     * The loop scans direct root slots 000..999 and returns the configured
     * maximum as the absent sentinel. Callers compare against
     * STORAGE_KIT_MAX_SLOTS rather than assuming 0 is empty, because slot 000
     * is a real user slot. Occupancy comes directly from the active shared
     * cache row.
     */
    for (slot = 0u; slot < STORAGE_KIT_MAX_SLOTS; slot++) {
        if (filesystem_librarySlotExists(FS_NAME_CACHE_KIT, slot))
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
        if (filesystem_librarySlotExists(FS_NAME_CACHE_SCENE, slot))
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
        if (filesystem_librarySlotExists(FS_NAME_CACHE_BANK, slot))
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

uint16_t filesystem_lastBankLoadSceneMask(void)
{
    /*
     * Expose the completed Bank reader's effective child mask.
     *
     * Input: op_bank_scene_load_mask after child discovery/intersection and
     * successful resident commit. Output: the exact mask consumed immediately
     * by Preset completion provenance. Why: the original all/partial request
     * can contain children absent from the selected Bank. Affiliates: Bank
     * load phases 16/17 and filesystem.h's operation-lifetime contract.
     */
    return op_bank_scene_load_mask;
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
    for (uint16_t i = 0u; i < filesystem_cachedInstrumentCount(type); i++) {
        if (filesystem_instrumentCacheStemMatches(type, i, display))
            return 1u;
    }
    return 0u;
}

const char *filesystem_residentInstrumentName(uint8_t scene_index,
                                              uint8_t instrument_slot)
{
    uint16_t row = filesystem_residentInstrumentRow(scene_index,
                                                    instrument_slot);

    /*
     * Borrow one Instrument name from the temporary HCNAMES cache view.
     *
     * Inputs must match the completed resident-name read/update request.
     * Output is the selected eight-character, NUL-terminated cache cell, or
     * `Empty   ` when the coordinates/cache domain are invalid or the file row
     * is blank. The pointer becomes invalid as soon as the typed `.hcindex`
     * loader reuses the generalized cache, so Menu copies it immediately into
     * its existing edit/display buffer instead of retaining this pointer.
     */
    if (fs_list_cache_kind != FS_NAME_CACHE_HCNAMES ||
        row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        filesystem_residentNameIsBlank(fs_list_cache_name[row])) {
        return "Empty   ";
    }
    return fs_list_cache_name[row];
}

const char *filesystem_residentKitName(uint8_t scene_index)
{
    uint16_t row = filesystem_residentKitRow(scene_index);

    /*
     * Borrow one Kit name from the temporary HCNAMES cache view.
     *
     * Input must match the completed resident-Kit-name read/update request.
     * Output is the eight-character, NUL-terminated cache cell, or `Empty   `
     * when the Scene/cache domain is invalid or the file row is blank. The
     * pointer becomes invalid as soon as `/Kit/.hcindex` reuses the generalized
     * cache, so Menu copies it into preset_currentName before requesting that
     * index and never retains the pointer.
     */
    if (fs_list_cache_kind != FS_NAME_CACHE_HCNAMES ||
        row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        filesystem_residentNameIsBlank(fs_list_cache_name[row])) {
        return "Empty   ";
    }
    return fs_list_cache_name[row];
}

const char *filesystem_residentSceneName(uint8_t scene_index)
{
    uint16_t row = filesystem_residentSceneRow(scene_index);

    /*
     * Borrow one Scene identity after filesystem_requestLoadResidentSceneName.
     *
     * Input: resident Scene coordinate. Output: HCNAMES' eight-cell text or
     * the normal `Empty` fallback for a blank/invalid row. The pointer belongs
     * to the shared cache and becomes invalid when Menu loads `/Scene/.hcindex`;
     * callers must copy it into their sole operation-scoped Scene scratch.
     * Affiliates: Core/Menu/menu.c Scene Save entry.
     */
    if (fs_list_cache_kind != FS_NAME_CACHE_HCNAMES ||
        row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        filesystem_residentNameIsBlank(fs_list_cache_name[row])) {
        return "Empty   ";
    }
    return fs_list_cache_name[row];
}

uint16_t filesystem_instrumentCount(instrument_type_t type)
{
    /*
     * Return the cached Instrument/ count for the active type.
     *
     * Input: instrument type. Output: number of sorted files found by the most
     * recent scan, up to the complete 1,000 rows of the shared cache. Menu uses
     * this to bound browser indices without owning another name array.
     */
    return filesystem_cachedInstrumentCount(type);
}

const char *filesystem_instrumentName(instrument_type_t type,
                                      uint16_t browser_index)
{
    const char *name = filesystem_cachedInstrumentName(type, browser_index);

    /*
     * Return one cached Instrument/ display name.
     *
     * Inputs: instrument type and zero-based browser index. Output: an
     * exact cached filename, or "Empty   " for invalid/empty selections. The
     * returned pointer is filesystem-owned cache storage and may include an
     * extension, so it is only suitable for filesystem key derivation.
     */
    return name ? name : "Empty   ";
}

void filesystem_copyInstrumentDisplayName(char destination[9],
                                          instrument_type_t type,
                                          uint16_t browser_index)
{
    const char *filename = filesystem_cachedInstrumentName(type, browser_index);

    /*
     * Derive a menu-safe Instrument stem from one cached filename.
     *
     * Inputs: type/index validated against the active typed `.hcindex` cache
     * and a caller's nine-byte field. Output: padded eight-cell stem only;
     * short names never leak `.snr`, `.drm`, or another extension into LCD or
     * HCNAMES text. No cache row is copied or retained beyond destination.
     * Affiliates: filesystem_copyInstrumentStemDisplay(), nested Menu Load,
     * and preview finalization at a session boundary.
     */
    if (!destination)
        return;
    if (!filename) {
        memset(destination, ' ', STORAGE_KIT_DISPLAY_NAME_LEN);
        destination[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
        return;
    }
    filesystem_copyInstrumentStemDisplay(destination, filename);
}

uint16_t filesystem_instrumentDisplayIndex(instrument_type_t type,
                                           uint16_t browser_index)
{
    uint16_t display_index;

    /*
     * Convert a cached Instrument/ index into the visible one-based counter.
     *
     * Inputs: type and zero-based browser index. Output: one-based list
     * position, visually saturated at 999 because the LCD counter has three
     * digits. The underlying index still addresses all 1,000 rows, so row 999
     * remains selectable even though its one-based display is 1000.
     */
    if (filesystem_cachedInstrumentCount(type) == 0u ||
        browser_index >= filesystem_cachedInstrumentCount(type))
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
