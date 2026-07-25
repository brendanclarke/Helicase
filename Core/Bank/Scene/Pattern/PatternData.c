/*
 * PatternData.c
 *
 * The only persistent pattern state is one fixed-grid on/off bitmap per Scene.
 */

#include "PatternData.h"
#include "SceneData.h"

#include <string.h>

uint8_t pat_trackValid(uint8_t track)
{
    return (uint8_t)(track < NUM_TRACKS);
}

uint8_t pat_patternValid(uint8_t scene_index)
{
    return scene_indexValid(scene_index);
}

uint8_t pat_stepValid(uint8_t step)
{
    return (uint8_t)(step < NUM_STEPS);
}

uint8_t pat_patternSetGetStep(const PatternSet *pattern, uint8_t track,
                              uint8_t step)
{
    /*
     * Return one staged or resident trigger bit.
     *
     * Inputs: PatternSet plus track/step coordinates. Output: zero or one;
     * invalid inputs return zero. StorageTypes uses this boundary so file code
     * never depends on a public raw-byte layout, while Sequencer/UI use the
     * Scene-indexed wrapper below.
     */
    if (!pattern || !pat_trackValid(track) || !pat_stepValid(step))
        return 0u;
    return (uint8_t)((pattern->step_on[track][step >> 3u] >> (step & 7u)) & 1u);
}

uint8_t pat_patternSetSetStep(PatternSet *pattern, uint8_t track,
                              uint8_t step, uint8_t on)
{
    uint8_t *byte;
    uint8_t mask;

    /*
     * Set one staged or resident trigger bit without allocating Step data.
     *
     * Inputs: PatternSet, bounded coordinate, and boolean on state. Output:
     * one bitmap bit is updated and success is returned; invalid input leaves
     * storage untouched. Filesystem and generators are affiliates because this
     * is their shared representation boundary.
     */
    if (!pattern || !pat_trackValid(track) || !pat_stepValid(step))
        return 0u;
    byte = &pattern->step_on[track][step >> 3u];
    mask = (uint8_t)(1u << (step & 7u));
    if (on)
        *byte |= mask;
    else
        *byte &= (uint8_t)~mask;
    return 1u;
}

void pat_initPatternSet(PatternSet *pattern)
{
    /*
     * Clear a complete Scene pattern payload.
     *
     * Input: caller-owned 112-byte PatternSet. Output: all seven tracks are
     * silent. SceneData and filesystem call this for resident/final load
     * targets; no default Step records or timing fields are retained.
     */
    if (pattern)
        memset(pattern, 0, sizeof(*pattern));
}

void pat_initScene(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Initialize persistent pattern storage for one validated Scene.
     *
     * Input: resident Scene index. Output: that Scene's 112-byte bitmap is
     * cleared. SceneData owns the lifecycle and invokes this exactly once per
     * Scene during initialization; Sequencer does not reinitialize it.
     */
    if (scene)
        pat_initPatternSet(&scene->pattern);
}

uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Playback-safe Scene wrapper for one bitmap query. */
    return scene ? pat_patternSetGetStep(&scene->pattern, track, step) : 0u;
}

void pat_setStepActive(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t on)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Apply an on/off edit to one resident Scene bit.
     *
     * Inputs: Scene/track/step and desired state. Output: only the matching
     * trigger bit changes. Sequencer recording/live erase, button UI, and
     * Euclidean transfer share this operation; invalid scenes are ignored.
     */
    if (scene)
        (void)pat_patternSetSetStep(&scene->pattern, track, step, on);
}

void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /* Toggle one resident bit while keeping bit ordering private to this API. */
    if (scene && pat_trackValid(track) && pat_stepValid(step))
        (void)pat_patternSetSetStep(&scene->pattern, track, step,
                                    (uint8_t)!pat_patternSetGetStep(
                                        &scene->pattern, track, step));
}

void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    /* Live erase is exactly a clear-bit operation in the reduced model. */
    pat_setStepActive(scene_index, track, step, 0u);
}

uint8_t pat_sceneHasActiveSteps(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t track;
    uint8_t byte;

    /*
     * Report whether a Scene contains any trigger bit.
     *
     * Input: resident Scene index. Output: nonzero at the first nonzero bitmap
     * byte, otherwise zero. Menu load feedback uses this owner-level scan
     * rather than learning PatternSet layout.
     */
    if (!scene)
        return 0u;
    for (track = 0u; track < NUM_TRACKS; track++)
        for (byte = 0u; byte < PATTERN_TRACK_BYTES; byte++)
            if (scene->pattern.step_on[track][byte])
                return 1u;
    return 0u;
}

