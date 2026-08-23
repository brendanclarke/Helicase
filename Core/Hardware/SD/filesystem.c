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
#include "globals.h"
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
 * One compact provenance word accompanies each logical HCNAMES row.
 *
 * Direct numbered-library sources occupy 0..999; the row class determines the
 * library.  The three high values are non-library tokens.  Bit 15 is retained
 * only while an asynchronous HCNAMES rewrite has a caller-staged source
 * update: it lets the reader preserve a new source while it streams the old
 * register into the shared name cache, with no second dirty bitmap allocation.
 */
#define FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT 1000u
#define FS_RESIDENT_SOURCE_DIRTY_FLAG        0x8000u
#define FS_RESIDENT_SOURCE_VALUE_MASK        0x7fffu
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
/*
 * Frozen eight-record ASENSURE failure capsule, retained only in a logging
 * build. Input: in-place observations of one boot-time creation operation;
 * output: a 64-byte suffix for bootlog.bin after recovery remount. Why: the
 * timeout path must snapshot before it destroys the live FAT/SD state, yet
 * must not perform diagnostic I/O while the stalled operation owns the card.
 * Region/lifetime/owner: 64 bytes normal SRAM1 for one boot, filesystem.c.
 */
static uint8_t fs_hcprms_boot_capsule[HCPRMS_BOOT_CAPSULE_BYTES];

/*
 * DEV_LOGGING_IWDG retained cross-reset capsule (config.h owns the feature
 * contract). Region/lifetime/owner: 12 of 32 approved bytes, SRAM2, the
 * `.devwdg_noinit` linker section (STM32F765VIHx_FLASH.ld), one boot
 * attempt, filesystem.c. That section is explicitly excluded from
 * Reset_Handler's zero-fill loop, so this survives an IWDG-caused warm
 * reset -- it is lost only on an actual power cycle, which is not the
 * failure this exists to catch.
 *
 * Input: the same eight-byte code already tracked by fs_boot_logging_code,
 * mirrored here on every Arm/SetDetail call below. Output: read back once,
 * at the very next boot, by filesystem_devIwdgBootCheck(); `magic` alone is
 * never trusted as proof of a watchdog reset -- RCC_CSR's IWDGRSTF hardware
 * flag is the actual gate, checked before this capsule is ever consulted.
 */
#if DEV_LOGGING_IWDG
#define DEV_IWDG_CAPSULE_MAGIC 0x49574447u /* ASCII "IWDG" */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  code[8];
} devIwdgCapsule_t;
__attribute__((section(".devwdg_noinit")))
static volatile devIwdgCapsule_t fs_devwdg_capsule;
static uint32_t fs_devwdg_boot_start_tick = 0u;
static uint8_t  fs_devwdg_lapsed = 0u;
/*
 * Nonzero only after filesystem_devIwdgStart() actually reached the 0xCCCC
 * start key. It gates the feed so that a boot which deliberately skipped the
 * watchdog (LSI never became ready) never pretends to have one, and so the
 * DEV_LOGGING_IWDG_EXPIRE backstop cannot report a lapse for a watchdog that
 * was never armed. Region/lifetime/owner: one byte of normal SRAM1 inside the
 * existing DEV_MODE_LOGGING-gated block, one boot attempt, filesystem.c.
 */
static uint8_t  fs_devwdg_armed = 0u;
#endif
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

/*
 * Bound each trace append to the existing staging buffer, independently of
 * the larger retained ring. The assertion prevents a record-size or buffer
 * change from making the batch serializer overrun staging_buf.
 */
#define AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS \
    ((uint16_t)(sizeof(staging_buf) / AUTOSAVE_TRACE_RECORD_BYTES))
_Static_assert((sizeof(staging_buf) % AUTOSAVE_TRACE_RECORD_BYTES) == 0u,
               "staging_buf must hold whole autosave trace records");

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
/* Result paired with the asynchronous remove latch; failure must gate create. */
static afatfsResultCode_t op_remove_result = AFATFS_RESULT_OK;
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
static uint16_t op_delete_slot_number = 0u;
/* Complete scan result copied for one exact delete; no display re-lookup. */
static afatfsObjectInfo_t op_delete_slot_target;
/* Scan latches prove singularity before any delete is accepted. */
static uint8_t op_delete_slot_match_count = 0u;
static uint8_t op_delete_slot_scan_error = 0u;
/*
 * Distinguishing reason code for a non-BUSY FS_STATUS_ERROR completion.
 *
 * What: a small enum-like byte identifying exactly which of
 * filesystem_deleteSlotDirectory_tick()'s several distinct failure branches
 * produced this session's error, plus (for the two delete-outcome reasons)
 * the raw afatfsResultCode_t in the low byte of
 * op_delete_slot_error_detail. Why: the ScnS05/KitS/BnkS-visible failure
 * status alone cannot distinguish a scan I/O error from a malformed LFN, a
 * duplicate same-slot candidate, a rejected native-delete start, or a
 * non-OK native-delete result — five architecturally different problems
 * that need different next steps. Inputs: set once per session, at the
 * single branch that actually failed. Outputs: read by the caller's own
 * O/DELETE_RESULT trace record (filesystem_saveKitDirectory_tick() /
 * filesystem_saveSceneDirectory_tick()) immediately after this function
 * returns FS_STATUS_ERROR; reset to FS_DELETE_SLOT_REASON_NONE only by
 * filesystem_deleteSlotDirectoryStart(). Affiliates:
 * AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT's packed value.
 */
typedef enum {
    FS_DELETE_SLOT_REASON_NONE = 0u,
    FS_DELETE_SLOT_REASON_SCAN_IO,
    FS_DELETE_SLOT_REASON_MALFORMED_LFN,
    FS_DELETE_SLOT_REASON_WRONG_KIND,
    FS_DELETE_SLOT_REASON_DUPLICATE,
    FS_DELETE_SLOT_REASON_MATCH_COUNT_BACKSTOP,
    FS_DELETE_SLOT_REASON_DELETE_REJECTED,
    FS_DELETE_SLOT_REASON_DELETE_RESULT,
    /*
     * The "." open that OPEN_SCAN/the post-stall WAIT_SCAN recovery block
     * issues resolved to a NULL handle. Unlike every other branch here, this
     * one does not depend on scan progress at all -- opening "." is a
     * synchronous snapshot of afatfs.currentDirectory (see
     * afatfs_createFileInternal()'s strcmp(name, ".") == 0 branch), so a
     * failure here points either at open-handle-pool pressure from a
     * concurrent/leaked handle, or at afatfs.currentDirectory itself being
     * invalid at the moment this operation's chdir left off.
     */
    FS_DELETE_SLOT_REASON_DIR_OPEN_FAILED,
    /*
     * The stall observer (filesystem_pollPhaseStall(), 50,000-poll budget)
     * gave up on OPEN_SCAN/SCAN_NEXT/CLOSE_SCAN specifically (not the three
     * phases that already have their own deliberate "observation, not
     * cancellation" wait) and abandoned the scan. A paired
     * AUTOSAVE_TRACE_STAGE_PHASE_STALL record for site DELETE_SLOT always
     * precedes this in the trace.
     */
    FS_DELETE_SLOT_REASON_STALL_ABANDONED
} fs_delete_slot_reason_t;

static fs_delete_slot_reason_t op_delete_slot_error_reason =
    FS_DELETE_SLOT_REASON_NONE;
static uint8_t op_delete_slot_error_detail = 0u;
/*
 * The exact internal asyncfatfs check behind a FS_DELETE_SLOT_REASON_DELETE_RESULT
 * failure (afatfsDeleteTreeFailureSite_e, asyncfatfs.c). Read via
 * afatfs_getDeleteTreeFailureSite() immediately after op_delete_tree_result
 * is known non-OK, before starting any other delete -- that call resets it.
 * Only meaningful when op_delete_slot_error_reason ==
 * FS_DELETE_SLOT_REASON_DELETE_RESULT; zero (NONE) otherwise. Affiliates:
 * AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT's packed value.
 */
static uint8_t op_delete_slot_error_site = 0u;
static uint8_t op_delete_slot_timeout_observed = 0u;
/*
 * Completion latch for asyncfatfs' maintained recursive deleter.
 *
 * What: op_delete_tree_done/op_delete_tree_result record whether/how the one
 * foreground-pumped afatfs_deleteTree() call for this slot finished.
 * op_delete_slot_timeout_observed is separate, purely diagnostic state: it
 * records that the 50,000-poll stall counter fired during scan or delete, but
 * never turns a later AFATFS_RESULT_OK/clean-scan result into FS_STATUS_ERROR.
 * Why: observation is not cancellation; native delete has no abort API, so a
 * slow legitimate nested-tree delete must finish and report its true result.
 * The completion gates therefore use only scan-error/duplicate latches and
 * the native delete result. The observation remains durable through the
 * AUTOSAVE_TRACE_STAGE_PHASE_STALL record emitted by
 * filesystem_deleteSlotDirectory_tick(). Inputs: afatfs_deleteTree()'s
 * callback for the object captured by the slot scanner. Output: the
 * slot-delete state machine learns whether the one native delete finished and
 * how. Affiliates: filesystem_deleteSlotDirectory_tick(),
 * filesystem_pollPhaseStall(), and autosaveTrace_record().
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
/*
 * Persistent HCNAMES provenance register: one two-byte source per logical
 * Bank/Scene/Kit/Instrument row.  This is the user-approved 258-byte cache;
 * it replaces SceneData's former 32-byte settings provenance array and never
 * belongs to playable Scene/Kit data or Menu scratch.
 */
static uint16_t fs_resident_source[FS_RESIDENT_NAMES_ROW_COUNT];
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
_Static_assert(sizeof(fs_resident_source) == 258u,
               "HCNAMES provenance register must remain 129 x uint16_t");
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
/*
 * Keep the configured CRC work interval inside the existing stream buffer.
 *
 * What: compile-time proof that every AutoSave CRC caller can request its
 * bounded interval without a second buffer. Why: the CRC cap is a cooperative
 * CPU-work contract, and silently exceeding staging_buf would either break
 * that contract or require forbidden permanent SRAM. Affiliates:
 * filesystem_autosaveCrcChunkBytes(), AutoSave validation, copy, creation,
 * and recovery phases below.
 */
_Static_assert(AUTOSAVE_CRC_BYTES_PER_TICK > 0u &&
               AUTOSAVE_CRC_BYTES_PER_TICK <= sizeof(staging_buf),
               "autosave CRC budget must fit the existing staging buffer");
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
static char op_save_bank_dir_open_name[AFATFS_SHORT_FILENAME_MAX];
static uint16_t op_bank_child_present_mask = 0u;
static uint16_t op_bank_scene_load_mask = 0u;
static uint16_t op_bank_scene_save_mask = 0u;
static uint8_t op_bank_active_scene = 0u;
static uint8_t op_bank_child_cursor = 0u;
static uint8_t op_bank_loaded_scene = 0u;
/*
 * Bank-Load-owned copy of the currently validated child Scene name.
 *
 * Inputs: copied from op_scene_display_name at phase 31, immediately after
 * the Bank-parent rescan has completed successfully for op_bank_child_cursor.
 * Output: phase 61's HCNAMES overlay reads this independent value instead of
 * trusting shared Scene/Kit scratch across the intervening settings, Kit,
 * Instrument, Pattern, and Effect phases. Storage: exactly 9 bytes in normal
 * SRAM1 .bss, owned by this asynchronous filesystem operation and reused for
 * one child at a time. Why: Session 056 found a Bank-local Scene row changed
 * to an unrelated root-library name while the matching Kit/Instrument rows
 * remained correct; freezing the validated name closes that shared-scratch
 * lifetime regardless of which intervening phase caused the drift. Affiliates:
 * phase 27 reset, phase 31 capture, and
 * filesystem_cacheCurrentBankSceneNameBlock()'s phase 61 read.
 */
static char op_bank_child_scene_display_name[STORAGE_SCENE_DISPLAY_NAME_LEN + 1u];
static uint8_t op_bank_payload_active = 0u;
static uint8_t op_rename_done = 0u;
/* Result paired with rename completion; open-name output is trusted only on OK. */
static afatfsResultCode_t op_rename_result = AFATFS_RESULT_OK;
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
#if DEV_MODE_LOGGING && DEV_LOGGING_IWDG
/*
 * Declared ahead of its definition because both foreground pumps feed the
 * watchdog: filesystem_tick() (defined far below, beside the feed itself) and
 * filesystem_blockPoll() (defined earlier, for the blocking helpers that
 * deliberately bypass filesystem_tick()). See the call site in
 * filesystem_blockPoll() for why that second feed is mandatory.
 */
static void filesystem_devIwdgFeed(void);
#endif
/* Shared edge-triggered stall detector used by the foreground state machines. */
static uint8_t filesystem_pollPhaseStall(uint8_t phase,
                                         uint8_t *last_phase,
                                         uint32_t *stall_ticks,
                                         uint32_t threshold_ticks);
static void filesystem_autosaveTraceFlush_tick(void);
static void filesystem_settingsWriterSchedule_tick(void);
static void filesystem_settingsWriterCompleted(void);
static void filesystem_autosaveWriterSchedule_tick(void);
static void filesystem_autosaveWriterCompleted(void);
static void filesystem_autosaveTraceFlushSchedule_tick(void);
static void filesystem_autosaveTraceFlushCompleted(void);
static void filesystem_autosaveTraceCaptured(uint8_t budget_exhausted);
static void filesystem_autosaveSetupCompleted(void);
static void filesystem_clearResidentSourceDirtyFlags(void);
static void filesystem_beginResidentNamePublish(fs_internal_op_t publish_op);
static uint8_t filesystem_residentNameIsBlank(const char *name);
static uint8_t filesystem_formatResidentNameLine(char *dst,
                                                 uint16_t cap,
                                                 const char *name,
                                                 uint8_t present,
                                                 uint16_t source,
                                                 uint16_t row);
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
/* Hidden reversible-load origin: ordinary temp, or Morph-only temp snapshot. */
#define FS_INSTRUMENT_LOAD_TEMP_NONE  0u
#define FS_INSTRUMENT_LOAD_TEMP_NORMAL 1u
#define FS_INSTRUMENT_LOAD_TEMP_MORPH  2u
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
 * Running raw-byte fingerprint for one normal root Instrument Save.
 *
 * What: retains the four-byte CRC32C accumulator while the text serializer
 * spans foreground polls. Why: the lifecycle trace needs content evidence for
 * an overwrite without buffering the complete Instrument file. Inputs are
 * bytes accepted by afatfs_fwrite(); output is the CREATE_RESULT CRC16.
 * Affiliates: filesystem_writeTextLine() and Autosave.h's raw CRC helper.
 */
static uint32_t op_instrument_save_content_crc = 0u;

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
/*
 * Edge latches make the evidence records bounded: W and F describe an episode
 * once instead of appending on every foreground tick while a page/command
 * remains active. The dropped-count latch makes G describe each new amount of
 * ring loss once. All three objects exist only with the logging ring, so an
 * ordinary build receives no production-RAM allocation for this observer.
 */
static uint8_t fs_autosave_suppress_witness = 0u;
static uint8_t fs_trace_suppress_witness = 0u;
static uint16_t fs_trace_reported_dropped = 0u;
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

static void on_remove_complete(afatfsResultCode_t result)
{
    /*
     * Mark completion of asyncfatfs overwrite preflight work.
     *
     * What: Latches that afatfs_removeObjects_lfn() has called back. The
     * following state-machine phase decides whether creation may proceed from
     * this structured result.
     *
     * Why: File overwrite is now a two-step operation: collapse same-casefold
     * file variants before writing, then create exactly one object with the
     * user's entered case. The filesystem pump needs this callback bridge
     * between asyncfatfs completion and the next phase.
     *
     * Affiliates/clients: filesystem_saveInstrument_tick(), InstrumentMrp
     * Save, and the autosave writer's inactive-record deduplication phases.
     */
    op_remove_result = result;
    op_remove_done = 1u;
}

