#ifndef SCENE_MOD_TARGETS_H_
#define SCENE_MOD_TARGETS_H_

#include <stdint.h>
#include "InstrumentManager.h"

typedef uint16_t scene_mod_target_id_t;

typedef enum {
    SCENE_MOD_TARGET_KIND_VOICE_MORPH = 0,
    SCENE_MOD_TARGET_KIND_DECIMATION_ALL,
    SCENE_MOD_TARGET_KIND_EFFECT_PARAMETER
} scene_mod_target_kind_t;

typedef enum {
    SCENE_MOD_TARGET_USE_VELOCITY = 1u << 0,
    SCENE_MOD_TARGET_USE_LFO      = 1u << 1
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
 * this module owns only Scene-level targets such as per-voice Morph, global
 * decimation, and future effects parameters.
 *
 * Inputs to the helpers are stored target IDs from menu/Scene parameter cells.
 * Outputs are validation, display text, and decoded range/kind metadata used by
 * Menu, InstrumentManager, velocity trigger handling, LFO dispatch, and the
 * Morph worker. Keeping the namespace here prevents hardcoded Scene target
 * lists from spreading into Menu or DSP code.
 */
uint8_t sceneModTarget_isSceneTarget(uint16_t id);
const scene_mod_target_descriptor_t *sceneModTarget_descriptor(uint16_t id);
uint8_t sceneModTarget_valid(uint16_t id, scene_mod_target_use_t use);
uint16_t sceneModTarget_step(uint16_t current, int8_t direction,
                             scene_mod_target_use_t use);
void sceneModTarget_formatShort(uint16_t id, char out[3]);
void sceneModTarget_formatFull(uint16_t id, char out_category[8],
                               char out_long[8]);

#endif /* SCENE_MOD_TARGETS_H_ */
