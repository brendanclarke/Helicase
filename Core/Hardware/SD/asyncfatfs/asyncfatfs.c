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
 * Static application-file handle capacity.
 *
 * What: Five independent slots allow a retained source and destination stream
 * to coexist with directory/payload work that temporarily needs additional
 * handles. Why: Bank -> Scene -> Kit traversal normally releases each explicit
 * directory handle after afatfs_chdir() copies it into currentDirectory, but
 * callers may legitimately retain files at different directory levels while
 * continuing asynchronous I/O. Five provides that concurrency without making
 * path depth itself dictate handle lifetime.
 *
 * SRAM effect on the current STM32F765 build: afatfsFile_t is 188 bytes, so
 * raising this pool from three to five consumes 376 additional zero-initialized
 * SRAM1 bytes. All allocation, polling, shutdown, and diagnostic loops below
 * use this constant, so no independent loop bound may be introduced.
 *
 * Five handles are sufficient for the serialized filesystem facade. Session
 * 057 temporarily raised this to eight while testing the apparent five-child
 * Bank Save stop, but the handle census proved zero retained handles between
 * children and exactly one expected handle after each child mkdir. Restoring
 * five must remain paired with that ownership model: it reclaims the three
 * unneeded 188-byte handles (564 bytes of handle payload) instead of masking a
 * leak or spending Pattern-reserved memory on disproved headroom.
 */
#define AFATFS_MAX_OPEN_FILES 5

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

/*
 * Terminator-aware create phases. The scan owns only collision observation;
 * placement phases separately prepare a logical target, publish its run, wait
 * for moved-target persistence, and retire the old marker tail.
 */
enum {
    AFATFS_CREATEFILE_PHASE_INITIAL = 0,
    AFATFS_CREATEFILE_PHASE_FIND_FILE,
    AFATFS_CREATEFILE_PHASE_SEEK_NEXT_SECTOR,
    AFATFS_CREATEFILE_PHASE_PREPARE_TARGET_SECTOR,
    AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE,
    AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE,
    AFATFS_CREATEFILE_PHASE_WAIT_TARGET_PERSISTENCE,
    AFATFS_CREATEFILE_PHASE_RETIRE_OLD_TERMINATOR_TAIL,
    AFATFS_CREATEFILE_PHASE_SUCCESS,
    AFATFS_CREATEFILE_PHASE_FAILURE,
};

/*
 * Proven origin of the selected directory-entry run. DELETED preserves the
 * following live entries; either TERMINATOR origin requires a replacement
 * 0x00 entry, with MOVED additionally retaining the old marker pointer.
 */
typedef enum {
    AFATFS_DIRECTORY_RUN_ORIGIN_NONE = 0,
    AFATFS_DIRECTORY_RUN_ORIGIN_DELETED,
    AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL,
    AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED,
} afatfsDirectoryRunOrigin_e;

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
    afatfsFileCallback_t callback;

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
     * opens without learning raw FAT internals.
     *
     * shortNameCaseFlags describes filename[]: the raw 8.3 key remains
     * uppercase for FAT lookup, while these ntReserved bits preserve
     * all-lowercase display names such as kitset.kcg and slakd1.drm without
     * changing case-insensitive open behavior.
     */
    /*
     * Shared directory-entry run reservation for create and rename.
     *
     * What: Persists one sector-local candidate or selected FAT
     * directory-entry run across asynchronous polls. requestedEntryCount is
     * one for an SFN and lfnEntryCount plus one for an LFN/SFN run.
     * reservationOrigin records whether selectedRunStart is a proven deleted
     * run, the current terminator, or entry zero of a prepared next logical
     * sector. scanRunStart tracks the current deleted run and is reused for
     * the old terminator only after a moved-marker decision makes the scan run
     * dead.
     *
     * Why: Deleted entries and the end marker are not interchangeable. Every
     * writer must know whether it may overwrite only selected slots or must
     * also publish a replacement zero entry. Keeping this state in the create
     * operation makes short create, LFN create, and rename share one rule
     * without adding a RAM-only sector-initialization witness or a second
     * rename allocator.
     *
     * Inputs: requested object-entry count, raw finder position, entry class,
     * create permission, and the 16-entry directory-sector boundary.
     * Outputs/effects: either a latched deleted run that leaves the
     * terminator untouched, a local terminator-owned run with room for a
     * replacement marker, or resumable state for preparing a next logical
     * sector and later retiring the old terminator tail.
     * Affiliates: afatfs_createFileContinue(), the short and LFN writers,
     * afatfs_renameObjectContinue(), afatfs_extendSubdirectory(),
     * afatfs_findNext(), and AFATFS_FILES_PER_DIRECTORY_SECTOR.
     */
    uint8_t requestedEntryCount;
    uint8_t lfnEntryCount;
    uint8_t deletedRunLength;
    uint8_t reservationOrigin;
    uint8_t scanLongNameValid;
    uint8_t scanLongNameChecksum;
    afatfsMatchMode_t matchMode;
    uint16_t aliasOrdinal;
    char longName[AFATFS_LONG_FILENAME_MAX + 1u];
    char scanLongName[AFATFS_LONG_FILENAME_MAX + 1u];
    char *openNameOut;
    afatfsDirEntryPointer_t scanRunStart;
    afatfsDirEntryPointer_t selectedRunStart;
} afatfsCreateFile_t;

/*
 * Rename phases mirror create's shared reservation lifecycle: collision scan,
 * logical target seek/prepare, target-media barrier, old-marker-tail cleanup,
 * new-run publication, and complete old-name retirement.
 */
typedef enum {
    AFATFS_RENAME_OBJECT_PHASE_INITIAL = 0,
    AFATFS_RENAME_OBJECT_PHASE_FIND_SOURCE,
    AFATFS_RENAME_OBJECT_PHASE_LOAD_SOURCE_ENTRY,
    AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_NAME,
    AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN,
    AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN,
    AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_SEEK,
    AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_TARGET,
    AFATFS_RENAME_OBJECT_PHASE_WAIT_TARGET_PERSISTENCE,
    AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_TERMINATOR_TAIL,
    AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN,
    AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN,
    AFATFS_RENAME_OBJECT_PHASE_FINISH,
} afatfsRenameObjectPhase_e;

typedef struct afatfsRenameObject_t {
    uint8_t active;
    uint8_t succeeded;
    uint8_t movedEntryRun;
    uint8_t oldEntryCount;
    afatfsResultCode_t result;
    afatfsRenameObjectPhase_e phase;
    afatfsMatchMode_t matchMode;
    /* Structured result callback; open-name output is published only on OK. */
    afatfsResultCallback_t callback;
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
    /* Next physical old-name entry to retire; permits cross-sector yielding. */
    uint8_t oldRunNextEntry;
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
    /*
     * Bound the allocator's second, wrapped search pass.
     *
     * `searchStartCluster` records where the first pass began and
     * `searchWrapped` records whether the search has restarted at FAT cluster
     * 2. Together they make the last-allocation hint an optimization rather
     * than an incorrect end-of-volume boundary: every legal cluster is
     * examined once before the filesystem is declared full.
     *
     * Inputs: afatfs.lastClusterAllocated at operation initialization.
     * Output: the exclusive upper bound for the wrapped pass. Affiliates:
     * afatfs_appendRegularFreeClusterInitOperationState() and
     * afatfs_appendRegularFreeClusterContinue().
     */
    uint32_t searchStartCluster;
    uint8_t searchWrapped;
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
    afatfsTruncateFilePhase_e phase;
} afatfsTruncateFile_t;

typedef enum {
    AFATFS_DELETE_FILE_DELETE_DIRECTORY_ENTRY,
    AFATFS_DELETE_FILE_DEALLOCATE_CLUSTERS,
} afatfsDeleteFilePhase_e;

typedef struct afatfsCloseFile_t {
    afatfsCallback_t callback;
} afatfsCloseFile_t;

typedef enum {
    AFATFS_DELETE_TREE_INITIAL,
    AFATFS_DELETE_TREE_OPEN_DIR,
    AFATFS_DELETE_TREE_SCAN,
    AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND,
    AFATFS_DELETE_TREE_REOPEN_PARENT,
    AFATFS_DELETE_TREE_DESCEND_DIR,
    AFATFS_DELETE_TREE_RESUME_PARENT,
    AFATFS_DELETE_TREE_RETIRE_ENTRIES,
    AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS,
    AFATFS_DELETE_TREE_SUCCESS
} afatfsDeleteTreePhase_e;

typedef struct afatfsDeleteTree_t {
    /* Complete iterator identities are copied so no later name lookup occurs. */
    afatfsObjectInfo_t root;
    afatfsObjectInfo_t currentTarget;
    uint32_t currentCluster;
    uint32_t parentCluster;
    /*
     * The parent directory's OWN directory-entry pointer, captured at descend.
     *
     * What: while AFATFS_DELETE_TREE_SCAN is iterating a directory, that
     * directory's own identity lives in file->directoryEntryPos (written by
     * AFATFS_DELETE_TREE_OPEN_DIR). Descending into a child overwrites that
     * field with the child's identity, so the parent's copy is saved here
     * first and restored by AFATFS_DELETE_TREE_REOPEN_PARENT on the way back
     * up. Why: file->directoryEntryPos is what AFATFS_DELETE_TREE_SCAN's
     * "directory is now empty" branch compares against root.id.sfnEntry to
     * decide whether it has finished the whole tree (retire the root, then
     * SUCCESS) or has merely emptied a nested child (ascend). If that field
     * does not identify the directory actually being scanned, an emptied
     * delete root is misread as a nested child and the traversal ascends out
     * of the tree it was asked to delete. Pairs with parentCluster, and like
     * parentCluster it holds one level only -- a descend nested inside
     * another descend overwrites it, which is the same depth-one bound
     * parentCluster's own ".."-agreement check already enforces.
     */
    afatfsDirEntryPointer_t parentEntry;
    /*
     * The complete identity of the directory this traversal descended into.
     *
     * What: a verbatim copy of currentTarget taken at descend time, when it
     * still describes the child directory itself. Why: currentTarget is the
     * "object currently being deleted" register, and AFATFS_DELETE_TREE_SCAN
     * rewrites it for every object it processes -- so once the traversal has
     * deleted the child's own contents, currentTarget names the LAST FILE
     * deleted inside that child, not the child. The ascend path needs the
     * child's cluster (to free its chain) and its complete VFAT name run
     * (lfnEntryCount/lfnFirstEntry/lfnFollowingEntry, so
     * afatfs_retireObjectNameRun() retires every fragment rather than
     * orphaning them); neither survives in currentTarget, and
     * file->directoryEntryPos carries only the short-entry pointer. Saving
     * the whole afatfsObjectInfo_t at the one moment it is known-good is
     * what lets AFATFS_DELETE_TREE_REOPEN_PARENT delete the right object
     * without re-deriving it from a second physical scan. Same depth-one
     * bound as parentCluster/parentEntry above.
     */
    afatfsObjectInfo_t descendTarget;
    /* Saturated structural-work allowance; this is not a wall-clock timeout. */
    uint32_t structuralBudget;
    /* Next physical target-name entry to retire; zero means a fresh run. */
    uint8_t nameRunNextEntry;
    /* True only while finder owns the private directory scan's cache sector. */
    uint8_t finderActive;
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
    /*
     * Which exact check inside afatfs_deleteTreeContinue() produced the most
     * recent non-OK afatfsResultCode_t.
     *
     * What: afatfsResultCode_t alone cannot distinguish AFATFS_RESULT_UNSUPPORTED_LAYOUT
     * raised by ~17 architecturally different checks scattered across this
     * traversal (out-of-range cluster on open, a malformed ".." entry, a
     * parent-cluster mismatch on ascend, failing to re-find a child's exact
     * SFN/cluster identity in its re-scanned parent, a structural-budget
     * exhaustion, and more). Why: filesystem.c's caller only ever sees the
     * bare result code through afatfs_deleteTree()'s callback, and this
     * project's card testing found that code alone is not enough to know
     * which check to investigate. Set immediately before every non-OK
     * afatfs_deleteTreeFinish() call in this file; read afterward through
     * afatfs_getDeleteTreeFailureSite(), which -- unlike
     * afatfs_getDeleteTreePhase() -- deliberately does not require an
     * active openFiles[] entry, because by the time a caller wants this
     * value, afatfs_deleteTreeFinish() has already reset the handle via
     * afatfs_initFileHandle(); this struct is the persistent
     * afatfs.deleteTreeState singleton, not part of that handle, so its
     * fields (including this one) survive that reset until the next delete
     * starts. Reset only by afatfs_deleteTree()'s own setup. Affiliates:
     * afatfsDeleteTreeFailureSite_e, afatfs_getDeleteTreeFailureSite(),
     * filesystem.c's op_delete_slot_error_detail/DELETE_RESULT trace record.
     */
    uint8_t failureSite;
} afatfsDeleteTree_t;

/* afatfsDeleteTreeFailureSite_e is declared in asyncfatfs.h (public API,
 * needed by filesystem.c to recognize specific sites) and defined there. */

/*
 * afatfs_getDeleteTreeFailureSite() is declared in asyncfatfs.h (public API)
 * and defined beside afatfs_getDeleteTreePhase() below.
 */

typedef enum {
    AFATFS_FILE_OPERATION_NONE,
    AFATFS_FILE_OPERATION_CREATE_FILE,
    AFATFS_FILE_OPERATION_SEEK, // Seek the file's cursorCluster forwards by seekOffset bytes
    AFATFS_FILE_OPERATION_CLOSE,
    AFATFS_FILE_OPERATION_TRUNCATE,
#ifdef AFATFS_USE_FREEFILE
    AFATFS_FILE_OPERATION_APPEND_SUPERCLUSTER,
    AFATFS_FILE_OPERATION_LOCKED,
#endif
    AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER,
    AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY,
    AFATFS_FILE_OPERATION_DELETE_TREE,
} afatfsFileOperation_e;

