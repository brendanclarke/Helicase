import re

with open("/Users/bc/Helicase/Core/Hardware/SD/filesystem.c", "r") as f:
    content = f.read()

# Add global state for delete tree callback
new_globals = """static afatfsObjectId_t op_delete_slot_target_id;
static bool op_delete_tree_done = false;
static afatfsResultCode_t op_delete_tree_result = AFATFS_RESULT_NONE;

static void on_delete_tree_complete(afatfsResultCode_t result)
{
    op_delete_tree_done = true;
    op_delete_tree_result = result;
}"""

# Insert globals after op_delete_slot_bank_scene
content = re.sub(
    r'(static uint8_t op_delete_slot_bank_scene = 0u;\nstatic uint16_t op_delete_slot_number = 0u;)',
    r'\1\n' + new_globals,
    content
)

# Remove op_delete_slot_target_name and open_name
content = re.sub(r'static char op_delete_slot_target_name\[AFATFS_LONG_FILENAME_MAX \+ 1u\];\n', '', content)
content = re.sub(r'static char op_delete_slot_target_open_name\[AFATFS_SHORT_FILENAME_MAX\];\n', '', content)

# Update filesystem_deleteSlotDirectoriesStart
content = re.sub(
    r'memset\(op_delete_slot_target_name,\s*0,\s*sizeof\(op_delete_slot_target_name\)\);\n\s*memset\(op_delete_slot_target_open_name,\s*0,\s*sizeof\(op_delete_slot_target_open_name\)\);',
    r'op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;',
    content
)

# Update filesystem_deleteKitSlotDirectories_tick
content = re.sub(
    r'op_delete_slot_target_name\[0\] == \'\\0\'',
    r'op_delete_slot_target_id.kind == AFATFS_OBJECT_NONE',
    content
)

content = re.sub(
    r'op_delete_slot_target_name\[0\] = \'\\0\';',
    r'op_delete_slot_target_id.kind = AFATFS_OBJECT_NONE;',
    content
)

replace_match = """                filesystem_copyLongComponent(
                    op_delete_slot_target_name,
                    sizeof(op_delete_slot_target_name),
                    op_object.id.displayName);
                filesystem_copyLongComponent(
                    op_delete_slot_target_open_name,
                    sizeof(op_delete_slot_target_open_name),
                    op_object.id.shortName);"""

content = content.replace(replace_match, "                op_delete_slot_target_id = op_object.id;")

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
