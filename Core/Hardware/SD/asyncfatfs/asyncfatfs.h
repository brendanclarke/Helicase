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

/*
 * Structured result codes for async operations.
 * Why: High-level product code currently relies on generic SUCCESS/FAILURE
 * booleans, which obscures the root cause (e.g., collision vs IO error).
 * Affiliates: Replaces `bool` returns in async completion callbacks.
 */
typedef enum {
    AFATFS_RESULT_OK = 0,
    AFATFS_RESULT_NOT_FOUND,
    AFATFS_RESULT_ALREADY_EXISTS,
    AFATFS_RESULT_INVALID_NAME,
    AFATFS_RESULT_UNSUPPORTED_NAME,
    AFATFS_RESULT_NOT_EMPTY,
    AFATFS_RESULT_NOT_DIRECTORY,
    AFATFS_RESULT_NOT_FILE,
    AFATFS_RESULT_IO_ERROR,
    AFATFS_RESULT_CORRUPT_LFN_RUN,
    AFATFS_RESULT_UNSUPPORTED_LAYOUT,
} afatfsResultCode_t;

typedef void (*afatfsResultCallback_t)(afatfsResultCode_t result);

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
 * Explicit policies for file/directory creation.
 * Why: `mkdir_lfn` previously used "create or open", which caused Bank overwrites
 * to merge into stale folders instead of replacing them.
 */
typedef enum {
    AFATFS_CREATE_NEW,       // Fail if object exists
    AFATFS_OPEN_EXISTING,    // Fail if object absent
    AFATFS_CREATE_OR_OPEN,   // Legacy fallback behavior
    AFATFS_REPLACE_FILE      // Delete existing and create new
} afatfsCreateMode_t;

/*
 * Opaque physical identity of a FAT object.
 * Why: Resolves the "stale short alias" and "duplicate LFN" bugs. By storing
 * the physical directory entry pointers and cluster chains, subsequent operations
 * (open, delete, move) guarantee they act on the exact same object discovered
 * during scanning, regardless of string overlaps.
 */
typedef struct {
    afatfsObjectKind_t kind;
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    char shortName[AFATFS_SHORT_FILENAME_MAX];
    afatfsDirEntryPointer_t lfnFirstEntry;
    uint8_t lfnEntryCount;
    afatfsDirEntryPointer_t sfnEntry;
    uint32_t firstCluster;
    uint32_t logicalSize;
    uint8_t attrib;
} afatfsObjectId_t;

/*
 * Scope selector for afatfs_removeObjects_lfn().
 *
 * AFATFS_REMOVE_FILES_ONLY is the current production overwrite mode: matching
 * files are removed, matching directories are left in place because deleting a
 * directory safely requires a recursive child walk. AFATFS_REMOVE_EMPTY_DIRECTORIES
 * is the final low-level step for filesystem.c's recursive deleter: callers
 * must have already removed every child, because asyncfatfs deliberately does
 * not scan or prove directory emptiness here.
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
 *
 * Note: Now wraps afatfsObjectId_t to enforce unified identity semantics.
 */
typedef struct {
    afatfsObjectId_t id;
    uint8_t ntReserved;
    uint8_t hasLongName;
} afatfsObjectInfo_t;

/*
 * Persistent state for one LFN-aware object scan.
 *
 * What: raw owns the physical directory-entry cursor, while the remaining
 * fields accumulate and validate the VFAT fragments that precede an SFN.
 * Why the complete type matters: afatfs_findFirstObject(),
 * afatfs_findNextObject(), and afatfs_findLastObject() read and write every
 * field in this structure. A caller must allocate afatfsObjectFinder_t itself;
 * casting a smaller afatfsFinder_t to this type overwrites adjacent state.
 * Inputs/outputs: callers retain one instance for the lifetime of a scan and
 * pass it unchanged to all three object-iterator functions. The iterator owns
 * its contents between those calls.
 * Affiliates: native recursive deletion, product directory scans, LFN rename,
 * and same-name removal all keep this exact object beside their phase state.
 */
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

typedef afatfsFilePtr_t afatfsDirHandle_t;

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
 * Inputs: displayName is a single component in the current directory. LFN
 * operations convert unsupported display characters to '_' and strip trailing
 * spaces and periods before matching or creating objects.
 * AFATFS_REMOVE_FILES_ONLY is used before Instrument and Kit member file
 * writes. AFATFS_REMOVE_EMPTY_DIRECTORIES is used only by filesystem.c after
 * its recursive delete state machine has emptied the target directory.
 *
 * Outputs/effects: callbacks fire once the scan has reached the end or failed.
 * A successful no-op is allowed when no matching object exists. The operation
 * restarts its scan after each deletion because retiring entries mutates the
 * directory being scanned.
 *
 * Affiliates/clients: filesystem.c file overwrite preflight, duplicate
 * case-fold cleanup, recursive directory deletion, FAT chain truncate helpers,
 * and VFAT entry-run retirement helpers.
 */
bool afatfs_removeObjects_lfn(const char *displayName,
                              afatfsMatchMode_t matchMode,
                              afatfsRemoveObjectMode_t mode,
                              afatfsCallback_t complete);
