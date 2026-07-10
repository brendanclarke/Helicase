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
#include "modulationNode.h"
#include "valueShaper.h"
#include "globals.h"
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
      drum_param_descriptors, 35u, drum_menu_pages, 8u },
    { INSTRUMENT_TYPE_SNR, "snr", ".snr",
      snare_param_descriptors, 34u, snare_menu_pages, 8u },
    { INSTRUMENT_TYPE_CYM, "cym", ".cym",
      cymbal_param_descriptors, 35u, cymbal_menu_pages, 8u },
    { INSTRUMENT_TYPE_HAT, "hat", ".hat",
      hihat_param_descriptors, 35u, hihat_menu_pages, 8u },
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
    return instrumentManager_menuDescriptorIndex(type, page, position, 0);
}

const ParamDescriptor *instrumentManager_menuDescriptorIndex(
    instrument_type_t type, uint8_t page, uint8_t position, uint8_t *index_out)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t descriptor_index;

    /*
     * Resolve an instrument-owned menu cell.
     *
     * Inputs are a slot's instrument type plus old-menu page/position. The
     * layout tables store descriptor indices, not flat PAR_* ids, so the output
     * is the immutable descriptor that owns text, dtype, storage cell identity,
     * and runtime writer metadata. Menu uses this to render dynamic voice pages
     * while non-voice pages remain in Core/Menu/menuPages.h.
     */
    if (index_out)
        *index_out = INSTRUMENT_MENU_EMPTY;
    if (!entry || !entry->menu_pages)
        return 0;
    if (page >= entry->menu_page_count || position >= 8u)
        return 0;
    descriptor_index = entry->menu_pages[page].descriptor_index[position];
    if (descriptor_index == INSTRUMENT_MENU_SKIP) {
        if (index_out)
            *index_out = descriptor_index;
        return 0;
    }
    if (descriptor_index == INSTRUMENT_MENU_EMPTY ||
        descriptor_index >= entry->descriptor_count) {
        return 0;
    }
    if (index_out)
        *index_out = descriptor_index;
    return &entry->descriptors[descriptor_index];
}

const ParamDescriptor *instrumentManager_voicePageDescriptorIndex(
    instrument_type_t type, uint8_t voice_page, uint8_t page, uint8_t position,
    uint8_t *index_out)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    const instrument_menu_page_t *pages;
    uint8_t page_count;
    uint8_t descriptor_index;

    /*
     * Voice-page aware variant of the menu lookup.
     *
     * Most instrument types have one layout, but the legacy hihat UI exposed
     * VOICE6 as closed-hat decay and VOICE7 as open-hat decay while both edit
     * the same runtime hihat slot. Menu passes the physical voice page here so
     * InstrumentManager can select the open-hat layout without teaching Menu
     * hihat descriptor indices.
     */
    if (index_out)
        *index_out = INSTRUMENT_MENU_EMPTY;
    if (!entry)
        return 0;

    pages = entry->menu_pages;
    page_count = entry->menu_page_count;
    if (type == INSTRUMENT_TYPE_HAT && voice_page == 6u) {
        pages = hihat_open_menu_pages;
        page_count = hihat_menu_page_count;
    }

    if (!pages || page >= page_count || position >= 8u)
        return 0;
    descriptor_index = pages[page].descriptor_index[position];
    if (descriptor_index == INSTRUMENT_MENU_SKIP) {
        if (index_out)
            *index_out = descriptor_index;
        return 0;
    }
    if (descriptor_index == INSTRUMENT_MENU_EMPTY ||
        descriptor_index >= entry->descriptor_count) {
        return 0;
    }
    if (index_out)
        *index_out = descriptor_index;
    return &entry->descriptors[descriptor_index];
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

static float instrumentManager_pitchModAmount(uint8_t value)
{
    const float val = value / 127.0f;
    return val * val * PITCH_AMOUNT_FACTOR;
}

