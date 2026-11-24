/*
 * KatranParameters.h (S103 P1).
 *
 * What:       the registry-facing exports of KatranParameters.c: the 57
 *             descriptor rows, their count, the eight voice pages, the display
 *             label, the type flags and the inactive-cell test.
 * Why:        InstrumentManager's instrument_registry[] imports them, as it
 *             imports the other engines' tables.
 * KATRAN_PARAM_DESCRIPTOR_COUNT 57: 21 platform rows + 36 Katran rows.
 * KATRAN_PARAM_FX_FIRST          60: the first index reserved for the later
 *                                insert effect (fx_type, fx_rate, fx_depth,
 *                                fx_mix; P§9). The Basic table must end at or
 *                                before it (asserted in KatranParameters.c),
 *                                so the four effect rows always fit the 64-row
 *                                Kit image when the Basic/Advanced merge
 *                                appends them after the last Basic row, as
 *                                the Colloid's clx rows follow its 59.
 * KATRAN_MENU_PAGE_COUNT         8.
 * KAT_IDX_*                      the indices InstrumentManager uses (none at
 *                                first; kept for the merge's fx_type).
 * Accessors:  InstrumentManager.c (registry row; cellInactive).
 * Affiliates: KatranVoice.h; MenuText.h (katSourceNames, katLoopTypeNames,
 *             alkTypeNames); tools/katran/make_init_kat.py.
 */
#ifndef KATRANPARAMETERS_H_
#define KATRANPARAMETERS_H_

#include "InstrumentManager.h"
#include "KatranVoice.h"

#define KATRAN_PARAM_DESCRIPTOR_COUNT  57u
#define KATRAN_PARAM_FX_FIRST          60u
#define KATRAN_MENU_PAGE_COUNT         8u
#define KATRAN_INSTRUMENT_TYPE_FLAGS   INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor katran_param_descriptors[];
extern const uint8_t katran_param_descriptor_count;
extern const instrument_menu_page_t katran_menu_pages[];
extern const uint8_t katran_menu_page_count;
extern const char katran_instrument_display_label[];
extern const uint8_t katran_instrument_type_flags;
/* The later insert effect's first row (the merge's fx_type). */
#define KAT_IDX_FX_TYPE   KATRAN_PARAM_FX_FIRST

/*
 * Whether a Katran cell has no effect in the voice's current mode (S103 P1).
 *
 * What:       1 for dmp while src is off (nothing reads the arena), and for
 *             pos under typ wav when OSC A is not an arena reader (the wav
 *             oscillators are free-running: no frame, width or window).
 * Why:        the compact cell shows "---" for a value that changes nothing
 *             (menu_formatCellValue3()); the value is still stored and
 *             edited, and takes effect when the mode changes.
 * Inputs:     voice (the slot's runtime member), descriptor (a row of
 *             katran_param_descriptors[], or any other pointer: 0).
 * Accessors:  InstrumentManager.c instrumentManager_cellInactive().
 */
uint8_t katranParameters_cellInactive(const KatranVoice *voice,
                                      const ParamDescriptor *descriptor);

#endif /* KATRANPARAMETERS_H_ */
