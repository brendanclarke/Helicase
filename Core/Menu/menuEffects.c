/*
 * Core/Menu/menuEffects.c
 *
 *  Created on: 28.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Effect page implementation (contract in menuEffects.h).
 *
 * Layout (plan §13.2, A39):
 *   SELECT 1  screen 0: typ out vol pan    screen 1: run len scl mrp
 *   SELECT 2..8: type-specific rows 3..63 (N <= 61), 4 per screen, S =
 *     ceil(N / 28) screens per button (1..3), filled button by button.
 *   A type with select_layout overrides SELECT 2..8 (up to 4 screens each;
 *   cells hold descriptor indices, EFFECT_LANE_NONE = empty). SELECT 1 is
 *   always manager-owned so `typ` stays reachable.
 * Navigation: the encoder walks every populated cell linearly across screens
 * AND SELECT buttons, without wrapping; SELECT n lands on its first screen,
 * and a re-press cycles its screens (S072_ST7 D1).
 */
#include "menuEffects.h"
#include "menu.h"
#include "MenuText.h"
#include "SceneData.h"
#include "buttonHandler.h"
#include <string.h>

#define MENU_FX_SELECT_COUNT          8u
#define MENU_FX_SCREENS_MAX           4u
#define MENU_FX_CELLS                 4u
#define MENU_FX_ROWS_PER_SCREEN_SET  28u

/*
 * Effect page state (14 B SRAM1; plan §16.1 item 8).
 *
 * screen[] remembers a screen per SELECT button. show_morph follows SHIFT;
 * type_edit/candidate/scene own the `typ` transaction; last_scene/type let
 * the foreground service detect a Scene or external type change.
 */
static uint8_t menuEffects_screen[MENU_FX_SELECT_COUNT];
static uint8_t menuEffects_showMorphFlag;
static uint8_t menuEffects_typeEdit;
static effect_type_id_t menuEffects_typeCandidate;
static uint8_t menuEffects_typeScene;
static uint8_t menuEffects_lastScene;
static effect_type_id_t menuEffects_lastType;

/* Run-mode labels in effect_seq_run_mode_t order: compact / full view. */
static const char menuEffects_runShort[EFFECT_SEQ_RUN_MODE_COUNT][4] = {
    "fwd", "rev", "pip", "rnd", "sel"
};
static const char *const menuEffects_runLong[EFFECT_SEQ_RUN_MODE_COUNT] = {
    "forward", "reverse", "pingpong", "random", "select"
};

/* Provisional Step 7 scale labels; Step 8 moves them to shared ownership. */
static const char menuEffects_scaleShort[EFFECT_SEQ_SCALE_COUNT][4] = {
    "/64", "32t", "/32", "16t", "/16", "/8t", "16.",
    "/8 ", "/4t", "/8.", "/4 ", "/2 ", "1br", "2br"
};
static const char *const menuEffects_scaleLong[EFFECT_SEQ_SCALE_COUNT] = {
    "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/16.",
    "1/8", "1/4T", "1/8.", "1/4", "1/2", "1 bar", "2 bars"
};

/* Active Scene's retained record and registry row (`off` fallback). */
static const effect_record_t *menuEffects_record(void)
{
    return scene_effectConst(scene_getActiveIndex());
}

static const effect_registry_entry_t *menuEffects_entry(void)
{
    const effect_record_t *record = menuEffects_record();
    const effect_registry_entry_t *entry =
        record ? effects_registryEntry(record->type) : NULL;

    return entry ? entry : effects_registryEntry(EFFECT_TYPE_OFF);
}

/* Type-specific row count N (indices 3..descriptor_count-1). */
static uint8_t menuEffects_typeRowCount(const effect_registry_entry_t *entry)
{
    return (entry->descriptor_count > EFFECT_COMMON_PARAM_COUNT)
        ? (uint8_t)(entry->descriptor_count - EFFECT_COMMON_PARAM_COUNT)
        : 0u;
}

/* Screens per SELECT 2..8 button in the default layout: ceil(N / 28). */
static uint8_t menuEffects_defaultScreensPerButton(uint8_t rows)
{
    return (uint8_t)((rows + MENU_FX_ROWS_PER_SCREEN_SET - 1u) /
                     MENU_FX_ROWS_PER_SCREEN_SET);
}

