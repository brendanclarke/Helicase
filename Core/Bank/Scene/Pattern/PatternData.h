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
 * Special-flags byte assignments for one dynamic pool block (S079 §2).
 *
 * What: encodes which value bytes are present in a pool block. Bits 0..6
 * each gate one stored byte; bit 7 is reserved. The ordering determines the
 * byte sequence in the pool: note, velocity, condition, roll, timing,
 * rhythm, fill. Why: all block readers and writers must agree on bit
 * assignment so the positional byte stream is parsed consistently. Inputs:
 * compile-time masks only. Outputs: PatternData.c, PatternStackService.c,
 * and the Sequencer use these to agree on value-byte ordering. Affiliates:
 * pat_blockRead(), pat_blockWrite(), pat_readStepSpecials(),
 * pat_rawDecode(), pat_rawEncode(), pat_blockChunks(),
 * patSvc_blockChunksFor().
 *
 * S079 replaces the former bit-2 probability override with a generic trigger
 * condition and assigns bits 3..6 to roll rate, micro-timing, roll rhythm,
 * and roll fill. There is no backward compatibility: old probability bytes
 * are translated offline by tools/convert_pattern_specials.py.
 */
#define PAT_SPECIAL_NOTE_BIT      (1u << 0)
#define PAT_SPECIAL_VEL_BIT       (1u << 1)
#define PAT_SPECIAL_COND_BIT      (1u << 2)
#define PAT_SPECIAL_ROLL_BIT      (1u << 3)
#define PAT_SPECIAL_TIMING_BIT    (1u << 4)
#define PAT_SPECIAL_RHYTHM_BIT    (1u << 5)
#define PAT_SPECIAL_FILL_BIT      (1u << 6)
#define PAT_SPECIAL_FLAGS_MASK    0x7Fu

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
    /*
     * Per-track play mode (S078 §4.1).
     *
     * What: one byte per track selecting the playback direction: 0 fwd,
     * 1 rev, 2 pip, 3 rnd, 4 onc (once synced), 5 1fr (once free). Values
     * >= 6 are treated as fwd. Why: allows each track to have an independent
     * playback direction, matching the FX sequencer's existing run modes.
     * Inputs: Menu edits, Scene/Pattern loads. Outputs: seq_advanceTrackStep()
     * direction logic. Storage: one byte per track at offset 3 in the PAT4
     * 16-byte track header. RAM: 7 bytes per Scene region. Affiliates:
     * sequencer.c play-mode ISR logic, filesystem.c PAT4 reader/writer.
     */
    uint8_t track_play_mode[NUM_TRACKS];
    /*
     * Per-track looper exclusion (S080 P2 Defect 8a).
     *
     * What: bit N set = track N is never captured by the PERF looper ("lop"
     * off); 0 = loopable (the default). Stored inverted so a zero-filled
     * region or an older PAT4 file reads as "all loopable". Why: a persistent
     * opt-out for utility tracks. Inputs: STEP track page edits, Scene/Pattern
     * loads, copy/clear. Outputs: seq_loopableTrackMask(). Storage: byte 4 of
     * each track's 16-byte PAT4 track header (0/1 per track; the bytes were
     * reserved and zero, so the file version is unchanged). RAM: 1 byte per
     * region (17 B across pat_regions[16] and pat_background_region).
     * Affiliates: pat_setTrackLoopable()/pat_trackLoopable(), filesystem.c
     * PAT4 writer/readers.
     */
    uint8_t track_loop_exclude;
    /*
     * Per-track automation ramp mode (S081 P2, "rmp" / Automtn / Ramp (was "asm", S083 follow-up)).
     *
     * What: bit N set = track N is "on": an Instrument parameter automated on
     * two consecutively played steps ramps linearly from the first step's
     * value to the second's; 0 = "off" (the default), every value is applied
     * as a jump. Why: per-track smooth automation; a Pattern setting, not
     * automatable or morphable. A zero-filled region and every existing PAT4
     * file read as "off". Inputs: STEP track page edits, Scene/Pattern loads,
     * copy/clear. Outputs: seq_rampStepEvent() (TIM3), the ramp service.
     * Storage: byte 5 of each track's 16-byte PAT4 track header (0/1; the
     * byte was reserved and zero, so the file version stays 2). RAM: 1 byte
     * per region (17 B across pat_regions[16] and pat_background_region).
     * Affiliates: pat_setTrackAutoSmooth()/pat_trackAutoSmooth(),
     * filesystem.c PAT4 writer/readers.
     */
    uint8_t track_auto_smooth;
    uint8_t pattern_change_bar;
    uint8_t pattern_next;
} pat_scene_region_t;

