# S069 Pass 1 — Implementation Schedule

Parent plan: `S069_ATS_PAT_BOUNDED_CLAUDE.md` (Pass 1: items 3.1, 3.2, 5).

All line numbers were verified against the pre-pass `dev-ph4-5-fixes` HEAD
(`a174fb6`).

Pass 1 source implementation is complete. Hardware acceptance remains pending.

## Work log

### 2026-09-20 — Pass 1 implementation

- Added the private `autosave_dirty_count` population tracker and 256-byte
  `popcount8_lut[]`. `autosave_maskByteOr()` counts only fresh 0-to-1 bits;
  `autosave_maskBitTake()` decrements only a consumed set bit; discard resets
  the derived count with the canonical mask. `autosave_maskHasDirty()` is now
  an O(1) count test. The DEV logging build audits the invariant every 1,000
  calls under a coherent PRIMASK snapshot and emits trace stage `Z` on drift;
  `tools/decode_devlogs.py` decodes the maintained/full counts.
- Optimized `patSvc_countUsed()` from 2,048 per-bit reads to 64
  `memcpy()`/`__builtin_popcount()` word reads. Removed only the clean idle
  tick-tail recount; all existing mutation/lifecycle reconciliation call sites
  remain. No Pattern storage or new API was added.
- Added `AUTOSAVE_PATTERN_QUIET_WINDOW_MS` = 250 and
  `AUTOSAVE_PATTERN_MAX_LATENCY_MS` = 5,000. Semantic Pattern mutation now
  records one global TIM2 timestamp. The scheduler captures a dirty epoch,
  defers until quiet or the deadline, rotates Scene selection for fairness, and
  resets the epoch when the mask is clean even if policy/card gates suppressed
  the scheduler. Non-semantic Pattern scheduling is unchanged.
- The implementation keeps the 11-byte source-level SRAM estimate from the
  pass plan: 2-byte scalar count, 4-byte semantic timestamp, 4-byte filesystem
  first-dirty timestamp, and 1-byte Scene cursor. The 256-byte LUT is ROM.
- Clean verification passed: `make clean && make && make img`; linked sizes
  are `text=449,476`, `data=404`, `bss=291,724`, and the packaged image is
  449,896 bytes (449,880-byte firmware payload plus the 16-byte image header).
  `git diff --check` and Python syntax compilation of
  `tools/decode_devlogs.py` also pass. Existing compiler/linker warnings are
  unchanged and unrelated to this pass.
- Descriptive contract blocks were kept adjacent to the changed declarations
  and implementations in the AutoSave, PatternStackService, and filesystem
  `.h`/`.c` surfaces; the new trace stage and timing controls are documented
  at their owning header/config declarations as well.

---

## Change summary

| # | File | Line(s) | Action | Item |
|---|------|---------|--------|------|
| C01 | Autosave.c | 76–77 | ADD | 3.1 |
| C02 | Autosave.c | 76 (before) | ADD | 3.1 |
| C03 | Autosave.c | 257–264 | MODIFY | 3.1 |
| C04 | Autosave.c | 2015–2041 | MODIFY | 3.1 |
| C05 | Autosave.c | 1344–1359 | MODIFY | 3.1 |
| C06 | Autosave.c | 1934–1951 | MODIFY | 3.1 |
| C07 | PatternStackService.c | 487–498 | MODIFY | 3.2 |
| C08 | PatternStackService.c | 1554–1557 | MODIFY | 3.2 |
| C09 | config.h | after 468 | ADD | 5 |
| C10 | Autosave.c | 11 (includes) | MODIFY | 5 |
| C11 | Autosave.c | 89 (after) | ADD | 5 |
| C12 | Autosave.c | 145–158 | MODIFY | 5 |
| C13 | Autosave.c | 1344–1359 | MODIFY | 5 |
| C14 | Autosave.h | 558–560 | ADD (after) | 5 |
| C15 | filesystem.c | 1826–1827 | ADD (after) | 5 |
| C16 | filesystem.c | 24306–24364 | MODIFY | 5 |

Note: C05 and C13 modify the same function (`autosave_discardDirtyMask`) —
apply together as a single edit.

---

## Item 3.1 — Scalar dirty predicate (exact dirty-bit count)

### C01 — Add `autosave_dirty_count` variable

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: after line 76 (`autosave_dirty_mask[]` declaration), before line 77
**Action**: ADD

```
/*
 * Exact population count of set bits in autosave_dirty_mask[].
 *
 * What: tracks how many of the 30,848 possible dirty bits are currently set.
 * Why: replaces the O(3856) sequential scan in autosave_maskHasDirty() with
 * an O(1) zero test. The count is maintained atomically alongside the mask:
 * incremented inside autosave_maskByteOr() (the sole OR-merge entry) and
 * decremented inside autosave_maskBitTake() (the sole consume entry), both
 * already under PRIMASK critical sections, so no new concurrency hazard is
 * introduced. Reset to zero alongside the mask in autosave_discardDirtyMask().
 * Maximum value is 30,848 (AUTOSAVE_MASK_BYTES * 8), within uint16_t range.
 * Inputs: autosave_maskByteOr() popcount-delta on fresh bits;
 * autosave_maskBitTake() unit decrement on consumed bits. Output: read by
 * autosave_maskHasDirty() for the zero test. Affiliates: filesystem.c drain
 * phase 55 entry gate, autonomous-writer completion callback.
 * RAM cost: 2 bytes SRAM1 .bss.
 */
static volatile uint16_t autosave_dirty_count;
```

