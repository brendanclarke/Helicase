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
#include "BankData.h"
#include "sequencer.h"
#include "StepScale.h"
#include "StereoFilterParameters.h"
#include "StereoFilterEffect.h"
/* S074: CrumpBit (`cbt`), the first buffer-using Effect type. */
#include "CrumpBitParameters.h"
#include "CrumpBitEffect.h"
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
    /* S074: 56 B. Its audio lives in the FxBuffer share, not in this union. */
    CrumpBitRuntime crump_bit;
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
    /*
     * CrumpBit (`cbt`, S074): stereo in and out. The 8-bit mono tape loop
     * uses CRUMPBIT_BUFFER_BYTES of the FxBuffer Effect share (min = pref;
     * the fixed range fits the minimum share). Lanes 1..9: bit off, bit
     * invert, mix, feedback, rate, sync, vol, pan, delay pan. Lane 0 is
     * Effect Morph.
     */
    {
        crumpBit_token3, crumpBit_abbrev5, crumpBit_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        crumpBit_descriptors, CRUMPBIT_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
          CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
          CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
          CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
          CRUMPBIT_PARAM_DLY_PAN,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        &crumpBit_layout, &crumpBit_ops, &crumpBit_ui,
        (uint16_t)sizeof(CrumpBitRuntime),
        CRUMPBIT_BUFFER_BYTES, CRUMPBIT_BUFFER_BYTES
    },
};

