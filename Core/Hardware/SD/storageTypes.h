/*
 * Core/Hardware/SD/storageTypes.h
 *
 * Phase 2 storage-format helpers.
 *
 * This layer exists so filesystem.c can remain an SD access/state-machine
 * module instead of also becoming the owner of on-card text schemas. The
 * functions here know how Kit/NNN Name folders, kitset.kcg, and individual
 * instrument files are represented in memory, but they deliberately do not
 * open files, change directories, or call asyncfatfs.
 *
 * Inputs are complete, NUL-terminated text lines and names supplied by
 * filesystem.c. Outputs are storage_status_t validation results, sanitized
 * fixed-width names, kit/instrument parse state, and writes into a caller-owned
 * Scene kit record. Clients must call the Init(), ParseLine(), and Finalize()
 * functions in that order.
 *
 * Affiliates: Core/Hardware/SD/filesystem.c is the runtime client; the
 * generated SD_CARD/Kit tree and tools/convert_legacy_kits.py must stay in
 * sync with this schema and the canonical descriptors in InstrumentManager.
 */
#ifndef STORAGETYPES_H_
#define STORAGETYPES_H_

#include <stdint.h>
#include "InstrumentManager.h"
#include "SceneData.h"

/* Public storage constants for the Phase 2 kit directory loader.
 *
 * STORAGE_ROOT_KIT and STORAGE_KITSET_FILENAME are the literal on-card names
 * that filesystem.c opens. STORAGE_KIT_SLOT_COUNT is the synth voice count in
 * one kit. STORAGE_KIT_MAX_SLOTS is the number of numbered Kit/ folders the
 * browser exposes. STORAGE_KIT_FILENAME_MAX is 8.3 plus NUL because asyncfatfs
 * opens short names. STORAGE_KIT_DISPLAY_NAME_LEN mirrors the existing LCD and
 * preset name buffers, which are exactly eight printable characters.
 */
#define STORAGE_ROOT_KIT              "Kit"
#define STORAGE_ROOT_INSTRUMENT       "Instrument"
#define STORAGE_KITSET_FILENAME       "kitset.kcg"
#define STORAGE_KIT_SLOT_COUNT        6u
#define STORAGE_KIT_MAX_SLOTS         128u
#define STORAGE_KIT_FILENAME_MAX      13u
#define STORAGE_KIT_DISPLAY_NAME_LEN  8u

/* Result codes returned by every parser/validator in this layer.
 *
 * OK means the line or object was accepted. WAIT is reserved for clients that
 * share status plumbing with async readers; storageTypes itself never blocks.
 * The remaining values identify schema, version, type, slot, value, required
 * field, or text length failures so filesystem.c can mark a kit invalid without
 * guessing where parsing failed.
 */
typedef enum {
    STORAGE_STATUS_OK = 0,
    STORAGE_STATUS_WAIT,
    STORAGE_STATUS_INVALID_FORMAT,
    STORAGE_STATUS_UNSUPPORTED_VERSION,
    STORAGE_STATUS_BAD_TYPE,
    STORAGE_STATUS_BAD_SLOT,
    STORAGE_STATUS_BAD_VALUE,
    STORAGE_STATUS_MISSING_REQUIRED,
    STORAGE_STATUS_LINE_TOO_LONG,
} storage_status_t;

/* Instrument type stored in kitset.kcg and instrument file extensions.
 *
 * These values are intentionally format-level types, not voice numbers. The
 * parser combines a type with the slot number supplied by kitset.kcg to select
 * the correct descriptor table. UNKNOWN is used during initialization and for
 * rejecting unsupported future extensions.
 */
typedef instrument_type_t storage_instrument_type_t;
#define STORAGE_INSTRUMENT_DRM     INSTRUMENT_TYPE_DRM
#define STORAGE_INSTRUMENT_SNR     INSTRUMENT_TYPE_SNR
#define STORAGE_INSTRUMENT_CYM     INSTRUMENT_TYPE_CYM
#define STORAGE_INSTRUMENT_HAT     INSTRUMENT_TYPE_HAT
#define STORAGE_INSTRUMENT_UNKNOWN INSTRUMENT_TYPE_UNKNOWN

