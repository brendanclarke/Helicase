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
 * slot and how the value reaches that instrument's runtime instance. The
 * extension and storage_directory fields are intentionally adjacent in every
 * row: filesystem.c can classify a filename and navigate to its `.hcindex`
 * folder through one registry record, with no parallel type-folder table.
 */
static const instrument_registry_entry_t instrument_registry[] = {
    { INSTRUMENT_TYPE_DRM, "drm", drum_instrument_display_label, ".drm", "Drum",
      DRUM_INSTRUMENT_TYPE_FLAGS,
      drum_param_descriptors, DRUM_PARAM_DESCRIPTOR_COUNT,
      drum_menu_pages, DRUM_MENU_PAGE_COUNT },
    { INSTRUMENT_TYPE_SNR, "snr", snare_instrument_display_label, ".snr", "Snare",
      SNARE_INSTRUMENT_TYPE_FLAGS,
      snare_param_descriptors, SNARE_PARAM_DESCRIPTOR_COUNT,
      snare_menu_pages, SNARE_MENU_PAGE_COUNT },
    { INSTRUMENT_TYPE_CYM, "cym", cymbal_instrument_display_label, ".cym", "Cymbal",
      CYMBAL_INSTRUMENT_TYPE_FLAGS,
      cymbal_param_descriptors, CYMBAL_PARAM_DESCRIPTOR_COUNT,
      cymbal_menu_pages, CYMBAL_MENU_PAGE_COUNT },
    { INSTRUMENT_TYPE_HAT, "hat", hihat_instrument_display_label, ".hat", "HiHat",
      HIHAT_INSTRUMENT_TYPE_FLAGS,
      hihat_param_descriptors, HIHAT_PARAM_DESCRIPTOR_COUNT,
      hihat_menu_pages, HIHAT_MENU_PAGE_COUNT },
};

#define INSTRUMENT_REGISTRY_COUNT \
    ((uint8_t)(sizeof(instrument_registry) / sizeof(instrument_registry[0])))

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
    instrument_mod_domain_t domain;
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

typedef struct {
    uint8_t active;
    uint8_t slot;
    uint8_t descriptor_index;
    instrument_param_id_t id;
    const ParamDescriptor *descriptor;
    instrument_mod_domain_t domain;
    instrument_param_value_t base_value;
} instrument_lfo_target_adapter_t;

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
/*
 * Descriptor LFO adapters.
 *
 * Inputs: one source slot/pair selected by lfo_target_voice/_param. Output:
 * every LFO block applies a temporary descriptor-domain value through
 * instrumentManager_writeRuntime() rather than writing the resolved runtime
 * pointer directly. The adapter keeps the target id, descriptor, legal
 * parameter-domain range, and current Scene/Morph base so amount zero and
 * target clear restore exactly match ordinary descriptor writes.
 */
static instrument_lfo_target_adapter_t
    lfo_descriptor_targets[INSTRUMENT_SLOT_COUNT][2u];
static uint8_t slot6_track7_decay_lfo_active;
static uint8_t slot6_track7_decay_lfo_value;
/*
 * Runtime type shadow for deferred Scene switching.
 *
 * Inputs: active Scene changes immediately, while Preset may wait to commit
 * each instrument slot until the old amp envelope is quiet or until the new
 * Scene's pattern triggers that slot. Output: InstrumentManager runtime
 * dispatch follows this shadow instead of SceneData directly, so a ringing slot
 * keeps rendering with the instrument type whose parameters are actually loaded.
 *
 * Access: instrumentManager_resetRuntimeSlot() is the only writer; it copies
 * the currently active Scene's type into the shadow at the same moment the DSP
 * runtime is reinitialized and about to receive descriptor values.
 */
static instrument_type_t runtime_slot_type[INSTRUMENT_SLOT_COUNT];
#define INSTRUMENT_AMP_EG_QUIET_THRESHOLD 0.0001f

/*
 * Per-slot runtime pools for loadable instrument types.
 *
 * Inputs: SceneData says which instrument type currently lives in each of the
 * six storage slots. Output: InstrumentManager maps that slot/type pair to a
 * concrete DSP object from these pools, while preserving the original globals
 * for their native slots so legacy fixed-slot callers keep working. These
 * pools cannot live in the voice engines because each engine owns only its own
 * state layout; the cross-type slot policy belongs at the registry boundary.
 */
static DrumVoice runtime_drum_extra[INSTRUMENT_SLOT_COUNT - NUM_VOICES];
static SnareVoice runtime_snare_slots[INSTRUMENT_SLOT_COUNT];
static CymbalVoice runtime_cymbal_slots[INSTRUMENT_SLOT_COUNT];
static HiHatVoice runtime_hihat_slots[INSTRUMENT_SLOT_COUNT];

static uint8_t instrumentManager_resolveModulationTarget(
    uint8_t scene_index,
    instrument_param_id_t id,
    instrument_runtime_target_t *target_out);
static instrument_type_t instrumentManager_slotType(uint8_t slot);
static SlopeEg2 *instrumentManager_ampEg(uint8_t slot);
static void instrumentManager_restoreLfoSupplementalTarget(uint8_t source_slot,
                                                           uint8_t target_pair);
static void instrumentManager_restoreVelocitySupplementalTarget(
    uint8_t source_slot);
static uint8_t instrumentManager_writeRuntimeInternal(
    uint8_t slot, const ParamDescriptor *descriptor,
    instrument_param_value_t value,
    uint8_t notify_base_change);

static DrumVoice *instrumentManager_drumRuntime(uint8_t slot)
{
    if (slot < NUM_VOICES)
        return &voiceArray[slot];
    if (slot < INSTRUMENT_SLOT_COUNT)
        return &runtime_drum_extra[slot - NUM_VOICES];
    return 0;
}

static SnareVoice *instrumentManager_snareRuntime(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return (slot == 3u) ? &snareVoice : &runtime_snare_slots[slot];
}

static CymbalVoice *instrumentManager_cymbalRuntime(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return (slot == 4u) ? &cymbalVoice : &runtime_cymbal_slots[slot];
}

static HiHatVoice *instrumentManager_hihatRuntime(uint8_t slot)
{
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return (slot == 5u) ? &hatVoice : &runtime_hihat_slots[slot];
}

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
    for (i = 0u; i < INSTRUMENT_REGISTRY_COUNT; i++) {
        if (instrument_registry[i].type == type)
            return &instrument_registry[i];
    }
    return 0;
}

uint8_t instrumentManager_registryCount(void)
{
    /*
     * Public count for the private instrument registry.
     *
     * Inputs: none. Output: number of immutable registry rows. This is a thin
     * accessor, but it is the narrow boundary that lets Menu/filesystem
     * enumerate supported types without duplicating the static registry array
     * or depending on enum contiguity.
     */
    return INSTRUMENT_REGISTRY_COUNT;
}

const instrument_registry_entry_t *instrumentManager_registryEntryAt(
    uint8_t index)
{
    /*
     * Indexed registry borrow.
     *
     * Input: zero-based registry row index. Output: const row pointer or NULL
     * for out of range. Clients are Instrument Load type browsing and root
     * Instrument scanning; affiliates are the static registry and the
     * instrument-definition files that provide labels/flags.
     */
    if (index >= INSTRUMENT_REGISTRY_COUNT)
        return 0;
    return &instrument_registry[index];
}

instrument_type_t instrumentManager_typeFromText(const char *text)
{
    uint8_t i;
    if (!text)
        return INSTRUMENT_TYPE_UNKNOWN;
    for (i = 0u; i < INSTRUMENT_REGISTRY_COUNT; i++) {
        if (strcmp(text, instrument_registry[i].type_text) == 0)
            return instrument_registry[i].type;
    }
    return INSTRUMENT_TYPE_UNKNOWN;
}

