/*
 * PatternData.c
 *
 * Live Scene Pattern state is a Scene-indexed resident region. The v4
 * filesystem streams this same region through the public accessors in
 * PatternData.h; no separate trigger-only file bridge is maintained.
 */

#include "PatternData.h"
#include "SceneData.h"
#include "BankData.h"
#include "Autosave.h"
#include "config.h"
/*
 * Menu owns the parameter buffer and PAR_STEP_* identifiers used by the
 * selected-step display bridge. Inputs/outputs: declaration visibility only;
 * PatternData remains the owner of persisted live step-special values.
 */
#include "menu.h"

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
 * pat_initScene(), the Session-062 C allocator, and SRAM_MANIFEST.md.
 */
_Static_assert(PAT_STACK_SIZE > 0u && PAT_STACK_SIZE <= 512u,
               "PAT_STACK_SIZE must fit the 14-bit pool bitmap");
_Static_assert(sizeof(pat_scene_region_t) ==
               (PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) +
               512u + 23u,
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
 * Pattern AutoSave snapshot staging buffer.
 *
 * What: one standalone pat_scene_region_t separate from pat_regions[]. Why:
 * the background writer snapshots one Scene in the main loop, then streams
 * it over many filesystem ticks without reading data that recording/erasing
 * may later change. SRAM cost: 10,519 bytes in SRAM1 .bss. Lifetime: static;
 * written by pat_snapshotScene() and read by pat_autosaveSnapshot(). Owner:
 * PatternData.c exclusively. Affiliate: filesystem.c Pattern drain writer.
 */
static pat_scene_region_t pat_autosave_snapshot;

/*
 * Mark one Pattern mutation at the existing card-clean boundary.
 *
 * What: combines the established Bank card-clean invalidation with the new
 * per-Scene Pattern AutoSave dirty bit. Why: bank_invalidateSdCleanScene()
 * is shared by non-Pattern owners, so wiring the Pattern bit in that generic
 * helper would falsely dirty Pattern files for Scene/Kit/Instrument edits.
 * Inputs: validated resident Scene index. Output: both ownership registers
 * receive the same mutation boundary. Affiliates: every Pattern setter below.
 */
static void pat_markSceneDirty(uint8_t scene_index)
{
    bank_invalidateSdCleanScene(scene_index);
    autosave_markPatternDirty(scene_index);
}

/*
 * Capture one coherent Pattern region for the background writer.
 *
 * Inputs: validated resident Scene index and an idle RECORD/ERASE boundary.
 * Output: the dedicated 10,519-byte snapshot becomes a plain copy of the
 * selected live region. No interrupt masking is performed; filesystem.c owns
 * the scheduler guard that makes the copy safe. Affiliate:
 * pat_autosaveSnapshot().
 */
void pat_snapshotScene(uint8_t scene_index)
{
    if (!scene_indexValid(scene_index))
        return;
    memcpy(&pat_autosave_snapshot, &pat_regions[scene_index],
           sizeof(pat_scene_region_t));
}

/*
 * Borrow the latest Pattern AutoSave snapshot for bounded file streaming.
 *
 * Input: none. Output: const pointer to PatternData's dedicated snapshot,
 * valid until the next pat_snapshotScene() call. No allocation or I/O occurs;
 * filesystem.c is the sole consumer. Affiliate: Pattern drain state machine.
 */
const pat_scene_region_t *pat_autosaveSnapshot(void)
{
    return &pat_autosave_snapshot;
}

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

/*
 * Read one occupancy bit from a Scene's four-byte-chunk bitmap.
 *
 * What: convert a nominal pool chunk index into the bitmap byte and bit.
 * Why: the allocator and release path must share one LSB-first convention.
 * Inputs: a valid Scene region and chunk 0..4095. Output: zero when free or
 * one when occupied. No bounds check is performed; callers validate ranges.
 * Affiliates: pat_poolAlloc(), pat_poolFree(), and pat_initScene().
 */
static uint8_t pat_bitmapGet(const pat_scene_region_t *r, uint16_t chunk)
{
    return (uint8_t)((r->bitmap[chunk >> 3u] >> (chunk & 7u)) & 1u);
}

/*
 * Mark one free-tracking bitmap chunk occupied.
 *
 * What: set the bit corresponding to one four-byte pool chunk. Why: successful
 * first-fit allocation must reserve every chunk before exposing its offset in
 * an address entry. Inputs: validated region and chunk index. Output: one
 * bitmap bit is set. Affiliates: pat_poolAlloc().
 */
static void pat_bitmapSet(pat_scene_region_t *r, uint16_t chunk)
{
    r->bitmap[chunk >> 3u] |= (uint8_t)(1u << (chunk & 7u));
}

/*
 * Mark one occupied bitmap chunk free.
 *
 * What: clear the bit corresponding to one four-byte pool chunk. Why: erased
 * or resized blocks must return their complete allocation to the Scene pool.
 * Inputs: validated region and chunk index. Output: one bitmap bit is clear.
 * Affiliates: pat_poolFree().
 */
static void pat_bitmapClear(pat_scene_region_t *r, uint16_t chunk)
{
    r->bitmap[chunk >> 3u] &= (uint8_t)~(1u << (chunk & 7u));
}

/*
 * Validate one address-array pool offset against the configured pool.
 *
 * What: accept only four-byte-aligned offsets backed by pool storage. Why:
 * `PAT_ADDR_SENTINEL` and the unbacked upper address range must never reach a
 * pool read or free operation. Inputs: the 14-bit address-field value. Output:
 * nonzero for a valid pool base offset. Affiliates: allocator clients and the
 * public specials reader.
 */
static uint8_t pat_poolOffsetValid(uint16_t byte_offset)
{
    uint16_t pool_bytes = (uint16_t)(PAT_STACK_SIZE * 32u);

    return (uint8_t)(byte_offset != PAT_ADDR_SENTINEL &&
                     (byte_offset & 3u) == 0u &&
                     byte_offset < pool_bytes);
}

/*
 * Allocate a contiguous first-fit run of dynamic-pool chunks.
 *
 * What: scan the free bitmap from chunk zero and reserve `chunks` adjacent
 * four-byte units. Why: menu-paced special edits need a bounded synchronous
 * allocator; defragmentation and relocation are deferred. Inputs: Scene region
 * and a nonzero chunk count. Output: byte offset on success or
 * PAT_ADDR_SENTINEL when the pool has no suitable run. Affiliates:
 * pat_poolFree(), pat_writeSpecials(), and pat_blockChunks().
 */
static uint16_t pat_poolAlloc(pat_scene_region_t *r, uint8_t chunks)
{
    uint16_t max_chunk = (uint16_t)(PAT_STACK_SIZE * 8u);
    uint16_t start;
    uint16_t run;
    uint16_t i;

    if (!r || chunks == 0u)
        return PAT_ADDR_SENTINEL;

    start = 0u;
    while ((uint32_t)start + chunks <= max_chunk) {
        run = 0u;
        for (i = start; i < (uint16_t)(start + chunks); i++) {
            if (pat_bitmapGet(r, i)) {
                start = (uint16_t)(i + 1u);
                run = 0u;
                break;
            }
            run++;
        }
        if (run == chunks) {
            for (i = start; i < (uint16_t)(start + chunks); i++)
                pat_bitmapSet(r, i);
            return (uint16_t)(start << 2u);
        }
    }
    return PAT_ADDR_SENTINEL;
}

/*
 * Release a previously allocated dynamic-pool block.
 *
 * What: clear the block's bitmap run and zero its bytes. Why: erase, clear,
 * and specials reallocation must reclaim storage and prevent stale values from
 * appearing in a later allocation. Inputs: region, original aligned byte
 * offset, and original chunk count. Output: the allocation is free; malformed
 * offsets/runs are ignored. The caller updates its address entry separately.
 * Affiliates: pat_poolAlloc(), pat_eraseStep(), pat_clearTrack(), and
 * pat_writeSpecials().
 */
static void pat_poolFree(pat_scene_region_t *r, uint16_t byte_offset,
                         uint8_t chunks)
{
    uint16_t max_chunk = (uint16_t)(PAT_STACK_SIZE * 8u);
    uint16_t base_chunk;
    uint16_t i;

    if (!r || !pat_poolOffsetValid(byte_offset) || chunks == 0u)
        return;
    base_chunk = (uint16_t)(byte_offset >> 2u);
    if ((uint32_t)base_chunk + chunks > max_chunk)
        return;
    for (i = base_chunk; i < (uint16_t)(base_chunk + chunks); i++)
        pat_bitmapClear(r, i);
    memset(&r->pool[byte_offset], 0, (size_t)chunks * 4u);
}

/*
 * Calculate the four-byte allocation size for one dynamic block.
 *
 * What: include the two-byte header, flags byte, one byte per supported
 * special, and two bytes per automation entry, then round up to a chunk. Why:
 * allocator/free/reallocation paths must agree on complete block ownership.
 * Inputs: supported special flags and a bounded automation count. Output: the
 * required four-byte chunk count. Affiliates: all dynamic block readers and
 * writers below.
 */
static uint8_t pat_blockChunks(uint8_t special_flags, uint8_t auto_count)
{
    uint8_t value_count = 0u;
    uint8_t total;

    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    if (auto_count > PAT_BLOCK_AUTO_COUNT_MASK)
        auto_count = PAT_BLOCK_AUTO_COUNT_MASK;
    total = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count +
                      ((uint16_t)auto_count * 2u));
    return (uint8_t)((total + 3u) >> 2u);
}

