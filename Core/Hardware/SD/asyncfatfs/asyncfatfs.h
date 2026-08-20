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

/*
 * Read-only diagnostic copy of one active AsyncFATFS file and allocator.
 *
 * What: exposes scalar state needed to distinguish a stalled FAT search,
 * FAT update, cache wait, and explicit-full result after an external boot
 * timeout. Why: the opaque file handle otherwise loses all of these values
 * when the recovery path destroys AsyncFATFS. Input: a live handle or NULL;
 * output: copied values only, with `available` zero for NULL. This API never
 * polls, allocates, starts I/O, releases cache ownership, or retains a pointer.
 * filesystem.c owns the resulting logging capsule; this header owns no storage.
 */
typedef struct {
    uint8_t available;
    uint8_t filesystem_state;
    uint8_t filesystem_full;
    uint8_t file_operation;
    uint8_t append_phase;
    uint8_t search_wrapped;
    uint8_t cache_dirty_count;
    uint8_t cache_locked_count;
    uint8_t cache_reading_count;
    uint8_t cache_writing_count;
    uint8_t cache_flush_in_progress;
    int8_t active_cache_index;
    uint32_t cursor_offset;
    uint32_t logical_size;
    uint32_t physical_size;
    uint32_t cursor_cluster;
    uint32_t cursor_previous_cluster;
    uint32_t append_previous_cluster;
    uint32_t search_cluster;
    uint32_t search_start_cluster;
    uint32_t sectors_per_cluster;
} afatfsDiagnosticSnapshot_t;

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
/* Four VFAT fragments cover the complete 48-character component bound. */
#define AFATFS_LONG_FILENAME_ENTRY_MAX \
    ((AFATFS_LONG_FILENAME_MAX + FAT_LFN_CHARS_PER_ENTRY - 1u) / \
     FAT_LFN_CHARS_PER_ENTRY)

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
 * (open, delete, and name retirement) guarantee they act on the exact same
 * object discovered
 * during scanning, regardless of string overlaps. `lfnFirstEntry` is the
 * first on-card LFN fragment and `lfnFollowingEntry[0..N-2]` are the remaining
 * fragments in physical directory order; they are never reconstructed by
 * pointer arithmetic.
 */
typedef struct {
    afatfsObjectKind_t kind;
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    char shortName[AFATFS_SHORT_FILENAME_MAX];
    afatfsDirEntryPointer_t lfnFirstEntry;
    /* Physical VFAT entries after lfnFirstEntry, in on-card order. */
    afatfsDirEntryPointer_t lfnFollowingEntry[AFATFS_LONG_FILENAME_ENTRY_MAX - 1u];
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
 * lfnMalformed is set when VFAT-looking entries immediately precede this SFN
 * but fail ordinal, checksum, shape, or completeness validation; destructive
 * clients must reject that object rather than guess which entries belong to it.
 *
 * Note: Now wraps afatfsObjectId_t to enforce unified identity semantics.
 */
typedef struct {
    afatfsObjectId_t id;
    uint8_t ntReserved;
    uint8_t hasLongName;
    uint8_t lfnMalformed;
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
    uint8_t lfnExpectedOrdinal;
    uint8_t lfnMalformed;
    char lfnName[AFATFS_LONG_FILENAME_MAX + 1u];
    afatfsDirEntryPointer_t lfnFirstEntry;
    afatfsDirEntryPointer_t lfnFollowingEntry[AFATFS_LONG_FILENAME_ENTRY_MAX - 1u];
} afatfsObjectFinder_t;

typedef enum {
    AFATFS_SEEK_SET,
    AFATFS_SEEK_CUR,
    AFATFS_SEEK_END,
} afatfsSeek_e;

typedef void (*afatfsFileCallback_t)(afatfsFilePtr_t file);
typedef void (*afatfsCallback_t)();

typedef afatfsFilePtr_t afatfsDirHandle_t;

/*
 * Open-handle pool contract.
 *
 * The implementation provisions five afatfsFile_t application slots. The
 * separate currentDirectory object is not one of those five: after a successful
 * afatfs_chdir(), callers may close the source directory handle and reuse its
 * slot. Conversely, already-open regular-file handles remain valid when the
 * current directory changes, allowing a reader below Bank/Scene/Kit and a
 * root-level writer to be pumped in an interleaved fashion. Open/create calls
 * return false when all five application slots are occupied, and the caller
 * must close a no-longer-needed handle before retrying.
 */
bool afatfs_fopen(const char *filename, const char *mode, afatfsFileCallback_t complete);
bool afatfs_fopen_lfn(const char *displayName,
                      const char *mode,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);
bool afatfs_ftruncate(afatfsFilePtr_t file, afatfsFileCallback_t callback);
bool afatfs_fclose(afatfsFilePtr_t file, afatfsCallback_t callback);

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
 * not paths. openNameOut may be NULL. complete receives a structured result
 * after success or failure; openNameOut is valid only with OK.
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
                             afatfsResultCallback_t complete);

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
 * user's entered case. The retired opened-handle unlink shortcut was removed
 * because it could only retire an SFN entry and was not a safe product
 * primitive.
 *
 * Inputs: displayName is a single component in the current directory. LFN
 * operations convert unsupported display characters to '_' and strip trailing
 * spaces and periods before matching or creating objects.
 * AFATFS_REMOVE_FILES_ONLY is used before Instrument and Kit member file
 * writes. AFATFS_REMOVE_EMPTY_DIRECTORIES is used only by filesystem.c after
 * its recursive delete state machine has emptied the target directory.
 *
 * Outputs/effects: callbacks receive a structured result once the scan has
 * reached the end or failed. A successful no-op is allowed when no matching
 * object exists. The operation
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
                              afatfsResultCallback_t complete);
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
 * Output/effects: the result callback fires after the scan either retires that
 * exact entry or reaches the end as a successful no-op.
 */
