/*
 * PatternStackService.c — unified dynamic Pattern-pool mutation service.
 *
 * What: owns deferred pool edits, bulk barriers, mutation-target handover,
 * elastic gap maintenance, and paced compaction. Why: TIM3 playback may
 * publish trigger bits, but all pool bytes, bitmap bits, and dynamic address
 * changes must be serialized by one foreground owner. Inputs arrive through
 * the patSvc_* API or the TIM3 live-erase enqueue hook. Outputs are committed
 * PatternData mutations and bounded diagnostic trace records. Affiliates:
 * PatternData.c, sequencer.c, filesystem.c, and timebase.c.
 */

#include "PatternStackService.h"
#include "PatternTrace.h"
#include "sequencer.h"
#include "SceneData.h"
#include "timebase.h"
#include "config.h"

#include <string.h>

#define PATSVC_QUEUE_SIZE       64u
#define PATSVC_QUEUE_MASK       (PATSVC_QUEUE_SIZE - 1u)
#define PATSVC_STEP_MASK        0x03FFu
#define PATSVC_OPERATION_MASK   0x3Fu
#define PATSVC_POOL_CHUNKS      (PAT_STACK_SIZE * 8u)
#define PATSVC_POOL_BYTES       (PAT_STACK_SIZE * 32u)
#define PATSVC_ADDRESS_COUNT    PAT_STEPS_PER_SCENE
#define PATSVC_BULK_STEPS       8u
#define PATSVC_STEP_ANY         0xFFFFu

/*
 * Compact FIFO operation identifiers.
 *
 * What: six low event bits leave ten bits for the track/step identity and
 * sixteen bits for operation payload. Why: all foreground and TIM3 deferred
 * work has one four-byte queue geometry. Inputs/outputs are internal enum
 * values only; the event format is consumed by the helpers below.
 */
typedef enum {
    PATSVC_OP_NONE = 0u,
    PATSVC_OP_DELETE_DYNAMIC,
    PATSVC_OP_WRITE_AUTOMATION,
    PATSVC_OP_REMOVE_AUTOMATION,
    PATSVC_OP_SET_NOTE,
    PATSVC_OP_SET_VOLUME,
    PATSVC_OP_SET_PROBABILITY,
    PATSVC_OP_CLEAR_TRACK_BARRIER,
    PATSVC_OP_REMOVE_TRACK_TARGET_BARRIER,
    PATSVC_OP_CLEAR_PATTERN
} patSvc_operation_t;

/*
 * One four-byte SPSC event per queue slot.
 *
 * What: producer/consumer cursors are protected at publication so TIM3 and
 * foreground producers cannot collide. Why: queueing keeps all pool work out
 * of ISR context while retaining all 64 slots. RAM: 256 bytes for entries and
 * two cursor bytes. Affiliate: patSvc_enqueue().
 */
static volatile uint32_t service_queue[PATSVC_QUEUE_SIZE];
static volatile uint8_t service_queue_prod;
static volatile uint8_t service_queue_cons;

/*
 * Mutation-target and handover state.
 *
 * What: service_scene identifies the one Scene whose pool may be mutated;
 * service_open controls admission; service_handover keeps old queued work
 * draining after playback changes target at a TIM3 boundary. Why: playback
 * switching is immediate, but foreground ownership transfer is serialized.
 * RAM: four SRAM1 bytes. Affiliates: seq_activePattern and patSvc_tick().
 */
static uint8_t service_scene;
static uint8_t service_open;
static uint8_t service_handover;
static uint8_t service_replace_pending;

/*
 * Bulk barrier cursor state.
 *
 * What: retains one FIFO barrier's operation, target, and 0..127 step cursor.
 * Why: track clear and track-wide target removal must not monopolize a
 * foreground pass. RAM: six SRAM1 bytes, including the reserved future
 * multi-track cursor. Affiliate: patSvc_tick().
 */
static uint8_t bulk_op;
static uint8_t bulk_track;
static uint16_t bulk_target;
static uint8_t bulk_step_cursor;
static uint8_t bulk_track_cursor;

/*
 * Bounded maintenance/recovery state.
 *
 * What: cursors split Tier 1/Tier 2 work across service ticks; reactive state
 * retains a blocked queue head while fragmentation is compacted. Why: a
 * failed queue allocation must not permanently block FIFO progress or starve
 * handover/AutoSave. RAM: ten bytes including the running logical occupancy
 * count and last compaction tick. Affiliates: the bitmap/address helpers.
 */
static uint16_t tier1_scan_cursor;
static uint16_t tier2_scan_cursor;
static uint16_t reactive_scan_cursor;
static uint16_t logical_chunks_used;
static uint16_t last_compact_tick;
static uint16_t reactive_required;
static uint8_t reactive_active;

/* Return one packed event from operation, step identity, and payload. */
static uint32_t patSvc_packEvent(uint8_t operation, uint8_t track,
                                 uint8_t step, uint16_t payload)
{
    uint16_t step_id = (uint16_t)(track * NUM_STEPS + step);

    return (uint32_t)(operation & PATSVC_OPERATION_MASK) |
           ((uint32_t)(step_id & PATSVC_STEP_MASK) << 6u) |
           ((uint32_t)payload << 16u);
}

/* Decode the operation field of one queue event. */
static uint8_t patSvc_eventOperation(uint32_t event)
{
    return (uint8_t)(event & PATSVC_OPERATION_MASK);
}

/* Decode the ten-bit track/step identity of one queue event. */
static uint16_t patSvc_eventStepId(uint32_t event)
{
    return (uint16_t)((event >> 6u) & PATSVC_STEP_MASK);
}

/* Decode the operation-specific sixteen-bit payload. */
static uint16_t patSvc_eventPayload(uint32_t event)
{
    return (uint16_t)(event >> 16u);
}

