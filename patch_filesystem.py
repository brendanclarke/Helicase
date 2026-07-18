import re

with open("Core/Hardware/SD/filesystem.c", "r") as f:
    content = f.read()

old_block = """            if (filesystem_directoryObjectMatchesSlot(
                    &op_object,
                    op_delete_slot_number,
                    op_delete_slot_allow_short_alias,
                    op_delete_slot_bank_scene)) {
                op_delete_slot_target_id = op_object.id;
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            break;"""

new_block = """            if (filesystem_directoryObjectMatchesSlot(
                    &op_object,
                    op_delete_slot_number,
                    op_delete_slot_allow_short_alias,
                    op_delete_slot_bank_scene)) {
                op_delete_slot_target_id = op_object.id;
                afatfs_findLastObject(op_delete_slot_dir,
                                      &op_object_finder);
                op_delete_slot_phase = FS_DELETE_SLOT_CLOSE_SCAN;
                break;
            }
            return FS_STATUS_BUSY;"""

if old_block in content:
    content = content.replace(old_block, new_block)
    with open("Core/Hardware/SD/filesystem.c", "w") as f:
        f.write(content)
    print("Patched filesystem.c")
else:
    print("Could not find block in filesystem.c")