bool afatfs_removeObject(const char *filename,
                         afatfsRemoveObjectMode_t mode,
                         afatfsResultCallback_t complete);

bool afatfs_feof(afatfsFilePtr_t file);
void afatfs_fputc(afatfsFilePtr_t file, uint8_t c);
uint32_t afatfs_fwrite(afatfsFilePtr_t file, const uint8_t *buffer, uint32_t len);
uint32_t afatfs_fread(afatfsFilePtr_t file, uint8_t *buffer, uint32_t len);
/* Copy current diagnostics without changing AsyncFATFS progress or ownership. */
void afatfs_getDiagnosticSnapshot(afatfsFilePtr_t file,
                                  afatfsDiagnosticSnapshot_t *snapshot);
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
/**
 * @brief Create a new directory with a VFAT Long File Name (LFN), or open it if it exists.
 *
 * Scans the current directory for an exact or case-insensitive match (depending on matchMode)
 * against `displayName`. If a match is found and it is a directory, it is opened. If not,
 * a new directory is created using LFN entries for `displayName` and a generated 8.3 alias.
 *
 * @param displayName The intended long name (e.g. "My Folder").
 * @param matchMode AFATFS_MATCH_CASE_SENSITIVE for exact match, AFATFS_MATCH_CASE_INSENSITIVE for folded match.
 * @param openNameOut Buffer (AFATFS_SHORT_FILENAME_MAX) to receive the 8.3 short name alias generated. May be NULL if not needed.
 * @param complete Callback executed when the handle is ready. Passed NULL on failure.
 * @return true if the creation/open operation was successfully queued, false if too many files are open.
 */
bool afatfs_mkdir_lfn(const char *displayName,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete);

/**
 * @brief Open an existing directory by its VFAT Long File Name (LFN).
 *
 * Similar to afatfs_mkdir_lfn, but will strictly fail if the directory does not already exist.
 * It will not attempt to create a new directory. This is used for navigation where creation is unintended.
 *
 * @param displayName The intended long name to search for.
 * @param matchMode AFATFS_MATCH_CASE_SENSITIVE for exact match, AFATFS_MATCH_CASE_INSENSITIVE for folded match.
 * @param openNameOut Buffer to receive the 8.3 short name alias. May be NULL if not needed.
 * @param complete Callback executed when the handle is ready. Passed NULL on failure.
 * @return true if the open operation was successfully queued, false if too many files are open.
 */
bool afatfs_opendir_lfn(const char *displayName,
                        afatfsMatchMode_t matchMode,
                        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                        afatfsFileCallback_t complete);

/**
 * @brief Change the working directory to the specified directory handle.
 *
 * Once this synchronously succeeds, the global `afatfs.currentDirectory` becomes the target.
 * Passing NULL resets the current directory to the FAT root directory.
 *
 * @param dirHandle An open directory handle, or NULL for root.
 * @return true on immediate success. false if the filesystem or handle is busy (caller should retry).
 */
bool afatfs_chdir(afatfsFilePtr_t dirHandle);

