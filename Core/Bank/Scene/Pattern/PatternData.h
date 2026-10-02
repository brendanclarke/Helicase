/*
 * PatternData.h
 *
 * Scene-indexed fixed-grid trigger and dynamic-pattern storage API.
 */

#ifndef PATTERNDATA_H_
#define PATTERNDATA_H_

#include <stdint.h>
#include "StepScale.h"
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
 * announces a dynamic block, and bits 13..0 are a
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
 * Resident Pattern storage owned by one Scene.
 *
 * What: the live address array, dynamic pool, allocator bitmap, and the small
 * track/global parameter set persisted by the v4 Pattern file. Why: a Scene
 * load/save must transfer the complete resident Pattern state without the
 * legacy trigger-only bridge. Inputs/outputs: PatternData owns the object
 * and the filesystem streams its fields in the documented v4 order.
 */
typedef struct __attribute__((packed)) {
    uint16_t address[NUM_TRACKS][NUM_STEPS];
    uint8_t pool[PAT_STACK_SIZE * 32u];
    uint8_t bitmap[512u];
    uint8_t track_length[NUM_TRACKS];
    uint8_t track_scale[NUM_TRACKS];
    uint8_t track_shuffle[NUM_TRACKS];
    uint8_t pattern_change_bar;
    uint8_t pattern_next;
} pat_scene_region_t;

_Static_assert(sizeof(pat_scene_region_t) ==
               (PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) +
               512u + 23u,
               "pat_scene_region_t size must match the Pattern budget");

/*
 * Fixed v4 Pattern file geometry.
 *
 * What: compile-time offsets and lengths for the 160-byte base header and
 * resident payload. Why: readers/writers must agree on one bounded stream and
 * may skip a future header extension without moving the payload contract.
 * Inputs/outputs: PAT_STACK_SIZE and the public resident region above. The
 * CRC field is four bytes at offset 14 and is treated as zero while hashing.
 */
#define PATTERN_FILE_VERSION             1u
#define PATTERN_FILE_FIXED_HEADER_BYTES  32u
#define PATTERN_FILE_PARAMETER_BYTES     16u
#define PATTERN_FILE_TRACK_HEADER_BYTES  16u
#define PATTERN_FILE_HEADER_BYTES        160u
#define PATTERN_FILE_CRC_OFFSET          14u
#define PATTERN_FILE_ADDRESS_BYTES       (PAT_STEPS_PER_SCENE * 2u)
#define PATTERN_FILE_BITMAP_BYTES        512u
#define PATTERN_FILE_POOL_BYTES          (PAT_STACK_SIZE * 32u)
#define PATTERN_FILE_PAYLOAD_BYTES       (PATTERN_FILE_ADDRESS_BYTES + \
                                          PATTERN_FILE_BITMAP_BYTES + \
                                          PATTERN_FILE_POOL_BYTES)
#define PATTERN_FILE_TOTAL_BYTES         (PATTERN_FILE_HEADER_BYTES + \
                                          PATTERN_FILE_PAYLOAD_BYTES)

_Static_assert(PATTERN_FILE_HEADER_BYTES ==
               (PATTERN_FILE_FIXED_HEADER_BYTES +
                PATTERN_FILE_PARAMETER_BYTES +
                (NUM_TRACKS * PATTERN_FILE_TRACK_HEADER_BYTES) +
                0u),
               "Pattern v4 header geometry must remain 160 bytes");
_Static_assert(PATTERN_FILE_ADDRESS_BYTES == 1792u,
               "Pattern v4 address payload must remain 1792 bytes");
_Static_assert(PATTERN_FILE_PAYLOAD_BYTES ==
               (PAT_STEPS_PER_SCENE * 2u) + 512u + (PAT_STACK_SIZE * 32u),
               "Pattern v4 payload geometry must match resident storage");

/* Coordinate validation for Scene, track, and fixed-grid step indices. */
uint8_t pat_trackValid(uint8_t track);
uint8_t pat_patternValid(uint8_t scene_index);
uint8_t pat_stepValid(uint8_t step);

void pat_initScene(uint8_t scene_index);

/* Read-only and mutable access to one initialized resident Scene region. */
const pat_scene_region_t *pat_sceneRegion(uint8_t scene_index);
pat_scene_region_t *pat_sceneRegionMut(uint8_t scene_index);

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