### C02 — Add 256-byte popcount lookup table

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: before line 76 (before the dirty-mask declarations), after
the existing `#include` and `_Static_assert` block — best placement is after
line 63 (end of the initial `_Static_assert` group) and before the comment
block at line 65.
**Action**: ADD

```
/*
 * 256-byte ROM lookup table: popcount8_lut[b] == number of set bits in b.
 *
 * What: maps every byte value to its Hamming weight. Why: used inside the
 * PRIMASK critical section of autosave_maskByteOr() to count freshly set
 * bits without a multi-cycle software popcount loop. A ROM lookup is ~4
 * cycles (LDR via PC-relative literal pool) versus ~12 cycles for the
 * portable bit-twiddling sequence, keeping the IRQ-disabled window minimal.
 * Inputs: any uint8_t. Output: 0..8. Owner: Autosave.c (private).
 * ROM cost: 256 bytes .rodata.
 */
static const uint8_t popcount8_lut[256] = {
    0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4,1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    1,2,2,3,2,3,3,4,2,3,3,4,3,4,4,5,2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    2,3,3,4,3,4,4,5,3,4,4,5,4,5,5,6,3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,
    3,4,4,5,4,5,5,6,4,5,5,6,5,6,6,7,4,5,5,6,5,6,6,7,5,6,6,7,6,7,7,8
};
```

### C03 — Modify `autosave_maskByteOr()` to maintain dirty count

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: lines 257–264
**Action**: MODIFY

Replace the current body. The function already runs under PRIMASK; the
count update adds ~4 cycles (one LDR from the LUT) to the critical section.

**Before** (current):
```c
static void autosave_maskByteOr(uint16_t mask_byte, uint8_t bits)
{
    uint32_t primask = autosave_irqSave();

    autosave_dirty_mask[mask_byte] = (uint8_t)(
        autosave_dirty_mask[mask_byte] | bits);
    autosave_irqRestore(primask);
}
```

**After**:
```c
/*
 * Atomically OR one set of bits into one canonical mask byte.
 *
 * Inputs: bounded mask-byte index and set-bit pattern. Output: those bits are
 * retained without losing concurrent foreground/interrupt producers, and
 * autosave_dirty_count is incremented by the number of freshly set bits.
 * Why: every producer, recovery merge, and rollback has identical OR
 * semantics. The fresh-bit delta (bits that were not already set) is
 * computed inside the same PRIMASK section that updates the mask byte, so
 * the count remains atomically consistent with the mask. The popcount8_lut
 * ROM lookup adds ~4 cycles to the critical section. The caller performs
 * range checks so this helper stays one-byte and bounded.
 * Affiliates: autosave_markPayloadOffsetDirty(), autosave_maskMergeChunk(),
 * autosave_maskRestoreCaptured(). Counterpart: autosave_maskBitTake().
 */
static void autosave_maskByteOr(uint16_t mask_byte, uint8_t bits)
{
    uint32_t primask = autosave_irqSave();
    uint8_t old = autosave_dirty_mask[mask_byte];
    uint8_t fresh = (uint8_t)(bits & (uint8_t)~old);

    autosave_dirty_mask[mask_byte] = (uint8_t)(old | bits);
    autosave_dirty_count = (uint16_t)(
        autosave_dirty_count + popcount8_lut[fresh]);
    autosave_irqRestore(primask);
}
```

**Invariant**: `fresh` captures only bits that transition from 0 to 1.
Already-set bits (from concurrent producers or rollback re-ORing) contribute
zero to the popcount delta. This handles `autosave_maskMergeChunk()` recovery
merge and `autosave_maskRestoreCaptured()` rollback correctly — re-ORing
an already-set bit does not double-count.

### C04 — Modify `autosave_maskBitTake()` to maintain dirty count

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: lines 2015–2041
**Action**: MODIFY

Add one decrement inside the existing PRIMASK critical section.

**Before** (current, inside the PRIMASK section at lines 2035–2039):
```c
    primask = autosave_irqSave();
    was_set = (uint8_t)((autosave_dirty_mask[mask_byte] & bit) != 0u);
    autosave_dirty_mask[mask_byte] = (uint8_t)(
        autosave_dirty_mask[mask_byte] & (uint8_t)~bit);
    autosave_irqRestore(primask);
```

**After**:
```c
    /*
     * Atomically claim one LSB-first dirty cell for foreground classification.
     *
     * Input: payload offset. Output: its prior bit state; a set bit is cleared
     * in the same one-byte critical section, and autosave_dirty_count is
     * decremented by one. Why: a later interrupt mutation re-sets the bit via
     * autosave_maskByteOr() and survives for continuation; the count decrement
     * here exactly balances the increment that occurred when the bit was
     * originally set. Parameter get remains outside this section.
     * Affiliate: filesystem autosave phase 56. Counterpart: autosave_maskByteOr().
     */
    primask = autosave_irqSave();
    was_set = (uint8_t)((autosave_dirty_mask[mask_byte] & bit) != 0u);
    autosave_dirty_mask[mask_byte] = (uint8_t)(
        autosave_dirty_mask[mask_byte] & (uint8_t)~bit);
    if (was_set)
        autosave_dirty_count = (uint16_t)(autosave_dirty_count - 1u);
    autosave_irqRestore(primask);
```

