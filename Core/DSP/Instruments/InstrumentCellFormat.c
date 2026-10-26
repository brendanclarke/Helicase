/*
 * InstrumentCellFormat.c
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

#include "InstrumentCellFormat.h"
#include "transientGenerator.h"
#include "InsertEffectsManager.h"
#include <string.h>

/*
 * Describe one instrument cell by its runtime meaning (S084 P1 P1-10.7).
 *
 * What:       switches on the descriptor's special writer tag and returns the
 *             format kind, clamped index and current maximum; returns 0 for
 *             every other row so Menu falls through to its dtype paths.
 * Why:        plan §5.2: one definition of the type-dependent cell semantics
 *             for the compact value text, the clamp, the automation view and
 *             the single-parameter view.
 * Inputs:     slot 0..5, descriptor, stored value.
 * Outputs:    1 and *out filled for a formatted row; 0 otherwise.
 * Accessors:  menu.c formatted-cell sites.
 * Affiliates: InstrumentManager.c max rules, MenuText.h name tables.
 */
uint8_t instrumentCellFormat_describe(uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out)
{
    OscInfo *osc;
    uint8_t maxValue;

    if (!descriptor || !out)
        return 0u;
    memset(out, 0, sizeof(*out));
    switch (descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) {
    case IM_SPECIAL_OSC_TYPE:
        out->kind = CELL_FMT_OSC_TYPE;
        out->max = (uint8_t)(OSC_TYPE_COUNT - 1u);
        out->index = (value > out->max) ? out->max : value;
        return 1u;

    case IM_SPECIAL_RUN_MODE:
        if (!instrumentManager_oscSection(slot))
            return 0u;
        out->kind = CELL_FMT_RUN_MODE;
        out->max = (uint8_t)(OSC_RUN_COUNT - 1u);
        out->index = (value > out->max) ? out->max : value;
        return 1u;

    case IM_SPECIAL_OSC_SOURCE:
        osc = instrumentManager_oscForDescriptor(slot, descriptor);
        if (!osc)
            return 0u;
        switch (osc->oscType) {
        case OSC_TYPE_PWM:
            out->kind = CELL_FMT_PWM_FAMILY;
            break;
        case OSC_TYPE_SMP:
        case OSC_TYPE_SLP:
            out->kind = CELL_FMT_SAMPLE;
            break;
        case OSC_TYPE_WTB:
            /* S084 VX2 VX2-9.3: one installed wavetable per src value. */
            out->kind = CELL_FMT_WAVETABLE;
            break;
        case OSC_TYPE_WAV:
        default:
            out->kind = CELL_FMT_NO_SOURCE;
            break;
        }
        maxValue = instrumentManager_oscSourceMax(osc);
        out->max = maxValue;
        out->index = (value > maxValue) ? maxValue : value;
        return 1u;

    case IM_SPECIAL_OSC_WAVE:
        osc = instrumentManager_oscForDescriptor(slot, descriptor);
        if (!osc)
            return 0u;
        switch (osc->oscType) {
        case OSC_TYPE_WAV:
            out->kind = CELL_FMT_STOCK_WAVE;
            break;
        case OSC_TYPE_PWM:
            out->kind = CELL_FMT_PWM_WIDTH;
            break;
        case OSC_TYPE_WTB: {
            /* S084 VX2 VX2-9.3: frame position, with the table's count for
            ** the single-parameter frame display. */
            WavetableInfo info;

            out->kind = CELL_FMT_WTB_POSITION;
            if (sampleMemory_getWavetableInfo(osc->source, &info))
                out->aux = info.frame_count;
            break; }
        case OSC_TYPE_SMP:
        case OSC_TYPE_SLP:
        default:
            out->kind = CELL_FMT_SAMPLE_START;
            break;
        }
        maxValue = instrumentManager_oscWaveMax(osc);
        out->max = maxValue;
        out->index = (value > maxValue) ? maxValue : value;
        return 1u;

    /*
     * Transient and drive selectors (S084 VX2 VX2-9.3).
     *
     * What:       trt: type name; transient_wave: under trs the Snp/Ofs
     *             name, under wav the list position resolved into a PCM
     *             name, a sample (CELL_FMT_SAMPLE, sample index) or a stock
     *             wave (CELL_FMT_STOCK_WAVE, 0..10), with max = the whole
     *             list.
     * Why:        the transient wav row's meaning depends on trt, exactly as
     *             the oscillator wav row depends on typ; the DTYPE_MENU ids
     *             are exhausted, so these rows are recognised by their writer
     *             tags.
     * Inputs:     slot, descriptor, stored value.
     * Outputs:    kind, index, max.
     * Accessors:  menu.c formatted-cell sites.
     * Affiliates: transient_waveMax(), instrumentManager_transientType().
     */
    case IM_SPECIAL_TRANSIENT_TYPE:
        out->kind = CELL_FMT_TRANSIENT_TYPE;
        out->max = (uint8_t)(TRANSIENT_TYPE_SELECTABLE - 1u);
        out->hidden = insertFx_transientHiddenMask(slot);
        out->index = (value > out->max) ? out->max : value;
        return 1u;
    case IM_SPECIAL_TRANSIENT_WAVE: {
        const uint8_t type = instrumentManager_transientType(slot);
        const uint8_t samples = sampleMemory_getNumSamples();

        out->max = transient_waveMax(type);
        value = (value > out->max) ? out->max : value;
        if (type == TRANSIENT_TYPE_BUF) {
            out->kind = CELL_FMT_TRANSIENT_BUF;
            out->index = value;
            /* Remediation §18.4: over-budget options are not offered. */
            out->hidden = insertFx_transientDisabledMask();
        } else if (type == TRANSIENT_TYPE_TRS) {
            out->kind = CELL_FMT_TRANSIENT_SHAPE;
            out->index = value;
        } else if (value < TRANSIENT_STOCK_PCM_COUNT) {
            out->kind = CELL_FMT_TRANSIENT_PCM;
            out->index = value;
        } else if (value < (uint8_t)(TRANSIENT_STOCK_PCM_COUNT + samples)) {
            out->kind = CELL_FMT_SAMPLE;
            out->index = (uint8_t)(value - TRANSIENT_STOCK_PCM_COUNT);
        } else {
            out->kind = CELL_FMT_STOCK_WAVE;
            out->index = (uint8_t)(value - TRANSIENT_STOCK_PCM_COUNT -
                                   samples);
        }
        return 1u; }

    case IM_SPECIAL_MIX_FX_TYPE:
        out->kind = CELL_FMT_MIX_FX_TYPE;
        out->max = (uint8_t)(INSERT_MIX_COUNT - 1u);
        out->hidden = insertFx_mixHiddenMask(slot);
        out->index = (value > out->max) ? out->max : value;
        /* Remediation §18.4: a stored disabled type plays as D+S; show what
        ** plays rather than the stored name. */
        if ((out->hidden & (1u << out->index)) != 0u &&
            insertFx_mixType(slot) != out->index &&
            !insertFx_mixDescriptorEnabled(out->index))
            out->index = insertFx_mixType(slot);
        return 1u;

    default:
        return 0u;
    }
}

