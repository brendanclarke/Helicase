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
 * Root directory literals are exact display components.
 *
 * asyncfatfs now preserves and matches case through SFN case bits and VFAT LFN
 * entries, so production code asks for "Instrument" rather than the old
 * compatibility alias "INSTRU~1". Callers that need to open these roots should
 * use the LFN-aware directory APIs when the operation is part of the new
 * production storage surface.
 *
 * System file literals are written in their intended display case. asyncfatfs
 * preserves all-lowercase 8.3 names through FAT ntReserved case bits, so
 * callers should not uppercase these constants to match raw SFN storage.
 */
#define STORAGE_ROOT_KIT              "Kit"
#define STORAGE_ROOT_SCENE            "Scene"
#define STORAGE_ROOT_INSTRUMENT       "Instrument"
#define STORAGE_KITSET_FILENAME       "kitset.kcg"
#define STORAGE_SCENESET_FILENAME     "sceneset.scg"
#define STORAGE_KIT_SLOT_COUNT        6u
/*
 * Kit and Scene folders are numbered directory entries, not legacy file slots.
 *
 * The old 128 limit came from P000.SND..P127.SND. New-format Kit/ folders are
 * now addressed by a three-digit 000..999 prefix, so the storage boundary
 * exposes 1000 slots and callers that hold library positions must use
 * uint16_t. Slot 000 is a real save/load slot, not an empty sentinel.
 */
#define STORAGE_KIT_MAX_SLOTS         1000u
#define STORAGE_SCENE_MAX_SLOTS       1000u
#define STORAGE_KIT_FILENAME_MAX      13u
/*
 * Maximum Kit member filename stored in kitset.kcg.
 *
 * What: Allows kitset.kcg to store the user-visible LFN component generated
 * for member Instrument files, not only the returned 8.3 alias.
 *
 * Why: Instruments inside Kits have a product naming convention: the eighth
 * stem character is forced to the one-based voice number. That convention must
 * be visible in both the real member filename and the kitset reference. Short
 * aliases collapse spaces and can turn a convention-preserving display name
 * into text such as `slakd11.drm`, so the schema field needs LFN-sized storage.
 *
 * Inputs/outputs: parser and formatter buffers for `file=` lines. The value
 * mirrors asyncfatfs' current single-component LFN limit without making this
 * storage-format layer call asyncfatfs directly.
 *
 * Affiliates/clients: storage_kitset_t and filesystem_loadKitDirectory_tick().
 */
#define STORAGE_KIT_MEMBER_FILENAME_MAX 49u
#define STORAGE_KIT_DISPLAY_NAME_LEN  8u
#define STORAGE_SCENE_DISPLAY_NAME_LEN STORAGE_KIT_DISPLAY_NAME_LEN

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
    char instrument_file[STORAGE_KIT_SLOT_COUNT]
                        [STORAGE_KIT_MEMBER_FILENAME_MAX];
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

/*
 * Incremental parse state for sceneset.scg.
 *
 * The file validates a Scene folder and stores Scene-level settings only. It
 * deliberately does not store the embedded Kit directory name: Scene loading
 * discovers the first valid "Kit *" directory in the folder and uses the text
 * after "Kit " as the loaded Kit name. Inputs arrive one NUL-terminated line
 * at a time. Outputs are validation bits, a caller-owned eight-character Scene
 * display name, and writes into scene_t::settings. Clients are future
 * filesystem Scene load/save state machines and SD_CARD fixture generators.
 */
typedef struct {
    uint8_t seen_format;
    uint8_t seen_version;
    uint8_t seen_name;
} storage_sceneset_t;

/*
 * Incremental validation state for placeholder effect files.
 *
 * Real effect storage is future DSP work. Scene folders still always contain
 * an effect file, so the first pass accepts a tiny guarded placeholder:
 * format=helicase.effect, version=1, placeholder=1. Inputs are text lines from
 * filesystem.c; outputs are validation bits used to accept or reject the first
 * discovered .fx file.
 */