### C05 — Modify `autosave_discardDirtyMask()` to reset dirty count

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: lines 1344–1359
**Action**: MODIFY (combined with C13 for item 5)

Add `autosave_dirty_count = 0u;` after the `memset` and before the Pattern
mask clears. Also add `autosave_last_pattern_semantic_us = 0u;` (item 5,
see C13).

**Before** (current body):
```c
    memset((void *)autosave_dirty_mask, 0, sizeof(autosave_dirty_mask));
    autosave_pattern_dirty_mask = 0u;
    autosave_nonsemantic_pattern_dirty_mask = 0u;
```

**After**:
```c
    /*
     * Clear the sole canonical record after producers and transforms stop.
     *
     * Inputs: filesystem lifecycle has disabled tracking and verified that no
     * autosave operation is consuming mask chunks. Output: every pending
     * scalar, semantic Pattern, and non-semantic Pattern bit is discarded in
     * SRAM; the dirty-count population tracker and the Pattern semantic
     * timestamp are reset; SD records remain untouched. Why: stale work from
     * an intentionally disabled/retired Bank session must not reappear after
     * re-enable, and derived state (count, timestamp) must not outlive the
     * primary record. Affiliates: filesystem's immediate/deferred OFF
     * transition.
     */
    memset((void *)autosave_dirty_mask, 0, sizeof(autosave_dirty_mask));
    autosave_dirty_count = 0u;
    autosave_last_pattern_semantic_us = 0u;
    autosave_pattern_dirty_mask = 0u;
    autosave_nonsemantic_pattern_dirty_mask = 0u;
```

### C06 — Replace `autosave_maskHasDirty()` body with constant-time test

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: lines 1934–1951
**Action**: MODIFY

Replace the O(AUTOSAVE_MASK_BYTES) sequential scan with an O(1) zero test.
Add a DEV-only periodic full-scan assertion to catch drift during
development.

**Before** (current):
```c
uint8_t autosave_maskHasDirty(void)
{
    uint16_t byte_index;

    /*
     * Test the canonical SRAM completeness register without changing it.
     * ...
     */
    for (byte_index = 0u; byte_index < AUTOSAVE_MASK_BYTES; byte_index++) {
        if (autosave_dirty_mask[byte_index] != 0u)
            return 1u;
    }
    return 0u;
}
```

**After**:
```c
/*
 * Test whether any scalar dirty bit is pending in the canonical mask.
 *
 * What: returns nonzero when at least one bit in autosave_dirty_mask[] is
 * set. Why: the maintained autosave_dirty_count replaces the former O(3856)
 * byte scan with a single comparison, eliminating ~29.3M byte inspections
 * per second at steady-state clean. Input: autosave_dirty_count (maintained
 * by autosave_maskByteOr increment and autosave_maskBitTake decrement).
 * Output: 1 when dirty work exists, 0 when clean. Affiliates: filesystem
 * drain phase 55 entry gate and the autonomous-writer completion callback.
 *
 * DEV assertion: every 1000th call, a full-scan popcount is compared against
 * the maintained count to catch implementation drift. The assertion is
 * compiled only under DEV_MODE_LOGGING and its cost (~3856 byte reads every
 * ~130 ms at ~7600 calls/s) is negligible relative to the eliminated scan.
 */
uint8_t autosave_maskHasDirty(void)
{
#if DEV_MODE_LOGGING
    {
        static uint16_t hasdirty_audit_counter;

        if (++hasdirty_audit_counter >= 1000u) {
            uint16_t byte_index;
            uint16_t full_count = 0u;

            hasdirty_audit_counter = 0u;
            for (byte_index = 0u; byte_index < AUTOSAVE_MASK_BYTES;
                 byte_index++) {
                full_count = (uint16_t)(
                    full_count +
                    popcount8_lut[autosave_dirty_mask[byte_index]]);
            }
            if (full_count != autosave_dirty_count) {
                autosaveTrace_record(AUTOSAVE_TRACE_STAGE_DIRTY_COUNT_MISMATCH,
                    (uint8_t)(autosave_dirty_count >> 8u),
                    (uint32_t)((full_count << 16u) |
                               autosave_dirty_count));
            }
        }
    }
#endif
    return (uint8_t)(autosave_dirty_count != 0u);
}
```

**Trace record**: if the audit fires, a `DIRTY_COUNT_MISMATCH` stage is
emitted with maintained count and scan count packed into the value field.
This requires adding one enum value to `autosave_trace_stage_t` (see
auxiliary changes below). The trace record is diagnostic evidence only and
does not change the return value — the maintained count is trusted as the
return, and the mismatch record alerts the developer to investigate.

