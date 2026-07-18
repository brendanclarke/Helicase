#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "fat_standard.h"

typedef struct afatfsFile_t *afatfsFilePtr_t;
/*
 * Explicit directory-parent handle alias.
 *
 * What: directory handles use the same opaque storage as file handles. Why:
 * parent-relative copy/replace APIs need to state directory intent without
 * exposing afatfsFile_t internals or creating a second handle representation.
 * Inputs/outputs: callers receive this pointer from directory open/create and
 * pass it unchanged as a parent; ownership rules remain those of the underlying
 * file handle. Affiliates: child APIs, recursive copy, and replace recovery.
 */
typedef afatfsFilePtr_t afatfsDirHandle_t;

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
    /* Allocation failed after the operation was accepted; affiliates: create/copy. */
    AFATFS_RESULT_NO_SPACE,
    /* The physical SFN fingerprint no longer describes the scanned object. */
    AFATFS_RESULT_STALE_OBJECT,
    /* A bounded tree walker reached AFATFS_TREE_DEPTH_MAX. */
    AFATFS_RESULT_DEPTH_LIMIT,
    /* Scratch state exists but no valid journal record authorizes cleanup. */
    AFATFS_RESULT_RECOVERY_REQUIRED,
    /* Structural directory metadata (`.`/`..` or cluster ancestry) is invalid. */
    AFATFS_RESULT_CORRUPT_DIRECTORY,
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

/*
 * Maximum product-tree nesting accepted by bounded async walkers.
 *
 * What: limits recursive delete/copy/move ancestry to eight child levels.
 * Why: corrupt `..` links or directory cycles must terminate with a diagnostic
 * instead of polling forever, and product Kit/Scene/Bank schemas are shallower
 * than this bound. Input/output: walkers increment on descent and fail before
 * exceeding the value. Affiliates: afatfs_deleteTree(), recursive copy, and
 * destination-descendant checks for move.
 */
#define AFATFS_TREE_DEPTH_MAX 8u

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
    AFATFS_CREATE_NEW,       // Fail if any same-display object exists.
    AFATFS_OPEN_EXISTING,    // Fail if the requested object is absent.
    AFATFS_CREATE_OR_OPEN,   // Open a typed match or create it when absent.
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
    /*
     * Parent and raw-SFN validation fingerprint.
     *
     * What: parentFirstCluster/parentIsFat16Root identify the directory that
     * owned sfnEntry when this capability was emitted; rawShortName preserves
     * the authoritative 11-byte FAT key instead of its printable alias.
     * Why: sector/entry offsets may be reused after a rename or deletion. A
     * by-identity mutator must reload the entry and compare parent, raw name,
     * kind, and first cluster before it changes the card.
     * Inputs/outputs: populated only by afatfs_findNextObject(); callers copy
     * the complete afatfsObjectId_t and treat it as invalid after any mutation
     * of the source parent.
     * Affiliates: afatfs_validateObject(), native delete, rename/move/copy, and
     * transaction recovery.
     */
    uint32_t parentFirstCluster;
    uint8_t parentIsFat16Root;
    uint8_t rawShortName[FAT_FILENAME_LENGTH];
} afatfsObjectId_t;

/*
 * Structured open/create completion callbacks.
 *
 * What: report the terminal reason independently from the optional returned
 * file or object. Why: legacy NULL callbacks cannot distinguish not-found,
 * type collision, invalid input, media failure, and exhausted space.
 * Inputs: result is one afatfsResultCode_t; file/object is valid only when the
 * result is OK (the object pointer is callback-scoped).
 * Outputs/lifetime: accepted operations call exactly once after releasing all
 * internal iterator/cache ownership. Affiliates: parent-relative child APIs,
 * by-identity mutators, and legacy callback adapters.
 */
typedef void (*afatfsOpenResultCallback_t)(afatfsResultCode_t result,
                                           afatfsFilePtr_t file);
typedef void (*afatfsObjectResultCallback_t)(afatfsResultCode_t result,
                                             const afatfsObjectId_t *object);

/*
 * Recursive-copy completion policy.
 *
 * What: AFATFS_COPY_DURABLE adds an explicit cache-to-card sync after every
 * destination handle has closed; AFATFS_COPY_DEFAULT leaves that barrier to a
 * surrounding replace transaction. Why: standalone diagnostics need a durable
 * completion, while transaction builders already sync once before promotion
 * and should not duplicate card traffic. Inputs: bitwise flags supplied to
 * afatfs_copyObjectTree(). Output: the callback still reports one structured
 * result. Affiliates: afatfs_sync() and tree-replace PREPARED ordering.
 */
typedef enum {
    AFATFS_COPY_DEFAULT = 0u,
    AFATFS_COPY_DURABLE = 1u << 0,
} afatfsCopyFlags_t;

/*
 * Opaque handle for the one supported staged directory replacement.
 *
 * What: callers receive this pointer after asyncfatfs has created a guaranteed
 * new scratch directory. Why: begin requires card I/O and therefore cannot
 * return a populated transaction synchronously. Lifetime: the pointer refers
 * to driver-owned static storage and is valid until commit or abort completes;
 * callers must not copy or inspect it. Affiliates: begin/commit/abort below,
 * explicit recovery, and the staging directory returned with begin.
 */
