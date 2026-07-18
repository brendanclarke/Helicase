import re

with open("/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Replace afatfsCopyTreePhase_e and afatfsCopyTree_t
copy_struct = """typedef enum {
    AFATFS_COPY_TREE_INITIAL = 0,
    AFATFS_COPY_TREE_CREATE_ROOT_DIR,
    AFATFS_COPY_TREE_OPEN_SRC_DIR,
    AFATFS_COPY_TREE_SCAN_SRC,
    AFATFS_COPY_TREE_CREATE_DST_DIR,
    AFATFS_COPY_TREE_COPY_FILE_INIT,
    AFATFS_COPY_TREE_COPY_FILE_CLUSTERS,
    AFATFS_COPY_TREE_COPY_FILE_FINISH,
    AFATFS_COPY_TREE_DESCEND,
    AFATFS_COPY_TREE_ASCEND,
    AFATFS_COPY_TREE_SUCCESS
} afatfsCopyTreePhase_e;

typedef struct afatfsCopyTree_t {
    afatfsResultCallback_t callback;
    afatfsCopyTreePhase_e phase;
    afatfsObjectId_t rootSrcId;
    afatfsDirHandle_t rootDstParent;
    char rootDstName[AFATFS_LONG_FILENAME_MAX + 1];
    
    afatfsObjectId_t currentSrcDir;
    afatfsDirHandle_t currentDstDir;
    
    afatfsFinder_t finder;
    afatfsCreateFile_t createFileState;
    afatfsObjectId_t currentChildId;
    
    uint32_t srcCurrentCluster;
    uint32_t dstCurrentCluster;
    uint32_t dstFirstCluster;
    uint32_t clustersRemaining;
} afatfsCopyTree_t;"""

content = re.sub(r'typedef enum \{[^}]*AFATFS_COPY_TREE_SUCCESS\n\} afatfsCopyTreePhase_e;\n\ntypedef struct afatfsCopyTree_t \{[^}]*\} afatfsCopyTree_t;', copy_struct, content, flags=re.MULTILINE)

# Replace afatfsReplaceTreePhase_e and afatfsReplaceTree_t
replace_struct = """typedef enum {
    AFATFS_REPLACE_TREE_INITIAL = 0,
    AFATFS_REPLACE_TREE_FIND_OLD,
    AFATFS_REPLACE_TREE_RENAME_OLD,
    AFATFS_REPLACE_TREE_RENAME_TMP,
    AFATFS_REPLACE_TREE_DELETE_OLD,
    AFATFS_REPLACE_TREE_SUCCESS
} afatfsReplaceTreePhase_e;

typedef struct afatfsReplaceTree_t {
    afatfsResultCallback_t callback;
    afatfsReplaceTreePhase_e phase;
    afatfsDirHandle_t parent;
    char targetName[AFATFS_LONG_FILENAME_MAX + 1];
    afatfsDirHandle_t tmpHandle;
    
    afatfsFinder_t finder;
    afatfsObjectId_t oldTargetId;
    char oldTargetRenameBuffer[AFATFS_LONG_FILENAME_MAX + 1];
    
    bool subOpActive;
    afatfsResultCode_t subOpResult;
} afatfsReplaceTree_t;"""

content = re.sub(r'typedef enum \{[^}]*AFATFS_REPLACE_TREE_SUCCESS\n\} afatfsReplaceTreePhase_e;\n\ntypedef struct afatfsReplaceTree_t \{[^}]*\} afatfsReplaceTree_t;', replace_struct, content, flags=re.MULTILINE)

with open("/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