**Auxiliary**: add `AUTOSAVE_TRACE_STAGE_DIRTY_COUNT_MISMATCH` to
`autosave_trace_stage_t` in `AutosaveTrace.h` (or its defining header).
This is a single enum value addition gated by `DEV_MODE_LOGGING`. Add a
matching decoder entry to `tools/decode_devlogs.py`.

---

## Item 3.2 — Pattern logical occupancy (tick-tail removal)

### C07 — Optimise `patSvc_countUsed()` with word-level popcount

**File**: `Core/Bank/Scene/Pattern/PatternStackService.c`
**Location**: lines 487–498
**Action**: MODIFY

Replace the per-bit loop (2,048 iterations of `patSvc_bitmapGet()`) with a
32-bit-word popcount loop (64 iterations of `memcpy` + `__builtin_popcount`),
matching the established pattern in `pat_poolUsagePercent()`
(PatternData.c:137–139). Cost drops from ~2,048 cycles to ~768 cycles.

**Before** (current):
```c
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
```

**After**:
```c
/*
 * Count occupied logical pool chunks for the current service target.
 *
 * What: returns the total number of set bits in the first PATSVC_POOL_CHUNKS
 * bits (256 bytes) of region->bitmap[]. Why: occupancy drives the
 * reservation-density policy and queue-failure classification. The word-level
 * popcount iterates 64 aligned uint32_t words via memcpy (unaligned-safe)
 * plus __builtin_popcount, matching the established pattern in
 * pat_poolUsagePercent() (PatternData.c). Cost is ~768 cycles versus ~2,048
 * cycles for the former per-bit loop. Input: pat_scene_region_t pointer
 * (NULL returns 0). Output: 0..PATSVC_POOL_CHUNKS. Affiliates: every
 * mutation-boundary recount in patSvc_drainQueue, patSvc_submit,
 * patSvc_drainBulk, patSvc_clearTrack, patSvc_init, patSvc_finishSceneReplace,
 * patSvc_removeTrackAutomationByTarget, and the scene-match recheck in
 * patSvc_tick. The tick-tail idle recount was removed (item 3.2).
 */
static uint16_t patSvc_countUsed(const pat_scene_region_t *region)
{
    uint16_t used = 0u;
    uint16_t i;
    uint32_t word;

    if (!region)
        return 0u;
    for (i = 0u; i < (uint16_t)(PATSVC_POOL_CHUNKS / 32u); i++) {
        memcpy(&word, &region->bitmap[i * 4u], sizeof(word));
        used = (uint16_t)(used + (uint16_t)__builtin_popcount(word));
    }
    return used;
}
```

**Note**: `PATSVC_POOL_CHUNKS / 32u` = `2048 / 32` = 64 iterations. Each
iteration reads 4 bytes (32 bits) from `bitmap[]`, matching exactly the 256
bytes that the scan covers. The `memcpy` approach is unaligned-safe on
Cortex-M and matches the existing `pat_poolUsagePercent()` precedent.

### C08 — Remove tick-tail idle recount

**File**: `Core/Bank/Scene/Pattern/PatternStackService.c`
**Location**: lines 1554–1557
**Action**: MODIFY

Remove the `patSvc_countUsed()` call from the tick tail. Keep
`patSvc_sampleRepairBudget()` and `patSvc_updateDensityLevel()` — the
latter uses the `logical_chunks_used` value from the most recent mutation
or reconciliation, which is correct because no mutation has occurred since.

**Before** (current):
```c
    /* Sample pressure and occupancy once per tick before repair work. */
    patSvc_sampleRepairBudget();
    logical_chunks_used = patSvc_countUsed(patSvc_region(service_scene));
    patSvc_updateDensityLevel();
```

**After**:
```c
    /*
     * Sample pressure once per tick before repair work.
     *
     * Why the idle-tick recount is removed: at ~500 ticks/s idle, the former
     * patSvc_countUsed() call produced ~1,024,000 bitmap tests per second with
     * zero information gain when the pool has not changed. The value
     * logical_chunks_used remains current from the most recent mutation-path
     * recount (patSvc_drainQueue, patSvc_submit, patSvc_drainBulk,
     * patSvc_clearTrack, patSvc_init, patSvc_finishSceneReplace, handover
     * completion, or patSvc_removeTrackAutomationByTarget). A one-chunk stale
     * window (if pat_tryAppendAutomation consumed a reserved chunk since the
     * last mutation recount) is at most a one-level density classification
     * error that self-corrects on the next mutation. Affiliates:
     * patSvc_updateDensityLevel() and the repair while-loop below.
     */
    patSvc_sampleRepairBudget();
    patSvc_updateDensityLevel();
```

**Retained call sites** (unchanged, not listed individually — all existing
`logical_chunks_used = patSvc_countUsed(...)` calls at mutation boundaries
remain):

| Line | Context |
|------|---------|
| 1026 | `patSvc_drainBulk` completion |
| 1063 | `patSvc_drainQueue` successful execution |
| 1074 | `patSvc_drainQueue` failure classification (local `used`) |
| 1118 | `patSvc_submit` direct idle path |
| 1202 | `patSvc_init` |
| 1249 | `patSvc_finishSceneReplace` |
| 1382 | `patSvc_clearTrack` direct |
| 1407 | `patSvc_clearPattern` direct (assigns 0, no call) |
| 1441 | `patSvc_removeTrackAutomationByTarget` |
| 1518 | `patSvc_tick` scene-match recheck |