/*
 * Type-dependent labels (S084 VX3 VX3-10.2, D13).
 *
 * What: maps dpt/rte/amt and transient buf controls to the active registry's
 * labels. Why: one visible row changes meaning with the selected type while
 * the persisted descriptor key remains stable. Inputs: slot and descriptor.
 * Outputs: a flash-resident short/long label or NULL for generic Menu text.
 */
static const char *instrumentCellFormat_label(uint8_t slot,
                                              const ParamDescriptor *descriptor,
                                              uint8_t long_label)
{
    uint8_t row = 0u;

    if (!descriptor)
        return NULL;
    if ((descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) ==
        IM_SPECIAL_INSTRUMENT_DRIVE)
        row = 0u;
    else if (descriptor->runtime.kind == INSTRUMENT_BIND_SLOT_DECIMATION)
        row = 1u;
    else if ((descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) ==
             IM_SPECIAL_MIX_FX_AMOUNT)
        row = 2u;
    else if (strcmp(descriptor->file_key, "transient_vol") == 0)
        return insertFx_transientActive(slot)
            ? insertFx_transientLabel(insertFx_transientOption(slot), 1u,
                                      long_label) : NULL;
    else if (strcmp(descriptor->file_key, "transient_freq") == 0)
        return insertFx_transientActive(slot)
            ? insertFx_transientLabel(insertFx_transientOption(slot), 2u,
                                      long_label) : NULL;
    else
        return NULL;
    return insertFx_mixLabel(insertFx_mixType(slot), row, long_label);
}

const char *instrumentCellFormat_shortLabel(uint8_t slot,
                                            const ParamDescriptor *descriptor)
{
    return instrumentCellFormat_label(slot, descriptor, 0u);
}

const char *instrumentCellFormat_longLabel(uint8_t slot,
                                           const ParamDescriptor *descriptor)
{
    return instrumentCellFormat_label(slot, descriptor, 1u);
}

/*
 * Next visible value for an encoder step (S084 VX3 VX3-10.2, D16).
 * Inputs: slot, descriptor, current value and signed increment. Output: the
 * bounded value after skipping hidden options; generic rows are unchanged.
 */
uint16_t instrumentCellFormat_step(uint8_t slot,
                                   const ParamDescriptor *descriptor,
                                   uint16_t value, int8_t inc)
{
    instrument_cell_format_t format;
    int8_t direction;
    uint8_t steps;

    if (!descriptor || inc == 0 ||
        !instrumentCellFormat_describe(slot, descriptor,
                                       (uint8_t)(value > 255u ? 255u : value),
                                       &format) || format.hidden == 0u)
        return value;
    direction = inc > 0 ? 1 : -1;
    steps = (uint8_t)(inc > 0 ? inc : -inc);
    /*
     * Remediation R17: `visible` is the last value the walk may stop on. The
     * first version returned the probe value when it reached an end, so a
     * hidden top option (e.g. Rv2 at max) could still be selected.
     */
    {
        uint16_t visible = value;
        uint16_t probe = value;

        while (steps--) {
            do {
                if ((direction > 0 && probe >= format.max) ||
                    (direction < 0 && probe == 0u))
                    return visible;
                probe = (uint16_t)(probe + direction);
            } while (probe < 32u && (format.hidden & (1u << probe)) != 0u);
            visible = probe;
        }
        return visible;
    }
}
