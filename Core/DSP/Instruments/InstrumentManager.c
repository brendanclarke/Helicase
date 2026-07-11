#include "InstrumentManager.h"
#include "DrumParameters.h"
#include "SnareParameters.h"
#include "CymbalParameters.h"
#include "HiHatParameters.h"
#include "SceneData.h"
#include "SceneModTargets.h"
#include "presetManager.h"
#include "presetMorphEngine.h"
#include "DrumVoice.h"
#include "Snare.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "mixer.h"
#include "modulationNode.h"
#include "menu.h"
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
      drum_param_descriptors, DRUM_PARAM_DESCRIPTOR_COUNT,
      drum_menu_pages, DRUM_MENU_PAGE_COUNT },
    { INSTRUMENT_TYPE_SNR, "snr", ".snr",
      snare_param_descriptors, SNARE_PARAM_DESCRIPTOR_COUNT,
      snare_menu_pages, SNARE_MENU_PAGE_COUNT },
    { INSTRUMENT_TYPE_CYM, "cym", ".cym",
      cymbal_param_descriptors, CYMBAL_PARAM_DESCRIPTOR_COUNT,
      cymbal_menu_pages, CYMBAL_MENU_PAGE_COUNT },
    { INSTRUMENT_TYPE_HAT, "hat", ".hat",
      hihat_param_descriptors, HIHAT_PARAM_DESCRIPTOR_COUNT,
      hihat_menu_pages, HIHAT_MENU_PAGE_COUNT },
};

/*
 * Resolved descriptor modulation target.
 *
 * Inputs are a canonical instrument_param_id_t plus the active Scene's current
 * slot type. Outputs are the target slot/local descriptor identity, immutable
 * descriptor metadata, direct runtime Parameter pointer/type, and optional
 * oscillator waveform interpolation affiliate plus a cached min/max range.
 * This private record exists so LFO and velocity installers share one
 * descriptor-to-DSP resolution path without Menu, Preset, or ModulationNode
 * learning instrument registry layout.
 */
typedef struct {
    instrument_param_id_t id;
    uint8_t slot;
    uint8_t descriptor_index;
    const ParamDescriptor *descriptor;
    Parameter parameter;
    void *waveInterpTarget;
    mod_node_range_t range;
} instrument_runtime_target_t;

typedef enum {
    INSTALLED_MOD_TARGET_NONE = 0,
    INSTALLED_MOD_TARGET_SLOT_DECIMATION,
    INSTALLED_MOD_TARGET_SCENE_TARGET
} installed_mod_target_kind_t;

typedef struct {
    installed_mod_target_kind_t kind;
    uint16_t target_id;
} installed_mod_target_t;

/*
 * Supplemental modulation targets are installed beside, not inside,
 * ModulationNode.
 *
 * Direct descriptor targets resolve to a live Parameter pointer and can be
 * restored every audio block by ModulationNode. Slot decimation and Scene
 * targets are sound targets with supplemental owners, so they need
 * owner-specific setters instead of fake pointers. InstrumentManager owns this
 * routing table because it already translates menu/Scene target IDs into
 * runtime modulation backends.
 */
static installed_mod_target_t velocity_installed_targets[INSTRUMENT_SLOT_COUNT];
static installed_mod_target_t lfo_installed_targets[INSTRUMENT_SLOT_COUNT][2u];

static uint8_t instrumentManager_resolveModulationTarget(
    uint8_t scene_index,
    instrument_param_id_t id,
    instrument_runtime_target_t *target_out);

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
    if (page >= entry->menu_page_count ||
        position >= INSTRUMENT_MENU_PAGE_CELLS)
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

    if (!pages || page >= page_count ||
        position >= INSTRUMENT_MENU_PAGE_CELLS)
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

const ParamDescriptor *instrumentManager_descriptorIndexForBinding(
    instrument_type_t type, instrument_binding_kind_t kind, uint8_t *index_out)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;

    /*
     * Descriptor binding lookup.
     *
     * Inputs: an instrument type and one runtime binding kind. Output: the
     * descriptor whose runtime.kind matches, plus its local descriptor index
     * when index_out is supplied. The function returns NULL when the instrument
     * type is unknown or the binding does not exist for that type.
     *
     * Why this helper is not inlined into Menu: binding ownership belongs to
     * the instrument registry, and the descriptor index is intentionally local
     * to the current instrument type. Menu needs this lookup to find sibling
     * cells such as lfo_target_voice/lfo_target_param without hardcoding that
     * today's instruments happen to place them at the same local indices.
     *
     * Common accessors/affiliates: instrumentManager_registryEntry() supplies
     * the descriptor table; ParamDescriptor::runtime.kind is the stable
     * binding contract; Menu and storage normalization consume the returned
     * local index to read/write SceneData's generic per-slot parameter cells.
     */
    if (index_out)
        *index_out = INSTRUMENT_MENU_EMPTY;
    if (!entry || !entry->descriptors)
        return 0;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (entry->descriptors[i].runtime.kind == kind) {
            if (index_out)
                *index_out = i;
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
        /*
         * Supplemental target selector defaults.
         *
         * Inputs: descriptor rows with target voice/parameter binding kinds do
         * not write directly into the instrument runtime image. Output: target
         * voices start at visible voice 1, while target parameters start at the
         * explicit off sentinel. This cannot be left as the zeroed memset value
         * because canonical descriptor id 0 is a valid "slot 1, local 0" target
         * rather than off. The morph buffers mirror these defaults so browsing
         * target cells while SHIFT+VOICE is active does not display stale zero
         * as a live target.
         */
        switch (entry->descriptors[i].runtime.kind) {
        case INSTRUMENT_BIND_LFO_TARGET_VOICE:
        case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
            slot->parameter_images.instrument_parameters[i] = 1u;
            slot->parameter_images.morph_instrument_parameters[i] = 1u;
            slot->parameter_images.morph_interpolation[i] = 1u;
            break;
        case INSTRUMENT_BIND_LFO_TARGET_PARAM:
        case INSTRUMENT_BIND_LFO_TARGET_PARAM_2:
        case INSTRUMENT_BIND_VELOCITY_TARGET:
            slot->parameter_images.instrument_parameters[i] =
                INSTRUMENT_PARAM_INVALID;
            slot->parameter_images.morph_instrument_parameters[i] =
                INSTRUMENT_PARAM_INVALID;
            slot->parameter_images.morph_interpolation[i] =
                INSTRUMENT_PARAM_INVALID;
            break;
        default:
            break;
        }
    }
}