---

## Item 5 — Pattern AutoSave quiet window and maximum latency

### C09 — Add timing defines to config.h

**File**: `config.h`
**Location**: after line 468 (`AUTOSAVE_TRACE_FLUSH_INTERVAL_MS`), before
line 470 (session-065 comment block)
**Action**: ADD

```c
/*
 * Pattern AutoSave quiet-window and maximum-latency timing controls.
 *
 * AUTOSAVE_PATTERN_QUIET_WINDOW_MS: minimum silence after the last semantic
 * Pattern mutation before a PAT4 drain fires. What: milliseconds of no
 * autosave_markPatternDirty() calls. Why: coalesces rapid editing bursts
 * (automation recording, fast knob turns) into a single snapshot instead of
 * producing intermediate generations that are immediately stale. Input:
 * autosave_lastPatternSemanticUs() timestamp. Output: the semantic Pattern
 * drain scheduler defers until this window elapses. Does not apply to the
 * non-semantic drain (maintenance work is bounded by the CPU budget and runs
 * only when no higher-priority work is pending). Affiliates: filesystem.c
 * semantic Pattern drain scheduler, Autosave.c timestamp producer.
 *
 * AUTOSAVE_PATTERN_MAX_LATENCY_MS: hard ceiling on elapsed time from the
 * first dirty Scene to the first drain attempt. What: milliseconds since the
 * Pattern dirty mask first became nonzero. Why: ensures convergence under
 * sustained editing where the quiet window never elapses. When this deadline
 * fires, the drain proceeds regardless of the quiet window. Input: the
 * first-dirty timestamp maintained by the drain scheduler. Output: the
 * scheduler fires the drain even though the quiet window has not elapsed.
 * Affiliates: filesystem.c semantic Pattern drain scheduler.
 */
#define AUTOSAVE_PATTERN_QUIET_WINDOW_MS  250u
#define AUTOSAVE_PATTERN_MAX_LATENCY_MS  5000u
```

### C10 — Add `#include "timebase.h"` to Autosave.c

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: after line 20 (`#include "filesystem.h"`), before line 22
(`#include <string.h>`)
**Action**: ADD

```c
/* Supplies timebase_tim2Now() for the Pattern semantic mutation timestamp. */
#include "timebase.h"
```

**Why**: `autosave_markPatternDirty()` needs `timebase_tim2Now()` to record
when the most recent semantic Pattern mutation occurred. `timebase_tim2Now()`
is a TIM2 counter-register read, safe from any context including under
PRIMASK.

### C11 — Add `autosave_last_pattern_semantic_us` variable

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: after line 89 (`autosave_pattern_dirty_mask` declaration),
before the non-semantic comment block at line 91
**Action**: ADD

```c
/*
 * Microsecond timestamp of the most recent semantic Pattern mutation.
 *
 * What: records timebase_tim2Now() at each autosave_markPatternDirty() call.
 * Why: the filesystem.c Pattern drain scheduler uses this to enforce a quiet
 * window — deferring the snapshot until editing pauses. Only one Pattern
 * Scene can be the active mutation target at a time (service_scene in
 * PatternStackService.c), so quiet is a global property: the timestamp
 * reflects edits to whichever Scene is currently active. Input: written by
 * autosave_markPatternDirty() under PRIMASK. Output: read by the foreground
 * filesystem scheduler via autosave_lastPatternSemanticUs(). A single
 * 32-bit word is atomic on ARM Cortex-M (aligned word read/write). Reset to
 * zero by autosave_discardDirtyMask(). Lifetime: static SRAM1 .bss.
 * RAM cost: 4 bytes. Affiliates: filesystem.c Pattern drain scheduler.
 */
static volatile uint32_t autosave_last_pattern_semantic_us;
```

### C12 — Record timestamp in `autosave_markPatternDirty()`

**File**: `Core/Bank/Scene/Autosave.c`
**Location**: lines 145–158
**Action**: MODIFY

Add `timebase_tim2Now()` inside the existing PRIMASK critical section.

**Before** (current):
```c
void autosave_markPatternDirty(uint8_t scene_index)
{
    uint32_t primask;

    if (!autosave_mutation_tracking_enabled || scene_index >= SCENE_COUNT)
        return;
    primask = autosave_irqSave();
    autosave_pattern_dirty_mask |= (uint16_t)(1u << scene_index);
    (void)filesystem_clearResidentRefreshed(
        (uint16_t)(AUTOSAVE_HCNAMES_PATTERN_BASE + scene_index));
    autosave_irqRestore(primask);
}
```