typedef struct {
    uint8_t seen_format;
    uint8_t seen_version;
    uint8_t seen_placeholder;
} storage_effect_state_t;

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
 * plus validation flags in state. LFO target voices are clamped into the valid
 * 1..6 Scene-domain range while parsing because legacy converted files can
 * carry the old zero placeholder.
 *
 * The file-only token "self" is accepted only for lfo_target_voice and
 * lfo_target_voice_2. It resolves through state->expected_slot before writing
 * Scene-owned descriptor images, so every caller after storage sees the same
 * numeric 1..6 selector it already handles today. Unknown keys are ignored so
 * future saves can add fields older firmware does not understand. Clients are
 * filesystem_loadKitDirectory_tick() and Preset's later Scene-to-runtime apply
 * bridge.
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
 * Inputs: member filename from kitset.kcg and expected type. Output: nonzero
 * when extensions match (.drm/.snr/.cym/.hat), whether the filename is a
 * convention-preserving LFN display component or an older 8.3 alias. Client:
 * storage_kitsetFinalize(), which prevents an obviously wrong file from being
 * loaded into a slot before filesystem.c opens it.
 */
uint8_t storage_instrumentFilenameMatchesType(const char *filename,
                                              storage_instrument_type_t type);

/*
 * Initialize, parse, and finalize sceneset.scg.
 *
 * Inputs: parser state, one text line at a time, optional target Scene, and a
 * fixed display-name buffer. Outputs: required guard/name bits plus retained
 * Scene settings. Missing optional settings leave the caller's defaults in
 * place, so filesystem should initialize the staged Scene before parsing.
 */
void storage_scenesetInit(storage_sceneset_t *state);
storage_status_t storage_scenesetParseLine(
    storage_sceneset_t *state,
    const char *line,
    scene_t *target_scene,
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN]);
storage_status_t storage_scenesetFinalize(const storage_sceneset_t *state);

/* Parse a numbered folder name like "000 Slak" into internal slot/name data.
 *
 * Inputs: display/LFN folder name, slot output pointer, and an eight-char
 * display buffer. The name must begin with a three-digit 000..999 slot ID
 * followed by at least one space or underscore separator; additional
 * spaces/underscores before the visible name are skipped. Spaces inside the
 * visible eight-character name are preserved. Outputs: nonzero on a valid
 * prefix/name, direct 0..999 slot for menu/preset code, and sanitized/padded
 * display text. Slot 000 is a real library slot, not a sentinel. Client:
 * filesystem_recordKitDirectory() during Kit/ scans.
 */