_Static_assert(sizeof(pat_scene_region_t) ==
               (PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) +
               512u + 32u,
               "pat_scene_region_t size must match the Pattern budget (S081 P2: +1 B auto smooth)");

/*
 * Fixed v4 Pattern file geometry.
 *
 * What: compile-time offsets and lengths for the 160-byte base header and
 * resident payload. Why: readers/writers must agree on one bounded stream and
 * may skip a future header extension without moving the payload contract.
 * Inputs/outputs: PAT_STACK_SIZE and the public resident region above. The
 * CRC field is four bytes at offset 14 and is treated as zero while hashing.
 */
/*
 * PAT4 fixed-header version byte (S079).
 *
 * What: the value stored at header offset 4 and required by the
 * filesystem_patternHeaderValid() reader. Bumping 1 -> 2 (with the S079
 * 7-special block format) makes the reader reject every pre-S079 Pattern file,
 * which is the intended no-backward-compatibility behaviour: old probability
 * bytes are not interpreted as conditions. Files are converted offline by
 * tools/convert_pattern_specials.py. Affiliates: filesystem.c Pattern
 * reader/writer, the converter tool.
 */
#define PATTERN_FILE_VERSION             2u
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
 * Resolved values read from one dynamic step block (S079 §8).
 *
 * What: holds the seven per-step special values plus a flags byte reporting
 * which were explicitly stored. Absent fields carry their default: note 63,
 * velocity 100, condition 0 (always fire), roll 0 (off), timing 0 (none),
 * rhythm 0 (1-1), fill 100 (full). Why: callers read one struct instead of
 * parsing the pool block. Inputs: Scene/track/step coordinate. Outputs:
 * usable values for Sequencer playback, STEP menu display/edit, and
 * copy/clear. Affiliates: pat_readStepSpecials(), pat_rawDecode(),
 * pat_blockRead(), seq_advanceTrackStep(), pat_applyStepToMenu().
 */
typedef struct {
    uint8_t note;       /* bit 0 */
    uint8_t velocity;   /* bit 1 */
    uint8_t condition;  /* bit 2: trigger condition index 0..36 */
    uint8_t roll;       /* bit 3: roll rate index 0..8 */
    uint8_t timing;     /* bit 4: micro-timing delay 0..99 */
    uint8_t rhythm;     /* bit 5: roll rhythm packed byte */
    uint8_t fill;       /* bit 6: roll fill percent 0..100 */
    uint8_t flags;      /* which bits were present */
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
 * Recorded step spec (S081 D-2b, §8.4; S084 P4 rhythm).
 *
 * What: the fields a live-recorded trigger writes to one step: note,
 * velocity, micro-timing, roll rate, fill and (S084) roll rhythm, plus
 * setFil (R10) and noteIsDefault (a `def` note records no note special).
 * rhythm is stored only with a roll (roll != 0) and only when nonzero;
 * otherwise the step's rhythm is cleared. The condition is kept unless
 * setFil is set. Why: an overlay roll played with a `pv` velocity rhythm
 * must replay with it (S084 P0 F7, Q8). RAM: 8 B per copy (4 staging slots
 * in PatternStackService, +4 B).
 */
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint8_t timing;
    uint8_t roll;
    uint8_t fill;
    uint8_t rhythm;
    uint8_t setFil;
    uint8_t noteIsDefault;
} pat_record_spec_t;

