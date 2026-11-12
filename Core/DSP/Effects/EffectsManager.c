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
/* S095 P1: the Space kit (the arena prefix, the feed and the drain). */
#include "SpaceKit.h"
#include "SpaceParameters.h"
#include "SpaceEffect.h"
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
    /*
     * S074: 56 B; S091 P2: 76 B; S091 P3: 88 B; S091 P4: 100 B (a shared
     * part plus a 44 B union of five sub-engine states, the reworked `grn`
     * the largest; user-approved growth up to +32 B over 76 B).
     * Its audio lives in the FxBuffer share, not in this union.
     */
    CrumpBitRuntime crump_bit;
    /* S095 P1: the Space types (Delay, Reverb, Echo), <= 100 B (SD13). */
    SpaceRuntime space;
} effects_runtime_t;

static INDTCMZ effects_runtime_t effects_runtime;

/* The live runtime member (defined below; S091 P3 needs it earlier). */
static void *effects_runtimeMember(void);

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
     * CrumpBit (`cbt`, S074; S091 P2; S091 P3 sub-types): stereo in and
     * out. The 8-bit buffer is elastic (S091 P2 §5.1): it uses the whole
     * Effect share up to CRUMPBIT_LOOP_MAX_BYTES (pref, the full arena) and
     * still works at the share left with every voice unit claimed (min);
     * the Rate row is clamped to the share, under the live sub-type's law,
     * through crumpBit_ops.effective_max. These two fields are declarative
     * (no code reads them today).
     * lanes[] below is the `dly` map (sub 0) and equals the S091 P2 table:
     * lanes 1..13 bit off, bit invert, mix, feedback, rate, sync, vol, pan,
     * delay pan, tone, filter type, filter freq, filter reso; lane 0 Effect
     * Morph; lanes 14..15 free. The live table follows `sub`
     * (crumpBit_ops.lane_map, effects_laneTable()); the self-check
     * (code 11) holds this row equal to crumpBit_laneMap(0).
     */
    {
        crumpBit_token3, crumpBit_abbrev5, crumpBit_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        crumpBit_descriptors, CRUMPBIT_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
          CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
          /* S091 P6: lane 6 empty (`dly` no longer has `snc`). */
          CRUMPBIT_PARAM_RATE, EFFECT_LANE_NONE,
          CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
          CRUMPBIT_PARAM_SUB_PAN,
          CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
          CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        &crumpBit_layout, &crumpBit_ops, &crumpBit_ui,
        (uint16_t)sizeof(CrumpBitRuntime),
        CRUMPBIT_LOOP_MIN_SHARE_BYTES, CRUMPBIT_LOOP_MAX_BYTES
    },
    /*
     * S095 P1: Delay (`dly`), Reverb (`rvb`), Echo (`ech`). Stereo in and
     * out. Every one is an elastic buffer user (D2): it plays in the whole
     * usable share above the arena prefix (§S18). buffer_min_bytes is the
     * minimum share, buffer_pref_bytes the arena (declarative, as
     * CrumpBit's). No sub-types: one fixed lane row each
     * (plan §4, lanes 14/15 = the common vol and pan). The default layout
     * places rows 3.. on SELECT 2.. (menuEffects.c, no select_layout); each
     * type's ui hooks label its times, ratios, states and pitches (one
     * hook set per type: format_value3 receives no type id).
     */
    {
        space_delayToken3, space_delayAbbrev5, space_delayFull8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        space_delayDescriptors, SPACE_DELAY_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          SPACE_DLY_MIX, SPACE_DLY_TIME, SPACE_DLY_FEEDBACK, SPACE_DLY_TONE,
          SPACE_DLY_DEGRADE, SPACE_DLY_WOW, SPACE_DLY_XFEED, SPACE_DLY_SPREAD,
          SPACE_DLY_REVERSE, SPACE_DLY_HOLD, SPACE_DLY_HEADS, SPACE_DLY_DUCK,
          EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN,
          EFFECT_LANE_NONE },
        NULL, &space_delayOps, &space_delayUi,
        (uint16_t)sizeof(SpaceRuntime),
        SPACE_MIN_SHARE_BYTES, SK_ARENA_PREF_BYTES
    },
    {
        space_reverbToken3, space_reverbAbbrev5, space_reverbFull8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        space_reverbDescriptors, SPACE_REVERB_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          SPACE_RVB_MIX, SPACE_RVB_DECAY, SPACE_RVB_FREEZE, SPACE_RVB_SHIMMER,
          SPACE_RVB_INTERVAL, SPACE_RVB_DETUNE, SPACE_RVB_ENV, SPACE_RVB_DUCK,
          SPACE_RVB_LOW_SHELF, SPACE_RVB_HIGH_SHELF, SPACE_RVB_PREDELAY,
          SPACE_RVB_MOD, SPACE_RVB_WIDTH,
          EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN },
        NULL, &space_reverbOps, &space_reverbUi,
        (uint16_t)sizeof(SpaceRuntime),
        SPACE_MIN_SHARE_BYTES, SK_ARENA_PREF_BYTES
    },
    {
        space_echoToken3, space_echoAbbrev5, space_echoFull8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        space_echoDescriptors, SPACE_ECHO_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          SPACE_ECH_MIX, SPACE_ECH_TIME, SPACE_ECH_FEEDBACK, SPACE_ECH_TONE,
          SPACE_ECH_DEGRADE, SPACE_ECH_WOW, SPACE_ECH_TO_REVERB,
          SPACE_ECH_BLEND, SPACE_ECH_DECAY, SPACE_ECH_LOW_SHELF, SPACE_ECH_HEADS,
          SPACE_ECH_DUCK, SPACE_ECH_ROUTING,
          EFFECT_COMMON_PARAM_LEVEL, EFFECT_COMMON_PARAM_PAN },
        NULL, &space_echoOps, &space_echoUi,
        (uint16_t)sizeof(SpaceRuntime),
        SPACE_MIN_SHARE_BYTES, SK_ARENA_PREF_BYTES
    },
};