_Static_assert(sizeof(StereoFilterRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold StereoFilter");
_Static_assert(sizeof(CrumpBitRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold CrumpBit");
/*
 * RAM guard (S074): the DTCM union is 76 B (STORAGE_SRAM_MANIFEST.md). A
 * larger member grows .dtcmz and shrinks the FxBuffer arena by the same
 * amount, which needs RAM approval before it is merged.
 */
_Static_assert(sizeof(effects_runtime_t) == 76u,
               "Effect runtime union size changed: RAM approval required");

/*
 * Manager-owned runtime state.
 *
 * The state is SRAM1-resident and exactly 84 bytes: active runtime type and
 * Scene, a force-write flag, one last-applied byte per common type domain,
 * live FX step/selection state, the held Morph lock, and the common return
 * settings consumed by the mixer's FX return. The spare `reserved` byte keeps
 * the 84-byte layout.
 */
typedef struct {
    effect_type_id_t runtime_type;
    uint8_t scene_index;
    uint8_t force_all;
    uint8_t reserved;
    uint8_t last_applied[EFFECT_PARAM_COUNT];
    uint8_t seq_step;
    uint8_t seq_step_valid;
    uint8_t seq_sel_step;
    uint8_t held_morph;
    uint8_t held_morph_valid;
    uint8_t seq_serial;
    effects_common_runtime_t common;
} effects_state_t;

_Static_assert(sizeof(effects_state_t) == 84u,
               "effects_state_t size is recorded in STORAGE_SRAM_MANIFEST.md");
_Static_assert(STEP_SCALE_COUNT == EFFECT_SEQ_SCALE_COUNT &&
               STEP_SCALE_DEFAULT == EFFECT_SEQ_SCALE_DEFAULT,
               "Pattern and FX scale contracts must remain identical");

static effects_state_t effects_state;

/*
 * Pattern-automation and LFO overlay state (Session 072 step 9; plan §9,
 * §10.2-10.3, §16.1 items 4-5).
 *
 * active/value/owner hold one Pattern overlay per Effect-local parameter.
 * pending_end/pending_track bracket the open FX step-marker group. owner_tracks
 * is mirrored to Sequencer so TIM3 queues markers only for owning tracks.
 * morph_override is the Pattern `fxm` base layer. lfo stores one
 * base-independent direction/depth contribution per source slot and pair.
 * Lifetime: SRAM1, foreground-only writers; retained SceneData is untouched.
 */
typedef struct {
    uint8_t target;
    uint8_t direction;
    uint8_t depth;
} effects_lfo_entry_t;

#define EFFECT_AUTOMATION_TRACK_NONE  0xFFu
#define EFFECT_AUTOMATION_TRACK_LIMIT 8u

typedef struct {
    uint64_t active;
    uint64_t pending_end;
    uint8_t value[EFFECT_PARAM_COUNT];
    uint8_t owner[EFFECT_PARAM_COUNT];
    effects_lfo_entry_t lfo[INSTRUMENT_SLOT_COUNT][2];
    uint8_t pending_track;
    uint8_t owner_tracks;
    uint8_t morph_override;
    uint8_t morph_override_valid;
} effects_automation_t;

_Static_assert(sizeof(effects_automation_t) == 184u,
               "effects_automation_t size is recorded in STORAGE_SRAM_MANIFEST.md");

static effects_automation_t effects_automation;

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

/* Resolve the retained Effect type of one resident Scene. */
static effect_type_id_t effects_sceneType(uint8_t scene_index)
{
    const effect_record_t *record = scene_effectConst(scene_index);

    return (record && effects_registryEntry(record->type))
        ? record->type : EFFECT_TYPE_OFF;
}

/* Apply the use-specific capability rule to one Effect-local index. */
static uint8_t effects_localValid(effect_type_id_t type, uint8_t local,
                                  instrument_target_use_t use)
{
    return (use == INSTRUMENT_TARGET_AUTOMATION)
        ? effects_paramAutomatable(type, local)
        : effects_paramModulatable(type, local);
}

/* Validate one block-7 target against a viewed Scene's Effect type. */
uint8_t effects_targetValid(uint8_t scene_index, uint16_t id,
                            instrument_target_use_t use)
{
    if (!effectTarget_isEffectId(id))
        return 0u;
    return effects_localValid(effects_sceneType(scene_index),
                              effectTarget_local(id), use);
}

/* Return a valid Effect descriptor for display/edit clients. */
const effect_param_descriptor_t *effects_targetDescriptor(
    uint8_t scene_index, uint16_t id, instrument_target_use_t use)
{
    if (!effects_targetValid(scene_index, id, use))
        return NULL;
    return effects_descriptor(effects_sceneType(scene_index),
                              effectTarget_local(id));
}

/* Walk a Scene's valid Effect targets in registry descriptor order. */
uint16_t effects_stepTarget(uint8_t scene_index, uint16_t current,
                            int8_t direction, instrument_target_use_t use)
{
    effect_type_id_t type = effects_sceneType(scene_index);
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    int16_t local;

    if (!entry || direction == 0)
        return current;
    if (!effectTarget_isEffectId(current)) {
        if (direction < 0)
            return INSTRUMENT_PARAM_INVALID;
        local = -1;
    } else {
        local = (int16_t)effectTarget_local(current);
    }
    for (;;) {
        local = (int16_t)(local + ((direction > 0) ? 1 : -1));
        if (local < 0)
            return INSTRUMENT_PARAM_INVALID;
        if (local >= (int16_t)entry->descriptor_count)
            return effectTarget_isEffectId(current)
                ? current : INSTRUMENT_PARAM_INVALID;
        if (effects_localValid(type, (uint8_t)local, use))
            return effectTarget_id((uint8_t)local);
    }
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

/*
 * Report whether one descriptor row owns a Morph endpoint.
 *
 * Inputs: registry type and descriptor index. Output: nonzero for rows with
 * INSTRUMENT_PARAM_FLAG_MORPHABLE. Clients: the edit API image rule and the
 * Effect page's SHIFT Morph view.
 */
uint8_t effects_paramMorphable(effect_type_id_t type, uint8_t index)
{
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);

    return (uint8_t)(descriptor &&
        (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u);
}

/*
 * Read one retained Effect endpoint (contract in EffectsManager.h).
 *
 * Output: the Morph cell for a Morphable row in the Morph image, otherwise
 * the normal cell; 0 for an invalid Scene. Indices beyond the type's rows
 * read their stored unused byte, which remains a valid 0..255 value.
 */
uint8_t effects_getParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image)
{
    const effect_record_t *record = scene_effectConst(scene_index);

    if (!record || index >= EFFECT_PARAM_COUNT)
        return 0u;
    if (image == EFFECT_IMAGE_MORPH &&
        effects_paramMorphable(record->type, index))
        return record->morph[index];
    return record->normal[index];
}

/*
 * Resolve which resident Scenes one Effect edit reaches (plan §7.4, A44).
 *
 * An active-Scene edit adds that Scene's VOICE edit mask and always keeps the
 * origin. Parameter, sequence, and lane edits then remove masked Scenes whose
 * Effect type differs, protecting local descriptor/lane indices until the
 * layout gate and revalidation have repaired every mask. Inactive edits reach
 * only their origin, matching VOICE editing. Foreground-only; no SRAM.
 */
static uint16_t effects_fanoutMask(uint8_t scene_index, uint8_t match_type)
{
    const effect_record_t *origin = scene_effectConst(scene_index);
    uint16_t mask;
    uint8_t s;

    if (!origin || scene_index >= SCENE_COUNT)
        return 0u;
    mask = (uint16_t)(1u << scene_index);
    /*
     * S075: every Scene owns a directional VOICE edit-mask entry. The active
     * Scene keeps the self-repairing UI accessor; an inactive Scene must use
     * its indexed entry so an Effect paste cannot collapse to the audible
     * Scene.
     */
    if (scene_index == scene_getActiveIndex())
        mask = (uint16_t)(mask | bank_sceneMaskVoiceEdit());
    else
        mask = (uint16_t)(mask | bank_sceneMaskVoiceEditForScene(scene_index));
    if (!match_type)
        return mask;
    for (s = 0u; s < SCENE_COUNT; s++) {
        const effect_record_t *record;

        if (s == scene_index || (mask & (uint16_t)(1u << s)) == 0u)
            continue;
        record = scene_effectConst(s);
        if (!record || record->type != origin->type)
            mask = (uint16_t)(mask & (uint16_t)~(1u << s));
    }
    return mask;
}

/*
 * Write one retained Effect endpoint (contract in EffectsManager.h).
 *
 * Inputs: Scene, descriptor index, image, and value. Output: nonzero when the
 * retained byte changed. Rows outside the Scene's type are rejected, values
 * clamp to the descriptor maximum, and non-Morphable Morph writes land on
 * the single normal value. Single-Scene worker; the public wrapper below
 * performs the edit-mask fan-out.
 */
static uint8_t effects_setParameterScene(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    const effect_param_descriptor_t *descriptor;
    uint8_t before;

    if (!record)
        return 0u;
    descriptor = effects_descriptor(record->type, index);
    if (!descriptor)
        return 0u;
    if (value > descriptor->max_value)
        value = descriptor->max_value;
    if (image == EFFECT_IMAGE_MORPH &&
        (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u) {
        before = record->morph[index];
        scene_setEffectMorphParameter(scene_index, index, value);
        return (uint8_t)(record->morph[index] != before);
    }
    before = record->normal[index];
    scene_setEffectNormalParameter(scene_index, index, value);
    return (uint8_t)(record->normal[index] != before);
}

uint8_t effects_setParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Fan out one retained row to every same-type masked Scene. Each worker
     * clamps against its own descriptor and SceneData marks its own AutoSave
     * cell/card-clean state; the active runtime is rescanned by service.
     */
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setParameterScene(s, index, image, value);
    }
    return changed;
}

/*
 * Sequence-setting and Effect Morph setters (contract in EffectsManager.h).
 *
 * Each compares the retained byte around SceneData's normalizing setter, so
 * callers learn whether a repaint or LED refresh is needed.
 */
static uint8_t effects_setSeqRunModeScene(uint8_t scene_index, uint8_t mode)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t before;

    if (!record)
        return 0u;
    before = record->seq_run_mode;
    scene_setEffectSeqRunMode(scene_index, mode);
    return (uint8_t)(record->seq_run_mode != before);
}

static uint8_t effects_setSeqLengthScene(uint8_t scene_index, uint8_t length)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t before;

    if (!record)
        return 0u;
    before = record->seq_length;
    scene_setEffectSeqLength(scene_index, length);
    return (uint8_t)(record->seq_length != before);
}

static uint8_t effects_setSeqStepScaleScene(uint8_t scene_index, uint8_t scale)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t before;

    if (!record)
        return 0u;
    before = record->seq_step_scale;
    scene_setEffectSeqStepScale(scene_index, scale);
    return (uint8_t)(record->seq_step_scale != before);
}