/*
 * Write a recorded step's specials and trigger (S081 D-2a, §19.3).
 *
 * What: reads the existing specials and automation for the step, applies the
 * record spec (note, velocity, timing, roll, fill, rhythm with a roll; clears
 * rhythm without one; keeps the
 * condition or sets fil), and writes the result with trigger ON. A result with no
 * specials and no automation only sets bit 15 (pat_setStepActive). Why: the old
 * seq_recordTrigger only set the trigger bit; live recording writes the full step
 * in one block replacement. Inputs: scene, track, step, spec. Outputs: nonzero on
 * commit, 0 when allocation fails (old state untouched). Affiliates:
 * pat_writeDynamic (trigger ON), pat_readStepSpecials, pat_readStepAutomations.
 */
uint8_t pat_recordStep(uint8_t scene_index, uint8_t track, uint8_t step,
                       const pat_record_spec_t *spec);

/*
 * Erase a step's specials while keeping automation (S081 D-3a, §10.2).
 *
 * What: if automation is present, keeps the condition and removes all other
 * specials (note, velocity, timing, roll, rhythm, fill). If no automation,
 * releases the block entirely (trigger KEEP; TIM3 already cleared bit 15). Why:
 * SEQ-hold erase removes triggers and specials but automation still plays.
 */
void pat_eraseSpecials(uint8_t scene_index, uint8_t track, uint8_t step);


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
 * header, flags, seven specials, 63 automation entries, rounded to 34
 * chunks); caller buffers of that size hold any block. Trigger policies:
 * KEEP re-reads the live trigger bit inside the critical section (paste of
 * automation/specials only), OFF publishes trigger clear, ON publishes trigger
 * set (merge and replace pastes resolve the trigger before publishing).
 */
/*
 * Maximum encoded block size (S079 §10.1): 2-byte header + 1 flags byte + 7
 * special value bytes + 63 x 2-byte automation entries = 136 bytes (34
 * chunks). Why: PAT_RAW_BLOCK_MAX sizes every stack-allocated block buffer
 * and must be the ceiling across all callers. Affiliates: pat_rawReadBlock(),
 * pat_rawEncode(), pat_rawRegionCopiedBlock(), copy/clear paste buffers,
 * config.h PAT_POOL_SWAP_CHUNKS.
 */
#define PAT_RAW_BLOCK_MAX   136u
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
 * What: writes a complete pool block from the specials struct and automation
 * entries. Why: separating encode from placement lets copy/clear rewrite
 * blocks without placement. Inputs: specials struct (only fields gated by
 * flags are written), up to 63 automation entries (merge pastes drop entries
 * past 63 before calling). Output: allocated byte size, or 0 when the block
 * would be empty (no specials and no automation) or invalid. The
 * back-reference field is left zero; placement stamps it. Affiliates:
 * pat_rawPlace(), pat_rawPlaceViaSwap(), copyClearService.c.
 */