/**
 * @brief Change the working directory to the parent of the current directory ("..").
 *
 * **WARNING**: This function executes asynchronously and returns an enum, not a boolean!
 * Evaluators must check the return against `AFATFS_OPERATION_SUCCESS`, `AFATFS_OPERATION_IN_PROGRESS`,
 * or `AFATFS_OPERATION_FAILURE`. Using `!afatfs_chdirParent()` will evaluate `SUCCESS` (0) as true
 * and `IN_PROGRESS` (1) as false, causing catastrophic infinite loops!
 *
 * @return AFATFS_OPERATION_SUCCESS (0) when complete.
 *         AFATFS_OPERATION_IN_PROGRESS (1) if asynchronous sector reads are still pending.
 *         AFATFS_OPERATION_FAILURE (2) if the parent lookup failed (e.g., at root or corrupt FS).
 */
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
/*
 * Report that a complete, wrap-around search of the regular FAT cluster pool
 * (or the optional contiguous pool) found no space. The result is sticky until
 * remount, so a caller extending a file must treat a zero-byte fwrite plus
 * this result as terminal rather than retrying forever.
 */
bool afatfs_isFull();

afatfsFilesystemState_e afatfs_getFilesystemState();
afatfsError_e afatfs_getLastError();

/*
 * One code per distinct non-OK check inside afatfs_deleteTreeContinue() in
 * asyncfatfs.c. Declaration order matches the order those checks appear in
 * that function, top to bottom; add new sites at the end so previously-
 * captured card evidence stays decodable. AFATFS_DELETE_TREE_FAILURE_SITE_NONE
 * is the value afatfs_deleteTree() resets to before a fresh traversal
 * starts, and is also what a caller reads if a result completed OK (no site
 * fired). Declared here rather than in asyncfatfs.c so filesystem.c can
 * recognize specific sites by name (see afatfs_getDeleteTreeFailureSite()).
 *
 * AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED and
 * ..._MATCH_CLUSTER_OUT_OF_RANGE are permanently unreachable as of the fix
 * eliminating AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF/_LOOP (see
 * AFATFS_DELETE_TREE_REOPEN_PARENT's doc comment in asyncfatfs.c for why
 * that re-scan-by-identity step was unnecessary and is gone, not just
 * fixed) -- kept declared, not removed, only so already-captured card
 * evidence from Session 054 rounds 5/6 naming them stays decodable.
 */
typedef enum {
    AFATFS_DELETE_TREE_FAILURE_SITE_NONE = 0u,
    AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_BAD_ROOT_ON_FAT32,
    AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_ROOT_CLUSTER_MISMATCH,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_MALFORMED_OBJECT,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_CHILD_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_STRUCTURAL_BUDGET_EXHAUSTED_DESCEND,
    AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_BAD_DOTDOT_ENTRY,
    AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_PARENT_CLUSTER_OUT_OF_RANGE,
    AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_PARENT_CLUSTER_MISMATCH,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_BAD_ROOT_ON_FAT32,
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED, /* unreachable, see above */
    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_MATCH_CLUSTER_OUT_OF_RANGE, /* unreachable, see above */
    AFATFS_DELETE_TREE_FAILURE_SITE_RETIRE_ENTRIES_ROOT_CLUSTER_MISMATCH,
    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NONFILE_NONZERO_SIZE,
    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_CLUSTER_OUT_OF_RANGE_OR_BUDGET,
    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NEXT_CLUSTER_INVALID,
    /* Defensive catch-all: op->phase held a value no case above matches.
     * Not expected to be reachable; tagged only so it is never silently
     * indistinguishable from a real layout problem if it somehow is. */
    AFATFS_DELETE_TREE_FAILURE_SITE_CORRUPT_PHASE
} afatfsDeleteTreeFailureSite_e;

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
 * Inputs: root must be the complete directory result returned by
 * afatfs_findNextObject() and remain valid only for the duration of this call
 * because the identity is copied into private operation state. cb may be NULL;
 * when non-NULL it is invoked exactly once with OK or an error.
 * Outputs/lifecycle: true means a private file handle accepted the operation;
 * false means no handle was available and no callback will occur. On every
 * terminal path the implementation releases retained cache sectors and resets
 * its private handle before invoking cb, so callback clients may safely queue
 * later filesystem work.
 * Affiliates: filesystem.c same-slot Kit/Scene cleanup,
 * afatfsObjectFinder_t, afatfs_getDeleteTreePhase(), and afatfs_poll().
 */
bool afatfs_deleteTree(const afatfsObjectInfo_t *root,
                       afatfsResultCallback_t cb);
uint8_t afatfs_getDeleteTreePhase(void);
/*
 * Read which exact internal check produced the last afatfs_deleteTree()
 * call's afatfsResultCode_t (see afatfsDeleteTreeFailureSite_e above for
 * the full list and afatfs_deleteTreeContinue() in asyncfatfs.c for where
 * each fires). Returns 0 (NONE) if the last completed delete succeeded.
 * Unlike afatfs_getDeleteTreePhase(), safe to call after the operation's
 * result callback has already run and its openFiles[] handle reset -- read
 * it there, before starting another delete, since that resets it.
 */
uint8_t afatfs_getDeleteTreeFailureSite(void);
