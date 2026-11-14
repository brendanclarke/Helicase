/*
 * TetraktParameters.h - the Tetrakt descriptor table and menu layout
 * (S096 P1).
 *
 * What:       the registry-facing exports of TetraktParameters.c: the 64
 *             descriptor rows, the eight voice sub-pages, the display label,
 *             the type flags, the switch row's index, and the velocity-group
 *             helpers.
 * Why:        InstrumentManager's instrument_registry[] imports these, as it
 *             imports the other engines' tables.
 * Accessors:  InstrumentManager.c (the registry row; the velocity target
 *             branches, SD3).
 * Affiliates: TetraktVoice.h (the bound members); check_special_tags.py
 *             (TABLES['TKT']); tools/tetrakt/make_init_tkt.py (the keys).
 */
#ifndef TETRAKT_PARAMETERS_H_
#define TETRAKT_PARAMETERS_H_

#include "InstrumentManager.h"
#include "TetraktVoice.h"

/* 64 rows (plan §8.1): 40 layer rows, the switch, the platform's 14 (vol,
 * pan, the LFO pair's 8, the LFO targets' 4), velocity's 3, the space
 * stage's 6. Every cell of the Kit image. */
#define TETRAKT_PARAM_DESCRIPTOR_COUNT 64u
/* eight sub-pages: the front-panel SELECT1..8 buttons (menu.c) */
#define TETRAKT_MENU_PAGE_COUNT        8u
/* Basic (plan Q2): any slot, any number per Scene, held to the Basic share. */
#define TETRAKT_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_BASIC
/* The layer switch row (lon): the registry's switch_row_plus1 is this + 1. */
#define TETRAKT_PARAM_LAYER_ON_INDEX   20u

extern const ParamDescriptor tetrakt_param_descriptors[];
extern const uint8_t tetrakt_param_descriptor_count;
extern const instrument_menu_page_t tetrakt_menu_pages[];
extern const uint8_t tetrakt_menu_page_count;
extern const char tetrakt_instrument_display_label[];
extern const uint8_t tetrakt_instrument_type_flags;

/*
 * The velocity group a destination token names (S096 P1 SD3, plan §4.2).
 *
 * What:       for a local token that is a layer-A row of one of the nine
 *             groups (tuA lvA dyA pnA drA srA spA tnA teA), its TKT_KIND_*;
 *             for every other token TKT_KIND_NONE.
 * Accessors:  InstrumentManager.c (the velocity target bind, the '*' test).
 */
uint8_t tetraktParameters_velocityKind(uint8_t token);
/*
 * Whether a token is hidden from the velocity list (S096 P1 SD3).
 *
 * What:       1 for the B, C and D rows of the nine groups: velocity names
 *             each group once, by its layer-A row; 0 otherwise.
 * Accessors:  InstrumentManager.c (the velocity target walker and validator).
 */
uint8_t tetraktParameters_velocityTokenHidden(uint8_t token);

#endif /* TETRAKT_PARAMETERS_H_ */