/*
 * Write one complete dynamic block at an allocated pool offset.
 *
 * What: encode the step back-reference, special flags/values, and packed
 * automation entries in the fixed block order. Why: every menu mutation and
 * future integrity reader needs one stable byte layout. Inputs: region/offset,
 * bounded track/step, supported flags, special values, and up to 63 decoded
 * automation entries. Output: the allocated block is written with a
 * big-endian header and little-endian automation words. Affiliates:
 * pat_blockRead(), pat_blockReadAutomations(), and pat_writeSpecials().
 */
static void pat_blockWrite(pat_scene_region_t *r, uint16_t byte_offset,
                           uint8_t track, uint8_t step,
                           uint8_t special_flags, uint8_t note,
                           uint8_t velocity, uint8_t probability,
                           const pat_automation_entry_t *autos,
                           uint8_t auto_count)
{
    uint8_t *p;
    uint16_t step_id;
    uint16_t header;
    uint8_t idx;
    uint8_t auto_idx;

    if (!r || !pat_poolOffsetValid(byte_offset))
        return;
    special_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (auto_count > PAT_BLOCK_AUTO_COUNT_MASK)
        auto_count = PAT_BLOCK_AUTO_COUNT_MASK;
    p = &r->pool[byte_offset];
    step_id = (uint16_t)(track * NUM_STEPS + step);
    header = (uint16_t)((step_id << PAT_BLOCK_STEP_ID_SHIFT) &
                        PAT_BLOCK_STEP_ID_MASK);
    header |= (uint16_t)(auto_count & PAT_BLOCK_AUTO_COUNT_MASK);

    memset(p, 0, (size_t)pat_blockChunks(special_flags, auto_count) * 4u);
    p[0] = (uint8_t)(header >> 8u);
    p[1] = (uint8_t)(header & 0xFFu);
    p[2] = special_flags;

    idx = 3u;
    if (special_flags & PAT_SPECIAL_NOTE_BIT)
        p[idx++] = note;
    if (special_flags & PAT_SPECIAL_VEL_BIT)
        p[idx++] = velocity;
    if (special_flags & PAT_SPECIAL_PROB_BIT)
        p[idx++] = probability;
    for (auto_idx = 0u; auto_idx < auto_count; auto_idx++) {
        uint16_t packed = (uint16_t)(((uint16_t)(autos[auto_idx].value & 0x7Fu)
                                      << 9u) |
                                     (autos[auto_idx].target & 0x01FFu));
        p[idx++] = (uint8_t)(packed & 0xFFu);
        p[idx++] = (uint8_t)(packed >> 8u);
    }
}