**After**:
```c
/*
 * Mark one resident Scene's Pattern region dirty for AutoSave.
 *
 * Input: scene_index 0..15. Output: the corresponding bit is atomically set
 * only while runtime mutation tracking is enabled, the Pattern HCNAMES
 * refreshed witness is cleared, and the global semantic-mutation timestamp
 * is updated to the current TIM2 microsecond value. Why: Pattern payloads
 * live outside the scalar record, while the refreshed bit must stop claiming
 * that the prior on-card image still matches SRAM after a user edit. The
 * timestamp drives the quiet-window deferral in the filesystem scheduler:
 * the drain waits until AUTOSAVE_PATTERN_QUIET_WINDOW_MS elapses after this
 * call, coalescing rapid edit bursts. No I/O occurs here.
 * Affiliates: PatternData.c mutation funnel, filesystem.c drain scheduler,
 * autosave_lastPatternSemanticUs() getter.
 */
void autosave_markPatternDirty(uint8_t scene_index)
{
    uint32_t primask;

    if (!autosave_mutation_tracking_enabled || scene_index >= SCENE_COUNT)
        return;
    primask = autosave_irqSave();
    autosave_pattern_dirty_mask |= (uint16_t)(1u << scene_index);
    autosave_last_pattern_semantic_us = timebase_tim2Now();
    (void)filesystem_clearResidentRefreshed(
        (uint16_t)(AUTOSAVE_HCNAMES_PATTERN_BASE + scene_index));
    autosave_irqRestore(primask);
}
```

### C13 — Reset timestamp in `autosave_discardDirtyMask()`

Combined with C05 above — see the single merged edit for
`autosave_discardDirtyMask()` under C05. The line
`autosave_last_pattern_semantic_us = 0u;` is included there.

### C14 — Add public getter to Autosave.h and Autosave.c

**File (header)**: `Core/Bank/Scene/Autosave.h`
**Location**: after line 560 (`autosave_clearPatternDirty` declaration),
before the non-semantic comment block at line 562
**Action**: ADD

```c
/*
 * Read the microsecond timestamp of the most recent semantic Pattern edit.
 *
 * What: returns the TIM2 value captured by the last autosave_markPatternDirty()
 * call, or zero if no edit has occurred since tracking was enabled/discarded.
 * Why: filesystem.c's Pattern drain scheduler computes the quiet-window
 * elapsed time against this value to defer snapshots during rapid editing.
 * Input: the volatile uint32_t maintained in Autosave.c. Output: the raw
 * TIM2 microsecond stamp — use timebase_tim2Delta() for interval computation.
 * Affiliates: filesystem_autosavePatternDrainSchedule_tick().
 */
uint32_t autosave_lastPatternSemanticUs(void);
```

**File (implementation)**: `Core/Bank/Scene/Autosave.c`
**Location**: after `autosave_clearPatternDirty()` (around line 246), before
the `autosave_maskByteOr()` comment block at line 249
**Action**: ADD

```c
uint32_t autosave_lastPatternSemanticUs(void)
{
    return autosave_last_pattern_semantic_us;
}
```

### C15 — Add drain scheduler state to filesystem.c

**File**: `Core/Hardware/SD/filesystem.c`
**Location**: after line 1827 (`fs_pattern_drain_scene`), before line 1828
(`op_file_version`)
**Action**: ADD

```c
/*
 * Pattern AutoSave quiet-window and fairness state.
 *
 * fs_pattern_first_dirty_us: TIM2 timestamp recorded when
 * autosave_patternDirtyMask() first becomes nonzero. Reset to zero when the
 * mask reaches zero (all pending Scenes drained). Why: the max-latency
 * timer computes elapsed time from this value to enforce convergence under
 * sustained editing where the quiet window never elapses. A zero value
 * means no dirty work is pending or the timestamp has not yet been captured.
 *
 * fs_pattern_scene_cursor: rotating index 0..15 for Scene drain fairness.
 * Advanced by one after each successful drain. Why: prevents a continuously
 * edited active Scene from starving other Scenes that became dirty earlier.
 * The cursor wraps modulo SCENE_COUNT and skips Scenes whose bit is not set.
 *
 * Inputs: maintained by filesystem_autosavePatternDrainSchedule_tick().
 * Output: governs when and which Scene the drain fires. Affiliates:
 * autosave_patternDirtyMask(), autosave_lastPatternSemanticUs(), and
 * timebase_tim2Delta(). RAM cost: 5 bytes SRAM1 .bss.
 */
static uint32_t fs_pattern_first_dirty_us;
static uint8_t fs_pattern_scene_cursor;
```

### C16 — Rewrite `filesystem_autosavePatternDrainSchedule_tick()`

**File**: `Core/Hardware/SD/filesystem.c`
**Location**: lines 24306–24364
**Action**: MODIFY

Replace the existing function body to add:
1. First-dirty timestamp capture when mask transitions from zero to nonzero.
2. Max-latency override that fires the drain regardless of quiet window.
3. Quiet-window check against `autosave_lastPatternSemanticUs()`.
4. Rotating cursor for Scene selection fairness.
5. First-dirty timestamp reset when the mask reaches zero after drain.