static void on_rename_complete(afatfsResultCode_t result)
{
    /*
     * Latch completion of an async directory rename.
     *
     * Inputs: asyncfatfs_renameObject_lfn() invokes this after it has either
     * rewritten the object name run or failed to find/rename the source.
     * Output: callers receive the structured result and may inspect the
     * caller-owned open-name buffer only when it is OK.
     */
    op_rename_result = result;
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
     * Input: every keyed settings load before file overlay. Output: AutoSave
     * starts ON.  HCNAMES, not settings.cfg, owns resident provenance, so this
     * reset deliberately cannot alter source fallback state.
     */
    parameter_values[PAR_AUTOSAVE_ENABLED] = 1u;
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
    if (strncmp(key, "scene_source_", 13u) == 0) {
        /* Legacy v1 provenance is deliberately ignored: HCNAMES owns it. */
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

#if DEV_MODE_LOGGING
/* The non-printable range cannot collide with ASCII boot tokens or trace stages. */
#define FS_HCPRMS_CAPSULE_CONTEXT_STAGE    0xe0u
#define FS_HCPRMS_CAPSULE_PROGRESS_STAGE   0xe1u
#define FS_HCPRMS_CAPSULE_CHUNK_STAGE      0xe2u
#define FS_HCPRMS_CAPSULE_CURSOR_STAGE     0xe3u
#define FS_HCPRMS_CAPSULE_ALLOC_STAGE      0xe4u
#define FS_HCPRMS_CAPSULE_OWNER_STAGE      0xe5u
#define FS_HCPRMS_CAPSULE_CACHE_STAGE      0xe6u
#define FS_HCPRMS_CAPSULE_TRANSPORT_STAGE  0xe7u
#define FS_HCPRMS_CAPSULE_ACTIVE_FLAG      0x80u
#define FS_HCPRMS_CAPSULE_FROZEN_FLAG      0x01u

/* Store fixed-width fields explicitly so the on-card capsule is little-endian. */
static void filesystem_hcprmsCapsulePut16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
}

static void filesystem_hcprmsCapsulePut24(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
}

static void filesystem_hcprmsCapsulePut32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

static uint8_t filesystem_hcprmsCapsuleIsActive(void)
{
    return (fs_hcprms_boot_capsule[7] & FS_HCPRMS_CAPSULE_ACTIVE_FLAG) != 0u;
}

static uint8_t filesystem_hcprmsCapsuleIsFrozen(void)
{
    return (fs_hcprms_boot_capsule[7] & FS_HCPRMS_CAPSULE_FROZEN_FLAG) != 0u;
}

static void filesystem_hcprmsCapsuleBegin(void)
{
    /*
     * Start a fresh in-place capsule only when ASENSURE enters phase zero.
     * Input: no target has been selected yet; output: active schema marker
     * and stable stage tags, with no FAT/SD call. Why: a later timeout can be
     * tied to this exact ensure attempt without retaining a trace ring.
     */
    memset(fs_hcprms_boot_capsule, 0, sizeof(fs_hcprms_boot_capsule));
    fs_hcprms_boot_capsule[0] = FS_HCPRMS_CAPSULE_CONTEXT_STAGE;
    fs_hcprms_boot_capsule[1] = HCPRMS_BOOT_CAPSULE_SCHEMA_VERSION;
    fs_hcprms_boot_capsule[2] = 0xffu; /* target unknown before root scan */
    fs_hcprms_boot_capsule[7] = FS_HCPRMS_CAPSULE_ACTIVE_FLAG;
    fs_hcprms_boot_capsule[8] = FS_HCPRMS_CAPSULE_PROGRESS_STAGE;
    fs_hcprms_boot_capsule[16] = FS_HCPRMS_CAPSULE_CHUNK_STAGE;
    fs_hcprms_boot_capsule[24] = FS_HCPRMS_CAPSULE_CURSOR_STAGE;
    fs_hcprms_boot_capsule[32] = FS_HCPRMS_CAPSULE_ALLOC_STAGE;
    fs_hcprms_boot_capsule[40] = FS_HCPRMS_CAPSULE_OWNER_STAGE;
    fs_hcprms_boot_capsule[48] = FS_HCPRMS_CAPSULE_CACHE_STAGE;
    fs_hcprms_boot_capsule[56] = FS_HCPRMS_CAPSULE_TRANSPORT_STAGE;
}

static void filesystem_hcprmsCapsuleSetTarget(uint8_t target)
{
    /* Record the A/B scan coordinate without changing its operation scratch. */
    if (filesystem_hcprmsCapsuleIsActive() &&
        !filesystem_hcprmsCapsuleIsFrozen()) {
        fs_hcprms_boot_capsule[2] = target;
        fs_hcprms_boot_capsule[3] = op_phase;
    }
}

static void filesystem_hcprmsCapsuleNoteWrite(uint16_t requested,
                                               uint32_t written,
                                               uint16_t chunk_offset)
{
    uint8_t *progress = fs_hcprms_boot_capsule + 8u;
    uint8_t *chunk = fs_hcprms_boot_capsule + 16u;

    /*
     * Replace only the latest write coordinates, never append a per-tick log.
     * Inputs: the request before fwrite and its returned count; output: enough
     * state to expose partial progress inside a 512-byte chunk plus a
     * saturating zero-write streak. Why: op_bytes_done moves only after a
     * complete chunk and alone would conceal a stalled partial write.
     */
    if (!filesystem_hcprmsCapsuleIsActive() ||
        filesystem_hcprmsCapsuleIsFrozen())
        return;
    filesystem_hcprmsCapsulePut32(progress + 1u, op_bytes_done);
    filesystem_hcprmsCapsulePut16(progress + 5u, op_write_line_len);
    if (written == 0u) {
        if (progress[7] != 0xffu)
            progress[7]++;
    } else {
        progress[7] = 0u;
    }
    filesystem_hcprmsCapsulePut16(chunk + 1u, (uint16_t)written);
    filesystem_hcprmsCapsulePut16(chunk + 3u, chunk_offset);
    filesystem_hcprmsCapsulePut16(chunk + 5u, requested);
    chunk[7] = op_file_version;
}

static void filesystem_hcprmsCapsuleFreeze(void)
{
    afatfsDiagnosticSnapshot_t fat_snapshot;
    sdcardTransportSnapshot_t sd_snapshot;
    uint8_t *context = fs_hcprms_boot_capsule;
    uint8_t *cursor = fs_hcprms_boot_capsule + 24u;
    uint8_t *allocation = fs_hcprms_boot_capsule + 32u;
    uint8_t *owner = fs_hcprms_boot_capsule + 40u;
    uint8_t *cache = fs_hcprms_boot_capsule + 48u;
    uint8_t *transport = fs_hcprms_boot_capsule + 56u;
    uint32_t cluster_bytes;
    uint8_t allocation_flags = 0u;

    /*
     * Freeze exactly once before timeout recovery aborts transport or destroys
     * AsyncFATFS. Input: live ensure scratch plus read-only FAT/SD snapshots;
     * output: a complete 64-byte capsule with no poll, file operation, cache
     * release, allocation, delay, or callback. Why: post-abort observations
     * describe recovery, not the 32-KiB failure being diagnosed.
     */
    if (!filesystem_hcprmsCapsuleIsActive() ||
        filesystem_hcprmsCapsuleIsFrozen())
        return;
    afatfs_getDiagnosticSnapshot(op_file, &fat_snapshot);
    sdcard_getTransportSnapshot(&sd_snapshot);
    context[2] = op_file_version;
    context[3] = op_phase;
    context[4] = (uint8_t)status;
    context[5] = fat_snapshot.file_operation;
    context[6] = fat_snapshot.append_phase;
    context[7] |= FS_HCPRMS_CAPSULE_FROZEN_FLAG;
    filesystem_hcprmsCapsulePut32(cursor + 1u, fat_snapshot.cursor_offset);
    filesystem_hcprmsCapsulePut24(cursor + 5u, fat_snapshot.logical_size);
    filesystem_hcprmsCapsulePut32(allocation + 1u,
                                  fat_snapshot.search_cluster);
    allocation[5] = (fat_snapshot.sectors_per_cluster > 0xffu)
        ? 0xffu : (uint8_t)fat_snapshot.sectors_per_cluster;
    allocation[6] = fat_snapshot.search_wrapped;
    if (fat_snapshot.filesystem_full)
        allocation_flags |= 0x01u;
    if (fat_snapshot.available)
        allocation_flags |= 0x02u;
    cluster_bytes = fat_snapshot.sectors_per_cluster * 512u;
    if (cluster_bytes != 0u && op_bytes_done % cluster_bytes == 0u)
        allocation_flags |= 0x04u;
    allocation[7] = allocation_flags;
    filesystem_hcprmsCapsulePut32(owner + 1u,
                                  fat_snapshot.append_previous_cluster);
    filesystem_hcprmsCapsulePut24(owner + 5u,
                                  fat_snapshot.cursor_cluster);
    cache[1] = fat_snapshot.cache_dirty_count;
    cache[2] = fat_snapshot.cache_locked_count;
    cache[3] = fat_snapshot.cache_reading_count;
    cache[4] = fat_snapshot.cache_writing_count;
    cache[5] = fat_snapshot.cache_flush_in_progress;
    cache[6] = (fat_snapshot.active_cache_index < 0) ? 0xffu :
        (uint8_t)fat_snapshot.active_cache_index;
    cache[7] = fat_snapshot.filesystem_full;
    transport[1] = sd_snapshot.state;
    transport[2] = sd_snapshot.operation;
    filesystem_hcprmsCapsulePut16(transport + 3u, sd_snapshot.offset);
    filesystem_hcprmsCapsulePut16(transport + 5u, sd_snapshot.retry_count);
    transport[7] = sd_snapshot.callback_pending;
}

static uint32_t filesystem_hcprmsCapsulePayloadBytes(void)
{
    /* A non-ASENSURE failure retains the existing eight-byte bootlog format. */
    return filesystem_hcprmsCapsuleIsFrozen()
        ? HCPRMS_BOOT_CAPSULE_BYTES : 0u;
}
#endif

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
    /*
     * Capture ASENSURE's live application/FAT/SD state before publishing the
     * error that leads to abort/destroy recovery. Why: all lower-level state
     * is deliberately invalidated by that recovery, so freezing afterward
     * would report the logger remount instead of the stalled hidden-record
     * creation. No other boot operation allocates this AutoSave-specific data.
     */
    if (current_op == FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES)
        filesystem_hcprmsCapsuleFreeze();
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
    /* Clear the prior boot's forensic image before a new ASENSURE can own it. */
    memset(fs_hcprms_boot_capsule, 0, sizeof(fs_hcprms_boot_capsule));
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
#if DEV_LOGGING_IWDG
    fs_devwdg_capsule.magic = DEV_IWDG_CAPSULE_MAGIC;
    memcpy((void *)fs_devwdg_capsule.code, code, sizeof(fs_devwdg_capsule.code));
#endif
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
#if DEV_LOGGING_IWDG
    memcpy((void *)fs_devwdg_capsule.code, code, sizeof(fs_devwdg_capsule.code));
#endif
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
    /*
     * Universal error-completion witness -- see AutosaveTrace.h's full
     * rationale beside AUTOSAVE_TRACE_STAGE_OPERATION_ERROR.
     *
     * What: records current_op/op_phase/op_slot for every single
     * FS_STATUS_ERROR completion in this file, with no per-branch call site
     * to add or forget. Why: this session's own retest cycle showed that
     * hand-picking which failure branches to instrument reliably misses one
     * (the delete-slot resolver's ". open failed" path went untagged for a
     * full round-trip before being found), and there is no way to
     * enumerate every current and future failure branch across every Load/
     * Save/scan/HCNAMES/index operation in this facade by hand with
     * confidence. Hooking the one shared completion point instead makes
     * that enumeration unnecessary: whatever fails, however it fails,
     * whenever a future change adds a new failure path nobody thought to
     * instrument, this still fires. Inputs: current_op, op_phase, op_slot,
     * and whether the delete-slot resolver already tagged a more specific
     * reason for this same failure. Outputs: one trace record; no state
     * change, no behavior change. Affiliates: AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE
     * DELETE_RESULT's packed delete-slot reason (more specific, when
     * present), AUTOSAVE_TRACE_STAGE_PHASE_STALL (more specific, when a
     * stall preceded this).
     */
    if (final_status == FS_STATUS_ERROR) {
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_OPERATION_ERROR,
            (op_delete_slot_error_reason != FS_DELETE_SLOT_REASON_NONE)
                ? AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_DELETE_REASON_SET : 0u,
            ((uint32_t)current_op <<
                 AUTOSAVE_TRACE_OPERATION_ERROR_OP_SHIFT) |
            ((uint32_t)op_phase <<
                 AUTOSAVE_TRACE_OPERATION_ERROR_PHASE_SHIFT) |
            ((uint32_t)op_slot <<
                 AUTOSAVE_TRACE_OPERATION_ERROR_SLOT_SHIFT));
    }
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

    /*
     * Universal error-completion witness, second instance.
     *
     * This is a deliberately separate terminal function from
     * filesystem_complete() -- see AutosaveTrace.h beside
     * AUTOSAVE_TRACE_STAGE_OPERATION_ERROR for the full rationale. Every
     * Save path's own primary write can succeed and still have its
     * subsequent `.hcindex`/typed-Instrument-index rebuild fail here,
     * completely bypassing filesystem_complete(); without this second copy
     * of the same hook, that whole failure class would stay invisible no
     * matter how thoroughly the primary completion path is instrumented.
     * flags bit 1 (INDEX_REBUILD) distinguishes this record from a primary
     * one at decode time.
     */
    if (final_status == FS_STATUS_ERROR) {
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_OPERATION_ERROR,
            (uint8_t)(AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_INDEX_REBUILD |
                      ((op_delete_slot_error_reason != FS_DELETE_SLOT_REASON_NONE)
                           ? AUTOSAVE_TRACE_OPERATION_ERROR_FLAG_DELETE_REASON_SET
                           : 0u)),
            ((uint32_t)current_op <<
                 AUTOSAVE_TRACE_OPERATION_ERROR_OP_SHIFT) |
            ((uint32_t)op_phase <<
                 AUTOSAVE_TRACE_OPERATION_ERROR_PHASE_SHIFT) |
            ((uint32_t)op_slot <<
                 AUTOSAVE_TRACE_OPERATION_ERROR_SLOT_SHIFT));
    }
    op_library_index_rebuild_callback = NULL;
    op_library_index_rebuild_kind = FS_NAME_CACHE_NONE;
    op_library_index_rebuild_pending = 0u;
    status = final_status;
    current_op = FS_INTERNAL_OP_NONE;
    if (cb)
        cb();
}

/*
 * Rebuild one registry-owned Instrument cache after HCNAMES borrowed it.
 *
 * What: starts the existing single-type physical Instrument scan for the
 * selected saved type and parks the library-rebuild callback. Why: the shared
 * cache is deliberately retagged as HCNAMES during Instrument provenance
 * publication, so writing `.hcindex` directly afterward would serialize the
 * wrong cache domain. Inputs: op_instrument_index_type and the parked callback.
 * Outputs: one typed cache scan, followed by the existing index writer; no
 * second name array or extra persistent scratch. Affiliates:
 * filesystem_startLibraryIndexRebuild(), filesystem_scanInstruments_tick(),
 * and filesystem_libraryIndexRebuildScanComplete().
 */
static bool filesystem_startInstrumentIndexRebuildScan(fs_completion_cb_t cb)
{
    uint8_t i;
    uint8_t registry_index = instrumentManager_registryCount();

    if (op_instrument_index_type >= INSTRUMENT_TYPE_UNKNOWN ||
        status == FS_STATUS_BUSY)
        return false;
    for (i = 0u; i < instrumentManager_registryCount(); i++) {
        const instrument_registry_entry_t *entry =
            instrumentManager_registryEntryAt(i);

        if (entry && entry->type == op_instrument_index_type) {
            registry_index = i;
            break;
        }
    }
    if (registry_index >= instrumentManager_registryCount())
        return false;
    filesystem_clearInstrumentCacheStorage();
    fs_list_cache_kind = FS_NAME_CACHE_INSTRUMENT;
    fs_list_cache_type = op_instrument_index_type;
    op_instrument_scan_one_type = 1u;
    op_instrument_scan_registry_index = registry_index;
    return filesystem_start(FS_INTERNAL_OP_SCAN_INSTRUMENTS,
                            FS_FILE_KIT, 0u, cb);
}

/* Start the boot-equivalent physical rescan/index rewrite after a save flush.
 *
 * What: replaces the old cache-row-only save update with a real Kit/Scene/Bank
 * directory scan. Why: a save can create, rename, or remove a numbered folder;
 * only scanning the parent directory observes the complete resulting set.
 * Inputs: op_library_index_rebuild_kind and the parked original callback.
 * Output: numbered-root kinds are rescanned before their full `.hcindex`
 * writer; Instrument Save already refreshed its typed cache, so its selected
 * registry `.hcindex` is written directly. No additional operation scratch is
 * allocated for the distinction.
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

    if (kind == FS_NAME_CACHE_INSTRUMENT) {
        if (!filesystem_startInstrumentIndexRebuildScan(
                filesystem_libraryIndexRebuildScanComplete)) {
            filesystem_makeNamedErrorCode("Idx", 2u);
            filesystem_completeLibraryIndexRebuild(FS_STATUS_ERROR);
        }
        return;
    }

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
static void filesystem_libraryIndexDetail(const char label[8])
{
    /*
     * Retain which library-index phase is currently executing.
     *
     * What: patches the caller's eight-byte label with the active library
     * letter and publishes it through filesystem_bootLoggingSetDetail(),
     * which writes both the retained boot-log code and, in a DEV_LOGGING_IWDG
     * build, the SRAM2 watchdog capsule.
     *
     * Why it must exist: FS_INTERNAL_OP_CREATE_LIBRARY_INDEX arms one code,
     * "LIBINDEX", for all eight of its phases. SD_CARD5's watchdog capsule
     * therefore identified the operation but not the library, not the phase,
     * and not the primitive that stalled -- the whole eight-phase chain
     * collapsed to one word. Per-phase detail is the cheapest way to resolve
     * that, because SetDetail already updates the capsule and already leaves
     * the enclosing deadline untouched, so labelling cannot mask a timeout.
     *
     * Inputs: an exactly-eight-byte label whose fourth byte is a placeholder,
     * plus op_library_index_kind. Output: one retained code; no I/O, no
     * allocation, no phase change. Called on every entry to a phase including
     * retries, which is intentional: the capsule must always hold the phase
     * that was current when the foreground stopped, not the last one entered.
     *
     * Affiliates: filesystem_bootLoggingSetDetail(),
     * filesystem_createLibraryIndex_tick(), DEV_MODES.md's boot-code table,
     * and S056_BOOT_HANG_FOLLOWUP.md section 14.3.
     */
    char detail[8];

    memcpy(detail, label, sizeof(detail));
    detail[3] = (op_library_index_kind == FS_NAME_CACHE_KIT)   ? 'K'
              : (op_library_index_kind == FS_NAME_CACHE_SCENE) ? 'S'
              : (op_library_index_kind == FS_NAME_CACHE_BANK)  ? 'B'
                                                               : '?';
    filesystem_bootLoggingSetDetail(detail);
}

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
        filesystem_libraryIndexDetail("LIX?ROOT");
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
        filesystem_libraryIndexDetail("LIX?WAIT");
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
        filesystem_libraryIndexDetail("LIX?ENTR");
        if (!afatfs_chdir(op_kit_root_dir))
            return;
        op_phase = 3u;
        return;

    case 3: /* CLOSE ROOT DIRECTORY HANDLE */
        filesystem_libraryIndexDetail("LIX?CLOS");
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 4u;
        return;

    case 4: /* WAIT ROOT CLOSE + OPEN INDEX */
        filesystem_libraryIndexDetail("LIX?IOPN");
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
        filesystem_libraryIndexDetail("LIX?IWAI");
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 6u;
        return;

    case 6: /* WRITE SLOT-ORDERED ROWS */
        filesystem_libraryIndexDetail("LIX?ROWS");
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
        filesystem_libraryIndexDetail("LIX?DONE");
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
        /* Initial/recovery serialization also emits paired source tokens;
         * make any staged values clean only after its register close. */
        filesystem_clearResidentSourceDirtyFlags();
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
 * the eight-byte printable culprit and, only for a frozen ASENSURE timeout,
 * its 64-byte raw forensic suffix, then closes and enters the ordinary sync
 * gate. Why: recovery must obey the same single-owner asyncfatfs rules as
 * every other facade write and must not claim durability after only updating a
 * cache. The retained payload survives dirty recovery; ordinary failures stay
 * eight bytes, while ASENSURE becomes one 72-byte no-NUL/no-newline image.
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

    case 2u: /* WRITE BOOT TOKEN + OPTIONAL FROZEN ASENSURE CAPSULE */
