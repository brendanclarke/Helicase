/**
 * This is a FAT16/FAT32 filesystem for SD cards which uses asynchronous operations: The caller need never wait
 * for the SD card to be ready.
 *
 * On top of the regular FAT32 concepts, we add the idea of a "super cluster". Given one FAT sector, a super cluster is
 * the series of clusters which corresponds to all of the cluster entries in that FAT sector. If files are allocated
 * on super-cluster boundaries, they will have FAT sectors which are dedicated to them and independent of all other
 * files.
 *
 * We can pre-allocate a "freefile" which is a file on disk made up of contiguous superclusters. Then when we want
 * to allocate a file on disk, we can carve it out of the freefile, and know that the clusters will be contiguous
 * without needing to read the FAT at all (the freefile's FAT is completely determined from its start cluster and file
 * size, which we get from the directory entry). This allows for extremely fast append-only logging.
 */

/*
 * LXR-02 modifications from upstream asyncfatfs:
 *   - Removed <stdlib.h> (unused on bare-metal)
 *   - Removed AFATFS_USE_FREEFILE (not needed for kit load/save)
 *   - Removed AFATFS_USE_INTROSPECTIVE_LOGGING
 *   - Removed AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT (single-block writes only)
 *   - sdcard.h include → sdcard_lxr02.h
 */

#include <stdint.h>
#include <string.h>

#include "asyncfatfs.h"

#include "fat_standard.h"
#include "sdcard_lxr02.h"

#define ONLY_EXPOSE_FOR_TESTING static

#define AFATFS_NUM_CACHE_SECTORS 8

// FAT filesystems are allowed to differ from these parameters, but we choose not to support those weird filesystems:
#define AFATFS_SECTOR_SIZE  512
#define AFATFS_NUM_FATS     2

/*
 * Parent-relative coordinator handle budget.
 *
 * What: six slots cover two caller parents, active source/destination directory
 * cursors, and simultaneous source/destination streaming files. Why: five is
 * sufficient for a flat/root-file copy but deadlocks a nested file copy when
 * the sixth destination file allocation retries forever. The earlier pool of
 * three also forced unsafe handle reuse/global-CWD mutation. Inputs/outputs:
 * compile-time RAM allocation only; the exact increase remains inside the
 * enforced 9 KB singleton budget. Affiliates: child APIs, recursive copy,
 * replace recovery, and openFiles[] allocation.
 */
#define AFATFS_MAX_OPEN_FILES 6

/*
 * Shared tree-operation transfer size.
 *
 * What: recursive copy moves at most 96 bytes between source and destination
 * before yielding; replace journaling reuses the same storage for one complete
 * 69-byte fixed record. Why: these user-initiated save operations may be
 * slower, while the 32 bytes recovered from the initial 128-byte choice offset
 * part of the required sixth handle. Inputs/outputs: compile-time byte count
 * only; fread/fwrite may make still smaller sector-boundary progress.
 * Affiliates: afatfsTreeWorkspace_t, copy streaming, journal serialization,
 * and the 9 KB afatfs RAM budget.
 */
#define AFATFS_TREE_IO_BUFFER_SIZE 96u

#define AFATFS_DEFAULT_FILE_DATE FAT_MAKE_DATE(2015, 12, 01)
#define AFATFS_DEFAULT_FILE_TIME FAT_MAKE_TIME(00, 00, 00)

/* Multi-block write disabled — single-block writes sufficient for kit save. */
/* #define AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT 4 */

#define AFATFS_FILES_PER_DIRECTORY_SECTOR (AFATFS_SECTOR_SIZE / sizeof(fatDirectoryEntry_t))

#define AFATFS_FAT32_FAT_ENTRIES_PER_SECTOR  (AFATFS_SECTOR_SIZE / sizeof(uint32_t))
#define AFATFS_FAT16_FAT_ENTRIES_PER_SECTOR (AFATFS_SECTOR_SIZE / sizeof(uint16_t))

// We will read from the file
#define AFATFS_FILE_MODE_READ             1
// We will write to the file
#define AFATFS_FILE_MODE_WRITE            2
// We will append to the file, may not be combined with the write flag
#define AFATFS_FILE_MODE_APPEND           4
// File will occupy a series of superclusters (only valid for creating new files):
#define AFATFS_FILE_MODE_CONTIGUOUS       8
// File should be created if it doesn't exist:
#define AFATFS_FILE_MODE_CREATE           16
// The file's directory entry should be locked in cache so we can read it with no latency:
#define AFATFS_FILE_MODE_RETAIN_DIRECTORY 32

// Open the cache sector for read access (it will be read from disk)
#define AFATFS_CACHE_READ         1
// Open the cache sector for write access (it will be marked dirty)
#define AFATFS_CACHE_WRITE        2
// Lock this sector to prevent its state from transitioning (prevent flushes to disk)
#define AFATFS_CACHE_LOCK         4
// Discard this sector in preference to other sectors when it is in the in-sync state
#define AFATFS_CACHE_DISCARDABLE  8
// Increase the retain counter of the cache sector to prevent it from being discarded when in the in-sync state
#define AFATFS_CACHE_RETAIN       16

/* AFATFS_USE_FREEFILE removed — not needed for LXR-02 kit load/save */
/* AFATFS_USE_INTROSPECTIVE_LOGGING removed */

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef enum {
    AFATFS_SAVE_DIRECTORY_NORMAL,
    AFATFS_SAVE_DIRECTORY_FOR_CLOSE,
    AFATFS_SAVE_DIRECTORY_DELETED,
} afatfsSaveDirectoryEntryMode_e;

typedef enum {
    AFATFS_CACHE_STATE_EMPTY,
    AFATFS_CACHE_STATE_IN_SYNC,
    AFATFS_CACHE_STATE_READING,
    AFATFS_CACHE_STATE_WRITING,
    AFATFS_CACHE_STATE_DIRTY
} afatfsCacheBlockState_e;

typedef enum {
    AFATFS_FILE_TYPE_NONE,
    AFATFS_FILE_TYPE_NORMAL,
    AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY,
    AFATFS_FILE_TYPE_DIRECTORY
} afatfsFileType_e;

typedef enum {
    CLUSTER_SEARCH_FREE_AT_BEGINNING_OF_FAT_SECTOR,
    CLUSTER_SEARCH_FREE,
    CLUSTER_SEARCH_OCCUPIED,
} afatfsClusterSearchCondition_e;

enum {
    AFATFS_CREATEFILE_PHASE_INITIAL = 0,
    AFATFS_CREATEFILE_PHASE_FIND_FILE,
    AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE,
    AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE,
    AFATFS_CREATEFILE_PHASE_SUCCESS,
    AFATFS_CREATEFILE_PHASE_FAILURE,
};

typedef enum {
    AFATFS_FIND_CLUSTER_IN_PROGRESS,
    AFATFS_FIND_CLUSTER_FOUND,
    AFATFS_FIND_CLUSTER_FATAL,
    AFATFS_FIND_CLUSTER_NOT_FOUND,
} afatfsFindClusterStatus_e;

struct afatfsFileOperation_t;

typedef union afatfsFATSector_t {
    uint8_t *bytes;
    uint16_t *fat16;
    uint32_t *fat32;
} afatfsFATSector_t;

typedef struct afatfsCacheBlockDescriptor_t {
    /*
     * The physical sector index on disk that this cached block corresponds to
     */
    uint32_t sectorIndex;

    // We use an increasing timestamp to identify cache access times.

    // This is the timestamp that this sector was first marked dirty at (so we can flush sectors in write-order).
    uint32_t writeTimestamp;

    // This is the last time the sector was accessed
    uint32_t accessTimestamp;

    /* This is set to non-zero when we expect to write a consecutive series of this many blocks (including this block),
     * so we will tell the SD-card to pre-erase those blocks.
     *
     * This counter only needs to be set on the first block of a consecutive write (though setting it, appropriately
     * decreased, on the subsequent blocks won't hurt).
     */
    uint16_t consecutiveEraseBlockCount;

    afatfsCacheBlockState_e state;

    /*
     * The state of this block must not transition (do not flush to disk, do not discard). This is useful for a sector
     * which is currently being written to by the application (so flushing it would be a waste of time).
     *
     * This is a binary state rather than a counter because we assume that only one party will be responsible for and
     * so consider locking a given sector.
     */
    unsigned locked:1;

    /*
     * A counter for how many parties want this sector to be retained in memory (not discarded). If this value is
     * non-zero, the sector may be flushed to disk if dirty but must remain in the cache. This is useful if we require
     * a directory sector to be cached in order to meet our response time requirements.
     */
    unsigned retainCount:6;

    /*
     * If this block is in the In Sync state, it should be discarded from the cache in preference to other blocks.
     * This is useful for data that we don't expect to read again, e.g. data written to an append-only file. This hint
     * is overridden by the locked and retainCount flags.
     */
    unsigned discardable:1;
} afatfsCacheBlockDescriptor_t;

typedef enum {
    AFATFS_FAT_PATTERN_UNTERMINATED_CHAIN,
    AFATFS_FAT_PATTERN_TERMINATED_CHAIN,
    AFATFS_FAT_PATTERN_FREE
} afatfsFATPattern_e;

typedef enum {
    AFATFS_FREE_SPACE_SEARCH_PHASE_FIND_HOLE,
    AFATFS_FREE_SPACE_SEARCH_PHASE_GROW_HOLE
} afatfsFreeSpaceSearchPhase_e;

typedef struct afatfsFreeSpaceSearch_t {
    uint32_t candidateStart;
    uint32_t candidateEnd;
    uint32_t bestGapStart;
    uint32_t bestGapLength;
    afatfsFreeSpaceSearchPhase_e phase;
} afatfsFreeSpaceSearch_t;

typedef struct afatfsFreeSpaceFAT_t {
    uint32_t startCluster;
    uint32_t endCluster;
} afatfsFreeSpaceFAT_t;

typedef struct afatfsCreateFile_t {
    /* Legacy completion retained for current-directory API compatibility. */
    afatfsFileCallback_t callback;
    /* Structured completion used by parent-relative APIs; mutually optional with callback. */
    afatfsOpenResultCallback_t resultCallback;

    /*
     * Retained parent and explicit collision policy for one create/open scan.
     *
     * What: parent replaces hard-coded afatfs.currentDirectory access;
     * createMode separates missing-object behavior from access/truncation mode;
     * parentRetained records ownership of parent->childOperationRetainCount.
     * Why: source and destination parents must coexist during copy/replace, and
     * a parent must not be closed or rebound while its cursor/cache is used by
     * this asynchronous scan.
     * Inputs: initialized by afatfs_createFileInternal() before the first poll.
     * Outputs: terminalResult is delivered to resultCallback; retained parent
     * ownership is released before either callback fires.
     * Affiliates: afatfs_*Child(), afatfs_createFileContinue(), directory
     * extension handoff, afatfs_fclose(), and afatfs_chdir().
     */
    afatfsFilePtr_t parent;
    afatfsCreateMode_t createMode;
    afatfsResultCode_t terminalResult;
    uint8_t parentRetained;
    /* Access mode "w" requests truncation after policy resolves an existing file. */
    uint8_t truncateOnOpen;

    uint8_t phase;
    uint8_t filename[FAT_FILENAME_LENGTH];
    uint8_t shortNameCaseFlags;
    /*
     * Optional long-name creation state.
     *
     * The original asyncfatfs create path only carries one 11-byte FAT short
     * name. The LFN-capable path keeps that exact SFN in filename[] so all
     * existing open/truncate/close logic still works, and adds a bounded ASCII
     * display component used only when a new directory entry must be created.
     * openNameOut is caller-owned storage that receives the generated 8.3 alias
     * once the request has selected it; callers can keep using short-alias
     * opens without learning raw FAT internals. freeRunStart/freeRunLength are
     * populated while scanning the current directory for a sector-local run
     * large enough to hold LFN entries plus the final SFN entry.
     *
     * shortNameCaseFlags describes filename[]: the raw 8.3 key remains
     * uppercase for FAT lookup, while these ntReserved bits preserve
     * all-lowercase display names such as kitset.kcg and slakd1.drm without
     * changing case-insensitive open behavior.
     */
    uint8_t longNameEnabled;
    uint8_t lfnEntryCount;
    uint8_t freeRunLength;
    uint8_t scanLongNameValid;
    uint8_t scanLongNameChecksum;
    afatfsMatchMode_t matchMode;
    uint16_t aliasOrdinal;
    char longName[AFATFS_LONG_FILENAME_MAX + 1u];
    char scanLongName[AFATFS_LONG_FILENAME_MAX + 1u];
    char *openNameOut;
    afatfsDirEntryPointer_t freeRunStart;
} afatfsCreateFile_t;

typedef enum {
    AFATFS_RENAME_OBJECT_PHASE_INITIAL = 0,
    AFATFS_RENAME_OBJECT_PHASE_VALIDATE_SOURCE,
    AFATFS_RENAME_OBJECT_PHASE_CHECK_DEST_ANCESTRY,
    AFATFS_RENAME_OBJECT_PHASE_FIND_SOURCE,
    AFATFS_RENAME_OBJECT_PHASE_LOAD_SOURCE_ENTRY,
    AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_NAME,
    AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN,
    AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN,
    AFATFS_RENAME_OBJECT_PHASE_WAIT_EXTEND,
    AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN,
    AFATFS_RENAME_OBJECT_PHASE_SYNC_DESTINATION,
    AFATFS_RENAME_OBJECT_PHASE_UPDATE_DOTDOT,
    AFATFS_RENAME_OBJECT_PHASE_SYNC_DOTDOT,
    AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN,
    AFATFS_RENAME_OBJECT_PHASE_FINISH,
} afatfsRenameObjectPhase_e;

typedef struct afatfsRenameObject_t {
    uint8_t active;
    uint8_t succeeded;
    uint8_t movedEntryRun;
    uint8_t oldEntryCount;
    afatfsRenameObjectPhase_e phase;
    afatfsMatchMode_t matchMode;
    afatfsCallback_t callback;
    /*
     * Explicit-parent/by-identity rename ownership.
     *
     * parent replaces global currentDirectory access and is retained for the
     * complete scan/write/retire lifecycle. byIdentity selects validation of
     * requestedSource instead of a display-name source scan. resultCallback
     * receives terminalResult for the structured API; legacy callback ignores
     * it. Affiliates: afatfs_renameObjectAt(), legacy rename, object validation,
     * and future move/replace promotion.
     */
    afatfsDirHandle_t parent;
    afatfsDirHandle_t destinationParent;
    afatfsObjectId_t requestedSource;
    uint8_t byIdentity;
    uint8_t parentRetained;
    uint8_t destinationParentRetained;
    uint8_t moveAcrossParents;
    uint8_t ancestryDepth;
    uint32_t ancestryCluster;
    afatfsResultCallback_t resultCallback;
    afatfsResultCode_t terminalResult;
    char oldName[AFATFS_LONG_FILENAME_MAX + 1u];
    char newName[AFATFS_LONG_FILENAME_MAX + 1u];
    char generatedOpenName[AFATFS_SHORT_FILENAME_MAX];
    char *openNameOut;
    afatfsObjectFinder_t objectFinder;
    afatfsObjectInfo_t source;
    fatDirectoryEntry_t sourceEntry;
    afatfsCreateFile_t newNameState;
    afatfsFinder_t rawFinder;
    afatfsDirEntryPointer_t oldRunStart;
    afatfsDirEntryPointer_t newRunStart;
} afatfsRenameObject_t;

typedef enum {
    AFATFS_REMOVE_OBJECTS_PHASE_INITIAL = 0,
    AFATFS_REMOVE_OBJECTS_PHASE_SCAN,
    AFATFS_REMOVE_OBJECTS_PHASE_LOAD_ENTRY,
    AFATFS_REMOVE_OBJECTS_PHASE_TRUNCATE_FILE,
    AFATFS_REMOVE_OBJECTS_PHASE_RETIRE_NAME_RUN,
    AFATFS_REMOVE_OBJECTS_PHASE_RESTART_SCAN,
    AFATFS_REMOVE_OBJECTS_PHASE_FINISH,
} afatfsRemoveObjectsPhase_e;

typedef struct afatfsSeek_t {
    afatfsFileCallback_t callback;
    /*
     * Structured create/open completion transferred into an asynchronous seek.
     *
     * Appending to an existing file seeks to logical EOF after CREATE_FILE
     * state has been replaced. resultCallback and retainedParent preserve that
     * outer completion/ownership across the union transition. Ordinary seeks
     * leave both NULL. Affiliates: afatfs_fseekInternal(), create success, and
     * parent-relative fopenChild("a").
     */
    afatfsOpenResultCallback_t resultCallback;
    afatfsFilePtr_t retainedParent;

    uint32_t seekOffset;
} afatfsSeek_t;

typedef enum {
    AFATFS_APPEND_SUPERCLUSTER_PHASE_INIT = 0,
    AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FREEFILE_DIRECTORY,
    AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FAT,
    AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FILE_DIRECTORY,
} afatfsAppendSuperclusterPhase_e;

typedef struct afatfsAppendSupercluster_t {
    uint32_t previousCluster;
    uint32_t fatRewriteStartCluster;
    uint32_t fatRewriteEndCluster;
    afatfsAppendSuperclusterPhase_e phase;
} afatfsAppendSupercluster_t;

typedef enum {
    AFATFS_APPEND_FREE_CLUSTER_PHASE_INITIAL = 0,
    AFATFS_APPEND_FREE_CLUSTER_PHASE_FIND_FREESPACE = 0,
    AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FAT1,
    AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FAT2,
    AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FILE_DIRECTORY,
    AFATFS_APPEND_FREE_CLUSTER_PHASE_COMPLETE,
    AFATFS_APPEND_FREE_CLUSTER_PHASE_FAILURE,
} afatfsAppendFreeClusterPhase_e;

typedef struct afatfsAppendFreeCluster_t {
    uint32_t previousCluster;
    uint32_t searchCluster;
    afatfsAppendFreeClusterPhase_e phase;
} afatfsAppendFreeCluster_t;

typedef enum {
    AFATFS_EXTEND_SUBDIRECTORY_PHASE_INITIAL = 0,
    AFATFS_EXTEND_SUBDIRECTORY_PHASE_ADD_FREE_CLUSTER = 0,
    AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS,
    AFATFS_EXTEND_SUBDIRECTORY_PHASE_SUCCESS,
    AFATFS_EXTEND_SUBDIRECTORY_PHASE_FAILURE
} afatfsExtendSubdirectoryPhase_e;

typedef struct afatfsExtendSubdirectory_t {
    // We need to call this as a sub-operation so we have it as our first member to be compatible with its memory layout:
    afatfsAppendFreeCluster_t appendFreeCluster;

    afatfsExtendSubdirectoryPhase_e phase;

    uint32_t parentDirectoryCluster;
    afatfsFileCallback_t callback;
    /*
     * Structured mkdir handoff state.
     *
     * What: resultCallback mirrors callback for parent-relative directory
     * creation; retainedParent carries the create scanner's parent lock across
     * the operation-union handoff; destroyOnFailure distinguishes a new child
     * from extension of an existing directory.
     * Why: CREATE_FILE state is overwritten before `.`/`..` initialization,
     * so these values must travel with EXTEND_SUBDIRECTORY to guarantee one
     * callback and one parent release on every terminal path.
     * Inputs/outputs: supplied only by the create handoff; ordinary extension
     * passes NULL/zero. Affiliates: afatfs_handoffCreatedDirectoryToInitializer()
     * and afatfs_extendSubdirectoryContinue().
     */
    afatfsOpenResultCallback_t resultCallback;
    afatfsFilePtr_t retainedParent;
    uint8_t destroyOnFailure;
} afatfsExtendSubdirectory_t;

typedef enum {
    AFATFS_TRUNCATE_FILE_INITIAL = 0,
    AFATFS_TRUNCATE_FILE_UPDATE_DIRECTORY = 0,
    AFATFS_TRUNCATE_FILE_ERASE_FAT_CHAIN_NORMAL,
#ifdef AFATFS_USE_FREEFILE
    AFATFS_TRUNCATE_FILE_ERASE_FAT_CHAIN_CONTIGUOUS,
    AFATFS_TRUNCATE_FILE_PREPEND_TO_FREEFILE,
#endif
    AFATFS_TRUNCATE_FILE_SUCCESS,
} afatfsTruncateFilePhase_e;

typedef struct afatfsTruncateFile_t {
    uint32_t startCluster; // First cluster to erase
    uint32_t currentCluster; // Used to mark progress
    uint32_t endCluster; // Optional, for contiguous files set to 1 past the end cluster of the file, otherwise set to 0
    afatfsFileCallback_t callback;
    /*
     * Structured create/open completion transferred into truncate.
     *
     * Opening an existing file with access mode "w" replaces CREATE_FILE with
     * TRUNCATE before the public open has completed. These fields retain the
     * result callback and parent lock until truncation reaches a terminal
     * result. Ordinary ftruncate/unlink callers leave them NULL.
     * Affiliates: afatfs_ftruncateInternal() and afatfs_createFileContinue().
     */
    afatfsOpenResultCallback_t resultCallback;
    afatfsFilePtr_t retainedParent;
    afatfsTruncateFilePhase_e phase;
} afatfsTruncateFile_t;

typedef enum {
    AFATFS_DELETE_FILE_DELETE_DIRECTORY_ENTRY,
    AFATFS_DELETE_FILE_DEALLOCATE_CLUSTERS,
} afatfsDeleteFilePhase_e;

typedef struct afatfsDeleteFile_t {
    afatfsTruncateFile_t truncateFile;
    afatfsCallback_t callback;
} afatfsUnlinkFile_t;

typedef struct afatfsCloseFile_t {
    afatfsCallback_t callback;
} afatfsCloseFile_t;

typedef enum {
    AFATFS_DELETE_TREE_INITIAL,
    AFATFS_DELETE_TREE_VALIDATE_ROOT,
    AFATFS_DELETE_TREE_OPEN_DIR,
    AFATFS_DELETE_TREE_SCAN,
    AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND,
    AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF,
    AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP,
    AFATFS_DELETE_TREE_DESCEND_DIR,
    AFATFS_DELETE_TREE_RETIRE_ENTRIES,
    AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS,
    AFATFS_DELETE_TREE_SUCCESS
} afatfsDeleteTreePhase_e;

typedef struct afatfsDeleteTree_t {
    afatfsObjectId_t rootId;
    afatfsObjectId_t currentTarget;
    uint8_t currentTargetHasLongName;
    uint32_t currentCluster;
    uint32_t targetClusterToRetire;
    /*
     * Bounded structural depth for cycle/corruption protection.
     * Root is depth zero; descent increments before rebinding to a child and
     * validated ascent decrements before resuming the parent scan. The value
     * never indexes RAM, but shares AFATFS_TREE_DEPTH_MAX with future copy/move
     * walkers so all product-tree operations accept the same schema depth.
     */
    uint8_t depth;
    /*
     * LFN-aware traversal state must be stored at its complete declared size.
     *
     * What: finder follows raw FAT entries while retaining VFAT checksum,
     * fragment-name, and first-entry metadata between poll calls. Why: the
     * original native delete member was only afatfsFinder_t and its callers
     * cast it to afatfsObjectFinder_t. afatfs_objectScanReset() then cleared
     * past that smaller allocation and erased callback/phase state, producing
     * filesystem ERR TOut06 after the low-level handle disappeared. Output:
     * object iteration now owns only this correctly sized member and needs no
     * unsafe casts. Affiliates: afatfs_findFirstObject(),
     * afatfs_findNextObject(), and afatfs_findLastObject().
     */
    afatfsObjectFinder_t finder;
    afatfsResultCallback_t callback;
    afatfsDeleteTreePhase_e phase;
    afatfsDeleteTreePhase_e lastPhase;
    uint32_t timeoutTicks;
} afatfsDeleteTree_t;

typedef enum {
    AFATFS_FILE_OPERATION_NONE,
    AFATFS_FILE_OPERATION_CREATE_FILE,
    AFATFS_FILE_OPERATION_SEEK, // Seek the file's cursorCluster forwards by seekOffset bytes
    AFATFS_FILE_OPERATION_CLOSE,
    AFATFS_FILE_OPERATION_TRUNCATE,
    AFATFS_FILE_OPERATION_UNLINK,
#ifdef AFATFS_USE_FREEFILE
    AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER,
    AFATFS_FILE_OPERATION_LOCKED,
#endif
    AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER,
    AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY,
    AFATFS_FILE_OPERATION_DELETE_TREE,
    /*
     * Multi-handle operations deliberately do not appear in this per-file
     * dispatcher.
     *
     * What changed: the former MOVE_OBJECT/COPY_TREE/REPLACE_TREE values and
     * empty union states were removed. Why: those public starts could claim to
     * accept work, then poll a no-op continuation forever without callback.
     * Inputs/outputs: no runtime input is affected; unavailable operations now
     * fail at compile time. Affiliates: future global coordinators in afatfs_t,
     * asyncfatfs.h's availability boundary, and AFATFS expansion Phases 4-6.
     */
} afatfsFileOperation_e;

typedef struct afatfsFileOperation_t {
    afatfsFileOperation_e operation;
    union {
        afatfsCreateFile_t createFile;
        afatfsSeek_t seek;
        afatfsAppendSupercluster_t appendSupercluster;
        afatfsAppendFreeCluster_t appendFreeCluster;
        afatfsExtendSubdirectory_t extendSubdirectory;
        afatfsUnlinkFile_t unlinkFile;
        afatfsTruncateFile_t truncateFile;
        afatfsCloseFile_t closeFile;
        afatfsDeleteTree_t deleteTree;
    } state;
} afatfsFileOperation_t;

typedef struct afatfsFile_t {
    afatfsFileType_e type;

    // The byte offset of the cursor within the file
    uint32_t cursorOffset;

    /* The file size in bytes as seen by users of the filesystem (the exact length of the file they've written).
     *
     * This is only used by users of the filesystem, not us, so it only needs to be up to date for fseek() (to clip
     * seeks to the EOF), fread(), feof(), and fclose() (which writes the logicalSize to the directory).
     *
     * It becomes out of date when we fwrite() to extend the length of the file. In this situation, feof() is properly
     * true, so we don't have to update the logicalSize for fread() or feof() to get the correct result. We only need
     * to update it when we seek backwards (so we don't forget the logical EOF position), or fclose().
     */
    uint32_t logicalSize;

    /* The allocated size in bytes based on how many clusters have been assigned to the file. Always a multiple of
     * the cluster size.
     *
     * This is an underestimate for existing files, because we don't bother to check precisely how long the chain is
     * at the time the file is opened (it might be longer than needed to contain the logical size), but assuming the
     * filesystem metadata is correct, it should always be at least as many clusters as needed to contain logicalSize.
     *
     * Since this is an estimate, we only use it to exaggerate the filesize in the directory entry of a file that is
     * currently being written (so that the final cluster of the file will be entirely readable if power is lost before
     * we can could update the directory entry with a new logicalSize).
     */
    uint32_t physicalSize;

    /*
     * The cluster that the file pointer is currently within. When seeking to the end of the file, this will be
     * set to zero.
     */
    uint32_t cursorCluster;

    /*
     * The cluster before the one the file pointer is inside. This is set to zero when at the start of the file.
     */
    uint32_t cursorPreviousCluster;

    uint8_t mode; // A combination of AFATFS_FILE_MODE_* flags
    uint8_t attrib; // Combination of FAT_FILE_ATTRIBUTE_* flags for the directory entry of this file
    /*
     * Count of accepted parent-relative operations currently using this handle.
     *
     * The first implementation permits only zero or one owner. It exists
     * outside the operation union so child creation can hand off from CREATE
     * to EXTEND without losing the lock. afatfs_fclose()/afatfs_chdir() reject
     * retained handles; the child terminal helper decrements before callback.
     * Affiliates: afatfsCreateFile_t and future move/copy coordinators.
     */
    uint8_t childOperationRetainCount;

    /* We hold on to one sector entry in the cache and remember its index here. The cache is invalidated when we
     * seek across a sector boundary. This allows fwrite() to complete faster because it doesn't need to check the
     * cache on every call.
     */
    int8_t writeLockedCacheIndex;
    // Ditto for fread():
    int8_t readRetainCacheIndex;

    // The position of our directory entry on the disk (so we can update it without consulting a parent directory file)
    afatfsDirEntryPointer_t directoryEntryPos;

    // The first cluster number of the file, or 0 if this file is empty
    uint32_t firstCluster;

    // State for a queued operation on the file
    struct afatfsFileOperation_t operation;
} afatfsFile_t;

/*
 * Global same-name object removal operation.
 *
 * What: Owns the asynchronous scan/delete loop used before case-insensitive
 * overwrite. It scans the current directory, removes one matching physical
 * object, restarts the scan, and repeats until no matches remain.
 *
 * Why: FAT directories can contain externally-created names that differ only by
 * case. Product overwrite must collapse those variants into one newly written
 * object. Restarting after each deletion avoids keeping a raw finder cursor
 * alive across mutations to the same directory sector.
 *
 * Inputs retained here: displayName and matchMode define the product identity;
 * mode restricts whether directories are ignored or only empty directories may
 * be retired. callback returns control to filesystem.c once all matching
 * objects are gone.
 *
 * Outputs/effects: file cluster chains are freed through the existing truncate
 * code path, and complete LFN/SFN name runs are retired through the shared
 * name-run helper. The syntheticFile never escapes asyncfatfs.
 *
 * Affiliates/clients: afatfs_poll(), afatfs_findNextObject(),
 * afatfs_ftruncateContinue(), afatfs_retireObjectNameRun(), filesystem.c
 * overwrite preflight.
 */
typedef struct afatfsRemoveObjects_t {
    /* Non-zero while the global remove-by-display-name state machine owns the current directory scan. */
    uint8_t active;
    /* Latched terminal result used by the FINISH phase before the callback releases the slot. */
    uint8_t succeeded;
    /* Current async phase; each phase performs at most the next disk/cache-dependent step. */
    afatfsRemoveObjectsPhase_e phase;
    /* Caller-selected display-name comparison policy, normally case-insensitive for overwrite. */
    afatfsMatchMode_t matchMode;
    /* Caller-selected object scope; directory deletion is intentionally disabled until recursive support. */
    afatfsRemoveObjectMode_t mode;
    /*
     * Nonzero selects exact short-alias matching instead of display-name
     * matching.
     *
     * What/why: overwrite cleanup wants all same-display variants, so it keeps
     * matchShortName clear and compares displayName. Recursive directory
     * cleanup sometimes starts from one concrete afatfsObjectInfo_t and must
     * later retire that exact physical entry, especially on cards that already
     * contain duplicate LFNs. In that case afatfs_removeObject() sets this flag
     * and stores the object's printable shortName below.
     */
    uint8_t matchShortName;
    /* Exclusive currentDirectory lifetime held from accepted start to callback. */
    uint8_t parentRetained;
    /* Completion callback supplied by filesystem.c or diagnostics; it receives no payload. */
    afatfsCallback_t callback;
    /* Sanitized target component copied once so caller-owned menu buffers can change while we scan. */
    char displayName[AFATFS_LONG_FILENAME_MAX + 1u];
    /* Exact printable 8.3 alias used only when matchShortName is nonzero. */
    char shortName[AFATFS_SHORT_FILENAME_MAX];
    /* LFN-aware scanner state for the current pass through afatfs.currentDirectory. */
    afatfsObjectFinder_t finder;
    /* Most recently matched object; retained across load/truncate/retire phases. */
    afatfsObjectInfo_t object;
    /* Copy of the matched SFN directory entry, used to seed syntheticFile before truncating. */
    fatDirectoryEntry_t sourceEntry;
    /* Private file handle used only to reuse the existing cluster-chain truncate logic. */
    afatfsFile_t syntheticFile;
} afatfsRemoveObjects_t;

typedef enum {
    AFATFS_COPY_TREE_VALIDATE_SOURCE = 0,
    AFATFS_COPY_TREE_LOAD_SOURCE_ENTRY,
    AFATFS_COPY_TREE_CREATE_ROOT,
    AFATFS_COPY_TREE_WAIT_CREATE_ROOT,
    AFATFS_COPY_TREE_PREPARE_DIRECTORY,
    AFATFS_COPY_TREE_SCAN_DIRECTORY,
    AFATFS_COPY_TREE_LOAD_CHILD_ENTRY,
    AFATFS_COPY_TREE_CREATE_CHILD,
    AFATFS_COPY_TREE_WAIT_CREATE_CHILD,
    AFATFS_COPY_TREE_APPLY_METADATA,
    AFATFS_COPY_TREE_STREAM_READ,
    AFATFS_COPY_TREE_STREAM_WRITE,
    AFATFS_COPY_TREE_CLOSE_SOURCE_FILE,
    AFATFS_COPY_TREE_CLOSE_DESTINATION_FILE,
    AFATFS_COPY_TREE_DESCEND_DIRECTORY,
    AFATFS_COPY_TREE_ASCEND_DIRECTORY,
    AFATFS_COPY_TREE_RESUME_PARENT,
    AFATFS_COPY_TREE_SYNC,
    AFATFS_COPY_TREE_CLEANUP,
} afatfsCopyTreePhase_e;

/*
 * Compact resume state for one recursive-copy depth.
 *
 * What: source/destination clusters reconstruct the two parent cursors after a
 * child directory is exhausted; sourceChildEntry plus rawShortName identify
 * the already-copied child that a source-parent rescan must consume before it
 * resumes. Why: a full object ID plus finder pair costs 180 bytes per side and
 * per depth. This 28-byte physical key is sufficient because copy never mutates
 * the source tree. Inputs are captured immediately before descent; output is
 * consumed only by ASCEND/RESUME. Affiliates: AFATFS_TREE_DEPTH_MAX and the
 * source-rescan latency/RAM trade documented in AFATFS_EXPANSION_PLAN.md.
 */
typedef struct {
    uint32_t sourceParentCluster;
    uint32_t destinationParentCluster;
    afatfsDirEntryPointer_t sourceChildEntry;
    uint8_t rawShortName[FAT_FILENAME_LENGTH];
} afatfsCopyTreeFrame_t;

/*
 * Reduced-RAM global recursive-copy coordinator.
 *
 * What: owns exactly one active finder, current object/metadata, compact depth
 * frames, four transient handle pointers, and byte-stream offsets. Why: the
 * operation composes several existing async child/file primitives and cannot
 * live in any one file handle's operation union. Inputs are copied by the start
 * function; outputs are a single result callback plus a deliberately preserved
 * partial destination on failure. Affiliates: afatfs_copyObjectTree(),
 * afatfs_copyTreeContinue(), and afatfsTreeWorkspace_t.
 */
typedef struct {
    uint8_t active;
    uint8_t depth;
    uint8_t finderActive;
    /* Source/destination hold bits prevent double release when both are one handle. */
    uint8_t sourceParentHeld;
    uint8_t destinationParentHeld;
    /* Nonzero while currentObject/destinationName describe the copied root. */
    uint8_t creatingRoot;
    afatfsCopyTreePhase_e phase;
    afatfsResultCode_t terminalResult;
    afatfsCopyFlags_t flags;
    afatfsResultCallback_t callback;
    afatfsDirHandle_t sourceParent;
    afatfsDirHandle_t destinationParent;
    afatfsFilePtr_t sourceDirectory;
    afatfsFilePtr_t destinationDirectory;
    afatfsFilePtr_t sourceFile;
    afatfsFilePtr_t destinationFile;
    /* Child-create callbacks publish here before metadata is applied/adopted. */
    afatfsFilePtr_t createdHandle;
    afatfsObjectInfo_t currentObject;
    fatDirectoryEntry_t sourceEntry;
    afatfsObjectFinder_t finder;
    afatfsCopyTreeFrame_t frames[AFATFS_TREE_DEPTH_MAX];
    uint16_t bufferedBytes;
    uint16_t bufferOffset;
    char destinationName[AFATFS_LONG_FILENAME_MAX + 1u];
} afatfsCopyTree_t;

/* On-card transaction record constants; both journal slots use this ABI. */
#define AFATFS_REPLACE_JOURNAL_MAGIC 0x54414641u /* Little-endian "AFAT". */
#define AFATFS_REPLACE_JOURNAL_VERSION 1u
#define AFATFS_REPLACE_JOURNAL_SLOT0 "AFATJ0.SYS"
#define AFATFS_REPLACE_JOURNAL_SLOT1 "AFATJ1.SYS"

typedef enum {
    AFATFS_REPLACE_JOURNAL_CLEAN = 0,
    AFATFS_REPLACE_JOURNAL_PREPARED,
    AFATFS_REPLACE_JOURNAL_OLD_RENAMED,
    AFATFS_REPLACE_JOURNAL_PROMOTED,
} afatfsReplaceJournalState_e;

/*
 * Fixed CRC-protected journal payload stored alternately in two parent files.
 *
 * What: magic/version validate the schema, sequence orders slots, state selects
 * recovery, nonce regenerates exact scratch names, and targetName identifies
 * the one product component authorized for mutation. Why: a torn newer sector
 * must not destroy the prior recovery decision. Inputs are serialized before a
 * sync barrier; crc32 covers every preceding byte. Output fits inside the
 * shared 96-byte workspace. Affiliates: journal read/write helpers and recovery.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t state;
    uint8_t objectKind;
    uint8_t reserved;
    uint32_t sequence;
    uint32_t nonce;
    char targetName[AFATFS_LONG_FILENAME_MAX + 1u];
    uint32_t crc32;
} afatfsReplaceJournalRecord_t;

/* Compile-time guard: one journal record must fit the shared transfer bytes. */
typedef char afatfsReplaceJournalFitsSharedBuffer[
    sizeof(afatfsReplaceJournalRecord_t) <= AFATFS_TREE_IO_BUFFER_SIZE ? 1 : -1];

typedef enum {
    AFATFS_REPLACE_MODE_NONE = 0,
    AFATFS_REPLACE_MODE_BEGIN,
    AFATFS_REPLACE_MODE_COMMIT,
    AFATFS_REPLACE_MODE_ABORT,
    AFATFS_REPLACE_MODE_RECOVER,
} afatfsReplaceMode_e;

typedef enum {
    AFATFS_REPLACE_BEGIN_CREATE_STAGE = 0,
    AFATFS_REPLACE_BEGIN_WAIT_STAGE,
    AFATFS_REPLACE_CLOSE_STAGE,
    AFATFS_REPLACE_WAIT_CLOSE_STAGE,
    AFATFS_REPLACE_SYNC_STAGE,
    AFATFS_REPLACE_READ_JOURNAL_OPEN,
    AFATFS_REPLACE_READ_JOURNAL_WAIT_OPEN,
    AFATFS_REPLACE_READ_JOURNAL_DATA,
    AFATFS_REPLACE_READ_JOURNAL_CLOSE,
    AFATFS_REPLACE_READ_JOURNAL_AFTER_CLOSE,
    AFATFS_REPLACE_AFTER_JOURNALS,
    AFATFS_REPLACE_WRITE_JOURNAL_OPEN,
    AFATFS_REPLACE_WRITE_JOURNAL_WAIT_OPEN,
    AFATFS_REPLACE_WRITE_JOURNAL_DATA,
    AFATFS_REPLACE_WRITE_JOURNAL_CLOSE,
    AFATFS_REPLACE_WRITE_JOURNAL_AFTER_CLOSE,
    AFATFS_REPLACE_WRITE_JOURNAL_SYNC,
    AFATFS_REPLACE_SCAN_BEGIN,
    AFATFS_REPLACE_SCAN_NEXT,
    AFATFS_REPLACE_MOVE_FOUND,
    AFATFS_REPLACE_WAIT_MOVE,
    AFATFS_REPLACE_DELETE_FOUND,
    AFATFS_REPLACE_WAIT_DELETE,
    AFATFS_REPLACE_SYNC_NAMESPACE,
    AFATFS_REPLACE_FINISH,
} afatfsReplacePhase_e;

typedef enum {
    AFATFS_REPLACE_ACTION_NONE = 0,
    AFATFS_REPLACE_ACTION_COMMIT_CHECK_OLD,
    AFATFS_REPLACE_ACTION_COMMIT_FIND_TARGET,
    AFATFS_REPLACE_ACTION_COMMIT_PROMOTE_NEW,
    AFATFS_REPLACE_ACTION_COMMIT_DELETE_OLD,
    AFATFS_REPLACE_ACTION_ABORT_DELETE_NEW,
    AFATFS_REPLACE_ACTION_RECOVER_CHECK_TARGET,
    AFATFS_REPLACE_ACTION_RECOVER_FIND_NEW,
    AFATFS_REPLACE_ACTION_RECOVER_FIND_OLD,
    AFATFS_REPLACE_ACTION_RECOVER_DELETE_NEW,
    AFATFS_REPLACE_ACTION_RECOVER_DELETE_OLD,
    AFATFS_REPLACE_ACTION_CHECK_UNKNOWN_SCRATCH,
} afatfsReplaceAction_e;

/*
 * Small persistent transaction descriptor returned through the opaque API.
 *
 * What: retains only the explicit parent, staging handle, target component, and
 * nonce needed between begin and commit/abort. Why: the large finder/object and
 * journal execution fields can overlay copy state, but a begun transaction must
 * coexist with tree copy while the caller populates staging. Inputs are copied
 * at begin; outputs/lifetime end after successful commit/abort. Affiliates:
 * afatfsReplaceTransactionPtr_t and afatfsReplaceOperation_t.
 */
struct afatfsReplaceTransaction {
    uint8_t active;
    uint8_t parentHeld;
    uint8_t commitStarted;
    uint8_t reserved;
    uint32_t nonce;
    afatfsDirHandle_t parent;
    afatfsDirHandle_t stagingDirectory;
    char targetName[AFATFS_LONG_FILENAME_MAX + 1u];
};

/*
 * Shared-workspace execution state for begin/commit/abort/recovery.
 *
 * What: one finder/object pair services every exact parent scan; bestJournal is
 * the highest valid slot while the shared byte buffer holds the record being
 * read or written. action/returnPhase compose scans, moves, deletes, and syncs
 * without nested blocking calls. Why: this stays smaller than copy state so the
 * workspace union adds no Phase 6 peak allocation. Affiliates: all public tree
 * replace APIs and afatfs_replaceContinue().
 */
typedef struct {
    uint8_t active;
    uint8_t parentHeld;
    uint8_t finderActive;
    uint8_t journalSlot;
    uint8_t journalValid;
    /* Set when either slot exists, even if torn/CRC-invalid. */
    uint8_t journalSeen;
    /* Equal sequence with different valid bytes has no deterministic winner. */
    uint8_t journalAmbiguous;
    uint8_t found;
    uint16_t ioOffset;
    afatfsReplaceMode_e mode;
    afatfsReplacePhase_e phase;
    afatfsReplacePhase_e returnPhase;
    afatfsReplaceAction_e action;
    afatfsResultCode_t terminalResult;
    afatfsDirHandle_t parent;
    afatfsFilePtr_t journalFile;
    afatfsReplaceTransactionPtr_t transaction;
    afatfsReplaceBeginCallback_t beginCallback;
    afatfsResultCallback_t resultCallback;
    afatfsObjectFinder_t finder;
    afatfsObjectInfo_t foundObject;
    afatfsReplaceJournalRecord_t bestJournal;
    char scanName[AFATFS_LONG_FILENAME_MAX + 1u];
} afatfsReplaceOperation_t;

typedef enum {
    AFATFS_TREE_OPERATION_NONE = 0,
    AFATFS_TREE_OPERATION_COPY,
    AFATFS_TREE_OPERATION_REPLACE,
} afatfsTreeOperationKind_e;

/*
 * Mutually exclusive large-operation workspace.
 *
 * What: copy state overlays the Phase 6 replace/recovery state added below,
 * while ioBuffer is adjacent shared transfer storage. Why: a commit/recovery
 * cannot start while tree copy is active, and retaining both complete state
 * machines would defeat the reduced-RAM design. Output is compile-time global
 * storage only. Affiliates: afatfs_poll() arbitration and transaction state.
 */
typedef struct {
    afatfsTreeOperationKind_e kind;
    union {
        afatfsCopyTree_t copy;
        afatfsReplaceOperation_t replace;
    } operation;
    uint8_t ioBuffer[AFATFS_TREE_IO_BUFFER_SIZE];
} afatfsTreeWorkspace_t;

typedef enum {
    AFATFS_INITIALIZATION_READ_MBR,
    AFATFS_INITIALIZATION_READ_VOLUME_ID,

#ifdef AFATFS_USE_FREEFILE
    AFATFS_INITIALIZATION_FREEFILE_CREATE,
    AFATFS_INITIALIZATION_FREEFILE_CREATING,
    AFATFS_INITIALIZATION_FREEFILE_FAT_SEARCH,
    AFATFS_INITIALIZATION_FREEFILE_UPDATE_FAT,
    AFATFS_INITIALIZATION_FREEFILE_SAVE_DIR_ENTRY,
    AFATFS_INITIALIZATION_FREEFILE_LAST = AFATFS_INITIALIZATION_FREEFILE_SAVE_DIR_ENTRY,
#endif

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING
    AFATFS_INITIALIZATION_INTROSPEC_LOG_CREATE,
    AFATFS_INITIALIZATION_INTROSPEC_LOG_CREATING,
#endif

    AFATFS_INITIALIZATION_DONE
} afatfsInitializationPhase_e;

typedef struct afatfs_t {
    uint8_t cache[AFATFS_SECTOR_SIZE * AFATFS_NUM_CACHE_SECTORS];
    afatfsCacheBlockDescriptor_t cacheDescriptor[AFATFS_NUM_CACHE_SECTORS];
    fatFilesystemType_e filesystemType;

    afatfsFilesystemState_e filesystemState;
    afatfsInitializationPhase_e initPhase;

    // State used during FS initialisation where only one member of the union is used at a time
#ifdef AFATFS_USE_FREEFILE
    union {
        afatfsFreeSpaceSearch_t freeSpaceSearch;
        afatfsFreeSpaceFAT_t freeSpaceFAT;
    } initState;
#endif

    uint32_t cacheTimer;

    int cacheDirtyEntries; // The number of cache entries in the AFATFS_CACHE_STATE_DIRTY state
    bool cacheFlushInProgress;

    afatfsFile_t openFiles[AFATFS_MAX_OPEN_FILES];

#ifdef AFATFS_USE_FREEFILE
    afatfsFile_t freeFile;
#endif

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING
    afatfsFile_t introSpecLog;
#endif

    afatfsError_e lastError;

    bool filesystemFull;

    // The current working directory:
    afatfsFile_t currentDirectory;

    afatfsRenameObject_t renameObject;
    afatfsRemoveObjects_t removeObjects;
    /* One shared, bounded coordinator workspace for recursive copy/replace. */
    afatfsTreeWorkspace_t treeWorkspace;
    /* Descriptor persists while callers populate a begun staging directory. */
    struct afatfsReplaceTransaction replaceTransaction;

    uint32_t partitionStartSector; // The physical sector that the first partition on the device begins at

    uint32_t fatStartSector; // The first sector of the first FAT
    uint32_t fatSectors;     // The size in sectors of a single FAT

    /*
     * Number of clusters available for storing user data. Note that clusters are numbered starting from 2, so the
     * index of the last cluster on the volume is numClusters + 1 !!!
     */
    uint32_t numClusters;
    uint32_t clusterStartSector; // The physical sector that the clusters area begins at
    uint32_t sectorsPerCluster;

    /*
     * Number of the cluster we last allocated (i.e. free->occupied). Searches for a free cluster will begin after this
     * cluster.
     */
    uint32_t lastClusterAllocated;

    /* Mask to be ANDed with a byte offset within a file to give the offset within the cluster */
    uint32_t byteInClusterMask;

    uint32_t rootDirectoryCluster; // Present on FAT32 and set to zero for FAT16
    uint32_t rootDirectorySectors; // Zero on FAT32, for FAT16 the number of sectors that the root directory occupies
} afatfs_t;

/*
 * Compile-time static-RAM budget for the complete filesystem singleton.
 *
 * What: makes any later field/array growth beyond 9,000 decimal bytes fail the
 * ARM build. Why: Phases 5-6 deliberately traded directory rescans and smaller
 * transfers for RAM, and silently regressing toward the original 12 KB design
 * would consume stack headroom. Input is sizeof(afatfs_t); output is a valid
 * one-byte typedef only while the budget holds. Affiliates: the 8,880-byte ABI
 * measurement and AFATFS_EXPANSION_PLAN.md's reduced-RAM contract.
 */
typedef char afatfsStaticRamBudget[
    sizeof(afatfs_t) <= 9000u ? 1 : -1];

static afatfs_t afatfs;

static void afatfs_fileOperationContinue(afatfsFile_t *file);
static void afatfs_initFileHandle(afatfsFilePtr_t file);
static afatfsOperationStatus_e afatfs_validateObjectId(
    afatfsDirHandle_t expectedParent,
    const afatfsObjectId_t *object,
    afatfsResultCode_t *validationResult);
static bool afatfs_parentCanAcceptChild(afatfsDirHandle_t parent);
static bool afatfs_childComponentCanStart(const char *displayName);
static void afatfs_renameObjectContinue(void);
static void afatfs_removeObjectsContinue(void);
static void afatfs_deleteTreeContinue(afatfsFile_t *file);
static void afatfs_copyTreeContinue(void);
static void afatfs_replaceContinue(void);
static uint8_t* afatfs_fileLockCursorSectorForWrite(afatfsFilePtr_t file);
static uint8_t* afatfs_fileRetainCursorSectorForRead(afatfsFilePtr_t file);

static uint32_t roundUpTo(uint32_t value, uint32_t rounding)
{
    uint32_t remainder = value % rounding;

    if (remainder > 0) {
        value += rounding - remainder;
    }

    return value;
}

static bool isPowerOfTwo(unsigned int x)
{
    return ((x != 0) && ((x & (~x + 1)) == x));
}

/**
 * Check for conditions that should always be true (and if otherwise mean a bug or a corrupt filesystem).
 *
 * If the condition is false, the filesystem is marked as being in a fatal state.
 *
 * Returns the value of the condition.
 */
static bool afatfs_assert(bool condition)
{
    if (!condition) {
        if (afatfs.lastError == AFATFS_ERROR_NONE) {
            afatfs.lastError = AFATFS_ERROR_GENERIC;
        }
        afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
    }

    return condition;
}

static bool afatfs_fileIsBusy(afatfsFilePtr_t file)
{
    return file->operation.operation != AFATFS_FILE_OPERATION_NONE;
}

static void afatfs_releaseChildParent(afatfsFilePtr_t parent)
{
    /*
     * Release the exclusive parent lifetime acquired by a child operation.
     *
     * What: validates the current single-owner contract and clears the count.
     * Why: callbacks may immediately close the parent or queue another child,
     * so every terminal path must release before publishing completion.
     * Input: NULL for ordinary suboperations, otherwise the exact retained
     * directory handle. Output: parent becomes caller-usable.
     * Affiliates: create, append-seek, truncate, mkdir extension, and future
     * move/copy coordinators.
     */
    if (!parent)
        return;
    afatfs_assert(parent->childOperationRetainCount == 1u);
    parent->childOperationRetainCount = 0u;
}

/**
 * The number of FAT table entries that fit within one AFATFS sector size.
 *
 * Note that this is the same as the number of clusters in an AFATFS supercluster.
 */
static uint32_t afatfs_fatEntriesPerSector()
{
    return afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT32 ? AFATFS_FAT32_FAT_ENTRIES_PER_SECTOR : AFATFS_FAT16_FAT_ENTRIES_PER_SECTOR;
}

/**
 * Size of a FAT cluster in bytes
 */
ONLY_EXPOSE_FOR_TESTING
uint32_t afatfs_clusterSize()
{
    return afatfs.sectorsPerCluster * AFATFS_SECTOR_SIZE;
}

/**
 * Given a byte offset within a file, return the byte offset of that position within the cluster it belongs to.
 */
static uint32_t afatfs_byteIndexInCluster(uint32_t byteOffset)
{
    return afatfs.byteInClusterMask & byteOffset;
}

/**
 * Given a byte offset within a file, return the index of the sector within the cluster it belongs to.
 */
static uint32_t afatfs_sectorIndexInCluster(uint32_t byteOffset)
{
    return afatfs_byteIndexInCluster(byteOffset) / AFATFS_SECTOR_SIZE;
}

// Get the buffer memory for the cache entry of the given index.
static uint8_t *afatfs_cacheSectorGetMemory(int cacheEntryIndex)
{
    return afatfs.cache + cacheEntryIndex * AFATFS_SECTOR_SIZE;
}

static int afatfs_getCacheDescriptorIndexForBuffer(uint8_t *memory)
{
    int index = (memory - afatfs.cache) / AFATFS_SECTOR_SIZE;

    if (afatfs_assert(index >= 0 && index < AFATFS_NUM_CACHE_SECTORS)) {
        return index;
    } else {
        return -1;
    }
}

static afatfsCacheBlockDescriptor_t* afatfs_getCacheDescriptorForBuffer(uint8_t *memory)
{
    return afatfs.cacheDescriptor + afatfs_getCacheDescriptorIndexForBuffer(memory);
}

static void afatfs_cacheSectorMarkDirty(afatfsCacheBlockDescriptor_t *descriptor)
{
    if (descriptor->state != AFATFS_CACHE_STATE_DIRTY) {
        descriptor->writeTimestamp = ++afatfs.cacheTimer;
        descriptor->state = AFATFS_CACHE_STATE_DIRTY;
        afatfs.cacheDirtyEntries++;
    }
}

static void afatfs_cacheSectorInit(afatfsCacheBlockDescriptor_t *descriptor, uint32_t sectorIndex, bool locked)
{
    descriptor->sectorIndex = sectorIndex;

    descriptor->accessTimestamp = descriptor->writeTimestamp = ++afatfs.cacheTimer;

    descriptor->consecutiveEraseBlockCount = 0;

    descriptor->state = AFATFS_CACHE_STATE_EMPTY;

    descriptor->locked = locked;
    descriptor->retainCount = 0;
    descriptor->discardable = 0;
}

/**
 * Called by the SD card driver when one of our read operations completes.
 */
static void afatfs_sdcardReadComplete(sdcardBlockOperation_e operation, uint32_t sectorIndex, uint8_t *buffer, uint32_t callbackData)
{
    (void) operation;
    (void) callbackData;

    for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
        if (afatfs.cacheDescriptor[i].state != AFATFS_CACHE_STATE_EMPTY
            && afatfs.cacheDescriptor[i].sectorIndex == sectorIndex
        ) {
            if (buffer == NULL) {
                // Read failed, mark the sector as empty and whoever asked for it will ask for it again later to retry
                afatfs.cacheDescriptor[i].state = AFATFS_CACHE_STATE_EMPTY;
            } else {
                afatfs_assert(afatfs_cacheSectorGetMemory(i) == buffer && afatfs.cacheDescriptor[i].state == AFATFS_CACHE_STATE_READING);

                afatfs.cacheDescriptor[i].state = AFATFS_CACHE_STATE_IN_SYNC;
            }

            break;
        }
    }
}

/**
 * Called by the SD card driver when one of our write operations completes.
 */
static void afatfs_sdcardWriteComplete(sdcardBlockOperation_e operation, uint32_t sectorIndex, uint8_t *buffer, uint32_t callbackData)
{
    (void) operation;
    (void) callbackData;

    afatfs.cacheFlushInProgress = false;

    for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
        /* Keep in mind that someone may have marked the sector as dirty after writing had already begun. In this case we must leave
         * it marked as dirty because those modifications may have been made too late to make it to the disk!
         */
        if (afatfs.cacheDescriptor[i].sectorIndex == sectorIndex
            && afatfs.cacheDescriptor[i].state == AFATFS_CACHE_STATE_WRITING
        ) {
            if (buffer == NULL) {
                // Write failed, remark the sector as dirty
                afatfs.cacheDescriptor[i].state = AFATFS_CACHE_STATE_DIRTY;
                afatfs.cacheDirtyEntries++;
            } else {
                afatfs_assert(afatfs_cacheSectorGetMemory(i) == buffer);

                afatfs.cacheDescriptor[i].state = AFATFS_CACHE_STATE_IN_SYNC;
            }
            break;
        }
    }
}

/**
 * Attempt to flush the dirty cache entry with the given index to the SDcard.
 */
static void afatfs_cacheFlushSector(int cacheIndex)
{
    afatfsCacheBlockDescriptor_t *cacheDescriptor = &afatfs.cacheDescriptor[cacheIndex];

#ifdef AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT
    if (cacheDescriptor->consecutiveEraseBlockCount) {
        sdcard_beginWriteBlocks(cacheDescriptor->sectorIndex, cacheDescriptor->consecutiveEraseBlockCount);
    }
#endif

    switch (sdcard_writeBlock(cacheDescriptor->sectorIndex, afatfs_cacheSectorGetMemory(cacheIndex), afatfs_sdcardWriteComplete, 0)) {
        case SDCARD_OPERATION_IN_PROGRESS:
            // The card will call us back later when the buffer transmission finishes
            afatfs.cacheDirtyEntries--;
            cacheDescriptor->state = AFATFS_CACHE_STATE_WRITING;
            afatfs.cacheFlushInProgress = true;
            break;

        case SDCARD_OPERATION_SUCCESS:
            // Buffer is already transmitted
            afatfs.cacheDirtyEntries--;
            cacheDescriptor->state = AFATFS_CACHE_STATE_IN_SYNC;
            break;

        case SDCARD_OPERATION_BUSY:
        case SDCARD_OPERATION_FAILURE:
        default:
            ;
    }
}

/**
 * Find a sector in the cache which corresponds to the given physical sector index, or NULL if the sector isn't
 * cached. Note that the cached sector could be in any state including completely empty.
 */
static afatfsCacheBlockDescriptor_t* afatfs_findCacheSector(uint32_t sectorIndex)
{
    for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
        if (afatfs.cacheDescriptor[i].sectorIndex == sectorIndex) {
            return &afatfs.cacheDescriptor[i];
        }
    }

    return NULL;
}

/**
 * Find or allocate a cache sector for the given sector index on disk. Returns a block which matches one of these
 * conditions (in descending order of preference):
 *
 * - The requested sector that already exists in the cache
 * - The index of an empty sector
 * - The index of a synced discardable sector
 * - The index of the oldest synced sector
 *
 * Otherwise it returns -1 to signal failure (cache is full!)
 */
static int afatfs_allocateCacheSector(uint32_t sectorIndex)
{
    int allocateIndex;
    int emptyIndex = -1, discardableIndex = -1;

    uint32_t oldestSyncedSectorLastUse = 0xFFFFFFFF;
    int oldestSyncedSectorIndex = -1;

    if (
        !afatfs_assert(
            afatfs.numClusters == 0 // We're unable to check sector bounds during startup since we haven't read volume label yet
            || sectorIndex < afatfs.clusterStartSector + afatfs.numClusters * afatfs.sectorsPerCluster
        )
    ) {
        return -1;
    }

    for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
        if (afatfs.cacheDescriptor[i].sectorIndex == sectorIndex) {
            /*
             * If the sector is actually empty then do a complete re-init of it just like the standard
             * empty case. (Sectors marked as empty should be treated as if they don't have a block index assigned)
             */
            if (afatfs.cacheDescriptor[i].state == AFATFS_CACHE_STATE_EMPTY) {
                emptyIndex = i;
                break;
            }

            // Bump the last access time
            afatfs.cacheDescriptor[i].accessTimestamp = ++afatfs.cacheTimer;
            return i;
        }

        switch (afatfs.cacheDescriptor[i].state) {
            case AFATFS_CACHE_STATE_EMPTY:
                emptyIndex = i;
            break;
            case AFATFS_CACHE_STATE_IN_SYNC:
                // Is this a synced sector that we could evict from the cache?
                if (!afatfs.cacheDescriptor[i].locked && afatfs.cacheDescriptor[i].retainCount == 0) {
                    if (afatfs.cacheDescriptor[i].discardable) {
                        discardableIndex = i;
                    } else if (afatfs.cacheDescriptor[i].accessTimestamp < oldestSyncedSectorLastUse) {
                        // This is older than last block we decided to evict, so evict this one in preference
                        oldestSyncedSectorLastUse = afatfs.cacheDescriptor[i].accessTimestamp;
                        oldestSyncedSectorIndex = i;
                    }
                }
            break;
            default:
                ;
        }
    }

    if (emptyIndex > -1) {
        allocateIndex = emptyIndex;
    } else if (discardableIndex > -1) {
        allocateIndex = discardableIndex;
    } else if (oldestSyncedSectorIndex > -1) {
        allocateIndex = oldestSyncedSectorIndex;
    } else {
        allocateIndex = -1;
    }

    if (allocateIndex > -1) {
        afatfs_cacheSectorInit(&afatfs.cacheDescriptor[allocateIndex], sectorIndex, false);
    }

    return allocateIndex;
}

/**
 * Attempt to flush dirty cache pages out to the sdcard.
 *
 * The return value is intentionally stricter than "no dirty sectors were found
 * this pass": it remains false while a previously-started sector write is still
 * waiting for sdcard_poll() to deliver afatfs_sdcardWriteComplete(). Filesystem
 * save completion uses this as the last persistence boundary before it tells
 * Menu/Preset that a save is done; without the in-flight check, the UI can
 * return to normal while the directory entry or FAT sector that names a new Kit
 * folder is still only in transit.
 */
bool afatfs_flush()
{
    if (afatfs.cacheDirtyEntries > 0) {
        // Flush the oldest flushable sector
        uint32_t earliestSectorTime = 0xFFFFFFFF;
        int earliestSectorIndex = -1;

        for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
            if (afatfs.cacheDescriptor[i].state == AFATFS_CACHE_STATE_DIRTY && !afatfs.cacheDescriptor[i].locked
                && (earliestSectorIndex == -1 || afatfs.cacheDescriptor[i].writeTimestamp < earliestSectorTime)
            ) {
                earliestSectorIndex = i;
                earliestSectorTime = afatfs.cacheDescriptor[i].writeTimestamp;
            }
        }

        if (earliestSectorIndex > -1) {
            afatfs_cacheFlushSector(earliestSectorIndex);

            // That flush will take time to complete so we may as well tell caller to come back later
            return false;
        }
    }

    /*
     * cacheDirtyEntries is decremented when the write is queued, not when the
     * card reports completion. Keep reporting "not flushed" across that gap so
     * callers that care about removable-media visibility can wait until the
     * write callback has either synced the sector or re-marked it dirty.
     */
    return (bool)!afatfs.cacheFlushInProgress;
}

bool afatfs_sync()
{
    /*
     * Public persistence boundary for callers that have finished a write flow.
     *
     * Today this is a named wrapper around afatfs_flush(), whose implementation
     * already waits for dirty sectors and in-flight SD writes. Keeping a
     * separate API lets filesystem.c express save completion in storage terms
     * and leaves room for future metadata/FAT barriers without changing every
     * caller again.
     */
    return afatfs_flush();
}

/**
 * Returns true if either the freefile or the regular cluster pool has been exhausted during a previous write operation.
 */
bool afatfs_isFull()
{
    return afatfs.filesystemFull;
}

/**
 * Get the physical sector number that corresponds to the FAT sector of the given fatSectorIndex within the given
 * FAT (fatIndex may be 0 or 1). (0, 0) gives the first sector of the first FAT.
 */
static uint32_t afatfs_fatSectorToPhysical(int fatIndex, uint32_t fatSectorIndex)
{
    return afatfs.fatStartSector + (fatIndex ? afatfs.fatSectors : 0) + fatSectorIndex;
}

static uint32_t afatfs_fileClusterToPhysical(uint32_t clusterNumber, uint32_t sectorIndex)
{
    return afatfs.clusterStartSector + (clusterNumber - 2) * afatfs.sectorsPerCluster + sectorIndex;
}

static uint32_t afatfs_fileGetCursorPhysicalSector(afatfsFilePtr_t file)
{
    if (file->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) {
        return afatfs.fatStartSector + AFATFS_NUM_FATS * afatfs.fatSectors + file->cursorOffset / AFATFS_SECTOR_SIZE;
    } else {
        uint32_t cursorSectorInCluster = afatfs_sectorIndexInCluster(file->cursorOffset);
        return afatfs_fileClusterToPhysical(file->cursorCluster, cursorSectorInCluster);
    }
}

/**
 * Sector here is the sector index within the cluster.
 */
static void afatfs_fileGetCursorClusterAndSector(afatfsFilePtr_t file, uint32_t *cluster, uint16_t *sector)
{
    *cluster = file->cursorCluster;

    if (file->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) {
        *sector = file->cursorOffset / AFATFS_SECTOR_SIZE;
    } else {
        *sector = afatfs_sectorIndexInCluster(file->cursorOffset);
    }
}

/**
 * Get a cache entry for the given sector and store a pointer to the cached memory in *buffer.
 *
 * physicalSectorIndex - The index of the sector in the SD card to cache
 * sectorflags         - A union of AFATFS_CACHE_* constants that says which operations the sector will be cached for.
 * buffer              - A pointer to the 512-byte memory buffer for the sector will be stored here upon success
 * eraseCount          - For write operations, set to a non-zero number to hint that we plan to write that many sectors
 *                       consecutively (including this sector)
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - On success
 *     AFATFS_OPERATION_IN_PROGRESS - Card is busy, call again later
 *     AFATFS_OPERATION_FAILURE     - When the filesystem encounters a fatal error
 */
static afatfsOperationStatus_e afatfs_cacheSector(uint32_t physicalSectorIndex, uint8_t **buffer, uint8_t sectorFlags, uint32_t eraseCount)
{
    // We never write to the MBR, so any attempt to write there is an asyncfatfs bug
    if (!afatfs_assert((sectorFlags & AFATFS_CACHE_WRITE) == 0 || physicalSectorIndex != 0)) {
        return AFATFS_OPERATION_FAILURE;
    }

    int cacheSectorIndex = afatfs_allocateCacheSector(physicalSectorIndex);

    if (cacheSectorIndex == -1) {
        // We don't have enough free cache to service this request right now, try again later
        return AFATFS_OPERATION_IN_PROGRESS;
    }

    switch (afatfs.cacheDescriptor[cacheSectorIndex].state) {
        case AFATFS_CACHE_STATE_READING:
            return AFATFS_OPERATION_IN_PROGRESS;
        break;

        case AFATFS_CACHE_STATE_EMPTY:
            if ((sectorFlags & AFATFS_CACHE_READ) != 0) {
                if (sdcard_readBlock(physicalSectorIndex, afatfs_cacheSectorGetMemory(cacheSectorIndex), afatfs_sdcardReadComplete, 0)) {
                    afatfs.cacheDescriptor[cacheSectorIndex].state = AFATFS_CACHE_STATE_READING;
                }
                return AFATFS_OPERATION_IN_PROGRESS;
            }

            // We only get to decide these fields if we're the first ones to cache the sector:
            afatfs.cacheDescriptor[cacheSectorIndex].discardable = (sectorFlags & AFATFS_CACHE_DISCARDABLE) != 0 ? 1 : 0;

#ifdef AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT
            // Don't bother pre-erasing for small block sequences
            if (eraseCount < AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT) {
                eraseCount = 0;
            } else {
                eraseCount = MIN(eraseCount, UINT16_MAX); // If caller asked for a longer chain of sectors we silently truncate that here
            }

            afatfs.cacheDescriptor[cacheSectorIndex].consecutiveEraseBlockCount = eraseCount;
#endif

            // Fall through

        case AFATFS_CACHE_STATE_WRITING:
        case AFATFS_CACHE_STATE_IN_SYNC:
            if ((sectorFlags & AFATFS_CACHE_WRITE) != 0) {
                afatfs_cacheSectorMarkDirty(&afatfs.cacheDescriptor[cacheSectorIndex]);
            }
            // Fall through

        case AFATFS_CACHE_STATE_DIRTY:
            if ((sectorFlags & AFATFS_CACHE_LOCK) != 0) {
                afatfs.cacheDescriptor[cacheSectorIndex].locked = 1;
            }
            if ((sectorFlags & AFATFS_CACHE_RETAIN) != 0) {
                afatfs.cacheDescriptor[cacheSectorIndex].retainCount++;
            }

            *buffer = afatfs_cacheSectorGetMemory(cacheSectorIndex);

            return AFATFS_OPERATION_SUCCESS;
        break;

        default:
            // Cache block in unknown state, should never happen
            afatfs_assert(false);
            return AFATFS_OPERATION_FAILURE;
    }
}

/**
 * Parse the details out of the given MBR sector (512 bytes long). Return true if a compatible filesystem was found.
 */
static bool afatfs_parseMBR(const uint8_t *sector)
{
    // Check MBR signature
    if (sector[AFATFS_SECTOR_SIZE - 2] != 0x55 || sector[AFATFS_SECTOR_SIZE - 1] != 0xAA)
        return false;

    mbrPartitionEntry_t *partition = (mbrPartitionEntry_t *) (sector + 446);

    for (int i = 0; i < 4; i++) {
        if (
            partition[i].lbaBegin > 0
            && (
                partition[i].type == MBR_PARTITION_TYPE_FAT32
                || partition[i].type == MBR_PARTITION_TYPE_FAT32_LBA
                || partition[i].type == MBR_PARTITION_TYPE_FAT16
                || partition[i].type == MBR_PARTITION_TYPE_FAT16_LBA
            )
        ) {
            afatfs.partitionStartSector = partition[i].lbaBegin;

            return true;
        }
    }

    return false;
}

static bool afatfs_parseVolumeID(const uint8_t *sector)
{
    fatVolumeID_t *volume = (fatVolumeID_t *) sector;

    afatfs.filesystemType = FAT_FILESYSTEM_TYPE_INVALID;

    if (volume->bytesPerSector != AFATFS_SECTOR_SIZE || volume->numFATs != AFATFS_NUM_FATS
            || sector[510] != FAT_VOLUME_ID_SIGNATURE_1 || sector[511] != FAT_VOLUME_ID_SIGNATURE_2) {
        return false;
    }

    afatfs.fatStartSector = afatfs.partitionStartSector + volume->reservedSectorCount;

    afatfs.sectorsPerCluster = volume->sectorsPerCluster;
    if (afatfs.sectorsPerCluster < 1 || afatfs.sectorsPerCluster > 128 || !isPowerOfTwo(afatfs.sectorsPerCluster)) {
        return false;
    }

    afatfs.byteInClusterMask = AFATFS_SECTOR_SIZE * afatfs.sectorsPerCluster - 1;

    afatfs.fatSectors = volume->FATSize16 != 0 ? volume->FATSize16 : volume->fatDescriptor.fat32.FATSize32;

    // Always zero on FAT32 since rootEntryCount is always zero (this is non-zero on FAT16)
    afatfs.rootDirectorySectors = ((volume->rootEntryCount * FAT_DIRECTORY_ENTRY_SIZE) + (volume->bytesPerSector - 1)) / volume->bytesPerSector;
    uint32_t totalSectors = volume->totalSectors16 != 0 ? volume->totalSectors16 : volume->totalSectors32;
    uint32_t dataSectors = totalSectors - (volume->reservedSectorCount + (AFATFS_NUM_FATS * afatfs.fatSectors) + afatfs.rootDirectorySectors);

    afatfs.numClusters = dataSectors / volume->sectorsPerCluster;

    if (afatfs.numClusters <= FAT12_MAX_CLUSTERS) {
        afatfs.filesystemType = FAT_FILESYSTEM_TYPE_FAT12;

        return false; // FAT12 is not a supported filesystem
    } else if (afatfs.numClusters <= FAT16_MAX_CLUSTERS) {
        afatfs.filesystemType = FAT_FILESYSTEM_TYPE_FAT16;
    } else {
        afatfs.filesystemType = FAT_FILESYSTEM_TYPE_FAT32;
    }

    if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT32) {
        afatfs.rootDirectoryCluster = volume->fatDescriptor.fat32.rootCluster;
    } else {
        // FAT16 doesn't store the root directory in clusters
        afatfs.rootDirectoryCluster = 0;
    }

    uint32_t endOfFATs = afatfs.fatStartSector + AFATFS_NUM_FATS * afatfs.fatSectors;

    afatfs.clusterStartSector = endOfFATs + afatfs.rootDirectorySectors;

    return true;
}

/**
 * Get the position of the FAT entry for the cluster with the given number.
 */
static void afatfs_getFATPositionForCluster(uint32_t cluster, uint32_t *fatSectorIndex, uint32_t *fatSectorEntryIndex)
{
    if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16) {
        uint32_t entriesPerFATSector = AFATFS_SECTOR_SIZE / sizeof(uint16_t);

        *fatSectorIndex = cluster / entriesPerFATSector;
        *fatSectorEntryIndex = cluster & (entriesPerFATSector - 1);
    } else {
        uint32_t entriesPerFATSector = AFATFS_SECTOR_SIZE / sizeof(uint32_t);

        *fatSectorIndex = fat32_decodeClusterNumber(cluster) / entriesPerFATSector;
        *fatSectorEntryIndex = cluster & (entriesPerFATSector - 1);
    }
}

static bool afatfs_FATIsEndOfChainMarker(uint32_t clusterNumber)
{
    if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT32) {
        return fat32_isEndOfChainMarker(clusterNumber);
    } else {
        return fat16_isEndOfChainMarker(clusterNumber);
    }
}

/**
 * Look up the FAT to find out which cluster follows the one with the given number and store it into *nextCluster.
 *
 * Use fat_isFreeSpace() and fat_isEndOfChainMarker() on nextCluster to distinguish those special values from regular
 * cluster numbers.
 *
 * Note that if you're trying to find the next cluster of a file, you should be calling afatfs_fileGetNextCluster()
 * instead, as that one supports contiguous freefile-based files (which needn't consult the FAT).
 *
 * Returns:
 *     AFATFS_OPERATION_IN_PROGRESS - FS is busy right now, call again later
 *     AFATFS_OPERATION_SUCCESS     - *nextCluster is set to the next cluster number
 */
static afatfsOperationStatus_e afatfs_FATGetNextCluster(int fatIndex, uint32_t cluster, uint32_t *nextCluster)
{
    uint32_t fatSectorIndex, fatSectorEntryIndex;
    afatfsFATSector_t sector;

    afatfs_getFATPositionForCluster(cluster, &fatSectorIndex, &fatSectorEntryIndex);

    afatfsOperationStatus_e result = afatfs_cacheSector(afatfs_fatSectorToPhysical(fatIndex, fatSectorIndex), &sector.bytes, AFATFS_CACHE_READ, 0);

    if (result == AFATFS_OPERATION_SUCCESS) {
        if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16) {
            *nextCluster = sector.fat16[fatSectorEntryIndex];
        } else {
            *nextCluster = fat32_decodeClusterNumber(sector.fat32[fatSectorEntryIndex]);
        }
    }

    return result;
}

/**
 * Set the cluster number that follows the given cluster. Pass 0xFFFFFFFF for nextCluster to terminate the FAT chain.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - On success
 *     AFATFS_OPERATION_IN_PROGRESS - Card is busy, call again later
 *     AFATFS_OPERATION_FAILURE     - When the filesystem encounters a fatal error
 */
static afatfsOperationStatus_e afatfs_FATSetNextCluster(uint32_t startCluster, uint32_t nextCluster)
{
    afatfsFATSector_t sector;
    uint32_t fatSectorIndex, fatSectorEntryIndex, fatPhysicalSector;
    afatfsOperationStatus_e result;

    if(!afatfs_assert(startCluster >= FAT_SMALLEST_LEGAL_CLUSTER_NUMBER)){
        return AFATFS_OPERATION_FAILURE; // startCluster is not valid;
    }

    afatfs_getFATPositionForCluster(startCluster, &fatSectorIndex, &fatSectorEntryIndex);

    fatPhysicalSector = afatfs_fatSectorToPhysical(0, fatSectorIndex);

    result = afatfs_cacheSector(fatPhysicalSector, &sector.bytes, AFATFS_CACHE_READ | AFATFS_CACHE_WRITE, 0);

    if (result == AFATFS_OPERATION_SUCCESS) {
        if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16) {
            sector.fat16[fatSectorEntryIndex] = nextCluster;
        } else {
            sector.fat32[fatSectorEntryIndex] = nextCluster;
        }
    }

    return result;
}

/**
 * Bring the logical filesize up to date with the current cursor position.
 */
static void afatfs_fileUpdateFilesize(afatfsFile_t *file)
{
    file->logicalSize = MAX(file->logicalSize, file->cursorOffset);
}

static void afatfs_fileUnlockCacheSector(afatfsFilePtr_t file)
{
    if (file->writeLockedCacheIndex != -1) {
        afatfs.cacheDescriptor[file->writeLockedCacheIndex].locked = 0;
        file->writeLockedCacheIndex = -1;
    }
    if (file->readRetainCacheIndex != -1) {
        afatfs.cacheDescriptor[file->readRetainCacheIndex].retainCount = MAX((int) afatfs.cacheDescriptor[file->readRetainCacheIndex].retainCount - 1, 0);
        file->readRetainCacheIndex = -1;
    }
}

/**
 * Starting from and including the given cluster number, find the number of the first cluster which matches the given
 * condition.
 *
 * searchLimit - Last cluster to examine (exclusive). To search the entire volume, pass:
 *                   afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER
 *
 * Condition:
 *     CLUSTER_SEARCH_FREE_AT_BEGINNING_OF_FAT_SECTOR - Find a cluster marked as free in the FAT which lies at the
 *         beginning of its FAT sector. The passed initial search 'cluster' must correspond to the first entry of a FAT sector.
 *     CLUSTER_SEARCH_FREE            - Find a cluster marked as free in the FAT
 *     CLUSTER_SEARCH_OCCUPIED        - Find a cluster marked as occupied in the FAT.
 *
 * Returns:
 *     AFATFS_FIND_CLUSTER_FOUND       - A cluster matching the criteria was found and stored in *cluster
 *     AFATFS_FIND_CLUSTER_IN_PROGRESS - The search is not over, call this routine again later with the updated *cluster value to resume
 *     AFATFS_FIND_CLUSTER_FATAL       - An unexpected read error occurred, the volume should be abandoned
 *     AFATFS_FIND_CLUSTER_NOT_FOUND   - The entire device was searched without finding a suitable cluster (the
 *                                       *cluster points to just beyond the final cluster).
 */
static afatfsFindClusterStatus_e afatfs_findClusterWithCondition(afatfsClusterSearchCondition_e condition, uint32_t *cluster, uint32_t searchLimit)
{
    afatfsFATSector_t sector;
    uint32_t fatSectorIndex, fatSectorEntryIndex;

    uint32_t fatEntriesPerSector = afatfs_fatEntriesPerSector();
    bool lookingForFree = condition == CLUSTER_SEARCH_FREE_AT_BEGINNING_OF_FAT_SECTOR || condition == CLUSTER_SEARCH_FREE;

    int jump;

    // Get the FAT entry which corresponds to this cluster so we can begin our search there
    afatfs_getFATPositionForCluster(*cluster, &fatSectorIndex, &fatSectorEntryIndex);

    switch (condition) {
        case CLUSTER_SEARCH_FREE_AT_BEGINNING_OF_FAT_SECTOR:
            jump = fatEntriesPerSector;

            // We're supposed to call this routine with the cluster properly aligned
            if (!afatfs_assert(fatSectorEntryIndex == 0)) {
                return AFATFS_FIND_CLUSTER_FATAL;
            }
        break;
        case CLUSTER_SEARCH_OCCUPIED:
        case CLUSTER_SEARCH_FREE:
            jump = 1;
        break;
        default:
            afatfs_assert(false);
            return AFATFS_FIND_CLUSTER_FATAL;
    }

    while (*cluster < searchLimit) {

#ifdef AFATFS_USE_FREEFILE
        // If we're looking inside the freefile, we won't find any free clusters! Skip it!
        if (afatfs.freeFile.logicalSize > 0 && *cluster == afatfs.freeFile.firstCluster) {
            *cluster += (afatfs.freeFile.logicalSize + afatfs_clusterSize() - 1) / afatfs_clusterSize();

            // Maintain alignment
            *cluster = roundUpTo(*cluster, jump);
            continue; // Go back to check that the new cluster number is within the volume
        }
#endif

        afatfsOperationStatus_e status = afatfs_cacheSector(afatfs_fatSectorToPhysical(0, fatSectorIndex), &sector.bytes, AFATFS_CACHE_READ | AFATFS_CACHE_DISCARDABLE, 0);

        switch (status) {
            case AFATFS_OPERATION_SUCCESS:
                do {
                    uint32_t clusterNumber;

                    switch (afatfs.filesystemType) {
                        case FAT_FILESYSTEM_TYPE_FAT16:
                            clusterNumber = sector.fat16[fatSectorEntryIndex];
                        break;
                        case FAT_FILESYSTEM_TYPE_FAT32:
                            clusterNumber = fat32_decodeClusterNumber(sector.fat32[fatSectorEntryIndex]);
                        break;
                        default:
                            return AFATFS_FIND_CLUSTER_FATAL;
                    }

                    if (fat_isFreeSpace(clusterNumber) == lookingForFree) {
                        /*
                         * The final FAT sector may have fewer than fatEntriesPerSector entries in it, so we need to
                         * check the cluster number is valid here before we report a bogus success!
                         */
                        if (*cluster < searchLimit) {
                            return AFATFS_FIND_CLUSTER_FOUND;
                        } else {
                            *cluster = searchLimit;
                            return AFATFS_FIND_CLUSTER_NOT_FOUND;
                        }
                    }

                    (*cluster) += jump;
                    fatSectorEntryIndex += jump;
                } while (fatSectorEntryIndex < fatEntriesPerSector);

                // Move on to the next FAT sector
                fatSectorIndex++;
                fatSectorEntryIndex = 0;
            break;
            case AFATFS_OPERATION_FAILURE:
                return AFATFS_FIND_CLUSTER_FATAL;
            break;
            case AFATFS_OPERATION_IN_PROGRESS:
                return AFATFS_FIND_CLUSTER_IN_PROGRESS;
            break;
        }
    }

    // We looked at every available cluster and didn't find one matching the condition
    *cluster = searchLimit;
    return AFATFS_FIND_CLUSTER_NOT_FOUND;
}

/**
 * Get the cluster that follows the currentCluster in the FAT chain for the given file.
 *
 * Returns:
 *     AFATFS_OPERATION_IN_PROGRESS - FS is busy right now, call again later
 *     AFATFS_OPERATION_SUCCESS     - *nextCluster is set to the next cluster number
 */
static afatfsOperationStatus_e afatfs_fileGetNextCluster(afatfsFilePtr_t file, uint32_t currentCluster, uint32_t *nextCluster)
{
#ifndef AFATFS_USE_FREEFILE
    (void) file;
#else
    if ((file->mode & AFATFS_FILE_MODE_CONTIGUOUS) != 0) {
        uint32_t freeFileStart = afatfs.freeFile.firstCluster;

        afatfs_assert(currentCluster + 1 <= freeFileStart);

        // Would the next cluster lie outside the allocated file? (i.e. beyond the end of the file into the start of the freefile)
        if (currentCluster + 1 == freeFileStart) {
            *nextCluster = 0;
        } else {
            *nextCluster = currentCluster + 1;
        }

        return AFATFS_OPERATION_SUCCESS;
    } else
#endif
    {
        return afatfs_FATGetNextCluster(0, currentCluster, nextCluster);
    }
}

#ifdef AFATFS_USE_FREEFILE

/**
 * Update the FAT to fill the contiguous series of clusters with indexes [*startCluster...endCluster) with the
 * specified pattern.
 *
 * AFATFS_FAT_PATTERN_TERMINATED_CHAIN - Chain the clusters together in linear sequence and terminate the final cluster
 * AFATFS_FAT_PATTERN_CHAIN            - Chain the clusters together without terminating the final entry
 * AFATFS_FAT_PATTERN_FREE             - Mark the clusters as free space
 *
 * Returns -
 *     AFATFS_OPERATION_SUCCESS        - When the entire chain has been written
 *     AFATFS_OPERATION_IN_PROGRESS    - Call again later with the updated *startCluster value in order to resume writing.
 */
static afatfsOperationStatus_e afatfs_FATFillWithPattern(afatfsFATPattern_e pattern, uint32_t *startCluster, uint32_t endCluster)
{
    afatfsFATSector_t sector;
    uint32_t fatSectorIndex, firstEntryIndex, fatPhysicalSector;
    uint8_t fatEntrySize;
    uint32_t nextCluster;
    afatfsOperationStatus_e result;
    uint32_t eraseSectorCount;

    // Find the position of the initial cluster to begin our fill
    afatfs_getFATPositionForCluster(*startCluster, &fatSectorIndex, &firstEntryIndex);

    fatPhysicalSector = afatfs_fatSectorToPhysical(0, fatSectorIndex);

    // How many consecutive FAT sectors will we be overwriting?
    eraseSectorCount = (endCluster - *startCluster + firstEntryIndex + afatfs_fatEntriesPerSector() - 1) / afatfs_fatEntriesPerSector();

    while (*startCluster < endCluster) {
        // The last entry we will fill inside this sector (exclusive):
        uint32_t lastEntryIndex = MIN(firstEntryIndex + (endCluster - *startCluster), afatfs_fatEntriesPerSector());

        uint8_t cacheFlags = AFATFS_CACHE_WRITE | AFATFS_CACHE_DISCARDABLE;

        if (firstEntryIndex > 0 || lastEntryIndex < afatfs_fatEntriesPerSector()) {
            // We're not overwriting the entire FAT sector so we must read the existing contents
            cacheFlags |= AFATFS_CACHE_READ;
        }

        result = afatfs_cacheSector(fatPhysicalSector, &sector.bytes, cacheFlags, eraseSectorCount);

        if (result != AFATFS_OPERATION_SUCCESS) {
            return result;
        }

#ifdef AFATFS_DEBUG_VERBOSE
        if (pattern == AFATFS_FAT_PATTERN_FREE) {
            fprintf(stderr, "Marking cluster %u to %u as free in FAT sector %u...\n", *startCluster, endCluster, fatPhysicalSector);
        } else {
            fprintf(stderr, "Writing FAT chain from cluster %u to %u in FAT sector %u...\n", *startCluster, endCluster, fatPhysicalSector);
        }
#endif

        switch (pattern) {
            case AFATFS_FAT_PATTERN_TERMINATED_CHAIN:
            case AFATFS_FAT_PATTERN_UNTERMINATED_CHAIN:
                nextCluster = *startCluster + 1;
                // Write all the "next cluster" pointers
                if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16) {
                    for (uint32_t i = firstEntryIndex; i < lastEntryIndex; i++, nextCluster++) {
                        sector.fat16[i] = nextCluster;
                    }
                } else {
                    for (uint32_t i = firstEntryIndex; i < lastEntryIndex; i++, nextCluster++) {
                        sector.fat32[i] = nextCluster;
                    }
                }

                *startCluster += lastEntryIndex - firstEntryIndex;

                if (pattern == AFATFS_FAT_PATTERN_TERMINATED_CHAIN && *startCluster == endCluster) {
                    // We completed the chain! Overwrite the last entry we wrote with the terminator for the end of the chain
                    if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16) {
                        sector.fat16[lastEntryIndex - 1] = 0xFFFF;
                    } else {
                        sector.fat32[lastEntryIndex - 1] = 0xFFFFFFFF;
                    }
                    break;
                }
            break;
            case AFATFS_FAT_PATTERN_FREE:
                fatEntrySize = afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16 ? sizeof(uint16_t) : sizeof(uint32_t);

                memset(sector.bytes + firstEntryIndex * fatEntrySize, 0, (lastEntryIndex - firstEntryIndex) * fatEntrySize);

                *startCluster += lastEntryIndex - firstEntryIndex;
            break;
        }

        fatPhysicalSector++;
        eraseSectorCount--;
        firstEntryIndex = 0;
    }

    return AFATFS_OPERATION_SUCCESS;
}

#endif

/**
 * Write the directory entry for the file into its `directoryEntryPos` position in its containing directory.
 *
 * mode:
 *     AFATFS_SAVE_DIRECTORY_NORMAL    - Store the file's physical size, not the logical size, in the directory entry
 *     AFATFS_SAVE_DIRECTORY_FOR_CLOSE - We're done extending the file so we can write the logical size now.
 *     AFATFS_SAVE_DIRECTORY_DELETED   - Mark the directory entry as deleted
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS - The directory entry has been stored into the directory sector in cache.
 *     AFATFS_OPERATION_IN_PROGRESS - Cache is too busy, retry later
 *     AFATFS_OPERATION_FAILURE - If the filesystem enters the fatal state
 */
static afatfsOperationStatus_e afatfs_saveDirectoryEntry(afatfsFilePtr_t file, afatfsSaveDirectoryEntryMode_e mode)
{
    uint8_t *sector;
    afatfsOperationStatus_e result;

    if (file->directoryEntryPos.sectorNumberPhysical == 0) {
        return AFATFS_OPERATION_SUCCESS; // Root directories don't have a directory entry
    }

    result = afatfs_cacheSector(file->directoryEntryPos.sectorNumberPhysical, &sector, AFATFS_CACHE_READ | AFATFS_CACHE_WRITE, 0);

#ifdef AFATFS_DEBUG_VERBOSE
    fprintf(stderr, "Saving directory entry to sector %u...\n", file->directoryEntryPos.sectorNumberPhysical);
#endif

    if (result == AFATFS_OPERATION_SUCCESS) {
        if (afatfs_assert(file->directoryEntryPos.entryIndex >= 0)) {
            fatDirectoryEntry_t *entry = (fatDirectoryEntry_t *) sector + file->directoryEntryPos.entryIndex;

            switch (mode) {
               case AFATFS_SAVE_DIRECTORY_NORMAL:
                   /* We exaggerate the length of the written file so that if power is lost, the end of the file will
                    * still be readable (though the very tail of the file will be uninitialized data).
                    *
                    * This way we can avoid updating the directory entry too many times during fwrites() on the file.
                    */
                   entry->fileSize = file->physicalSize;
               break;
               case AFATFS_SAVE_DIRECTORY_DELETED:
                   entry->filename[0] = FAT_DELETED_FILE_MARKER;
                   //Fall through

               case AFATFS_SAVE_DIRECTORY_FOR_CLOSE:
                   // We write the true length of the file on close.
                   entry->fileSize = file->logicalSize;
            }

            // (sub)directories don't store a filesize in their directory entry:
            if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
                entry->fileSize = 0;
            }

            /*
             * FAT32 stores the first cluster split across high/low 16-bit
             * fields. This assignment is what turns a visible directory entry
             * into an enterable directory after appendRegularFreeClusterContinue()
             * chooses a cluster for a newly-created subdirectory.
             */
            entry->firstClusterHigh = file->firstCluster >> 16;
            entry->firstClusterLow = file->firstCluster & 0xFFFF;
        } else {
            return AFATFS_OPERATION_FAILURE;
        }
    }

    return result;
}

/**
 * Attempt to add a free cluster to the end of the given file. If the file was previously empty, the directory entry
 * is updated to point to the new cluster.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - The cluster has been appended
 *     AFATFS_OPERATION_IN_PROGRESS - Cache was busy, so call again later to continue
 *     AFATFS_OPERATION_FAILURE     - Cluster could not be appended because the filesystem ran out of space
 *                                    (afatfs.filesystemFull is set to true)
 *
 * If the file's operation was AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER, the file operation is cleared upon completion,
 * otherwise it is left alone so that this operation can be called as a sub-operation of some other operation on the
 * file.
 */
static afatfsOperationStatus_e afatfs_appendRegularFreeClusterContinue(afatfsFile_t *file)
{
    afatfsAppendFreeCluster_t *opState = &file->operation.state.appendFreeCluster;
    afatfsOperationStatus_e status;

    doMore:

    switch (opState->phase) {
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_FIND_FREESPACE:
            switch (afatfs_findClusterWithCondition(CLUSTER_SEARCH_FREE, &opState->searchCluster, afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER)) {
                case AFATFS_FIND_CLUSTER_FOUND:
                    afatfs.lastClusterAllocated = opState->searchCluster;

                    /*
                     * Assign the new cluster to the active cursor before the
                     * FAT and directory entry are fully committed. Directory
                     * initialization relies on this exactly like fwrite(): the
                     * following zero-fill phase needs a physical sector to
                     * write. When previousCluster is zero this is also the
                     * first cluster of the file/directory, so
                     * saveDirectoryEntry() must publish it in firstClusterHigh
                     * and firstClusterLow before mkdir reports success.
                     */
                    file->cursorCluster = opState->searchCluster;
                    file->physicalSize += afatfs_clusterSize();

                    if (opState->previousCluster == 0) {
                        // This is the new first cluster in the file
                        file->firstCluster = opState->searchCluster;
                    }

                    opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FAT1;
                    goto doMore;
                break;
                case AFATFS_FIND_CLUSTER_FATAL:
                case AFATFS_FIND_CLUSTER_NOT_FOUND:
                    // We couldn't find an empty cluster to append to the file
                    opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_FAILURE;
                    goto doMore;
                break;
                case AFATFS_FIND_CLUSTER_IN_PROGRESS:
                break;
            }
        break;
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FAT1:
            // Terminate the new cluster
            status = afatfs_FATSetNextCluster(opState->searchCluster, 0xFFFFFFFF);

            if (status == AFATFS_OPERATION_SUCCESS) {
                if (opState->previousCluster) {
                    opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FAT2;
                } else {
                    opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FILE_DIRECTORY;
                }

                goto doMore;
            }
        break;
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FAT2:
            // Add the new cluster to the pre-existing chain
            status = afatfs_FATSetNextCluster(opState->previousCluster, opState->searchCluster);

            if (status == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FILE_DIRECTORY;
                goto doMore;
            }
        break;
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_UPDATE_FILE_DIRECTORY:
            if (afatfs_saveDirectoryEntry(file, AFATFS_SAVE_DIRECTORY_NORMAL) == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_COMPLETE;
                goto doMore;
            }
        break;
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_COMPLETE:
            if (file->operation.operation == AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER) {
                file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            }

            return AFATFS_OPERATION_SUCCESS;
        break;
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_FAILURE:
            if (file->operation.operation == AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER) {
                file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            }

            afatfs.filesystemFull = true;
            return AFATFS_OPERATION_FAILURE;
        break;
    }

    return AFATFS_OPERATION_IN_PROGRESS;
}

static void afatfs_appendRegularFreeClusterInitOperationState(afatfsAppendFreeCluster_t *state, uint32_t previousCluster)
{
    state->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_INITIAL;
    state->previousCluster = previousCluster;
    state->searchCluster = afatfs.lastClusterAllocated;
}

/**
 * Queue up an operation to append a free cluster to the file and update the file's cursorCluster to point to it.
 *
 * You must seek to the end of the file first, so file.cursorCluster will be 0 for the first call, and
 * `file.cursorPreviousCluster` will be the cluster to append after.
 *
 * Note that the cursorCluster will be updated before this operation is completely finished (i.e. before the FAT is
 * updated) but you can go ahead and write to it before the operation succeeds.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - The append completed successfully
 *     AFATFS_OPERATION_IN_PROGRESS - The operation was queued on the file and will complete later
 *     AFATFS_OPERATION_FAILURE     - Operation could not be queued or append failed, check afatfs.fileSystemFull
 */
static afatfsOperationStatus_e afatfs_appendRegularFreeCluster(afatfsFilePtr_t file)
{
    if (file->operation.operation == AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER)
        return AFATFS_OPERATION_IN_PROGRESS;

    if (afatfs.filesystemFull || afatfs_fileIsBusy(file)) {
        return AFATFS_OPERATION_FAILURE;
    }

    file->operation.operation = AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER;

    afatfs_appendRegularFreeClusterInitOperationState(&file->operation.state.appendFreeCluster, file->cursorPreviousCluster);

    return afatfs_appendRegularFreeClusterContinue(file);
}

/**
 * Size of a AFATFS supercluster in bytes
 */
ONLY_EXPOSE_FOR_TESTING
uint32_t afatfs_superClusterSize()
{
    return afatfs_fatEntriesPerSector() * afatfs_clusterSize();
}

#ifdef AFATFS_USE_FREEFILE
/**
 * Continue to attempt to add a supercluster to the end of the given file.
 *
 * If the file operation was set to AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER and the operation completes, the file's
 * operation is cleared.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - On completion
 *     AFATFS_OPERATION_IN_PROGRESS - Operation still in progress
 */
static afatfsOperationStatus_e afatfs_appendSuperclusterContinue(afatfsFile_t *file)
{
    afatfsAppendSupercluster_t *opState = &file->operation.state.appendSupercluster;

    afatfsOperationStatus_e status;

    doMore:
    switch (opState->phase) {
        case AFATFS_APPEND_SUPERCLUSTER_PHASE_INIT:
            // Our file steals the first cluster of the freefile

            // We can go ahead and write to that space before the FAT and directory are updated
            file->cursorCluster = afatfs.freeFile.firstCluster;
            file->physicalSize += afatfs_superClusterSize();

            /* Remove the first supercluster from the freefile
             *
             * Even if the freefile becomes empty, we still don't set its first cluster to zero. This is so that
             * afatfs_fileGetNextCluster() can tell where a contiguous file ends (at the start of the freefile).
             *
             * Note that normally the freefile can't become empty because it is allocated as a non-integer number
             * of superclusters to avoid precisely this situation.
             */
            afatfs.freeFile.firstCluster += afatfs_fatEntriesPerSector();
            afatfs.freeFile.logicalSize -= afatfs_superClusterSize();
            afatfs.freeFile.physicalSize -= afatfs_superClusterSize();

            // The new supercluster needs to have its clusters chained contiguously and marked with a terminator at the end
            opState->fatRewriteStartCluster = file->cursorCluster;
            opState->fatRewriteEndCluster = opState->fatRewriteStartCluster + afatfs_fatEntriesPerSector();

            if (opState->previousCluster == 0) {
                // This is the new first cluster in the file so we need to update the directory entry
                file->firstCluster = file->cursorCluster;
            } else {
                /*
                 * We also need to update the FAT of the supercluster that used to end the file so that it no longer
                 * terminates there
                 */
                opState->fatRewriteStartCluster -= afatfs_fatEntriesPerSector();
            }

            opState->phase = AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FREEFILE_DIRECTORY;
            goto doMore;
        break;
        case AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FREEFILE_DIRECTORY:
            // First update the freefile's directory entry to remove the first supercluster so we don't risk cross-linking the file
            status = afatfs_saveDirectoryEntry(&afatfs.freeFile, AFATFS_SAVE_DIRECTORY_NORMAL);

            if (status == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FAT;
                goto doMore;
            }
        break;
        case AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FAT:
            status = afatfs_FATFillWithPattern(AFATFS_FAT_PATTERN_TERMINATED_CHAIN, &opState->fatRewriteStartCluster, opState->fatRewriteEndCluster);

            if (status == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FILE_DIRECTORY;
                goto doMore;
            }
        break;
        case AFATFS_APPEND_SUPERCLUSTER_PHASE_UPDATE_FILE_DIRECTORY:
            // Update the fileSize/firstCluster in the directory entry for the file
            status = afatfs_saveDirectoryEntry(file, AFATFS_SAVE_DIRECTORY_NORMAL);
        break;
    }

    if ((status == AFATFS_OPERATION_FAILURE || status == AFATFS_OPERATION_SUCCESS) && file->operation.operation == AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER) {
        file->operation.operation = AFATFS_FILE_OPERATION_NONE;
    }

    return status;
}

/**
 * Attempt to queue up an operation to append the first supercluster of the freefile to the given `file` (file's cursor
 * must be at end-of-file).
 *
 * The new cluster number will be set into the file's cursorCluster.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - The append completed successfully and the file's cursorCluster has been updated
 *     AFATFS_OPERATION_IN_PROGRESS - The operation was queued on the file and will complete later, or there is already an
 *                                    append in progress.
 *     AFATFS_OPERATION_FAILURE     - Operation could not be queued (file was busy) or append failed (filesystem is full).
 *                                    Check afatfs.fileSystemFull
 */
static afatfsOperationStatus_e afatfs_appendSupercluster(afatfsFilePtr_t file)
{
    uint32_t superClusterSize = afatfs_superClusterSize();

    if (file->operation.operation == AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER) {
        return AFATFS_OPERATION_IN_PROGRESS;
    }

    if (afatfs.freeFile.logicalSize < superClusterSize) {
        afatfs.filesystemFull = true;
    }

    if (afatfs.filesystemFull || afatfs_fileIsBusy(file)) {
        return AFATFS_OPERATION_FAILURE;
    }

    afatfsAppendSupercluster_t *opState = &file->operation.state.appendSupercluster;

    file->operation.operation = AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER;
    opState->phase = AFATFS_APPEND_SUPERCLUSTER_PHASE_INIT;
    opState->previousCluster = file->cursorPreviousCluster;

    return afatfs_appendSuperclusterContinue(file);
}

#endif

/**
 * Queue an operation to add a cluster of free space to the end of the file. Must be called when the file's cursor
 * is beyond the last allocated cluster.
 */
static afatfsOperationStatus_e afatfs_appendFreeCluster(afatfsFilePtr_t file)
{
    afatfsOperationStatus_e status;

#ifdef AFATFS_USE_FREEFILE
    if ((file->mode & AFATFS_FILE_MODE_CONTIGUOUS) != 0) {
        // Steal the first cluster from the beginning of the freefile if we can
        status = afatfs_appendSupercluster(file);
    } else
#endif
    {
        status = afatfs_appendRegularFreeCluster(file);
    }

    return status;
}

/**
 * Returns true if the file's cursor is sitting beyond the end of the last allocated cluster (i.e. the logical fileSize
 * is not checked).
 */
static bool afatfs_isEndOfAllocatedFile(afatfsFilePtr_t file)
{
    if (file->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) {
        return file->cursorOffset >= AFATFS_SECTOR_SIZE * afatfs.rootDirectorySectors;
    } else {
        return file->cursorCluster == 0 || afatfs_FATIsEndOfChainMarker(file->cursorCluster);
    }
}

/**
 * Take a lock on the sector at the current file cursor position.
 *
 * Returns a pointer to the sector buffer if successful, or NULL if at the end of file (check afatfs_isEndOfAllocatedFile())
 * or the sector has not yet been read in from disk.
 */
static uint8_t* afatfs_fileRetainCursorSectorForRead(afatfsFilePtr_t file)
{
    uint8_t *result;

    uint32_t physicalSector = afatfs_fileGetCursorPhysicalSector(file);

    /* If we've already got a locked sector then we can assume that was the same one that's at the cursor (because this
     * cache is invalidated when crossing a sector boundary)
     */
    if (file->readRetainCacheIndex != -1) {
        if (!afatfs_assert(physicalSector == afatfs.cacheDescriptor[file->readRetainCacheIndex].sectorIndex)) {
            return NULL;
        }

        result = afatfs_cacheSectorGetMemory(file->readRetainCacheIndex);
    } else {
        if (afatfs_isEndOfAllocatedFile(file)) {
            return NULL;
        }

        afatfs_assert(physicalSector > 0); // We never read the root sector using files

        afatfsOperationStatus_e status = afatfs_cacheSector(
            physicalSector,
            &result,
            AFATFS_CACHE_READ | AFATFS_CACHE_RETAIN,
            0
        );

        if (status != AFATFS_OPERATION_SUCCESS) {
            // Sector not ready for read
            return NULL;
        }

        file->readRetainCacheIndex = afatfs_getCacheDescriptorIndexForBuffer(result);
    }

    return result;
}

/**
 * Lock the sector at the file's cursor position for write, and return a reference to the memory for that sector.
 *
 * Returns NULL if the cache was too busy, try again later.
 */
static uint8_t* afatfs_fileLockCursorSectorForWrite(afatfsFilePtr_t file)
{
    afatfsOperationStatus_e status;
    uint8_t *result;
    uint32_t eraseBlockCount;

    // Do we already have a sector locked in our cache at the cursor position?
    if (file->writeLockedCacheIndex != -1) {
        uint32_t physicalSector = afatfs_fileGetCursorPhysicalSector(file);

        if (!afatfs_assert(physicalSector == afatfs.cacheDescriptor[file->writeLockedCacheIndex].sectorIndex)) {
            return NULL;
        }

        result = afatfs_cacheSectorGetMemory(file->writeLockedCacheIndex);
    } else {
        // Find / allocate a sector and lock it in the cache so we can rely on it sticking around

        // Are we at the start of an empty file or the end of a non-empty file? If so we need to add a cluster
        if (afatfs_isEndOfAllocatedFile(file) && afatfs_appendFreeCluster(file) != AFATFS_OPERATION_SUCCESS) {
            // The extension of the file is in progress so please call us again later to try again
            return NULL;
        }

        uint32_t physicalSector = afatfs_fileGetCursorPhysicalSector(file);
        uint8_t cacheFlags = AFATFS_CACHE_WRITE | AFATFS_CACHE_LOCK;
        uint32_t cursorOffsetInSector = file->cursorOffset % AFATFS_SECTOR_SIZE;
        uint32_t offsetOfStartOfSector = file->cursorOffset & ~((uint32_t) AFATFS_SECTOR_SIZE - 1);
        uint32_t offsetOfEndOfSector = offsetOfStartOfSector + AFATFS_SECTOR_SIZE;

        /*
         * If there is data before the write point in this sector, or there could be data after the write-point
         * then we need to have the original contents of the sector in the cache for us to merge into
         */
        if (
            cursorOffsetInSector > 0
            || offsetOfEndOfSector < file->logicalSize
        ) {
            cacheFlags |= AFATFS_CACHE_READ;
        }

        // In contiguous append mode, we'll pre-erase the whole supercluster
        if ((file->mode & (AFATFS_FILE_MODE_APPEND | AFATFS_FILE_MODE_CONTIGUOUS)) == (AFATFS_FILE_MODE_APPEND | AFATFS_FILE_MODE_CONTIGUOUS)) {
            uint32_t cursorOffsetInSupercluster = file->cursorOffset & (afatfs_superClusterSize() - 1);

            eraseBlockCount = afatfs_fatEntriesPerSector() * afatfs.sectorsPerCluster - cursorOffsetInSupercluster / AFATFS_SECTOR_SIZE;
        } else {
            eraseBlockCount = 0;
        }

        status = afatfs_cacheSector(
            physicalSector,
            &result,
            cacheFlags,
            eraseBlockCount
        );

        if (status != AFATFS_OPERATION_SUCCESS) {
            // Not enough cache available to accept this write / sector not ready for read
            return NULL;
        }

        file->writeLockedCacheIndex = afatfs_getCacheDescriptorIndexForBuffer(result);
    }

    return result;
}

/**
 * Attempt to seek the file pointer by the offset, relative to the current position.
 *
 * Returns true if the seek was completed, or false if you should try again later by calling this routine again (the
 * cursor is not moved and no seek operation is queued on the file for you).
 *
 * You can only seek forwards by the size of a cluster or less, or backwards to stay within the same cluster. Otherwise
 * false will always be returned (calling this routine again will never make progress on the seek).
 *
 * This amount of seek is special because we will have to wait on at most one read operation, so it's easy to make
 * the seek atomic.
 */
static bool afatfs_fseekAtomic(afatfsFilePtr_t file, int32_t offset)
{
    // Seeks within a sector
    uint32_t newSectorOffset = offset + file->cursorOffset % AFATFS_SECTOR_SIZE;

    // i.e. newSectorOffset is non-negative and smaller than AFATFS_SECTOR_SIZE, we're staying within the same sector
    if (newSectorOffset < AFATFS_SECTOR_SIZE) {
        file->cursorOffset += offset;
        return true;
    }

    // We're seeking outside the sector so unlock it if we were holding it
    afatfs_fileUnlockCacheSector(file);

    // FAT16 root directories are made up of contiguous sectors rather than clusters
    if (file->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) {
        file->cursorOffset += offset;

        return true;
    }

    uint32_t clusterSizeBytes = afatfs_clusterSize();
    uint32_t offsetInCluster = afatfs_byteIndexInCluster(file->cursorOffset);
    uint32_t newOffsetInCluster = offsetInCluster + offset;

    afatfsOperationStatus_e status;

    if (offset > (int32_t) clusterSizeBytes || offset < -(int32_t) offsetInCluster) {
        return false;
    }

    // Are we seeking outside the cluster? If so we'll need to find out the next cluster number
    if (newOffsetInCluster >= clusterSizeBytes) {
        uint32_t nextCluster;

        status = afatfs_fileGetNextCluster(file, file->cursorCluster, &nextCluster);

        if (status == AFATFS_OPERATION_SUCCESS) {
            // Seek to the beginning of the next cluster
            uint32_t bytesToSeek = clusterSizeBytes - offsetInCluster;

            file->cursorPreviousCluster = file->cursorCluster;
            file->cursorCluster = nextCluster;
            file->cursorOffset += bytesToSeek;

            offset -= bytesToSeek;
        } else {
            // Try again later
            return false;
        }
    }

    // If we didn't already hit the end of the file, add any remaining offset needed inside the cluster
    if (!afatfs_isEndOfAllocatedFile(file)) {
        file->cursorOffset += offset;
    }

    return true;
}

/**
 * Returns true if the seek was completed, or false if it is still in progress.
 */
static bool afatfs_fseekInternalContinue(afatfsFile_t *file)
{
    afatfsSeek_t *opState = &file->operation.state.seek;
    uint32_t clusterSizeBytes = afatfs_clusterSize();
    uint32_t offsetInCluster = afatfs_byteIndexInCluster(file->cursorOffset);

    afatfsOperationStatus_e status;

    // Keep advancing the cursor cluster forwards to consume seekOffset
    while (offsetInCluster + opState->seekOffset >= clusterSizeBytes && !afatfs_isEndOfAllocatedFile(file)) {
        uint32_t nextCluster;

        status = afatfs_fileGetNextCluster(file, file->cursorCluster, &nextCluster);

        if (status == AFATFS_OPERATION_SUCCESS) {
            // Seek to the beginning of the next cluster
            uint32_t bytesToSeek = clusterSizeBytes - offsetInCluster;

            file->cursorPreviousCluster = file->cursorCluster;
            file->cursorCluster = nextCluster;

            file->cursorOffset += bytesToSeek;
            opState->seekOffset -= bytesToSeek;
            offsetInCluster = 0;
        } else {
            // Try again later
            return false;
        }
    }

    // If we didn't already hit the end of the file, add any remaining offset needed inside the cluster
    if (!afatfs_isEndOfAllocatedFile(file)) {
        file->cursorOffset += opState->seekOffset;
    }

    afatfs_fileUpdateFilesize(file); // TODO do we need this?

    {
        afatfsFileCallback_t callback = opState->callback;
        afatfsOpenResultCallback_t resultCallback = opState->resultCallback;
        afatfsFilePtr_t retainedParent = opState->retainedParent;

        /*
         * Finish both ordinary seeks and append-open handoffs.
         *
         * Callback fields are copied before clearing the operation union;
         * parent ownership is released before publication so completion code
         * can close/reuse it immediately. A queued forward seek has no terminal
         * error branch here: unavailable FAT/cache data keeps it in progress.
         * Affiliates: afatfs_fseekInternal() and fopenChild("a").
         */
        file->operation.operation = AFATFS_FILE_OPERATION_NONE;
        afatfs_releaseChildParent(retainedParent);
        if (resultCallback)
            resultCallback(AFATFS_RESULT_OK, file);
        else if (callback)
            callback(file);
    }

    return true;
}

/**
 * Seek the file pointer forwards by offset bytes. Calls the callback when the seek is complete.
 *
 * Will happily seek beyond the logical end of the file.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - The seek was completed immediately
 *     AFATFS_OPERATION_IN_PROGRESS - The seek was queued and will complete later
 *     AFATFS_OPERATION_FAILURE     - The seek could not be queued because the file was busy with another operation,
 *                                    try again later.
 */
static afatfsOperationStatus_e afatfs_fseekInternal(
        afatfsFilePtr_t file,
        uint32_t offset,
        afatfsFileCallback_t callback,
        afatfsOpenResultCallback_t resultCallback,
        afatfsFilePtr_t retainedParent)
{
    // See if we can seek without queuing an operation
    if (afatfs_fseekAtomic(file, offset)) {
        /*
         * Atomic and queued seeks share identical parent/callback ordering.
         * Releasing first avoids a timing-dependent race where a small file's
         * callback can reuse the parent but a multi-cluster file's cannot.
         */
        afatfs_releaseChildParent(retainedParent);
        if (resultCallback) {
            resultCallback(AFATFS_RESULT_OK, file);
        } else if (callback) {
            callback(file);
        }

        return AFATFS_OPERATION_SUCCESS;
    } else {
        // Our operation must queue
        if (afatfs_fileIsBusy(file)) {
            return AFATFS_OPERATION_FAILURE;
        }

        afatfsSeek_t *opState = &file->operation.state.seek;

        file->operation.operation = AFATFS_FILE_OPERATION_SEEK;
        opState->callback = callback;
        opState->resultCallback = resultCallback;
        opState->retainedParent = retainedParent;
        opState->seekOffset = offset;

        return AFATFS_OPERATION_IN_PROGRESS;
    }
}

/**
 * Attempt to seek the file cursor from the given point (`whence`) by the given offset, just like C's fseek().
 *
 * AFATFS_SEEK_SET with offset 0 will always return AFATFS_OPERATION_SUCCESS.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS     - The seek was completed immediately
 *     AFATFS_OPERATION_IN_PROGRESS - The seek was queued and will complete later. Feel free to attempt read/write
 *                                    operations on the file, they'll fail until the seek is complete.
 *     AFATFS_OPERATION_FAILURE     - The seek could not be queued because the file was busy with another operation,
 *                                    try again later.
 */
afatfsOperationStatus_e afatfs_fseek(afatfsFilePtr_t file, int32_t offset, afatfsSeek_e whence)
{
    // We need an up-to-date logical filesize so we can clamp seeks to the EOF
    afatfs_fileUpdateFilesize(file);

    switch (whence) {
        case AFATFS_SEEK_CUR:
            if (offset >= 0) {
                // Only forwards seeks are supported by this routine:
                return afatfs_fseekInternal(
                    file,
                    MIN(file->cursorOffset + offset, file->logicalSize),
                    NULL, NULL, NULL);
            }

            // Convert a backwards relative seek into a SEEK_SET. TODO considerable room for improvement if within the same cluster
            offset += file->cursorOffset;
        break;

        case AFATFS_SEEK_END:
            // Are we already at the right position?
            if (file->logicalSize + offset == file->cursorOffset) {
                return AFATFS_OPERATION_SUCCESS;
            }

            // Convert into a SEEK_SET
            offset += file->logicalSize;
        break;

        case AFATFS_SEEK_SET:
            ;
            // Fall through
    }

    // Now we have a SEEK_SET with a positive offset. Begin by seeking to the start of the file
    afatfs_fileUnlockCacheSector(file);

    file->cursorPreviousCluster = 0;
    file->cursorCluster = file->firstCluster;
    file->cursorOffset = 0;

    // Then seek forwards by the offset
    return afatfs_fseekInternal(file,
                                MIN((uint32_t)offset, file->logicalSize),
                                NULL, NULL, NULL);
}

/**
 * Get the byte-offset of the file's cursor from the start of the file.
 *
 * Returns true on success, or false if the file is busy (try again later).
 */
bool afatfs_ftell(afatfsFilePtr_t file, uint32_t *position)
{
    if (afatfs_fileIsBusy(file)) {
        return false;
    } else {
        *position = file->cursorOffset;
        return true;
    }
}

/**
 * Attempt to advance the directory pointer `finder` to the next entry in the directory.
 *
 * Returns:
 *     AFATFS_OPERATION_SUCCESS -     A pointer to the next directory entry has been loaded into *dirEntry. If the
 *                                    directory was exhausted then *dirEntry will be set to NULL.
 *     AFATFS_OPERATION_IN_PROGRESS - The disk is busy. The pointer is not advanced, call again later to retry.
 */
afatfsOperationStatus_e afatfs_findNext(afatfsFilePtr_t directory, afatfsFinder_t *finder, fatDirectoryEntry_t **dirEntry)
{
    uint8_t *sector;

    if (finder->entryIndex == AFATFS_FILES_PER_DIRECTORY_SECTOR - 1) {
        if (afatfs_fseekAtomic(directory, AFATFS_SECTOR_SIZE)) {
            finder->entryIndex = -1;
            // Fall through to read the first entry of that new sector
        } else {
            return AFATFS_OPERATION_IN_PROGRESS;
        }
    }

    sector = afatfs_fileRetainCursorSectorForRead(directory);

    if (sector) {
        finder->entryIndex++;

        *dirEntry = (fatDirectoryEntry_t*) sector + finder->entryIndex;

        finder->sectorNumberPhysical = afatfs_fileGetCursorPhysicalSector(directory);

        return AFATFS_OPERATION_SUCCESS;
    } else {
        if (afatfs_isEndOfAllocatedFile(directory)) {
            *dirEntry = NULL;

            return AFATFS_OPERATION_SUCCESS;
        }

        return AFATFS_OPERATION_IN_PROGRESS;
    }
}

/**
 * Release resources associated with a find operation. Calling this more than once is harmless.
 */
void afatfs_findLast(afatfsFilePtr_t directory)
{
    afatfs_fileUnlockCacheSector(directory);
}

/**
 * Initialise the finder so that the first call with the directory to findNext() will return the first file in the
 * directory.
 */
void afatfs_findFirst(afatfsFilePtr_t directory, afatfsFinder_t *finder)
{
    afatfs_fseek(directory, 0, AFATFS_SEEK_SET);
    finder->entryIndex = -1;
}

static void afatfs_objectScanReset(afatfsObjectFinder_t *finder)
{
    /*
     * Clear the pending VFAT fragment chain for object enumeration.
     *
     * The raw finder can encounter deleted entries, ordinary SFN entries, or a
     * terminator at any time. Resetting the LFN side state at those boundaries
     * prevents stale fragments from naming the next physical file/directory.
     */
    finder->lfnValid = 0u;
    finder->lfnChecksum = 0u;
    finder->lfnEntryCount = 0u;
    memset(finder->lfnName, 0, sizeof(finder->lfnName));
    finder->lfnFirstEntry.sectorNumberPhysical = 0u;
    finder->lfnFirstEntry.entryIndex = -1;
}

static void afatfs_objectScanAppendLfn(afatfsObjectFinder_t *finder,
                                       const afatfsFinder_t *rawFinder,
                                       const fatDirectoryEntry_t *entry)
{
    static const uint8_t offsets[FAT_LFN_CHARS_PER_ENTRY] = {
        1u, 3u, 5u, 7u, 9u,
        14u, 16u, 18u, 20u, 22u, 24u,
        28u, 30u
    };
    const uint8_t *raw = (const uint8_t *)entry;
    uint8_t seq = raw[0] & 0x1fu;

    /*
     * Rebuild one ASCII-safe VFAT display name while walking raw entries.
     *
     * VFAT stores the final text fragment first and marks it with 0x40. The
     * low five bits are the one-based ordinal, so `(seq - 1) * 13` recovers
     * the absolute display-name offset for this fragment. The checksum must
     * remain identical across fragments; final validation happens when the next
     * SFN entry arrives because that entry owns the physical object metadata.
     */
    if (seq == 0u ||
        seq > ((AFATFS_LONG_FILENAME_MAX + FAT_LFN_CHARS_PER_ENTRY - 1u) /
               FAT_LFN_CHARS_PER_ENTRY)) {
        afatfs_objectScanReset(finder);
        return;
    }

    if (raw[0] & FAT_LFN_LAST_LONG_ENTRY) {
        afatfs_objectScanReset(finder);
        finder->lfnValid = 1u;
        finder->lfnChecksum = raw[13];
        finder->lfnFirstEntry = *rawFinder;
    } else if (!finder->lfnValid) {
        return;
    } else if (finder->lfnChecksum != raw[13]) {
        afatfs_objectScanReset(finder);
        return;
    }

    uint8_t pos = (uint8_t)((seq - 1u) * FAT_LFN_CHARS_PER_ENTRY);
    for (uint8_t i = 0u;
         i < FAT_LFN_CHARS_PER_ENTRY && pos < AFATFS_LONG_FILENAME_MAX;
         i++, pos++) {
        uint16_t ch = (uint16_t)raw[offsets[i]] |
                      ((uint16_t)raw[offsets[i] + 1u] << 8);
        if (ch == 0x0000u)
            break;
        if (ch == 0xffffu)
            continue;
        /*
         * Firmware-created names are ASCII today. Host-created non-ASCII names
         * are retained as visible underscores so object sorting and browsing
         * stay bounded without pretending we have full Unicode rendering.
         */
        finder->lfnName[pos] = (ch < 0x80u) ? (char)ch : '_';
    }
    finder->lfnEntryCount++;
}

static bool afatfs_isStructuralDotEntry(const fatDirectoryEntry_t *entry)
{
    /*
     * Hide only FAT's synthetic directory links.
     *
     * Ordinary host files may begin with '.', and product scanners should see
     * those concrete objects. The structural entries are SFN directory records
     * whose raw base is "." or ".." padded with spaces.
     */
    if (!entry || (entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0)
        return false;
    if (entry->filename[0] != '.')
        return false;
    if (entry->filename[1] == ' ')
        return true;
    if (entry->filename[1] != '.')
        return false;
    return entry->filename[2] == ' ';
}

void afatfs_findFirstObject(afatfsFilePtr_t directory,
                            afatfsObjectFinder_t *finder)
{
    /*
     * Initialize an LFN-aware directory scan.
     *
     * This wraps the raw afatfs_findFirst() cursor with VFAT reconstruction
     * state. Callers must pair it with afatfs_findLastObject() just as raw
     * scanners pair findFirst/findLast, because the underlying directory sector
     * may be retained in the asyncfatfs cache while scanning.
     */
    afatfs_findFirst(directory, &finder->raw);
    afatfs_objectScanReset(finder);
}

afatfsOperationStatus_e afatfs_findNextObject(afatfsFilePtr_t directory,
                                              afatfsObjectFinder_t *finder,
                                              afatfsObjectInfo_t *object)
{
    /*
     * Return the next concrete file/directory object from a FAT directory.
     *
     * Output kind == AFATFS_OBJECT_NONE means the directory is exhausted. LFN,
     * deleted, volume-label, dot, and terminator records are not returned as
     * objects. The loop intentionally consumes multiple raw entries per public
     * object so higher layers cannot accidentally treat a VFAT fragment as a
     * file they should open or display.
     */
    if (!object)
        return AFATFS_OPERATION_FAILURE;

    memset(object, 0, sizeof(*object));
    object->id.kind = AFATFS_OBJECT_NONE;

    for (;;) {
        fatDirectoryEntry_t *entry = NULL;
        afatfsOperationStatus_e status =
            afatfs_findNext(directory, &finder->raw, &entry);

        if (status != AFATFS_OPERATION_SUCCESS)
            return status;
        if (entry == NULL || fat_isDirectoryEntryTerminator(entry)) {
            afatfs_objectScanReset(finder);
            return AFATFS_OPERATION_SUCCESS;
        }
        if (fat_isDirectoryEntryEmpty(entry)) {
            afatfs_objectScanReset(finder);
            continue;
        }
        if (fat_isLongDirectoryEntry(entry)) {
            afatfs_objectScanAppendLfn(finder, &finder->raw, entry);
            continue;
        }
        if (entry->attrib & FAT_FILE_ATTRIBUTE_VOLUME_ID) {
            afatfs_objectScanReset(finder);
            continue;
        }

        fat_convertFATStyleToFilename(entry->filename, object->id.shortName);
        fat_applyFilenameCaseFlags(object->id.shortName, entry->ntReserved);
        if (afatfs_isStructuralDotEntry(entry)) {
            afatfs_objectScanReset(finder);
            continue;
        }

        object->id.kind = (entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY)
            ? AFATFS_OBJECT_DIRECTORY
            : AFATFS_OBJECT_FILE;
        object->id.attrib = entry->attrib;
        object->ntReserved = entry->ntReserved;
        object->id.sfnEntry = finder->raw;
        object->id.firstCluster = ((uint32_t)entry->firstClusterHigh << 16u) |
                                  entry->firstClusterLow;
        object->id.logicalSize = entry->fileSize;
        /*
         * Capture the short-lived physical validation fingerprint.
         *
         * What: retain the scanning parent's cluster/root form and the exact
         * 11 raw SFN bytes alongside the entry pointer. Why: a deleted entry
         * slot can later be reused at the same sector/index; pointer and
         * display text alone would let a delayed mutator target the replacement
         * object. Inputs: directory is the live scan handle and entry is the
         * authoritative SFN just returned by the raw iterator. Outputs: every
         * non-NONE object ID is self-contained for a later stale-entry check.
         * Affiliates: native delete validation, by-identity rename/move/copy,
         * and transaction recovery.
         */
        object->id.parentFirstCluster = directory->firstCluster;
        object->id.parentIsFat16Root =
            directory->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY ? 1u : 0u;
        memcpy(object->id.rawShortName,
               entry->filename,
               FAT_FILENAME_LENGTH);

        if (finder->lfnValid &&
            finder->lfnChecksum ==
                fat_lfnChecksum((const uint8_t *)entry->filename) &&
            finder->lfnName[0] != '\0') {
            uint8_t i;

            /*
             * Prefer checksum-verified VFAT display text. This is the exact
             * mixed-case spelling the test menus must show and match.
             */
            for (i = 0u;
                 i < AFATFS_LONG_FILENAME_MAX && finder->lfnName[i] != '\0';
                 i++)
                object->id.displayName[i] = finder->lfnName[i];
            object->id.displayName[i] = '\0';
            object->hasLongName = 1u;
            object->id.lfnEntryCount = finder->lfnEntryCount;
            object->id.lfnFirstEntry = finder->lfnFirstEntry;
        } else {
            uint8_t i;

            /*
             * Fall back to the case-preserved SFN alias when no valid LFN
             * chain belongs to this object. This keeps legacy 8.3 media fully
             * browsable while still hiding raw uppercase storage bytes.
             */
            for (i = 0u;
                 i < AFATFS_LONG_FILENAME_MAX &&
                 object->id.shortName[i] != '\0';
                 i++)
                object->id.displayName[i] = object->id.shortName[i];
            object->id.displayName[i] = '\0';
        }

        afatfs_objectScanReset(finder);
        return AFATFS_OPERATION_SUCCESS;
    }
}

void afatfs_findLastObject(afatfsFilePtr_t directory,
                           afatfsObjectFinder_t *finder)
{
    /*
     * Release resources retained by the underlying raw directory scan and
     * discard any incomplete LFN chain from the caller's finder state.
     */
    afatfs_objectScanReset(finder);
    afatfs_findLast(directory);
}

static afatfsOperationStatus_e afatfs_extendSubdirectoryContinue(afatfsFile_t *directory)
{
    afatfsExtendSubdirectory_t *opState = &directory->operation.state.extendSubdirectory;
    afatfsOperationStatus_e status;
    uint8_t *sectorBuffer;
    uint32_t clusterNumber, physicalSector;
    uint16_t sectorInCluster;

    doMore:
    switch (opState->phase) {
        case AFATFS_EXTEND_SUBDIRECTORY_PHASE_ADD_FREE_CLUSTER:
            status = afatfs_appendRegularFreeClusterContinue(directory);

            if (status == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS;
                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
                opState->phase = AFATFS_EXTEND_SUBDIRECTORY_PHASE_FAILURE;
                goto doMore;
            }
        break;
        case AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS:
            // Now, zero out that cluster
            afatfs_fileGetCursorClusterAndSector(directory, &clusterNumber, &sectorInCluster);
            physicalSector = afatfs_fileGetCursorPhysicalSector(directory);

            while (1) {
                status = afatfs_cacheSector(physicalSector, &sectorBuffer, AFATFS_CACHE_WRITE, 0);

                if (status != AFATFS_OPERATION_SUCCESS) {
                    return status;
                }

                /*
                 * Zero every sector in the newly-appended cluster. FAT
                 * directory scans stop at empty entries, so clearing the whole
                 * cluster prevents stale card data from looking like child
                 * files. sectorInCluster is compared against
                 * sectorsPerCluster - 1 because both values are zero-based
                 * within the new cluster.
                 */
                memset(sectorBuffer, 0, AFATFS_SECTOR_SIZE);

                /*
                 * The first sector of a non-root subdirectory must start with
                 * "." and "..". directory->firstCluster points "." back at
                 * this directory; parentDirectory supplied by mkdir/create
                 * points ".." back at the directory that contained the
                 * newly-created entry. Later directory extensions pass
                 * parentDirectory == NULL and skip this block because
                 * cursorOffset is no longer zero.
                 */
                if (directory->directoryEntryPos.sectorNumberPhysical != 0 && directory->cursorOffset == 0) {
                    fatDirectoryEntry_t *dirEntries = (fatDirectoryEntry_t *) sectorBuffer;

                    memset(dirEntries[0].filename, ' ', sizeof(dirEntries[0].filename));
                    dirEntries[0].filename[0] = '.';
                    dirEntries[0].firstClusterHigh = directory->firstCluster >> 16;
                    dirEntries[0].firstClusterLow = directory->firstCluster & 0xFFFF;
                    dirEntries[0].attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;

                    memset(dirEntries[1].filename, ' ', sizeof(dirEntries[1].filename));
                    dirEntries[1].filename[0] = '.';
                    dirEntries[1].filename[1] = '.';
                    dirEntries[1].firstClusterHigh = opState->parentDirectoryCluster >> 16;
                    dirEntries[1].firstClusterLow = opState->parentDirectoryCluster & 0xFFFF;
                    dirEntries[1].attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
                }

                if (sectorInCluster < afatfs.sectorsPerCluster - 1) {
                    // Move to next sector
                    afatfs_assert(afatfs_fseekAtomic(directory, AFATFS_SECTOR_SIZE));
                    sectorInCluster++;
                    physicalSector++;
                } else {
                    break;
                }
            }

            // Seek back to the beginning of the cluster
            afatfs_assert(afatfs_fseekAtomic(directory, -AFATFS_SECTOR_SIZE * (afatfs.sectorsPerCluster - 1)));
            opState->phase = AFATFS_EXTEND_SUBDIRECTORY_PHASE_SUCCESS;
            goto doMore;
        break;
        case AFATFS_EXTEND_SUBDIRECTORY_PHASE_SUCCESS:
        {
            afatfsFileCallback_t callback = opState->callback;
            afatfsOpenResultCallback_t resultCallback = opState->resultCallback;
            afatfsFilePtr_t retainedParent = opState->retainedParent;

            /*
             * Publish a successfully initialized directory after releasing its
             * parent lock.
             *
             * CREATE_FILE handed its callbacks and retained parent through the
             * shared operation union, so copy them before clearing the state.
             * The parent count is decremented first: callback code may legally
             * close the parent or queue the next child immediately. Ordinary
             * directory extension supplies no retained parent or structured
             * callback and follows the legacy branch unchanged.
             */
            directory->operation.operation = AFATFS_FILE_OPERATION_NONE;
            if (retainedParent) {
                afatfs_assert(retainedParent->childOperationRetainCount == 1u);
                retainedParent->childOperationRetainCount = 0u;
            }
            if (resultCallback)
                resultCallback(AFATFS_RESULT_OK, directory);
            else if (callback)
                callback(directory);

            return AFATFS_OPERATION_SUCCESS;
        }
        break;
        case AFATFS_EXTEND_SUBDIRECTORY_PHASE_FAILURE:
        {
            afatfsFileCallback_t callback = opState->callback;
            afatfsOpenResultCallback_t resultCallback = opState->resultCallback;
            afatfsFilePtr_t retainedParent = opState->retainedParent;
            uint8_t destroyOnFailure = opState->destroyOnFailure;
            afatfsResultCode_t result = afatfs.filesystemFull
                ? AFATFS_RESULT_NO_SPACE
                : AFATFS_RESULT_IO_ERROR;

            /*
             * Unwind a failed first-cluster allocation without leaking either
             * handle.
             *
             * A new directory is not usable until its cluster and structural
             * entries exist, so destroyOnFailure returns that recycled slot to
             * openFiles[]. Existing-directory extension keeps its live handle.
             * Callback and parent ownership are copied before initialization
             * clears the operation union. Affiliates: mkdir child creation,
             * allocator-triggered extension, and handle-pool retry behavior.
             */
            directory->operation.operation = AFATFS_FILE_OPERATION_NONE;
            if (retainedParent) {
                afatfs_assert(retainedParent->childOperationRetainCount == 1u);
                retainedParent->childOperationRetainCount = 0u;
            }
            if (destroyOnFailure)
                afatfs_initFileHandle(directory);
            if (resultCallback)
                resultCallback(result, NULL);
            else if (callback)
                callback(NULL);
            return AFATFS_OPERATION_FAILURE;
        }
        break;
    }

    return AFATFS_OPERATION_IN_PROGRESS;
}

/**
 * Queue an operation to add a cluster to a sub-directory.
 *
 * Tthe new cluster is zero-filled. "." and ".." entries are added if it is the first cluster of a new subdirectory.
 *
 * The directory must not be busy, otherwise AFATFS_OPERATION_FAILURE is returned immediately.
 *
 * The directory's cursor must lie at the end of the directory file (i.e. isEndOfAllocatedFile() would return true).
 *
 * You must provide parentDirectory if this is the first extension to the subdirectory, otherwise pass NULL for that argument.
 */
static afatfsOperationStatus_e afatfs_extendSubdirectory(
        afatfsFile_t *directory,
        afatfsFilePtr_t parentDirectory,
        afatfsFileCallback_t callback,
        afatfsOpenResultCallback_t resultCallback,
        afatfsFilePtr_t retainedParent,
        uint8_t destroyOnFailure)
{
    // FAT16 root directories cannot be extended
    if (directory->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY || afatfs_fileIsBusy(directory)) {
        return AFATFS_OPERATION_FAILURE;
    }

    /*
     * We'll assume that we're never asked to append the first cluster of a root directory, since any
     * reasonably-formatted volume should have a root!
     */
    afatfsExtendSubdirectory_t *opState = &directory->operation.state.extendSubdirectory;

    directory->operation.operation = AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY;

    opState->phase = AFATFS_EXTEND_SUBDIRECTORY_PHASE_INITIAL;
    opState->parentDirectoryCluster = parentDirectory ? parentDirectory->firstCluster : 0;
    opState->callback = callback;
    /*
     * Preserve create-specific completion/ownership across the operation-union
     * handoff. Inputs are NULL/zero for ordinary directory growth; first-time
     * mkdir supplies both parentDirectory (for `..`) and retainedParent (for
     * lifetime release). Output is consumed only by the terminal continuation.
     */
    opState->resultCallback = resultCallback;
    opState->retainedParent = retainedParent;
    opState->destroyOnFailure = destroyOnFailure;

    afatfs_appendRegularFreeClusterInitOperationState(&opState->appendFreeCluster, directory->cursorPreviousCluster);

    return afatfs_extendSubdirectoryContinue(directory);
}

/**
 * Allocate space for a new directory entry to be written, store the position of that entry in the finder, and set
 * the *dirEntry pointer to point to the entry within the cached FAT sector. This pointer's lifetime is only as good
 * as the life of the cache, so don't dawdle.
 *
 * Before the first call to this function, call afatfs_findFirst() on the directory.
 *
 * The directory sector in the cache is marked as dirty, so any changes written through to the entry will be flushed out
 * in a subsequent poll cycle.
 *
 * Returns:
 *     AFATFS_OPERATION_IN_PROGRESS - Call again later to continue
 *     AFATFS_OPERATION_SUCCESS     - Entry has been inserted and *dirEntry and *finder have been updated
 *     AFATFS_OPERATION_FAILURE     - When the directory is full.
 */
static afatfsOperationStatus_e afatfs_allocateDirectoryEntry(afatfsFilePtr_t directory, fatDirectoryEntry_t **dirEntry, afatfsFinder_t *finder)
{
    afatfsOperationStatus_e result;

    if (afatfs_fileIsBusy(directory)) {
        return AFATFS_OPERATION_IN_PROGRESS;
    }

    while ((result = afatfs_findNext(directory, finder, dirEntry)) == AFATFS_OPERATION_SUCCESS) {
        if (*dirEntry) {
            if (fat_isDirectoryEntryEmpty(*dirEntry) || fat_isDirectoryEntryTerminator(*dirEntry)) {
                afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer((uint8_t*) *dirEntry));

                afatfs_findLast(directory);
                return AFATFS_OPERATION_SUCCESS;
            }
        } else {
            /*
             * This allocator may extend an already-initialized current
             * directory, but it must not create the first cluster of a
             * subdirectory. The first cluster needs a real parentDirectory so
             * ".." can be written correctly; only mkdir/create still has that
             * parent available before chdir() copies the child into
             * currentDirectory.
             */
            if (directory->type == AFATFS_FILE_TYPE_DIRECTORY &&
                directory->directoryEntryPos.sectorNumberPhysical != 0 &&
                directory->firstCluster == 0) {
                return AFATFS_OPERATION_FAILURE;
            }
            // Need to extend directory size by adding a cluster
            result = afatfs_extendSubdirectory(directory, NULL, NULL,
                                               NULL, NULL, 0u);

            if (result == AFATFS_OPERATION_SUCCESS) {
                // Continue the search in the newly-extended directory
                continue;
            } else {
                // The status (in progress or failure) of extending the directory becomes our status
                break;
            }
        }
    }

    return result;
}

/**
 * Return a pointer to a free entry in the open files table (a file whose type is "NONE"). You should initialize
 * the file afterwards with afatfs_initFileHandle().
 */
static afatfsFilePtr_t afatfs_allocateFileHandle()
{
    for (int i = 0; i < AFATFS_MAX_OPEN_FILES; i++) {
        if (afatfs.openFiles[i].type == AFATFS_FILE_TYPE_NONE) {
            return &afatfs.openFiles[i];
        }
    }

    return NULL;
}

/**
 * Continue the file truncation.
 *
 * When truncation finally succeeds or fails, the current operation is cleared on the file (if the current operation
 * was a truncate), then the truncate operation's callback is called. This allows the truncation to be called as a
 * sub-operation without it clearing the parent file operation.
 */
static afatfsOperationStatus_e afatfs_ftruncateContinue(afatfsFilePtr_t file, bool markDeleted)
{
    afatfsTruncateFile_t *opState = &file->operation.state.truncateFile;
    afatfsOperationStatus_e status;

#ifdef AFATFS_USE_FREEFILE
    uint32_t oldFreeFileStart, freeFileGrow;
#endif

    doMore:

    switch (opState->phase) {
        case AFATFS_TRUNCATE_FILE_UPDATE_DIRECTORY:
            status = afatfs_saveDirectoryEntry(file, markDeleted ? AFATFS_SAVE_DIRECTORY_DELETED : AFATFS_SAVE_DIRECTORY_NORMAL);

            if (status == AFATFS_OPERATION_SUCCESS) {
                if(opState->currentCluster == 0x0){ //current cluster 0 at this phase means it is an empty file
                    opState->phase = AFATFS_TRUNCATE_FILE_SUCCESS;
                    goto doMore;
                }
#ifdef AFATFS_USE_FREEFILE
                if (opState->endCluster) {
                    opState->phase = AFATFS_TRUNCATE_FILE_ERASE_FAT_CHAIN_CONTIGUOUS;
                } else
#endif
                {
                    opState->phase = AFATFS_TRUNCATE_FILE_ERASE_FAT_CHAIN_NORMAL;
                }
                goto doMore;
            }
        break;
#ifdef AFATFS_USE_FREEFILE
        case AFATFS_TRUNCATE_FILE_ERASE_FAT_CHAIN_CONTIGUOUS:
            // Prepare the clusters to be added back on to the beginning of the freefile
            status = afatfs_FATFillWithPattern(AFATFS_FAT_PATTERN_UNTERMINATED_CHAIN, &opState->currentCluster, opState->endCluster);

            if (status == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_TRUNCATE_FILE_PREPEND_TO_FREEFILE;
                goto doMore;
            }
        break;
        case AFATFS_TRUNCATE_FILE_PREPEND_TO_FREEFILE:
            // Note, it's okay to run this code several times:
            oldFreeFileStart = afatfs.freeFile.firstCluster;

            afatfs.freeFile.firstCluster = opState->startCluster;

            freeFileGrow = (oldFreeFileStart - opState->startCluster) * afatfs_clusterSize();

            afatfs.freeFile.logicalSize += freeFileGrow;
            afatfs.freeFile.physicalSize += freeFileGrow;

            status = afatfs_saveDirectoryEntry(&afatfs.freeFile, AFATFS_SAVE_DIRECTORY_NORMAL);
            if (status == AFATFS_OPERATION_SUCCESS) {
                opState->phase = AFATFS_TRUNCATE_FILE_SUCCESS;
                goto doMore;
            }
        break;
#endif
        case AFATFS_TRUNCATE_FILE_ERASE_FAT_CHAIN_NORMAL:
            while (!afatfs_FATIsEndOfChainMarker(opState->currentCluster)) {
                uint32_t nextCluster;

                status = afatfs_FATGetNextCluster(0, opState->currentCluster, &nextCluster);

                if (status != AFATFS_OPERATION_SUCCESS) {
                    return status;
                }

                status = afatfs_FATSetNextCluster(opState->currentCluster, 0);

                if (status != AFATFS_OPERATION_SUCCESS) {
                    return status;
                }

                opState->currentCluster = nextCluster;

                // Searches for unallocated regular clusters should be told about this free cluster now
                afatfs.lastClusterAllocated = MIN(afatfs.lastClusterAllocated, opState->currentCluster - 1);
            }

            opState->phase = AFATFS_TRUNCATE_FILE_SUCCESS;
            goto doMore;
        break;
        case AFATFS_TRUNCATE_FILE_SUCCESS:
        {
            afatfsFileCallback_t callback = opState->callback;
            afatfsOpenResultCallback_t resultCallback = opState->resultCallback;
            afatfsFilePtr_t retainedParent = opState->retainedParent;

            /*
             * Complete a standalone truncate or a write-open handoff.
             *
             * The truncate state may be embedded under UNLINK, so only a
             * top-level TRUNCATE clears the operation discriminator. Structured
             * write-open state additionally releases its retained parent and
             * reports OK with the still-open, now-empty file. Affiliates:
             * afatfs_ftruncateInternal(), unlink, and fopenChild("w").
             */
            if (file->operation.operation == AFATFS_FILE_OPERATION_TRUNCATE) {
                file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            }
            afatfs_releaseChildParent(retainedParent);
            if (resultCallback)
                resultCallback(AFATFS_RESULT_OK, file);
            else if (callback)
                callback(file);

            return AFATFS_OPERATION_SUCCESS;
        }
        break;
    }

    if (status == AFATFS_OPERATION_FAILURE && file->operation.operation == AFATFS_FILE_OPERATION_TRUNCATE) {
        afatfsFileCallback_t callback = opState->callback;
        afatfsOpenResultCallback_t resultCallback = opState->resultCallback;
        afatfsFilePtr_t retainedParent = opState->retainedParent;
        afatfsResultCode_t result = afatfs.filesystemFull
            ? AFATFS_RESULT_NO_SPACE
            : AFATFS_RESULT_IO_ERROR;

        /*
         * Publish an accepted truncate failure exactly once.
         *
         * Legacy code historically received no callback on this path, which
         * can strand an outer async state machine. The structured branch also
         * releases the create parent's lock. Inputs are copied before clearing
         * union ownership; output is NULL/error because file contents may have
         * been partially truncated. Affiliates: filesystem timeout handling
         * and the accepted-operation callback invariant.
         */
        file->operation.operation = AFATFS_FILE_OPERATION_NONE;
        afatfs_releaseChildParent(retainedParent);
        if (resultCallback)
            resultCallback(result, NULL);
        else if (callback)
            callback(NULL);
    }

    return status;
}

/**
 * Queue an operation to truncate the file to zero bytes in length.
 *
 * Returns true if the operation was successfully queued or false if the file is busy (try again later).
 *
 * The callback is called once the file has been truncated (some time after this routine returns).
 */
static bool afatfs_ftruncateInternal(
        afatfsFilePtr_t file,
        afatfsFileCallback_t callback,
        afatfsOpenResultCallback_t resultCallback,
        afatfsFilePtr_t retainedParent)
{
    afatfsTruncateFile_t *opState;

    if (afatfs_fileIsBusy(file))
        return false;

    file->operation.operation = AFATFS_FILE_OPERATION_TRUNCATE;

    opState = &file->operation.state.truncateFile;
    opState->callback = callback;
    /*
     * Transfer structured open completion only for create/open callers.
     * Public ftruncate passes NULL for both values; retaining them in the
     * truncate state lets success and failure release parent ownership after
     * CREATE_FILE's union storage has been replaced.
     */
    opState->resultCallback = resultCallback;
    opState->retainedParent = retainedParent;
    opState->phase = AFATFS_TRUNCATE_FILE_INITIAL;
    opState->startCluster = file->firstCluster;
    opState->currentCluster = opState->startCluster;

#ifdef AFATFS_USE_FREEFILE
    if ((file->mode & AFATFS_FILE_MODE_CONTIGUOUS) != 0) {
        // The file is contiguous and ends where the freefile begins
        opState->endCluster = afatfs.freeFile.firstCluster;
    } else
#endif
    {
        // The range of clusters to delete is not contiguous, so follow it as a linked-list instead
        opState->endCluster = 0;
    }

    // We'll drop the cluster chain from the directory entry immediately
    file->firstCluster = 0;
    file->logicalSize = 0;
    file->physicalSize = 0;

    afatfs_fseek(file, 0, AFATFS_SEEK_SET);

    return true;
}

bool afatfs_ftruncate(afatfsFilePtr_t file, afatfsFileCallback_t callback)
{
    /*
     * Public compatibility wrapper for standalone truncation.
     * Inputs/outputs retain the original API; no parent lifetime or structured
     * open callback is transferred. Affiliates: unlink, removeObjects, and the
     * internal write-open handoff.
     */
    return afatfs_ftruncateInternal(file, callback, NULL, NULL);
}

/**
 * Load details from the given FAT directory entry into the file object.
 *
 * Inputs: file is the asyncfatfs handle being opened; entry is the FAT
 * directory entry found by afatfs_createFileContinue() or directory traversal.
 * Outputs: cluster, logical/physical size, attributes, and file->type are
 * populated from the entry. file->type must be set here because later clients
 * such as afatfs_chdir(), directory scans, and the Phase 2 Kit/ loader need to
 * know whether an opened handle represents a directory or a normal file. Before
 * this assignment, opened directories kept the caller-initialized normal-file
 * type and could not reliably be used as directories.
 *
 * Affiliates/clients: afatfs_createFileContinue() calls this after a matching
 * entry is found; afatfs_chdir() and filesystem.c depend on the resulting type.
 */
static void afatfs_fileLoadDirectoryEntry(afatfsFile_t *file, fatDirectoryEntry_t *entry)
{
    file->firstCluster = (uint32_t) (entry->firstClusterHigh << 16) | entry->firstClusterLow;
    file->logicalSize = entry->fileSize;
    file->attrib = entry->attrib;
    file->type = (entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY)
        ? AFATFS_FILE_TYPE_DIRECTORY
        : AFATFS_FILE_TYPE_NORMAL;
    /*
     * Reconstruct allocated size for opened subdirectories.
     *
     * What: FAT directory entries store fileSize as zero for directories, even
     * when firstCluster points at one or more allocated directory clusters.
     * Normal files can derive physicalSize from fileSize, but directories need
     * at least one cluster of allocated-size metadata as soon as firstCluster is
     * nonzero.
     *
     * Why: Kit Save opens `/Kit`, chdir() copies that handle into
     * currentDirectory, and then mkdir_lfn() creates the selected `NNN Name`
     * child inside it. If the opened `/Kit` handle reports physicalSize == 0,
     * directory scans immediately behave as though the allocated cluster is
     * absent. Save:[Dir] does not expose this because it creates directly in
     * root before scanning an existing subdirectory.
     *
     * Inputs: entry is the SFN record for an existing file or directory.
     * Outputs/effects: file->physicalSize is the rounded file size for normal
     * files, or one cluster for an existing subdirectory with a first cluster.
     * Empty/new directory creation still starts from firstCluster == 0 and is
     * initialized by afatfs_extendSubdirectory().
     *
     * Affiliates/clients: afatfs_chdir(), afatfs_findNext(), and
     * afatfs_createFileContinue().
     */
    if (file->type == AFATFS_FILE_TYPE_DIRECTORY && file->firstCluster != 0u) {
        file->physicalSize = afatfs_clusterSize();
    } else {
        file->physicalSize = roundUpTo(entry->fileSize, afatfs_clusterSize());
    }
}

static bool afatfs_isLfnDirectoryEntry(const fatDirectoryEntry_t *entry)
{
    /*
     * Recognize FAT long-filename fragments through the shared FAT helper.
     *
     * Keeping this wrapper avoids churn in the create/open state machine while
     * moving the actual on-disk rule into fat_standard.c, where the object
     * iterator and future delete/rename code use the same classification.
     */
    return fat_isLongDirectoryEntry(entry);
}

static uint8_t afatfs_lfnChecksum(const uint8_t fatFilename[FAT_FILENAME_LENGTH])
{
    /*
     * Compatibility wrapper around the shared VFAT checksum helper.
     */
    return fat_lfnChecksum(fatFilename);
}

static char afatfs_lfnSanitizeChar(char c)
{
    /*
     * Compatibility wrapper around the shared LFN sanitizer.
     */
    return fat_lfnSanitizeChar(c);
}

static char afatfs_sfnChar(char c)
{
    /*
     * Convert one display-name byte into a legal SFN alias byte.
     *
     * Spaces and punctuation are skipped by the alias builder. Alphabetic case
     * is preserved in the printable alias so fat_calculateFilenameCaseFlags()
     * can later decide whether the on-card SFN should display as lowercase.
     * fat_convertFilenameToFATStyle() still uppercases the raw FAT key.
     */
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9'))
        return c;
    if (c == '_')
        return c;
    return '\0';
}

static void afatfs_copyShortAliasText(
        const uint8_t fatFilename[FAT_FILENAME_LENGTH],
        uint8_t ntReserved,
        char out[AFATFS_SHORT_FILENAME_MAX])
{
    /*
     * Return the printable alias exactly as FAT readers should display it.
     *
     * The raw SFN is uppercase, but ntReserved may mark the base and/or
     * extension as lowercase. Callers store this string in kitset.kcg and later
     * pass it back to afatfs_fopen(), whose lookup remains case-insensitive.
     */
    fat_convertFATStyleToFilename((const char *)fatFilename, out);
    fat_applyFilenameCaseFlags(out, ntReserved);
}

static uint8_t afatfs_copySanitizedLongName(char *dst, const char *src)
{
    uint8_t len = 0u;

    /*
     * Copy one path component into bounded create-state storage.
     *
     * asyncfatfs still does not accept slash-separated paths. The long-name
     * API is a component create/open primitive in the current working
     * directory, mirroring afatfs_fopen()/afatfs_mkdir().
     *
     * First, characters outside the firmware's filesystem-safe input set are
     * converted to '_'. Then trailing spaces and periods are stripped at this
     * boundary for every LFN file and directory operation. FAT/VFAT can encode
     * them, but Mac/Windows filesystems do not treat those names as normal
     * user-visible objects, and the firmware UI has no way to distinguish or
     * delete trailing padding.
     */
    if (!dst || !src)
        return 0u;
    while (*src != '\0' && len < AFATFS_LONG_FILENAME_MAX) {
        dst[len++] = afatfs_lfnSanitizeChar(*src++);
    }
    while (len > 0u && (dst[len - 1u] == ' ' || dst[len - 1u] == '.'))
        len--;
    dst[len] = '\0';
    return len;
}

static bool afatfs_generateShortAlias(afatfsCreateFile_t *opState)
{
    char base[9];
    char ext[4];
    char alias[AFATFS_SHORT_FILENAME_MAX];
    const char *name = opState->longName;
    const char *dot = NULL;
    uint8_t baseLen = 0u;
    uint8_t extLen = 0u;
    uint8_t aliasLen = 0u;
    uint16_t ordinal = opState->aliasOrdinal;

    /*
     * Generate one deterministic 8.3 alias candidate for an LFN component.
     *
     * Ordinal zero tries the most readable alias first. Later ordinals use the
     * familiar "~N" suffix so colliding long names can still live in the same
     * directory. The caller scans for collisions and bumps aliasOrdinal until a
     * matching existing entry or free alias is found.
     */
    memset(base, 0, sizeof(base));
    memset(ext, 0, sizeof(ext));
    memset(alias, 0, sizeof(alias));

    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '.')
            dot = p;
    }

    for (const char *p = name; *p != '\0' && p != dot; p++) {
        char c = afatfs_sfnChar(*p);
        if (c != '\0' && baseLen < sizeof(base) - 1u)
            base[baseLen++] = c;
    }
    if (baseLen == 0u) {
        base[baseLen++] = 'F';
        base[baseLen++] = 'I';
        base[baseLen++] = 'L';
        base[baseLen++] = 'E';
    }

    if (dot && dot[1] != '\0') {
        for (const char *p = dot + 1; *p != '\0' && extLen < 3u; p++) {
            char c = afatfs_sfnChar(*p);
            if (c != '\0')
                ext[extLen++] = c;
        }
    }

    if (ordinal > 0u) {
        char suffix[6];
        uint8_t suffixLen = 0u;
        uint16_t value = ordinal;
        char digits[5];
        uint8_t digitCount = 0u;
        uint8_t baseLimit;

        if (ordinal > 9999u)
            return false;
        do {
            digits[digitCount++] = (char)('0' + (value % 10u));
            value = (uint16_t)(value / 10u);
        } while (value != 0u && digitCount < sizeof(digits));
        suffix[suffixLen++] = '~';
        while (digitCount > 0u)
            suffix[suffixLen++] = digits[--digitCount];
        suffix[suffixLen] = '\0';

        baseLimit = (uint8_t)(8u - suffixLen);
        if (baseLen > baseLimit)
            baseLen = baseLimit;
        memcpy(alias, base, baseLen);
        aliasLen = baseLen;
        memcpy(alias + aliasLen, suffix, suffixLen);
        aliasLen = (uint8_t)(aliasLen + suffixLen);
    } else {
        if (baseLen > 8u)
            baseLen = 8u;
        memcpy(alias, base, baseLen);
        aliasLen = baseLen;
    }

    if (extLen > 0u) {
        alias[aliasLen++] = '.';
        memcpy(alias + aliasLen, ext, extLen);
        aliasLen = (uint8_t)(aliasLen + extLen);
    }
    alias[aliasLen] = '\0';

    /*
     * The generated SFN alias is still the physical lookup key for an LFN
     * object. Store lowercase-display flags for that alias too, so kitset.kcg
     * can receive the same case-preserved open name that host tools display for
     * the final SFN entry. The LFN entry remains the authoritative mixed-case
     * display name.
     */
    opState->shortNameCaseFlags = fat_calculateFilenameCaseFlags(alias);
    fat_convertFilenameToFATStyle(alias, opState->filename);
    if (opState->openNameOut)
        afatfs_copyShortAliasText(opState->filename,
                                  opState->shortNameCaseFlags,
                                  opState->openNameOut);
    return true;
}

static void afatfs_lfnScanReset(afatfsCreateFile_t *opState)
{
    /*
     * Clear the in-progress LFN scanner for create/open matching.
     *
     * The create state machine walks ordinary directory entries before it
     * decides whether to open, collide, or create. This scanner mirrors the
     * higher-level directory browsers so a matching SFN alias is only reused
     * when the preceding LFN chain matches the requested display name.
     * scanLongNameChecksum stores the VFAT checksum from the accumulated LFN
     * fragments. Display-name matching may open an object whose SFN alias
     * differs from our generated candidate, so the following SFN entry must
     * prove it owns the accumulated LFN chain before strcmp(scanLongName,
     * longName) is trusted.
     */
    memset(opState->scanLongName, 0, sizeof(opState->scanLongName));
    opState->scanLongNameValid = 0u;
    opState->scanLongNameChecksum = 0u;
}

static void afatfs_lfnScanAppend(afatfsCreateFile_t *opState,
                                 const fatDirectoryEntry_t *entry)
{
    static const uint8_t offsets[13] = {
        1u, 3u, 5u, 7u, 9u,
        14u, 16u, 18u, 20u, 22u, 24u,
        28u, 30u
    };
    const uint8_t *raw = (const uint8_t *)entry;
    uint8_t seq = raw[0] & 0x1fu;

    /*
     * Reconstruct one ASCII-safe LFN while scanning for existing aliases.
     *
     * Only the low ASCII subset is supported for firmware-created names today.
     * Non-ASCII entries are treated as mismatches, which prevents overwriting a
     * host-created file whose alias happens to collide with our generated SFN.
     * VFAT stores the last text fragment first and identifies it with bit 0x40.
     * The low five bits are a one-based ordinal, so (seq - 1) * 13 maps each
     * fragment back to its absolute character offset in the reconstructed
     * display component. Every fragment also carries the checksum of the
     * following SFN entry; keep only chains with a consistent checksum so the
     * normal-entry branch can prove the display name belongs to that SFN.
     */
    if (seq == 0u || seq > ((AFATFS_LONG_FILENAME_MAX + 12u) / 13u)) {
        afatfs_lfnScanReset(opState);
        return;
    }
    if (raw[0] & 0x40u) {
        memset(opState->scanLongName, 0, sizeof(opState->scanLongName));
        opState->scanLongNameValid = 1u;
        opState->scanLongNameChecksum = raw[13];
    } else if (!opState->scanLongNameValid) {
        return;
    } else if (opState->scanLongNameChecksum != raw[13]) {
        afatfs_lfnScanReset(opState);
        return;
    }

    uint8_t pos = (uint8_t)((seq - 1u) * 13u);
    for (uint8_t i = 0u; i < 13u && pos < AFATFS_LONG_FILENAME_MAX; i++, pos++) {
        uint16_t ch = (uint16_t)raw[offsets[i]] |
                      ((uint16_t)raw[offsets[i] + 1u] << 8);
        if (ch == 0x0000u)
            break;
        if (ch == 0xffffu)
            continue;
        if (ch >= 0x80u) {
            opState->scanLongNameValid = 0u;
            return;
        }
        opState->scanLongName[pos] = (char)ch;
    }
}

static void afatfs_noteFreeDirectoryEntry(afatfsCreateFile_t *opState,
                                          const afatfsFinder_t *finder)
{
    uint8_t needed = (uint8_t)(opState->lfnEntryCount + 1u);

    /*
     * Track a contiguous free run in one directory sector.
     *
     * VFAT LFN entries must immediately precede their owning SFN entry. By
     * constraining the first implementation to a sector-local run, the writer
     * can later cache one sector and write the whole chain atomically relative
     * to asyncfatfs cache ownership, without assuming directory clusters are
     * physically contiguous.
     */
    if (finder->entryIndex < 0)
        return;
    if (opState->freeRunLength == 0u ||
        finder->sectorNumberPhysical != opState->freeRunStart.sectorNumberPhysical ||
        finder->entryIndex == 0) {
        opState->freeRunStart = *finder;
        opState->freeRunLength = 1u;
    } else {
        opState->freeRunLength++;
    }
    if (opState->freeRunLength > AFATFS_FILES_PER_DIRECTORY_SECTOR)
        opState->freeRunLength = AFATFS_FILES_PER_DIRECTORY_SECTOR;
    (void)needed;
}

static bool afatfs_freeRunIsReady(const afatfsCreateFile_t *opState)
{
    /*
     * Report whether the current scan has found enough adjacent slots for the
     * requested LFN fragment count plus the final SFN entry.
     */
    return opState->freeRunLength >= (uint8_t)(opState->lfnEntryCount + 1u);
}

static void afatfs_retireDirectoryTerminator(fatDirectoryEntry_t *entry)
{
    /*
     * Convert a FAT directory terminator into an ordinary deleted/free entry.
     *
     * LFN creation may need a sector-local run of two or more entries. If the
     * first terminator is too close to the end of a sector, the writer has to
     * place the new LFN/SFN run later. A 0x00 terminator left before that run
     * would make normal directory enumeration stop before the new object, so
     * retire the skipped terminator before scanning onward.
     */
    entry->filename[0] = FAT_DELETED_FILE_MARKER;
    afatfs_cacheSectorMarkDirty(
        afatfs_getCacheDescriptorForBuffer((uint8_t *)entry));
}

static void afatfs_writeLfnChar(uint8_t *raw, uint8_t offset, uint16_t value)
{
    raw[offset] = (uint8_t)(value & 0xffu);
    raw[offset + 1u] = (uint8_t)(value >> 8);
}

static void afatfs_writeLfnDirectoryEntry(fatDirectoryEntry_t *entry,
                                          const afatfsCreateFile_t *opState,
                                          uint8_t lfnIndexFromStart,
                                          uint8_t checksum)
{
    static const uint8_t offsets[13] = {
        1u, 3u, 5u, 7u, 9u,
        14u, 16u, 18u, 20u, 22u, 24u,
        28u, 30u
    };
    uint8_t ordinal = (uint8_t)(opState->lfnEntryCount - lfnIndexFromStart);
    uint8_t nameOffset = (uint8_t)((ordinal - 1u) * 13u);
    uint8_t wroteTerminator = 0u;
    uint8_t *raw = (uint8_t *)entry;

    /*
     * Emit one VFAT LFN fragment.
     *
     * Directory order is highest ordinal first, then descending fragments, then
     * the SFN entry. The scanner reconstructs text by using ordinal to place
     * each 13-character fragment back into the display-name buffer.
     */
    memset(entry, 0, sizeof(*entry));
    raw[0] = ordinal;
    if (lfnIndexFromStart == 0u)
        raw[0] |= 0x40u;
    entry->attrib = 0x0f;
    raw[12] = 0u;
    raw[13] = checksum;
    entry->firstClusterLow = 0u;

    for (uint8_t i = 0u; i < 13u; i++) {
        uint16_t value;
        char c = opState->longName[nameOffset + i];
        if (!wroteTerminator && c != '\0') {
            value = (uint16_t)(uint8_t)c;
        } else if (!wroteTerminator) {
            value = 0x0000u;
            wroteTerminator = 1u;
        } else {
            value = 0xffffu;
        }
        afatfs_writeLfnChar(raw, offsets[i], value);
    }
}

static afatfsOperationStatus_e afatfs_createLongDirectoryEntries(
    afatfsFile_t *file)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    uint8_t *sector;
    afatfsOperationStatus_e status;
    uint8_t checksum = afatfs_lfnChecksum(opState->filename);
    fatDirectoryEntry_t *entries;
    uint8_t sfnIndex;

    /*
     * Write the reserved LFN run and final SFN entry into one cached sector.
     *
     * The SFN entry is written last because it is the authoritative entry that
     * ordinary FAT readers open. If power is lost before that point, the card
     * may contain orphan LFN fragments but not a complete file/directory entry.
     */
    status = afatfs_cacheSector(opState->freeRunStart.sectorNumberPhysical,
                                &sector,
                                AFATFS_CACHE_READ | AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entries = (fatDirectoryEntry_t *)sector + opState->freeRunStart.entryIndex;
    for (uint8_t i = 0u; i < opState->lfnEntryCount; i++)
        afatfs_writeLfnDirectoryEntry(&entries[i], opState, i, checksum);

    sfnIndex = opState->lfnEntryCount;
    memset(&entries[sfnIndex], 0, sizeof(entries[sfnIndex]));
    memcpy(entries[sfnIndex].filename, opState->filename, FAT_FILENAME_LENGTH);
    entries[sfnIndex].attrib = file->attrib;
    /*
     * The LFN fragments preserve the full display name. The final SFN entry
     * still needs coherent ntReserved case bits because higher layers cache and
     * write the returned alias into files such as kitset.kcg.
     */
    entries[sfnIndex].ntReserved = opState->shortNameCaseFlags;
    entries[sfnIndex].creationDate = AFATFS_DEFAULT_FILE_DATE;
    entries[sfnIndex].creationTime = AFATFS_DEFAULT_FILE_TIME;
    entries[sfnIndex].lastWriteDate = AFATFS_DEFAULT_FILE_DATE;
    entries[sfnIndex].lastWriteTime = AFATFS_DEFAULT_FILE_TIME;

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    file->directoryEntryPos = opState->freeRunStart;
    file->directoryEntryPos.entryIndex =
        (int16_t)(file->directoryEntryPos.entryIndex + opState->lfnEntryCount);
    return AFATFS_OPERATION_SUCCESS;
}

static void afatfs_handoffCreatedDirectoryToInitializer(
    afatfsFile_t *file,
    afatfsFileCallback_t callback,
    afatfsOpenResultCallback_t resultCallback,
    afatfsFilePtr_t retainedParent)
{
    /*
     * Newly-created directories cannot complete through the ordinary
     * empty-file success path.
     *
     * CREATE_FILE is still active when the parent directory entry has just
     * been written, so afatfs_extendSubdirectory() would reject the handle as
     * busy and its operation union would overwrite createFile state. The
     * handoff is deliberate: preserve the original callback, clear
     * CREATE_FILE, then let EXTEND_SUBDIRECTORY own the handle until it has
     * allocated the first cluster and initialized "." / "..".
     *
     * Inputs: file is the newly-created directory entry; callback/resultCallback
     * are mutually exclusive legacy/structured completions; retainedParent is
     * both the source for `..` and the parent lifetime lock. Output: the chosen
     * callback is invoked by EXTEND_SUBDIRECTORY with OK/file or an error/NULL.
     * Affiliates: afatfs_createFileContinue(), afatfs_extendSubdirectory(),
     * afatfs_appendRegularFreeClusterContinue(), and afatfs_saveDirectoryEntry().
     */
    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
    (void)afatfs_extendSubdirectory(file, retainedParent, callback,
                                    resultCallback, retainedParent, 1u);
}

static void afatfs_createFileFinish(afatfsFile_t *file,
                                    afatfsResultCode_t result)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    afatfsFileCallback_t callback = opState->callback;
    afatfsOpenResultCallback_t resultCallback = opState->resultCallback;
    afatfsFilePtr_t parent = opState->parent;
    uint8_t parentRetained = opState->parentRetained;

    /*
     * Terminate one create/open through a single ownership boundary.
     *
     * What: copies callbacks and parent state before clearing the operation
     * union, releases the parent lock, destroys a failed child handle, and then
     * publishes exactly one legacy or structured callback. Why: scattered
     * success/failure exits previously changed only type/operation, which made
     * cache/handle lifetime and error classification timing-dependent.
     * Inputs: file owns CREATE_FILE and result is its precise terminal status.
     * Outputs: OK leaves file open; every error returns its slot to openFiles[].
     * Affiliates: create policy resolution, child APIs, append/truncate
     * handoffs, and the accepted-operation callback invariant.
     */
    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
    if (parentRetained)
        afatfs_releaseChildParent(parent);
    if (result != AFATFS_RESULT_OK) {
        afatfs_fileUnlockCacheSector(file);
        afatfs_initFileHandle(file);
    }
    if (resultCallback)
        resultCallback(result,
                       result == AFATFS_RESULT_OK ? file : NULL);
    else if (callback)
        callback(result == AFATFS_RESULT_OK ? file : NULL);
}

static void afatfs_createFileContinue(afatfsFile_t *file)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    fatDirectoryEntry_t *entry;
    afatfsOperationStatus_e status;

    doMore:

    switch (opState->phase) {
        case AFATFS_CREATEFILE_PHASE_INITIAL:
            /*
             * Bind every cursor mutation to the retained caller-selected
             * parent. Legacy wrappers store currentDirectory here, while child
             * APIs store an independently open handle. No later phase may
             * consult the global CWD, or concurrent source/destination trees
             * would resolve in whichever directory was selected most recently.
             */
            afatfs_findFirst(opState->parent, &file->directoryEntryPos);
            opState->freeRunLength = 0u;
            afatfs_lfnScanReset(opState);
            opState->phase = AFATFS_CREATEFILE_PHASE_FIND_FILE;
            goto doMore;
        break;
        case AFATFS_CREATEFILE_PHASE_FIND_FILE:
            do {
                status = afatfs_findNext(opState->parent,
                                         &file->directoryEntryPos,
                                         &entry);

                switch (status) {
                    case AFATFS_OPERATION_SUCCESS:
                        /*
                         * Directory exhaustion and terminators are where the
                         * create path diverges. Short-name creation can use
                         * the old one-entry allocator. LFN creation must keep
                         * looking until it has a sector-local run large enough
                         * for every LFN fragment plus the final SFN entry.
                         */
                        if (entry == NULL) {
                            afatfs_findLast(opState->parent);

                            if ((file->mode & AFATFS_FILE_MODE_CREATE) != 0) {
                                if (opState->longNameEnabled) {
                                    if (afatfs_freeRunIsReady(opState)) {
                                        opState->phase =
                                            AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE;
                                        goto doMore;
                                    }
                                    status = afatfs_extendSubdirectory(
                                        opState->parent, NULL, NULL,
                                        NULL, NULL, 0u);
                                    if (status == AFATFS_OPERATION_SUCCESS) {
                                        /*
                                         * extendSubdirectory() leaves the
                                         * current directory cursor at the
                                         * start of the newly-added cluster.
                                         * The raw finder still carries the
                                         * index from the exhausted sector, so
                                         * reset just the entry index here to
                                         * scan the fresh cluster from entry 0
                                         * instead of skipping its first sector.
                                         */
                                        file->directoryEntryPos.entryIndex = -1;
                                        opState->freeRunLength = 0u;
                                        opState->phase = AFATFS_CREATEFILE_PHASE_FIND_FILE;
                                        break;
                                    }
                                    if (status == AFATFS_OPERATION_IN_PROGRESS)
                                        break;
                                    opState->terminalResult = afatfs.filesystemFull
                                        ? AFATFS_RESULT_NO_SPACE
                                        : AFATFS_RESULT_IO_ERROR;
                                    opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                    goto doMore;
                                }
                                // The file didn't already exist, so we can create it. Allocate a new directory entry
                                afatfs_findFirst(opState->parent,
                                                 &file->directoryEntryPos);

                                opState->phase = AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE;
                                goto doMore;
                            } else {
                                /* OPEN_EXISTING exhausted the parent without a match. */
                                opState->terminalResult = AFATFS_RESULT_NOT_FOUND;
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                        } else if (!opState->longNameEnabled &&
                                   fat_isDirectoryEntryTerminator(entry)) {
                            afatfs_findLast(opState->parent);

                            if ((file->mode & AFATFS_FILE_MODE_CREATE) != 0) {
                                afatfs_findFirst(opState->parent,
                                                 &file->directoryEntryPos);
                                opState->phase =
                                    AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE;
                                goto doMore;
                            }
                            opState->terminalResult = AFATFS_RESULT_NOT_FOUND;
                            opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                            goto doMore;
                        } else if (opState->longNameEnabled &&
                                   (fat_isDirectoryEntryEmpty(entry) ||
                                    fat_isDirectoryEntryTerminator(entry))) {
                            uint8_t wasTerminator =
                                fat_isDirectoryEntryTerminator(entry) ? 1u : 0u;
                            afatfs_noteFreeDirectoryEntry(opState,
                                                          &file->directoryEntryPos);
                            if ((file->mode & AFATFS_FILE_MODE_CREATE) != 0 &&
                                afatfs_freeRunIsReady(opState)) {
                                afatfs_findLast(opState->parent);
                                opState->phase =
                                    AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE;
                                goto doMore;
                            }
                            if ((file->mode & AFATFS_FILE_MODE_CREATE) != 0 &&
                                wasTerminator) {
                                afatfs_retireDirectoryTerminator(entry);
                            }
                            afatfs_lfnScanReset(opState);
                        } else if (afatfs_isLfnDirectoryEntry(entry)) {
                            opState->freeRunLength = 0u;
                            if (opState->longNameEnabled)
                                afatfs_lfnScanAppend(opState, entry);
                            else
                                afatfs_lfnScanReset(opState);
                        } else if (opState->longNameEnabled) {
                            uint8_t displayMatches = 0u;
                            char shortDisplay[AFATFS_SHORT_FILENAME_MAX];

                            /*
                             * Match LFN requests against the same display
                             * component returned by the object iterator.
                             *
                             * A FAT object may have a checksum-valid VFAT
                             * long-name chain, or it may be a plain short-name
                             * entry whose display spelling comes from
                             * filename[] plus ntReserved case bits.
                             * Load:[File] and Load:[Dir] browse both forms
                             * through afatfs_findNextObject(), so open/create
                             * must resolve the same display string here.
                             * Without the SFN-display fallback, existing card
                             * files such as settings.cfg, P000.ALL, samples/,
                             * and Instrument directory files list correctly
                             * but fail when OK is selected because the
                             * generated alias is mistaken for an unrelated
                             * collision.
                             */
                            if (opState->scanLongNameValid &&
                                opState->scanLongNameChecksum ==
                                    afatfs_lfnChecksum((const uint8_t *)entry->filename)) {
                                if (fat_compareDisplayName(
                                        opState->scanLongName,
                                        opState->longName,
                                        opState->matchMode ==
                                            AFATFS_MATCH_CASE_SENSITIVE) == 0) {
                                    displayMatches = 1u;
                                }
                                /*
                                 * Preserve the one-object browser contract
                                 * when a caller reopens that object by its
                                 * cached SFN alias.  The iterator exposes both
                                 * the validated LFN display and its physical
                                 * short alias; matching only the LFN would
                                 * make a selected Kit or Instrument directory
                                 * appear in the list but fail on OK. Inputs
                                 * are the current FAT entry and requested
                                 * alias; output is the same displayMatches
                                 * decision used by the typed child open.
                                 * Affiliates: findNextObject(),
                                 * openDirChild(), and filesystem Kit Load.
                                 */
                                if (!displayMatches) {
                                    afatfs_copyShortAliasText(
                                        (const uint8_t *)entry->filename,
                                        entry->ntReserved,
                                        shortDisplay);
                                    if (fat_compareDisplayName(
                                            shortDisplay,
                                            opState->longName,
                                            opState->matchMode ==
                                                AFATFS_MATCH_CASE_SENSITIVE) == 0) {
                                        displayMatches = 1u;
                                    }
                                }
                            } else {
                                afatfs_copyShortAliasText(
                                    (const uint8_t *)entry->filename,
                                    entry->ntReserved,
                                    shortDisplay);
                                if (fat_compareDisplayName(
                                        shortDisplay,
                                        opState->longName,
                                        opState->matchMode ==
                                            AFATFS_MATCH_CASE_SENSITIVE) == 0) {
                                    displayMatches = 1u;
                                }
                            }
                            if (displayMatches) {
                                uint8_t requestedDirectory =
                                    (file->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0u;
                                uint8_t existingDirectory =
                                    (entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0u;

                                /*
                                 * Enforce create policy before opening a typed
                                 * display match.
                                 *
                                 * CREATE_NEW treats every same-display object
                                 * as ALREADY_EXISTS, even when its kind differs.
                                 * Open policies then distinguish file/directory
                                 * collisions so callers can diagnose the card
                                 * instead of silently creating a duplicate.
                                 * Inputs are the requested attributes and the
                                 * scanned SFN; output is either a precise result
                                 * or the normal open path. Affiliates: child API
                                 * policy and legacy CREATE_OR_OPEN wrappers.
                                 */
                                if (opState->createMode == AFATFS_CREATE_NEW) {
                                    afatfs_findLast(opState->parent);
                                    opState->terminalResult =
                                        AFATFS_RESULT_ALREADY_EXISTS;
                                    opState->phase =
                                        AFATFS_CREATEFILE_PHASE_FAILURE;
                                    goto doMore;
                                }
                                if (requestedDirectory != existingDirectory) {
                                    afatfs_findLast(opState->parent);
                                    opState->terminalResult = requestedDirectory
                                        ? AFATFS_RESULT_NOT_DIRECTORY
                                        : AFATFS_RESULT_NOT_FILE;
                                    opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                    goto doMore;
                                }
                                // We found a file with this name!
                                afatfs_fileLoadDirectoryEntry(file, entry);
                                if (opState->openNameOut)
                                    afatfs_copyShortAliasText(
                                        (const uint8_t *)entry->filename,
                                        entry->ntReserved,
                                        opState->openNameOut);

                                afatfs_findLast(opState->parent);

                                opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                                goto doMore;
                            }
                            if (strncmp(entry->filename,
                                        (char*) opState->filename,
                                        FAT_FILENAME_LENGTH) != 0) {
                                opState->freeRunLength = 0u;
                                afatfs_lfnScanReset(opState);
                                break;
                            }
                            /*
                             * Alias collision is only useful when creation is
                             * allowed.
                             *
                             * Read-only LFN opens cannot resolve by inventing
                             * a new "~N" alias; if the display component did
                             * not match this entry under the caller's display
                             * policy, the colliding SFN proves that the
                             * requested object is absent for this open.
                             * Create/write modes still generate the next
                             * "~N" candidate and restart the scan so duplicate
                             * display names or same-folded SFN names do not
                             * reuse the wrong physical entry. Product overwrite
                             * removes same-casefold physical duplicates before
                             * it reaches this create path, so a new object
                             * created here is the single surviving visible
                             * variant.
                             */
                            if ((file->mode & AFATFS_FILE_MODE_CREATE) == 0u) {
                                afatfs_findLast(opState->parent);
                                opState->terminalResult = AFATFS_RESULT_NOT_FOUND;
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                            opState->aliasOrdinal++;
                            if (!afatfs_generateShortAlias(opState)) {
                                afatfs_findLast(opState->parent);
                                opState->terminalResult =
                                    AFATFS_RESULT_ALREADY_EXISTS;
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                            afatfs_findFirst(opState->parent,
                                             &file->directoryEntryPos);
                            opState->freeRunLength = 0u;
                            afatfs_lfnScanReset(opState);
                            break;
                        } else if (strncmp(entry->filename, (char*) opState->filename, FAT_FILENAME_LENGTH) == 0) {
                            uint8_t requestedDirectory =
                                (file->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0u;
                            uint8_t existingDirectory =
                                (entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0u;

                            /*
                             * Apply explicit create/type policy to raw SFN
                             * matches as well as LFN display matches.
                             *
                             * Without this branch CREATE_NEW could open and
                             * later truncate an existing 8.3 file, while a
                             * typed child request could accept the wrong kind.
                             * Inputs are the requested attributes and matched
                             * entry; output is ALREADY_EXISTS, a typed error,
                             * or the original open path. Affiliates: fopenChild,
                             * createDirChild, and legacy short-name APIs.
                             */
                            if (opState->createMode == AFATFS_CREATE_NEW) {
                                afatfs_findLast(opState->parent);
                                opState->terminalResult =
                                    AFATFS_RESULT_ALREADY_EXISTS;
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                            if (requestedDirectory != existingDirectory) {
                                afatfs_findLast(opState->parent);
                                opState->terminalResult = requestedDirectory
                                    ? AFATFS_RESULT_NOT_DIRECTORY
                                    : AFATFS_RESULT_NOT_FILE;
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                            /*
                             * Existing short-name files opened for write should
                             * also pick up the caller's display-case metadata.
                             * This lets a save over an older KITSET.KCG entry
                             * refresh ntReserved so host tools display
                             * kitset.kcg after the file is truncated/rewritten.
                             * Read-only opens leave metadata untouched.
                             */
                            if ((file->mode & (AFATFS_FILE_MODE_WRITE |
                                               AFATFS_FILE_MODE_APPEND)) != 0u) {
                                entry->ntReserved =
                                    opState->shortNameCaseFlags;
                                afatfs_cacheSectorMarkDirty(
                                    afatfs_getCacheDescriptorForBuffer(
                                        (uint8_t*)entry));
                            }
                            // We found a short-name file with this name!
                            afatfs_fileLoadDirectoryEntry(file, entry);

                            afatfs_findLast(opState->parent);

                            opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                            goto doMore;
                        } else {
                            opState->freeRunLength = 0u;
                            afatfs_lfnScanReset(opState);
                        } // Else this entry doesn't match, fall through and continue the search
                    break;
                    case AFATFS_OPERATION_FAILURE:
                        afatfs_findLast(opState->parent);
                        opState->terminalResult = AFATFS_RESULT_IO_ERROR;
                        opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                        goto doMore;
                    break;
                    case AFATFS_OPERATION_IN_PROGRESS:
                        ;
                }
            } while (status == AFATFS_OPERATION_SUCCESS);
        break;
        case AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE:
            status = afatfs_allocateDirectoryEntry(opState->parent,
                                                   &entry,
                                                   &file->directoryEntryPos);

            if (status == AFATFS_OPERATION_SUCCESS) {
                memset(entry, 0, sizeof(*entry));

                memcpy(entry->filename, opState->filename, FAT_FILENAME_LENGTH);
                entry->attrib = file->attrib;
                /*
                 * Preserve the caller's 8.3 display case in ntReserved while
                 * keeping filename[] as the uppercase FAT lookup key. This is
                 * what makes newly-created lowercase system files show as
                 * kitset.kcg instead of KITSET.KCG.
                 */
                entry->ntReserved = opState->shortNameCaseFlags;
                entry->creationDate = AFATFS_DEFAULT_FILE_DATE;
                entry->creationTime = AFATFS_DEFAULT_FILE_TIME;
                entry->lastWriteDate = AFATFS_DEFAULT_FILE_DATE;
                entry->lastWriteTime = AFATFS_DEFAULT_FILE_TIME;

#ifdef AFATFS_DEBUG_VERBOSE
                fprintf(stderr, "Adding directory entry for %.*s to sector %u\n", FAT_FILENAME_LENGTH, opState->filename, file->directoryEntryPos.sectorNumberPhysical);
#endif

                /*
                 * A just-created directory entry still has firstCluster == 0.
                 * Regular files can remain in that state until fwrite(), but
                 * directories must be initialized before the mkdir callback
                 * because callers immediately chdir() and create children.
                 * callback is copied before the handoff because switching the
                 * file operation to EXTEND_SUBDIRECTORY overwrites the
                 * createFile union storage.
                 */
                if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
                    afatfsFileCallback_t callback = opState->callback;
                    afatfsOpenResultCallback_t resultCallback =
                        opState->resultCallback;
                    afatfsFilePtr_t retainedParent = opState->parentRetained
                        ? opState->parent
                        : NULL;
                    /*
                     * Copy every outer completion/lifetime input before
                     * EXTEND_SUBDIRECTORY overwrites createFile union storage.
                     * The extension terminal path owns the one remaining parent
                     * release and callback from this point onward.
                     */
                    afatfs_handoffCreatedDirectoryToInitializer(
                        file, callback, resultCallback, retainedParent);
                    return;
                }
                opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
                opState->terminalResult = afatfs.filesystemFull
                    ? AFATFS_RESULT_NO_SPACE
                    : AFATFS_RESULT_IO_ERROR;
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                goto doMore;
            }
        break;
        case AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE:
            status = afatfs_createLongDirectoryEntries(file);

            if (status == AFATFS_OPERATION_SUCCESS) {
#ifdef AFATFS_DEBUG_VERBOSE
                fprintf(stderr, "Adding LFN directory entry for %s as %.*s to sector %u\n", opState->longName, FAT_FILENAME_LENGTH, opState->filename, file->directoryEntryPos.sectorNumberPhysical);
#endif
                /*
                 * LFN directory creation writes the visible VFAT fragments and
                 * final SFN entry first, then uses the same
                 * directory-initialization handoff as short mkdir(). The LFN
                 * entries are only names; the child directory is not usable
                 * until its first cluster is allocated and recorded in the SFN
                 * entry.
                 */
                if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
                    afatfsFileCallback_t callback = opState->callback;
                    afatfsOpenResultCallback_t resultCallback =
                        opState->resultCallback;
                    afatfsFilePtr_t retainedParent = opState->parentRetained
                        ? opState->parent
                        : NULL;
                    /* Same ownership transfer as the short-directory branch. */
                    afatfs_handoffCreatedDirectoryToInitializer(
                        file, callback, resultCallback, retainedParent);
                    return;
                }
                opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
                opState->terminalResult = AFATFS_RESULT_IO_ERROR;
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                goto doMore;
            }
        break;
        case AFATFS_CREATEFILE_PHASE_SUCCESS:
            if ((file->mode & AFATFS_FILE_MODE_RETAIN_DIRECTORY) != 0) {
                /*
                 * For this high performance file type, we require the directory entry for the file to be retained
                 * in the cache at all times.
                 */
                uint8_t *directorySector;

                status = afatfs_cacheSector(
                    file->directoryEntryPos.sectorNumberPhysical,
                    &directorySector,
                    AFATFS_CACHE_READ | AFATFS_CACHE_RETAIN,
                    0
                );

                if (status != AFATFS_OPERATION_SUCCESS) {
                    // Retry next time
                    break;
                }
            }

            afatfs_fseek(file, 0, AFATFS_SEEK_SET);

            // Is file empty?
            if (file->cursorCluster == 0) {
#ifdef AFATFS_USE_FREEFILE
                if ((file->mode & AFATFS_FILE_MODE_CONTIGUOUS) != 0) {
                    if (afatfs_fileIsBusy(&afatfs.freeFile)) {
                        // Someone else's using the freefile, come back later.
                        break;
                    } else {
                        // Lock the freefile for our exclusive access
                        afatfs.freeFile.operation.operation = AFATFS_FILE_OPERATION_LOCKED;
                    }
                }
#endif
            } else {
                // We can't guarantee that the existing file contents are contiguous
                file->mode &= ~AFATFS_FILE_MODE_CONTIGUOUS;

                // Seek to the end of the file if it is in append mode
                if ((file->mode & AFATFS_FILE_MODE_APPEND) != 0) {
                    afatfsFileCallback_t callback = opState->callback;
                    afatfsOpenResultCallback_t resultCallback =
                        opState->resultCallback;
                    afatfsFilePtr_t retainedParent = opState->parentRetained
                        ? opState->parent
                        : NULL;

                    /*
                     * Transfer append-open completion into SEEK state.
                     * The copied parent remains locked until EOF positioning
                     * completes; afatfs_fseekInternal() releases it before the
                     * callback in both immediate and queued cases.
                     */
                    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
                    (void)afatfs_fseekInternal(file, file->logicalSize,
                                               callback, resultCallback,
                                               retainedParent);
                    break;
                }

                // If we're only writing (not reading) the file must be truncated
                if (opState->truncateOnOpen) {
                    afatfsFileCallback_t callback = opState->callback;
                    afatfsOpenResultCallback_t resultCallback =
                        opState->resultCallback;
                    afatfsFilePtr_t retainedParent = opState->parentRetained
                        ? opState->parent
                        : NULL;

                    /*
                     * Transfer write-open completion into TRUNCATE state.
                     * Access mode controls truncation only after create policy
                     * has resolved an existing object; CREATE_NEW never reaches
                     * this branch for a collision.
                     */
                    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
                    (void)afatfs_ftruncateInternal(file, callback,
                                                  resultCallback,
                                                  retainedParent);
                    break;
                }
            }

            afatfs_createFileFinish(file, AFATFS_RESULT_OK);
        break;
        case AFATFS_CREATEFILE_PHASE_FAILURE:
            afatfs_createFileFinish(file, opState->terminalResult);
        break;
    }
}

/**
 * Reset the in-memory data for the given handle back to the zeroed initial state
 */
static void afatfs_initFileHandle(afatfsFilePtr_t file)
{
    memset(file, 0, sizeof(*file));
    file->writeLockedCacheIndex = -1;
    file->readRetainCacheIndex = -1;
}

static void afatfs_funlinkContinue(afatfsFilePtr_t file)
{
    afatfsUnlinkFile_t *opState = &file->operation.state.unlinkFile;
    afatfsOperationStatus_e status;

    status = afatfs_ftruncateContinue(file, true);

    if (status == AFATFS_OPERATION_SUCCESS) {
        // Once the truncation is completed, we can close the file handle
        file->operation.operation = AFATFS_FILE_OPERATION_NONE;
        afatfs_fclose(file, opState->callback);
    }
}

/**
 * Delete and close the file.
 *
 * Returns true if the operation was successfully queued (callback will be called some time after this routine returns)
 * or false if the file is busy and you should try again later.
 */
bool afatfs_funlink(afatfsFilePtr_t file, afatfsCallback_t callback)
{
    afatfsUnlinkFile_t *opState = &file->operation.state.unlinkFile;

    if (!file || file->type == AFATFS_FILE_TYPE_NONE) {
        return true;
    }

    /*
     * Internally an unlink is implemented by first doing a ftruncate(), marking the directory entry as deleted,
     * then doing a fclose() operation.
     */

    // Start the sub-operation of truncating the file
    if (!afatfs_ftruncate(file, NULL))
        return false;

    /*
     * The unlink operation has its own private callback field so that the truncate suboperation doesn't end up
     * calling back early when it completes:
     */
    opState->callback = callback;

    file->operation.operation = AFATFS_FILE_OPERATION_UNLINK;

    return true;
}

/**
 * Open (or create) a file in the CWD with the given filename.
 *
 * file             - Memory location to store the newly opened file details
 * name             - Filename in "name.ext" format. No path.
 * attrib           - FAT file attributes to give the file (if created)
 * fileMode         - Bitset of AFATFS_FILE_MODE_* constants. Include AFATFS_FILE_MODE_CREATE to create the file if
 *                    it does not exist.
 * callback         - Called when the operation is complete
 */
static afatfsFilePtr_t afatfs_createFileInternal(
        afatfsFilePtr_t parent,
        afatfsFilePtr_t file,
        const char *name,
        uint8_t attrib,
        uint8_t fileMode,
        afatfsCreateMode_t createMode,
        afatfsMatchMode_t matchMode,
        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
        bool createLongName,
        afatfsFileCallback_t callback,
        afatfsOpenResultCallback_t resultCallback)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    uint8_t longNameLength = 0u;

    afatfs_initFileHandle(file);

    // Queue the operation to finish the file creation
    file->operation.operation = AFATFS_FILE_OPERATION_CREATE_FILE;

    /*
     * Capture parent ownership, policy, and both callback forms before any
     * validation can complete synchronously.
     *
     * CREATE_NEW/CREATE_OR_OPEN authorize allocation independently from the
     * access string; OPEN_EXISTING explicitly clears the legacy CREATE bit.
     * The exclusive parent count prevents close/rebind and a second child scan
     * from sharing its mutable cursor. Inputs live in caller memory only for
     * this start call; output ownership is released by the terminal helper.
     */
    opState->parent = parent;
    opState->createMode = createMode;
    opState->terminalResult = AFATFS_RESULT_IO_ERROR;
    opState->callback = callback;
    opState->resultCallback = resultCallback;
    /*
     * Preserve the access-mode truncation decision before OPEN_EXISTING clears
     * the legacy CREATE bit. The arithmetic is a bit-mask equality: plain "w"
     * is WRITE|CREATE, while "w+" also has READ and must retain its established
     * read/write semantics. Output is consumed only after a matching file opens.
     * Affiliates: afatfs_modeFromString() and the TRUNCATE handoff.
     */
    opState->truncateOnOpen =
        fileMode == (AFATFS_FILE_MODE_CREATE | AFATFS_FILE_MODE_WRITE)
            ? 1u
            : 0u;
    if (createMode == AFATFS_OPEN_EXISTING)
        fileMode &= (uint8_t)~AFATFS_FILE_MODE_CREATE;
    else
        fileMode |= AFATFS_FILE_MODE_CREATE;
    file->mode = fileMode;
    opState->matchMode = matchMode;

    if (!parent ||
        (parent->type != AFATFS_FILE_TYPE_DIRECTORY &&
         parent->type != AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) ||
        afatfs_fileIsBusy(parent) ||
        parent->childOperationRetainCount != 0u) {
        opState->terminalResult = AFATFS_RESULT_NOT_DIRECTORY;
        opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
        afatfs_createFileContinue(file);
        return file;
    }
    parent->childOperationRetainCount = 1u;
    opState->parentRetained = 1u;

    if (strcmp(name, ".") == 0) {
        /* Legacy "." opens clone the selected parent, never the global CWD. */
        file->firstCluster = parent->firstCluster;
        file->physicalSize = parent->physicalSize;
        file->logicalSize = parent->logicalSize;
        file->attrib = parent->attrib;
        file->type = parent->type;
    } else {
        /*
         * Select the physical short alias before the async create scan starts.
         *
         * Legacy callers pass createLongName=false and retain the exact old
         * behavior: one input component is converted directly to an uppercase
         * 8.3 FAT name while retaining FAT display-case bits. LFN callers keep
         * a sanitized display component in opState->longName and generate an
         * SFN alias candidate that the scan can collision-test and return
         * through openNameOut.
         */
        if (createLongName) {
            longNameLength = afatfs_copySanitizedLongName(opState->longName,
                                                         name);
            if (longNameLength == 0u) {
                opState->terminalResult = AFATFS_RESULT_INVALID_NAME;
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                afatfs_createFileContinue(file);
                return file;
            }
            opState->longNameEnabled = 1u;
            opState->lfnEntryCount =
                (uint8_t)((longNameLength + 12u) / 13u);
            opState->aliasOrdinal = 0u;
            opState->openNameOut = openNameOut;
            if (!afatfs_generateShortAlias(opState)) {
                opState->terminalResult = AFATFS_RESULT_INVALID_NAME;
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                afatfs_createFileContinue(file);
                return file;
            }
        } else {
            /*
             * Preserve display case for ordinary 8.3 callers before converting
             * the raw FAT key to uppercase. This is what lets system files
             * created through afatfs_fopen appear as kitset.kcg,
             * sceneset.scg, pattern.pat, effects.fx, and settings.cfg while
             * keeping existing case-insensitive open behavior.
             */
            opState->shortNameCaseFlags =
                fat_calculateFilenameCaseFlags(name);
            fat_convertFilenameToFATStyle(name, opState->filename);
        }
        file->attrib = attrib;

        if ((attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0) {
            file->type = AFATFS_FILE_TYPE_DIRECTORY;
        } else {
            file->type = AFATFS_FILE_TYPE_NORMAL;
        }
    }

    if (strcmp(name, ".") == 0) {
        // Since we already have the directory entry details, we can skip straight to the final operations requried
        opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
    } else {
        opState->phase = AFATFS_CREATEFILE_PHASE_INITIAL;
    }

    afatfs_createFileContinue(file);

    return file;
}

static bool afatfs_entryPointerEquals(const afatfsDirEntryPointer_t *a,
                                      const afatfsDirEntryPointer_t *b)
{
    return a->sectorNumberPhysical == b->sectorNumberPhysical &&
           a->entryIndex == b->entryIndex;
}

static bool afatfs_renameObjectRunIsSectorLocal(
        const afatfsObjectInfo_t *object)
{
    /*
     * The current LFN writer reserves one contiguous run inside one sector.
     * Keep rename on the same footing until cross-sector VFAT runs are added.
     */
    if (object->id.sfnEntry.entryIndex < 0)
        return false;
    if (object->id.lfnEntryCount == 0u)
        return true;
    if (object->id.lfnFirstEntry.entryIndex < 0)
        return false;
    return object->id.lfnFirstEntry.sectorNumberPhysical ==
               object->id.sfnEntry.sectorNumberPhysical &&
           object->id.lfnFirstEntry.entryIndex + object->id.lfnEntryCount ==
               object->id.sfnEntry.entryIndex;
}

static afatfsOperationStatus_e afatfs_retireObjectNameRun(
        const afatfsObjectInfo_t *object)
{
    uint8_t *sector;
    afatfsOperationStatus_e status;
    afatfsDirEntryPointer_t runStart;
    uint8_t entryCount;
    fatDirectoryEntry_t *entries;

    /*
     * Retire one object's complete VFAT name entry run.
     *
     * What: Marks the checksum-verified LFN fragments and the owning SFN entry
     * as deleted. It operates only on the directory entries that name the
     * object.
     *
     * Why: Removing or moving a VFAT object must not leave orphan display
     * fragments visible to later scans. afatfs_funlink() currently marks only
     * the SFN entry, which is acceptable for old short-name files but not for
     * case-preserving LFN overwrite.
     *
     * Inputs: afatfsObjectInfo_t from afatfs_findNextObject(). Its
     * lfnFirstEntry, lfnEntryCount, and sfnEntry identify the entry run. The
     * helper requires the current sector-local run shape used by the existing
     * LFN writer.
     *
     * Outputs/effects: directory cache sector is marked dirty after entries are
     * marked deleted. It does not free clusters and does not inspect directory
     * children.
     *
     * Affiliates/clients: afatfs_renameObjectRetireOldRun(),
     * afatfs_removeObjects_lfn(), filesystem.c recursive delete, and save
     * overwrite preflight.
     */
    if (!object || !afatfs_renameObjectRunIsSectorLocal(object))
        return AFATFS_OPERATION_FAILURE;

    entryCount = (uint8_t)(object->id.lfnEntryCount + 1u);
    runStart = (object->id.lfnEntryCount != 0u)
        ? object->id.lfnFirstEntry
        : object->id.sfnEntry;
    if (runStart.entryIndex < 0 ||
        (uint16_t)runStart.entryIndex + entryCount >
            AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        /* Signed cursor indices are validated before unsigned sector arithmetic. */
        return AFATFS_OPERATION_FAILURE;
    }

    status = afatfs_cacheSector(runStart.sectorNumberPhysical,
                                &sector,
                                AFATFS_CACHE_READ | AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entries = (fatDirectoryEntry_t *)sector + runStart.entryIndex;
    for (uint8_t i = 0u; i < entryCount; i++) {
        /*
         * Mark every name entry in the run deleted.
         *
         * This includes all LFN fragments and the owning SFN entry. The loop
         * does not touch clusters; callers that delete file data free the
         * cluster chain before retiring the visible name metadata.
         */
        entries[i].filename[0] = FAT_DELETED_FILE_MARKER;
    }

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    return AFATFS_OPERATION_SUCCESS;
}

static void afatfs_renameObjectSetOldRunStart(afatfsRenameObject_t *op)
{
    op->oldEntryCount = (uint8_t)(op->source.id.lfnEntryCount + 1u);
    if (op->source.id.lfnEntryCount != 0u) {
        op->oldRunStart = op->source.id.lfnFirstEntry;
    } else {
        op->oldRunStart = op->source.id.sfnEntry;
    }
}

static void afatfs_renameObjectCopyOpenName(const char *src, char *dst)
{
    if (!dst)
        return;
    for (uint8_t i = 0u; i < AFATFS_SHORT_FILENAME_MAX; i++) {
        dst[i] = src ? src[i] : '\0';
        if (dst[i] == '\0')
            return;
    }
    dst[AFATFS_SHORT_FILENAME_MAX - 1u] = '\0';
}

static void afatfs_renameObjectFinish(bool success)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;
    afatfsCallback_t callback = op->callback;
    afatfsResultCallback_t resultCallback = op->resultCallback;
    afatfsResultCode_t result = success
        ? AFATFS_RESULT_OK
        : op->terminalResult;
    afatfsDirHandle_t parent = op->parent;
    afatfsDirHandle_t destinationParent = op->destinationParent;
    uint8_t parentRetained = op->parentRetained;
    uint8_t destinationParentRetained = op->destinationParentRetained;

    /*
     * Publish rename completion after releasing its explicit parent.
     * Callback/result and ownership fields are copied before active is cleared;
     * structured and legacy starts share the same teardown. Successful alias
     * output is copied first, then the parent count is released so callback
     * code can immediately close it or start promotion's next rename.
     * Affiliates: renameObjectAt(), renameObject_lfn(), and replace sequencing.
     */
    op->succeeded = success ? 1u : 0u;
    if (success) {
        afatfs_renameObjectCopyOpenName(op->generatedOpenName,
                                        op->openNameOut);
    }
    op->active = 0u;
    if (destinationParentRetained)
        afatfs_releaseChildParent(destinationParent);
    if (parentRetained)
        afatfs_releaseChildParent(parent);
    if (resultCallback)
        resultCallback(result);
    else if (callback)
        callback();
}

static bool afatfs_renameObjectRawEntryMatchesNew(
        afatfsRenameObject_t *op,
        const fatDirectoryEntry_t *entry)
{
    char shortDisplay[AFATFS_SHORT_FILENAME_MAX];

    if (op->newNameState.scanLongNameValid &&
        op->newNameState.scanLongNameChecksum ==
            afatfs_lfnChecksum((const uint8_t *)entry->filename)) {
        return fat_compareDisplayName(
                   op->newNameState.scanLongName,
                   op->newNameState.longName,
                   op->matchMode == AFATFS_MATCH_CASE_SENSITIVE) == 0;
    }

    afatfs_copyShortAliasText((const uint8_t *)entry->filename,
                              entry->ntReserved,
                              shortDisplay);
    return fat_compareDisplayName(
               shortDisplay,
               op->newNameState.longName,
               op->matchMode == AFATFS_MATCH_CASE_SENSITIVE) == 0;
}

static void afatfs_renameObjectRestartCollisionScan(afatfsRenameObject_t *op)
{
    /* Collision scans run exclusively against the retained explicit parent. */
    afatfs_findFirst(op->destinationParent, &op->rawFinder);
    op->newNameState.freeRunLength = 0u;
    afatfs_lfnScanReset(&op->newNameState);
}

static bool afatfs_renameObjectCanRewriteInPlace(
        const afatfsRenameObject_t *op)
{
    uint8_t newEntryCount =
        (uint8_t)(op->newNameState.lfnEntryCount + 1u);

    /* Cross-parent move must allocate a destination run before source retire. */
    if (op->moveAcrossParents || newEntryCount > op->oldEntryCount)
        return false;
    if (!afatfs_renameObjectRunIsSectorLocal(&op->source))
        return false;
    if ((uint16_t)op->source.id.sfnEntry.entryIndex <
        op->newNameState.lfnEntryCount)
        return false;
    return true;
}

static afatfsOperationStatus_e afatfs_renameObjectWriteRun(
        afatfsRenameObject_t *op)
{
    uint8_t *sector;
    afatfsOperationStatus_e status;
    fatDirectoryEntry_t *entries;
    uint8_t newLfnCount = op->newNameState.lfnEntryCount;
    uint8_t newEntryCount = (uint8_t)(newLfnCount + 1u);
    uint8_t checksum = afatfs_lfnChecksum(op->newNameState.filename);

    if (op->newRunStart.entryIndex < 0 ||
        (uint16_t)op->newRunStart.entryIndex + newEntryCount >
            AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        /* The cast is safe after the negative sentinel check above. */
        return AFATFS_OPERATION_FAILURE;
    }

    status = afatfs_cacheSector(op->newRunStart.sectorNumberPhysical,
                                &sector,
                                AFATFS_CACHE_READ | AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entries = (fatDirectoryEntry_t *)sector + op->newRunStart.entryIndex;

    if (!op->movedEntryRun && op->oldEntryCount > newEntryCount) {
        uint8_t staleCount = (uint8_t)(op->oldEntryCount - newEntryCount);
        fatDirectoryEntry_t *stale =
            (fatDirectoryEntry_t *)sector + op->oldRunStart.entryIndex;
        for (uint8_t i = 0u; i < staleCount; i++)
            stale[i].filename[0] = FAT_DELETED_FILE_MARKER;
    }

    for (uint8_t i = 0u; i < newLfnCount; i++)
        afatfs_writeLfnDirectoryEntry(&entries[i],
                                      &op->newNameState,
                                      i,
                                      checksum);

    entries[newLfnCount] = op->sourceEntry;
    memcpy(entries[newLfnCount].filename,
           op->newNameState.filename,
           FAT_FILENAME_LENGTH);
    entries[newLfnCount].ntReserved = op->newNameState.shortNameCaseFlags;

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    return AFATFS_OPERATION_SUCCESS;
}

static afatfsOperationStatus_e afatfs_renameObjectRetireOldRun(
        afatfsRenameObject_t *op)
{
    (void)op->oldRunStart;
    (void)op->oldEntryCount;
    return afatfs_retireObjectNameRun(&op->source);
}

static void afatfs_renameObjectChooseRun(afatfsRenameObject_t *op)
{
    if (afatfs_renameObjectCanRewriteInPlace(op)) {
        op->movedEntryRun = 0u;
        op->newRunStart = op->source.id.sfnEntry;
        op->newRunStart.entryIndex =
            (int16_t)(op->newRunStart.entryIndex -
                      op->newNameState.lfnEntryCount);
    } else {
        op->movedEntryRun = 1u;
        op->newRunStart = op->newNameState.freeRunStart;
    }
}

static void afatfs_renameObjectContinue(void)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;
    afatfsOperationStatus_e status;

    if (!op->active)
        return;

doMore:
    switch (op->phase) {
    case AFATFS_RENAME_OBJECT_PHASE_INITIAL:
        if (afatfs_fileIsBusy(op->destinationParent))
            return;
        if (op->byIdentity) {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_VALIDATE_SOURCE;
            goto doMore;
        }
        afatfs_findFirstObject(op->parent, &op->objectFinder);
        op->phase = AFATFS_RENAME_OBJECT_PHASE_FIND_SOURCE;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_VALIDATE_SOURCE:
    {
        afatfsResultCode_t validationResult;

        /*
         * Validate a direct source before reading or rewriting its metadata.
         * Inputs are the copied capability and retained parent; output either
         * advances with an afatfsObjectInfo_t wrapper or terminates STALE/IO.
         * This bypasses ambiguous display-name resolution entirely.
         */
        status = afatfs_validateObjectId(op->parent,
                                         &op->requestedSource,
                                         &validationResult);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE ||
            validationResult != AFATFS_RESULT_OK) {
            op->terminalResult = status == AFATFS_OPERATION_FAILURE
                ? AFATFS_RESULT_IO_ERROR
                : validationResult;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        memset(&op->source, 0, sizeof(op->source));
        op->source.id = op->requestedSource;
        op->source.hasLongName =
            op->requestedSource.lfnEntryCount != 0u ? 1u : 0u;
        if (!afatfs_renameObjectRunIsSectorLocal(&op->source)) {
            op->terminalResult = AFATFS_RESULT_UNSUPPORTED_LAYOUT;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        afatfs_renameObjectSetOldRunStart(op);
        if (op->moveAcrossParents &&
            op->requestedSource.kind == AFATFS_OBJECT_DIRECTORY) {
            op->ancestryCluster = op->destinationParent->firstCluster;
            op->ancestryDepth = 0u;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_CHECK_DEST_ANCESTRY;
        } else {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_LOAD_SOURCE_ENTRY;
        }
        goto doMore;
    }

    case AFATFS_RENAME_OBJECT_PHASE_CHECK_DEST_ANCESTRY:
    {
        uint8_t *sector;
        fatDirectoryEntry_t *dotDot;
        uint32_t parentCluster;
        uint8_t atFat16Root =
            afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16 &&
            op->ancestryCluster == 0u;
        uint8_t atFat32Root =
            afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT32 &&
            op->ancestryCluster == afatfs.rootDirectoryCluster;

        /*
         * Walk destination ancestry one structural link per poll.
         *
         * What: follows validated `..` records until root and rejects any link
         * that reaches the source directory. Why: placing a directory inside
         * itself/descendant creates an unreachable cycle. Inputs are the copied
         * source cluster and live destination parent; outputs are safe advance,
         * UNSUPPORTED_LAYOUT for descendant/self, DEPTH_LIMIT, CORRUPT_DIRECTORY,
         * or IO_ERROR. Affiliates: directory creation, delete ancestry checks,
         * and AFATFS_TREE_DEPTH_MAX.
         */
        if (op->ancestryCluster == op->source.id.firstCluster) {
            op->terminalResult = AFATFS_RESULT_UNSUPPORTED_LAYOUT;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (atFat16Root || atFat32Root) {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_LOAD_SOURCE_ENTRY;
            goto doMore;
        }
        if (op->ancestryCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
            op->ancestryCluster > afatfs.numClusters + 1u) {
            op->terminalResult = AFATFS_RESULT_CORRUPT_DIRECTORY;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (op->ancestryDepth >= AFATFS_TREE_DEPTH_MAX) {
            op->terminalResult = AFATFS_RESULT_DEPTH_LIMIT;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }

        status = afatfs_cacheSector(
            afatfs_fileClusterToPhysical(op->ancestryCluster, 0u),
            &sector, AFATFS_CACHE_READ, 0);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        dotDot = &((fatDirectoryEntry_t *)sector)[1];
        if ((dotDot->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0u ||
            dotDot->filename[0] != '.' || dotDot->filename[1] != '.' ||
            dotDot->filename[2] != ' ') {
            op->terminalResult = AFATFS_RESULT_CORRUPT_DIRECTORY;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        parentCluster = ((uint32_t)dotDot->firstClusterHigh << 16u) |
                        dotDot->firstClusterLow;
        if (parentCluster == 0u &&
            afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT32) {
            parentCluster = afatfs.rootDirectoryCluster;
        }
        if (parentCluster == op->ancestryCluster) {
            op->terminalResult = AFATFS_RESULT_CORRUPT_DIRECTORY;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }

        /* One link is consumed per invocation to preserve bounded poll work. */
        op->ancestryCluster = parentCluster;
        op->ancestryDepth++;
        return;
    }

    case AFATFS_RENAME_OBJECT_PHASE_FIND_SOURCE:
        status = afatfs_findNextObject(op->parent,
                                       &op->objectFinder,
                                       &op->source);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(op->parent, &op->objectFinder);
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (op->source.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(op->parent, &op->objectFinder);
            op->terminalResult = AFATFS_RESULT_NOT_FOUND;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        /*
         * Source scan: match the old display component under the caller's
         * policy.
         *
         * Production Kit/Scene saves pass case-insensitive matching because an
         * occupied slot is the same product object even if the on-card
         * component case differs from the user-entered replacement. Exact
         * diagnostics may still pass case-sensitive matching when they need to
         * probe raw VFAT behavior.
         */
        if (fat_compareDisplayName(
                op->source.id.displayName,
                op->oldName,
                op->matchMode == AFATFS_MATCH_CASE_SENSITIVE) != 0) {
            return;
        }
        afatfs_findLastObject(op->parent, &op->objectFinder);
        if (!afatfs_renameObjectRunIsSectorLocal(&op->source)) {
            op->terminalResult = AFATFS_RESULT_UNSUPPORTED_LAYOUT;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        afatfs_renameObjectSetOldRunStart(op);
        op->phase = AFATFS_RENAME_OBJECT_PHASE_LOAD_SOURCE_ENTRY;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_LOAD_SOURCE_ENTRY:
    {
        uint8_t *sector;
        status = afatfs_cacheSector(op->source.id.sfnEntry.sectorNumberPhysical,
                                    &sector,
                                    AFATFS_CACHE_READ,
                                    0);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE ||
            op->source.id.sfnEntry.entryIndex < 0) {
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->sourceEntry =
            ((fatDirectoryEntry_t *)sector)[op->source.id.sfnEntry.entryIndex];
        /*
         * Same-display fast path.
         *
         * Under case-insensitive matching, folded equality is not enough to
         * skip work: a case-only rename must still rewrite the LFN/SFN run so
         * the visible card name changes to newName. Only byte-identical display
         * text can use this success shortcut.
         */
        if (!op->moveAcrossParents &&
            fat_compareDisplayName(op->source.id.displayName,
                                   op->newName,
                                   true) == 0) {
            afatfs_renameObjectCopyOpenName(op->source.id.shortName,
                                            op->generatedOpenName);
            op->succeeded = 1u;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_NAME;
        goto doMore;
    }

    case AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_NAME:
    {
        uint8_t len;
        memset(&op->newNameState, 0, sizeof(op->newNameState));
        len = afatfs_copySanitizedLongName(op->newNameState.longName,
                                           op->newName);
        if (len == 0u) {
            op->terminalResult = AFATFS_RESULT_INVALID_NAME;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->newNameState.longNameEnabled = 1u;
        op->newNameState.lfnEntryCount = (uint8_t)((len + 12u) / 13u);
        op->newNameState.matchMode = op->matchMode;
        op->newNameState.aliasOrdinal = 0u;
        op->newNameState.openNameOut = op->generatedOpenName;
        if (!afatfs_generateShortAlias(&op->newNameState)) {
            op->terminalResult = AFATFS_RESULT_ALREADY_EXISTS;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN;
        goto doMore;
    }

    case AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN:
        if (afatfs_fileIsBusy(op->destinationParent))
            return;
        afatfs_renameObjectRestartCollisionScan(op);
        op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN:
    {
        fatDirectoryEntry_t *entry = NULL;
        status = afatfs_findNext(op->destinationParent,
                                 &op->rawFinder,
                                 &entry);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            afatfs_findLast(op->destinationParent);
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (entry == NULL) {
            afatfs_findLast(op->destinationParent);
            if (afatfs_renameObjectCanRewriteInPlace(op) ||
                afatfs_freeRunIsReady(&op->newNameState)) {
                afatfs_renameObjectChooseRun(op);
                op->phase = AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN;
                goto doMore;
            }
            if (afatfs_fileIsBusy(op->destinationParent))
                return;
            status = afatfs_extendSubdirectory(op->destinationParent,
                                               NULL, NULL,
                                               NULL, NULL, 0u);
            if (status == AFATFS_OPERATION_FAILURE) {
                op->terminalResult = afatfs.filesystemFull
                    ? AFATFS_RESULT_NO_SPACE
                    : AFATFS_RESULT_IO_ERROR;
                op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
                goto doMore;
            }
            op->phase = AFATFS_RENAME_OBJECT_PHASE_WAIT_EXTEND;
            return;
        }
        if (fat_isDirectoryEntryEmpty(entry) ||
            fat_isDirectoryEntryTerminator(entry)) {
            afatfs_noteFreeDirectoryEntry(&op->newNameState, &op->rawFinder);
            afatfs_lfnScanReset(&op->newNameState);
            return;
        }
        if (afatfs_isLfnDirectoryEntry(entry)) {
            op->newNameState.freeRunLength = 0u;
            afatfs_lfnScanAppend(&op->newNameState, entry);
            return;
        }
        if ((entry->attrib & FAT_FILE_ATTRIBUTE_VOLUME_ID) != 0u) {
            op->newNameState.freeRunLength = 0u;
            afatfs_lfnScanReset(&op->newNameState);
            return;
        }
        if ((op->moveAcrossParents ||
             !afatfs_entryPointerEquals(&op->rawFinder,
                                        &op->source.id.sfnEntry)) &&
            afatfs_renameObjectRawEntryMatchesNew(op, entry)) {
            afatfs_findLast(op->destinationParent);
            op->terminalResult = AFATFS_RESULT_ALREADY_EXISTS;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if ((op->moveAcrossParents ||
             !afatfs_entryPointerEquals(&op->rawFinder,
                                        &op->source.id.sfnEntry)) &&
            memcmp(entry->filename,
                   op->newNameState.filename,
                   FAT_FILENAME_LENGTH) == 0) {
            afatfs_findLast(op->destinationParent);
            op->newNameState.aliasOrdinal++;
            if (!afatfs_generateShortAlias(&op->newNameState)) {
                op->terminalResult = AFATFS_RESULT_ALREADY_EXISTS;
                op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
                goto doMore;
            }
            op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN;
            goto doMore;
        }
        op->newNameState.freeRunLength = 0u;
        afatfs_lfnScanReset(&op->newNameState);
        return;
    }

    case AFATFS_RENAME_OBJECT_PHASE_WAIT_EXTEND:
        if (afatfs_fileIsBusy(op->destinationParent))
            return;
        op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN:
        status = afatfs_renameObjectWriteRun(op);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (op->movedEntryRun) {
            /*
             * A newly allocated destination run must reach media before the
             * source can be retired. Cache write order alone is not a durable
             * namespace guarantee; the sync phase ensures power loss can leave
             * duplicates but not remove the sole on-card name.
             */
            op->phase = AFATFS_RENAME_OBJECT_PHASE_SYNC_DESTINATION;
        } else {
            op->succeeded = 1u;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
        }
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_SYNC_DESTINATION:
        /*
         * Persistence barrier between destination visibility and any source
         * metadata change. afatfs_sync() is non-blocking: false yields until all
         * dirty/in-flight cache sectors have completed.
         */
        if (!afatfs_sync())
            return;
        if (op->moveAcrossParents &&
            op->source.id.kind == AFATFS_OBJECT_DIRECTORY) {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_UPDATE_DOTDOT;
        } else {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN;
        }
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_UPDATE_DOTDOT:
    {
        uint8_t *sector;
        fatDirectoryEntry_t *dotDot;
        uint32_t parentCluster = op->destinationParent->firstCluster;

        /*
         * Re-parent a moved directory before retiring its source name.
         *
         * Inputs: source.firstCluster remains the directory data cluster and
         * destinationParent supplies the new structural parent (zero is valid
         * for FAT16 root). Output: entry one keeps its `..` name/attributes and
         * receives only the new high/low cluster halves. The split is the FAT
         * on-disk 32-bit cluster representation: high 16 bits and low 16 bits.
         * Visibility order already wrote the destination name, so failure here
         * leaves the source authoritative and reports IO/CORRUPT.
         * Affiliates: extendSubdirectory's initial `..` writer and move recovery.
         */
        status = afatfs_cacheSector(
            afatfs_fileClusterToPhysical(op->source.id.firstCluster, 0u),
            &sector, AFATFS_CACHE_READ | AFATFS_CACHE_WRITE, 0);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        dotDot = &((fatDirectoryEntry_t *)sector)[1];
        if ((dotDot->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0u ||
            dotDot->filename[0] != '.' || dotDot->filename[1] != '.' ||
            dotDot->filename[2] != ' ') {
            op->terminalResult = AFATFS_RESULT_CORRUPT_DIRECTORY;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        dotDot->firstClusterHigh = (uint16_t)(parentCluster >> 16u);
        dotDot->firstClusterLow = (uint16_t)(parentCluster & 0xffffu);
        afatfs_cacheSectorMarkDirty(
            afatfs_getCacheDescriptorForBuffer(sector));
        op->phase = AFATFS_RENAME_OBJECT_PHASE_SYNC_DOTDOT;
        goto doMore;
    }

    case AFATFS_RENAME_OBJECT_PHASE_SYNC_DOTDOT:
        /* Persist the new ancestry before the old parent name is retired. */
        if (!afatfs_sync())
            return;
        op->phase = AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN:
        status = afatfs_renameObjectRetireOldRun(op);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_SUCCESS)
            op->succeeded = 1u;
        else
            op->terminalResult = AFATFS_RESULT_IO_ERROR;
        op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_FINISH:
        afatfs_renameObjectFinish(op->succeeded != 0u);
        return;
    }
}

bool afatfs_renameObject_lfn(const char *oldDisplayName,
                             const char *newDisplayName,
                             afatfsMatchMode_t matchMode,
                             char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                             afatfsCallback_t complete)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;

    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active ||
        afatfs.removeObjects.active ||
        afatfs_fileIsBusy(&afatfs.currentDirectory) ||
        afatfs.currentDirectory.childOperationRetainCount != 0u) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    if (openNameOut)
        openNameOut[0] = '\0';
    if (afatfs_copySanitizedLongName(op->oldName, oldDisplayName) == 0u ||
        afatfs_copySanitizedLongName(op->newName, newDisplayName) == 0u) {
        return false;
    }

    op->active = 1u;
    op->phase = AFATFS_RENAME_OBJECT_PHASE_INITIAL;
    op->matchMode = matchMode;
    op->callback = complete;
    op->openNameOut = openNameOut;
    op->parent = &afatfs.currentDirectory;
    op->destinationParent = &afatfs.currentDirectory;
    op->parentRetained = 1u;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    /*
     * Legacy rename now takes the same exclusive parent lock as renameObjectAt.
     * This prevents a concurrent child create from sharing currentDirectory's
     * cursor while preserving the legacy callback and name-source scan.
     */
    afatfs.currentDirectory.childOperationRetainCount = 1u;
    afatfs_renameObjectContinue();
    return true;
}

bool afatfs_renameObjectAt(afatfsDirHandle_t parent,
                           const afatfsObjectId_t *source,
                           const char *newDisplayName,
                           afatfsMatchMode_t matchMode,
                           char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                           afatfsResultCallback_t complete)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;

    /*
     * Start a parent-relative rename from a copied physical capability.
     *
     * Start validation is side-effect free: false means no callback. Once
     * accepted, parent is exclusively retained, source/name are copied into
     * global coordinator state, and every terminal path reports exactly one
     * structured result after release. Affiliates: object iterator, stale-SFN
     * validation, same-parent move optimization, and replace promotion.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active || afatfs.removeObjects.active || !source ||
        source->kind == AFATFS_OBJECT_NONE ||
        !afatfs_parentCanAcceptChild(parent) ||
        !afatfs_childComponentCanStart(newDisplayName) ||
        matchMode > AFATFS_MATCH_CASE_SENSITIVE) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    if (openNameOut)
        openNameOut[0] = '\0';
    if (afatfs_copySanitizedLongName(op->newName, newDisplayName) == 0u)
        return false;

    op->active = 1u;
    op->phase = AFATFS_RENAME_OBJECT_PHASE_INITIAL;
    op->matchMode = matchMode;
    op->resultCallback = complete;
    op->openNameOut = openNameOut;
    op->parent = parent;
    op->destinationParent = parent;
    op->requestedSource = *source;
    op->byIdentity = 1u;
    op->parentRetained = 1u;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    parent->childOperationRetainCount = 1u;
    afatfs_renameObjectContinue();
    return true;
}

bool afatfs_moveObject(afatfsDirHandle_t sourceParent,
                       const afatfsObjectId_t *source,
                       afatfsDirHandle_t destinationParent,
                       const char *destinationName,
                       afatfsMatchMode_t matchMode,
                       char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                       afatfsResultCallback_t complete)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;
    uint8_t sameParent = sourceParent == destinationParent ? 1u : 0u;

    /*
     * Start cross-parent move in the shared rename coordinator.
     *
     * What: validates both idle parents, copies every async input, and acquires
     * one exclusive lifetime count per distinct parent. Why: destination VFAT
     * allocation reuses rename's collision/run writer, while explicit source
     * validation and ancestry phases keep object identity safe. Inputs remain
     * caller-owned after return; output true guarantees one result callback.
     * Affiliates: afatfs_renameObjectContinue(), directory `..` update, staged
     * promotion, and child-operation lifetime guards.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active || afatfs.removeObjects.active || !source ||
        source->kind == AFATFS_OBJECT_NONE ||
        !afatfs_parentCanAcceptChild(sourceParent) ||
        (!sameParent &&
         !afatfs_parentCanAcceptChild(destinationParent)) ||
        !afatfs_childComponentCanStart(destinationName) ||
        matchMode > AFATFS_MATCH_CASE_SENSITIVE) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    if (openNameOut)
        openNameOut[0] = '\0';
    if (afatfs_copySanitizedLongName(op->newName, destinationName) == 0u)
        return false;

    op->active = 1u;
    op->phase = AFATFS_RENAME_OBJECT_PHASE_INITIAL;
    op->matchMode = matchMode;
    op->resultCallback = complete;
    op->openNameOut = openNameOut;
    op->parent = sourceParent;
    op->destinationParent = destinationParent;
    op->requestedSource = *source;
    op->byIdentity = 1u;
    op->parentRetained = 1u;
    op->moveAcrossParents = sameParent ? 0u : 1u;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    sourceParent->childOperationRetainCount = 1u;
    if (!sameParent) {
        op->destinationParentRetained = 1u;
        destinationParent->childOperationRetainCount = 1u;
    }
    afatfs_renameObjectContinue();
    return true;
}

static uint8_t afatfs_removeObjectMatches(
        const afatfsRemoveObjects_t *op,
        const afatfsObjectInfo_t *object)
{
    uint8_t i;

    /*
     * Match under the requested policy, not raw byte equality.
     *
     * Production overwrite passes case-insensitive matching so every case
     * variant of the same user-facing filename is removed. Exact diagnostics
     * can still pass case-sensitive matching to test a single physical display
     * component.
     *
     * Exact short-alias mode exists for recursive directory deletion. Inputs
     * there come from afatfsObjectInfo_t::shortName, the same alias used to
     * open/chdir into a child directory. Comparing the printable alias byte for
     * byte prevents a duplicate visible LFN from causing the final empty-dir
     * removal to retire a sibling that was not the directory just emptied.
     */
    if (!object || object->id.kind == AFATFS_OBJECT_NONE)
        return 0u;
    if (op->matchShortName) {
        for (i = 0u; i < AFATFS_SHORT_FILENAME_MAX; i++) {
            if (object->id.shortName[i] != op->shortName[i])
                return 0u;
            if (object->id.shortName[i] == '\0')
                return 1u;
        }
        return 1u;
    }
    return (uint8_t)(fat_compareDisplayName(
        object->id.displayName,
        op->displayName,
        op->matchMode == AFATFS_MATCH_CASE_SENSITIVE) == 0);
}

static uint8_t afatfs_removeObjectDirectoryAllowed(
        const afatfsRemoveObjects_t *op,
        const afatfsObjectInfo_t *object)
{
    (void)object;

    /*
     * Directory handling is intentionally conservative.
     *
     * This primitive may retire a directory only when the caller selected
     * AFATFS_REMOVE_EMPTY_DIRECTORIES. It does not inspect children; higher
     * layers must recurse first and call this only for an already-empty
     * directory. File overwrite continues to use AFATFS_REMOVE_FILES_ONLY and
     * therefore leaves matching directories untouched.
     */
    return (uint8_t)(op->mode == AFATFS_REMOVE_EMPTY_DIRECTORIES);
}

static void afatfs_removeObjectPrepareSyntheticFile(
        afatfsRemoveObjects_t *op)
{
    /*
     * Build a synthetic file handle from the source SFN entry.
     *
     * The truncate code already knows how to release a file cluster chain and
     * mark one SFN entry deleted. This synthetic handle supplies exactly the
     * metadata it expects: directoryEntryPos, firstCluster, cursorCluster,
     * logicalSize, physicalSize, type, attrib, and a truncate operation state.
     * The handle is private to the remove operation and is never returned to
     * callers.
     */
    afatfs_initFileHandle(&op->syntheticFile);
    afatfs_fileLoadDirectoryEntry(&op->syntheticFile, &op->sourceEntry);
    op->syntheticFile.directoryEntryPos = op->object.id.sfnEntry;
    op->syntheticFile.mode = AFATFS_FILE_MODE_WRITE;
}

static void afatfs_removeObjectsFinish(bool success)
{
    afatfsRemoveObjects_t *op = &afatfs.removeObjects;
    afatfsCallback_t callback = op->callback;

    /*
     * Finish exactly once and release the global removal slot.
     *
     * complete is deliberately callback-only like rename: callers sequence the
     * next open/create phase from their own filesystem state machine, while
     * asyncfatfs keeps no persistent success object to return.
     */
    op->succeeded = success ? 1u : 0u;
    op->active = 0u;
    /* Release before callback so overwrite may queue its replacement child. */
    if (op->parentRetained)
        afatfs_releaseChildParent(&afatfs.currentDirectory);
    if (callback)
        callback();
}

static void afatfs_removeObjectsContinue(void)
{
    afatfsRemoveObjects_t *op = &afatfs.removeObjects;
    afatfsOperationStatus_e status;

    if (!op->active)
        return;

doMore:
    switch (op->phase) {
    case AFATFS_REMOVE_OBJECTS_PHASE_INITIAL:
        if (afatfs_fileIsBusy(&afatfs.currentDirectory))
            return;
        afatfs_findFirstObject(&afatfs.currentDirectory, &op->finder);
        op->phase = AFATFS_REMOVE_OBJECTS_PHASE_SCAN;
        goto doMore;

    case AFATFS_REMOVE_OBJECTS_PHASE_SCAN:
        status = afatfs_findNextObject(&afatfs.currentDirectory,
                                       &op->finder,
                                       &op->object);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(&afatfs.currentDirectory, &op->finder);
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        if (op->object.id.kind == AFATFS_OBJECT_NONE) {
            afatfs_findLastObject(&afatfs.currentDirectory, &op->finder);
            op->succeeded = 1u;
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        if (!afatfs_removeObjectMatches(op, &op->object)) {
            /*
             * Yield after one non-matching object.
             *
             * The finder retains its cursor, so the next poll resumes at the
             * following object. This keeps overwrite cleanup asynchronous even
             * in large directories where most entries do not match the target
             * display component.
             */
            return;
        }
        if (op->object.id.kind == AFATFS_OBJECT_DIRECTORY &&
            !afatfs_removeObjectDirectoryAllowed(op, &op->object)) {
            /*
             * Skip matching directories that this removal mode may not delete.
             *
             * What: Leaves the current directory-shaped object untouched,
             * clears any pending LFN fragments that belonged to it, and
             * continues the scan at the following raw directory entry.
             *
             * Why: AFATFS_REMOVE_FILES_ONLY is used before Kit and Instrument
             * file saves to collapse same-casefold file variants. If a host
             * filesystem created a directory with the same display component,
             * that directory must not be recursively removed here, but the
             * scan must still advance. Returning without advancing would
             * revisit the same matching directory on every poll and stall the
             * caller before it can create the replacement file.
             *
             * Inputs: op->object is the matching directory returned by the
             * current afatfs_findNextObject() call. Outputs/effects: no FAT
             * entries are changed; the raw finder remains positioned after
             * the skipped object so the next state-machine pass can inspect the
             * following object or reach directory exhaustion.
             *
             * Affiliates/clients: filesystem_saveInstrument_tick(),
             * InstrumentMrp Save, and filesystem.c recursive directory
             * cleanup.
             */
            afatfs_objectScanReset(&op->finder);
            goto doMore;
        }
        afatfs_findLastObject(&afatfs.currentDirectory, &op->finder);
        if (!afatfs_renameObjectRunIsSectorLocal(&op->object)) {
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_REMOVE_OBJECTS_PHASE_LOAD_ENTRY;
        goto doMore;

    case AFATFS_REMOVE_OBJECTS_PHASE_LOAD_ENTRY:
    {
        uint8_t *sector;
        status = afatfs_cacheSector(op->object.id.sfnEntry.sectorNumberPhysical,
                                    &sector,
                                    AFATFS_CACHE_READ,
                                    0);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE ||
            op->object.id.sfnEntry.entryIndex < 0) {
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        op->sourceEntry =
            ((fatDirectoryEntry_t *)sector)[op->object.id.sfnEntry.entryIndex];
        if (op->object.id.kind == AFATFS_OBJECT_FILE ||
            op->mode == AFATFS_REMOVE_EMPTY_DIRECTORIES) {
            afatfs_removeObjectPrepareSyntheticFile(op);
            if (!afatfs_ftruncate(&op->syntheticFile, NULL)) {
                op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
                goto doMore;
            }
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_TRUNCATE_FILE;
        } else {
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_RETIRE_NAME_RUN;
        }
        goto doMore;
    }

    case AFATFS_REMOVE_OBJECTS_PHASE_TRUNCATE_FILE:
        status = afatfs_ftruncateContinue(&op->syntheticFile, true);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_REMOVE_OBJECTS_PHASE_RETIRE_NAME_RUN;
        goto doMore;

    case AFATFS_REMOVE_OBJECTS_PHASE_RETIRE_NAME_RUN:
        /*
         * Retire the LFN/SFN entry run after cluster release.
         *
         * Cluster release must happen first for files so no newly-created
         * object can reuse a directory entry that still points at live
         * clusters. The shared run helper then removes both LFN fragments and
         * the already-deleted SFN entry from the visible directory namespace.
         */
        status = afatfs_retireObjectNameRun(&op->object);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_REMOVE_OBJECTS_PHASE_RESTART_SCAN;
        goto doMore;

    case AFATFS_REMOVE_OBJECTS_PHASE_RESTART_SCAN:
        /*
         * Restart the scan after every deletion.
         *
         * Directory sectors have just been mutated and the previous finder may
         * point into an entry run that no longer exists. Restarting is cheaper
         * and safer than trying to repair raw scan state in place, and
         * overwrite cleanup is a bounded foreground filesystem operation rather
         * than an audio-thread path.
         */
        op->phase = AFATFS_REMOVE_OBJECTS_PHASE_INITIAL;
        goto doMore;

    case AFATFS_REMOVE_OBJECTS_PHASE_FINISH:
        afatfs_removeObjectsFinish(op->succeeded != 0u);
        return;
    }
}

bool afatfs_removeObjects_lfn(const char *displayName,
                              afatfsMatchMode_t matchMode,
                              afatfsRemoveObjectMode_t mode,
                              afatfsCallback_t complete)
{
    afatfsRemoveObjects_t *op = &afatfs.removeObjects;

    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active ||
        afatfs.renameObject.active ||
        afatfs_fileIsBusy(&afatfs.currentDirectory) ||
        afatfs.currentDirectory.childOperationRetainCount != 0u) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    if (afatfs_copySanitizedLongName(op->displayName, displayName) == 0u)
        return false;

    op->active = 1u;
    op->phase = AFATFS_REMOVE_OBJECTS_PHASE_INITIAL;
    op->matchMode = matchMode;
    op->mode = mode;
    op->callback = complete;
    /* Own currentDirectory's cursor exclusively until removeObjectsFinish(). */
    op->parentRetained = 1u;
    afatfs.currentDirectory.childOperationRetainCount = 1u;
    afatfs_removeObjectsContinue();
    return true;
}

bool afatfs_removeObject(const char *filename,
                         afatfsRemoveObjectMode_t mode,
                         afatfsCallback_t complete)
{
    afatfsRemoveObjects_t *op = &afatfs.removeObjects;
    uint8_t i;

    /*
     * Remove a single object by the printable SFN alias returned by asyncfatfs.
     *
     * Inputs: filename is an AFATFS_SHORT_FILENAME_MAX-sized open component
     * such as afatfsObjectInfo_t::shortName or mkdir_lfn()'s openNameOut.
     * Output/effects: the same remove state machine scans currentDirectory,
     * but afatfs_removeObjectMatches() compares object->id.shortName exactly
     * instead of comparing display names. That lets filesystem.c delete the
     * precise empty directory it already opened, even if another directory on
     * the card has the same long display component.
     *
     * The copy loop rejects empty input and guarantees NUL termination. It does
     * not sanitize or uppercase because callers pass aliases produced by this
     * module, and preserving case bits keeps the comparison aligned with
     * afatfs_findNextObject() output.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active ||
        afatfs.renameObject.active ||
        afatfs_fileIsBusy(&afatfs.currentDirectory) ||
        afatfs.currentDirectory.childOperationRetainCount != 0u) {
        return false;
    }
    if (!filename || filename[0] == '\0')
        return false;

    memset(op, 0, sizeof(*op));
    for (i = 0u; i < AFATFS_SHORT_FILENAME_MAX; i++) {
        op->shortName[i] = filename[i];
        if (filename[i] == '\0')
            break;
    }
    op->shortName[AFATFS_SHORT_FILENAME_MAX - 1u] = '\0';

    op->active = 1u;
    op->phase = AFATFS_REMOVE_OBJECTS_PHASE_INITIAL;
    op->mode = mode;
    op->matchShortName = 1u;
    op->callback = complete;
    /* Exact-alias removal shares the same exclusive global-parent lifetime. */
    op->parentRetained = 1u;
    afatfs.currentDirectory.childOperationRetainCount = 1u;
    afatfs_removeObjectsContinue();
    return true;
}

static afatfsFilePtr_t afatfs_createFile(afatfsFilePtr_t file, const char *name, uint8_t attrib, uint8_t fileMode,
        afatfsFileCallback_t callback)
{
    /*
     * Compatibility wrapper for original short-name callers.
     *
     * Keeping this small wrapper means all pre-existing call sites continue to
     * route through the same symbol and keep 8.3-only semantics unless they opt
     * into the explicit LFN API.
     */
    return afatfs_createFileInternal(&afatfs.currentDirectory,
                                     file, name, attrib, fileMode,
                                     (fileMode & AFATFS_FILE_MODE_CREATE)
                                         ? AFATFS_CREATE_OR_OPEN
                                         : AFATFS_OPEN_EXISTING,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     NULL, false,
                                     callback, NULL);
}

static void afatfs_fcloseContinue(afatfsFilePtr_t file)
{
    afatfsCacheBlockDescriptor_t *descriptor;
    afatfsCloseFile_t *opState = &file->operation.state.closeFile;

    /*
     * Directories don't update their parent directory entries over time, because their fileSize field in the directory
     * never changes (when we add the first cluster to the directory we save the directory entry at that point and it
     * doesn't change afterwards). So don't bother trying to save their directory entries during fclose().
     *
     * Also if we only opened the file for read then we didn't change the directory entry either.
     */
    if (file->type != AFATFS_FILE_TYPE_DIRECTORY && file->type != AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY
            && (file->mode & (AFATFS_FILE_MODE_APPEND | AFATFS_FILE_MODE_WRITE)) != 0) {
        if (afatfs_saveDirectoryEntry(file, AFATFS_SAVE_DIRECTORY_FOR_CLOSE) != AFATFS_OPERATION_SUCCESS) {
            return;
        }
    }

    // Release our reservation on the directory cache if needed
    if ((file->mode & AFATFS_FILE_MODE_RETAIN_DIRECTORY) != 0) {
        descriptor = afatfs_findCacheSector(file->directoryEntryPos.sectorNumberPhysical);

        if (descriptor) {
            descriptor->retainCount = MAX((int) descriptor->retainCount - 1, 0);
        }
    }

    // Release locks on the sector at the file cursor position
    afatfs_fileUnlockCacheSector(file);

#ifdef AFATFS_USE_FREEFILE
    // Release our exclusive lock on the freefile if needed
    if ((file->mode & AFATFS_FILE_MODE_CONTIGUOUS) != 0) {
        afatfs_assert(afatfs.freeFile.operation.operation == AFATFS_FILE_OPERATION_LOCKED);
        afatfs.freeFile.operation.operation = AFATFS_FILE_OPERATION_NONE;
    }
#endif

    file->type = AFATFS_FILE_TYPE_NONE;
    file->operation.operation = AFATFS_FILE_OPERATION_NONE;

    if (opState->callback) {
        opState->callback();
    }
}

/**
 * Returns true if an operation was successfully queued to close the file and destroy the file handle. If the file is
 * currently busy, false is returned and you should retry later.
 *
 * If provided, the callback will be called after the operation completes (pass NULL for no callback).
 *
 * If this function returns true, you should not make any further calls to the file (as the handle might be reused for a
 * new file).
 */
bool afatfs_fclose(afatfsFilePtr_t file, afatfsCallback_t callback)
{
    if (!file || file->type == AFATFS_FILE_TYPE_NONE) {
        return true;
    } else if (afatfs_fileIsBusy(file) ||
               file->childOperationRetainCount != 0u) {
        /*
         * A retained parent cannot be closed while a child scan owns its
         * mutable cursor/cache lifetime. Output false asks the caller to retry
         * after the child callback, which releases the count before firing.
         * Affiliates: parent-relative create/open and future copy coordinators.
         */
        return false;
    } else {
        afatfs_fileUpdateFilesize(file);

        file->operation.operation = AFATFS_FILE_OPERATION_CLOSE;
        file->operation.state.closeFile.callback = callback;
        afatfs_fcloseContinue(file);
        return true;
    }
}

/**
 * Create a new directory with the given name, or open the directory if it already exists.
 *
 * The directory will be passed to the callback, or NULL if the creation failed.
 *
 * Returns true if the directory creation was begun, or false if there are too many open files.
 */
bool afatfs_mkdir(const char *filename, afatfsFileCallback_t callback)
{
    if (!afatfs_parentCanAcceptChild(&afatfs.currentDirectory))
        return false;
    afatfsFilePtr_t file = afatfs_allocateFileHandle();

    if (file) {
        afatfs_createFile(file, filename, FAT_FILE_ATTRIBUTE_DIRECTORY, AFATFS_FILE_MODE_CREATE | AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE, callback);
    } else if (callback) {
        callback(NULL);
    }

    return file != NULL;
}

bool afatfs_opendir(const char *filename, afatfsFileCallback_t callback)
{
    if (!afatfs_parentCanAcceptChild(&afatfs.currentDirectory))
        return false;
    afatfsFilePtr_t file = afatfs_allocateFileHandle();

    if (file) {
        afatfs_createFile(file, filename, FAT_FILE_ATTRIBUTE_DIRECTORY,
                          AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE,
                          callback);
    } else if (callback) {
        callback(NULL);
    }

    return file != NULL;
}

bool afatfs_mkdir_lfn(const char *displayName,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t callback)
{
    if (!afatfs_parentCanAcceptChild(&afatfs.currentDirectory))
        return false;
    afatfsFilePtr_t file = afatfs_allocateFileHandle();

    /*
     * Create/open a directory with a VFAT display name.
     *
     * The returned handle behaves exactly like afatfs_mkdir(); the only new
     * contract is that a newly-created directory receives LFN fragments for
     * displayName and openNameOut receives the generated 8.3 alias. Higher
     * layers can cache the alias for existing afatfs_fopen()/chdir flows while
     * scans display the preserved long name. matchMode selects whether an
     * existing LFN must match byte-for-byte, which is required by the File/Dir
     * test menus, or through compatibility ASCII folding.
     */
    if (file) {
        afatfs_createFileInternal(&afatfs.currentDirectory,
                                  file, displayName,
                                  FAT_FILE_ATTRIBUTE_DIRECTORY,
                                  AFATFS_FILE_MODE_CREATE |
                                      AFATFS_FILE_MODE_READ |
                                      AFATFS_FILE_MODE_WRITE,
                                  AFATFS_CREATE_OR_OPEN,
                                  matchMode,
                                  openNameOut, true, callback, NULL);
    } else if (callback) {
        callback(NULL);
    }

    return file != NULL;
}

bool afatfs_opendir_lfn(const char *displayName,
                        afatfsMatchMode_t matchMode,
                        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                        afatfsFileCallback_t callback)
{
    if (!afatfs_parentCanAcceptChild(&afatfs.currentDirectory))
        return false;
    afatfsFilePtr_t file = afatfs_allocateFileHandle();

    /*
     * Open an existing directory by its preserved display component.
     *
     * This is deliberately separate from afatfs_mkdir_lfn(): Load:[Dir] must
     * prove that the selected directory already exists and must not create a
     * fresh empty directory when a case-sensitive lookup misses. Inputs mirror
     * mkdir_lfn except fileMode omits AFATFS_FILE_MODE_CREATE; output is a
     * directory handle suitable for afatfs_chdir() and object enumeration.
     */
    if (file) {
        afatfs_createFileInternal(&afatfs.currentDirectory,
                                  file, displayName,
                                  FAT_FILE_ATTRIBUTE_DIRECTORY,
                                  AFATFS_FILE_MODE_READ |
                                      AFATFS_FILE_MODE_WRITE,
                                  AFATFS_OPEN_EXISTING,
                                  matchMode,
                                  openNameOut, true, callback, NULL);
    } else if (callback) {
        callback(NULL);
    }

    return file != NULL;
}

/**
 * Change the working directory to the directory with the given handle (use fopen). Pass NULL for `directory` in order to
 * change to the root directory.
 *
 * Returns true on success, false if you should call again later to retry. After changing into a directory, your handle
 * to that directory may be closed by fclose().
 */
bool afatfs_chdir(afatfsFilePtr_t directory)
{
    if (afatfs_fileIsBusy(&afatfs.currentDirectory) ||
        afatfs.currentDirectory.childOperationRetainCount != 0u) {
        /* CurrentDirectory is a retained parent; rebinding would orphan its child scan. */
        return false;
    }

    if (directory) {
        if (afatfs_fileIsBusy(directory) ||
            directory->childOperationRetainCount != 0u) {
            /* The source handle must also remain stable while another child owns it. */
            return false;
        }

        memcpy(&afatfs.currentDirectory, directory, sizeof(*directory));
        return true;
    } else {
        afatfs_initFileHandle(&afatfs.currentDirectory);

        afatfs.currentDirectory.mode = AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE;

        if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16)
            afatfs.currentDirectory.type = AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY;
        else
            afatfs.currentDirectory.type = AFATFS_FILE_TYPE_DIRECTORY;

        afatfs.currentDirectory.firstCluster = afatfs.rootDirectoryCluster;
        afatfs.currentDirectory.attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;

        // Root directories don't have a directory entry to represent themselves:
        afatfs.currentDirectory.directoryEntryPos.sectorNumberPhysical = 0;

        afatfs_fseek(&afatfs.currentDirectory, 0, AFATFS_SEEK_SET);

        return true;
    }
}

afatfsOperationStatus_e afatfs_chdirParent(void)
{
    uint8_t *sector = NULL;
    fatDirectoryEntry_t *entries;
    fatDirectoryEntry_t *parent_entry;
    uint32_t parent_cluster;
    afatfsOperationStatus_e status;

    /*
     * Move currentDirectory to its structural FAT parent without resolving the
     * literal name ".." through the generic 8.3 filename parser.
     *
     * fat_convertFilenameToFATStyle("..") normalizes to an all-space name, so
     * afatfs_opendir("..") cannot reliably open the parent entry. Recursive
     * directory deletion needs parent traversal after it has emptied a child;
     * read entry 1 from the child directory's first sector instead, where FAT
     * stores the real ".." cluster link.
     */
    if (afatfs_fileIsBusy(&afatfs.currentDirectory))
        return AFATFS_OPERATION_IN_PROGRESS;
    if (afatfs.currentDirectory.type == AFATFS_FILE_TYPE_NONE ||
        afatfs.currentDirectory.type == AFATFS_FILE_TYPE_NORMAL) {
        return AFATFS_OPERATION_FAILURE;
    }
    if (afatfs.currentDirectory.type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY ||
        (afatfs.currentDirectory.directoryEntryPos.sectorNumberPhysical == 0 &&
         afatfs.currentDirectory.firstCluster == afatfs.rootDirectoryCluster)) {
        return afatfs_chdir(NULL)
            ? AFATFS_OPERATION_SUCCESS
            : AFATFS_OPERATION_IN_PROGRESS;
    }

    afatfs_fileUnlockCacheSector(&afatfs.currentDirectory);
    afatfs.currentDirectory.cursorOffset = 0u;
    afatfs.currentDirectory.cursorCluster = afatfs.currentDirectory.firstCluster;
    afatfs.currentDirectory.cursorPreviousCluster = 0u;
    status = afatfs_cacheSector(
        afatfs_fileGetCursorPhysicalSector(&afatfs.currentDirectory),
        &sector,
        AFATFS_CACHE_READ,
        0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entries = (fatDirectoryEntry_t *)sector;
    parent_entry = &entries[1];
    if (parent_entry->filename[0] != '.' ||
        parent_entry->filename[1] != '.' ||
        (parent_entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0u) {
        return AFATFS_OPERATION_FAILURE;
    }
    parent_cluster =
        ((uint32_t)parent_entry->firstClusterHigh << 16) |
        parent_entry->firstClusterLow;
    if (parent_cluster == 0u || parent_cluster == afatfs.rootDirectoryCluster) {
        return afatfs_chdir(NULL)
            ? AFATFS_OPERATION_SUCCESS
            : AFATFS_OPERATION_IN_PROGRESS;
    }

    afatfs_initFileHandle(&afatfs.currentDirectory);
    afatfs.currentDirectory.mode =
        AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE;
    afatfs.currentDirectory.type = AFATFS_FILE_TYPE_DIRECTORY;
    afatfs.currentDirectory.attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
    afatfs.currentDirectory.firstCluster = parent_cluster;
    afatfs.currentDirectory.cursorCluster = parent_cluster;
    afatfs.currentDirectory.cursorPreviousCluster = 0u;
    afatfs.currentDirectory.cursorOffset = 0u;
    afatfs.currentDirectory.physicalSize = afatfs_clusterSize();
    afatfs.currentDirectory.logicalSize = afatfs.currentDirectory.physicalSize;
    afatfs.currentDirectory.directoryEntryPos.sectorNumberPhysical = 0u;
    return AFATFS_OPERATION_SUCCESS;
}

static uint8_t afatfs_modeFromString(const char *mode)
{
    uint8_t fileMode = 0;

    /*
     * Parse the original asyncfatfs fopen mode strings in one shared place.
     *
     * The new LFN open path must behave exactly like afatfs_fopen() for read,
     * write, append, read/write, and optional contiguous/freefile flags. Keeping
     * this as a helper prevents future mode fixes from accidentally applying to
     * only one filename family.
     */
    if (!mode)
        return 0u;
    switch (mode[0]) {
        case 'r':
            fileMode = AFATFS_FILE_MODE_READ;
        break;
        case 'w':
            fileMode = AFATFS_FILE_MODE_WRITE | AFATFS_FILE_MODE_CREATE;
        break;
        case 'a':
            fileMode = AFATFS_FILE_MODE_APPEND | AFATFS_FILE_MODE_CREATE;
        break;
    }

    switch (mode[1]) {
        case '+':
            fileMode |= AFATFS_FILE_MODE_READ;

            if (fileMode == AFATFS_FILE_MODE_READ) {
                fileMode |= AFATFS_FILE_MODE_WRITE;
            }
        break;
        case 's':
#ifdef AFATFS_USE_FREEFILE
            fileMode |= AFATFS_FILE_MODE_CONTIGUOUS | AFATFS_FILE_MODE_RETAIN_DIRECTORY;
#endif
        break;
    }
    return fileMode;
}

/**
 * Begin the process of opening a file with the given name in the current working directory (paths in the filename are
 * not supported) using the given mode.
 *
 * To open the current working directory, pass "." for filename.
 *
 * The complete() callback is called when finished with either a file handle (file was opened) or NULL upon failure.
 *
 * Supported file mode strings:
 *
 * r - Read from an existing file
 * w - Create a file for write access, if the file already exists then truncate it
 * a - Create a file for write access to the end of the file only, if the file already exists then append to it
 *
 * r+ - Read and write from an existing file
 * w+ - Read and write from an existing file, if the file doesn't already exist it is created
 * a+ - Read from or append to an existing file, if the file doesn't already exist it is created TODO
 *
 * as - Create a new file which is stored contiguously on disk (high performance mode/freefile) for append or write
 * ws   If the file is already non-empty or freefile support is not compiled in then it will fall back to non-contiguous
 *      operation.
 *
 * All other mode strings are illegal. In particular, don't add "b" to the end of the mode string.
 *
 * Returns false if the the open failed really early (out of file handles).
 */
bool afatfs_fopen(const char *filename, const char *mode, afatfsFileCallback_t complete)
{
    uint8_t fileMode = afatfs_modeFromString(mode);
    afatfsFilePtr_t file;

    if (!afatfs_parentCanAcceptChild(&afatfs.currentDirectory))
        return false;
    file = afatfs_allocateFileHandle();

    if (file) {
        afatfs_createFile(file, filename, FAT_FILE_ATTRIBUTE_ARCHIVE, fileMode, complete);
    } else if (complete) {
        complete(NULL);
    }

    return file != NULL;
}

bool afatfs_fopen_lfn(const char *displayName,
                      const char *mode,
                      afatfsMatchMode_t matchMode,
                      char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                      afatfsFileCallback_t complete)
{
    uint8_t fileMode = afatfs_modeFromString(mode);
    if (!afatfs_parentCanAcceptChild(&afatfs.currentDirectory))
        return false;
    afatfsFilePtr_t file = afatfs_allocateFileHandle();

    /*
     * Open/create a file using a preserved display component plus SFN alias.
     *
     * This is the system-wide long-name companion to afatfs_fopen(). It still
     * operates in the current working directory and still returns a normal file
     * handle, but when creation is needed it emits VFAT LFN entries and writes
     * the generated short alias to openNameOut for later fast/open-by-alias
     * workflows. matchMode controls only display-name lookup; creation still
     * writes exactly the sanitized component supplied by the caller.
     */
    if (file) {
        afatfs_createFileInternal(&afatfs.currentDirectory,
                                  file, displayName,
                                  FAT_FILE_ATTRIBUTE_ARCHIVE,
                                  fileMode,
                                  (fileMode & AFATFS_FILE_MODE_CREATE)
                                      ? AFATFS_CREATE_OR_OPEN
                                      : AFATFS_OPEN_EXISTING,
                                  matchMode, openNameOut, true,
                                  complete, NULL);
    } else if (complete) {
        complete(NULL);
    }
    return file != NULL;
}

static bool afatfs_parentCanAcceptChild(afatfsDirHandle_t parent)
{
    /*
     * Validate the exclusive parent-relative start boundary.
     *
     * What: requires a live directory, no queued operation, and no existing
     * child owner. Why: directory scans mutate cursor fields and cache retains;
     * sharing those fields across two async operations corrupts both finders.
     * Input: caller-supplied parent handle. Output: true only when a child start
     * may increment childOperationRetainCount. Affiliates: all three child APIs,
     * afatfs_createFileInternal(), fclose(), and chdir().
     */
    return parent &&
           (parent->type == AFATFS_FILE_TYPE_DIRECTORY ||
            parent->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) &&
           !afatfs_fileIsBusy(parent) &&
           parent->childOperationRetainCount == 0u;
}

static bool afatfs_childComponentCanStart(const char *displayName)
{
    /*
     * Reject path-like/structural components before accepting ownership.
     * Sanitizable user characters remain valid and are copied by the create
     * state; NULL, empty, ".", and ".." cannot identify a product child.
     * Affiliates: VFAT sanitation and structural directory iteration.
     */
    return displayName && displayName[0] != '\0' &&
           strcmp(displayName, ".") != 0 &&
           strcmp(displayName, "..") != 0;
}

afatfsDirHandle_t afatfs_openRoot(void)
{
    afatfsFilePtr_t root;
    uint32_t rootBytes;

    /*
     * Materialize the FAT root in a caller-owned open-file slot.
     *
     * What: copies only filesystem geometry into a freshly initialized handle;
     * it does not borrow currentDirectory cursor/cache state. Why: explicit
     * parent APIs must be able to keep root and a child directory open at the
     * same time without a global chdir mutation. Input validation is limited to
     * READY plus pool availability because no SD I/O is required. Output is an
     * idle read/write directory handle whose cursor begins before entry zero.
     * Affiliates: afatfs_allocateFileHandle(), afatfs_initFileHandle(),
     * afatfs_findFirstObject(), child-operation retain counts, and fclose().
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY)
        return NULL;
    root = afatfs_allocateFileHandle();
    if (!root)
        return NULL;

    afatfs_initFileHandle(root);
    root->mode = AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE;
    root->attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
    root->firstCluster = afatfs.rootDirectoryCluster;
    root->cursorCluster = afatfs.rootDirectoryCluster;
    root->cursorPreviousCluster = 0u;
    root->cursorOffset = 0u;
    root->directoryEntryPos.sectorNumberPhysical = 0u;
    root->directoryEntryPos.entryIndex = -1;

    if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16) {
        /*
         * FAT16 root is a fixed sector extent rather than a cluster chain.
         * Multiplying the mounted sector count by the invariant 512-byte
         * asyncfatfs sector size gives both logical and physical scan bounds.
         */
        rootBytes = afatfs.rootDirectorySectors * AFATFS_SECTOR_SIZE;
        root->type = AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY;
        root->logicalSize = rootBytes;
        root->physicalSize = rootBytes;
    } else {
        /*
         * FAT32 root follows an ordinary directory cluster chain. One cluster
         * is the known initial allocation; directory iteration follows FAT as
         * required, while the size fields match other opened directories.
         */
        rootBytes = afatfs_clusterSize();
        root->type = AFATFS_FILE_TYPE_DIRECTORY;
        root->logicalSize = rootBytes;
        root->physicalSize = rootBytes;
    }
    return root;
}

bool afatfs_fopenChild(afatfsDirHandle_t parent,
                       const char *displayName,
                       const char *accessMode,
                       afatfsCreateMode_t createMode,
                       afatfsMatchMode_t matchMode,
                       char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                       afatfsOpenResultCallback_t complete)
{
    uint8_t fileMode;
    afatfsFilePtr_t file;

    /*
     * Start one structured, parent-relative file open/create.
     *
     * Inputs are validated before allocation so false always means no callback.
     * displayName and policy are copied into CREATE_FILE state; parent remains
     * retained until normal open, append seek, or write truncation completes.
     * Output OK returns an ordinary open file handle. Affiliates: the shared
     * create scanner, recursive copy streaming, and replace staging.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        !afatfs_parentCanAcceptChild(parent) ||
        !afatfs_childComponentCanStart(displayName) ||
        !accessMode ||
        createMode > AFATFS_CREATE_OR_OPEN ||
        matchMode > AFATFS_MATCH_CASE_SENSITIVE) {
        return false;
    }
    fileMode = afatfs_modeFromString(accessMode);
    if (fileMode == 0u)
        return false;
    file = afatfs_allocateFileHandle();
    if (!file)
        return false;

    afatfs_createFileInternal(parent, file, displayName,
                              FAT_FILE_ATTRIBUTE_ARCHIVE,
                              fileMode, createMode, matchMode,
                              openNameOut, true, NULL, complete);
    return true;
}

bool afatfs_openDirChild(afatfsDirHandle_t parent,
                         const char *displayName,
                         afatfsMatchMode_t matchMode,
                         char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                         afatfsOpenResultCallback_t complete)
{
    afatfsFilePtr_t file;

    /*
     * Open one existing directory relative to parent.
     * Missing objects report NOT_FOUND and type collisions report
     * NOT_DIRECTORY through the shared policy state. False queues no callback.
     * Affiliates: copy traversal and explicit transaction recovery parents.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        !afatfs_parentCanAcceptChild(parent) ||
        !afatfs_childComponentCanStart(displayName) ||
        matchMode > AFATFS_MATCH_CASE_SENSITIVE) {
        return false;
    }
    file = afatfs_allocateFileHandle();
    if (!file)
        return false;

    afatfs_createFileInternal(parent, file, displayName,
                              FAT_FILE_ATTRIBUTE_DIRECTORY,
                              AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE,
                              AFATFS_OPEN_EXISTING, matchMode,
                              openNameOut, true, NULL, complete);
    return true;
}

bool afatfs_createDirChild(afatfsDirHandle_t parent,
                           const char *displayName,
                           afatfsCreateMode_t createMode,
                           afatfsMatchMode_t matchMode,
                           char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                           afatfsOpenResultCallback_t complete)
{
    afatfsFilePtr_t file;

    /*
     * Create or resolve one directory relative to parent under explicit policy.
     * CREATE_NEW prevents stale-tree merging; CREATE_OR_OPEN preserves legacy
     * mkdir semantics when deliberately requested. On creation the retained
     * parent also supplies the structural `..` cluster through the extension
     * handoff. Affiliates: staged replace roots and recursive copy descent.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        !afatfs_parentCanAcceptChild(parent) ||
        !afatfs_childComponentCanStart(displayName) ||
        createMode > AFATFS_CREATE_OR_OPEN ||
        matchMode > AFATFS_MATCH_CASE_SENSITIVE) {
        return false;
    }
    file = afatfs_allocateFileHandle();
    if (!file)
        return false;

    afatfs_createFileInternal(parent, file, displayName,
                              FAT_FILE_ATTRIBUTE_DIRECTORY,
                              AFATFS_FILE_MODE_READ | AFATFS_FILE_MODE_WRITE,
                              createMode, matchMode,
                              openNameOut, true, NULL, complete);
    return true;
}

/**
 * Write a single character to the file at the current cursor position. If the cache is too busy to accept the write,
 * it is silently dropped.
 */
void afatfs_fputc(afatfsFilePtr_t file, uint8_t c)
{
    uint32_t cursorOffsetInSector = file->cursorOffset % AFATFS_SECTOR_SIZE;

    int cacheIndex = file->writeLockedCacheIndex;

    /* If we've already locked the current sector in the cache, and we won't be completing the sector, we won't
     * be caching/uncaching/seeking, so we can just run this simpler fast case.
     */
    if (cacheIndex != -1 && cursorOffsetInSector != AFATFS_SECTOR_SIZE - 1) {
        afatfs_cacheSectorGetMemory(cacheIndex)[cursorOffsetInSector] = c;
        file->cursorOffset++;
    } else {
        // Slow path
        afatfs_fwrite(file, &c, sizeof(c));
    }
}

/**
 * Attempt to write `len` bytes from `buffer` into the `file`.
 *
 * Returns the number of bytes actually written.
 *
 * 0 will be returned when:
 *     The filesystem is busy (try again later)
 *
 * Fewer bytes will be written than requested when:
 *     The write spanned a sector boundary and the next sector's contents/location was not yet available in the cache.
 *     Or you tried to extend the length of the file but the filesystem is full (check afatfs_isFull()).
 */
uint32_t afatfs_fwrite(afatfsFilePtr_t file, const uint8_t *buffer, uint32_t len)
{
    if ((file->mode & (AFATFS_FILE_MODE_APPEND | AFATFS_FILE_MODE_WRITE)) == 0) {
        return 0;
    }

    if (afatfs_fileIsBusy(file)) {
        // There might be a seek pending
        return 0;
    }

    uint32_t cursorOffsetInSector = file->cursorOffset % AFATFS_SECTOR_SIZE;
    uint32_t writtenBytes = 0;

    while (len > 0) {
        uint32_t bytesToWriteThisSector = MIN(AFATFS_SECTOR_SIZE - cursorOffsetInSector, len);
        uint8_t *sectorBuffer;

        sectorBuffer = afatfs_fileLockCursorSectorForWrite(file);
        if (!sectorBuffer) {
            // Cache is currently busy
            break;
        }

        memcpy(sectorBuffer + cursorOffsetInSector, buffer, bytesToWriteThisSector);

        writtenBytes += bytesToWriteThisSector;

        /*
         * If the seek doesn't complete immediately then we'll break and wait for that seek to complete by waiting for
         * the file to be non-busy on entry again.
         *
         * A seek operation should always be able to queue on the file since we have checked that the file wasn't busy
         * on entry (fseek will never return AFATFS_OPERATION_FAILURE).
         *
         * If the seek has to queue, when the seek completes, it'll update the fileSize for us to contain the cursor.
         */
        if (afatfs_fseekInternal(file, bytesToWriteThisSector,
                                 NULL, NULL, NULL) ==
            AFATFS_OPERATION_IN_PROGRESS) {
            break;
        }

#ifdef AFATFS_USE_FREEFILE
        if ((file->mode & AFATFS_FILE_MODE_CONTIGUOUS) != 0) {
            afatfs_assert(file->cursorCluster < afatfs.freeFile.firstCluster);
        }
#endif

        len -= bytesToWriteThisSector;
        buffer += bytesToWriteThisSector;
        cursorOffsetInSector = 0;
    }

    return writtenBytes;
}

/**
 * Attempt to read `len` bytes from `file` into the `buffer`.
 *
 * Returns the number of bytes actually read.
 *
 * 0 will be returned when:
 *     The filesystem is busy (try again later)
 *     EOF was reached (check afatfs_isEof())
 *
 * Fewer bytes than requested will be read when:
 *     The read spans a AFATFS_SECTOR_SIZE boundary and the following sector was not available in the cache yet.
 */
uint32_t afatfs_fread(afatfsFilePtr_t file, uint8_t *buffer, uint32_t len)
{
    if ((file->mode & AFATFS_FILE_MODE_READ) == 0) {
        return 0;
    }

    if (afatfs_fileIsBusy(file)) {
        // There might be a seek pending
        return 0;
    }

    /*
     * If we've just previously fwritten() to extend the file, the logical filesize will be out of date and the cursor
     * will appear to be beyond the end of the file (but actually it's precisely at the end of the file, because if
     * we had seeked backwards to where we could read something with fseek(), we would have updated the filesize).
     */
    if (file->cursorOffset >= file->logicalSize)
        return 0;

    len = MIN(file->logicalSize - file->cursorOffset, len);

    uint32_t readBytes = 0;
    uint32_t cursorOffsetInSector = file->cursorOffset % AFATFS_SECTOR_SIZE;

    while (len > 0) {
        uint32_t bytesToReadThisSector = MIN(AFATFS_SECTOR_SIZE - cursorOffsetInSector, len);
        uint8_t *sectorBuffer;

        sectorBuffer = afatfs_fileRetainCursorSectorForRead(file);
        if (!sectorBuffer) {
            // Cache is currently busy
            return readBytes;
        }

        memcpy(buffer, sectorBuffer + cursorOffsetInSector, bytesToReadThisSector);

        readBytes += bytesToReadThisSector;

        /*
         * If the seek doesn't complete immediately then we'll break and wait for that seek to complete by waiting for
         * the file to be non-busy on entry again.
         *
         * A seek operation should always be able to queue on the file since we have checked that the file wasn't busy
         * on entry (fseek will never return AFATFS_OPERATION_FAILURE).
         */
        if (afatfs_fseekInternal(file, bytesToReadThisSector,
                                 NULL, NULL, NULL) ==
            AFATFS_OPERATION_IN_PROGRESS) {
            break;
        }

        len -= bytesToReadThisSector;
        buffer += bytesToReadThisSector;
        cursorOffsetInSector = 0;
    }

    return readBytes;
}

/**
 * Returns true if the file's pointer position currently lies at the end-of-file point (i.e. one byte beyond the last
 * byte in the file).
 */
bool afatfs_feof(afatfsFilePtr_t file)
{
    return file->cursorOffset >= file->logicalSize;
}

/**
 * Continue any queued operations on the given file.
 */
static void afatfs_fileOperationContinue(afatfsFile_t *file)
{
    if (file->type == AFATFS_FILE_TYPE_NONE)
        return;

    switch (file->operation.operation) {
        case AFATFS_FILE_OPERATION_CREATE_FILE:
            afatfs_createFileContinue(file);
        break;
        case AFATFS_FILE_OPERATION_SEEK:
            afatfs_fseekInternalContinue(file);
        break;
        case AFATFS_FILE_OPERATION_CLOSE:
            afatfs_fcloseContinue(file);
        break;
        case AFATFS_FILE_OPERATION_UNLINK:
             afatfs_funlinkContinue(file);
        break;
        case AFATFS_FILE_OPERATION_TRUNCATE:
            afatfs_ftruncateContinue(file, false);
        break;
#ifdef AFATFS_USE_FREEFILE
        case AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER:
            afatfs_appendSuperclusterContinue(file);
        break;
        case AFATFS_FILE_OPERATION_LOCKED:
            ;
        break;
#endif
        case AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER:
            afatfs_appendRegularFreeClusterContinue(file);
        break;
        case AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY:
            afatfs_extendSubdirectoryContinue(file);
        break;
        case AFATFS_FILE_OPERATION_DELETE_TREE:
            afatfs_deleteTreeContinue(file);
        break;
        case AFATFS_FILE_OPERATION_NONE:
            ;
        break;
    }
}

/**
 * Check files for pending operations and execute them.
 */
static void afatfs_fileOperationsPoll()
{
    afatfs_fileOperationContinue(&afatfs.currentDirectory);

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING
    afatfs_fileOperationContinue(&afatfs.introSpecLog);
#endif

    for (int i = 0; i < AFATFS_MAX_OPEN_FILES; i++) {
        afatfs_fileOperationContinue(&afatfs.openFiles[i]);
    }
}

#ifdef AFATFS_USE_FREEFILE

/**
 * Return the available size of the freefile (used for files in contiguous append mode)
 */
uint32_t afatfs_getContiguousFreeSpace()
{
    return afatfs.freeFile.logicalSize;
}

/**
 * Call to set up the initial state for finding the largest block of free space on the device whose corresponding FAT
 * sectors are themselves entirely free space (so the free space has dedicated FAT sectors of its own).
 */
static void afatfs_findLargestContiguousFreeBlockBegin()
{
    // The first FAT sector has two reserved entries, so it isn't eligible for this search. Start at the next FAT sector.
    afatfs.initState.freeSpaceSearch.candidateStart = afatfs_fatEntriesPerSector();
    afatfs.initState.freeSpaceSearch.candidateEnd = afatfs.initState.freeSpaceSearch.candidateStart;
    afatfs.initState.freeSpaceSearch.bestGapStart = 0;
    afatfs.initState.freeSpaceSearch.bestGapLength = 0;
    afatfs.initState.freeSpaceSearch.phase = AFATFS_FREE_SPACE_SEARCH_PHASE_FIND_HOLE;
}

/**
 * Call to continue the search for the largest contiguous block of free space on the device.
 *
 * Returns:
 *     AFATFS_OPERATION_IN_PROGRESS - SD card is busy, call again later to resume
 *     AFATFS_OPERATION_SUCCESS - When the search has finished and afatfs.initState.freeSpaceSearch has been updated with the details of the best gap.
 *     AFATFS_OPERATION_FAILURE - When a read error occured
 */
static afatfsOperationStatus_e afatfs_findLargestContiguousFreeBlockContinue()
{
    afatfsFreeSpaceSearch_t *opState = &afatfs.initState.freeSpaceSearch;
    uint32_t fatEntriesPerSector = afatfs_fatEntriesPerSector();
    uint32_t candidateGapLength, searchLimit;
    afatfsFindClusterStatus_e searchStatus;

    while (1) {
        switch (opState->phase) {
            case AFATFS_FREE_SPACE_SEARCH_PHASE_FIND_HOLE:
                // Find the first free cluster
                switch (afatfs_findClusterWithCondition(CLUSTER_SEARCH_FREE_AT_BEGINNING_OF_FAT_SECTOR, &opState->candidateStart, afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER)) {
                    case AFATFS_FIND_CLUSTER_FOUND:
                        opState->candidateEnd = opState->candidateStart + 1;
                        opState->phase = AFATFS_FREE_SPACE_SEARCH_PHASE_GROW_HOLE;
                    break;

                    case AFATFS_FIND_CLUSTER_FATAL:
                        // Some sort of read error occured
                        return AFATFS_OPERATION_FAILURE;

                    case AFATFS_FIND_CLUSTER_NOT_FOUND:
                        // We finished searching the volume (didn't find any more holes to examine)
                        return AFATFS_OPERATION_SUCCESS;

                    case AFATFS_FIND_CLUSTER_IN_PROGRESS:
                        return AFATFS_OPERATION_IN_PROGRESS;
                }
            break;
            case AFATFS_FREE_SPACE_SEARCH_PHASE_GROW_HOLE:
                // Find the first used cluster after the beginning of the hole (that signals the end of the hole)

                // Don't search beyond the end of the volume, or such that the freefile size would exceed the max filesize
                searchLimit = MIN((uint64_t) opState->candidateStart + FAT_MAXIMUM_FILESIZE / afatfs_clusterSize(), afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER);

                searchStatus = afatfs_findClusterWithCondition(CLUSTER_SEARCH_OCCUPIED, &opState->candidateEnd, searchLimit);

                switch (searchStatus) {
                    case AFATFS_FIND_CLUSTER_NOT_FOUND:
                    case AFATFS_FIND_CLUSTER_FOUND:
                        // Either we found a used sector, or the search reached the end of the volume or exceeded the max filesize
                        candidateGapLength = opState->candidateEnd - opState->candidateStart;

                        if (candidateGapLength > opState->bestGapLength) {
                            opState->bestGapStart = opState->candidateStart;
                            opState->bestGapLength = candidateGapLength;
                        }

                        if (searchStatus == AFATFS_FIND_CLUSTER_NOT_FOUND) {
                            // This is the best hole there can be
                            return AFATFS_OPERATION_SUCCESS;
                        } else {
                            // Start a new search for a new hole
                            opState->candidateStart = roundUpTo(opState->candidateEnd + 1, fatEntriesPerSector);
                            opState->phase = AFATFS_FREE_SPACE_SEARCH_PHASE_FIND_HOLE;
                        }
                    break;

                    case AFATFS_FIND_CLUSTER_FATAL:
                        // Some sort of read error occured
                        return AFATFS_OPERATION_FAILURE;

                    case AFATFS_FIND_CLUSTER_IN_PROGRESS:
                        return AFATFS_OPERATION_IN_PROGRESS;
                }
            break;
        }
    }
}

static void afatfs_freeFileCreated(afatfsFile_t *file)
{
    if (file) {
        // Did the freefile already have allocated space?
        if (file->logicalSize > 0) {
            // We've completed freefile init, move on to the next init phase
            afatfs.initPhase = AFATFS_INITIALIZATION_FREEFILE_LAST + 1;
        } else {
            // Allocate clusters for the freefile
            afatfs_findLargestContiguousFreeBlockBegin();
            afatfs.initPhase = AFATFS_INITIALIZATION_FREEFILE_FAT_SEARCH;
        }
    } else {
        // Failed to allocate an entry
        afatfs.lastError = AFATFS_ERROR_GENERIC;
        afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
    }
}

#endif

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING

static void afatfs_introspecLogCreated(afatfsFile_t *file)
{
    if (file) {
        afatfs.initPhase++;
    } else {
        afatfs.lastError = AFATFS_ERROR_GENERIC;
        afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
    }
}

#endif

static void afatfs_initContinue()
{
#ifdef AFATFS_USE_FREEFILE
    afatfsOperationStatus_e status;
#endif

    uint8_t *sector;

    doMore:

    switch (afatfs.initPhase) {
        case AFATFS_INITIALIZATION_READ_MBR:
            if (afatfs_cacheSector(0, &sector, AFATFS_CACHE_READ | AFATFS_CACHE_DISCARDABLE, 0) == AFATFS_OPERATION_SUCCESS) {
                if (afatfs_parseMBR(sector)) {
                    afatfs.initPhase = AFATFS_INITIALIZATION_READ_VOLUME_ID;
                    goto doMore;
                } else {
                    afatfs.lastError = AFATFS_ERROR_BAD_MBR;
                    afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
                }
            }
        break;
        case AFATFS_INITIALIZATION_READ_VOLUME_ID:
            if (afatfs_cacheSector(afatfs.partitionStartSector, &sector, AFATFS_CACHE_READ | AFATFS_CACHE_DISCARDABLE, 0) == AFATFS_OPERATION_SUCCESS) {
                if (afatfs_parseVolumeID(sector)) {
                    // Open the root directory
                    afatfs_chdir(NULL);

                    afatfs.initPhase++;
                } else {
                    afatfs.lastError = AFATFS_ERROR_BAD_FILESYSTEM_HEADER;
                    afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
                }
            }
        break;

#ifdef AFATFS_USE_FREEFILE
        case AFATFS_INITIALIZATION_FREEFILE_CREATE:
            afatfs.initPhase = AFATFS_INITIALIZATION_FREEFILE_CREATING;

            afatfs_createFile(&afatfs.freeFile, AFATFS_FREESPACE_FILENAME, FAT_FILE_ATTRIBUTE_SYSTEM | FAT_FILE_ATTRIBUTE_READ_ONLY,
                AFATFS_FILE_MODE_CREATE | AFATFS_FILE_MODE_RETAIN_DIRECTORY, afatfs_freeFileCreated);
        break;
        case AFATFS_INITIALIZATION_FREEFILE_CREATING:
            afatfs_fileOperationContinue(&afatfs.freeFile);
        break;
        case AFATFS_INITIALIZATION_FREEFILE_FAT_SEARCH:
            if (afatfs_findLargestContiguousFreeBlockContinue() == AFATFS_OPERATION_SUCCESS) {
                // If the freefile ends up being empty then we only have to save its directory entry:
                afatfs.initPhase = AFATFS_INITIALIZATION_FREEFILE_SAVE_DIR_ENTRY;

                if (afatfs.initState.freeSpaceSearch.bestGapLength > AFATFS_FREEFILE_LEAVE_CLUSTERS + 1) {
                    afatfs.initState.freeSpaceSearch.bestGapLength -= AFATFS_FREEFILE_LEAVE_CLUSTERS;

                    /* So that the freefile never becomes empty, we want it to occupy a non-integer number of
                     * superclusters. So its size mod the number of clusters in a supercluster should be 1.
                     */
                    afatfs.initState.freeSpaceSearch.bestGapLength = ((afatfs.initState.freeSpaceSearch.bestGapLength - 1) & ~(afatfs_fatEntriesPerSector() - 1)) + 1;

                    // Anything useful left over?
                    if (afatfs.initState.freeSpaceSearch.bestGapLength > afatfs_fatEntriesPerSector()) {
                        uint32_t startCluster = afatfs.initState.freeSpaceSearch.bestGapStart;
                        // Points 1-beyond the final cluster of the freefile:
                        uint32_t endCluster = afatfs.initState.freeSpaceSearch.bestGapStart + afatfs.initState.freeSpaceSearch.bestGapLength;

                        afatfs_assert(endCluster < afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER);

                        afatfs.initState.freeSpaceFAT.startCluster = startCluster;
                        afatfs.initState.freeSpaceFAT.endCluster = endCluster;

                        afatfs.freeFile.firstCluster = startCluster;

                        afatfs.freeFile.logicalSize = afatfs.initState.freeSpaceSearch.bestGapLength * afatfs_clusterSize();
                        afatfs.freeFile.physicalSize = afatfs.freeFile.logicalSize;

                        // We can write the FAT table for the freefile now
                        afatfs.initPhase = AFATFS_INITIALIZATION_FREEFILE_UPDATE_FAT;
                    } // Else the freefile's FAT chain and filesize remains the default (empty)
                }

                goto doMore;
            }
        break;
        case AFATFS_INITIALIZATION_FREEFILE_UPDATE_FAT:
            status = afatfs_FATFillWithPattern(AFATFS_FAT_PATTERN_TERMINATED_CHAIN, &afatfs.initState.freeSpaceFAT.startCluster, afatfs.initState.freeSpaceFAT.endCluster);

            if (status == AFATFS_OPERATION_SUCCESS) {
                afatfs.initPhase = AFATFS_INITIALIZATION_FREEFILE_SAVE_DIR_ENTRY;

                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
                afatfs.lastError = AFATFS_ERROR_GENERIC;
                afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
            }
        break;
        case AFATFS_INITIALIZATION_FREEFILE_SAVE_DIR_ENTRY:
            status = afatfs_saveDirectoryEntry(&afatfs.freeFile, AFATFS_SAVE_DIRECTORY_NORMAL);

            if (status == AFATFS_OPERATION_SUCCESS) {
                afatfs.initPhase++;
                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
                afatfs.lastError = AFATFS_ERROR_GENERIC;
                afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_FATAL;
            }
        break;
#endif

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING
        case AFATFS_INITIALIZATION_INTROSPEC_LOG_CREATE:
            afatfs.initPhase = AFATFS_INITIALIZATION_INTROSPEC_LOG_CREATING;

            afatfs_createFile(&afatfs.introSpecLog, AFATFS_INTROSPEC_LOG_FILENAME, FAT_FILE_ATTRIBUTE_ARCHIVE,
                AFATFS_FILE_MODE_CREATE | AFATFS_FILE_MODE_APPEND, afatfs_introspecLogCreated);
        break;
        case AFATFS_INITIALIZATION_INTROSPEC_LOG_CREATING:
            afatfs_fileOperationContinue(&afatfs.introSpecLog);
        break;
#endif

        case AFATFS_INITIALIZATION_DONE:
            afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_READY;
        break;
    }
}

/**
 * Check to see if there are any pending operations on the filesystem and perform a little work (without waiting on the
 * sdcard). You must call this periodically.
 */
void afatfs_poll()
{
    // Only attempt to continue FS operations if the card is present & ready, otherwise we would just be wasting time
    if (sdcard_poll()) {
        afatfs_flush();

        switch (afatfs.filesystemState) {
            case AFATFS_FILESYSTEM_STATE_INITIALIZATION:
                afatfs_initContinue();
            break;
            case AFATFS_FILESYSTEM_STATE_READY:
                afatfs_fileOperationsPoll();
                afatfs_renameObjectContinue();
                afatfs_removeObjectsContinue();
                /*
                 * Continue at most one reduced-RAM multi-handle coordinator.
                 *
                 * Ordinary handle operations run first so callbacks awaited by
                 * copy/replace can complete before the coordinator observes
                 * their phase on the next poll. The continuation itself yields
                 * after bounded scan/stream work and never waits for the card.
                 * Affiliates: afatfsTreeWorkspace_t and child-operation callbacks.
                 */
                if (afatfs.treeWorkspace.kind == AFATFS_TREE_OPERATION_COPY)
                    afatfs_copyTreeContinue();
                else if (afatfs.treeWorkspace.kind ==
                         AFATFS_TREE_OPERATION_REPLACE)
                    afatfs_replaceContinue();
            break;
            default:
                ;
        }
    }
}

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING

void afatfs_sdcardProfilerCallback(sdcardBlockOperation_e operation, uint32_t blockIndex, uint32_t duration)
{
    // Make sure the log file has actually been opened before we try to log to it:
    if (afatfs.introSpecLog.type == AFATFS_FILE_TYPE_NONE) {
        return;
    }

    enum {
        LOG_ENTRY_SIZE = 16 // Log entry size should be a power of two to avoid partial fwrites()
    };

    uint8_t buffer[LOG_ENTRY_SIZE];

    buffer[0] = operation;

    // Padding/reserved:
    buffer[1] = 0;
    buffer[2] = 0;
    buffer[3] = 0;

    buffer[4] = blockIndex & 0xFF;
    buffer[5] = (blockIndex >> 8) & 0xFF;
    buffer[6] = (blockIndex >> 16) & 0xFF;
    buffer[7] = (blockIndex >> 24) & 0xFF;

    buffer[8] = duration & 0xFF;
    buffer[9] = (duration >> 8) & 0xFF;
    buffer[10] = (duration >> 16) & 0xFF;
    buffer[11] = (duration >> 24) & 0xFF;

    // Padding/reserved:
    buffer[12] = 0;
    buffer[13] = 0;
    buffer[14] = 0;
    buffer[15] = 0;

    // Ignore write failures
    afatfs_fwrite(&afatfs.introSpecLog, buffer, LOG_ENTRY_SIZE);
}

#endif

afatfsFilesystemState_e afatfs_getFilesystemState()
{
    return afatfs.filesystemState;
}

afatfsError_e afatfs_getLastError()
{
    return afatfs.lastError;
}

void afatfs_init()
{
    afatfs.filesystemState = AFATFS_FILESYSTEM_STATE_INITIALIZATION;
    afatfs.initPhase = AFATFS_INITIALIZATION_READ_MBR;
    afatfs.lastClusterAllocated = FAT_SMALLEST_LEGAL_CLUSTER_NUMBER;

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING
    sdcard_setProfilerCallback(afatfs_sdcardProfilerCallback);
#endif
}

/**
 * Shut down the filesystem, flushing all data to the disk. Keep calling until it returns true.
 *
 * dirty - Set to true to skip the flush operation and terminate immediately (buffered data will be lost!)
 */
bool afatfs_destroy(bool dirty)
{
    // Only attempt detailed cleanup if the filesystem is in reasonable looking state
    if (!dirty && afatfs.filesystemState == AFATFS_FILESYSTEM_STATE_READY) {
        int openFileCount = 0;

        /*
         * Let multi-handle coordinators release their own handle graph first.
         *
         * What: active copy/commit/recovery receives another normal poll and
         * destroy retries later; an idle begun transaction is converted into
         * its exact abort operation. Why: blindly fclose()ing source,
         * destination, journal, or staging handles out from under their owner
         * corrupts callback state, while a begun transaction's retained parent
         * would otherwise make destroy wait forever. Input dirty=false requests
         * recoverable shutdown; output false means keep polling destroy.
         * Affiliates: afatfsTreeWorkspace_t and abort ownership transfer.
         */
        if (afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_NONE) {
            afatfs_poll();
            return false;
        }
        if (afatfs.replaceTransaction.active) {
            if (!afatfs_abortTreeReplace(&afatfs.replaceTransaction, NULL))
                return false;
            afatfs_poll();
            return false;
        }

        for (int i = 0; i < AFATFS_MAX_OPEN_FILES; i++) {
            if (afatfs.openFiles[i].type != AFATFS_FILE_TYPE_NONE) {
                afatfs_fclose(&afatfs.openFiles[i], NULL);
                // The close operation might not finish right away, so count this file as still open for now
                openFileCount++;
            }
        }

#ifdef AFATFS_USE_INTROSPECTIVE_LOGGING
        if (afatfs.introSpecLog.type != AFATFS_FILE_TYPE_NONE) {
            afatfs_fclose(&afatfs.introSpecLog, NULL);
            openFileCount++;
        }
#endif

#ifdef AFATFS_USE_FREEFILE
        if (afatfs.freeFile.type != AFATFS_FILE_TYPE_NONE) {
            afatfs_fclose(&afatfs.freeFile, NULL);
            openFileCount++;
        }
#endif

        if (afatfs.currentDirectory.type != AFATFS_FILE_TYPE_NONE) {
            afatfs_fclose(&afatfs.currentDirectory, NULL);
            openFileCount++;
        }

        afatfs_poll();

        if (!afatfs_flush()) {
            return false;
        }

        if (afatfs.cacheFlushInProgress) {
            return false;
        }

        if (openFileCount > 0) {
            return false;
        }

#ifdef AFATFS_DEBUG
        /* All sector locks should have been released by closing the files, so the subsequent flush should have written
         * all dirty pages to disk. If not, something's wrong:
         */
        for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
            afatfs_assert(afatfs.cacheDescriptor[i].state != AFATFS_CACHE_STATE_DIRTY);
        }
#endif
    }

    // Clear the afatfs so it's as if we never ran
    memset(&afatfs, 0, sizeof(afatfs));

    return true;
}

/**
 * Get a pessimistic estimate of the amount of buffer space that we have available to write to immediately.
 */
uint32_t afatfs_getFreeBufferSpace()
{
    uint32_t result = 0;
    for (int i = 0; i < AFATFS_NUM_CACHE_SECTORS; i++) {
        if (!afatfs.cacheDescriptor[i].locked && (afatfs.cacheDescriptor[i].state == AFATFS_CACHE_STATE_EMPTY || afatfs.cacheDescriptor[i].state == AFATFS_CACHE_STATE_IN_SYNC)) {
            result += AFATFS_SECTOR_SIZE;
        }
    }
    return result;
}

bool afatfs_deleteTree(const afatfsObjectId_t *root, afatfsResultCallback_t cb)
{
    afatfsFile_t *file;

    /*
     * Validate and fully initialize the private operation handle.
     *
     * Inputs: root must be a concrete directory identity discovered by object
     * iteration. Output: false queues nothing for invalid input or when all
     * open-file slots are occupied. Why initialization is mandatory:
     * afatfs_allocateFileHandle() deliberately returns recycled storage; only
     * afatfs_initFileHandle() restores cache indices to -1 and clears stale
     * cursor/operation union bytes. Setting type alone, as the first native
     * implementation did, violated that allocator contract and could inherit
     * ownership from an earlier file. Affiliates: openFiles[], the polling
     * dispatcher, and afatfs_deleteTreeFinish().
     */
    if (!root || root->kind != AFATFS_OBJECT_DIRECTORY ||
        root->firstCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER) {
        return false;
    }
    file = afatfs_allocateFileHandle();
    if (!file)
        return false;

    afatfs_initFileHandle(file);
    file->type = AFATFS_FILE_TYPE_NORMAL;
    file->operation.operation = AFATFS_FILE_OPERATION_DELETE_TREE;
    file->operation.state.deleteTree.phase = AFATFS_DELETE_TREE_INITIAL;
    file->operation.state.deleteTree.rootId = *root;
    file->operation.state.deleteTree.callback = cb;

    return true;
}

uint8_t afatfs_getDeleteTreePhase(void)
{
    for (int i = 0; i < AFATFS_MAX_OPEN_FILES; i++) {
        if (afatfs.openFiles[i].type != AFATFS_FILE_TYPE_NONE &&
            afatfs.openFiles[i].operation.operation == AFATFS_FILE_OPERATION_DELETE_TREE) {
            return (uint8_t)afatfs.openFiles[i].operation.state.deleteTree.phase;
        }
    }
    return 0xFF;
}

static afatfsOperationStatus_e afatfs_validateObjectId(
        afatfsDirHandle_t expectedParent,
        const afatfsObjectId_t *object,
        afatfsResultCode_t *validationResult)
{
    uint8_t *sector;
    fatDirectoryEntry_t *entry;
    afatfsOperationStatus_e status;
    uint32_t firstCluster;
    uint8_t entryIsDirectory;

    /*
     * Reload and validate one short-lived physical object capability.
     *
     * What: checks optional parent identity, physical SFN position, raw 11-byte
     * key, kind, and first cluster against current media. Why: a directory slot
     * may be deleted and reused at the same sector/index, so a delayed mutator
     * must never trust coordinates alone. Inputs: expectedParent may be NULL
     * when the caller has only the copied capability (native delete root);
     * object/result must be non-NULL. Outputs: IN_PROGRESS for cache I/O,
     * FAILURE/IO_ERROR for media failure, or SUCCESS with OK/STALE_OBJECT.
     * Affiliates: object iteration fingerprints, delete root validation, and
     * future by-identity rename/move/copy.
     */
    if (!object || !validationResult) {
        if (validationResult)
            *validationResult = AFATFS_RESULT_STALE_OBJECT;
        return AFATFS_OPERATION_SUCCESS;
    }
    *validationResult = AFATFS_RESULT_STALE_OBJECT;

    if (expectedParent &&
        (expectedParent->firstCluster != object->parentFirstCluster ||
         (expectedParent->type == AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY) !=
             (object->parentIsFat16Root != 0u))) {
        return AFATFS_OPERATION_SUCCESS;
    }
    if (object->sfnEntry.entryIndex < 0 ||
        (uint16_t)object->sfnEntry.entryIndex >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        /* Convert only after rejecting the -1 finder sentinel. */
        return AFATFS_OPERATION_SUCCESS;
    }

    status = afatfs_cacheSector(object->sfnEntry.sectorNumberPhysical,
                                &sector,
                                AFATFS_CACHE_READ,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS) {
        if (status == AFATFS_OPERATION_FAILURE)
            *validationResult = AFATFS_RESULT_IO_ERROR;
        return status;
    }
    entry = &((fatDirectoryEntry_t *)sector)[object->sfnEntry.entryIndex];
    if (fat_isDirectoryEntryEmpty(entry) ||
        fat_isDirectoryEntryTerminator(entry) ||
        fat_isLongDirectoryEntry(entry) ||
        (entry->attrib & FAT_FILE_ATTRIBUTE_VOLUME_ID) != 0u ||
        memcmp(entry->filename,
               object->rawShortName,
               FAT_FILENAME_LENGTH) != 0) {
        return AFATFS_OPERATION_SUCCESS;
    }

    entryIsDirectory =
        (entry->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0u ? 1u : 0u;
    firstCluster = ((uint32_t)entry->firstClusterHigh << 16u) |
                   entry->firstClusterLow;
    if ((object->kind == AFATFS_OBJECT_DIRECTORY) !=
            (entryIsDirectory != 0u) ||
        firstCluster != object->firstCluster) {
        return AFATFS_OPERATION_SUCCESS;
    }

    *validationResult = AFATFS_RESULT_OK;
    return AFATFS_OPERATION_SUCCESS;
}

/*
 * Complete one native tree delete through a single ownership boundary.
 *
 * Inputs: file is the private openFiles[] slot carrying DELETE_TREE state;
 * result is the terminal status delivered to the original caller. Effects:
 * copy the callback before clearing its union storage, release any directory
 * sector retained by the last scan, reset the complete handle (including both
 * cache indices), and only then invoke the callback exactly once. Why: directly
 * assigning type/operation to NONE leaked retained sectors and allowed the
 * handle to disappear before filesystem.c received completion, which is the
 * state reported as TOut06. Calling back after teardown also permits callback
 * clients to queue subsequent work without observing a still-busy handle.
 * Affiliates: every success/error exit in afatfs_deleteTreeContinue(),
 * afatfs_initFileHandle(), and filesystem.c::on_delete_tree_complete().
 */
static void afatfs_deleteTreeFinish(afatfsFile_t *file,
                                    afatfsResultCode_t result)
{
    afatfsResultCallback_t callback =
        file->operation.state.deleteTree.callback;

    afatfs_fileUnlockCacheSector(file);
    afatfs_initFileHandle(file);
    if (callback)
        callback(result);
}

static void afatfs_deleteTreeBindDirectory(afatfsFile_t *file,
                                           uint32_t firstCluster)
{
    uint8_t fat16Root =
        afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT16 &&
        firstCluster == 0u;

    /*
     * Rebind the private delete handle to a directory or FAT16 root cursor.
     *
     * What: releases any retained sector, resets all cursor/entry fields, and
     * selects FAT16's fixed root extent when cluster zero represents the parent.
     * Why: ordinary directories are cluster chains, but FAT16 root is a fixed
     * sector range; treating it as cluster zero would underflow physical-sector
     * arithmetic during child ascent. Input is a validated directory cluster
     * or zero FAT16 root marker. Output is a clean read-only scan handle.
     * Affiliates: OPEN_DIR, SCAN_PARENT_FOR_SELF, findFirstObject(), and the
     * structural `..` mapping.
     */
    afatfs_fileUnlockCacheSector(file);
    file->directoryEntryPos.sectorNumberPhysical = 0u;
    file->directoryEntryPos.entryIndex = -1;
    file->firstCluster = firstCluster;
    file->cursorCluster = firstCluster;
    file->cursorPreviousCluster = 0u;
    file->cursorOffset = 0u;
    file->mode = AFATFS_FILE_MODE_READ;
    file->attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
    if (fat16Root) {
        uint32_t rootBytes =
            afatfs.rootDirectorySectors * AFATFS_SECTOR_SIZE;

        /* FAT16 root size is a sector count, so multiply once by 512 bytes. */
        file->logicalSize = rootBytes;
        file->physicalSize = rootBytes;
        file->type = AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY;
    } else {
        file->logicalSize = 0u;
        file->physicalSize = 0u;
        file->type = AFATFS_FILE_TYPE_DIRECTORY;
    }
}

static void afatfs_deleteTreeContinue(afatfsFile_t *file)
{
    afatfsDeleteTree_t *op = &file->operation.state.deleteTree;
    afatfsOperationStatus_e status;
    afatfsObjectInfo_t object;

    doMore:
    switch (op->phase) {
        case AFATFS_DELETE_TREE_INITIAL:
            /*
             * Seed traversal from the copied physical root identity.
             *
             * The operation owns its root copy, so later product scans or UI
             * name changes cannot redirect deletion. OPEN_DIR will configure
             * the recycled file handle as a read-only directory cursor; root
             * itself is retired only after its children are exhausted.
             */
            op->currentTarget = op->rootId;
            op->depth = 0u;
            op->phase = AFATFS_DELETE_TREE_VALIDATE_ROOT;
            goto doMore;

        case AFATFS_DELETE_TREE_VALIDATE_ROOT:
        {
            afatfsResultCode_t validationResult;

            /*
             * Prove the caller's root capability still names the same SFN
             * before the first namespace mutation. The start function copied
             * root, so an intervening parent rename/delete is detected by raw
             * key/kind/cluster comparison and returns STALE_OBJECT. Cache/media
             * failure remains IO_ERROR; neither path retires any entry.
             */
            status = afatfs_validateObjectId(NULL, &op->rootId,
                                             &validationResult);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (validationResult != AFATFS_RESULT_OK) {
                afatfs_deleteTreeFinish(file, validationResult);
                return;
            }
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;
        }

        case AFATFS_DELETE_TREE_OPEN_DIR:
            /*
             * Rebind the traversal handle to one concrete directory cluster.
             *
             * Input: currentTarget is either root, a child selected during
             * SCAN, or a parent recovered from the structural ".." entry.
             * Effects: release the prior directory's retained sector before
             * changing cursorCluster, reset every cursor field used by raw FAT
             * iteration, and initialize a fresh LFN-aware scan. Why: retaining
             * a parent sector while changing firstCluster makes the next read
             * fail asyncfatfs' physical-sector ownership assertion; nested
             * Scene/Bank trees require this transition even though flat Kit
             * trees often do not. Affiliates: afatfs_fileRetainCursorSectorForRead(),
             * afatfs_findFirstObject(), and AFATFS_DELETE_TREE_DESCEND_DIR.
             */
            afatfs_deleteTreeBindDirectory(
                file, op->currentTarget.firstCluster);
            afatfs_findFirstObject(file, &op->finder);
            op->phase = AFATFS_DELETE_TREE_SCAN;
            goto doMore;

        case AFATFS_DELETE_TREE_SCAN:
            status = afatfs_findNextObject(file, &op->finder, &object);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            /*
             * Treat I/O failure and normal directory exhaustion separately.
             *
             * A failed scan does not prove a directory empty and must never
             * authorize retiring that directory. Both terminal branches first
             * release the iterator's retained sector; failure then tears down
             * with IO_ERROR, while a real NONE object advances to the normal
             * ascend/root-retire path. Affiliates: object iteration, cache
             * retain accounting, and the caller's structured result callback.
             */
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_findLastObject(file, &op->finder);
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(file, &op->finder);
                op->phase = AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND;
                return; // YIELD to poll loop to prevent freezing!
            }
            op->currentTarget = object.id;
            op->currentTargetHasLongName = object.hasLongName;
            if (object.id.kind == AFATFS_OBJECT_FILE) {
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                return; // YIELD
            }
            if (object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                /*
                 * The next phase changes this handle to the child cluster, so
                 * release the parent scan's retained sector now. Parent lookup
                 * is later reconstructed structurally from the child's ".."
                 * entry; no live parent iterator is required across descent.
                 */
                if (op->depth >= AFATFS_TREE_DEPTH_MAX) {
                    /*
                     * Stop before descending beyond the documented product
                     * tree bound. This also terminates corrupt ancestry cycles
                     * whose `..` links never lead back toward root.
                     */
                    afatfs_findLastObject(file, &op->finder);
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_DEPTH_LIMIT);
                    return;
                }
                afatfs_findLastObject(file, &op->finder);
                op->depth++;
                op->phase = AFATFS_DELETE_TREE_DESCEND_DIR;
                return; // YIELD
            }
            break;

        case AFATFS_DELETE_TREE_DESCEND_DIR:
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;

        case AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND:
            if (file->firstCluster == op->rootId.firstCluster) {
                /*
                 * Retire the root through the physical entry run captured by
                 * the caller's parent scan. rootId includes both SFN and LFN
                 * pointers, so no name lookup or parent chdir is necessary.
                 * The cluster comparison is safe for directories because a
                 * valid directory always owns a nonzero first cluster.
                 */
                op->currentTarget = op->rootId;
                op->currentTargetHasLongName = op->rootId.lfnEntryCount > 0 ? 1 : 0;
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                goto doMore;
            } else {
                uint32_t firstSector;
                firstSector = afatfs_fileClusterToPhysical(file->firstCluster, 0);
                uint8_t *sector;
                status = afatfs_cacheSector(firstSector, &sector, AFATFS_CACHE_READ, 0);
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                if (status == AFATFS_OPERATION_FAILURE) {
                    afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                    return;
                }
                fatDirectoryEntry_t *dotDot = &((fatDirectoryEntry_t*)sector)[1];
                uint32_t parentCluster;

                /*
                 * Validate the structural `..` record before trusting ancestry.
                 *
                 * Inputs are entry one of the emptied child's first sector.
                 * It must be a directory SFN named ".." and resolve to a legal,
                 * different cluster (zero is FAT's root encoding and is mapped
                 * to rootDirectoryCluster). Output is the parent cluster used
                 * for the physical self scan. Rejecting malformed/self links
                 * prevents an endless ascend/descend loop on damaged media.
                 * Affiliates: directory creation's `..` writer and depth bound.
                 */
                if ((dotDot->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0u ||
                    dotDot->filename[0] != '.' ||
                    dotDot->filename[1] != '.' ||
                    dotDot->filename[2] != ' ') {
                    afatfs_deleteTreeFinish(
                        file, AFATFS_RESULT_CORRUPT_DIRECTORY);
                    return;
                }
                parentCluster =
                    ((uint32_t)dotDot->firstClusterHigh << 16u) |
                    dotDot->firstClusterLow;
                if (parentCluster == 0u)
                    parentCluster = afatfs.rootDirectoryCluster;
                if (parentCluster == file->firstCluster ||
                    (parentCluster != 0u &&
                     (parentCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                      parentCluster > afatfs.numClusters + 1u))) {
                    afatfs_deleteTreeFinish(
                        file, AFATFS_RESULT_CORRUPT_DIRECTORY);
                    return;
                }
                op->targetClusterToRetire = file->firstCluster;
                op->currentTarget.firstCluster = parentCluster;
                op->phase = AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF;
                goto doMore;
            }
            break;

        case AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF:
            /*
             * Start a fresh scan of the recovered parent cluster.
             *
             * The child scan was released before descent/ascend. Releasing
             * again is harmless and guarantees this transition is safe if a
             * future phase begins retaining a sector. The parent is scanned by
             * physical firstCluster identity so duplicate display names cannot
             * select a sibling directory.
             */
            afatfs_deleteTreeBindDirectory(
                file, op->currentTarget.firstCluster);
            afatfs_findFirstObject(file, &op->finder);
            /*
             * One validated ascent has completed. Depth is decremented only
             * after the handle is safely rebound to the parent, so failures in
             * `..` validation cannot underflow or misreport traversal state.
             */
            if (op->depth == 0u) {
                afatfs_deleteTreeFinish(
                    file, AFATFS_RESULT_CORRUPT_DIRECTORY);
                return;
            }
            op->depth--;
            op->phase = AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP;
            goto doMore;

        case AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP:
            status = afatfs_findNextObject(file, &op->finder, &object);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE || object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(file, &op->finder);
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (object.id.firstCluster == op->targetClusterToRetire) {
                op->currentTarget = object.id;
                op->currentTargetHasLongName = object.hasLongName;
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                return; // YIELD
            }
            return; // YIELD (process one entry per poll)

        case AFATFS_DELETE_TREE_RETIRE_ENTRIES:
            object.id = op->currentTarget;
            object.hasLongName = op->currentTargetHasLongName;
            status = afatfs_retireObjectNameRun(&object);
            if (status == AFATFS_OPERATION_IN_PROGRESS) return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            op->currentCluster = op->currentTarget.firstCluster;
            op->phase = AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS;
            goto doMore;

        case AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS:
            if (op->currentCluster == 0 || afatfs_FATIsEndOfChainMarker(op->currentCluster)) {
                if (op->currentTarget.firstCluster == op->rootId.firstCluster) {
                    op->phase = AFATFS_DELETE_TREE_SUCCESS;
                    return; // YIELD
                } else {
                    op->phase = AFATFS_DELETE_TREE_SCAN;
                    return; // YIELD
                }
            }
            uint32_t nextCluster;
            status = afatfs_FATGetNextCluster(0, op->currentCluster, &nextCluster);
            if (status != AFATFS_OPERATION_SUCCESS) {
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            status = afatfs_FATSetNextCluster(op->currentCluster, 0);
            if (status != AFATFS_OPERATION_SUCCESS) {
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            afatfs.lastClusterAllocated = MIN(afatfs.lastClusterAllocated, op->currentCluster - 1);
            op->currentCluster = nextCluster;
            return;

        case AFATFS_DELETE_TREE_SUCCESS:
            /*
             * Publish success only after all native resources are released.
             * afatfs_deleteTreeFinish() copies the callback before resetting
             * union storage, preventing the disappeared-operation/no-callback
             * condition that the outer filesystem reports as TOut06.
             */
            afatfs_deleteTreeFinish(file, AFATFS_RESULT_OK);
            return;

        default:
            /* Corrupt or unsupported phase state is a terminal driver error. */
            afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
            return;
    }
}

/*
 * Load the authoritative SFN metadata for a previously validated copy object.
 *
 * What: reads the physical SFN sector and copies the packed FAT entry into
 * operation-owned storage. Why: afatfsObjectId_t intentionally omits timestamps
 * and creation/access metadata, yet a byte-faithful tree copy should preserve
 * those supported fields. Inputs: object is the current iterator/root identity;
 * outputEntry receives a snapshot only on SUCCESS. Outputs: IN_PROGRESS yields,
 * FAILURE reports media/coordinate errors. Affiliates: object validation,
 * afatfs_copyApplyMetadata(), and direct read-handle binding.
 */
static afatfsOperationStatus_e afatfs_copyLoadSourceEntry(
        const afatfsObjectId_t *object,
        fatDirectoryEntry_t *outputEntry)
{
    uint8_t *sector;
    afatfsOperationStatus_e status;

    if (!object || !outputEntry || object->sfnEntry.entryIndex < 0 ||
        (uint16_t)object->sfnEntry.entryIndex >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        return AFATFS_OPERATION_FAILURE;
    }
    status = afatfs_cacheSector(object->sfnEntry.sectorNumberPhysical,
                                &sector, AFATFS_CACHE_READ, 0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    memcpy(outputEntry,
           &((fatDirectoryEntry_t *)sector)[object->sfnEntry.entryIndex],
           sizeof(*outputEntry));
    if (memcmp(outputEntry->filename, object->rawShortName,
               FAT_FILENAME_LENGTH) != 0) {
        /* A source-parent mutation between scan and use invalidates the key. */
        return AFATFS_OPERATION_FAILURE;
    }
    return AFATFS_OPERATION_SUCCESS;
}

/*
 * Bind a recycled private handle directly to one just-scanned source object.
 *
 * What: initializes cache indices/operation storage, imports the source SFN
 * metadata, and positions the cursor at byte zero. Why: rescanning the source
 * by display name would reintroduce duplicate-LFN ambiguity and consume another
 * asynchronous create/open state. Inputs: object supplies physical entry
 * coordinates and entry supplies the current metadata snapshot; writable is
 * false for source objects and true only for reconstructed destination parents.
 * Output: a ready file/directory cursor. Affiliates: iterator identity,
 * afatfs_fileLoadDirectoryEntry(), stream read, and compact-frame ascent.
 */
static void afatfs_copyBindObjectHandle(afatfsFilePtr_t file,
                                        const afatfsObjectId_t *object,
                                        const fatDirectoryEntry_t *entry,
                                        uint8_t writable)
{
    afatfs_initFileHandle(file);
    file->directoryEntryPos = object->sfnEntry;
    afatfs_fileLoadDirectoryEntry(file, (fatDirectoryEntry_t *)entry);
    file->mode = AFATFS_FILE_MODE_READ |
        (writable ? AFATFS_FILE_MODE_WRITE : 0u);
    file->cursorOffset = 0u;
    file->cursorCluster = file->firstCluster;
    file->cursorPreviousCluster = 0u;
}

/*
 * Reconstruct a private directory cursor from a compact traversal frame.
 *
 * What: drops all cache ownership and rebinds one openFiles[] slot to a known
 * directory cluster. Why: parent handles are not retained at every depth; the
 * frame stores only clusters and source resume identity. Inputs: firstCluster
 * is a product directory cluster (not an arbitrary path); writable selects
 * whether child creation may use the reconstructed destination parent. Output:
 * a clean cursor at entry zero with one-cluster minimum allocated size.
 * Affiliates: ASCEND_DIRECTORY, RESUME_PARENT, and child creation.
 */
static void afatfs_copyBindDirectoryCluster(afatfsFilePtr_t file,
                                            uint32_t firstCluster,
                                            uint8_t writable)
{
    afatfs_initFileHandle(file);
    file->firstCluster = firstCluster;
    file->cursorCluster = firstCluster;
    file->physicalSize = afatfs_clusterSize();
    file->attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
    file->mode = AFATFS_FILE_MODE_READ |
        (writable ? AFATFS_FILE_MODE_WRITE : 0u);
    file->type = AFATFS_FILE_TYPE_DIRECTORY;
    file->directoryEntryPos.entryIndex = -1;
}

/*
 * Preserve supported FAT metadata on one newly-created destination object.
 *
 * What: copies attributes and FAT creation/access/write timestamps while
 * deliberately retaining the destination name, cluster, and size fields.
 * Why: normal create supplies default 2015 timestamps; recursive copy promises
 * file bytes and supported metadata without aliasing source cluster ownership.
 * Inputs: destination is the live created handle and sourceEntry is the packed
 * SFN snapshot. Output: destination directory sector becomes dirty on SUCCESS.
 * Affiliates: close-time size/cluster update and directory creation.
 */
static afatfsOperationStatus_e afatfs_copyApplyMetadata(
        afatfsFilePtr_t destination,
        const fatDirectoryEntry_t *sourceEntry)
{
    uint8_t *sector;
    fatDirectoryEntry_t *destinationEntry;
    afatfsOperationStatus_e status;

    if (!destination || !sourceEntry ||
        destination->directoryEntryPos.entryIndex < 0) {
        return AFATFS_OPERATION_FAILURE;
    }
    status = afatfs_cacheSector(
        destination->directoryEntryPos.sectorNumberPhysical,
        &sector, AFATFS_CACHE_READ | AFATFS_CACHE_WRITE, 0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    destinationEntry = &((fatDirectoryEntry_t *)sector)
        [destination->directoryEntryPos.entryIndex];
    destinationEntry->attrib = sourceEntry->attrib;
    destinationEntry->creationTimeTenths = sourceEntry->creationTimeTenths;
    destinationEntry->creationTime = sourceEntry->creationTime;
    destinationEntry->creationDate = sourceEntry->creationDate;
    destinationEntry->lastAccessDate = sourceEntry->lastAccessDate;
    destinationEntry->lastWriteTime = sourceEntry->lastWriteTime;
    destinationEntry->lastWriteDate = sourceEntry->lastWriteDate;
    destination->attrib = sourceEntry->attrib;
    afatfs_cacheSectorMarkDirty(
        afatfs_getCacheDescriptorForBuffer(sector));
    return AFATFS_OPERATION_SUCCESS;
}

/* Forward declarations for callbacks invoked by existing async primitives. */
static void afatfs_copyCreated(afatfsResultCode_t result,
                               afatfsFilePtr_t file);
static void afatfs_copySourceClosed(void);
static void afatfs_copyDestinationClosed(void);
static void afatfs_copyCreatedClosed(void);

/* Release the long-lived caller-parent holds acquired by copy start. */
static void afatfs_copyReleaseParents(afatfsCopyTree_t *op)
{
    if (op->sourceParentHeld) {
        afatfs_releaseChildParent(op->sourceParent);
        op->sourceParentHeld = 0u;
    }
    if (op->destinationParentHeld) {
        afatfs_releaseChildParent(op->destinationParent);
        op->destinationParentHeld = 0u;
    }
}

/*
 * Temporarily hand the destination parent to CREATE_FILE for root creation.
 *
 * What: releases the coordinator's hold immediately before a child start; the
 * child operation then owns the same retain counter until its callback. Why:
 * parent-relative APIs correctly reject a parent already retained by another
 * owner. The same-parent branch clears only one physical counter. Output: both
 * hold bits describe the released state. Affiliates: afatfs_copyCreated().
 */
static void afatfs_copyReleaseRootParentForCreate(afatfsCopyTree_t *op)
{
    if (op->destinationParent == op->sourceParent) {
        if (op->sourceParentHeld) {
            afatfs_releaseChildParent(op->sourceParent);
            op->sourceParentHeld = 0u;
        }
        op->destinationParentHeld = 0u;
    } else if (op->destinationParentHeld) {
        afatfs_releaseChildParent(op->destinationParent);
        op->destinationParentHeld = 0u;
    }
}

/* Reacquire the coordinator hold after root create releases its child hold. */
static void afatfs_copyReacquireRootParent(afatfsCopyTree_t *op)
{
    if (op->destinationParent == op->sourceParent) {
        afatfs_assert(op->sourceParent->childOperationRetainCount == 0u);
        op->sourceParent->childOperationRetainCount = 1u;
        op->sourceParentHeld = 1u;
    } else {
        afatfs_assert(op->destinationParent->childOperationRetainCount == 0u);
        op->destinationParent->childOperationRetainCount = 1u;
        op->destinationParentHeld = 1u;
    }
}

/* Latch an error and enter the asynchronous handle-cleanup phase. */
static void afatfs_copyFail(afatfsCopyTree_t *op,
                            afatfsResultCode_t result)
{
    if (op->finderActive && op->sourceDirectory) {
        afatfs_findLastObject(op->sourceDirectory, &op->finder);
        op->finderActive = 0u;
    }
    op->terminalResult = result;
    op->phase = AFATFS_COPY_TREE_CLEANUP;
}

static void afatfs_copyCreated(afatfsResultCode_t result,
                               afatfsFilePtr_t file)
{
    afatfsCopyTree_t *op = &afatfs.treeWorkspace.operation.copy;

    /*
     * Publish child-create completion back to the coordinator.
     * The existing child terminal helper has already released its parent before
     * this callback, so root creation can safely restore the caller-parent hold.
     * No card work occurs here; the next poll applies metadata or cleans up.
     */
    if (!op->active)
        return;
    if (op->creatingRoot)
        afatfs_copyReacquireRootParent(op);
    if (result != AFATFS_RESULT_OK || !file) {
        afatfs_copyFail(op, result == AFATFS_RESULT_OK
                           ? AFATFS_RESULT_IO_ERROR : result);
        return;
    }
    op->createdHandle = file;
    op->phase = AFATFS_COPY_TREE_APPLY_METADATA;
}

static void afatfs_copySourceClosed(void)
{
    afatfsCopyTree_t *op = &afatfs.treeWorkspace.operation.copy;

    op->sourceFile = NULL;
    if (op->phase != AFATFS_COPY_TREE_CLEANUP)
        op->phase = AFATFS_COPY_TREE_CLOSE_DESTINATION_FILE;
}

static void afatfs_copyDestinationClosed(void)
{
    afatfsCopyTree_t *op = &afatfs.treeWorkspace.operation.copy;

    op->destinationFile = NULL;
    if (op->phase != AFATFS_COPY_TREE_CLEANUP) {
        op->phase = op->creatingRoot
            ? AFATFS_COPY_TREE_SYNC
            : AFATFS_COPY_TREE_SCAN_DIRECTORY;
    }
}

static void afatfs_copyCreatedClosed(void)
{
    /* Error cleanup waits for the created file/directory close to finish. */
    afatfs.treeWorkspace.operation.copy.createdHandle = NULL;
}

/*
 * Complete copy only after every transient handle and parent hold is released.
 *
 * Callback is copied before clearing the union arm so callback code may start a
 * later coordinator immediately. Partial destination objects are intentionally
 * not deleted here; abort/recovery or a diagnostic caller owns that policy.
 */
static void afatfs_copyFinish(afatfsCopyTree_t *op)
{
    afatfsResultCallback_t callback = op->callback;
    afatfsResultCode_t result = op->terminalResult;

    if (op->sourceDirectory)
        afatfs_initFileHandle(op->sourceDirectory);
    if (op->destinationDirectory)
        afatfs_initFileHandle(op->destinationDirectory);
    op->sourceDirectory = NULL;
    op->destinationDirectory = NULL;
    afatfs_copyReleaseParents(op);
    memset(op, 0, sizeof(*op));
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_NONE;
    if (callback)
        callback(result);
}

static void afatfs_copyTreeContinue(void)
{
    afatfsCopyTree_t *op = &afatfs.treeWorkspace.operation.copy;
    afatfsOperationStatus_e status;

    if (afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_COPY || !op->active)
        return;

    switch (op->phase) {
        case AFATFS_COPY_TREE_VALIDATE_SOURCE:
            status = afatfs_validateObjectId(op->sourceParent,
                                             &op->currentObject.id,
                                             &op->terminalResult);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE ||
                op->terminalResult != AFATFS_RESULT_OK) {
                afatfs_copyFail(op, status == AFATFS_OPERATION_FAILURE
                    ? AFATFS_RESULT_IO_ERROR : op->terminalResult);
                return;
            }
            op->phase = AFATFS_COPY_TREE_LOAD_SOURCE_ENTRY;
            return;

        case AFATFS_COPY_TREE_LOAD_SOURCE_ENTRY:
            status = afatfs_copyLoadSourceEntry(&op->currentObject.id,
                                                &op->sourceEntry);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_copyFail(op, AFATFS_RESULT_STALE_OBJECT);
                return;
            }
            if (op->creatingRoot) {
                afatfsFilePtr_t sourceHandle = afatfs_allocateFileHandle();

                if (!sourceHandle)
                    return;
                afatfs_copyBindObjectHandle(sourceHandle,
                                            &op->currentObject.id,
                                            &op->sourceEntry, 0u);
                if (op->currentObject.id.kind == AFATFS_OBJECT_DIRECTORY)
                    op->sourceDirectory = sourceHandle;
                else
                    op->sourceFile = sourceHandle;
                op->phase = AFATFS_COPY_TREE_CREATE_ROOT;
            } else if (op->currentObject.id.kind == AFATFS_OBJECT_FILE) {
                afatfsFilePtr_t sourceHandle = afatfs_allocateFileHandle();

                if (!sourceHandle)
                    return;
                afatfs_copyBindObjectHandle(sourceHandle,
                                            &op->currentObject.id,
                                            &op->sourceEntry, 0u);
                op->sourceFile = sourceHandle;
                op->phase = AFATFS_COPY_TREE_CREATE_CHILD;
            } else {
                op->phase = AFATFS_COPY_TREE_CREATE_CHILD;
            }
            return;

        case AFATFS_COPY_TREE_CREATE_ROOT:
        {
            bool accepted;

            /* Root create temporarily transfers the caller-parent retain. */
            afatfs_copyReleaseRootParentForCreate(op);
            op->phase = AFATFS_COPY_TREE_WAIT_CREATE_ROOT;
            if (op->currentObject.id.kind == AFATFS_OBJECT_DIRECTORY) {
                accepted = afatfs_createDirChild(
                    op->destinationParent, op->destinationName,
                    AFATFS_CREATE_NEW, AFATFS_MATCH_CASE_SENSITIVE,
                    NULL, afatfs_copyCreated);
            } else {
                accepted = afatfs_fopenChild(
                    op->destinationParent, op->destinationName, "w",
                    AFATFS_CREATE_NEW, AFATFS_MATCH_CASE_SENSITIVE,
                    NULL, afatfs_copyCreated);
            }
            if (!accepted) {
                afatfs_copyReacquireRootParent(op);
                op->phase = AFATFS_COPY_TREE_CREATE_ROOT;
            }
            return;
        }

        case AFATFS_COPY_TREE_WAIT_CREATE_ROOT:
        case AFATFS_COPY_TREE_WAIT_CREATE_CHILD:
            /* Existing child API owns progress and will change phase in callback. */
            return;

        case AFATFS_COPY_TREE_PREPARE_DIRECTORY:
            afatfs_findFirstObject(op->sourceDirectory, &op->finder);
            op->finderActive = 1u;
            op->phase = AFATFS_COPY_TREE_SCAN_DIRECTORY;
            return;

        case AFATFS_COPY_TREE_SCAN_DIRECTORY:
            status = afatfs_findNextObject(op->sourceDirectory,
                                           &op->finder,
                                           &op->currentObject);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_copyFail(op, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (op->currentObject.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op->sourceDirectory, &op->finder);
                op->finderActive = 0u;
                op->phase = op->depth == 0u
                    ? AFATFS_COPY_TREE_SYNC
                    : AFATFS_COPY_TREE_ASCEND_DIRECTORY;
                return;
            }
            if (op->currentObject.id.kind == AFATFS_OBJECT_DIRECTORY &&
                op->depth >= AFATFS_TREE_DEPTH_MAX) {
                afatfs_copyFail(op, AFATFS_RESULT_DEPTH_LIMIT);
                return;
            }
            /* Release the source cache sector while child I/O uses the cache. */
            afatfs_findLast(op->sourceDirectory);
            op->phase = AFATFS_COPY_TREE_LOAD_CHILD_ENTRY;
            return;

        case AFATFS_COPY_TREE_LOAD_CHILD_ENTRY:
            op->creatingRoot = 0u;
            op->phase = AFATFS_COPY_TREE_LOAD_SOURCE_ENTRY;
            return;

        case AFATFS_COPY_TREE_CREATE_CHILD:
            op->phase = AFATFS_COPY_TREE_WAIT_CREATE_CHILD;
            if (op->currentObject.id.kind == AFATFS_OBJECT_DIRECTORY) {
                if (!afatfs_createDirChild(
                        op->destinationDirectory,
                        op->currentObject.id.displayName,
                        AFATFS_CREATE_NEW, AFATFS_MATCH_CASE_SENSITIVE,
                        NULL, afatfs_copyCreated)) {
                    op->phase = AFATFS_COPY_TREE_CREATE_CHILD;
                }
            } else if (!afatfs_fopenChild(
                           op->destinationDirectory,
                           op->currentObject.id.displayName, "w",
                           AFATFS_CREATE_NEW, AFATFS_MATCH_CASE_SENSITIVE,
                           NULL, afatfs_copyCreated)) {
                op->phase = AFATFS_COPY_TREE_CREATE_CHILD;
            }
            return;

        case AFATFS_COPY_TREE_APPLY_METADATA:
            status = afatfs_copyApplyMetadata(op->createdHandle,
                                              &op->sourceEntry);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_copyFail(op, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (op->currentObject.id.kind == AFATFS_OBJECT_FILE) {
                op->destinationFile = op->createdHandle;
                op->createdHandle = NULL;
                op->bufferedBytes = 0u;
                op->bufferOffset = 0u;
                op->phase = AFATFS_COPY_TREE_STREAM_READ;
            } else if (op->creatingRoot) {
                op->destinationDirectory = op->createdHandle;
                op->createdHandle = NULL;
                op->phase = AFATFS_COPY_TREE_PREPARE_DIRECTORY;
            } else {
                op->phase = AFATFS_COPY_TREE_DESCEND_DIRECTORY;
            }
            return;

        case AFATFS_COPY_TREE_STREAM_READ:
        {
            uint32_t readBytes = afatfs_fread(
                op->sourceFile, afatfs.treeWorkspace.ioBuffer,
                AFATFS_TREE_IO_BUFFER_SIZE);

            if (readBytes != 0u) {
                op->bufferedBytes = (uint16_t)readBytes;
                op->bufferOffset = 0u;
                op->phase = AFATFS_COPY_TREE_STREAM_WRITE;
                return;
            }
            if (afatfs_fileIsBusy(op->sourceFile))
                return;
            if (afatfs_feof(op->sourceFile)) {
                op->phase = AFATFS_COPY_TREE_CLOSE_SOURCE_FILE;
                return;
            }
            /* Zero without EOF is cache backpressure; retry on a later poll. */
            return;
        }

        case AFATFS_COPY_TREE_STREAM_WRITE:
        {
            uint32_t remaining =
                (uint32_t)op->bufferedBytes - op->bufferOffset;
            uint32_t written = afatfs_fwrite(
                op->destinationFile,
                afatfs.treeWorkspace.ioBuffer + op->bufferOffset,
                remaining);

            if (written != 0u) {
                op->bufferOffset = (uint16_t)(op->bufferOffset + written);
                if (op->bufferOffset == op->bufferedBytes) {
                    op->bufferedBytes = 0u;
                    op->bufferOffset = 0u;
                    op->phase = AFATFS_COPY_TREE_STREAM_READ;
                }
                return;
            }
            if (afatfs_isFull()) {
                afatfs_copyFail(op, AFATFS_RESULT_NO_SPACE);
                return;
            }
            /* Busy handle/cache produces a zero write and is retried. */
            return;
        }

        case AFATFS_COPY_TREE_CLOSE_SOURCE_FILE:
            if (afatfs_fclose(op->sourceFile, afatfs_copySourceClosed))
                return;
            return;

        case AFATFS_COPY_TREE_CLOSE_DESTINATION_FILE:
            if (afatfs_fclose(op->destinationFile,
                              afatfs_copyDestinationClosed))
                return;
            return;

        case AFATFS_COPY_TREE_DESCEND_DIRECTORY:
        {
            afatfsCopyTreeFrame_t *frame = &op->frames[op->depth];

            /*
             * Capture only the physical resume key before replacing both
             * parent cursors. The 11-byte raw SFN comparison protects against
             * a reused sector slot even though copy itself never mutates source.
             */
            frame->sourceParentCluster = op->sourceDirectory->firstCluster;
            frame->destinationParentCluster =
                op->destinationDirectory->firstCluster;
            frame->sourceChildEntry = op->currentObject.id.sfnEntry;
            memcpy(frame->rawShortName, op->currentObject.id.rawShortName,
                   FAT_FILENAME_LENGTH);
            afatfs_findLastObject(op->sourceDirectory, &op->finder);
            op->finderActive = 0u;

            afatfs_copyBindObjectHandle(op->sourceDirectory,
                                        &op->currentObject.id,
                                        &op->sourceEntry, 0u);
            afatfs_initFileHandle(op->destinationDirectory);
            op->destinationDirectory = op->createdHandle;
            op->createdHandle = NULL;
            op->depth++;
            op->phase = AFATFS_COPY_TREE_PREPARE_DIRECTORY;
            return;
        }

        case AFATFS_COPY_TREE_ASCEND_DIRECTORY:
        {
            afatfsCopyTreeFrame_t *frame;

            if (op->depth == 0u) {
                afatfs_copyFail(op, AFATFS_RESULT_CORRUPT_DIRECTORY);
                return;
            }
            op->depth--;
            frame = &op->frames[op->depth];
            afatfs_copyBindDirectoryCluster(op->sourceDirectory,
                                            frame->sourceParentCluster, 0u);
            afatfs_copyBindDirectoryCluster(op->destinationDirectory,
                                            frame->destinationParentCluster, 1u);
            afatfs_findFirstObject(op->sourceDirectory, &op->finder);
            op->finderActive = 1u;
            op->phase = AFATFS_COPY_TREE_RESUME_PARENT;
            return;
        }

        case AFATFS_COPY_TREE_RESUME_PARENT:
        {
            afatfsCopyTreeFrame_t *frame = &op->frames[op->depth];

            status = afatfs_findNextObject(op->sourceDirectory,
                                           &op->finder,
                                           &op->currentObject);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE ||
                op->currentObject.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_copyFail(op, status == AFATFS_OPERATION_FAILURE
                    ? AFATFS_RESULT_IO_ERROR : AFATFS_RESULT_STALE_OBJECT);
                return;
            }
            if (op->currentObject.id.sfnEntry.sectorNumberPhysical ==
                    frame->sourceChildEntry.sectorNumberPhysical &&
                op->currentObject.id.sfnEntry.entryIndex ==
                    frame->sourceChildEntry.entryIndex &&
                memcmp(op->currentObject.id.rawShortName,
                       frame->rawShortName, FAT_FILENAME_LENGTH) == 0) {
                /* Finder now points immediately after the completed child. */
                op->phase = AFATFS_COPY_TREE_SCAN_DIRECTORY;
            }
            return;
        }

        case AFATFS_COPY_TREE_SYNC:
            if ((op->flags & AFATFS_COPY_DURABLE) != 0u && !afatfs_sync())
                return;
            op->terminalResult = AFATFS_RESULT_OK;
            op->phase = AFATFS_COPY_TREE_CLEANUP;
            return;

        case AFATFS_COPY_TREE_CLEANUP:
            if (op->createdHandle) {
                if (!afatfs_fclose(op->createdHandle,
                                   afatfs_copyCreatedClosed))
                    return;
                return;
            }
            if (op->sourceFile) {
                if (!afatfs_fclose(op->sourceFile, afatfs_copySourceClosed))
                    return;
                return;
            }
            if (op->destinationFile) {
                if (!afatfs_fclose(op->destinationFile,
                                   afatfs_copyDestinationClosed))
                    return;
                return;
            }
            afatfs_copyFinish(op);
            return;

        default:
            afatfs_copyFail(op, AFATFS_RESULT_IO_ERROR);
            return;
    }
}

bool afatfs_copyObjectTree(afatfsDirHandle_t sourceParent,
                           const afatfsObjectId_t *source,
                           afatfsDirHandle_t destinationParent,
                           const char *destinationName,
                           afatfsCopyFlags_t flags,
                           afatfsResultCallback_t complete)
{
    afatfsCopyTree_t *op = &afatfs.treeWorkspace.operation.copy;

    /*
     * Accept and fully copy one recursive-copy request.
     *
     * Validation precedes parent retention, so false has no side effects and no
     * callback. On acceptance both explicit parents are exclusively held (one
     * counter when they are identical), source identity/name/flags are copied,
     * and the poll coordinator owns all later callbacks. Destination text is
     * sanitized once into operation storage so caller menu buffers may change.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_NONE ||
        afatfs.renameObject.active ||
        afatfs.removeObjects.active || !source ||
        source->kind == AFATFS_OBJECT_NONE ||
        !afatfs_parentCanAcceptChild(sourceParent) ||
        !afatfs_parentCanAcceptChild(destinationParent) ||
        !afatfs_childComponentCanStart(destinationName) ||
        (flags & (afatfsCopyFlags_t)~AFATFS_COPY_DURABLE) != 0u) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    if (afatfs_copySanitizedLongName(op->destinationName,
                                     destinationName) == 0u) {
        return false;
    }
    op->active = 1u;
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_COPY;
    op->creatingRoot = 1u;
    op->phase = AFATFS_COPY_TREE_VALIDATE_SOURCE;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    op->flags = flags;
    op->callback = complete;
    op->sourceParent = sourceParent;
    op->destinationParent = destinationParent;
    op->currentObject.id = *source;
    op->currentObject.hasLongName = source->lfnEntryCount != 0u;

    sourceParent->childOperationRetainCount = 1u;
    op->sourceParentHeld = 1u;
    if (destinationParent != sourceParent) {
        destinationParent->childOperationRetainCount = 1u;
        op->destinationParentHeld = 1u;
    }
    return true;
}

/*
 * Compute the journal's standard reflected CRC-32 (polynomial 0xEDB88320).
 *
 * Each input byte is XORed into the low accumulator byte, then eight reflected
 * shifts fold one source bit at a time. The mask arithmetic applies the
 * polynomial only when the discarded low bit was one, avoiding a branch in the
 * important mathematical loop. Input excludes the crc32 field itself; output
 * is the complemented 32-bit checksum used by both journal slots.
 */
static uint32_t afatfs_replaceCrc32(const uint8_t *bytes, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;

    while (length-- != 0u) {
        uint8_t bit;

        crc ^= *bytes++;
        for (bit = 0u; bit < 8u; bit++) {
            uint32_t lowBitMask = 0u - (crc & 1u);

            crc = (crc >> 1u) ^ (0xEDB88320u & lowBitMask);
        }
    }
    return ~crc;
}

/* Regenerate reserved scratch names from the journal's compact nonce. */
static void afatfs_replaceScratchName(uint32_t nonce,
                                      uint8_t oldName,
                                      char output[AFATFS_LONG_FILENAME_MAX + 1u])
{
    static const char hex[] = "0123456789abcdef";
    uint8_t digit;

    memcpy(output, ".afat-", 6u);
    /* Write most-significant hexadecimal digit first for stable host display. */
    for (digit = 0u; digit < 8u; digit++) {
        uint8_t shift = (uint8_t)((7u - digit) * 4u);

        output[6u + digit] = hex[(nonce >> shift) & 0x0Fu];
    }
    memcpy(&output[14], oldName ? "-old" : "-new", 5u);
}

/* Reserved-name recognizer used only to report unauthorised orphan scratch. */
static uint8_t afatfs_replaceIsScratchName(const char *name)
{
    uint8_t i;

    if (!name || strncmp(name, ".afat-", 6u) != 0)
        return 0u;
    for (i = 6u; i < 14u; i++) {
        char c = name[i];

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return 0u;
    }
    return (uint8_t)(strcmp(&name[14], "-new") == 0 ||
                     strcmp(&name[14], "-old") == 0);
}

/* Validate one complete journal record without trusting any embedded field. */
static uint8_t afatfs_replaceJournalValid(
        const afatfsReplaceJournalRecord_t *record)
{
    uint32_t expectedCrc;
    uint8_t nameIndex;

    if (!record || record->magic != AFATFS_REPLACE_JOURNAL_MAGIC ||
        record->version != AFATFS_REPLACE_JOURNAL_VERSION ||
        record->objectKind != AFATFS_OBJECT_DIRECTORY ||
        record->state > AFATFS_REPLACE_JOURNAL_PROMOTED ||
        record->targetName[0] == '\0' ||
        record->targetName[AFATFS_LONG_FILENAME_MAX] != '\0') {
        return 0u;
    }
    /*
     * A CRC-valid but non-component target must not be allowed to redirect a
     * recovery rename through sanitation. Begin stores canonical FAT LFN text,
     * so recovery accepts only allowed characters and trimmed endings.
     */
    for (nameIndex = 0u;
         nameIndex < AFATFS_LONG_FILENAME_MAX &&
         record->targetName[nameIndex] != '\0';
         nameIndex++) {
        if (!fat_lfnCharAllowed(record->targetName[nameIndex]))
            return 0u;
    }
    if (nameIndex == 0u ||
        record->targetName[nameIndex - 1u] == ' ' ||
        record->targetName[nameIndex - 1u] == '.' ||
        strcmp(record->targetName, ".") == 0 ||
        strcmp(record->targetName, "..") == 0 ||
        afatfs_replaceIsScratchName(record->targetName) ||
        strcmp(record->targetName, AFATFS_REPLACE_JOURNAL_SLOT0) == 0 ||
        strcmp(record->targetName, AFATFS_REPLACE_JOURNAL_SLOT1) == 0) {
        return 0u;
    }
    expectedCrc = afatfs_replaceCrc32(
        (const uint8_t *)record,
        (uint32_t)(sizeof(*record) - sizeof(record->crc32)));
    return expectedCrc == record->crc32 ? 1u : 0u;
}

static void afatfs_replaceReacquireParent(afatfsReplaceOperation_t *op)
{
    if (!op->parentHeld) {
        afatfs_assert(op->parent->childOperationRetainCount == 0u);
        op->parent->childOperationRetainCount = 1u;
        op->parentHeld = 1u;
    }
}

static void afatfs_replaceReleaseParent(afatfsReplaceOperation_t *op)
{
    if (op->parentHeld) {
        afatfs_releaseChildParent(op->parent);
        op->parentHeld = 0u;
    }
}

/* Copy one component without retaining a caller-owned string across polls. */
static void afatfs_replaceCopyName(
        char destination[AFATFS_LONG_FILENAME_MAX + 1u],
        const char *source)
{
    uint8_t i = 0u;

    while (i < AFATFS_LONG_FILENAME_MAX && source && source[i] != '\0') {
        destination[i] = source[i];
        i++;
    }
    destination[i] = '\0';
}

static void afatfs_replaceFail(afatfsReplaceOperation_t *op,
                               afatfsResultCode_t result)
{
    if (op->finderActive) {
        afatfs_findLastObject(op->parent, &op->finder);
        op->finderActive = 0u;
    }
    op->terminalResult = result;
    op->phase = AFATFS_REPLACE_FINISH;
}

/*
 * End one replace operation and transfer or release transaction ownership.
 *
 * Commit/abort failures deliberately return the parent hold to the still-live
 * transaction so the caller can request recovery; success releases the parent
 * and clears the opaque descriptor. Standalone recovery always releases its
 * caller parent. Callbacks are copied before the workspace union is cleared so
 * they may immediately start another operation.
 */
static void afatfs_replaceFinish(afatfsReplaceOperation_t *op)
{
    afatfsReplaceBeginCallback_t beginCallback = op->beginCallback;
    afatfsResultCallback_t resultCallback = op->resultCallback;
    afatfsReplaceTransactionPtr_t transaction = op->transaction;
    afatfsReplaceMode_e mode = op->mode;
    afatfsResultCode_t result = op->terminalResult;
    afatfsDirHandle_t staging = transaction
        ? transaction->stagingDirectory : NULL;

    if (op->finderActive) {
        afatfs_findLastObject(op->parent, &op->finder);
        op->finderActive = 0u;
    }
    if (mode == AFATFS_REPLACE_MODE_COMMIT ||
        mode == AFATFS_REPLACE_MODE_ABORT) {
        if (transaction && result != AFATFS_RESULT_OK) {
            transaction->parentHeld = op->parentHeld;
            op->parentHeld = 0u;
        } else {
            afatfs_replaceReleaseParent(op);
            if (transaction)
                memset(transaction, 0, sizeof(*transaction));
        }
    } else if (mode == AFATFS_REPLACE_MODE_RECOVER) {
        afatfs_replaceReleaseParent(op);
    } else if (mode == AFATFS_REPLACE_MODE_BEGIN &&
               result != AFATFS_RESULT_OK && transaction) {
        memset(transaction, 0, sizeof(*transaction));
    }

    memset(op, 0, sizeof(*op));
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_NONE;
    if (mode == AFATFS_REPLACE_MODE_BEGIN && beginCallback) {
        beginCallback(result,
                      result == AFATFS_RESULT_OK ? transaction : NULL,
                      result == AFATFS_RESULT_OK ? staging : NULL);
    } else if (resultCallback) {
        resultCallback(result);
    }
}

static void afatfs_replaceStartScan(afatfsReplaceOperation_t *op,
                                    const char *name,
                                    afatfsReplaceAction_e action)
{
    afatfs_replaceCopyName(op->scanName, name ? name : "");
    op->action = action;
    op->found = 0u;
    op->phase = AFATFS_REPLACE_SCAN_BEGIN;
}

/* Build the next alternating journal record in the shared byte buffer. */
static void afatfs_replaceQueueJournal(afatfsReplaceOperation_t *op,
                                       afatfsReplaceJournalState_e state)
{
    afatfsReplaceJournalRecord_t *record =
        (afatfsReplaceJournalRecord_t *)afatfs.treeWorkspace.ioBuffer;
    const char *targetName = op->transaction
        ? op->transaction->targetName : op->bestJournal.targetName;
    uint32_t nonce = op->transaction
        ? op->transaction->nonce : op->bestJournal.nonce;

    memset(record, 0, sizeof(*record));
    record->magic = AFATFS_REPLACE_JOURNAL_MAGIC;
    record->version = AFATFS_REPLACE_JOURNAL_VERSION;
    record->state = (uint8_t)state;
    record->objectKind = AFATFS_OBJECT_DIRECTORY;
    record->sequence = op->journalValid
        ? op->bestJournal.sequence + 1u : 1u;
    record->nonce = nonce;
    afatfs_replaceCopyName(record->targetName, targetName);
    record->crc32 = afatfs_replaceCrc32(
        (const uint8_t *)record,
        (uint32_t)(sizeof(*record) - sizeof(record->crc32)));

    /* Low sequence bit alternates slots; a torn write leaves the other valid. */
    op->journalSlot = (uint8_t)(record->sequence & 1u);
    op->ioOffset = 0u;
    op->phase = AFATFS_REPLACE_WRITE_JOURNAL_OPEN;
}

static void afatfs_replaceStageCreated(afatfsResultCode_t result,
                                       afatfsFilePtr_t directory)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;
    afatfsReplaceTransactionPtr_t transaction = op->transaction;

    if (afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_REPLACE ||
        !op->active || op->mode != AFATFS_REPLACE_MODE_BEGIN) {
        return;
    }
    if (result == AFATFS_RESULT_ALREADY_EXISTS) {
        /* CREATE_NEW collision belongs to an older nonce; never reopen it. */
        transaction->nonce++;
        if (transaction->nonce == 0u)
            transaction->nonce = 1u;
        op->phase = AFATFS_REPLACE_BEGIN_CREATE_STAGE;
        return;
    }
    if (result != AFATFS_RESULT_OK || !directory) {
        afatfs_replaceFail(op, result == AFATFS_RESULT_OK
            ? AFATFS_RESULT_IO_ERROR : result);
        return;
    }

    transaction->stagingDirectory = directory;
    transaction->parent->childOperationRetainCount = 1u;
    transaction->parentHeld = 1u;
    op->terminalResult = AFATFS_RESULT_OK;
    op->phase = AFATFS_REPLACE_FINISH;
}

static void afatfs_replaceStageClosed(void)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    if (op->transaction)
        op->transaction->stagingDirectory = NULL;
    if (op->mode == AFATFS_REPLACE_MODE_COMMIT) {
        op->phase = AFATFS_REPLACE_SYNC_STAGE;
    } else if (op->mode == AFATFS_REPLACE_MODE_ABORT &&
               op->transaction && !op->transaction->commitStarted) {
        afatfs_replaceScratchName(op->transaction->nonce, 0u, op->scanName);
        afatfs_replaceStartScan(op, op->scanName,
                                AFATFS_REPLACE_ACTION_ABORT_DELETE_NEW);
    } else {
        op->journalSlot = 0u;
        op->phase = AFATFS_REPLACE_READ_JOURNAL_OPEN;
    }
}

static void afatfs_replaceJournalOpened(afatfsResultCode_t result,
                                        afatfsFilePtr_t file)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    afatfs_replaceReacquireParent(op);
    if (op->phase == AFATFS_REPLACE_READ_JOURNAL_WAIT_OPEN) {
        if (result == AFATFS_RESULT_NOT_FOUND) {
            op->phase = AFATFS_REPLACE_READ_JOURNAL_AFTER_CLOSE;
            return;
        }
        if (result != AFATFS_RESULT_OK || !file) {
            afatfs_replaceFail(
                op, result == AFATFS_RESULT_IO_ERROR
                    ? AFATFS_RESULT_IO_ERROR
                    : AFATFS_RESULT_RECOVERY_REQUIRED);
            return;
        }
        /* Existing short/torn records are distinguished from absent journals. */
        op->journalSeen = 1u;
        op->journalFile = file;
        op->ioOffset = 0u;
        memset(afatfs.treeWorkspace.ioBuffer, 0,
               sizeof(afatfs.treeWorkspace.ioBuffer));
        op->phase = AFATFS_REPLACE_READ_JOURNAL_DATA;
    } else if (op->phase == AFATFS_REPLACE_WRITE_JOURNAL_WAIT_OPEN) {
        if (result != AFATFS_RESULT_OK || !file) {
            afatfs_replaceFail(op, result == AFATFS_RESULT_OK
                ? AFATFS_RESULT_IO_ERROR : result);
            return;
        }
        op->journalFile = file;
        op->ioOffset = 0u;
        op->phase = AFATFS_REPLACE_WRITE_JOURNAL_DATA;
    }
}

static void afatfs_replaceJournalClosed(void)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    op->journalFile = NULL;
    if (op->phase == AFATFS_REPLACE_READ_JOURNAL_CLOSE)
        op->phase = AFATFS_REPLACE_READ_JOURNAL_AFTER_CLOSE;
    else if (op->phase == AFATFS_REPLACE_WRITE_JOURNAL_CLOSE)
        op->phase = AFATFS_REPLACE_WRITE_JOURNAL_AFTER_CLOSE;
}

static void afatfs_replaceMoveDone(afatfsResultCode_t result)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    afatfs_replaceReacquireParent(op);
    if (result != AFATFS_RESULT_OK) {
        afatfs_replaceFail(op, result);
        return;
    }
    op->phase = AFATFS_REPLACE_SYNC_NAMESPACE;
}

static void afatfs_replaceDeleteDone(afatfsResultCode_t result)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    if (result != AFATFS_RESULT_OK) {
        afatfs_replaceFail(op, result);
        return;
    }
    op->phase = AFATFS_REPLACE_SYNC_NAMESPACE;
}

/* Apply the selected valid journal state after both slots have been read. */
static void afatfs_replaceBeginRecovery(afatfsReplaceOperation_t *op)
{
    if (!op->journalValid) {
        if (op->journalSeen) {
            /* Corrupt slots authorize no cleanup, even if scratch is absent. */
            op->terminalResult = AFATFS_RESULT_RECOVERY_REQUIRED;
            op->phase = AFATFS_REPLACE_FINISH;
            return;
        }
        /* No record authorizes mutation; detect but preserve reserved orphans. */
        afatfs_replaceStartScan(
            op, NULL, AFATFS_REPLACE_ACTION_CHECK_UNKNOWN_SCRATCH);
        return;
    }
    if (op->bestJournal.state == AFATFS_REPLACE_JOURNAL_CLEAN) {
        if (op->mode == AFATFS_REPLACE_MODE_ABORT && op->transaction) {
            char newName[AFATFS_LONG_FILENAME_MAX + 1u];

            /* A torn PREPARED write leaves the prior CLEAN record authoritative. */
            afatfs_replaceScratchName(op->transaction->nonce, 0u, newName);
            afatfs_replaceStartScan(
                op, newName, AFATFS_REPLACE_ACTION_ABORT_DELETE_NEW);
        } else {
            /* CLEAN plus any scratch is suspicious but never authorizes deletion. */
            afatfs_replaceStartScan(
                op, NULL, AFATFS_REPLACE_ACTION_CHECK_UNKNOWN_SCRATCH);
        }
        return;
    }
    afatfs_replaceStartScan(
        op, op->bestJournal.targetName,
        AFATFS_REPLACE_ACTION_RECOVER_CHECK_TARGET);
}

/*
 * Convert one completed parent scan into the next recovery/commit primitive.
 *
 * Every branch acts only on foundObject from the explicit parent and on names
 * derived from the validated journal/active transaction. Missing objects are
 * interpreted by journal state; unrelated objects are never deleted.
 */
static void afatfs_replaceHandleScanResult(afatfsReplaceOperation_t *op)
{
    char name[AFATFS_LONG_FILENAME_MAX + 1u];

    switch (op->action) {
        case AFATFS_REPLACE_ACTION_COMMIT_CHECK_OLD:
            if (op->found) {
                /* A stale old scratch must never be adopted by this nonce. */
                afatfs_replaceFail(op, AFATFS_RESULT_ALREADY_EXISTS);
            } else {
                op->transaction->commitStarted = 1u;
                afatfs_replaceQueueJournal(
                    op, AFATFS_REPLACE_JOURNAL_PREPARED);
            }
            return;

        case AFATFS_REPLACE_ACTION_COMMIT_FIND_TARGET:
            if (op->found) {
                if (op->foundObject.id.kind != AFATFS_OBJECT_DIRECTORY) {
                    afatfs_replaceFail(op, AFATFS_RESULT_NOT_DIRECTORY);
                    return;
                }
                op->phase = AFATFS_REPLACE_MOVE_FOUND;
            } else {
                afatfs_replaceQueueJournal(
                    op, AFATFS_REPLACE_JOURNAL_OLD_RENAMED);
            }
            return;

        case AFATFS_REPLACE_ACTION_COMMIT_PROMOTE_NEW:
            if (!op->found) {
                afatfs_replaceFail(op, AFATFS_RESULT_RECOVERY_REQUIRED);
                return;
            }
            op->phase = AFATFS_REPLACE_MOVE_FOUND;
            return;

        case AFATFS_REPLACE_ACTION_COMMIT_DELETE_OLD:
            if (op->found) {
                op->phase = AFATFS_REPLACE_DELETE_FOUND;
            } else {
                afatfs_replaceQueueJournal(
                    op, AFATFS_REPLACE_JOURNAL_CLEAN);
            }
            return;

        case AFATFS_REPLACE_ACTION_ABORT_DELETE_NEW:
            if (op->found) {
                op->phase = AFATFS_REPLACE_DELETE_FOUND;
            } else {
                op->terminalResult = AFATFS_RESULT_OK;
                op->phase = AFATFS_REPLACE_FINISH;
            }
            return;

        case AFATFS_REPLACE_ACTION_CHECK_UNKNOWN_SCRATCH:
            op->terminalResult = op->found
                ? AFATFS_RESULT_RECOVERY_REQUIRED : AFATFS_RESULT_OK;
            op->phase = AFATFS_REPLACE_FINISH;
            return;

        case AFATFS_REPLACE_ACTION_RECOVER_CHECK_TARGET:
            if (op->found) {
                if (op->foundObject.id.kind != AFATFS_OBJECT_DIRECTORY) {
                    afatfs_replaceFail(op, AFATFS_RESULT_NOT_DIRECTORY);
                    return;
                }
                if (op->bestJournal.state ==
                        AFATFS_REPLACE_JOURNAL_PREPARED) {
                    afatfs_replaceScratchName(op->bestJournal.nonce, 0u, name);
                    afatfs_replaceStartScan(
                        op, name, AFATFS_REPLACE_ACTION_RECOVER_DELETE_NEW);
                } else if (op->bestJournal.state ==
                               AFATFS_REPLACE_JOURNAL_OLD_RENAMED) {
                    /* Promotion may have completed before its journal update. */
                    afatfs_replaceQueueJournal(
                        op, AFATFS_REPLACE_JOURNAL_PROMOTED);
                } else {
                    afatfs_replaceScratchName(op->bestJournal.nonce, 1u, name);
                    afatfs_replaceStartScan(
                        op, name, AFATFS_REPLACE_ACTION_RECOVER_DELETE_OLD);
                }
            } else if (op->bestJournal.state ==
                           AFATFS_REPLACE_JOURNAL_PREPARED) {
                /* Crash between old rename and OLD_RENAMED write: restore old. */
                afatfs_replaceScratchName(op->bestJournal.nonce, 1u, name);
                afatfs_replaceStartScan(
                    op, name, AFATFS_REPLACE_ACTION_RECOVER_FIND_OLD);
            } else {
                afatfs_replaceScratchName(op->bestJournal.nonce, 0u, name);
                afatfs_replaceStartScan(
                    op, name, AFATFS_REPLACE_ACTION_RECOVER_FIND_NEW);
            }
            return;

        case AFATFS_REPLACE_ACTION_RECOVER_FIND_NEW:
            if (op->found) {
                op->phase = AFATFS_REPLACE_MOVE_FOUND;
            } else {
                afatfs_replaceScratchName(op->bestJournal.nonce, 1u, name);
                afatfs_replaceStartScan(
                    op, name, AFATFS_REPLACE_ACTION_RECOVER_FIND_OLD);
            }
            return;

        case AFATFS_REPLACE_ACTION_RECOVER_FIND_OLD:
            if (!op->found) {
                afatfs_replaceFail(op, AFATFS_RESULT_RECOVERY_REQUIRED);
                return;
            }
            op->phase = AFATFS_REPLACE_MOVE_FOUND;
            return;

        case AFATFS_REPLACE_ACTION_RECOVER_DELETE_NEW:
        case AFATFS_REPLACE_ACTION_RECOVER_DELETE_OLD:
            if (op->found)
                op->phase = AFATFS_REPLACE_DELETE_FOUND;
            else
                afatfs_replaceQueueJournal(
                    op, AFATFS_REPLACE_JOURNAL_CLEAN);
            return;

        default:
            afatfs_replaceFail(op, AFATFS_RESULT_IO_ERROR);
            return;
    }
}

/* Select the destination component for the current exact-object move. */
static void afatfs_replaceMoveDestination(afatfsReplaceOperation_t *op,
                                          char output[AFATFS_LONG_FILENAME_MAX + 1u])
{
    if (op->action == AFATFS_REPLACE_ACTION_COMMIT_FIND_TARGET) {
        afatfs_replaceScratchName(op->transaction->nonce, 1u, output);
    } else if (op->transaction) {
        afatfs_replaceCopyName(output, op->transaction->targetName);
    } else {
        afatfs_replaceCopyName(output, op->bestJournal.targetName);
    }
}

/* Continue after a namespace sync according to the move/delete just completed. */
static void afatfs_replaceAfterNamespaceSync(afatfsReplaceOperation_t *op)
{
    char name[AFATFS_LONG_FILENAME_MAX + 1u];

    switch (op->action) {
        case AFATFS_REPLACE_ACTION_COMMIT_FIND_TARGET:
            afatfs_replaceQueueJournal(
                op, AFATFS_REPLACE_JOURNAL_OLD_RENAMED);
            return;
        case AFATFS_REPLACE_ACTION_COMMIT_PROMOTE_NEW:
        case AFATFS_REPLACE_ACTION_RECOVER_FIND_NEW:
            afatfs_replaceQueueJournal(
                op, AFATFS_REPLACE_JOURNAL_PROMOTED);
            return;
        case AFATFS_REPLACE_ACTION_COMMIT_DELETE_OLD:
        case AFATFS_REPLACE_ACTION_RECOVER_DELETE_NEW:
        case AFATFS_REPLACE_ACTION_RECOVER_DELETE_OLD:
            afatfs_replaceQueueJournal(op, AFATFS_REPLACE_JOURNAL_CLEAN);
            return;
        case AFATFS_REPLACE_ACTION_ABORT_DELETE_NEW:
            op->terminalResult = AFATFS_RESULT_OK;
            op->phase = AFATFS_REPLACE_FINISH;
            return;
        case AFATFS_REPLACE_ACTION_RECOVER_FIND_OLD:
            /* Old is authoritative again; remove exact staging before CLEAN. */
            afatfs_replaceScratchName(op->bestJournal.nonce, 0u, name);
            afatfs_replaceStartScan(
                op, name, AFATFS_REPLACE_ACTION_RECOVER_DELETE_NEW);
            return;
        default:
            afatfs_replaceFail(op, AFATFS_RESULT_IO_ERROR);
            return;
    }
}

/* Continue after a durable journal write according to its newly-valid state. */
static void afatfs_replaceAfterJournalSync(afatfsReplaceOperation_t *op)
{
    char name[AFATFS_LONG_FILENAME_MAX + 1u];

    switch ((afatfsReplaceJournalState_e)op->bestJournal.state) {
        case AFATFS_REPLACE_JOURNAL_PREPARED:
            afatfs_replaceStartScan(
                op, op->bestJournal.targetName,
                AFATFS_REPLACE_ACTION_COMMIT_FIND_TARGET);
            return;
        case AFATFS_REPLACE_JOURNAL_OLD_RENAMED:
            afatfs_replaceScratchName(op->bestJournal.nonce, 0u, name);
            afatfs_replaceStartScan(
                op, name, AFATFS_REPLACE_ACTION_COMMIT_PROMOTE_NEW);
            return;
        case AFATFS_REPLACE_JOURNAL_PROMOTED:
            afatfs_replaceScratchName(op->bestJournal.nonce, 1u, name);
            afatfs_replaceStartScan(
                op, name,
                op->mode == AFATFS_REPLACE_MODE_COMMIT
                    ? AFATFS_REPLACE_ACTION_COMMIT_DELETE_OLD
                    : AFATFS_REPLACE_ACTION_RECOVER_DELETE_OLD);
            return;
        case AFATFS_REPLACE_JOURNAL_CLEAN:
            op->terminalResult = AFATFS_RESULT_OK;
            op->phase = AFATFS_REPLACE_FINISH;
            return;
        default:
            afatfs_replaceFail(op, AFATFS_RESULT_IO_ERROR);
            return;
    }
}

static void afatfs_replaceContinue(void)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;
    afatfsOperationStatus_e status;
    const char *journalName;

    if (afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_REPLACE ||
        !op->active) {
        return;
    }

    switch (op->phase) {
        case AFATFS_REPLACE_BEGIN_CREATE_STAGE:
            afatfs_replaceScratchName(op->transaction->nonce, 0u,
                                      op->scanName);
            op->phase = AFATFS_REPLACE_BEGIN_WAIT_STAGE;
            if (!afatfs_createDirChild(
                    op->parent, op->scanName, AFATFS_CREATE_NEW,
                    AFATFS_MATCH_CASE_SENSITIVE, NULL,
                    afatfs_replaceStageCreated)) {
                op->phase = AFATFS_REPLACE_BEGIN_CREATE_STAGE;
            }
            return;

        case AFATFS_REPLACE_BEGIN_WAIT_STAGE:
        case AFATFS_REPLACE_WAIT_CLOSE_STAGE:
        case AFATFS_REPLACE_READ_JOURNAL_WAIT_OPEN:
        case AFATFS_REPLACE_WRITE_JOURNAL_WAIT_OPEN:
        case AFATFS_REPLACE_WAIT_MOVE:
        case AFATFS_REPLACE_WAIT_DELETE:
            /* An existing async primitive owns progress until its callback. */
            return;

        case AFATFS_REPLACE_CLOSE_STAGE:
            if (!op->transaction || !op->transaction->stagingDirectory) {
                afatfs_replaceStageClosed();
                return;
            }
            op->phase = AFATFS_REPLACE_WAIT_CLOSE_STAGE;
            if (!afatfs_fclose(op->transaction->stagingDirectory,
                               afatfs_replaceStageClosed)) {
                op->phase = AFATFS_REPLACE_CLOSE_STAGE;
            }
            return;

        case AFATFS_REPLACE_SYNC_STAGE:
            /* PREPARED is never written until every payload sector is durable. */
            if (!afatfs_sync())
                return;
            op->journalSlot = 0u;
            op->journalValid = 0u;
            op->phase = AFATFS_REPLACE_READ_JOURNAL_OPEN;
            return;

        case AFATFS_REPLACE_READ_JOURNAL_OPEN:
            if (op->journalSlot >= 2u) {
                op->phase = AFATFS_REPLACE_AFTER_JOURNALS;
                return;
            }
            journalName = op->journalSlot == 0u
                ? AFATFS_REPLACE_JOURNAL_SLOT0
                : AFATFS_REPLACE_JOURNAL_SLOT1;
            afatfs_replaceReleaseParent(op);
            op->phase = AFATFS_REPLACE_READ_JOURNAL_WAIT_OPEN;
            if (!afatfs_fopenChild(
                    op->parent, journalName, "r", AFATFS_OPEN_EXISTING,
                    AFATFS_MATCH_CASE_SENSITIVE, NULL,
                    afatfs_replaceJournalOpened)) {
                afatfs_replaceReacquireParent(op);
                op->phase = AFATFS_REPLACE_READ_JOURNAL_OPEN;
            }
            return;

        case AFATFS_REPLACE_READ_JOURNAL_DATA:
        {
            uint32_t remaining = sizeof(afatfsReplaceJournalRecord_t) -
                                 op->ioOffset;
            uint32_t read = afatfs_fread(
                op->journalFile,
                afatfs.treeWorkspace.ioBuffer + op->ioOffset,
                remaining);

            if (read != 0u) {
                op->ioOffset = (uint16_t)(op->ioOffset + read);
                if (op->ioOffset == sizeof(afatfsReplaceJournalRecord_t)) {
                    afatfsReplaceJournalRecord_t *record =
                        (afatfsReplaceJournalRecord_t *)
                            afatfs.treeWorkspace.ioBuffer;

                    if (afatfs_replaceJournalValid(record) &&
                        (!op->journalValid ||
                         record->sequence > op->bestJournal.sequence)) {
                        op->bestJournal = *record;
                        op->journalValid = 1u;
                    } else if (afatfs_replaceJournalValid(record) &&
                               op->journalValid &&
                               record->sequence ==
                                   op->bestJournal.sequence &&
                               memcmp(record, &op->bestJournal,
                                      sizeof(*record)) != 0) {
                        /* Two valid but conflicting equal generations are unsafe. */
                        op->journalAmbiguous = 1u;
                    }
                    op->phase = AFATFS_REPLACE_READ_JOURNAL_CLOSE;
                }
                return;
            }
            if (afatfs_fileIsBusy(op->journalFile))
                return;
            if (afatfs_feof(op->journalFile)) {
                /* A short/torn slot is ignored; the alternate slot remains. */
                op->phase = AFATFS_REPLACE_READ_JOURNAL_CLOSE;
            }
            return;
        }

        case AFATFS_REPLACE_READ_JOURNAL_CLOSE:
            if (!afatfs_fclose(op->journalFile,
                               afatfs_replaceJournalClosed)) {
                return;
            }
            return;

        case AFATFS_REPLACE_READ_JOURNAL_AFTER_CLOSE:
            op->journalSlot++;
            op->phase = op->journalSlot < 2u
                ? AFATFS_REPLACE_READ_JOURNAL_OPEN
                : AFATFS_REPLACE_AFTER_JOURNALS;
            return;

        case AFATFS_REPLACE_AFTER_JOURNALS:
            if (op->journalAmbiguous) {
                afatfs_replaceFail(op, AFATFS_RESULT_RECOVERY_REQUIRED);
                return;
            }
            if (op->mode == AFATFS_REPLACE_MODE_COMMIT) {
                char oldName[AFATFS_LONG_FILENAME_MAX + 1u];

                if ((op->journalValid &&
                     op->bestJournal.state !=
                         AFATFS_REPLACE_JOURNAL_CLEAN) ||
                    (!op->journalValid && op->journalSeen)) {
                    afatfs_replaceFail(
                        op, AFATFS_RESULT_RECOVERY_REQUIRED);
                    return;
                }
                afatfs_replaceScratchName(
                    op->transaction->nonce, 1u, oldName);
                afatfs_replaceStartScan(
                    op, oldName, AFATFS_REPLACE_ACTION_COMMIT_CHECK_OLD);
            } else {
                afatfs_replaceBeginRecovery(op);
            }
            return;

        case AFATFS_REPLACE_WRITE_JOURNAL_OPEN:
            journalName = op->journalSlot == 0u
                ? AFATFS_REPLACE_JOURNAL_SLOT0
                : AFATFS_REPLACE_JOURNAL_SLOT1;
            afatfs_replaceReleaseParent(op);
            op->phase = AFATFS_REPLACE_WRITE_JOURNAL_WAIT_OPEN;
            if (!afatfs_fopenChild(
                    op->parent, journalName, "w", AFATFS_CREATE_OR_OPEN,
                    AFATFS_MATCH_CASE_SENSITIVE, NULL,
                    afatfs_replaceJournalOpened)) {
                afatfs_replaceReacquireParent(op);
                op->phase = AFATFS_REPLACE_WRITE_JOURNAL_OPEN;
            }
            return;

        case AFATFS_REPLACE_WRITE_JOURNAL_DATA:
        {
            uint32_t remaining = sizeof(afatfsReplaceJournalRecord_t) -
                                 op->ioOffset;
            uint32_t written = afatfs_fwrite(
                op->journalFile,
                afatfs.treeWorkspace.ioBuffer + op->ioOffset,
                remaining);

            if (written != 0u) {
                op->ioOffset = (uint16_t)(op->ioOffset + written);
                if (op->ioOffset == sizeof(afatfsReplaceJournalRecord_t))
                    op->phase = AFATFS_REPLACE_WRITE_JOURNAL_CLOSE;
                return;
            }
            if (afatfs_isFull())
                afatfs_replaceFail(op, AFATFS_RESULT_NO_SPACE);
            return;
        }

        case AFATFS_REPLACE_WRITE_JOURNAL_CLOSE:
            if (!afatfs_fclose(op->journalFile,
                               afatfs_replaceJournalClosed)) {
                return;
            }
            return;

        case AFATFS_REPLACE_WRITE_JOURNAL_AFTER_CLOSE:
            op->phase = AFATFS_REPLACE_WRITE_JOURNAL_SYNC;
            return;

        case AFATFS_REPLACE_WRITE_JOURNAL_SYNC:
            if (!afatfs_sync())
                return;
            op->bestJournal = *(afatfsReplaceJournalRecord_t *)
                afatfs.treeWorkspace.ioBuffer;
            op->journalValid = 1u;
            afatfs_replaceAfterJournalSync(op);
            return;

        case AFATFS_REPLACE_SCAN_BEGIN:
            afatfs_findFirstObject(op->parent, &op->finder);
            op->finderActive = 1u;
            op->phase = AFATFS_REPLACE_SCAN_NEXT;
            return;

        case AFATFS_REPLACE_SCAN_NEXT:
        {
            afatfsObjectInfo_t object;
            uint8_t matches;

            status = afatfs_findNextObject(op->parent, &op->finder, &object);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_replaceFail(op, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (object.id.kind == AFATFS_OBJECT_NONE) {
                afatfs_findLastObject(op->parent, &op->finder);
                op->finderActive = 0u;
                op->found = 0u;
                afatfs_replaceHandleScanResult(op);
                return;
            }

            if (op->action ==
                    AFATFS_REPLACE_ACTION_CHECK_UNKNOWN_SCRATCH) {
                matches = afatfs_replaceIsScratchName(object.id.displayName);
            } else {
                uint8_t caseSensitive =
                    op->action == AFATFS_REPLACE_ACTION_COMMIT_FIND_TARGET ||
                    op->action == AFATFS_REPLACE_ACTION_RECOVER_CHECK_TARGET
                        ? 0u : 1u;

                matches = fat_compareDisplayName(
                    object.id.displayName, op->scanName,
                    caseSensitive) == 0 ? 1u : 0u;
            }
            if (matches) {
                op->foundObject = object;
                op->found = 1u;
                afatfs_findLastObject(op->parent, &op->finder);
                op->finderActive = 0u;
                afatfs_replaceHandleScanResult(op);
            }
            return; /* At most one concrete directory object per poll. */
        }

        case AFATFS_REPLACE_MOVE_FOUND:
        {
            char destination[AFATFS_LONG_FILENAME_MAX + 1u];

            afatfs_replaceMoveDestination(op, destination);
            afatfs_replaceReleaseParent(op);
            op->phase = AFATFS_REPLACE_WAIT_MOVE;
            if (!afatfs_renameObjectAt(
                    op->parent, &op->foundObject.id, destination,
                    AFATFS_MATCH_CASE_SENSITIVE, NULL,
                    afatfs_replaceMoveDone)) {
                afatfs_replaceReacquireParent(op);
                op->phase = AFATFS_REPLACE_MOVE_FOUND;
            }
            return;
        }

        case AFATFS_REPLACE_DELETE_FOUND:
            op->phase = AFATFS_REPLACE_WAIT_DELETE;
            if (!afatfs_deleteTree(&op->foundObject.id,
                                   afatfs_replaceDeleteDone)) {
                op->phase = AFATFS_REPLACE_DELETE_FOUND;
            }
            return;

        case AFATFS_REPLACE_SYNC_NAMESPACE:
            if (!afatfs_sync())
                return;
            afatfs_replaceAfterNamespaceSync(op);
            return;

        case AFATFS_REPLACE_FINISH:
            if (op->journalFile) {
                if (!afatfs_fclose(op->journalFile,
                                   afatfs_replaceJournalClosed)) {
                    return;
                }
                return;
            }
            afatfs_replaceFinish(op);
            return;

        default:
            afatfs_replaceFail(op, AFATFS_RESULT_IO_ERROR);
            return;
    }
}

bool afatfs_beginTreeReplace(afatfsDirHandle_t parent,
                             const char *targetDisplayName,
                             afatfsReplaceBeginCallback_t complete)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;
    afatfsReplaceTransactionPtr_t transaction =
        &afatfs.replaceTransaction;

    /*
     * Accept one asynchronous staging-directory creation.
     *
     * Inputs are checked before either global structure changes. The sanitized
     * target component and deterministic nonzero nonce are copied into the small
     * persistent descriptor; CREATE_NEW collision handling advances that nonce
     * without ever reopening stale scratch. Output false has no callback;
     * accepted work is completed by afatfs_replaceStageCreated().
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_NONE ||
        transaction->active || afatfs.renameObject.active ||
        afatfs.removeObjects.active || !afatfs_parentCanAcceptChild(parent) ||
        !afatfs_childComponentCanStart(targetDisplayName)) {
        return false;
    }

    memset(transaction, 0, sizeof(*transaction));
    if (afatfs_copySanitizedLongName(transaction->targetName,
                                     targetDisplayName) == 0u) {
        return false;
    }
    transaction->active = 1u;
    transaction->parent = parent;
    transaction->nonce = afatfs.cacheTimer ^ afatfs.lastClusterAllocated ^
                         afatfs.partitionStartSector;
    if (transaction->nonce == 0u)
        transaction->nonce = 1u;

    memset(op, 0, sizeof(*op));
    op->active = 1u;
    op->mode = AFATFS_REPLACE_MODE_BEGIN;
    op->phase = AFATFS_REPLACE_BEGIN_CREATE_STAGE;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    op->parent = parent;
    op->transaction = transaction;
    op->beginCallback = complete;
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_REPLACE;
    return true;
}

bool afatfs_commitTreeReplace(afatfsReplaceTransactionPtr_t transaction,
                              afatfsResultCallback_t complete)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    /*
     * Transfer a populated transaction into journaled commit execution.
     * The staging directory itself must be idle and have no retained child;
     * commit takes ownership of closing it. The parent retain counter moves
     * from descriptor to operation without changing its physical value, keeping
     * unrelated child starts blocked throughout commit except deliberate
     * journal/move handoffs.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        transaction != &afatfs.replaceTransaction || !transaction->active ||
        !transaction->parentHeld || !transaction->stagingDirectory ||
        afatfs_fileIsBusy(transaction->stagingDirectory) ||
        transaction->stagingDirectory->childOperationRetainCount != 0u ||
        afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_NONE ||
        afatfs.renameObject.active || afatfs.removeObjects.active) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    op->active = 1u;
    op->mode = AFATFS_REPLACE_MODE_COMMIT;
    op->phase = AFATFS_REPLACE_CLOSE_STAGE;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    op->parent = transaction->parent;
    op->parentHeld = transaction->parentHeld;
    op->transaction = transaction;
    op->resultCallback = complete;
    transaction->parentHeld = 0u;
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_REPLACE;
    return true;
}

bool afatfs_abortTreeReplace(afatfsReplaceTransactionPtr_t transaction,
                             afatfsResultCallback_t complete)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    /*
     * Abort retains exact transaction identity instead of deleting by prefix.
     * Before commit it closes/scans/deletes only nonce-new. After any commit
     * attempt it reads the durable journal and follows recovery, because the
     * live target may already be under nonce-old. Ownership transfer mirrors
     * commit so a failed abort can be retried without exposing the parent.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        transaction != &afatfs.replaceTransaction || !transaction->active ||
        !transaction->parentHeld ||
        (transaction->stagingDirectory &&
         (afatfs_fileIsBusy(transaction->stagingDirectory) ||
          transaction->stagingDirectory->childOperationRetainCount != 0u)) ||
        afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_NONE ||
        afatfs.renameObject.active || afatfs.removeObjects.active) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    op->active = 1u;
    op->mode = AFATFS_REPLACE_MODE_ABORT;
    op->phase = transaction->stagingDirectory
        ? AFATFS_REPLACE_CLOSE_STAGE
        : (transaction->commitStarted
            ? AFATFS_REPLACE_READ_JOURNAL_OPEN
            : AFATFS_REPLACE_SCAN_BEGIN);
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    op->parent = transaction->parent;
    op->parentHeld = transaction->parentHeld;
    op->transaction = transaction;
    op->resultCallback = complete;
    op->journalSlot = 0u;
    transaction->parentHeld = 0u;
    if (!transaction->stagingDirectory && !transaction->commitStarted) {
        afatfs_replaceScratchName(transaction->nonce, 0u, op->scanName);
        op->action = AFATFS_REPLACE_ACTION_ABORT_DELETE_NEW;
    }
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_REPLACE;
    return true;
}

bool afatfs_recoverTreeReplace(afatfsDirHandle_t parent,
                               afatfsResultCallback_t complete)
{
    afatfsReplaceOperation_t *op =
        &afatfs.treeWorkspace.operation.replace;

    /*
     * Start explicit recovery only for one caller-selected product parent.
     * A live transaction is rejected because its scratch is intentionally under
     * construction. On acceptance the parent is exclusively retained, both
     * journal slots are read, and no scratch mutation occurs unless a valid CRC
     * record names its exact nonce/target. Output follows the one-callback rule.
     */
    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        afatfs.treeWorkspace.kind != AFATFS_TREE_OPERATION_NONE ||
        afatfs.replaceTransaction.active || afatfs.renameObject.active ||
        afatfs.removeObjects.active || !afatfs_parentCanAcceptChild(parent)) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    op->active = 1u;
    op->mode = AFATFS_REPLACE_MODE_RECOVER;
    op->phase = AFATFS_REPLACE_READ_JOURNAL_OPEN;
    op->terminalResult = AFATFS_RESULT_IO_ERROR;
    op->parent = parent;
    op->resultCallback = complete;
    parent->childOperationRetainCount = 1u;
    op->parentHeld = 1u;
    afatfs.treeWorkspace.kind = AFATFS_TREE_OPERATION_REPLACE;
    return true;
}
