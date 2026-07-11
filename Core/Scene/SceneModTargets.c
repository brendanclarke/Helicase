#include "SceneModTargets.h"
#include "MenuText.h"
#include <string.h>

#define SCENE_MOD_TARGET_BASE ((uint16_t)INSTRUMENT_VOICE_ID_COUNT)
#define SCENE_MOD_TARGET_ID(index_) \
    ((scene_mod_target_id_t)(SCENE_MOD_TARGET_BASE + (uint16_t)(index_)))
#define SCENE_MOD_TARGET_COUNT \
    ((uint8_t)(sizeof(scene_mod_targets) / sizeof(scene_mod_targets[0])))

/*
 * Scene target metadata.
 *
 * Inputs: none at runtime; this immutable table defines the Scene-target
 * namespace. Output: the table order is also picker order. Per-voice Morph
 * targets intentionally come before global Scene Decimation so the velocity
 * list does not show voice-local instrument_decimation "srt" immediately
 * beside Scene Decimation "srt" after the voice descriptor portion.
 */
static const scene_mod_target_descriptor_t scene_mod_targets[] = {
    { SCENE_MOD_TARGET_ID(0u), SCENE_MOD_TARGET_KIND_VOICE_MORPH, 0u,
      0u, 255u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Voice", "1 Morph", "1vm" },
    { SCENE_MOD_TARGET_ID(1u), SCENE_MOD_TARGET_KIND_VOICE_MORPH, 1u,
      0u, 255u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Voice", "2 Morph", "2vm" },
    { SCENE_MOD_TARGET_ID(2u), SCENE_MOD_TARGET_KIND_VOICE_MORPH, 2u,
      0u, 255u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Voice", "3 Morph", "3vm" },
    { SCENE_MOD_TARGET_ID(3u), SCENE_MOD_TARGET_KIND_VOICE_MORPH, 3u,
      0u, 255u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Voice", "4 Morph", "4vm" },
    { SCENE_MOD_TARGET_ID(4u), SCENE_MOD_TARGET_KIND_VOICE_MORPH, 4u,
      0u, 255u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Voice", "5 Morph", "5vm" },
    { SCENE_MOD_TARGET_ID(5u), SCENE_MOD_TARGET_KIND_VOICE_MORPH, 5u,
      0u, 255u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Voice", "6 Morph", "6vm" },
    { SCENE_MOD_TARGET_ID(6u), SCENE_MOD_TARGET_KIND_DECIMATION_ALL, 0xffu,
      0u, 127u, SCENE_MOD_TARGET_USE_VELOCITY | SCENE_MOD_TARGET_USE_LFO,
      "Scene", "SampleRt", "srt" },
};

static void sceneModTarget_copyPadded(char *dst, const char *src, uint8_t width)
{
    uint8_t i = 0u;

    /*
     * Copy normal C strings into fixed LCD fields.
     *
     * Inputs: destination field, source string, and exact field width. Output:
     * dst is filled with source characters up to width or terminator, then
     * padded with spaces. This local helper keeps Scene target rendering
     * independent from menu.c internals while preserving the same no-leak
     * display contract used by descriptor target labels.
     */
    if (src) {
        while (i < width && src[i]) {
            dst[i] = src[i];
            i++;
        }
    }
    while (i < width) {
        dst[i] = ' ';
        i++;
    }
}

uint8_t sceneModTarget_isSceneTarget(uint16_t id)
{
    return (uint8_t)(id >= SCENE_MOD_TARGET_BASE &&
                     id < (uint16_t)(SCENE_MOD_TARGET_BASE +
                                      SCENE_MOD_TARGET_COUNT));
}

const scene_mod_target_descriptor_t *sceneModTarget_descriptor(uint16_t id)
{
    /*
     * Decode one stored Scene target ID.
     *
     * Input: any 16-bit target value from Menu/Scene storage. Output: the
     * immutable descriptor for valid Scene target IDs, otherwise NULL. The ID
     * range starts after the packed voice descriptor range so stale or
     * instrument-local IDs cannot be mistaken for Scene settings.
     */
    if (!sceneModTarget_isSceneTarget(id))
        return 0;
    return &scene_mod_targets[id - SCENE_MOD_TARGET_BASE];
}

uint8_t sceneModTarget_valid(uint16_t id, scene_mod_target_use_t use)
{
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(id);

    /*
     * Validate a Scene target for one modulation use.
     *
     * Inputs: stored target ID and requested use flag. Output: nonzero only
     * when the ID belongs to the Scene namespace and the target opts into that
     * use. Future effects can be added to the table without changing Menu or
     * InstrumentManager traversal logic.
     */
    return (uint8_t)(descriptor &&
                     ((descriptor->use_flags & (uint8_t)use) != 0u));
}

uint16_t sceneModTarget_step(uint16_t current, int8_t direction,
                             scene_mod_target_use_t use)
{
    uint8_t current_valid = 0u;
    uint8_t current_index = 0u;

    /*
     * Walk the Scene target list with a single off position owned by caller.
     *
     * Inputs: current stored target, signed direction, and requested use.
     * Output: next valid Scene target or INSTRUMENT_PARAM_INVALID when moving
     * below the first target/off. The function does not wrap; callers combine
     * this Scene-only walk with voice-local descriptor walks as needed.
     */
    if (direction == 0)
        return current;

    if (sceneModTarget_valid(current, use)) {
        current_valid = 1u;
        current_index = (uint8_t)(current - SCENE_MOD_TARGET_BASE);
    }

    if (direction > 0) {
        uint8_t i = current_valid ? (uint8_t)(current_index + 1u) : 0u;
        while (i < SCENE_MOD_TARGET_COUNT) {
            if (sceneModTarget_valid(scene_mod_targets[i].id, use))
                return scene_mod_targets[i].id;
            i++;
        }
        return current_valid ? current : INSTRUMENT_PARAM_INVALID;
    }

    if (!current_valid)
        return INSTRUMENT_PARAM_INVALID;
    while (current_index > 0u) {
        current_index--;
        if (sceneModTarget_valid(scene_mod_targets[current_index].id, use))
            return scene_mod_targets[current_index].id;
    }
    return INSTRUMENT_PARAM_INVALID;
}

void sceneModTarget_formatShort(uint16_t id, char out[3])
{
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(id);

    /*
     * Format compact Scene target text.
     *
     * Inputs: stored Scene target ID and a three-character output field.
     * Output: the target's short label, or "off" for stale/non-Scene IDs.
     * Menu uses this beside instrument descriptor short-name rendering so both
     * target namespaces can share target-picker widgets without sharing tables.
     */
    if (!descriptor) {
        memcpy(out, menuText_off, 3);
        return;
    }
    sceneModTarget_copyPadded(out, descriptor->short_name, 3u);
}

void sceneModTarget_formatFull(uint16_t id, char out_category[8],
                               char out_long[8])
{
    const scene_mod_target_descriptor_t *descriptor =
        sceneModTarget_descriptor(id);

    /*
     * Format full Scene target text for single-parameter edit view.
     *
     * Inputs: stored Scene target ID and fixed eight-character category/long
     * fields. Output: padded display fields or "off" when invalid. This mirrors
     * descriptor target rendering but reads Scene target metadata instead of
     * the active slot's instrument descriptor table.
     */
    if (!descriptor) {
        memcpy(out_category, menuText_off, 3);
        return;
    }
    sceneModTarget_copyPadded(out_category, descriptor->category, 8u);
    sceneModTarget_copyPadded(out_long, descriptor->long_name, 8u);
}
