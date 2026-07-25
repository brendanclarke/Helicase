#ifndef HIHAT_PARAMETERS_H_
#define HIHAT_PARAMETERS_H_

#include "InstrumentManager.h"

/*
 * Compile-time registry counts.
 *
 * InstrumentManager's static registry initializer needs constant expressions,
 * while the exported *_count variables remain useful for runtime checks and
 * diagnostics. Keep these macros aligned with the descriptor/page arrays in
 * HiHatParameters.c whenever rows/pages are added.
 */
#define HIHAT_PARAM_DESCRIPTOR_COUNT 39u
#define HIHAT_MENU_PAGE_COUNT       8u
#define HIHAT_INSTRUMENT_TYPE_FLAGS \
    (INSTRUMENT_FLAG_ADVANCED | INSTRUMENT_FLAG_CHOKE)

extern const ParamDescriptor hihat_param_descriptors[];
extern const uint8_t hihat_param_descriptor_count;
extern const instrument_menu_page_t hihat_menu_pages[];
extern const uint8_t hihat_menu_page_count;
extern const char hihat_instrument_display_label[];
extern const uint8_t hihat_instrument_type_flags;

#endif
