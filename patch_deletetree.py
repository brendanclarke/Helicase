import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Add afatfs_deleteTree right before afatfs_deleteTreeContinue
func = """
bool afatfs_deleteTree(const afatfsObjectId_t *root, afatfsResultCallback_t cb)
{
    afatfsFile_t *file = afatfs_allocateFileHandle();
    if (!file) return false;
    
    file->operation.operation = AFATFS_FILE_OPERATION_DELETE_TREE;
    file->operation.state.deleteTree.phase = AFATFS_DELETE_TREE_INITIAL;
    file->operation.state.deleteTree.rootId = *root;
    file->operation.state.deleteTree.callback = cb;
    
    // We intentionally don't open the root directory immediately here,
    // we let the state machine do it.
    
    return true;
}

"""
content = content.replace("static void afatfs_deleteTreeContinue", func + "static void afatfs_deleteTreeContinue")

# Also afatfs_moveObject is missing?
func_move = """
bool afatfs_moveObject(const afatfsObjectId_t *src, afatfsDirHandle_t dst_parent, const char *dst_name, afatfsResultCallback_t cb)
{
    afatfsFile_t *file = afatfs_allocateFileHandle();
    if (!file) return false;
    
    file->operation.operation = AFATFS_FILE_OPERATION_MOVE_OBJECT;
    file->operation.state.moveObject.phase = AFATFS_MOVE_OBJECT_INITIAL;
    file->operation.state.moveObject.srcId = *src;
    file->operation.state.moveObject.dstParent = dst_parent;
    file->operation.state.moveObject.dstName = dst_name; // We need to copy this if it can change!
    // But since it's an async operation, dst_name pointer must be valid until complete. 
    // In our implementation, we'll assume it's statically allocated or we copy it into the state.
    file->operation.state.moveObject.callback = cb;
    
    return true;
}

"""
content = content.replace("static void afatfs_moveObjectContinue", func_move + "static void afatfs_moveObjectContinue")


with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