static instrument_type_t instrumentManager_slotType(uint8_t slot)
{
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_getActiveIndex(), slot);
    return instrument ? instrument->type : INSTRUMENT_TYPE_UNKNOWN;
}

static OscInfo *instrumentManager_osc(uint8_t slot, const char *key)
{
    instrument_type_t type = instrumentManager_slotType(slot);

    if (!key)
        return 0;
    if (slot < 3u && type == INSTRUMENT_TYPE_DRM) {
        if (strncmp(key, "osc1_", 5) == 0) return &voiceArray[slot].osc;
        if (strncmp(key, "osc2_", 5) == 0) return &voiceArray[slot].modOsc;
    }
    if (slot == 3u && type == INSTRUMENT_TYPE_SNR) {
        if (strncmp(key, "osc1_", 5) == 0) return &snareVoice.osc;
        if (strncmp(key, "noise_", 6) == 0) return &snareVoice.noiseOsc;
    }
    if (slot == 4u && type == INSTRUMENT_TYPE_CYM) {
        if (strncmp(key, "osc1_", 5) == 0) return &cymbalVoice.osc;
        if (strncmp(key, "osc2_", 5) == 0) return &cymbalVoice.modOsc;
        if (strncmp(key, "osc3_", 5) == 0) return &cymbalVoice.modOsc2;
    }
    if (slot == 5u && type == INSTRUMENT_TYPE_HAT) {
        if (strncmp(key, "osc1_", 5) == 0) return &hatVoice.osc;
        if (strncmp(key, "osc2_", 5) == 0) return &hatVoice.modOsc;
        if (strncmp(key, "osc3_", 5) == 0) return &hatVoice.modOsc2;
    }
    return 0;
}

static ResonantFilter *instrumentManager_filter(uint8_t slot)
{
    switch (slot) {
    case 0u:
    case 1u:
    case 2u: return &voiceArray[slot].filter;
    case 3u: return &snareVoice.filter;
    case 4u: return &cymbalVoice.filter;
    case 5u: return &hatVoice.filter;
    default: return 0;
    }
}

static SlopeEg2 *instrumentManager_ampEg(uint8_t slot)
{
    switch (slot) {
    case 0u:
    case 1u:
    case 2u: return &voiceArray[slot].oscVolEg;
    case 3u: return &snareVoice.oscVolEg;
    case 4u: return &cymbalVoice.oscVolEg;
    case 5u: return &hatVoice.oscVolEg;
    default: return 0;
    }
}

static DecayEg *instrumentManager_pitchEg(uint8_t slot)
{
    switch (slot) {
    case 0u:
    case 1u:
    case 2u: return &voiceArray[slot].oscPitchEg;
    case 3u: return &snareVoice.oscPitchEg;
    default: return 0;
    }
}

static Distortion *instrumentManager_distortion(uint8_t slot)
{
    switch (slot) {
    case 0u:
    case 1u:
    case 2u: return &voiceArray[slot].distortion;
    case 3u: return &snareVoice.distortion;
    case 4u: return &cymbalVoice.distortion;
    case 5u: return &hatVoice.distortion;
    default: return 0;
    }
}

static TransientGenerator *instrumentManager_transient(uint8_t slot)
{
    switch (slot) {
    case 0u:
    case 1u:
    case 2u: return &voiceArray[slot].transGen;
    case 3u: return &snareVoice.transGen;
    case 4u: return &cymbalVoice.transGen;
    case 5u: return &hatVoice.transGen;
    default: return 0;
    }
}

