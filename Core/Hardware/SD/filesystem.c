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
#include "storageTypes.h"
#include "SceneData.h"
#include "sd_routines.h"
#include "spi_sd.h"
#include "presetManager.h"
#include "ParameterArray.h"
#include "SampleMemory.h"
#include "kitBrowser.h"
#include "sequencer.h"
#include "PatternData.h"
#include "timebase.h"
#include <string.h>
#include <stdint.h>

#define FS_SECTOR_SIZE_BYTES 512u
#define FS_NUM_FATS_EXPECTED 2u
#define MBR_PARTITION_TYPE_EXFAT 0x07u
#define FS_CONTAINER_META_LEN 64u
#define FS_CONTAINER_KIT_LEN 512u
#define FS_CONTAINER_PAD_BYTE 0xffu
#define FS_KIT_LFN_MAX 80u
#define FS_TEXT_LINE_MAX 96u

/* -----------------------------------------------------------------------
** Operation types
** ----------------------------------------------------------------------- */
typedef enum {
    FS_INTERNAL_OP_NONE,
    FS_INTERNAL_OP_LOAD_KIT,
    FS_INTERNAL_OP_SAVE_KIT,
    FS_INTERNAL_OP_LOAD_MORPH,
    FS_INTERNAL_OP_SAVE_MORPH,
    FS_INTERNAL_OP_LOAD_PATTERN,
    FS_INTERNAL_OP_SAVE_PATTERN,
    FS_INTERNAL_OP_LOAD_ALL,
    FS_INTERNAL_OP_SAVE_ALL,
    FS_INTERNAL_OP_LOAD_PERFORMANCE,
    FS_INTERNAL_OP_SAVE_PERFORMANCE,
    FS_INTERNAL_OP_LOAD_GLOBALS,
    FS_INTERNAL_OP_SAVE_GLOBALS,
    FS_INTERNAL_OP_SCAN_KITS,
    FS_INTERNAL_OP_LOAD_NAME,
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
    { FS_FILE_KIT,         ".snd", NULL,      1, 1, 1, 1 },
    { FS_FILE_MORPH,       ".snd", NULL,      1, 1, 1, 1 },
    { FS_FILE_PATTERN,     ".pat", NULL,      1, 1, 1, 1 },
    { FS_FILE_PERFORMANCE, ".prf", NULL,      1, 1, 1, 1 },
    { FS_FILE_ALL,         ".all", NULL,      1, 1, 1, 1 },
    { FS_FILE_GLOBALS,     NULL,   "glo.cfg", 0, 0, 1, 1 },
    { FS_FILE_SAMPLES,     NULL,   NULL,      0, 0, 1, 0 },
};

/* -----------------------------------------------------------------------
** State
** ----------------------------------------------------------------------- */
static fs_internal_op_t current_op   = FS_INTERNAL_OP_NONE;
static fs_status_t      status       = FS_STATUS_IDLE;
static uint8_t          op_phase     = 0;
static uint8_t          op_slot      = 0;
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
 * NUM_PARAMS=275, so worst case is currently 275 bytes. Use 320 for margin. */
static uint8_t staging_buf[320];
static uint16_t staging_len = 0;

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
static afatfsFilePtr_t op_kit_root_dir = NULL;
static afatfsFilePtr_t op_kit_slot_dir = NULL;
static afatfsFinder_t op_finder;
static char op_lfn_name[FS_KIT_LFN_MAX];
static uint8_t op_lfn_valid = 0;
static char op_line_buf[FS_TEXT_LINE_MAX];
static uint8_t op_line_len = 0;
static storage_kitset_t op_kitset;
static storage_instrument_state_t op_instrument_state;
static uint8_t op_instrument_slot = 0;
static uint32_t op_stream_index = 0;
static uint8_t op_item_offset = 0;
static uint8_t op_loaded_active_pattern_running = 0;
static uint8_t op_file_version = 0;
static fs_mount_result_t fs_last_mount_result = FS_MOUNT_RESULT_UNKNOWN;
static uint8_t fs_boot_detected_unsupported_card = 0;
static fs_stale_warning_source_t fs_stale_warning_pending = FS_STALE_WARNING_NONE;

#define FS_IDLE_POLL_MS 5u
/* Session 025 keeps glo.cfg/.all raw and unversioned. The only explicitly
** compatible historical globals payload is the original LXR/LXR-master
** 22-byte span; the current span is derived from NUM_PARAMS below. */
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
extern uint8_t kb_map[];
extern uint8_t kb_numKits;