typedef struct afatfsFileOperation_t {
    afatfsFileOperation_e operation;
    union {
        afatfsCreateFile_t createFile;
        afatfsSeek_t seek;
        afatfsAppendSupercluster_t appendSupercluster;
        afatfsAppendFreeCluster_t appendFreeCluster;
        afatfsExtendSubdirectory_t extendSubdirectory;
        afatfsTruncateFile_t truncateFile;
        afatfsCloseFile_t closeFile;
        /* One native delete is allowed at a time; its expanded state lives in
         * afatfs.deleteTreeState instead of inflating every open handle. */
        afatfsDeleteTree_t *deleteTree;
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
    afatfsResultCode_t result;
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
    /* Structured result callback supplied by filesystem.c or diagnostics. */
    afatfsResultCallback_t callback;
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
    /* Next physical object-name entry to retire across cache-sector yields. */
    uint8_t nameRunNextEntry;
} afatfsRemoveObjects_t;

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

    /*
     * Sole owner of the expanded native delete state. Keeping this one active
     * operation outside the five-handle union preserves the approved SRAM
     * budget while the operation still retains one private file handle for
     * cursor/cache ownership and polling dispatch.
     */
    afatfsDeleteTree_t deleteTreeState;

    afatfsRenameObject_t renameObject;
    afatfsRemoveObjects_t removeObjects;

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
 * Retained-state ceiling for the terminator-aware reservation refactor.
 *
 * What: Verifies the target ABI still gives the create state, each file
 * handle, and the global rename state their accepted pre-Phase-One sizes.
 * Why: afatfsCreateFile_t is the largest per-handle operation-union member,
 * so casual growth multiplies through five open handles and currentDirectory
 * and also enlarges rename's embedded newNameState. Inputs are the compiler's
 * completed private type layouts. Output is a build failure instead of an
 * unapproved normal-SRAM1 increase. Affiliates are AFATFS_MAX_OPEN_FILES,
 * afatfsFileOperation_t, afatfs_t, and SRAM_MANIFEST.md.
 */
_Static_assert(sizeof(afatfsCreateFile_t) == 144u,
               "Phase One create state must remain 144 bytes");
_Static_assert(sizeof(afatfsFile_t) == 188u,
               "Phase One file handle must remain 188 bytes");
_Static_assert(sizeof(afatfsRenameObject_t) == 552u,
               "Phase One rename state must remain 552 bytes");

static afatfs_t afatfs;

static void afatfs_fileOperationContinue(afatfsFile_t *file);
static void afatfs_renameObjectContinue(void);
static void afatfs_removeObjectsContinue(void);
static void afatfs_deleteTreeContinue(afatfsFile_t *file);
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
    uint32_t searchLimit;

    doMore:

    switch (opState->phase) {
        case AFATFS_APPEND_FREE_CLUSTER_PHASE_FIND_FREESPACE:
            /*
             * Search the FAT as a ring around the last-allocation hint.
             *
             * The old single pass stopped at the physical end of the FAT. If
             * free clusters existed before the hint, a multi-cluster fwrite()
             * could allocate its first cluster, fail at the next cluster
             * boundary, and set filesystemFull forever. The autosave baseline
             * exposed that as a 16 KiB `.hcprms1` followed by a blocking boot.
             *
             * Pass one covers [searchStartCluster, volumeEnd). If it has no
             * free entry, pass two covers [cluster 2, searchStartCluster).
             * Only failure of both passes means the regular cluster pool is
             * actually exhausted.
             */
            searchLimit = opState->searchWrapped
                ? opState->searchStartCluster
                : afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER;
            switch (afatfs_findClusterWithCondition(
                        CLUSTER_SEARCH_FREE, &opState->searchCluster,
                        searchLimit)) {
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
                    // We couldn't find an empty cluster to append to the file
                    opState->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_FAILURE;
                    goto doMore;
                break;
                case AFATFS_FIND_CLUSTER_NOT_FOUND:
                    if (!opState->searchWrapped &&
                        opState->searchStartCluster >
                            FAT_SMALLEST_LEGAL_CLUSTER_NUMBER) {
                        /*
                         * The hint-to-end range was occupied; resume at the
                         * first legal FAT cluster and stop just before the
                         * original hint. This transition is scalar-only and
                         * remains resumable through afatfs_poll().
                         */
                        opState->searchCluster =
                            FAT_SMALLEST_LEGAL_CLUSTER_NUMBER;
                        opState->searchWrapped = 1u;
                        goto doMore;
                    }
                    opState->phase =
                        AFATFS_APPEND_FREE_CLUSTER_PHASE_FAILURE;
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
    uint32_t volumeEnd =
        afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER;

    state->phase = AFATFS_APPEND_FREE_CLUSTER_PHASE_INITIAL;
    state->previousCluster = previousCluster;
    /*
     * Normalize the allocation hint before beginning the two-pass search.
     *
     * A valid hint starts the first pass near recent allocations. A stale or
     * out-of-range hint falls back to cluster 2, which still searches the
     * complete volume. `searchStartCluster` must remain unchanged while
     * searchCluster advances asynchronously because it becomes the wrapped
     * pass's exclusive bound.
     */
    state->searchStartCluster = afatfs.lastClusterAllocated;
    if (state->searchStartCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
        state->searchStartCluster >= volumeEnd) {
        state->searchStartCluster = FAT_SMALLEST_LEGAL_CLUSTER_NUMBER;
    }
    state->searchCluster = state->searchStartCluster;
    state->searchWrapped = 0u;
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

    afatfs_fileUpdateFilesize(file);

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

    afatfs_fileUpdateFilesize(file);

    file->operation.operation = AFATFS_FILE_OPERATION_NONE;

    if (opState->callback) {
        opState->callback(file);
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
static afatfsOperationStatus_e afatfs_fseekInternal(afatfsFilePtr_t file, uint32_t offset, afatfsFileCallback_t callback)
{
    // See if we can seek without queuing an operation
    if (afatfs_fseekAtomic(file, offset)) {
        if (callback) {
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
                return afatfs_fseekInternal(file, MIN(file->cursorOffset + offset, file->logicalSize), NULL);
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
    return afatfs_fseekInternal(file, MIN((uint32_t) offset, file->logicalSize), NULL);
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
    finder->lfnExpectedOrdinal = 0u;
    finder->lfnMalformed = 0u;
    memset(finder->lfnName, 0, sizeof(finder->lfnName));
    finder->lfnFirstEntry.sectorNumberPhysical = 0u;
    finder->lfnFirstEntry.entryIndex = -1;
    memset(finder->lfnFollowingEntry, 0, sizeof(finder->lfnFollowingEntry));
}

static bool afatfs_objectLfnEntryShapeIsValid(const fatDirectoryEntry_t *entry)
{
    const uint8_t *raw = (const uint8_t *)entry;
    bool terminated = false;

    /* LFN entries have type zero and no first-cluster payload. The character
     * field is name text followed by an end marker; FAT implementations vary,
     * so both 0x0000 and 0xffff terminate the name and 0xffff is legal padding
     * with or without a preceding 0x0000. Non-ASCII units are retained because
     * the name builder renders them as underscores. */
    if (!entry || raw[11] != FAT_FILE_ATTRIBUTE_LFN || raw[12] != 0u ||
        raw[26] != 0u || raw[27] != 0u)
        return false;
    for (uint8_t i = 0u; i < FAT_LFN_CHARS_PER_ENTRY; i++) {
        static const uint8_t offsets[FAT_LFN_CHARS_PER_ENTRY] = {
            1u, 3u, 5u, 7u, 9u, 14u, 16u, 18u, 20u, 22u, 24u, 28u, 30u
        };
        uint16_t ch = (uint16_t)raw[offsets[i]] |
                      ((uint16_t)raw[offsets[i] + 1u] << 8u);
        if (ch == 0x0000u || ch == 0xffffu) {
            terminated = true;
        } else if (terminated) {
            return false;
        }
    }
    return true;
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
    uint8_t sequence = raw[0];
    uint8_t seq = sequence & 0x1fu;

    /*
     * Rebuild one ASCII-safe VFAT display name while walking raw entries.
     *
     * VFAT stores the final text fragment first and marks it with 0x40. The
     * low five bits are the one-based ordinal, so `(seq - 1) * 13` recovers
     * the absolute display-name offset for this fragment. The checksum must
     * remain identical across fragments; final validation happens when the next
     * SFN entry arrives because that entry owns the physical object metadata.
     */
    if (seq == 0u || seq > AFATFS_LONG_FILENAME_ENTRY_MAX ||
        (sequence & 0xa0u) != 0u ||
        !afatfs_objectLfnEntryShapeIsValid(entry)) {
        finder->lfnValid = 0u;
        finder->lfnMalformed = 1u;
        return;
    }

    if (sequence & FAT_LFN_LAST_LONG_ENTRY) {
        afatfs_objectScanReset(finder);
        finder->lfnValid = 1u;
        finder->lfnChecksum = raw[13];
        finder->lfnFirstEntry = *rawFinder;
        finder->lfnEntryCount = 1u;
        finder->lfnExpectedOrdinal = (uint8_t)(seq - 1u);
    } else {
        if (!finder->lfnValid || finder->lfnExpectedOrdinal != seq ||
            finder->lfnChecksum != raw[13] ||
            finder->lfnEntryCount >= AFATFS_LONG_FILENAME_ENTRY_MAX) {
            finder->lfnValid = 0u;
            finder->lfnMalformed = 1u;
            return;
        }
        finder->lfnFollowingEntry[finder->lfnEntryCount - 1u] = *rawFinder;
        finder->lfnEntryCount++;
        finder->lfnExpectedOrdinal = (uint8_t)(seq - 1u);
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

        if (finder->lfnMalformed ||
            (finder->lfnValid &&
             (finder->lfnExpectedOrdinal != 0u ||
              finder->lfnChecksum !=
                  fat_lfnChecksum((const uint8_t *)entry->filename) ||
              finder->lfnName[0] == '\0'))) {
            /* Preserve the SFN as a browsable object, but expose that its
             * preceding VFAT-looking run is not a trusted identity. */
            object->lfnMalformed = 1u;
            for (uint8_t i = 0u;
                 i < AFATFS_LONG_FILENAME_MAX &&
                 object->id.shortName[i] != '\0'; i++)
                object->id.displayName[i] = object->id.shortName[i];
        } else if (finder->lfnValid &&
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
            memcpy(object->id.lfnFollowingEntry,
                   finder->lfnFollowingEntry,
                   sizeof(object->id.lfnFollowingEntry));
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
            directory->operation.operation = AFATFS_FILE_OPERATION_NONE;

            if (opState->callback) {
                opState->callback(directory);
            }

            return AFATFS_OPERATION_SUCCESS;
        break;
        case AFATFS_EXTEND_SUBDIRECTORY_PHASE_FAILURE:
            directory->operation.operation = AFATFS_FILE_OPERATION_NONE;

            if (opState->callback) {
                opState->callback(NULL);
            }
            return AFATFS_OPERATION_FAILURE;
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
static afatfsOperationStatus_e afatfs_extendSubdirectory(afatfsFile_t *directory, afatfsFilePtr_t parentDirectory, afatfsFileCallback_t callback)
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

    afatfs_appendRegularFreeClusterInitOperationState(&opState->appendFreeCluster, directory->cursorPreviousCluster);

    return afatfs_extendSubdirectoryContinue(directory);
}

/*
 * The former single-entry allocator was removed with the phase-one marker
 * contract. Create and rename now retain the selected run and its origin until
 * their writers run, so a second scan cannot pass 0x00 or lose that origin.
 */

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

/*
 * Count how many file handles in the pool are currently allocated.
 *
 * Returns the number of openFiles[] entries whose type is not NONE.
 * Called by filesystem.c's Bank Save diagnostic to detect whether
 * handles accumulate across per-child iterations. A count > 0 at the
 * top of a new child cycle (when no handle should be in use) proves a
 * leak; the value itself shows how many children have leaked so far.
 * Pure read-only snapshot — no state change, no I/O, no allocation.
 */
uint8_t afatfs_countOpenHandles(void)
{
    uint8_t count = 0u;
    for (int i = 0; i < AFATFS_MAX_OPEN_FILES; i++) {
        if (afatfs.openFiles[i].type != AFATFS_FILE_TYPE_NONE)
            count++;
    }
    return count;
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
            if (afatfs_FATIsEndOfChainMarker(opState->currentCluster)) {
                opState->phase = AFATFS_TRUNCATE_FILE_SUCCESS;
                goto doMore;
            }
            {
                uint32_t nextCluster;

                /* One FAT entry per continuation keeps regular removal bounded
                 * just like native tree deletion; cache hits still yield. */
                status = afatfs_FATGetNextCluster(0, opState->currentCluster,
                                                  &nextCluster);
                if (status != AFATFS_OPERATION_SUCCESS)
                    return status;
                status = afatfs_FATSetNextCluster(opState->currentCluster, 0);
                if (status != AFATFS_OPERATION_SUCCESS)
                    return status;
                afatfs.lastClusterAllocated = MIN(
                    afatfs.lastClusterAllocated,
                    opState->currentCluster - 1u);
                opState->currentCluster = nextCluster;
            }
            return AFATFS_OPERATION_IN_PROGRESS;
        break;
        case AFATFS_TRUNCATE_FILE_SUCCESS:
            if (file->operation.operation == AFATFS_FILE_OPERATION_TRUNCATE) {
                file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            }

            if (opState->callback) {
                opState->callback(file);
            }

            return AFATFS_OPERATION_SUCCESS;
        break;
    }

    if (status == AFATFS_OPERATION_FAILURE && file->operation.operation == AFATFS_FILE_OPERATION_TRUNCATE) {
        file->operation.operation = AFATFS_FILE_OPERATION_NONE;
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
bool afatfs_ftruncate(afatfsFilePtr_t file, afatfsFileCallback_t callback)
{
    afatfsTruncateFile_t *opState;

    if (afatfs_fileIsBusy(file))
        return false;

    file->operation.operation = AFATFS_FILE_OPERATION_TRUNCATE;

    opState = &file->operation.state.truncateFile;
    opState->callback = callback;
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

    /*
     * A leading dot denotes a hidden display component, not an empty basename
     * plus extension. Treat only later dots as the 8.3 extension separator.
     *
     * Why: `.hcprms1` and `.hcprms2` previously both collapsed to the initial
     * alias FILE.HCP, forcing the second boot-created autosave record through
     * an avoidable collision/restart path. Ignoring the leading dot here gives
     * them the independent HCPRMS1 and HCPRMS2 aliases while their VFAT names
     * remain exactly `.hcprms1` and `.hcprms2`.
     *
     * Inputs: the sanitized LFN component in `name`. Output: `dot` points only
     * at a true extension separator after the first character. Affiliates:
     * afatfs_fopen_lfn(), afatfs_mkdir_lfn(), and the autosave boot creator.
     */
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '.' && p != name)
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

/*
 * Reset one create/rename directory-run reservation without touching the
 * request's names, alias, callback, LFN count, or requested entry count.
 *
 * What: Clears the current deleted-run observation, selected origin, and both
 * sector-local pointers. Why: alias retries and new asynchronous placement
 * decisions must not inherit a candidate or old marker from an earlier scan.
 * Inputs: the embedded create-state reservation. Outputs/effects: invalid,
 * unselected run state only; no directory byte or cache descriptor changes.
 * Affiliates: create initial/retry paths, rename collision restarts, target
 * preparation, and terminal cleanup.
 */
static void afatfs_resetDirectoryRunReservation(
        afatfsCreateFile_t *opState)
{
    opState->deletedRunLength = 0u;
    opState->reservationOrigin = AFATFS_DIRECTORY_RUN_ORIGIN_NONE;
    opState->scanRunStart.sectorNumberPhysical = 0u;
    opState->scanRunStart.entryIndex = -1;
    opState->selectedRunStart.sectorNumberPhysical = 0u;
    opState->selectedRunStart.entryIndex = -1;
}

/*
 * Observe deleted runs without ending collision scanning.
 *
 * What: Tracks sector-local 0xE5 runs and latches the first run large enough
 * for the requested SFN or LFN/SFN entry count. A live entry breaks the
 * current run without discarding an already-latched candidate.
 * Why: A deleted hole is reusable but does not prove absence; a matching live
 * display name or alias may still occur before the first 0x00 marker. Inputs
 * are the reservation state, raw finder position, requested count, and the
 * caller's deleted-versus-live classification. Outputs/effects update only
 * reservation bytes/pointers; no directory or cache state is modified.
 * Affiliates: create/rename collision scans, fat_isDirectoryEntryEmpty(),
 * alias-restart reset, and the short/LFN writers.
 */
static void afatfs_noteDeletedDirectoryEntry(
        afatfsCreateFile_t *opState,
        const afatfsFinder_t *finder,
        bool isDeleted)
{
    if (!isDeleted || !finder || finder->entryIndex < 0) {
        opState->deletedRunLength = 0u;
        return;
    }

    if (opState->deletedRunLength == 0u ||
        finder->sectorNumberPhysical !=
            opState->scanRunStart.sectorNumberPhysical ||
        finder->entryIndex !=
            opState->scanRunStart.entryIndex +
                (int16_t)opState->deletedRunLength) {
        opState->scanRunStart = *finder;
        opState->deletedRunLength = 1u;
    } else if (opState->deletedRunLength < AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        opState->deletedRunLength++;
    }

    if (opState->reservationOrigin == AFATFS_DIRECTORY_RUN_ORIGIN_NONE &&
        opState->deletedRunLength >= opState->requestedEntryCount) {
        opState->selectedRunStart = opState->scanRunStart;
        opState->reservationOrigin = AFATFS_DIRECTORY_RUN_ORIGIN_DELETED;
    }
}

/*
 * Finish collision scanning and select a run at the first FAT terminator.
 *
 * What: Treats 0x00 as the end of the live namespace. It chooses an earlier
 * proven deleted run when one exists, otherwise reserves the terminator if
 * the object run plus one replacement zero entry fits, or records that the
 * complete run must move to the next logical sector.
 * Why: No valid collision exists after 0x00, and retiring that marker while
 * searching destroys the only persistent boundary hiding later stale bytes.
 * Inputs: reservation state, terminator finder pointer, requested count, and
 * AFATFS_FILES_PER_DIRECTORY_SECTOR. Outputs/effects select a run/origin or
 * save the old marker pointer for a moved run. No cache sector is dirtied.
 * Affiliates: create/open scan, rename collision scan, next-sector
 * preparation, replacement-marker writers, and old-tail retirement.
 */
static bool afatfs_selectDirectoryRunAtTerminator(
        afatfsCreateFile_t *opState,
        const afatfsFinder_t *terminator)
{
    if (opState->reservationOrigin == AFATFS_DIRECTORY_RUN_ORIGIN_DELETED &&
        opState->selectedRunStart.entryIndex >= 0) {
        return true;
    }

    if (!terminator || terminator->entryIndex < 0 ||
        (uint16_t)terminator->entryIndex >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR ||
        opState->requestedEntryCount == 0u) {
        return false;
    }

    if ((uint16_t)terminator->entryIndex +
            (uint16_t)opState->requestedEntryCount <
        AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        opState->selectedRunStart = *terminator;
        opState->reservationOrigin =
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL;
        return true;
    }

    /* The current scan run is dead after this decision; reuse its pointer for
     * the marker sector while selectedRunStart becomes the next-sector target.
     */
    opState->scanRunStart = *terminator;
    opState->selectedRunStart.sectorNumberPhysical = 0u;
    opState->selectedRunStart.entryIndex = -1;
    opState->reservationOrigin =
        AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED;
    return false;
}

/*
 * Prepare a next logical directory sector before moving the end marker.
 *
 * What: Uses currentDirectory's cursor/FAT chain, extends an extendable
 * directory when the cursor reaches allocated EOF, obtains the resulting
 * sector with write ownership, clears all 512 bytes, and selects entry zero.
 * Why: Physical sectors are not a directory-ordering primitive, and bytes
 * after 0x00 may be stale. Inputs are currentDirectory and an already-selected
 * moved/local origin. Outputs/effects are a dirty zeroed target or the normal
 * asynchronous/FAT failure status. Affiliates: afatfs_fseekAtomic(),
 * afatfs_extendSubdirectory(), afatfs_cacheSector(AFATFS_CACHE_WRITE),
 * create/rename phases, and the unchanged Gate-A full-cluster initializer.
 */
static afatfsOperationStatus_e afatfs_prepareDirectoryRunTarget(
        afatfsCreateFile_t *opState)
{
    uint8_t *sector;
    afatfsOperationStatus_e status;
    uint32_t physicalSector;

    if (afatfs_fileIsBusy(&afatfs.currentDirectory))
        return AFATFS_OPERATION_IN_PROGRESS;

    if (afatfs_isEndOfAllocatedFile(&afatfs.currentDirectory)) {
        status = afatfs_extendSubdirectory(&afatfs.currentDirectory,
                                           NULL,
                                           NULL);
        if (status != AFATFS_OPERATION_SUCCESS)
            return status;
    }

    physicalSector = afatfs_fileGetCursorPhysicalSector(
        &afatfs.currentDirectory);
    if (physicalSector == 0u)
        return AFATFS_OPERATION_FAILURE;
    status = afatfs_cacheSector(physicalSector,
                                &sector,
                                AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    memset(sector, 0, AFATFS_SECTOR_SIZE);
    opState->selectedRunStart.sectorNumberPhysical = physicalSector;
    opState->selectedRunStart.entryIndex = 0;
    return AFATFS_OPERATION_SUCCESS;
}

/*
 * Prove a moved-run target has reached media before exposing it.
 *
 * What: Checks the existing cache descriptor for the selected target sector.
 * IN_SYNC, or absence after preparation/write, means the target has completed
 * its SD write. DIRTY or WRITING means the caller must keep yielding.
 * Why: The old marker sector can already have an older dirty timestamp, so
 * dirty-order alone does not prove target-before-tail persistence. Inputs are
 * the selected target physical sector and cache descriptors. Outputs/effects
 * are a read-only ready/not-ready/failure decision with no polling, I/O,
 * allocation, timestamp, or ownership change. Affiliates: afatfs_poll(),
 * afatfs_flush(), afatfs_findCacheSector(), moved create/rename phases, and
 * old-tail retirement.
 */
static afatfsOperationStatus_e afatfs_directoryRunTargetPersistence(
        const afatfsCreateFile_t *opState)
{
    afatfsCacheBlockDescriptor_t *descriptor =
        afatfs_findCacheSector(opState->selectedRunStart.sectorNumberPhysical);

    if (!descriptor)
        return AFATFS_OPERATION_SUCCESS;

    switch (descriptor->state) {
        case AFATFS_CACHE_STATE_IN_SYNC:
            return AFATFS_OPERATION_SUCCESS;
        case AFATFS_CACHE_STATE_DIRTY:
        case AFATFS_CACHE_STATE_WRITING:
            return AFATFS_OPERATION_IN_PROGRESS;
        case AFATFS_CACHE_STATE_EMPTY:
        case AFATFS_CACHE_STATE_READING:
        default:
            afatfs_assert(false);
            return AFATFS_OPERATION_FAILURE;
    }
}

/*
 * Retire the old terminator tail only after the new boundary is durable.
 *
 * What: Marks every entry from the saved old 0x00 position through entry 15
 * as deleted after a prepared next-sector run and replacement marker have
 * reached media. Why: Changing only the marker could expose stale directory
 * bytes in the skipped tail, while changing the tail first could expose a
 * partial target after reboot. Inputs are the saved old marker pointer and the
 * completed target barrier. Outputs/effects are one dirty read/modify/write
 * cache sector; live entries before the marker are untouched. Affiliates:
 * target preparation/persistence, create completion, rename's write-new-before
 * retire-old ordering, and final afatfs_sync().
 */
static afatfsOperationStatus_e afatfs_retireDirectoryTerminatorTail(
        const afatfsCreateFile_t *opState)
{
    uint8_t *sector;
    afatfsOperationStatus_e status;
    fatDirectoryEntry_t *entries;

    if (opState->reservationOrigin !=
        AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED)
        return AFATFS_OPERATION_SUCCESS;
    if (opState->scanRunStart.sectorNumberPhysical == 0u ||
        opState->scanRunStart.entryIndex < 0 ||
        (uint16_t)opState->scanRunStart.entryIndex >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR)
        return AFATFS_OPERATION_FAILURE;

    status = afatfs_cacheSector(opState->scanRunStart.sectorNumberPhysical,
                                &sector,
                                AFATFS_CACHE_READ | AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entries = (fatDirectoryEntry_t *)sector;
    for (uint8_t i = (uint8_t)opState->scanRunStart.entryIndex;
         i < AFATFS_FILES_PER_DIRECTORY_SECTOR;
         i++) {
        entries[i].filename[0] = FAT_DELETED_FILE_MARKER;
    }
    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    return AFATFS_OPERATION_SUCCESS;
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
    uint8_t cacheFlags;

    /*
     * Write a complete VFAT LFN/SFN run under the selected-marker contract.
     *
     * What: Emits the existing highest-to-lowest LFN fragments followed by
     * their SFN in one sector. When the reservation consumed a terminator it
     * also writes one complete zero entry after the SFN; when it reused deleted
     * entries it leaves the following entry untouched.
     * Why: The replacement marker keeps stale later bytes outside the live
     * namespace, but clearing after a deleted run could erase the first entry
     * of a valid object that follows the hole. Inputs are the selected
     * run/origin, requested and LFN entry counts, sanitized long name,
     * generated FAT filename/checksum, case bits, attributes, and timestamps.
     * Outputs/effects: one valid dirty sector-local VFAT run, optional
     * replacement marker, and file->directoryEntryPos at its SFN. Affiliates:
     * LFN fragment writer/checksum, create state machine, mkdir initializer,
     * object iterator, remove/delete identity, rename writer, and moved-target
     * persistence.
     */
    if (opState->selectedRunStart.sectorNumberPhysical == 0u ||
        opState->requestedEntryCount !=
            (uint8_t)(opState->lfnEntryCount + 1u) ||
        opState->selectedRunStart.entryIndex < 0 ||
        (uint16_t)opState->selectedRunStart.entryIndex +
                (uint16_t)opState->requestedEntryCount >
            AFATFS_FILES_PER_DIRECTORY_SECTOR ||
        ((opState->reservationOrigin ==
              AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL ||
          opState->reservationOrigin ==
              AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) &&
         (uint16_t)opState->selectedRunStart.entryIndex +
                 (uint16_t)opState->requestedEntryCount >=
             AFATFS_FILES_PER_DIRECTORY_SECTOR)) {
        return AFATFS_OPERATION_FAILURE;
    }

    cacheFlags = opState->reservationOrigin ==
        AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED
        ? AFATFS_CACHE_WRITE
        : AFATFS_CACHE_READ | AFATFS_CACHE_WRITE;
    status = afatfs_cacheSector(opState->selectedRunStart.sectorNumberPhysical,
                                &sector,
                                cacheFlags,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entries = (fatDirectoryEntry_t *)sector +
              opState->selectedRunStart.entryIndex;
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

    if (opState->reservationOrigin ==
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL ||
        opState->reservationOrigin ==
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
        memset(&entries[sfnIndex + 1u], 0, sizeof(entries[sfnIndex + 1u]));
    }

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    file->directoryEntryPos = opState->selectedRunStart;
    file->directoryEntryPos.entryIndex =
        (int16_t)(file->directoryEntryPos.entryIndex + opState->lfnEntryCount);
    return AFATFS_OPERATION_SUCCESS;
}

/*
 * Write one SFN into a previously selected directory-entry run.
 *
 * What: Initializes the selected short entry with the existing FAT name,
 * attributes, case bits, and timestamps. A terminator-owned selection also
 * clears the complete following directory entry; a deleted selection touches
 * no byte after its SFN.
 * Why: Short writers such as settings.tmp cannot rely on zero-filled bytes
 * after the chosen slot, and a second allocator scan would lose whether the
 * selected entry consumed the live namespace marker. Inputs are reservation
 * origin/start, requested attributes/type, raw FAT filename, case bits, and
 * create callback state. Outputs/effects are a dirty selected sector,
 * file->directoryEntryPos at the SFN, an optional replacement 0x00 entry, and
 * unchanged regular-file or mkdir completion flow. Affiliates:
 * afatfs_fopen(), afatfs_mkdir(), settings safe-write,
 * afatfs_handoffCreatedDirectoryToInitializer(), moved-target barrier, and
 * close/truncate metadata publication.
 */
static afatfsOperationStatus_e afatfs_createShortDirectoryEntry(
        afatfsFile_t *file)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    afatfsOperationStatus_e status;
    uint8_t *sector;
    fatDirectoryEntry_t *entry;
    uint8_t cacheFlags;

    if (opState->selectedRunStart.sectorNumberPhysical == 0u ||
        opState->requestedEntryCount != 1u ||
        opState->selectedRunStart.entryIndex < 0 ||
        (uint16_t)opState->selectedRunStart.entryIndex >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR ||
        ((opState->reservationOrigin ==
              AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL ||
          opState->reservationOrigin ==
              AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) &&
         (uint16_t)opState->selectedRunStart.entryIndex + 1u >=
             AFATFS_FILES_PER_DIRECTORY_SECTOR)) {
        return AFATFS_OPERATION_FAILURE;
    }

    cacheFlags = opState->reservationOrigin ==
        AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED
        ? AFATFS_CACHE_WRITE
        : AFATFS_CACHE_READ | AFATFS_CACHE_WRITE;
    status = afatfs_cacheSector(opState->selectedRunStart.sectorNumberPhysical,
                                &sector,
                                cacheFlags,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    entry = (fatDirectoryEntry_t *)sector + opState->selectedRunStart.entryIndex;
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->filename, opState->filename, FAT_FILENAME_LENGTH);
    entry->attrib = file->attrib;
    entry->ntReserved = opState->shortNameCaseFlags;
    entry->creationDate = AFATFS_DEFAULT_FILE_DATE;
    entry->creationTime = AFATFS_DEFAULT_FILE_TIME;
    entry->lastWriteDate = AFATFS_DEFAULT_FILE_DATE;
    entry->lastWriteTime = AFATFS_DEFAULT_FILE_TIME;

    if (opState->reservationOrigin ==
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL ||
        opState->reservationOrigin ==
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
        memset(entry + 1, 0, sizeof(*entry));
    }

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    file->directoryEntryPos = opState->selectedRunStart;
    return AFATFS_OPERATION_SUCCESS;
}

static void afatfs_handoffCreatedDirectoryToInitializer(
    afatfsFile_t *file,
    afatfsFileCallback_t callback)
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
     * Inputs: file is the newly-created directory entry; callback is the
     * original afatfs_mkdir()/afatfs_mkdir_lfn() completion. Output: callback
     * is invoked by EXTEND_SUBDIRECTORY with file or NULL. Affiliates:
     * afatfs_createFileContinue(), afatfs_extendSubdirectory(),
     * afatfs_appendRegularFreeClusterContinue(), afatfs_saveDirectoryEntry().
     */
    afatfs_resetDirectoryRunReservation(&file->operation.state.createFile);
    afatfs_lfnScanReset(&file->operation.state.createFile);
    file->operation.state.createFile.requestedEntryCount = 0u;
    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
    (void)afatfs_extendSubdirectory(file, &afatfs.currentDirectory, callback);
}

static void afatfs_createFileContinue(afatfsFile_t *file)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    fatDirectoryEntry_t *entry;
    afatfsOperationStatus_e status;
    afatfsFileCallback_t callback;

    doMore:

    switch (opState->phase) {
        case AFATFS_CREATEFILE_PHASE_INITIAL:
            afatfs_findFirst(&afatfs.currentDirectory, &file->directoryEntryPos);
            afatfs_resetDirectoryRunReservation(opState);
            afatfs_lfnScanReset(opState);
            opState->phase = AFATFS_CREATEFILE_PHASE_FIND_FILE;
            goto doMore;
        break;
        case AFATFS_CREATEFILE_PHASE_FIND_FILE:
            do {
                status = afatfs_findNext(&afatfs.currentDirectory, &file->directoryEntryPos, &entry);

                switch (status) {
                    case AFATFS_OPERATION_SUCCESS:
                        /*
                         * Terminator-aware create/open collision scan.
                         *
                         * What: Scans live SFN/LFN objects and deleted holes
                         * only until the first 0x00. Deleted runs are latched
                         * as candidates while matching continues. At the
                         * marker, open-only reports absence and create mode
                         * receives a deleted-, local-terminator-, or
                         * moved-terminator run.
                         * Why: FAT guarantees that no live object follows
                         * 0x00. Continuing to physical exhaustion wastes I/O
                         * and destroys the persistent boundary, while
                         * creating immediately in a deleted hole can duplicate
                         * a matching object later in the live prefix. Inputs
                         * are file mode, requested type, display/match mode,
                         * SFN alias, LFN scanner state, and shared reservation.
                         * Outputs/effects are an existing open, collision or
                         * absence, alias restart, safe selected run, or
                         * target preparation request. Affiliates: alias
                         * generation, LFN scan helpers, raw finder ownership,
                         * create phases, and both directory-entry writers.
                         */
                        if (entry == NULL) {
                            afatfs_findLast(&afatfs.currentDirectory);

                            if ((file->mode & AFATFS_FILE_MODE_CREATE) == 0u) {
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }

                            if (opState->reservationOrigin ==
                                AFATFS_DIRECTORY_RUN_ORIGIN_DELETED) {
                                opState->phase = opState->lfnEntryCount
                                    ? AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE
                                    : AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE;
                                goto doMore;
                            }

                            /* A no-terminator directory either extends at its
                             * logical EOF or fails normally for a fixed root.
                             * The unchanged extension initializer makes entry
                             * zero a local terminator-owned run.
                             */
                            opState->reservationOrigin =
                                AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL;
                            opState->phase =
                                AFATFS_CREATEFILE_PHASE_PREPARE_TARGET_SECTOR;
                            goto doMore;
                        } else if (fat_isDirectoryEntryTerminator(entry)) {
                            afatfs_noteDeletedDirectoryEntry(
                                opState, &file->directoryEntryPos, false);
                            afatfs_findLast(&afatfs.currentDirectory);

                            if ((file->mode & AFATFS_FILE_MODE_CREATE) == 0u) {
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                            if (afatfs_selectDirectoryRunAtTerminator(
                                    opState, &file->directoryEntryPos)) {
                                opState->phase = opState->lfnEntryCount
                                    ? AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE
                                    : AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE;
                            } else if (opState->reservationOrigin ==
                                       AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
                                opState->phase =
                                    AFATFS_CREATEFILE_PHASE_SEEK_NEXT_SECTOR;
                            } else {
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                            }
                            goto doMore;
                        } else if (fat_isDirectoryEntryEmpty(entry)) {
                            afatfs_noteDeletedDirectoryEntry(
                                opState, &file->directoryEntryPos, true);
                            afatfs_lfnScanReset(opState);
                        } else if (afatfs_isLfnDirectoryEntry(entry)) {
                            afatfs_noteDeletedDirectoryEntry(
                                opState, &file->directoryEntryPos, false);
                            if (opState->lfnEntryCount != 0u)
                                afatfs_lfnScanAppend(opState, entry);
                            else
                                afatfs_lfnScanReset(opState);
                        } else if (opState->lfnEntryCount != 0u) {
                            afatfs_noteDeletedDirectoryEntry(
                                opState, &file->directoryEntryPos, false);
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
                                 * Do not resolve a same-display-name file as a
                                 * directory or a directory as a file. FAT
                                 * permits both attributes in raw entries, but
                                 * this component API is typed by its caller:
                                 * mkdir_lfn() requests directories and
                                 * fopen_lfn() requests archive files. A type
                                 * mismatch is a real collision, not an
                                 * invitation to make another visible duplicate.
                                 */
                                if (requestedDirectory != existingDirectory) {
                                    afatfs_findLast(&afatfs.currentDirectory);
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

                                afatfs_findLast(&afatfs.currentDirectory);

                                opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                                goto doMore;
                            }
                            if (strncmp(entry->filename,
                                        (char*) opState->filename,
                                        FAT_FILENAME_LENGTH) != 0) {
                                afatfs_lfnScanReset(opState);
                                break;
                            }
                            /*
                             * Alias collision is only useful when creation is
                             * allowed.
                             *
                             * Read-only LFN opens cannot resolve by inventing
                             * a new "~N" alias, but a colliding SFN is not
                             * proof that the requested display component is
                             * absent: a later live LFN object may still match.
                             * Create/write modes generate the next candidate
                             * and restart the complete scan.
                             */
                            if ((file->mode & AFATFS_FILE_MODE_CREATE) == 0u) {
                                afatfs_lfnScanReset(opState);
                                break;
                            }
                            opState->aliasOrdinal++;
                            if (!afatfs_generateShortAlias(opState)) {
                                afatfs_findLast(&afatfs.currentDirectory);
                                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                                goto doMore;
                            }
                            afatfs_findFirst(&afatfs.currentDirectory,
                                             &file->directoryEntryPos);
                            afatfs_resetDirectoryRunReservation(opState);
                            afatfs_lfnScanReset(opState);
                            break;
                        } else if (strncmp(entry->filename, (char*) opState->filename, FAT_FILENAME_LENGTH) == 0) {
                            afatfs_noteDeletedDirectoryEntry(
                                opState, &file->directoryEntryPos, false);
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

                            afatfs_findLast(&afatfs.currentDirectory);

                            opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                            goto doMore;
                        } else {
                            afatfs_noteDeletedDirectoryEntry(
                                opState, &file->directoryEntryPos, false);
                            afatfs_lfnScanReset(opState);
                        } // Else this entry doesn't match, fall through and continue the search
                    break;
                    case AFATFS_OPERATION_FAILURE:
                        afatfs_findLast(&afatfs.currentDirectory);
                        opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                        goto doMore;
                    break;
                    case AFATFS_OPERATION_IN_PROGRESS:
                        ;
                }
            } while (status == AFATFS_OPERATION_SUCCESS);
        break;
        case AFATFS_CREATEFILE_PHASE_SEEK_NEXT_SECTOR:
            if (afatfs_fileIsBusy(&afatfs.currentDirectory) ||
                !afatfs_fseekAtomic(&afatfs.currentDirectory,
                                    AFATFS_SECTOR_SIZE)) {
                return;
            }
            opState->phase = AFATFS_CREATEFILE_PHASE_PREPARE_TARGET_SECTOR;
            goto doMore;
        break;
        case AFATFS_CREATEFILE_PHASE_PREPARE_TARGET_SECTOR:
            status = afatfs_prepareDirectoryRunTarget(opState);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                goto doMore;
            }
            opState->phase = opState->lfnEntryCount
                ? AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE
                : AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE;
            goto doMore;
        break;
        case AFATFS_CREATEFILE_PHASE_CREATE_NEW_FILE:
            status = afatfs_createShortDirectoryEntry(file);

            if (status == AFATFS_OPERATION_SUCCESS) {
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
                if (opState->reservationOrigin ==
                    AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
                    opState->phase =
                        AFATFS_CREATEFILE_PHASE_WAIT_TARGET_PERSISTENCE;
                    goto doMore;
                }
                if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
                    callback = opState->callback;
                    afatfs_handoffCreatedDirectoryToInitializer(file, callback);
                    return;
                }
                opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
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
                if (opState->reservationOrigin ==
                    AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
                    opState->phase =
                        AFATFS_CREATEFILE_PHASE_WAIT_TARGET_PERSISTENCE;
                    goto doMore;
                }
                if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
                    callback = opState->callback;
                    afatfs_handoffCreatedDirectoryToInitializer(file, callback);
                    return;
                }
                opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
                goto doMore;
            } else if (status == AFATFS_OPERATION_FAILURE) {
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                goto doMore;
            }
        break;
        case AFATFS_CREATEFILE_PHASE_WAIT_TARGET_PERSISTENCE:
            status = afatfs_directoryRunTargetPersistence(opState);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                goto doMore;
            }
            opState->phase =
                AFATFS_CREATEFILE_PHASE_RETIRE_OLD_TERMINATOR_TAIL;
            goto doMore;
        break;
        case AFATFS_CREATEFILE_PHASE_RETIRE_OLD_TERMINATOR_TAIL:
            status = afatfs_retireDirectoryTerminatorTail(opState);
            if (status == AFATFS_OPERATION_IN_PROGRESS)
                return;
            if (status == AFATFS_OPERATION_FAILURE) {
                opState->phase = AFATFS_CREATEFILE_PHASE_FAILURE;
                goto doMore;
            }
            if (file->type == AFATFS_FILE_TYPE_DIRECTORY) {
                callback = opState->callback;
                afatfs_handoffCreatedDirectoryToInitializer(file, callback);
                return;
            }
            opState->phase = AFATFS_CREATEFILE_PHASE_SUCCESS;
            goto doMore;
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
                    // This replaces our open file operation
                    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
                    afatfs_fseekInternal(file, file->logicalSize, opState->callback);
                    break;
                }

                // If we're only writing (not reading) the file must be truncated
                if (file->mode == (AFATFS_FILE_MODE_CREATE | AFATFS_FILE_MODE_WRITE)) {
                    // This replaces our open file operation
                    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
                    afatfs_ftruncate(file, opState->callback);
                    break;
                }
            }

            callback = opState->callback;
            afatfs_resetDirectoryRunReservation(opState);
            opState->requestedEntryCount = 0u;
            file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            callback(file);
        break;
        case AFATFS_CREATEFILE_PHASE_FAILURE:
            /*
             * Release the handle on every failed create/open.
             *
             * The old path only changed type, but the LFN path has more
             * validation/collision-failure exits. Clearing the queued operation
             * here guarantees callers can retry and the open-file pool does not
             * retain a dead CREATE_FILE operation after callback(NULL).
             */
            callback = opState->callback;
            afatfs_resetDirectoryRunReservation(opState);
            opState->requestedEntryCount = 0u;
            file->type = AFATFS_FILE_TYPE_NONE;
            file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            if (callback)
                callback(NULL);
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
        afatfsFilePtr_t file,
        const char *name,
        uint8_t attrib,
        uint8_t fileMode,
        afatfsMatchMode_t matchMode,
        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
        bool createLongName,
        afatfsFileCallback_t callback)
{
    afatfsCreateFile_t *opState = &file->operation.state.createFile;
    uint8_t longNameLength = 0u;

    afatfs_initFileHandle(file);

    // Queue the operation to finish the file creation
    file->operation.operation = AFATFS_FILE_OPERATION_CREATE_FILE;

    file->mode = fileMode;
    opState->matchMode = matchMode;

    if (strcmp(name, ".") == 0) {
        file->firstCluster = afatfs.currentDirectory.firstCluster;
        file->physicalSize = afatfs.currentDirectory.physicalSize;
        file->logicalSize = afatfs.currentDirectory.logicalSize;
        file->attrib = afatfs.currentDirectory.attrib;
        file->type = afatfs.currentDirectory.type;
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
                file->type = AFATFS_FILE_TYPE_NONE;
                file->operation.operation = AFATFS_FILE_OPERATION_NONE;
                if (callback)
                    callback(NULL);
                return file;
            }
            opState->lfnEntryCount =
                (uint8_t)((longNameLength + 12u) / 13u);
            /*
             * LFN requests deliberately retain their complete VFAT run even
             * when the display text could fit an 8.3 alias. This byte is the
             * scan discriminator and the writer's immutable entry count.
             */
            opState->requestedEntryCount =
                (uint8_t)(opState->lfnEntryCount + 1u);
            opState->aliasOrdinal = 0u;
            opState->openNameOut = openNameOut;
            if (!afatfs_generateShortAlias(opState)) {
                file->type = AFATFS_FILE_TYPE_NONE;
                file->operation.operation = AFATFS_FILE_OPERATION_NONE;
                if (callback)
                    callback(NULL);
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
            /* Short APIs reserve exactly their SFN slot; shared collision and
             * terminator handling still applies without an LFN fragment. */
            opState->requestedEntryCount = 1u;
        }
        file->attrib = attrib;

        if ((attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) != 0) {
            file->type = AFATFS_FILE_TYPE_DIRECTORY;
        } else {
            file->type = AFATFS_FILE_TYPE_NORMAL;
        }
    }

    opState->callback = callback;

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
     * In-place rename still needs the existing single-sector writer. The
     * shared retirement continuation below is independent of this placement
     * optimization and accepts valid runs crossing sectors or clusters.
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

static afatfsResultCode_t afatfs_validateObjectInfo(
        const afatfsObjectInfo_t *object,
        afatfsObjectKind_t requiredKind)
{
    if (!object || object->id.kind != requiredKind || object->lfnMalformed ||
        object->id.sfnEntry.sectorNumberPhysical == 0u ||
        object->id.sfnEntry.entryIndex < 0 ||
        (uint16_t)object->id.sfnEntry.entryIndex >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR ||
        (object->id.firstCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER &&
         !(object->id.kind == AFATFS_OBJECT_FILE &&
           object->id.firstCluster == 0u && object->id.logicalSize == 0u)) ||
        object->id.lfnEntryCount > AFATFS_LONG_FILENAME_ENTRY_MAX) {
        return AFATFS_RESULT_UNSUPPORTED_LAYOUT;
    }
    if (object->id.lfnEntryCount != 0u &&
        object->id.lfnFirstEntry.sectorNumberPhysical == 0u) {
        return AFATFS_RESULT_CORRUPT_LFN_RUN;
    }
    for (uint8_t i = 0u; i < object->id.lfnEntryCount; i++) {
        const afatfsDirEntryPointer_t *entry =
            (i == 0u) ? &object->id.lfnFirstEntry
                      : &object->id.lfnFollowingEntry[i - 1u];
        if (entry->sectorNumberPhysical == 0u || entry->entryIndex < 0 ||
            (uint16_t)entry->entryIndex >= AFATFS_FILES_PER_DIRECTORY_SECTOR)
            return AFATFS_RESULT_CORRUPT_LFN_RUN;
    }
    return AFATFS_RESULT_OK;
}

static const afatfsDirEntryPointer_t *afatfs_objectNameEntryAt(
        const afatfsObjectInfo_t *object,
        uint8_t ordinal)
{
    if (ordinal < object->id.lfnEntryCount) {
        return (ordinal == 0u) ? &object->id.lfnFirstEntry
                              : &object->id.lfnFollowingEntry[ordinal - 1u];
    }
    return &object->id.sfnEntry;
}

static afatfsOperationStatus_e afatfs_retireObjectNameRun(
        const afatfsObjectInfo_t *object,
        uint8_t *nextOrdinal)
{
    uint8_t *sector;
    afatfsOperationStatus_e status;
    uint8_t entryCount;
    uint32_t sectorNumber;

    /*
     * Retire one object's complete VFAT name entry run.
     *
     * What: Marks the checksum-verified LFN fragments and the owning SFN entry
     * as deleted. It operates only on the directory entries that name the
     * object.
     *
     * Why: Removing or moving a VFAT object must not leave orphan display
     * fragments visible to later scans. All destructive callers now use this
     * complete-run continuation rather than an SFN-only unlink shortcut.
     *
     * Inputs: afatfsObjectInfo_t from afatfs_findNextObject(), plus a caller-
     * owned next ordinal initially set to zero. The complete physical pointer
     * list identifies every fragment; no pointer arithmetic or display-name
     * lookup is performed. Outputs: one same-sector batch is marked deleted
     * and nextOrdinal advances; callers yield before requesting the next batch.
     *
     * Outputs/effects: directory cache sector is marked dirty after entries are
     * marked deleted. It does not free clusters and does not inspect directory
     * children.
     *
     * Affiliates/clients: afatfs_renameObjectRetireOldRun(),
     * afatfs_removeObjects_lfn(), filesystem.c recursive delete, and save
     * overwrite preflight.
     */
    if (!nextOrdinal || afatfs_validateObjectInfo(object, object->id.kind) !=
        AFATFS_RESULT_OK)
        return AFATFS_OPERATION_FAILURE;
    entryCount = (uint8_t)(object->id.lfnEntryCount + 1u);
    if (*nextOrdinal >= entryCount)
        return AFATFS_OPERATION_SUCCESS;
    sectorNumber = afatfs_objectNameEntryAt(object, *nextOrdinal)
                       ->sectorNumberPhysical;
    status = afatfs_cacheSector(sectorNumber,
                                &sector,
                                AFATFS_CACHE_READ | AFATFS_CACHE_WRITE,
                                0);
    if (status != AFATFS_OPERATION_SUCCESS)
        return status;

    while (*nextOrdinal < entryCount) {
        const afatfsDirEntryPointer_t *entryPointer =
            afatfs_objectNameEntryAt(object, *nextOrdinal);
        if (entryPointer->sectorNumberPhysical != sectorNumber)
            break;
        /*
         * Mark every name entry in the run deleted.
         *
         * This includes all LFN fragments and the owning SFN entry. The loop
         * does not touch clusters; callers that delete file data free the
         * cluster chain before retiring the visible name metadata.
         */
        ((fatDirectoryEntry_t *)sector)[entryPointer->entryIndex]
            .filename[0] = FAT_DELETED_FILE_MARKER;
        (*nextOrdinal)++;
    }

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    return AFATFS_OPERATION_SUCCESS;
}

static void afatfs_renameObjectSetOldRunStart(afatfsRenameObject_t *op)
{
    op->oldEntryCount = (uint8_t)(op->source.id.lfnEntryCount + 1u);
    op->oldRunNextEntry = 0u;
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

static void afatfs_renameObjectFinish(afatfsResultCode_t result)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;
    afatfsResultCallback_t callback = op->callback;

    /*
     * Complete rename ownership before returning to the caller.
     *
     * The embedded create-state object owns the collision reservation, LFN
     * scanner, and target pointers. Reset those bytes before the callback can
     * start another rename; names, callback storage, and the fixed operation
     * object itself remain statically allocated and no retained state grows.
     */
    op->result = result;
    op->succeeded = (result == AFATFS_RESULT_OK) ? 1u : 0u;
    if (result == AFATFS_RESULT_OK) {
        afatfs_renameObjectCopyOpenName(op->generatedOpenName,
                                        op->openNameOut);
    }
    afatfs_resetDirectoryRunReservation(&op->newNameState);
    afatfs_lfnScanReset(&op->newNameState);
    op->newNameState.requestedEntryCount = 0u;
    op->active = 0u;
    if (callback)
        callback(result);
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
    /*
     * Restart from a clean reservation.
     *
     * Alias retries must discard both the current deleted run and any marker
     * decision from the previous candidate, then rebuild the raw finder and
     * LFN chain for the new SFN alias. The reset is state-only and preserves
     * the request's sanitized name, requested count, alias ordinal, and
     * callback output.
     */
    afatfs_resetDirectoryRunReservation(&op->newNameState);
    afatfs_findFirst(&afatfs.currentDirectory, &op->rawFinder);
    afatfs_lfnScanReset(&op->newNameState);
}

static bool afatfs_renameObjectCanRewriteInPlace(
        const afatfsRenameObject_t *op)
{
    uint8_t newEntryCount = op->newNameState.requestedEntryCount;

    if (newEntryCount == 0u || newEntryCount > op->oldEntryCount)
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
    uint8_t cacheFlags;
    uint8_t newLfnCount = op->newNameState.lfnEntryCount;
    uint8_t newEntryCount = op->newNameState.requestedEntryCount;
    uint8_t checksum = afatfs_lfnChecksum(op->newNameState.filename);

    /*
     * Publish a selected rename run, preserving marker provenance.
     *
     * What: Writes the LFN fragments and SFN into the already-selected
     * sector-local run. Terminator-owned runs also receive a complete zero
     * replacement entry immediately after the SFN; deleted-hole runs leave
     * following entries unchanged. Moved runs are written through a WRITE-only
     * cache request because their target sector was zeroed before selection.
     * Why: The new name must become valid before the old source is retired, and
     * a deleted-hole write must not erase a later live entry. Inputs are the
     * requested count, origin, selected target, source metadata, and generated
     * alias. Outputs/effects are one dirty target sector and, for local/moved
     * marker origins, a replacement 0x00 entry. Affiliates: collision scan,
     * target preparation/barrier, old-tail retirement, and old-run retirement.
     */
    if (op->newRunStart.sectorNumberPhysical == 0u ||
        newEntryCount != (uint8_t)(newLfnCount + 1u) ||
        op->newRunStart.entryIndex < 0 ||
        (uint16_t)op->newRunStart.entryIndex + (uint16_t)newEntryCount >
            AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        return AFATFS_OPERATION_FAILURE;
    }
    if ((op->newNameState.reservationOrigin ==
             AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL ||
         op->newNameState.reservationOrigin ==
             AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) &&
        (uint16_t)op->newRunStart.entryIndex +
                (uint16_t)newEntryCount >=
            AFATFS_FILES_PER_DIRECTORY_SECTOR) {
        return AFATFS_OPERATION_FAILURE;
    }

    cacheFlags = op->newNameState.reservationOrigin ==
        AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED
        ? AFATFS_CACHE_WRITE
        : AFATFS_CACHE_READ | AFATFS_CACHE_WRITE;
    status = afatfs_cacheSector(op->newRunStart.sectorNumberPhysical,
                                &sector,
                                cacheFlags,
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

    if (op->newNameState.reservationOrigin ==
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL ||
        op->newNameState.reservationOrigin ==
            AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
        memset(&entries[newLfnCount + 1u], 0,
               sizeof(entries[newLfnCount + 1u]));
    }

    afatfs_cacheSectorMarkDirty(afatfs_getCacheDescriptorForBuffer(sector));
    return AFATFS_OPERATION_SUCCESS;
}

static afatfsOperationStatus_e afatfs_renameObjectRetireOldRun(
        afatfsRenameObject_t *op)
{
    (void)op->oldRunStart;
    (void)op->oldEntryCount;
    return afatfs_retireObjectNameRun(&op->source,
                                      &op->oldRunNextEntry);
}

static void afatfs_renameObjectChooseRun(afatfsRenameObject_t *op)
{
    /*
     * Choose placement in the required priority order.
     *
     * A same-sector shrink/fit reuses the source run first. Otherwise the
     * collision scan's selected deleted or terminator-derived target is used;
     * no second allocator scan is permitted here. movedEntryRun records
     * whether the old source must be retired after the new run is published.
     */
    if (afatfs_renameObjectCanRewriteInPlace(op)) {
        op->movedEntryRun = 0u;
        op->newRunStart = op->source.id.sfnEntry;
        op->newRunStart.entryIndex =
            (int16_t)(op->newRunStart.entryIndex -
                      op->newNameState.lfnEntryCount);
    } else {
        op->movedEntryRun = 1u;
        op->newRunStart = op->newNameState.selectedRunStart;
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
        if (afatfs_fileIsBusy(&afatfs.currentDirectory))
            return;
        afatfs_findFirstObject(&afatfs.currentDirectory, &op->objectFinder);
        op->phase = AFATFS_RENAME_OBJECT_PHASE_FIND_SOURCE;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_FIND_SOURCE:
        status = afatfs_findNextObject(&afatfs.currentDirectory,
                                       &op->objectFinder,
                                       &op->source);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            afatfs_findLastObject(&afatfs.currentDirectory, &op->objectFinder);
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (op->source.id.kind == AFATFS_OBJECT_NONE) {
            op->result = AFATFS_RESULT_NOT_FOUND;
            afatfs_findLastObject(&afatfs.currentDirectory, &op->objectFinder);
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
        if (op->source.lfnMalformed) {
            op->result = AFATFS_RESULT_CORRUPT_LFN_RUN;
            afatfs_findLastObject(&afatfs.currentDirectory, &op->objectFinder);
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        afatfs_findLastObject(&afatfs.currentDirectory, &op->objectFinder);
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
            op->result = AFATFS_RESULT_IO_ERROR;
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
        if (fat_compareDisplayName(op->source.id.displayName,
                                   op->newName,
                                   true) == 0) {
            afatfs_renameObjectCopyOpenName(op->source.id.shortName,
                                            op->generatedOpenName);
            op->result = AFATFS_RESULT_OK;
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
            op->result = AFATFS_RESULT_INVALID_NAME;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->newNameState.lfnEntryCount = (uint8_t)((len + 12u) / 13u);
        /* The collision scan and writer share this immutable LFN/SFN run size. */
        op->newNameState.requestedEntryCount =
            (uint8_t)(op->newNameState.lfnEntryCount + 1u);
        op->newNameState.matchMode = op->matchMode;
        op->newNameState.aliasOrdinal = 0u;
        op->newNameState.openNameOut = op->generatedOpenName;
        if (!afatfs_generateShortAlias(&op->newNameState)) {
            op->result = AFATFS_RESULT_ALREADY_EXISTS;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN;
        goto doMore;
    }

    case AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN:
        if (afatfs_fileIsBusy(&afatfs.currentDirectory))
            return;
        afatfs_renameObjectRestartCollisionScan(op);
        op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN:
    {
        /*
         * Rename collision scan and reservation selection.
         *
         * What: Scans only through the first 0x00, records sector-local E5
         * runs without ending the scan, and selects in-place, deleted-hole,
         * local-terminator, or moved-terminator placement in that order. An
         * alias collision restarts the complete scan with a clean reservation.
         * Why: The source object may be the only matching live name, while a
         * later live object can still collide with a generated alias. The old
         * marker must stay untouched until the new run is ready to publish.
         * Inputs: source identity, requested LFN/SFN count, candidate alias,
         * match mode, raw finder, and shared reservation state. Outputs/effects:
         * a selected run or a resumable target-preparation phase; scan cache
         * ownership is released before every placement/restart/failure path.
         * Affiliates: afatfs_findNext(), afatfs_findLast(), alias generation,
         * deleted-run helpers, target barrier, and rename run writer.
         */
        fatDirectoryEntry_t *entry = NULL;
        status = afatfs_findNext(&afatfs.currentDirectory,
                                 &op->rawFinder,
                                 &entry);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            afatfs_findLast(&afatfs.currentDirectory);
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (entry == NULL) {
            afatfs_findLast(&afatfs.currentDirectory);
            if (afatfs_renameObjectCanRewriteInPlace(op) ||
                op->newNameState.reservationOrigin ==
                    AFATFS_DIRECTORY_RUN_ORIGIN_DELETED) {
                afatfs_renameObjectChooseRun(op);
                op->phase = AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN;
                goto doMore;
            }
            op->newNameState.reservationOrigin =
                AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_TARGET;
            goto doMore;
        }
        if (fat_isDirectoryEntryTerminator(entry)) {
            afatfs_noteDeletedDirectoryEntry(
                &op->newNameState, &op->rawFinder, false);
            afatfs_findLast(&afatfs.currentDirectory);
            if (afatfs_renameObjectCanRewriteInPlace(op) ||
                afatfs_selectDirectoryRunAtTerminator(
                    &op->newNameState, &op->rawFinder)) {
                afatfs_renameObjectChooseRun(op);
                op->phase = AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN;
                goto doMore;
            }
            if (op->newNameState.reservationOrigin ==
                AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
                op->phase = AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_SEEK;
                goto doMore;
            }
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (fat_isDirectoryEntryEmpty(entry)) {
            afatfs_noteDeletedDirectoryEntry(
                &op->newNameState, &op->rawFinder, true);
            afatfs_lfnScanReset(&op->newNameState);
            return;
        }
        if (afatfs_isLfnDirectoryEntry(entry)) {
            afatfs_noteDeletedDirectoryEntry(
                &op->newNameState, &op->rawFinder, false);
            afatfs_lfnScanAppend(&op->newNameState, entry);
            return;
        }
        if ((entry->attrib & FAT_FILE_ATTRIBUTE_VOLUME_ID) != 0u) {
            afatfs_noteDeletedDirectoryEntry(
                &op->newNameState, &op->rawFinder, false);
            afatfs_lfnScanReset(&op->newNameState);
            return;
        }
        if (!afatfs_entryPointerEquals(&op->rawFinder, &op->source.id.sfnEntry) &&
            afatfs_renameObjectRawEntryMatchesNew(op, entry)) {
            afatfs_findLast(&afatfs.currentDirectory);
            op->result = AFATFS_RESULT_ALREADY_EXISTS;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (!afatfs_entryPointerEquals(&op->rawFinder, &op->source.id.sfnEntry) &&
            memcmp(entry->filename,
                   op->newNameState.filename,
                   FAT_FILENAME_LENGTH) == 0) {
            afatfs_findLast(&afatfs.currentDirectory);
            op->newNameState.aliasOrdinal++;
            if (!afatfs_generateShortAlias(&op->newNameState)) {
                op->result = AFATFS_RESULT_ALREADY_EXISTS;
                op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
                goto doMore;
            }
            op->phase = AFATFS_RENAME_OBJECT_PHASE_COLLISION_SCAN_BEGIN;
            goto doMore;
        }
        afatfs_noteDeletedDirectoryEntry(
            &op->newNameState, &op->rawFinder, false);
        afatfs_lfnScanReset(&op->newNameState);
        return;
    }

    case AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_SEEK:
        /*
         * Move by logical FAT traversal; physical-sector increment is invalid
         * because the next directory sector may be in a non-contiguous FAT
         * cluster.
         */
        if (afatfs_fileIsBusy(&afatfs.currentDirectory) ||
            !afatfs_fseekAtomic(&afatfs.currentDirectory, AFATFS_SECTOR_SIZE))
            return;
        op->phase = AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_TARGET;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_PREPARE_NEW_RUN_TARGET:
        status = afatfs_prepareDirectoryRunTarget(&op->newNameState);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        afatfs_renameObjectChooseRun(op);
        op->phase = AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_WAIT_TARGET_PERSISTENCE:
        status = afatfs_directoryRunTargetPersistence(&op->newNameState);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_TERMINATOR_TAIL;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_TERMINATOR_TAIL:
        status = afatfs_retireDirectoryTerminatorTail(&op->newNameState);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        op->phase = AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_WRITE_NEW_RUN:
        status = afatfs_renameObjectWriteRun(op);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
            goto doMore;
        }
        if (op->movedEntryRun &&
            op->newNameState.reservationOrigin ==
                AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED) {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_WAIT_TARGET_PERSISTENCE;
        } else if (op->movedEntryRun) {
            op->phase = AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN;
        } else {
            op->result = AFATFS_RESULT_OK;
            op->succeeded = 1u;
            op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
        }
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_RETIRE_OLD_RUN:
        status = afatfs_renameObjectRetireOldRun(op);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_SUCCESS &&
            op->oldRunNextEntry < op->source.id.lfnEntryCount + 1u)
            return;
        if (status == AFATFS_OPERATION_SUCCESS)
            op->result = AFATFS_RESULT_OK;
        if (status == AFATFS_OPERATION_SUCCESS)
            op->succeeded = 1u;
        else
            op->result = AFATFS_RESULT_IO_ERROR;
        op->phase = AFATFS_RENAME_OBJECT_PHASE_FINISH;
        goto doMore;

    case AFATFS_RENAME_OBJECT_PHASE_FINISH:
        afatfs_renameObjectFinish(op->succeeded ? AFATFS_RESULT_OK : op->result);
        return;
    }
}

bool afatfs_renameObject_lfn(const char *oldDisplayName,
                             const char *newDisplayName,
                             afatfsMatchMode_t matchMode,
                             char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                             afatfsResultCallback_t complete)
{
    afatfsRenameObject_t *op = &afatfs.renameObject;

    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active ||
        afatfs_fileIsBusy(&afatfs.currentDirectory)) {
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
    op->result = AFATFS_RESULT_NOT_FOUND;
    op->phase = AFATFS_RENAME_OBJECT_PHASE_INITIAL;
    op->matchMode = matchMode;
    op->callback = complete;
    op->openNameOut = openNameOut;
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

static void afatfs_removeObjectsFinish(afatfsResultCode_t result)
{
    afatfsRemoveObjects_t *op = &afatfs.removeObjects;
    afatfsResultCallback_t callback = op->callback;

    /*
     * Finish exactly once and release the global removal slot.
     *
     * complete is deliberately callback-only like rename: callers sequence the
     * next open/create phase from their own filesystem state machine, while
     * asyncfatfs keeps no persistent success object to return.
     */
    op->result = result;
    op->succeeded = (result == AFATFS_RESULT_OK) ? 1u : 0u;
    op->active = 0u;
    if (callback)
        callback(result);
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
            op->result = AFATFS_RESULT_IO_ERROR;
            afatfs_findLastObject(&afatfs.currentDirectory, &op->finder);
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        if (op->object.id.kind == AFATFS_OBJECT_NONE) {
            op->result = AFATFS_RESULT_OK;
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
        if (op->object.lfnMalformed) {
            op->result = AFATFS_RESULT_CORRUPT_LFN_RUN;
            afatfs_findLastObject(&afatfs.currentDirectory, &op->finder);
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
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
        op->nameRunNextEntry = 0u;
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
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        op->sourceEntry =
            ((fatDirectoryEntry_t *)sector)[op->object.id.sfnEntry.entryIndex];
        if (op->object.id.kind == AFATFS_OBJECT_FILE ||
            op->mode == AFATFS_REMOVE_EMPTY_DIRECTORIES) {
            afatfs_removeObjectPrepareSyntheticFile(op);
            if (!afatfs_ftruncate(&op->syntheticFile, NULL)) {
                op->result = AFATFS_RESULT_IO_ERROR;
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
        /* Release the chain while the SFN remains intact; the shared complete
         * LFN/SFN retire continuation runs only after FAT cleanup succeeds. */
        status = afatfs_ftruncateContinue(&op->syntheticFile, false);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
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
        status = afatfs_retireObjectNameRun(&op->object,
                                            &op->nameRunNextEntry);
        if (status == AFATFS_OPERATION_IN_PROGRESS)
            return;
        if (status == AFATFS_OPERATION_FAILURE) {
            op->result = AFATFS_RESULT_IO_ERROR;
            op->phase = AFATFS_REMOVE_OBJECTS_PHASE_FINISH;
            goto doMore;
        }
        if (op->nameRunNextEntry < op->object.id.lfnEntryCount + 1u)
            return;
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
        afatfs_removeObjectsFinish(op->succeeded ? AFATFS_RESULT_OK : op->result);
        return;
    }
}

bool afatfs_removeObjects_lfn(const char *displayName,
                              afatfsMatchMode_t matchMode,
                              afatfsRemoveObjectMode_t mode,
                              afatfsResultCallback_t complete)
{
    afatfsRemoveObjects_t *op = &afatfs.removeObjects;

    if (afatfs.filesystemState != AFATFS_FILESYSTEM_STATE_READY ||
        op->active ||
        afatfs.renameObject.active ||
        afatfs_fileIsBusy(&afatfs.currentDirectory)) {
        return false;
    }

    memset(op, 0, sizeof(*op));
    if (afatfs_copySanitizedLongName(op->displayName, displayName) == 0u)
        return false;

    op->active = 1u;
    op->result = AFATFS_RESULT_OK;
    op->phase = AFATFS_REMOVE_OBJECTS_PHASE_INITIAL;
    op->matchMode = matchMode;
    op->mode = mode;
    op->callback = complete;
    afatfs_removeObjectsContinue();
    return true;
}

bool afatfs_removeObject(const char *filename,
                         afatfsRemoveObjectMode_t mode,
                         afatfsResultCallback_t complete)
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
        afatfs_fileIsBusy(&afatfs.currentDirectory)) {
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
    op->result = AFATFS_RESULT_OK;
    op->phase = AFATFS_REMOVE_OBJECTS_PHASE_INITIAL;
    op->mode = mode;
    op->matchShortName = 1u;
    op->callback = complete;
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
    return afatfs_createFileInternal(file, name, attrib, fileMode,
                                     AFATFS_MATCH_CASE_INSENSITIVE,
                                     NULL, false,
                                     callback);
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
    } else if (afatfs_fileIsBusy(file)) {
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
        afatfs_createFileInternal(file, displayName,
                                  FAT_FILE_ATTRIBUTE_DIRECTORY,
                                  AFATFS_FILE_MODE_CREATE |
                                      AFATFS_FILE_MODE_READ |
                                      AFATFS_FILE_MODE_WRITE,
                                  matchMode,
                                  openNameOut, true, callback);
    } else if (callback) {
        callback(NULL);
    }

    return file != NULL;
}

/**
 * @brief Open an existing directory by its VFAT Long File Name (LFN).
 *
 * This function will search for the directory using the provided `displayName`.
 * It strictly opens an existing directory and will not create a new one.
 * If you need to tolerate creation, use `afatfs_mkdir_lfn` instead.
 * 
 * @param displayName The intended long name to search for.
 * @param matchMode Specifies case folding. Use AFATFS_MATCH_CASE_INSENSITIVE for
 *                  user-supplied folders, as FAT is fundamentally case-insensitive.
 * @param openNameOut Buffer to receive the 8.3 short name alias. May be NULL.
 * @param callback Executed when the handle is ready.
 * @return true if the open operation was successfully queued.
 */
bool afatfs_opendir_lfn(const char *displayName,
                        afatfsMatchMode_t matchMode,
                        char openNameOut[AFATFS_SHORT_FILENAME_MAX],
                        afatfsFileCallback_t callback)
{
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
        afatfs_createFileInternal(file, displayName,
                                  FAT_FILE_ATTRIBUTE_DIRECTORY,
                                  AFATFS_FILE_MODE_READ |
                                      AFATFS_FILE_MODE_WRITE,
                                  matchMode,
                                  openNameOut, true, callback);
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
    if (afatfs_fileIsBusy(&afatfs.currentDirectory)) {
        return false;
    }

    if (directory) {
        if (afatfs_fileIsBusy(directory)) {
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

/**
 * @brief Change the working directory to the parent of the current directory ("..").
 *
 * **WARNING: EXTREMELY IMPORTANT RETURN TYPE**
 * This function returns `afatfsOperationStatus_e`, NOT a boolean!
 *   0 = AFATFS_OPERATION_SUCCESS
 *   1 = AFATFS_OPERATION_IN_PROGRESS
 *   2 = AFATFS_OPERATION_FAILURE
 * 
 * NEVER evaluate this as a boolean (e.g., `if (!afatfs_chdirParent())`).
 * Doing so will evaluate `SUCCESS` (0) as true and `IN_PROGRESS` (1) as false,
 * which will completely break asynchronous state machines and cause infinite loops.
 * Always check explicitly against `AFATFS_OPERATION_SUCCESS`, `AFATFS_OPERATION_IN_PROGRESS`,
 * or `AFATFS_OPERATION_FAILURE`.
 */
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
        afatfs_createFileInternal(file, displayName, FAT_FILE_ATTRIBUTE_ARCHIVE,
                                  fileMode, matchMode, openNameOut, true,
                                  complete);
    } else if (complete) {
        complete(NULL);
    }
    return file != NULL;
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
        if (afatfs_fseekInternal(file, bytesToWriteThisSector, NULL) == AFATFS_OPERATION_IN_PROGRESS) {
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

void afatfs_getDiagnosticSnapshot(afatfsFilePtr_t file,
                                  afatfsDiagnosticSnapshot_t *snapshot)
{
    uint8_t cache_index;

    /*
     * Freeze-copy the state an external boot watchdog would otherwise lose.
     * Input: the currently owned handle, which may be busy in a cluster
     * append, plus AsyncFATFS global cache state. Output: scalar diagnostics
     * only; no operation phase, cache flag, file cursor, poll, allocation, or
     * SD request is changed. Why: callers must be able to classify a stalled
     * append before afatfs_destroy(true) invalidates the handle during boot
     * recovery. A NULL handle deliberately yields an unavailable snapshot.
     */
    if (!snapshot)
        return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->active_cache_index = -1;
    snapshot->filesystem_state = (uint8_t)afatfs.filesystemState;
    snapshot->filesystem_full = afatfs.filesystemFull ? 1u : 0u;
    snapshot->cache_flush_in_progress = afatfs.cacheFlushInProgress ? 1u : 0u;
    for (cache_index = 0u; cache_index < AFATFS_NUM_CACHE_SECTORS;
         cache_index++) {
        const afatfsCacheBlockDescriptor_t *descriptor =
            &afatfs.cacheDescriptor[cache_index];

        if (descriptor->state == AFATFS_CACHE_STATE_DIRTY)
            snapshot->cache_dirty_count++;
        if (descriptor->locked)
            snapshot->cache_locked_count++;
        if (descriptor->state == AFATFS_CACHE_STATE_READING)
            snapshot->cache_reading_count++;
        if (descriptor->state == AFATFS_CACHE_STATE_WRITING)
            snapshot->cache_writing_count++;
    }
    if (!file)
        return;
    snapshot->available = 1u;
    snapshot->cursor_offset = file->cursorOffset;
    snapshot->logical_size = file->logicalSize;
    snapshot->physical_size = file->physicalSize;
    snapshot->cursor_cluster = file->cursorCluster;
    snapshot->cursor_previous_cluster = file->cursorPreviousCluster;
    snapshot->file_operation = (uint8_t)file->operation.operation;
    snapshot->sectors_per_cluster = afatfs.sectorsPerCluster;
    if (file->writeLockedCacheIndex >= 0 &&
        file->writeLockedCacheIndex < AFATFS_NUM_CACHE_SECTORS) {
        snapshot->active_cache_index = file->writeLockedCacheIndex;
    }
    if (file->operation.operation ==
        AFATFS_FILE_OPERATION_APPEND_FREE_CLUSTER) {
        const afatfsAppendFreeCluster_t *append =
            &file->operation.state.appendFreeCluster;

        snapshot->append_phase = (uint8_t)append->phase;
        snapshot->append_previous_cluster = append->previousCluster;
        snapshot->search_cluster = append->searchCluster;
        snapshot->search_start_cluster = append->searchStartCluster;
        snapshot->search_wrapped = append->searchWrapped;
    }
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
        if (afatfs_fseekInternal(file, bytesToReadThisSector, NULL) == AFATFS_OPERATION_IN_PROGRESS) {
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

bool afatfs_deleteTree(const afatfsObjectInfo_t *root,
                       afatfsResultCallback_t cb)
{
    afatfsFile_t *file;
    afatfsResultCode_t validation =
        afatfs_validateObjectInfo(root, AFATFS_OBJECT_DIRECTORY);

    /*
     * Accept only one complete iterator result and copy it before returning.
     * Inputs: the caller's immediate-parent scan result. Outputs: one private
     * foreground-pumped handle or false with no callback. Why: the physical
     * LFN/SFN pointers are the exact object identity, and the scan owner may
     * be closed immediately after acceptance. Affiliates: filesystem.c's
     * singular slot resolver, the object finder, and deleteTreeFinish().
     */
    if (validation != AFATFS_RESULT_OK || afatfs.numClusters == 0u)
        return false;
    for (uint8_t i = 0u; i < AFATFS_MAX_OPEN_FILES; i++) {
        if (afatfs.openFiles[i].type != AFATFS_FILE_TYPE_NONE &&
            afatfs.openFiles[i].operation.operation ==
                AFATFS_FILE_OPERATION_DELETE_TREE)
            return false;
    }
    file = afatfs_allocateFileHandle();
    if (!file)
        return false;

    afatfs_initFileHandle(file);
    file->type = AFATFS_FILE_TYPE_NORMAL;
    file->operation.operation = AFATFS_FILE_OPERATION_DELETE_TREE;
    /* The sole state owner is recycled only after the previous callback. */
    memset(&afatfs.deleteTreeState, 0, sizeof(afatfs.deleteTreeState));
    afatfs.deleteTreeState.phase = AFATFS_DELETE_TREE_INITIAL;
    afatfs.deleteTreeState.root = *root;
    afatfs.deleteTreeState.callback = cb;
    /* A valid tree may spend one unit on a descent and one on that directory's
     * cluster; two volume-cluster units are therefore the conservative bound. */
    afatfs.deleteTreeState.structuralBudget =
        (afatfs.numClusters > UINT32_MAX / 2u)
            ? UINT32_MAX : afatfs.numClusters * 2u;
    afatfs.deleteTreeState.lastPhase = AFATFS_DELETE_TREE_INITIAL;
    file->operation.state.deleteTree = &afatfs.deleteTreeState;

    return true;
}

uint8_t afatfs_getDeleteTreePhase(void)
{
    for (int i = 0; i < AFATFS_MAX_OPEN_FILES; i++) {
        if (afatfs.openFiles[i].type != AFATFS_FILE_TYPE_NONE &&
            afatfs.openFiles[i].operation.operation == AFATFS_FILE_OPERATION_DELETE_TREE) {
            return (uint8_t)afatfs.openFiles[i].operation.state.deleteTree->phase;
        }
    }
    return 0xFF;
}

uint8_t afatfs_getDeleteTreeFailureSite(void)
{
    /*
     * Deliberately reads afatfs.deleteTreeState directly rather than
     * scanning openFiles[] the way afatfs_getDeleteTreePhase() does: by the
     * time a caller wants this value, the delete has already completed and
     * afatfs_deleteTreeFinish() has already reset its borrowed handle, so
     * no matching openFiles[] entry exists to scan for. This struct is the
     * persistent singleton afatfs_deleteTree() writes into, not part of
     * that handle, and afatfs_deleteTreeFinish() never touches it -- only
     * the next afatfs_deleteTree() call's memset() does.
     */
    return afatfs.deleteTreeState.failureSite;
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
    afatfsDeleteTree_t *op = file->operation.state.deleteTree;
    afatfsResultCallback_t callback =
        op->callback;

    /* Finder owns a retained directory sector only between these two calls;
     * finish closes that ownership before resetting the recycled handle. */
    if (op->finderActive) {
        afatfs_findLastObject(file, &op->finder);
        op->finderActive = 0u;
    }
    afatfs_fileUnlockCacheSector(file);
    afatfs_initFileHandle(file);
    if (callback)
        callback(result);
}

static void afatfs_deleteTreeContinue(afatfsFile_t *file)
{
    afatfsDeleteTree_t *op = file->operation.state.deleteTree;
    afatfsOperationStatus_e status;
    afatfsResultCode_t objectResult;
    afatfsObjectInfo_t object;

    /* The phase latch is diagnostic only; structuralBudget is the actual
     * corruption bound and is consumed by descents and released clusters. */
    op->lastPhase = op->phase;

    doMore:
    switch (op->phase) {
        case AFATFS_DELETE_TREE_INITIAL:
            /* Seed traversal from the copied root identity. */
            op->currentTarget = op->root;
            op->currentCluster = op->root.id.firstCluster;
            op->nameRunNextEntry = 0u;
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;

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
            afatfs_fileUnlockCacheSector(file);
            file->directoryEntryPos.sectorNumberPhysical = 0;
            file->directoryEntryPos.entryIndex = -1;
            if (op->currentTarget.id.firstCluster == 0u) {
                if (afatfs.filesystemType != FAT_FILESYSTEM_TYPE_FAT16) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_BAD_ROOT_ON_FAT32;
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                file->type = AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY;
                file->firstCluster = 0u;
                file->cursorCluster = 0u;
            } else {
                if (op->currentTarget.id.firstCluster <
                        FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                    op->currentTarget.id.firstCluster >=
                        afatfs.numClusters + FAT_SMALLEST_LEGAL_CLUSTER_NUMBER) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_CLUSTER_OUT_OF_RANGE;
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                file->type = AFATFS_FILE_TYPE_DIRECTORY;
                file->firstCluster = op->currentTarget.id.firstCluster;
                file->cursorCluster = op->currentTarget.id.firstCluster;
            }
            file->directoryEntryPos = op->currentTarget.id.sfnEntry;
            file->cursorPreviousCluster = 0;
            file->logicalSize = 0;
            file->physicalSize = 0;
            file->cursorOffset = 0;
            file->mode = AFATFS_FILE_MODE_READ;
            file->attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
            afatfs_findFirstObject(file, &op->finder);
            op->finderActive = 1u;
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
                op->finderActive = 0u;
                afatfs_findLastObject(file, &op->finder);
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (object.id.kind == AFATFS_OBJECT_NONE) {
                op->finderActive = 0u;
                afatfs_findLastObject(file, &op->finder);
                if (afatfs_entryPointerEquals(&file->directoryEntryPos,
                                              &op->root.id.sfnEntry)) {
                    if (file->firstCluster != op->root.id.firstCluster) {
                        op->failureSite =
                            AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_ROOT_CLUSTER_MISMATCH;
                        afatfs_deleteTreeFinish(
                            file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                        return;
                    }
                    op->currentTarget = op->root;
                    op->nameRunNextEntry = 0u;
                    op->phase = AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS;
                } else {
                    op->phase = AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND;
                }
                return; // YIELD to poll loop to prevent freezing!
            }
            /* Destructive traversal must never guess through a damaged VFAT
             * run. Browsers may expose its SFN fallback, but delete must stop
             * before touching either the child chain or its name entries. */
            objectResult = afatfs_validateObjectInfo(&object, object.id.kind);
            if (objectResult != AFATFS_RESULT_OK) {
                op->finderActive = 0u;
                afatfs_findLastObject(file, &op->finder);
                if (!object.lfnMalformed) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_MALFORMED_OBJECT;
                }
                afatfs_deleteTreeFinish(
                    file, object.lfnMalformed
                        ? AFATFS_RESULT_CORRUPT_LFN_RUN
                        : AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                return;
            }
            if (object.id.firstCluster != 0u &&
                (object.id.firstCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                 object.id.firstCluster >= afatfs.numClusters +
                     FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                 afatfs_FATIsEndOfChainMarker(object.id.firstCluster))) {
                op->finderActive = 0u;
                afatfs_findLastObject(file, &op->finder);
                op->failureSite =
                    AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_CHILD_CLUSTER_OUT_OF_RANGE;
                afatfs_deleteTreeFinish(
                    file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                return;
            }
            op->currentTarget = object;
            op->currentCluster = object.id.firstCluster;
            op->nameRunNextEntry = 0u;
            if (object.id.kind == AFATFS_OBJECT_FILE) {
                /* The file-chain/free-name phases use FAT and directory cache
                 * sectors independently of the scan cursor. */
                op->finderActive = 0u;
                afatfs_findLastObject(file, &op->finder);
                op->phase = AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS;
                return; // YIELD
            }
            if (object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                /*
                 * The next phase changes this handle to the child cluster, so
                 * release the parent scan's retained sector now. Parent lookup
                 * is later reconstructed structurally from the child's ".."
                 * entry; no live parent iterator is required across descent.
                 */
                if (op->structuralBudget == 0u) {
                    op->finderActive = 0u;
                    afatfs_findLastObject(file, &op->finder);
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_STRUCTURAL_BUDGET_EXHAUSTED_DESCEND;
                    afatfs_deleteTreeFinish(
                        file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                op->structuralBudget--;
                op->parentCluster = file->firstCluster;
                /*
                 * Snapshot everything the eventual ascend will need, now,
                 * while it is all still known-good.
                 *
                 * What: parentEntry saves the directory-entry pointer of the
                 * directory being left (file->directoryEntryPos still names
                 * it, because AFATFS_DELETE_TREE_OPEN_DIR has not yet
                 * rebound the handle to the child); descendTarget saves the
                 * child's complete object identity, including its VFAT name
                 * run. Why: both are destroyed by the descent itself --
                 * OPEN_DIR overwrites file->directoryEntryPos with the
                 * child's pointer, and the child's own scan overwrites
                 * currentTarget with each object it deletes inside the
                 * child. AFATFS_DELETE_TREE_REOPEN_PARENT restores both.
                 * Inputs: file->directoryEntryPos and currentTarget as of
                 * this instant. Outputs: two operation-state copies; no I/O
                 * and no change to the card. Affiliates:
                 * AFATFS_DELETE_TREE_REOPEN_PARENT (the sole reader),
                 * afatfs_retireObjectNameRun() (consumes the saved name run).
                 */
                op->parentEntry = file->directoryEntryPos;
                op->descendTarget = op->currentTarget;
                op->finderActive = 0u;
                afatfs_findLastObject(file, &op->finder);
                op->phase = AFATFS_DELETE_TREE_DESCEND_DIR;
                return; // YIELD
            }
            break;

        case AFATFS_DELETE_TREE_DESCEND_DIR:
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;

        case AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND:
            {
                uint32_t firstSector = afatfs_fileClusterToPhysical(
                    file->firstCluster, 0u);
                uint8_t *sector;
                fatDirectoryEntry_t *dotDot;
                uint32_t parentCluster;

                status = afatfs_cacheSector(firstSector, &sector,
                                            AFATFS_CACHE_READ, 0u);
                if (status == AFATFS_OPERATION_IN_PROGRESS)
                    return;
                if (status == AFATFS_OPERATION_FAILURE) {
                    afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                    return;
                }
                dotDot = &((fatDirectoryEntry_t *)sector)[1];
                if ((dotDot->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0u ||
                    dotDot->filename[0] != '.' || dotDot->filename[1] != '.' ||
                    dotDot->filename[2] != ' ') {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_BAD_DOTDOT_ENTRY;
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                parentCluster = ((uint32_t)dotDot->firstClusterHigh << 16u) |
                                dotDot->firstClusterLow;
                if (afatfs.filesystemType == FAT_FILESYSTEM_TYPE_FAT32 &&
                    parentCluster == 0u)
                    parentCluster = afatfs.rootDirectoryCluster;
                if (parentCluster != 0u &&
                    (parentCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                     parentCluster >= afatfs.numClusters +
                         FAT_SMALLEST_LEGAL_CLUSTER_NUMBER)) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_PARENT_CLUSTER_OUT_OF_RANGE;
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                if (op->parentCluster != parentCluster &&
                    !(parentCluster == op->root.id.firstCluster &&
                      op->parentCluster == op->root.id.firstCluster)) {
                    /*
                     * This is the specific check most consistent with
                     * Session 054's slot-11 evidence: a nested Scene child
                     * directory's own ".." entry disagrees with the parent
                     * cluster recorded when this traversal descended into
                     * it. See the failureSite doc comment on
                     * afatfsDeleteTree_t for why this needed its own tag
                     * rather than being lumped with every other
                     * UNSUPPORTED_LAYOUT site.
                     */
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_ASCEND_PARENT_CLUSTER_MISMATCH;
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                op->parentCluster = parentCluster;
                op->phase = AFATFS_DELETE_TREE_REOPEN_PARENT;
                goto doMore;
            }
            break;

        case AFATFS_DELETE_TREE_REOPEN_PARENT:
            /*
             * Rebind `file` to the parent directory -- no search.
             *
             * What: reopens `file` by cluster identity only (op->parentCluster,
             * already validated and ".."-agreement checked in
             * AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND just above), restores both
             * identities the descent destroyed, and proceeds to free the
             * just-ascended-from child's own cluster chain.
             *
             * Why the identities are restored from saved copies rather than
             * read out of live state: this phase previously took the child's
             * identity straight from op->currentTarget, on the assumption
             * that it still described the child. It does not. currentTarget
             * is the "object currently being deleted" register, rewritten by
             * AFATFS_DELETE_TREE_SCAN for every object it processes -- so
             * whenever the child directory had contents, currentTarget by
             * this point names the LAST FILE deleted inside the child, whose
             * cluster chain was freed moments ago. Freeing it a second time
             * walked a FAT link that now reads as free space, which is
             * exactly the AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NEXT_CLUSTER_INVALID
             * that Session 054's Scene-slot-12 evidence reported (the
             * assumption held only for a child that was already empty when
             * discovered, which is why simpler fixtures appeared to work).
             * op->descendTarget is that identity captured at descend time,
             * complete with the VFAT name run afatfs_retireObjectNameRun()
             * needs to retire every fragment instead of orphaning them.
             *
             * file->directoryEntryPos must likewise be restored to the
             * PARENT's own pointer, not left cleared: AFATFS_DELETE_TREE_SCAN
             * reads it to tell "I have emptied the delete root, retire it and
             * finish" from "I have emptied a nested child, ascend". Leaving
             * it zeroed made an emptied delete root look like a nested child
             * and sent the traversal ascending out of the tree it was
             * deleting. The root case is restored from op->root (the delete
             * root's identity, always available); any deeper parent is
             * restored from op->parentEntry.
             *
             * Inputs: op->parentCluster, op->parentEntry, op->descendTarget,
             * op->root. Outputs: `file` rebound to and correctly identified
             * as the parent; currentTarget/currentCluster/nameRunNextEntry
             * set up so AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS then
             * AFATFS_DELETE_TREE_RETIRE_ENTRIES delete the child. No search
             * and no extra card I/O. Affiliates:
             * AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_FOR_SELF_EXHAUSTED
             * and ..._MATCH_CLUSTER_OUT_OF_RANGE, permanently unreachable
             * since the re-scan this phase replaced was removed, and retained
             * only so already-captured card evidence naming them decodes.
             */
            afatfs_fileUnlockCacheSector(file);
            file->directoryEntryPos.sectorNumberPhysical = 0;
            file->directoryEntryPos.entryIndex = -1;
            if (op->parentCluster == 0u) {
                if (afatfs.filesystemType != FAT_FILESYSTEM_TYPE_FAT16) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_SCAN_PARENT_BAD_ROOT_ON_FAT32;
                    afatfs_deleteTreeFinish(file,
                                            AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                file->type = AFATFS_FILE_TYPE_FAT16_ROOT_DIRECTORY;
                file->firstCluster = 0u;
                file->cursorCluster = 0u;
            } else {
                file->type = AFATFS_FILE_TYPE_DIRECTORY;
                file->firstCluster = op->parentCluster;
                file->cursorCluster = op->parentCluster;
            }
            file->cursorPreviousCluster = 0;
            file->logicalSize = 0;
            file->physicalSize = 0;
            file->cursorOffset = 0;
            file->mode = AFATFS_FILE_MODE_READ;
            file->attrib = FAT_FILE_ATTRIBUTE_DIRECTORY;
            /*
             * Re-establish "which directory is `file` open on" for the parent.
             * The delete root is identified from op->root so the traversal can
             * still recognize the root once the parent's scan empties it; any
             * deeper parent uses the pointer saved when we descended past it.
             */
            file->directoryEntryPos =
                (op->parentCluster == op->root.id.firstCluster)
                    ? op->root.id.sfnEntry
                    : op->parentEntry;
            /*
             * Restore the ascended-from child as the object being deleted,
             * then hand off: FREE_FILE_CLUSTERS releases its cluster chain and
             * RETIRE_ENTRIES retires its complete name run. nameRunNextEntry
             * is rewound to zero exactly as AFATFS_DELETE_TREE_SCAN does when
             * it selects a fresh object.
             */
            op->currentTarget = op->descendTarget;
            op->currentCluster = op->currentTarget.id.firstCluster;
            op->nameRunNextEntry = 0u;
            op->phase = AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS;
            goto doMore;

        case AFATFS_DELETE_TREE_RETIRE_ENTRIES:
            status = afatfs_retireObjectNameRun(&op->currentTarget,
                                                &op->nameRunNextEntry);
            if (status == AFATFS_OPERATION_IN_PROGRESS) return;
            if (status == AFATFS_OPERATION_FAILURE) {
                afatfs_deleteTreeFinish(
                    file, op->currentTarget.lfnMalformed
                        ? AFATFS_RESULT_CORRUPT_LFN_RUN
                        : AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (op->nameRunNextEntry <
                op->currentTarget.id.lfnEntryCount + 1u)
                return;
            if (afatfs_entryPointerEquals(&op->currentTarget.id.sfnEntry,
                                          &op->root.id.sfnEntry)) {
                if (op->currentTarget.id.firstCluster !=
                    op->root.id.firstCluster) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_RETIRE_ENTRIES_ROOT_CLUSTER_MISMATCH;
                    afatfs_deleteTreeFinish(
                        file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                op->phase = AFATFS_DELETE_TREE_SUCCESS;
            } else {
                op->phase = AFATFS_DELETE_TREE_RESUME_PARENT;
            }
            goto doMore;

        case AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS:
            if (op->currentCluster == 0u) {
                if (op->currentTarget.id.kind != AFATFS_OBJECT_FILE ||
                    op->currentTarget.id.logicalSize != 0u) {
                    op->failureSite =
                        AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NONFILE_NONZERO_SIZE;
                    afatfs_deleteTreeFinish(
                        file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                    return;
                }
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                goto doMore;
            }
            if (afatfs_FATIsEndOfChainMarker(op->currentCluster)) {
                op->nameRunNextEntry = 0u;
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                goto doMore;
            }
            if (op->currentCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                op->currentCluster >= afatfs.numClusters +
                    FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                op->structuralBudget == 0u) {
                op->failureSite =
                    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_CLUSTER_OUT_OF_RANGE_OR_BUDGET;
                afatfs_deleteTreeFinish(
                    file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                return;
            }
            uint32_t nextCluster;
            status = afatfs_FATGetNextCluster(0, op->currentCluster, &nextCluster);
            if (status != AFATFS_OPERATION_SUCCESS) {
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            if (fat_isFreeSpace(nextCluster) ||
                (!afatfs_FATIsEndOfChainMarker(nextCluster) &&
                 (nextCluster < FAT_SMALLEST_LEGAL_CLUSTER_NUMBER ||
                  nextCluster >= afatfs.numClusters +
                      FAT_SMALLEST_LEGAL_CLUSTER_NUMBER))) {
                op->failureSite =
                    AFATFS_DELETE_TREE_FAILURE_SITE_FREE_CLUSTERS_NEXT_CLUSTER_INVALID;
                afatfs_deleteTreeFinish(
                    file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
                return;
            }
            status = afatfs_FATSetNextCluster(op->currentCluster, 0);
            if (status != AFATFS_OPERATION_SUCCESS) {
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                afatfs_deleteTreeFinish(file, AFATFS_RESULT_IO_ERROR);
                return;
            }
            afatfs.lastClusterAllocated = MIN(afatfs.lastClusterAllocated,
                                              op->currentCluster - 1u);
            op->structuralBudget--;
            op->currentCluster = nextCluster;
            return;

        case AFATFS_DELETE_TREE_RESUME_PARENT:
            /*
             * Re-scan the directory `file` already has open -- no reopen.
             *
             * What: resets the scan cursor on the currently-open handle and
             * restarts AFATFS_DELETE_TREE_SCAN on it directly, instead of
             * reconstructing a target from op->parentCluster and re-entering
             * through AFATFS_DELETE_TREE_OPEN_DIR. Why: `file` is already
             * correctly bound to the directory that needs to be resumed in
             * both paths that reach this phase -- after deleting a plain
             * file, `file` was never rebound away from the directory that
             * contained it; after retiring an ascended, now-empty child's
             * own entry, `file` is the parent AFATFS_DELETE_TREE_REOPEN_PARENT
             * already reopened. Deriving a fresh target from
             * op->parentCluster was the bug: that field is only ever
             * assigned by a prior descend or ascend, so the first plain
             * file object encountered directly under the traversal's own
             * root -- before any descend/ascend has run this operation --
             * reopened using its zero-initialized default, which
             * AFATFS_DELETE_TREE_OPEN_DIR then reads as a FAT16 root
             * reference on a FAT32 card and rejects. Root-caused from
             * Session 054's ScnS05 evidence (Scene slot 2, "002 Hard",
             * whose first scanned child was a plain file rather than its
             * nested "Kit Hard" subdirectory). Inputs: the already-open
             * `file` handle, whose firstCluster and directoryEntryPos both
             * already describe the directory to resume. Outputs: one
             * restarted scan; op->currentTarget and op->parentCluster are
             * untouched (currentTarget is about to be overwritten by the
             * next object AFATFS_DELETE_TREE_SCAN selects anyway; the
             * ascend path reads op->descendTarget, not currentTarget).
             * Affiliates:
             * AFATFS_DELETE_TREE_FAILURE_SITE_OPEN_DIR_BAD_ROOT_ON_FAT32,
             * AFATFS_DELETE_TREE_OPEN_DIR's reset sequence (which resets the
             * same cursor fields but, unlike this phase, legitimately
             * rebinds the handle and therefore rewrites directoryEntryPos).
             */
            afatfs_fileUnlockCacheSector(file);
            /*
             * Deliberately does NOT touch file->directoryEntryPos.
             *
             * This phase rewinds a scan of the directory `file` is already
             * open on; it never rebinds the handle, so that directory's own
             * identity must survive untouched. Clearing it here (as an
             * earlier revision of this phase did, by copying
             * AFATFS_DELETE_TREE_OPEN_DIR's reset sequence wholesale) erased
             * the only record of which directory was being scanned, with two
             * consequences: AFATFS_DELETE_TREE_SCAN could no longer recognize
             * an emptied delete root and ascended out of the tree instead of
             * retiring it, and the pointer that AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND
             * hands onward became a zeroed placeholder. Only the cursor
             * fields below are reset, which is all a rewind requires.
             */
            file->cursorCluster = file->firstCluster;
            file->cursorPreviousCluster = 0;
            file->logicalSize = 0;
            file->physicalSize = 0;
            file->cursorOffset = 0;
            afatfs_findFirstObject(file, &op->finder);
            op->finderActive = 1u;
            op->phase = AFATFS_DELETE_TREE_SCAN;
            goto doMore;

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
            op->failureSite = AFATFS_DELETE_TREE_FAILURE_SITE_CORRUPT_PHASE;
            afatfs_deleteTreeFinish(file, AFATFS_RESULT_UNSUPPORTED_LAYOUT);
            return;
    }
}