/* Incremental parse state for kitset.kcg.
 *
 * The kitset file is the guard that proves a Kit/NNN Name directory is a kit
 * and lists the six instrument files to load. The kit display name is owned by
 * the numbered folder name, not by kitset.kcg. instrument_file/type are filled
 * per slot. current_slot is changed by [slotN] section headers. seen_* masks
 * record required fields so storage_kitsetFinalize() can reject partial or
 * hand-edited files.
 *
 * Inputs arrive through storage_kitsetParseLine(). Outputs are this struct and
 * writes to scene->kit.settings.audio_out[]. Clients are
 * filesystem_loadKitDirectory_tick() in filesystem.c and the generated
 * kitset.kcg files.
 */
typedef struct {
    char instrument_file[STORAGE_KIT_SLOT_COUNT][STORAGE_KIT_FILENAME_MAX];
    storage_instrument_type_t instrument_type[STORAGE_KIT_SLOT_COUNT];
    uint8_t current_slot;
    uint8_t seen_format;
    uint8_t seen_version;
    uint8_t seen_type_mask;
    uint8_t seen_file_mask;
    uint8_t seen_audio_out_mask;
} storage_kitset_t;

/* Incremental parse state for one instrument file.
 *
 * expected_type and expected_slot come from the already-validated kitset entry.
 * The type is checked against the instrument header, while the slot selects the
 * instrument record for the file named by kitset.kcg. current_section tracks
 * top metadata, [params], and [morph]. seen_param_count proves at least one
 * primary parameter landed. seen_morph_count tells filesystem.c whether an
 * explicit morph endpoint was loaded or whether it must copy the main image
 * into the morph image as the Phase 2 fallback.
 *
 * Inputs arrive through storage_instrumentParseLine(). Outputs are validation
 * flags plus writes into kit_instrument_slot_t. Clients are the
 * directory kit loader and future save code that will emit the same sections.
 */
typedef struct {
    storage_instrument_type_t expected_type;
    uint8_t expected_slot;
    uint8_t current_section;
    uint8_t seen_format;
    uint8_t seen_version;
    uint8_t seen_type;
    uint8_t seen_param_count;
    uint8_t seen_morph_count;
} storage_instrument_state_t;

/* Initialize kitset parse state before the first line of kitset.kcg.
 *
 * Input/output: kit points to caller-owned storage_kitset_t scratch. The
 * function clears all fields and marks instrument slots as UNKNOWN. Client:
 * filesystem_loadKitDirectory_tick().
 */
void storage_kitsetInit(storage_kitset_t *kit);

/* Parse one complete line from kitset.kcg.
 *
 * Inputs: kit is the state initialized by storage_kitsetInit(); line is one
 * NUL-terminated line with CR/LF already removed; target_kit is the Scene kit
 * being assembled. Outputs: updated parse state and audio routing settings.
 * Unknown keys are ignored for forward compatibility;
 * malformed required keys return an error status.
 */
storage_status_t storage_kitsetParseLine(storage_kitset_t *kit,
                                         const char *line,
                                         kit_t *target_kit);

/* Validate that all required kitset.kcg fields were present and coherent.
 *
 * Input: kit parse state after EOF. Output: OK or an error status. This checks
 * the file guard/version, all six type/file/audio_out fields, and that listed
 * filenames have extensions matching their declared instrument type.
 */
storage_status_t storage_kitsetFinalize(const storage_kitset_t *kit);

/* Initialize one instrument parser before reading an instrument file.
 *
 * Inputs: type and one-based slot from storage_kitset_t. Output: cleared parser
 * state that expects that exact type/slot header. Client:
 * filesystem_loadKitDirectory_tick() calls this once for each of the six files.
 */
void storage_instrumentStateInit(storage_instrument_state_t *state,
                                 storage_instrument_type_t type,
                                 uint8_t slot);