/* Return the populated screen count behind one SELECT button. */
static uint8_t menuEffects_screenCount(uint8_t sub_page)
{
    const effect_registry_entry_t *entry = menuEffects_entry();
    uint8_t rows;
    uint8_t per_button;
    uint8_t first;
    uint8_t screens;

    if (sub_page >= MENU_FX_SELECT_COUNT)
        return 0u;
    if (sub_page == 0u)
        return 2u;
    if (entry->select_layout) {
        screens = entry->select_layout->screen_count[sub_page];
        return (screens > MENU_FX_SCREENS_MAX) ? MENU_FX_SCREENS_MAX : screens;
    }
    rows = menuEffects_typeRowCount(entry);
    per_button = menuEffects_defaultScreensPerButton(rows);
    first = (uint8_t)((sub_page - 1u) * per_button * MENU_FX_CELLS);
    if (per_button == 0u || first >= rows)
        return 0u;
    screens = (uint8_t)((rows - first + MENU_FX_CELLS - 1u) /
                        MENU_FX_CELLS);
    return (screens > per_button) ? per_button : screens;
}

/* Resolve one (SELECT, screen, column) coordinate to an Effect cell. */
static uint8_t menuEffects_cellAt(uint8_t sub_page, uint8_t screen,
                                  uint8_t column, menuEffects_cell_t *out)
{
    const effect_registry_entry_t *entry = menuEffects_entry();
    uint8_t index = EFFECT_LANE_NONE;

    if (!out)
        return 0u;
    memset(out, 0, sizeof(*out));
    if (sub_page >= MENU_FX_SELECT_COUNT || column >= MENU_FX_CELLS ||
        screen >= menuEffects_screenCount(sub_page))
        return 0u;
    if (sub_page == 0u) {
        static const uint8_t second[MENU_FX_CELLS] = {
            MENU_FX_CELL_RUN, MENU_FX_CELL_LENGTH,
            MENU_FX_CELL_SCALE, MENU_FX_CELL_MORPH_AMOUNT
        };
        if (screen == 1u) {
            out->kind = second[column];
            return 1u;
        }
        if (column == 0u) {
            out->kind = MENU_FX_CELL_TYPE;
            return 1u;
        }
        index = (uint8_t)(column - 1u);
    } else if (entry->select_layout) {
        index = entry->select_layout->cells[sub_page][screen][column];
    } else {
        uint8_t rows = menuEffects_typeRowCount(entry);
        uint8_t ordinal = (uint8_t)(
            ((sub_page - 1u) * menuEffects_defaultScreensPerButton(rows) +
             screen) * MENU_FX_CELLS + column);

        if (ordinal < rows)
            index = (uint8_t)(EFFECT_COMMON_PARAM_COUNT + ordinal);
    }
    if (index == EFFECT_LANE_NONE || index >= entry->descriptor_count)
        return 0u;
    out->kind = MENU_FX_CELL_PARAM;
    out->index = index;
    out->descriptor = &entry->descriptors[index].base;
    return 1u;
}

/* First selectable column of one screen, or 0 when empty. */
static uint8_t menuEffects_firstColumn(uint8_t sub_page, uint8_t screen)
{
    menuEffects_cell_t cell;
    uint8_t column;

    for (column = 0u; column < MENU_FX_CELLS; column++) {
        if (menuEffects_cellAt(sub_page, screen, column, &cell))
            return column;
    }
    return 0u;
}

/* Enter the Effect page at SELECT 1 / screen 0 / `typ`. */
void menuEffects_enter(uint8_t *sub_page, uint8_t *column)
{
    const effect_record_t *record = menuEffects_record();

    memset(menuEffects_screen, 0, sizeof(menuEffects_screen));
    menuEffects_typeEdit = 0u;
    menuEffects_showMorphFlag = 0u;
    menuEffects_lastScene = scene_getActiveIndex();
    menuEffects_lastType = record ? record->type : EFFECT_TYPE_OFF;
    *sub_page = 0u;
    *column = 0u;
}

/* Repeated SHIFT+PERF toggles SELECT 1's two manager screens. */
void menuEffects_toggleFirstScreen(uint8_t *sub_page, uint8_t *column)
{
    menuEffects_typeEdit = 0u;
    menuEffects_screen[0] = (uint8_t)(menuEffects_screen[0] ? 0u : 1u);
    *sub_page = 0u;
    *column = 0u;
}

