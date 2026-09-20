# S069 — Slack reservation image and reactive compaction: Implementation schedule

## Authority

Implements `S069_SLACK_REACTIVE_COMPACTION_CLAUDE.md` (all ten follow-ups
resolved). The schedule below specifies the source changes by file, line, and
add/remove/modify; the implementation notes at the top record the landed
changes, reconciliations, and verification.

## Implementation notes — 2026-09-20

Source implementation and build verification are complete against the clean
Session-069 non-semantic baseline. Hardware verification remains pending. The
source layout matches the schedule, with current line numbers shifted by the
existing Session-069 comments and the implementation comments below.

- The reservation image remains service-owned SRAM1 state: 512 bytes for the
  bit image plus three one-byte policy flags.
- The final repair epoch does not use `patSvc_trailingGap()` or
  `patSvc_gapTarget()`; both old Tier-1 gap helpers will be removed so the
  build has no dead static functions. The old gap threshold and free-run
  threshold macros will likewise be removed as dead policy after the reactive
  path is updated.
- Because reservations are positional rather than tagged with an owner, the
  ordinary PatternData block-free path and service relocation path will clear
  the freed block's former trailing reservation. This preserves the invariant
  across erase, replacement, shrink, and relocation mutations without adding
  RAM.
- Clean-build and image-generation results are recorded below; hardware
  observations should be appended here when the fixture is run.

## Implementation notes — source/build pass

The source implementation now covers Steps 0–14, with two invariant-preserving
details made explicit in code:

- `pat_poolAlloc()` and service free-run classifiers refuse reserved chunks;
  Gate-6 append consumes its adjacent reservation after setting occupancy.
- The ordinary PatternData free path and both service relocation paths clear a
  former positional trailing reservation, preventing stale claims after erase,
  replacement, shrink, or move.
- The old periodic Tier-2 cursor/timestamp and Tier-1 gap helpers are gone.
  Direct idle-path allocation failures now use the same capacity/fragmentation
  classification as queued failures and emit the new `D` retention witness.
- `PATTERN_DYNAMIC_STACK.md` §12 and `SRAM_MANIFEST.md` now describe the
  non-persisted image, density hysteresis, adaptive budget, lifecycle rebuild,
  and trace stages. The source allocation is 515 bytes; the old four bytes of
  periodic state are removed.

Schedule reconciliation: Step 4C's reservation-aware `patSvc_trailingGap()`
edit is superseded by removal of that helper along with the old Tier-1 gap
algorithm; no final repair path calls it. `PAT_GAP_REDUCE_THRESHOLD` and
`PAT_COMPACT_FREE_RUN_THRESHOLD` are also removed as dead policy, while
`PAT_COMPACT_SCAN_PER_TICK` remains the reactive recovery bound. The revised
Step 5E rule is used: Gate-6 accepts free trailing chunks positionally and
consumes any reservation only after occupancy is set.

The clean ARM build passes. Current link result:
`text=449,404`, `data=408`, `bss=291,708`. `nm` shows a 512-byte
`reservation_image` plus three one-byte policy/lifecycle flags; no references
remain in source/config for `PAT_COMPACT_INTERVAL_MS`, `tier2_scan_cursor`,
`last_compact_tick`, `patSvc_tier1Step`, or `patSvc_gapTarget`. The remaining
compiler/linker warnings are pre-existing project warnings outside this
change. `make img` generated `build/LXRV2_lxr02.img` at 449,828 bytes.
Hardware verification of reservation traces, Gate-6 growth, density
hysteresis, adaptive budgets, and AutoSave OFF-to-ON convergence remains
pending.

Build baseline: `text=448,580`, `data=412`, `bss=291,196` (Session 069
non-semantic implementation).

## RAM budget

| Item | Bytes | Region | Owner |
|------|------:|--------|-------|
| Reservation image | 512 | SRAM1 .bss | PatternStackService.c |
| Density latch + adaptive budget flag + rebuild-pending flag | 3 | SRAM1 .bss | PatternStackService.c |
| **Total** | **515** | | |

Within the approved 512 + 20 ceiling. `SRAM_MANIFEST.md` update in the final
step.

---

## Step 0 — New `config.h` constants

**File: `config.h`**

### 0A — Add reservation-density thresholds and adaptive budget constants

**Location:** after line 270 (`PAT_COMPACT_FREE_RUN_THRESHOLD`).
**Action:** ADD the following block.

```c
/*
 * Reservation-density policy thresholds (percent of PATSVC_POOL_CHUNKS).
 *
 * What: PAT_RESERVATION_REDUCE_THRESHOLD is the pool-occupancy percentage
 * above which the repair pass stops creating new trailing-chunk reservations.
 * PAT_RESERVATION_RESTORE_THRESHOLD is the percentage below which
 * reservations are re-enabled after a reduction. The gap between the two
 * provides hysteresis, preventing oscillation when occupancy hovers near a
 * boundary. Why: at high occupancy the pool cannot afford to hold chunks out
 * of general circulation; at low occupancy one reserved trailing chunk per
 * block makes in-place growth nearly free. The repair pass reads a latched
 * density-level state variable that transitions only when occupancy crosses
 * the relevant threshold. Inputs: compile-time percentage values. Outputs:
 * PatternStackService.c density-latch transition decisions. Affiliates:
 * patSvc_updateDensityLevel(), patSvc_tick() repair epoch. A future pool
 * resize (PAT_STACK_SIZE > 256) changes PATSVC_POOL_CHUNKS; these percentages
 * remain valid because the service computes absolute chunk counts from them at
 * runtime.
 */
#define PAT_RESERVATION_REDUCE_THRESHOLD   70u
#define PAT_RESERVATION_RESTORE_THRESHOLD  50u

/*
 * Adaptive repair-tick budget bounds.
 *
 * What: PAT_REPAIR_SCAN_IDLE is the per-tick address-entry scan limit when no
 * AutoSave work is pending. PAT_REPAIR_SCAN_BUSY is the reduced limit when
 * AutoSave has pending semantic, non-semantic, or parameter dirty bits.
 * Why: yielding foreground cycles to the filesystem facade under AutoSave
 * pressure keeps file I/O responsive while still making bounded progress on
 * reservation repair. Inputs: compile-time entry counts. Outputs:
 * PatternStackService.c scan-loop bound. Affiliates: patSvc_repairBudget(),
 * autosave_maskHasDirty(), autosave_patternDirtyMask(),
 * autosave_nonSemanticPatternDirtyMask(). A future pool resize does not
 * invalidate these values; they bound address-entry inspections, not chunk
 * counts.
 */
#define PAT_REPAIR_SCAN_IDLE   16u
#define PAT_REPAIR_SCAN_BUSY    4u
```

**Description:** Two threshold pairs and two budget limits. All values are
relative to `PATSVC_POOL_CHUNKS` or are address-entry counts, so a future
`PAT_STACK_SIZE` resize needs no change here — only re-tuning if the new pool
geometry makes different thresholds desirable.

### 0B — Update the existing policy comment block

**Location:** lines 257–265 (the existing comment for
`PAT_GAP_REDUCE_THRESHOLD` etc.).
**Action:** MODIFY — rewrite the comment block to cover the full policy set
including the new constants and the planned removal of `PAT_COMPACT_INTERVAL_MS`.

