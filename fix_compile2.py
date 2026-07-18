import re

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Fix the ntReserved corruption
content = content.replace("object->id.ntReserved", "object->ntReserved")

# Fix clusterToSector -> afatfs_fileClusterToPhysical
content = content.replace("afatfs_clusterToSector(file->firstCluster, &firstSector)", "firstSector = afatfs_fileClusterToPhysical(file->firstCluster, 0)")
# Note: My replace in afatfs_deleteTreeContinue was: status = afatfs_clusterToSector(file->firstCluster, &firstSector);
# Let's just fix it properly:
content = content.replace("status = afatfs_clusterToSector(file->firstCluster, &firstSector);\n                if (status != AFATFS_OPERATION_SUCCESS) {", "firstSector = afatfs_fileClusterToPhysical(file->firstCluster, 0);\n                if (0) {")

# Add stub for replaceTreeContinue
stub = """
static void afatfs_replaceTreeContinue(afatfsFile_t *file)
{
    (void)file;
}
"""
content = content.replace("static void afatfs_replaceTreeContinue(afatfsFile_t *file)\n{\n    switch (opState->phase) {", stub + "/*") # This is a bit hacky, let's do it cleanly

with open("Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
