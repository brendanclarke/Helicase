/*
 * PatternData.h
 *
 * Scene-indexed fixed-grid trigger and dynamic-pattern storage API.
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
 * Address-array encoding for the live Session-062 Pattern representation.
 *
 * Every 16-bit entry owns one track/step: bit 15 is the trigger state, bit 14
 * announces a dynamic block with specials, and bits 13..0 are a
 * 4-byte-aligned pool byte offset. PAT_ADDR_SENTINEL is the reserved no-data
 * value; it cannot be a valid aligned offset. These constants are shared by
 * PatternData and future Sequencer/pool readers so no caller repeats masks.
 * Inputs/outputs: compile-time values only. Affiliate: PatternData.c.
 */
#define PAT_ADDR_SENTINEL     0x3FFFu
#define PAT_ADDR_TRIGGER_BIT  (1u << 15)
#define PAT_ADDR_SPECIALS_BIT (1u << 14)
#define PAT_ADDR_OFFSET_MASK  0x3FFFu

/*
 * Special-flags byte assignments for one dynamic pool block.
 *
 * Bit 0 stores a note override, bit 1 stores a velocity override, and bit 2
 * stores a probability override. Bits 3..7 remain reserved and are kept clear
 * by this session's writer. Inputs/outputs are compile-time masks only;
 * PatternData.c and the Sequencer use them to agree on value-byte ordering.
 * Affiliates: pat_blockRead(), pat_blockWrite(), and pat_readStepSpecials().
 */
#define PAT_SPECIAL_NOTE_BIT     (1u << 0)
#define PAT_SPECIAL_VEL_BIT      (1u << 1)
#define PAT_SPECIAL_PROB_BIT     (1u << 2)
#define PAT_SPECIAL_FLAGS_MASK   (PAT_SPECIAL_NOTE_BIT | \
                                  PAT_SPECIAL_VEL_BIT | \
                                  PAT_SPECIAL_PROB_BIT)

/*
 * Dynamic pool block header encoding.
 *
 * The first two bytes carry a 10-bit `track * NUM_STEPS + step` back-reference
 * in bits 15..6 and a six-bit automation count in bits 5..0. Automation is
 * zero for Session 062, but retaining the field shape keeps future block
 * readers compatible. Inputs/outputs are compile-time constants used by the
 * allocator's block-size and read/write helpers. Affiliate: PatternData.c.
 */
#define PAT_BLOCK_HEADER_BYTES    2u
#define PAT_BLOCK_STEP_ID_SHIFT   6u
#define PAT_BLOCK_STEP_ID_MASK    0xFFC0u
#define PAT_BLOCK_AUTO_COUNT_MASK 0x003Fu

/* Total live address entries in one resident Scene: 7 tracks x 128 steps. */
#define PAT_STEPS_PER_SCENE   (NUM_TRACKS * NUM_STEPS)

/*
 * PatternSet is the retained legacy v3 file-bridge payload, not resident
 * Scene storage. The live Pattern module owns its address array, pool, and
 * free bitmap separately in PatternData.c; storageTypes.c still needs this
 * 112-byte shape while the v4 file format remains out of scope.
 *
 * Each `step_on[track][step >> 3]` byte owns eight chronological steps; bit
 * `step & 7` is the on/off state for that step, with bit zero representing the
 * lowest-numbered step. Inputs/outputs are deliberately only bitmap bits.
 * Filesystem and storageTypes use the bounded helpers below to parse/write the
 * legacy bridge without coupling future live storage to this layout.
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
 * Read or write one caller-owned legacy PatternSet bit without exposing its
 * layout. Inputs are a PatternSet and bounded track/step coordinates; get
 * returns zero and set returns zero when invalid. These helpers are retained
 * only for the disconnected v3 storage bridge.
 */
uint8_t pat_patternSetGetStep(const PatternSet *pattern, uint8_t track,
                              uint8_t step);
uint8_t pat_patternSetSetStep(PatternSet *pattern, uint8_t track,
                              uint8_t step, uint8_t on);

/*
 * Initialize one 112-byte legacy PatternSet to silence. The generic helper is
 * used by the filesystem discard bridge; pat_initScene() initializes live
 * Scene-indexed address/pool/bitmap storage. Neither operation creates
 * defaults for note, velocity, probability, timing, or automation fields.
 */
void pat_initPatternSet(PatternSet *pattern);
void pat_initScene(uint8_t scene_index);

/*
 * Scene-indexed playback and edit operations for address-array trigger bits.
 * pat_setStepActive() and pat_toggleStep() preserve bits 14..0 so a future
 * pool block survives an on -> off -> on cycle; pat_eraseStep(), clear, and
 * the Scene initializer return entries to PAT_ADDR_SENTINEL.
 */
uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index);
void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index);
void pat_setStepActive(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t on);
void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step);
uint8_t pat_sceneHasActiveSteps(uint8_t scene_index);

/*
 * Resolved values read from one dynamic step block.
 *
 * `note`, `velocity`, and `probability` always contain usable values: absent
 * specials are filled with PAT_DEFAULT_NOTE, PAT_DEFAULT_VELOCITY, and 127.
 * `flags` reports which of those values were explicitly stored. Inputs are a
 * Scene/track/step coordinate; invalid or unallocated steps return defaults.
 * Affiliates: Sequencer playback and the STEP menu display/edit path.
 */
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t probability;
    uint8_t flags;
} pat_step_specials_t;

pat_step_specials_t pat_readStepSpecials(uint8_t scene_index,
                                         uint8_t track, uint8_t step);

/*
 * Range operations for UI and generators. Clear operations reset address
 * entries; copy operations are deliberate no-ops in Session 062 because
 * duplicating pool blocks is deferred to the later copy-operations design.
 * Inputs are validated Scene/track/bar coordinates and invalid calls do no
 * work.
 */
void pat_clearTrack(uint8_t scene_index, uint8_t track);
void pat_clearPattern(uint8_t scene_index);
void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track);
void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene);
void pat_copyBar(uint8_t scene_index, uint8_t track, uint8_t src_bar,
                 uint8_t dst_bar);

/*
 * Transitional UI compatibility entry points retain no legacy PatternSet
 * state. Track/global compatibility setters remain storage-free, while the
 * three Step-062 special setters now read/write the dynamic pool. Inputs from
 * stale menu cells are ignored; outputs for pat_applyStepToMenu() are resolved
 * defaults or stored specials. They exist until the menu ID table is compacted
 * and must never be used by persistence.
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
