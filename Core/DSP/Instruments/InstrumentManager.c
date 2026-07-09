#include "InstrumentManager.h"
#include "DrumParameters.h"
#include "SnareParameters.h"
#include "CymbalParameters.h"
#include "HiHatParameters.h"
#include "SceneData.h"
#include <string.h>

/*
 * Registry entries contain immutable identity/metadata only.
 *
 * Mutable endpoints and routing live in SceneData; DSP application lives in
 * Preset. This boundary keeps file/Menu lookup reusable for inactive Scenes.
 */
static const instrument_registry_entry_t instrument_registry[] = {
    { INSTRUMENT_TYPE_DRM, "drm", ".drm",
      drum_param_descriptors, 35u },
    { INSTRUMENT_TYPE_SNR, "snr", ".snr",
      snare_param_descriptors, 34u },
    { INSTRUMENT_TYPE_CYM, "cym", ".cym",
      cymbal_param_descriptors, 35u },
    { INSTRUMENT_TYPE_HAT, "hat", ".hat",
      hihat_param_descriptors, 35u },
};

static char instrumentManager_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

instrument_param_id_t instrumentParam_make(uint8_t slot, uint8_t local_param)
{
    if (slot >= INSTRUMENT_SLOT_COUNT || local_param >= INSTRUMENT_PARAM_COUNT)
        return INSTRUMENT_PARAM_INVALID;
    return (instrument_param_id_t)((uint16_t)slot * INSTRUMENT_PARAM_COUNT +
                                   local_param);
}

uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id)
{
    return (uint8_t)(id < INSTRUMENT_VOICE_ID_COUNT);
}

uint8_t instrumentParam_slot(instrument_param_id_t id)
{
    return instrumentParam_isVoiceParameter(id)
        ? (uint8_t)(id / INSTRUMENT_PARAM_COUNT)
        : 0xffu;
}

uint8_t instrumentParam_local(instrument_param_id_t id)
{
    return instrumentParam_isVoiceParameter(id)
        ? (uint8_t)(id % INSTRUMENT_PARAM_COUNT)
        : 0xffu;
}

const instrument_registry_entry_t *instrumentManager_registryEntry(
    instrument_type_t type)
{
    uint8_t i;
    for (i = 0u;
         i < (uint8_t)(sizeof(instrument_registry) / sizeof(instrument_registry[0]));
         i++) {
        if (instrument_registry[i].type == type)
            return &instrument_registry[i];
    }
    return 0;
}

instrument_type_t instrumentManager_typeFromText(const char *text)
{
    uint8_t i;
    if (!text)
        return INSTRUMENT_TYPE_UNKNOWN;
    for (i = 0u;
         i < (uint8_t)(sizeof(instrument_registry) / sizeof(instrument_registry[0]));
         i++) {
        if (strcmp(text, instrument_registry[i].type_text) == 0)
            return instrument_registry[i].type;
    }
    return INSTRUMENT_TYPE_UNKNOWN;
}

uint8_t instrumentManager_filenameMatchesType(const char *filename,
                                               instrument_type_t type)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t name_len = 0u;
    uint8_t ext_len = 0u;
    uint8_t i;
    if (!filename || !entry)
        return 0u;
    while (filename[name_len] != '\0')
        name_len++;
    while (entry->extension[ext_len] != '\0')
        ext_len++;
    if (name_len < ext_len)
        return 0u;
    for (i = 0u; i < ext_len; i++) {
        if (instrumentManager_lower(filename[name_len - ext_len + i]) !=
            instrumentManager_lower(entry->extension[i])) {
            return 0u;
        }
    }
    return 1u;
}

const ParamDescriptor *instrumentManager_descriptor(instrument_type_t type,
                                                     uint8_t local_param)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;
    if (!entry)
        return 0;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (entry->descriptors[i].local_param == local_param)
            return &entry->descriptors[i];
    }
    return 0;
}

const ParamDescriptor *instrumentManager_descriptorByKey(instrument_type_t type,
                                                          const char *file_key)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;
    if (!entry || !file_key)
        return 0;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (strcmp(entry->descriptors[i].file_key, file_key) == 0)
            return &entry->descriptors[i];
    }
    return 0;
}

const ParamDescriptor *instrumentManager_menuDescriptor(instrument_type_t type,
                                                         uint8_t page,
                                                         uint8_t position)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;
    if (!entry)
        return 0;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (entry->descriptors[i].menu_page == page &&
            entry->descriptors[i].menu_position == position) {
            return &entry->descriptors[i];
        }
    }
    return 0;
}

void instrumentManager_resetSlot(struct kit_instrument_slot *raw_slot,
                                 instrument_type_t type)
{
    kit_instrument_slot_t *slot = (kit_instrument_slot_t *)raw_slot;
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;
    if (!slot)
        return;

    /*
     * Reset all three images together so a shorter replacement instrument
     * cannot expose stale values from the old type. Supplementals use explicit
     * off sentinels because zero is a valid canonical target.
     */
    memset(slot, 0, sizeof(*slot));
    slot->type = entry ? type : INSTRUMENT_TYPE_UNKNOWN;
    slot->supplemental.velocity_target_param = INSTRUMENT_PARAM_INVALID;
    slot->supplemental.lfo_target_param = INSTRUMENT_PARAM_INVALID;
    if (!entry)
        return;
    for (i = 0u; i < entry->descriptor_count; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        uint8_t local = descriptor->local_param;
        if (descriptor->value_owner != INSTRUMENT_VALUE_PARAMETER_IMAGE)
            continue;
        slot->parameter_images.instrument_parameters[local] =
            descriptor->default_value;
        slot->parameter_images.morph_instrument_parameters[local] =
            descriptor->default_value;
        slot->parameter_images.morph_interpolation[local] =
            descriptor->default_value;
    }
}

uint8_t instrumentManager_targetValid(uint8_t scene_index,
                                      instrument_param_id_t id,
                                      instrument_target_use_t use)
{
    const kit_instrument_slot_t *slot;
    const ParamDescriptor *descriptor;
    uint8_t target_slot;
    if (!instrumentParam_isVoiceParameter(id))
        return 0u;
    target_slot = instrumentParam_slot(id);
    slot = scene_instrumentSlotConst(scene_index, target_slot);
    if (!slot)
        return 0u;
    descriptor = instrumentManager_descriptor(slot->type,
                                               instrumentParam_local(id));
    if (!descriptor ||
        descriptor->value_owner != INSTRUMENT_VALUE_PARAMETER_IMAGE) {
        return 0u;
    }
    return (use == INSTRUMENT_TARGET_MODULATION)
        ? (uint8_t)descriptor->is_modulatable
        : (uint8_t)descriptor->is_step_automatable;
}
