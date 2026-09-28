/*
 * Core/DSP/Effects/EffectsManager.c
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "EffectsManager.h"
#include "menu.h"
#include "MenuText.h"
#include "EffectParamRows.h"
#include "SceneData.h"
#include "StereoFilterParameters.h"
#include "StereoFilterEffect.h"
#include <string.h>

_Static_assert(EFFECT_TYPE_OFF == FXBUF_EFFECT_TYPE_NONE,
               "registry id 0 must be FxBuffer's off id");
_Static_assert(EFFECT_TYPE_COUNT <= 255u, "Effect type ids must fit in a byte");

/*
 * Built-in `off` type.
 *
 * It has the common rows and the mandatory Morph lane but no DSP operations.
 * The mixer will therefore publish its common values while the Step 5 bus
 * skips processing and return audio. There is no filesystem type folder.
 */
static const effect_param_descriptor_t effects_off_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
};

/*
 * Manager-owned type runtime.
 *
 * The union is zero-initialized in DTCM and only its active member has meaning.
 * It is deliberately separate from the FxBuffer audio arena: this state is
 * DSP control state, not audio storage, and may be cleared during a type
 * switch without violating the arena handoff contract.
 */
typedef union {
    StereoFilterRuntime stereo_filter;
} effects_runtime_t;

static INDTCMZ effects_runtime_t effects_runtime;

/*
 * Registry table, indexed by the append-only Effect type id.
 *
 * The three common descriptors always occupy indices 0..2. Lane 0 names the
 * retained Scene Effect Morph source; the remaining lanes are type-specific
 * descriptor indices. Tokens, not numeric ids, are the persisted identity.
 */
static const effect_registry_entry_t effects_registry[EFFECT_TYPE_COUNT] = {
    {
        "off", "Off  ", "Off",
        0u,
        effects_off_descriptors,
        (uint8_t)(sizeof(effects_off_descriptors) /
                  sizeof(effects_off_descriptors[0])),
        { EFFECT_LANE_MORPH_SOURCE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        NULL, NULL, NULL,
        0u, 0u, 0u
    },
    {
        stereoFilter_token3, stereoFilter_abbrev5, stereoFilter_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        stereoFilter_descriptors, STEREO_FILTER_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          STEREO_FILTER_PARAM_FREQ, STEREO_FILTER_PARAM_RESO,
          STEREO_FILTER_PARAM_DRIVE, STEREO_FILTER_PARAM_TYPE,
          STEREO_FILTER_PARAM_LEVEL, STEREO_FILTER_PARAM_PAN,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        NULL, &stereoFilter_ops, NULL,
        (uint16_t)sizeof(StereoFilterRuntime), 0u, 0u
    },
};

_Static_assert(sizeof(StereoFilterRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold StereoFilter");

/*
 * Manager-owned runtime state.
 *
 * The state is SRAM1-resident and exactly 76 bytes: active runtime type and
 * Scene, a force-write flag, one last-applied byte per common type domain, and
 * the common return settings consumed by the later FX bus.
 */
typedef struct {
    effect_type_id_t runtime_type;
    uint8_t scene_index;
    uint8_t force_all;
    uint8_t reserved;
    uint8_t last_applied[EFFECT_PARAM_COUNT];
    effects_common_runtime_t common;
} effects_state_t;

_Static_assert(sizeof(effects_state_t) == 76u,
               "effects_state_t size is recorded in SRAM_MANIFEST.md");

static effects_state_t effects_state;

#if DEV_MODE_DIAGNOSTIC
static uint8_t effects_registryCheckCode;
#endif

/*
 * Interpolate one retained endpoint pair.
 *
 * This is the same rounded 0..255 interpolation contract used by the voice
 * Morph engine. Keeping the arithmetic here avoids exporting voice-engine
 * internals while ensuring Effect Morph has identical endpoint behavior.
 */
static uint8_t effects_interpolate(uint8_t normal, uint8_t morph,
                                   uint8_t amount)
{
    int32_t numerator;

    if (amount == 0u)
        return normal;
    if (amount == 255u)
        return morph;
    numerator = (int32_t)normal * 255 +
                ((int32_t)morph - (int32_t)normal) * amount + 127;
    if (numerator < 0)
        return 0u;
    return (uint8_t)(numerator / 255);
}

uint8_t effect_expand7Linear(uint8_t value7)
{
    /* Preserve the 127 -> 255 endpoint used by voice Morph automation. */
    return (value7 < 127u) ? (uint8_t)(value7 * 2u) : 255u;
}

uint8_t effects_registryCount(void)
{
    return EFFECT_TYPE_COUNT;
}

const effect_registry_entry_t *effects_registryEntry(effect_type_id_t type)
{
    return (type < EFFECT_TYPE_COUNT) ? &effects_registry[type] : NULL;
}

const char *effects_typeToken(effect_type_id_t type)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);

    return entry ? entry->token3 : NULL;
}

uint8_t effects_typeFromToken(const char token[3], effect_type_id_t *type_out)
{
    effect_type_id_t type;

    /* The registry is intentionally tiny; exact three-byte comparison is clear. */
    if (!token)
        return 0u;
    for (type = 0u; type < EFFECT_TYPE_COUNT; type++) {
        if (memcmp(effects_registry[type].token3, token, 3u) == 0) {
            if (type_out)
                *type_out = type;
            return 1u;
        }
    }
    return 0u;
}

const effect_param_descriptor_t *effects_descriptor(effect_type_id_t type,
                                                    uint8_t index)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);

    return (entry && index < entry->descriptor_count) ?
           &entry->descriptors[index] : NULL;
}