static uint8_t effects_setMorphAmountScene(uint8_t scene_index, uint8_t amount)
{
    uint8_t before = scene_getEffectMorphAmount(scene_index);

    scene_setEffectMorphAmount(scene_index, amount);
    /*
     * S076 Rule A: a non-automation retained write supersedes the Pattern
     * `fxm` Morph override for the active Scene. The service loop reads
     * morph_override_valid every block; once cleared it uses the retained
     * Scene Effect Morph amount for all LFO and base resolution.
     * Input: active Scene guard against effects_state.scene_index (the
     * runtime's own active Scene, which tracks scene_getActiveIndex()).
     * Output: effects_automation.morph_override_valid = 0.
     * Callers (upstream): effects_setMorphAmount() fan-out, called by
     * menu_commitEffectMorphParam() and clearOps.c. Affiliate:
     * effects_setMorphAutomation() (the automation setter).
     */
    if (scene_index == effects_state.scene_index)
        effects_clearMorphAutomationOverride();
    return (uint8_t)(scene_getEffectMorphAmount(scene_index) != before);
}

/* Sequence-setting fan-out: run, length, and scale share the same-type mask. */
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setSeqRunModeScene(s, mode);
    }
    return changed;
}

uint8_t effects_setSeqLength(uint8_t scene_index, uint8_t length)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setSeqLengthScene(s, length);
    }
    return changed;
}

uint8_t effects_setSeqStepScale(uint8_t scene_index, uint8_t scale)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setSeqStepScaleScene(s, scale);
    }
    return changed;
}

/* Effect Morph is type-agnostic, so its retained amount reaches the mask. */
uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount)
{
    uint16_t mask = effects_fanoutMask(scene_index, 0u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setMorphAmountScene(s, amount);
    }
    return changed;
}

/* Clamp a retained FX sequence length into the live 1..16 domain. */
static uint8_t effects_seqLength(const effect_record_t *record)
{
    uint8_t len = record->seq_length;

    return (len < EFFECT_SEQ_LENGTH_MIN || len > EFFECT_SEQ_LENGTH_MAX)
        ? EFFECT_SEQ_LENGTH_DEFAULT : len;
}

/* Latch a Morph-lane lock as the held Effect Morph base (plan §9). */
static void effects_seqLatchMorph(const effect_record_t *record, uint8_t step)
{
    const effect_seq_step_t *entry = &record->steps[step];

    if ((entry->lock_mask & (uint16_t)(1u << EFFECT_SEQ_LANE_MORPH)) != 0u) {
        effects_state.held_morph = entry->value[EFFECT_SEQ_LANE_MORPH];
        effects_state.held_morph_valid = 1u;
    }
}

/*
 * Consume the one-byte TIM3 FX latch in foreground context.
 *
 * RESET invalidates the previous step and clears the held Morph lane. STEP
 * records the newest position and latches its Morph lock. RESET and STEP may
 * be combined by a start/reset boundary, so RESET is intentionally handled
 * first. No DSP, LCD, or physical LED work is performed here.
 */
static void effects_seqConsume(const effect_record_t *record)
{
    uint8_t event = seq_fxTakeEvent();

    if (event == 0u)
        return;
    if ((event & SEQ_FX_EVENT_RESET) != 0u) {
        effects_state.seq_step_valid = 0u;
        effects_state.held_morph_valid = 0u;
    }
    if ((event & SEQ_FX_EVENT_STEP) != 0u) {
        /* A queued clock event cannot create a selection after a mode change. */
        if (record->seq_run_mode != EFFECT_SEQ_RUN_SEL) {
            effects_state.seq_step = (uint8_t)(event & SEQ_FX_EVENT_INDEX);
            effects_state.seq_step_valid = 1u;
            effects_seqLatchMorph(record,
                                   (uint8_t)(effects_state.seq_step %
                                             effects_seqLength(record)));
        }
    }
    effects_state.seq_serial++;
}

/* Return the active retained step, or the public NONE sentinel. */
static uint8_t effects_seqStepFor(const effect_record_t *record)
{
    uint8_t len = effects_seqLength(record);

    /* `sel` always applies; seq_step_valid belongs to the clock step only. */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        return (uint8_t)(effects_state.seq_sel_step % len);
    if (!seq_isRunning() || !effects_state.seq_step_valid)
        return EFFECT_SEQ_STEP_NONE;
    return (uint8_t)(effects_state.seq_step % len);
}