/* Leaving the page discards an open `typ` candidate and Morph view. */
void menuEffects_leave(void)
{
    menuEffects_typeEdit = 0u;
    menuEffects_showMorphFlag = 0u;
}

/* Resolve a visible column on the remembered screen of one SELECT button. */
uint8_t menuEffects_resolveCell(uint8_t sub_page, uint8_t column,
                                menuEffects_cell_t *out)
{
    if (sub_page >= MENU_FX_SELECT_COUNT)
        sub_page = 0u;
    return menuEffects_cellAt(sub_page, menuEffects_screen[sub_page],
                              (uint8_t)(column % MENU_FX_CELLS), out);
}

/* Return the compact-view scroll marker for one SELECT button. */
uint8_t menuEffects_scrollSign(uint8_t sub_page)
{
    uint8_t count;
    uint8_t screen;

    if (sub_page >= MENU_FX_SELECT_COUNT)
        return 0u;
    count = menuEffects_screenCount(sub_page);
    screen = menuEffects_screen[sub_page];
    if (count <= 1u)
        return 0u;
    if (screen == 0u)
        return '>';
    return (screen + 1u < count) ? '*' : '<';
}

/* Walk every populated cell across screens and SELECT buttons, without wrap. */
uint8_t menuEffects_move(int8_t inc, uint8_t *sub_page, uint8_t *column)
{
    int8_t step = (inc > 0) ? 1 : -1;
    int8_t sp = (int8_t)((*sub_page < MENU_FX_SELECT_COUNT) ? *sub_page : 0u);
    int8_t screen = (int8_t)menuEffects_screen[sp];
    int8_t col = (int8_t)(*column % MENU_FX_CELLS);
    menuEffects_cell_t cell;

    for (;;) {
        col = (int8_t)(col + step);
        if (col >= (int8_t)MENU_FX_CELLS || col < 0) {
            col = (step > 0) ? 0 : (int8_t)(MENU_FX_CELLS - 1u);
            screen = (int8_t)(screen + step);
            while (screen < 0 ||
                   screen >= (int8_t)menuEffects_screenCount((uint8_t)sp)) {
                sp = (int8_t)(sp + step);
                if (sp < 0 || sp >= (int8_t)MENU_FX_SELECT_COUNT)
                    return 0u;
                screen = (step > 0) ? 0 :
                    (int8_t)menuEffects_screenCount((uint8_t)sp) - 1;
            }
        }
        if (menuEffects_cellAt((uint8_t)sp, (uint8_t)screen, (uint8_t)col,
                               &cell)) {
            menuEffects_screen[sp] = (uint8_t)screen;
            *sub_page = (uint8_t)sp;
            *column = (uint8_t)col;
            return 1u;
        }
    }
}

/* Different SELECT buttons land on screen 0; re-presses cycle screens. */
uint8_t menuEffects_selectPressed(uint8_t button, uint8_t *sub_page,
                                  uint8_t *column)
{
    uint8_t count;

    if (button >= MENU_FX_SELECT_COUNT)
        return 0u;
    count = menuEffects_screenCount(button);
    if (count == 0u)
        return 0u;
    if (button == *sub_page)
        menuEffects_screen[button] =
            (uint8_t)((menuEffects_screen[button] + 1u) % count);
    else
        menuEffects_screen[button] = 0u;
    *sub_page = button;
    *column = menuEffects_firstColumn(button, menuEffects_screen[button]);
    return 1u;
}

/* Repair remembered screens and the active cell after a layout change. */
void menuEffects_repairCursor(uint8_t *sub_page, uint8_t *column)
{
    menuEffects_cell_t cell;
    uint8_t sp = (*sub_page < MENU_FX_SELECT_COUNT) ? *sub_page : 0u;
    uint8_t i;

    for (i = 0u; i < MENU_FX_SELECT_COUNT; i++) {
        if (menuEffects_screen[i] >= menuEffects_screenCount(i))
            menuEffects_screen[i] = 0u;
    }
    if (menuEffects_screenCount(sp) == 0u)
        sp = 0u;
    if (!menuEffects_cellAt(sp, menuEffects_screen[sp],
                            (uint8_t)(*column % MENU_FX_CELLS), &cell))
        *column = menuEffects_firstColumn(sp, menuEffects_screen[sp]);
    *sub_page = sp;
}

