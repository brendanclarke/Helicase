#define INSTRUMENT_CELL_FORMAT_H_

#include <stdint.h>
#include "InstrumentManager.h"

typedef enum {
    CELL_FMT_NONE = 0,       /* not a formatted row: Menu uses its dtype */
    CELL_FMT_OSC_TYPE,       /* index into oscTypeNames */
    CELL_FMT_RUN_MODE,       /* index into runModeNames */
    CELL_FMT_PWM_FAMILY,     /* index into pwmFamilyNames */
    CELL_FMT_SAMPLE,         /* sample index: short sNN, full = name */
    CELL_FMT_STOCK_WAVE,     /* index into waveformNames */
    CELL_FMT_WAVETABLE,      /* wavetable index: short wNN, full = name */
    CELL_FMT_NUMBER          /* mod row 0..127: full view, number on the left */
} instrument_cell_format_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t index;   /* value clamped to max */
    uint8_t max;     /* current maximum for this cell */
} instrument_cell_format_t;

/*
 * Describe one instrument cell (S085 C15.1).
 *
 * What:       switches on the row's special-writer tag. Returns 1 and fills
 *             *out for OSC_TYPE, OSC_SOURCE, OSC_WAVE and RUN_MODE rows;
 *             returns 0 for every other row (Menu keeps its dtype path).
 * Inputs:     scene (the image the value comes from), slot, descriptor, value.
 * Outputs:    *out.
 * Accessors:  menu.c formatted-cell sites (C15.5).
 */
uint8_t instrumentCellFormat_describe(uint8_t scene, uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out);
