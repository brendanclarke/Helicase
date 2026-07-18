import re

with open("/Users/bc/Helicase/Core/Hardware/SD/filesystem.c", "r") as f:
    content = f.read()

# Fix object.kind -> object.id.kind
content = re.sub(r'op_object\.kind', 'op_object.id.kind', content)
content = re.sub(r'op_object\.displayName', 'op_object.id.displayName', content)
content = re.sub(r'op_object\.shortName', 'op_object.id.shortName', content)

content = re.sub(r'op_test_object\.kind', 'op_test_object.id.kind', content)
content = re.sub(r'op_test_object\.displayName', 'op_test_object.id.displayName', content)
content = re.sub(r'op_test_object\.shortName', 'op_test_object.id.shortName', content)

content = re.sub(r'object->kind', 'object->id.kind', content)
content = re.sub(r'object->displayName', 'object->id.displayName', content)
content = re.sub(r'object->shortName', 'object->id.shortName', content)

# Check for the delete tick fix
# In the previous python script, my regex didn't match the delete tick replacement properly.
replace_delete_tick = """            filesystem_deleteTreeStartWithOpenName(
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
            break;"""

new_delete_tick = """            op_delete_tree_done = false;
            if (!afatfs_deleteTree(&op_delete_slot_target_id, on_delete_tree_complete)) {
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_phase = FS_DELETE_SLOT_DELETE_MATCH;
            return FS_STATUS_BUSY;

        case FS_DELETE_SLOT_DELETE_MATCH:
            if (!op_delete_tree_done)
                return FS_STATUS_BUSY;
            if (op_delete_tree_result != AFATFS_RESULT_OK) {
                op_delete_slot_phase = FS_DELETE_SLOT_ERROR;
                return FS_STATUS_ERROR;
            }
            op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;
            op_delete_slot_phase = FS_DELETE_SLOT_OPEN_SCAN;
            break;"""

content = content.replace(replace_delete_tick, new_delete_tick)

with open("/Users/bc/Helicase/Core/Hardware/SD/filesystem.c", "w") as f:
    f.write(content)