const char *instrumentManager_typeDisplayLabel(instrument_type_t type)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    /*
     * Human-facing type label.
     *
     * Input: instrument type. Output: the short Load-menu label supplied by
     * that instrument's parameter file, or blank padding for unknown types.
     * This keeps display text out of Menu and separate from file tokens such
     * as "drm" and "hat".
     */
    return entry && entry->display_label ? entry->display_label : "        ";
}

uint8_t instrumentManager_typeFlags(instrument_type_t type)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    /*
     * Type-level assignment flags.
     *
     * Input: instrument type. Output: Basic/Advanced/Choke bitmask, or zero
     * for unknown. Callers should use this single bitmask instead of adding
     * thin isBasic/isAdvanced wrappers that would duplicate policy checks.
     */
    return entry ? entry->type_flags : 0u;
}

uint8_t instrumentManager_advancedCountForScene(uint8_t scene_index,
                                                uint8_t ignore_slot)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t slot;
    uint8_t count = 0u;

    /*
     * Count assigned Advanced instruments in one Scene kit.
     *
     * Inputs: Scene index and an optional slot to ignore while testing a
     * replacement. Output: number of current slots whose type carries
     * INSTRUMENT_FLAG_ADVANCED. The ignore slot lets Instrument Load ask "what
     * if this destination changes?" without mutating SceneData first.
     */
    if (!scene)
        return 0u;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (slot == ignore_slot)
            continue;
        if (instrumentManager_typeFlags(scene->kit.instruments[slot].type) &
            INSTRUMENT_FLAG_ADVANCED) {
            count++;
        }
    }
    return count;
}

uint8_t instrumentManager_typeSelectableForSceneSlot(
    uint8_t scene_index, uint8_t destination_slot, instrument_type_t candidate)
{
    uint8_t flags = instrumentManager_typeFlags(candidate);
    uint8_t advanced_count;

    /*
     * Apply Instrument Load assignment policy for one candidate type.
     *
     * Inputs: Scene index, destination slot, candidate type. Output: nonzero
     * when the type is known and can be selected under the Basic/Advanced
     * policy. Basic and Advanced are mutually exclusive by registry contract;
     * an Advanced candidate is allowed only when the other kit slots contain
     * fewer than two Advanced instruments.
     */
    if (destination_slot >= INSTRUMENT_SLOT_COUNT || flags == 0u)
        return 0u;
    if ((flags & INSTRUMENT_FLAG_BASIC) &&
        (flags & INSTRUMENT_FLAG_ADVANCED))
        return 0u;
    if (!(flags & INSTRUMENT_FLAG_ADVANCED))
        return 1u;
    advanced_count =
        instrumentManager_advancedCountForScene(scene_index, destination_slot);
    return (uint8_t)(advanced_count < 2u);
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

const char *instrumentManager_storageDirectory(instrument_type_t type)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);

    /*
     * Borrow the immutable folder component from the same registry row that
     * owns the type extension. Returning NULL for an unknown type lets every
     * asynchronous filesystem caller fail before attempting to open a malformed
     * path, while valid rows share one source of truth for storage navigation.
     */
    return entry ? entry->storage_directory : NULL;
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
    /*
     * Voice-page aware variant of the menu lookup.
     *
     * This now delegates to the normal instrument-owned page lookup. The
     * previous implementation switched to a private hihat_open_menu_pages[]
     * table for VOICE7. Choke behavior is no longer a HiHat-only layout fork:
     * Menu resolves the base descriptor first and then asks
     * instrumentManager_chokeDescriptorIndexForBase() for a `_choke` sibling
     * when the visible page is VOICE7 and the owning slot is slot 6.
     */
    (void)voice_page;
    return instrumentManager_menuDescriptorIndex(type, page, position,
                                                 index_out);
}

uint8_t instrumentManager_chokeDescriptorIndexForBase(
    instrument_type_t type, uint8_t base_index, uint8_t *choke_index_out)
{
    const ParamDescriptor *base = instrumentManager_descriptor(type, base_index);
    char choke_key[40];
    uint8_t len = 0u;

    /*
     * Resolve a descriptor's `_choke` sibling inside one instrument type.
     *
     * Inputs: instrument type and the base descriptor index already chosen by
     * a normal menu page. Output: nonzero plus sibling descriptor index when
     * the same type exposes `<base_key>_choke`. This function owns the suffix
     * convention so Menu does not parse descriptor names and InstrumentManager
     * does not need to know VOICE-page UI context.
     *
     * Clients: VOICE7 menu substitution now; future storage/save validation
     * can reuse the same key relationship. Affiliates are the descriptor
     * registry and instrument files that store the canonical descriptor keys.
     */
    if (choke_index_out)
        *choke_index_out = INSTRUMENT_MENU_EMPTY;
    if (!base || !base->file_key)
        return 0u;
    while (base->file_key[len] != '\0' &&
           len < (uint8_t)(sizeof(choke_key) - sizeof("_choke"))) {
        choke_key[len] = base->file_key[len];
        len++;
    }
    if (base->file_key[len] != '\0')
        return 0u;
    memcpy(&choke_key[len], "_choke", sizeof("_choke"));
    return (uint8_t)(instrumentManager_descriptorIndexByKey(
        type, choke_key, choke_index_out) != 0);
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
                INSTRUMENT_TARGET_TOKEN_OFF;
            slot->parameter_images.morph_instrument_parameters[i] =
                INSTRUMENT_TARGET_TOKEN_OFF;
            slot->parameter_images.morph_interpolation[i] =
                INSTRUMENT_TARGET_TOKEN_OFF;
            break;
        default:
            break;
        }
    }
}

static uint8_t instrumentManager_descriptorModDomain(
    const ParamDescriptor *descriptor,
    instrument_mod_domain_t *domain_out)
{
    instrument_mod_domain_t domain;

    /*
     * Expand a descriptor-owned modulation domain into concrete bounds.
     *
     * Inputs: one immutable descriptor row. Output: domain_out receives a
     * legal descriptor-space range for transient modulation, or the function
     * returns zero when the row is not a continuous/integer modulation target.
     * This replaces the old dtype/runtime-type guess: a TYPE_UINT8 row can be
     * an envelope byte, an oscillator waveform id, or a selector such as sync
     * rate, and only the instrument descriptor owns that musical distinction.
     */
    if (domain_out)
        memset(domain_out, 0, sizeof(*domain_out));
    if (!descriptor)
        return 0u;
    domain = descriptor->mod_domain;
    if (domain.flags == INSTRUMENT_MOD_DOMAIN_NONE)
        return 0u;
    if (domain.flags & INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) {
        domain.max_value = modNode_getMaxWaveformIndex();
    }
    if (domain.max_value < domain.min_value)
        return 0u;
    if (domain_out)
        *domain_out = domain;
    return 1u;
}

static uint8_t instrumentManager_descriptorSupportsModulationRange(
    const ParamDescriptor *descriptor)
{
    /*
     * Descriptor-declared modulation target eligibility.
     *
     * Input: descriptor metadata. Output: nonzero only when the instrument file
     * declared a modulation domain. Target browsing and DSP installation share
     * this helper so enum selectors do not leak back into the LFO target list
     * just because their storage happens to be a byte.
     */
    return instrumentManager_descriptorModDomain(descriptor, 0);
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

uint8_t instrumentManager_targetTokenValidForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use)
{
    /*
     * Validate a retained descriptor-local target token.
     *
     * Inputs: Scene index, zero-based target slot, local byte token, and target
     * use. Output: nonzero for off or a local descriptor that is valid in that
     * slot/use namespace. Scene target and velocity own-Morph tokens are
     * handled by their namespace-specific helpers so plain voice target
     * browsing cannot accidentally expose Scene routes.
     */
    if (token == INSTRUMENT_TARGET_TOKEN_OFF)
        return 1u;
    if (token > INSTRUMENT_TARGET_TOKEN_MAX_LOCAL)
        return 0u;
    return instrumentManager_targetLocalValid(scene_index, target_slot, token,
                                              use);
}