/*
 * Service-only dynamic detach that preserves the current trigger bit.
 *
 * What: atomically replace one dynamic address with its sentinel while
 * retaining bit 15, then reclaim the captured pool block. Why: deferred live
 * erase and clear-track barriers must remove pool content without erasing a
 * trigger bit that was set after the barrier was requested. Inputs are valid
 * Scene/track/step coordinates; invalid or trigger-only entries are harmless.
 * Affiliate: PatternStackService.c's DELETE_DYNAMIC and CLEAR_TRACK events.
 */
void pat_releaseStepDynamic(uint8_t scene_index, uint8_t track,
                            uint8_t step);

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

/*
 * One decoded two-byte step-automation entry.
 *
 * What: the canonical nine-bit instrument/Scene target and its seven-bit
 * automation value. Why: PatternData owns the packed pool representation but
 * callers should not depend on its byte layout. Inputs/outputs: public CRUD
 * APIs exchange this bounded value object; `target` is 0..511 and `value` is
 * 0..127. Affiliate: PatternData.c automation block helpers.
 */
typedef struct {
    uint16_t target;
    uint8_t value;
} pat_automation_entry_t;

/*
 * Pattern-only automation-list off sentinel.
 *
 * Inputs: Menu's D17 category transition needs a persistent entry whose VOI
 * category can be changed before a PAR target is selected. Output: this
 * reserved nine-bit target occupies the pool field without truncating
 * INSTRUMENT_PARAM_INVALID (0xffff); PatternData and the sequencer treat it
 * as a no-op. IDs 404..511 are currently outside the canonical voice/Scene
 * target table, and 0x1ff is reserved here for this purpose.
 * Affiliate: PatternStackService admission and Menu step-automation editing.
 */
#define PAT_AUTOMATION_TARGET_OFF 0x01FFu

pat_step_specials_t pat_readStepSpecials(uint8_t scene_index,
                                         uint8_t track, uint8_t step);

/*
 * Whole-track and whole-Pattern clear primitives (legacy direct form).
 *
 * Inputs are validated Scene/track coordinates; invalid calls do no work.
 * Output: address entries reset with detach-before-free order. S075 removed
 * the Session 062 no-op copy stubs (pat_copyTrack/Pattern/Bar) and the
 * interim pat_copyStep(); every copy now runs through the raw block API below
 * under the copy/clear exclusive claim (Core/Menu/CopyClear/).
 */
void pat_clearTrack(uint8_t scene_index, uint8_t track);
void pat_clearPattern(uint8_t scene_index);

/*
 * Raw block API for the exclusive copy/clear holder (S075).
 *
 * Restriction: callers must hold patSvc_beginExclusive(scene) == 1 through
 * patSvc_endExclusive(scene); no other pool writer runs in that window and
 * TIM3/audio never calls these functions. Publication order is kept for every
 * step: new bytes are written into unreferenced pool space, the complete
 * address halfword is published in one PRIMASK store, and only then is the
 * old run freed. PAT_RAW_BLOCK_MAX is the largest encoded block (2-byte
 * header, flags, three specials, 63 automation entries, rounded to 33
 * chunks); caller buffers of that size hold any block. Trigger policies:
 * KEEP re-reads the live trigger bit inside the critical section (paste of
 * automation/specials only), OFF publishes trigger clear, ON publishes trigger
 * set (merge and replace pastes resolve the trigger before publishing).
 */
#define PAT_RAW_BLOCK_MAX   132u
#define PAT_RAW_TRIGGER_KEEP 0u
#define PAT_RAW_TRIGGER_OFF  1u
#define PAT_RAW_TRIGGER_ON   2u

/*
 * Copy one live block into a caller buffer.
 *
 * Inputs: resident Scene/track/step and a PAT_RAW_BLOCK_MAX buffer.
 * Output: block byte size (chunks*4) or 0 when the step holds no block;
 * *entry_out (optional) always receives the live address entry, so the
 * caller can read the trigger bit of a block-less step. Always the live
 * source (spec: source reads are live).
 */
uint8_t pat_rawReadBlock(uint8_t scene_index, uint8_t track, uint8_t step,
                         uint8_t out[PAT_RAW_BLOCK_MAX],
                         uint16_t *entry_out);

/*
 * Allocated byte size of an encoded block (header count + flags), or 0 for
 * NULL/oversized input.
 */
uint8_t pat_rawBlockBytes(const uint8_t *block);