/*
 * Read resolved values from one dynamic block.
 *
 * What: parse the flags byte and value bytes in ascending flag order. Why: the
 * Sequencer and STEP menu must resolve the same defaults and overrides. Inputs:
 * a valid allocated Scene region offset. Output: a specials struct with
 * PAT_DEFAULT_NOTE, PAT_DEFAULT_VELOCITY, and probability 127 where flags are
 * absent. The header is retained for future integrity scans but skipped here.
 * Affiliates: pat_readStepSpecials().
 */
static pat_step_specials_t pat_blockRead(const pat_scene_region_t *r,
                                         uint16_t byte_offset)
{
    pat_step_specials_t out;
    const uint8_t *p;
    uint8_t flags;
    uint8_t idx;
    uint8_t auto_count;

    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.probability = 127u;
    out.flags = 0u;
    if (!r || !pat_poolOffsetValid(byte_offset))
        return out;

    p = &r->pool[byte_offset];
    flags = (uint8_t)(p[2] & PAT_SPECIAL_FLAGS_MASK);
    auto_count = (uint8_t)(p[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    if ((uint32_t)byte_offset +
            ((uint32_t)pat_blockChunks(flags, auto_count) * 4u) >
        (PAT_STACK_SIZE * 32u))
        return out;
    out.flags = flags;
    idx = 3u;
    if (flags & PAT_SPECIAL_NOTE_BIT)
        out.note = p[idx++];
    if (flags & PAT_SPECIAL_VEL_BIT)
        out.velocity = p[idx++];
    if (flags & PAT_SPECIAL_PROB_BIT)
        out.probability = p[idx++];

    return out;
}

/*
 * Decode the automation tail of one dynamic pool block.
 *
 * What: read the header count and unpack each little-endian 16-bit entry into
 * the public target/value form. Why: menu editing and migration code need the
 * automation list without duplicating the block layout. Inputs: a validated
 * pool base, output storage, and its capacity. Outputs: the number copied,
 * capped by both the encoded count and `max_count`; malformed tails return
 * zero. Affiliate: the step-automation CRUD functions below.
 */
static uint8_t pat_blockReadAutomations(const pat_scene_region_t *r,
                                        uint16_t byte_offset,
                                        pat_automation_entry_t *out,
                                        uint8_t max_count)
{
    const uint8_t *p;
    uint8_t flags;
    uint8_t value_count = 0u;
    uint8_t auto_count;
    uint8_t copy_count;
    uint8_t idx;
    uint8_t auto_idx;

    if (!r || !out || max_count == 0u || !pat_poolOffsetValid(byte_offset))
        return 0u;
    p = &r->pool[byte_offset];
    flags = (uint8_t)(p[2] & PAT_SPECIAL_FLAGS_MASK);
    if (flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    auto_count = (uint8_t)(p[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    if ((uint32_t)byte_offset +
            ((uint32_t)pat_blockChunks(flags, auto_count) * 4u) >
        (PAT_STACK_SIZE * 32u))
        return 0u;
    copy_count = auto_count < max_count ? auto_count : max_count;
    idx = (uint8_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count);
    for (auto_idx = 0u; auto_idx < copy_count; auto_idx++) {
        uint16_t packed = (uint16_t)(p[idx] | ((uint16_t)p[idx + 1u] << 8u));
        out[auto_idx].target = (uint16_t)(packed & 0x01FFu);
        out[auto_idx].value = (uint8_t)((packed >> 9u) & 0x7Fu);
        idx = (uint8_t)(idx + 2u);
    }
    return copy_count;
}

/*
 * Replace one step's complete dynamic block while preserving its trigger bit.
 *
 * What: free, reuse, or allocate the block selected by the special flags and
 * automation list, then swap the address entry to its resulting offset. Why:
 * special and automation edits must share one ownership transaction, including
 * automation-only blocks whose special-flags byte is zero. Inputs: Scene/
 * track/step, supported flags, special values, at most 63 entries, and a
 * shrink-in-place policy used only by removal. Output: nonzero when the
 * block/address state was committed; allocation failure otherwise leaves the
 * old block and address untouched. Affiliates: pat_writeSpecials() and the
 * public automation CRUD functions below.
 */
static uint8_t pat_writeDynamic(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint8_t new_flags,
                                uint8_t note, uint8_t velocity,
                                uint8_t probability,
                                const pat_automation_entry_t *autos,
                                uint8_t auto_count,
                                uint8_t allow_shrink_in_place)
{
    pat_scene_region_t *r;
    uint16_t *entry;
    uint16_t addr;
    uint16_t old_offset;
    uint8_t old_chunks;
    uint8_t new_chunks;
    uint16_t new_offset;
    uint16_t trigger_bits;
    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return 0u;
    r = &pat_regions[scene_index];
    new_flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (auto_count > PAT_BLOCK_AUTO_COUNT_MASK)
        return 0u;
    if (auto_count > 0u && !autos)
        return 0u;
    addr = *entry;
    trigger_bits = (uint16_t)(addr & PAT_ADDR_TRIGGER_BIT);
    old_offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);

    if (new_flags == 0u && auto_count == 0u) {
        if (pat_poolOffsetValid(old_offset)) {
            old_chunks = pat_blockChunks(
                r->pool[old_offset + 2u],
                (uint8_t)(r->pool[old_offset + 1u] &
                          PAT_BLOCK_AUTO_COUNT_MASK));
            pat_poolFree(r, old_offset, old_chunks);
        }
        *entry = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(scene_index);
        return 1u;
    }

    new_chunks = pat_blockChunks(new_flags, auto_count);
    if (pat_poolOffsetValid(old_offset)) {
        old_chunks = pat_blockChunks(
            r->pool[old_offset + 2u],
            (uint8_t)(r->pool[old_offset + 1u] & PAT_BLOCK_AUTO_COUNT_MASK));
        if (old_chunks == new_chunks) {
            pat_blockWrite(r, old_offset, track, step, new_flags, note,
                           velocity, probability, autos, auto_count);
            *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                                old_offset);
            pat_markSceneDirty(scene_index);
            return 1u;
        }
    }

    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL) {
        /*
         * A removal can safely shrink in place when the pool has no separate
         * run for the replacement. Add/special-growth edits never take this
         * path: their old block remains authoritative until a new run exists.
         */
        if (allow_shrink_in_place && pat_poolOffsetValid(old_offset) &&
            new_chunks < old_chunks) {
            pat_blockWrite(r, old_offset, track, step, new_flags, note,
                           velocity, probability, autos, auto_count);
            pat_poolFree(r, (uint16_t)(old_offset + new_chunks * 4u),
                         (uint8_t)(old_chunks - new_chunks));
            *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                                old_offset);
            pat_markSceneDirty(scene_index);
            return 1u;
        }
        return 0u;
    }

    pat_blockWrite(r, new_offset, track, step, new_flags, note, velocity,
                   probability, autos, auto_count);
    if (pat_poolOffsetValid(old_offset))
        pat_poolFree(r, old_offset, old_chunks);
    *entry = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT | new_offset);
    pat_markSceneDirty(scene_index);
    return 1u;
}