/* Replace a descriptor value with a locked value from the active step. */
static uint8_t effects_seqOverride(const effect_registry_entry_t *entry,
                                   const effect_seq_step_t *step,
                                   uint8_t index, uint8_t *value)
{
    uint8_t lane;

    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (entry->lanes[lane] == index &&
            (step->lock_mask & (uint16_t)(1u << lane)) != 0u) {
            *value = step->value[lane];
            return 1u;
        }
    }
    return 0u;
}

uint8_t effects_seqActiveStep(void)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);

    return record ? effects_seqStepFor(record) : EFFECT_SEQ_STEP_NONE;
}

uint8_t effects_seqSelectedStep(void)
{
    /* The `sel` cursor is always defined; LED callers gate on run mode. */
    return effects_state.seq_sel_step;
}

/* Select one retained step immediately; `sel` applies it whether or not the
 * transport runs. The clock-step validity flag is not touched. */
void effects_seqSelect(uint8_t step)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);

    if (!record || step >= effects_seqLength(record))
        return;
    effects_state.seq_sel_step = step;
    /*
     * An unlocked selection keeps the held Morph value; a Morph lock on the
     * selected step replaces it. Service still re-latches after RESET/Scene
     * switch, so this path preserves the held-lane rule in `sel`.
     */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        effects_seqLatchMorph(record, step);
    effects_state.seq_serial++;
}

/* Return the foreground-visible sequence signature generation. */
uint8_t effects_seqSerial(void)
{
    return effects_state.seq_serial;
}

/* Resolve one type-specific descriptor index to its retained lane number. */
uint8_t effects_laneOfParam(effect_type_id_t type, uint8_t index,
                            uint8_t *lane_out)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t lane;

    if (!entry)
        return 0u;
    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (entry->lanes[lane] == index) {
            if (lane_out)
                *lane_out = lane;
            return 1u;
        }
    }
    return 0u;
}

/* Read one retained lane value and whether its lock bit is set. */
uint8_t effects_getLaneLock(uint8_t scene_index, uint8_t step, uint8_t lane,
                            uint8_t *value_out)
{
    const effect_record_t *record = scene_effectConst(scene_index);

    if (!record || step >= EFFECT_SEQ_STEP_COUNT ||
        lane >= EFFECT_SEQ_LANE_COUNT)
        return 0u;
    if (value_out)
        *value_out = record->steps[step].value[lane];
    return (uint8_t)((record->steps[step].lock_mask &
                      (uint16_t)(1u << lane)) != 0u);
}

/* Write-and-lock one lane across the physically held steps of one Scene. */
static uint8_t effects_setSeqLaneLockScene(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t step;
    uint8_t changed = 0u;

    if (!record || lane >= EFFECT_SEQ_LANE_COUNT ||
        !effects_laneFileKey(record->type, lane))
        return 0u;
    if (lane != EFFECT_SEQ_LANE_MORPH) {
        const effect_registry_entry_t *entry =
            effects_registryEntry(record->type);
        const effect_param_descriptor_t *descriptor = entry
            ? effects_descriptor(record->type, entry->lanes[lane]) : NULL;

        if (descriptor && value > descriptor->max_value)
            value = descriptor->max_value;
    }
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        const effect_seq_step_t *entry = &record->steps[step];
        uint16_t bit = (uint16_t)(1u << lane);

        if ((step_mask & (uint16_t)(1u << step)) == 0u)
            continue;
        if (entry->value[lane] != value || (entry->lock_mask & bit) == 0u)
            changed = 1u;
        scene_setEffectSeqLaneValue(scene_index, step, lane, value);
        scene_setEffectSeqLaneLocked(scene_index, step, lane, 1u);
    }
    if (changed)
        effects_state.seq_serial++;
    return changed;
}

uint8_t effects_setSeqLaneLock(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value)
{
    uint16_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Fan out the held-step mask, lane, and value to same-type Scenes. Registry
     * lane meaning is shared by equal types, while each worker applies its own
     * descriptor clamp and retained SceneData dirty markers.
     */
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_setSeqLaneLockScene(s, step_mask, lane, value);
    }
    return changed;
}

/*
 * Fan-out Effect record and FX sequence operations for copy/clear (S075).
 *
 * Contract in EffectsManager.h. effects_pasteRecord(): the mask is computed
 * against the destination's *current* Effect type (members share it through
 * the layout gate), then every member receives the whole source record, so a
 * paste may change the type of the destination and its members (`copy
 * effect` copies the record, user B10). A member whose record is the source
 * itself is skipped (the source is read live, never copied onto itself).
 */
