#include "InstrumentCellFormat.h"
#include "SceneData.h"
#include "SampleMemory.h"
#include "Oscillator.h"
#include "OscSection.h"
#include <string.h>

/*
 * Stored oscillator type of a row's oscillator (S085 C15.2).
 *
 * What:       finds the slot instrument's OSC_TYPE row with the same
 *             IM_SPECIAL_OSC_* selector and returns its Normal image value,
 *             clamped to a valid type (wav when absent, and wav for a pwm
 *             the oscillator may not take, P2 §14).
 * Why:        S085 D5: the cell text follows the stored image.
 */
static uint8_t cellFormat_storedType(uint8_t scene, uint8_t slot,
                                     const ParamDescriptor *descriptor)
{
    const kit_instrument_slot_t *inst = scene_instrumentSlotConst(scene, slot);
    const instrument_registry_entry_t *entry;
    const uint8_t want = (uint8_t)(IM_SPECIAL_OSC_TYPE |
        (descriptor->runtime.special & IM_SPECIAL_OSC_MASK));
    uint8_t i;

    if (!inst || !(entry = instrumentManager_registryEntry(inst->type)))
        return OSC_TYPE_WAV;
    for (i = 0u; i < entry->descriptor_count && i < INSTRUMENT_PARAM_COUNT; i++) {
        if (entry->descriptors[i].runtime.special == want) {
            uint8_t t = inst->parameter_images.instrument_parameters[i];

            t = (t < OSC_TYPE_COUNT) ? t : (uint8_t)(OSC_TYPE_COUNT - 1u);
            /* P2 §14: the writer plays a disallowed pwm as wav. */
            return instrumentManager_oscTypeAllowed((uint8_t)inst->type, want, t)
                ? t : OSC_TYPE_WAV;
        }
    }
    return OSC_TYPE_WAV;
}

/*
 * Describe one instrument cell (S085 C15.2).
 *
 * What:       switches on the row's special-writer tag and returns the
 *             format kind, clamped index and current maximum. The wav row
 *             names the selection for its type (stock wave, pwm family,
 *             sample, wavetable); the mod row is a plain number 0..127 for
 *             every type (P2 §14 row swap).
 * Why:        the dtype nibble is full; the text must follow the stored Scene
 *             image (S085 D5), not the runtime.
 * Inputs:     scene, slot, descriptor, value.  Outputs: *out; 1 if handled.
 * Accessors:  menu.c formatted-cell sites.
 * Affiliates: InstrumentManager maxima, MenuText.h, SampleMemory.c.
 */
uint8_t instrumentCellFormat_describe(uint8_t scene, uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out)
{
    uint8_t type;

    if (!descriptor || !out)
        return 0u;
    memset(out, 0, sizeof(*out));
    switch (descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) {
    case IM_SPECIAL_OSC_TYPE:
        out->kind = CELL_FMT_OSC_TYPE;
        out->max = (uint8_t)(OSC_TYPE_COUNT - 1u);
        break;
    case IM_SPECIAL_RUN_MODE:
        out->kind = CELL_FMT_RUN_MODE;
        out->max = (uint8_t)(OSC_RUN_COUNT - 1u);
        break;
    case IM_SPECIAL_OSC_SOURCE:
        type = cellFormat_storedType(scene, slot, descriptor);
        out->kind = CELL_FMT_NUMBER;
        out->max = instrumentManager_oscSourceMaxForType(type);
        break;
    case IM_SPECIAL_OSC_WAVE:
        type = cellFormat_storedType(scene, slot, descriptor);
        out->kind = (type == OSC_TYPE_PWM) ? CELL_FMT_PWM_FAMILY
                  : (type == OSC_TYPE_SMP || type == OSC_TYPE_SLP)
                        ? CELL_FMT_SAMPLE
                  : (type == OSC_TYPE_WTB) ? CELL_FMT_WAVETABLE
                                           : CELL_FMT_STOCK_WAVE;
        out->max = instrumentManager_oscWaveMaxForType(type);
        break;
    default:
        return 0u;
    }
    out->index = (value > out->max) ? out->max : value;
    if ((descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) ==
            IM_SPECIAL_OSC_TYPE) {
        const kit_instrument_slot_t *inst =
            scene_instrumentSlotConst(scene, slot);

        /* P2 §14: a stored pwm on a Cymbal/HiHat FM oscillator reads wav. */
        if (inst && !instrumentManager_oscTypeAllowed(
                        (uint8_t)inst->type, descriptor->runtime.special,
                        out->index))
            out->index = OSC_TYPE_WAV;
    }
    return 1u;
}