#if DEV_MODE_LOGGING
        if (op_bytes_done < 8u + filesystem_hcprmsCapsulePayloadBytes()) {
            const uint8_t *source;
            uint32_t remaining;
            uint32_t n;

            /*
             * Select only retained RAM that survived the dirty teardown.
             * Input: absolute bootlog payload offset; output: one bounded
             * partial-write request. Why: the capsule must follow the ASCII
             * token in one durable file, but must never be emitted for an
             * unrelated boot failure or read from destroyed operation scratch.
             */
            if (op_bytes_done < 8u) {
                source = fs_boot_logging_code + op_bytes_done;
                remaining = 8u - op_bytes_done;
            } else {
                source = fs_hcprms_boot_capsule + (op_bytes_done - 8u);
                remaining = 8u + filesystem_hcprmsCapsulePayloadBytes() -
                    op_bytes_done;
            }
            n = afatfs_fwrite(op_file, source, remaining);
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
 * Inputs: a pending-count snapshot capped by AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS.
 * Output:
 * `byte_count` receives its exact eight-byte-record length and staging_buf
 * contains records in oldest-first order. Why: trace owns no filesystem
 * buffer, while one bounded batch fits the existing one-operation staging
 * buffer without introducing another retained allocation; repeated flushes
 * drain a larger retained ring. Affiliate: filesystem_autosaveTraceFlush_tick().
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
        if (op_stream_index > AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
            op_stream_index = AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS;
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

/* Validate one unflagged source value against the fixed HCNAMES row class. */
static uint8_t filesystem_residentSourceValid(uint16_t row, uint16_t source)
{
    source = (uint16_t)(source & FS_RESIDENT_SOURCE_VALUE_MASK);
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT)
        return 0u;
    if (source < FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT ||
        source == FS_RESIDENT_SOURCE_INHERIT ||
        source == FS_RESIDENT_SOURCE_UNKNOWN) {
        return 1u;
    }
    return (uint8_t)(source == FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT &&
                     row >= FS_RESIDENT_NAMES_INSTRUMENT_BASE);
}

uint16_t filesystem_residentSource(uint16_t row)
{
    /*
     * Return one logical HCNAMES provenance word without exposing its pending
     * rewrite flag.  Inputs are fixed register coordinates; output is UNKNOWN
     * for invalid rows.  This is RAM-only and never opens the register.
     */
    return row < FS_RESIDENT_NAMES_ROW_COUNT
        ? (uint16_t)(fs_resident_source[row] & FS_RESIDENT_SOURCE_VALUE_MASK)
        : FS_RESIDENT_SOURCE_UNKNOWN;
}

uint8_t filesystem_setResidentSource(uint16_t row, uint16_t source)
{
    /*
     * Stage one source update for the next ordinary HCNAMES rewrite.
     *
     * The dirty flag survives the subsequent read of the old register, so an
     * asynchronous preserve/overlay update cannot overwrite a just-committed
     * load source with stale on-card provenance.  The physical file changes
     * only through the existing close/sync state machine.
     */
    if (!filesystem_residentSourceValid(row, source)) {
        /*
         * Make a rejected provenance stage visible in the same HCNAMES
         * diagnostic stream as a rejected name overlay.
         *
         * What: records the logical row and unmasked requested source before
         * returning the existing failure result. Why: callers historically
         * discarded this boolean, so a malformed coordinate could silently
         * leave a newly committed identity paired with its old provenance.
         * Inputs: the caller's row/source; output: one H record only, with no
         * state or allocation change. Affiliate: AutosaveTrace.h's H refusal
         * layout and S056_HCNAMES_FOLLOW_UP.md section 11.4.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT,
            AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_SOURCE_STAGE_REFUSED,
            ((uint32_t)row << AUTOSAVE_TRACE_NAME_SCRATCH_ROW_SHIFT) |
            ((uint32_t)source << AUTOSAVE_TRACE_NAME_SCRATCH_DETAIL_SHIFT));
        return 0u;
    }
    fs_resident_source[row] = (uint16_t)(
        (source & FS_RESIDENT_SOURCE_VALUE_MASK) |
        FS_RESIDENT_SOURCE_DIRTY_FLAG);
    return 1u;
}

uint8_t filesystem_lastHcnamesVerified(void)
{
    /*
     * Return the last Bank-owned row-0 read-back result.
     *
     * Inputs: the existing operation-local probe byte retained after Bank
     * Load/Save read-back close (phase 97 for Load, phase 92 for Save).
     * Output: one boolean for Preset's K callback. This
     * accessor performs no I/O and does not allocate or clear the probe; the
     * next Bank operation resets it before its own read-back. Affiliate:
     * on_bank_load_complete()/on_bank_save_complete() and section 11.3.
     */
    return (uint8_t)(op_hcnames_probe_matches != 0u);
}

uint16_t filesystem_resolveResidentSource(uint16_t row,
                                          uint16_t *resolved_row)
{
    /*
     * Resolve a resident row's explicit fallback source through its fixed
     * enclosing hierarchy.
     *
     * Inputs: one HCNAMES row and optional output for the row that supplied a
     * direct source. Output: a direct token/slot, or UNKNOWN once Bank has no
     * usable source. Why: AutoSave boot recovery must consult the same durable
     * register semantics for Instrument, Kit, Scene, and Bank rather than
     * recreating parent-offset arithmetic in a future reader. Unknown and
     * inherit both continue upward; a later missing direct target can use the
     * same traversal from its parent before the normal global fallback.
     */
    while (row < FS_RESIDENT_NAMES_ROW_COUNT) {
        uint16_t source = filesystem_residentSource(row);

        if (source < FS_RESIDENT_SOURCE_DIRECT_SLOT_LIMIT ||
            source == FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT) {
            if (resolved_row)
                *resolved_row = row;
            return source;
        }
        if (row >= FS_RESIDENT_NAMES_INSTRUMENT_BASE) {
            row = (uint16_t)(FS_RESIDENT_NAMES_KIT_BASE +
                             ((row - FS_RESIDENT_NAMES_INSTRUMENT_BASE) /
                              STORAGE_KIT_SLOT_COUNT));
        } else if (row >= FS_RESIDENT_NAMES_KIT_BASE) {
            row = (uint16_t)(1u + (row - FS_RESIDENT_NAMES_KIT_BASE));
        } else if (row >= 1u) {
            row = FS_IDENTITY_BANK_ROW;
        } else {
            break;
        }
    }
    if (resolved_row)
        *resolved_row = FS_RESIDENT_NAMES_ROW_COUNT;
    return FS_RESIDENT_SOURCE_UNKNOWN;
}

static void filesystem_prepareResidentNamesCache(void)
{
    uint16_t row;

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
    /* A short/legacy register cannot retain source words from a prior cache
     * use.  Keep only caller-staged dirty values until this transaction writes
     * them through the normal durable HCNAMES path. */
    for (row = 0u; row < FS_RESIDENT_NAMES_ROW_COUNT; row++) {
        if ((fs_resident_source[row] & FS_RESIDENT_SOURCE_DIRTY_FLAG) == 0u)
            fs_resident_source[row] = FS_RESIDENT_SOURCE_UNKNOWN;
    }
    fs_list_cache_kind = FS_NAME_CACHE_HCNAMES;
    fs_list_cache_count = FS_RESIDENT_NAMES_ROW_COUNT;
}

static uint8_t filesystem_parseResidentSourceToken(const char *token,
                                                   uint16_t row,
                                                   uint16_t *source_out)
{
    /*
     * Decode the compact provenance field of one extended HCNAMES record.
     * Inputs are the tab-suffix token and its fixed row class; output is one
     * validated unflagged source word.  Strict parsing prevents malformed card
     * text from silently becoming an inherited fallback.
     */
    uint16_t value;

    if (!token || !source_out)
        return 0u;
    if (strcmp(token, "-") == 0)
        value = FS_RESIDENT_SOURCE_INHERIT;
    else if (strcmp(token, "?") == 0)
        value = FS_RESIDENT_SOURCE_UNKNOWN;
    else if (strcmp(token, "@") == 0)
        value = FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT;
    else {
        if (token[0] < '0' || token[0] > '9' ||
            token[1] < '0' || token[1] > '9' ||
            token[2] < '0' || token[2] > '9' || token[3] != '\0') {
            return 0u;
        }
        value = (uint16_t)((token[0] - '0') * 100u +
                           (token[1] - '0') * 10u +
                           (token[2] - '0'));
    }
    if (!filesystem_residentSourceValid(row, value))
        return 0u;
    *source_out = value;
    return 1u;
}

static uint8_t filesystem_cacheResidentRecord(uint16_t row, const char *line)
{
    const char *tab;
    char name[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];
    uint16_t source = FS_RESIDENT_SOURCE_UNKNOWN;
    uint8_t name_len = 0u;

    /*
     * Parse one physical HCNAMES line into its paired name/source cache cells.
     * A legacy name-only line is accepted as UNKNOWN provenance; a new-format
     * row must contain exactly one tab and a valid source token.  A caller's
     * staged source wins over old card content until the rewrite is durable.
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        fs_list_cache_kind != FS_NAME_CACHE_HCNAMES || !line) {
        return 0u;
    }
    tab = strchr(line, '\t');
    if (tab) {
        const char *tail = tab + 1u;
        if (strchr(tail, '\t') != NULL ||
            !filesystem_parseResidentSourceToken(tail, row, &source)) {
            return 0u;
        }
        while (line + name_len < tab) {
            if (name_len >= STORAGE_KIT_DISPLAY_NAME_LEN)
                return 0u;
            name[name_len] = line[name_len];
            name_len++;
        }
        name[name_len] = '\0';
    } else {
        /* Legacy rows retain current name normalization and gain UNKNOWN. */
        while (line[name_len] != '\0') {
            if (name_len >= STORAGE_KIT_DISPLAY_NAME_LEN)
                return 0u;
            name[name_len] = line[name_len];
            name_len++;
        }
        name[name_len] = '\0';
    }
    memset(fs_list_cache_name[row], 0, sizeof(fs_list_cache_name[row]));
    if (name[0] != '\0')
        storage_copyDisplayName(fs_list_cache_name[row], name);
    fs_list_cache_name[row][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    if ((fs_resident_source[row] & FS_RESIDENT_SOURCE_DIRTY_FLAG) == 0u)
        fs_resident_source[row] = source;
    return 1u;
}

static void filesystem_cacheResidentName(uint16_t row, const char *name)
{
    /*
     * Overlay only a committed display name while retaining its paired source.
     * Callers that change provenance use filesystem_setResidentSource() before
     * the rewrite; this helper deliberately cannot manufacture an implicit
     * source from a UI string.
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT ||
        fs_list_cache_kind != FS_NAME_CACHE_HCNAMES) {
        /*
         * A refused overlay is a publication loss, not an ignorable no-op.
         *
         * What: records the requested row and live cache domain before the
         * existing early return. Why: a caller can otherwise report success
         * while the writer later streams the old row unchanged. Inputs are
         * the row and cache tag; output is one H record and no cache mutation.
         * Affiliate: the HCNAMES refusal layout in AutosaveTrace.h.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT,
            AUTOSAVE_TRACE_NAME_SCRATCH_FLAG_OVERLAY_REFUSED,
            ((uint32_t)row << AUTOSAVE_TRACE_NAME_SCRATCH_ROW_SHIFT) |
            ((uint32_t)fs_list_cache_kind <<
             AUTOSAVE_TRACE_NAME_SCRATCH_DETAIL_SHIFT));
        return;
    }
    memset(fs_list_cache_name[row], 0, sizeof(fs_list_cache_name[row]));
    if (name && name[0] != '\0')
        storage_copyDisplayName(fs_list_cache_name[row], name);
    fs_list_cache_name[row][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
}

static void filesystem_clearResidentSourceDirtyFlags(void)
{
    uint16_t row;

    /*
     * Publish staged source values only after the enclosing HCNAMES file has
     * closed through the normal flush gate.  Before this point the dirty flag
     * intentionally protects a newly committed load source from the old file
     * image being streamed into the cache.
     */
    for (row = 0u; row < FS_RESIDENT_NAMES_ROW_COUNT; row++) {
        fs_resident_source[row] &= FS_RESIDENT_SOURCE_VALUE_MASK;
    }
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

static uint16_t filesystem_residentPublishMask(void)
{
    /*
     * Select the immutable destination mask owned by the active HCNAMES
     * publisher. Scene publication keeps its original Scene mask even after
     * the Kit-family overlay reuses op_kit_load_scene_mask.
     */
    if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE)
        return op_scene_load_scene_mask;
    return op_kit_load_scene_mask;
}

static uint16_t filesystem_residentPublishProbeRow(void)
{
    uint16_t mask = filesystem_residentPublishMask();
    uint8_t scene_index;

    /*
     * Choose the lowest affected logical row for the generic read-back probe.
     * Inputs are the existing operation masks and Instrument destination slot;
     * output is a fixed HCNAMES row or the row-count sentinel for invalid
     * state. No coordinate is retained in new storage.
     */
    for (scene_index = 0u;
         scene_index < STORAGE_BANK_SCENE_MAX_SLOTS;
         scene_index++) {
        if ((mask & (uint16_t)(1u << scene_index)) == 0u)
            continue;
        if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE)
            return filesystem_residentSceneRow(scene_index);
        if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_KIT)
            return filesystem_residentKitRow(scene_index);
        return filesystem_residentInstrumentRow(scene_index,
                                                (uint8_t)op_slot);
    }
    return FS_RESIDENT_NAMES_ROW_COUNT;
}

static uint8_t filesystem_residentPublishClass(void)
{
    if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_KIT)
        return AUTOSAVE_TRACE_NAME_PUBLISH_CLASS_KIT;
    if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT)
        return AUTOSAVE_TRACE_NAME_PUBLISH_CLASS_INSTRUMENT;
    return AUTOSAVE_TRACE_NAME_PUBLISH_CLASS_SCENE;
}

static uint8_t filesystem_residentRowMatchesCache(uint16_t row,
                                                  const char *line)
{
    const char *tab;
    const char *cache_name;
    uint16_t source;
    uint8_t expected_len = 0u;
    uint8_t actual_len;
    uint8_t index;

    /*
     * Compare one physical HCNAMES row with the live staged cache pair.
     *
     * What: validates the tab-separated source token and compares the trimmed,
     * printable name serialization against the cache cell. Why: a successful
     * close and flush currently provide no evidence that the visible register
     * contains the newly staged row. Inputs: one row and its NUL-terminated
     * read-back line; outputs: a boolean only, with no I/O, mutation, or new
     * persistent storage. The source comparison masks the dirty bit so this
     * probe works before the enclosing transaction clears pending stages.
     * Affiliates: filesystem_cacheResidentRecord(),
     * filesystem_parseResidentSourceToken(), and the generic/Bank verify
     * phases below. See S056_HCNAMES_FOLLOW_UP.md section 11.3.
     */
    if (row >= FS_RESIDENT_NAMES_ROW_COUNT || !line)
        return 0u;
    tab = strchr(line, '\t');
    if (!tab || strchr(tab + 1u, '\t') != NULL)
        return 0u;
    cache_name = fs_list_cache_name[row];
    if (!filesystem_residentNameIsBlank(cache_name)) {
        expected_len = (uint8_t)SCENE_OBJECT_DISPLAY_NAME_LEN;
        while (expected_len > 0u &&
               (cache_name[expected_len - 1u] == ' ' ||
                cache_name[expected_len - 1u] == '\0')) {
            expected_len--;
        }
    }
    actual_len = (uint8_t)(tab - line);
    if (actual_len != expected_len)
        return 0u;
    for (index = 0u; index < expected_len; index++) {
        char expected = cache_name[index];
        if (expected < 0x20 || expected > 0x7e)
            expected = ' ';
        if (line[index] != expected)
            return 0u;
    }
    if (!filesystem_parseResidentSourceToken(tab + 1u, row, &source))
        return 0u;
    return (uint8_t)(source ==
                     (fs_resident_source[row] &
                      FS_RESIDENT_SOURCE_VALUE_MASK));
}

static void filesystem_recordResidentNamePublish(uint8_t status_done)
{
    uint8_t flags = (uint8_t)(
        (status_done != 0u)
            ? AUTOSAVE_TRACE_NAME_PUBLISH_FLAG_STATUS_DONE : 0u);

    /*
     * Emit the generic writer's one terminal proof record before the final
     * source cleanup. Inputs are the existing probe result in
     * op_hcnames_probe_matches, op_bytes_done's probe row, and the operation's
     * destination mask. Output is one U record; completion behavior is
     * unchanged. Affiliates: AutosaveTrace.h and the three update handoffs.
     */
    if (op_hcnames_probe_matches != 0u)
        flags |= AUTOSAVE_TRACE_NAME_PUBLISH_FLAG_HCNAMES_VERIFIED;
    flags |= (uint8_t)(filesystem_residentPublishClass() <<
                       AUTOSAVE_TRACE_NAME_PUBLISH_CLASS_SHIFT);
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_NAME_PUBLISH,
        flags,
        ((uint32_t)op_bytes_done << AUTOSAVE_TRACE_NAME_PUBLISH_ROW_SHIFT) |
        ((uint32_t)filesystem_residentPublishMask() <<
         AUTOSAVE_TRACE_NAME_PUBLISH_MASK_SHIFT));
}

static void filesystem_beginResidentNamePublish(fs_internal_op_t publish_op)
{
    /*
     * Re-target one completed payload operation at the shared HCNAMES writer.
     *
     * What: prepares the 129-row register and replaces current_op with the
     * requested UPDATE_HCNAMES_* operation, leaving op_phase at zero for the
     * read -> overlay -> write -> verify sequence. Why: identity publication
     * belongs to the filesystem operation that changed the payload and must
     * finish before its original callback is released; the old Menu boundary
     * publisher never executed in 69,012 recorded events. Inputs: publish_op,
     * already-staged source words, and the caller-owned destination mask.
     * Outputs: one shared HCNAMES transaction with the existing callback still
     * parked; no file is opened and no SRAM is allocated. This bypasses
     * filesystem_start() deliberately because that reset would erase the mask
     * and display-name scratch the overlay reads. Affiliates:
     * filesystem_prepareResidentNamesCache(),
     * filesystem_residentNames_tick(), and the six Load/Save completion sites.
     * See S056_HCNAMES_FOLLOW_UP.md section 11.2.
     */
    filesystem_prepareResidentNamesCache();
    /*
     * DEV_MODE_LOGGING writes operation codes to file for use in debugging.
     * It must never print anything to the screen or otherwise delay operations
     * unnecessarily since logging may be used to assess timing failures in
     * other modules that might otherwise be obscured by screen write delays.
     */
    filesystem_bootLoggingArm("HCNAMES ");
    current_op = publish_op;
    op_phase = 0u;
}

static void filesystem_cacheCurrentBankSceneNameBlock(uint8_t scene_index)
{
    uint8_t slot;

    /*
     * Overlay one successfully committed Bank child onto the HCNAMES register.
     *
     * Inputs: the Bank loader's one-bit child cursor, the child's Scene
     * display name in the Bank-Load-owned op_bank_child_scene_display_name
     * snapshot, and the resident Scene just atomically committed by the shared
     * Scene loader. Output: only that Scene row, its Kit row, and its six
     * Instrument rows change as name/source pairs in the borrowed cache. The
     * child hierarchy inherits the Bank source; unmasked resident Scenes are
     * deliberately never touched, preserving both their payload/name/source
     * pairing during every mask-selective Bank Load. Affiliates:
     * filesystem_loadSceneDirectory_tick() phase 31 (snapshot capture), phase
     * 61 (this read), and the final Bank HCNAMES writer.
     */
    if (scene_index >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return;

    /*
     * Diagnostic-only Session 056 drift witness. Compare the shared scratch
     * against the frozen child value immediately before publication so a
     * future reproduction identifies the affected child and the first byte of
     * each value. flags is reserved and remains zero; the H value layout is
     * documented in AutosaveTrace.h. This comparison never controls the
     * HCNAMES write: the snapshot below is always authoritative, so detected
     * drift cannot corrupt the published Scene row. Affiliate:
     * AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT.
     */
    if (memcmp(op_scene_display_name, op_bank_child_scene_display_name,
               sizeof(op_scene_display_name)) != 0) {
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_NAME_SCRATCH_DRIFT,
            0u,
            (uint32_t)scene_index |
            ((uint32_t)(uint8_t)op_bank_child_scene_display_name[0] << 8u) |
            ((uint32_t)(uint8_t)op_scene_display_name[0] << 16u));
    }

    filesystem_cacheResidentName(filesystem_residentSceneRow(scene_index),
                                 op_bank_child_scene_display_name);
    (void)filesystem_setResidentSource(filesystem_residentSceneRow(scene_index),
                                       FS_RESIDENT_SOURCE_INHERIT);
    filesystem_cacheResidentName(filesystem_residentKitRow(scene_index),
                                 filesystem_identityName(FS_IDENTITY_KIT_ROW));
    (void)filesystem_setResidentSource(filesystem_residentKitRow(scene_index),
                                       FS_RESIDENT_SOURCE_INHERIT);
    for (slot = 0u; slot < STORAGE_KIT_SLOT_COUNT; slot++) {
        uint16_t row = filesystem_residentInstrumentRow(scene_index, slot);
        filesystem_cacheResidentName(
            row,
            filesystem_identityName((uint8_t)(
                FS_IDENTITY_INSTRUMENT_ROW_0 + slot)));
        (void)filesystem_setResidentSource(row, FS_RESIDENT_SOURCE_INHERIT);
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
            if (!filesystem_cacheResidentRecord(op_item_offset, op_line_buf)) {
                op_close_status = FS_STATUS_ERROR;
                op_close_done = false;
                if (afatfs_fclose(op_file, on_file_closed))
                    op_phase = 3u;
                return;
            }
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
        else if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE) {
            /*
             * A Scene action replaces the Scene row and its complete embedded
             * Kit identity block in one publication transaction.
             *
             * What: overlays Scene rows, then the matching Kit plus six
             * Instrument rows. Why: Scene Load/Save commits all eight identity
             * rows, and deferring seven of them to a Menu boundary left the
             * durable register stale whenever that boundary was not crossed.
             * Inputs: op_scene_load_scene_mask, op_scene_display_name, and the
             * committed filesystem identity block. Output: every row changed
             * by the action is streamed together; unrelated rows are preserved.
             * The mask assignment is safe because this shared writer is entered
             * directly from a completed Scene operation, not filesystem_start().
             * Affiliate: filesystem_cacheCurrentResidentKitNames() and the
             * retired Menu write owner. See S056 section 11.2/A6.
             */
            filesystem_cacheCurrentResidentSceneNames();
            op_kit_load_scene_mask = op_scene_load_scene_mask;
            filesystem_cacheCurrentResidentKitNames();
        } else
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
                    (uint8_t)!filesystem_residentNameIsBlank(name),
                    fs_resident_source[op_item_offset], op_item_offset);
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
         * Reopen the just-closed register before acknowledging publication.
         * The row probe is witness-only: a mismatch must not turn already
         * committed musical payload into a filesystem error. Reuse the
         * existing op_bytes_done field for the logical probe row, and the
         * existing probe-match byte for the boolean result; no new SRAM is
         * allocated. Affiliates: the U trace record and section 11.3.
         */
        op_bytes_done = filesystem_residentPublishProbeRow();
        op_hcnames_probe_matches = 0u;
        op_phase = 10u;
        return;

    case 10: /* RETURN ROOT + REOPEN HCNAMES READ-ONLY FOR PROBE */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 11u;
        return;

    case 11: /* WAIT HCNAMES PROBE OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            /*
             * A failed read-back open is evidence, not a second write failure.
             * Record DONE with VERIFIED clear, then preserve the original
             * successful payload outcome.
             */
            filesystem_recordResidentNamePublish(1u);
            filesystem_clearResidentSourceDirtyFlags();
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_item_offset = 0u;
        op_line_len = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 12u;
        return;

    case 12: /* READ THROUGH THE GENERIC PUBLISH PROBE ROW */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t read_status = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            /*
             * A failed read-back is evidence, not a payload failure.
             *
             * What: abandon the probe and proceed to the normal close,
             * leaving op_hcnames_probe_matches at zero so the U witness
             * reports VERIFIED clear. Why: this phase runs after the
             * register was streamed, closed, and flushed, and its handle is
             * read-only, so nothing here can change the card. Promoting a
             * probe I/O error to FS_STATUS_ERROR would falsely fail a
             * committed operation, skip the source-dirty clear, and suppress
             * the U evidence this probe exists to provide. Inputs:
             * read_status only. Output: op_phase only; op_close_status stays
             * at the DONE value established before the probe. Affiliates:
             * filesystem_recordResidentNamePublish(), the failed-open policy
             * above, the mismatch branch below, and S056 section 12.2.
             */
            op_phase = 13u;
            return;
        }
        if (line_ready) {
            if (op_item_offset == op_bytes_done)
                op_hcnames_probe_matches =
                    filesystem_residentRowMatchesCache(
                        (uint16_t)op_bytes_done, op_line_buf);
            if (op_item_offset == op_bytes_done || eof) {
                op_phase = 13u;
                return;
            }
            if (op_item_offset < UINT16_MAX)
                op_item_offset++;
            op_line_len = 0u;
        }
        if (eof)
            op_phase = 13u;
        return;
    }

    case 13: /* START CLOSE OF GENERIC HCNAMES PROBE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 14u;
        return;

    case 14: /* WAIT PROBE CLOSE + PUBLISH WITNESS */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * The probe has exactly one exit. The payload and register write
         * completed before this phase was reachable, so every probe result
         * publishes the U witness, clears staged source dirtiness, and
         * completes the original operation DONE. A clear VERIFIED bit is
         * diagnostic evidence, never a payload failure. Affiliates:
         * filesystem_recordResidentNamePublish(),
         * filesystem_clearResidentSourceDirtyFlags(), and section 12.2.
         */
        filesystem_recordResidentNamePublish(1u);
        filesystem_clearResidentSourceDirtyFlags();
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
        else if (current_op == FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE) {
            /*
             * The missing-register bootstrap follows the same complete Scene
             * publication contract as the ordinary preserve-read path: a
             * Scene action owns its Scene, Kit, and six Instrument rows. The
             * cache is blank after a proven absence, so apply the same mask
             * bridge before the first HCNAMES write. Affiliate: section
             * 11.2/A6 and the dispatch above.
             */
            filesystem_cacheCurrentResidentSceneNames();
            op_kit_load_scene_mask = op_scene_load_scene_mask;
            filesystem_cacheCurrentResidentKitNames();
        } else
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

