/*
 * Core/Menu/menuEffects.h
 *
 *  Created on: 28.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */
#ifndef MENU_EFFECTS_H_
#define MENU_EFFECTS_H_

#include <stdint.h>
#include "EffectsManager.h"

/*
 * Effect page (SHIFT+PERF) owner (Session 072 step 7; plan §13, A43).
 *
 * What: owns the Effect page's SELECT layout, per-SELECT screen memory,
 * cursor navigation, cell values and formatting, the `typ` click-in/turn/
 * click-out transaction, the momentary SHIFT Morph view, and type UI hook
 * dispatch. menu.c keeps its generic compact renderer, full edit view,
 * encoder, and endless-pot plumbing and delegates every Effect cell here
 * through MENU_CELL_EFFECT.
 * Why a module: the Effect page layout is registry-driven and type-dependent;
 * keeping it outside menu.c leaves the VOICE screen machinery untouched.
 * Context: foreground only (Menu/ButtonHandler). All retained writes go
 * through the EffectsManager edit API; this module never calls SceneData
 * Effect setters directly.
 * Affiliates: menu.c (EFFECT_PAGE branches), buttonHandler.c
 * (SELECT_MODE_FX), EffectsManager (registry, edit API, effects_changeType()).
 */

/* Cell classes on the Effect page. */
typedef enum {
    MENU_FX_CELL_NONE = 0,
    MENU_FX_CELL_TYPE,          /* `typ`: record type, encoder-only view     */
    MENU_FX_CELL_PARAM,         /* descriptor index 0..63                    */
    MENU_FX_CELL_RUN,           /* sequence run mode                         */
    MENU_FX_CELL_LENGTH,        /* sequence length 1..16                     */
    MENU_FX_CELL_SCALE,         /* sequence step scale (shared table index)  */
    MENU_FX_CELL_MORPH_AMOUNT   /* Scene Effect Morph amount (`mrp`)         */
} menuEffects_cell_kind_t;

/* One resolved Effect cell; descriptor is meaningful for PARAM cells only. */
typedef struct {
    uint8_t kind;
    uint8_t index;
    const ParamDescriptor *descriptor;
} menuEffects_cell_t;

/*
 * menuEffects_service() action bits, consumed by menu_serviceRuntimeWidgets().
 *
 * MENU_FX_ACT_REPAINT: the active Scene or its Effect type changed. The page
 *   is redrawn with menu_repaintAll() (forced full resend) unless the same
 *   pass also reports MENU_FX_ACT_HOLD_REPAINT.
 * MENU_FX_ACT_REPAIR: the layout may have changed; menu.c repairs the cursor
 *   (menu_resetActiveParameter()) and the endless-pot mapping.
 * MENU_FX_ACT_EXIT_EDIT: an open `typ` transaction was abandoned; menu.c
 *   leaves the full view.
 * MENU_FX_ACT_HOLD_REPAINT (S074): the SEQ lock-edit hold started, changed
 *   its held steps, or ended. A visible cell keeps its CGRAM marker slot while
 *   its underline moves between the name (row 0) and the held value (row 1),
 *   so menu.c redraws with menu_repaint(). That keeps currentDisplayBuffer
 *   equal to the LCD, and va_queueMarkerTransaction() can restore the old
 *   cell to its plain character before it redefines the slot and writes the
 *   new cell. menu_repaintAll() would erase that knowledge and flash the new
 *   glyph in the old row (the S066 Fix 5 defect). It takes precedence over
 *   MENU_FX_ACT_REPAINT in the same pass.
 * Producer: menuEffects_service(). Consumer: the EFFECT_PAGE branch of
 * menu_serviceRuntimeWidgets(). Affiliates: menu_applyEffectMarkers(),
 * va_queueMarkerTransaction().
 */
#define MENU_FX_ACT_REPAINT    0x01u
#define MENU_FX_ACT_REPAIR     0x02u
#define MENU_FX_ACT_EXIT_EDIT  0x04u
#define MENU_FX_ACT_HOLD_REPAINT 0x08u

/* Page lifecycle and cursor (menuIndex = subPage << 3 | column). */
void menuEffects_enter(uint8_t *sub_page, uint8_t *column);
void menuEffects_toggleFirstScreen(uint8_t *sub_page, uint8_t *column);
void menuEffects_leave(void);
uint8_t menuEffects_resolveCell(uint8_t sub_page, uint8_t column,
                                menuEffects_cell_t *out);
uint8_t menuEffects_scrollSign(uint8_t sub_page);
uint8_t menuEffects_move(int8_t inc, uint8_t *sub_page, uint8_t *column);
uint8_t menuEffects_selectPressed(uint8_t button, uint8_t *sub_page,
                                  uint8_t *column);
void menuEffects_repairCursor(uint8_t *sub_page, uint8_t *column);

/* Cell values, formatting, clamping, and commits. */
uint8_t menuEffects_cellDtype(const menuEffects_cell_t *cell);
uint16_t menuEffects_cellValue(const menuEffects_cell_t *cell);
void menuEffects_clampValue(const menuEffects_cell_t *cell, uint16_t *value);
uint8_t menuEffects_cellCommit(const menuEffects_cell_t *cell, uint16_t value);
uint8_t menuEffects_formatValue3(const menuEffects_cell_t *cell, char *dst);
void menuEffects_shortName(const menuEffects_cell_t *cell, char *dst);
uint8_t menuEffects_paintEditView(const menuEffects_cell_t *cell);
uint8_t menuEffects_cellWantsDoublePot(const menuEffects_cell_t *cell);