typedef struct afatfsReplaceTransaction *afatfsReplaceTransactionPtr_t;

/*
 * Completion for asynchronous replacement begin.
 *
 * Inputs: result is the precise begin outcome. On OK, transaction and
 * stagingDirectory are non-NULL and stagingDirectory is an ordinary open
 * directory that may be passed to parent-relative create/copy APIs. Outputs:
 * errors return both pointers as NULL after all internal ownership is released.
 * Affiliates: afatfs_beginTreeReplace() and afatfs_commitTreeReplace().
 */
typedef void (*afatfsReplaceBeginCallback_t)(
    afatfsResultCode_t result,
    afatfsReplaceTransactionPtr_t transaction,
    afatfsDirHandle_t stagingDirectory);

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

/*
 * Acquire an independent handle for the mounted filesystem root.
 *
 * What: allocates one ordinary directory handle representing FAT16's fixed
 * root extent or FAT32's root cluster without changing asyncfatfs' legacy
 * process-wide current directory. Why: parent-relative product operations need
 * an explicit root from which they can open `.names`, Instrument, Kit, Scene,
 * and Bank children; bootstrapping those graphs through chdir would reintroduce
 * global location state. Inputs: the filesystem must be READY and one open-file
 * pool entry must be free. Output: caller-owned directory handle, or NULL when
 * acquisition cannot start; close it with afatfs_fclose() after every child is
 * closed. Affiliates: afatfs_fopenChild(), afatfs_openDirChild(),
 * afatfs_createDirChild(), tree-replace recovery, and filesystem.c.
 */
afatfsDirHandle_t afatfs_openRoot(void);

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
 * Rename one validated object inside its explicit parent.
 *
 * What: rewrites the source object's complete LFN/SFN name run without a
 * display-name source scan. Why: duplicate/stale visible names must not redirect
 * a save after product code selected a concrete directory entry.
 * Inputs: parent is the open source directory; source is copied and validated
 * against its raw SFN fingerprint; newDisplayName is one component; matchMode
 * controls destination collision folding. parent remains caller-owned but may
 * not be used until complete.
 * Outputs: true accepts the operation and guarantees one structured callback;
 * OK preserves cluster, size, attributes, timestamps, and children while
 * openNameOut receives the new printable alias. STALE_OBJECT changes nothing.
 * Affiliates: afatfs_findNextObject(), the legacy name-based rename wrapper,
 * cross-parent move, and replace promotion.
 */
bool afatfs_renameObjectAt(afatfsDirHandle_t parent,
                           const afatfsObjectId_t *source,
                           const char *newDisplayName,
                           afatfsMatchMode_t matchMode,
                           char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                           afatfsResultCallback_t complete);

/*
 * Move one validated object between explicit parents.
 *
 * What: creates a destination LFN/SFN run that preserves source metadata and
 * cluster ownership, updates a moved directory's structural `..` entry, then
 * retires the source name run. Same-parent moves use the rename core.
 * Why: product staging and recovery need exact-object promotion without global
 * chdir or data copying. Inputs: both parents must remain open/unused until the
 * callback; source is copied and validated; destinationName is one component.
 * Outputs: true guarantees one callback. Destination collision, stale source,
 * descendant/self move, depth/corruption, and I/O are reported distinctly.
 * Visibility ordering writes destination before retiring source, so failure can
 * leave two names but cannot remove the sole live name.
 * Affiliates: afatfs_renameObjectAt(), replace promotion, object validation,
 * directory `..` creation, and AFATFS_TREE_DEPTH_MAX.
 */