/* Return the number of pending queue entries using wrapping uint8 cursors. */
static uint8_t patSvc_queueCount(void)
{
    return (uint8_t)(service_queue_prod - service_queue_cons);
}

/*
 * Trace and reject work that cannot target the current service Scene.
 *
 * What: records a compact X witness for wrong-scene or closed-admission work.
 * Why: callers receive the existing Boolean/void contract, so diagnostics are
 * the only visibility for rejected deferred requests. Inputs: scene and event
 * word; output is trace-only. Affiliate: all patSvc_* admission wrappers.
 */
static void patSvc_rejectScene(uint8_t scene, uint32_t event)
{
    patternTrace_record(PAT_TRACE_STAGE_WRONG_SCENE,
                        (uint8_t)(service_scene & 0x0Fu),
                        event | ((uint32_t)scene << 24u));
}

/*
 * Publish one four-byte queue event under a short PRIMASK section.
 *
 * What: reserve an all-usable ring slot, copy the complete event, and advance
 * the producer cursor. Why: TIM3 and foreground producers share the ring, but
 * no allocation, scan, or block copy may run with interrupts disabled.
 * Inputs: packed event. Output: nonzero on admission, zero plus Q trace on a
 * full queue. Affiliate: patSvc_tick() consumer.
 */
static uint8_t patSvc_enqueue(uint32_t event)
{
    uint8_t distance;

    __asm volatile("cpsid i" ::: "memory");
    distance = (uint8_t)(service_queue_prod - service_queue_cons);
    if (distance >= PATSVC_QUEUE_SIZE) {
        __asm volatile("cpsie i" ::: "memory");
        patternTrace_record(PAT_TRACE_STAGE_QUEUE_OVERFLOW, 0u, event);
        return 0u;
    }
    service_queue[service_queue_prod & PATSVC_QUEUE_MASK] = event;
    service_queue_prod++;
    __asm volatile("cpsie i" ::: "memory");
    return 1u;
}

/* Consume the queue head after its mutation/barrier has completed. */
static void patSvc_consumeHead(void)
{
    service_queue_cons++;
}

/* Read one resident region owned by the current service target. */
static pat_scene_region_t *patSvc_region(uint8_t scene)
{
    return pat_sceneRegionMut(scene);
}

/* Check one packed address offset against the live pool geometry. */
static uint8_t patSvc_offsetValid(uint16_t offset)
{
    return (uint8_t)(offset != PAT_ADDR_SENTINEL &&
                     (offset & 3u) == 0u && offset < PATSVC_POOL_BYTES);
}

/* Read one free/occupied bit from the resident pool bitmap. */
static uint8_t patSvc_bitmapGet(const pat_scene_region_t *region,
                                uint16_t chunk)
{
    return (uint8_t)((region->bitmap[chunk >> 3u] >> (chunk & 7u)) & 1u);
}

/* Set one bitmap chunk occupied for a service-owned relocation destination. */
static void patSvc_bitmapSet(pat_scene_region_t *region, uint16_t chunk)
{
    region->bitmap[chunk >> 3u] |= (uint8_t)(1u << (chunk & 7u));
}

/* Clear one bitmap chunk after an address has been detached or swapped. */
static void patSvc_bitmapClear(pat_scene_region_t *region, uint16_t chunk)
{
    region->bitmap[chunk >> 3u] &= (uint8_t)~(1u << (chunk & 7u));
}

/*
 * Decode one block's logical chunk count without trusting malformed tails.
 *
 * What: reproduce the public block geometry from its flags/count bytes. Why:
 * relocation must enumerate address-backed blocks rather than treating every
 * occupied bitmap bit as a block start. Inputs: resident region and offset;
 * output is zero for malformed/unallocated data. Affiliate: compaction.
 */