/*
 * Decode a caller-held block.
 *
 * Inputs: block (NULL = no block), optional specials output, optional
 * automation output and its capacity. Output: specials with defaults for
 * absent fields; return value is the number of automation entries copied
 * (or the encoded count when autos is NULL).
 */
uint8_t pat_rawDecode(const uint8_t *block, pat_step_specials_t *specials,
                      pat_automation_entry_t *autos, uint8_t capacity);

/*
 * Encode a block into a caller buffer.
 *
 * Inputs: special flags/values and up to 63 automation entries (merge pastes
 * drop entries past 63 before calling). Output: allocated byte size, or 0
 * when the block would be empty (no specials and no automation) or invalid.
 * The back-reference field is left zero; placement stamps it.
 */
uint8_t pat_rawEncode(uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t flags,
                      uint8_t note, uint8_t velocity, uint8_t probability,
                      const pat_automation_entry_t *autos, uint8_t count);

/*
 * Place an encoded block below the swap block and publish it.
 *
 * Output: 1 after allocate/write/publish/free-old; 0 when no contiguous free
 * run below PAT_POOL_SWAP_OFFSET exists (nothing changed; the caller
 * compacts, or places via the swap block).
 */
uint8_t pat_rawPlace(uint8_t scene_index, uint8_t track, uint8_t step,
                     const uint8_t *block, uint8_t trigger_mode);

/*
 * Place an encoded block in the permanent swap block and publish it.
 *
 * Output: 1 when the swap block was free; 0 when it is occupied. The step
 * must later be moved below the reserve with pat_rawSwapReturn() before the
 * exclusive claim ends (copy/clear never ends a claim with the swap block in
 * use).
 */
uint8_t pat_rawPlaceViaSwap(uint8_t scene_index, uint8_t track, uint8_t step,
                            const uint8_t *block, uint8_t trigger_mode);

/*
 * Move a swap-resident step below the reserve.
 *
 * Output: 1 after allocate/copy/publish (trigger kept) and the swap block is
 * free again; 0 when the step is not in the swap block or no run exists yet.
 */
uint8_t pat_rawSwapReturn(uint8_t scene_index, uint8_t track, uint8_t step);

/*
 * Publish "no block" for one step: one PRIMASK store of trigger|0x3FFF using
 * the trigger policy, then the old block is freed.
 */
void pat_rawPublishEmpty(uint8_t scene_index, uint8_t track, uint8_t step,
                         uint8_t trigger_mode);

/* Free chunks below the swap block (paste planning and compaction gate). */
uint16_t pat_rawFreeChunks(uint8_t scene_index);

/* Nonzero when every swap-block chunk is free. */
uint8_t pat_rawSwapFree(uint8_t scene_index);

/*
 * Whole-Pattern copy and clear (Scene-level paste, `clear pattern`).
 *
 * Order for a literal copy: pat_rawRegionSilence(dst) publishes the sentinel
 * into every destination entry (no entry references the pool any more), then
 * pat_rawRegionCopyBody(src, dst) copies pool, bitmap, track settings and
 * Pattern globals, then pat_rawRegionPublishSteps() publishes the source
 * entries in slices (flat index = track*128 + step). For a retargeting copy
 * the caller instead walks every index with pat_rawRegionCopiedBlock() to read
 * the copied block, rewrites its targets (drop or rename, never grow), and
 * pat_rawRegionPublishRewritten() overwrites the still-unreferenced copy in
 * place, frees its tail chunks, then publishes the entry. A rewrite that would
 * grow the block returns 0 and publishes the copied block unchanged.
 * pat_rawRegionReset() rebuilds an empty region with sentinels written first.
 */
void pat_rawRegionSilence(uint8_t scene_index);
void pat_rawRegionCopyBody(uint8_t src_scene, uint8_t dst_scene);
void pat_rawRegionPublishSteps(uint8_t src_scene, uint8_t dst_scene,
                               uint16_t first, uint16_t count);
uint8_t pat_rawRegionCopiedBlock(uint8_t src_scene, uint8_t dst_scene,
                                 uint16_t index,
                                 uint8_t out[PAT_RAW_BLOCK_MAX]);
uint8_t pat_rawRegionPublishRewritten(uint8_t src_scene, uint8_t dst_scene,
                                      uint16_t index, const uint8_t *block);
void pat_rawRegionReset(uint8_t scene_index);

