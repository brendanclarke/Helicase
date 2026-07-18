import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Add structs before afatfsFileOperation_e
structs = """
typedef enum {
    AFATFS_DELETE_TREE_INITIAL,
    AFATFS_DELETE_TREE_OPEN_DIR,
    AFATFS_DELETE_TREE_SCAN,
    AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND,
    AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF,
    AFATFS_DELETE_TREE_DESCEND_DIR,
    AFATFS_DELETE_TREE_RETIRE_ENTRIES,
    AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS,
    AFATFS_DELETE_TREE_SUCCESS
} afatfsDeleteTreePhase_e;

typedef struct afatfsDeleteTree_t {
    afatfsObjectId_t rootId;
    afatfsObjectId_t currentTarget;
    uint32_t currentCluster;
    uint32_t targetClusterToRetire;
    afatfsFinder_t finder;
    afatfsResultCallback_t callback;
    afatfsDeleteTreePhase_e phase;
} afatfsDeleteTree_t;

typedef enum {
    AFATFS_MOVE_OBJECT_INITIAL,
} afatfsMoveObjectPhase_e;

typedef struct afatfsMoveObject_t {
    afatfsObjectId_t srcId;
    afatfsDirHandle_t dstParent;
    const char *dstName;
    afatfsResultCallback_t callback;
    afatfsMoveObjectPhase_e phase;
} afatfsMoveObject_t;

typedef struct afatfsCopyTree_t {
    afatfsResultCallback_t callback;
} afatfsCopyTree_t;

typedef struct afatfsReplaceTree_t {
    afatfsResultCallback_t callback;
} afatfsReplaceTree_t;

typedef enum {"""

content = content.replace("typedef enum {\n    AFATFS_FILE_OPERATION_NONE,", structs + "\n    AFATFS_FILE_OPERATION_NONE,")

# Add enums to afatfsFileOperation_e
enums = """    AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY,
    AFATFS_FILE_OPERATION_DELETE_TREE,
    AFATFS_FILE_OPERATION_MOVE_OBJECT,
    AFATFS_FILE_OPERATION_COPY_TREE,
    AFATFS_FILE_OPERATION_REPLACE_TREE,"""
content = content.replace("    AFATFS_FILE_OPERATION_EXTEND_SUBDIRECTORY,", enums)

# Add state to union
union_state = """        afatfsCloseFile_t closeFile;
        afatfsDeleteTree_t deleteTree;
        afatfsMoveObject_t moveObject;
        afatfsCopyTree_t copyTree;
        afatfsReplaceTree_t replaceTree;
    } state;"""
content = content.replace("        afatfsCloseFile_t closeFile;\n    } state;", union_state)

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