/* Return the generic Menu dtype for one Effect cell. */
uint8_t menuEffects_cellDtype(const menuEffects_cell_t *cell)
{
    if (!cell)
        return DTYPE_0B127;
    switch (cell->kind) {
    case MENU_FX_CELL_PARAM:
        return cell->descriptor ? cell->descriptor->dtype : DTYPE_0B127;
    case MENU_FX_CELL_LENGTH:
        return DTYPE_1B16;
    case MENU_FX_CELL_MORPH_AMOUNT:
        return DTYPE_0B255;
    default:
        return DTYPE_0B127;
    }
}

/* Read the visible normal/Morph endpoint or manager-owned cell value. */
uint16_t menuEffects_cellValue(const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t scene_index = scene_getActiveIndex();

    if (!cell || !record)
        return 0u;
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE:
        return menuEffects_typeEdit ? menuEffects_typeCandidate : record->type;
    case MENU_FX_CELL_PARAM:
        return effects_getParameter(
            scene_index, cell->index,
            menuEffects_showMorphFlag ? EFFECT_IMAGE_MORPH
                                      : EFFECT_IMAGE_NORMAL);
    case MENU_FX_CELL_RUN:
        return record->seq_run_mode;
    case MENU_FX_CELL_LENGTH:
        return record->seq_length;
    case MENU_FX_CELL_SCALE:
        return record->seq_step_scale;
    case MENU_FX_CELL_MORPH_AMOUNT:
        return scene_getEffectMorphAmount(scene_index);
    default:
        return 0u;
    }
}

/* Clamp an edited value to the cell's retained domain. */
void menuEffects_clampValue(const menuEffects_cell_t *cell, uint16_t *value)
{
    uint16_t max = 127u;
    uint16_t min = 0u;

    if (!cell || !value)
        return;
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE:
        max = effects_registryCount() ?
            (uint16_t)(effects_registryCount() - 1u) : 0u;
        break;
    case MENU_FX_CELL_PARAM: {
        const effect_record_t *record = menuEffects_record();
        const effect_param_descriptor_t *descriptor = record
            ? effects_descriptor(record->type, cell->index) : NULL;
        max = descriptor ? descriptor->max_value : 0u;
        break; }
    case MENU_FX_CELL_RUN:
        max = (uint16_t)(EFFECT_SEQ_RUN_MODE_COUNT - 1u);
        break;
    case MENU_FX_CELL_LENGTH:
        min = EFFECT_SEQ_LENGTH_MIN;
        max = EFFECT_SEQ_LENGTH_MAX;
        break;
    case MENU_FX_CELL_SCALE:
        max = (uint16_t)(EFFECT_SEQ_SCALE_COUNT - 1u);
        break;
    case MENU_FX_CELL_MORPH_AMOUNT:
        max = 255u;
        break;
    default:
        max = 0u;
        break;
    }
    if (*value < min)
        *value = min;
    if (*value > max)
        *value = max;
}

/* Commit one edited cell through the EffectsManager mutation boundary. */
uint8_t menuEffects_cellCommit(const menuEffects_cell_t *cell, uint16_t value)
{
    uint8_t scene_index = scene_getActiveIndex();

    if (!cell)
        return 0u;
    switch (cell->kind) {
    case MENU_FX_CELL_PARAM:
        return effects_setParameter(
            scene_index, cell->index,
            menuEffects_showMorphFlag ? EFFECT_IMAGE_MORPH
                                      : EFFECT_IMAGE_NORMAL,
            (uint8_t)value);
    case MENU_FX_CELL_RUN:
        return effects_setSeqRunMode(scene_index, (uint8_t)value);
    case MENU_FX_CELL_LENGTH:
        return effects_setSeqLength(scene_index, (uint8_t)value);
    case MENU_FX_CELL_SCALE:
        return effects_setSeqStepScale(scene_index, (uint8_t)value);
    case MENU_FX_CELL_MORPH_AMOUNT:
        return effects_setMorphAmount(scene_index, (uint8_t)value);
    default:
        return 0u;
    }
}

