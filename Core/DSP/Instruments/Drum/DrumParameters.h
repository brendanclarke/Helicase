#ifndef DRUM_PARAMETERS_H_
#define DRUM_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Compile-time registry counts.
 *
 * InstrumentManager's static registry initializer needs constant expressions,
 * while the exported *_count variables remain useful for runtime checks and
 * diagnostics. Keep these macros aligned with the descriptor/page arrays in
 * DrumParameters.c whenever rows/pages are added.
 */
#define DRUM_PARAM_DESCRIPTOR_COUNT 39u
#define DRUM_MENU_PAGE_COUNT       8u

extern const ParamDescriptor drum_param_descriptors[];
extern const uint8_t drum_param_descriptor_count;
extern const instrument_menu_page_t drum_menu_pages[];
extern const uint8_t drum_menu_page_count;

#endif
