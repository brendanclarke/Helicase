#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "fat_standard.h"

typedef struct afatfsFile_t *afatfsFilePtr_t;

typedef enum {
    AFATFS_FILESYSTEM_STATE_UNKNOWN,
    AFATFS_FILESYSTEM_STATE_FATAL,
    AFATFS_FILESYSTEM_STATE_INITIALIZATION,
    AFATFS_FILESYSTEM_STATE_READY,
} afatfsFilesystemState_e;

typedef enum {
    AFATFS_OPERATION_IN_PROGRESS,
    AFATFS_OPERATION_SUCCESS,
    AFATFS_OPERATION_FAILURE,
} afatfsOperationStatus_e;

typedef enum {
    AFATFS_ERROR_NONE = 0,
    AFATFS_ERROR_GENERIC = 1,
    AFATFS_ERROR_BAD_MBR = 2,
    AFATFS_ERROR_BAD_FILESYSTEM_HEADER = 3
} afatfsError_e;

typedef struct afatfsDirEntryPointer_t {
    uint32_t sectorNumberPhysical;
    int16_t entryIndex;
} afatfsDirEntryPointer_t;

typedef afatfsDirEntryPointer_t afatfsFinder_t;

/*
 * Public filename buffer sizes for asyncfatfs-created aliases.
 *
 * Why this lives in the public header: higher-level filesystem code needs a
 * stable, system-wide scratch size for the short alias returned by the new LFN
 * creation calls. AFATFS_SHORT_FILENAME_MAX is the printable 8.3 form
 * ("12345678.EXT") plus NUL. AFATFS_LONG_FILENAME_MAX bounds one path
 * component handled by the async writer; paths are still not accepted here.
 */
#define AFATFS_SHORT_FILENAME_MAX 13u
#define AFATFS_LONG_FILENAME_MAX  48u

typedef enum {
    AFATFS_MATCH_CASE_INSENSITIVE = 0,
    AFATFS_MATCH_CASE_SENSITIVE
} afatfsMatchMode_t;

typedef enum {
    AFATFS_OBJECT_NONE = 0,
    AFATFS_OBJECT_FILE,
    AFATFS_OBJECT_DIRECTORY
} afatfsObjectKind_t;

/*
 * Scope selector for afatfs_removeObjects_lfn().
 *
 * AFATFS_REMOVE_FILES_ONLY is the current production overwrite mode: matching
 * files are removed, matching directories are left in place because deleting a
 * directory safely requires a recursive child walk. AFATFS_REMOVE_EMPTY_DIRECTORIES
 * is reserved for the later non-recursive directory cleanup step; asyncfatfs.c
 * currently keeps it conservative until that emptiness check lands.
 */
typedef enum {
    AFATFS_REMOVE_FILES_ONLY = 0,
    AFATFS_REMOVE_EMPTY_DIRECTORIES,
} afatfsRemoveObjectMode_t;

/*
 * LFN-aware directory object returned by afatfs_findNextObject().
 *
 * displayName is the firmware-facing component name: a checksum-verified VFAT
 * long name when present, otherwise the short 8.3 alias with ntReserved case
 * flags applied. shortName is always the printable SFN alias for compatibility
 * opens. sfnEntry points at the owning physical directory entry; lfnFirstEntry
 * and lfnEntryCount identify the preceding VFAT fragment run so future delete
 * or rename code can update the whole object instead of only the short entry.
 */
typedef struct {
    afatfsObjectKind_t kind;
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    char shortName[AFATFS_SHORT_FILENAME_MAX];
    uint8_t attrib;
    uint8_t ntReserved;
    uint8_t hasLongName;
    uint8_t lfnEntryCount;
    afatfsDirEntryPointer_t lfnFirstEntry;
    afatfsDirEntryPointer_t sfnEntry;
} afatfsObjectInfo_t;

typedef struct {
    afatfsFinder_t raw;
    uint8_t lfnValid;
    uint8_t lfnChecksum;
    uint8_t lfnEntryCount;
    char lfnName[AFATFS_LONG_FILENAME_MAX + 1u];
    afatfsDirEntryPointer_t lfnFirstEntry;
} afatfsObjectFinder_t;

typedef enum {
    AFATFS_SEEK_SET,
    AFATFS_SEEK_CUR,
    AFATFS_SEEK_END,
} afatfsSeek_e;

typedef void (*afatfsFileCallback_t)(afatfsFilePtr_t file);
typedef void (*afatfsCallback_t)();

bool afatfs_fopen(const char *filename, const char *mode, afatfsFileCallback_t complete);
bool afatfs_fopen_lfn(const char *displayName,
                      const char *mode,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);
bool afatfs_ftruncate(afatfsFilePtr_t file, afatfsFileCallback_t callback);
bool afatfs_fclose(afatfsFilePtr_t file, afatfsCallback_t callback);
bool afatfs_funlink(afatfsFilePtr_t file, afatfsCallback_t callback);

