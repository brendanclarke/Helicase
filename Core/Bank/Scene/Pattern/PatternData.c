/*
 * PatternData.c
 *
 * Live Scene Pattern state is a Scene-indexed address array plus the reserved
 * dynamic-pool/bitmap region defined below. The legacy PatternSet helpers are
 * retained only for the disconnected v3 file bridge.
 */

#include "PatternData.h"
#include "SceneData.h"
#include "BankData.h"
#include "config.h"

#include <string.h>

/*
 * Permanent SRAM1 Pattern region for one Scene.
 *
 * What: 896 two-byte address entries, the configurable future dynamic pool,
 * and a full-width 4,096-chunk free bitmap. Why: PatternData owns all live
 * per-Scene Pattern storage independently of scene_t, allowing the retired
 * 112-byte bridge bitmap to disappear without embedding a much larger payload
 * in every Scene record. Inputs: NUM_TRACKS, NUM_STEPS, and PAT_STACK_SIZE.
 * Outputs: one fixed region that pat_* functions index by Scene. Affiliates:
 * pat_initScene(), the future Session-062 C allocator, and SRAM_MANIFEST.md.
 */
typedef struct {
    uint16_t address[NUM_TRACKS][NUM_STEPS];
    uint8_t  pool[PAT_STACK_SIZE * 32u];
    uint8_t  bitmap[512u];
} pat_scene_region_t;

_Static_assert(PAT_STACK_SIZE > 0u && PAT_STACK_SIZE <= 512u,
               "PAT_STACK_SIZE must fit the 14-bit pool bitmap");
_Static_assert(sizeof(pat_scene_region_t) ==
               (PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) + 512u,
               "pat_scene_region_t size must match the Pattern budget");

/*
 * Static lifetime owner for all resident Scene Pattern storage.
 *
 * Inputs: startup zero-initialization followed by pat_initScene() for each
 * Scene. Outputs: 16 independent regions in normal SRAM1. No caller may
 * allocate, point into, or free this storage; the Scene index is its owner
 * key. Affiliate: every Scene-indexed pat_* operation in this file.
 */
static pat_scene_region_t pat_regions[SCENE_COUNT];

/*
 * Resolve one live address-array entry.
 *
 * Inputs: resident Scene, track, and step coordinates. Output: a mutable
 * pointer to one 2-byte entry, or NULL for any invalid coordinate. Keeping
 * this check in one helper makes all address writes bounded and preserves the
 * Cortex-M7 halfword read/modify/write contract. Affiliates: playback, UI,
 * recording, clear, and future pool operations.
 */
static uint16_t *pat_addrPtr(uint8_t scene_index, uint8_t track,
                             uint8_t step)
{
    if (!scene_indexValid(scene_index) || !pat_trackValid(track) ||
        !pat_stepValid(step))
        return NULL;
    return &pat_regions[scene_index].address[track][step];
}

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
     * Return one legacy bridge trigger bit.
     *
     * Inputs: PatternSet plus track/step coordinates. Output: zero or one;
     * invalid inputs return zero. storageTypes uses this boundary so v3 file
     * code never depends on a raw-byte layout; live Sequencer/UI reads use the
     * address-array wrapper below instead.
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
     * Set one legacy bridge trigger bit without allocating live Step data.
     *
     * Inputs: PatternSet, bounded coordinate, and boolean on state. Output:
     * one bitmap bit is updated and success is returned; invalid input leaves
     * storage untouched. The filesystem v3 parser is the client; generators
     * and live playback use the address-array API instead.
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
     * Clear a complete legacy PatternSet bridge payload.
     *
     * Input: caller-owned 112-byte PatternSet. Output: all seven tracks are
     * silent. The filesystem discard bridge calls this before parse/save
     * operations; no live Scene region or default Step record is touched.
     */
    if (pattern)
        memset(pattern, 0, sizeof(*pattern));
}

void pat_initScene(uint8_t scene_index)
{
    pat_scene_region_t *region;
    uint8_t track;
    uint16_t step;

    /*
     * Initialize live address/pool/bitmap storage for one validated Scene.
     *
     * Input: resident Scene index. Output: every address is the no-data
     * sentinel, the reserved pool is zeroed, and the lower
     * PAT_STACK_SIZE*8 bitmap chunks are free while the unbacked upper range
     * is permanently occupied. SceneData owns the lifecycle and invokes this
     * at boot; Scene Load and pattern-clear reuse the same reset boundary.
     * The pool is reserved now but has no allocator in B/B½.
     */
    if (!scene_indexValid(scene_index))
        return;
    region = &pat_regions[scene_index];
    for (track = 0u; track < NUM_TRACKS; track++)
        for (step = 0u; step < NUM_STEPS; step++)
            region->address[track][step] = PAT_ADDR_SENTINEL;
    memset(region->pool, 0, sizeof(region->pool));
    memset(region->bitmap, 0, PAT_STACK_SIZE);
    memset(region->bitmap + PAT_STACK_SIZE, 0xFF,
           512u - PAT_STACK_SIZE);
}

uint8_t pat_isStepActive(uint8_t track, uint8_t step, uint8_t scene_index)
{
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);

    /* Playback-safe bit-15 query for one live address-array entry. */
    return entry ? (uint8_t)((*entry >> 15u) & 1u) : 0u;
}

