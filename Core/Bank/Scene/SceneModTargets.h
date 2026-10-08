#ifndef SCENE_MOD_TARGETS_H_
#define SCENE_MOD_TARGETS_H_

#include <stdint.h>
#include "InstrumentManager.h"

typedef uint16_t scene_mod_target_id_t;

typedef enum {
    SCENE_MOD_TARGET_KIND_VOICE_MORPH = 0,
    /* Retired `srt` placeholder; ID 390 remains reserved (S075). */
    SCENE_MOD_TARGET_KIND_RETIRED,
    /*
     * Generated slot-6 alternate decay.
     *
     * This Scene target is not owned by an instrument descriptor because it is
     * created when a non-Choke instrument on slot 6 needs a track-7 alternate
     * amp_envelope_decay. Inputs/outputs route through kit_settings_t rather
     * than through a slot descriptor, while InstrumentManager consumes the
     * value at trigger time.
     */
    SCENE_MOD_TARGET_KIND_SLOT6_TRACK7_AMP_DECAY,
    /*
     * Per-voice audio output route.
     *
     * Inputs: voice_slot selects the instrument and the stored value is a
     * mixer route enum. Output: step automation reaches the retained Scene
     * route through preset_setVoiceAudioOut(); this discrete selector is not
     * exposed to velocity or LFO modulation.
     */
    SCENE_MOD_TARGET_KIND_AUDIO_OUT,
    /*
     * Per-voice FX send amount.
     *
     * Inputs: voice_slot and a 0..127 stored value. Output: step automation
     * sets the runtime send overlay; the mixer applies the effective send to
     * the live FX bus each block (Session 072 step 5).
     */
    SCENE_MOD_TARGET_KIND_FX_SEND,
    /*
     * Scene Effect Morph amount, `fxm` (ID 404).
     *
     * Effect parameter cells are separate block-7 targets; this kind is only
     * the Scene-level Morph amount. Step automation and LFO apply runtime
     * overlays through EffectsManager; velocity is not offered.
     */
    SCENE_MOD_TARGET_KIND_EFFECT_MORPH,
    /*
     * Per-track step-automation targets (S078 §5.5).
     *
     * Inputs: voice_slot carries the track index 0..6; the stored 7-bit value
     * is a length (1..128), scale CC (0..127), or shuffle (0..127). Output:
     * step automation sets a runtime-only overlay consumed by
     * presetMorphEngine.c's effective track getters, so the retained Pattern
     * Normal values are never overwritten. Why: these three are the morphable
     * track timing parameters, offered to the Scene automation category only
     * (no velocity/LFO flags). Affiliates: SceneModTargets.c table rows
     * 405..425, seq_applySceneAutomation(),
     * presetMorph_setTrackParamStepOverride().
     */
    SCENE_MOD_TARGET_KIND_TRACK_LENGTH,
    SCENE_MOD_TARGET_KIND_TRACK_SCALE,
    SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE
} scene_mod_target_kind_t;

typedef enum {
    SCENE_MOD_TARGET_USE_VELOCITY   = 1u << 0,
    SCENE_MOD_TARGET_USE_LFO        = 1u << 1,
    /*
     * Step automation eligibility.
     *
     * Inputs: picker and Pattern validation paths request this flag. Output:
     * only targets explicitly owned by the step-automation runtime path are
     * offered to the `scn` category; velocity/LFO eligibility stays separate.
     */
    SCENE_MOD_TARGET_USE_AUTOMATION = 1u << 2
} scene_mod_target_use_t;

typedef struct {
    scene_mod_target_id_t id;
    scene_mod_target_kind_t kind;
    uint8_t voice_slot;
    uint16_t min_value;
    uint16_t max_value;
    uint8_t use_flags;
    const char *category;
    const char *long_name;
    const char *short_name;
} scene_mod_target_descriptor_t;

/*
 * Scene mod targets are sound-affecting destinations that are not owned by an
 * instrument descriptor table. Voice descriptor targets stay in
 * InstrumentManager so swappable instruments keep their own parameter lists;
 * this module owns only Scene-level targets such as per-voice Morph, audio
 * routing, FX sends, and Effect Morph (`fxm`). Effect
 * parameters are block-7 IDs owned by EffectsManager.
 *
 * Inputs to the helpers are stored target IDs from menu/Scene parameter cells.
 * Outputs are validation, display text, and decoded range/kind metadata used by
 * Menu, InstrumentManager, velocity trigger handling, LFO dispatch, and the
 * Morph worker. Keeping the namespace here prevents hardcoded Scene target
 * lists from spreading into Menu or DSP code.
 */