static uint8_t instrumentManager_writeSpecialRuntime(
    uint8_t slot, const ParamDescriptor *descriptor, uint16_t value)
{
    const char *key = descriptor ? descriptor->file_key : 0;
    OscInfo *osc;
    ResonantFilter *filter;
    SlopeEg2 *ampEg;
    DecayEg *pitchEg;
    TransientGenerator *transient;
    Distortion *distortion;
    uint8_t byteValue = (value > 255u) ? 255u : (uint8_t)value;

    /*
     * Descriptor-owned shaper bridge.
     *
     * The storage address is still slot+descriptor_index. This function only
     * restores the old DSP-side meaning for rows whose runtime update was more
     * than a plain normalized float write in MidiParser.c: oscillator tuning,
     * filter shapers, envelope setters, transient setters, and distortion
     * curves. It intentionally keys from descriptor->file_key so no flat PAR_*
     * identity layer comes back.
     */
    if (!key)
        return 0u;

    osc = instrumentManager_osc(slot, key);
    if (osc) {
        if (strcmp(key, "noise_freq") == 0) {
            osc->freq = byteValue / 127.0f * 22000.0f;
            return 1u;
        }
        if (strstr(key, "pitch_coarse")) {
            osc->midiFreq = (uint16_t)((osc->midiFreq & 0x00ffu) |
                                       ((uint16_t)byteValue << 8));
            osc_recalcFreq(osc);
            return 1u;
        }
        if (strstr(key, "pitch_fine")) {
            osc->midiFreq = (uint16_t)((osc->midiFreq & 0xff00u) |
                                       byteValue);
            osc_recalcFreq(osc);
            return 1u;
        }
    }

    filter = instrumentManager_filter(slot);
    if (filter) {
        if (strcmp(key, "filter_freq") == 0) {
            SVF_directSetFilterValue(filter,
                valueShaperF2F(byteValue / 127.0f, FILTER_SHAPER));
            return 1u;
        }
        if (strcmp(key, "filter_reso") == 0) {
            SVF_setReso(filter, byteValue / 127.0f);
            return 1u;
        }
        if (strcmp(key, "filter_drive") == 0) {
#if UNIT_GAIN_DRIVE
            filter->drive = byteValue / 127.0f;
#else
            SVF_setDrive(filter, byteValue);
#endif
            return 1u;
        }
        if (strcmp(key, "filter_type") == 0) {
            instrumentManager_writeParameter(
                (Parameter){ (void *)((uint8_t *)instrumentManager_runtimeInstance(slot) +
                                      descriptor->runtime.offset),
                             descriptor->runtime.parameter_type },
                (uint16_t)(byteValue + 1u));
            return 1u;
        }
    }

    ampEg = instrumentManager_ampEg(slot);
    if (ampEg) {
        if (strcmp(key, "amp_envelope_attack") == 0) {
            slopeEg2_setAttack(ampEg, byteValue,
                               (uint8_t)(slot < 3u ? AMP_EG_SYNC : 0u));
            return 1u;
        }
        if (strcmp(key, "amp_envelope_decay") == 0) {
            slopeEg2_setDecay(ampEg, byteValue,
                              (uint8_t)(slot < 3u ? AMP_EG_SYNC : 0u));
            return 1u;
        }
        if (strcmp(key, "amp_envelope_slope") == 0) {
            slopeEg2_setSlope(ampEg, byteValue);
            return 1u;
        }
        if (strcmp(key, "amp_envelope_decay_closed") == 0) {
            hatVoice.decayClosed = slopeEg2_calcDecay(byteValue);
            return 1u;
        }
        if (strcmp(key, "amp_envelope_decay_open") == 0) {
            hatVoice.decayOpen = slopeEg2_calcDecay(byteValue);
            return 1u;
        }
    }

    pitchEg = instrumentManager_pitchEg(slot);
    if (pitchEg) {
        if (strcmp(key, "pitch_envelope_decay") == 0) {
            DecayEg_setDecay(pitchEg, byteValue);
            return 1u;
        }
        if (strcmp(key, "pitch_envelope_slope") == 0) {
            DecayEg_setSlope(pitchEg, byteValue);
            return 1u;
        }
        if (strcmp(key, "pitch_envelope_amount") == 0) {
            if (slot < 3u)
                voiceArray[slot].egPitchModAmount =
                    instrumentManager_pitchModAmount(byteValue);
            else
                snareVoice.egPitchModAmount =
                    instrumentManager_pitchModAmount(byteValue);
            return 1u;
        }
    }

    transient = instrumentManager_transient(slot);
    if (transient) {
        if (strcmp(key, "transient_wave") == 0) {
            transient_setWaveform(transient, byteValue);
            return 1u;
        }
        if (strcmp(key, "transient_freq") == 0) {
            transient->pitch = 1.0f + ((byteValue / 33.9f) - 0.75f);
            return 1u;
        }
    }

    distortion = instrumentManager_distortion(slot);
    if (distortion && strcmp(key, "instrument_drive") == 0) {
        setDistortionShape(distortion, byteValue);
        return 1u;
    }

    if (strcmp(key, "lfo_rate") == 0) {
        switch (slot) {
        case 0u:
        case 1u:
        case 2u: lfo_setFreq(&voiceArray[slot].lfo, byteValue); break;
        case 3u: lfo_setFreq(&snareVoice.lfo, byteValue); break;
        case 4u: lfo_setFreq(&cymbalVoice.lfo, byteValue); break;
        case 5u: lfo_setFreq(&hatVoice.lfo, byteValue); break;
        default: return 0u;
        }
        return 1u;
    }

    return 0u;
}

uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       uint16_t value)
{
    void *instance;
    Parameter parameter;

    if (!descriptor)
        return 0u;

    if (instrumentManager_writeSpecialRuntime(slot, descriptor, value))
        return 1u;

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
        mixer_decimation_rate[slot] =
            valueShaperI2F((uint8_t)((value > 127u) ? 127u : value), -0.7f);
        return 1u;

    case INSTRUMENT_BIND_VELOCITY_AMOUNT:
        if (slot >= INSTRUMENT_SLOT_COUNT)
            return 0u;
        velocityModulators[slot].amount =
            (float)((value > 127u) ? 127u : value) / 127.0f;
        return 1u;

    case INSTRUMENT_BIND_VELOCITY_TARGET:
        if (slot >= INSTRUMENT_SLOT_COUNT)
            return 0u;
        /*
         * Canonical target IDs are wider than the legacy ModulationNode
         * destination field. Until ModulationNode is moved fully to
         * descriptor IDs, only the off sentinel can be applied without
         * reviving a static PAR_* target table. Non-off values remain stored in
         * SceneData and will become active when the descriptor-target
         * ModulationNode patch lands.
         */
        if (value == INSTRUMENT_PARAM_INVALID) {
            modNode_setDestination(&velocityModulators[slot], 0u);
            return 1u;
        }
        return instrumentManager_targetValid(scene_getActiveIndex(),
                                             value,
                                             INSTRUMENT_TARGET_MODULATION);

    case INSTRUMENT_BIND_LFO_TARGET_VOICE:
        /*
         * The selected target voice is stored in the descriptor cell and paired
         * with lfo_target_param when that later binding is applied. There is no
         * standalone DSP write for this value.
         */
        return (uint8_t)(value >= 1u && value <= INSTRUMENT_SLOT_COUNT);

    case INSTRUMENT_BIND_LFO_TARGET_PARAM:
        /*
         * Same limitation as velocity targets: descriptor IDs are stored and
         * validated now, but legacy ModulationNode cannot yet apply non-off
         * descriptor destinations without an adapter.
         */
        if (value == INSTRUMENT_PARAM_INVALID) {
            switch (slot) {
            case 0u:
            case 1u:
            case 2u:
                modNode_setDestination(&voiceArray[slot].lfo.modTarget, 0u);
                break;
            case 3u:
                modNode_setDestination(&snareVoice.lfo.modTarget, 0u);
                break;
            case 4u:
                modNode_setDestination(&cymbalVoice.lfo.modTarget, 0u);
                break;
            case 5u:
                modNode_setDestination(&hatVoice.lfo.modTarget, 0u);
                break;
            default:
                return 0u;
            }
            return 1u;
        }
        return instrumentManager_targetValid(scene_getActiveIndex(),
                                             value,
                                             INSTRUMENT_TARGET_MODULATION);

    default:
        return 0u;
    }
}