/*
 * Replace one step's special values while retaining every automation entry.
 *
 * Inputs: Scene/track/step, desired special flags, and candidate values.
 * Output: the shared dynamic-block transaction preserves automation entries
 * and commits the special edit or leaves the old state on allocation failure.
 * Affiliate: pat_setStepNote(), pat_setStepVolume(), and
 * pat_setStepProbability().
 */
static void pat_writeSpecials(uint8_t scene_index, uint8_t track,
                              uint8_t step, uint8_t new_flags,
                              uint8_t note, uint8_t velocity,
                              uint8_t probability)
{
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t offset;
    uint8_t auto_count = 0u;

    if (!r || !entry)
        return;
    offset = (uint16_t)(*entry & PAT_ADDR_OFFSET_MASK);
    if (pat_poolOffsetValid(offset))
        auto_count = pat_blockReadAutomations(r, offset, autos,
                                              PAT_BLOCK_AUTO_COUNT_MASK);
    (void)pat_writeDynamic(scene_index, track, step, new_flags, note, velocity,
                           probability, autos, auto_count, 0u);
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
     * The pool bitmap is prepared for the Session-062 synchronous allocator.
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
    for (track = 0u; track < NUM_TRACKS; track++) {
        region->track_length[track] = NUM_STEPS;
        region->track_scale[track] = TRACK_SCALE_OFF;
        region->track_shuffle[track] = 0u;
    }
    region->pattern_change_bar = 0u;
    region->pattern_next = 0u;
}