instrument_param_id_t instrumentManager_targetIdFromTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use)
{
    /*
     * Expand one retained descriptor token to a canonical runtime ID.
     *
     * Inputs: target slot namespace plus byte token. Output: a slot/local
     * instrument_param_id_t when the token is a valid descriptor target, or
     * INSTRUMENT_PARAM_INVALID for off/stale values. Callers use this for
     * display and runtime installation only; the wide ID is not written back to
     * SceneData.
     */
    if (token == INSTRUMENT_TARGET_TOKEN_OFF)
        return INSTRUMENT_PARAM_INVALID;
    if (!instrumentManager_targetTokenValidForSlot(scene_index, target_slot,
                                                   token, use)) {
        return INSTRUMENT_PARAM_INVALID;
    }
    return instrumentParam_make(target_slot, token);
}

instrument_target_token_t instrumentManager_targetTokenFromIdForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_param_id_t id,
    instrument_target_use_t use)
{
    uint8_t local;

    /*
     * Collapse a canonical descriptor target ID to retained token storage.
     *
     * Inputs: target slot namespace and runtime/display ID. Output: a local
     * descriptor token only when the ID belongs to the requested slot and is
     * valid for the requested use; all other IDs become the byte off token.
     */
    if (id == INSTRUMENT_PARAM_INVALID)
        return INSTRUMENT_TARGET_TOKEN_OFF;
    if (!instrumentParam_isVoiceParameter(id) ||
        instrumentParam_slot(id) != target_slot) {
        return INSTRUMENT_TARGET_TOKEN_OFF;
    }
    local = instrumentParam_local(id);
    return instrumentManager_targetTokenValidForSlot(scene_index, target_slot,
                                                     local, use)
        ? (instrument_target_token_t)local
        : INSTRUMENT_TARGET_TOKEN_OFF;
}

instrument_target_token_t instrumentManager_stepTargetTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use)
{
    instrument_param_id_t current_id;
    instrument_param_id_t next_id;

    /*
     * Step through valid targets while retaining only byte tokens.
     *
     * Inputs mirror the canonical descriptor stepper, but current/output are
     * retained local tokens. The function expands to a canonical ID only for
     * the duration of traversal, then collapses back to byte storage.
     */
    if (current == INSTRUMENT_TARGET_TOKEN_OFF) {
        current_id = INSTRUMENT_PARAM_INVALID;
    } else {
        current_id = instrumentManager_targetIdFromTokenForSlot(
            scene_index, target_slot, current, use);
    }
    next_id = instrumentManager_stepTargetForSlot(scene_index, target_slot,
                                                  current_id, direction, use);
    return instrumentManager_targetTokenFromIdForSlot(scene_index, target_slot,
                                                      next_id, use);
}

uint8_t instrumentManager_lfoTargetVoiceValid(uint8_t voice)
{
    /*
     * Validate the retained LFO target namespace byte.
     *
     * Values 1..6 address instrument slots and value 7 is the Scene namespace
     * displayed by Menu as `scn`. Future effect namespaces can be added above
     * this value without widening lfo_target_param.
     */
    return (uint8_t)(voice >= INSTRUMENT_TARGET_VOICE_FIRST &&
                     voice <= INSTRUMENT_TARGET_VOICE_SCENE);
}

instrument_param_id_t instrumentManager_lfoTargetIdFromToken(
    uint8_t scene_index,
    uint8_t source_slot,
    uint8_t target_voice,
    instrument_target_token_t token,
    instrument_target_use_t use)
{
    (void)source_slot;

    /*
     * Expand an LFO target pair into a canonical runtime target.
     *
     * lfo_target_voice chooses the namespace. Voice namespaces interpret the
     * parameter byte as a local descriptor index; the Scene namespace
     * interprets it as a Scene target-table index. Off and invalid tokens
     * always expand to INSTRUMENT_PARAM_INVALID.
     */
    if (token == INSTRUMENT_TARGET_TOKEN_OFF ||
        !instrumentManager_lfoTargetVoiceValid(target_voice)) {
        return INSTRUMENT_PARAM_INVALID;
    }
    if (target_voice == INSTRUMENT_TARGET_VOICE_SCENE) {
        uint16_t id = sceneModTarget_idFromIndex(token);
        return sceneModTarget_valid(id, SCENE_MOD_TARGET_USE_LFO)
            ? id : INSTRUMENT_PARAM_INVALID;
    }
    return instrumentManager_targetIdFromTokenForSlot(
        scene_index, (uint8_t)(target_voice - 1u), token, use);
}

instrument_target_token_t instrumentManager_lfoTargetTokenFromId(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_param_id_t id,
    instrument_target_use_t use)
{
    /*
     * Collapse a canonical LFO target ID into the selected namespace token.
     *
     * Scene IDs become Scene target-table indices only when target_voice is
     * the `scn` namespace. Instrument IDs become local descriptor indices only
     * when they belong to the selected voice namespace.
     */
    if (id == INSTRUMENT_PARAM_INVALID ||
        !instrumentManager_lfoTargetVoiceValid(target_voice)) {
        return INSTRUMENT_TARGET_TOKEN_OFF;
    }
    if (target_voice == INSTRUMENT_TARGET_VOICE_SCENE) {
        uint8_t index;
        return (sceneModTarget_indexFromId(id, &index) &&
                sceneModTarget_valid(id, SCENE_MOD_TARGET_USE_LFO))
            ? (instrument_target_token_t)index
            : INSTRUMENT_TARGET_TOKEN_OFF;
    }
    return instrumentManager_targetTokenFromIdForSlot(
        scene_index, (uint8_t)(target_voice - 1u), id, use);
}