/* Format manager-owned tokens; return zero for generic descriptor formatting. */
uint8_t menuEffects_formatValue3(const menuEffects_cell_t *cell, char *dst)
{
    uint16_t value;

    if (!cell || !dst)
        return 0u;
    value = menuEffects_cellValue(cell);
    if (cell->kind == MENU_FX_CELL_TYPE) {
        const char *token = effects_typeToken((effect_type_id_t)value);
        memcpy(dst, token ? token : "---", 3u);
        return 1u;
    }
    if (cell->kind == MENU_FX_CELL_RUN && value < EFFECT_SEQ_RUN_MODE_COUNT) {
        memcpy(dst, menuEffects_runShort[value], 3u);
        return 1u;
    }
    if (cell->kind == MENU_FX_CELL_SCALE && value < EFFECT_SEQ_SCALE_COUNT) {
        memcpy(dst, menuEffects_scaleShort[value], 3u);
        return 1u;
    }
    return 0u;
}

/* Compact three-character label for one cell. */
void menuEffects_shortName(const menuEffects_cell_t *cell, char *dst)
{
    const char *text = "   ";
    uint8_t i;

    if (!cell || !dst)
        return;
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE:         text = "typ"; break;
    case MENU_FX_CELL_RUN:          text = "run"; break;
    case MENU_FX_CELL_LENGTH:       text = "len"; break;
    case MENU_FX_CELL_SCALE:        text = "scl"; break;
    case MENU_FX_CELL_MORPH_AMOUNT: text = "mrp"; break;
    case MENU_FX_CELL_PARAM:
        if (cell->descriptor && cell->descriptor->short_name)
            text = cell->descriptor->short_name;
        break;
    default:
        break;
    }
    for (i = 0u; i < 3u; i++)
        dst[i] = (text[i] != '\0') ? text[i] : ' ';
}

/* Copy a bounded/padded LCD field. */
static void menuEffects_copyField(char *dst, const char *src, uint8_t width)
{
    uint8_t i;

    for (i = 0u; i < width; i++)
        dst[i] = (src && src[i] != '\0') ? src[i] : ' ';
}

/* Paint the manager-owned full view; descriptor PARAM cells use Menu's path. */
uint8_t menuEffects_paintEditView(const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint16_t value = menuEffects_cellValue(cell);

    if (!cell || !record || cell->kind == MENU_FX_CELL_PARAM ||
        cell->kind == MENU_FX_CELL_NONE)
        return 0u;
    memset(&editDisplayBuffer[0][0], ' ', 16u);
    memset(&editDisplayBuffer[1][0], ' ', 16u);
    switch (cell->kind) {
    case MENU_FX_CELL_TYPE: {
        const effect_registry_entry_t *entry =
            effects_registryEntry((effect_type_id_t)value);
        menuEffects_copyField(&editDisplayBuffer[0][0], "Effect", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "Type", 8u);
        menuEffects_copyField(&editDisplayBuffer[1][0],
                              entry ? entry->full8 : "?", 8u);
        if (menuEffects_typeEdit && value != record->type)
            editDisplayBuffer[1][11] = '*';
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
        break; }
    case MENU_FX_CELL_RUN:
        menuEffects_copyField(&editDisplayBuffer[0][0], "FX Seq", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "RunMode", 8u);
        if (value < EFFECT_SEQ_RUN_MODE_COUNT)
            menuEffects_copyField(&editDisplayBuffer[1][0],
                                  menuEffects_runLong[value], 8u);
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
        break;
    case MENU_FX_CELL_LENGTH:
        menuEffects_copyField(&editDisplayBuffer[0][0], "FX Seq", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "Length", 8u);
        numtostrpu(&editDisplayBuffer[1][13], (uint8_t)value, ' ');
        break;
    case MENU_FX_CELL_SCALE:
        menuEffects_copyField(&editDisplayBuffer[0][0], "FX Seq", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "StepScal", 8u);
        if (value < EFFECT_SEQ_SCALE_COUNT)
            menuEffects_copyField(&editDisplayBuffer[1][0],
                                  menuEffects_scaleLong[value], 8u);
        (void)menuEffects_formatValue3(cell, &editDisplayBuffer[1][13]);
        break;
    case MENU_FX_CELL_MORPH_AMOUNT:
        menuEffects_copyField(&editDisplayBuffer[0][0], "Effect", 8u);
        menuEffects_copyField(&editDisplayBuffer[0][8], "Morph", 8u);
        numtostrpu(&editDisplayBuffer[1][13], (uint8_t)value, ' ');
        break;
    default:
        return 0u;
    }
    return 1u;
}

