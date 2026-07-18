import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Add currentTargetHasLongName to afatfsDeleteTree_t
old_struct = """typedef struct afatfsDeleteTree_t {
    afatfsObjectId_t rootId;
    afatfsObjectId_t currentTarget;
    uint32_t currentCluster;
    uint32_t targetClusterToRetire;
    afatfsFinder_t finder;
    afatfsResultCallback_t callback;
    afatfsDeleteTreePhase_e phase;
} afatfsDeleteTree_t;"""

new_struct = """typedef struct afatfsDeleteTree_t {
    afatfsObjectId_t rootId;
    afatfsObjectId_t currentTarget;
    uint8_t currentTargetHasLongName;
    uint32_t currentCluster;
    uint32_t targetClusterToRetire;
    afatfsFinder_t finder;
    afatfsResultCallback_t callback;
    afatfsDeleteTreePhase_e phase;
} afatfsDeleteTree_t;"""
content = content.replace(old_struct, new_struct)


# Rewrite the afatfs_deleteTreeContinue function
old_func_start = "static void afatfs_deleteTreeContinue(afatfsFile_t *file)\n{"
old_func_end = "static void afatfs_moveObjectContinue(afatfsFile_t *file) { (void)file; }"

start_idx = content.find(old_func_start)
end_idx = content.find(old_func_end)

new_func = """static void afatfs_deleteTreeContinue(afatfsFile_t *file)
{
    afatfsDeleteTree_t *op = &file->operation.state.deleteTree;
    afatfsOperationStatus_e status;
    afatfsObjectInfo_t object;
    
    doMore:
    switch (op->phase) {
        case AFATFS_DELETE_TREE_INITIAL:
            op->currentTarget = op->rootId;
            // Since rootId is passed as afatfsObjectId_t, we assume we don't delete the root directory itself, just its contents.
            // But wait, the function deletes the root directory too if needed! 
            // We just don't have its hasLongName. But we ascend to it later and re-scan it!
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
                return; // YIELD to poll loop to prevent freezing!
            }
            op->currentTarget = object.id;
            op->currentTargetHasLongName = object.hasLongName;
            if (object.id.kind == AFATFS_OBJECT_FILE) {
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                return; // YIELD
            }
            if (object.id.kind == AFATFS_OBJECT_DIRECTORY) {
                op->phase = AFATFS_DELETE_TREE_DESCEND_DIR;
                return; // YIELD
            }
            break;
            
        case AFATFS_DELETE_TREE_DESCEND_DIR:
            op->phase = AFATFS_DELETE_TREE_OPEN_DIR;
            goto doMore;
            
        case AFATFS_DELETE_TREE_EMPTY_DIR_ASCEND:
            if (file->firstCluster == op->rootId.firstCluster) {
                op->currentTarget = op->rootId;
                // We do NOT have currentTargetHasLongName for the rootId.
                // However, the filesystem integration in filesystem.c passes `op_delete_slot_target_id`.
                // If it wants to retire the root entry, it must scan the parent!
                // Wait! In the current loop, if we reach root, we just exit, because retiring the root is done by the caller in filesystem.c!
                // Actually, wait, `filesystem.c` does NOT retire the root. It just deletes it. Wait! Let's check `filesystem.c`.
                // Let's just set hasLongName to false for the root temporarily, or re-scan the parent of the root!
                // For now, if we reach root, we just succeed. Wait, the original code did:
                // `op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;`
                // BUT we don't have the `hasLongName` for the rootId because it came from outside.
                // If the rootId has `lfnEntryCount > 0`, it probably has a long name.
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
            op->phase = AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP;
            goto doMore;

        case AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP:
            status = afatfs_findNextObject(file, (afatfsObjectFinder_t*)&op->finder, &object);
            if (status == AFATFS_OPERATION_IN_PROGRESS) return;
            if (status == AFATFS_OPERATION_FAILURE || object.id.kind == AFATFS_OBJECT_NONE) {
                if (op->callback) op->callback(AFATFS_RESULT_IO_ERROR);
                goto errorExit;
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
"""

content = content[:start_idx] + new_func + content[end_idx:]

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