uint8_t pat_rawEncode(uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t flags,
                      const pat_step_specials_t *specials,
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
 * Per-track play mode setter (S078 §4.1).
 *
 * What: writes the play mode byte for one track in the Pattern region.
 * Input: scene_index, track 0..6, value 0..5 (>= 6 is stored but plays fwd).
 * Output: region updated, Pattern AutoSave marked dirty. Affiliates: menu.c
 * PAR_TRACK_PLAY_MODE edit handler, pat_applyTrackSettingsToMenu().
 */
void pat_setTrackPlayMode(uint8_t scene_index, uint8_t track, uint8_t value);

/*
 * Per-track looper opt-out (S080 P2 Defect 8a).
 *
 * pat_setTrackLoopable() stores loopable = 1 (default) or 0 for one track of a
 * resident Scene and marks the Pattern dirty; pat_trackLoopable() returns 1
 * when the track may be looped (also for invalid coordinates). Clients: Menu
 * STEP track page, copy/clear, seq_loopableTrackMask().
 */
void pat_setTrackLoopable(uint8_t scene_index, uint8_t track, uint8_t loopable);
uint8_t pat_trackLoopable(uint8_t scene_index, uint8_t track);

/*
 * Per-track automation ramp mode (S081 P2): set/clear the rmp bit of one
 * track (marks the Pattern dirty on change); read it (0 when invalid).
 * Clients: Menu STEP track page (rmp cell), copy/clear. The sequencer reads
 * pat_scene_region_t.track_auto_smooth directly from TIM3.
 */
void pat_setTrackAutoSmooth(uint8_t scene_index, uint8_t track, uint8_t on);
uint8_t pat_trackAutoSmooth(uint8_t scene_index, uint8_t track);

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
/*
 * Find one target's 7-bit value on one step without copying the list
 * (S081 P2 ramp pairing); 0 when absent or invalid. Foreground. Client:
 * seq_rampStartIfPaired().
 */
uint8_t pat_findStepAutomation(uint8_t scene_index, uint8_t track,
                               uint8_t step, uint16_t target,
                               uint8_t *value_out);
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
 * resident coordinates and one value; output is nonzero on commit.
 * PatternStackService.h supplies the application-facing queue boundary.
 */
/*
 * Set or clear one step's trigger condition (S079 §3).
 *
 * What: retain all other specials while changing the condition index. Value
 * 0 is the always-fire default and clears the condition flag bit. Inputs:
 * Scene/track/step and a condition index 0..36. Output:
 * pat_writeSpecials() updates or releases storage and returns nonzero after
 * commit. Affiliates: menu PAR_STEP_CONDITION dispatch,
 * pat_readStepSpecials().
 */
uint8_t pat_setStepCondition(uint8_t scene_index, uint8_t track,
                             uint8_t step, uint8_t value);
/*
 * Set or clear one step's roll rate (S079 §4).
 *
 * What: retain all other specials while changing roll rate. Value 0 is off
 * (no roll) and clears the roll flag bit. Inputs: Scene/track/step and a
 * roll rate index 0..8. Output: pat_writeSpecials() updates or releases
 * storage. Affiliates: menu PAR_STEP_ROLL dispatch.
 */
uint8_t pat_setStepRoll(uint8_t scene_index, uint8_t track,
                        uint8_t step, uint8_t value);
/*
 * Set or clear one step's micro-timing delay (S079 §7).
 *
 * What: retain all other specials while changing timing. Value 0 is no delay
 * and clears the timing flag bit. Inputs: Scene/track/step and a delay value
 * 0..99. Output: pat_writeSpecials() updates or releases storage.
 * Affiliates: menu PAR_STEP_TIMING dispatch.
 */
uint8_t pat_setStepTiming(uint8_t scene_index, uint8_t track,
                          uint8_t step, uint8_t value);
/*
 * Set or clear one step's roll rhythm pattern (S079 §5).
 *
 * What: retain all other specials while changing rhythm. Value 0 is off (no
 * velocity change, the default) and clears the rhythm flag bit. Inputs:
 * Scene/track/step and a packed rhythm byte. Output: pat_writeSpecials()
 * updates or releases storage. Affiliates: menu PAR_STEP_RHYTHM dispatch.
 */
uint8_t pat_setStepRhythm(uint8_t scene_index, uint8_t track,
                          uint8_t step, uint8_t value);
/*
 * Set or clear one step's roll fill percentage (S079 §6).
 *
 * What: retain all other specials while changing fill. Value 100 is the
 * default (full step) and clears the fill flag bit. Inputs: Scene/track/step
 * and a fill value 0..100. Output: pat_writeSpecials() updates or releases
 * storage. Affiliates: menu PAR_STEP_FILL dispatch.
 */
uint8_t pat_setStepFill(uint8_t scene_index, uint8_t track,
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
 * Mutable pointer to the background region's pool (S077).
 *
 * What: the raw PAT_STACK_SIZE * 32 pool bytes of the background region, for
 * use as copy/clear scratch storage during an overlapping paste. Why:
 * replaces the 9 kB name-buffer borrow for block data; the pool is always
 * large enough (same type as the source pool) and scales with PAT_STACK_SIZE.
 * The caller must ensure no concurrent snapshot reader
 * (filesystem_patternSnapshotInUse() gate). Inputs: none. Outputs: non-NULL
 * mutable pointer (static lifetime). Affiliates: copyClearService.c,
 * pat_snapshotScene(), pat_autosaveSnapshot().
 */
uint8_t *pat_backgroundPoolMut(void);

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