instrument_target_token_t instrumentManager_stepLfoTargetToken(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use)
{
    /*
     * Walk an LFO destination list in the selected namespace.
     *
     * Voice namespaces reuse descriptor-token stepping. The Scene namespace
     * walks SceneModTargets and stores only the resulting local Scene index in
     * lfo_target_param.
     */
    if (!instrumentManager_lfoTargetVoiceValid(target_voice) || direction == 0)
        return current;
    if (target_voice == INSTRUMENT_TARGET_VOICE_SCENE) {
        uint16_t current_id = instrumentManager_lfoTargetIdFromToken(
            scene_index, 0u, target_voice, current, use);
        uint16_t next_id = sceneModTarget_step(current_id, direction,
                                               SCENE_MOD_TARGET_USE_LFO);
        return instrumentManager_lfoTargetTokenFromId(scene_index, target_voice,
                                                      next_id, use);
    }
    return instrumentManager_stepTargetTokenForSlot(
        scene_index, (uint8_t)(target_voice - 1u), current, direction, use);
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

static instrument_target_token_t instrumentManager_lastTargetTokenForSlot(
    uint8_t scene_index, uint8_t target_slot, instrument_target_use_t use)
{
    instrument_target_token_t current = INSTRUMENT_TARGET_TOKEN_OFF;

    /*
     * Find the last valid retained descriptor token for one slot.
     *
     * Inputs: Scene index, target slot, and target use. Output: the final
     * descriptor-local byte token or off when the slot has no valid targets.
     * Velocity uses this when stepping backward from the own-Morph Scene token
     * into the source voice's descriptor portion.
     */
    while (1) {
        instrument_target_token_t next =
            instrumentManager_stepTargetTokenForSlot(scene_index, target_slot,
                                                     current, 1, use);
        if (next == current)
            return current;
        current = next;
    }
}

static instrument_param_id_t instrumentManager_velocityTargetIdFromToken(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t token)
{
    /*
     * Expand one retained velocity token for runtime/display only.
     *
     * Velocity destinations are self-scoped. Descriptor tokens target the
     * source voice's own descriptor table, and the single extra byte token
     * expands to that same source voice's Scene Morph target. Cross-voice and
     * arbitrary Scene destinations are intentionally not representable here.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT ||
        token == INSTRUMENT_TARGET_TOKEN_OFF) {
        return INSTRUMENT_PARAM_INVALID;
    }
    if (token == INSTRUMENT_TARGET_TOKEN_VOICE_MORPH) {
        uint16_t id = sceneModTarget_voiceMorphId(source_slot);
        return sceneModTarget_valid(id, SCENE_MOD_TARGET_USE_VELOCITY)
            ? id : INSTRUMENT_PARAM_INVALID;
    }
    return instrumentManager_targetIdFromTokenForSlot(
        scene_index, source_slot, token, INSTRUMENT_TARGET_MODULATION);
}

uint8_t instrumentManager_targetValidForVelocitySource(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t token)
{
    /*
     * Validate a retained velocity destination token for one source voice.
     *
     * Velocity is intentionally self-scoped: the source voice's trigger
     * velocity can modulate descriptor targets on that same instrument slot or
     * that voice's own Scene Morph target. The retained byte is therefore off,
     * a local descriptor index, or the explicit own-Morph token.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (token == INSTRUMENT_TARGET_TOKEN_OFF)
        return 1u;
    if (token == INSTRUMENT_TARGET_TOKEN_VOICE_MORPH) {
        return sceneModTarget_valid(sceneModTarget_voiceMorphId(source_slot),
                                    SCENE_MOD_TARGET_USE_VELOCITY);
    }
    return instrumentManager_targetTokenValidForSlot(
        scene_index, source_slot, token, INSTRUMENT_TARGET_MODULATION);
}

instrument_target_token_t instrumentManager_stepVelocityTargetForSource(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t current,
    int8_t direction)
{
    instrument_target_token_t normalized = current;

    /*
     * Walk the velocity target list for one source voice.
     *
     * Inputs: Scene index, source slot, retained token, and signed direction.
     * Output: one off entry, then this source slot's valid descriptor targets,
     * then the source voice's own Morph Scene target. Arbitrary Scene targets
     * belong to LFO's explicit `scn` namespace, not velocity storage.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || direction == 0)
        return current;
    if (!instrumentManager_targetValidForVelocitySource(scene_index,
                                                        source_slot,
                                                        normalized)) {
        normalized = INSTRUMENT_TARGET_TOKEN_OFF;
    }

    if (direction > 0) {
        if (normalized == INSTRUMENT_TARGET_TOKEN_OFF ||
            normalized <= INSTRUMENT_TARGET_TOKEN_MAX_LOCAL) {
            instrument_target_token_t next =
                instrumentManager_stepTargetTokenForSlot(
                    scene_index, source_slot, normalized, 1,
                    INSTRUMENT_TARGET_MODULATION);
            if (next != normalized)
                return next;
            return instrumentManager_targetValidForVelocitySource(
                scene_index, source_slot, INSTRUMENT_TARGET_TOKEN_VOICE_MORPH)
                ? INSTRUMENT_TARGET_TOKEN_VOICE_MORPH
                : normalized;
        }
        return INSTRUMENT_TARGET_TOKEN_OFF;
    }

    if (normalized == INSTRUMENT_TARGET_TOKEN_VOICE_MORPH) {
        return instrumentManager_lastTargetTokenForSlot(
            scene_index, source_slot, INSTRUMENT_TARGET_MODULATION);
    }
    if (normalized <= INSTRUMENT_TARGET_TOKEN_MAX_LOCAL) {
        return instrumentManager_stepTargetTokenForSlot(
            scene_index, source_slot, normalized, -1,
            INSTRUMENT_TARGET_MODULATION);
    }
    return INSTRUMENT_TARGET_TOKEN_OFF;
}

void *instrumentManager_runtimeInstance(uint8_t slot)
{
    /*
     * Resolve the live DSP instance for the slot's current instrument type.
     *
     * Input: zero-based Scene slot. Output: typed voice memory borrowed as
     * void* for descriptor-offset writes, or NULL for an unknown type/slot.
     * The current Scene's instrument type is deliberately consulted here so
     * every descriptor writer, modulation resolver, and morph apply follows
     * Instrument Load assignments instead of the old fixed physical slots.
     */
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: return instrumentManager_drumRuntime(slot);
    case INSTRUMENT_TYPE_SNR: return instrumentManager_snareRuntime(slot);
    case INSTRUMENT_TYPE_CYM: return instrumentManager_cymbalRuntime(slot);
    case INSTRUMENT_TYPE_HAT: return instrumentManager_hihatRuntime(slot);
    default: return 0;
    }
}

static Lfo *instrumentManager_runtimeLfo(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? &voice->lfo : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? &voice->lfo : 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? &voice->lfo : 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? &voice->lfo : 0; }
    default:
        return 0;
    }
}

void instrumentManager_runtimeInit(void)
{
    uint8_t slot;

    /*
     * Initialize all dynamic runtime instances.
     *
     * Inputs: none; legacy engine init functions should already have prepared
     * voiceArray[0..2], snareVoice, cymbalVoice, and hatVoice. Output: every
     * non-native runtime pool instance is initialized with its engine defaults.
     * This function is separate from the engine init wrappers because only
     * InstrumentManager knows which legacy globals are preserved for
     * compatibility and which additional per-slot instances exist.
    */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        /*
         * Seed the runtime type shadow from resident SceneData when available.
         *
         * Input: boot-time active Scene slot type. Output: runtime dispatch has
         * a valid initial type until Preset's boot apply or the first deferred
         * Scene-slot commit refreshes it through instrumentManager_resetRuntimeSlot().
         */
        const kit_instrument_slot_t *instrument =
            scene_instrumentSlotConst(scene_getActiveIndex(), slot);
        runtime_slot_type[slot] =
            instrument ? instrument->type : INSTRUMENT_TYPE_UNKNOWN;
        if (slot >= NUM_VOICES)
            Drum_initVoice(instrumentManager_drumRuntime(slot), slot);
        if (slot != 3u)
            Snare_initVoice(&runtime_snare_slots[slot]);
        if (slot != 4u)
            Cymbal_initVoice(&runtime_cymbal_slots[slot]);
        if (slot != 5u)
            HiHat_initVoice(&runtime_hihat_slots[slot]);
    }
}

uint8_t instrumentManager_ampEnvelopeQuiet(uint8_t slot)
{
    SlopeEg2 *ampEg;

    /*
     * Determine whether a runtime slot is quiet enough to swap.
     *
     * Inputs: zero-based slot using the current runtime type shadow. Outputs:
     * 1 for invalid/no-envelope/stopped/near-zero slots, 0 for slots whose amp
     * envelope is still meaningfully above silence. Invalid or missing runtime
     * instances are treated as quiet because there is no old sound to protect.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return 1u;
    ampEg = instrumentManager_ampEg(slot);
    if (!ampEg)
        return 1u;
    if (ampEg->state == EG_STOPPED)
        return 1u;
    return (uint8_t)(ampEg->value <= INSTRUMENT_AMP_EG_QUIET_THRESHOLD);
}

void instrumentManager_clearAllRuntimeModulationTargets(void)
{
    uint8_t source_slot;

    /*
     * Tear down the modulation graph while every outgoing runtime is findable.
     *
     * Inputs are the six active Scene slot types before a staged Instrument
     * commit. For each source, both LFO pairs first restore supplemental owners
     * such as Scene Morph/decimation, then direct ModulationNode destinations
     * restore their captured runtime baseline and forget their pointer. Velocity
     * follows the same order. Output is an empty graph ready to be rebuilt from
     * retained Scene descriptors after the incoming runtime image is applied.
     *
     * The loop is intentionally all-source rather than target-slot-only: a
     * destination in the replaced slot may be owned by any source voice, and a
     * source in the replaced slot must be resolved before SceneData changes its
     * type. No post-commit lookup can reliably recover an outgoing pool node.
     */
    for (source_slot = 0u; source_slot < INSTRUMENT_SLOT_COUNT; source_slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(source_slot);
        uint8_t pair;

        for (pair = 0u; pair < 2u; pair++) {
            instrumentManager_restoreLfoSupplementalTarget(source_slot, pair);
            if (lfo) {
                modNode_clearDestination(pair ? &lfo->modTarget2
                                              : &lfo->modTarget);
            }
        }
        instrumentManager_restoreVelocitySupplementalTarget(source_slot);
        modNode_clearDestination(&velocityModulators[source_slot]);
    }
}