uint8_t storage_parseNumberedFolder(const char *name,
                                    uint16_t *slot,
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
/*
 * Copy a kitset.kcg member Instrument filename.
 *
 * Input: src is a single FAT display component from a `file=` line or a save
 * generator. Output: dst is NUL-terminated and large enough for the
 * convention-preserving LFN member name. Clients use this for Kit member
 * references, while storage_copyFilename() remains the short-alias helper.
 */
void storage_copyKitMemberFilename(
    char dst[STORAGE_KIT_MEMBER_FILENAME_MAX],
    const char *src);

/*
 * Convert registry instrument types back into storage schema text.
 *
 * Save code needs the inverse of the parser's type lookup for kitset.kcg and
 * instrument file headers. Unknown types return NULL so filesystem can reject
 * impossible saves before creating partial Kit folder contents.
 */
const char *storage_instrumentTypeToText(storage_instrument_type_t type);

/*
 * Return the instrument filename extension for a storage type.
 *
 * The extension intentionally shares the same lowercase token as the type
 * field today, keeping saved 8.3 filenames aligned with the loader's extension
 * validator.
 */
const char *storage_instrumentTypeExtension(storage_instrument_type_t type);
/*
 * Instrument text save value projection.
 *
 * What: Selects whether storage text writers emit the resident endpoint images
 * as a normal save, or as a Morph Save projection.
 *
 * Why: normal Save and Morph Save use the same text schemas, descriptor keys,
 * section ordering, and asyncfatfs overwrite path. Their difference is only
 * the value chosen for morphable endpoint cells: Morph Save writes the current
 * interpolated value into both [params] and [morph]. Keeping the mode in
 * storageTypes lets filesystem.c sequence SD writes without learning
 * descriptor counts or morphability flags.
 *
 * Affiliates/clients: storage_instrument_write_view_t and root Instrument
 * save state machines.
 */
typedef enum {
    STORAGE_INSTRUMENT_SAVE_NORMAL = 0u,
    STORAGE_INSTRUMENT_SAVE_MORPH
} storage_instrument_save_mode_t;

/*
 * Instrument text save value view.
 *
 * What: Describes how storage_formatInstrumentLineView() should project one
 * resident instrument slot into an on-card Instrument file.
 *
 * Why: normal Save and Morph Save use the same text schema, descriptor keys,
 * self-token handling, and section ordering, but they choose different values
 * for morphable cells. In Morph Save, [params] and [morph] receive the same
 * interpolated value. Keeping the mode in storageTypes lets filesystem.c
 * sequence SD writes without learning descriptor counts or morphability flags.
 *
 * Inputs: instrument points at the Scene-owned slot image; type is the
 * registry/storage type for descriptor lookup; one_based_voice drives the
 * file-only `self` token; morph_amount is the retained per-slot Morph amount
 * used only by STORAGE_INSTRUMENT_SAVE_MORPH.
 *
 * Outputs: no state is stored in the view. The formatter reads it line by line
 * while filesystem.c owns op_write_line_index.
 *
 * Affiliates/clients: filesystem_saveInstrument_tick() and InstrumentMrp
 * Save.
 */
typedef struct {
    const kit_instrument_slot_t *instrument;
    storage_instrument_type_t type;
    uint8_t one_based_voice;
    uint8_t morph_amount;
    storage_instrument_save_mode_t mode;
} storage_instrument_write_view_t;

/*
 * Text save helpers mirror the parser-owned schema.
 *
 * Filesystem streams one line at a time, but storageTypes owns which keys are
 * emitted and how descriptor-indexed images become [params]/[morph] text.
 * Keeping writers next to parsers prevents save from drifting away from the
 * accepted load grammar.
 */
uint8_t storage_formatInstrumentLine(char *dst, uint16_t capacity,
                                     const kit_instrument_slot_t *instrument,
                                     storage_instrument_type_t type,
                                     uint8_t one_based_voice,
                                     uint16_t line_index);
uint8_t storage_formatInstrumentLineView(
    char *dst,
    uint16_t capacity,
    const storage_instrument_write_view_t *view,
    uint16_t line_index);
void storage_effectStateInit(storage_effect_state_t *state);
storage_status_t storage_effectParseLine(storage_effect_state_t *state,
                                         const char *line);
storage_status_t storage_effectFinalize(const storage_effect_state_t *state);
/*
 * Build an 8.3-safe saved instrument filename from Scene-retained metadata.
 *
 * The source stem can be longer than a FAT short alias because SceneData
 * retains the first 16 characters. This compatibility helper still sanitizes
 * to a short basename and adds a voice suffix when requested; LFN-capable save
 * paths should use storage_makeSavedInstrumentDisplayFilename() and let
 * asyncfatfs return the alias actually selected on disk.
 */
void storage_makeSavedInstrumentFilename(
    char dst[STORAGE_KIT_FILENAME_MAX],
    const char *stem,
    storage_instrument_type_t type,
    uint8_t one_based_voice,
    uint8_t force_voice_suffix);

/*
 * Build a visible Instrument member filename for LFN-capable saves.
 *
 * Inputs mirror storage_makeSavedInstrumentFilename(), but the output is a
 * user-facing long filename component rather than an 8.3 open alias. Spaces
 * and upper/lowercase ASCII are preserved where FAT permits them; invalid FAT
 * display characters are replaced with underscores. When force_voice_suffix is
 * nonzero, character 8 of the stem is the one-based voice number, padding
 * shorter stems with spaces and truncating longer stems before that cell.
 * Filesystem.c passes this display component to asyncfatfs and also writes it
 * into kitset.kcg. asyncfatfs may still return a generated 8.3 alias for its
 * own reopen/cache needs, but the schema-visible Kit member identity remains
 * the convention-preserving display filename.
 */
void storage_makeSavedInstrumentDisplayFilename(char *dst,
                                                uint8_t capacity,
                                                const char *stem,
                                                storage_instrument_type_t type,
                                                uint8_t one_based_voice,
                                                uint8_t force_voice_suffix);

#endif /* STORAGETYPES_H_ */