**Before** (current):
```c
static void filesystem_autosavePatternDrainSchedule_tick(void)
{
    uint16_t mask;
    uint8_t scene;
    uint32_t generation;

    if (!fs_autosave_enabled || !fs_autosave_runtime_ready ||
        !fs_autosave_writer_boot_ready || !bank_hasResidentBank())
        return;
    if (menu_isLoadSaveCommandActive())
        return;
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
    if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
        return;
    if (seq_recordActive || seq_eraseActive)
        return;

    /* Defer Pattern AutoSave until the unified stack service is quiescent. */
    if (!patSvc_idle())
        return;

    mask = autosave_patternDirtyMask();
    if (mask == 0u)
        return;
    for (scene = 0u; scene < SCENE_COUNT && scene < 16u; scene++) {
        if ((mask & (uint16_t)(1u << scene)) != 0u)
            break;
    }
    if (scene >= SCENE_COUNT || scene >= 16u)
        return;

    /* Move the dirty bit into the in-flight ownership boundary before copy. */
    autosave_clearPatternDirty(scene);
    pat_snapshotScene(scene);
    fs_pattern_drain_scene = scene;
    generation = fs_pattern_generation[scene] + 1u;
    if (generation == 0u)
        generation = 1u;
    fs_pattern_generation[scene] = generation;
    if (!filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN,
                          FS_FILE_SETTINGS, 0u,
                          filesystem_autosavePatternDrainCompleted)) {
        autosave_markPatternDirty(scene);
        return;
    }
    op_pattern_scene = scene;
    filesystem_patternAutosaveFilename(op_pattern_filename, scene, generation);
}
```

**After**:
```c
/*
 * Schedule one semantic Pattern AutoSave drain with quiet-window coalescing.
 *
 * What: defers the snapshot until AUTOSAVE_PATTERN_QUIET_WINDOW_MS elapses
 * after the most recent semantic Pattern mutation, preventing unnecessary
 * intermediate PAT4 generations during rapid editing bursts. A hard ceiling
 * (AUTOSAVE_PATTERN_MAX_LATENCY_MS) forces the drain under sustained editing
 * to ensure convergence. A rotating scene cursor provides fairness when
 * multiple Scenes are dirty simultaneously.
 *
 * Inputs: autosave_patternDirtyMask() (pending Scene bits),
 * autosave_lastPatternSemanticUs() (TIM2 timestamp of last edit),
 * fs_pattern_first_dirty_us (TIM2 timestamp when mask first became nonzero),
 * fs_pattern_scene_cursor (fairness rotation). Output: at most one Scene is
 * snapshotted and queued for writing per call. Why: the quiet window
 * coalesces rapid edits into one generation, reducing both CPU and SD wear;
 * the max-latency timer ensures bounded persistence; the rotating cursor
 * prevents the active Scene from starving stale dirty Scenes.
 *
 * Scene-change exit: when the user switches the active Scene, the outgoing
 * Scene's dirty bit is already set from its last edit. No further edits
 * arrive for it, so the quiet window naturally elapses and the drain fires.
 * The service handover ensures all queued mutations are drained before the
 * new Scene becomes the mutation target.
 *
 * Affiliates: autosave_markPatternDirty() (timestamp source),
 * pat_snapshotScene(), filesystem_start(), patSvc_idle().
 * This function does NOT gate non-semantic Pattern drain; that scheduler
 * (filesystem_autosaveNonSemanticPatternDrainSchedule_tick) is independent
 * and operates under its own arm/due-tick debounce.
 */
static void filesystem_autosavePatternDrainSchedule_tick(void)
{
    uint16_t mask;
    uint8_t scene;
    uint8_t i;
    uint32_t generation;
    uint32_t now_us;
    uint32_t elapsed_us;

    if (!fs_autosave_enabled || !fs_autosave_runtime_ready ||
        !fs_autosave_writer_boot_ready || !bank_hasResidentBank())
        return;
    if (menu_isLoadSaveCommandActive())
        return;
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
    if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
        return;
    if (seq_recordActive || seq_eraseActive)
        return;
    if (!patSvc_idle())
        return;

    mask = autosave_patternDirtyMask();
    if (mask == 0u) {
        fs_pattern_first_dirty_us = 0u;
        return;
    }

    /* Capture the first-dirty timestamp on the zero-to-nonzero transition. */
    now_us = timebase_tim2Now();
    if (fs_pattern_first_dirty_us == 0u)
        fs_pattern_first_dirty_us = now_us;

    /* Max-latency override: fire regardless of quiet window. */
    elapsed_us = timebase_tim2Delta(now_us, fs_pattern_first_dirty_us);
    if (elapsed_us < (uint32_t)(AUTOSAVE_PATTERN_MAX_LATENCY_MS * 1000u)) {
        /* Quiet-window check: defer if the last edit is too recent. */
        uint32_t quiet_us = timebase_tim2Delta(
            now_us, autosave_lastPatternSemanticUs());
        if (quiet_us < (uint32_t)(AUTOSAVE_PATTERN_QUIET_WINDOW_MS * 1000u))
            return;
    }

    /* Select the next dirty Scene using the rotating cursor for fairness. */
    scene = SCENE_COUNT;
    for (i = 0u; i < SCENE_COUNT; i++) {
        uint8_t candidate = (uint8_t)(
            (fs_pattern_scene_cursor + i) % SCENE_COUNT);
        if ((mask & (uint16_t)(1u << candidate)) != 0u) {
            scene = candidate;
            break;
        }
    }
    if (scene >= SCENE_COUNT)
        return;

    autosave_clearPatternDirty(scene);
    pat_snapshotScene(scene);
    fs_pattern_drain_scene = scene;
    generation = fs_pattern_generation[scene] + 1u;
    if (generation == 0u)
        generation = 1u;
    fs_pattern_generation[scene] = generation;
    if (!filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN,
                          FS_FILE_SETTINGS, 0u,
                          filesystem_autosavePatternDrainCompleted)) {
        autosave_markPatternDirty(scene);
        return;
    }
    op_pattern_scene = scene;
    filesystem_patternAutosaveFilename(op_pattern_filename, scene, generation);

    /* Advance cursor past this Scene for the next drain. */
    fs_pattern_scene_cursor = (uint8_t)((scene + 1u) % SCENE_COUNT);

    /* If no more dirty Scenes remain, reset the first-dirty timestamp so
     * the next dirty transition gets a fresh measurement window. */
    if (autosave_patternDirtyMask() == 0u)
        fs_pattern_first_dirty_us = 0u;
}
```

