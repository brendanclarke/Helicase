/*
 * KatranParameters.h (S103 P1).
 *
 * What:       the registry-facing exports of KatranParameters.c: the 58
 *             descriptor rows (S105 P1: reorganised, §A1.4), their count,
 *             the eight voice pages, the display label, the type flags and the
 *             inactive-cell test.
 * Why:        InstrumentManager's instrument_registry[] imports them, as it
 *             imports the other engines' tables.
 * KATRAN_PARAM_DESCRIPTOR_COUNT 58: 21 platform rows + 37 Katran rows.
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
 * Affiliates: KatranVoice.h; MenuText.h (katOscTypeNames, katRingModeNames);
 *             tools/katran/make_init_kat.py.
 */
#ifndef KATRANPARAMETERS_H_
#define KATRANPARAMETERS_H_

#include "InstrumentManager.h"
#include "KatranVoice.h"

#define KATRAN_PARAM_DESCRIPTOR_COUNT  58u
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
 * What:       1 for dmp while neither oscillator is buf; for each pos under
 *             wav when its oscillator is not buf; for each wav while its
 *             oscillator is buf (S105 P1 A8).
 * Why:        the compact cell shows "---" for a value that changes nothing
 *             (menu_formatCellValue3()); the value is still stored and
 *             edited, and takes effect when the mode changes.
 * Inputs:     voice (the slot's runtime member), descriptor (a row of
 *             katran_param_descriptors[], or any other pointer: 0).
 * Accessors:  InstrumentManager.c instrumentManager_cellInactive().
 */
uint8_t katranParameters_cellInactive(const KatranVoice *voice,
                                      const ParamDescriptor *descriptor);
/*
 * The rte stop names (S105 P1 A2).
 *
 * What:       for a value that is one of katPlfoStopRow[], copies the
 *             stop's 3-character name and returns 1; else 0 (the caller
 *             prints the number).
 * Accessors:  menu.c menu_formatDtype3().
 */
uint8_t katranParameters_rateName3(uint8_t value, char out[3]);

#endif /* KATRANPARAMETERS_H_ */
