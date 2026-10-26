/*
 * InstrumentCellFormat.h
 *
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef INSTRUMENT_CELL_FORMAT_H_
#define INSTRUMENT_CELL_FORMAT_H_

#include <stdint.h>
#include "InstrumentManager.h"
#include "InsertEffectsManager.h"

/*
 * InstrumentCellFormat — type-dependent meaning of instrument cells
 * (S084 P1 P1-10.7).
 *
 * What:       for cells whose meaning depends on runtime state (osc type,
 *             osc source, osc waveform, run mode) returns a semantic
 *             description: a format kind, an index and the current maximum.
 *             Menu renders names from MenuText.h tables (decision O3).
 * Why:        plan §5.2: `wav` shows a stock wave, a pwm width or a sample
 *             start depending on type; `src` shows a pwm family or a sample;
 *             the 4-bit dtype field is full, so these rows are recognised by
 *             their special writer tag instead of a new dtype (the
 *             MENU_CELL_STATIC special-case precedent in menu.c).
 * Inputs:     slot, descriptor, stored value.
 * Outputs:    instrument_cell_format_t.
 * Accessors:  menu.c (cell value text, clamp, automation view, single-
 *             parameter view).
 * Affiliates: InstrumentManager.c max rules (shared helpers), MenuText.h
 *             oscTypeNames[], runModeNames[], pwmFamilyNames[],
 *             waveformNames[], SampleMemory display names.
 */
typedef enum {
    CELL_FMT_NONE = 0,       /* not a formatted row: Menu uses its dtype */
    CELL_FMT_OSC_TYPE,       /* index into oscTypeNames */
    CELL_FMT_RUN_MODE,       /* index into runModeNames */
    CELL_FMT_PWM_FAMILY,     /* index into pwmFamilyNames */
    CELL_FMT_SAMPLE,         /* sample index (short sNN, full display name) */
    CELL_FMT_NO_SOURCE,      /* `wav` type source: shown as "-" */
    CELL_FMT_STOCK_WAVE,     /* index into waveformNames */
    CELL_FMT_PWM_WIDTH,      /* 0..127 control, full view "width NN%" */
    CELL_FMT_SAMPLE_START,   /* 0..127 control, full view "start NN%" */
    CELL_FMT_WAVETABLE,      /* wtb source: short wNN, full = folder name */
    CELL_FMT_WTB_POSITION,   /* wtb wav: 0..127, full "frame NNN" (aux = frames) */
    CELL_FMT_TRANSIENT_TYPE, /* index into transientTypeNames */
    CELL_FMT_TRANSIENT_SHAPE,/* trs wav: transientNames[1 + index] */
    CELL_FMT_TRANSIENT_PCM,  /* wav list 0..11: transientNames[3 + index] */
    CELL_FMT_DRIVE_TYPE,     /* index into driveTypeNames */
    CELL_FMT_MIX_FX_TYPE,    /* InsertEffects Mix registry name */
    CELL_FMT_TRANSIENT_BUF   /* InsertEffects transient registry name */
} instrument_cell_format_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t index;
    uint8_t max;
    uint8_t aux;    /* frame count for CELL_FMT_WTB_POSITION, else 0 */
    uint32_t hidden;/* value bits unavailable to the current slot */
} instrument_cell_format_t;

/*
 * Describe one instrument cell by its runtime meaning (S084 P1 P1-10.7).
 *
 * What:       switches on the descriptor's special writer tag and returns the
 *             format kind, clamped index and current maximum.
 * Why:        one definition of the type-dependent cell semantics for the
 *             compact value text, the clamp, the automation view and the
 *             single-parameter view.
 * Inputs:     slot 0..5, descriptor, stored value.
 * Outputs:    1 and *out filled for a formatted row; 0 otherwise (Menu falls
 *             through to its dtype paths).
 * Accessors:  menu.c (va_formatValue3, menu_formatCellValue3,
 *             menu_clampCellValue, menu_repaintGeneric, the automation
 *             formatters).
 * Affiliates: InstrumentManager.h accessors, MenuText.h name tables.
 */
uint8_t instrumentCellFormat_describe(uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out);

const char *instrumentCellFormat_shortLabel(uint8_t slot,
                                            const ParamDescriptor *descriptor);
const char *instrumentCellFormat_longLabel(uint8_t slot,
                                           const ParamDescriptor *descriptor);
uint16_t instrumentCellFormat_step(uint8_t slot,
                                   const ParamDescriptor *descriptor,
                                   uint16_t value, int8_t inc);

#endif /* INSTRUMENT_CELL_FORMAT_H_ */