```c
/*
 * Pattern stack service maintenance policy.
 *
 * What: owned trailing-slack reservation with a latchable density level that
 * scales with pool occupancy, plus reactive-only compaction triggered by a
 * blocked allocation. Why: Tier 1/Tier 2 periodic relocation is replaced by a
 * finite bounded repair epoch that sleeps when converged. Inputs: compile-time
 * service policy. Outputs: bounded PatternStackService.c repair and reactive
 * work. Affiliates: patSvc_tick(), patSvc_reactiveStep().
 *
 * PAT_GAP_REDUCE_THRESHOLD is retained for the reactive-recovery gap target
 * (patSvc_gapTarget). PAT_COMPACT_SCAN_PER_TICK bounds reactive recovery
 * per tick. PAT_COMPACT_FREE_RUN_THRESHOLD is unused after Tier 2 removal and
 * may be deleted in a cleanup pass.
 *
 * A future resize of PAT_STACK_SIZE beyond 256 changes PATSVC_POOL_CHUNKS and
 * the backed/unbacked bitmap split. All thresholds and reservation rules are
 * expressed as percentages of PATSVC_POOL_CHUNKS, so they remain valid without
 * re-architecture — only re-tuning. If this ever proves insufficient, add a
 * comment here and a note to PATTERN_DYNAMIC_STACK.md §12.
 */
```

### 0C — Remove `PAT_COMPACT_INTERVAL_MS`

**Location:** line 268 (`#define PAT_COMPACT_INTERVAL_MS   100u`).
**Action:** REMOVE. This constant gates the periodic Tier 2 sweep, which is
deleted in Step 7. All references are removed in that step.

---

## Step 1 — Reservation image and service state declarations

**File: `PatternStackService.c`**

### 1A — Add the 512-byte reservation image

**Location:** after line 109 (after `reactive_active`), before the
`patSvc_packEvent` helper.
**Action:** ADD.

```c
/*
 * Slack-reservation bitmap image.
 *
 * What: a 512-byte bit-packed image with the same chunk >> 3, chunk & 7
 * indexing as the occupancy bitmap in pat_scene_region_t.bitmap[]. A set bit
 * means the corresponding backed, currently-unoccupied chunk is reserved as
 * trailing slack for a specific block and must be refused to ordinary
 * allocation and reactive recovery searches. Why: owned trailing slack lets
 * pat_tryAppendAutomation() grow a block in place without a pool-wide search.
 * Inputs: the repair pass sets bits; append consumption and reactive recovery
 * clear bits. Invariant: a chunk is never simultaneously reserved and
 * occupied — consuming a reserved chunk clears the reservation bit in the
 * same transaction that sets the occupancy bit. The upper 256 bytes (unbacked
 * range) are never consulted; every reservation-aware helper bounds its loop
 * to PATSVC_POOL_CHUNKS, exactly like the existing allocator helpers.
 * Lifetime: firmware lifetime, describes only the current service_scene.
 * Cleared at each lifecycle boundary (init, handover-complete, filesystem
 * replacement) and lazily repopulated by the next bounded repair epoch.
 * RAM: 512 bytes, SRAM1, .bss. Affiliates: patSvc_reservationGet/Set/Clear,
 * patSvc_repairStep, pat_tryAppendAutomation (via the cross-module accessor).
 */
static uint8_t reservation_image[512u];

/*
 * Latchable reservation-density level.
 *
 * What: nonzero when pool occupancy is below PAT_RESERVATION_REDUCE_THRESHOLD
 * and the repair pass should create trailing-chunk reservations; zero when
 * occupancy exceeds that threshold and reservations are defined away. The
 * transition from 1 to 0 occurs when occupancy crosses
 * PAT_RESERVATION_REDUCE_THRESHOLD upward; the reverse transition occurs when
 * occupancy drops below PAT_RESERVATION_RESTORE_THRESHOLD. The gap between
 * the two thresholds provides hysteresis. Why: at high occupancy the pool
 * cannot afford reserved chunks; at low occupancy reservations make in-place
 * growth nearly free. Inputs: logical_chunks_used, updated after every
 * mutation and recount. Outputs: the repair pass reads this to decide whether
 * to create reservations; reactive recovery reads it to decide whether a
 * reservation is "surplus" (cheap to reclaim) or "active" (harder to
 * reclaim). RAM: 1 byte, SRAM1, .bss. Affiliates:
 * patSvc_updateDensityLevel(), patSvc_repairStep(), patSvc_reactiveStep().
 */
static uint8_t reservation_density_active;

/*
 * Adaptive repair-budget flag.
 *
 * What: nonzero when AutoSave has pending work (semantic, non-semantic, or
 * parameter dirty bits). The repair pass uses PAT_REPAIR_SCAN_BUSY when set
 * and PAT_REPAIR_SCAN_IDLE when clear. Why: yielding foreground cycles to the
 * filesystem facade during AutoSave I/O keeps file throughput high while
 * still making bounded progress on reservation repair. Inputs: sampled once
 * per patSvc_tick() from autosave_maskHasDirty(), autosave_patternDirtyMask(),
 * and autosave_nonSemanticPatternDirtyMask(). RAM: 1 byte, SRAM1, .bss.
 * Affiliates: patSvc_repairBudget().
 */
static uint8_t repair_budget_busy;

/*
 * Rebuild-pending flag.
 *
 * What: set to 1 at each lifecycle boundary (init, handover-complete,
 * filesystem replacement) after clearing the reservation image. The repair
 * pass treats a set flag identically to a normal wake — it resets the cursor
 * to 0 and begins a fresh epoch. Cleared when the first repair epoch starts.
 * Why: rebuild is not a separate algorithm; it is epoch one from a zeroed
 * image. This flag ensures the repair pass wakes even when no other wake
 * event fires. RAM: 1 byte, SRAM1, .bss. Affiliates: patSvc_init(),
 * patSvc_finishSceneReplace(), patSvc_tick() handover-complete.
 */
static uint8_t reservation_rebuild_pending;
```

**Description:** 515 bytes of new service-owned state. The image mirrors the
occupancy bitmap's geometry. The three flag bytes control the density policy,
the adaptive budget, and the rebuild lifecycle.

---

## Step 2 — Reservation bitmap helpers

**File: `PatternStackService.c`**

### 2A — Add reservation get/set/clear helpers

**Location:** after `patSvc_bitmapClear()` (line 223), before
`patSvc_blockChunksAt()`.
**Action:** ADD.

```c
/*
 * Read one reservation bit from the service-owned slack image.
 *
 * What: identical chunk >> 3, chunk & 7 indexing to patSvc_bitmapGet but
 * against reservation_image[] instead of region->bitmap[]. Why: the repair
 * pass and allocation/reactive helpers must distinguish "free and unreserved"
 * from "free but reserved as trailing slack." Inputs: chunk index 0..4095.
 * Output: 1 when the chunk is reserved, 0 otherwise. No bounds check; callers
 * must bound to PATSVC_POOL_CHUNKS. Affiliates: patSvc_repairStep(),
 * patSvc_findFreeUnreservedRun(), patSvc_reserveConsume().
 */
static uint8_t patSvc_reservationGet(uint16_t chunk)
{
    return (uint8_t)((reservation_image[chunk >> 3u] >> (chunk & 7u)) & 1u);
}

/*
 * Mark one free chunk as reserved trailing slack.
 *
 * What: set the reservation bit for a chunk the occupancy bitmap shows free.
 * Why: the repair pass creates one trailing reservation per occupied block
 * when the density level is active. Inputs: chunk index. Output: one bit set.
 * Callers must verify the chunk is free and unoccupied before calling.
 * Affiliates: patSvc_repairStep().
 */
static void patSvc_reservationSet(uint16_t chunk)
{
    reservation_image[chunk >> 3u] |= (uint8_t)(1u << (chunk & 7u));
}

/*
 * Release one reservation without changing the occupancy bitmap.
 *
 * What: clear the reservation bit for a chunk. Why: consumed reservations
 * (trailing-slack append), reclaimed reservations (reactive recovery under
 * pressure), and lifecycle clears all use this path. Inputs: chunk index.
 * Output: one bit cleared. Affiliates: patSvc_reserveConsume(),
 * patSvc_reactiveStep(), patSvc_clearReservationImage().
 */
static void patSvc_reservationClear(uint16_t chunk)
{
    reservation_image[chunk >> 3u] &= (uint8_t)~(1u << (chunk & 7u));
}
```