/*
 * Return the resident read-only region for one Scene.
 *
 * Inputs: Scene index. Output: the initialized region or NULL for an invalid
 * index. Filesystem readers use this boundary after pat_initScene() so the
 * storage owner remains private to PatternData.c.
 */
const pat_scene_region_t *pat_sceneRegion(uint8_t scene_index)
{
    return scene_indexValid(scene_index) ? &pat_regions[scene_index] : NULL;
}

/*
 * Return the resident mutable region for one Scene.
 *
 * Inputs: Scene index. Output: writable region or NULL for an invalid index.
 * The v4 file reader uses this accessor to stream validated bytes directly
 * into resident storage without a second full-size staging buffer.
 */
pat_scene_region_t *pat_sceneRegionMut(uint8_t scene_index)
{
    return scene_indexValid(scene_index) ? &pat_regions[scene_index] : NULL;
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
     * bits 14..0 retain any special flag and pool offset. Sequencer
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
    /* The local Pattern mutation funnel also sets the S064 dirty bit. */
    pat_markSceneDirty(scene_index);
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
    pat_markSceneDirty(scene_index);
}

void pat_eraseStep(uint8_t scene_index, uint8_t track, uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t addr;
    uint16_t offset;

    /*
     * Erase one step's complete address state.
     *
     * Inputs: bounded Scene/track/step coordinates. Output: trigger and
     * specials are cleared, the old dynamic block is returned to the bitmap,
     * and the pool offset becomes PAT_ADDR_SENTINEL. This destructive path
     * differs from toggling, which deliberately preserves bits 14..0.
     * Affiliate: pat_poolFree().
     */
    if (!entry)
        return;
    addr = *entry;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    if (pat_poolOffsetValid(offset)) {
        pat_scene_region_t *r = &pat_regions[scene_index];
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                         (uint8_t)(r->pool[offset + 1u] &
                                                   PAT_BLOCK_AUTO_COUNT_MASK));
        pat_poolFree(r, offset, chunks);
    }
    *entry = PAT_ADDR_SENTINEL;
    pat_markSceneDirty(scene_index);
}