uint16_t effects_pasteRecord(uint8_t dst_scene, const effect_record_t *src)
{
    const effect_record_t *dst = scene_effectConst(dst_scene);
    uint16_t mask;
    uint8_t member;

    if (!dst || !src || dst_scene >= SCENE_COUNT)
        return 0u;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_COUNT; member++) {
        if ((mask & (uint16_t)(1u << member)) == 0u ||
            scene_effectConst(member) == src)
            continue;
        (void)scene_commitEffectRecord(member, src);
    }
    if ((mask & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        effects_activateScene(scene_getActiveIndex());
    bank_revalidateVoiceEditMasks();
    return mask;
}

/*
 * Reset an Effect record to its registered off defaults with fan-out.
 *
 * Inputs: destination Scene. Output: same-type edit-mask members are reset in
 * place, AutoSave-marked by SceneData, and the active runtime is reactivated.
 * No 420-byte temporary is allocated; each member uses the retained whole-
 * record commit pair directly.
 */
uint16_t effects_resetRecord(uint8_t dst_scene)
{
    const effect_record_t *dst = scene_effectConst(dst_scene);
    uint16_t mask;
    uint8_t member;

    if (!dst || dst_scene >= SCENE_COUNT)
        return 0u;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_COUNT; member++) {
        effect_record_t *record;

        if ((mask & (uint16_t)(1u << member)) == 0u)
            continue;
        record = scene_effectRecordForWholeCommit(member);
        if (!record)
            continue;
        scene_effectRecordDefaults(record);
        scene_finishEffectWholeCommit(member);
    }
    if ((mask & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        effects_activateScene(scene_getActiveIndex());
    bank_revalidateVoiceEditMasks();
    return mask;
}

/*
 * Reset Effect morph endpoints to Normal values with fan-out (S076 P3).
 * Contract in EffectsManager.h.
 *
 * What:       for every same-type edit-mask member, copies each morphable
 *             parameter's retained Normal byte onto its Morph byte through the
 *             in-place whole-record commit pair. A member with no differing
 *             byte is left closed and unmarked.
 * Why:        "reset fx morph" fans out exactly like "clear fx": same-type
 *             members share the descriptor layout, so the same morphable index
 *             set is equalised in each.
 * Inputs:     dst_scene; the destination's current type fixes the mask and the
 *             morphable index set.
 * Outputs:    the Scene mask of Scenes written. Side effects:
 *             scene_finishEffectWholeCommit() marks the Effect region;
 *             effects_activateScene() rebuilds the active runtime.
 * Affiliates: effects_resetRecord() (model), effects_fanoutMask(),
 *             effects_paramMorphable(), ccClear_runResetFxMorph().
 */
uint16_t effects_resetMorphToNormal(uint8_t dst_scene)
{
    const effect_record_t *dst = scene_effectConst(dst_scene);
    effect_type_id_t type;
    uint16_t mask;
    uint16_t written = 0u;
    uint8_t member;

    if (!dst || dst_scene >= SCENE_COUNT)
        return 0u;
    type = dst->type;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_COUNT; member++) {
        effect_record_t *record;
        uint8_t changed = 0u;
        uint8_t i;

        if ((mask & (uint16_t)(1u << member)) == 0u)
            continue;
        record = scene_effectRecordForWholeCommit(member);
        if (!record)
            continue;
        for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
            if (!effects_paramMorphable(type, i))
                continue;
            if (record->morph[i] != record->normal[i]) {
                record->morph[i] = record->normal[i];
                changed = 1u;
            }
        }
        if (changed) {
            scene_finishEffectWholeCommit(member);
            written |= (uint16_t)(1u << member);
        }
    }
    if ((written & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        effects_activateScene(scene_getActiveIndex());
    return written;
}

/*
 * Reset Effect morph endpoints for one Scene, no fan-out (S076 P3).
 * Contract in EffectsManager.h.
 *
 * What:       the same morphable Normal -> Morph copy as
 *             effects_resetMorphToNormal() but applied to exactly one Scene.
 * Why:        the whole-Scene "reset morph" clear owns its own non-fanned-out
 *             loop and hands the Effect slice to this single-Scene helper.
 * Inputs:     scene_index.
 * Outputs:    nonzero if any byte changed. Side effects:
 *             scene_finishEffectWholeCommit() on change and
 *             effects_activateScene() when the active Scene changed.
 * Affiliates: effects_resetMorphToNormal() above, effects_paramMorphable(),
 *             ccClear_runResetSceneMorph().
 */
uint8_t effects_resetMorphToNormalSingle(uint8_t scene_index)
{
    const effect_record_t *dst = scene_effectConst(scene_index);
    effect_record_t *record;
    effect_type_id_t type;
    uint8_t changed = 0u;
    uint8_t i;

    if (!dst || scene_index >= SCENE_COUNT)
        return 0u;
    type = dst->type;
    record = scene_effectRecordForWholeCommit(scene_index);
    if (!record)
        return 0u;
    for (i = 0u; i < EFFECT_PARAM_COUNT; i++) {
        if (!effects_paramMorphable(type, i))
            continue;
        if (record->morph[i] != record->normal[i]) {
            record->morph[i] = record->normal[i];
            changed = 1u;
        }
    }
    if (changed) {
        scene_finishEffectWholeCommit(scene_index);
        if (scene_index == scene_getActiveIndex())
            effects_activateScene(scene_index);
    }
    return changed;
}

/*
 * Paste one retained FX-sequence step across same-type edit-mask members.
 *
 * Inputs: destination Scene, 0..15 step, and an 18-byte step image. Output:
 * nonzero when at least one retained lane or lock changed. Active Morph-lane
 * changes invalidate the held Morph latch and advance the UI serial.
 */
uint8_t effects_pasteSeqStep(uint8_t dst_scene, uint8_t step,
                             const effect_seq_step_t *src)
{
    const effect_record_t *dst = scene_effectConst(dst_scene);
    uint16_t mask;
    uint8_t member;
    uint8_t lane;
    uint8_t changed = 0u;
    uint8_t active_changed_morph = 0u;

    if (!dst || !src || dst_scene >= SCENE_COUNT ||
        step >= EFFECT_SEQ_STEP_COUNT)
        return 0u;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_COUNT; member++) {
        const effect_record_t *record;

        if ((mask & (uint16_t)(1u << member)) == 0u)
            continue;
        record = scene_effectConst(member);
        if (!record)
            continue;
        for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
            uint16_t bit = (uint16_t)(1u << lane);
            uint8_t old_value = record->steps[step].value[lane];
            uint8_t old_locked = (uint8_t)((record->steps[step].lock_mask &
                                            bit) != 0u);
            uint8_t new_locked = (uint8_t)((src->lock_mask & bit) != 0u);

            if (old_value != src->value[lane] || old_locked != new_locked)
                changed = 1u;
            if (member == scene_getActiveIndex() &&
                lane == EFFECT_SEQ_LANE_MORPH &&
                (old_value != src->value[lane] || old_locked != new_locked))
                active_changed_morph = 1u;
            scene_setEffectSeqLaneValue(member, step, lane, src->value[lane]);
            scene_setEffectSeqLaneLocked(member, step, lane, new_locked);
        }
    }
    if (active_changed_morph) {
        effects_state.seq_serial++;
        effects_state.held_morph_valid = 0u;
    }
    return changed;
}