/* `typ` transaction and SHIFT Morph view. */
uint8_t menuEffects_editModeChanged(uint8_t edit_active,
                                    const menuEffects_cell_t *cell);
void menuEffects_typeBrowse(int8_t inc);
void menuEffects_setShowMorph(uint8_t on);
uint8_t menuEffects_showMorph(void);

/* Foreground service and type UI hooks. */
uint8_t menuEffects_service(void);
uint8_t menuEffects_hookSelect(uint8_t button, uint8_t shift, uint8_t pressed);
uint8_t menuEffects_hookTrack(uint8_t track, uint8_t shift, uint8_t pressed);
uint8_t menuEffects_hookBar(uint8_t bar, uint8_t shift, uint8_t pressed);
void menuEffects_renderLeds(void);

/*
 * Type page extensions (S074; used by CrumpBit through EffectsManager's
 * select_layout and effect_ui_hooks_t).
 *
 * menuEffects_screenHasCustomRow0(sub_page): nonzero when the remembered
 * screen of that SELECT is flagged in select_layout->custom_row0 and the type
 * has paint_row0. Caller: menu_applyEffectMarkers().
 * menuEffects_paintRow0(sub_page, row0): lets the type paint columns 0..14 of
 * the compact top row on such a screen. Caller: menu_repaintGeneric().
 * menuEffects_formatParamValue3(cell, value, dst): the type's value text for
 * an explicit PARAM value. Caller: compact/full/held Effect rendering.
 * menuEffects_renderSelectLeds(sub_page): the SELECT LED row for the page;
 * the type owns it when requested, otherwise the active SELECT LED is shown.
 * menuEffects_home(sub_page, column): moves page memory to a type home screen.
 * menuEffects_liveRefreshWanted(): nonzero when type-labelled values may
 * depend on live state such as tempo. All are foreground-only and
 * allocation-free; only _home() changes page memory.
 */
uint8_t menuEffects_screenHasCustomRow0(uint8_t sub_page);
void menuEffects_paintRow0(uint8_t sub_page, char *row0);
uint8_t menuEffects_formatParamValue3(const menuEffects_cell_t *cell,
                                      uint8_t value, char *dst);
void menuEffects_renderSelectLeds(uint8_t sub_page);
uint8_t menuEffects_home(uint8_t *sub_page, uint8_t *column);
uint8_t menuEffects_liveRefreshWanted(void);

/*
 * FX-sequencer hold gestures and row rendering (Session 072 step 8; plan
 * §13.4; S074). SEQ presses jump immediately in `sel`; the shared
 * ButtonHandler hold threshold calls menuEffects_seqHoldExpired(), after
 * which sequenceable cells write lane locks across the physically held steps.
 * Non-sequenceable manager cells are ignored while held. S074: the displayed
 * and seeding step is the last step held (the most recently pressed SEQ
 * button still down; if it is released while others stay down, the highest
 * numbered remaining step), so the whole page shows one step. Every held edit
 * writes one value to every held step. The display and LED helpers are
 * foreground-only and keep all Scene writes inside EffectsManager.
 */
void menuEffects_seqButtonPressed(uint8_t step);
void menuEffects_seqHoldExpired(void);
uint8_t menuEffects_seqHoldActive(void);
uint8_t menuEffects_holdEdit(const menuEffects_cell_t *cell, int16_t delta);
uint8_t menuEffects_holdDisplay(const menuEffects_cell_t *cell,
                                uint8_t *value, uint8_t *locked);

/*
 * Held-aware row access for type hooks (S074).
 *
 * menuEffects_shownParam(index): the value the page shows for a row of the
 * active Scene's Effect: the last step held's lane lock when that row has a
 * lane locked there, otherwise the retained value.
 * menuEffects_editParam(index, value): writes a row the way the page edits;
 * during a SEQ hold it locks the row's lane on every held step with value;
 * otherwise it sets the retained value through effects_setParameter().
 * Returns nonzero if a byte changed. Callers: CrumpBit's UI hooks.
 */
uint8_t menuEffects_shownParam(uint8_t index);
uint8_t menuEffects_editParam(uint8_t index, uint8_t value);

/*
 * FX-sequence lock presence for the Effect-page name underline (S074).
 *
 * What: nonzero when the cell's FX-sequence lane is locked on any of the 16
 * retained steps, whether or not that step plays (FX length, run mode, `sel`
 * step and transport are ignored). Lane 0 is Effect Morph (`mrp`); PARAM
 * cells use the active type's registry lane map; cells without a lane
 * (typ/run/len/scl and lane-less rows such as `out`) return zero.
 * Why: the S074 rule (user, 2026-09-29) - any stored automation on a
 * parameter underlines its name. It reads the active Scene's retained record
 * directly (16 mask reads), so it is always current and keeps no cache.
 * Input: a resolved Effect cell. Output: 0/1. Foreground-only and read-only;
 * it allocates nothing. Caller: menu_effectCellAutomated() in menu.c.
 * Affiliates: menuEffects_cellLane(), effects_laneOfParam(), and
 * SceneData's effect_record_t.
 */
uint8_t menuEffects_cellSeqLocked(const menuEffects_cell_t *cell);
void menuEffects_renderSeqLeds(void);

#endif /* MENU_EFFECTS_H_ */
