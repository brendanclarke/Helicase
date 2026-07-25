#ifndef CYMBAL_PARAMETERS_H_
#define CYMBAL_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Compile-time registry counts.
 *
 * InstrumentManager's static registry initializer needs constant expressions,
 * while the exported *_count variables remain useful for runtime checks and
 * diagnostics. Keep these macros aligned with the descriptor/page arrays in
 * CymbalParameters.c whenever rows/pages are added.
 */
#define CYMBAL_PARAM_DESCRIPTOR_COUNT 39u
#define CYMBAL_MENU_PAGE_COUNT       8u
#define CYMBAL_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_ADVANCED

extern const ParamDescriptor cymbal_param_descriptors[];
extern const uint8_t cymbal_param_descriptor_count;
extern const instrument_menu_page_t cymbal_menu_pages[];
extern const uint8_t cymbal_menu_page_count;
extern const char cymbal_instrument_display_label[];
extern const uint8_t cymbal_instrument_type_flags;

#endif