static uint8_t patSvc_blockChunksAt(const pat_scene_region_t *region,
                                    uint16_t offset,
                                    uint16_t expected_step_id)
{
    uint16_t header;
    uint8_t flags;
    uint8_t auto_count;
    uint8_t value_count = 0u;
    uint16_t bytes;

    if (!region || !patSvc_offsetValid(offset))
        return 0u;
    header = (uint16_t)(((uint16_t)region->pool[offset] << 8u) |
                        region->pool[offset + 1u]);
    if (expected_step_id != PATSVC_STEP_ANY &&
        ((header & PAT_BLOCK_STEP_ID_MASK) >> PAT_BLOCK_STEP_ID_SHIFT) !=
            expected_step_id)
        return 0u;
    flags = (uint8_t)(region->pool[offset + 2u] & PAT_SPECIAL_FLAGS_MASK);
    auto_count = (uint8_t)(region->pool[offset + 1u] &
                           PAT_BLOCK_AUTO_COUNT_MASK);
    if (flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    bytes = (uint16_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count +
                       ((uint16_t)auto_count * 2u));
    bytes = (uint16_t)((bytes + 3u) & (uint16_t)~3u);
    if (bytes == 0u || (uint32_t)offset + bytes > PATSVC_POOL_BYTES)
        return 0u;
    return (uint8_t)(bytes >> 2u);
}

/* Calculate a logical block's chunk count from flags and automation count. */
static uint8_t patSvc_blockChunksFor(uint8_t flags, uint8_t auto_count)
{
    uint8_t value_count = 0u;
    uint16_t bytes;

    flags &= (uint8_t)PAT_SPECIAL_FLAGS_MASK;
    if (flags & PAT_SPECIAL_NOTE_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_VEL_BIT)
        value_count++;
    if (flags & PAT_SPECIAL_PROB_BIT)
        value_count++;
    if (auto_count > PAT_BLOCK_AUTO_COUNT_MASK)
        auto_count = PAT_BLOCK_AUTO_COUNT_MASK;
    bytes = (uint16_t)(PAT_BLOCK_HEADER_BYTES + 1u + value_count +
                       ((uint16_t)auto_count * 2u));
    return (uint8_t)((bytes + 3u) >> 2u);
}

/* Count occupied logical pool chunks for the current target. */
static uint16_t patSvc_countUsed(const pat_scene_region_t *region)
{
    uint16_t chunk;
    uint16_t used = 0u;

    if (!region)
        return 0u;
    for (chunk = 0u; chunk < PATSVC_POOL_CHUNKS; chunk++)
        used += patSvc_bitmapGet(region, chunk);
    return used;
}

/* Find the largest free bitmap run for allocation-failure classification. */
static uint16_t patSvc_largestFreeRun(const pat_scene_region_t *region)
{
    uint16_t chunk;
    uint16_t run = 0u;
    uint16_t largest = 0u;

    if (!region)
        return 0u;
    for (chunk = 0u; chunk < PATSVC_POOL_CHUNKS; chunk++) {
        if (!patSvc_bitmapGet(region, chunk)) {
            run++;
            if (run > largest)
                largest = run;
        } else {
            run = 0u;
        }
    }
    return largest;
}

/* Find a free run; lower_only is used by bottom-packing compaction. */
static uint16_t patSvc_findFreeRun(const pat_scene_region_t *region,
                                   uint16_t chunks, uint16_t upper_chunk,
                                   uint8_t lower_only)
{
    uint16_t start;
    uint16_t i;

    if (!region || chunks == 0u || chunks > PATSVC_POOL_CHUNKS)
        return PAT_ADDR_SENTINEL;
    for (start = 0u; (uint32_t)start + chunks <= PATSVC_POOL_CHUNKS;
         start++) {
        if (lower_only && (uint32_t)start + chunks > upper_chunk)
            break;
        for (i = 0u; i < chunks; i++) {
            if (patSvc_bitmapGet(region, (uint16_t)(start + i)))
                break;
        }
        if (i == chunks)
            return (uint16_t)(start << 2u);
    }
    return PAT_ADDR_SENTINEL;
}

/*
 * Publish one complete dynamic address under PRIMASK.
 *
 * What: re-read bit 15 immediately before the aligned halfword store and
 * compose the current trigger with the new specials/offset. Why: a static
 * trigger edit can occur between block copy and publication, and must not be
 * overwritten by relocation. Inputs: live entry and replacement offset;
 * output is one atomic address publication. Affiliate: TIM3 playback reader.
 */
static void patSvc_publishOffset(uint16_t *entry, uint16_t offset)
{
    uint16_t published;

    __asm volatile("cpsid i" ::: "memory");
    published = (uint16_t)((*entry & PAT_ADDR_TRIGGER_BIT) |
                           PAT_ADDR_SPECIALS_BIT | offset);
    *entry = published;
    __asm volatile("cpsie i" ::: "memory");
}

/* Pack the relocation witness required by PatternTrace's fixed record. */
static uint32_t patSvc_relocationValue(uint16_t step_id,
                                       uint16_t old_offset,
                                       uint16_t new_offset)
{
    return ((uint32_t)step_id & 0x03FFu) |
           (((uint32_t)(old_offset >> 2u) & 0x07FFu) << 10u) |
           (((uint32_t)(new_offset >> 2u) & 0x07FFu) << 21u);
}

/*
 * Relocate one validated address entry to a free run.
 *
 * What: reserve only the logical block chunks, copy complete bytes, publish
 * the new address, then free the old run. A positive gap argument searches a
 * larger free run but deliberately leaves the trailing gap free. Why: this is
 * the common write-new/swap/free-old transaction used by Tier 1 and Tier 2.
 * Inputs: Scene, address index, desired gap, and optional lower-only rule.
 * Output: nonzero when one block moved. Affiliate: patSvc_tick().
 */
static uint8_t patSvc_relocateIndex(uint8_t scene, uint16_t address_index,
                                    uint8_t gap, uint8_t lower_only,
                                    uint16_t *old_offset_out,
                                    uint16_t *new_offset_out)
{
    pat_scene_region_t *region = patSvc_region(scene);
    uint8_t track;
    uint8_t step;
    uint16_t *entry;
    uint16_t addr;
    uint16_t old_offset;
    uint16_t new_offset;
    uint8_t logical_chunks;
    uint16_t old_chunk;
    uint16_t required;
    uint16_t i;

    if (!region || address_index >= PATSVC_ADDRESS_COUNT)
        return 0u;
    track = (uint8_t)(address_index / NUM_STEPS);
    step = (uint8_t)(address_index % NUM_STEPS);
    entry = &region->address[track][step];
    addr = *entry;
    if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
        return 0u;
    old_offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    logical_chunks = patSvc_blockChunksAt(region, old_offset, address_index);
    if (logical_chunks == 0u)
        return 0u;
    old_chunk = (uint16_t)(old_offset >> 2u);
    required = (uint16_t)logical_chunks + gap;
    if (required > PATSVC_POOL_CHUNKS)
        return 0u;
    new_offset = patSvc_findFreeRun(region, required, old_chunk,
                                    lower_only);
    if (new_offset == PAT_ADDR_SENTINEL ||
        new_offset == old_offset)
        return 0u;

    for (i = 0u; i < logical_chunks; i++)
        patSvc_bitmapSet(region, (uint16_t)((new_offset >> 2u) + i));
    memmove(&region->pool[new_offset], &region->pool[old_offset],
            (size_t)logical_chunks * 4u);
    patSvc_publishOffset(entry, new_offset);
    for (i = 0u; i < logical_chunks; i++)
        patSvc_bitmapClear(region, (uint16_t)(old_chunk + i));
    memset(&region->pool[old_offset], 0, (size_t)logical_chunks * 4u);
    pat_markPoolMutationDirty(scene);
    if (old_offset_out)
        *old_offset_out = old_offset;
    if (new_offset_out)
        *new_offset_out = new_offset;
    return 1u;
}

/* Calculate the current elastic trailing-gap target. */
static uint8_t patSvc_gapTarget(void)
{
    uint32_t percent = ((uint32_t)logical_chunks_used * 100u) /
                       PATSVC_POOL_CHUNKS;

    return percent >= PAT_GAP_REDUCE_THRESHOLD ? 1u : 2u;
}

/* Count free chunks immediately following one logical block. */
static uint8_t patSvc_trailingGap(const pat_scene_region_t *region,
                                  uint16_t offset, uint8_t logical_chunks)
{
    uint16_t chunk = (uint16_t)((offset >> 2u) + logical_chunks);
    uint8_t gap = 0u;

    while (chunk < PATSVC_POOL_CHUNKS && !patSvc_bitmapGet(region, chunk)) {
        if (gap == UINT8_MAX)
            break;
        gap++;
        chunk++;
    }
    return gap;
}

/*
 * Inspect one Tier 1 candidate and create its requested trailing gap.
 *
 * Output: 1 for a successful relocation, 0 when the address is absent,
 * already has enough gap, or no suitable run exists. The caller owns the M/G
 * trace classification. Affiliate: patSvc_tick() priority 3.
 */
static uint8_t patSvc_tier1Step(uint8_t scene, uint16_t address_index,
                                uint8_t *fallback_used,
                                uint16_t *old_offset_out,
                                uint16_t *new_offset_out)
{
    pat_scene_region_t *region = patSvc_region(scene);
    uint8_t track;
    uint8_t step;
    uint16_t addr;
    uint16_t offset;
    uint8_t logical_chunks;
    uint8_t target_gap;
    uint8_t trailing;

    if (fallback_used)
        *fallback_used = 0u;
    if (old_offset_out)
        *old_offset_out = 0u;
    if (new_offset_out)
        *new_offset_out = 0u;
    if (!region || address_index >= PATSVC_ADDRESS_COUNT)
        return 0u;
    track = (uint8_t)(address_index / NUM_STEPS);
    step = (uint8_t)(address_index % NUM_STEPS);
    addr = region->address[track][step];
    if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
        return 0u;
    offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
    logical_chunks = patSvc_blockChunksAt(region, offset, address_index);
    if (logical_chunks == 0u)
        return 0u;
    target_gap = patSvc_gapTarget();
    trailing = patSvc_trailingGap(region, offset, logical_chunks);
    if (trailing >= target_gap)
        return 0u;
    if (patSvc_relocateIndex(scene, address_index, target_gap, 0u,
                             old_offset_out, new_offset_out))
        return 1u;
    if (target_gap > 1u &&
        patSvc_relocateIndex(scene, address_index, 1u, 0u,
                             old_offset_out, new_offset_out)) {
        if (fallback_used)
            *fallback_used = 1u;
        return 1u;
    }
    return 0u;
}

/*
 * Find one lower destination for a blocked queue head.
 *
 * What: inspect at most sixteen address entries from the retained reactive
 * cursor and move one block to a lower free run. Why: a queued allocation can
 * be retried without allowing compaction to starve FIFO progress. Inputs:
 * required destination size and reactive cursor. Output: one improving move
 * or cursor exhaustion. Affiliate: patSvc_drainQueue().
 */
static uint8_t patSvc_reactiveStep(uint8_t scene, uint16_t required)
{
    uint8_t inspected = 0u;

    /* The required size classifies the blocked head; every move may help. */
    (void)required;

    while (inspected < PAT_COMPACT_SCAN_PER_TICK &&
           reactive_scan_cursor < PATSVC_ADDRESS_COUNT) {
        uint16_t address_index = reactive_scan_cursor++;
        pat_scene_region_t *region = patSvc_region(scene);
        uint8_t track = (uint8_t)(address_index / NUM_STEPS);
        uint8_t step = (uint8_t)(address_index % NUM_STEPS);
        uint16_t addr;
        uint16_t offset;
        uint8_t chunks;
        uint16_t old_offset;
        uint16_t new_offset;

        inspected++;
        if (!region)
            continue;
        addr = region->address[track][step];
        if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
            continue;
        offset = (uint16_t)(addr & PAT_ADDR_OFFSET_MASK);
        chunks = patSvc_blockChunksAt(region, offset, address_index);
        /* Any lower move can coalesce the freed tail with another gap. */
        if (chunks == 0u)
            continue;
        if (patSvc_relocateIndex(scene, address_index, 0u, 1u,
                                 &old_offset, &new_offset)) {
            patternTrace_record(PAT_TRACE_STAGE_TIER2_RELOC,
                                (uint8_t)(scene & 0x0Fu),
                                patSvc_relocationValue(address_index,
                                                        old_offset,
                                                        new_offset));
            reactive_active = 0u;
            reactive_scan_cursor = 0u;
            return 1u;
        }
    }
    return 0u;
}

/* Compute the replacement block size expected by one queued operation. */
static uint8_t patSvc_requiredChunks(uint8_t scene, uint8_t operation,
                                     uint8_t track, uint8_t step,
                                     uint16_t payload)
{
    pat_step_specials_t specials;
    uint8_t count;
    uint8_t flags;

    if (operation == PATSVC_OP_WRITE_AUTOMATION) {
        count = pat_stepAutomationCount(scene, track, step);
        if (count < PAT_BLOCK_AUTO_COUNT_MASK)
            count++;
        specials = pat_readStepSpecials(scene, track, step);
        return patSvc_blockChunksFor(specials.flags, count);
    }
    if (operation == PATSVC_OP_REMOVE_AUTOMATION) {
        count = pat_stepAutomationCount(scene, track, step);
        if (count == 0u)
            return 0u;
        count--;
        specials = pat_readStepSpecials(scene, track, step);
        return patSvc_blockChunksFor(specials.flags, count);
    }
    if (operation != PATSVC_OP_SET_NOTE &&
        operation != PATSVC_OP_SET_VOLUME &&
        operation != PATSVC_OP_SET_PROBABILITY)
        return 0u;

    specials = pat_readStepSpecials(scene, track, step);
    flags = specials.flags;
    if (operation == PATSVC_OP_SET_NOTE) {
        if (payload == PAT_DEFAULT_NOTE)
            flags &= (uint8_t)~PAT_SPECIAL_NOTE_BIT;
        else
            flags |= PAT_SPECIAL_NOTE_BIT;
    } else if (operation == PATSVC_OP_SET_VOLUME) {
        if (payload == PAT_DEFAULT_VELOCITY)
            flags &= (uint8_t)~PAT_SPECIAL_VEL_BIT;
        else
            flags |= PAT_SPECIAL_VEL_BIT;
    } else if (payload == 127u) {
        flags &= (uint8_t)~PAT_SPECIAL_PROB_BIT;
    } else {
        flags |= PAT_SPECIAL_PROB_BIT;
    }
    count = pat_stepAutomationCount(scene, track, step);
    return patSvc_blockChunksFor(flags, count);
}

/* Execute one non-barrier event through the raw PatternData mutation API. */
static uint8_t patSvc_executeEvent(uint32_t event)
{
    uint8_t operation = patSvc_eventOperation(event);
    uint16_t step_id = patSvc_eventStepId(event);
    uint16_t payload = patSvc_eventPayload(event);
    uint8_t track = (uint8_t)(step_id / NUM_STEPS);
    uint8_t step = (uint8_t)(step_id % NUM_STEPS);

    switch (operation) {
    case PATSVC_OP_DELETE_DYNAMIC:
        /* Payload 1 is a foreground destructive erase; live TIM3 handoff
         * uses zero because it already cleared the trigger bit. */
        if (payload != 0u)
            pat_eraseStep(service_scene, track, step);
        else
            pat_releaseStepDynamic(service_scene, track, step);
        return 1u;
    case PATSVC_OP_WRITE_AUTOMATION:
        return pat_writeStepAutomation(service_scene, track, step,
                                       (uint16_t)(payload & 0x01FFu),
                                       (uint8_t)((payload >> 9u) & 0x7Fu));
    case PATSVC_OP_REMOVE_AUTOMATION:
        return pat_removeStepAutomation(service_scene, track, step,
                                         (uint16_t)(payload & 0x01FFu));
    case PATSVC_OP_SET_NOTE:
        return pat_setStepNote(service_scene, track, step, (uint8_t)payload);
    case PATSVC_OP_SET_VOLUME:
        return pat_setStepVolume(service_scene, track, step, (uint8_t)payload);
    case PATSVC_OP_SET_PROBABILITY:
        return pat_setStepProbability(service_scene, track, step,
                                      (uint8_t)payload);
    case PATSVC_OP_CLEAR_PATTERN:
        pat_clearPattern(service_scene);
        return 1u;
    default:
        return 0u;
    }
}

/* Begin a barrier without consuming its queue entry. */
static void patSvc_beginBulk(uint32_t event)
{
    uint8_t operation = patSvc_eventOperation(event);

    bulk_op = operation;
    bulk_track = (uint8_t)(patSvc_eventStepId(event) / NUM_STEPS);
    bulk_target = (uint16_t)(patSvc_eventPayload(event) & 0x01FFu);
    bulk_step_cursor = 0u;
    bulk_track_cursor = 0u;
}

/* Drain up to eight steps from the active FIFO barrier. */
static void patSvc_drainBulk(void)
{
    uint8_t processed = 0u;

    while (processed < PATSVC_BULK_STEPS &&
           bulk_step_cursor < NUM_STEPS) {
        if (bulk_op == PATSVC_OP_CLEAR_TRACK_BARRIER) {
            /* Preserve a trigger set after the clear request; only dynamic
             * block ownership is removed by this barrier step. */
            pat_releaseStepDynamic(service_scene, bulk_track,
                                   bulk_step_cursor);
        } else if (bulk_op == PATSVC_OP_REMOVE_TRACK_TARGET_BARRIER) {
            (void)pat_removeStepAutomation(service_scene, bulk_track,
                                            bulk_step_cursor, bulk_target);
        }
        bulk_step_cursor++;
        processed++;
    }
    if (bulk_step_cursor >= NUM_STEPS) {
        bulk_op = PATSVC_OP_NONE;
        patSvc_consumeHead();
        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
    }
}

/*
 * Drain the current queue head or start its barrier.
 *
 * Output: 1 when the head was consumed or a barrier was started, 0 when the
 * event remains blocked for reactive compaction. Capacity/fragmentation
 * classification is intentionally here so FIFO admission remains unchanged.
 */
static uint8_t patSvc_drainQueue(void)
{
    uint32_t event;
    uint8_t operation;
    uint16_t step_id;
    uint8_t track;
    uint8_t step;

    if (patSvc_queueCount() == 0u)
        return 0u;
    event = service_queue[service_queue_cons & PATSVC_QUEUE_MASK];
    operation = patSvc_eventOperation(event);
    if (operation == PATSVC_OP_CLEAR_TRACK_BARRIER ||
        operation == PATSVC_OP_REMOVE_TRACK_TARGET_BARRIER) {
        patSvc_beginBulk(event);
        return 1u;
    }

    step_id = patSvc_eventStepId(event);
    track = (uint8_t)(step_id / NUM_STEPS);
    step = (uint8_t)(step_id % NUM_STEPS);
    if (patSvc_executeEvent(event)) {
        patSvc_consumeHead();
        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
        return 1u;
    }

    {
        uint8_t required = patSvc_requiredChunks(
            service_scene, operation, track, step,
            patSvc_eventPayload(event));
        pat_scene_region_t *region = patSvc_region(service_scene);
        uint16_t used = patSvc_countUsed(region);
        uint16_t free_chunks = (uint16_t)(PATSVC_POOL_CHUNKS - used);
        uint16_t largest = patSvc_largestFreeRun(region);

        if (required == 0u || free_chunks < required) {
            patternTrace_record(PAT_TRACE_STAGE_CAPACITY_DROP,
                                (uint8_t)(service_scene & 0x0Fu), event);
            patSvc_consumeHead();
            return 1u;
        }
        if (largest < required) {
            reactive_active = 1u;
            reactive_required = required;
            reactive_scan_cursor = 0u;
            return 0u;
        }
        patternTrace_record(PAT_TRACE_STAGE_FRAG_DROP,
                            (uint8_t)(service_scene & 0x0Fu), event);
        patSvc_consumeHead();
    }
    return 1u;
}

/*
 * Admit one foreground operation, choosing synchronous execution while idle.
 *
 * What: enforce the single mutation target and route busy work to the FIFO.
 * Inputs: validated Scene/track/step and packed event. Output: direct result,
 * optimistic queue acceptance, or rejection. Affiliate: public patSvc_* API.
 */
static uint8_t patSvc_submit(uint8_t scene, uint32_t event,
                             uint8_t direct_allowed)
{
    if (!service_open || service_handover || scene != service_scene) {
        patSvc_rejectScene(scene, event);
        return 0u;
    }
    if (direct_allowed && patSvc_queueCount() == 0u && bulk_op == PATSVC_OP_NONE &&
        seq_activePattern == service_scene) {
        uint8_t result = patSvc_executeEvent(event);

        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
        return result;
    }
    return patSvc_enqueue(event);
}

/* Validate common coordinates and keep malformed events out of the queue. */
static uint8_t patSvc_validStepRequest(uint8_t scene, uint8_t track,
                                       uint8_t step, uint16_t target,
                                       uint8_t needs_target)
{
    if (!scene_indexValid(scene) || !pat_trackValid(track) ||
        !pat_stepValid(step) || (needs_target && target > 0x01FFu)) {
        patSvc_rejectScene(scene, 0u);
        return 0u;
    }
    return 1u;
}

/*
 * Initialize every service-owned byte/cursor after boot Pattern loading.
 *
 * Inputs: seq_activePattern and all resident Pattern regions. Outputs: an open
 * service with an empty FIFO, a reconciled logical occupancy count, and fresh
 * Tier 1/Tier 2 cursors. Affiliate: main.c's pre-audio boot sequence.
 */
void patSvc_init(void)
{
    memset((void *)service_queue, 0, sizeof(service_queue));
    service_queue_prod = 0u;
    service_queue_cons = 0u;
    service_scene = seq_activePattern;
    service_open = 1u;
    service_handover = 0u;
    service_replace_pending = 0u;
    bulk_op = PATSVC_OP_NONE;
    bulk_track = 0u;
    bulk_target = 0u;
    bulk_step_cursor = 0u;
    bulk_track_cursor = 0u;
    tier1_scan_cursor = 0u;
    tier2_scan_cursor = 0u;
    reactive_scan_cursor = 0u;
    reactive_required = 0u;
    reactive_active = 0u;
    logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
    last_compact_tick = time_sysTick;
}

/*
 * Close service admission while a filesystem replacement waits for quiescence.
 *
 * What: retain the current service target, close new edits, and let
 * patSvc_tick() drain queued work before direct filesystem writes begin. Why:
 * a loader writes the resident address/bitmap/pool image in place and cannot
 * overlap a relocation or raw PatternData mutation. Inputs: one target Scene;
 * output: one readiness result. Non-service Scenes need no gate. Affiliate:
 * filesystem_patternServiceReady().
 */
uint8_t patSvc_prepareSceneReplace(uint8_t scene)
{
    if (!scene_indexValid(scene))
        return 0u;
    if (scene != service_scene)
        return 1u;
    if (!service_replace_pending) {
        service_replace_pending = 1u;
        service_open = 0u;
        service_handover = 1u;
    }
    return (uint8_t)(patSvc_queueCount() == 0u &&
                     bulk_op == PATSVC_OP_NONE && !reactive_active);
}

/*
 * Publish a filesystem-replaced Pattern image as the new service baseline.
 *
 * What: recount live chunks, reset relocation cursors, and reopen admission
 * after all direct resident writes have stopped. Why: the service must never
 * maintain a bitmap or address layout from before the replacement. Inputs: the
 * target Scene whose replacement was prepared; output: ready service state or
 * a normal active-Scene handover if playback changed during I/O. Affiliate:
 * filesystem.c Pattern load completion/error paths.
 */
void patSvc_finishSceneReplace(uint8_t scene)
{
    if (!service_replace_pending || scene != service_scene)
        return;
    logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
    tier1_scan_cursor = 0u;
    tier2_scan_cursor = 0u;
    reactive_scan_cursor = 0u;
    reactive_required = 0u;
    reactive_active = 0u;
    service_replace_pending = 0u;
    last_compact_tick = time_sysTick;
    if (seq_activePattern == service_scene) {
        service_handover = 0u;
        service_open = 1u;
    } else {
        service_handover = 1u;
        service_open = 0u;
    }
}

/*
 * Report whether queued Pattern mutations and target handover are quiescent.
 *
 * Output: nonzero only for the currently playing service Scene when no FIFO,
 * bulk, reactive-compaction, or handover work remains. Affiliate:
 * filesystem.c's Pattern AutoSave scheduler.
 */
uint8_t patSvc_idle(void)
{
    return (uint8_t)(service_open && !service_handover &&
                     seq_activePattern == service_scene &&
                     patSvc_queueCount() == 0u &&
                     bulk_op == PATSVC_OP_NONE && !reactive_active);
}

/* Submit one automation add/update with target/value packed into sixteen bits. */
uint8_t patSvc_writeStepAutomation(uint8_t scene, uint8_t track,
                                   uint8_t step, uint16_t target,
                                   uint8_t value)
{
    uint16_t payload;

    if (!patSvc_validStepRequest(scene, track, step, target, 1u))
        return 0u;
    payload = (uint16_t)((target & 0x01FFu) |
                        ((uint16_t)(value & 0x7Fu) << 9u));
    return patSvc_submit(scene,
                         patSvc_packEvent(PATSVC_OP_WRITE_AUTOMATION,
                                          track, step, payload),
                         1u);
}

/* Submit one automation removal through the same FIFO/direct boundary. */
uint8_t patSvc_removeStepAutomation(uint8_t scene, uint8_t track,
                                    uint8_t step, uint16_t target)
{
    if (!patSvc_validStepRequest(scene, track, step, target, 1u))
        return 0u;
    return patSvc_submit(scene,
                         patSvc_packEvent(PATSVC_OP_REMOVE_AUTOMATION,
                                          track, step,
                                          (uint16_t)(target & 0x01FFu)),
                         1u);
}

/* Submit a note-special edit; the raw setter retains all other specials. */
uint8_t patSvc_setStepNote(uint8_t scene, uint8_t track, uint8_t step,
                           uint8_t value)
{
    if (!patSvc_validStepRequest(scene, track, step, 0u, 0u))
        return 0u;
    return patSvc_submit(scene,
                         patSvc_packEvent(PATSVC_OP_SET_NOTE, track, step,
                                          value), 1u);
}

/* Submit a velocity-special edit through the Pattern stack service. */
uint8_t patSvc_setStepVolume(uint8_t scene, uint8_t track, uint8_t step,
                             uint8_t value)
{
    if (!patSvc_validStepRequest(scene, track, step, 0u, 0u))
        return 0u;
    return patSvc_submit(scene,
                         patSvc_packEvent(PATSVC_OP_SET_VOLUME, track, step,
                                          value), 1u);
}

/* Submit a probability-special edit through the Pattern stack service. */
uint8_t patSvc_setStepProbability(uint8_t scene, uint8_t track,
                                  uint8_t step, uint8_t value)
{
    if (!patSvc_validStepRequest(scene, track, step, 0u, 0u))
        return 0u;
    return patSvc_submit(scene,
                         patSvc_packEvent(PATSVC_OP_SET_PROBABILITY, track,
                                          step, value), 1u);
}

/* Route a foreground destructive erase, queueing it when service work exists. */
void patSvc_eraseStep(uint8_t scene, uint8_t track, uint8_t step)
{
    uint32_t event;

    if (!patSvc_validStepRequest(scene, track, step, 0u, 0u))
        return;
    event = patSvc_packEvent(PATSVC_OP_DELETE_DYNAMIC, track, step, 1u);
    (void)patSvc_submit(scene, event, 1u);
}

/* Admit the track barrier, then clear triggers immediately on success. */
void patSvc_clearTrack(uint8_t scene, uint8_t track)
{
    uint8_t step;
    uint32_t event;

    if (!scene_indexValid(scene) || !pat_trackValid(track)) {
        patSvc_rejectScene(scene, 0u);
        return;
    }
    if (!service_open || service_handover || scene != service_scene) {
        patSvc_rejectScene(scene, 0u);
        return;
    }
    event = patSvc_packEvent(PATSVC_OP_CLEAR_TRACK_BARRIER,
                             track, 0u, track);
    if (patSvc_queueCount() == 0u && bulk_op == PATSVC_OP_NONE &&
        seq_activePattern == service_scene) {
        for (step = 0u; step < NUM_STEPS; step++)
            pat_setStepActive(scene, track, step, 0u);
        pat_clearTrack(scene, track);
        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
    } else if (patSvc_enqueue(event)) {
        /* Publish the barrier first; a full FIFO must leave triggers intact. */
        for (step = 0u; step < NUM_STEPS; step++)
            pat_setStepActive(scene, track, step, 0u);
    }
}

/* Admit the full reset before clearing its static triggers. */
void patSvc_clearPattern(uint8_t scene)
{
    uint8_t track;
    uint8_t step;
    uint32_t event;

    if (!scene_indexValid(scene) || !service_open || service_handover ||
        scene != service_scene) {
        patSvc_rejectScene(scene, 0u);
        return;
    }
    if (patSvc_queueCount() == 0u && bulk_op == PATSVC_OP_NONE &&
        seq_activePattern == service_scene) {
        pat_clearPattern(scene);
        logical_chunks_used = 0u;
        tier1_scan_cursor = 0u;
        return;
    }
    event = patSvc_packEvent(PATSVC_OP_CLEAR_PATTERN, 0u, 0u, 0u);
    if (patSvc_enqueue(event)) {
        /* Do not clear static triggers unless the full reset is admitted. */
        for (track = 0u; track < NUM_TRACKS; track++)
            for (step = 0u; step < NUM_STEPS; step++)
                pat_setStepActive(scene, track, step, 0u);
    }
}

/* Queue or synchronously scan all 128 steps for one target. */
uint8_t patSvc_removeTrackAutomationByTarget(uint8_t scene, uint8_t track,
                                             uint16_t target)
{
    if (!scene_indexValid(scene) || !pat_trackValid(track) || target > 0x01FFu) {
        patSvc_rejectScene(scene, 0u);
        return 0u;
    }
    if (!service_open || service_handover || scene != service_scene) {
        patSvc_rejectScene(scene, 0u);
        return 0u;
    }
    if (patSvc_queueCount() == 0u && bulk_op == PATSVC_OP_NONE &&
        seq_activePattern == service_scene) {
        uint8_t removed = pat_removeTrackAutomationByTarget(scene, track,
                                                              target);
        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
        return removed;
    }
    return patSvc_enqueue(
        patSvc_packEvent(PATSVC_OP_REMOVE_TRACK_TARGET_BARRIER,
                         track, 0u, (uint16_t)(target & 0x01FFu)));
}

/*
 * Publish a live erase request from TIM3 without touching pool state.
 *
 * Inputs: ISR-observed Scene/track/step. Output: one DELETE_DYNAMIC event or
 * a traced rejection/overflow. The caller clears the trigger bit separately
 * because that static halfword operation is safe in TIM3 context.
 */
void patSvc_enqueueErase(uint8_t scene, uint8_t track, uint8_t step)
{
    if (!patSvc_validStepRequest(scene, track, step, 0u, 0u))
        return;
    if (!service_open || service_handover || scene != service_scene) {
        patSvc_rejectScene(scene,
                           patSvc_packEvent(PATSVC_OP_DELETE_DYNAMIC,
                                            track, step, 0u));
        return;
    }
    (void)patSvc_enqueue(patSvc_packEvent(PATSVC_OP_DELETE_DYNAMIC,
                                          track, step, 0u));
}

/*
 * Advance one bounded service pass.
 *
 * Priority 0 closes/drains target handover; priority 1 drains one FIFO event
 * or eight barrier steps; priority 2 performs one Tier 1 relocation; priority
 * 3 performs paced Tier 2 scanning. Playback never waits for this function.
 */
void patSvc_tick(void)
{
    if (!service_open && !service_handover)
        return;

    if (!service_handover && seq_activePattern != service_scene) {
        service_open = 0u;
        service_handover = 1u;
        reactive_active = 0u;
    }

    if (service_handover) {
        if (reactive_active) {
            if (patSvc_reactiveStep(service_scene, reactive_required))
                return;
            if (reactive_scan_cursor < PATSVC_ADDRESS_COUNT)
                return;
            patternTrace_record(PAT_TRACE_STAGE_FRAG_DROP,
                                (uint8_t)(service_scene & 0x0Fu),
                                reactive_required);
            reactive_active = 0u;
            reactive_scan_cursor = 0u;
            if (patSvc_queueCount() != 0u)
                patSvc_consumeHead();
            return;
        }
        if (bulk_op != PATSVC_OP_NONE) {
            patSvc_drainBulk();
            return;
        }
        if (patSvc_queueCount() != 0u) {
            (void)patSvc_drainQueue();
            return;
        }
        if (service_replace_pending)
            return;
        service_scene = seq_activePattern;
        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
        tier1_scan_cursor = 0u;
        tier2_scan_cursor = 0u;
        reactive_scan_cursor = 0u;
        reactive_required = 0u;
        service_handover = 0u;
        service_open = 1u;
        last_compact_tick = time_sysTick;
        return;
    }

    if (reactive_active) {
        if (patSvc_reactiveStep(service_scene, reactive_required))
            return;
        if (reactive_scan_cursor < PATSVC_ADDRESS_COUNT)
            return;
        patternTrace_record(PAT_TRACE_STAGE_FRAG_DROP,
                            (uint8_t)(service_scene & 0x0Fu),
                            reactive_required);
        reactive_active = 0u;
        reactive_scan_cursor = 0u;
        if (patSvc_queueCount() != 0u)
            patSvc_consumeHead();
        return;
    }

    if (bulk_op != PATSVC_OP_NONE) {
        patSvc_drainBulk();
        return;
    }
    if (patSvc_queueCount() != 0u) {
        (void)patSvc_drainQueue();
        return;
    }

    logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
    if (tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
        uint8_t fallback_used = 0u;
        uint16_t address_index = tier1_scan_cursor++;

        uint16_t old_offset = 0u;
        uint16_t new_offset = 0u;

        if (patSvc_tier1Step(service_scene, address_index, &fallback_used,
                             &old_offset, &new_offset)) {
            patternTrace_record(fallback_used ?
                                PAT_TRACE_STAGE_GAP_FALLBACK :
                                PAT_TRACE_STAGE_TIER1_GAP,
                                (uint8_t)(service_scene & 0x0Fu),
                                patSvc_relocationValue(address_index,
                                                       old_offset,
                                                       new_offset));
        }
        return;
    }

    if ((uint16_t)(time_sysTick - last_compact_tick) >=
        PAT_COMPACT_INTERVAL_MS) {
        uint8_t moved = 0u;
        uint8_t inspected = 0u;

        while (inspected < PAT_COMPACT_SCAN_PER_TICK &&
               tier2_scan_cursor < PATSVC_ADDRESS_COUNT) {
            uint16_t address_index = tier2_scan_cursor++;
            pat_scene_region_t *region = patSvc_region(service_scene);
            uint8_t track = (uint8_t)(address_index / NUM_STEPS);
            uint8_t step = (uint8_t)(address_index % NUM_STEPS);
            uint16_t old_offset;
            uint16_t new_offset;
            uint8_t chunks;
            uint16_t addr;

            inspected++;
            if (!region)
                continue;
            addr = region->address[track][step];
            if ((addr & PAT_ADDR_SPECIALS_BIT) == 0u)
                continue;
            chunks = patSvc_blockChunksAt(
                region, (uint16_t)(addr & PAT_ADDR_OFFSET_MASK),
                address_index);
            if (chunks == 0u)
                continue;
            if (patSvc_relocateIndex(service_scene, address_index, 0u, 1u,
                                     &old_offset, &new_offset)) {
                patternTrace_record(PAT_TRACE_STAGE_TIER2_RELOC,
                                    (uint8_t)(service_scene & 0x0Fu),
                                    patSvc_relocationValue(address_index,
                                                            old_offset,
                                                            new_offset));
                moved = 1u;
                break;
            }
        }
        if (moved || tier2_scan_cursor >= PATSVC_ADDRESS_COUNT) {
            tier2_scan_cursor = 0u;
            last_compact_tick = time_sysTick;
            tier1_scan_cursor = 0u;
        }
    } else {
        /* Start a fresh Tier 1 sweep after the completed compaction window. */
        tier1_scan_cursor = 0u;
    }
}