static uint8_t instrumentManager_descriptorIsOscWaveTarget(
    const ParamDescriptor *descriptor)
{
    const char *key;

    /*
     * Identify descriptor waveform rows that have an oscillator runtime target.
     *
     * Inputs: one descriptor row. Output: nonzero only for osc/noise waveform
     * fields that InstrumentManager can affiliate with an OscInfo and the
     * bounded waveform interpolation path. This exists so generic menu-byte
     * fields such as filter type, LFO waveform, and sync rate are not treated
     * as safe continuous modulation destinations merely because they are stored
     * as TYPE_UINT8.
     */
    if (!descriptor || descriptor->runtime.parameter_type != TYPE_UINT8 ||
        !descriptor->file_key || !strstr(descriptor->file_key, "_wave")) {
        return 0u;
    }
    key = descriptor->file_key;
    return (uint8_t)(strncmp(key, "osc", 3) == 0 ||
                     strncmp(key, "noise_", 6) == 0);
}

static uint8_t instrumentManager_descriptorSupportsModulationRange(
    const ParamDescriptor *descriptor)
{
    uint8_t dtype;

    /*
     * Decide whether a descriptor has a defined modulation min/max contract.
     *
     * Inputs: descriptor metadata only, before a live runtime pointer is
     * resolved. Output: nonzero when InstrumentManager can later build a
     * stable range for ModulationNode. The rule deliberately excludes
     * TYPE_UINT32, generic DTYPE_MENU bytes, and DTYPE_LFO_POLARITY: their
     * valid values are selectors rather than continuous 0..127 modulation
     * domains, and inventing ranges here would let LFO target selection write
     * invalid DSP states. Oscillator/noise waveform rows are the exception
     * because they have an explicit waveform-id range and an interpolation
     * affiliate.
     */
    if (!descriptor)
        return 0u;
    dtype = (uint8_t)(descriptor->dtype & 0x0fu);
    switch (descriptor->runtime.parameter_type) {
    case TYPE_FLT:
    case TYPE_SPECIAL_F:
        return 1u;
    case TYPE_UINT8:
        if (dtype == DTYPE_LFO_POLARITY)
            return 0u;
        if (dtype == DTYPE_MENU)
            return instrumentManager_descriptorIsOscWaveTarget(descriptor);
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t instrumentManager_descriptorIsSlotDecimationTarget(
    const ParamDescriptor *descriptor)
{
    /*
     * Identify the voice-local supplemental decimation target.
     *
     * Input: descriptor metadata for the target slot's current instrument.
     * Output: nonzero only for the descriptor-owned per-instrument sample-rate
     * parameter. This helper keeps the supplemental binding rule out of Menu
     * and prevents Scene Decimation from being confused with the voice-local
     * instrument_decimation row that happens to share the same short label.
     */
    return (uint8_t)(descriptor &&
                     descriptor->runtime.kind == INSTRUMENT_BIND_SLOT_DECIMATION &&
                     (descriptor->dtype & 0x0fu) == DTYPE_0B127);
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
    if (use == INSTRUMENT_TARGET_MODULATION) {
        /*
         * Modulation targets are either direct runtime fields for the
         * ModulationNode pointer backend or explicit supplemental descriptor
         * targets with their own adapter. Slot decimation is the first
         * supplemental voice target: it is descriptor-owned and image-backed,
         * but applies through mixer_decimation_rate[slot] rather than a
         * voice-struct member pointer. Keeping both cases in the shared
         * validator makes Menu target stepping and DSP target installation
         * agree without a hardcoded parameter list.
         */
        return (uint8_t)(((descriptor->flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u) &&
                         ((descriptor->runtime.kind == INSTRUMENT_BIND_INSTANCE_OFFSET &&
                           instrumentManager_descriptorSupportsModulationRange(descriptor)) ||
                          instrumentManager_descriptorIsSlotDecimationTarget(descriptor)));
    }
    return (uint8_t)((descriptor->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) != 0u);
}

uint8_t instrumentManager_targetLocalValid(uint8_t scene_index,
                                           uint8_t target_slot,
                                           uint8_t local,
                                           instrument_target_use_t use)
{
    instrument_param_id_t id;

    /*
     * Local target validator.
     *
     * Inputs: active/inactive Scene index, zero-based target slot, local
     * descriptor index in that slot's current instrument type, and target use.
     * Output: nonzero only when the packed canonical target exists and passes
     * the normal descriptor flag checks for modulation/automation.
     *
     * Why this is separate from instrumentManager_targetValid(): callers such
     * as the LFO target picker are walking a local descriptor table for one
     * selected voice slot, not editing a pre-packed canonical ID. Keeping the
     * packing rule here prevents Menu from duplicating slot*64 arithmetic and
     * keeps all descriptor capability checks in InstrumentManager.
     *
     * Common clients/affiliates: menu LFO target selection, future load
     * normalization, instrumentParam_make(), SceneData slot type lookup, and
     * ParamDescriptor flag validation.
     */
    id = instrumentParam_make(target_slot, local);
    if (id == INSTRUMENT_PARAM_INVALID)
        return 0u;
    return instrumentManager_targetValid(scene_index, id, use);
}

instrument_param_id_t instrumentManager_stepTargetForSlot(
    uint8_t scene_index, uint8_t target_slot, instrument_param_id_t current,
    int8_t direction, instrument_target_use_t use)
{
    const kit_instrument_slot_t *slot;
    const instrument_registry_entry_t *entry;
    uint8_t current_valid = 0u;
    uint8_t current_local = 0u;

    /*
     * Registry-driven target stepper.
     *
     * Inputs: Scene index, zero-based target slot, current canonical target or
     * INSTRUMENT_PARAM_INVALID for off, signed direction, and target use.
     * Output: a canonical target ID for the next valid descriptor, or
     * INSTRUMENT_PARAM_INVALID for the single off position.
     *
     * Behavior: descriptor order is the picker order. Non-modulatable or
     * non-automatable descriptors are skipped completely rather than rendered
     * as extra "off" entries. Positive movement from off selects the first
     * valid descriptor; negative movement from the first valid descriptor
     * returns off; movement beyond the last valid descriptor stays on the last
     * valid target.
     *
     * Why this cannot live inside Menu's encoder/knob handlers: both input
     * paths need identical traversal, and future load normalization / target
     * browsers need the same registry scan. This function owns descriptor-table
     * traversal while callers own UI intent and commit timing.
     *
     * Common accessors/affiliates: SceneData tells us the target slot's current
     * instrument type; instrumentManager_targetLocalValid() applies the shared
     * capability rules; instrumentParam_make()/instrumentParam_local() convert
     * between local descriptor indices and canonical Scene storage IDs.
     */
    if (target_slot >= INSTRUMENT_SLOT_COUNT || direction == 0)
        return current;

    slot = scene_instrumentSlotConst(scene_index, target_slot);
    if (!slot)
        return INSTRUMENT_PARAM_INVALID;
    entry = instrumentManager_registryEntry(slot->type);
    if (!entry || !entry->descriptors)
        return INSTRUMENT_PARAM_INVALID;

    if (instrumentParam_isVoiceParameter(current) &&
        instrumentParam_slot(current) == target_slot &&
        instrumentManager_targetValid(scene_index, current, use)) {
        current_valid = 1u;
        current_local = instrumentParam_local(current);
    }

    if (direction > 0) {
        uint8_t i = current_valid ? (uint8_t)(current_local + 1u) : 0u;
        while (i < entry->descriptor_count) {
            if (instrumentManager_targetLocalValid(scene_index, target_slot,
                                                   i, use)) {
                return instrumentParam_make(target_slot, i);
            }
            i++;
        }
        return current_valid ? current : INSTRUMENT_PARAM_INVALID;
    }

    if (!current_valid || current_local == 0u)
        return INSTRUMENT_PARAM_INVALID;
    {
        int16_t i = (int16_t)current_local - 1;
        while (i >= 0) {
            if (instrumentManager_targetLocalValid(scene_index, target_slot,
                                                   (uint8_t)i, use)) {
                return instrumentParam_make(target_slot, (uint8_t)i);
            }
            i--;
        }
    }
    return INSTRUMENT_PARAM_INVALID;
}

static uint16_t instrumentManager_lastTargetForSlot(uint8_t scene_index,
                                                    uint8_t target_slot,
                                                    instrument_target_use_t use)
{
    uint16_t current = INSTRUMENT_PARAM_INVALID;

    /*
     * Find the last valid descriptor target for one slot by using the public
     * descriptor stepper.
     *
     * Inputs: Scene index, target slot, and target use. Output: the final
     * valid canonical descriptor target or off when the slot has none. This
     * helper exists only for mixed-list boundary navigation: moving backward
     * from the first Scene velocity target should land on the last voice-local
     * descriptor target without duplicating descriptor scans here.
     */
    while (1) {
        uint16_t next = instrumentManager_stepTargetForSlot(
            scene_index, target_slot, current, 1, use);
        if (next == current)
            return current;
        current = next;
    }
}

uint8_t instrumentManager_targetValidForVelocitySource(
    uint8_t scene_index, uint8_t source_slot, uint16_t target_id)
{
    /*
     * Validate a mixed velocity target for one source voice.
     *
     * Inputs: Scene index, zero-based source slot, and stored target ID.
     * Output: nonzero for off, a modulatable descriptor on the same source
     * slot, or a Scene modulation target. This keeps velocity from browsing
     * every kit voice while still appending Scene targets after the voice-local
     * descriptor list.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (target_id == INSTRUMENT_PARAM_INVALID)
        return 1u;
    if (sceneModTarget_valid(target_id, SCENE_MOD_TARGET_USE_VELOCITY))
        return 1u;
    return (uint8_t)(instrumentParam_isVoiceParameter(target_id) &&
                     instrumentParam_slot(target_id) == source_slot &&
                     instrumentManager_targetValid(scene_index, target_id,
                                                   INSTRUMENT_TARGET_MODULATION));
}

uint16_t instrumentManager_stepVelocityTargetForSource(
    uint8_t scene_index, uint8_t source_slot, uint16_t current,
    int8_t direction)
{
    uint16_t normalized = current;

    /*
     * Walk the velocity target list for one source voice.
     *
     * Inputs: Scene index, source slot, current target or off, and signed
     * direction. Output: one off entry, then this source slot's current
     * instrument descriptor targets, then Scene mod targets. The descriptor
     * portion delegates to instrumentManager_stepTargetForSlot(), so
     * instrument swaps change the list without a hardcoded target table.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || direction == 0)
        return current;
    if (!instrumentManager_targetValidForVelocitySource(scene_index,
                                                        source_slot,
                                                        normalized)) {
        normalized = INSTRUMENT_PARAM_INVALID;
    }

    if (direction > 0) {
        if (normalized == INSTRUMENT_PARAM_INVALID ||
            instrumentParam_isVoiceParameter(normalized)) {
            uint16_t next = instrumentManager_stepTargetForSlot(
                scene_index, source_slot, normalized, 1,
                INSTRUMENT_TARGET_MODULATION);
            if (next != normalized)
                return next;
            return sceneModTarget_step(INSTRUMENT_PARAM_INVALID, 1,
                                       SCENE_MOD_TARGET_USE_VELOCITY);
        }
        if (sceneModTarget_valid(normalized, SCENE_MOD_TARGET_USE_VELOCITY))
            return sceneModTarget_step(normalized, 1,
                                       SCENE_MOD_TARGET_USE_VELOCITY);
        return INSTRUMENT_PARAM_INVALID;
    }

    if (sceneModTarget_valid(normalized, SCENE_MOD_TARGET_USE_VELOCITY)) {
        uint16_t next = sceneModTarget_step(normalized, -1,
                                           SCENE_MOD_TARGET_USE_VELOCITY);
        if (next != INSTRUMENT_PARAM_INVALID)
            return next;
        return instrumentManager_lastTargetForSlot(
            scene_index, source_slot, INSTRUMENT_TARGET_MODULATION);
    }
    if (instrumentParam_isVoiceParameter(normalized)) {
        return instrumentManager_stepTargetForSlot(
            scene_index, source_slot, normalized, -1,
            INSTRUMENT_TARGET_MODULATION);
    }
    return INSTRUMENT_PARAM_INVALID;
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

static uint8_t instrumentManager_descriptorIndexForPointer(
    uint8_t slot, const ParamDescriptor *descriptor, uint8_t *index_out)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(instrumentManager_slotType(slot));
    uint8_t i;

    /*
     * Recover a descriptor's local index for notification paths.
     *
     * Inputs: slot is the zero-based runtime slot whose value just changed;
     * descriptor is the registry row that was applied. Output: the descriptor's
     * local index when the pointer belongs to the slot's current instrument
     * type. This exists because instrumentManager_writeRuntime() receives a
     * descriptor pointer, while descriptor-backed modulation nodes identify
     * targets with canonical slot+local ids for original-value refresh.
     */
    if (index_out)
        *index_out = INSTRUMENT_MENU_EMPTY;
    if (!entry || !descriptor)
        return 0u;
    for (i = 0u; i < entry->descriptor_count; i++) {
        if (&entry->descriptors[i] == descriptor) {
            if (index_out)
                *index_out = i;
            return 1u;
        }
    }
    return 0u;
}

static void instrumentManager_noteRuntimeValueChanged(
    uint8_t slot, const ParamDescriptor *descriptor)
{
    uint8_t descriptor_index;
    instrument_param_id_t id;
    instrument_runtime_target_t target;

    /*
     * Refresh descriptor-backed modulation baselines/ranges after an ordinary write.
     *
     * Inputs: slot/descriptor identify a live runtime value just changed by a
     * menu edit, morph apply, automation write, or loaded-kit apply. Output:
     * active direct ModulationNode targets for the same canonical descriptor id
     * recapture their originalValue and cached min/max range. This is not a
     * SceneData write and not automation recording; it only keeps
     * modNode_resetTargets() restoring the latest base value and keeps LFO
     * amount scaled by the descriptor range instead of by a stale current
     * value. Morph does not need its own min/max pass because it already
     * applies values through this common runtime writer.
     */
    if (!instrumentManager_descriptorIndexForPointer(slot, descriptor,
                                                     &descriptor_index)) {
        return;
    }
    id = instrumentParam_make(slot, descriptor_index);
    if (id != INSTRUMENT_PARAM_INVALID &&
        instrumentManager_resolveModulationTarget(scene_getActiveIndex(), id,
                                                  &target)) {
        modNode_directOriginalValueChanged(id, target.range);
    }
}

static ModulationNode *instrumentManager_lfoModNodeForSlot(uint8_t slot,
                                                           uint8_t target_index)
{
    /*
     * Resolve one source LFO modulation node for one runtime slot and target pair.
     *
     * Inputs: slot is the source voice whose LFO is doing the modulation, and
     * target_index is 0 for pair 1 or 1 for pair 2. Output: the address of the
     * source LFO's destination node, or NULL for an invalid slot/pair. This
     * stays separate from target resolution because source and target slots can
     * differ; lfo_target_voice/lfo_target_param choose the destination, while
     * this helper chooses which node the source LFO dispatch updates.
     */
    if (target_index > 1u)
        return 0;
    switch (slot) {
    case 0u:
    case 1u:
    case 2u: return target_index ? &voiceArray[slot].lfo.modTarget2
                                  : &voiceArray[slot].lfo.modTarget;
    case 3u: return target_index ? &snareVoice.lfo.modTarget2
                                  : &snareVoice.lfo.modTarget;
    case 4u: return target_index ? &cymbalVoice.lfo.modTarget2
                                  : &cymbalVoice.lfo.modTarget;
    case 5u: return target_index ? &hatVoice.lfo.modTarget2
                                  : &hatVoice.lfo.modTarget;
    default: return 0;
    }
}

static void *instrumentManager_waveInterpTarget(
    uint8_t slot, const ParamDescriptor *descriptor)
{
    /*
     * Resolve optional oscillator waveform interpolation state for a target.
     *
     * Inputs: target slot and descriptor. Output: an OscInfo* carried as void*
     * only when the target is an oscillator waveform field; NULL otherwise.
     * ModulationNode owns the interpolation budget, but InstrumentManager owns
     * the instrument-specific osc1/osc2/osc3 mapping, so the affiliation lives
     * here instead of in a hardcoded modulation target table.
     */
    if (!descriptor ||
        descriptor->runtime.parameter_type != TYPE_UINT8 ||
        !strstr(descriptor->file_key, "_wave")) {
        return 0;
    }
    return instrumentManager_osc(slot, descriptor->file_key);
}

static uint8_t instrumentManager_buildModulationRange(
    const ParamDescriptor *descriptor,
    void *waveInterpTarget,
    mod_node_range_t *range_out)
{
    uint8_t dtype;

    /*
     * Build the stable min/max contract for one direct modulation target.
     *
     * Inputs: descriptor metadata and the already resolved optional waveform
     * interpolation affiliate. Output: range_out receives the min/max values
     * ModulationNode uses to scale amount by usable range instead of by the
     * target's current value. This is private to InstrumentManager because it
     * bridges descriptor dtype, scalar runtime type, and oscillator waveform
     * affiliation; putting this in Menu would point dependencies the wrong way,
     * and putting it in ModulationNode would make the DSP node learn registry
     * layout.
     */
    if (range_out) {
        range_out->min = 0.f;
        range_out->max = 0.f;
        range_out->valid = 0u;
    }
    if (!descriptor || !range_out ||
        !instrumentManager_descriptorSupportsModulationRange(descriptor)) {
        return 0u;
    }

    dtype = (uint8_t)(descriptor->dtype & 0x0fu);
    switch (descriptor->runtime.parameter_type) {
    case TYPE_FLT:
        range_out->min = 0.f;
        range_out->max = 1.f;
        range_out->valid = 1u;
        return 1u;

    case TYPE_SPECIAL_F:
        /*
         * TYPE_SPECIAL_F rows are runtime multiplier overlays. The neutral
         * base is captured by ModulationNode as 1.0f, while this 0..2 range
         * makes LFO amount stable regardless of the edited value that produced
         * the multiplier. Bipolar uses half this width around the captured
         * base; negative/positive clamp at the range edges.
         */
        range_out->min = 0.f;
        range_out->max = 2.f;
        range_out->valid = 1u;
        return 1u;

    case TYPE_UINT8:
        if (waveInterpTarget) {
            range_out->min = 0.f;
            range_out->max = (float)modNode_getMaxWaveformIndex();
            range_out->valid = 1u;
            return 1u;
        }
        if (dtype == DTYPE_MENU)
            return 0u;
        range_out->min = (dtype == DTYPE_1B16 || dtype == DTYPE_1B128) ? 1.f : 0.f;
        switch (dtype) {
        case DTYPE_MIX_FM:
        case DTYPE_ON_OFF:
        case DTYPE_0b1:
            range_out->max = 1.f;
            break;
        case DTYPE_0B15:
            range_out->max = 15.f;
            break;
        case DTYPE_1B16:
            range_out->max = 16.f;
            break;
        case DTYPE_1B128:
            range_out->max = 128.f;
            break;
        default:
            range_out->max = 127.f;
            break;
        }
        range_out->valid = 1u;
        return 1u;

    default:
        return 0u;
    }
}

static uint8_t instrumentManager_resolveModulationTarget(
    uint8_t scene_index,
    instrument_param_id_t id,
    instrument_runtime_target_t *target_out)
{
    const kit_instrument_slot_t *slot;
    const ParamDescriptor *descriptor;
    void *instance;
    uint8_t target_slot;
    uint8_t local;

    /*
     * Resolve a canonical descriptor target into a live runtime pointer.
     *
     * Inputs: scene_index is explicit to match the target validators, id is the
     * target slot+descriptor id stored by SceneData, and target_out receives the
     * resolved runtime target. Output: nonzero only for the active Scene when
     * the target slot's current instrument exposes a morphable, modulatable,
     * direct-offset descriptor. This cannot live in Menu because Menu should not
     * know DSP addresses, and it cannot live in ModulationNode because
     * ModulationNode should receive an already resolved Parameter rather than
     * learning SceneData and instrument registries.
     */
    if (target_out)
        memset(target_out, 0, sizeof(*target_out));
    if (!target_out || scene_index != scene_getActiveIndex())
        return 0u;
    if (!instrumentManager_targetValid(scene_index, id,
                                       INSTRUMENT_TARGET_MODULATION)) {
        return 0u;
    }

    target_slot = instrumentParam_slot(id);
    local = instrumentParam_local(id);
    slot = scene_instrumentSlotConst(scene_index, target_slot);
    if (!slot)
        return 0u;
    descriptor = instrumentManager_descriptor(slot->type, local);
    instance = instrumentManager_runtimeInstance(target_slot);
    if (!descriptor || !instance ||
        descriptor->runtime.kind != INSTRUMENT_BIND_INSTANCE_OFFSET) {
        return 0u;
    }

    target_out->id = id;
    target_out->slot = target_slot;
    target_out->descriptor_index = local;
    target_out->descriptor = descriptor;
    target_out->parameter.ptr =
        (void *)((uint8_t *)instance + descriptor->runtime.offset);
    target_out->parameter.type = descriptor->runtime.parameter_type;
    target_out->waveInterpTarget =
        instrumentManager_waveInterpTarget(target_slot, descriptor);
    if (!instrumentManager_buildModulationRange(descriptor,
                                                target_out->waveInterpTarget,
                                                &target_out->range)) {
        return 0u;
    }
    return (uint8_t)(target_out->parameter.ptr != 0);
}

static uint8_t instrumentManager_isSlotDecimationTarget(
    uint8_t scene_index, uint16_t id)
{
    const kit_instrument_slot_t *slot;
    const ParamDescriptor *descriptor;

    /*
     * Validate a canonical descriptor ID as voice-local slot decimation.
     *
     * Inputs: Scene index and stored target ID. Output: nonzero only when the
     * selected slot's current instrument owns an instrument_decimation
     * descriptor that is valid for modulation. This keeps per-instrument `srt`
     * in descriptor space and avoids putting it in SceneModTargets.
     */
    if (!instrumentParam_isVoiceParameter(id) ||
        !instrumentManager_targetValid(scene_index, id,
                                       INSTRUMENT_TARGET_MODULATION)) {
        return 0u;
    }
    slot = scene_instrumentSlotConst(scene_index, instrumentParam_slot(id));
    if (!slot)
        return 0u;
    descriptor = instrumentManager_descriptor(slot->type,
                                              instrumentParam_local(id));
    return instrumentManager_descriptorIsSlotDecimationTarget(descriptor);
}

static uint8_t instrumentManager_applySlotDecimationTarget(uint16_t id,
                                                           uint16_t value)
{
    const kit_instrument_slot_t *slot_state;
    const ParamDescriptor *descriptor;
    uint8_t slot;

    /*
     * Apply one voice-local slot-decimation modulation value.
     *
     * Inputs: canonical descriptor target ID and 0..127 value. Output:
     * InstrumentManager's existing supplemental runtime writer updates
     * mixer_decimation_rate[target_slot]. This helper keeps velocity and LFO
     * supplemental modulation paths from duplicating descriptor decode and
     * binding rules.
     */
    if (!instrumentParam_isVoiceParameter(id))
        return 0u;
    slot = instrumentParam_slot(id);
    slot_state = scene_instrumentSlotConst(scene_getActiveIndex(), slot);
    if (!slot_state)
        return 0u;
    descriptor = instrumentManager_descriptor(slot_state->type,
                                              instrumentParam_local(id));
    if (!instrumentManager_descriptorIsSlotDecimationTarget(descriptor))
        return 0u;
    if (value > 127u)
        value = 127u;
    return instrumentManager_writeRuntime(slot, descriptor, value);
}

static uint8_t instrumentManager_slotDecimationBase(uint16_t id)
{
    const scene_t *scene;
    uint8_t slot;
    uint8_t local;

    /*
     * Read the current base value for LFO slot-decimation modulation.
     *
     * Inputs: canonical instrument_decimation target ID. Output: the active
     * Scene's current Morph interpolation image for that descriptor, clamped to
     * 0..127. LFO modulation should be centered on the current voice-local base
     * value, not on a hardcoded full-rate value.
     */
    if (!instrumentParam_isVoiceParameter(id))
        return 127u;
    scene = scene_getConst(scene_getActiveIndex());
    slot = instrumentParam_slot(id);
    local = instrumentParam_local(id);
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        local >= INSTRUMENT_PARAM_COUNT) {
        return 127u;
    }
    {
        uint16_t value =
            scene->kit.instruments[slot].parameter_images.morph_interpolation[local];
        return (uint8_t)((value > 127u) ? 127u : value);
    }
}

static uint8_t instrumentManager_applyVelocitySceneTarget(
    uint16_t target_id, float velocity_0_1, float amount)
{
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(target_id);
    uint16_t value;

    /*
     * Apply one velocity-triggered Scene modulation target.
     *
     * Inputs: Scene target ID, normalized trigger velocity, and normalized
     * velocity amount. Output: retained Scene settings are changed through
     * their owners. Per-voice Morph uses preset_morphVoice(), so the PERF
     * value updates exactly like a menu edit; Scene Decimation uses its
     * retained setter for the same reason.
     */
    if (!descriptor)
        return 0u;
    if (velocity_0_1 < 0.f)
        velocity_0_1 = 0.f;
    else if (velocity_0_1 > 1.f)
        velocity_0_1 = 1.f;
    if (amount < 0.f)
        amount = 0.f;
    else if (amount > 1.f)
        amount = 1.f;
    value = (uint16_t)((float)descriptor->max_value * velocity_0_1 * amount +
                       0.5f);
    switch (descriptor->kind) {
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
        preset_morphVoice(descriptor->voice_slot, (uint8_t)value);
        return 1u;
    case SCENE_MOD_TARGET_KIND_DECIMATION_ALL:
        preset_setVoiceDecimationAll(scene_getActiveIndex(), (uint8_t)value);
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t instrumentManager_updateLfoSceneDestination(
    uint16_t target_id, uint8_t source_slot, uint8_t target_pair,
    float lfo_value_0_1, uint8_t polarity, float amount)
{
    const scene_t *scene = scene_getConst(scene_getActiveIndex());
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(target_id);
    uint16_t base;
    uint16_t shaped;

    /*
     * Apply one LFO sample to a Scene modulation target.
     *
     * Inputs: Scene target ID, source LFO identity, normalized LFO value,
     * polarity, and normalized amount. Output: voice Morph targets write the
     * hidden Morph-worker layer, while Scene Decimation uses a runtime-only
     * mixer apply so the retained PERF value does not move every LFO block.
     */
    if (!descriptor || !scene)
        return 0u;
    switch (descriptor->kind) {
    case SCENE_MOD_TARGET_KIND_VOICE_MORPH:
        base = scene->settings.voice_morph_amount[descriptor->voice_slot];
        shaped = modNode_shapeRangeU16(base, descriptor->min_value,
                                       descriptor->max_value,
                                       lfo_value_0_1, amount, polarity);
        presetMorph_setVoiceLfoModulation(scene_getActiveIndex(),
                                          descriptor->voice_slot,
                                          source_slot, target_pair,
                                          1u, (uint8_t)shaped);
        return 1u;
    case SCENE_MOD_TARGET_KIND_DECIMATION_ALL:
        base = scene->settings.voice_decimation_all;
        shaped = modNode_shapeRangeU16(base, descriptor->min_value,
                                       descriptor->max_value,
                                       lfo_value_0_1, amount, polarity);
        preset_applyVoiceDecimationAllRuntime((uint8_t)shaped);
        return 1u;
    default:
        return 0u;
    }
}

static void instrumentManager_restoreLfoSupplementalTarget(uint8_t source_slot,
                                                           uint8_t target_pair)
{
    installed_mod_target_t *installed;
    const scene_t *scene;

    /*
     * Restore the base value for one installed supplemental LFO target.
     *
     * Inputs: source slot and LFO pair whose target is about to be cleared or
     * replaced. Output: slot decimation and Scene Decimation return to their
     * current retained/runtime base, and hidden Morph contributions from that
     * source/pair are cleared. Direct ModulationNode targets already restore
     * themselves through modNode_clearDestination(), so this helper handles
     * only the adapter backends owned by InstrumentManager.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || target_pair > 1u)
        return;
    installed = &lfo_installed_targets[source_slot][target_pair];
    scene = scene_getConst(scene_getActiveIndex());
    switch (installed->kind) {
    case INSTALLED_MOD_TARGET_SLOT_DECIMATION:
        (void)instrumentManager_applySlotDecimationTarget(
            installed->target_id,
            instrumentManager_slotDecimationBase(installed->target_id));
        break;
    case INSTALLED_MOD_TARGET_SCENE_TARGET: {
        const scene_mod_target_descriptor_t *descriptor =
            sceneModTarget_descriptor(installed->target_id);
        if (descriptor &&
            descriptor->kind == SCENE_MOD_TARGET_KIND_DECIMATION_ALL &&
            scene) {
            preset_applyVoiceDecimationAllRuntime(
                scene->settings.voice_decimation_all);
        }
        break; }
    default:
        break;
    }
    presetMorph_clearLfoSource(source_slot, target_pair);
    installed->kind = INSTALLED_MOD_TARGET_NONE;
    installed->target_id = INSTRUMENT_PARAM_INVALID;
}

static void instrumentManager_restoreVelocitySupplementalTarget(uint8_t source_slot)
{
    installed_mod_target_t *installed;

    /*
     * Restore one velocity supplemental target before target replacement.
     *
     * Inputs: source voice slot whose velocity destination is changing. Output:
     * voice-local slot decimation returns to the current Morph/base image.
     * Scene velocity targets intentionally do not restore because their
     * behavior is a retained set operation, exactly like editing PERF values.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT)
        return;
    installed = &velocity_installed_targets[source_slot];
    if (installed->kind == INSTALLED_MOD_TARGET_SLOT_DECIMATION) {
        (void)instrumentManager_applySlotDecimationTarget(
            installed->target_id,
            instrumentManager_slotDecimationBase(installed->target_id));
    }
    installed->kind = INSTALLED_MOD_TARGET_NONE;
    installed->target_id = INSTRUMENT_PARAM_INVALID;
}

static uint8_t instrumentManager_installLfoModulationTarget(
    uint8_t source_slot, uint8_t target_index, instrument_param_id_t target_id)
{
    ModulationNode *node =
        instrumentManager_lfoModNodeForSlot(source_slot, target_index);
    instrument_runtime_target_t target;

    /*
     * Install or clear one LFO descriptor modulation destination.
     *
     * Inputs: source_slot selects the LFO that emits modulation values,
     * target_index selects destination pair 1 or 2 on that LFO, and target_id
     * is either INSTRUMENT_PARAM_INVALID for off or a canonical descriptor id
     * whose slot may differ from the source slot. Output: nonzero when the
     * source exists and the target is cleared or installed. This helper keeps
     * source-node lookup, descriptor target/range resolution, and direct
     * ModulationNode installation together instead of spreading the six
     * voice-object cases through instrumentManager_writeRuntime().
     */
    if (!node)
        return 0u;
    instrumentManager_restoreLfoSupplementalTarget(source_slot, target_index);
    if (target_id == INSTRUMENT_PARAM_INVALID) {
        modNode_clearDestination(node);
        return 1u;
    }
    if (instrumentManager_isSlotDecimationTarget(scene_getActiveIndex(),
                                                 target_id)) {
        modNode_clearDestination(node);
        lfo_installed_targets[source_slot][target_index].kind =
            INSTALLED_MOD_TARGET_SLOT_DECIMATION;
        lfo_installed_targets[source_slot][target_index].target_id = target_id;
        return 1u;
    }
    if (sceneModTarget_valid(target_id, SCENE_MOD_TARGET_USE_LFO)) {
        modNode_clearDestination(node);
        lfo_installed_targets[source_slot][target_index].kind =
            INSTALLED_MOD_TARGET_SCENE_TARGET;
        lfo_installed_targets[source_slot][target_index].target_id = target_id;
        return 1u;
    }
    if (!instrumentManager_resolveModulationTarget(scene_getActiveIndex(),
                                                   target_id, &target)) {
        modNode_clearDestination(node);
        return 0u;
    }
    return modNode_setDirectDestination(node, target.id, target.parameter,
                                        target.waveInterpTarget, target.range);
}

static uint8_t instrumentManager_installVelocityModulationTarget(
    uint8_t source_slot, instrument_param_id_t target_id)
{
    instrument_runtime_target_t target;

    /*
     * Install or clear one velocity descriptor modulation destination.
     *
     * Inputs: source_slot selects velocityModulators[source_slot], while
     * target_id is off or a canonical descriptor id. Output: nonzero when the
     * source slot is valid and the target is cleared or installed. Velocity
     * targets had the same descriptor-id/ParameterArray mismatch as LFO
     * targets, so they share the resolver and direct ModulationNode backend
     * rather than keeping a parallel legacy path.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    instrumentManager_restoreVelocitySupplementalTarget(source_slot);
    if (target_id == INSTRUMENT_PARAM_INVALID) {
        modNode_clearDestination(&velocityModulators[source_slot]);
        return 1u;
    }
    if (instrumentManager_isSlotDecimationTarget(scene_getActiveIndex(),
                                                 target_id)) {
        modNode_clearDestination(&velocityModulators[source_slot]);
        velocity_installed_targets[source_slot].kind =
            INSTALLED_MOD_TARGET_SLOT_DECIMATION;
        velocity_installed_targets[source_slot].target_id = target_id;
        return 1u;
    }
    if (sceneModTarget_valid(target_id, SCENE_MOD_TARGET_USE_VELOCITY)) {
        modNode_clearDestination(&velocityModulators[source_slot]);
        velocity_installed_targets[source_slot].kind =
            INSTALLED_MOD_TARGET_SCENE_TARGET;
        velocity_installed_targets[source_slot].target_id = target_id;
        return 1u;
    }
    if (!instrumentManager_resolveModulationTarget(scene_getActiveIndex(),
                                                   target_id, &target)) {
        modNode_clearDestination(&velocityModulators[source_slot]);
        return 0u;
    }
    return modNode_setDirectDestination(&velocityModulators[source_slot],
                                        target.id, target.parameter,
                                        target.waveInterpTarget, target.range);
}

void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot,
                                                     float velocity_0_1)
{
    const installed_mod_target_t *installed;
    float amount;
    uint16_t value;

    /*
     * Apply a velocity-triggered supplemental or Scene modulation target.
     *
     * Inputs: source slot and normalized trigger velocity. Output: no-op for
     * direct descriptor targets, because modNode_updateValue() has already
     * handled them. Slot decimation writes the voice-local supplemental runtime
     * binding; Scene targets call retained Scene setters where required.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT)
        return;
    installed = &velocity_installed_targets[source_slot];
    amount = velocityModulators[source_slot].amount;
    switch (installed->kind) {
    case INSTALLED_MOD_TARGET_SLOT_DECIMATION:
        if (velocity_0_1 < 0.f)
            velocity_0_1 = 0.f;
        else if (velocity_0_1 > 1.f)
            velocity_0_1 = 1.f;
        value = (uint16_t)(127.f * velocity_0_1 * amount + 0.5f);
        (void)instrumentManager_applySlotDecimationTarget(installed->target_id,
                                                          value);
        break;
    case INSTALLED_MOD_TARGET_SCENE_TARGET:
        (void)instrumentManager_applyVelocitySceneTarget(installed->target_id,
                                                         velocity_0_1, amount);
        break;
    default:
        break;
    }
}

void instrumentManager_updateLfoSceneTarget(uint8_t source_slot,
                                            uint8_t target_pair,
                                            float lfo_value_0_1,
                                            uint8_t polarity,
                                            float amount)
{
    const installed_mod_target_t *installed;
    uint16_t shaped;
    uint8_t base;

    /*
     * Apply one LFO sample to an installed supplemental or Scene target.
     *
     * Inputs: source slot, target pair, normalized LFO value, shared polarity,
     * and normalized pair amount. Output: direct ModulationNode targets remain
     * handled in lfo_dispatchNextValue(); this function handles only
     * slot-decimation and Scene adapters.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || target_pair > 1u)
        return;
    installed = &lfo_installed_targets[source_slot][target_pair];
    switch (installed->kind) {
    case INSTALLED_MOD_TARGET_SLOT_DECIMATION:
        base = instrumentManager_slotDecimationBase(installed->target_id);
        shaped = modNode_shapeRangeU16(base, 0u, 127u, lfo_value_0_1,
                                       amount, polarity);
        (void)instrumentManager_applySlotDecimationTarget(installed->target_id,
                                                          shaped);
        break;
    case INSTALLED_MOD_TARGET_SCENE_TARGET:
        (void)instrumentManager_updateLfoSceneDestination(
            installed->target_id, source_slot, target_pair,
            lfo_value_0_1, polarity, amount);
        break;
    default:
        break;
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

    if (instrumentManager_writeSpecialRuntime(slot, descriptor, value)) {
        instrumentManager_noteRuntimeValueChanged(slot, descriptor);
        return 1u;
    }

    switch (descriptor->runtime.kind) {
    case INSTRUMENT_BIND_INSTANCE_OFFSET:
        instance = instrumentManager_runtimeInstance(slot);
        if (!instance)
            return 0u;
        parameter.ptr = (void *)((uint8_t *)instance + descriptor->runtime.offset);
        parameter.type = descriptor->runtime.parameter_type;
        instrumentManager_writeParameter(parameter, value);
        instrumentManager_noteRuntimeValueChanged(slot, descriptor);
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
        /*
         * Descriptor velocity targets use the same direct ModulationNode
         * backend as LFO targets. The stored value is a canonical descriptor id
         * or INSTRUMENT_PARAM_INVALID, never a legacy parameterArray[] index.
         */
        return instrumentManager_installVelocityModulationTarget(slot, value);

    case INSTRUMENT_BIND_LFO_TARGET_VOICE:
    case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
        /*
         * The selected target voice is stored in its descriptor cell and paired
         * with the matching lfo_target_param binding when that later binding is
         * applied. There is no standalone DSP write for this value. Pair 1 and
         * pair 2 share this validation but keep separate binding identities so
         * Menu/storage can find the correct sibling descriptor cells.
         */
        return (uint8_t)(value >= 1u &&
                         value <= (uint16_t)(INSTRUMENT_SLOT_COUNT + 1u));

    case INSTRUMENT_BIND_LFO_TARGET_PARAM:
    case INSTRUMENT_BIND_LFO_TARGET_PARAM_2:
        /*
         * lfo_target_param is the cell that fully identifies one DSP
         * destination. lfo_target_voice is paired UI/storage context; by the
         * time this value is written, Menu or file normalization should have
         * packed the selected target voice into the canonical descriptor id.
         * Installing goes through the descriptor-aware backend so the id never
         * masquerades as a legacy parameterArray[] destination. Pair 2 routes
         * to Lfo::modTarget2 while pair 1 keeps using Lfo::modTarget.
         */
        return instrumentManager_installLfoModulationTarget(
            slot,
            (descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_PARAM_2)
                ? 1u : 0u,
            value);

    default:
        return 0u;
    }
}