void instrumentManager_resetRuntimeSlot(uint8_t slot)
{
    const kit_instrument_slot_t *incoming;

    /*
     * Reset only the incoming runtime selected by the committed Scene type.
     *
     * Input is a zero-based slot after staged SceneData commit. Output is one
     * fully initialized engine object with stopped envelopes, default LFO
     * nodes, and no state inherited from an earlier use of its runtime pool.
     * Descriptor/Morph application follows in Preset and replaces these engine
     * defaults with the loaded values. Other slots are deliberately untouched.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    incoming = scene_instrumentSlotConst(scene_getActiveIndex(), slot);
    runtime_slot_type[slot] =
        incoming ? incoming->type : INSTRUMENT_TYPE_UNKNOWN;
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM:
        Drum_initVoice(instrumentManager_drumRuntime(slot), slot);
        break;
    case INSTRUMENT_TYPE_SNR:
        Snare_initVoice(instrumentManager_snareRuntime(slot));
        break;
    case INSTRUMENT_TYPE_CYM:
        Cymbal_initVoice(instrumentManager_cymbalRuntime(slot));
        break;
    case INSTRUMENT_TYPE_HAT:
        HiHat_initVoice(instrumentManager_hihatRuntime(slot));
        break;
    default:
        break;
    }
}

void instrumentManager_dispatchRuntimeLfos(void)
{
    uint8_t slot;

    /*
     * Dispatch one LFO block for every current runtime slot.
     *
     * Inputs: active Scene slot assignments and each slot's current runtime
     * LFO. Output: direct ModulationNode destinations and supplemental/Scene
     * LFO targets update exactly once per audio block. This belongs here
     * instead of mixer.c so mixer does not duplicate the type-to-instance
     * routing table.
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (lfo)
            lfo_dispatchNextValue(lfo, slot);
    }
}

void instrumentManager_recalcRuntimeLfoSync(void)
{
    uint8_t slot;

    /*
     * Recalculate synced LFO rates for current slot runtime instances.
     *
     * Inputs: tempo/sync state read by lfo_calcPhaseInc(). Output: each
     * active slot's current instrument LFO receives a refreshed phaseInc. This
     * replaces lfo.c's fixed global list so synced LFOs continue to work after
     * an instrument type is loaded into a different slot.
     */
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (lfo)
            lfo->phaseInc = lfo_calcPhaseInc(lfo->freq, lfo->sync);
    }
}

void instrumentManager_retriggerRuntimeLfos(uint8_t trigger_track)
{
    uint8_t slot;

    /*
     * Retrigger source LFOs whose retrigger selector matches a track press.
     *
     * Inputs: zero-based trigger track from MIDI/sequencer/voice engine, where
     * track 6 is the alternate trigger for slot 6. Output: every current slot
     * LFO whose retrigger field equals the visible 1-based track receives its
     * phase offset. This cannot stay in lfo.c's old globals because current
     * source LFO ownership now follows loaded instrument types.
     */
    if (trigger_track > INSTRUMENT_SLOT_COUNT)
        return;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (lfo && lfo->retrigger == (uint8_t)(trigger_track + 1u))
            lfo->phase = lfo->phaseOffset;
    }
}

uint8_t instrumentManager_runtimePan(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? voice->pan : 64u; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? voice->pan : 64u; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? voice->pan : 64u; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? voice->pan : 64u; }
    default:
        return 64u;
    }
}

void instrumentManager_recalcSlotFilter(uint8_t slot)
{
    /*
     * Recalculate the current slot instrument's filter coefficients.
     *
     * Inputs: zero-based slot. Output: the active runtime filter for that
     * slot/type receives SVF_recalcFreq(). This small dispatcher is kept here
     * rather than folded into mixer.c because filter ownership depends on the
     * instrument currently loaded into the slot, not on the physical voice
     * number.
     */
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        if (voice) SVF_recalcFreq(&voice->filter);
        break; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        if (voice) SVF_recalcFreq(&voice->filter);
        break; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        if (voice) SVF_recalcFreq(&voice->filter);
        break; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        if (voice) SVF_recalcFreq(&voice->filter);
        break; }
    default:
        break;
    }
}

void instrumentManager_calcSlotAsync(uint8_t slot)
{
    /*
     * Run the current slot instrument's control-rate block.
     *
     * Inputs: zero-based render slot. Output: exactly one engine async
     * function advances the active runtime instance. This is the mixer-facing
     * half of dynamic Instrument Load; without it the menu/storage assignment
     * would change SceneData while the audio path kept calculating the old
     * fixed instrument engines.
     */
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM:
        Drum_calcVoiceAsync(instrumentManager_drumRuntime(slot), AMP_EG_SYNC);
        break;
    case INSTRUMENT_TYPE_SNR:
        Snare_calcAsyncVoice(instrumentManager_snareRuntime(slot));
        break;
    case INSTRUMENT_TYPE_CYM:
        Cymbal_calcAsyncVoice(instrumentManager_cymbalRuntime(slot));
        break;
    case INSTRUMENT_TYPE_HAT:
        HiHat_calcAsyncVoice(instrumentManager_hihatRuntime(slot));
        break;
    default:
        break;
    }
}

void instrumentManager_calcSlotSyncBlock(uint8_t slot, int16_t *buf,
                                         uint8_t size)
{
    /*
     * Render the current slot instrument into one mono audio block.
     *
     * Inputs: zero-based render slot, output buffer, and block size. Output:
     * the selected engine writes a mono voice block, or silence for unknown
     * slot/type. Mixer remains responsible for decimation, pan, routing, and
     * slider interpolation after this call.
     */
    if (!buf)
        return;
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM:
        Drum_calcVoiceSyncBlock(instrumentManager_drumRuntime(slot), buf, size);
        break;
    case INSTRUMENT_TYPE_SNR:
        Snare_calcSyncBlockVoice(instrumentManager_snareRuntime(slot), buf, size);
        break;
    case INSTRUMENT_TYPE_CYM:
        Cymbal_calcSyncBlockVoice(instrumentManager_cymbalRuntime(slot), buf, size);
        break;
    case INSTRUMENT_TYPE_HAT:
        HiHat_calcSyncBlockVoice(instrumentManager_hihatRuntime(slot), buf, size);
        break;
    default:
        memset(buf, 0, size * sizeof(buf[0]));
        break;
    }
}

static void instrumentManager_applySlot6AlternateDecay(uint8_t alternate)
{
    const scene_t *scene = scene_getConst(scene_getActiveIndex());
    const kit_instrument_slot_t *slot_state;
    uint8_t base_index = INSTRUMENT_MENU_EMPTY;
    instrument_type_t type;
    SlopeEg2 *ampEg;
    uint16_t value;

    /*
     * Apply slot-6 generated track-7 amp decay for non-Choke instruments.
     *
     * Inputs: alternate is nonzero when visible track 7 triggered slot 6.
     * Output: slot 6's active amplitude envelope decay is swapped to the
     * hidden Scene setting for track 7, or restored to the current base/morph
     * amp_envelope_decay value for track 6. Choke instruments such as HiHat
     * are excluded because their descriptor-owned `_choke` parameters already
     * maintain separate runtime decay caches.
     */
    if (!scene)
        return;
    slot_state = &scene->kit.instruments[5u];
    type = slot_state->type;
    if (instrumentManager_typeFlags(type) & INSTRUMENT_FLAG_CHOKE)
        return;
    if (!instrumentManager_descriptorIndexByKey(type, "amp_envelope_decay",
                                                &base_index)) {
        return;
    }
    ampEg = instrumentManager_ampEg(5u);
    if (!ampEg)
        return;
    value = alternate
        ? (slot6_track7_decay_lfo_active
              ? slot6_track7_decay_lfo_value
              : scene->kit.settings.slot6_track7_amp_envelope_decay)
        : slot_state->parameter_images.morph_interpolation[base_index];
    if (value > 127u)
        value = 127u;
    slopeEg2_setDecay(ampEg, (uint8_t)value, AMP_EG_SYNC);
}

