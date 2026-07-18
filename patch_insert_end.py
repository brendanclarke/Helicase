import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

new_functions = """
bool afatfs_deleteTree(const afatfsObjectId_t *root, afatfsResultCallback_t cb)
{
    afatfsFile_t *file = afatfs_allocateFileHandle();
    if (!file) return false;
    
    file->operation.operation = AFATFS_FILE_OPERATION_DELETE_TREE;
    file->operation.state.deleteTree.phase = AFATFS_DELETE_TREE_INITIAL;
    file->operation.state.deleteTree.rootId = *root;
    file->operation.state.deleteTree.callback = cb;
    
    return true;
}

bool afatfs_moveObject(const afatfsObjectId_t *src, afatfsDirHandle_t dst_parent, const char *dst_name, afatfsResultCallback_t cb)
{
    afatfsFile_t *file = afatfs_allocateFileHandle();
    if (!file) return false;
    
    file->operation.operation = AFATFS_FILE_OPERATION_MOVE_OBJECT;
    file->operation.state.moveObject.phase = AFATFS_MOVE_OBJECT_INITIAL;
    file->operation.state.moveObject.srcId = *src;
    file->operation.state.moveObject.dstParent = dst_parent;
    file->operation.state.moveObject.dstName = dst_name; 
    file->operation.state.moveObject.callback = cb;
    
    return true;
}

static void afatfs_deleteTreeContinue(afatfsFile_t *file)
{
    afatfsDeleteTree_t *op = &file->operation.state.deleteTree;
    afatfsOperationStatus_e status;
    afatfsObjectInfo_t object;
    
    doMore:
    switch (op->phase) {
        case AFATFS_DELETE_TREE_INITIAL:
            op->currentTarget = op->rootId;
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;
            
        case AFATFS_DELETE_TREE_OPEN_DIR:
            file->directoryEntryPos.sectorNumberPhysical = 0;
            file->directoryEntryPos.entryIndex = -1;
            file->firstCluster = op->currentTarget.firstCluster;
            file->cursorCluster = op->currentTarget.firstCluster;
            file->logicalSize = 0;
            file->cursorOffset = 0;
            file->mode = AFATFS_FILE_MODE_READ;
            file->type = AFATFS_FILE_TYPE_DIRECTORY;
            afatfs_objectScanReset((afatfsObjectFinder_t*)&op->finder);
            op->phase = AFATFS_DELETE_TREE_SCAN;
            goto doMore;
            
        case AFATFS_DELETE_TREE_SCAN:
            status = afatfs_findNextObject(file, (afatfsObjectFinder_t*)&op->finder, &object);
            if (status == AFATFS_OPERATION_IN_PROGRESS) return;
            if (status == AFATFS_OPERATION_FAILURE || object.id.kind == AFATFS_OBJECT_NONE) {
                op->phase = AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND;
                goto doMore;
            }
            op->currentTarget = object.id;
            if (object.id.kind == AFATFS_OBJECT_FILE) {
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                goto doMore;
            }
            if (object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                op->phase = AFATFS_DELETE_TREE_DESCEND_DIR;
                goto doMore;
            }
            break;
            
        case AFATFS_DELETE_TREE_DESCEND_DIR:
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;
            
        case AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND:
            if (file->firstCluster == op->rootId.firstCluster) {
                op->currentTarget = op->rootId;
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                goto doMore;
            } else {
                uint32_t firstSector;
                firstSector = afatfs_fileClusterToPhysical(file->firstCluster, 0);
                uint8_t *sector;
                status = afatfs_cacheSector(firstSector, &sector, AFATFS_CACHE_READ, 0);
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                if (status == AFATFS_OPERATION_FAILURE) {
                    if (op->callback) op->callback(AFATFS_RESULT_IO_ERROR);
                    goto errorExit;
                }
                fatDirectoryEntry_t *dotDot = &((fatDirectoryEntry_t*)sector)[1];
                uint32_t parentCluster = (uint32_t)(((uint32_t)dotDot->firstClusterHigh << 16u) | dotDot->firstClusterLow);
                if (parentCluster == 0) parentCluster = afatfs.rootDirectoryCluster;
                op->targetClusterToRetire = file->firstCluster;
                op->currentTarget.firstCluster = parentCluster;
                op->phase = AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF;
                goto doMore;
            }
            break;
            
        case AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF:
            file->directoryEntryPos.sectorNumberPhysical = 0;
            file->directoryEntryPos.entryIndex = -1;
            file->firstCluster = op->currentTarget.firstCluster;
            file->cursorCluster = op->currentTarget.firstCluster;
            file->logicalSize = 0;
            file->cursorOffset = 0;
            file->mode = AFATFS_FILE_MODE_READ;
            file->type = AFATFS_FILE_TYPE_DIRECTORY;
            afatfs_objectScanReset((afatfsObjectFinder_t*)&op->finder);
            while (1) {
                status = afatfs_findNextObject(file, (afatfsObjectFinder_t*)&op->finder, &object);
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                if (status == AFATFS_OPERATION_FAILURE || object.id.kind == AFATFS_OBJECT_NONE) {
                    if (op->callback) op->callback(AFATFS_RESULT_IO_ERROR);
                    goto errorExit;
                }
                if (object.id.firstCluster == op->targetClusterToRetire) {
                    op->currentTarget = object.id;
                    op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                    goto doMore;
                }
            }
            break;
            
        case AFATFS_DELETE_TREE_RETIRE_ENTRIES:
            object.id = op->currentTarget;
            status = afatfs_retireObjectNameRun(&object);
            if (status == AFATFS_OPERATION_IN_PROGRESS) return;
            if (status == AFATFS_OPERATION_FAILURE) {
                if (op->callback) op->callback(AFATFS_RESULT_IO_ERROR);
                goto errorExit;
            }
            op->currentCluster = op->currentTarget.firstCluster;
            op->phase = AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS;
            goto doMore;
            
        case AFATFS_DELETE_TREE_FREE_FILE_CLUSTERS:
            if (op->currentCluster == 0 || afatfs_FATIsEndOfChainMarker(op->currentCluster)) {
                if (op->currentTarget.firstCluster == op->rootId.firstCluster) {
                    op->phase = AFATFS_DELETE_TREE_SUCCESS;
                    goto doMore;
                } else {
                    op->phase = AFATFS_DELETE_TREE_SCAN;
                    goto doMore;
                }
            }
            uint32_t nextCluster;
            status = afatfs_FATGetNextCluster(0, op->currentCluster, &nextCluster);
            if (status != AFATFS_OPERATION_SUCCESS) {
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                if (op->callback) op->callback(AFATFS_RESULT_IO_ERROR);
                goto errorExit;
            }
            status = afatfs_FATSetNextCluster(op->currentCluster, 0);
            if (status != AFATFS_OPERATION_SUCCESS) {
                if (status == AFATFS_OPERATION_IN_PROGRESS) return;
                if (op->callback) op->callback(AFATFS_RESULT_IO_ERROR);
                goto errorExit;
            }
            afatfs.lastClusterAllocated = MIN(afatfs.lastClusterAllocated, op->currentCluster - 1);
            op->currentCluster = nextCluster;
            return;
            
        case AFATFS_DELETE_TREE_SUCCESS:
            if (op->callback) op->callback(AFATFS_RESULT_OK);
            file->operation.operation = AFATFS_FILE_OPERATION_NONE;
            file->type = AFATFS_FILE_TYPE_NONE;
            break;
    }
    return;
errorExit:
    file->operation.operation = AFATFS_FILE_OPERATION_NONE;
    file->type = AFATFS_FILE_TYPE_NONE;
}

static void afatfs_moveObjectContinue(afatfsFile_t *file) { (void)file; }
static void afatfs_copyTreeContinue(afatfsFile_t *file) { (void)file; }
static void afatfs_replaceTreeContinue(afatfsFile_t *file) { (void)file; }
"""

# Append at the end of the file
content += new_functions

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