/*
 * Resolve one address entry into menu/playback-ready step specials.
 *
 * What: validate the entry's specials bit and pool offset, then parse its
 * dynamic block. Why: Sequencer and Menu need one owner for defaults and pool
 * bounds. Inputs: resident Scene/track/step coordinates. Output: note,
 * velocity, probability, and explicit-special flags; invalid or trigger-only
 * entries return PAT_DEFAULT_NOTE, PAT_DEFAULT_VELOCITY, probability 127, and
 * zero flags. Affiliate: pat_blockRead().
 */
pat_step_specials_t pat_readStepSpecials(uint8_t scene_index,
                                         uint8_t track, uint8_t step)
{
    pat_step_specials_t out;
    const uint16_t *entry;
    uint16_t addr;
    uint16_t offset;

    out.note = PAT_DEFAULT_NOTE;
    out.velocity = PAT_DEFAULT_VELOCITY;
    out.probability = 127u;
    out.flags = 0u;

    entry = pat_addrPtr(scene_index, track, step);
    if (!entry)
        return out;
    addr = *entry;
    if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
        return out;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return out;
    return pat_blockRead(&pat_regions[scene_index], offset);
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
    pat_scene_region_t *r;
    uint16_t step;
    uint16_t addr;
    uint16_t offset;

    /*
     * Return all 128 address entries in one track to the empty sentinel.
     *
     * Inputs: resident Scene and track. Output: the track has no trigger or
     * address state and every referenced dynamic block is freed before its
     * entry is reset. Affiliates: copyClearTools, EuklidGenerator, and
     * pat_poolFree().
     */
    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return;
    r = &pat_regions[scene_index];
    for (step = 0u; step < NUM_STEPS; step++) {
        addr = r->address[track][step];
        offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
        if (pat_poolOffsetValid(offset)) {
            uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                             (uint8_t)(r->pool[offset + 1u] &
                                                       PAT_BLOCK_AUTO_COUNT_MASK));
            pat_poolFree(r, offset, chunks);
        }
        r->address[track][step] = PAT_ADDR_SENTINEL;
    }
    pat_markSceneDirty(scene_index);
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
    pat_markSceneDirty(scene_index);
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

/*
 * Repaint the resident global Pattern parameters in the menu buffer.
 *
 * Inputs: Scene index. Outputs: PAR_PATTERN_BEAT and PAR_PATTERN_NEXT mirror
 * the resident v4 fields; invalid Scenes leave the menu untouched.
 */
void pat_applyPatternSettingsToMenu(uint8_t scene_index)
{
    const pat_scene_region_t *region = pat_sceneRegion(scene_index);

    if (!region)
        return;
    parameter_values[PAR_PATTERN_BEAT] = region->pattern_change_bar;
    parameter_values[PAR_PATTERN_NEXT] = region->pattern_next;
}

/*
 * Repaint one resident track's v4 parameters in the menu buffer.
 *
 * Inputs: Scene and track. Outputs: length, scale, and shuffle cells mirror
 * the resident region; invalid coordinates leave the menu untouched.
 */
void pat_applyTrackSettingsToMenu(uint8_t scene_index, uint8_t track)
{
    const pat_scene_region_t *region = pat_sceneRegion(scene_index);

    if (!region || !pat_trackValid(track))
        return;
    parameter_values[PAR_TRACK_LENGTH] = region->track_length[track];
    parameter_values[PAR_TRACK_SCALE] = region->track_scale[track];
    parameter_values[PAR_SHUFFLE] = region->track_shuffle[track];
}

/* Persist one track-length menu edit in the resident Scene region. */
void pat_setTrackLength(uint8_t scene_index, uint8_t track, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);

    if (!region || !pat_trackValid(track))
        return;
    region->track_length[track] = value;
    pat_markSceneDirty(scene_index);
}

/* Persist one track-scale menu edit in the resident Scene region. */
void pat_setTrackScale(uint8_t scene_index, uint8_t track, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);

    if (!region || !pat_trackValid(track))
        return;
    region->track_scale[track] = value;
    pat_markSceneDirty(scene_index);
}

/* Persist one track-shuffle menu edit in the resident Scene region. */
void pat_setTrackShuffle(uint8_t scene_index, uint8_t track, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);

    if (!region || !pat_trackValid(track))
        return;
    region->track_shuffle[track] = value;
    pat_markSceneDirty(scene_index);
}

/*
 * Return the encoded automation count for one dynamic step block.
 *
 * Inputs: resident Scene/track/step coordinates. Output: the six-bit count
 * stored in the block header, or zero for trigger-only, invalid, or malformed
 * entries. The complete block geometry is checked before the count is exposed
 * so callers never trust a tail beyond the configured pool. Affiliate:
 * pat_readStepAutomations().
 */