const effect_param_descriptor_t *effects_descriptorByKey(
    effect_type_id_t type, const char *file_key, uint8_t *index_out)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t index;

    if (!entry || !file_key)
        return NULL;
    for (index = 0u; index < entry->descriptor_count; index++) {
        if (strcmp(entry->descriptors[index].base.file_key, file_key) == 0) {
            if (index_out)
                *index_out = index;
            return &entry->descriptors[index];
        }
    }
    return NULL;
}

uint8_t effects_paramAutomatable(effect_type_id_t type, uint8_t index)
{
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);

    /* Local 63 is the automation-off alias; WIDE8 requires expand7. */
    if (!descriptor || index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
        return 0u;
    if ((descriptor->base.flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) == 0u)
        return 0u;
    if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) != 0u &&
        !descriptor->expand7)
        return 0u;
    return 1u;
}

uint8_t effects_paramModulatable(effect_type_id_t type, uint8_t index)
{
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);

    return (uint8_t)(descriptor &&
        (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u &&
        descriptor->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE);
}

/*
 * Build one complete type-default Effect record for a storage transaction.
 *
 * Inputs: caller-owned record and registry id. Output: SceneData's `off`
 * defaults, then this type's token and type-specific defaults in both normal
 * and Morph images; unused descriptor cells remain zero. Unknown ids leave
 * the valid `off` record. No resident Scene or AutoSave state is touched.
 * Affiliates: storageTypes `.fx` parsing, AutoSave, and the boot reader.
 */
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t index;

    if (!record)
        return;
    scene_effectRecordDefaults(record);
    if (!entry)
        return;
    record->type = type;
    for (index = EFFECT_COMMON_PARAM_COUNT; index < EFFECT_PARAM_COUNT;
         index++) {
        uint8_t value = (index < entry->descriptor_count) ?
                        entry->descriptors[index].default_value : 0u;

        record->normal[index] = value;
        record->morph[index] = value;
    }
}

/*
 * Resolve one retained FX-sequence lane to its `.fx` file key.
 *
 * Inputs: registry type and lane number. Output: the fixed Effect Morph key,
 * a descriptor key, or NULL for an unused lane. The registry remains the only
 * owner of lane meaning, so storage and future UI cannot drift apart.
 */
const char *effects_laneFileKey(effect_type_id_t type, uint8_t lane)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t index;

    if (!entry || lane >= EFFECT_SEQ_LANE_COUNT)
        return NULL;
    index = entry->lanes[lane];
    if (index == EFFECT_LANE_MORPH_SOURCE)
        return EFFECT_LANE_MORPH_FILE_KEY;
    if (index == EFFECT_LANE_NONE || index >= entry->descriptor_count)
        return NULL;
    return entry->descriptors[index].base.file_key;
}

/*
 * Resolve a `.fx` lane key to the registry's retained lane number.
 *
 * Inputs: registry type, key after the `lane.` prefix, and optional output
 * pointer. Output: nonzero on a named lane match; unused lanes and unknown
 * keys return zero. The bounded 16-entry scan runs only during file parsing.
 */
uint8_t effects_laneByFileKey(effect_type_id_t type, const char *file_key,
                              uint8_t *lane_out)
{
    uint8_t lane;

    if (!file_key)
        return 0u;
    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        const char *key = effects_laneFileKey(type, lane);

        if (key && strcmp(key, file_key) == 0) {
            if (lane_out)
                *lane_out = lane;
            return 1u;
        }
    }
    return 0u;
}