bool afatfs_moveObject(afatfsDirHandle_t sourceParent,
                       const afatfsObjectId_t *source,
                       afatfsDirHandle_t destinationParent,
                       const char *destinationName,
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
 * Parent-relative file and directory open/create operations.
 *
 * What: resolve one display component inside parent without changing the
 * process-wide current directory. createMode explicitly selects collision and
 * missing-object behavior; matchMode applies only to display-name lookup.
 * Why: recursive copy and staged Bank saves must keep source and destination
 * parents open concurrently. Implicit current-directory mutation cannot safely
 * represent that operation graph.
 * Inputs: parent must be an open, idle directory; displayName is one component
 * and may not be "." or "..". accessMode uses afatfs_fopen() syntax. When a
 * VFAT long name is present, lookup first compares that visible LFN and then
 * its physical SFN alias; this permits a one-object browser to reopen the
 * selected alias without retaining a directory list.
 * Outputs/lifetime: true means the request retained parent and complete will
 * fire exactly once. The caller must not close, seek, scan, or otherwise use
 * parent until completion. On OK, file remains caller-owned and openNameOut
 * contains its printable SFN alias when non-NULL.
 * Affiliates: afatfsCreateFile_t, directory extension/`.`/`..` initialization,
 * recursive copy, replace staging, and legacy current-directory wrappers.
 */
bool afatfs_fopenChild(afatfsDirHandle_t parent,
                       const char *displayName,
                       const char *accessMode,
                       afatfsCreateMode_t createMode,
                       afatfsMatchMode_t matchMode,
                       char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                       afatfsOpenResultCallback_t complete);
bool afatfs_openDirChild(afatfsDirHandle_t parent,
                         const char *displayName,
                         afatfsMatchMode_t matchMode,
                         char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                         afatfsOpenResultCallback_t complete);
bool afatfs_createDirChild(afatfsDirHandle_t parent,
                           const char *displayName,
                           afatfsCreateMode_t createMode,
                           afatfsMatchMode_t matchMode,
                           char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                           afatfsOpenResultCallback_t complete);

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
 * Copy one exact file or directory tree into an explicit destination parent.
 *
 * What: validates source in sourceParent, creates destinationName with
 * CREATE_NEW semantics, then copies file bytes or recursively recreates a
 * directory tree. Why: Bank/Scene preservation must not parse and reserialize
 * unknown files. Inputs: both parents must be open idle directories; source is
 * copied at start; destinationName is one component; flags selects the final
 * sync boundary. Outputs/lifetime: false accepts nothing and gives no callback;
 * true retains both parents until exactly one callback. A failure deliberately
 * leaves its partial destination for transaction abort or diagnosis.
 *
 * RAM/latency: the implementation uses a 96-byte stream buffer, one active LFN
 * finder, and compact depth frames. Directory ascent therefore rescans the
 * source parent to its saved physical SFN key. This is slower but keeps the
 * complete filesystem object under the project's 9 KB target.
 * Affiliates: afatfs_findNextObject(), child create APIs, afatfs_fread(),
 * afatfs_fwrite(), AFATFS_TREE_DEPTH_MAX, and replace staging.
 */
bool afatfs_copyObjectTree(afatfsDirHandle_t sourceParent,
                           const afatfsObjectId_t *source,
                           afatfsDirHandle_t destinationParent,
                           const char *destinationName,
                           afatfsCopyFlags_t flags,
                           afatfsResultCallback_t complete);

/*
 * Begin one crash-recoverable staged directory replacement.
 *
 * What: creates a nonce-derived `.afat-...-new` child with CREATE_NEW and
 * returns it with an opaque transaction. Existing scratch collisions cause a
 * new nonce attempt; stale scratch is never reopened. Why: product save code
 * must build a clean tree rather than merge into the live target or an old temp
 * directory. Inputs: parent is an explicit open directory and targetDisplayName
 * is copied as one component. Outputs: accepted calls callback exactly once;
 * on OK the caller owns stagingDirectory until commit/abort is requested.
 * Affiliates: child creation, tree copy, commit journaling, and recovery.
 */
bool afatfs_beginTreeReplace(afatfsDirHandle_t parent,
                             const char *targetDisplayName,
                             afatfsReplaceBeginCallback_t complete);

/*
 * Commit one begun replacement using journaled FAT-realistic ordering.
 *
 * What: syncs the staged payload, alternates CRC-protected journal records,
 * moves the old target aside by identity, promotes staging, removes the old
 * tree, and records CLEAN. Why: FAT cannot atomically rename two arbitrary LFN
 * objects, so recovery data must make every power-loss boundary deterministic.
 * Input transaction must be the active pointer returned by begin and all child
 * handles beneath staging must be closed. Output true guarantees one result
 * callback; OK means the promoted target and cleanup state are synced.
 * Affiliates: afatfs_sync(), afatfs_moveObject(), afatfs_deleteTree(), and
 * afatfs_recoverTreeReplace().
 */
bool afatfs_commitTreeReplace(afatfsReplaceTransactionPtr_t transaction,
                              afatfsResultCallback_t complete);

/*
 * Abort one begun replacement without endangering the live target.
 *
 * What: before promotion starts, deletes only this transaction's exact staging
 * tree and records clean completion. If commit has retired the target, abort
 * switches to the same deterministic recovery rules as remount recovery. Why:
 * blindly deleting scratch after OLD_RENAMED could discard the only complete
 * tree. Inputs/outputs follow commit's accepted-operation callback contract.
 * Affiliates: native tree delete and recovery journal states.
 */
bool afatfs_abortTreeReplace(afatfsReplaceTransactionPtr_t transaction,
                             afatfsResultCallback_t complete);

/*
 * Recover interrupted replacement work in one known product parent.
 *
 * What: reads both fixed journal slots, selects the highest-sequence valid CRC,
 * and applies PREPARED/OLD_RENAMED/PROMOTED cleanup or promotion rules only to
 * the nonce and target named by that record. Why: mount cannot safely scan the
 * whole card or infer product roots. Input parent remains retained until the
 * callback. Output RECOVERY_REQUIRED leaves unknown scratch untouched when no
 * valid record authorizes mutation; repeated successful recovery is idempotent.
 * Affiliates: browser preflight for Bank/Scene/Kit parents and journal commit.
 */
bool afatfs_recoverTreeReplace(afatfsDirHandle_t parent,
                               afatfsResultCallback_t complete);