/*
 * Select one bounded AutoSave CRC interval without introducing timing waits.
 *
 * Input: bytes remaining in the fixed record. Output: 1..128 bytes when work
 * remains, otherwise zero. Why: every creation, recovery, validation, and
 * transformed-copy caller must share the one CPU-work cap so audio gets a main
 * loop opportunity between CRC intervals. This does not delay SD transfers or
 * pace unrelated filesystem operations. Affiliates: config.h's
 * AUTOSAVE_CRC_BYTES_PER_TICK and the two AutoSave state machines below.
 */
static uint16_t filesystem_autosaveCrcChunkBytes(uint32_t remaining)
{
    return (uint16_t)((remaining > AUTOSAVE_CRC_BYTES_PER_TICK)
        ? AUTOSAVE_CRC_BYTES_PER_TICK : remaining);
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
#if DEV_MODE_LOGGING
        /* Begin the RAM-only capsule before this ensure transaction owns FAT. */
        if (!filesystem_hcprmsCapsuleIsActive())
            filesystem_hcprmsCapsuleBegin();
#endif
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
            if (!filesystem_cacheResidentRecord(op_item_offset, op_line_buf)) {
                /* A malformed paired record is a read failure, but the open
                 * register still must close before this async transaction can
                 * report it. */
                op_close_status = FS_STATUS_ERROR;
                op_close_done = false;
                if (afatfs_fclose(op_file, on_file_closed))
                    op_phase = 3u;
                return;
            }
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
#if DEV_MODE_LOGGING
        /* Preserve the selected A/B scan coordinate independently of CRC scratch. */
        filesystem_hcprmsCapsuleSetTarget((uint8_t)op_stream_index);
#endif
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

    case 8: /* WAIT ROOT SCAN CLOSE, THEN PREPARE A MISSING RECORD'S CRC */
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
        /*
         * Prepare the complete initial-image CRC before CREATE can truncate a
         * missing target. Inputs: the proven-absent A/B selector and resident
         * identity cache. Output: one retained accumulator/cursor reused from
         * generic operation scratch; a reset during this preparation leaves
         * the filesystem unchanged. Why: creation must yield every bounded CRC
         * interval and must not widen the power-loss window around mutation.
         */
        op_file_version = (uint8_t)op_stream_index;
        op_stream_index = autosave_recordCrcBegin();
        op_bytes_done = 0u;
        op_phase = 9u;
        return;

    case 9: /* UPDATE ONE INITIAL-RECORD CRC INTERVAL BEFORE CREATE */
    {
        uint16_t crc_bytes;

        /*
         * Advance only the next bounded serialized interval and yield.
         *
         * Inputs: op_bytes_done as the absolute CRC cursor and op_stream_index
         * as the unfinalized accumulator. Output: cursor advances only by the
         * requested capped count; finalization occurs once after the full
         * record. Why: no single ensure pass may calculate all 34,768 bytes.
         */
        if (op_bytes_done < AUTOSAVE_RECORD_BYTES) {
            crc_bytes = filesystem_autosaveCrcChunkBytes(
                AUTOSAVE_RECORD_BYTES - op_bytes_done);
            op_stream_index = autosave_initialRecordCrcUpdate(
                op_stream_index, op_bytes_done, crc_bytes,
                filesystem_autosaveCreatedTargetGeneration(),
                bank_restoreBankSlot(), bank_displayName(),
                (const char (*)[AUTOSAVE_HCNAMES_ROW_BYTES])fs_list_cache_name);
            op_bytes_done += crc_bytes;
            return;
        }
        op_stream_index = autosave_recordCrcFinish(op_stream_index);
        op_phase = 10u;
        return;
    }

    case 10: /* OPEN THE CRC-PREPARED MISSING TARGET */
        if (!afatfs_chdir(NULL))
            return;
        op_file_ready = false;
        op_file = NULL;
        /*
         * op_stream_index now holds the finalized whole-record CRC, not the
         * A/B selector. Use op_file_version, captured before CRC work, so a
         * missing A creates .hcprms1 and a missing B creates .hcprms2. This
         * separation is required because every practical CRC is nonzero and
         * would otherwise select B, falsely completing ensure with only B.
         */
        if (!afatfs_fopen_lfn(
                filesystem_autosaveFilenameForIndex(op_file_version), "w",
                              AFATFS_MATCH_CASE_INSENSITIVE, NULL,
                              on_file_opened)) {
            return;
        }
        op_phase = 11u;
        return;

    case 11: /* WAIT NEW TARGET OPEN, THEN INITIALIZE ITS WRITE CURSOR */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        op_bytes_done = 0u;
        op_phase = 12u;
        return;

    case 12: /* STREAM ONE COMPLETE HEADER/MASK/NAME CHUNK AT A TIME */
    {
        uint16_t requested;
        uint32_t written;

        if (op_bytes_done >= AUTOSAVE_RECORD_BYTES) {
            op_close_done = false;
            if (afatfs_fclose(op_file, on_file_closed))
                op_phase = 13u;
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
        requested = (uint16_t)(op_write_line_len - op_write_line_offset);
        written = afatfs_fwrite(op_file, staging_buf + op_write_line_offset,
                                requested);
#if DEV_MODE_LOGGING
        /* Capture before advancing the chunk offset, including partial writes. */
        filesystem_hcprmsCapsuleNoteWrite(requested, written,
                                          op_write_line_offset);
#endif
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
                op_phase = 14u;
            return;
        }
        if (op_write_line_offset >= op_write_line_len) {
            op_bytes_done += op_write_line_len;
            op_write_line_len = 0u;
            op_write_line_offset = 0u;
        }
        return;
    }

    case 13: /* WAIT NEW TARGET CLOSE, THEN HANDLE THE OTHER TARGET */
        if (!op_close_done)
            return;
        op_file = NULL;
        /* Restore op_stream_index's ordinary A/B selector before advancing. */
        op_stream_index = op_file_version;
        filesystem_autosaveAdvanceTarget();
        return;

    case 14: /* WAIT FAILED PARTIAL-FILE CLOSE, THEN RELEASE BOOT */
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
/* Diagnostic-only phase observer for the runtime AutoSave drain. */
static uint8_t op_autosave_drain_last_phase = 0u;
static uint32_t op_autosave_drain_stall_ticks = 0u;

static void filesystem_autosaveParameterDrain_tick(void)
{
    /*
     * Observe and recover a true cooperative drain stall.
     *
     * What: records one PHASE_STALL after 30,000 unchanged polls and routes
     * the operation through the existing asynchronous writer error close-down.
     * Why: unlike the delete and Bank observers, this state machine previously
     * had no bounded escape from a soft SD stall. Inputs: op_phase and the
     * current streamed byte offset. Outputs: one trace record and ERROR
     * completion; no blocking close, remount, or new storage. Affiliates:
     * filesystem_pollPhaseStall(), filesystem_autosaveWriterFinishError(),
     * and op_autosave_writer.stream_offset.
     */
    if (filesystem_pollPhaseStall(op_phase,
                                  &op_autosave_drain_last_phase,
                                  &op_autosave_drain_stall_ticks,
                                  30000u)) {
        uint32_t value = (uint32_t)op_phase <<
                         AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT;

        value |= (op_autosave_writer.stream_offset >> 4u) <<
                 AUTOSAVE_TRACE_PHASE_STALL_EXTRA_SHIFT;
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_PHASE_STALL,
                             AUTOSAVE_TRACE_PHASE_STALL_SITE_DRAIN,
                             value);
        filesystem_autosaveWriterFinishError();
        return;
    }

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

    case 3: /* STREAM ONE BOUNDED CANDIDATE INTERVAL THROUGH VALIDATION */
    {
        uint16_t read_bytes;
        uint32_t n;

        /*
         * Limit every CRC-bearing candidate read to the shared work budget.
         *
         * Inputs: op_bytes_done is the next validation offset until the exact
         * record end. Output: no more than AUTOSAVE_CRC_BYTES_PER_TICK reaches
         * Autosave.c per filesystem pass; one later single-byte read detects a
         * trailing overlong record without adding CRC work. Why: validation is
         * a foreground CRC producer just like initial creation and copy.
         */
        read_bytes = (op_bytes_done < AUTOSAVE_RECORD_BYTES)
            ? filesystem_autosaveCrcChunkBytes(
                  AUTOSAVE_RECORD_BYTES - op_bytes_done)
            : 1u;
        n = afatfs_fread(op_file, staging_buf, read_bytes);

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
        op_remove_result = AFATFS_RESULT_OK;
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
        if (op_remove_result != AFATFS_RESULT_OK) {
            filesystem_autosaveWriterFinishError();
            return;
        }
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
        /*
         * Read only one shared CRC-work interval before transforming it.
         *
         * Input: the remaining winner stream. Output: the transformed-copy
         * checksum and subsequent write see at most the configured byte cap,
         * while AsyncFATFS retains its normal asynchronous transfer behavior.
         * Why: this bounds CPU CRC work without reviving rejected fixed-delay
         * filesystem pacing or allocating another stream buffer.
         */
        n = afatfs_fread(
            op_file, staging_buf, filesystem_autosaveCrcChunkBytes(
                AUTOSAVE_RECORD_BYTES - op_autosave_writer.stream_offset));
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
            if (!filesystem_cacheResidentRecord(op_item_offset, op_line_buf)) {
                /* Preserve the normal close-before-error contract even when
                 * an extended HCNAMES source token is malformed. */
                op_close_status = FS_STATUS_ERROR;
                op_close_done = false;
                if (afatfs_fclose(op_file, on_file_closed))
                    op_phase = 33u;
                return;
            }
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

    case 33: /* WAIT HCNAMES CLOSE, THEN PREPARE B'S RECOVERY CRC */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (op_close_status != FS_STATUS_DONE) {
            filesystem_autosaveWriterFinishError();
            return;
        }
        op_autosave_writer.recovery_target_index = 1u;
        op_autosave_writer.target_crc32c = autosave_recordCrcBegin();
        op_autosave_writer.stream_offset = 0u;
        op_phase = 39u;
        return;

    case 39: /* UPDATE ONE RECOVERY INITIAL-RECORD CRC INTERVAL BEFORE REMOVE */
    {
        uint16_t crc_bytes;

        /*
         * Prepare one complete recovery image before touching its target name.
         *
         * Inputs: the current B-then-A recovery selector, retained accumulator,
         * and absolute CRC cursor. Output: only a capped interval is
         * synthesized per pass; after finalization the cursor is reset solely
         * for later file writing. Why: a reset during CRC preparation leaves
         * both on-card candidates untouched, and pair recovery must never
         * combine unbounded CPU work with destructive target removal.
         */
        if (op_autosave_writer.stream_offset < AUTOSAVE_RECORD_BYTES) {
            crc_bytes = filesystem_autosaveCrcChunkBytes(
                AUTOSAVE_RECORD_BYTES - op_autosave_writer.stream_offset);
            op_autosave_writer.target_crc32c =
                autosave_initialRecordCrcUpdate(
                    op_autosave_writer.target_crc32c,
                    op_autosave_writer.stream_offset, crc_bytes,
                    filesystem_autosaveRecoveryGeneration(),
                    bank_restoreBankSlot(), bank_displayName(),
                    (const char (*)[AUTOSAVE_HCNAMES_ROW_BYTES])
                        fs_list_cache_name);
            op_autosave_writer.stream_offset += crc_bytes;
            return;
        }
        op_autosave_writer.target_crc32c = autosave_recordCrcFinish(
            op_autosave_writer.target_crc32c);
        op_autosave_writer.stream_offset = 0u;
        op_phase = 34u;
        return;
    }

    case 34: /* REMOVE CRC-PREPARED CORRUPT TARGET VARIANTS BEFORE CREATE */
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Neither record passed validation, so recovery may safely collapse
         * all file variants for this one A/B target before rebuilding it.
         * The same case-folded removal used by normal copy-forward guarantees
         * a repaired card returns with exactly one .hcprms1 and one .hcprms2.
         */
        op_remove_done = 0u;
        op_remove_result = AFATFS_RESULT_OK;
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
        if (op_remove_result != AFATFS_RESULT_OK) {
            filesystem_autosaveWriterFinishError();
            return;
        }
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

    case 35: /* WAIT RECOVERY TARGET OPEN AND INITIALIZE ITS WRITE CURSOR */
        if (!op_file_ready)
            return;
        if (!op_file) {
            filesystem_autosaveWriterFinishError();
            return;
        }
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
            /*
             * Begin A's CRC only after B is closed and durable.
             *
             * Inputs: the completed B recovery target and the fixed A
             * generation. Output: phase 39 performs bounded preparation while
             * B remains a complete card-resident peer. Why: pair recovery may
             * not leave both targets intentionally in destructive progress.
             */
            op_autosave_writer.target_crc32c = autosave_recordCrcBegin();
            op_autosave_writer.stream_offset = 0u;
            op_phase = 39u;
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
        op_rename_result = AFATFS_RESULT_OK;
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
        if (op_rename_result != AFATFS_RESULT_OK) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
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
    if (op_library_index_rebuild_kind == FS_NAME_CACHE_INSTRUMENT) {
        if (!filesystem_start(FS_INTERNAL_OP_CREATE_BOOT_INDEX,
                              FS_FILE_SETTINGS, 0u,
                              filesystem_libraryIndexRebuildWriteComplete)) {
            filesystem_makeNamedErrorCode("Idx", 3u);
            filesystem_completeLibraryIndexRebuild(FS_STATUS_ERROR);
        }
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
                /* Stage paired provenance before Menu's deferred HCNAMES
                 * flush rereads the old register: this root Kit is the direct
                 * source and its member Instrument rows inherit from it. */
                for (scene_index = 0u;
                     scene_index < SCENE_COUNT && scene_index < 16u;
                     scene_index++) {
                    uint8_t source_slot;
                    if ((op_kit_load_scene_mask &
                         (uint16_t)(1u << scene_index)) == 0u) {
                        continue;
                    }
                    (void)filesystem_setResidentSource(
                        filesystem_residentKitRow(scene_index), op_slot);
                    for (source_slot = 0u;
                         source_slot < STORAGE_KIT_SLOT_COUNT;
                         source_slot++) {
                        (void)filesystem_setResidentSource(
                            filesystem_residentInstrumentRow(scene_index,
                                                             source_slot),
                            FS_RESIDENT_SOURCE_INHERIT);
                    }
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
        /*
         * Normal Kit Load owns the Kit row and six Instrument rows for every
         * destination Scene. Their sources were staged with the validated
         * payload; hand the original callback to the filesystem-owned HCNAMES
         * publisher before reporting completion. Morph loads intentionally
         * remain endpoint-only and must not publish a morph filename as Kit
         * identity. See S056_HCNAMES_FOLLOW_UP.md section 11.2/A2.
         */
        if (op_close_status == FS_STATUS_DONE &&
            current_op == FS_INTERNAL_OP_LOAD_KIT) {
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_KIT);
            return;
        }
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
                    /* A root Scene supplies this whole child hierarchy;
                     * Bank-local Scene loads use the enclosing Bank instead. */
                    for (uint8_t source_scene = 0u;
                         source_scene < STORAGE_BANK_SCENE_MAX_SLOTS;
                         source_scene++) {
                        if ((op_scene_load_scene_mask &
                             (uint16_t)(1u << source_scene)) == 0u) {
                            continue;
                        }
                        (void)filesystem_setResidentSource(
                            filesystem_residentSceneRow(source_scene),
                            current_op == FS_INTERNAL_OP_LOAD_SCENE
                                ? op_slot : FS_RESIDENT_SOURCE_INHERIT);
                        (void)filesystem_setResidentSource(
                            filesystem_residentKitRow(source_scene),
                            FS_RESIDENT_SOURCE_INHERIT);
                        for (identity_slot = 0u;
                             identity_slot < STORAGE_KIT_SLOT_COUNT;
                             identity_slot++) {
                            (void)filesystem_setResidentSource(
                                filesystem_residentInstrumentRow(
                                    source_scene, identity_slot),
                                FS_RESIDENT_SOURCE_INHERIT);
                        }
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
            /*
             * Timestamp one embedded Instrument-entry request.
             *
             * What: records the Bank-local Scene cursor and voice slot before
             * the asynchronous open. Why: the retained bootlog suffix keeps
             * only the final substep, while consecutive N records expose
             * per-Instrument timing from their existing tick16 fields. Inputs:
             * op_bank_child_cursor/op_instrument_slot. Output: one trace record
             * and no state change. Affiliate: the existing boot-detail marker.
             */
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_INSTRUMENT_ENTRY,
                AUTOSAVE_TRACE_INSTRUMENT_ENTRY_PHASE_REQUEST,
                ((uint32_t)op_bank_child_cursor <<
                 AUTOSAVE_TRACE_INSTRUMENT_ENTRY_SCENE_SHIFT) |
                ((uint32_t)op_instrument_slot <<
                 AUTOSAVE_TRACE_INSTRUMENT_ENTRY_SLOT_SHIFT));
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
            /*
             * The shared handoff now also publishes the embedded Kit family;
             * its callback remains parked until the complete HCNAMES
             * read/write/read-back sequence is done. See section 11.2/A1/A6.
             */
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE);
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
            if (!filesystem_cacheResidentRecord(op_item_offset, op_line_buf)) {
                /* The Bank preload owns an open HCNAMES handle here; close it
                 * before surfacing a malformed name/source record. */
                op_close_status = FS_STATUS_ERROR;
                op_close_done = false;
                if (afatfs_fclose(op_file, on_file_closed))
                    op_phase = 82u;
                return;
            }
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
        /* The root Bank directory selected by this completed load is the
         * direct top-level fallback for every child that inherits upward. */
        (void)filesystem_setResidentSource(0u, op_slot);
            /*
             * No requested child exists in this Bank. Preserve the existing
             * resident Scene availability rather than clearing it: the caller
             * asked for a selective Bank identity/load operation, not a reset
             * of every unselected playable Scene. HCNAMES rows remain the
             * preloaded register values for the same reason.
            */
            if (!bank_setScenePresentMask(bank_scenePresentMask())) {
                /*
                 * Guarantee a fresh present-mask capture for an empty Bank.
                 *
                 * Inputs: the preserved resident mask and a setter result of
                 * zero, meaning the normalized value was unchanged. Output:
                 * the existing two canonical mask bits are marked for the
                 * next drain. Why: this branch intentionally preserves the
                 * mask, so change-aware setters cannot otherwise refresh a
                 * record whose previous mask bytes are stale or zero.
                 * Affiliate: autosave_markBankFieldDirty().
                 */
                autosave_markBankFieldDirty(
                    AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);
            }
            /*
             * Witness the empty-Bank presence boundary in the retained trace.
             *
             * Inputs: the deliberately preserved resident mask and the zero
             * effective load mask. Output: one B record with the drain-site
             * flag clear; value32 packs both masks for comparison with the
             * later live-byte capture. Why: an empty Bank's setter is a
             * deliberate no-op, so this proves which value the commit kept.
             * Affiliate: autosave_getLivePayloadByte().
             */
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_BANK_PRESENT, 0u,
                ((uint32_t)bank_scenePresentMask() <<
                 AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT) |
                    op_bank_scene_load_mask);
            bank_selectActiveSceneForEditMask(op_bank_active_scene);
            bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
            bank_setRestoreBankSlot(op_slot);
            /*
             * Persist the newly selected boot-restore Bank after a valid
             * empty-Bank identity load.
             *
             * Inputs: the committed restore slot above. Output: the existing
             * debounced settings writer re-serializes active_bank later; this
             * call opens no file. Why: an empty Bank is still the new boot
             * selection authority and must not leave a stale settings value.
             * Affiliates: filesystem_settingsWriterSchedule_tick() and
             * filesystem_nextSettingsLine().
             */
            filesystem_markSettingsDirty();
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
         * The completed root Bank payload is the direct top-level source for
         * its selected Bank-local children.  This mirrors the empty-Bank
         * completion above: both paths publish row zero through the same
         * HCNAMES close gate, so boot's settings-selected Bank cannot retain
         * an obsolete UNKNOWN token after a normal child load.
         */
        (void)filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot);
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
        if (!bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                                 op_bank_scene_load_mask))) {
            /*
             * Guarantee a fresh present-mask capture when the Bank Load union
             * is a no-op against the already-resident mask.
             *
             * Inputs: the effective selected-child union and a setter result
             * of zero. Output: the existing two-byte field is marked without
             * allocating state or changing the resident mask. Why: the
             * change-aware setter correctly suppresses equal writes, but a
             * later AutoSave reader still needs the committed value refreshed
             * after this successful load boundary. Affiliate:
             * autosave_markBankFieldDirty().
             */
            autosave_markBankFieldDirty(
                AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);
        }
        /*
         * Witness the post-commit Bank presence boundary in the retained
         * trace.
         *
         * Inputs: the merged resident mask and effective selected-child mask.
         * Output: one B record with the drain-site flag clear; value32 packs
         * the resident mask in bits 16..31 and the effective load mask in
         * bits 0..15. Why: D records identify marked offsets but cannot prove
         * the live mask value seen by the later drain. Affiliate: the drain
         * witness in autosave_getLivePayloadByte().
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_BANK_PRESENT, 0u,
            ((uint32_t)bank_scenePresentMask() <<
             AUTOSAVE_TRACE_BANK_PRESENT_MASK_SHIFT) |
                op_bank_scene_load_mask);
        bank_selectActiveSceneForEditMask(op_bank_active_scene);
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
        bank_setRestoreBankSlot(op_slot);
        /*
         * Persist the newly selected boot-restore Bank after a complete
         * non-empty Bank Load.
         *
         * Inputs: the committed restore slot above. Output: dirty/revision/
         * deadline state only; no file is opened here. Why: the Bank Load
         * changed the boot-selection authority, so a reboot must select this
         * Bank rather than the stale settings.cfg slot. Affiliates:
         * filesystem_settingsWriterSchedule_tick() and
         * filesystem_nextSettingsLine().
         */
        filesystem_markSettingsDirty();
        bank_setHasResidentBank(1u);
        scene_selectActive(op_bank_active_scene);
        /*
         * Realign Pattern playback and the STEP view with the Scene just
         * committed as active.
         *
         * Why: scene_selectActive() is deliberately "identity, never data" and
         * changes only SceneData's active index. Pattern is the one Scene-owned
         * payload that playback and the STEP UI do NOT address through
         * scene_getActiveIndex(): the sequencer reads seq_activePattern and the
         * LED/button layer reads menu_shownPattern. Both are BSS-zero at boot
         * and were previously assigned only by a front-panel PERF press, so a
         * Bank whose manifest selects any Scene other than 0 left them
         * disagreeing with SceneData for the whole session. A later Scene Load
         * then wrote its Pattern into the committed active Scene (correctly)
         * while playback and the STEP LEDs kept reading Scene 0, which
         * presented as "Scene Load never loads the pattern". Every other Scene
         * payload resolved through scene_getActiveIndex() and looked fine,
         * which is why only Pattern appeared broken.
         *
         * Inputs: the Bank manifest's committed active Scene. Outputs: the
         * sequencer's active/pending Pattern indices and Menu's viewed Pattern
         * follow it. Both callees validate the index themselves and are pure
         * state realignment — no SD access, no allocation, no payload change,
         * and no MIDI emission (the reason seq_alignActivePatternToScene()
         * exists instead of seq_selectActivePattern(), which would send a
         * program change and force note-offs during pre-audio boot).
         *
         * Repaint is intentionally not triggered here: filesystem.c must not
         * drive LCD/LED work. Boot repaints via menu_start()/menu_repaintAll()
         * and runtime Bank Load repaints through menu_startSoundApply() in
         * Menu's PRESET_OP_BANK_LOAD completion.
         *
         * Affiliates: seq_alignActivePatternToScene(), menu_setShownPattern(),
         * menu_perfModeSceneButtonPressed() (the equivalent front-panel
         * pairing), and SCENE_LOAD_PAT_RESTORE.md.
         */
        seq_alignActivePatternToScene(op_bank_active_scene);
        menu_setShownPattern(op_bank_active_scene);
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
        /*
         * Start each Bank-child name lifetime empty as well. If a future
         * control-flow error reaches phase 61 without a successful phase-31
         * capture, this prevents silently reusing the previous child's name;
         * the existing phase-29/31 error path still rejects the child before
         * publication. Affiliate: op_bank_child_scene_display_name's phase-31
         * write and phase-61 HCNAMES read.
         */
        op_bank_child_scene_display_name[0] = '\0';
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
        /*
         * Freeze the validated Bank-local Scene display name before entering
         * the shared multi-phase Scene payload reader. Inputs: the
         * op_bank_child_cursor-selected name that phase 29 found and the
         * successful close status above. Output: an independent nine-byte
         * value for phase 61 HCNAMES publication, immune to any later reuse of
         * op_scene_display_name by settings/Kit/Instrument/Pattern/Effect
         * work. Why: the Scene directory name is not serialized in sceneset.scg
         * and must remain paired with the committed resident Scene. Affiliate:
         * op_bank_child_scene_display_name and
         * filesystem_cacheCurrentBankSceneNameBlock().
         */
        memcpy(op_bank_child_scene_display_name, op_scene_display_name,
               sizeof(op_bank_child_scene_display_name));
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
                        fs_list_cache_name[op_item_offset]),
                    fs_resident_source[op_item_offset], op_item_offset);
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

    case 86: /* CLOSE HCNAMES, THEN PROVE THE BANK REGISTER */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Bank Load's row-0 identity is staged unconditionally, so reopen the
         * visible register and read exactly that row before clearing its dirty
         * source bit. The result is witness-only: payload commit is already
         * valid and a mismatch must not turn it into a failed Bank Load. The
         * existing op_hcnames_probe_matches byte carries the result through
         * the later Preset K callback; no new persistent SRAM is allocated.
         * See S056_HCNAMES_FOLLOW_UP.md section 11.3/B3.
         */
        op_hcnames_probe_matches = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 93u;
        return;

    case 93: /* OPEN BANK HCNAMES READ-BACK */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 94u;
        return;

    case 94: /* WAIT BANK HCNAMES READ-BACK OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            /* No handle is a failed proof, not a failed payload operation. */
            filesystem_clearResidentSourceDirtyFlags();
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_line_len = 0u;
        op_phase = 95u;
        return;

    case 95: /* READ BANK ROW 0 AND START CLOSE */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t read_status = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            /*
             * A failed Bank row-0 read-back is evidence, not a Bank-load
             * failure. The register was already streamed, closed, and
             * flushed before this read-only probe ran, so leave the probe
             * boolean clear and continue through the normal close. This
             * preserves the committed payload, source-dirty cleanup, and the
             * later K callback witness. Inputs: read_status only. Output:
             * op_phase only; op_close_status remains DONE. Affiliates:
             * filesystem_lastHcnamesVerified(), the mismatch branch below,
             * and S056_HCNAMES_FOLLOW_UP.md section 12.2.
             */
            op_phase = 96u;
            return;
        }
        if (line_ready) {
            op_hcnames_probe_matches =
                filesystem_residentRowMatchesCache(0u, op_line_buf);
            op_phase = 96u;
            return;
        }
        if (eof)
            op_phase = 96u;
        return;
    }

    case 96: /* START CLOSE BANK HCNAMES READ-BACK */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 97u;
        return;

    case 97: /* WAIT CLOSE + COMPLETE BANK LOAD */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Bank Load's probe has one unconditional completion path. The
         * payload and HCNAMES write were complete before this diagnostic
         * read, so a clear VERIFIED bit is retained for K but cannot fail the
         * Bank operation or skip source cleanup. Affiliates:
         * filesystem_lastHcnamesVerified(), on_bank_load_complete(), and
         * S056_HCNAMES_FOLLOW_UP.md section 12.2.
         */
        filesystem_clearResidentSourceDirtyFlags();
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
                    /* The accepted root Instrument row is a direct source
                     * for the destination only.  This precedes the deferred
                     * HCNAMES reread so its high-bit staging flag protects it
                     * from the older card record until publication succeeds. */
                    (void)filesystem_setResidentSource(
                        filesystem_residentInstrumentRow(
                            op_instrument_load_destination_scene,
                            op_instrument_load_destination_slot),
                        FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT);
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
        /*
         * A successful normal pool Instrument Load owns one HCNAMES voice row.
         * Point the shared writer at the captured destination coordinates and
         * keep hidden `.hctmp` restores endpoint-only, so their temporary
         * source can never replace visible identity. The source word and
         * identity name were staged during validation. See S056 section
         * 11.2/A4.
         */
        if (op_close_status == FS_STATUS_DONE &&
            !op_instrument_load_temporary) {
            op_kit_load_scene_mask =
                (uint16_t)(1u << op_instrument_load_destination_scene);
            op_slot = op_instrument_load_destination_slot;
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT);
            return;
        }
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
        op_remove_result = AFATFS_RESULT_OK;
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
        if (op_remove_result != AFATFS_RESULT_OK) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
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
            /* The target open failed before any content fingerprint exists. */
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED,
                (uint32_t)op_instrument_save_source_slot <<
                    AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_write_line_index = 0u;
        op_write_line_len = 0u;
        op_write_line_offset = 0u;
        /* Start the raw-byte fingerprint at the first serialized line. */
        op_instrument_save_content_crc = autosave_recordCrcBegin();
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
        /*
         * Record successful Instrument materialization and its content hash.
         *
         * What: publishes the top sixteen bits of the completed streamed
         * CRC32C in the existing O/CREATE_RESULT value. Why: directory
         * identity alone cannot prove an in-place overwrite changed bytes.
         * Inputs: all bytes accepted by filesystem_writeTextLine(). Output:
         * one lifecycle record; the normal close path remains unchanged.
         * This is deliberately the raw running accumulator, not the
         * autosave_recordCrcFinish()-complemented form used by the fixed
         * AutoSave record elsewhere in this file: it is a diagnostic
         * fingerprint compared only against another value produced the same
         * way (a later Save/Load of the same slot), never against a
         * "finished" CRC32C. Affiliates: op_instrument_save_content_crc and
         * the later HCNAMES SOURCE_STAGED handoff.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT <<
             AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
            AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT,
            (op_instrument_save_content_crc & 0xffff0000u) |
            ((uint32_t)op_instrument_save_source_slot <<
             AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT));
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
            /*
             * Stage direct Instrument provenance before the HCNAMES merge.
             *
             * What: marks the saved resident voice's row with the '@' source
             * token and prepares the typed index rebuild that follows the
             * durable register update. Why: the old path updated only the
             * ephemeral identity/cache and never requested an Instrument
             * HCNAMES publication. Inputs: captured resident Scene/slot and
             * saved Instrument type. Outputs: one dirty source cell, one
             * source witness, and a deferred index-chain request; no new
             * SRAM. Affiliates: Instrument Load staging,
             * filesystem_cacheCurrentResidentInstrumentNames(), and the
             * FS_NAME_CACHE_INSTRUMENT rebuild branch.
             */
            (void)filesystem_setResidentSource(
                filesystem_residentInstrumentRow(
                    op_instrument_save_source_scene,
                    op_instrument_save_source_slot),
                FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT);
            op_kit_load_scene_mask = (uint16_t)(1u <<
                                                op_instrument_save_source_scene);
            op_slot = op_instrument_save_source_slot;
            /* op_instrument_index_type is set unconditionally just below,
             * for both the morph and non-morph path; no need to set it here
             * too. */
            op_library_index_rebuild_kind = FS_NAME_CACHE_INSTRUMENT;
            op_library_index_rebuild_pending = 1u;
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT,
                (uint32_t)op_instrument_save_source_slot <<
                    AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT,
                (uint32_t)op_instrument_save_source_slot <<
                    AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
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
        if (!morph_save) {
            /*
             * Normal Instrument Save owns the direct voice identity row. The
             * shared handoff preserves the parked callback through HCNAMES and
             * then the already-armed typed-index rebuild. Morph Save remains
             * endpoint-only by policy: its file name is not resident identity.
             * See S056_HCNAMES_FOLLOW_UP.md section 11.2/A5.
             */
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_INSTRUMENT);
        } else {
            /*
             * Morph Save deliberately skips identity publication. It writes a
             * second parameter image for an existing voice, so publishing its
             * morph filename would overwrite the normal HCNAMES name/source.
             */
            filesystem_bootLoggingArm("INSINDEX");
            current_op = FS_INTERNAL_OP_CREATE_BOOT_INDEX;
            op_phase = 0u;
        }
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
    uint32_t written;

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
    written = afatfs_fwrite(op_file,
                            (const uint8_t *)op_write_line_buf +
                                op_write_line_offset,
                            op_write_line_len - op_write_line_offset);
    /*
     * Fingerprint only bytes accepted by a normal root Instrument Save.
     *
     * What: folds the exact streamed write interval into the persistent
     * Instrument-save accumulator. Why: a line may be split across many
     * foreground polls, so the complete file cannot be fingerprinted from a
     * single buffer at case 18. Inputs: the returned byte count and current
     * line offset. Output: one running CRC32C; all other text writers are
     * untouched. Affiliates: Autosave.h's raw-byte helper and the O
     * CREATE_RESULT record in filesystem_saveInstrument_tick().
     */
    if (current_op == FS_INTERNAL_OP_SAVE_INSTRUMENT &&
        !op_instrument_save_temporary) {
        uint32_t i;

        for (i = 0u; i < written; i++) {
            op_instrument_save_content_crc = autosave_crc32cByteUpdate(
                op_instrument_save_content_crc,
                (uint8_t)op_write_line_buf[op_write_line_offset + i]);
        }
    }
    op_write_line_offset = (uint16_t)(op_write_line_offset + written);
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
     * values, plus retired HCNAMES provenance, are absent even though they
     * still have ParameterArray ids or legacy file keys.
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