/* Wide 0..255 Effect rows use the double-rate endless-pot scale. */
uint8_t menuEffects_cellWantsDoublePot(const menuEffects_cell_t *cell)
{
    const effect_param_descriptor_t *descriptor;

    if (!cell)
        return 0u;
    if (cell->kind == MENU_FX_CELL_MORPH_AMOUNT)
        return 1u;
    if (cell->kind != MENU_FX_CELL_PARAM)
        return 0u;
    descriptor = menuEffects_record()
        ? effects_descriptor(menuEffects_record()->type, cell->index) : NULL;
    return (uint8_t)(descriptor && descriptor->max_value > 127u);
}

/* Open/close the `typ` browse transaction and commit only on click-out. */
uint8_t menuEffects_editModeChanged(uint8_t edit_active,
                                    const menuEffects_cell_t *cell)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t scene_index = scene_getActiveIndex();
    uint8_t i;

    if (edit_active) {
        if (cell && cell->kind == MENU_FX_CELL_TYPE && record) {
            menuEffects_typeEdit = 1u;
            menuEffects_typeCandidate = record->type;
            menuEffects_typeScene = scene_index;
        }
        return 0u;
    }
    if (!menuEffects_typeEdit)
        return 0u;
    menuEffects_typeEdit = 0u;
    if (!record || scene_index != menuEffects_typeScene ||
        menuEffects_typeCandidate == record->type)
        return 0u;
    if (!effects_changeType(scene_index, menuEffects_typeCandidate))
        return 0u;
    for (i = 1u; i < MENU_FX_SELECT_COUNT; i++)
        menuEffects_screen[i] = 0u;
    menuEffects_lastType = menuEffects_typeCandidate;
    return 1u;
}

/* Turn inside the `typ` view browses registry ids without wrapping. */
void menuEffects_typeBrowse(int8_t inc)
{
    uint8_t count = effects_registryCount();

    if (!menuEffects_typeEdit || count == 0u)
        return;
    if (inc > 0 && menuEffects_typeCandidate + 1u < count)
        menuEffects_typeCandidate++;
    else if (inc < 0 && menuEffects_typeCandidate > 0u)
        menuEffects_typeCandidate--;
}

/* Momentary SHIFT Morph view. */
void menuEffects_setShowMorph(uint8_t on)
{
    menuEffects_showMorphFlag = (uint8_t)(on != 0u);
}

uint8_t menuEffects_showMorph(void)
{
    return menuEffects_showMorphFlag;
}

/* Detect a Scene/type change while the Effect page remains visible. */
uint8_t menuEffects_service(void)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t scene_index = scene_getActiveIndex();
    effect_type_id_t type = record ? record->type : EFFECT_TYPE_OFF;
    uint8_t actions = 0u;

    if (scene_index != menuEffects_lastScene || type != menuEffects_lastType) {
        menuEffects_lastScene = scene_index;
        menuEffects_lastType = type;
        actions = (uint8_t)(MENU_FX_ACT_REPAINT | MENU_FX_ACT_REPAIR);
        if (menuEffects_typeEdit) {
            menuEffects_typeEdit = 0u;
            actions |= MENU_FX_ACT_EXIT_EDIT;
        }
    }
    return actions;
}

/* Return the active type's optional gesture hook table. */
static const effect_ui_hooks_t *menuEffects_hooks(void)
{
    return menuEffects_entry()->ui;
}

/* Dispatch SELECT hooks; zero falls back to the default page behavior. */
uint8_t menuEffects_hookSelect(uint8_t button, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    return (hooks && hooks->select) ? hooks->select(button, shift, pressed)
                                    : 0u;
}

/* Dispatch TRACK hooks; zero falls back to mute/select behavior. */
uint8_t menuEffects_hookTrack(uint8_t track, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    return (hooks && hooks->track) ? hooks->track(track, shift, pressed) : 0u;
}

/* Dispatch BAR hooks; zero leaves BAR inert on the Effect page. */
uint8_t menuEffects_hookBar(uint8_t bar, uint8_t shift, uint8_t pressed)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    return (hooks && hooks->bar) ? hooks->bar(bar, shift, pressed) : 0u;
}

/* Let a type add LEDs after the page paints its own. */
void menuEffects_renderLeds(void)
{
    const effect_ui_hooks_t *hooks = menuEffects_hooks();
    if (hooks && hooks->render_leds)
        hooks->render_leds();
}