_Static_assert(sizeof(StereoFilterRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold StereoFilter");
_Static_assert(sizeof(CrumpBitRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold CrumpBit");
_Static_assert(sizeof(SpaceRuntime) <= sizeof(effects_runtime_t),
               "Effect runtime union must hold the Space types");
_Static_assert(sizeof(SpaceRuntime) <= 100u,
               "the Space runtime stays inside the 100 B union (SD13)");
/*
 * RAM guard (S074; 100 B since S091 P4): the DTCM union size is recorded in
 * STORAGE_SRAM_MANIFEST.md. A larger member grows .dtcmz and shrinks the
 * FxBuffer arena by the same amount (in 32 B granules), which needs RAM
 * approval before it is merged. S091 P3 and P4 used 24 B of a 32 B
 * approval (P3 +12 B, P4 +12 B for the reworked `grn` state); the
 * arena loses one granule (126,688 -> 126,656 B) when .dtcmz crosses a 32 B
 * boundary, which depends on LTO's packing (CRUMPBIT_LOOP_MAX_BYTES assumes
 * the smaller arena).
 */
_Static_assert(sizeof(effects_runtime_t) == 100u,
               "Effect runtime union size changed: RAM approval required");

/*
 * Manager-owned runtime state.
 *
 * The state is SRAM1-resident and exactly 80 bytes: active runtime type and
 * Scene, a force-write flag, one last-applied byte per common type domain,
 * live FX step/selection state, and the common return settings consumed by the
 * mixer's FX return. `reserved` (S095 P1) holds two flags and no longer pads:
 * bit 0 EFFECTS_STATE_FEED_LOCK (one interpolation slot locked while a
 * type-change feed runs, plan §5.7) and bit 1 EFFECTS_STATE_DRAIN (the runtime
 * type is draining its tail into OFF, plan §5.5). The former held Morph latch
 * was removed (S081 §5.3, one-step Morph lane).
 * scene_index is the Scene whose Effect record effects_service() reads every
 * render block. effects_activateScene() sets it for a real Scene change;
 * effects_rebindSourceScene() moves it to Scene 17 at the background-load
 * switch without disturbing the runtime (S082 P2 fix 1). It must always name
 * the active Scene while audio runs.
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
    uint8_t seq_serial;
    effects_common_runtime_t common;
} effects_state_t;

_Static_assert(sizeof(effects_state_t) == 80u,
               "effects_state_t size is recorded in STORAGE_SRAM_MANIFEST.md");
_Static_assert(STEP_SCALE_COUNT == EFFECT_SEQ_SCALE_COUNT &&
               STEP_SCALE_DEFAULT == EFFECT_SEQ_SCALE_DEFAULT,
               "Pattern and FX scale contracts must remain identical");

/*
 * effects_state.reserved flags (S095 P1).
 *
 * EFFECTS_STATE_FEED_LOCK: set by sk_feedBegin() (through
 * effects_setFeedSlotLock()) when an entering type starts a feed; cleared on
 * the feed's last block, when a share change ends the feed, and whenever a
 * type exits (effects_switchRuntime()).
 * EFFECTS_STATE_DRAIN: set when a buffer-using type is switched to OFF
 * (D25); while it is set, runtime_type is the draining type, the logical
 * type is OFF (effects_logicalType()), effects_service() resolves no rows,
 * and effects_process() runs the draining type on a muted input under the
 * drain envelope. Cleared at the drain's end, by a switch back to the
 * draining type, or by a switch to another type.
 */
#define EFFECTS_STATE_FEED_LOCK   0x01u
#define EFFECTS_STATE_DRAIN       0x02u

static effects_state_t effects_state;

/*
 * Pattern-automation and LFO overlay state (Session 072 step 9; plan §9,
 * §10.2-10.3, §16.1 items 4-5).
 *
 * active/value hold one Pattern overlay per Effect-local parameter; menuHold is
 * the R13 per-parameter menu-hold bitmap and menuHoldMorph its Morph flag.
 * morph_override is the Pattern `fxm` base layer. lfo stores one
 * base-independent direction/depth contribution per source slot and pair.
 * Lifetime: SRAM1, foreground-only writers; retained SceneData is untouched.
 */
typedef struct {
    uint8_t target;
    uint8_t direction;
    uint8_t depth;
} effects_lfo_entry_t;

#define EFFECT_AUTOMATION_TRACK_LIMIT 8u

typedef struct {
    uint64_t active;
    uint64_t menuHold;
    uint8_t value[EFFECT_PARAM_COUNT];
    effects_lfo_entry_t lfo[INSTRUMENT_SLOT_COUNT][2];
    uint8_t morph_override;
    uint8_t morph_override_valid;
    uint8_t menuHoldMorph;
} effects_automation_t;

_Static_assert(sizeof(effects_automation_t) == 120u,
               "effects_automation_t size is recorded in STORAGE_SRAM_MANIFEST.md");

static effects_automation_t effects_automation;

/*
 * Locks excluded by live erase, per FX step (S081 §10.4, P1B item 28).
 *
 * What: effects_eraseLanes is the set of lanes the live erase is removing;
 * effects_erasedLanes[s] is the set of lanes whose locks were kept from
 * sounding on FX step s since the last drain. Why: one combined
 * (stepMask, laneMask) pair cleared the union of lanes on every step seen in
 * a drain window, erasing locks that were never passed. Per-step masks make
 * the retained clear exact. Inputs: effects_lockClearLayers(). Outputs:
 * effects_takeErasedLocks(). RAM: 2 + 32 B. Affiliates: liveRec_service(),
 * effects_clearSeqLanes().
 */
static uint16_t effects_eraseLanes;
static uint16_t effects_erasedLanes[EFFECT_SEQ_STEP_COUNT];

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

uint8_t effects_paramEffectiveMax(effect_type_id_t type, uint8_t index)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);
    /* max: the descriptor's inclusive clamp, lowered by the share below. */
    uint8_t max;

    if (!entry || !descriptor)
        return 0u;
    max = descriptor->max_value;
    /*
     * The same rule effects_service() applies before write_param(): only a
     * BUFFER_DEPENDENT row of a type with an effective_max op is clamped,
     * against the share as it is now (a struct copy; no arena access).
     */
    if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) != 0u &&
        entry->ops && entry->ops->effective_max) {
        fx_share_t share;
        uint8_t limit;

        fxbuf_effectShare(&share);
        /*
         * S091 P3: the live runtime when this type is the live one (its
         * active sub-engine decides the law), else NULL (the type's default).
         */
        limit = entry->ops->effective_max(
            (type == effects_state.runtime_type) ? effects_runtimeMember()
                                                 : NULL,
            index, &share);
        if (limit < max)
            max = limit;
    }
    return max;
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

uint8_t effects_recordVariant(const effect_record_t *record)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(record ? record->type : EFFECT_TYPE_OFF);
    uint8_t variant;

    if (!record || !entry || !entry->ops || entry->ops->lane_variant_count == 0u)
        return 0u;
    variant = record->normal[entry->ops->lane_variant_row];
    return (variant < entry->ops->lane_variant_count)
        ? variant : (uint8_t)(entry->ops->lane_variant_count - 1u);
}

void effects_rowLabels(effect_type_id_t type, uint8_t variant, uint8_t index,
                       const char **short3, const char **category,
                       const char **long_name)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);
    /* s, c, l: the names so far (the descriptor's, then the hook's). */
    const char *s;
    const char *c;
    const char *l;

    if (!descriptor)
        return;
    s = descriptor->base.short_name;
    c = descriptor->base.category;
    l = descriptor->base.long_name;
    if (entry && entry->ui && entry->ui->labels)
        (void)entry->ui->labels(index, variant, &s, &c, &l);
    if (short3)
        *short3 = s;
    if (category)
        *category = c;
    if (long_name)
        *long_name = l;
}

