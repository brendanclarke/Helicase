import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Add AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP to enum
content = content.replace("AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF,", "AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF,\n    AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF_LOOP,")

# Fix the state machine logic
old_logic = """        case AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF:
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
            break;"""

new_logic = """        case AFATFS_DELETE_TREE_SCAN_PARENT_FOR_SELF:
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
                op->phase = AFATFS_DELETE_TREE_RETIRE_ENTRIES;
                goto doMore;
            }
            goto doMore;
"""

content = content.replace(old_logic, new_logic)

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