---

## Auxiliary changes

### A01 — AutosaveTrace stage enum

**File**: `Core/Bank/Scene/AutosaveTrace.h` (or wherever
`autosave_trace_stage_t` is defined)
**Location**: at end of the enum, before the closing brace
**Action**: ADD

```c
    AUTOSAVE_TRACE_STAGE_DIRTY_COUNT_MISMATCH,
```

**Why**: the DEV-only audit in `autosave_maskHasDirty()` (C06) emits this
stage when the maintained `autosave_dirty_count` diverges from a full scan.
This is a diagnostic-only producer in the existing trace ring and has no
effect on production builds where `DEV_MODE_LOGGING` is 0.

### A02 — Decoder update

**File**: `tools/decode_devlogs.py`
**Location**: in the autosave-trace stage name lookup table
**Action**: ADD

Add the `DIRTY_COUNT_MISMATCH` stage name and a format string that unpacks
the value field as `maintained_count | scan_count`.

### A03 — PatternStackService.c `#include <string.h>` guard

**File**: `Core/Bank/Scene/Pattern/PatternStackService.c`
**Location**: include block at top of file
**Action**: VERIFY / ADD if missing

The optimised `patSvc_countUsed()` (C07) uses `memcpy()`. Verify that
`<string.h>` is already included; add it if not.

---

## RAM / ROM cost summary

| Item | RAM (SRAM1 .bss) | ROM (.rodata / .text) |
|------|------------------|----------------------|
| 3.1 `autosave_dirty_count` | 2 bytes | — |
| 3.1 `popcount8_lut[]` | — | 256 bytes |
| 3.1 code delta | — | ~+40 bytes |
| 3.2 code delta | 0 | ~−20 bytes (shorter loop) |
| 5 `autosave_last_pattern_semantic_us` | 4 bytes | — |
| 5 `fs_pattern_first_dirty_us` | 4 bytes | — |
| 5 `fs_pattern_scene_cursor` | 1 byte | — |
| 5 code delta | — | ~+80 bytes |
| **Total** | **11 bytes** | **~+356 bytes** |

---

## Implementation order

```
Step 1:  C02 (popcount LUT)
Step 2:  C01 (dirty count variable)
Step 3:  C03 (maskByteOr increment)
Step 4:  C04 (maskBitTake decrement)
Step 5:  C05+C13 (discardDirtyMask reset — combined 3.1+5)
Step 6:  C06 + A01 + A02 (maskHasDirty replacement + trace stage)
         ── item 3.1 complete; build + verify ──
Step 7:  C07 + A03 (patSvc_countUsed word-level popcount)
Step 8:  C08 (tick-tail removal)
         ── item 3.2 complete; build + verify ──
Step 9:  C09 (config.h timing defines)
Step 10: C10 (Autosave.c include)
Step 11: C11 (timestamp variable)
Step 12: C12 (markPatternDirty timestamp write)
Step 13: C14 (Autosave.h/c getter)
Step 14: C15 (filesystem.c scheduler state)
Step 15: C16 (drain scheduler rewrite)
         ── item 5 complete; build + verify ──
```

Build verification after each item group: `make clean && make && make img`.

---

## Files touched (summary)

| File | Changes |
|------|---------|
| `Core/Bank/Scene/Autosave.c` | C01, C02, C03, C04, C05+C13, C06, C10, C11, C12, C14 impl |
| `Core/Bank/Scene/Autosave.h` | C14 decl |
| `Core/Bank/Scene/AutosaveTrace.h` | A01 |
| `Core/Bank/Scene/Pattern/PatternStackService.c/.h` | C07, C08, A03 and adjacent service contract |
| `Core/Hardware/SD/filesystem.c/.h` | C15, C16 and adjacent scheduler contract |
| `config.h` | C09 |
| `tools/decode_devlogs.py` | A02 |

Reference updates: `MEMORY.md`, `S069_ATS_PAT_BOUNDED_CLAUDE.md`,
`knowledge_files/specification_reference/AUTOSAVE.md`,
`knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md`, and
`knowledge_files/specification_reference/SRAM_MANIFEST.md` record the landed
Pass 1 behavior, allocation, and verification status.
