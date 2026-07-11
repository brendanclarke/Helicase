#ifndef HIHAT_PARAMETERS_H_
#define HIHAT_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Compile-time registry counts.
 *
 * InstrumentManager's static registry initializer needs constant expressions,
 * while the exported *_count variables remain useful for runtime checks and
 * diagnostics. Keep these macros aligned with the descriptor/page arrays in
 * HiHatParameters.c whenever rows/pages are added. hihat_open_menu_pages must
 * expose the same page count as hihat_menu_pages.
 */
#define HIHAT_PARAM_DESCRIPTOR_COUNT 39u
#define HIHAT_MENU_PAGE_COUNT       8u

extern const ParamDescriptor hihat_param_descriptors[];
extern const uint8_t hihat_param_descriptor_count;
extern const instrument_menu_page_t hihat_menu_pages[];
extern const instrument_menu_page_t hihat_open_menu_pages[];
extern const uint8_t hihat_menu_page_count;

#endif