### 2B — Add reservation-image clear helper

**Location:** immediately after the three helpers above.
**Action:** ADD.

```c
/*
 * Clear the entire reservation image and arm a rebuild.
 *
 * What: zero all 512 bytes of reservation_image[] and set
 * reservation_rebuild_pending so the next repair epoch repopulates it. Why:
 * lifecycle boundaries (init, handover-complete, filesystem replacement) must
 * start from a clean image; rebuild is lazy — the next bounded repair epoch
 * creates reservations as it scans. Inputs: none. Output: zeroed image and a
 * pending-rebuild flag. Affiliates: patSvc_init(), patSvc_finishSceneReplace(),
 * the handover-complete branch in patSvc_tick().
 */
static void patSvc_clearReservationImage(void)
{
    memset(reservation_image, 0, sizeof(reservation_image));
    reservation_rebuild_pending = 1u;
}
```

---

## Step 3 — Density-level and adaptive-budget helpers

**File: `PatternStackService.c`**

### 3A — Add density-level update function

**Location:** after `patSvc_clearReservationImage()`, before the existing
`patSvc_gapTarget()` (line 451).
**Action:** ADD.

```c
/*
 * Transition the reservation-density latch based on current pool occupancy.
 *
 * What: compares logical_chunks_used against the two hysteresis thresholds
 * (PAT_RESERVATION_REDUCE_THRESHOLD and PAT_RESERVATION_RESTORE_THRESHOLD,
 * both expressed as percentages of PATSVC_POOL_CHUNKS). When occupancy rises
 * above the reduce threshold, density is latched off (0); when it drops below
 * the restore threshold, density is latched on (1). Between the two
 * thresholds, the current state is retained. Why: hysteresis prevents
 * oscillation when occupancy hovers near a boundary. Inputs:
 * logical_chunks_used. Output: reservation_density_active may change. A
 * transition from 0 to 1 is a wake event that resets tier1_scan_cursor to 0
 * so the repair pass creates reservations on its next epoch. Affiliates:
 * patSvc_tick() after every logical_chunks_used update.
 */
static void patSvc_updateDensityLevel(void)
{
    uint32_t percent = ((uint32_t)logical_chunks_used * 100u) /
                       PATSVC_POOL_CHUNKS;

    if (reservation_density_active) {
        if (percent >= PAT_RESERVATION_REDUCE_THRESHOLD)
            reservation_density_active = 0u;
    } else {
        if (percent < PAT_RESERVATION_RESTORE_THRESHOLD) {
            reservation_density_active = 1u;
            /* Wake the repair pass to create reservations. */
            if (tier1_scan_cursor >= PATSVC_ADDRESS_COUNT)
                tier1_scan_cursor = 0u;
        }
    }
}
```

### 3B — Add adaptive-budget sampler

**Location:** immediately after `patSvc_updateDensityLevel()`.
**Action:** ADD.

```c
/*
 * Sample AutoSave pressure and set the repair budget flag.
 *
 * What: reads the existing scalar, semantic-Pattern, and non-semantic-Pattern
 * dirty predicates and sets repair_budget_busy to nonzero when any work is
 * pending. Why: the repair pass yields per-tick scan budget to the filesystem
 * facade when AutoSave I/O is competing for foreground cycles. Inputs:
 * autosave_maskHasDirty(), autosave_patternDirtyMask(),
 * autosave_nonSemanticPatternDirtyMask(). Output: repair_budget_busy is 0
 * or 1. Affiliates: patSvc_tick(), called once per tick before the repair
 * pass.
 */
static void patSvc_sampleRepairBudget(void)
{
    repair_budget_busy = (uint8_t)(
        autosave_maskHasDirty() ||
        autosave_patternDirtyMask() != 0u ||
        autosave_nonSemanticPatternDirtyMask() != 0u);
}

/*
 * Return the current per-tick repair scan limit.
 *
 * What: PAT_REPAIR_SCAN_BUSY when AutoSave is under pressure,
 * PAT_REPAIR_SCAN_IDLE otherwise. Why: one conditional selects the budget;
 * every repair and reactive loop reads this instead of the raw config
 * constant. Inputs: repair_budget_busy. Output: a bounded entry count.
 * Affiliates: patSvc_tick() repair-epoch loop.
 */
static uint8_t patSvc_repairBudget(void)
{
    return repair_budget_busy ? PAT_REPAIR_SCAN_BUSY : PAT_REPAIR_SCAN_IDLE;
}
```

---

## Step 4 — Reservation-aware allocation helpers

**File: `PatternStackService.c`**

### 4A — Make `patSvc_findFreeRun()` reservation-aware

**Location:** lines 322–344 (the existing `patSvc_findFreeRun` function).
**Action:** MODIFY — add a reservation check inside the inner loop so that
reserved chunks are treated as occupied for allocation purposes.

Change the inner-loop condition at line 337 from:

```c
            if (patSvc_bitmapGet(region, (uint16_t)(start + i)))
                break;
```

to:

```c
            if (patSvc_bitmapGet(region, (uint16_t)(start + i)) ||
                patSvc_reservationGet((uint16_t)(start + i)))
                break;
```

**Description:** A reserved-but-unoccupied chunk is now refused by the
allocation search. The occupancy bitmap remains the authority for what is
physically free; the reservation check adds the policy constraint that a
reserved chunk is unavailable to general allocation. When the reservation
image is empty (post-rebuild), no reservation bits are set and this check is
always false — allocation falls back to "wherever there is space" as
intended. Affiliates: `pat_poolAlloc()` in `PatternData.c` also needs the
corresponding change (Step 5).

### 4B — Make `patSvc_largestFreeRun()` reservation-aware

**Location:** lines 302–319 (the existing `patSvc_largestFreeRun` function).
**Action:** MODIFY — add the same reservation check.

Change the condition at line 311 from:

```c
        if (!patSvc_bitmapGet(region, chunk)) {
```

to:

```c
        if (!patSvc_bitmapGet(region, chunk) &&
            !patSvc_reservationGet(chunk)) {
```

**Description:** The fragmentation classifier now correctly reports the
largest contiguous run of *unreserved* free chunks. A reserved chunk breaks
the free run, so the classifier treats a pool with many reserved-but-free
chunks as more fragmented than one with the same total free space and no
reservations. This ensures the capacity-vs-fragmentation classification in
`patSvc_drainQueue()` (line 755) accurately reflects available space.

### 4C — Make `patSvc_trailingGap()` reservation-aware

**Location:** lines 460–473 (the existing `patSvc_trailingGap` function).
**Action:** MODIFY — add a reservation check so that a reserved chunk is not
counted as a "free gap" for someone else's block.

Change line 466 from:

```c
    while (chunk < PATSVC_POOL_CHUNKS && !patSvc_bitmapGet(region, chunk)) {
```

to:

```c
    while (chunk < PATSVC_POOL_CHUNKS &&
           !patSvc_bitmapGet(region, chunk) &&
           !patSvc_reservationGet(chunk)) {
```

**Description:** Trailing-gap counting now treats a reserved neighbor as
occupied. The repair pass will not relocate a block to create a gap that
overlaps another block's reservation. The one exception: a block's *own*
reserved trailing chunk is found by the repair pass using direct reservation
lookup, not by this gap-counting helper.

---

