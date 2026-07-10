#include "InstrumentManager.h"
#include "DrumParameters.h"
#include "SnareParameters.h"
#include "CymbalParameters.h"
#include "HiHatParameters.h"
#include "SceneData.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "mixer.h"
#include <string.h>

/*
 * Registry entries contain the instrument-owned meaning of generic slot
 * storage.
 *
 * ParameterArray/SceneData allocate the amount of per-slot storage. These
 * descriptors define what each storage cell means for a drum/snare/cymbal/hat
 * slot and how the value reaches that instrument's runtime instance.
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

instrument_param_id_t instrumentParam_make(uint8_t slot, uint8_t descriptor_index)
{
    if (slot >= INSTRUMENT_SLOT_COUNT ||
        descriptor_index >= INSTRUMENT_PARAM_COUNT)
        return INSTRUMENT_PARAM_INVALID;
    return (instrument_param_id_t)((uint16_t)slot * INSTRUMENT_PARAM_COUNT +
                                   descriptor_index);
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
                                                     uint8_t descriptor_index)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    if (!entry)
        return 0;
    if (descriptor_index >= entry->descriptor_count)
        return 0;
    return &entry->descriptors[descriptor_index];
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

const ParamDescriptor *instrumentManager_descriptorIndexByKey(
    instrument_type_t type, const char *file_key, uint8_t *index_out)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;
    if (index_out)
        *index_out = 0xffu;
    if (!entry || !file_key)
        return 0;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (strcmp(entry->descriptors[i].file_key, file_key) == 0) {
            if (index_out)
                *index_out = i;
            return &entry->descriptors[i];
        }
    }
    return 0;
}

const ParamDescriptor *instrumentManager_menuDescriptor(instrument_type_t type,
                                                         uint8_t page,
                                                         uint8_t position)
{
    (void)type;
    (void)page;
    (void)position;
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
     * Reset all generic storage images together so a shorter replacement
     * instrument cannot expose stale values from the old type.
     */
    memset(slot, 0, sizeof(*slot));
    slot->type = entry ? type : INSTRUMENT_TYPE_UNKNOWN;
    if (!entry)
        return;
    for (i = 0u; i < entry->descriptor_count; i++) {
        slot->parameter_images.instrument_parameters[i] = 0u;
        slot->parameter_images.morph_instrument_parameters[i] = 0u;
        slot->parameter_images.morph_interpolation[i] = 0u;
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
        !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        return 0u;
    }
    return (use == INSTRUMENT_TARGET_MODULATION)
        ? (uint8_t)((descriptor->flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u)
        : (uint8_t)((descriptor->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) != 0u);
}

void *instrumentManager_runtimeInstance(uint8_t slot)
{
    switch (slot) {
    case 0u:
    case 1u:
    case 2u:
        return &voiceArray[slot];
    case 3u:
        return &snareVoice;
    case 4u:
        return &cymbalVoice;
    case 5u:
        return &hatVoice;
    default:
        return 0;
    }
}

static void instrumentManager_writeParameter(Parameter parameter, uint16_t value)
{
    ptrValue shaped;

    if (!parameter.ptr)
        return;

    shaped.itg = value;
    shaped.flt = (float)value / 127.0f;

    switch (parameter.type) {
    case TYPE_UINT8:
        *((uint8_t *)parameter.ptr) = (uint8_t)value;
        break;
    case TYPE_UINT32:
        *((uint32_t *)parameter.ptr) = (uint32_t)value;
        break;
    case TYPE_FLT:
    case TYPE_SPECIAL_F:
    case TYPE_SPECIAL_P:
    case TYPE_SPECIAL_FILTER_F:
        *((float *)parameter.ptr) = shaped.flt;
        break;
    default:
        break;
    }
}

uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       uint16_t value)
{
    void *instance;
    Parameter parameter;

    if (!descriptor)
        return 0u;

    switch (descriptor->runtime.kind) {
    case INSTRUMENT_BIND_INSTANCE_OFFSET:
        instance = instrumentManager_runtimeInstance(slot);
        if (!instance)
            return 0u;
        parameter.ptr = (void *)((uint8_t *)instance + descriptor->runtime.offset);
        parameter.type = descriptor->runtime.parameter_type;
        instrumentManager_writeParameter(parameter, value);
        return 1u;

    case INSTRUMENT_BIND_SLOT_DECIMATION:
        if (slot >= INSTRUMENT_SLOT_COUNT)
            return 0u;
        mixer_decimation_rate[slot] = (float)value / 127.0f;
        return 1u;

    default:
        return 0u;
    }
}