/* Apply FAT NT lowercase flags to a short 8.3 filename.
 *
 * Input: mutable NUL-terminated short filename plus ntReserved flags from the
 * FAT directory entry. Output: filename edited in place. Clients: sample scan
 * and Kit/ scan code after fat_convertFATStyleToFilename().
 */
static void filesystem_applyFatShortNameCase(char *filename, uint8_t ntReserved);

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

static bool filesystem_makeFilename(char *buf, fs_file_type_t type, uint8_t num)
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
    buf[1] = '0' + (num / 100);
    buf[2] = '0' + ((num / 10) % 10);
    buf[3] = '0' + (num % 10);
    for (i = 0; desc->extension[i] && i < 4u; i++)
        buf[4u + i] = desc->extension[i];
    buf[4u + i] = '\0';
    return true;
}

static uint8_t filesystem_morphSaveUsesBase(uint16_t index)
{
    if (index >= PAR_VEL_DEST_1 && index <= PAR_VEL_DEST_6)
        return 1;
    if (index >= PAR_VOICE_LFO1 && index <= PAR_VOICE_LFO6)
        return 1;
    if (index >= PAR_TARGET_LFO1 && index <= PAR_TARGET_LFO6)
        return 1;
    return 0;
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

static uint8_t filesystem_defaultTrackMidiChannel(uint8_t track)
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
    step->volume = buf[0];
    step->prob = buf[1];
    step->note = buf[2];
    step->param1Nr = (instrument_param_id_t)buf[3] |
                     ((instrument_param_id_t)buf[4] << 8);
    step->param1Val = buf[5];
    step->param2Nr = (instrument_param_id_t)buf[6] |
                     ((instrument_param_id_t)buf[7] << 8);
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

static void filesystem_finish(fs_status_t final_status)
{
    status = final_status;
    current_op = FS_INTERNAL_OP_NONE;
    if (completion_callback) {
        fs_completion_cb_t cb = completion_callback;
        completion_callback = NULL;
        cb();
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
static void filesystem_setLoadedNameFromKitSlot(uint8_t slot)
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
 * a valid 001..128 slot number and has some non-empty tail. Client:
 * filesystem_recordKitDirectory() after normal LFN parsing fails.
 */
static void filesystem_recordKitShortAlias(const char *open_name)
{
    uint16_t number;
    uint8_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN];

    if (open_name[0] < '0' || open_name[0] > '9' ||
        open_name[1] < '0' || open_name[1] > '9' ||
        open_name[2] < '0' || open_name[2] > '9') {
        return;
    }

    number = (uint16_t)((uint16_t)(open_name[0] - '0') * 100u +
                        (uint16_t)(open_name[1] - '0') * 10u +
                        (uint16_t)(open_name[2] - '0'));
    if (number == 0u || number > STORAGE_KIT_MAX_SLOTS ||
        open_name[3] == '\0') {
        return;
    }

    slot = (uint8_t)(number - 1u);
    storage_copyDisplayName(display, open_name + 3u);
    kit_slot_present[slot] = 1u;
    memcpy(kit_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    kit_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(kit_slot_open_name[slot], open_name);

    if (kb_numKits < KITBROWSER_MAX_KITS)
        kb_map[kb_numKits++] = slot;
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
    uint8_t slot;
    char display[STORAGE_KIT_DISPLAY_NAME_LEN];

    if (!storage_parseNumberedFolder(display_name, &slot, display)) {
        filesystem_recordKitShortAlias(open_name);
        return;
    }

    kit_slot_present[slot] = 1u;
    memcpy(kit_slot_name[slot], display, STORAGE_KIT_DISPLAY_NAME_LEN);
    kit_slot_name[slot][STORAGE_KIT_DISPLAY_NAME_LEN] = '\0';
    storage_copyFilename(kit_slot_open_name[slot], open_name);

    if (kb_numKits < KITBROWSER_MAX_KITS)
        kb_map[kb_numKits++] = slot;
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
** parameter_values[] receives slot routing and instrument [params] data,
** parameters2[] receives [morph] data or the main-to-morph fallback, and
** preset_currentName receives the kit display name, "Empty   ", or "-       ".
**
** Affiliates/clients: preset_loadDrumset(..., morph=0) requests
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
        parameter_values[PAR_VOICE_DECIMATION_ALL] = 127u;
        if (!afatfs_chdir(NULL))
            return;
        op_phase = 1;
        return;

    case 1: /* OPEN Kit/ */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(STORAGE_ROOT_KIT, "r", on_file_opened))
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
            st = storage_kitsetParseLine(
                &op_kitset, op_line_buf,
                &scene_get(scene_getActiveIndex())->kit);
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
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
            memcpy(preset_currentName, kit_slot_name[op_slot], 8);
            op_close_status = FS_STATUS_DONE;
            op_phase = 28;
            return;
        }
        storage_instrumentStateInit(&op_instrument_state,
                                    op_kitset.instrument_type[op_instrument_slot],
                                    (uint8_t)(op_instrument_slot + 1u));
        instrumentManager_resetSlot(
            scene_instrumentSlot(scene_getActiveIndex(), op_instrument_slot),
            op_kitset.instrument_type[op_instrument_slot]);
        op_line_len = 0u;
        op_phase = 17;
        return;

    case 17: /* OPEN INSTRUMENT */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(op_kitset.instrument_file[op_instrument_slot],
                          "r",
                          on_file_opened)) {
            return;
        }
        op_phase = 18;
        return;

    case 18: /* WAIT INSTRUMENT */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_setPresetNameInvalid();
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
            op_close_status = FS_STATUS_ERROR;
            op_phase = 20;
            return;
        }
        if (line_ready) {
            st = storage_instrumentParseLine(&op_instrument_state,
                                             op_line_buf,
                                             scene_instrumentSlot(
                                                 scene_getActiveIndex(),
                                                 op_instrument_slot));
            if (st != STORAGE_STATUS_OK) {
                filesystem_setPresetNameInvalid();
                op_close_status = FS_STATUS_ERROR;
                op_phase = 20;
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
                        scene_instrumentSlot(scene_getActiveIndex(),
                                             op_instrument_slot));
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
** SAVE KIT state machine
**
** Phases: 0=stage, 1=open, 2=wait_open, 3=write, 4=close,
**         5=wait_close, 6=done
** ----------------------------------------------------------------------- */
static void filesystem_saveKit_tick(void)
{
    uint16_t i;

    switch (op_phase) {
    case 0: /* STAGE - copy name + params into staging buffer */
        memcpy(staging_buf, preset_currentName, 8);
        if (current_op == FS_INTERNAL_OP_SAVE_MORPH) {
            for (i = 0; i < END_OF_SOUND_PARAMETERS; i++) {
                staging_buf[8u + i] = filesystem_morphSaveUsesBase(i)
                    ? parameter_values[i]
                    : preset_getMorphValue(i, parameter_values[PAR_MORPH]);
            }
        } else {
            memcpy(staging_buf + 8, parameter_values, END_OF_SOUND_PARAMETERS);
        }
        staging_len = 8 + END_OF_SOUND_PARAMETERS;
        op_phase = 1;
        return;

    case 1: /* OPEN */
    {
        char fname[13];
        if (!filesystem_makeFilename(fname, op_file_type, op_slot)) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen(fname, "w", on_file_opened)) {
            return;  /* retry next tick */
        }
        op_phase = 2;
        return;
    }

    case 2: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 3;
        op_bytes_done = 0;
        return;

    case 3: /* WRITE */
    {
        uint32_t n = afatfs_fwrite(op_file,
                                   staging_buf + op_bytes_done,
                                   staging_len - op_bytes_done);
        op_bytes_done += n;
        if (op_bytes_done >= staging_len) {
            op_phase = 4;
        }
        return;
    }

    case 4: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT_CLOSE */
        if (!op_close_done) return;
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
** LOAD GLOBALS state machine
**
** Phases: 0=open, 1=wait_open, 2=read, 3=close, 4=wait_close, 5=apply
** ----------------------------------------------------------------------- */
static void filesystem_loadGlobals_tick(void)
{
    switch (op_phase) {
    case 0: /* OPEN */
        op_file_ready = false;
        op_file = NULL;
        fs_stale_warning_pending = FS_STALE_WARNING_NONE;
        if (!afatfs_fopen("glo.cfg", "r", on_file_opened))
            return;
        op_phase = 1;
        return;

    case 1: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            /* No globals file - not an error, just skip */
            filesystem_finish(FS_STATUS_DONE);
            return;
        }
        op_phase = 2;
        op_bytes_done = 0;
        op_stream_index = 0;
        return;

    case 2: /* READ + validate length */
    {
        uint16_t globals_len = (uint16_t)(NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS);
        uint32_t n = 0;

        /* Read one byte past the current expected length so oversized glo.cfg
        ** files are detected as stale instead of silently truncating. Exact
        ** current length loads normally; exact legacy-22 loads silently with
        ** compatibility overrides; every other length warns. */
        if (op_stream_index <= globals_len) {
            uint8_t byte = 0u;
            n = afatfs_fread(op_file, &byte, 1u);
            if (n > 0u) {
                if (op_stream_index < sizeof(staging_buf))
                    staging_buf[op_stream_index] = byte;
                op_stream_index++;
            }
        }

        if (op_stream_index > globals_len) {
            filesystem_applyStaleGlobalsFallback(staging_buf, (uint16_t)op_stream_index);
            fs_stale_warning_pending = FS_STALE_WARNING_GLO;
            op_phase = 3;
        } else if (n == 0u && afatfs_feof(op_file)) {
            if (op_stream_index == globals_len) {
                filesystem_applyGlobalsPrefix(staging_buf, globals_len);
            } else if (op_stream_index == FS_GLOBALS_LEGACY_LEN_22) {
                /* Legacy 22-byte globals: load silently with compatibility
                ** overrides, no stale-warning screen. */
                filesystem_applyLegacy22Globals(staging_buf, (uint16_t)op_stream_index);
            } else {
                filesystem_applyStaleGlobalsFallback(staging_buf, (uint16_t)op_stream_index);
                fs_stale_warning_pending = FS_STALE_WARNING_GLO;
            }
            op_phase = 3;
        }
        return;
    }

    case 3: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 4;
        return;

    case 4: /* WAIT_CLOSE */
        if (!op_close_done) return;
        /* Data is in parameter_values[]. Globals apply happens in
        ** menu layer when it sees UPDATE_READY. */
        filesystem_finish(FS_STATUS_DONE);
        return;

    default:
        filesystem_finish(FS_STATUS_ERROR);
        return;
    }
}

/* -----------------------------------------------------------------------
** SAVE GLOBALS state machine
**
** Phases: 0=stage, 1=open, 2=wait_open, 3=write, 4=close, 5=wait_close
** ----------------------------------------------------------------------- */
static void filesystem_saveGlobals_tick(void)
{
    switch (op_phase) {
    case 0: /* STAGE */
    {
        uint16_t total = NUM_PARAMS - PAR_BEGINNING_OF_GLOBALS;
        memcpy(staging_buf, parameter_values + PAR_BEGINNING_OF_GLOBALS, total);
        staging_len = total;
        op_phase = 1;
        return;
    }

    case 1: /* OPEN */
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen("glo.cfg", "w", on_file_opened))
            return;
        op_phase = 2;
        return;

    case 2: /* WAIT_OPEN */
        if (!op_file_ready) return;
        if (op_file == NULL) {
            filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_phase = 3;
        op_bytes_done = 0;
        return;

    case 3: /* WRITE */
    {
        uint32_t n = afatfs_fwrite(op_file,
                                   staging_buf + op_bytes_done,
                                   staging_len - op_bytes_done);
        op_bytes_done += n;
        if (op_bytes_done >= staging_len)
            op_phase = 4;
        return;
    }

    case 4: /* CLOSE */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = 5;
        return;

    case 5: /* WAIT_CLOSE */
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
** root Kit/ directory, walks every FAT entry, reconstructs any LFN display name,
** accepts only numbered folders via storage_parseNumberedFolder(), and caches
** both the user display name and short open name.
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
        if (!afatfs_fopen(STORAGE_ROOT_KIT, "r", on_file_opened))
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
            filesystem_applyFatShortNameCase(short_name, entry->ntReserved);

            display_name = op_lfn_valid ? op_lfn_name : short_name;
            filesystem_recordKitDirectory(display_name, short_name);
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

static void filesystem_applyFatShortNameCase(char *filename, uint8_t ntReserved)
{
    if (ntReserved & 0x08u) {
        for (uint8_t i = 0; filename[i] != '\0' && filename[i] != '.'; i++) {
            if (filename[i] >= 'A' && filename[i] <= 'Z')
                filename[i] = (char)(filename[i] + ('a' - 'A'));
        }
    }

    if (ntReserved & 0x10u) {
        uint8_t i = 0;
        while (filename[i] != '\0' && filename[i] != '.')
            i++;
        if (filename[i] == '.')
            i++;
        while (filename[i] != '\0') {
            if (filename[i] >= 'A' && filename[i] <= 'Z')
                filename[i] = (char)(filename[i] + ('a' - 'A'));
            i++;
        }
    }
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
        filesystem_applyFatShortNameCase(filename, entry->ntReserved);
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

/* =======================================================================
** Public API
** ======================================================================= */

void filesystem_initAfterCardReady(void)
{
    current_op = FS_INTERNAL_OP_NONE;
    status = FS_STATUS_IDLE;
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
    case FS_INTERNAL_OP_LOAD_KIT:
        filesystem_loadKitDirectory_tick();
        break;
    case FS_INTERNAL_OP_LOAD_MORPH:
        filesystem_loadKit_tick();
        break;
    case FS_INTERNAL_OP_SAVE_KIT:
    case FS_INTERNAL_OP_SAVE_MORPH:
        filesystem_saveKit_tick();
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
                             uint8_t slot, fs_completion_cb_t cb)
{
    if (status == FS_STATUS_BUSY) return false;
    status = FS_STATUS_BUSY;
    current_op = op;
    op_phase = 0;
    op_slot = slot;
    op_file_type = type;
    op_file = NULL;
    op_file_ready = false;
    op_close_done = false;
    op_close_status = FS_STATUS_DONE;
    op_bytes_done = 0;
    op_stream_index = 0;
    op_item_offset = 0;
    op_loaded_active_pattern_running = 0;
    op_file_version = 0;
    op_kit_root_dir = NULL;
    op_kit_slot_dir = NULL;
    op_lfn_valid = 0;
    op_line_len = 0;
    op_instrument_slot = 0;
    completion_callback = cb;
    return true;
}

bool filesystem_requestLoad(fs_file_type_t type, uint8_t slot, fs_completion_cb_t cb)
{
    const fs_file_desc_t *desc = filesystem_desc(type);
    if (desc == NULL || !desc->supports_load)
        return false;

    switch (type) {
    case FS_FILE_KIT:
        return filesystem_start(FS_INTERNAL_OP_LOAD_KIT, type, slot, cb);
    case FS_FILE_MORPH:
        return filesystem_start(FS_INTERNAL_OP_LOAD_MORPH, type, slot, cb);
    case FS_FILE_PATTERN:
        return filesystem_start(FS_INTERNAL_OP_LOAD_PATTERN, type, slot, cb);
    case FS_FILE_ALL:
        return filesystem_start(FS_INTERNAL_OP_LOAD_ALL, type, slot, cb);
    case FS_FILE_PERFORMANCE:
        return filesystem_start(FS_INTERNAL_OP_LOAD_PERFORMANCE, type, slot, cb);
    case FS_FILE_GLOBALS:
        return filesystem_start(FS_INTERNAL_OP_LOAD_GLOBALS, type, 0, cb);
    default:
        return false;
    }
}

bool filesystem_requestSave(fs_file_type_t type, uint8_t slot, fs_completion_cb_t cb)
{
    const fs_file_desc_t *desc = filesystem_desc(type);
    if (desc == NULL || !desc->supports_save)
        return false;

    switch (type) {
    case FS_FILE_KIT:
        return filesystem_start(FS_INTERNAL_OP_SAVE_KIT, type, slot, cb);
    case FS_FILE_MORPH:
        return filesystem_start(FS_INTERNAL_OP_SAVE_MORPH, type, slot, cb);
    case FS_FILE_PATTERN:
        return filesystem_start(FS_INTERNAL_OP_SAVE_PATTERN, type, slot, cb);
    case FS_FILE_ALL:
        return filesystem_start(FS_INTERNAL_OP_SAVE_ALL, type, slot, cb);
    case FS_FILE_PERFORMANCE:
        return filesystem_start(FS_INTERNAL_OP_SAVE_PERFORMANCE, type, slot, cb);
    case FS_FILE_GLOBALS:
        return filesystem_start(FS_INTERNAL_OP_SAVE_GLOBALS, type, 0, cb);
    default:
        return false;
    }
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

bool filesystem_requestLoadName(fs_file_type_t type, uint8_t slot, fs_completion_cb_t cb)
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

/* Query whether the most recent Kit/ scan found a numbered folder.
 *
 * Input: zero-based slot used by menu/preset code. Output: nonzero if a
 * Kit/NNN Name folder exists. Clients: filesystem_kitSlotName() and future UI
 * code that wants to distinguish absent slots from malformed present kits.
 */
uint8_t filesystem_kitSlotExists(uint8_t zero_based_slot)
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
const char *filesystem_kitSlotName(uint8_t zero_based_slot)
{
    if (!filesystem_kitSlotExists(zero_based_slot))
        return "Empty   ";
    return kit_slot_name[zero_based_slot];
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
