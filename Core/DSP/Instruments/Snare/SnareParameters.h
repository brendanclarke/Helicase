#ifndef SNARE_PARAMETERS_H_
#define SNARE_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Compile-time registry counts.
 *
 * InstrumentManager's static registry initializer needs constant expressions,
 * while the exported *_count variables remain useful for runtime checks and
 * diagnostics. Keep these macros aligned with the descriptor/page arrays in
 * SnareParameters.c whenever rows/pages are added.
 */
#define SNARE_PARAM_DESCRIPTOR_COUNT 38u
#define SNARE_MENU_PAGE_COUNT       8u
#define SNARE_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor snare_param_descriptors[];
extern const uint8_t snare_param_descriptor_count;
extern const instrument_menu_page_t snare_menu_pages[];
extern const uint8_t snare_menu_page_count;
extern const char snare_instrument_display_label[];
extern const uint8_t snare_instrument_type_flags;

#endif