#if DEV_MODE_DIAGNOSTIC
/*
 * Validate registry invariants that the compiler cannot express.
 *
 * Return 0 for pass, otherwise the first compact diagnostic code. The boot
 * diagnostic displays this value as the final hex digit of the FxBf screen.
 * Pure: reads immutable tables only; it adds no runtime behavior or storage
 * outside the diagnostic-only result byte.
 */
static uint8_t effects_registrySelfCheck(void)
{
    static const char *const common_keys[EFFECT_COMMON_PARAM_COUNT] = {
        "effect_audio_out", "effect_level", "effect_pan"
    };
    effect_type_id_t type;
    effect_type_id_t previous;

    for (type = 0u; type < EFFECT_TYPE_COUNT; type++) {
        const effect_registry_entry_t *entry = &effects_registry[type];
        uint64_t seen = 0u;
        uint8_t index;
        uint8_t lane;

        if (entry->descriptor_count < EFFECT_COMMON_PARAM_COUNT ||
            entry->descriptor_count > EFFECT_PARAM_COUNT)
            return 1u;
        for (index = 0u; index < EFFECT_COMMON_PARAM_COUNT; index++)
            if (strcmp(entry->descriptors[index].base.file_key,
                       common_keys[index]) != 0)
                return 2u;
        for (index = 0u; index < entry->descriptor_count; index++) {
            const effect_param_descriptor_t *descriptor =
                &entry->descriptors[index];
            uint8_t automatable = (uint8_t)(
                (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE) != 0u);

            if (automatable && index >= EFFECT_TARGET_PATTERN_LOCAL_LIMIT)
                return 3u;
            if (automatable &&
                (descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) != 0u &&
                !descriptor->expand7)
                return 4u;
            if (descriptor->default_value > descriptor->max_value)
                return 7u;
        }
        if (entry->lanes[0] != EFFECT_LANE_MORPH_SOURCE)
            return 5u;
        for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
            uint8_t descriptor_index = entry->lanes[lane];

            if (descriptor_index == EFFECT_LANE_NONE)
                continue;
            if (descriptor_index >= entry->descriptor_count ||
                (seen & ((uint64_t)1u << descriptor_index)) != 0u)
                return 6u;
            seen |= (uint64_t)1u << descriptor_index;
        }
        if (entry->runtime_bytes > sizeof(effects_runtime_t))
            return 8u;
        if (strlen(entry->token3) != 3u || strlen(entry->abbrev5) != 5u ||
            strlen(entry->full8) > 8u)
            return 9u;
        for (previous = 0u; previous < type; previous++)
            if (memcmp(effects_registry[previous].token3, entry->token3, 3u) == 0)
                return 10u;
    }
    return 0u;
}

uint8_t effects_registryCheckResult(void)
{
    return effects_registryCheckCode;
}
#endif

/* Return the manager union as the opaque runtime pointer required by ops. */
static void *effects_runtimeMember(void)
{
    return &effects_runtime;
}

/*
 * Switch the live runtime type through the FxBuffer handoff.
 *
 * The outgoing type exports its arena state, the manager clears only its
 * control union, and the incoming type initializes from the handoff. The next
 * effects_service() pass forces every descriptor into the fresh runtime.
 */
static void effects_switchRuntime(effect_type_id_t incoming)
{
    const effect_registry_entry_t *old_entry =
        effects_registryEntry(effects_state.runtime_type);
    const effect_registry_entry_t *new_entry = effects_registryEntry(incoming);
    fxbuf_handoff_t *handoff;

    if (!new_entry) {
        incoming = EFFECT_TYPE_OFF;
        new_entry = effects_registryEntry(EFFECT_TYPE_OFF);
    }
    handoff = fxbuf_handoffBeginExit();
    handoff->effect_type = effects_state.runtime_type;
    handoff->effect_channels = (old_entry &&
        (old_entry->io_flags & EFFECT_IO_STEREO_OUT) != 0u) ? 2u :
        ((old_entry && old_entry->io_flags != 0u) ? 1u : 0u);
    if (old_entry && old_entry->ops && old_entry->ops->export_handoff)
        old_entry->ops->export_handoff(effects_runtimeMember(), handoff);
    memset(&effects_runtime, 0, sizeof(effects_runtime));
    effects_state.runtime_type = incoming;
    if (new_entry && new_entry->ops && new_entry->ops->init)
        new_entry->ops->init(effects_runtimeMember(), fxbuf_handoff());
    effects_state.force_all = 1u;
}

