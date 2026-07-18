import re

with open("/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "r") as f:
    content = f.read()

# Fix object-> accesses
content = content.replace("object->lfnFirstEntry", "object->id.lfnFirstEntry")
content = content.replace("object->sfnEntry", "object->id.sfnEntry")
content = content.replace("object->lfnEntryCount", "object->id.lfnEntryCount")
content = content.replace("object->shortName", "object->id.shortName")
content = content.replace("object->displayName", "object->id.displayName")

# Fix op->source accesses
content = content.replace("op->source.sfnEntry", "op->source.id.sfnEntry")

# Replace afatfs_renameObjectRawEntryMatchesNew with afatfs_createFileNameMatches
content = content.replace("afatfs_renameObjectRawEntryMatchesNew(op, entry)", "afatfs_createFileNameMatches(&op->newNameState, entry)")

# Fix afatfs_ticks()
content = content.replace("if (afatfs_ticks() % 8 == 0) return;", "")

# Fix afatfs_freeFileHandle
content = content.replace("afatfs_freeFileHandle(file);", "file->operation.operation = AFATFS_FILE_OPERATION_NONE;\n            file->type = AFATFS_FILE_TYPE_NONE;")

# Fix AFATFS_RESULT_INVALID_ARGUMENT
content = content.replace("AFATFS_RESULT_INVALID_ARGUMENT", "AFATFS_RESULT_INVALID_NAME")

with open("/Users/bc/Helicase/Core/Hardware/SD/asyncfatfs/asyncfatfs.c", "w") as f:
    f.write(content)
