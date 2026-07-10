#include "presetMorphEngine.h"
#include "presetManager.h"
#include "InstrumentManager.h"
#include "SceneData.h"
#include <stdint.h>

typedef struct {
    uint8_t scene_index;
    uint8_t slot;
    uint8_t descriptor_index;
    uint8_t requested_amount;
    uint8_t pass_amount;
    uint16_t requested_generation;
    uint16_t pass_generation;
    uint8_t active;
} preset_morph_worker_t;

static preset_morph_worker_t morph_worker;

/*
 * Preserve the firmware's rounded 0..127 endpoint interpolation while using
 * signed arithmetic for descending ranges. Inputs are two bytes plus a 0..127
 * position; output is the rounded byte-domain runtime value.
 */
static uint8_t presetMorph_interpolate(uint8_t a, uint8_t b, uint8_t position)
{
    int32_t fixed = (int32_t)a * 256 +
                    ((int32_t)b - (int32_t)a) * position;
    int32_t value = fixed / 256;
    if ((fixed & 0xff) >= 0x7f)
        value++;
    if (value < 0)
        return 0u;
    if (value > 127)
        return 127u;
    return (uint8_t)value;
}

static void presetMorph_beginPass(void)
{
    morph_worker.slot = 0u;
    morph_worker.descriptor_index = 0u;
    morph_worker.pass_amount = morph_worker.requested_amount;
    morph_worker.pass_generation = morph_worker.requested_generation;
    morph_worker.active = 1u;
}

void presetMorph_init(void)
{
    morph_worker.scene_index = 0u;
    morph_worker.slot = 0u;
    morph_worker.descriptor_index = 0u;
    morph_worker.requested_amount = 0u;
    morph_worker.pass_amount = 0u;
    morph_worker.requested_generation = 0u;
    morph_worker.pass_generation = 0u;
    morph_worker.active = 0u;
}

void presetMorph_request(uint8_t scene_index, uint8_t morph_amount)
{
    scene_t *scene = scene_get(scene_index);
    if (!scene)
        return;
    if (morph_amount > 127u)
        morph_amount = 127u;

    /*
     * Scene settings are authoritative even while the Scene is inactive.
     * A generation counter means a request arriving mid-pass gets a complete
     * follow-up pass without synchronous DSP work in this setter.
     */
    scene->settings.morph_amount = morph_amount;
    morph_worker.scene_index = scene_index;
    morph_worker.requested_amount = morph_amount;
    morph_worker.requested_generation++;
    if (!morph_worker.active)
        presetMorph_beginPass();
}

uint8_t presetMorph_tick(void)
{
    scene_t *scene;

    if (!morph_worker.active)
        return 0u;
    scene = scene_get(morph_worker.scene_index);
    if (!scene) {
        morph_worker.active = 0u;
        return 0u;
    }

    while (morph_worker.slot < INSTRUMENT_SLOT_COUNT) {
        kit_instrument_slot_t *instrument =
            &scene->kit.instruments[morph_worker.slot];

        while (morph_worker.descriptor_index < INSTRUMENT_PARAM_COUNT) {
            uint8_t local = morph_worker.descriptor_index++;
            const ParamDescriptor *descriptor =
                instrumentManager_descriptor(instrument->type, local);
            uint16_t value;
            instrument_param_id_t id;

            if (!descriptor ||
                !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
                continue;
            }
            value = presetMorph_interpolate(
                (uint8_t)instrument->parameter_images.instrument_parameters[local],
                (uint8_t)instrument->parameter_images.morph_instrument_parameters[local],
                morph_worker.pass_amount);
            instrument->parameter_images.morph_interpolation[local] = value;
            id = instrumentParam_make(morph_worker.slot, local);
            if (morph_worker.scene_index == scene_getActiveIndex())
                preset_applyInstrumentRuntimeValue(morph_worker.scene_index,
                                                   id, value);
            return 1u;
        }
        morph_worker.slot++;
        morph_worker.descriptor_index = 0u;
    }

    if (morph_worker.pass_generation != morph_worker.requested_generation) {
        presetMorph_beginPass();
    } else {
        morph_worker.active = 0u;
    }
    return 0u;
}

void presetMorph_rebuildScene(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (scene)
        presetMorph_request(scene_index, scene->settings.morph_amount);
}
