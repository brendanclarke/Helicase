/*
 * AlkalineParameters.h - the Alkaline (TB-303) descriptor table and menu
 * layout (S093 P4; S093 P8 typ and wavetable rows).
 *
 * What:       the registry-facing exports of AlkalineParameters.c: the
 *             descriptor rows, their count, the eight voice sub-pages, the
 *             display label and the type flags (DrumParameters.h shape).
 * Why:        InstrumentManager's instrument_registry[] imports these, as it
 *             imports the four other engines' tables.
 * Accessors:  InstrumentManager.c (registry row); nothing else.
 * Affiliates: AlkalineVoice.h (the bound members); check_special_tags.py
 *             (TABLES['ALK']); tools/alkaline/make_init_alk.py (the keys).
 */
#ifndef ALKALINE_PARAMETERS_H_
#define ALKALINE_PARAMETERS_H_

#include "InstrumentManager.h"

/* 29 rows: 10 Alkaline rows, vol/pan, 8 LFO rows, 2 velocity rows, 4 LFO
 * target rows, then (S093 P6, appended) alk_attack and osc1_position, and
 * (S093 P8, appended) osc1_type (asserted against alkaline_param_index_t in
 * the .c) */
#define ALKALINE_PARAM_DESCRIPTOR_COUNT 31u   /* S094 P1 S4: + instrument_drive (29), alk_tone (30) */
/* S099 P1 (P-E): the Advanced Alkaline (alx) uses the same table with 35
 * rows: the 31 above, then fx rte dpt mix (asserted; S099 P2 removed cbw
 * cdv cwd). */
#define ALKALINE_ADV_PARAM_DESCRIPTOR_COUNT 35u
#define ALKALINE_ADV_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_ADVANCED
#define ALK_IDX_FX_TYPE                 31u   /* fx_type (alx only) */
/* eight sub-pages: the front-panel SELECT1..8 buttons (menu.c ≈ l. 4608) */
#define ALKALINE_MENU_PAGE_COUNT        8u
/* Basic (S093 D2‴): may take any slot without counting towards the
 * two-Advanced limit; _ADVANCED only if the device gate fails (P5 K1) */
#define ALKALINE_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor alkaline_param_descriptors[];
extern const uint8_t alkaline_param_descriptor_count;
extern const instrument_menu_page_t alkaline_menu_pages[];
extern const uint8_t alkaline_menu_page_count;
extern const char alkaline_instrument_display_label[];
extern const uint8_t alkaline_instrument_type_flags;
extern const instrument_menu_page_t alkaline_adv_menu_pages[];
extern const char alkaline_adv_instrument_display_label[];

/*
 * Whether an Advanced Alkaline cell has no effect now (S099 P1 P-E).
 *
 * What:       1 for crt and cdp unless cho is var or dbl; for cmx, cbw, cdv and
 *             cwd with cho off; else 0 (any row outside the chorus rows, and
 *             any pointer outside this table: 0).
 * Why:        the compact cell shows "---" for a value that changes nothing
 *             (menu_formatCellValue3()), as the Colloid's.
 * Inputs:     voice (the slot's runtime member), descriptor.
 * Accessors:  InstrumentManager.c instrumentManager_cellInactive().
 */
struct AlkalineVoiceStruct;
uint8_t alkalineParameters_cellInactive(const struct AlkalineVoiceStruct *voice,
                                        const ParamDescriptor *descriptor);

#endif /* ALKALINE_PARAMETERS_H_ */