/*
 * Rename one object in the current directory by display component.
 *
 * What: Starts an asynchronous rename of one file or directory named by a
 * single visible component. Matching follows matchMode; production callers use
 * case-insensitive matching so a case-only save can refresh visible casing. The
 * operation updates the complete VFAT LFN/SFN name entry run and returns the
 * new asyncfatfs-openable short alias in openNameOut when requested.
 *
 * Why: Numbered directory saves must change `NNN OldName/` into
 * `NNN NewName/` while preserving children. The slot number is the product
 * identity; the FAT directory entry run is only visible metadata plus the
 * short alias needed by existing open paths.
 *
 * Inputs: oldDisplayName and newDisplayName are current-directory components,
 * not paths. openNameOut may be NULL. complete fires after success or failure;
 * callers inspect openNameOut[0] or their outer filesystem state to decide
 * whether the rename succeeded.
 *
 * Outputs/effects: first cluster, file size, attributes, timestamps, and
 * directory children are preserved. Only the object name entry run changes.
 *
 * Affiliates/clients: filesystem.c Kit, KitMrp, Scene, Bank, and future Effect
 * directory-shaped save phases; asyncfatfs object scanning; LFN/SFN name
 * generation helpers.
 */
bool afatfs_renameObject_lfn(const char *oldDisplayName,
                             const char *newDisplayName,
                             afatfsMatchMode_t matchMode,
                             char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                             afatfsCallback_t complete);

/*
 * Remove all objects whose display name matches one component.
 *
 * What: Scans the current directory and removes every object whose visible
 * display component matches displayName under matchMode. File removal frees the
 * file's cluster chain and retires the full VFAT LFN/SFN entry run. Directory
 * removal is limited by mode and must never recursively delete children.
 *
 * Why: Product overwrite is case-insensitive and case-preserving. If an
 * external filesystem created `Kick.drm` and `kick.drm`, saving `KiCk.drm`
 * must remove both old physical variants before writing one new object with the
 * user's entered case. afatfs_funlink() cannot do this because it needs an open
 * handle and deletes only the SFN entry.
 *
 * Inputs: displayName is a single component in the current directory.
 * AFATFS_REMOVE_FILES_ONLY is used before Instrument and Kit member file
 * writes. AFATFS_REMOVE_EMPTY_DIRECTORIES is reserved for directory-shaped save
 * cleanup when recursive delete is not available.
 *
 * Outputs/effects: callbacks fire once the scan has reached the end or failed.
 * A successful no-op is allowed when no matching object exists. The operation
 * restarts its scan after each deletion because retiring entries mutates the
 * directory being scanned.
 *
 * Affiliates/clients: filesystem.c file overwrite preflight, duplicate
 * case-fold cleanup, future recursive directory deletion, FAT chain truncate
 * helpers, and VFAT entry-run retirement helpers.
 */
bool afatfs_removeObjects_lfn(const char *displayName,
                              afatfsMatchMode_t matchMode,
                              afatfsRemoveObjectMode_t mode,
                              afatfsCallback_t complete);

bool afatfs_feof(afatfsFilePtr_t file);
void afatfs_fputc(afatfsFilePtr_t file, uint8_t c);
uint32_t afatfs_fwrite(afatfsFilePtr_t file, const uint8_t *buffer, uint32_t len);
uint32_t afatfs_fread(afatfsFilePtr_t file, uint8_t *buffer, uint32_t len);
afatfsOperationStatus_e afatfs_fseek(afatfsFilePtr_t file, int32_t offset, afatfsSeek_e whence);
bool afatfs_ftell(afatfsFilePtr_t file, uint32_t *position);

/*
 * Directory create/open contract.
 *
 * The callback receives either NULL or a directory handle that is immediately
 * safe to pass to afatfs_chdir(). For newly-created subdirectories that means
 * asyncfatfs has already allocated the first cluster, written the firstCluster
 * fields back into the parent SFN entry, zero-filled the cluster, and created
 * "." / ".." entries. Regular files may still allocate their first cluster
 * lazily on first fwrite(); directories may not because callers create children
 * through currentDirectory immediately after chdir().
 */
bool afatfs_mkdir(const char *filename, afatfsFileCallback_t complete);
bool afatfs_mkdir_lfn(const char *displayName,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);
bool afatfs_opendir_lfn(const char *displayName,
                        afatfsMatchMode_t matchMode,
                        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                        afatfsFileCallback_t complete);
bool afatfs_chdir(afatfsFilePtr_t dirHandle);

void afatfs_findFirst(afatfsFilePtr_t directory, afatfsFinder_t *finder);
afatfsOperationStatus_e afatfs_findNext(afatfsFilePtr_t directory, afatfsFinder_t *finder, fatDirectoryEntry_t **dirEntry);
void afatfs_findLast(afatfsFilePtr_t directory);
void afatfs_findFirstObject(afatfsFilePtr_t directory,
                            afatfsObjectFinder_t *finder);
afatfsOperationStatus_e afatfs_findNextObject(afatfsFilePtr_t directory,
                                              afatfsObjectFinder_t *finder,
                                              afatfsObjectInfo_t *object);
void afatfs_findLastObject(afatfsFilePtr_t directory,
                           afatfsObjectFinder_t *finder);

/*
 * Drain dirty cache sectors to the SD card.
 *
 * Return value: true only when there are no dirty cache entries and no sector
 * write callback is still pending. Save completion code depends on this stricter
 * boundary so UI-visible "done" cannot outrun the final FAT/directory sectors
 * that make a newly-created folder visible to a desktop reader after power-off.
 */
bool afatfs_flush();
bool afatfs_sync();
void afatfs_init();
bool afatfs_destroy(bool dirty);
void afatfs_poll();

uint32_t afatfs_getFreeBufferSpace();
uint32_t afatfs_getContiguousFreeSpace();
bool afatfs_isFull();

afatfsFilesystemState_e afatfs_getFilesystemState();
afatfsError_e afatfs_getLastError();