/* Forward the changed share to the active type and force a re-resolution. */
static void effects_onShareChanged(const fx_share_t *share)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    if (entry && entry->ops && entry->ops->buffer_changed)
        entry->ops->buffer_changed(effects_runtimeMember(), share);
    effects_state.force_all = 1u;
}

void effects_init(void)
{
    /* Boot init runs after FxBuffer and InstrumentManager, before activation. */
    memset(&effects_state, 0, sizeof(effects_state));
    effects_state.runtime_type = EFFECT_TYPE_OFF;
    effects_state.scene_index = scene_getActiveIndex();
    effects_state.common.route = EFFECT_COMMON_DEFAULT_AUDIO_OUT;
    effects_state.common.pan = EFFECT_COMMON_DEFAULT_PAN;
    effects_state.common.level = EFFECT_COMMON_DEFAULT_LEVEL / 127.0f;
    effects_state.force_all = 1u;
    fxbuf_setShareChangedCallback(effects_onShareChanged);
#if DEV_MODE_DIAGNOSTIC
    effects_registryCheckCode = effects_registrySelfCheck();
#endif
}

void effects_activateScene(uint8_t scene_index)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    effect_type_id_t type;

    /* Same type keeps tails; a different/unknown type enters through handoff. */
    if (!record)
        return;
    type = effects_registryEntry(record->type) ? record->type : EFFECT_TYPE_OFF;
    effects_state.scene_index = scene_index;
    if (type != effects_state.runtime_type)
        effects_switchRuntime(type);
    effects_state.force_all = 1u;
}

uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    effect_record_t *record;
    uint8_t index;

    /* Type changes preserve common rows, sequence settings, and Effect Morph. */
    if (!entry)
        return 0u;
    record = scene_effectRecordForWholeCommit(scene_index);
    if (!record || record->type == type)
        return 0u;
    record->type = type;
    for (index = EFFECT_COMMON_PARAM_COUNT; index < EFFECT_PARAM_COUNT; index++) {
        uint8_t value = (index < entry->descriptor_count) ?
                        entry->descriptors[index].default_value : 0u;

        record->normal[index] = value;
        record->morph[index] = value;
    }
    memset(record->steps, 0, sizeof(record->steps));
    scene_finishEffectWholeCommit(scene_index);
    if (scene_index == effects_state.scene_index)
        effects_switchRuntime(type);
    return 1u;
}

void effects_service(void)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);
    fx_share_t share;
    uint8_t morph;
    uint8_t index;

    /* Full descriptor rescan avoids requiring every retained writer to notify. */
    if (!record || !entry)
        return;
    morph = scene_getEffectMorphAmount(effects_state.scene_index);
    fxbuf_effectShare(&share);
    for (index = 0u; index < entry->descriptor_count; index++) {
        const effect_param_descriptor_t *descriptor =
            &entry->descriptors[index];
        uint8_t value = (descriptor->base.flags &
                         INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u ?
            effects_interpolate(record->normal[index], record->morph[index], morph) :
            record->normal[index];

        /* Pattern, sequence-lock, and LFO layers are added in later steps. */
        if (value > descriptor->max_value)
            value = descriptor->max_value;
        if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) != 0u &&
            entry->ops && entry->ops->effective_max) {
            uint8_t limit = entry->ops->effective_max(index, &share);

            if (value > limit)
                value = limit;
        }
        if (!effects_state.force_all &&
            value == effects_state.last_applied[index])
            continue;
        effects_state.last_applied[index] = value;
        if (index == EFFECT_COMMON_PARAM_AUDIO_OUT)
            effects_state.common.route = value;
        else if (index == EFFECT_COMMON_PARAM_LEVEL)
            effects_state.common.level = value / 127.0f;
        else if (index == EFFECT_COMMON_PARAM_PAN)
            effects_state.common.pan = value;
        else if (entry->ops && entry->ops->write_param)
            entry->ops->write_param(effects_runtimeMember(), index, value);
    }
    effects_state.force_all = 0u;
}

void effects_process(effect_io_t *io)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    /* Step 5 calls this after effects_service(); `off` has no process hook. */
    if (entry && entry->ops && entry->ops->process && io)
        entry->ops->process(effects_runtimeMember(), io);
}

effect_type_id_t effects_activeType(void)
{
    return effects_state.runtime_type;
}

uint8_t effects_activeIoFlags(void)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    return entry ? entry->io_flags : 0u;
}

const effects_common_runtime_t *effects_commonRuntime(void)
{
    return &effects_state.common;
}