## Step 5 — Cross-module reservation accessor and PatternData.c changes

### 5A — Declare the cross-module accessor in PatternStackService.h

**File: `PatternStackService.h`**

**Location:** after line 104 (`patSvc_enqueueErase` declaration), before the
`#endif`.
**Action:** ADD.

```c
/*
 * Query and consume one chunk's reservation for the Gate-6 growth path.
 *
 * What: patSvc_isChunkReserved() returns nonzero if the chunk is reserved in
 * the service-owned reservation image. patSvc_consumeReservation() clears the
 * reservation bit — the caller must set the occupancy bit in the same
 * transaction so the chunk is never simultaneously reserved and occupied.
 * Why: pat_tryAppendAutomation() in PatternData.c needs to consult and
 * consume a reservation owned by PatternStackService.c without reaching into
 * its statics. Inputs: a chunk index bounded to PATSVC_POOL_CHUNKS by the
 * caller. Outputs: nonzero/zero for query; cleared reservation bit for
 * consume. Affiliates: PatternData.c pat_tryAppendAutomation().
 */
uint8_t patSvc_isChunkReserved(uint16_t chunk);
void patSvc_consumeReservation(uint16_t chunk);
```

### 5B — Implement the cross-module accessor in PatternStackService.c

**File: `PatternStackService.c`**

**Location:** after the `patSvc_clearReservationImage()` helper (added in
Step 2B), before the density-level helpers (added in Step 3A).
**Action:** ADD.

```c
/*
 * Public reservation query for the Gate-6 growth path.
 *
 * What: reads one bit from the service-owned reservation image. Why:
 * PatternData.c's pat_tryAppendAutomation() must consult this image without
 * direct access to service statics. Inputs: chunk index 0..PATSVC_POOL_CHUNKS-1.
 * Output: nonzero when reserved. Affiliates: patSvc_consumeReservation().
 */
uint8_t patSvc_isChunkReserved(uint16_t chunk)
{
    if (chunk >= PATSVC_POOL_CHUNKS)
        return 0u;
    return patSvc_reservationGet(chunk);
}

/*
 * Public reservation consume for the Gate-6 growth path.
 *
 * What: clears one reservation bit. Why: the caller (pat_tryAppendAutomation)
 * must clear the reservation in the same transaction it sets the occupancy
 * bit, enforcing the invariant that a chunk is never simultaneously reserved
 * and occupied. Inputs: chunk index. Output: reservation bit cleared.
 * Affiliates: patSvc_isChunkReserved().
 */
void patSvc_consumeReservation(uint16_t chunk)
{
    if (chunk < PATSVC_POOL_CHUNKS)
        patSvc_reservationClear(chunk);
}
```

### 5C — Make `pat_poolAlloc()` reservation-aware

**File: `PatternData.c`**

**Location:** line 237, inside `pat_poolAlloc()`.
**Action:** MODIFY — add a reservation check alongside the occupancy check.

Change line 237 from:

```c
            if (pat_bitmapGet(r, i)) {
```

to:

```c
            if (pat_bitmapGet(r, i) ||
                patSvc_isChunkReserved(i)) {
```

**Description:** The first-fit allocator in `PatternData.c` now refuses
reserved chunks. When the reservation image is empty (post-rebuild, or when
density is off), `patSvc_isChunkReserved()` always returns 0 and the
allocator behaves exactly as before — "wherever there is space."

This requires adding `#include "PatternStackService.h"` to `PatternData.c`
(see 5D).

### 5D — Add include for PatternStackService.h

**File: `PatternData.c`**

**Location:** after line 19 (`#include "menu.h"`), before `#include <string.h>`.
**Action:** ADD.

```c
#include "PatternStackService.h"
```

**Description:** Needed for `patSvc_isChunkReserved()` and
`patSvc_consumeReservation()` used in `pat_poolAlloc()` and
`pat_tryAppendAutomation()`.

### 5E — Make `pat_tryAppendAutomation()` reservation-aware

**File: `PatternData.c`**

**Location:** lines 496–499, the inner loop that checks free trailing chunks.
**Action:** MODIFY — try the block's own reserved trailing chunk first; fall
back to "free and unreserved."

Replace lines 496–499:

```c
    for (i = old_chunks; i < new_chunks; i++) {
        if (pat_bitmapGet(r, (uint16_t)((old_offset >> 2u) + i)))
            return 0u;
    }
```

with:

```c
    for (i = old_chunks; i < new_chunks; i++) {
        uint16_t chunk = (uint16_t)((old_offset >> 2u) + i);

        if (pat_bitmapGet(r, chunk))
            return 0u;
        /* Accept a reserved chunk only if it is this block's own trailing
         * reservation; refuse a chunk reserved for a different block. The
         * occupancy bitmap is always authoritative — a free-and-unreserved
         * chunk is accepted even when the reservation image is incomplete. */
    }
```

And after line 515 where the new chunks are marked occupied:

```c
    for (i = old_chunks; i < new_chunks; i++)
        pat_bitmapSet(r, (uint16_t)((old_offset >> 2u) + i));
```

add immediately after:

```c
    /* Consume reservations for any trailing chunks that were reserved. The
     * invariant: a chunk is never simultaneously reserved and occupied. */
    for (i = old_chunks; i < new_chunks; i++)
        patSvc_consumeReservation((uint16_t)((old_offset >> 2u) + i));
```

**Description:** The Gate-6 growth path now (1) accepts a reserved trailing
chunk as available for *this* block's growth (it was reserved for exactly
this purpose) while refusing chunks reserved for other blocks, and (2) clears
the reservation bit immediately after setting the occupancy bit. When the
reservation image is empty (post-rebuild), both `patSvc_isChunkReserved` and
`patSvc_consumeReservation` are no-ops and the behavior matches today's code
exactly. This is the key "owned trailing slack" mechanism.

**Implementation note on "this block's own" reservation:** the reservation
image does not record *which* block owns a reservation — it is positional.
A reservation on chunk N is implicitly the trailing slack of whatever occupied
block ends at chunk N-1. The `pat_tryAppendAutomation()` function already
knows the block's extent (old_offset, old_chunks), so "this block's trailing
reservation" is exactly `reservation_image[old_chunk + old_chunks]`. If that
chunk is reserved, it was reserved *for this block* by the repair pass. A
reserved chunk that is not immediately trailing the requesting block is never
reached by this loop (the loop starts at `old_chunks` and counts up
contiguously), so no ownership confusion can occur.

Therefore the free-trailing check becomes: accept the chunk if it is free in
the occupancy bitmap, regardless of reservation status, because any reserved
chunk at this position was placed for this block by the repair pass. The
`patSvc_isChunkReserved` call in `pat_poolAlloc` (5C) correctly refuses
reserved chunks for *new* allocations (different blocks at different
positions), while this gate-6 path correctly accepts them for in-place
growth.

**Revised 5E change** — the actual code for the free-trailing check loop
should be:

Replace lines 496–499:

```c
    for (i = old_chunks; i < new_chunks; i++) {
        if (pat_bitmapGet(r, (uint16_t)((old_offset >> 2u) + i)))
            return 0u;
    }
```

with:

```c
    /* Accept trailing chunks that are free in the occupancy bitmap. A
     * reservation on an immediately-trailing chunk was placed by the repair
     * pass for this block specifically (positional ownership), so reserved-
     * but-free trailing chunks are accepted here. pat_poolAlloc refuses
     * reserved chunks for new allocations at different positions. */
    for (i = old_chunks; i < new_chunks; i++) {
        if (pat_bitmapGet(r, (uint16_t)((old_offset >> 2u) + i)))
            return 0u;
    }
```

And after line 515:

```c
    for (i = old_chunks; i < new_chunks; i++)
        pat_bitmapSet(r, (uint16_t)((old_offset >> 2u) + i));
```

add:

```c
    for (i = old_chunks; i < new_chunks; i++)
        patSvc_consumeReservation((uint16_t)((old_offset >> 2u) + i));
```

The free-trailing check itself does NOT reject reserved chunks — the
reservation was placed for this block. The `patSvc_consumeReservation` call
clears the reservation bit after the occupancy bit is set, enforcing the
invariant.

---

## Step 6 — Replace Tier 1 with finite bounded repair

**File: `PatternStackService.c`**

### 6A — Add the repair-step function

**Location:** after `patSvc_trailingGap()` (currently line 473), replacing
`patSvc_tier1Step()` (lines 482–528).
**Action:** REMOVE `patSvc_tier1Step()` entirely, ADD `patSvc_repairStep()`.

```c
/*
 * Inspect one address entry and create or verify its trailing reservation.
 *
 * What: for an occupied block, check whether its immediately-following chunk
 * is free and unreserved; if so, reserve it. If the trailing chunk is
 * occupied or already reserved for a different block, attempt to relocate
 * this block to a position where a trailing reservation can be created. When
 * reservation_density_active is 0, no reservations are created — the pass
 * still advances the cursor but performs no reservation work, so a later
 * density-restore transition finds the cursor at its natural sleep position.
 * Why: this replaces the old Tier 1 gap-creation mechanism. Unlike Tier 1,
 * no "gap" is left free — instead, a reservation bit is set, preventing any
 * other allocator from claiming that chunk. The repair pass converges in one
 * bounded epoch and sleeps. Inputs: scene, address index. Output: 1 when a
 * reservation was created or a relocation+reservation occurred, 0 when no
 * work was needed or possible. Affiliates: patSvc_tick() repair epoch.
 */
static uint8_t patSvc_repairStep(uint8_t scene, uint16_t address_index,
                                 uint16_t *old_offset_out,
                                 uint16_t *new_offset_out)
{
    pat_scene_region_t *region = patSvc_region(scene);
    uint8_t track;
    uint8_t step;
    uint16_t addr;
    uint16_t offset;
    uint8_t logical_chunks;
    uint16_t trailing_chunk;

    if (old_offset_out)
        *old_offset_out = 0u;
    if (new_offset_out)
        *new_offset_out = 0u;
    if (!region || address_index >= PATSVC_ADDRESS_COUNT)
        return 0u;
    if (!reservation_density_active)
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

    trailing_chunk = (uint16_t)((offset >> 2u) + logical_chunks);
    if (trailing_chunk >= PATSVC_POOL_CHUNKS)
        return 0u;

    /* Case 1: trailing chunk is free and unreserved — reserve in place. */
    if (!patSvc_bitmapGet(region, trailing_chunk) &&
        !patSvc_reservationGet(trailing_chunk)) {
        patSvc_reservationSet(trailing_chunk);
        return 1u;
    }

    /* Case 2: trailing chunk is already reserved (for this block) — done. */
    if (!patSvc_bitmapGet(region, trailing_chunk) &&
        patSvc_reservationGet(trailing_chunk))
        return 0u;

    /* Case 3: trailing chunk is occupied or at pool end — try to relocate
     * this block to a position where a trailing reservation can be created.
     * Search for a free run of logical_chunks + 1. */
    {
        uint16_t old_chunk = (uint16_t)(offset >> 2u);
        uint16_t required = (uint16_t)(logical_chunks + 1u);
        uint16_t new_offset;
        uint16_t new_trailing;
        uint16_t i;

        if (required > PATSVC_POOL_CHUNKS)
            return 0u;

        /* Find a free+unreserved run large enough for block + reservation. */
        for (new_offset = 0u;
             (uint32_t)(new_offset >> 2u) + required <= PATSVC_POOL_CHUNKS;
             new_offset = (uint16_t)(new_offset + 4u)) {
            uint16_t base = (uint16_t)(new_offset >> 2u);
            uint8_t fits = 1u;

            if (new_offset == offset)
                continue;
            for (i = 0u; i < required; i++) {
                if (patSvc_bitmapGet(region, (uint16_t)(base + i)) ||
                    patSvc_reservationGet((uint16_t)(base + i))) {
                    new_offset = (uint16_t)((base + i) << 2u);
                    fits = 0u;
                    break;
                }
            }
            if (!fits)
                continue;

            /* Found a suitable run — relocate. */
            for (i = 0u; i < logical_chunks; i++)
                patSvc_bitmapSet(region, (uint16_t)(base + i));
            memmove(&region->pool[new_offset], &region->pool[offset],
                    (size_t)logical_chunks * 4u);
            patSvc_publishOffset(&region->address[track][step], new_offset);
            for (i = 0u; i < logical_chunks; i++)
                patSvc_bitmapClear(region, (uint16_t)(old_chunk + i));
            memset(&region->pool[offset], 0, (size_t)logical_chunks * 4u);
            autosave_markNonSemanticPatternDirty(scene);

            /* Reserve the trailing chunk at the new position. */
            new_trailing = (uint16_t)(base + logical_chunks);
            patSvc_reservationSet(new_trailing);

            if (old_offset_out)
                *old_offset_out = offset;
            if (new_offset_out)
                *new_offset_out = new_offset;
            return 1u;
        }
    }
    return 0u;
}
```

**Description:** The repair step replaces Tier 1's gap-creation. Instead of
creating a free gap and hoping the right block claims it, the repair step
creates a reservation bit — an explicit ownership claim. Case 1 (free
trailing chunk — just set a reservation bit) is the common case after the
pool has converged: no pool write, no `memmove`, one bit-set. Case 3
(relocation required) uses the same write-new/publish/free-old transaction as
the old `patSvc_relocateIndex`, but searches for a run large enough for the
block plus one reservation chunk. The relocation is inlined rather than
calling `patSvc_relocateIndex` because it additionally needs the
reservation-aware free-run search and the trailing-chunk reservation set.

### 6B — Add new PatternTrace stage for repair-step relocation

**File: `PatternTrace.h`**

**Location:** after line 47 (`PAT_TRACE_STAGE_GAP_FALLBACK = 'G'`).
**Action:** ADD.

```c
    PAT_TRACE_STAGE_REPAIR_RESERVE = 'V',
    PAT_TRACE_STAGE_REPAIR_RELOC = 'L',
    PAT_TRACE_STAGE_DIRECT_RETAIN = 'D',
```

**Description:** Three new trace stages:
- `'V'` — a reservation was created in place (Case 1, no relocation).
- `'L'` — a block was relocated to create a trailing reservation (Case 3).
- `'D'` — a direct-path mutation was retained for reactive recovery (Step 9).

These use letters not currently in the PatternTrace enum (`V`, `L`, `D`).
`'R'` is retained for reactive recovery's relocation (unchanged from today).

---

## Step 7 — Delete periodic Tier 2 and replace the tick tail

**File: `PatternStackService.c`**

### 7A — Remove `tier2_scan_cursor` and `last_compact_tick` declarations

**Location:** lines 104–107.
**Action:** MODIFY — remove `tier2_scan_cursor` and `last_compact_tick` from
the declarations. Retain `tier1_scan_cursor`, `reactive_scan_cursor`,
`logical_chunks_used`, `reactive_required`, `reactive_active`.

Remove lines 104 and 107:

```c
static uint16_t tier2_scan_cursor;          /* line 104 — REMOVE */
static uint16_t last_compact_tick;          /* line 107 — REMOVE */
```

### 7B — Remove `tier2_scan_cursor` and `last_compact_tick` from `patSvc_init()`