void pat_setStepActive(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t on)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);

    /*
     * Apply an on/off edit to one resident address-array trigger bit.
     *
     * Inputs: Scene/track/step and desired state. Output: only bit 15 changes;
     * bits 14..0 retain any future special flag and pool offset. Sequencer
     * recording, button UI, and Euclidean transfer share this operation;
     * invalid coordinates are ignored. The retained-data invalidation remains
     * the one resident Pattern mutation boundary.
     */
    if (!entry)
        return;
    if (on)
        *entry |= (uint16_t)PAT_ADDR_TRIGGER_BIT;
    else
        *entry &= (uint16_t)~PAT_ADDR_TRIGGER_BIT;
    /* Option 2: live Pattern edits invalidate the owning Scene's card-clean
     * bit; AutoSave deliberately does not yet own Pattern payload bytes. */
    bank_invalidateSdCleanScene(scene_index);
}

void pat_toggleStep(uint8_t track, uint8_t step, uint8_t scene_index)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);

    /*
     * Toggle only bit 15 of one live address entry.
     *
     * Inputs: bounded track/step/Scene coordinates. Output: trigger state is
     * flipped while bits 14..0 remain untouched, so a later pool block
     * survives an off -> on edit cycle. Affiliates: the SEQ button handler
     * and the same Scene card-clean invalidation boundary as setStepActive().
     */
    if (!entry)
        return;
    *entry ^= (uint16_t)PAT_ADDR_TRIGGER_BIT;
    bank_invalidateSdCleanScene(scene_index);
}

void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);

    /*
     * Erase one step's complete address state.
     *
     * Inputs: bounded Scene/track/step coordinates. Output: trigger and
     * specials are cleared and the pool offset becomes PAT_ADDR_SENTINEL.
     * B/B½ has no allocated blocks to release; the later pool implementation
     * will free the old block immediately before this reset. This destructive
     * path differs from toggling, which deliberately preserves bits 14..0.
     */
    if (!entry)
        return;
    *entry = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}

uint8_t pat_sceneHasActiveSteps(uint8_t scene_index)
{
    const pat_scene_region_t *region;
    uint8_t track;
    uint16_t step;

    /*
     * Report whether a Scene address array contains any trigger bit.
     *
     * Input: resident Scene index. Output: nonzero at the first bit-15 entry,
     * otherwise zero. Menu load feedback uses this owner-level scan rather
     * than learning either the address-array layout or future pool format.
     */
    if (!scene_indexValid(scene_index))
        return 0u;
    region = &pat_regions[scene_index];
    for (track = 0u; track < NUM_TRACKS; track++)
        for (step = 0u; step < NUM_STEPS; step++)
            if ((region->address[track][step] & PAT_ADDR_TRIGGER_BIT) != 0u)
                return 1u;
    return 0u;
}

void pat_clearTrack(uint8_t scene_index, uint8_t track)
{
    uint16_t step;

    /*
     * Return all 128 address entries in one track to the empty sentinel.
     *
     * Inputs: resident Scene and track. Output: the track has no trigger or
     * address state. B/B½ has no live pool blocks to reclaim; Step C will add
     * the corresponding block-free walk before these writes. Affiliates:
     * copyClearTools and EuklidGenerator.
     */
    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return;
    for (step = 0u; step < NUM_STEPS; step++)
        pat_regions[scene_index].address[track][step] = PAT_ADDR_SENTINEL;
    bank_invalidateSdCleanScene(scene_index);
}

void pat_clearPattern(uint8_t scene_index)
{
    /*
     * Clear the complete live Pattern region without touching Scene settings
     * or Kit data. Inputs: resident Scene index. Outputs: address array,
     * reserved pool, and free bitmap return to pat_initScene()'s empty state.
     * Affiliate: copyClearTools' whole-pattern action.
     */
    if (!scene_indexValid(scene_index))
        return;
    pat_initScene(scene_index);
    bank_invalidateSdCleanScene(scene_index);
}

void pat_copyTrack(uint8_t scene_index, uint8_t src_track, uint8_t dst_track)
{
    /*
     * Deliberate Session-062 no-op for track copy.
     *
     * Inputs: source/destination Scene track coordinates. Output: none.
     * Duplicating address entries also requires duplicating or defining
     * ownership for every referenced dynamic block, so copy operations are
     * deferred to SCOPING_TARGETS Phase 4.5 rather than copying stale offsets.
     */
    (void)scene_index;
    (void)src_track;
    (void)dst_track;
}

void pat_copyPattern(uint8_t src_scene, uint8_t dst_scene)
{
    /*
     * Deliberate Session-062 no-op for cross-Scene Pattern copy.
     *
     * Inputs: source and destination Scene indices. Output: none. A correct
     * implementation must duplicate the source address array and each pool
     * block into destination-owned chunks; that allocator/ownership design is
     * deferred to SCOPING_TARGETS Phase 4.5.
     */
    (void)src_scene;
    (void)dst_scene;
}

void pat_copyBar(uint8_t scene_index, uint8_t track, uint8_t src_bar,
                 uint8_t dst_bar)
{
    /*
     * Deliberate Session-062 no-op for bar copy.
     *
     * Inputs: Scene, track, and source/destination bars. Output: none. A bar
     * copy must duplicate sixteen address entries and their dynamic blocks,
     * not merely copy offsets into shared storage; that work is deferred with
     * the other copy operations to SCOPING_TARGETS Phase 4.5.
     */
    (void)scene_index;
    (void)track;
    (void)src_bar;
    (void)dst_bar;
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