/*
 * Clear retained FX-sequence lanes across a same-type edit-mask fan-out.
 *
 * Inputs: destination Scene, step mask, and lane mask. Output: selected lane
 * values become zero and unlocked; parameters and sequence settings stay
 * intact. Active Morph-lane changes invalidate the held latch and increment
 * the sequence serial consumed by Menu.
 */
uint8_t effects_clearSeqLanes(uint8_t scene_index, uint16_t step_mask,
                              uint16_t lane_mask)
{
    const effect_record_t *dst = scene_effectConst(scene_index);
    uint16_t mask;
    uint8_t member;
    uint8_t step;
    uint8_t lane;
    uint8_t changed = 0u;
    uint8_t active_changed_morph = 0u;

    if (!dst || scene_index >= SCENE_COUNT)
        return 0u;
    mask = effects_fanoutMask(scene_index, 1u);
    for (member = 0u; member < SCENE_COUNT; member++) {
        const effect_record_t *record;

        if ((mask & (uint16_t)(1u << member)) == 0u)
            continue;
        record = scene_effectConst(member);
        if (!record)
            continue;
        for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
            if ((step_mask & (uint16_t)(1u << step)) == 0u)
                continue;
            for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
                uint16_t bit = (uint16_t)(1u << lane);

                if ((lane_mask & bit) == 0u)
                    continue;
                if (record->steps[step].value[lane] != 0u ||
                    (record->steps[step].lock_mask & bit) != 0u)
                    changed = 1u;
                if (member == scene_getActiveIndex() &&
                    lane == EFFECT_SEQ_LANE_MORPH &&
                    (record->steps[step].value[lane] != 0u ||
                     (record->steps[step].lock_mask & bit) != 0u))
                    active_changed_morph = 1u;
                scene_setEffectSeqLaneValue(member, step, lane, 0u);
                scene_setEffectSeqLaneLocked(member, step, lane, 0u);
            }
        }
    }
    if (active_changed_morph) {
        effects_state.seq_serial++;
        effects_state.held_morph_valid = 0u;
    }
    return changed;
}

/* Publish the tracks that own at least one Effect overlay. */
static void effects_automationPublishOwners(void)
{
    uint64_t mask = effects_automation.active;
    uint8_t tracks = 0u;

    while (mask) {
        uint8_t local = (uint8_t)__builtin_ctzll(mask);

        tracks |= (uint8_t)(1u << effects_automation.owner[local]);
        mask &= (mask - 1ULL);
    }
    if (tracks != effects_automation.owner_tracks) {
        effects_automation.owner_tracks = tracks;
        seq_setEffectAutomationTracks(tracks);
    }
}

/* Clear parameter overlays, with optional clearing of the Scene-level fxm. */
static void effects_automationClear(uint8_t include_morph)
{
    effects_automation.active = 0u;
    effects_automation.pending_end = 0u;
    effects_automation.pending_track = EFFECT_AUTOMATION_TRACK_NONE;
    if (include_morph)
        effects_automation.morph_override_valid = 0u;
    effects_automationPublishOwners();
}

void effects_automationReset(void)
{
    /* Common reset path and Scene activation clear all transient Effect layers. */
    effects_automationClear(1u);
}

void effects_automationStepFlush(void)
{
    /* End owner-track candidates not rewritten by the completed FX step group. */
    if (effects_automation.pending_end != 0u) {
        effects_automation.active &= ~effects_automation.pending_end;
        effects_automation.pending_end = 0u;
        effects_automationPublishOwners();
    }
    effects_automation.pending_track = EFFECT_AUTOMATION_TRACK_NONE;
}

void effects_automationStepBegin(uint8_t track)
{
    uint64_t mask;
    uint64_t owned = 0u;

    /* The marker precedes this step's entries, so a later entry re-holds a bit. */
    effects_automationStepFlush();
    if (track >= EFFECT_AUTOMATION_TRACK_LIMIT)
        return;
    mask = effects_automation.active;
    while (mask) {
        uint8_t local = (uint8_t)__builtin_ctzll(mask);

        if (effects_automation.owner[local] == track)
            owned |= (1ULL << local);
        mask &= (mask - 1ULL);
    }
    effects_automation.pending_end = owned;
    effects_automation.pending_track = track;
}

uint8_t effects_applyAutomation(uint8_t track, uint8_t local, uint8_t value7)
{
    const effect_param_descriptor_t *descriptor;
    uint64_t bit;
    uint8_t value;

    /* Apply one validated Pattern Effect entry as a runtime-only overlay. */
    if (track >= EFFECT_AUTOMATION_TRACK_LIMIT ||
        !effects_paramAutomatable(effects_state.runtime_type, local))
        return 0u;
    descriptor = effects_descriptor(effects_state.runtime_type, local);
    value = (uint8_t)(value7 & 0x7Fu);
    if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) != 0u)
        value = descriptor->expand7(value);
    if (value > descriptor->max_value)
        value = descriptor->max_value;
    bit = 1ULL << local;
    effects_automation.value[local] = value;
    effects_automation.owner[local] = track;
    effects_automation.active |= bit;
    effects_automation.pending_end &= ~bit;
    effects_automationPublishOwners();
    return 1u;
}

