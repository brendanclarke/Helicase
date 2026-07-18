import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Fix object-> fields to object->id.
content = re.sub(r'object->displayName', r'object->id.displayName', content)
content = re.sub(r'object->shortName', r'object->id.shortName', content)
content = re.sub(r'object->sfnEntry', r'object->id.sfnEntry', content)
content = re.sub(r'object->lfnEntryCount', r'object->id.lfnEntryCount', content)
content = re.sub(r'object->lfnFirstEntry', r'object->id.lfnFirstEntry', content)
content = re.sub(r'object->kind', r'object->id.kind', content)
content = re.sub(r'object->ntReserved', r'object->id.ntReserved', content)

# But wait! op->source.sfnEntry needs to be op->source.id.sfnEntry!
content = re.sub(r'op->source\.displayName', r'op->source.id.displayName', content)
content = re.sub(r'op->source\.shortName', r'op->source.id.shortName', content)
content = re.sub(r'op->source\.sfnEntry', r'op->source.id.sfnEntry', content)
content = re.sub(r'op->source\.lfnEntryCount', r'op->source.id.lfnEntryCount', content)
content = re.sub(r'op->source\.lfnFirstEntry', r'op->source.id.lfnFirstEntry', content)
content = re.sub(r'op->source\.kind', r'op->source.id.kind', content)

# Fix op_test_object.kind etc. Wait, op_test_object is in filesystem.c, which I'm not touching here.

# Fix dotDot->highCluster and dotDot->lowCluster
content = content.replace("dotDot->highCluster", "dotDot->firstClusterHigh")
content = content.replace("dotDot->lowCluster", "dotDot->firstClusterLow")

# Fix afatfs.fatConfig.rootDirectoryCluster
content = content.replace("afatfs.fatConfig.rootDirectoryCluster", "afatfs.rootDirectoryCluster")

# Fix afatfs_FATClusterToSector -> afatfs_sectorFromCluster?
# Let's check what the function is actually called. It might be afatfs_clusterToSector
# We will just write the file and let the next compile tell us if it's wrong
content = content.replace("afatfs_FATClusterToSector(", "afatfs_clusterToSector(")

# Fix afatfs_freeFileHandle
content = content.replace("afatfs_freeFileHandle(file)", "file->type = AFATFS_FILE_TYPE_NONE")

# Fix missing switch cases in afatfs_fileOperationContinue
missing_cases = """
        case AFATFS_FILE_OPERATION_DELETE_TREE:
            afatfs_deleteTreeContinue(file);
        break;
        case AFATFS_FILE_OPERATION_COPY_TREE:
            afatfs_copyTreeContinue(file);
        break;
        case AFATFS_FILE_OPERATION_MOVE_OBJECT:
            afatfs_moveObjectContinue(file);
        break;
        case AFATFS_FILE_OPERATION_REPLACE_TREE:
            afatfs_replaceTreeContinue(file);
        break;
        case AFATFS_FILE_OPERATION_NONE:
            ;
        break;
    }
"""
content = content.replace("    }\n}\n\n/*\n * afatfs_replaceTreeContinue", missing_cases + "\n}\n\n/*\n * afatfs_replaceTreeContinue")

# Wait, in afatfs_poll, I need to remove afatfs_deleteTreeContinue() and afatfs_moveObjectContinue()
content = content.replace("                afatfs_deleteTreeContinue();\n", "")
content = content.replace("                afatfs_moveObjectContinue();\n", "")

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