/* Parse one complete line from an instrument file.
 *
 * Inputs: state, a NUL-terminated line, and the destination kit instrument
 * slot. Outputs: descriptor-indexed writes to generic main or morph storage,
 * plus validation flags in state. LFO target voice is clamped into the valid
 * 1..6 Scene-domain range while parsing
 * because legacy converted files can carry the old zero placeholder. Unknown
 * keys are ignored so future saves can add fields older firmware does not
 * understand. Clients are filesystem_loadKitDirectory_tick() and Preset's
 * later Scene-to-runtime apply bridge.
 */
storage_status_t storage_instrumentParseLine(storage_instrument_state_t *state,
                                             const char *line,
                                             kit_instrument_slot_t *slot);

/* Validate required instrument metadata after EOF.
 *
 * Input: state after all lines were parsed. Output: OK only if the file guard,
 * version, type, and at least one [params] value were seen. Morph data is
 * optional during this load pass because old converted kits do not carry it.
 */
storage_status_t storage_instrumentFinalize(const storage_instrument_state_t *state);

/* Phase 2 morph fallback for instrument files without a [morph] section.
 *
 * Inputs: format type, one-based slot, and the already-loaded instrument
 * record. Output: every descriptor-owned main image value is copied to the
 * corresponding morph image value. Client:
 * filesystem_loadKitDirectory_tick() calls this when seen_morph_count is zero.
 */
void storage_instrumentCopyMainToMorphFallback(storage_instrument_type_t type,
                                               uint8_t slot,
                                               kit_instrument_slot_t *instrument);

/* Convert a kitset/instrument type token such as "drm" into the enum above.
 *
 * Input: lowercase schema text. Output: a storage_instrument_type_t, or
 * STORAGE_INSTRUMENT_UNKNOWN for unsupported strings. Clients: kitset and
 * instrument metadata parsers.
 */
storage_instrument_type_t storage_instrumentTypeFromText(const char *text);

/* Check that an instrument filename extension agrees with its declared type.
 *
 * Inputs: short 8.3 filename from kitset.kcg and expected type. Output: nonzero
 * when extensions match (.drm/.snr/.cym/.hat). Client:
 * storage_kitsetFinalize(), which prevents an obviously wrong file from being
 * loaded into a slot before filesystem.c opens it.
 */
uint8_t storage_instrumentFilenameMatchesType(const char *filename,
                                              storage_instrument_type_t type);

/* Parse a numbered folder name like "001 Slak" into internal slot/name data.
 *
 * Inputs: display/LFN folder name, zero_based_slot output pointer, and an
 * eight-char display buffer. The name must begin with a three-digit 001..128
 * slot ID followed by at least one space or underscore separator; additional
 * spaces/underscores before the visible name are skipped. Spaces inside the
 * visible eight-character name are preserved. Outputs: nonzero on a valid
 * prefix/name, zero-based slot for menu/preset code, and sanitized/padded
 * display text. Client: filesystem_recordKitDirectory() during Kit/ scans.
 */
uint8_t storage_parseNumberedFolder(const char *name,
                                    uint8_t *zero_based_slot,
                                    char display[STORAGE_KIT_DISPLAY_NAME_LEN]);

/* Copy arbitrary text into the firmware's fixed eight-character name format.
 *
 * Input: src may be shorter than eight characters. Output: dst is filled with
 * printable ASCII, padded with spaces, and not NUL-terminated. Clients:
 * kitset parsing and numbered-folder parsing.
 */
void storage_copyDisplayName(char dst[STORAGE_KIT_DISPLAY_NAME_LEN],
                             const char *src);

/* Copy an asyncfatfs-openable short filename into fixed scratch storage.
 *
 * Input: src is expected to be 8.3-compatible text. Output: dst is
 * NUL-terminated and truncated to STORAGE_KIT_FILENAME_MAX - 1 if needed.
 * Clients: kitset parsing and filesystem's scanned short-name cache.
 */
void storage_copyFilename(char dst[STORAGE_KIT_FILENAME_MAX],
                          const char *src);

#endif /* STORAGETYPES_H_ */