void effects_setMorphAutomation(uint8_t amount)
{
    /* Pattern `fxm` is a runtime-only expanded Morph-base override. */
    effects_automation.morph_override = amount;
    effects_automation.morph_override_valid = 1u;
}

/*
 * Clear the Pattern `fxm` Morph-base override (S076 Rule A).
 *
 * What: deactivates effects_automation.morph_override_valid so the Effect
 * service loop falls back to the retained Scene Effect Morph amount.
 * Why: a non-automation write (menu edit, MIDI CC1 global, copy/clear)
 * must supersede any active Pattern fxm override so the user's edit is
 * audible. The full transport-boundary clear (effects_automationReset)
 * continues to own bulk clear; this API clears only the morph override
 * without touching per-parameter Effect overlays or owner tracks.
 * Inputs: none. Output: morph_override_valid = 0; the morph_override
 * value is left stale because it is not read while invalid.
 * Callers: effects_setMorphAmountScene() (the active-Scene retained
 * write path). Affiliates: effects_setMorphAutomation() (the setter),
 * effects_automationReset() (the transport/Scene clear),
 * effects_service() service loop line 1567–1568 (the consumer).
 */
void effects_clearMorphAutomationOverride(void)
{
    effects_automation.morph_override_valid = 0u;
}

void effects_setLfoContribution(uint8_t source_slot, uint8_t pair,
                                uint8_t target, uint8_t direction,
                                uint8_t depth)
{
    effects_lfo_entry_t *entry;

    /* Store one base-independent Effect LFO direction/depth contribution. */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || pair > 1u)
        return;
    entry = &effects_automation.lfo[source_slot][pair];
    if (direction > EFFECT_LFO_DIRECTION_UP || depth == 0u)
        direction = EFFECT_LFO_DIRECTION_NONE;
    entry->target = target;
    entry->direction = direction;
    entry->depth = (direction == EFFECT_LFO_DIRECTION_NONE) ? 0u : depth;
}

void effects_clearLfoSource(uint8_t source_slot, uint8_t pair)
{
    /* Clear one Effect LFO source; the next service pass returns to its base. */
    if (source_slot >= INSTRUMENT_SLOT_COUNT || pair > 1u)
        return;
    effects_automation.lfo[source_slot][pair].target = EFFECT_LFO_TARGET_NONE;
    effects_automation.lfo[source_slot][pair].direction =
        EFFECT_LFO_DIRECTION_NONE;
    effects_automation.lfo[source_slot][pair].depth = 0u;
}

/* Resolve all matching LFO entries around a held base and clamp the domain. */
static uint8_t effects_lfoResolve(uint8_t target, uint8_t base,
                                  uint8_t min_value, uint8_t max_value)
{
    int32_t effective;
    uint8_t source;
    uint8_t pair;

    if (base < min_value)
        base = min_value;
    else if (base > max_value)
        base = max_value;
    effective = base;
    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        for (pair = 0u; pair < 2u; pair++) {
            const effects_lfo_entry_t *entry =
                &effects_automation.lfo[source][pair];

            if (entry->target != target)
                continue;
            if (entry->direction == EFFECT_LFO_DIRECTION_UP)
                effective += ((int32_t)(max_value - base) * entry->depth +
                              127) / 255;
            else if (entry->direction == EFFECT_LFO_DIRECTION_DOWN)
                effective -= ((int32_t)(base - min_value) * entry->depth +
                              127) / 255;
        }
    }
    if (effective < (int32_t)min_value)
        effective = min_value;
    else if (effective > (int32_t)max_value)
        effective = max_value;
    return (uint8_t)effective;
}

/* Build a bit mask of active local Effect LFO destinations. */
static uint64_t effects_lfoTargetMask(void)
{
    uint64_t mask = 0u;
    uint8_t source;
    uint8_t pair;

    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        for (pair = 0u; pair < 2u; pair++) {
            const effects_lfo_entry_t *entry =
                &effects_automation.lfo[source][pair];

            if (entry->direction != EFFECT_LFO_DIRECTION_NONE &&
                entry->target < EFFECT_PARAM_COUNT)
                mask |= (1ULL << entry->target);
        }
    }
    return mask;
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
 * Export the live type's arena description into a fresh handoff snapshot.
 *
 * What: begins a new FxBuffer handoff record (share bounds and unit owners
 * refreshed, Effect fields reset), stamps the live type and its io-derived
 * channel count, and lets the type describe its arena use through
 * export_handoff. Why: the handoff must be current both when a type exits
 * (type switch) and when a same-type Scene switch keeps the runtime alive
 * (S074 gap 1). Inputs: effects_state.runtime_type and the runtime union.
 * Output: the handoff record. No runtime or arena byte changes. Callers:
 * effects_switchRuntime(), effects_activateScene(). Affiliates:
 * fxbuf_handoffBeginExit(), crumpBit_exportHandoff().
 */