static uint8_t filesystem_objectMatchesSlot(
        const afatfsObjectInfo_t *object,
        uint16_t slot,
        uint8_t allow_short_alias)
{
    uint16_t parsed_slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN + 1u];

    /*
     * Decide whether one child directory is eligible for slot replacement.
     *
     * Inputs: asyncfatfs object info from the current parent directory, the
     * requested slot and whether old compact 8.3 aliases may match by their
     * first three short-name digits. Output: nonzero for any immediate object
     * whose root-library name parses as the requested slot; the caller then
     * rejects files as a same-slot conflict.
     *
     * Bank-local two-digit children are deliberately outside this resolver;
     * Bank Save replaces the root Bank tree as one exact object.
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
    if (!object || object->id.kind == AFATFS_OBJECT_NONE)
        return 0u;
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

/*
 * Detect a cooperative state-machine phase that stops advancing.
 *
 * What: compares a caller-owned phase with its previous value and returns one
 * exactly once when the unchanged phase crosses threshold_ticks. Why:
 * foreground-pumped SD work must remain asynchronous, while several
 * diagnostic sites need the same edge-triggered stall observation. Inputs:
 * phase, caller-owned last_phase/stall_ticks cells, and a site threshold.
 * Output: one on the crossing poll, zero otherwise; a phase change resets and
 * rearms the counter. This helper performs no logging or recovery because the
 * correct response differs between delete, Bank Save, and AutoSave drain.
 * Affiliates: filesystem_deleteSlotDirectory_tick(),
 * filesystem_saveBankDirectory_tick(), and
 * filesystem_autosaveParameterDrain_tick().
 */
static uint8_t filesystem_pollPhaseStall(uint8_t phase,
                                         uint8_t *last_phase,
                                         uint32_t *stall_ticks,
                                         uint32_t threshold_ticks)
{
    if (phase != *last_phase) {
        *last_phase = phase;
        *stall_ticks = 0u;
        return 0u;
    }
    (*stall_ticks)++;
    return (uint8_t)(*stall_ticks == threshold_ticks + 1u);
}

static void filesystem_deleteSlotDirectoryStart(uint16_t slot,
                                                uint8_t allow_short_alias)
{
    /*
     * Start same-slot directory cleanup in the current parent directory.
     *
     * Inputs: caller must already be chdir'd into the parent directory that
     * owns the numbered children; slot is the exact 000..999 root slot or
     * Bank-local 00..15 child number to replace. allow_short_alias controls
     * whether the scan may match legacy compact 8.3 aliases by short-name
     * digits. Output: the delete-slot state machine proves zero or one matching
     * directory, then deletes only that captured object.
     *
     * Safety: this function never receives a path to delete. It only deletes
     * children discovered by filesystem_objectMatchesSlot(), so Scene
     * Save can opt out of short-alias matching and avoid deleting anything
     * except visibly numbered Scene folders for the target slot.
     */
    memset(&op_delete_slot_target, 0, sizeof(op_delete_slot_target));
    op_delete_slot_target.id.kind = AFATFS_OBJECT_NONE;
    op_delete_slot_dir = NULL;
    op_delete_slot_number = slot;
    op_delete_slot_allow_short_alias = allow_short_alias;
    op_delete_slot_match_count = 0u;
    op_delete_slot_scan_error = 0u;
    op_delete_slot_timeout_observed = 0u;
    op_delete_slot_error_reason = FS_DELETE_SLOT_REASON_NONE;
    op_delete_slot_error_detail = 0u;
    op_delete_slot_error_site = 0u;
    op_delete_slot_phase = FS_DELETE_SLOT_OPEN_SCAN;
}

static void filesystem_deleteKitSlotDirectoryStart(void)
{
    /*
     * Kit Save cleanup allows short-alias fallback.
     *
     * Older Kit cards may contain same-slot directories whose visible name is
     * only an 8.3 alias such as 001SLA~1. Matching those by their first three
     * digits is acceptable inside /Kit because the product tree contains only
     * Kit directories and member files.
     */
    filesystem_deleteSlotDirectoryStart(op_slot, 1u);
}

static void filesystem_deleteSceneSlotDirectoryStart(void)
{
    /*
     * Scene Save cleanup forbids short-alias fallback.
     *
     * The current parent must be /Scene. Only children whose visible display
     * name parses as "NNN Name" for op_slot are recursively deleted. This keeps
     * replacement scoped to the requested Scene slot no matter how many nested
     * directories exist inside other Scene folders.
     */
    filesystem_deleteSlotDirectoryStart(op_slot, 0u);
}

static uint32_t op_delete_slot_stall_ticks = 0u;
static uint8_t op_delete_slot_last_phase = 0u;