uint8_t sceneModTarget_isSceneTarget(uint16_t id);
/*
 * Return the immutable descriptor for one Scene target ID.
 *
 * Input: a stored target ID from Menu, Scene storage, or an installed
 * modulation destination. Output: descriptor metadata for valid Scene targets,
 * or NULL for off/voice/stale IDs. Common clients are Menu display and
 * InstrumentManager apply code.
 */
const scene_mod_target_descriptor_t *sceneModTarget_descriptor(uint16_t id);
/*
 * Validate one Scene target for a requested use.
 *
 * Inputs: stored target ID and a SCENE_MOD_TARGET_USE_* flag. Output: nonzero
 * only when the ID is in the Scene namespace and the descriptor opts into that
 * modulation use. This keeps velocity/LFO picker filtering out of Menu.
 */
uint8_t sceneModTarget_valid(uint16_t id, scene_mod_target_use_t use);
/*
 * Resolve the Scene target ID for one voice's Morph amount.
 *
 * Input: zero-based voice slot. Output: the canonical Scene target ID for that
 * voice Morph parameter, or INSTRUMENT_PARAM_INVALID for invalid slots. This
 * keeps velocity's retained own-Morph byte token from depending on the private
 * ordering of the Scene target table.
 */
uint16_t sceneModTarget_voiceMorphId(uint8_t voice_slot);
/*
 * Resolve the Scene target ID of the Effect Morph amount `fxm` (S075).
 *
 * Output: the canonical ID (404 today) found by kind, so callers do not depend
 * on table order; INSTRUMENT_PARAM_INVALID if the row is missing. Clients:
 * Menu's pot-clear target resolver (PERF `fxm`, Effect page `mrp`).
 */
uint16_t sceneModTarget_effectMorphId(void);
/*
 * Resolve the Scene target ID of the generated slot-6/track-7 decay `7dc`
 * (S075).
 *
 * Output: the canonical ID found by kind, or INSTRUMENT_PARAM_INVALID.
 * Clients: Menu's pot-clear target resolver (VOICE7 generated decay cell) and
 * copy/clear retargeting of track-7 alternates (copyOps.c).
 */
uint16_t sceneModTarget_slot6DecayId(void);
/*
 * Convert Scene target IDs to compact Scene-namespace indices and back.
 *
 * LFO target storage uses lfo_target_voice=scn plus a byte index token. These
 * helpers are the storage-facing adapter for the wider runtime Scene target ID
 * namespace.
 */
uint8_t sceneModTarget_indexFromId(uint16_t id, uint8_t *index_out);
uint16_t sceneModTarget_idFromIndex(uint8_t index);
uint8_t sceneModTarget_count(void);
/*
 * Walk the Scene target list in display order.
 *
 * Inputs: current Scene target or off/stale value, signed direction, and use
 * flag. Output: next valid Scene target or INSTRUMENT_PARAM_INVALID for off.
 * The walk does not wrap; callers combine this with voice-local descriptor
 * traversal for mixed target lists.
 */
uint16_t sceneModTarget_step(uint16_t current, int8_t direction,
                             scene_mod_target_use_t use);
/*
 * Format the three-character Scene target short label.
 *
 * Inputs: stored Scene target ID and a 3-byte output field. Output: padded
 * target short name, or the shared `off` text when invalid. Menu uses this for
 * compact target cells.
 */
void sceneModTarget_formatShort(uint16_t id, char out[3]);
/*
 * Format the full Scene target edit label.
 *
 * Inputs: stored Scene target ID and two 8-byte output fields. Output: padded
 * category and long-name fields, or `off` in the category field when invalid.
 * This mirrors descriptor full-label rendering without exposing the table to
 * Menu.
 */
void sceneModTarget_formatFull(uint16_t id, char out_category[8],
                               char out_long[8]);

#endif /* SCENE_MOD_TARGETS_H_ */