static void effects_exportHandoff(void)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);
    fxbuf_handoff_t *handoff = fxbuf_handoffBeginExit();

    handoff->effect_type = effects_state.runtime_type;
    handoff->effect_channels = (entry &&
        (entry->io_flags & EFFECT_IO_STEREO_OUT) != 0u) ? 2u :
        ((entry && entry->io_flags != 0u) ? 1u : 0u);
    if (entry && entry->ops && entry->ops->export_handoff)
        entry->ops->export_handoff(effects_runtimeMember(), handoff);
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
    const effect_registry_entry_t *new_entry = effects_registryEntry(incoming);

    if (!new_entry) {
        incoming = EFFECT_TYPE_OFF;
        new_entry = effects_registryEntry(EFFECT_TYPE_OFF);
    }
    /* The outgoing type describes its arena use before the union clears. */
    effects_exportHandoff();
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
    /* Step 9 overlay/LFO state starts empty; LFO entries target nothing. */
    memset(&effects_automation, 0, sizeof(effects_automation));
    effects_automation.pending_track = EFFECT_AUTOMATION_TRACK_NONE;
    {
        uint8_t source;

        for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
            effects_clearLfoSource(source, 0u);
            effects_clearLfoSource(source, 1u);
        }
    }
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
    if (type != effects_state.runtime_type) {
        effects_switchRuntime(type);
    } else {
        /*
         * Same-type Scene switch (S074 gap 1): the runtime and its arena
         * content stay live, so a CrumpBit tail keeps ringing into the new
         * Scene's settings. The handoff is refreshed without init, so share
         * bounds, unit owners and type positions describe the arena as it is.
         */
        effects_exportHandoff();
    }
    /*
     * Scene rule (S072_ST8 D3): the new Scene's lane locks begin at its next
     * FX boundary; the held Morph lane and Pattern automation overlays never
     * carry across a Scene switch.
     */
    effects_state.seq_step_valid = 0u;
    effects_state.held_morph_valid = 0u;
    effects_automationReset();
    effects_state.seq_serial++;
    effects_state.force_all = 1u;
}

/*
 * One-Scene type-change transaction. The public wrapper below fans the change
 * out and revalidates directional VOICE masks, while this worker preserves the
 * existing common-row, sequence, AutoSave, and active-runtime behavior.
 */
static uint8_t effects_changeTypeScene(uint8_t scene_index, effect_type_id_t type)
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
    /* Type change clears every lock, including the held Morph lane (F3). */
    if (scene_index == effects_state.scene_index) {
        effects_state.held_morph_valid = 0u;
        /* Old-type local overlays are meaningless; fxm is Scene-level. */
        effects_automationClear(0u);
        effects_state.seq_serial++;
    }
    if (scene_index == effects_state.scene_index)
        effects_switchRuntime(type);
    return 1u;
}

uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type)
{
    uint16_t mask;
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Type changes restore the layout match, so no type filter is applied to
     * the active edit mask. Directional masks owned by other Scenes are then
     * repaired by BankData, dropping members made incompatible by the change.
     * Unknown types are rejected before any resident Scene is touched.
     */
    if (!effects_registryEntry(type))
        return 0u;
    mask = effects_fanoutMask(scene_index, 0u);
    for (s = 0u; s < SCENE_COUNT; s++) {
        if ((mask & (uint16_t)(1u << s)) != 0u)
            changed |= effects_changeTypeScene(s, type);
    }
    if (changed)
        bank_revalidateVoiceEditMasks();
    return changed;
}

void effects_service(void)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);
    fx_share_t share;
    uint64_t lfo_mask;
    uint8_t morph;
    uint8_t active_step;
    uint8_t index;
    const effect_seq_step_t *step;

    /* Full descriptor rescan avoids requiring every retained writer to notify. */
    if (!record || !entry)
        return;
    /*
     * Plan §9 resolution (S072 steps 8-9), once per render block: consume the
     * FX latch, resolve Morph base priority and LFO, then apply menu, FX lock,
     * Pattern overlay, and LFO layers before the existing runtime clamps.
     */
    effects_seqConsume(record);
    active_step = effects_seqStepFor(record);
    step = (active_step != EFFECT_SEQ_STEP_NONE)
        ? &record->steps[active_step] : NULL;
    /* `sel` always applies, so its Morph lock re-latches after RESET/switch. */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL && step &&
        !effects_state.held_morph_valid)
        effects_seqLatchMorph(record, active_step);
    if (effects_automation.morph_override_valid)
        morph = effects_automation.morph_override;
    else if (effects_state.held_morph_valid)
        morph = effects_state.held_morph;
    else
        morph = scene_getEffectMorphAmount(effects_state.scene_index);
    morph = effects_lfoResolve(EFFECT_LFO_TARGET_MORPH, morph, 0u, 255u);
    lfo_mask = effects_lfoTargetMask();
    fxbuf_effectShare(&share);
    for (index = 0u; index < entry->descriptor_count; index++) {
        const effect_param_descriptor_t *descriptor =
            &entry->descriptors[index];
        uint8_t value = (descriptor->base.flags &
                         INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u ?
            effects_interpolate(record->normal[index], record->morph[index], morph) :
            record->normal[index];

        /* A locked lane replaces the Morph-interpolated menu value (A16). */
        if (step && (step->lock_mask & 0xFFFEu) != 0u)
            (void)effects_seqOverride(entry, step, index, &value);
        /* Pattern automation is held ahead of the FX lock (A18). */
        if ((effects_automation.active & (1ULL << index)) != 0u)
            value = effects_automation.value[index];
        /* LFO resolves around the held value within its modulation domain. */
        if ((lfo_mask & (1ULL << index)) != 0u &&
            (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u &&
            descriptor->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE)
            value = effects_lfoResolve(index, value,
                                       descriptor->base.mod_domain.min_value,
                                       descriptor->base.mod_domain.max_value);
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

    /* The mixer calls this after effects_service() in the same block; `off` has
     * no process hook and the mixer skips the bus entirely for it. */
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
