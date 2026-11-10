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
#define ALKALINE_PARAM_DESCRIPTOR_COUNT 29u
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

#endif /* ALKALINE_PARAMETERS_H_ */