uint8_t pat_stepAutomationCount(uint8_t scene_index, uint8_t track,
                                uint8_t step)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t offset;
    uint8_t flags;
    uint8_t count;

    if (!r || !entry || ((*entry & PAT_ADDR_SPECIALS_BIT) == 0u))
        return 0u;
    offset = (uint16_t)(*entry & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return 0u;
    flags = (uint8_t)(r->pool[offset + 2u] & PAT_SPECIAL_FLAGS_MASK);
    count = (uint8_t)(r->pool[offset + 1u] & PAT_BLOCK_AUTO_COUNT_MASK);
    if ((uint32_t)offset +
            ((uint32_t)pat_blockChunks(flags, count) * 4u) >
        (PAT_STACK_SIZE * 32u))
        return 0u;
    return count;
}

/*
 * Decode one step's automation entries into caller-owned storage.
 *
 * Inputs: resident coordinates, output array, and its capacity. Output: the
 * number of entries copied, preserving on-disk order and capping the result at
 * `max_count`; invalid or trigger-only steps return zero. Affiliate:
 * STEP automation rendering and sequencer persistence tests.
 */
uint8_t pat_readStepAutomations(uint8_t scene_index, uint8_t track,
                                uint8_t step, pat_automation_entry_t *out,
                                uint8_t max_count)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t offset;

    if (!r || !entry || !out || max_count == 0u ||
        ((*entry & PAT_ADDR_SPECIALS_BIT) == 0u))
        return 0u;
    offset = (uint16_t)(*entry & PAT_ADDR_OFFSET_MASK);
    if (!pat_poolOffsetValid(offset))
        return 0u;
    return pat_blockReadAutomations(r, offset, out, max_count);
}

/*
 * Add or update one step automation entry.
 *
 * Inputs: resident coordinates, a canonical voice/Scene target ID, and a
 * 7-bit value. Output: nonzero when the validated target is updated or
 * appended; duplicate targets update in place, while a 64th entry or pool
 * exhaustion leaves the existing block unchanged. Affiliate:
 * instrumentManager_targetValid().
 */
uint8_t pat_writeStepAutomation(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint16_t target, uint8_t value)
{
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    pat_step_specials_t sp;
    uint8_t count;
    uint8_t i;

    if (!pat_addrPtr(scene_index, track, step) ||
        target >= INSTRUMENT_TOTAL_ID_COUNT ||
        !instrumentManager_targetValid(scene_index, target,
                                       INSTRUMENT_TARGET_AUTOMATION))
        return 0u;
    count = pat_readStepAutomations(scene_index, track, step, autos,
                                    PAT_BLOCK_AUTO_COUNT_MASK);
    for (i = 0u; i < count; i++) {
        if (autos[i].target == target) {
            autos[i].value = (uint8_t)(value & 0x7Fu);
            sp = pat_readStepSpecials(scene_index, track, step);
            return pat_writeDynamic(scene_index, track, step, sp.flags, sp.note,
                                    sp.velocity, sp.probability, autos, count,
                                    0u);
        }
    }
    if (count >= PAT_BLOCK_AUTO_COUNT_MASK)
        return 0u;
    autos[count].target = target;
    autos[count].value = (uint8_t)(value & 0x7Fu);
    count++;
    sp = pat_readStepSpecials(scene_index, track, step);
    return pat_writeDynamic(scene_index, track, step, sp.flags, sp.note,
                            sp.velocity, sp.probability, autos, count, 0u);
}

/*
 * Remove one exact target from a step's automation list.
 *
 * Inputs: resident coordinates and a canonical target ID. Output: nonzero
 * when an entry was removed and the compacted block committed; the final
 * automation may release the block entirely when no specials remain. Target
 * validity is not required here so stale entries can be cleaned after an
 * instrument replacement. Affiliate: menu delete/clear actions.
 */
uint8_t pat_removeStepAutomation(uint8_t scene_index, uint8_t track,
                                 uint8_t step, uint16_t target)
{
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    pat_step_specials_t sp;
    uint8_t count;
    uint8_t i;
    uint8_t found = 0u;

    if (!pat_addrPtr(scene_index, track, step) ||
        target >= INSTRUMENT_TOTAL_ID_COUNT)
        return 0u;
    count = pat_readStepAutomations(scene_index, track, step, autos,
                                    PAT_BLOCK_AUTO_COUNT_MASK);
    for (i = 0u; i < count; i++) {
        if (autos[i].target == target) {
            found = 1u;
            break;
        }
    }
    if (!found)
        return 0u;
    for (; i + 1u < count; i++)
        autos[i] = autos[i + 1u];
    count--;
    sp = pat_readStepSpecials(scene_index, track, step);
    return pat_writeDynamic(scene_index, track, step, sp.flags, sp.note,
                            sp.velocity, sp.probability, autos, count, 1u);
}