static fs_status_t filesystem_deleteSlotDirectory_tick(void)
{
    if (filesystem_pollPhaseStall((uint8_t)op_delete_slot_phase,
                                  &op_delete_slot_last_phase,
                                  &op_delete_slot_stall_ticks,
                                  50000u) &&
        !op_delete_slot_timeout_observed) {
        uint8_t subphase = afatfs_getDeleteTreePhase();
        uint8_t flags = AUTOSAVE_TRACE_PHASE_STALL_SITE_DELETE_SLOT;
        uint32_t value = (uint32_t)op_delete_slot_phase <<
                         AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT;

        value |= (uint32_t)op_delete_slot_number <<
                 AUTOSAVE_TRACE_PHASE_STALL_SLOT_SHIFT;
        if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH && subphase != 0xFF) {
            flags |= AUTOSAVE_TRACE_PHASE_STALL_FLAG_IN_NATIVE_DELETE;
            value |= (uint32_t)subphase <<
                     AUTOSAVE_TRACE_PHASE_STALL_EXTRA_SHIFT;
            filesystem_makeNamedErrorCode("TDel", subphase);
        } else {
            filesystem_makeNamedErrorCode("TOut", (uint8_t)op_delete_slot_phase);
        }
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_PHASE_STALL, flags, value);
        /* Observation is not cancellation: native delete has no abort API,
         * so retain ownership until its callback releases the handle. */
        op_delete_slot_timeout_observed = 1u;
        if (op_delete_slot_phase == FS_DELETE_SLOT_DELETE_MATCH ||
            op_delete_slot_phase == FS_DELETE_SLOT_WAIT_SCAN ||
            op_delete_slot_phase == FS_DELETE_SLOT_WAIT_CLOSE_SCAN)
            return FS_STATUS_BUSY;
        /*
         * Tag this abandonment before branching, so both the immediate
         * (OPEN_SCAN, no dir yet) and the deferred (SCAN_NEXT/CLOSE_SCAN,
         * via WAIT_CLOSE_SCAN) paths to FS_DELETE_SLOT_ERROR carry the same
         * correct reason instead of leaving the deferred path to fall
         * through to WAIT_CLOSE_SCAN's untagged-backstop label.
         */
        op_delete_slot_error_reason = FS_DELETE_SLOT_REASON_STALL_ABANDONED;
        op_delete_slot_scan_error = 1u;
        if (op_delete_slot_dir)
            op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
        else
            op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
    }

    /* A timed-out open/close remains the owner of its native handle. Once the
     * callback publishes that handle, close it before returning failure; never
     * release the facade while asyncfatfs may still be using the slot. */
    if (op_delete_slot_timeout_observed &&
        op_delete_slot_phase == FS_DELETE_SLOT_WAIT_SCAN) {
        if (!op_file_ready)
            return FS_STATUS_BUSY;
        if (!op_file) {
            op_delete_slot_error_reason = FS_DELETE_SLOT_REASON_DIR_OPEN_FAILED;
            op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
            return FS_STATUS_ERROR;
        }
        op_delete_slot_dir = op_file;
        op_file = NULL;
        op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
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
                /*
                 * The "." open resolved to a NULL handle.
                 *
                 * What: tags this specific failure branch so the caller's
                 * O/DELETE_RESULT trace record (and the universal
                 * E/OPERATION_ERROR backstop in filesystem_complete()) can
                 * distinguish it from every other delete-slot failure mode.
                 * Why: this is the branch that produced an untagged
                 * (reason=NONE) DELETE_RESULT record on Session 054's first
                 * post-fix retest -- opening "." is a synchronous snapshot
                 * of afatfs.currentDirectory
                 * (afatfs_createFileInternal()'s strcmp(name,".")==0
                 * branch), not a directory scan, so its failure points at
                 * open-handle-pool pressure or an invalid currentDirectory
                 * rather than anything about the target slot's own entry.
                 * Inputs: none beyond the NULL callback result already
                 * observed. Outputs: op_delete_slot_error_reason set; no
                 * other state change. Affiliates:
                 * FS_DELETE_SLOT_REASON_DIR_OPEN_FAILED,
                 * afatfs_createFileInternal(), afatfs_allocateFileHandle().
                 */
                op_delete_slot_error_reason =
                    FS_DELETE_SLOT_REASON_DIR_OPEN_FAILED;
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
            if (op_delete_slot_scan_error) {
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            afatfsOperationStatus_e st =
                afatfs_findNextObject(op_delete_slot_dir,
                                      &op_object_finder,
                                      &op_object);
            if (st == AFATFS_OPERATION_IN_PROGRESS)
                return FS_STATUS_BUSY;
            if (st == AFATFS_OPERATION_FAILURE) {
                op_delete_slot_scan_error = 1u;
                op_delete_slot_error_reason = FS_DELETE_SLOT_REASON_SCAN_IO;
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            if (op_object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            if (filesystem_objectMatchesSlot(
                    &op_object,
                    op_delete_slot_number,
                    op_delete_slot_allow_short_alias)) {
                /*
                 * Distinguish which eligibility check rejected this match.
                 * Kept as separate branches (rather than the equivalent
                 * single ||-chain) purely so op_delete_slot_error_reason can
                 * name the exact rejected condition for the trace record;
                 * behavior/evaluation order is unchanged.
                 */
                if (op_object.lfnMalformed) {
                    op_delete_slot_scan_error = 1u;
                    op_delete_slot_error_reason =
                        FS_DELETE_SLOT_REASON_MALFORMED_LFN;
                } else if (op_object.id.kind != AFATFS_OBJECT_DIRECTORY) {
                    op_delete_slot_scan_error = 1u;
                    op_delete_slot_error_reason =
                        FS_DELETE_SLOT_REASON_WRONG_KIND;
                } else if (op_delete_slot_match_count != 0u) {
                    op_delete_slot_scan_error = 1u;
                    op_delete_slot_error_reason =
                        FS_DELETE_SLOT_REASON_DUPLICATE;
                } else {
                    op_delete_slot_target = op_object;
                }
                if (op_delete_slot_match_count != 0xffu)
                    op_delete_slot_match_count++;
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
            if (op_delete_slot_scan_error ||
                op_delete_slot_match_count > 1u) {
                if (op_delete_slot_error_reason == FS_DELETE_SLOT_REASON_NONE) {
                    /* scan_error was clear but match_count > 1 anyway: the
                     * per-match branch above should always have already
                     * tagged DUPLICATE before match_count could exceed 1, so
                     * reaching here untagged is itself diagnostic. */
                    op_delete_slot_error_reason =
                        FS_DELETE_SLOT_REASON_MATCH_COUNT_BACKSTOP;
                }
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            if (op_delete_slot_match_count == 0u) {
                op_delete_slot_phase = FS_DELETE_SLOT_DONE;
                return FS_STATUS_DONE;
            }
            /*
             * Delete the complete object captured by the parent scan.
             *
             * Input: op_delete_slot_target contains the validated display,
             * SFN, LFN-fragment pointers, and first cluster from that one scan.
             * Output: native deletion follows that copied physical identity;
             * it performs no display-name or short-alias re-resolution. This
             * is essential when a card already has duplicate visible names.
             */
            op_delete_tree_done = false;
            if (!afatfs_deleteTree(&op_delete_slot_target,
                                   on_delete_tree_complete)) {
                op_delete_slot_error_reason =
                    FS_DELETE_SLOT_REASON_DELETE_REJECTED;
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_phase = FS_DELETE_SLOT_DELETE_MATCH;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_DELETE_MATCH:
            if (!op_delete_tree_done)
                return FS_STATUS_BUSY;
            if (op_delete_tree_result != AFATFS_RESULT_OK) {
                op_delete_slot_error_reason =
                    FS_DELETE_SLOT_REASON_DELETE_RESULT;
                op_delete_slot_error_detail = (uint8_t)op_delete_tree_result;
                /*
                 * Capture asyncfatfs's own exact internal failure site now,
                 * before any later delete request resets it. See the doc
                 * comment on op_delete_slot_error_site and on
                 * afatfsDeleteTree_t::failureSite in asyncfatfs.c for why
                 * the bare afatfsResultCode_t above is not specific enough
                 * on its own -- ~17 different checks in
                 * afatfs_deleteTreeContinue() can all return
                 * AFATFS_RESULT_UNSUPPORTED_LAYOUT.
                 */
                op_delete_slot_error_site = afatfs_getDeleteTreeFailureSite();
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_phase = FS_DELETE_SLOT_DONE;
            return FS_STATUS_DONE;
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
         * Prove zero/one directory for this numbered Kit slot, then delete
         * only the captured exact object before creating the fresh one.
         *
         * A card containing duplicate same-slot folders such as "003 RedSnap"
         * and "003 Slak" is rejected before deletion or creation; the shared
         * name cache is not used to rediscover a target.
        */
        filesystem_deleteKitSlotDirectoryStart();
        op_phase = 5u;
        return;

    case 5:
        delete_status = filesystem_deleteSlotDirectory_tick();
        if (delete_status == FS_STATUS_BUSY)
            return;
        /*
         * Record the one resolved delete result after BUSY clears.
         *
         * A failed result additionally packs op_delete_slot_error_reason and
         * (when the reason is a non-OK native-delete result)
         * op_delete_slot_error_detail's raw afatfsResultCode_t, so this one
         * record can distinguish a scan I/O error, a malformed LFN, a wrong
         * object kind, a duplicate match, the match-count backstop, a
         * rejected native-delete start, or a genuine non-OK delete result —
         * five to seven architecturally different problems that previously
         * all looked identical as a bare FAILED bit.
         */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (uint8_t)((AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT <<
                       AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                      AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT |
                      (delete_status == FS_STATUS_ERROR
                           ? AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED : 0u)),
            ((uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT) |
            (delete_status == FS_STATUS_ERROR
                 ? (((uint32_t)op_delete_slot_error_reason <<
                     AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_REASON_SHIFT) |
                    ((uint32_t)op_delete_slot_error_detail <<
                     AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_DETAIL_SHIFT) |
                    ((uint32_t)op_delete_slot_error_site <<
                     AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_SITE_SHIFT))
                 : 0u));
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
        /* Record whether the fresh Kit directory materialized. */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (uint8_t)((AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT <<
                       AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                      AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT |
                      (!op_file ? AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED : 0u)),
            (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
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
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL) {
            uint8_t instrument_slot;

            filesystem_setIdentityName(FS_IDENTITY_KIT_ROW,
                                       preset_currentName);
            /*
             * Stage the saved Kit's direct source and inherited members.
             *
             * What: marks the saved Kit row direct to op_slot and its six
             * Instrument rows inherited from that Kit before Menu's existing
             * HCNAMES session-exit flush. Why: the LCD identity cache alone
             * cannot change durable /.hcnames provenance. Inputs: saved root
             * slot and source Scene. Outputs: seven dirty source cells; no
             * file I/O. Affiliates: Kit Load staging and
             * menu_endResidentNameScratchSession().
             */
            (void)filesystem_setResidentSource(
                filesystem_residentKitRow(op_kit_save_source_scene),
                op_slot);
            for (instrument_slot = 0u;
                 instrument_slot < STORAGE_KIT_SLOT_COUNT;
                 instrument_slot++) {
                (void)filesystem_setResidentSource(
                    filesystem_residentInstrumentRow(
                        op_kit_save_source_scene, instrument_slot),
                    FS_RESIDENT_SOURCE_INHERIT);
            }
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT,
                (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
        }
        /*
         * Defer successful completion until the boot-equivalent Kit rebuild
         * chain has finished. The directory is now written, but the active
         * cache may still describe the pre-save card contents. The final save
         * flush starts a physical Kit/ scan; that scan then starts the complete
         * 000..999 `.hcindex` writer before the original callback is released.
         */
        op_library_index_rebuild_kind = FS_NAME_CACHE_KIT;
        op_library_index_rebuild_pending = 1u;
        /* The callback is not released until the index rebuild chain ends. */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH <<
             AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
            AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT,
            (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_NORMAL) {
            /*
             * Kit Save owns the saved Scene's Kit row and six inherited
             * Instrument rows. Publish them before the pending Kit index
             * rebuild can retag and clear the shared name cache; the parked
             * callback then continues through that rebuild as before. See
             * S056_HCNAMES_FOLLOW_UP.md section 11.2/A3.
             */
            op_kit_load_scene_mask =
                (uint16_t)(1u << op_kit_save_source_scene);
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_KIT);
            return;
        }
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* Diagnostic-only phase observer for Bank Save's non-payload phases. */
static uint8_t op_bank_save_entry_last_phase = 0u;
static uint32_t op_bank_save_entry_stall_ticks = 0u;

static void filesystem_saveBankDirectory_tick(void)
{
    if (op_bank_payload_active) {
        filesystem_saveSceneDirectory_tick();
        return;
    }

    /*
     * Observe Bank Save entry/metadata phases without changing the writer.
     *
     * What: records one PHASE_STALL when op_phase remains unchanged for
     * 20,000 polls. Why: a Bank Save hang must be separated into Menu request,
     * Bank metadata, and delegated Scene-payload evidence. Inputs: op_phase
     * and op_slot; output: one diagnostic record only. The delegated payload
     * returns above and is intentionally not misclassified as a Bank-entry
     * stall. Affiliates: filesystem_pollPhaseStall(), Menu's Bank request
     * witness, and filesystem_saveSceneDirectory_tick().
     */
    if (filesystem_pollPhaseStall(op_phase,
                                  &op_bank_save_entry_last_phase,
                                  &op_bank_save_entry_stall_ticks,
                                  20000u)) {
        uint32_t value = (uint32_t)op_phase <<
                         AUTOSAVE_TRACE_PHASE_STALL_PHASE_SHIFT;

        value |= (uint32_t)op_slot << AUTOSAVE_TRACE_PHASE_STALL_SLOT_SHIFT;
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_PHASE_STALL,
                             AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY,
                             value);
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
         * restoration after direct replacement. Affiliates:
         * prepareBankSceneSaveSource
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
            if (!filesystem_cacheResidentRecord(op_item_offset, op_line_buf)) {
                /* Retain the save reader's close-before-error invariant for
                 * malformed extended HCNAMES input. */
                op_close_status = FS_STATUS_ERROR;
                op_close_done = false;
                if (afatfs_fclose(op_file, on_file_closed))
                    op_phase = 82u;
                return;
            }
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

    case 82: /* CLOSE PRELOAD, THEN ENTER ROOT BANK REPLACEMENT */
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
        /* Prove zero/one exact root Bank candidate before creating anything. */
        filesystem_deleteSlotDirectoryStart(op_slot, 0u);
        op_phase = 5u;
        return;

    case 5:
    {
        fs_status_t delete_status = filesystem_deleteSlotDirectory_tick();
        if (delete_status == FS_STATUS_BUSY)
            return;
        if (delete_status == FS_STATUS_ERROR) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 49u;
        return;
    }

    case 49:
        /*
         * Create the final numbered Bank directory after exact deletion.
         *
         * Inputs: current directory is `/Bank/`; the old exact slot has already
         * been removed by the singular resolver. Output: phases 5..12 write
         * bankset.bcg and selected Bank-local Scenes directly into this fresh
         * final tree. No temporary-root or old-name promotion exists.
         */
        op_file_ready = false;
        op_file = NULL;
        memset(op_save_bank_dir_open_name, 0,
               sizeof(op_save_bank_dir_open_name));
        if (!afatfs_mkdir_lfn(op_save_bank_dir_display_name,
                              AFATFS_MATCH_CASE_INSENSITIVE,
                              op_save_bank_dir_open_name,
                              on_file_opened)) {
            return;
        }
        op_phase = 50u;
        return;

    case 50:
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
            op_phase = 45u;
            return;
        }
        for (op_bank_child_cursor = 0u;
             op_bank_child_cursor < STORAGE_BANK_SCENE_MAX_SLOTS;
             op_bank_child_cursor++) {
            if ((op_bank_scene_save_mask &
                 (uint16_t)(1u << op_bank_child_cursor)) != 0u) {
                /*
                 * Write the selected Scene into the freshly-created final
                 * Bank folder.
                 *
                 * Input: op_bank_child_cursor is both resident Scene index and
                 * two-digit Bank-local child number. Output: phase 20 prepares
                 * the per-child Scene writer and delegates to the existing
                 * Scene payload save. No recursive child cleanup is needed
                 * here because exact replacement already removed the previous
                 * Bank tree.
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
        op_phase = 45u;
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
         * the final Bank folder was created/opened. Output: the final Bank
         * directory handle is restored after a child Scene payload returned to
         * root.
         * This must use afatfs_opendir(), not afatfs_opendir_lfn(): the LFN
         * opener compares display names, while this operation-local alias is
         * the 8.3 open name. Using the LFN opener here produced ERR BnkS11 after the
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
         * Prepare and write one Bank-local child into the final Bank folder.
         *
         * Inputs: current directory is the unique final Bank directory and
         * op_bank_child_cursor selects the resident Scene slot. Output: the
         * existing Scene writer starts at phase 8 and creates `SS Name/` with
         * sceneset, embedded Kit, pattern, and effects. Exact replacement
         * completed first, so this path cannot merge stale Instrument files
         * into the child.
         */
        if (!filesystem_prepareBankSceneSaveSource(op_bank_child_cursor)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_bank_payload_active = 1u;
        op_phase = 8u;
        return;
    }

    case 45:
        /* The final directory is already openable through its captured alias. */
        if (!afatfs_chdir(NULL))
            return;
        filesystem_recordSavedBankDirectory(op_save_bank_dir_display_name,
                                            op_save_bank_dir_open_name);
        bank_setDisplayName(op_bank_display_name);
        bank_setScenePresentMask(op_bank_scene_save_mask);
        bank_selectActiveSceneForEditMask(op_bank_active_scene);
        bank_setSceneMaskVoiceEdit(op_bankset_state.scene_mask_voice_edit);
        bank_setRestoreBankSlot(op_slot);
        /*
         * Persist the boot-restore Bank selected by a successful Bank Save.
         *
         * Inputs: the committed restore slot above. Output: the existing
         * debounced settings writer re-emits active_bank later; this call
         * opens no file. Why: Bank Save changes the same boot-selection
         * authority as Bank Load, so the two paths must remain symmetric.
         * Affiliates: filesystem_settingsWriterSchedule_tick() and
         * filesystem_nextSettingsLine().
         */
        filesystem_markSettingsDirty();
        bank_setHasResidentBank(1u);
        /*
         * The newly-created final root folder is the authoritative Bank identity.
         * Update only row zero in the already-read register; selected Scene,
         * Kit, and Instrument rows were copied from HCNAMES into the just
         * written Bank tree and remain unchanged in resident memory.
        */
        filesystem_cacheResidentName(0u, op_bank_display_name);
        /*
         * Stage the newly committed Bank's direct root provenance.
         *
         * What: marks HCNAMES row zero direct to the saved root slot before
         * phase 85 streams the already-read register back to disk. Why: the
         * direct Bank writer otherwise preserves the prior row-zero source.
         * Inputs: op_slot, the final numbered Bank directory. Output: one
         * dirty source cell and one lifecycle witness; no new storage or I/O.
         * Affiliates: Bank Load's symmetric staging and phase 85.
         */
        (void)filesystem_setResidentSource(FS_IDENTITY_BANK_ROW, op_slot);
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED <<
             AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
            AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK,
            (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
        op_phase = 83u;
        return;

    case 83: /* OPEN ROOT HCNAMES FOR DIRECT-REPLACEMENT REGISTER WRITE */
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
                        fs_list_cache_name[op_item_offset]),
                    fs_resident_source[op_item_offset], op_item_offset);
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

    case 86: /* CLOSE REGISTER, THEN PROVE IT BEFORE BANK INDEX RESTORE */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Bank Save uses the same row-0 read-back proof as Bank Load. The
         * register mismatch is diagnostic-only, while the original callback
         * remains parked through the already-required Bank index rebuild. The
         * existing probe byte carries VERIFIED into the Preset K witness; no
         * extra state is allocated. See section 11.3/B3-B4.
         */
        op_hcnames_probe_matches = 0u;
        op_close_status = FS_STATUS_DONE;
        op_phase = 88u;
        return;

    case 88: /* OPEN BANK-SAVE HCNAMES READ-BACK */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME,
                              "r",
                              FS_RESIDENT_NAMES_MATCH_MODE,
                              NULL,
                              on_file_opened))
            return;
        op_phase = 89u;
        return;

    case 89: /* WAIT BANK-SAVE HCNAMES READ-BACK OPEN */
        if (!op_file_ready)
            return;
        if (!op_file) {
            /* Witness clear is retained by the callback; payload stays valid. */
            filesystem_clearResidentSourceDirtyFlags();
            /* The index rebuild remains armed below only on the normal path. */
            op_library_index_rebuild_kind = FS_NAME_CACHE_BANK;
            op_library_index_rebuild_pending = 1u;
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK,
                (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_line_len = 0u;
        op_phase = 90u;
        return;

    case 90: /* READ BANK-SAVE ROW 0 AND START CLOSE */
    {
        uint8_t line_ready = 0u;
        uint8_t eof = 0u;
        storage_status_t read_status = filesystem_readTextLine(
            op_file, op_line_buf, &op_line_len, sizeof(op_line_buf),
            &line_ready, &eof);

        if (read_status != STORAGE_STATUS_OK &&
            read_status != STORAGE_STATUS_WAIT) {
            /*
             * A failed Bank-save row-0 read-back is witness-only. The
             * register was streamed, closed, and flushed before this
             * read-only probe, so preserve DONE, leave VERIFIED clear, and
             * continue to the common close/index-rebuild path. Failing here
             * would also skip the /Bank/.hcindex rebuild and make a newly
             * created or renamed Bank invisible until reboot; the failed-open
             * branch above already arms that rebuild correctly. Inputs:
             * read_status only. Output: op_phase only; op_close_status stays
             * DONE. Affiliates: filesystem_lastHcnamesVerified(), the Bank
             * Save lifecycle witness, and S056 section 12.2.
             */
            op_phase = 91u;
            return;
        }
        if (line_ready) {
            op_hcnames_probe_matches =
                filesystem_residentRowMatchesCache(0u, op_line_buf);
            op_phase = 91u;
            return;
        }
        if (eof)
            op_phase = 91u;
        return;
    }

    case 91: /* START CLOSE BANK-SAVE HCNAMES READ-BACK */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 92u;
        return;

    case 92: /* WAIT CLOSE + RESTORE ROOT BANK INDEX */
        if (!op_close_done)
            return;
        op_file = NULL;
        if (!afatfs_chdir(NULL))
            return;
        /*
         * Bank Save's probe has one unconditional exit. The payload and
         * register write already completed; this phase therefore always
         * clears source dirtiness, arms the Bank index rebuild, emits the
         * lifecycle witness, and completes DONE. Only the diagnostic K bit
         * distinguishes a confirmed row match from an ambiguous probe.
         * Affiliates: filesystem_lastHcnamesVerified(), the Bank index
         * rebuild handoff, and S056_HCNAMES_FOLLOW_UP.md section 12.2.
         */
        filesystem_clearResidentSourceDirtyFlags();
        /*
         * Bank Save has completed its direct delete/recreate tree. Park the
         * original callback and run the same boot-equivalent Bank rescan plus
         * `/Bank/.hcindex` rewrite used by Kit, root Scene, and Bank. This is
         * required for newly-created, renamed, or removed root Bank folders to
         * become visible immediately without a restart.
         */
        op_library_index_rebuild_kind = FS_NAME_CACHE_BANK;
        op_library_index_rebuild_pending = 1u;
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH <<
             AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
            AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK,
            (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
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
        filesystem_deleteSceneSlotDirectoryStart();
        op_phase = 5u;
        return;

    case 5:
        delete_status = filesystem_deleteSlotDirectory_tick();
        if (delete_status == FS_STATUS_BUSY)
            return;
        /* See the Kit Save equivalent (filesystem_saveKitDirectory_tick()
         * case 5) for what the packed reason/detail bits mean. */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (uint8_t)((AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_DELETE_RESULT <<
                       AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                      AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE |
                      (delete_status == FS_STATUS_ERROR
                           ? AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED : 0u)),
            ((uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT) |
            (delete_status == FS_STATUS_ERROR
                 ? (((uint32_t)op_delete_slot_error_reason <<
                     AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_REASON_SHIFT) |
                    ((uint32_t)op_delete_slot_error_detail <<
                     AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_DETAIL_SHIFT) |
                    ((uint32_t)op_delete_slot_error_site <<
                     AUTOSAVE_TRACE_SAVE_LIFECYCLE_DELETE_SITE_SHIFT))
                 : 0u));
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
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (uint8_t)((AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_CREATE_RESULT <<
                       AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                      AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE |
                      (!op_file ? AUTOSAVE_TRACE_SAVE_LIFECYCLE_FLAG_FAILED : 0u)),
            (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
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
            /*
             * Stage the direct Scene source before the HCNAMES read/merge.
             *
             * What: marks the saved Scene direct to op_slot and its embedded
             * Kit/Instrument rows inherited. Why: the immediately following
             * register read must preserve the new provenance over the old
             * on-card values. Inputs: saved root slot/source Scene. Outputs:
             * dirty source cells and one SOURCE_STAGED trace; no file I/O.
             * Affiliates: Scene Load's equivalent staging and the shared
             * HCNAMES updater.
             */
            (void)filesystem_setResidentSource(
                filesystem_residentSceneRow(op_kit_save_source_scene),
                op_slot);
            (void)filesystem_setResidentSource(
                filesystem_residentKitRow(op_kit_save_source_scene),
                FS_RESIDENT_SOURCE_INHERIT);
            {
                uint8_t instrument_slot;
                for (instrument_slot = 0u;
                     instrument_slot < STORAGE_KIT_SLOT_COUNT;
                     instrument_slot++) {
                    (void)filesystem_setResidentSource(
                        filesystem_residentInstrumentRow(
                            op_kit_save_source_scene, instrument_slot),
                        FS_RESIDENT_SOURCE_INHERIT);
                }
            }
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SOURCE_STAGED <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE,
                (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
                (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_FINISH <<
                 AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
                AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE,
                (uint32_t)op_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
            /*
             * Scene Save now uses the common ownership handoff. Its complete
             * Scene/Kit/Instrument identity block is published before the
             * parked Scene index rebuild begins. See section 11.2/A1/A6.
             */
            filesystem_beginResidentNamePublish(
                FS_INTERNAL_OP_UPDATE_HCNAMES_SCENE);
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
#if DEV_MODE_LOGGING && DEV_LOGGING_IWDG
    /*
     * This is the second foreground pump, and it must feed the watchdog for
     * exactly the reason stated above: the blocking helpers bypass
     * filesystem_tick(), which is where the only other feed lives.
     *
     * Concretely, the modal sample install
     * (filesystem_installSampleFolderBlocking() -> filesystem_blockOpen/
     * blockChdir/installOneSample) runs entirely through this path and can
     * legitimately take far longer than the IWDG's ~32.8 s period while it
     * erases six 256 KB flash sectors and streams megabytes over bit-bang SPI.
     * Without a feed here that operation would be reset part-way through a
     * sampleFlash erase/program, which risks corrupting the sample-FLASH
     * region rather than merely rebooting. Feeding here keeps the watchdog
     * scoped to genuine hangs instead of penalising long legitimate work.
     */
    filesystem_devIwdgFeed();
#endif
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
    op_rename_result = AFATFS_RESULT_OK;
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
    if (op_rename_result != AFATFS_RESULT_OK || open_name[0] == '\0')
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
                                                 uint8_t present,
                                                 uint16_t source,
                                                 uint16_t row)
{
    uint8_t len = 0u;
    const char *token;

    /*
     * Format one fixed-order name-register row.
     *
     * Inputs: a resident eight-cell display field, presence predicate, paired
     * source word, and row class. Output: one `name<TAB>source` record. Empty
     * names still emit a token, so fixed logical row identity is never lost.
     */
    source = (uint16_t)(source & FS_RESIDENT_SOURCE_VALUE_MASK);
    if (!dst || cap < 4u || !filesystem_residentSourceValid(row, source))
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
    if (len + 2u >= cap)
        return 0u;
    dst[len++] = '\t';
    if (source == FS_RESIDENT_SOURCE_INHERIT)
        token = "-";
    else if (source == FS_RESIDENT_SOURCE_UNKNOWN)
        token = "?";
    else if (source == FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT)
        token = "@";
    else {
        if (len + 4u >= cap)
            return 0u;
        dst[len++] = (char)('0' + (source / 100u));
        dst[len++] = (char)('0' + ((source / 10u) % 10u));
        dst[len++] = (char)('0' + (source % 10u));
        dst[len++] = '\n';
        dst[len] = '\0';
        return len;
    }
    if (len + 2u >= cap)
        return 0u;
    dst[len++] = token[0];
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
                                                 bank_hasResidentBank(),
                                                 fs_resident_source[0u], 0u);
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
        return filesystem_formatResidentNameLine(dst, cap, NULL, 0u,
                                                 fs_resident_source[row + 1u],
                                                 (uint16_t)(row + 1u));
    }
    /*
     * The generic boot serializer cannot reconstruct Kit/Instrument identity
     * from audio SRAM any more. Runtime targeted HCNAMES updates preserve and
     * replace those authoritative rows from the identity block instead.
     */
    (void)row;
    return filesystem_formatResidentNameLine(
        dst, cap, NULL, 0u,
        fs_resident_source[row + 1u], (uint16_t)(row + 1u));
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
    uint16_t source_row;

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
    /* A remount must never inherit a direct source from the prior card. */
    for (source_row = 0u;
         source_row < FS_RESIDENT_NAMES_ROW_COUNT;
         source_row++) {
        fs_resident_source[source_row] = FS_RESIDENT_SOURCE_UNKNOWN;
    }
    autosave_setMutationTrackingEnabled(0u);
    afatfs_init();
}

void filesystem_markSettingsDirty(void)
{
    /*
     * Record one settings change and restart trailing debounce.
     *
     * Inputs: live time_sysTick after a changed Global Menu byte. Outputs:
     * dirty/revision state only; no file is
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

void filesystem_handleSettingsWriteResult(fs_status_t result)
{
    /*
     * Apply the shared retry policy for one completed settings.cfg write.
     *
     * Input: terminal SAVE_GLOBALS result. Output: DONE leaves the durable
     * revision decision to filesystem_complete(); ERROR preserves/creates
     * dirty work and restarts the one-second retry deadline. Why: both the
     * autonomous debounced writer and Bank Load/Save's immediate bridge need
     * identical retry behavior, but their callers acknowledge the terminal
     * facade state at different ownership boundaries. No filesystem I/O is
     * started here. Affiliates: filesystem_settingsWriterCompleted() and
     * presetManager.c's on_bank_settings_flush_complete().
     */
    if (result != FS_STATUS_DONE) {
        fs_settings_dirty = 1u;
        fs_settings_next_due_tick = (uint16_t)(
            time_sysTick + SETTINGS_AUTOWRITE_DEBOUNCE_MS);
    }
}

static void filesystem_settingsWriterCompleted(void)
{
    /*
     * Complete an invisible debounced settings.cfg write without involving
     * Preset/Menu. The shared retry policy runs before this callback releases
     * the terminal facade state, so a failed write converges later without
     * trapping future foreground or autonomous requests at DONE/ERROR.
     * Affiliate: filesystem_handleSettingsWriteResult().
     */
    filesystem_handleSettingsWriteResult(status);
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
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
#if DEV_MODE_LOGGING
        /*
         * The page guard is intentional: Load/Save owns the shared 9,000-byte
         * name cache while the user browses, so the AutoSave writer must wait
         * until the page is left. This witness proves that the writer was
         * armed with canonical dirty work and was stopped at that designed
         * admission boundary. flags bit 0 mirrors the dirty predicate and
         * value32 records the existing debounce deadline; no writer admission
         * or mutation-mask behavior is changed here.
         */
        if (!fs_autosave_suppress_witness && autosave_maskHasDirty()) {
            autosaveTrace_record(AUTOSAVE_TRACE_STAGE_WRITER_SUPPRESSED,
                                 1u,
                                 (uint32_t)fs_autosave_next_due_tick);
            fs_autosave_suppress_witness = 1u;
        }
#endif
        return;
    }
#if DEV_MODE_LOGGING
    /* Leaving the page ends this W episode; a later dirty arm may report anew. */
    fs_autosave_suppress_witness = 0u;
#endif
    if ((uint16_t)(now - fs_autosave_next_due_tick) >= 0x8000u)
        return;
    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosaveWriterCompleted)) {
        /*
         * Rearm the runtime drain's stall observer for this fresh admission.
         *
         * What: forces filesystem_pollPhaseStall()'s first call for this
         * drain to see a "changed" phase, the same way Bank Save's request
         * function rearms its own observer. Why: unlike the two
         * purely-diagnostic stall sites, a stall here forces a real
         * FS_STATUS_ERROR completion (see filesystem_autosaveParameterDrain_tick());
         * a stale near-threshold count carried over from an earlier,
         * unrelated drain admission could otherwise make a healthy drain fail
         * earlier than the intended ~30,000-poll budget if both happened to
         * linger at the same early phase. Inputs: none. Outputs: two
         * statics; no file I/O. Affiliates: filesystem_pollPhaseStall(),
         * filesystem_autosaveParameterDrain_tick().
         */
        op_autosave_drain_last_phase = 0xffu;
        op_autosave_drain_stall_ticks = 0u;
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
#if DEV_MODE_LOGGING
    if (status == FS_STATUS_DONE) {
        /* A full batch leaves work pending and should receive the existing
         * immediate continuation cadence rather than waiting another interval. */
        if (autosaveTrace_pendingCount() >=
            AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
            fs_autosave_trace_next_due_tick = 0u;

        /*
         * The trace ring's dropped counter is otherwise only an internal
         * getter. Publish its changed value after a successful append so a
         * field capture can distinguish a missing producer from records that
         * were overwritten before the SD drain acknowledged them. One G per
         * changed value avoids adding a record on every successful append.
         */
        {
            uint16_t dropped = autosaveTrace_droppedCount();
            if (dropped != fs_trace_reported_dropped) {
                autosaveTrace_record(AUTOSAVE_TRACE_STAGE_TRACE_DROPPED,
                                     0u, (uint32_t)dropped);
                fs_trace_reported_dropped = dropped;
            }
        }
        /* A completed append ends both the gate/error episode. */
        fs_trace_suppress_witness = 0u;
    } else if (status == FS_STATUS_ERROR && !fs_trace_suppress_witness) {
        /*
         * An append reached its terminal callback but failed. The flush cursor
         * is intentionally not advanced by that failure, so the same records
         * remain pending for retry; F only documents the failed boundary.
         */
        autosaveTrace_record(AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED,
                             AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_APPEND_ERROR,
                             0u);
        fs_trace_suppress_witness = 1u;
    }
#endif
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

    /*
     * Reserve the one filesystem facade for a foreground Load/Save command.
     *
     * Input: menu_isLoadSaveCommandActive(), true only for the accepted
     * command and its post-apply root-index restore.
     * Output: retain the RAM trace ring and its existing deadline without
     * opening `asavetrc.bin`. Why: this optional diagnostic append previously
     * could start between Bank payload completion and Menu's final read-only
     * `.hcindex` request, making that foreground request fail with generic
     * FsErr solely because the shared facade was busy. The narrower busy
     * predicate also permits flushing while the user remains on the page
     * after the command completes; no trace data is discarded and no
     * foreground filesystem operation is delayed.
     */
    if (menu_isLoadSaveCommandActive()) {
#if DEV_MODE_LOGGING
        /*
         * Trace flushing uses the staging buffer and is independent of the
         * name-cache ownership rule, but it must still defer while an accepted
         * Load/Save command owns the filesystem facade. Record the pending
         * count at this gate once per episode; if no later flush appears, the
         * evidence identifies command finalization as the missing boundary.
         */
        if (!fs_trace_suppress_witness &&
            autosaveTrace_pendingCount() != 0u) {
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_TRACE_SUPPRESSED,
                AUTOSAVE_TRACE_TRACE_SUPPRESSED_FLAG_COMMAND_ACTIVE,
                (uint32_t)autosaveTrace_pendingCount());
            fs_trace_suppress_witness = 1u;
        }
#endif
        return;
    }

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
#if DEV_MODE_LOGGING
        /* Starting an append proves the gate opened; permit a later error
         * callback to report a new failure episode. */
        fs_trace_suppress_witness = 0u;
#endif
    }
#endif
}

#if DEV_MODE_LOGGING && DEV_LOGGING_IWDG
/*
 * IWDG/RCC register access, following this file's plain-register style (see
 * clocks.c's own RCC/PWR/FLASH defines) -- no CMSIS device header exists in
 * this project. Addresses and bit positions are from RM0410 (STM32F76xxx
 * reference manual): Chapter 34 (IWDG) and RCC_CSR (Section 6.3.20). Local to
 * this file; not exposed, not used anywhere else.
 */
#define DEV_IWDG_KR          (*((volatile uint32_t *)0x40003000UL))
#define DEV_IWDG_PR          (*((volatile uint32_t *)0x40003004UL))
#define DEV_IWDG_RLR         (*((volatile uint32_t *)0x40003008UL))
#define DEV_IWDG_SR          (*((volatile uint32_t *)0x4000300CUL))
#define DEV_RCC_CSR          (*((volatile uint32_t *)0x40023874UL))
#define DEV_RCC_CSR_IWDGRSTF (1UL << 29)
#define DEV_RCC_CSR_RMVF     (1UL << 24)
/*
 * LSI control lives in the same RCC_CSR register as the reset-cause flags.
 * The IWDG counts on the LSI, and nothing else in this firmware ever starts
 * it, so filesystem_devIwdgStart() must enable it explicitly and confirm
 * LSIRDY before touching any IWDG register that handshakes across that clock
 * domain. See the hazard note in filesystem_devIwdgStart().
 */
#define DEV_RCC_CSR_LSION    (1UL << 0)
#define DEV_RCC_CSR_LSIRDY   (1UL << 1)
/*
 * Bounds for the two watchdog bring-up handshakes, in TIM6 milliseconds.
 * LSI startup is specified in the hundreds of microseconds, so 10 ms is
 * generous; these exist purely so a dead LSI or an unresponsive register can
 * never stall boot the way the original unbounded spin did.
 */
#define DEV_IWDG_LSI_READY_TIMEOUT_MS   10u
#define DEV_IWDG_REG_UPDATE_TIMEOUT_MS  10u

/* Max 12-bit reload with the max /256 prescaler: ~32.8s at nominal 32kHz
** LSI. This is only the hardware dead-man period; DEV_LOGGING_IWDG_EXPIRE
** (config.h) is a separate, much longer software ceiling layered on top by
** filesystem_devIwdgFeed() below -- the two are not the same timeout. */
#define DEV_IWDG_PRESCALER_DIV256 0x6UL
#define DEV_IWDG_RELOAD_MAX       0xFFFUL

static void filesystem_devIwdgStart(void)
{
    uint16_t wait_start;

    /*
     * Bring up the independent watchdog without ever being able to hang boot.
     *
     * HAZARD THIS FUNCTION EXISTS TO AVOID (regression fixed 2026-08-21): the
     * IWDG is clocked by the LSI, and IWDG_SR's PVU/RVU bits only clear once a
     * PR/RLR write has propagated into that LSI clock domain. This firmware
     * never enables the LSI anywhere (no LSION write exists outside this
     * function), and the LSI is off after reset. An earlier version of this
     * code wrote the unlock key, then PR/RLR, then spun on
     * `while (IWDG_SR & 3)` BEFORE writing the 0xCCCC start key. Since 0xCCCC
     * is what implicitly starts the LSI, the LSI was still stopped, PVU/RVU
     * could never clear, and the spin never exited: an indefinite boot hang
     * with no timeout, no bootlog, and no trace file, because it ran before
     * the card was even mounted. The watchdog also could not rescue it, since
     * it had not been started yet.
     *
     * ORDER IS LOAD-BEARING. The LSI is enabled and confirmed ready FIRST, so
     * every subsequent register handshake has a running clock domain to
     * complete against. Only then is PR/RLR programmed, and only then is the
     * watchdog started.
     *
     * FAIL-SAFE ON A DEAD LSI: if LSIRDY never appears, this returns having
     * started nothing at all. That is deliberate and is the only safe
     * outcome. Starting the IWDG with its reset-default PR/RLR would arm a
     * ~512 ms period (PR=0 => /4, RLR=0xFFF at ~32 kHz), which the pre-audio
     * SD ladder would blow through immediately, converting a diagnostic aid
     * into a permanent reset boot-loop. No watchdog is strictly better than a
     * watchdog that reboots the instrument twice a second.
     *
     * EVERY WAIT IS BOUNDED. Both handshakes time out against the TIM6 1 kHz
     * time_sysTick, which main.c starts (time_initTimer()) long before this
     * runs. By construction this function cannot spin forever regardless of
     * LSI, register, or silicon behaviour.
     *
     * Inputs: a running TIM6 millisecond tick. Outputs: either a fully armed
     * ~32.8 s watchdog plus initialized feed-deadline state, or no watchdog at
     * all and an untouched IWDG peripheral. Affiliates:
     * filesystem_devIwdgBootCheck() (sole caller) and
     * filesystem_devIwdgFeed().
     */

    /* 1. Enable the LSI and confirm it is actually running. */
    DEV_RCC_CSR |= DEV_RCC_CSR_LSION;
    wait_start = time_sysTick;
    while ((DEV_RCC_CSR & DEV_RCC_CSR_LSIRDY) == 0u) {
        if ((uint16_t)(time_sysTick - wait_start) >= DEV_IWDG_LSI_READY_TIMEOUT_MS)
            return;   /* LSI dead: start nothing, boot continues normally. */
    }

    /* 2. Program the longest available period now that the LSI is clocking. */
    DEV_IWDG_KR = 0x5555UL;                  /* unlock PR/RLR for write */
    DEV_IWDG_PR = DEV_IWDG_PRESCALER_DIV256;
    DEV_IWDG_RLR = DEV_IWDG_RELOAD_MAX;
    wait_start = time_sysTick;
    while ((DEV_IWDG_SR & 0x3UL) != 0u) {    /* PVU/RVU propagate to LSI */
        if ((uint16_t)(time_sysTick - wait_start) >= DEV_IWDG_REG_UPDATE_TIMEOUT_MS)
            return;   /* Never armed, so the default short period cannot bite. */
    }

    /* 3. Load the new period, then start counting. */
    DEV_IWDG_KR = 0xAAAAUL;                  /* first feed, applies RLR */
    DEV_IWDG_KR = 0xCCCCUL;                  /* start counting */
    fs_devwdg_boot_start_tick = systick_ticks;
    fs_devwdg_lapsed = 0u;
    fs_devwdg_armed = 1u;
}

/*
 * Feed the IWDG only from ordinary foreground code -- this is called solely
 * from filesystem_tick(), itself reachable only from main.c's boot
 * spin-loops and the post-audio main superloop, never from an ISR. A raw
 * blocking call that never returns therefore also never returns here to
 * feed it; the hardware watchdog then resets on its own ~32-second native
 * period regardless of interrupts or any other software state.
 *
 * While still inside the pre-audio window (fs_boot_logging_active), feeding
 * additionally stops on its own once DEV_LOGGING_IWDG_EXPIRE elapses without
 * ever reaching filesystem_bootLoggingEnd(). This catches a foreground loop
 * that keeps calling filesystem_tick() forever without making real boot
 * progress, which a call-never-returns check alone cannot see. After boot
 * ends, feeding is unconditional: the IWDG cannot be stopped once started,
 * so it remains a full-runtime hang backstop for the rest of the session.
 */
static void filesystem_devIwdgFeed(void)
{
    /*
     * Never feed a watchdog that was not actually armed. filesystem_devIwdgStart()
     * intentionally returns without starting anything when the LSI does not come
     * ready, and in that case there is nothing to service; writing the reload key
     * to a stopped IWDG is harmless but misleading, and the EXPIRE backstop below
     * must not treat a never-armed boot as having lapsed.
     */
    if (!fs_devwdg_armed)
        return;
    if (fs_devwdg_lapsed)
        return;
    if (fs_boot_logging_active &&
        (uint32_t)(systick_ticks - fs_devwdg_boot_start_tick) >=
            ((uint32_t)DEV_LOGGING_IWDG_EXPIRE * SYSTICK_TICKS_PER_MS)) {
        fs_devwdg_lapsed = 1u;  /* deliberately stop feeding; let it lapse */
        return;
    }
    DEV_IWDG_KR = 0xAAAAUL;
}

void filesystem_devIwdgBootCheck(void)
{
    uint8_t was_iwdg_reset;
    uint8_t capsule_valid;

    /*
     * Sample the reset cause BEFORE filesystem_devIwdgStart() touches RCC_CSR.
     * LSION and the reset-cause flags share that register, so reading first
     * keeps the diagnosis independent of the read-modify-write below and makes
     * the ordering intent explicit rather than incidental.
     */
    was_iwdg_reset = (DEV_RCC_CSR & DEV_RCC_CSR_IWDGRSTF) != 0u;
    capsule_valid = (fs_devwdg_capsule.magic == DEV_IWDG_CAPSULE_MAGIC);

    /* Always clear reset-cause flags after reading them, so IWDGRSTF cannot
     * misattribute some later, unrelated reset to this feature. */
    DEV_RCC_CSR |= DEV_RCC_CSR_RMVF;

    /* Arm before the recovery write below, so a stall inside that one-time
     * write is itself covered rather than running unwatched. This call is
     * bounded and fail-safe: it cannot hang, and it starts nothing at all if
     * the LSI does not come ready. */
    filesystem_devIwdgStart();

    if (was_iwdg_reset && capsule_valid) {
        memcpy(fs_boot_logging_code, (const void *)fs_devwdg_capsule.code,
               sizeof(fs_boot_logging_code));
        (void)filesystem_writeBootFailureLogBlocking();
    }
    fs_devwdg_capsule.magic = 0u;  /* consumed, or never valid; start clean */
}
#else
void filesystem_devIwdgBootCheck(void)
{
}
#endif /* DEV_MODE_LOGGING && DEV_LOGGING_IWDG */

void filesystem_tick(void)
{
#if DEV_MODE_LOGGING && DEV_LOGGING_IWDG
    filesystem_devIwdgFeed();
#endif
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
     * Input: no current filesystem owner. Output: settings may start; trace
     * and autosave scheduling run only if the facade remains idle. Why: short
     * source/Global persistence should not be starved by an Autosave backlog;
     * the trace remains behind settings because it is diagnostic-only.
     * Affiliates: both autonomous completion callbacks and the trace scheduler.
     */
    if (status == FS_STATUS_IDLE)
        filesystem_settingsWriterSchedule_tick();
    /*
     * Persist a pending diagnostic batch before an eligible AutoSave drain
     * claims the one facade after leaving Load/Save.
     *
     * Inputs: an idle facade outside Load/Save and any RAM lifecycle records
     * produced while the foreground Instrument operation owned the card.
     * Output: at most one existing bounded append starts before the overdue
     * first full-bank drain; the normal writer starts on the next idle poll.
     * Why: the former writer-before-trace order let an exit immediately start
     * a long initial drain, so a power-off during that drain left only an older
     * S record and concealed the J/I evidence needed to distinguish a missed
     * publication from an unfinished persistence attempt. This changes only
     * logging-build diagnostic arbitration: tracing still never starts on a
     * Load/Save page and neither mask ownership nor writer bytes change.
     * Affiliates: filesystem_autosaveTraceFlushSchedule_tick(),
     * filesystem_autosaveWriterSchedule_tick(), and AutosaveTrace.c.
     */
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveTraceFlushSchedule_tick();
    /*
     * Start the durable AutoSave writer only after settings and, when pending,
     * its pre-drain diagnostic witness declined the idle facade.
     *
     * Inputs: the unchanged writer gate/deadline and an idle facade. Output:
     * the original parameter-drain state machine owns the card exactly as
     * before, except that a pending logging-build trace has one append first.
     * Why: the trace's J/I batch must become durable before a long first drain
     * can be interrupted by power removal. Affiliate: the scheduler above.
     */
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveWriterSchedule_tick();
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
    /*
     * HCNAMES probe scratch is reset at the boundary that owns each probe:
     * filesystem_hcnamesProbeBegin(), the generic writer's read-back phase,
     * and Bank Load/Save's row-0 proof. Do not reset the result here: a Bank
     * Save carries its verified bit through the deferred `.hcindex` rebuild,
     * whose nested filesystem_start() would otherwise erase the witness before
     * Preset emits K. Inputs/outputs are the existing one-byte scratch; no new
     * storage is allocated. Affiliate: filesystem_lastHcnamesVerified().
     */
    op_hcnames_probe_state = FS_HCNAMES_PROBE_IDLE;
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
    op_delete_slot_number = 0u;
    memset(&op_delete_slot_target, 0, sizeof(op_delete_slot_target));
    op_delete_slot_target.id.kind = AFATFS_OBJECT_NONE;
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
    memset(op_save_bank_dir_open_name, 0, sizeof(op_save_bank_dir_open_name));
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

static uint8_t filesystem_pumpBlockingOperation(void)
{
    uint16_t started = time_sysTick;

    /*
     * Drive one facade-owned blocking wrapper to a terminal state, bounded.
     *
     * What: replaces the bare BUSY/tick pump body used by every blocking
     * wrapper below. On
     * expiry it forces FS_STATUS_ERROR and returns zero, so the caller's
     * existing `if (status != FS_STATUS_DONE)` branch runs its normal
     * acknowledge-and-clean-up path unchanged.
     *
     * Why: these loops were bounded only as a side effect of
     * filesystem_bootLoggingPollDeadline(), which is compiled out when
     * DEV_MODE_LOGGING == 0. In that build a stalled facade state machine
     * could spin forever inside the callee, before main.c's own pump bound
     * could regain control. This is the production-build form of the Session
     * 056 boot hang one layer below the main ladder. See
     * S056_BOOT_HANG_FOLLOWUP.md sections 9.2, 9.5, and 10.
     *
     * This deliberately duplicates the logging poll's policy rather than
     * replacing it: the logger additionally latches timeout identity, freezes
     * its forensic capsule, and preserves the retained operation code. When
     * both bounds are active the logger fires first at the top of
     * filesystem_tick(), so the current logging build keeps its behavior.
     *
     * Inputs: module-scope status, time_sysTick, and
     * BOOT_FILESYSTEM_PUMP_WAIT_MS. Output: nonzero when the operation leaves
     * BUSY on its own; zero when this deadline forces ERROR. State: one 16-bit
     * stack local; no static storage is added. Like the logging poll, this is
     * cooperative and cannot preempt a C or SD-driver call that never returns.
     * Affiliates: filesystem_bootLoggingPollDeadline(), every blocking wrapper
     * below, and main.c's boot_waitFilesystemPump().
     */
    for (; status == FS_STATUS_BUSY; ) {
        if ((uint16_t)(time_sysTick - started) >=
            BOOT_FILESYSTEM_PUMP_WAIT_MS) {
            status = FS_STATUS_ERROR;
            return 0u;
        }
        filesystem_tick();
    }
    return 1u;
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
        (void)filesystem_pumpBlockingOperation();
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
        (void)filesystem_pumpBlockingOperation();
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
 * Pump every pending trace batch to its durable terminal boundary for a bench harness.
 *
 * Inputs: an idle facade and any current ring tail. Output: success only after
 * every pending batch completes; busy or a real I/O error returns zero. Why:
 * this supplies an explicit pre-power-cycle evidence boundary without making
 * normal runtime code block for diagnostics.
 * Terminal status is acknowledged before return so this optional helper cannot
 * leave the autonomous schedulers disabled behind a stale DONE/ERROR state.
 */
uint8_t filesystem_autosaveTraceFlushBlocking(void)
{
    while (autosaveTrace_pendingCount() != 0u) {
        /* Each loop owns one bounded append and acknowledges it before the next. */
        if (status == FS_STATUS_BUSY ||
            !filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                              FS_FILE_SETTINGS, 0u, NULL)) {
            return 0u;
        }
        (void)filesystem_pumpBlockingOperation();
        if (status != FS_STATUS_DONE) {
            filesystem_ack();
            return 0u;
        }
        filesystem_ack();
    }
    return 1u;
}

uint8_t filesystem_autosaveTraceFlushAfterBootFailureBlocking(void)
{
#if DEV_MODE_LOGGING
    uint8_t drained;

    /*
     * Drain the trace ring on a boot route that has already timed out.
     *
     * What: wraps filesystem_autosaveTraceFlushBlocking() in the same
     * recovery framing filesystem_writeBootFailureLogBlocking() uses, so the
     * append can still run after the cooperative boot deadline has latched.
     *
     * Why it must exist: once fs_boot_logging_timed_out is set and the
     * operation arm is cleared, filesystem_bootLoggingPollDeadline() returns
     * that latch for every subsequent call, and filesystem_tick()'s first
     * statement is `if (filesystem_bootLoggingPollDeadline()) return;`. The
     * facade can therefore never advance again, and the drain added to the
     * boot failure path spins until its own pump bound expires and writes
     * nothing. That is why SD_CARD5 produced an eight-byte bootlog.bin and
     * zero trace records: bootlog escapes only because it enters recovery
     * mode, which routes the poll down its separate branch. See
     * S056_BOOT_HANG_FOLLOWUP.md section 14.4.
     *
     * Inputs: the module-scope logging latches and the pending ring. Output:
     * nonzero when every pending batch reached its terminal boundary. The
     * recovery episode is opened and closed here so
     * filesystem_writeBootFailureLogBlocking(), which refuses to start while
     * fs_boot_logging_recovery is set, still runs afterwards; its own entry
     * additionally resets fs_boot_logging_recovery_failed, so a drain that
     * exhausts this episode cannot cost the bootlog write.
     *
     * A terminal-but-unacknowledged facade is acknowledged first: the
     * underlying helper refuses to start while status is BUSY and cannot
     * start a new operation from DONE/ERROR.
     *
     * State: none. This adds no SRAM and no new file; the ring, the append
     * operation, and `/asavetrc.bin` are all pre-existing.
     *
     * Affiliates: filesystem_autosaveTraceFlushBlocking(),
     * filesystem_bootLoggingPollDeadline(),
     * filesystem_writeBootFailureLogBlocking(), and main.c's
     * boot_filesystem_failure label.
     */
    if (!fs_boot_logging_active || fs_boot_logging_recovery)
        return 0u;
    if (status == FS_STATUS_DONE || status == FS_STATUS_ERROR)
        filesystem_ack();

    fs_boot_logging_recovery = 1u;
    fs_boot_logging_recovery_failed = 0u;
    fs_boot_logging_recovery_started_tick = time_sysTick;

    drained = filesystem_autosaveTraceFlushBlocking();

    fs_boot_logging_recovery = 0u;
    fs_boot_logging_recovery_failed = 0u;
    return drained;
#else
    return 0u;
#endif
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
    (void)filesystem_pumpBlockingOperation();
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
    (void)filesystem_pumpBlockingOperation();
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
        (void)filesystem_pumpBlockingOperation();
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
    uint16_t pump_started = time_sysTick;

    while (status == FS_STATUS_BUSY) {
        /*
         * Bound the diagnostic-aware wrapper inline because its loop body
         * owns a per-iteration callback and therefore cannot use the uniform
         * helper above. On expiry, force FS_STATUS_ERROR so the existing
         * diagnostic failure branch acknowledges and returns zero. Why: a
         * DEV_MODE_LOGGING == 0 build otherwise leaves this ninth blocking
         * loop unbounded, even after the eight uniform wrappers are fixed.
         * See S056_BOOT_HANG_FOLLOWUP.md section 10.3.
         */
        if ((uint16_t)(time_sysTick - pump_started) >=
            BOOT_FILESYSTEM_PUMP_WAIT_MS) {
            status = FS_STATUS_ERROR;
            break;
        }
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
        (void)filesystem_pumpBlockingOperation();
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
    (void)filesystem_pumpBlockingOperation();
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

    /*
     * Save-lifecycle request witness.
     *
     * What: records that the Kit Save request was accepted by the filesystem
     * facade. Why: later DELETE_RESULT/CREATE_RESULT/SOURCE_STAGED/FINISH
     * records can now distinguish an upstream Menu gate from a filesystem
     * phase. Inputs: the accepted numbered Kit slot. Output: one eight-byte
     * trace record and no state change. Affiliate: AutosaveTrace.h's O layout.
     */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
        (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
         AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
        AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_KIT,
        (uint32_t)slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);

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

    /* Record accepted Scene Save before its asynchronous writer begins. */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
        (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
         AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
        AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_SCENE,
        (uint32_t)slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);

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

    /*
     * Rearm the Bank Save entry stall observer for this fresh request.
     *
     * What: forces filesystem_pollPhaseStall()'s next call to see a "changed"
     * phase by setting the retained last-observed phase to a value
     * op_phase can never hold (fs_delete_slot_phase_t-style phase numbers
     * here are all small case labels well under 0xff). Why: without this,
     * a stale last_phase/stall_ticks pair left by an earlier, unrelated Bank
     * Save request could carry a near-threshold count into this new request
     * if both happen to sit at the same early phase, causing an
     * earlier-than-intended AUTOSAVE_TRACE_STAGE_PHASE_STALL report. This
     * site is diagnostic-only (§3.3), so the only consequence was a
     * possibly-premature trace record, not a wrong result — but resetting
     * here removes the ambiguity for good. Inputs: none. Outputs: two
     * statics; no file I/O. Affiliates: filesystem_pollPhaseStall(),
     * filesystem_saveBankDirectory_tick().
     */
    op_bank_save_entry_last_phase = 0xffu;
    op_bank_save_entry_stall_ticks = 0u;

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
    memcpy(op_bank_display_name, display_name, STORAGE_KIT_DISPLAY_NAME_LEN);
    op_bank_display_name[STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    /* Record accepted Bank Save before its child payload loop begins. */
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
        (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
         AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
        AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_BANK,
        (uint32_t)slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
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
    if (op != FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP) {
        /* Record accepted root Instrument Save/Morph request. */
        autosaveTrace_record(
            AUTOSAVE_TRACE_STAGE_SAVE_LIFECYCLE,
            (AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_REQUEST <<
             AUTOSAVE_TRACE_SAVE_LIFECYCLE_CHECKPOINT_SHIFT) |
            AUTOSAVE_TRACE_SAVE_LIFECYCLE_TYPE_INSTRUMENT,
            (uint32_t)source_slot << AUTOSAVE_TRACE_SAVE_LIFECYCLE_SLOT_SHIFT);
    }
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

bool filesystem_requestSaveInstrumentMorphTemp(uint8_t source_scene,
                                               uint8_t source_slot,
                                               fs_completion_cb_t cb)
{
    /*
     * Write the hidden Morph-only baseline for InstrumentMrp.
     *
     * Inputs: the resident Scene/voice and the existing temporary-file
     * completion callback. Output: `.hctmp.<ext>` contains one parser anchor
     * plus the current Morphable Morph endpoints, never a second persistent
     * RAM image or a normal-load snapshot. The same filename is intentional:
     * only one nested Instrument mode can own this reversible source.
     */
    if (!filesystem_requestSaveInstrumentMode(
            FS_INTERNAL_OP_SAVE_INSTRUMENT_TEMP, source_scene, source_slot,
            NULL, cb)) {
        return false;
    }
    op_instrument_save_mode = STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT;
    return true;
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

bool filesystem_requestLoadInstrumentMorphTemp(uint8_t destination_scene,
                                               uint8_t destination_slot,
                                               instrument_type_t type,
                                               fs_completion_cb_t cb)
{
    /*
     * Load the hidden Morph-only baseline through the existing Instrument
     * parser/staging union.
     *
     * Inputs: the current destination Scene/slot/type and callback. Output:
     * the staged candidate is consumed by the Morph-only Preset commit; type,
     * Normal image, and HCNAMES identity are never copied to the resident slot.
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
    op_instrument_load_temporary = FS_INSTRUMENT_LOAD_TEMP_MORPH;
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

uint8_t filesystem_loadedInstrumentWasTemporary(void)
{
    /*
     * Expose the filesystem-captured origin of the completed Instrument.
     *
     * Inputs: the existing request-local flag, which filesystem_start() resets
     * for every new operation and filesystem_requestLoadInstrumentTemp() alone
     * sets. Output: the exact normal-pool versus `.hctmp` classification remains
     * available beside the validated staging image until a subsequent request
     * reuses operation storage. Why: Menu's temporary save/restore sequencing
     * latch can remain set for UI reasons even when this completed file was a
     * normal root-pool load, so it is not safe AutoSave provenance. This getter
     * copies no payload and allocates no state. Affiliate: Menu completion.
     */
    return op_instrument_load_temporary;
}

uint8_t filesystem_loadedInstrumentWasMorphTemporary(void)
{
    /* The origin flag remains valid beside the staged Instrument until the
     * next filesystem request reuses operation storage. */
    return (uint8_t)(op_instrument_load_temporary ==
                     FS_INSTRUMENT_LOAD_TEMP_MORPH);
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