uint8_t effects_targetLabels(uint8_t scene_index, uint16_t id,
                             instrument_target_use_t use,
                             const char **short3, const char **category,
                             const char **long_name)
{
    if (!effects_targetValid(scene_index, id, use))
        return 0u;
    effects_rowLabels(effects_sceneType(scene_index),
                      effects_recordVariant(scene_effectConst(scene_index)),
                      effectTarget_local(id), short3, category, long_name);
    return 1u;
}

uint8_t effects_formatTargetValue3(uint8_t scene_index, uint16_t target,
                                   uint8_t value, char *out)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_sceneType(scene_index));

    /*
     * S091 P3 (OQ5): a type may name a row's values on automation pages
     * (CrumpBit's `sub`: dly cmb pit grn rvb). Only rows whose text needs no
     * Effect-page context use this hook; everything else formats generically.
     */
    if (!out || !entry || !entry->ui || !entry->ui->format_target3 ||
        !effectTarget_isEffectId(target))
        return 0u;
    return entry->ui->format_target3(effectTarget_local(target), value, out);
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
 * The lane -> row table of one record (S091 P3 §2.9).
 *
 * What: a type with a lane_map op maps its lanes by the record's STORED
 * value of the variant row (CrumpBit: `sub`, record->normal[8]; a Pattern
 * switch changes the active engine, not the Scene's lanes); any other type
 * uses the registry's fixed lanes[]. Out-of-range variants read the last map.
 * Input: record (NULL = `off`). Output: a 16-entry table (never NULL).
 * Callers: every lane reader in this file and, through the lane-key helpers,
 * `.fx` storage. Affiliates: effect_type_ops_t.lane_map, crumpBit_laneMap().
 */
const uint8_t *effects_laneTable(const effect_record_t *record)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(record ? record->type : EFFECT_TYPE_OFF);

    if (!entry)
        entry = effects_registryEntry(EFFECT_TYPE_OFF);
    if (record && entry->ops && entry->ops->lane_map &&
        entry->ops->lane_variant_count != 0u) {
        uint8_t variant = record->normal[entry->ops->lane_variant_row];

        variant = (variant < entry->ops->lane_variant_count)
            ? variant : (uint8_t)(entry->ops->lane_variant_count - 1u);
        return entry->ops->lane_map(variant);
    }
    return entry->lanes;
}

/*
 * Resolve one retained FX-sequence lane to its `.fx` file key.
 *
 * Inputs: the record (S091 P3: its lane table) and lane number. Output: the
 * fixed Effect Morph key, a descriptor key, or NULL for an unused lane. The
 * registry remains the only owner of lane meaning, so storage and future UI
 * cannot drift apart.
 */
const char *effects_laneFileKey(const effect_record_t *record, uint8_t lane)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(record ? record->type : EFFECT_TYPE_OFF);
    uint8_t index;

    if (!entry || lane >= EFFECT_SEQ_LANE_COUNT)
        return NULL;
    index = effects_laneTable(record)[lane];
    if (index == EFFECT_LANE_MORPH_SOURCE)
        return EFFECT_LANE_MORPH_FILE_KEY;
    if (index == EFFECT_LANE_NONE || index >= entry->descriptor_count)
        return NULL;
    return entry->descriptors[index].base.file_key;
}

/*
 * Resolve a `.fx` lane key to the registry's retained lane number.
 *
 * Inputs: the record being parsed (S091 P3: its stored sub-type selects the
 * lane table), key after the `lane.` prefix, and optional output pointer.
 * Output: nonzero on a named lane match; unused lanes and unknown keys return
 * zero. The bounded 16-entry scan runs only during file parsing.
 */
uint8_t effects_laneByFileKey(const effect_record_t *record,
                              const char *file_key, uint8_t *lane_out)
{
    uint8_t lane;

    if (!file_key)
        return 0u;
    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        const char *key = effects_laneFileKey(record, lane);

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
 * Convert a retained Effect value to its 7-bit Pattern form (S081 §9.2,
 * P1B item 18).
 *
 * What: for a WIDE8 row (0..255 retained, expand7 on playback) returns the
 * 7-bit value whose expand7() first reaches `value`. For any other row it
 * returns the value clamped to 127. Why: Pattern automation stores 7 bits
 * (packed with & 0x7F). A WIDE8 value above 127 written raw would wrap and
 * play back as a different value. Inputs: Scene (for its Effect type),
 * local parameter, retained value. Output: 0..127. Context: foreground.
 * Accessors: liveRec_targetValue7(). Affiliates: effects_getParameter(),
 * effect_param_descriptor_t.expand7, EFFECT_PARAM_FLAG_WIDE8.
 */
uint8_t effects_storeValue7(uint8_t scene_index, uint8_t local,
                            uint8_t value)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    const effect_param_descriptor_t *descriptor;
    uint8_t v;

    if (!record || local >= EFFECT_PARAM_COUNT)
        return 0u;
    descriptor = effects_descriptor(record->type, local);
    if (!descriptor ||
        (descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) == 0u)
        return (uint8_t)((value > 127u) ? 127u : value);
    /* WIDE8: smallest v in 0..127 whose expand7(v) reaches value. */
    for (v = 0u; v < 127u; v++) {
        if (descriptor->expand7(v) >= value)
            return v;
    }
    return 127u;
}


/*
 * Resolve which resident Scenes one Effect edit reaches (plan §7.4, A44).
 *
 * An active-Scene edit adds that Scene's VOICE edit mask and always keeps the
 * origin. Parameter, sequence, and lane edits then remove masked Scenes whose
 * Effect type differs, protecting local descriptor/lane indices until the
 * layout gate and revalidation have repaired every mask. Inactive edits reach
 * only their origin, matching VOICE editing. Foreground-only; no SRAM.
 * Affiliates: bank_loadUnavailableMask().
 */
