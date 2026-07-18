import re

with open("/Users/bc/Helicase/Core/Hardware/SD/filesystem.c", "r") as f:
    content = f.read()

# I need to change filesystem_deleteSlot_tick
# We have a global afatfsObjectId_t op_delete_slot_target_id;
# We remove op_delete_slot_target_name and op_delete_slot_target_open_name
# And in FS_DELETE_SLOT_OPEN_SCAN matching we do:
# op_delete_slot_target_id = op_object.id;

content = content.replace("static char op_delete_slot_target_name[AFATFS_LONG_FILENAME_MAX + 1u];", "static afatfsObjectId_t op_delete_slot_target_id;")
content = content.replace("static char op_delete_slot_target_open_name[AFATFS_SHORT_FILENAME_MAX];", "static bool op_delete_tree_done;\nstatic afatfsResultCode_t op_delete_tree_result;")

content = content.replace("""    memset(op_delete_slot_target_name, 0,
           sizeof(op_delete_slot_target_name));
    memset(op_delete_slot_target_open_name, 0,
           sizeof(op_delete_slot_target_open_name));""", "    op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;")

content = content.replace("""    memset(op_delete_slot_target_name, 0,
                   sizeof(op_delete_slot_target_name));
            memset(op_delete_slot_target_open_name, 0,
                   sizeof(op_delete_slot_target_open_name));""", "    op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;")

content = content.replace("""                filesystem_copyLongComponent(
                    op_delete_slot_target_name,
                    sizeof(op_delete_slot_target_name),
                    op_object.id.displayName);
                filesystem_copyLongComponent(
                    op_delete_slot_target_open_name,
                    sizeof(op_delete_slot_target_open_name),
                    op_object.id.shortName);""", "                op_delete_slot_target_id = op_object.id;")

content = content.replace("""                op_delete_slot_target_name[0] = '\\0';""", "                op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;")

content = content.replace("""            if (op_delete_slot_target_name[0] == '\\0') {""", "            if (op_delete_slot_target_id.kind == AFATFS_OBJECT_NONE) {")

content = content.replace("""            filesystem_deleteTreeStartWithOpenName(
                op_delete_slot_target_name,
                op_delete_slot_target_open_name);
            op_delete_slot_phase = FS_DELETE_SLOT_DELETE_MATCH;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_DELETE_MATCH:
            delete_status = filesystem_deleteTree_tick();
            if (delete_status == FS_STATUS_BUSY)
                return FS_STATUS_BUSY;
            if (delete_status == FS_STATUS_ERROR) {
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            memset(op_delete_slot_target_name, 0,
                   sizeof(op_delete_slot_target_name));
            memset(op_delete_slot_target_open_name, 0,
                   sizeof(op_delete_slot_target_open_name));
            op_delete_slot_phase = FS_DELETE_SLOT_OPEN_SCAN;
            break;""", """            op_delete_tree_done = false;
            afatfs_deleteTree(&op_delete_slot_target_id, [](afatfsResultCode_t res) {
                op_delete_tree_done = true;
                op_delete_tree_result = res;
            });
            op_delete_slot_phase = FS_DELETE_SLOT_DELETE_MATCH;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_DELETE_MATCH:
            if (!op_delete_tree_done) return FS_STATUS_BUSY;
            if (op_delete_tree_result != AFATFS_RESULT_OK) {
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;
            op_delete_slot_phase = FS_DELETE_SLOT_OPEN_SCAN;
            break;""")

with open("/Users/bc/Helicase/Core/Hardware/SD/filesystem.c", "w") as f:
    f.write(content)