/*
 * Remove every matching target from one track.
 *
 * Inputs: resident Scene/track coordinates and a canonical target ID. Output:
 * all 128 steps are scanned and matching entries are removed through the
 * single-step owner, including complete block release where appropriate.
 * Affiliate: InstrumentManager slot replacement and future target cleanup.
 */
uint8_t pat_removeTrackAutomationByTarget(uint8_t scene_index, uint8_t track,
                                          uint16_t target)
{
    uint8_t step;
    uint8_t removed = 0u;

    if (!scene_indexValid(scene_index) || !pat_trackValid(track) ||
        target >= INSTRUMENT_TOTAL_ID_COUNT)
        return 0u;
    for (step = 0u; step < NUM_STEPS; step++)
        if (pat_removeStepAutomation(scene_index, track, step, target))
            removed++;
    return removed;
}

/* Persist the global Pattern change-bar selection. */
void pat_setPatternChangeBar(uint8_t scene_index, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);

    if (!region)
        return;
    region->pattern_change_bar = value;
    pat_markSceneDirty(scene_index);
}

/* Persist the global Pattern-next selection. */
void pat_setPatternNext(uint8_t scene_index, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);

    if (!region)
        return;
    region->pattern_next = value;
    pat_markSceneDirty(scene_index);
}

/*
 * Load one selected step's resolved specials into the STEP menu buffer.
 *
 * What: copy the pool reader's note, velocity, and probability into
 * parameter_values[]. Why: selecting a step must repaint its stored values and
 * show the agreed defaults when no block exists. Inputs: viewed Scene, active
 * track, and selected step. Output: the three PAR_STEP_* cells are refreshed.
 * Affiliates: menu_parseParameter() and pat_readStepSpecials().
 */
void pat_applyStepToMenu(uint8_t scene_index, uint8_t track, uint8_t step)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);

    parameter_values[PAR_STEP_NOTE] = sp.note;
    parameter_values[PAR_STEP_VOLUME] = sp.velocity;
    parameter_values[PAR_STEP_PROB] = sp.probability;
}

/*
 * Set or clear one step's note override through a read-modify-write.
 *
 * What: retain velocity/probability specials while changing note. Why: the
 * endless encoder edits one field at a time, and PAT_DEFAULT_NOTE needs no
 * pool byte. Inputs: Scene/track/step and a MIDI note value 0..127. Output:
 * pat_writeSpecials() reallocates, updates, or frees the block as required.
 * Affiliates: menu PAR_STEP_NOTE dispatch and pat_readStepSpecials().
 */
void pat_setStepNote(uint8_t scene_index, uint8_t track, uint8_t step,
                     uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    if (value == PAT_DEFAULT_NOTE)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_NOTE_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_NOTE_BIT);
    pat_writeSpecials(scene_index, track, step, new_flags,
                      value, sp.velocity, sp.probability);
}

/*
 * Set or clear one step's velocity override through a read-modify-write.
 *
 * What: retain note/probability specials while changing velocity. Why: the
 * Step volume encoder must not disturb another stored value, and
 * PAT_DEFAULT_VELOCITY needs no pool byte. Inputs: Scene/track/step and a
 * 0..127 velocity. Output: pat_writeSpecials() updates or releases storage.
 * Affiliates: menu PAR_STEP_VOLUME dispatch and pat_readStepSpecials().
 */
void pat_setStepVolume(uint8_t scene_index, uint8_t track, uint8_t step,
                       uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    if (value == PAT_DEFAULT_VELOCITY)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_VEL_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_VEL_BIT);
    pat_writeSpecials(scene_index, track, step, new_flags,
                      sp.note, value, sp.probability);
}

/*
 * Set or clear one step's probability override through a read-modify-write.
 *
 * What: retain note/velocity specials while changing probability. Why: 127 is
 * the always-fire default and therefore needs no pool byte. Inputs:
 * Scene/track/step and a 0..127 probability. Output: pat_writeSpecials()
 * updates or releases storage; lower values gate playback probabilistically.
 * Affiliates: menu PAR_STEP_PROB dispatch and pat_readStepSpecials().
 */
void pat_setStepProbability(uint8_t scene_index, uint8_t track, uint8_t step,
                            uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    if (value == 127u)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_PROB_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_PROB_BIT);
    pat_writeSpecials(scene_index, track, step, new_flags,
                      sp.note, sp.velocity, value);
}
