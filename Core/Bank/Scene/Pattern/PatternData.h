/*
 * PatternData.h
 *
 * Scene-owned, fixed-grid trigger bitmap API.
 */

#ifndef PATTERNDATA_H_
#define PATTERNDATA_H_

#include <stdint.h>
#include "globals.h"
#include "InstrumentManager.h"

/* The grid stays 128 steps wide for eight visible 16-step bars. */
#define NUM_TRACKS 7u
#define NUM_STEPS 128u
#define NUM_BARS 8u
#define NUM_STEPS_PER_BAR 16u
#define PATTERN_TRACK_BYTES (NUM_STEPS / 8u)

/*
 * PatternSet is the complete persistent pattern payload for one Scene.
 *
 * Each `step_on[track][step >> 3]` byte owns eight chronological steps; bit
 * `step & 7` is the on/off state for that step, with bit zero representing the
 * lowest-numbered step. Inputs/outputs are deliberately only bitmap bits.
 * SceneData embeds this type, while Sequencer, filesystem, generators, and UI
 * must use the bounded helpers below instead of adding parallel pattern state.
 */
typedef struct PatternSetStruct {
    uint8_t step_on[NUM_TRACKS][PATTERN_TRACK_BYTES];
} PatternSet;

_Static_assert(sizeof(PatternSet) == 112u,
               "PatternSet must remain seven 16-byte trigger bitmaps");

/* Coordinate validation for Scene, track, and fixed-grid step indices. */
uint8_t pat_trackValid(uint8_t track);
uint8_t pat_patternValid(uint8_t scene_index);
uint8_t pat_stepValid(uint8_t step);

/*
 * Read or write one caller-owned PatternSet bit without exposing its layout.
 * Inputs are a PatternSet and bounded track/step coordinates; get returns zero
 * and set returns zero when invalid. These helpers let storage parsing operate
 * on staged/final Scene memory without duplicating the bit-order contract.
 */
uint8_t pat_patternSetGetStep(const PatternSet *pattern, uint8_t track,
                              uint8_t step);
uint8_t pat_patternSetSetStep(PatternSet *pattern, uint8_t track,
                              uint8_t step, uint8_t on);

/*
 * Initialize one 112-byte PatternSet to silence. SceneData calls the
 * Scene-indexed wrapper for resident memory; filesystem uses the generic form
 * for its selected final Scene target. Neither operation creates defaults for
 * removed note, velocity, probability, timing, or automation fields.
 */
void pat_initPatternSet(PatternSet *pattern);
void pat_initScene(uint8_t scene_index);

/* Scene-indexed playback and edit operations for the trigger bitmap. */
uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index);
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index);
void pat_setStepActive(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t on);
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step);
uint8_t pat_sceneHasActiveSteps(uint8_t scene_index);

/*
 * Bitmap-range operations for UI and generators. Track copy moves 16 bytes,
 * pattern copy moves 112 bytes, and bar copy moves exactly two bytes. Their
 * inputs are validated Scene/track/bar coordinates and their only output is
 * the selected bitmap range; no removed track-length metadata is recreated.
 */
void pat_clearTrack(uint8_t scene_index, uint8_t track);
void pat_clearPattern(uint8_t scene_index);
void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track);
void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene);
void pat_copyBar(uint8_t scene_index, uint8_t track, uint8_t src_bar,
                 uint8_t dst_bar);

/*
 * Transitional UI compatibility entry points retain no PatternSet state.
 * Inputs from stale menu cells are ignored (apart from active-step cursor
 * selection); outputs are fixed defaults. They exist only until the menu ID
 * table is compacted and must never be used by playback, persistence, or UI.
 */
#define TRACK_SCALE_OFF 10u
void pat_applyPatternSettingsToMenu(uint8_t scene_index);
void pat_applyTrackSettingsToMenu(uint8_t scene_index, uint8_t track);
void pat_setTrackLength(uint8_t scene_index, uint8_t track, uint8_t value);
void pat_setTrackScale(uint8_t scene_index, uint8_t track, uint8_t value);
void pat_setTrackShuffle(uint8_t scene_index, uint8_t track, uint8_t value);
void pat_setActiveAutomationTrack(uint8_t value);
void pat_setSelectedStep(uint8_t step);
void pat_setStepAutomationDestination(uint8_t scene_index, uint8_t track,
                                      uint8_t step, uint8_t slot, uint16_t value);
void pat_setStepAutomationValue(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t slot, uint8_t value);
void pat_setPatternChangeBar(uint8_t scene_index, uint8_t value);
void pat_setPatternNext(uint8_t scene_index, uint8_t value);
void pat_applyStepToMenu(uint8_t scene_index, uint8_t track, uint8_t step);
void pat_setStepProbability(uint8_t scene_index, uint8_t track, uint8_t step, uint8_t value);
void pat_setStepNote(uint8_t scene_index, uint8_t track, uint8_t step, uint8_t value);
void pat_setStepVolume(uint8_t scene_index, uint8_t track, uint8_t step, uint8_t value);

#endif /* PATTERNDATA_H_ */
