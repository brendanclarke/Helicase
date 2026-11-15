/*
 * ColloidParameters.h - the Colloid descriptor table and menu layout
 * (S095 P2).
 *
 * What:       the registry-facing exports of ColloidParameters.c: the
 *             descriptor rows, their count, the eight voice sub-pages, the
 *             display label and the type flags (AlkalineParameters.h shape),
 *             and the menu's inactive-cell test.
 * Why:        InstrumentManager's instrument_registry[] imports these, as it
 *             imports the five other engines' tables.
 * Accessors:  InstrumentManager.c (registry row; instrumentManager_cell-
 *             Inactive()).
 * Affiliates: ColloidVoice.h (the bound members); check_special_tags.py
 *             (TABLES['COL']); tools/colloid/make_init_col.py (the keys).
 */
#ifndef COLLOID_PARAMETERS_H_
#define COLLOID_PARAMETERS_H_

#include "InstrumentManager.h"
#include "ColloidVoice.h"

/* 50 rows (S095 P3): 6 oscillator, 4 amp envelope, sustain and slew, 3
 * intervals with detune, spread and motion, 5 filter envelope with the
 * filter drive, 8 filter (frq res typ rte and four offsets), vol/pan with
 * tone and overdrive, 8 LFO rows, 2 velocity rows, 4 LFO target rows
 * (asserted against colloid_param_index_t in the .c) */
#define COLLOID_PARAM_DESCRIPTOR_COUNT 50u
/* eight sub-pages: the front-panel SELECT1..8 buttons (menu.c) */
#define COLLOID_MENU_PAGE_COUNT        8u
/* Basic (C-D1, user 2026-11-12): any slot, any number per Scene, held to
 * the Basic share. A type with no class flag is not selectable (S094 P2). */
#define COLLOID_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor colloid_param_descriptors[];
extern const uint8_t colloid_param_descriptor_count;
extern const instrument_menu_page_t colloid_menu_pages[];
extern const uint8_t colloid_menu_page_count;
extern const char colloid_instrument_display_label[];
extern const uint8_t colloid_instrument_type_flags;

/*
 * Whether a Colloid cell has no effect in the voice's current mode
 * (S095 P2, C-D16, C-D20; S095 P3 F9).
 *
 * What:       1 for pos and psp under typ wav (the oscillators are
 *             free-running: no start phase, no frame, no width), and for spr
 *             and mot in a mono route (rte a filter type, S095 P3 F9); else 0.
 * Why:        the compact cell shows "---" for a value that changes nothing
 *             (menu_formatCellValue3()); the value is still stored and edited,
 *             and takes effect when the mode changes.
 * Inputs:     voice (the slot's runtime member), descriptor (a row of
 *             colloid_param_descriptors[], or any other pointer: 0).
 * Accessors:  InstrumentManager.c instrumentManager_cellInactive().
 */
uint8_t colloidParameters_cellInactive(const ColloidVoice *voice,
                                       const ParamDescriptor *descriptor);

#endif /* COLLOID_PARAMETERS_H_ */