/*
 * Remove one exact short-alias object from the current directory.
 *
 * What: this is the precise-SFN companion to afatfs_removeObjects_lfn().
 * display-name removal intentionally removes every case-folded LFN variant for
 * product overwrites, while this function targets only the object whose
 * printable 8.3 alias equals filename. The mode rules are identical: files may
 * always be removed in AFATFS_REMOVE_FILES_ONLY, and directories may be retired
 * only in AFATFS_REMOVE_EMPTY_DIRECTORIES after higher-level code has emptied
 * them recursively.
 *
 * Why: recursive directory deletion first discovers a concrete directory entry
 * with afatfs_findNextObject(), then opens children by that entry's shortName.
 * When a damaged FAT card already has duplicate visible long names, removing
 * the final empty directory by displayName can re-resolve to the wrong sibling.
 * Passing the same shortName used to open the directory keeps open, recurse,
 * and retire operations attached to one physical object.
 *
 * Inputs/accessors: filename must be the printable shortName returned in
 * afatfsObjectInfo_t or an openNameOut returned by mkdir/open LFN helpers.
 * Output/effects: the completion callback fires after the scan either retires
 * that exact entry or reaches the end as a successful no-op.
 */
bool afatfs_removeObject(const char *filename,
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
 *
 * The *_lfn variants take one visible component in the current directory. They
 * sanitize unsupported UI characters to '_', strip trailing spaces/periods,
 * optionally match case-insensitively, and return an 8.3 alias in openNameOut
 * when the object is opened or created. Product code should store that alias
 * only as a reopen/chdir implementation detail; visible schemas such as
 * kitset.kcg should store the display component.
 */
bool afatfs_mkdir(const char *filename, afatfsFileCallback_t complete);
bool afatfs_opendir(const char *filename, afatfsFileCallback_t complete);
bool afatfs_mkdir_lfn(const char *displayName,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);
bool afatfs_opendir_lfn(const char *displayName,
                        afatfsMatchMode_t matchMode,
                        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                        afatfsFileCallback_t complete);

/*
 * Parent-relative operations.
 * Why: Prevents asynchronous product state machines from clobbering the global
 * `afatfs.currentDirectory`.
 * Inputs: A valid open directory handle instead of using global state.
 */
void afatfs_findFirstObjectInDir(afatfsDirHandle_t parent, afatfsObjectFinder_t *finder);
bool afatfs_fopenChild(afatfsDirHandle_t parent, const char *displayName, afatfsCreateMode_t mode, afatfsFileCallback_t complete);
bool afatfs_mkdirChild(afatfsDirHandle_t parent, const char *displayName, afatfsCreateMode_t mode, afatfsFileCallback_t complete);

bool afatfs_chdir(afatfsFilePtr_t dirHandle);
afatfsOperationStatus_e afatfs_chdirParent(void);

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

/*
 * Start native asynchronous deletion of one concrete directory tree.
 *
 * What: root is the physical object identity returned by
 * afatfs_findNextObject(). The operation scans that directory, retires every
 * child LFN/SFN run, frees each child cluster chain, and finally retires and
 * frees root itself. It does not resolve root again by display text.
 * Why: replacement saves must delete the exact same-slot directory selected by
 * the product scanner, including cards with duplicate or stale display names,
 * without blocking audio while FAT sectors are read and written.
 * Inputs: root must describe a directory and remain valid only for the duration
 * of this call because the identity is copied into private operation state. cb
 * may be NULL; when non-NULL it is invoked exactly once with OK or an error.
 * Outputs/lifecycle: true means a private file handle accepted the operation;
 * false means no handle was available and no callback will occur. On every
 * terminal path the implementation releases retained cache sectors and resets
 * its private handle before invoking cb, so callback clients may safely queue
 * later filesystem work.
 * Affiliates: filesystem.c same-slot Kit/Scene cleanup,
 * afatfsObjectFinder_t, afatfs_getDeleteTreePhase(), and afatfs_poll().
 */
bool afatfs_deleteTree(const afatfsObjectId_t *root, afatfsResultCallback_t cb);
uint8_t afatfs_getDeleteTreePhase(void);

/*
 * State machine for cross-directory movement.
 * Why: Moves a physical object (and its cluster chain) to a new parent directory.
 * Requires allocating a new directory entry run in the destination, copying the
 * cluster pointer, and marking the old entry run as deleted (0xE5).
 */
bool afatfs_moveObject(const afatfsObjectId_t *src, afatfsDirHandle_t dst_parent, const char *dst_name, afatfsResultCallback_t cb);

/*
 * State machine for deep tree copy.
 * Why: Avoids loading product files into RAM just to re-serialize them.
 * Reads source clusters into the 4KB cache and flushes them to newly allocated
 * destination clusters.
 */
bool afatfs_copyObjectTree(const afatfsObjectId_t *src, afatfsDirHandle_t dst_parent, const char *dst_name, afatfsResultCallback_t cb);

/*
 * Transactional directory replace.
 * Why: Bank Save needs to guarantee that old data is entirely displaced and the
 * new tree is completely synced before becoming visible.
 * Internals:
 * 1. Generates `tmp_XXXX` under the parent.
 * 2. Caller populates `tmp_XXXX` (via explicit handle, not `chdir`).
 * 3. `commitTreeReplace` executes a rename of the old target to `old_XXXX`,
 *    renames `tmp_XXXX` to target, and schedules `old_XXXX` for background deletion.
 */
bool afatfs_beginTreeReplace(afatfsDirHandle_t parent, const char *target_name, afatfsDirHandle_t *tx_out);
bool afatfs_commitTreeReplace(afatfsDirHandle_t tx, afatfsResultCallback_t cb);
bool afatfs_abortTreeReplace(afatfsDirHandle_t tx, afatfsResultCallback_t cb);