void instrumentManager_triggerTrack(uint8_t trigger_track, uint8_t note,
                                    uint8_t velocity)
{
    uint8_t slot;
    uint8_t alternate_slot6;

    /*
     * Trigger the instrument currently assigned to a visible track.
     *
     * Inputs: zero-based visible trigger track 0..6, note, and velocity.
     * Output: tracks 0..5 trigger matching storage slots; track 6 triggers
     * slot 6 as the choke/alternate path. Choke instruments receive the
     * alternate selector, while non-Choke slot-6 instruments borrow the hidden
     * generated amp decay for track 7 before triggering.
     */
    if (trigger_track > INSTRUMENT_SLOT_COUNT)
        return;
    slot = (trigger_track >= INSTRUMENT_SLOT_COUNT)
        ? (INSTRUMENT_SLOT_COUNT - 1u)
        : trigger_track;
    alternate_slot6 =
        (uint8_t)(slot == 5u && trigger_track == INSTRUMENT_SLOT_COUNT);
    if (slot == 5u)
        instrumentManager_applySlot6AlternateDecay(alternate_slot6);

    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM:
        Drum_triggerVoice(instrumentManager_drumRuntime(slot), slot,
                          velocity, note);
        break;
    case INSTRUMENT_TYPE_SNR:
        Snare_triggerVoice(instrumentManager_snareRuntime(slot), slot,
                           velocity, note);
        break;
    case INSTRUMENT_TYPE_CYM:
        Cymbal_triggerVoice(instrumentManager_cymbalRuntime(slot), slot,
                            velocity, note);
        break;
    case INSTRUMENT_TYPE_HAT:
        HiHat_triggerVoice(instrumentManager_hihatRuntime(slot), slot,
                           velocity, alternate_slot6, note);
        break;
    default:
        break;
    }
}

static void instrumentManager_writeParameter(Parameter parameter,
                                             instrument_param_value_t value)
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
    /*
     * Resolve the DSP runtime type, not merely the active Scene's stored type.
     *
     * Inputs: zero-based slot. Output: the type whose runtime pool currently
     * owns that slot's sounding state. This indirection is required because
     * Scene selection now swaps patterns immediately but defers instrument
     * parameter/type commits until a quiet envelope or the next trigger.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return INSTRUMENT_TYPE_UNKNOWN;
    return runtime_slot_type[slot];
}

static OscInfo *instrumentManager_osc(uint8_t slot, const char *key)
{
    instrument_type_t type = instrumentManager_slotType(slot);

    if (!key)
        return 0;
    if (type == INSTRUMENT_TYPE_DRM) {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        if (!voice) return 0;
        if (strncmp(key, "osc1_", 5) == 0) return &voice->osc;
        if (strncmp(key, "osc2_", 5) == 0) return &voice->modOsc;
    }
    if (type == INSTRUMENT_TYPE_SNR) {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        if (!voice) return 0;
        if (strncmp(key, "osc1_", 5) == 0) return &voice->osc;
        if (strncmp(key, "noise_", 6) == 0) return &voice->noiseOsc;
    }
    if (type == INSTRUMENT_TYPE_CYM) {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        if (!voice) return 0;
        if (strncmp(key, "osc1_", 5) == 0) return &voice->osc;
        if (strncmp(key, "osc2_", 5) == 0) return &voice->modOsc;
        if (strncmp(key, "osc3_", 5) == 0) return &voice->modOsc2;
    }
    if (type == INSTRUMENT_TYPE_HAT) {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        if (!voice) return 0;
        if (strncmp(key, "osc1_", 5) == 0) return &voice->osc;
        if (strncmp(key, "osc2_", 5) == 0) return &voice->modOsc;
        if (strncmp(key, "osc3_", 5) == 0) return &voice->modOsc2;
    }
    return 0;
}

static ResonantFilter *instrumentManager_filter(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? &voice->filter : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? &voice->filter : 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? &voice->filter : 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? &voice->filter : 0; }
    default:
        return 0;
    }
}

static SlopeEg2 *instrumentManager_ampEg(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? &voice->oscVolEg : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? &voice->oscVolEg : 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? &voice->oscVolEg : 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? &voice->oscVolEg : 0; }
    default:
        return 0;
    }
}

static DecayEg *instrumentManager_pitchEg(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? &voice->oscPitchEg : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? &voice->oscPitchEg : 0; }
    default:
        return 0;
    }
}

static Distortion *instrumentManager_distortion(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? &voice->distortion : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? &voice->distortion : 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? &voice->distortion : 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? &voice->distortion : 0; }
    default:
        return 0;
    }
}

static TransientGenerator *instrumentManager_transient(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? &voice->transGen : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? &voice->transGen : 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? &voice->transGen : 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? &voice->transGen : 0; }
    default:
        return 0;
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
    uint8_t slot, const ParamDescriptor *descriptor,
    instrument_param_value_t value)
{
    uint8_t descriptor_index;
    instrument_param_id_t id;
    instrument_runtime_target_t target;
    instrument_mod_domain_t domain;
    uint8_t source_slot;
    uint8_t pair;

    /*
     * Refresh descriptor-backed modulation baselines/ranges after an ordinary write.
     *
     * Inputs: slot/descriptor identify a live runtime value just changed by a
     * menu edit, morph apply, automation write, or loaded-kit apply, and value
     * is the descriptor-domain value that was applied. Output: active direct
     * ModulationNode targets for the same canonical id recapture their runtime
     * originalValue/range, and LFO descriptor adapters refresh their
     * descriptor-domain base cache. Temporary LFO overlay writes use the quiet
     * runtime writer path and do not call this function, because those writes
     * must not become the next unmodulated base.
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
    if (!instrumentManager_descriptorModDomain(descriptor, &domain))
        return;
    if (value < domain.min_value)
        value = domain.min_value;
    else if (value > domain.max_value)
        value = domain.max_value;
    for (source_slot = 0u; source_slot < INSTRUMENT_SLOT_COUNT; source_slot++) {
        for (pair = 0u; pair < 2u; pair++) {
            instrument_lfo_target_adapter_t *adapter =
                &lfo_descriptor_targets[source_slot][pair];
            if (adapter->active && adapter->id == id) {
                adapter->base_value = value;
            }
        }
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
    {
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (!lfo)
            return 0;
        return target_index ? &lfo->modTarget2 : &lfo->modTarget;
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
    instrument_mod_domain_t domain;

    /*
     * Build the stable min/max contract for one legacy direct modulation target.
     *
     * Inputs: descriptor metadata and the already resolved optional waveform
     * interpolation affiliate. Output: range_out receives the min/max values
     * ModulationNode uses for direct-pointer backends such as velocity while
     * they still exist. LFO descriptor targets no longer use this path; they
     * use descriptor-domain adapters and instrumentManager_writeRuntime().
     * Eligibility still comes from ParamDescriptor::mod_domain so target
     * browsing and runtime installation agree without dtype guessing.
     */
    if (range_out) {
        range_out->min = 0.f;
        range_out->max = 0.f;
        range_out->valid = 0u;
    }
    if (!descriptor || !range_out ||
        !instrumentManager_descriptorModDomain(descriptor, &domain)) {
        return 0u;
    }

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
        if ((domain.flags & INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) &&
            !waveInterpTarget) {
            return 0u;
        }
        range_out->min = (float)domain.min_value;
        range_out->max = (float)domain.max_value;
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
    if (!instrumentManager_descriptorModDomain(descriptor,
                                               &target_out->domain)) {
        return 0u;
    }
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
    return instrumentManager_writeRuntime(slot, descriptor, (uint8_t)value);
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