void pat_clearTrack(uint8_t scene_index, uint8_t track)
{
    scene_t *scene = scene_get(scene_index);

    /* Clear one 16-byte track bitmap; no removed automation lanes are touched. */
    if (scene && pat_trackValid(track))
        memset(scene->pattern.step_on[track], 0, PATTERN_TRACK_BYTES);
}

void pat_clearPattern(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /* Clear only the PatternSet of one Scene, preserving its kit and settings. */
    if (scene)
        pat_initPatternSet(&scene->pattern);
}

void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track)
{
    scene_t *scene = scene_get(scene_index);

    /* Copy precisely one track's 16-byte trigger bitmap within a Scene. */
    if (scene && pat_trackValid(src_track) && pat_trackValid(dst_track))
        memcpy(scene->pattern.step_on[dst_track],
               scene->pattern.step_on[src_track], PATTERN_TRACK_BYTES);
}

void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene)
{
    scene_t *src = scene_get(src_scene);
    scene_t *dst = scene_get(dst_scene);

    /* Copy the full 112-byte bitmap between Scenes, not Scene settings or kit. */
    if (src && dst)
        memcpy(&dst->pattern, &src->pattern, sizeof(dst->pattern));
}

void pat_copyBar(uint8_t scene_index, uint8_t track, uint8_t src_bar,
                 uint8_t dst_bar)
{
    scene_t *scene = scene_get(scene_index);
    uint8_t src_byte;
    uint8_t dst_byte;

    /*
     * Copy one visible 16-step bar as exactly two bytes.
     *
     * Inputs: Scene, track, and source/destination bars 0..7. Output: the two
     * destination bitmap bytes are overwritten. Copy/Clear UI is the client;
     * no track-length extension is possible because timing metadata is gone.
     */
    if (!scene || !pat_trackValid(track) || src_bar >= NUM_BARS ||
        dst_bar >= NUM_BARS)
        return;
    src_byte = (uint8_t)(src_bar * 2u);
    dst_byte = (uint8_t)(dst_bar * 2u);
    memcpy(&scene->pattern.step_on[track][dst_byte],
           &scene->pattern.step_on[track][src_byte], 2u);
}

/* Legacy menu bridge calls are intentionally storage-free while menus migrate. */
void pat_applyPatternSettingsToMenu(uint8_t s) { (void)s; }
void pat_applyTrackSettingsToMenu(uint8_t s, uint8_t t) { (void)s; (void)t; }
void pat_setTrackLength(uint8_t s, uint8_t t, uint8_t v) { (void)s; (void)t; (void)v; }
void pat_setTrackScale(uint8_t s, uint8_t t, uint8_t v) { (void)s; (void)t; (void)v; }
void pat_setTrackShuffle(uint8_t s, uint8_t t, uint8_t v) { (void)s; (void)t; (void)v; }
void pat_setActiveAutomationTrack(uint8_t v) { (void)v; }
void pat_setSelectedStep(uint8_t step) { (void)step; }
void pat_setStepAutomationDestination(uint8_t s,uint8_t t,uint8_t p,uint8_t l,uint16_t v) {(void)s;(void)t;(void)p;(void)l;(void)v;}
void pat_setStepAutomationValue(uint8_t s,uint8_t t,uint8_t p,uint8_t l,uint8_t v) {(void)s;(void)t;(void)p;(void)l;(void)v;}
void pat_setPatternChangeBar(uint8_t s,uint8_t v) {(void)s;(void)v;}
void pat_setPatternNext(uint8_t s,uint8_t v) {(void)s;(void)v;}
void pat_applyStepToMenu(uint8_t s,uint8_t t,uint8_t p) {(void)s;(void)t;(void)p;}
void pat_setStepProbability(uint8_t s,uint8_t t,uint8_t p,uint8_t v) {(void)s;(void)t;(void)p;(void)v;}
void pat_setStepNote(uint8_t s,uint8_t t,uint8_t p,uint8_t v) {(void)s;(void)t;(void)p;(void)v;}
void pat_setStepVolume(uint8_t s,uint8_t t,uint8_t p,uint8_t v) {(void)s;(void)t;(void)p;(void)v;}
