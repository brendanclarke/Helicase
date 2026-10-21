#ifndef PLAITS_KICK_PARAMETERS_H_
#define PLAITS_KICK_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Plaits analog kick (`pbd`) registry exports.
 *
 * Descriptor table, menu pages, Load-browser label and type flags of the
 * Plaits kick instrument. Basic tier: one resonator, about the cost of the
 * LXR Drum. Affiliates: PlaitsKickVoice.c/.h, InstrumentManager registry.
 */
#define PLAITS_KICK_PARAM_DESCRIPTOR_COUNT 27u
#define PLAITS_KICK_MENU_PAGE_COUNT       8u
#define PLAITS_KICK_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor plaits_kick_param_descriptors[];
extern const uint8_t plaits_kick_param_descriptor_count;
extern const instrument_menu_page_t plaits_kick_menu_pages[];
extern const uint8_t plaits_kick_menu_page_count;
extern const char plaits_kick_instrument_display_label[];
extern const uint8_t plaits_kick_instrument_type_flags;

#endif