static instrument_param_value_t instrumentManager_descriptorImageBase(
    uint8_t slot,
    uint8_t local,
    instrument_mod_domain_t domain)
{
    const scene_t *scene;
    instrument_param_value_t value;

    /*
     * Read the current descriptor-domain base for an LFO adapter.
     *
     * Inputs: target slot, local descriptor index, and the descriptor's legal
     * modulation domain. Output: the active Scene/Morph interpolation image
     * clamped to that domain. This deliberately does not read the live runtime
     * field: many DSP fields are scaled or inverted relative to storage bytes,
     * so runtime readback would break amount-zero parity and target-clear
     * restore for envelopes, filters, pitch, and other owner-shaped targets.
     */
    scene = scene_getConst(scene_getActiveIndex());
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        local >= INSTRUMENT_PARAM_COUNT) {
        return domain.min_value;
    }
    value =
        scene->kit.instruments[slot].parameter_images.morph_interpolation[local];
    if (value < domain.min_value)
        return domain.min_value;
    if (value > domain.max_value)
        return domain.max_value;
    return value;
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
    case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
        preset_setSlot6Track7AmpEnvelopeDecay(scene_getActiveIndex(), 0u,
                                              (uint8_t)value, 0u);
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
    case SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY:
        base = scene->kit.settings.slot6_track7_amp_envelope_decay;
        shaped = modNode_shapeRangeU16(base, descriptor->min_value,
                                       descriptor->max_value,
                                       lfo_value_0_1, amount, polarity);
        /*
         * LFO modulation of the generated track-7 decay is runtime-only.
         *
         * Inputs: retained kit setting as the base plus the shaped LFO value.
         * Output: a hidden runtime override used by the slot-6 alternate
         * trigger path. This cannot call the retained Scene setter because an
         * audio-rate LFO would otherwise rewrite the saved kit setting every
         * block; velocity targets still use the retained setter above because
         * they are discrete trigger operations.
         */
        slot6_track7_decay_lfo_active = 1u;
        slot6_track7_decay_lfo_value = (uint8_t)shaped;
        return 1u;
    default:
        return 0u;
    }
}

static const ParamDescriptor *instrumentManager_lfoAdapterDescriptor(
    const instrument_lfo_target_adapter_t *adapter)
{
    const ParamDescriptor *descriptor;

    /*
     * Validate that an installed descriptor adapter still matches its slot.
     *
     * Input: one adapter captured when an LFO target was installed. Output:
     * the current descriptor pointer only when the target slot still contains
     * the same instrument descriptor row. This prevents an old LFO adapter from
     * writing an offset from a previous instrument type after Instrument Load
     * changes the target slot before target normalization catches up.
     */
    if (!adapter || !adapter->active)
        return 0;
    descriptor = instrumentManager_descriptor(instrumentManager_slotType(adapter->slot),
                                              adapter->descriptor_index);
    return (descriptor == adapter->descriptor) ? descriptor : 0;
}

static void instrumentManager_restoreLfoDescriptorTarget(uint8_t source_slot,
                                                         uint8_t target_pair)
{
    instrument_lfo_target_adapter_t *adapter;
    const ParamDescriptor *descriptor;

    /*
     * Restore the base value for one descriptor-backed LFO target.
     *
     * Inputs: source slot and pair whose target is being cleared or replaced.
     * Output: the cached descriptor-domain base is applied quietly through the
     * normal runtime writer, then the adapter is cleared. This is the
     * parameter-space equivalent of modNode_clearDestination(): envelope,
     * filter, pitch, and distortion owners receive the same value they would
     * receive from a menu/load/morph write, but the restore itself is not a new
     * retained base edit.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || target_pair > 1u)
        return;
    adapter = &lfo_descriptor_targets[source_slot][target_pair];
    descriptor = instrumentManager_lfoAdapterDescriptor(adapter);
    if (descriptor) {
        (void)instrumentManager_writeRuntimeInternal(adapter->slot, descriptor,
                                                     adapter->base_value, 0u);
    }
    memset(adapter, 0, sizeof(*adapter));
}

static void instrumentManager_applyLfoDescriptorTarget(uint8_t source_slot,
                                                       uint8_t target_pair,
                                                       float lfo_value_0_1,
                                                       uint8_t polarity,
                                                       float amount)
{
    instrument_lfo_target_adapter_t *adapter;
    const ParamDescriptor *descriptor;
    uint16_t shaped;

    /*
     * Apply one LFO sample to a descriptor-backed instrument target.
     *
     * Inputs: source LFO identity, target pair, normalized waveform value,
     * polarity, and pair amount. Output: a temporary descriptor-domain value is
     * shaped with original-LXR negative math and applied quietly through
     * instrumentManager_writeRuntimeInternal(). The quiet write is essential:
     * LFO overlays must use the same DSP scaling as ordinary writes without
     * becoming the next base value seen by amount-zero modulation or target
     * restore.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || target_pair > 1u)
        return;
    adapter = &lfo_descriptor_targets[source_slot][target_pair];
    descriptor = instrumentManager_lfoAdapterDescriptor(adapter);
    if (!descriptor)
        return;
    shaped = modNode_shapeParameterU16(adapter->base_value,
                                       adapter->domain.min_value,
                                       adapter->domain.max_value,
                                       lfo_value_0_1, amount, polarity);
    if (shaped > 255u)
        shaped = 255u;
    (void)instrumentManager_writeRuntimeInternal(adapter->slot, descriptor,
                                                 (uint8_t)shaped, 0u);
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
        if (descriptor &&
            descriptor->kind == SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY) {
            slot6_track7_decay_lfo_active = 0u;
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
     * Install or clear one LFO modulation destination.
     *
     * Inputs: source_slot selects the LFO that emits modulation values,
     * target_index selects destination pair 1 or 2 on that LFO, and target_id
     * is either INSTRUMENT_PARAM_INVALID for off or a canonical descriptor id
     * whose slot may differ from the source slot. Output: nonzero when the
     * source exists and the target is cleared or installed. Descriptor
     * instrument targets are installed as parameter-domain adapters rather than
     * direct ModulationNode pointers, so LFO amount/polarity acts on the same
     * values that files, menus, morph, and automation store before the DSP
     * owner converts them to runtime math.
     */
    if (!node)
        return 0u;
    instrumentManager_restoreLfoDescriptorTarget(source_slot, target_index);
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
    modNode_clearDestination(node);
    {
        instrument_lfo_target_adapter_t *adapter =
            &lfo_descriptor_targets[source_slot][target_index];
        /*
         * Descriptor LFO adapter install.
         *
         * Inputs: resolved target descriptor, target slot/local id, and the
         * descriptor-owned modulation domain. Output: the adapter captures the
         * current Scene/Morph descriptor image as its base. The raw
         * ModulationNode is intentionally empty after this point; lfo.c still
         * reads its amount field, but InstrumentManager owns the descriptor
         * apply/restore path so shaped DSP fields are never written directly.
         */
        adapter->active = 1u;
        adapter->slot = target.slot;
        adapter->descriptor_index = target.descriptor_index;
        adapter->id = target.id;
        adapter->descriptor = target.descriptor;
        adapter->domain = target.domain;
        adapter->base_value =
            instrumentManager_descriptorImageBase(target.slot,
                                                  target.descriptor_index,
                                                  target.domain);
    }
    return 1u;
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

void instrumentManager_updateLfoAdapters(uint8_t source_slot,
                                         uint8_t target_pair,
                                         float lfo_value_0_1,
                                         uint8_t polarity,
                                         float amount)
{
    const installed_mod_target_t *installed;
    uint16_t shaped;
    uint8_t base;

    /*
     * Apply one LFO sample to every InstrumentManager-owned target backend.
     *
     * Inputs: source slot, target pair, normalized LFO value, shared polarity,
     * and normalized pair amount. Output: descriptor parameter-domain adapters,
     * slot-decimation adapters, and Scene adapters receive owner-specific
     * writes. Direct ModulationNode targets are no longer used for descriptor
     * instrument LFO targets because those targets must pass through the normal
     * descriptor runtime writer to preserve envelope/filter/pitch scaling.
     */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || target_pair > 1u)
        return;
    instrumentManager_applyLfoDescriptorTarget(source_slot, target_pair,
                                               lfo_value_0_1, polarity,
                                               amount);
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
    uint8_t slot, const ParamDescriptor *descriptor,
    instrument_param_value_t value)
{
    const char *key = descriptor ? descriptor->file_key : 0;
    OscInfo *osc;
    ResonantFilter *filter;
    SlopeEg2 *ampEg;
    DecayEg *pitchEg;
    TransientGenerator *transient;
    Distortion *distortion;
    uint8_t byteValue = value;

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
                (uint8_t)(byteValue + 1u));
            return 1u;
        }
    }

    ampEg = instrumentManager_ampEg(slot);
    if (ampEg) {
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_HAT &&
            strcmp(key, "amp_envelope_decay") == 0) {
            HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
            /*
             * HiHat base decay keeps its dedicated closed-hat cache.
             *
             * Inputs: canonical base amp_envelope_decay descriptor value for a
             * HiHat slot. Output: the slot hihat decayClosed receives the shaped
             * SlopeEg2 decay value used when slot 6 is triggered from track 6.
             * This must precede the generic amp_envelope_decay branch because
             * HiHat stores closed/open decay in separate cached floats rather
             * than only in oscVolEg.decay.
             */
            if (voice)
                voice->decayClosed = slopeEg2_calcDecay(byteValue);
            return 1u;
        }
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_HAT &&
            strcmp(key, "amp_envelope_decay_choke") == 0) {
            HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
            /*
             * HiHat choke decay keeps the former open-hat runtime cache.
             *
             * Inputs: canonical amp_envelope_decay_choke descriptor value.
             * Output: the slot hihat decayOpen receives the shaped value used
             * when the shared hihat slot is triggered from track 7. The descriptor
             * remains separately mod-targetable because it is a normal
             * descriptor row, not a hidden menu-only alternate.
             */
            if (voice)
                voice->decayOpen = slopeEg2_calcDecay(byteValue);
            return 1u;
        }
        if (strcmp(key, "amp_envelope_attack") == 0) {
            slopeEg2_setAttack(ampEg, byteValue,
                               (uint8_t)(instrumentManager_slotType(slot) ==
                                         INSTRUMENT_TYPE_DRM ? AMP_EG_SYNC : 0u));
            return 1u;
        }
        if (strcmp(key, "amp_envelope_decay") == 0) {
            slopeEg2_setDecay(ampEg, byteValue,
                              (uint8_t)(instrumentManager_slotType(slot) ==
                                        INSTRUMENT_TYPE_DRM ? AMP_EG_SYNC : 0u));
            return 1u;
        }
        if (strcmp(key, "amp_envelope_slope") == 0) {
            slopeEg2_setSlope(ampEg, byteValue);
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
            if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_DRM) {
                DrumVoice *voice = instrumentManager_drumRuntime(slot);
                if (voice)
                    voice->egPitchModAmount =
                        instrumentManager_pitchModAmount(byteValue);
            } else if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_SNR) {
                SnareVoice *voice = instrumentManager_snareRuntime(slot);
                if (voice)
                    voice->egPitchModAmount =
                        instrumentManager_pitchModAmount(byteValue);
            }
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
        Lfo *lfo = instrumentManager_runtimeLfo(slot);
        if (!lfo)
            return 0u;
        lfo_setFreq(lfo, byteValue);
        return 1u;
    }

    return 0u;
}