static uint32_t effects_fanoutMask(uint8_t scene_index, uint8_t match_type)
{
    const effect_record_t *origin = scene_effectConst(scene_index);
    uint32_t mask;
    uint8_t s;

    if (!origin)
        return 0u;
    if (scene_index == SCENE_BACKGROUND_INDEX)
        return scene_backgroundExists() ? (1ul << SCENE_BACKGROUND_INDEX) : 0u;
    if (scene_index >= SCENE_COUNT)
        return 0u;
    mask = (uint32_t)(1ul << scene_index);
    if (scene_index == scene_getActiveIndex())
        mask |= (uint32_t)bank_sceneMaskVoiceEdit();
    else
        mask |= (uint32_t)bank_sceneMaskVoiceEditForScene(scene_index);
    /*
     * S082 P3 D44: no Effect fan-out into a Scene a running background Bank
     * load has not finished (pending, in progress or emptied); zero mask
     * outside a load. Affiliates: bank_loadUnavailableMask(),
     * bank_editTargetMask().
     */
    mask &= ~(uint32_t)bank_loadUnavailableMask();
    if (!match_type)
        return mask;
    for (s = 0u; s < SCENE_COUNT; s++) {
        const effect_record_t *record;

        if (s == scene_index || (mask & (1ul << s)) == 0u)
            continue;
        record = scene_effectConst(s);
        if (!record || record->type != origin->type)
            mask &= ~(1ul << s);
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
    /* S091 P5: a ranged row's floor (effects_descriptorMin()). */
    if (value < effects_descriptorMin(descriptor))
        value = effects_descriptorMin(descriptor);
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
    uint32_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Fan out one retained row to every same-type masked Scene. Each worker
     * clamps against its own descriptor and SceneData marks its own AutoSave
     * cell/card-clean state; the active runtime is rescanned by service.
     */
    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
            changed |= effects_setParameterScene(s, index, image, value);
    }
    /*
     * Menu edit of an Effect parameter clears its overlay and sets menuHold
     * (S081 §5.3, R13, F-Q).
     *
     * What: when the origin Scene matches the active Scene and the row is in the
     * runtime Effect type, clears the overlay (active bit) for that local
     * parameter and sets menuHold for it. Why: R13 says a menu edit beats both
     * lock and overlay until the next automation. Clearing the overlay ensures
     * the lock can see the menu value when it starts. Setting menuHold prevents
     * the lock from overriding the menu value for the remainder of its step.
     * Inputs: local parameter index, origin Scene. Outputs: overlay cleared,
     * menuHold set. Affiliates: effects_applyAutomation (clears menuHold on next
     * automation), effects_service resolution.
     */
    if (scene_index == effects_state.scene_index && index < EFFECT_PARAM_COUNT &&
        effects_descriptor(effects_state.runtime_type, index)) {
        effects_automation.active &= ~(1ULL << index);
        effects_automation.menuHold |= (1ULL << index);
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
     * menu_commitEffectMorphParam() and clearOps.c; and
     * effects_setMorphAmountFromGlobal() (global Morph through
     * preset_morphScene(), S083 follow-up). Affiliate:
     * effects_setMorphAutomation() (the automation setter).
     */
    if (scene_index == effects_state.scene_index)
        effects_clearMorphAutomationOverride();
    return (uint8_t)(scene_getEffectMorphAmount(scene_index) != before);
}

/* Sequence-setting fan-out: run, length, and scale share the same-type mask. */
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode)
{
    uint32_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
            changed |= effects_setSeqRunModeScene(s, mode);
    }
    return changed;
}

uint8_t effects_setSeqLength(uint8_t scene_index, uint8_t length)
{
    uint32_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
            changed |= effects_setSeqLengthScene(s, length);
    }
    return changed;
}

uint8_t effects_setSeqStepScale(uint8_t scene_index, uint8_t scale)
{
    uint32_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
            changed |= effects_setSeqStepScaleScene(s, scale);
    }
    return changed;
}

/* Contract in EffectsManager.h. */
uint8_t effects_setMorphAmountFromGlobal(uint8_t scene_index, uint8_t amount)
{
    uint8_t changed = effects_setMorphAmountScene(scene_index, amount);

    if (scene_index == effects_state.scene_index)
        effects_automation.menuHoldMorph = 1u;
    return changed;
}

/* Effect Morph is type-agnostic, so its retained amount reaches the mask. */
uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount)
{
    uint32_t mask = effects_fanoutMask(scene_index, 0u);
    uint8_t changed = 0u;
    uint8_t s;

    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
            changed |= effects_setMorphAmountScene(s, amount);
    }
    /*
     * Menu edit of the Effect Morph amount sets menuHoldMorph (S081 §5.3).
     * effects_setMorphAmountScene() already clears morph_override_valid for the
     * active Scene; the Morph menuHold then keeps the menu value sounding until
     * the next Morph automation (an FX lock step start or `fxm`).
     */
    if (scene_index == effects_state.scene_index)
        effects_automation.menuHoldMorph = 1u;
    return changed;
}

/* Clamp a retained FX sequence length into the live 1..16 domain. */
static uint8_t effects_seqLength(const effect_record_t *record)
{
    uint8_t len = record->seq_length;

    return (len < EFFECT_SEQ_LENGTH_MIN || len > EFFECT_SEQ_LENGTH_MAX)
        ? EFFECT_SEQ_LENGTH_DEFAULT : len;
}

/*
 * Clear overlay and menuHold layers at an FX lock step start (S081 §5.3, R13).
 *
 * What: for each FX lane locked on the new step, clears the overlay bit and the
 * menuHold bit of the mapped Effect parameter. For lane 0 (Morph), clears
 * morph_override_valid and menuHoldMorph. Why: R13 says a lock beats a track
 * overlay, and a lock start after a menu edit should clear menuHold so the lock
 * value can apply. After the lock step ends, the parameter returns to the menu
 * value (not the overlay). Inputs: record and step (already wrapped to length).
 * Outputs: overlay and menuHold bits cleared. Affiliates: effects_service
 * resolution (lock > overlay when menuHold not set), live erase lane exclusion
 * (Phase G).
 */
static void effects_lockClearLayers(const effect_record_t *record, uint8_t step)
{
    const effect_registry_entry_t *entry;
    uint16_t lock_mask;
    uint8_t lane;

    if (!record)
        return;
    entry = effects_registryEntry(record->type);
    if (!entry)
        return;
    lock_mask = record->steps[step].lock_mask;
    if ((lock_mask & (uint16_t)(1u << EFFECT_SEQ_LANE_MORPH)) != 0u) {
        if ((effects_eraseLanes &
             (uint16_t)(1u << EFFECT_SEQ_LANE_MORPH)) != 0u) {
            /* S081 G-3c: an erased lane must not sound; queue it for removal. */
            effects_erasedLanes[step & 15u] |=
                (uint16_t)(1u << EFFECT_SEQ_LANE_MORPH);
        } else {
            effects_automation.morph_override_valid = 0u;
            effects_automation.menuHoldMorph = 0u;
        }
    }
    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        uint8_t index;

        if ((lock_mask & (uint16_t)(1u << lane)) == 0u)
            continue;
        if ((effects_eraseLanes & (uint16_t)(1u << lane)) != 0u) {
            /* S081 G-3c: an erased lane must not sound; queue it for removal. */
            effects_erasedLanes[step & 15u] |= (uint16_t)(1u << lane);
            continue;
        }
        /* S091 P3: the record's lane table (per stored sub-type). */
        index = effects_laneTable(record)[lane];
        if (index >= EFFECT_PARAM_COUNT)
            continue;
        effects_automation.active &= ~(1ULL << index);
        effects_automation.menuHold &= ~(1ULL << index);
    }
}

/*
 * Consume the one-byte TIM3 FX latch in foreground context.
 *
 * RESET invalidates the previous step. STEP records the newest position and
 * clears the overlay/menuHold layers of its locked lanes (R13). RESET and STEP
 * may be combined by a start/reset boundary, so RESET is intentionally handled
 * first. No DSP, LCD, or physical LED work is performed here.
 */