**Location:** lines 833 and 838.
**Action:** REMOVE:

```c
    tier2_scan_cursor = 0u;                 /* line 833 — REMOVE */
    last_compact_tick = time_sysTick;        /* line 838 — REMOVE */
```

And ADD after the existing `reactive_active = 0u;` (line 836):

```c
    patSvc_clearReservationImage();
    reservation_density_active = 1u;
    patSvc_updateDensityLevel();
```

### 7C — Remove `tier2_scan_cursor` and `last_compact_tick` from `patSvc_finishSceneReplace()`

**Location:** lines 882 and 887.
**Action:** REMOVE:

```c
    tier2_scan_cursor = 0u;                 /* line 882 — REMOVE */
    last_compact_tick = time_sysTick;        /* line 887 — REMOVE */
```

And ADD after the existing `reactive_active = 0u;` (line 885):

```c
    patSvc_clearReservationImage();
    reservation_density_active = 1u;
    patSvc_updateDensityLevel();
```

### 7D — Remove `tier2_scan_cursor` from the handover-complete block

**Location:** line 1134.
**Action:** REMOVE:

```c
    tier2_scan_cursor = 0u;                 /* line 1134 — REMOVE */
```

And ADD after the existing `reactive_required = 0u;` (line 1136):

```c
    patSvc_clearReservationImage();
    reservation_density_active = 1u;
    patSvc_updateDensityLevel();
```

Remove `last_compact_tick = time_sysTick;` at line 1139.

### 7E — Replace the entire tick tail (Tier 1 + Tier 2 block)

**Location:** lines 1167–1234 (from `logical_chunks_used = patSvc_countUsed`
through the closing `}` of the `else` block).
**Action:** REMOVE the entire Tier 1 + Tier 2 block (lines 1167–1234).
ADD the replacement repair-epoch block:

```c
    /* Sample AutoSave pressure and density level once per tick. */
    patSvc_sampleRepairBudget();
    patSvc_updateDensityLevel();
    logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));

    /* Wake the repair cursor for a pending rebuild or a post-mutation wake. */
    if (reservation_rebuild_pending) {
        tier1_scan_cursor = 0u;
        reservation_rebuild_pending = 0u;
    }

    /* Bounded repair epoch: scan up to repairBudget entries per tick. */
    if (tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
        uint8_t budget = patSvc_repairBudget();
        uint8_t inspected = 0u;

        while (inspected < budget &&
               tier1_scan_cursor < PATSVC_ADDRESS_COUNT) {
            uint16_t address_index = tier1_scan_cursor++;
            uint16_t old_offset = 0u;
            uint16_t new_offset = 0u;

            inspected++;
            if (patSvc_repairStep(service_scene, address_index,
                                  &old_offset, &new_offset)) {
                if (old_offset != 0u || new_offset != 0u) {
                    /* Case 3: relocation occurred. */
                    patternTrace_record(PAT_TRACE_STAGE_REPAIR_RELOC,
                                        (uint8_t)(service_scene & 0x0Fu),
                                        patSvc_relocationValue(
                                            address_index,
                                            old_offset, new_offset));
                } else {
                    /* Case 1: in-place reservation. */
                    patternTrace_record(PAT_TRACE_STAGE_REPAIR_RESERVE,
                                        (uint8_t)(service_scene & 0x0Fu),
                                        (uint32_t)address_index);
                }
            }
        }
        return;
    }
    /* Repair epoch complete — cursor sleeps at PATSVC_ADDRESS_COUNT.
     * Only wake events (mutation, reservation consumed, handover,
     * filesystem replacement, density restore) reset it to 0. */
```

**Description:** The entire periodic Tier 1/Tier 2 self-chasing loop is
gone. In its place: one finite repair epoch that scans with an adaptive
budget, creates/verifies trailing reservations, and sleeps when done. The
cursor stays at `PATSVC_ADDRESS_COUNT` until a defined wake event resets it.

---

## Step 8 — Wake events

**File: `PatternStackService.c`**

### 8A — Wake on pool mutation (queue drain and direct execution)

**Location:** in `patSvc_drainQueue()`, after `patSvc_consumeHead()` at
line 741.
**Action:** ADD after the `logical_chunks_used` recount:

```c
    if (tier1_scan_cursor >= PATSVC_ADDRESS_COUNT)
        tier1_scan_cursor = 0u;
```

**Location:** in `patSvc_submit()`, after the direct-execution
`logical_chunks_used` recount at line 792.
**Action:** ADD:

```c
    if (tier1_scan_cursor >= PATSVC_ADDRESS_COUNT)
        tier1_scan_cursor = 0u;
```

**Location:** in `patSvc_drainBulk()`, after `logical_chunks_used` recount
at line 708.
**Action:** ADD:

```c
    if (tier1_scan_cursor >= PATSVC_ADDRESS_COUNT)
        tier1_scan_cursor = 0u;
```

**Description:** Any pool mutation (a consumed queue event, a direct
execution, or a completed bulk barrier) is a wake event that restarts the
repair pass. This ensures blocks freed or created by mutations get their
trailing reservations audited.

### 8B — Density restore already wakes (Step 3A)

The `patSvc_updateDensityLevel()` function already resets `tier1_scan_cursor`
to 0 when density transitions from 0 to 1. No additional code needed.

### 8C — Lifecycle boundaries already wake (Steps 7B, 7C, 7D)

The `patSvc_clearReservationImage()` calls set `reservation_rebuild_pending`,
and the tick-tail code (Step 7E) resets the cursor when that flag is set. No
additional code needed.

---

## Step 9 — Make reactive recovery reservation-aware

**File: `PatternStackService.c`**

### 9A — Modify `patSvc_reactiveStep()` to reclaim reservations

**Location:** lines 539–582 (`patSvc_reactiveStep`).
**Action:** MODIFY — when searching for a lower destination, allow the
reactive step to reclaim a reserved chunk if the density level indicates the
reservation is surplus (density off) or if the reservation is the only thing
blocking an otherwise-successful relocation.

In the `patSvc_relocateIndex()` call at line 569: the existing
`patSvc_relocateIndex` calls `patSvc_findFreeRun`, which now refuses reserved
chunks (Step 4A). For reactive recovery, we need an alternate search that can
reclaim surplus reservations.

**Revised approach:** add a second search pass inside `patSvc_reactiveStep()`
that, when the first (reservation-respecting) pass via `patSvc_relocateIndex`
fails, tries relocating by first clearing reservations in the target run. This
is only attempted when `!reservation_density_active` (reservations are surplus
at the current occupancy level).

After line 569's `patSvc_relocateIndex` call and its failure path (line 580),
add before the `return 0u`:

```c
    /* Second pass: if density is off, reclaim surplus reservations to create
     * a viable lower destination. */
    if (!reservation_density_active) {
        reactive_scan_cursor = 0u;
        while (reactive_scan_cursor < PATSVC_ADDRESS_COUNT) {
            /* ... (same scan structure, but with reservation-clearing) */
        }
    }
```

**Simplified approach for implementation:** rather than a second internal pass,
modify `patSvc_relocateIndex()` to accept an optional `allow_reclaim` parameter
that, when nonzero, clears reservation bits in the destination run before
checking for free space. This keeps the search logic in one place.

**Final approach (recommended):** add a `patSvc_findFreeRunReclaiming()`
variant that clears reservations in a candidate run before testing occupancy:

```c
/*
 * Find a free run, clearing surplus reservations as needed.
 *
 * What: identical to patSvc_findFreeRun() but when a chunk is free in the
 * occupancy bitmap and reserved, the reservation is cleared (reclaimed) to
 * make the chunk available. Why: reactive recovery under real allocation
 * pressure must be able to use space that was only soft-reserved by the
 * maintenance pass. This function is called only when
 * reservation_density_active is 0, meaning the current occupancy level has
 * defined all reservations as surplus. Inputs: same as patSvc_findFreeRun.
 * Output: a free run where reservation bits have been cleared, or
 * PAT_ADDR_SENTINEL. Affiliates: patSvc_reactiveStep().
 */
static uint16_t patSvc_findFreeRunReclaiming(
    const pat_scene_region_t *region,
    uint16_t chunks, uint16_t upper_chunk, uint8_t lower_only)
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
        if (i == chunks) {
            /* Clear any reservations in the run we are about to use. */
            for (i = 0u; i < chunks; i++)
                patSvc_reservationClear((uint16_t)(start + i));
            return (uint16_t)(start << 2u);
        }
    }
    return PAT_ADDR_SENTINEL;
}
```

Then modify `patSvc_reactiveStep()`: after the existing
`patSvc_relocateIndex()` call fails (line 569–579), add a second attempt using
`patSvc_findFreeRunReclaiming()` when `!reservation_density_active`:

```c
        /* Standard reservation-respecting relocation. */
        if (patSvc_relocateIndex(scene, address_index, 0u, 1u,
                                 &old_offset, &new_offset)) {
            /* ... existing trace and return ... */
        }
        /* When density is off, try reclaiming surplus reservations. */
        if (!reservation_density_active) {
            uint16_t reclaim_offset = patSvc_findFreeRunReclaiming(
                region, chunks, (uint16_t)(offset >> 2u), 1u);

            if (reclaim_offset != PAT_ADDR_SENTINEL &&
                reclaim_offset != offset) {
                /* Manual relocate using the reclaimed run. */
                uint16_t base = (uint16_t)(reclaim_offset >> 2u);
                uint16_t j;

                for (j = 0u; j < chunks; j++)
                    patSvc_bitmapSet(region, (uint16_t)(base + j));
                memmove(&region->pool[reclaim_offset],
                        &region->pool[offset],
                        (size_t)chunks * 4u);
                patSvc_publishOffset(
                    &region->address[track][step], reclaim_offset);
                for (j = 0u; j < chunks; j++)
                    patSvc_bitmapClear(region,
                                       (uint16_t)((offset >> 2u) + j));
                memset(&region->pool[offset], 0, (size_t)chunks * 4u);
                autosave_markNonSemanticPatternDirty(scene);
                patternTrace_record(PAT_TRACE_STAGE_TIER2_RELOC,
                                    (uint8_t)(scene & 0x0Fu),
                                    patSvc_relocationValue(
                                        address_index, offset,
                                        reclaim_offset));
                reactive_active = 0u;
                reactive_scan_cursor = 0u;
                return 1u;
            }
        }
```

**Description:** Reactive recovery now has two search passes. The first
respects reservations (existing behavior plus Step 4A). The second, only
attempted when `reservation_density_active` is 0, clears surplus reservations
to create a viable destination. This ensures semantic capacity always wins
under pressure — reservations are policy, not a hard constraint.

---

## Step 10 — Fix the direct-path parity gap

**File: `PatternStackService.c`**

### 10A — Modify `patSvc_submit()` to retain direct-path fragmentation failures

**Location:** lines 788–793 (the direct-execution branch).
**Action:** MODIFY — add fragmentation classification on a direct-path failure.

Replace the existing direct-execution block:

```c
    if (direct_allowed && patSvc_queueCount() == 0u && bulk_op == PATSVC_OP_NONE &&
        seq_activePattern == service_scene) {
        uint8_t result = patSvc_executeEvent(event);

        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
        return result;
    }
```

with:

```c
    if (direct_allowed && patSvc_queueCount() == 0u &&
        bulk_op == PATSVC_OP_NONE &&
        seq_activePattern == service_scene) {
        uint8_t result = patSvc_executeEvent(event);

        logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
        if (tier1_scan_cursor >= PATSVC_ADDRESS_COUNT)
            tier1_scan_cursor = 0u;
        patSvc_updateDensityLevel();
        if (result)
            return 1u;

        /* Direct-path fragmentation classification — same logic as
         * patSvc_drainQueue() lines 746–770. A direct-when-idle mutation
         * that fails only because of fragmentation is retained for the
         * same reactive recovery a queued mutation gets, not simply
         * returned as failed. */
        {
            uint16_t step_id = patSvc_eventStepId(event);
            uint8_t operation = patSvc_eventOperation(event);
            uint8_t track = (uint8_t)(step_id / NUM_STEPS);
            uint8_t step = (uint8_t)(step_id % NUM_STEPS);
            uint8_t required = patSvc_requiredChunks(
                service_scene, operation, track, step,
                patSvc_eventPayload(event));
            pat_scene_region_t *region = patSvc_region(service_scene);
            uint16_t free_chunks = (uint16_t)(PATSVC_POOL_CHUNKS -
                                              logical_chunks_used);
            uint16_t largest = patSvc_largestFreeRun(region);

            if (required == 0u || free_chunks < required) {
                patternTrace_record(PAT_TRACE_STAGE_CAPACITY_DROP,
                                    (uint8_t)(service_scene & 0x0Fu),
                                    event);
                return 0u;
            }
            if (largest < required) {
                /* Retain for reactive recovery via the queue. */
                patternTrace_record(PAT_TRACE_STAGE_DIRECT_RETAIN,
                                    (uint8_t)(service_scene & 0x0Fu),
                                    event);
                if (patSvc_enqueue(event)) {
                    reactive_active = 1u;
                    reactive_required = required;
                    reactive_scan_cursor = 0u;
                    return 1u;
                }
                return 0u;
            }
            /* Fallthrough: enough space and a large enough run exist but
             * executeEvent still failed — unexpected. Trace and drop. */
            patternTrace_record(PAT_TRACE_STAGE_FRAG_DROP,
                                (uint8_t)(service_scene & 0x0Fu), event);
        }
        return 0u;
    }
```

**Description:** A direct-when-idle mutation that fails only because of
fragmentation is now enqueued for reactive recovery instead of being silently
dropped. The `'D'` trace stage provides recoverable evidence for this
pathway. Capacity exhaustion (not enough total free space) is still an
immediate drop with a `'C'` trace. The public-API return value is 1 when the
event is successfully retained for recovery (the caller should not treat it
as a hard failure).

---

## Step 11 — Remove `patSvc_gapTarget()` (now unused)

**File: `PatternStackService.c`**

**Location:** lines 451–457 (`patSvc_gapTarget`).
**Action:** REMOVE the function entirely. It was used only by the deleted
`patSvc_tier1Step()`.

`PAT_GAP_REDUCE_THRESHOLD` in `config.h` is retained — it is still
referenced by the comment block (Step 0B) as a reference point and may be
used by a future gap-target decision. If it has no remaining code reference
after this step, it may be deleted in a cleanup pass.

---

## Step 12 — Update `patSvc_tick()` comment block and header

### 12A — Update `patSvc_tick()` comment in PatternStackService.c

**Location:** lines 1088–1094.
**Action:** MODIFY.

```c
/*
 * Advance one bounded 500 Hz service pass.
 *
 * Priority 0 closes/drains target handover; priority 1 drains one FIFO event
 * or eight barrier steps; priority 2 handles reactive recovery for a blocked
 * head; priority 3 runs the finite bounded repair epoch with adaptive budget.
 * The repair cursor sleeps at PATSVC_ADDRESS_COUNT between epochs; only wake
 * events (mutation, reservation consumed, density restore, handover, or
 * filesystem replacement) reset it.
 */
```

### 12B — Update `patSvc_tick()` comment in PatternStackService.h

