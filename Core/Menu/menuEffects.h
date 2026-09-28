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

/* menuEffects_service() action bits consumed by menu.c. */
#define MENU_FX_ACT_REPAINT    0x01u
#define MENU_FX_ACT_REPAIR     0x02u
#define MENU_FX_ACT_EXIT_EDIT  0x04u

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
 * FX-sequencer hold gestures and row rendering (Session 072 step 8; plan
 * §13.4). SEQ presses jump immediately in `sel`; the shared ButtonHandler
 * hold threshold calls menuEffects_seqHoldExpired(), after which sequenceable
 * cells write lane locks across the physically held steps. Non-sequenceable
 * manager cells are ignored while held. The display and LED helpers are
 * foreground-only and keep all Scene writes inside EffectsManager.
 */
void menuEffects_seqButtonPressed(uint8_t step);
void menuEffects_seqHoldExpired(void);
uint8_t menuEffects_seqHoldActive(void);
uint8_t menuEffects_holdEdit(const menuEffects_cell_t *cell, int16_t delta);
uint8_t menuEffects_holdDisplay(const menuEffects_cell_t *cell,
                                uint8_t *value, uint8_t *locked);
void menuEffects_renderSeqLeds(void);

#endif /* MENU_EFFECTS_H_ */
