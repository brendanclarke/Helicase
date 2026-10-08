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

/*
 * PatternData's allocator and Gate-6 append path consult the service-owned
 * trailing-slack image without taking ownership of that image. Inputs/outputs:
 * declaration visibility only; PatternStackService.c remains the reservation
 * owner. Affiliate: pat_poolAlloc() and pat_tryAppendAutomation().
 */
#include "PatternStackService.h"

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
 * pat_initScene(), the Session-062 C allocator, and STORAGE_SRAM_MANIFEST.md.
 */
_Static_assert(PAT_STACK_SIZE > 0u && PAT_STACK_SIZE <= 512u,
               "PAT_STACK_SIZE must fit the 14-bit pool bitmap");
_Static_assert(sizeof(pat_scene_region_t) ==
               (PAT_STEPS_PER_SCENE * 2u) + (PAT_STACK_SIZE * 32u) +
               512u + 30u,
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
 * Background Scene region (S077).
 *
 * What: one additional pat_scene_region_t outside pat_regions[SCENE_COUNT].
 * Not a playable Scene: scene_indexValid(), pat_patternValid(), the sequencer,
 * the UI, PERF and Bank all continue to bound at SCENE_COUNT (16). It is
 * reached only through its named accessors. Why: serves three purposes that
 * must never overlap with live Scenes - (1) AutoSave snapshot staging for the
 * Pattern drain writer, (2) copy/clear scratch pool for overlapping pastes
 * that would otherwise borrow the 9 kB name buffer for block data, and
 * (3) future Bank Load staging. SRAM cost: 10,519 bytes in SRAM1 .bss (the
 * same footprint as the removed pat_autosave_snapshot; net zero). Lifetime:
 * static. Owner: PatternData.c exclusively. Accessors: pat_snapshotScene(),
 * pat_autosaveSnapshot(), pat_backgroundPoolMut(). Affiliates: filesystem.c
 * Pattern drain writer, copyClearService.c overlapping paste engine.
 *
 * Ownership contract (these users must never run concurrently):
 *   - AutoSave Pattern drain vs copy/clear snapshot: a new drain is blocked
 *     by copyClear_backgroundSuspended(); an in-flight drain by the
 *     filesystem_patternSnapshotInUse() gate at paste path selection.
 *   - AutoSave Pattern drain vs Bank Load staging: future; Bank Load
 *     suspends AutoSave by the same mechanism.
 *   - Copy/clear snapshot vs Bank Load staging: future; copy/clear is
 *     refused while Load/Save owns the UI (menu_storageBusy).
 *   - Boot reader rollback copy vs any: boot only; it runs before copy/clear
 *     or AutoSave can start.
 */
static pat_scene_region_t pat_background_region;

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
 * Output: the background region becomes a plain copy of the selected live
 * region. No interrupt masking is performed; the caller owns the scheduler
 * guard that makes the copy safe. The copy/clear paste engine must check
 * filesystem_patternSnapshotInUse() before overwriting the same region's
 * pool during an overlapping paste (S077). Affiliate: pat_autosaveSnapshot().
 */
void pat_snapshotScene(uint8_t scene_index)
{
    if (!scene_indexValid(scene_index))
        return;
    memcpy(&pat_background_region, &pat_regions[scene_index],
           sizeof(pat_scene_region_t));
}

/*
 * Borrow the latest Pattern AutoSave snapshot for bounded file streaming.
 *
 * Input: none. Output: const pointer into the background region, valid until
 * the next pat_snapshotScene() call. No allocation or I/O occurs;
 * filesystem.c is the sole consumer. Affiliate: Pattern drain state machine.
 */
const pat_scene_region_t *pat_autosaveSnapshot(void)
{
    return &pat_background_region;
}

/*
 * Mutable pointer to the background region's pool bytes.
 *
 * What: returns the raw pool array of pat_background_region for use as
 * copy/clear scratch storage during an overlapping paste. The pool is exactly
 * PAT_STACK_SIZE * 32 bytes (8,192 B today), the same size as every live
 * Scene pool. Why: the overlapping paste formerly borrowed the 9 kB name
 * buffer for block data; this accessor removes that dependency and scales
 * automatically with PAT_STACK_SIZE. The caller writes retargeted source
 * blocks contiguously from byte 0. The swap reservation and bitmap within the
 * background region are irrelevant for raw scratch; only the pool bytes are
 * used. Inputs: none. Outputs: a non-NULL mutable pointer valid for the
 * static lifetime. The caller must ensure no concurrent reader (see the
 * filesystem_patternSnapshotInUse() gate). Affiliates: copyClearService.c
 * overlapping paste engine, pat_snapshotScene(), pat_autosaveSnapshot().
 */
uint8_t *pat_backgroundPoolMut(void)
{
    return pat_background_region.pool;
}

/*
 * Compute one resident Scene's dynamic-pool occupancy for the Global widget.
 *
 * What: count set bits in the first PAT_STACK_SIZE bitmap bytes, which cover
 * the 2,048 backed four-byte chunks at the current 8,192-byte pool size, and
 * convert that count to a saturated 0..99 percentage. Why: the settings
 * widget needs a bounded one-shot reading while the bitmap is inside a
 * packed resident region. Inputs are a resident Scene index; invalid input
 * returns zero. Each four-byte word is copied with memcpy before popcount so
 * no unaligned access is assumed. Affiliates: pat_sceneRegion(),
 * PatternStackService.c, and menu.c.
 */
uint8_t pat_poolUsagePercent(uint8_t scene_index)
{
    const pat_scene_region_t *region = pat_sceneRegion(scene_index);
    uint32_t used = 0u;
    uint32_t word;
    uint16_t i;

    if (!region)
        return 0u;
    for (i = 0u; i < (uint16_t)(PAT_STACK_SIZE / 4u); i++) {
        memcpy(&word, &region->bitmap[i * 4u], sizeof(word));
        used += (uint32_t)__builtin_popcount(word);
    }
    /* S075: report occupancy against the allocatable pool (reserve excluded). */
    used = (used * 100u) / PAT_POOL_ALLOC_CHUNKS;
    return used > 99u ? 99u : (uint8_t)used;
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
 * four-byte units, treating service-owned trailing reservations as unavailable
 * to new blocks. Why: menu-paced special edits need a bounded synchronous
 * allocator; defragmentation and relocation are deferred, while reserved slack
 * remains available to its in-place Gate-6 owner. Inputs: Scene region and a
 * nonzero chunk count. Output: byte offset on success or PAT_ADDR_SENTINEL
 * when the pool has no suitable unreserved run. Affiliates: pat_poolFree(),
 * pat_writeSpecials(), and pat_blockChunks().
 */
static uint16_t pat_poolAlloc(pat_scene_region_t *r, uint8_t chunks)
{
    /*
     * Keep the permanent swap block out of ordinary allocation (S075).
     *
     * What: first-fit search stops at PAT_POOL_ALLOC_CHUNKS, so the top
     * PAT_POOL_SWAP_CHUNKS chunks (one maximum 132 B block) are never handed
     * to menu edits, service work, or load. Why: a copy/clear paste can then
     * always place one maximum block even in a full pool, and the reserve is
     * kept for future uses. A block already living in the reserve (placed by
     * pat_rawPlaceViaSwap()) stays readable and is released normally by
     * pat_poolFree(). Inputs: requested chunk count. Output: an offset below
     * PAT_POOL_SWAP_OFFSET or PAT_ADDR_SENTINEL. Affiliates: config.h swap
     * constants, pat_rawPlaceViaSwap(), pat_rawSwapReturn().
     */
    uint16_t max_chunk = (uint16_t)PAT_POOL_ALLOC_CHUNKS;
    uint16_t start;
    uint16_t run;
    uint16_t i;

    if (!r || chunks == 0u)
        return PAT_ADDR_SENTINEL;

    start = 0u;
    while ((uint32_t)start + chunks <= max_chunk) {
        run = 0u;
        for (i = start; i < (uint16_t)(start + chunks); i++) {
            if (pat_bitmapGet(r, i) || patSvc_isChunkReserved(i)) {
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
 * What: clear the block's bitmap run and zero its bytes, also releasing the
 * former positional trailing reservation. Why: erase, clear, replacement, and
 * reallocation must reclaim storage without leaving a stale soft claim in the
 * service image. Inputs: region, original aligned byte offset, and original
 * chunk count. Output: the allocation is free; malformed offsets/runs are
 * ignored. The caller updates its address entry separately. Affiliates:
 * pat_poolAlloc(), pat_eraseStep(), pat_clearTrack(), and pat_writeSpecials().
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
    if ((uint32_t)base_chunk + chunks < max_chunk)
        patSvc_consumeReservation((uint16_t)(base_chunk + chunks));
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
 * Append one new automation entry into an adjacent free chunk run.
 *
 * What: grow an unchanged-flags block in place only when the new operation is
 * exactly one appended automation and the extra logical chunks are adjacent
 * and free. A reserved trailing chunk is accepted here because positional
 * ownership makes it this block's own slack; new allocations reject it. The
 * new entry is written before the header count is updated. Why: this is the
 * safe Gate-6 growth optimization; existing block bytes are never cleared or
 * rewritten while TIM3 can read them. Inputs: the old block, the complete
 * requested automation list, and its new chunk count. Output: nonzero on an
 * in-place append; zero leaves the block untouched so the normal disjoint
 * write-new/swap/free-old path can run. Affiliate: pat_writeDynamic().
 */
static uint8_t pat_tryAppendAutomation(pat_scene_region_t *r,
                                       uint16_t old_offset,
                                       uint8_t old_chunks,
                                       uint8_t old_flags,
                                       uint8_t old_count,
                                       const pat_automation_entry_t *autos,
                                       uint8_t auto_count,
                                       uint8_t new_chunks)
{
    uint8_t value_count = 0u;
    uint16_t byte_index;
    uint16_t i;

    if (!r || !autos || old_count >= PAT_BLOCK_AUTO_COUNT_MASK ||
        auto_count != (uint8_t)(old_count + 1u) ||
        pat_blockChunks(old_flags, old_count) != old_chunks ||
        pat_blockChunks(old_flags, auto_count) != new_chunks)
        return 0u;
    if (old_flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (old_flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (old_flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    /*
     * S075: in-place growth must stay below the permanent swap-block reserve.
     * A failed bound falls back to the disjoint write-new path.
     */
    if ((uint32_t)(old_offset >> 2u) + new_chunks > PAT_POOL_ALLOC_CHUNKS)
        return 0u;
    for (i = old_chunks; i < new_chunks; i++) {
        if (pat_bitmapGet(r, (uint16_t)((old_offset >> 2u) + i)))
            return 0u;
    }
    byte_index = (uint16_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count);
    /* Confirm the old entries remain byte-for-byte in their original order. */
    for (i = 0u; i < old_count; i++) {
        uint16_t packed = (uint16_t)(r->pool[old_offset + byte_index] |
                                     ((uint16_t)r->pool[old_offset + byte_index + 1u]
                                      << 8u));
        uint16_t expected = (uint16_t)(
            ((uint16_t)(autos[i].value & 0x7Fu) << 9u) |
            (autos[i].target & 0x01FFu));

        if (packed != expected)
            return 0u;
        byte_index = (uint16_t)(byte_index + 2u);
    }
    for (i = old_chunks; i < new_chunks; i++)
        pat_bitmapSet(r, (uint16_t)((old_offset >> 2u) + i));
    /* Occupancy publication consumes any owned reservations in the same
     * transaction, so no chunk remains both reserved and occupied. */
    for (i = old_chunks; i < new_chunks; i++)
        patSvc_consumeReservation((uint16_t)((old_offset >> 2u) + i));
    {
        uint16_t packed = (uint16_t)(
            ((uint16_t)(autos[old_count].value & 0x7Fu) << 9u) |
            (autos[old_count].target & 0x01FFu));

        r->pool[old_offset + byte_index] = (uint8_t)packed;
        r->pool[old_offset + byte_index + 1u] = (uint8_t)(packed >> 8u);
    }
    /* The count is published last; its two step-id bits are retained. */
    r->pool[old_offset + 1u] = (uint8_t)(
        (r->pool[old_offset + 1u] & (uint8_t)~PAT_BLOCK_AUTO_COUNT_MASK) |
        (auto_count & PAT_BLOCK_AUTO_COUNT_MASK));
    pat_markSceneDirty((uint8_t)(r - pat_regions));
    return 1u;
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
        /*
         * Detach before freeing the old block.
         *
         * What: publish the no-data entry before clearing the old bitmap run
         * and pool bytes. Why: TIM3 may preempt this foreground writer after
         * the publish; it must see either the old complete block or the
         * sentinel, never an old address whose bytes have already been
         * zeroed. The short PRIMASK section also re-reads the live trigger bit
         * so a concurrent static trigger edit is retained. Inputs: the live
         * entry and its captured old offset. Output: a safe sentinel followed
         * by old-block reclamation. Affiliate: pat_eraseStep().
         */
        if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
            pat_poolOffsetValid(old_offset)) {
            old_chunks = pat_blockChunks(
                r->pool[old_offset + 2u],
                (uint8_t)(r->pool[old_offset + 1u] &
                          PAT_BLOCK_AUTO_COUNT_MASK));
        }
        {
            uint16_t published;

            __asm volatile("cpsid i" ::: "memory");
            trigger_bits = (uint16_t)(*entry & PAT_ADDR_TRIGGER_BIT);
            published = (uint16_t)(trigger_bits | PAT_ADDR_SENTINEL);
            *entry = published;
            __asm volatile("cpsie i" ::: "memory");
        }
        if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
            pat_poolOffsetValid(old_offset))
            pat_poolFree(r, old_offset, old_chunks);
        pat_markSceneDirty(scene_index);
        return 1u;
    }

    new_chunks = pat_blockChunks(new_flags, auto_count);
    if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
        pat_poolOffsetValid(old_offset)) {
        uint8_t old_flags;
        uint8_t old_count;

        old_flags = (uint8_t)(r->pool[old_offset + 2u] &
                              PAT_SPECIAL_FLAGS_MASK);
        old_count = (uint8_t)(r->pool[old_offset + 1u] &
                              PAT_BLOCK_AUTO_COUNT_MASK);
        old_chunks = pat_blockChunks(
            r->pool[old_offset + 2u],
            (uint8_t)(r->pool[old_offset + 1u] & PAT_BLOCK_AUTO_COUNT_MASK));
        if (new_flags == old_flags && new_chunks > old_chunks &&
            pat_tryAppendAutomation(r, old_offset, old_chunks, old_flags,
                                    old_count, autos, auto_count,
                                    new_chunks))
            return 1u;
    }

    new_offset = pat_poolAlloc(r, new_chunks);
    if (new_offset == PAT_ADDR_SENTINEL) {
        /*
         * Do not rewrite a live block in place on allocation failure.
         *
         * What: retain the old block and report failure when no disjoint
         * replacement run exists. Why: even a removal can compact existing
         * automation bytes, so rewriting the old block before publishing a
         * replacement would let TIM3 observe a partially rewritten block.
         * The service's deferred compaction path may create a run and retry
         * the operation. Inputs: old/new block sizes and the caller's legacy
         * shrink hint. Output: no live state changes on failure. Affiliate:
         * PatternStackService.c reactive compaction.
         */
        (void)old_chunks;
        (void)allow_shrink_in_place;
        return 0u;
    }

    /*
     * Publish the complete replacement before returning the old run.
     *
     * What: write the new block, atomically publish its offset with the latest
     * trigger bit, then free the old allocation. Why: the aligned address
     * halfword is TIM3's only pool pointer; publish-then-free guarantees that
     * playback sees either complete old bytes or complete new bytes. Inputs:
     * new_offset/new block and old_offset/old block. Output: one committed
     * address swap and one reclaimed old run. Affiliate: pat_poolAlloc().
     */
    pat_blockWrite(r, new_offset, track, step, new_flags, note, velocity,
                   probability, autos, auto_count);
    {
        uint16_t published;

        __asm volatile("cpsid i" ::: "memory");
        trigger_bits = (uint16_t)(*entry & PAT_ADDR_TRIGGER_BIT);
        published = (uint16_t)(trigger_bits | PAT_ADDR_SPECIALS_BIT |
                               new_offset);
        *entry = published;
        __asm volatile("cpsie i" ::: "memory");
    }
    if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
        pat_poolOffsetValid(old_offset))
        pat_poolFree(r, old_offset, old_chunks);
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
static uint8_t pat_writeSpecials(uint8_t scene_index, uint8_t track,
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
        return 0u;
    offset = (uint16_t)(*entry & PAT_ADDR_OFFSET_MASK);
    if (((*entry & PAT_ADDR_SPECIALS_BIT) != 0u) &&
        pat_poolOffsetValid(offset))
        auto_count = pat_blockReadAutomations(r, offset, autos,
                                              PAT_BLOCK_AUTO_COUNT_MASK);
    return pat_writeDynamic(scene_index, track, step, new_flags, note, velocity,
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
    /*
     * Per-track playback settings defaults.
     *
     * track_length: the number of steps the sequencer visits before wrapping
     * this track. Defaults to NUM_STEPS_PER_BAR (16) so a freshly initialized
     * Scene plays one 16-step bar per track, matching the sequencer's historic
     * fixed behavior. seq_advanceTrackStep() reads this at each step boundary
     * and wraps independently per track; valid range is 1–NUM_STEPS (128).
     *
     * track_scale: consumed by the per-track DDA accumulators (S078). The
     * value is a 128-position CC on StepScale's shared log curve; the default
     * is 76 (1/16).
     *
     * track_shuffle: consumed by the per-track shuffle deferral (S078). 0 is
     * the no-shuffle offset.
     *
     * track_play_mode: 0 fwd is the default; see pat_scene_region_t.
     */
    for (track = 0u; track < NUM_TRACKS; track++) {
        region->track_length[track] = NUM_STEPS_PER_BAR;
        region->track_scale[track] = TRACK_SCALE_DEFAULT;
        region->track_shuffle[track] = 0u;
        region->track_play_mode[track] = 0u;
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
     * Erase one step's complete address state with detach-before-free order.
     *
     * What: capture the old block, publish PAT_ADDR_SENTINEL under a short
     * PRIMASK section, then clear the detached pool run. Why: TIM3 must never
     * read an old address after its pool bytes have been zeroed. The operation
     * is destructive and intentionally clears the trigger bit as well as the
     * specials/offset. Inputs: bounded Scene/track/step coordinates. Output:
     * no-data address plus reclaimed old storage. Affiliate: deferred live
     * erase through PatternStackService.c.
     */
    if (!entry)
        return;
    addr = *entry;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    {
        __asm volatile("cpsid i" ::: "memory");
        *entry = PAT_ADDR_SENTINEL;
        __asm volatile("cpsie i" ::: "memory");
    }
    if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
        pat_poolOffsetValid(offset)) {
        pat_scene_region_t *r = &pat_regions[scene_index];
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                         (uint8_t)(r->pool[offset + 1u] &
                                                   PAT_BLOCK_AUTO_COUNT_MASK));
        pat_poolFree(r, offset, chunks);
    }
    pat_markSceneDirty(scene_index);
}

/*
 * Detach one dynamic block while preserving the latest trigger bit.
 *
 * What: publish the sentinel with bit 15 copied from the live address, then
 * free the captured logical block. Why: queued live erase and track-clear
 * barriers own pool reclamation in foreground context, while a trigger edit
 * may have arrived after the request was accepted. Inputs are bounded
 * Scene/track/step coordinates. Output: dynamic content is gone and the
 * trigger state survives. Affiliate: PatternStackService.c queue executor.
 */
void pat_releaseStepDynamic(uint8_t scene_index, uint8_t track,
                            uint8_t step)
{
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t addr;
    uint16_t offset;
    uint16_t published;

    if (!entry)
        return;
    addr = *entry;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    __asm volatile("cpsid i" ::: "memory");
    published = (uint16_t)((*entry & PAT_ADDR_TRIGGER_BIT) |
                           PAT_ADDR_SENTINEL);
    *entry = published;
    __asm volatile("cpsie i" ::: "memory");
    if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
        pat_poolOffsetValid(offset)) {
        pat_scene_region_t *r = &pat_regions[scene_index];
        uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                         (uint8_t)(r->pool[offset + 1u] &
                                                   PAT_BLOCK_AUTO_COUNT_MASK));
        pat_poolFree(r, offset, chunks);
    }
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
     * What: detach each address before clearing its referenced pool run. Why:
     * a TIM3 read must see an intact old block or a sentinel, never a freed
     * allocation still named by the address array. Inputs: resident Scene and
     * track. Output: every trigger/address state is cleared and all detached
     * blocks are reclaimed. Affiliates: the stack-service clear barrier and
     * pat_poolFree().
     */
    if (!scene_indexValid(scene_index) || !pat_trackValid(track))
        return;
    r = &pat_regions[scene_index];
    for (step = 0u; step < NUM_STEPS; step++) {
        addr = r->address[track][step];
        offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
        if ((addr & PAT_ADDR_SPECIALS_BIT) != 0u &&
            pat_poolOffsetValid(offset)) {
            uint8_t chunks = pat_blockChunks(r->pool[offset + 2u],
                                             (uint8_t)(r->pool[offset + 1u] &
                                                       PAT_BLOCK_AUTO_COUNT_MASK));
            __asm volatile("cpsid i" ::: "memory");
            r->address[track][step] = PAT_ADDR_SENTINEL;
            __asm volatile("cpsie i" ::: "memory");
            pat_poolFree(r, offset, chunks);
        } else {
            __asm volatile("cpsid i" ::: "memory");
            r->address[track][step] = PAT_ADDR_SENTINEL;
            __asm volatile("cpsie i" ::: "memory");
        }
    }
    pat_markSceneDirty(scene_index);
}

void pat_clearPattern(uint8_t scene_index)
{
    /*
     * Clear the complete live Pattern region without touching Scene settings
     * or Kit data. Inputs: resident Scene index. Outputs: address array,
     * reserved pool, and free bitmap return to pat_initScene()'s empty state.
     * Affiliates: copyClearService.c through pat_rawRegionReset().
     */
    if (!scene_indexValid(scene_index))
        return;
    pat_initScene(scene_index);
    pat_markSceneDirty(scene_index);
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
    parameter_values[PAR_TRACK_PLAY_MODE] = region->track_play_mode[track];
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
 * Per-track play mode setter (S078 §4.1).
 *
 * What: writes the play mode byte for one track in the Pattern region.
 * Input: scene_index, track 0..6, value 0..5. Output: region updated, Pattern
 * AutoSave marked dirty. Affiliates: menu.c PAR_TRACK_PLAY_MODE edit handler,
 * pat_applyTrackSettingsToMenu().
 */
void pat_setTrackPlayMode(uint8_t scene_index, uint8_t track, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);

    if (!region || !pat_trackValid(track))
        return;
    region->track_play_mode[track] = value;
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
 * appended; the reserved PAT_AUTOMATION_TARGET_OFF entry is also accepted as
 * a persistent Menu no-op. Duplicate targets update in place, while a 64th
 * entry or pool exhaustion leaves the existing block unchanged. Affiliate:
 * instrumentManager_targetValid() and PatternStackService's D17 boundary.
 */
uint8_t pat_writeStepAutomation(uint8_t scene_index, uint8_t track,
                                uint8_t step, uint16_t target, uint8_t value)
{
    pat_automation_entry_t autos[PAT_BLOCK_AUTO_COUNT_MASK];
    pat_step_specials_t sp;
    uint8_t count;
    uint8_t i;

    if (!pat_addrPtr(scene_index, track, step) ||
        target > 0x01FFu ||
        (target != PAT_AUTOMATION_TARGET_OFF &&
         (target >= INSTRUMENT_TOTAL_ID_COUNT ||
          !instrumentManager_targetValid(scene_index, target,
                                         INSTRUMENT_TARGET_AUTOMATION))))
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
 * pat_writeSpecials() reallocates, updates, or frees the block as required and
 * returns nonzero only after the address/pool transaction commits.
 * Affiliates: menu PAR_STEP_NOTE dispatch and pat_readStepSpecials().
 */
uint8_t pat_setStepNote(uint8_t scene_index, uint8_t track, uint8_t step,
                        uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    if (value == PAT_DEFAULT_NOTE)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_NOTE_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_NOTE_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags,
                             value, sp.velocity, sp.probability);
}

/*
 * Set or clear one step's velocity override through a read-modify-write.
 *
 * What: retain note/probability specials while changing velocity. Why: the
 * Step volume encoder must not disturb another stored value, and
 * PAT_DEFAULT_VELOCITY needs no pool byte. Inputs: Scene/track/step and a
 * 0..127 velocity. Output: pat_writeSpecials() updates or releases storage
 * and returns nonzero only after the address/pool transaction commits.
 * Affiliates: menu PAR_STEP_VOLUME dispatch and pat_readStepSpecials().
 */
uint8_t pat_setStepVolume(uint8_t scene_index, uint8_t track, uint8_t step,
                          uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    if (value == PAT_DEFAULT_VELOCITY)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_VEL_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_VEL_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags,
                             sp.note, value, sp.probability);
}

/*
 * Set or clear one step's probability override through a read-modify-write.
 *
 * What: retain note/velocity specials while changing probability. Why: 127 is
 * the always-fire default and therefore needs no pool byte. Inputs:
 * Scene/track/step and a 0..127 probability. Output: pat_writeSpecials()
 * updates or releases storage and returns nonzero only after commit; lower
 * values gate playback probabilistically.
 * Affiliates: menu PAR_STEP_PROB dispatch and pat_readStepSpecials().
 */
uint8_t pat_setStepProbability(uint8_t scene_index, uint8_t track,
                               uint8_t step, uint8_t value)
{
    pat_step_specials_t sp = pat_readStepSpecials(scene_index, track, step);
    uint8_t new_flags;

    if (value == 127u)
        new_flags = (uint8_t)(sp.flags & (uint8_t)~PAT_SPECIAL_PROB_BIT);
    else
        new_flags = (uint8_t)(sp.flags | PAT_SPECIAL_PROB_BIT);
    return pat_writeSpecials(scene_index, track, step, new_flags,
                             sp.note, sp.velocity, value);
}

/* =======================================================================
 * S075 raw block API for the exclusive copy/clear holder.
 *
 * Contract: PatternData.h ("Raw block API for the exclusive copy/clear
 * holder"). Every function below may be called only between
 * patSvc_beginExclusive(scene) == 1 and patSvc_endExclusive(scene); no other
 * pool writer runs in that window. Every step write keeps the publication
 * order: write the new bytes into unreferenced pool space, publish the
 * complete 16-bit address entry in one PRIMASK store (re-reading the live
 * trigger bit inside the critical section when the policy keeps it), then
 * free the old run. A reader (TIM3 now; chaining or per-track playback later)
 * therefore sees a complete old or a complete new step, never a mixture.
 * ======================================================================= */

/*
 * Resolve a raw trigger policy against one live address entry.
 *
 * Inputs: the live entry (read inside the caller's critical section) and
 * PAT_RAW_TRIGGER_KEEP/OFF/ON. Output: the trigger bit to publish. Unknown
 * policies keep the live bit. Callers: every raw publication below.
 */
static uint16_t pat_rawTriggerBits(uint16_t live, uint8_t trigger_mode)
{
    if (trigger_mode == PAT_RAW_TRIGGER_OFF)
        return 0u;
    if (trigger_mode == PAT_RAW_TRIGGER_ON)
        return PAT_ADDR_TRIGGER_BIT;
    return (uint16_t)(live & PAT_ADDR_TRIGGER_BIT);
}

/*
 * Chunk count of the block a live entry references, or zero.
 *
 * Inputs: region and entry. Output: allocated chunks for a valid block;
 * zero for trigger-only or malformed entries. Used to free the old run after
 * a publication.
 */
static uint8_t pat_rawEntryChunks(const pat_scene_region_t *r, uint16_t entry)
{
    uint16_t offset = (uint16_t)(entry & PAT_ADDR_OFFSET_MASK);

    if ((entry & PAT_ADDR_SPECIALS_BIT) == 0u || !pat_poolOffsetValid(offset))
        return 0u;
    return pat_blockChunks(r->pool[offset + 2u],
                           (uint8_t)(r->pool[offset + 1u] &
                                     PAT_BLOCK_AUTO_COUNT_MASK));
}

/*
 * Copy an encoded block into the pool and stamp its back-reference.
 *
 * Inputs: region, destination offset (already owned by the caller), block
 * bytes and size, and the owning track/step. Output: pool bytes written with
 * header bits 15..6 = track*128+step and the block's automation count kept.
 * The bytes are not referenced by any address entry yet.
 */
static void pat_rawStore(pat_scene_region_t *r, uint16_t offset,
                         const uint8_t *block, uint8_t bytes,
                         uint8_t track, uint8_t step)
{
    uint16_t header;

    memcpy(&r->pool[offset], block, bytes);
    header = (uint16_t)((((uint16_t)track * NUM_STEPS + step) <<
                         PAT_BLOCK_STEP_ID_SHIFT) & PAT_BLOCK_STEP_ID_MASK);
    header |= (uint16_t)(block[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    r->pool[offset] = (uint8_t)(header >> 8u);
    r->pool[offset + 1u] = (uint8_t)header;
}

/*
 * Publish one complete address entry and free the run it replaced.
 *
 * Inputs: region, entry pointer, new specials/offset bits (0x3FFF for no
 * block), trigger policy. Output: one PRIMASK halfword store composed from the
 * live trigger and the new bits, then the old block is released through
 * pat_poolFree() (which also drops its trailing reservation).
 */
static void pat_rawPublish(pat_scene_region_t *r, uint16_t *entry,
                           uint16_t new_bits, uint8_t trigger_mode)
{
    uint16_t old;
    uint8_t old_chunks;

    __asm volatile("cpsid i" ::: "memory");
    old = *entry;
    *entry = (uint16_t)(pat_rawTriggerBits(old, trigger_mode) | new_bits);
    __asm volatile("cpsie i" ::: "memory");
    old_chunks = pat_rawEntryChunks(r, old);
    if (old_chunks != 0u)
        pat_poolFree(r, (uint16_t)(old & PAT_ADDR_OFFSET_MASK), old_chunks);
}

/*
 * Copy one live block (contract in PatternData.h).
 *
 * Output: block bytes (chunks*4) or 0 when the step has no block; *entry_out
 * always receives the live entry (sentinel for invalid coordinates).
 */
uint8_t pat_rawReadBlock(uint8_t scene_index, uint8_t track, uint8_t step,
                         uint8_t out[PAT_RAW_BLOCK_MAX], uint16_t *entry_out)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    const uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint16_t addr = entry ? *entry : PAT_ADDR_SENTINEL;
    uint8_t chunks;

    if (entry_out)
        *entry_out = addr;
    if (!r || !entry || !out)
        return 0u;
    chunks = pat_rawEntryChunks(r, addr);
    if (chunks == 0u || (uint16_t)chunks * 4u > PAT_RAW_BLOCK_MAX ||
        (uint32_t)(addr & PAT_ADDR_OFFSET_MASK) + (uint32_t)chunks * 4u >
            (PAT_STACK_SIZE * 32u))
        return 0u;
    memcpy(out, &r->pool[addr & PAT_ADDR_OFFSET_MASK], (size_t)chunks * 4u);
    return (uint8_t)(chunks * 4u);
}

/* Allocated byte size of an encoded block (contract in PatternData.h). */
uint8_t pat_rawBlockBytes(const uint8_t *block)
{
    uint8_t chunks;

    if (!block)
        return 0u;
    chunks = pat_blockChunks((uint8_t)(block[2] & PAT_SPECIAL_FLAGS_MASK),
                             (uint8_t)(block[1] & PAT_BLOCK_AUTO_COUNT_MASK));
    if ((uint16_t)chunks * 4u > PAT_RAW_BLOCK_MAX)
        return 0u;
    return (uint8_t)(chunks * 4u);
}

/*
 * Decode a caller-held block (contract in PatternData.h).
 *
 * Output: specials (defaults for absent fields) and the automation count
 * copied into autos (at most capacity). A NULL block decodes as "no block".
 */
uint8_t pat_rawDecode(const uint8_t *block, pat_step_specials_t *specials,
                      pat_automation_entry_t *autos, uint8_t capacity)
{
    uint8_t flags;
    uint8_t auto_count;
    uint8_t copy_count;
    uint8_t index = 3u;
    uint8_t i;

    if (specials) {
        specials->note = PAT_DEFAULT_NOTE;
        specials->velocity = PAT_DEFAULT_VELOCITY;
        specials->probability = 127u;
        specials->flags = 0u;
    }
    if (!block || pat_rawBlockBytes(block) == 0u)
        return 0u;
    flags = (uint8_t)(block[2] & PAT_SPECIAL_FLAGS_MASK);
    auto_count = (uint8_t)(block[1] & PAT_BLOCK_AUTO_COUNT_MASK);
    if (flags & PAT_SPECIAL_NOTE_BIT) {
        if (specials) specials->note = block[index];
        index++;
    }
    if (flags & PAT_SPECIAL_VEL_BIT) {
        if (specials) specials->velocity = block[index];
        index++;
    }
    if (flags & PAT_SPECIAL_PROB_BIT) {
        if (specials) specials->probability = block[index];
        index++;
    }
    if (specials)
        specials->flags = flags;
    if (!autos || capacity == 0u)
        return auto_count;
    copy_count = (auto_count < capacity) ? auto_count : capacity;
    for (i = 0u; i < copy_count; i++) {
        uint16_t packed = (uint16_t)(block[index] |
                                     ((uint16_t)block[index + 1u] << 8u));
        autos[i].target = (uint16_t)(packed & 0x01FFu);
        autos[i].value = (uint8_t)((packed >> 9u) & 0x7Fu);
        index = (uint8_t)(index + 2u);
    }
    return copy_count;
}

/*
 * Encode a block into a caller buffer (contract in PatternData.h).
 *
 * Output: allocated byte size, or 0 for an empty block (no specials and no
 * automation) or invalid input. The back-reference is left 0; placement
 * stamps it.
 */
uint8_t pat_rawEncode(uint8_t out[PAT_RAW_BLOCK_MAX], uint8_t flags,
                      uint8_t note, uint8_t velocity, uint8_t probability,
                      const pat_automation_entry_t *autos, uint8_t count)
{
    uint8_t index = 3u;
    uint8_t bytes;
    uint8_t i;

    flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (!out || count > PAT_BLOCK_AUTO_COUNT_MASK ||
        (count != 0u && !autos) || (flags == 0u && count == 0u))
        return 0u;
    bytes = (uint8_t)(pat_blockChunks(flags, count) * 4u);
    if (bytes == 0u || bytes > PAT_RAW_BLOCK_MAX)
        return 0u;
    memset(out, 0, bytes);
    out[1] = (uint8_t)(count & PAT_BLOCK_AUTO_COUNT_MASK);
    out[2] = flags;
    if (flags & PAT_SPECIAL_NOTE_BIT) out[index++] = note;
    if (flags & PAT_SPECIAL_VEL_BIT) out[index++] = velocity;
    if (flags & PAT_SPECIAL_PROB_BIT) out[index++] = probability;
    for (i = 0u; i < count; i++) {
        uint16_t packed = (uint16_t)(((uint16_t)(autos[i].value & 0x7Fu) << 9u) |
                                     (autos[i].target & 0x01FFu));
        out[index++] = (uint8_t)packed;
        out[index++] = (uint8_t)(packed >> 8u);
    }
    return bytes;
}

/*
 * Place a block below the swap reserve (contract in PatternData.h).
 *
 * Output: 1 after allocate/write/publish/free; 0 when no contiguous run
 * below the reserve exists (nothing changed).
 */
uint8_t pat_rawPlace(uint8_t scene_index, uint8_t track, uint8_t step,
                     const uint8_t *block, uint8_t trigger_mode)
{
    pat_scene_region_t *r = pat_sceneRegionMut(scene_index);
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint8_t bytes = pat_rawBlockBytes(block);
    uint16_t offset;

    if (!r || !entry || bytes == 0u)
        return 0u;
    offset = pat_poolAlloc(r, (uint8_t)(bytes / 4u));
    if (offset == PAT_ADDR_SENTINEL)
        return 0u;
    pat_rawStore(r, offset, block, bytes, track, step);
    pat_rawPublish(r, entry, (uint16_t)(PAT_ADDR_SPECIALS_BIT | offset),
                   trigger_mode);
    pat_markSceneDirty(scene_index);
    return 1u;
}

/*
 * Place a block in the swap reserve (contract in PatternData.h).
 *
 * Output: 1 when the reserve was free and the step now references it; 0 when
 * the reserve is occupied. The reserve chunks are marked occupied while the
 * step lives there; pat_rawSwapReturn() moves it below the reserve.
 */
uint8_t pat_rawPlaceViaSwap(uint8_t scene_index, uint8_t track, uint8_t step,
                            const uint8_t *block, uint8_t trigger_mode)
{
    pat_scene_region_t *r = pat_sceneRegionMut(scene_index);
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint8_t bytes = pat_rawBlockBytes(block);
    uint8_t i;

    if (!r || !entry || bytes == 0u ||
        bytes / 4u > PAT_POOL_SWAP_CHUNKS || !pat_rawSwapFree(scene_index))
        return 0u;
    for (i = 0u; i < bytes / 4u; i++)
        pat_bitmapSet(r, (uint16_t)(PAT_POOL_ALLOC_CHUNKS + i));
    pat_rawStore(r, PAT_POOL_SWAP_OFFSET, block, bytes, track, step);
    pat_rawPublish(r, entry,
                   (uint16_t)(PAT_ADDR_SPECIALS_BIT | PAT_POOL_SWAP_OFFSET),
                   trigger_mode);
    pat_markSceneDirty(scene_index);
    return 1u;
}

/*
 * Move a swap-resident step below the reserve (contract in PatternData.h).
 *
 * Output: 1 after allocate/copy/publish (trigger kept) and the reserve has
 * been freed; 0 when the step is not in the swap block or no run exists yet
 * (the caller compacts and retries).
 */
uint8_t pat_rawSwapReturn(uint8_t scene_index, uint8_t track, uint8_t step)
{
    pat_scene_region_t *r = pat_sceneRegionMut(scene_index);
    uint16_t *entry = pat_addrPtr(scene_index, track, step);
    uint8_t block[PAT_RAW_BLOCK_MAX];
    uint8_t bytes;
    uint16_t offset;

    if (!r || !entry ||
        (*entry & PAT_ADDR_SPECIALS_BIT) == 0u ||
        (*entry & PAT_ADDR_OFFSET_MASK) != PAT_POOL_SWAP_OFFSET)
        return 0u;
    bytes = pat_rawBlockBytes(&r->pool[PAT_POOL_SWAP_OFFSET]);
    if (bytes == 0u)
        return 0u;
    offset = pat_poolAlloc(r, (uint8_t)(bytes / 4u));
    if (offset == PAT_ADDR_SENTINEL)
        return 0u;
    memcpy(block, &r->pool[PAT_POOL_SWAP_OFFSET], bytes);
    pat_rawStore(r, offset, block, bytes, track, step);
    /* pat_rawPublish() frees the swap run through pat_poolFree(). */
    pat_rawPublish(r, entry, (uint16_t)(PAT_ADDR_SPECIALS_BIT | offset),
                   PAT_RAW_TRIGGER_KEEP);
    pat_markSceneDirty(scene_index);
    return 1u;
}

/*
 * Publish "no block" for one step (contract in PatternData.h).
 *
 * Output: one publication of trigger|0x3FFF, then the old block is freed.
 */
void pat_rawPublishEmpty(uint8_t scene_index, uint8_t track, uint8_t step,
                         uint8_t trigger_mode)
{
    pat_scene_region_t *r = pat_sceneRegionMut(scene_index);
    uint16_t *entry = pat_addrPtr(scene_index, track, step);

    if (!r || !entry)
        return;
    pat_rawPublish(r, entry, PAT_ADDR_SENTINEL, trigger_mode);
    pat_markSceneDirty(scene_index);
}

/* Free chunks below the reserve (contract in PatternData.h). */
uint16_t pat_rawFreeChunks(uint8_t scene_index)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    uint16_t i;
    uint16_t free_count = 0u;

    if (!r)
        return 0u;
    for (i = 0u; i < PAT_POOL_ALLOC_CHUNKS; i++)
        if (!pat_bitmapGet(r, i))
            free_count++;
    return free_count;
}

/* Nonzero when every reserve chunk is free (contract in PatternData.h). */
uint8_t pat_rawSwapFree(uint8_t scene_index)
{
    const pat_scene_region_t *r = pat_sceneRegion(scene_index);
    uint8_t i;

    if (!r)
        return 0u;
    for (i = 0u; i < PAT_POOL_SWAP_CHUNKS; i++)
        if (pat_bitmapGet(r, (uint16_t)(PAT_POOL_ALLOC_CHUNKS + i)))
            return 0u;
    return 1u;
}

/*
 * Whole-region step 1: make every destination entry empty, trigger off.
 *
 * Each entry is one aligned halfword store, atomic for TIM3. After this pass
 * no destination entry references the pool, so the body can be replaced.
 */
void pat_rawRegionSilence(uint8_t scene_index)
{
    pat_scene_region_t *r = pat_sceneRegionMut(scene_index);
    uint8_t track;
    uint16_t step;

    if (!r)
        return;
    for (track = 0u; track < NUM_TRACKS; track++)
        for (step = 0u; step < NUM_STEPS; step++)
            r->address[track][step] = PAT_ADDR_SENTINEL;
}

/*
 * Whole-region step 2: copy pool, bitmap, track settings and Pattern globals.
 *
 * Call only after pat_rawRegionSilence(dst): the destination pool is not
 * referenced while it is overwritten.
 */
void pat_rawRegionCopyBody(uint8_t src_scene, uint8_t dst_scene)
{
    const pat_scene_region_t *src = pat_sceneRegion(src_scene);
    pat_scene_region_t *dst = pat_sceneRegionMut(dst_scene);

    if (!src || !dst || src_scene == dst_scene)
        return;
    memcpy(dst->pool, src->pool, sizeof(dst->pool));
    memcpy(dst->bitmap, src->bitmap, sizeof(dst->bitmap));
    memcpy(dst->track_length, src->track_length, sizeof(dst->track_length));
    memcpy(dst->track_scale, src->track_scale, sizeof(dst->track_scale));
    memcpy(dst->track_shuffle, src->track_shuffle, sizeof(dst->track_shuffle));
    memcpy(dst->track_play_mode, src->track_play_mode,
           sizeof(dst->track_play_mode));
    dst->pattern_change_bar = src->pattern_change_bar;
    dst->pattern_next = src->pattern_next;
}

/*
 * Whole-region step 3 (literal copy): publish source entries unchanged.
 *
 * Inputs: flat address range (index = track*128 + step). Output: the copied
 * pool offsets are valid in the destination because the body is identical.
 */
void pat_rawRegionPublishSteps(uint8_t src_scene, uint8_t dst_scene,
                               uint16_t first, uint16_t count)
{
    const pat_scene_region_t *src = pat_sceneRegion(src_scene);
    pat_scene_region_t *dst = pat_sceneRegionMut(dst_scene);
    uint16_t i;

    if (!src || !dst || first >= PAT_STEPS_PER_SCENE)
        return;
    if ((uint32_t)first + count > PAT_STEPS_PER_SCENE)
        count = (uint16_t)(PAT_STEPS_PER_SCENE - first);
    for (i = 0u; i < count; i++) {
        uint16_t index = (uint16_t)(first + i);

        dst->address[index / NUM_STEPS][index % NUM_STEPS] =
            src->address[index / NUM_STEPS][index % NUM_STEPS];
    }
    pat_markSceneDirty(dst_scene);
}

/*
 * Whole-region step 3 (retarget copy): read the copied, unpublished block.
 *
 * Inputs: source/destination Scenes and flat index. Output: block bytes taken
 * from the destination pool at the offset the source entry names, or 0 when
 * the source step has no block.
 */
uint8_t pat_rawRegionCopiedBlock(uint8_t src_scene, uint8_t dst_scene,
                                 uint16_t index, uint8_t out[PAT_RAW_BLOCK_MAX])
{
    const pat_scene_region_t *src = pat_sceneRegion(src_scene);
    const pat_scene_region_t *dst = pat_sceneRegion(dst_scene);
    uint16_t entry;
    uint8_t chunks;

    if (!src || !dst || !out || index >= PAT_STEPS_PER_SCENE)
        return 0u;
    entry = src->address[index / NUM_STEPS][index % NUM_STEPS];
    chunks = pat_rawEntryChunks(dst, entry);
    if (chunks == 0u || (uint16_t)chunks * 4u > PAT_RAW_BLOCK_MAX)
        return 0u;
    memcpy(out, &dst->pool[entry & PAT_ADDR_OFFSET_MASK], (size_t)chunks * 4u);
    return (uint8_t)(chunks * 4u);
}

/*
 * Whole-region step 3 (retarget copy): rewrite in place, then publish.
 *
 * Inputs: source/destination Scenes, flat index, and the rewritten block
 * (NULL or empty = drop the block). The rewritten block must not be larger
 * than the copied one (retargeting only drops or renames entries). Output:
 * the copied block is overwritten while still unreferenced, tail chunks are
 * freed, then the destination entry is published with the source trigger.
 * Returns 0 (and publishes the copied block unchanged) if the rewrite would
 * grow the block.
 */
uint8_t pat_rawRegionPublishRewritten(uint8_t src_scene, uint8_t dst_scene,
                                      uint16_t index, const uint8_t *block)
{
    const pat_scene_region_t *src = pat_sceneRegion(src_scene);
    pat_scene_region_t *dst = pat_sceneRegionMut(dst_scene);
    uint16_t entry;
    uint16_t offset;
    uint8_t old_chunks;
    uint8_t new_chunks;
    uint8_t track;
    uint8_t step;

    if (!src || !dst || index >= PAT_STEPS_PER_SCENE)
        return 0u;
    track = (uint8_t)(index / NUM_STEPS);
    step = (uint8_t)(index % NUM_STEPS);
    entry = src->address[track][step];
    old_chunks = pat_rawEntryChunks(dst, entry);
    offset = (uint16_t)(entry & PAT_ADDR_OFFSET_MASK);
    new_chunks = (uint8_t)(pat_rawBlockBytes(block) / 4u);
    if (old_chunks == 0u) {
        dst->address[track][step] = entry;
        return 1u;
    }
    if (new_chunks > old_chunks) {
        dst->address[track][step] = entry;
        return 0u;
    }
    if (new_chunks == 0u) {
        pat_poolFree(dst, offset, old_chunks);
        dst->address[track][step] =
            (uint16_t)((entry & PAT_ADDR_TRIGGER_BIT) | PAT_ADDR_SENTINEL);
        pat_markSceneDirty(dst_scene);
        return 1u;
    }
    pat_rawStore(dst, offset, block, (uint8_t)(new_chunks * 4u), track, step);
    if (new_chunks < old_chunks)
        pat_poolFree(dst, (uint16_t)(offset + (uint16_t)new_chunks * 4u),
                     (uint8_t)(old_chunks - new_chunks));
    dst->address[track][step] = entry;
    pat_markSceneDirty(dst_scene);
    return 1u;
}

/*
 * Reset a whole region (spec §9.8, `clear pattern`).
 *
 * pat_initScene() already writes every address entry to the sentinel before
 * it clears the pool and bitmap, so the publication order holds.
 */
void pat_rawRegionReset(uint8_t scene_index)
{
    pat_initScene(scene_index);
    pat_markSceneDirty(scene_index);
}