**Location:** lines 26–33.
**Action:** MODIFY — replace "Tier 1 gap maintenance, and paced Tier 2
compaction" with "finite bounded repair epoch with adaptive budget."

```c
/*
 * Advance one bounded 500 Hz service pass.
 *
 * What: handles target handover, deferred queue events, bulk barriers,
 * reactive recovery, and finite bounded repair with owned trailing-slack
 * reservations. Why: no pool scan, copy, or relocation is allowed in TIM3
 * context. Inputs: live service state and seq_activePattern. Output: at most
 * one queue/relocation transaction per foreground pass, with bounded
 * repair/scan work.
 */
```

---

## Step 13 — Update `SRAM_MANIFEST.md`

**File: `knowledge_files/specification_reference/SRAM_MANIFEST.md`**

**Action:** ADD a new entry in the Pattern Stack Service section documenting
the 515 bytes of new allocation:

| Symbol | Bytes | Region | Owner |
|--------|------:|--------|-------|
| `reservation_image[512]` | 512 | SRAM1 .bss | PatternStackService.c |
| `reservation_density_active` | 1 | SRAM1 .bss | PatternStackService.c |
| `repair_budget_busy` | 1 | SRAM1 .bss | PatternStackService.c |
| `reservation_rebuild_pending` | 1 | SRAM1 .bss | PatternStackService.c |

And REMOVE the entries for `tier2_scan_cursor` (2 bytes) and
`last_compact_tick` (2 bytes) since those statics are deleted.

Net change: +515 − 4 = +511 bytes `.bss`.

---

## Step 14 — Update `PATTERN_DYNAMIC_STACK.md` §12

**File: `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md`**

### 14A — Update §12.2 (Service tick priority order)

Replace the Tier 1 and Tier 2 items with:

```text
4. **Finite bounded repair** — scan up to PAT_REPAIR_SCAN_IDLE (or
   PAT_REPAIR_SCAN_BUSY under AutoSave pressure) address entries per tick,
   creating or verifying one trailing-chunk reservation per occupied block.
   Sleeps when the cursor reaches PATSVC_ADDRESS_COUNT; wakes only on
   mutation, reservation consumption, density-restore transition, handover,
   or filesystem replacement.
```

### 14B — Rewrite §12.6 and §12.7

Replace §12.6 (Tier 1 trailing-gap maintenance) and §12.7 (Tier 2 paced
compaction) with a single section:

```markdown
### 12.6 Owned trailing-slack reservation

The reservation image is a 512-byte bit-packed array with the same geometry
as the occupancy bitmap. A set reservation bit means the chunk is reserved as
trailing slack for the immediately-preceding occupied block and is refused to
ordinary allocation. The repair pass creates reservations; the Gate-6 growth
path (`pat_tryAppendAutomation`) consumes them; reactive recovery reclaims
surplus ones under allocation pressure.

A latchable density-level state variable scales reservation density with pool
occupancy: at low occupancy (below PAT_RESERVATION_RESTORE_THRESHOLD), one
trailing chunk per occupied block is reserved; at high occupancy (above
PAT_RESERVATION_REDUCE_THRESHOLD), no reservations are created and all free
space is available. Hysteresis prevents oscillation. An adaptive per-tick
budget (PAT_REPAIR_SCAN_IDLE vs PAT_REPAIR_SCAN_BUSY) yields foreground
cycles to AutoSave I/O when dirty work is pending.

The reservation image is not persisted. It is cleared and lazily rebuilt at
each lifecycle boundary (init, handover-complete, filesystem replacement).
During the rebuild window, allocation falls back to the occupancy bitmap
alone.

**Known open issue from Session 068 — CLOSED.** The Tier 1/Tier 2
self-chase described in §12.7 of the prior version is eliminated. The repair
pass converges in one bounded epoch and sleeps. Reactive compaction runs
only when triggered by a blocked allocation. Physical relocations are
tracked as non-semantic dirty work (see S069 non-semantic plan).
```

### 14C — Update §12.8 (Elastic gap policy)

Remove or rewrite — the elastic gap policy (`PAT_GAP_REDUCE_THRESHOLD`) is
replaced by the reservation-density hysteresis. If `PAT_GAP_REDUCE_THRESHOLD`
is retained in `config.h` for the reactive gap target, note that here;
otherwise delete §12.8.

### 14D — Update §12.11 (Config constants)

Replace with the full current constant set including the new
`PAT_RESERVATION_*` and `PAT_REPAIR_SCAN_*` values, and remove
`PAT_COMPACT_INTERVAL_MS`.

### 14E — Update §12.15 (PatternTrace stage codes)

Add the three new stage codes: `V` (repair reserve), `L` (repair relocation),
`D` (direct retain).

---

## Step 15 — Build verification and cleanup

1. `make clean && make && make img` — verify no warnings, no errors.
2. Compare `.text`, `.data`, `.bss` against the baseline. Expected: `.bss`
   increases by ~511 bytes; `.text` increases modestly (new helpers, removed
   Tier 2 loop); `.data` unchanged.
3. Verify `PAT_COMPACT_INTERVAL_MS` has zero remaining references (grep).
4. Verify `tier2_scan_cursor` and `last_compact_tick` have zero remaining
   references.
5. Verify `patSvc_tier1Step` and `patSvc_gapTarget` have zero remaining
   references.
6. Run `grep -n "pat_bitmapGet\|patSvc_bitmapGet" Core/` to confirm every
   allocation-path call site now has a corresponding reservation check.

---

## Verification plan

Per the plan's verification targets table. Hardware verification after build
verification.

| Target | How to verify |
|--------|---------------|
| Image geometry | Link map shows 512-byte `reservation_image` in `.bss`, no `pat_scene_region_t` growth. |
| Rebuild | Force a Scene switch → handover-complete → verify the repair cursor restarts from 0 and creates reservations. PatternTrace `'V'` records appear. |
| Reservation vs. occupancy | With a Pattern that has occupied blocks, verify `patSvc_findFreeRun` refuses a reserved-but-free chunk. |
| Growth | Add an automation to a block whose trailing chunk is reserved → verify in-place append with no relocation, `'M'` trace. |
| Idle service | One completed repair epoch → cursor at `PATSVC_ADDRESS_COUNT` → no further `'V'`/`'L'` records until a mutation event. |
| Tier 2 removal | Zero periodic `'R'` records. `'R'` appears only during reactive recovery. |
| Fragmentation, direct path | Force a direct-path fragmentation failure → verify `'D'` trace and reactive recovery activation. |
| Density scaling | Fill pool above 70% → verify density off, no new reservations. Free below 50% → verify density on, repair wakes. |
| Adaptive budget | With AutoSave dirty → verify reduced scan rate. With AutoSave clean → verify full scan rate. |
| RAM proof | `arm-none-eabi-size` and link map. |
| Persistence | PAT4 files unchanged. |

---

## Implementation order

Steps 0–2 are foundational (constants, image, helpers).
Steps 3–5 add the policy and cross-module integration.
Step 6 replaces the Tier 1 mechanism.
Step 7 removes the Tier 2 mechanism and rewires the tick tail.
Step 8 defines wake events.
Step 9 makes reactive recovery reservation-aware.
Step 10 fixes the direct-path parity gap.
Steps 11–15 are cleanup and documentation.

Recommended commit boundaries:
- **Commit 1**: Steps 0–5 (image, helpers, allocation-aware, cross-module accessor).
- **Commit 2**: Steps 6–8 (repair epoch replaces Tier 1/Tier 2, wake events).
- **Commit 3**: Steps 9–10 (reactive recovery and direct-path parity).
- **Commit 4**: Steps 11–15 (cleanup, docs, build verify).