static void effects_seqConsume(const effect_record_t *record)
{
    uint8_t event = seq_fxTakeEvent();

    if (event == 0u)
        return;
    if ((event & SEQ_FX_EVENT_RESET) != 0u) {
        effects_state.seq_step_valid = 0u;
    }
    if ((event & SEQ_FX_EVENT_STEP) != 0u) {
        /* A queued clock event cannot create a selection after a mode change. */
        if (record->seq_run_mode != EFFECT_SEQ_RUN_SEL) {
            effects_state.seq_step = (uint8_t)(event & SEQ_FX_EVENT_INDEX);
            effects_state.seq_step_valid = 1u;
            effects_lockClearLayers(record,
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

/*
 * Replace a descriptor value with a locked value from the active step.
 * S091 P3: `lanes` is the record's lane table (effects_laneTable()).
 */
static uint8_t effects_seqOverride(const uint8_t *lanes,
                                   const effect_seq_step_t *step,
                                   uint8_t index, uint8_t *value)
{
    uint8_t lane;

    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (lanes[lane] == index &&
            (step->lock_mask & (uint16_t)(1u << lane)) != 0u &&
            (effects_eraseLanes & (uint16_t)(1u << lane)) == 0u) {
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
     * `sel` applies immediately: a lock on the selected step clears the
     * overlay/menuHold layers of its lanes (R13), so the lock value applies
     * and, after another selection, the menu value returns.
     */
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        effects_lockClearLayers(record, step);
    effects_state.seq_serial++;
}

/* Return the foreground-visible sequence signature generation. */
uint8_t effects_seqSerial(void)
{
    return effects_state.seq_serial;
}

/* Resolve one type-specific descriptor index to its retained lane number. */
uint8_t effects_laneOfParam(const effect_record_t *record, uint8_t index,
                            uint8_t *lane_out)
{
    /* S091 P3: the record's lane table (per sub-type for CrumpBit). */
    const uint8_t *lanes = effects_laneTable(record);
    uint8_t lane;

    if (!record)
        return 0u;
    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (lanes[lane] == index) {
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
        !effects_laneFileKey(record, lane))
        return 0u;
    if (lane != EFFECT_SEQ_LANE_MORPH) {
        /* S091 P3: the lane's row in this record's lane table. */
        const effect_param_descriptor_t *descriptor =
            effects_descriptor(record->type, effects_laneTable(record)[lane]);

        if (descriptor && value > descriptor->max_value)
            value = descriptor->max_value;
        /* S091 P5: a ranged row's floor. */
        if (value < effects_descriptorMin(descriptor))
            value = effects_descriptorMin(descriptor);
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
    uint32_t mask = effects_fanoutMask(scene_index, 1u);
    uint8_t changed = 0u;
    uint8_t s;

    /*
     * Fan out the held-step mask, lane, and value to same-type Scenes. Registry
     * lane meaning is shared by equal types, while each worker applies its own
     * descriptor clamp and retained SceneData dirty markers.
     */
    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
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
    uint32_t mask;
    uint8_t member;

    if (!dst || !src || dst_scene >= SCENE_COUNT)
        return 0u;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_SLOT_TOTAL; member++) {
        if ((mask & (1ul << member)) == 0u ||
            scene_effectConst(member) == src)
            continue;
        (void)scene_commitEffectRecord(member, src);
    }
    if ((mask & (1ul << scene_getActiveIndex())) != 0u)
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
    uint32_t mask;
    uint8_t member;

    if (!dst || dst_scene >= SCENE_COUNT)
        return 0u;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_SLOT_TOTAL; member++) {
        effect_record_t *record;

        if ((mask & (1ul << member)) == 0u)
            continue;
        record = scene_effectRecordForWholeCommit(member);
        if (!record)
            continue;
        scene_effectRecordDefaults(record);
        scene_finishEffectWholeCommit(member);
    }
    if ((mask & (1ul << scene_getActiveIndex())) != 0u)
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
    uint32_t mask;
    uint16_t written = 0u;
    uint8_t member;

    if (!dst || dst_scene >= SCENE_COUNT)
        return 0u;
    type = dst->type;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_SLOT_TOTAL; member++) {
        effect_record_t *record;
        uint8_t changed = 0u;
        uint8_t i;

        if ((mask & (1ul << member)) == 0u)
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
 * Why:        the whole-Scene "reset morph" clear and "scene -> morph" copy
 *             own their own edit-mask fan-out loop and hand each member's
 *             Effect slice to this single-Scene helper.
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
    uint32_t mask;
    uint8_t member;
    uint8_t lane;
    uint8_t changed = 0u;
    uint8_t active_changed_morph = 0u;

    if (!dst || !src || dst_scene >= SCENE_COUNT ||
        step >= EFFECT_SEQ_STEP_COUNT)
        return 0u;
    mask = effects_fanoutMask(dst_scene, 1u);
    for (member = 0u; member < SCENE_SLOT_TOTAL; member++) {
        const effect_record_t *record;

        if ((mask & (1ul << member)) == 0u)
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
    uint32_t mask;
    uint8_t member;
    uint8_t step;
    uint8_t lane;
    uint8_t changed = 0u;
    uint8_t active_changed_morph = 0u;

    if (!dst || scene_index >= SCENE_COUNT)
        return 0u;
    mask = effects_fanoutMask(scene_index, 1u);
    for (member = 0u; member < SCENE_SLOT_TOTAL; member++) {
        const effect_record_t *record;

        if ((mask & (1ul << member)) == 0u)
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
    }
    return changed;
}

/*
 * Clear parameter overlays, with optional clearing of the Scene-level fxm
 * (S081 §5.3, R13).
 *
 * What: zeroes the per-parameter overlay and menuHold bitmaps, the Morph
 * menuHold flag, and, when requested, the Pattern `fxm` override. Why: under
 * R13 every transient Effect layer is last-writer-wins with no per-step
 * ownership, so the reset clears them all. Removed with R13: the owner/
 * pending-end step-marker machinery (effects_automationPublishOwners,
 * effects_automationStepBegin, effects_automationStepFlush) and the owner-track
 * publication to the Sequencer. Inputs: include_morph. Outputs: overlay/
 * menuHold/fxm cleared. Affiliates: effects_seqConsume/effects_seqSelect (lock
 * step start), effects_service (resolution).
 */
static void effects_automationClear(uint8_t include_morph)
{
    effects_automation.active = 0u;
    effects_automation.menuHold = 0u;
    effects_automation.menuHoldMorph = 0u;
    if (include_morph)
        effects_automation.morph_override_valid = 0u;
}

void effects_automationReset(void)
{
    /* Common reset path and Scene activation clear all transient Effect layers. */
    effects_automationClear(1u);
    memset(effects_erasedLanes, 0, sizeof(effects_erasedLanes));
}

/*
 * Register lanes for FX-sequence live erase (S081 §10.4, G-3a).
 *
 * What: sets the live-erase lane mask. At each FX step start the locked lanes in
 * this mask are excluded from resolution and recorded in the pending mask. Why:
 * FX-sequence erase removes locks as the FX playhead passes, without the locks
 * ever sounding. Inputs: laneMask (16 bits). Caller: LiveRecord.
 */
void effects_setLiveEraseLanes(uint16_t laneMask)
{
    effects_eraseLanes = laneMask;
}

/*
 * Take one FX step's erased lanes (S081 §10.4, P1B item 28).
 *
 * What: returns the lowest FX step with pending erased lanes and its lane
 * mask, and clears that entry. Returns 0 when nothing is pending. Why: the
 * caller clears the retained locks step by step with
 * effects_clearSeqLanes(scene, 1 << step, lanes), including the linked-Scene
 * fan-out, without touching lanes that were never passed. Context:
 * foreground. Accessors: liveRec_service() (loops until 0). Affiliates:
 * effects_erasedLanes[], effects_clearSeqLanes().
 */
uint8_t effects_takeErasedLocks(uint8_t *step, uint16_t *laneMask)
{
    uint8_t s;

    for (s = 0u; s < EFFECT_SEQ_STEP_COUNT; s++) {
        if (effects_erasedLanes[s] != 0u) {
            if (step)
                *step = s;
            if (laneMask)
                *laneMask = effects_erasedLanes[s];
            effects_erasedLanes[s] = 0u;
            return 1u;
        }
    }
    if (step)
        *step = 0u;
    if (laneMask)
        *laneMask = 0u;
    return 0u;
}

uint8_t effects_applyAutomation(uint8_t track, uint8_t local, uint8_t value7)
{
    const effect_param_descriptor_t *descriptor;
    uint8_t value;

    /*
     * Track automation of an Effect parameter clears menuHold (S081 §5.3).
     *
     * What: sets the overlay value and active bit for the local parameter, and
     * clears the menuHold bit. Why: R13 says the next automation of a parameter
     * after a menu edit clears the menu hold and applies. The track argument is
     * unused after owner removal but kept for signature stability. Inputs:
     * owner_track, local, value. Outputs: overlay set, menuHold cleared.
     * Affiliates: seq_drainPendingAutomationTo() Effect branch, effects_service
     * resolution.
     */
    if (track >= EFFECT_AUTOMATION_TRACK_LIMIT ||
        local >= EFFECT_PARAM_COUNT ||
        !effects_paramAutomatable(effects_state.runtime_type, local))
        return 0u;
    descriptor = effects_descriptor(effects_state.runtime_type, local);
    if (!descriptor)
        return 0u;
    value = (uint8_t)(value7 & 0x7Fu);
    if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_WIDE8) != 0u)
        value = descriptor->expand7(value);
    if (value > descriptor->max_value)
        value = descriptor->max_value;
    /* S091 P5: a ranged row's floor. */
    if (value < effects_descriptorMin(descriptor))
        value = effects_descriptorMin(descriptor);
    effects_automation.value[local] = value;
    effects_automation.active |= (1ULL << local);
    effects_automation.menuHold &= ~(1ULL << local);
    return 1u;
}

void effects_setMorphAutomation(uint8_t amount)
{
    /*
     * Track automation of the Morph amount clears menuHoldMorph (S081 §5.3).
     * Pattern `fxm` is a runtime-only expanded Morph-base override; the track
     * automation of it supersedes a menu edit until the next Scene/lock change.
     */
    effects_automation.morph_override = amount;
    effects_automation.morph_override_valid = 1u;
    effects_automation.menuHoldMorph = 0u;
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
            /* S091 P5: the default inside [min, max] (a ranged row). */
            if (descriptor->default_value > descriptor->max_value ||
                descriptor->default_value < effects_descriptorMin(descriptor))
                return 7u;
        }
        /*
         * S091 P3: every lane table the type can present is checked: the
         * registry's lanes[] and, for a type with a lane_map op, one map per
         * variant (CrumpBit: per sub-type). Each has lane 0 = Morph and no
         * row twice; variant 0's map must equal lanes[] (code 11), so a
         * record without the variant row and the S091 P2 `.fx` keys keep
         * their meaning.
         */
        {
            const uint8_t variants =
                (entry->ops && entry->ops->lane_map)
                    ? entry->ops->lane_variant_count : 0u;
            uint8_t variant;

            for (variant = 0u; variant <= variants; variant++) {
                const uint8_t *lanes = (variant == variants)
                    ? entry->lanes : entry->ops->lane_map(variant);

                seen = 0u;
                if (lanes[0] != EFFECT_LANE_MORPH_SOURCE)
                    return 5u;
                for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
                    uint8_t descriptor_index = lanes[lane];

                    if (descriptor_index == EFFECT_LANE_NONE)
                        continue;
                    if (descriptor_index >= entry->descriptor_count ||
                        (seen & ((uint64_t)1u << descriptor_index)) != 0u)
                        return 6u;
                    seen |= (uint64_t)1u << descriptor_index;
                }
            }
            if (variants != 0u &&
                memcmp(entry->ops->lane_map(0u), entry->lanes,
                       EFFECT_SEQ_LANE_COUNT) != 0)
                return 11u;
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
 * Drain and logical-type helpers (S095 P1 plan §5.5, D25; SD4).
 *
 * effects_draining(): 1 while the runtime type drains into OFF.
 * effects_logicalType(): the type the user and the record see, OFF while
 * draining, else the runtime type. Every reader outside this file
 * (effects_activeType(): menu, MIDI Learn) and the rebind test use it, so a
 * draining tail is never offered the draining type's rows.
 */
static inline uint8_t effects_draining(void)
{
    return (uint8_t)((effects_state.reserved & EFFECTS_STATE_DRAIN) != 0u);
}

static inline effect_type_id_t effects_logicalType(void)
{
    return effects_draining() ? (effect_type_id_t)EFFECT_TYPE_OFF
                              : effects_state.runtime_type;
}

uint8_t effects_feedSlotLock(void)
{
    return (uint8_t)(effects_state.reserved & EFFECTS_STATE_FEED_LOCK);
}

void effects_setFeedSlotLock(uint8_t on)
{
    if (on)
        effects_state.reserved |= EFFECTS_STATE_FEED_LOCK;
    else
        effects_state.reserved &= (uint8_t)~EFFECTS_STATE_FEED_LOCK;
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
 * S095 P1: returns the record just written, so the drain's end can clear
 * FXBUF_STATE_EFFECT_WRITTEN on it (F2: a tail heard to its end is not fed
 * again). export_handoff only describes (SD3).
 * Output: the handoff record. No runtime or arena byte changes. Callers:
 * effects_switchRuntime(), effects_activateScene(). Affiliates:
 * fxbuf_handoffBeginExit(), crumpBit_exportHandoff().
 */
static fxbuf_handoff_t *effects_exportHandoff(void)
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
    return handoff;
}

/*
 * Switch the live Effect runtime to another type (S074; S095 P1 drain and
 * share).
 *
 * What, in order:
 * 1. an unknown id becomes OFF;
 * 2. while draining (D25): a switch back to the draining type cancels the
 *    drain and keeps the runtime and its tail (as a same-type Scene switch);
 *    a switch to OFF again leaves the drain running;
 * 3. into OFF from a type that has a buffer (export_handoff non-NULL, SD11)
 *    and is not already OFF: the drain starts (sk_drainBegin() resets its
 *    counters in the arena control block) and nothing else changes; the
 *    runtime keeps rendering on a muted input until its tail ends;
 * 4. otherwise the outgoing type (or the draining one) exports its live
 *    handoff, both reserved flags clear (its feed and drain end with it), the
 *    union is zeroed, and the incoming type's init receives the handoff and
 *    the share (SD2), so it can seat, pack the old tail and clear its rings
 *    before the next block.
 * Why: plan §5 (feed-through, D20-D22) and §5.5 (drain, D25); the order keeps
 * the handoff a description of the old arena at the moment init reads it.
 * Inputs: incoming type id. Outputs: effects_runtime, effects_state.
 * Callers: effects_activateScene(), effects_changeTypeScene().
 * Affiliates: sk_drainBegin(), effects_drainProcess(), fxbuf_handoff().
 */
static void effects_switchRuntime(effect_type_id_t incoming)
{
    const effect_registry_entry_t *new_entry = effects_registryEntry(incoming);
    const effect_registry_entry_t *old_entry =
        effects_registryEntry(effects_state.runtime_type);
    fx_share_t share;

    if (!new_entry) {
        incoming = EFFECT_TYPE_OFF;
        new_entry = effects_registryEntry(EFFECT_TYPE_OFF);
    }
    fxbuf_effectShare(&share);
    if (effects_draining()) {
        if (incoming == effects_state.runtime_type) {
            /* Back to the draining type: its runtime and tail stay live. */
            effects_state.reserved &= (uint8_t)~EFFECTS_STATE_DRAIN;
            effects_state.force_all = 1u;
            return;
        }
        if (incoming == EFFECT_TYPE_OFF)
            return;   /* still OFF: the drain carries on */
    } else if (incoming == EFFECT_TYPE_OFF &&
               effects_state.runtime_type != EFFECT_TYPE_OFF &&
               old_entry && old_entry->ops &&
               old_entry->ops->export_handoff &&
               sk_drainBegin(&share, effects_state.runtime_type)) {
        effects_state.reserved |= EFFECTS_STATE_DRAIN;
        return;
    }
    /* The outgoing type describes its arena use before the union clears. */
    (void)effects_exportHandoff();
    effects_state.reserved &=
        (uint8_t)~(EFFECTS_STATE_DRAIN | EFFECTS_STATE_FEED_LOCK);
    memset(&effects_runtime, 0, sizeof(effects_runtime));
    effects_state.runtime_type = incoming;
    if (new_entry && new_entry->ops && new_entry->ops->init)
        new_entry->ops->init(effects_runtimeMember(), fxbuf_handoff(), &share);
    sk_idleReset(&share);
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
         * S095 P1: a Scene of the draining type ends the drain (the tail
         * keeps ringing into the new Scene's settings, as any same-type
         * switch). A Scene whose type is OFF reaches effects_switchRuntime()
         * instead, which leaves the drain running.
         */
        effects_state.reserved &= (uint8_t)~EFFECTS_STATE_DRAIN;
        (void)effects_exportHandoff();
    }
    /*
     * Scene rule (S072_ST8 D3): the new Scene's lane locks begin at its next
     * FX boundary; Pattern automation overlays never carry across a Scene
     * switch (S081 §5.3: one-step Morph lock, no held latch).
     */
    effects_state.seq_step_valid = 0u;
    effects_automationReset();
    effects_state.seq_serial++;
    effects_state.force_all = 1u;
}

/*
 * Retarget the running Effect to an identical copy of its source Scene
 * (S082 P2 fix 1).
 *
 * What: changes only effects_state.scene_index, the Scene whose Effect
 * record effects_service() reads every render block. The runtime type and
 * member, its arena and tails, last_applied[], the FX sequence step and clock
 * state, the selected step, Pattern overlays, menu holds, LFO contributions
 * and the force flag are all left exactly as they are. Refused when the new
 * Scene has no record or its record's type differs from the running type.
 * Why: the Scene 17 switch must make no audible change (S082_P1 D4, D5). A
 * full effects_activateScene() resets the sequence step, clears automation
 * overlays and forces every parameter write, which is audible. Without any
 * rebind, the index keeps naming the home Scene, and the Bank load then
 * replaces that Scene's Effect record under the sounding Effect (level, type
 * parameters, Morph, FX sequence). Scene 17's record is a copy of the home
 * record, so after the rebind every value effects_service() computes equals
 * last_applied[] and no runtime write happens.
 * Inputs: scene_index (Scene 17 while it exists). Outputs:
 * effects_state.scene_index; returns 1 when retargeted, 0 when refused.
 * Context: foreground only, the same context as effects_service() (mixer
 * render in the main loop), so the single-byte store is never seen half-done.
 * Accessors: preset_tickBackgroundBankLoad() phase 3 (fresh Scene 17 build).
 * Affiliates: effects_activateScene() (the full activation used for real
 * Scene changes, including the release from Scene 17), effects_service(),
 * preset_buildBackgroundScene() (copies the Effect record).
 */
uint8_t effects_rebindSourceScene(uint8_t scene_index)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    effect_type_id_t type;

    if (!record)
        return 0u;
    type = effects_registryEntry(record->type) ? record->type : EFFECT_TYPE_OFF;
    /* S095 P1: while draining, the logical type (OFF) is what the Scene
     * record must match; the draining runtime is left untouched. */
    if (type != effects_logicalType())
        return 0u;
    effects_state.scene_index = scene_index;
    return 1u;
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
    /* Type change clears every lock (S081 §5.3: one-step Morph lane, F3). */
    if (scene_index == effects_state.scene_index) {
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
    uint32_t mask;
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
    for (s = 0u; s < SCENE_SLOT_TOTAL; s++) {
        if ((mask & (1ul << s)) != 0u)
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
    const uint8_t *lanes;
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
    /*
     * S095 P1 (D25, SD12): while draining, the record is OFF's and its rows
     * mean nothing to the draining type. The FX latch above is still
     * consumed (the FX clock stays aligned, MEMORY: FX sequence = master
     * clock), then every row, including the return route/level/pan, stays
     * at the outgoing Scene's values. This also skips the descriptor loop's
     * cost (≈ 142 cycles a row) for the drain's length.
     */
    if (effects_draining()) {
        effects_state.force_all = 0u;
        return;
    }
    step = (active_step != EFFECT_SEQ_STEP_NONE)
        ? &record->steps[active_step] : NULL;
    /*
     * R13 Morph base resolution (S081 §5.3, A2-2h): menuHoldMorph > lockMorph
     * (current step only) > Pattern fxm override > menu. The former held Morph
     * latch is removed; a Morph lane lock applies on its own step only.
     */
    if (effects_automation.menuHoldMorph) {
        morph = scene_getEffectMorphAmount(effects_state.scene_index);
    } else if (step &&
               (step->lock_mask &
                (uint16_t)(1u << EFFECT_SEQ_LANE_MORPH)) != 0u &&
               (effects_eraseLanes &
                (uint16_t)(1u << EFFECT_SEQ_LANE_MORPH)) == 0u) {
        morph = step->value[EFFECT_SEQ_LANE_MORPH];
    } else if (effects_automation.morph_override_valid) {
        morph = effects_automation.morph_override;
    } else {
        morph = scene_getEffectMorphAmount(effects_state.scene_index);
    }
    morph = effects_lfoResolve(EFFECT_LFO_TARGET_MORPH, morph, 0u, 255u);
    lfo_mask = effects_lfoTargetMask();
    fxbuf_effectShare(&share);
    /* S091 P3: this record's lane table (per stored sub-type). */
    lanes = effects_laneTable(record);
    for (index = 0u; index < entry->descriptor_count; index++) {
        const effect_param_descriptor_t *descriptor =
            &entry->descriptors[index];
        uint8_t value = (descriptor->base.flags &
                         INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u ?
            effects_interpolate(record->normal[index], record->morph[index], morph) :
            record->normal[index];

        /*
         * R13 per-parameter resolution (S081 §5.3, A2-2h):
         *   menuHold > lock > overlay > menu.
         * A menuHold bit keeps the menu (interpolated) value and skips both the
         * overlay and the lock; otherwise the Pattern overlay is applied first
         * and a locked lane replaces it (lock beats overlay).
         */
        if ((effects_automation.menuHold & (1ULL << index)) == 0u) {
            if ((effects_automation.active & (1ULL << index)) != 0u)
                value = effects_automation.value[index];
            if (step && (step->lock_mask & 0xFFFEu) != 0u)
                (void)effects_seqOverride(lanes, step, index, &value);
        }
        /* LFO resolves around the held value within its modulation domain. */
        if ((lfo_mask & (1ULL << index)) != 0u &&
            (descriptor->base.flags & INSTRUMENT_PARAM_FLAG_MODULATABLE) != 0u &&
            descriptor->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE)
            value = effects_lfoResolve(index, value,
                                       descriptor->base.mod_domain.min_value,
                                       descriptor->base.mod_domain.max_value);
        if (value > descriptor->max_value)
            value = descriptor->max_value;
        /*
         * S091 P5: a ranged row's floor; this also covers values that reach
         * the record another way (an older file, a copy).
         */
        if (value < effects_descriptorMin(descriptor))
            value = effects_descriptorMin(descriptor);
        if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) != 0u &&
            entry->ops && entry->ops->effective_max) {
            /*
             * S091 P3: the live runtime decides the law (CrumpBit clamps by
             * its resolved sub-engine).
             */
            uint8_t limit = entry->ops->effective_max(effects_runtimeMember(),
                                                      index, &share);

            if (value > limit)
                value = limit;
        }
        /*
         * S091 P3: a BUFFER_DEPENDENT row is re-applied every block, changed
         * or not, so its clamp follows a sub-type switch or a share change
         * one block later without extra manager state (≈ 10 instructions per
         * block for CrumpBit's `rte`).
         */
        if (!effects_state.force_all &&
            (descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) == 0u &&
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

/*
 * One drain block (S095 P1 plan §5.5, D25, F2, F3).
 *
 * What:
 * 1. zero the block's input (both bus channels): the draining type hears
 *    silence, so it plays only what is already in its buffer;
 * 2. run the draining type's process at its frozen rows;
 * 3. sk_drainStep() scales the output by the envelope (1.0 until the last
 *    500 ms of the 6 s ceiling, then linear to 0) and returns 1 when the
 *    drain is over (the block peak stayed below -80 dBFS for 14 blocks, or
 *    the ceiling was reached);
 * 4. at the end: export the final handoff, clear FXBUF_STATE_EFFECT_WRITTEN
 *    (F2: no later feed of a tail already heard), zero the union and make
 *    the runtime OFF; effects_activeIoFlags() then returns 0 and the mixer
 *    drops the bus from the next block.
 * Why: an exact tail costs exactly the draining type's own budget, which
 * it already fits (the skipped descriptor loop frees ≈ 2,000-3,000 more).
 * Cost: the zeroing (64 stores) plus sk_drainStep() (≈ 6 per frame: two
 * multiplies, two abs/max) per block.
 * Inputs: the draining entry, io. Outputs: io, effects_state, the handoff.
 * Caller: effects_process(). Affiliates: sk_drainBegin(), sk_drainStep().
 */
static __attribute__((noinline)) void effects_drainProcess(
    const effect_registry_entry_t *entry, effect_io_t *io)
{
    uint32_t n;

    for (n = 0u; n < io->frames; n++) {
        io->l[n] = 0.0f;
        if (io->r)
            io->r[n] = 0.0f;
    }
    entry->ops->process(effects_runtimeMember(), io);
    if (sk_drainStep(io)) {
        fxbuf_handoff_t *h = effects_exportHandoff();

        h->state_flags &= (uint8_t)~FXBUF_STATE_EFFECT_WRITTEN;
        effects_state.reserved = 0u;
        memset(&effects_runtime, 0, sizeof(effects_runtime));
        effects_state.runtime_type = EFFECT_TYPE_OFF;
        effects_state.force_all = 1u;
    }
}

/*
 * Process one Effect block (S095 P1: with the drain and the stopped-
 * transport idle).
 *
 * What: the live type's process on the mixer's normalized bus. While
 * draining (D25), effects_drainProcess() zeroes the input first, runs the
 * draining type, scales its output by the drain envelope and ends the drain
 * when the tail is done: -80 dBFS held for 10 ms, or the 6 s ceiling (D16,
 * F3).
 * Why: into OFF the outgoing type keeps rendering its own exact tail; no
 * emulation (plan §5.5).
 * Inputs: io (the mixer's bus, the share). Output: io in place.
 * Caller: mixer_calcNextSampleBlock() after effects_service().
 */
/* Out of line (S095 P1): the mixer's code keeps its shape (elf_p4_check). */
__attribute__((noinline)) void effects_process(effect_io_t *io)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_state.runtime_type);

    if (!entry || !entry->ops || !entry->ops->process || !io)
        return;
    if (effects_draining()) {
        effects_drainProcess(entry, io);
        return;
    }
    {
        /*
         * S095 P1 (§S18, user): with the transport stopped every type keeps
         * processing until its input and output stay below -90 dBFS, then
         * its return ramps to 0 over 1 s (the mixer, sk_idleGain()) and it
         * stops processing until audio comes back (sk_idleBefore()).
         */
        const uint8_t running = seq_isRunning();

        if (sk_idleBefore(io, running))
            return;
        entry->ops->process(effects_runtimeMember(), io);
        if (!running)
            sk_idleAfter(io);
    }
}

/* The Effect return's idle gain (1, or the stopped-transport ramp to 0). */
float effects_returnGain(void)
{
    fx_share_t share;

    fxbuf_effectShare(&share);
    return sk_idleGain(&share);
}

/* S095 P1 (SD4): the logical type, OFF while a drain runs. */
effect_type_id_t effects_activeType(void)
{
    return effects_logicalType();
}

/* S095 P1: while draining, runtime_type is the draining type, so its io flags
 * keep the mixer's return path until the drain ends. */
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