/*
 * Menu synchronization and resident Pattern parameter setters.
 *
 * Inputs are resident Scene/track coordinates and menu values. Outputs update
 * the region fields that the v4 reader/writer persists; apply helpers repaint
 * the shared menu parameter buffer. The selected-step special setters below
 * continue to own their dynamic-pool read/modify/write behavior.
 */
/* Shared sequencer scale default: a new track displays/stores 1/16. */
#define TRACK_SCALE_DEFAULT STEP_SCALE_DEFAULT
void pat_applyPatternSettingsToMenu(uint8_t scene_index);
void pat_applyTrackSettingsToMenu(uint8_t scene_index, uint8_t track);
void pat_setTrackLength(uint8_t scene_index, uint8_t track, uint8_t value);
void pat_setTrackScale(uint8_t scene_index, uint8_t track, uint8_t value);
void pat_setTrackShuffle(uint8_t scene_index, uint8_t track, uint8_t value);

/*
 * Step-automation persistence operations.
 *
 * What: count, decode, add/update, remove, and track-wide-remove automation
 * entries for one resident step. Why: the STEP automation page and the
 * Sequencer use one owner for validation, uniqueness, pool allocation, and
 * dirty-state publication. The pat_* mutation entrypoints below are raw
 * service-exclusive workers; application callers use PatternStackService.h so
 * queued work and maintenance share this owner. Inputs: valid Scene/track/
 * step coordinates, canonical target IDs, and 7-bit values. Outputs: bounded
 * entry/removal counts or nonzero success; invalid targets and exhausted
 * storage leave the Pattern unchanged. Affiliates: InstrumentManager,
 * PatternStackService.c, menu.c, and sequencer.c.
 */
uint8_t pat_stepAutomationCount(uint8_t scene_index, uint8_t track,
                                uint8_t step);
uint8_t pat_readStepAutomations(uint8_t scene_index, uint8_t track,
                                uint8_t step, pat_automation_entry_t *out,
                                uint8_t max_count);
uint8_t pat_writeStepAutomation(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint16_t target, uint8_t value);
uint8_t pat_removeStepAutomation(uint8_t scene_index, uint8_t track,
                                 uint8_t step, uint16_t target);
uint8_t pat_removeTrackAutomationByTarget(uint8_t scene_index, uint8_t track,
                                          uint16_t target);
void pat_setPatternChangeBar(uint8_t scene_index, uint8_t value);
void pat_setPatternNext(uint8_t scene_index, uint8_t value);
void pat_applyStepToMenu(uint8_t scene_index, uint8_t track, uint8_t step);
/*
 * Raw service-exclusive special setters.
 *
 * What: retain the other special values and replace the complete dynamic
 * block. Why: S067 removed unsafe same-size in-place rewrites, so callers
 * need a failure result when a disjoint replacement cannot fit. Inputs are
 * resident coordinates and one 7-bit value; output is nonzero on commit.
 * PatternStackService.h supplies the application-facing queue boundary.
 */
uint8_t pat_setStepProbability(uint8_t scene_index, uint8_t track,
                               uint8_t step, uint8_t value);
uint8_t pat_setStepNote(uint8_t scene_index, uint8_t track, uint8_t step,
                        uint8_t value);
uint8_t pat_setStepVolume(uint8_t scene_index, uint8_t track, uint8_t step,
                          uint8_t value);

/*
 * Pattern AutoSave snapshot operations.
 *
 * pat_snapshotScene() copies one live resident Scene region into the internal
 * snapshot buffer. Input: scene_index 0..15. The caller must ensure RECORD
 * and ERASE are inactive; no interrupt masking is performed here.
 * pat_autosaveSnapshot() returns the const snapshot pointer, valid until the
 * next snapshot call. Affiliates: filesystem.c Pattern drain state machine.
 */
void pat_snapshotScene(uint8_t scene_index);
const pat_scene_region_t *pat_autosaveSnapshot(void);

/*
 * Compute one resident Scene's dynamic-pool occupancy for the Global widget.
 *
 * What: count the set bits in the backed PAT_STACK_SIZE-byte bitmap prefix
 * and return a saturated 0..99 percentage. Why: Menu needs one alignment-safe
 * pool-use sample without knowing PatternData's bitmap geometry. Inputs are a
 * resident Scene index; invalid indices return zero. Affiliates:
 * PatternStackService.c's pool accounting and menu.c's compute-on-entry
 * StoreUse widget.
 */
uint8_t pat_poolUsagePercent(uint8_t scene_index);

#endif /* PATTERNDATA_H_ */