static uint8_t instrumentManager_writeRuntimeInternal(
    uint8_t slot, const ParamDescriptor *descriptor,
    instrument_param_value_t value,
    uint8_t notify_base_change)
{
    void *instance;
    Parameter parameter;

    /*
     * Apply one descriptor value to its DSP owner.
     *
     * Inputs: slot, descriptor, descriptor-domain value, and a notification
     * flag. Output: the runtime owner receives the same write math used by
     * menu/load/morph/automation paths. Public retained writes notify
     * modulation baselines after applying; temporary LFO overlays pass
     * notify_base_change=0 so a block-local shaped value never becomes the
     * next unmodulated base.
     */
    if (!descriptor)
        return 0u;

    if (instrumentManager_writeSpecialRuntime(slot, descriptor, value)) {
        if (notify_base_change)
            instrumentManager_noteRuntimeValueChanged(slot, descriptor, value);
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
        if (notify_base_change)
            instrumentManager_noteRuntimeValueChanged(slot, descriptor, value);
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
         * Install a velocity destination from a retained byte token.
         *
         * The parameter cell no longer carries a packed destination ID. The
         * byte is off, a local self-descriptor index, or the source voice's own
         * Morph token. Runtime installation expands it here only long enough to
         * use the existing descriptor/Scene modulation backends.
         */
        return instrumentManager_installVelocityModulationTarget(
            slot,
            instrumentManager_velocityTargetIdFromToken(
                scene_getActiveIndex(), slot, value));

    case INSTRUMENT_BIND_LFO_TARGET_VOICE:
    case INSTRUMENT_BIND_LFO_TARGET_VOICE_2:
        /*
         * The selected target voice is stored in its descriptor cell and paired
         * with the matching lfo_target_param binding when that later binding is
         * applied. There is no standalone DSP write for this value. Pair 1 and
         * pair 2 share this validation but keep separate binding identities so
         * Menu/storage can find the correct sibling descriptor cells.
         */
        return instrumentManager_lfoTargetVoiceValid(value);

    case INSTRUMENT_BIND_LFO_TARGET_PARAM:
    case INSTRUMENT_BIND_LFO_TARGET_PARAM_2:
        /*
         * Install an LFO destination from retained byte cells.
         *
         * The parameter cell no longer carries a packed destination ID. The
         * paired target-voice cell supplies the namespace, and this byte
         * supplies a local descriptor/Scene index or off. Pair 2 routes to
         * Lfo::modTarget2 while pair 1 keeps using Lfo::modTarget.
         */
        {
            uint8_t target_voice = 1u;
            uint8_t target_pair =
                (descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_PARAM_2)
                    ? 1u : 0u;
            instrument_binding_kind_t voice_kind = target_pair
                ? INSTRUMENT_BIND_LFO_TARGET_VOICE_2
                : INSTRUMENT_BIND_LFO_TARGET_VOICE;
            const kit_instrument_slot_t *source =
                scene_instrumentSlotConst(scene_getActiveIndex(), slot);
            uint8_t voice_index;
            if (source &&
                instrumentManager_descriptorIndexForBinding(
                    source->type, voice_kind, &voice_index)) {
                target_voice =
                    source->parameter_images.instrument_parameters[voice_index];
            }
            if (!instrumentManager_lfoTargetVoiceValid(target_voice))
                target_voice = 1u;
            return instrumentManager_installLfoModulationTarget(
                slot, target_pair,
                instrumentManager_lfoTargetIdFromToken(
                    scene_getActiveIndex(), slot, target_voice, value,
                    INSTRUMENT_TARGET_MODULATION));
        }

    default:
        return 0u;
    }
}

uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       instrument_param_value_t value)
{
    return instrumentManager_writeRuntimeInternal(slot, descriptor, value, 1u);
}
