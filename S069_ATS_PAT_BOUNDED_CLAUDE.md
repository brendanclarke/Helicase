# S069 Bounded-CPU remaining items — implementation plan

Parent plan: `S069_ATS_PAT_BOUNDED_CPU.md` (6 items + AutoSave OFF-to-ON retest).

## Status summary

| Item | Description | Status |
|------|-------------|--------|
| 1 | Non-semantic relocation split | **DONE** — S069_NON_SEMANTIC_PAT_MAINT_RESTRICTION_CLAUDE.md |
| 2 | Owned slack + reactive compaction | **DONE** — S069_SLACK_REACTIVE_COMPACTION_CLAUDE.md |
| 3.1 | Scalar dirty predicate | **DONE** — source/build verified; hardware pending |
| 3.2 | Pattern logical occupancy (tick-tail removal) | **DONE** — source/build verified; hardware pending |
| 4A | Budget primitive + repair gating | Not started |
| 4B | Scalar AutoSave drain budgeting | Not started |
| 4C | Pattern AutoSave drain budgeting | Not started |
| 5 | Pattern AutoSave quiet window + max latency | **DONE** — source/build verified; hardware pending |
| 6 | Snapshot measurement | Not started (blocked on 1-5) |

Items 1 and 2 are hardware-verified.

### Excluded from this plan

- **Item 4D (OFF-to-ON seeding cursor)**: dropped. The synchronous
  `autosave_markResidentBankDirty()` seed is infrequent, bounded by resident
  Bank scope, and does not repeat. The risk of a new lifecycle state machine
  (retained cursor, OFF-during-seed handling, writer admission hold) outweighs
  the benefit given that items 4A-4C already bound steady-state CPU. If the
  synchronous burst proves problematic after 4A-4C land, it can be revisited
  as a standalone item.

- **AutoSave OFF-to-ON retest**: deferred to a separate session. The re-enable
  path does not currently re-engage and will be investigated independently after
  this plan converges.

- **`fs_autosave_setup_failed` retry**: deferred. Separate media-error policy
  work.

- **Item 6 chunking**: discouraged. Measurement only. The 10,519-byte memcpy
  is likely <30µs and not a meaningful peak. No coherence protocol or revision
  counter will be implemented unless future measurement data demands it.

## Implementation passes

```
Pass 1 ── 3.1, 3.2, 5 ── implement and hardware test
Pass 2 ── 4A, 4B, 4C ── implement and hardware test
Exit  ── 6 (measurement), final verification
```

---

## Pass 1

### Item 3.1 — Scalar dirty predicate (exact dirty-bit count)

#### Current state

`autosave_maskHasDirty()` at Autosave.c:1934 scans all 3,856 volatile mask bytes
sequentially until it finds a nonzero byte or exhausts the array. In steady-state
clean (the common case), this touches every byte — about 29.3M byte inspections
per second at the ~7,600 calls/s filesystem scheduler rate.

The mask modification primitives:

- `autosave_maskByteOr()` at Autosave.c:257 — atomic OR under PRIMASK. Every
  dirty bit enters through this function (per-parameter markers, recovery merge,
  failure rollback).
- `autosave_maskBitTake()` at Autosave.c:2015 — atomic test-and-clear under
  PRIMASK. Every consumed dirty bit exits through this function (phase 56
  classification).
- `autosave_discardDirtyMask()` at Autosave.c:1356 — memset to zero. Called
  only at safe boundaries (OFF transition, session reset).
- `autosave_maskMergeChunk()` at Autosave.c:1920 — loops calling
  `autosave_maskByteOr()`. Recovery merge path.
- `autosave_maskRestoreCaptured()` at Autosave.c:2043 — loops calling
  `autosave_maskByteOr()`. Writer failure rollback.

All dirty-bit production and consumption funnels through `autosave_maskByteOr()`
and `autosave_maskBitTake()`, making them the sole increment/decrement sites.

#### Proposed change

Add `static volatile uint16_t autosave_dirty_count;` beside `autosave_dirty_mask[]`.
Maximum value is 30,848 (3,856 bytes × 8 bits), well within `uint16_t` range.

In `autosave_maskByteOr()`:
- Read old byte before OR. Compute newly-set bits:
  `uint8_t fresh = bits & (uint8_t)~old;`
- Increment `autosave_dirty_count` by `popcount8(fresh)` using a 256-byte ROM
  lookup table. The entire read-modify sequence is already under PRIMASK, so the
  count update is atomic with the mask update. The LUT lookup adds ~4 cycles to
  the critical section.

In `autosave_maskBitTake()`:
- Decrement `autosave_dirty_count` when `was_set` is nonzero. Already under
  PRIMASK.

In `autosave_discardDirtyMask()`:
- Set `autosave_dirty_count = 0u` alongside the memset.

Replace `autosave_maskHasDirty()` body with `return autosave_dirty_count != 0u;`.

#### RAM/ROM cost

2 bytes SRAM (one `uint16_t`). 256 bytes ROM (popcount LUT, `static const`).

#### Risk assessment

**Low risk.**

- The atomic invariant is simple: count tracks the number of set bits in the
  mask array. Both increment and decrement sites are already under PRIMASK
  critical sections, so no new concurrency hazard is introduced.
- `autosave_maskRestoreCaptured()` calls `autosave_maskByteOr()` — the fresh-bit
  logic correctly handles re-ORing already-set bits (they contribute zero to the
  popcount delta).
- Recovery merge via `autosave_maskMergeChunk()` also goes through
  `autosave_maskByteOr()` — correct.
- `autosave_objectFullyCaptured()` is a scoped per-object check and remains
  unchanged; it does not use the global count.
- A DEV-only periodic full-scan assertion (e.g., every 1,000th tick, not every
  pass) catches implementation drift during development.

#### Acceptance

- `autosave_maskHasDirty()` returns in constant time.
- DEV assertion confirms count == full-scan popcount at low cadence.
- No change to any file format, CRC, or recovery behavior.

---

### Item 3.2 — Pattern logical occupancy (tick-tail removal)

#### Current state

`logical_chunks_used` exists as `static uint16_t` at PatternStackService.c:106.
It is maintained by calling `patSvc_countUsed()` (a full 2,048-bit scan,
PatternStackService.c:487) at every consumption site:

| Call site | Line | Frequency |
|-----------|------|-----------|
| Tick tail (idle) | 1556 | Every tick (~500/s when idle) |
| `patSvc_drainQueue` | 1063 | After each queued event drain |
| `patSvc_submit` direct | 1118 | After each direct mutation |
| `patSvc_drainBulk` completion | 1026 | After bulk barrier completes |
| `patSvc_clearTrack` direct | 1382 | After direct track clear |
| `patSvc_clearPattern` direct | 1407 | Sets to 0 (already optimal) |
| `patSvc_init` | 1202 | Once at boot |
| `patSvc_finishSceneReplace` | 1249 | After filesystem Scene replace |
| Handover tail | 1441 | After target handover |
| Scene-match recheck | 1518 | After scene re-match |

The idle tick-tail recount (line 1556) produces ~1,024,000 bitmap tests per
second with zero information gain when the pool hasn't changed. This is the
dominant idle-state cost.

#### Proposed change

**Remove only the tick-tail recount. Keep all mutation-path recounts.**

This is the high-benefit, non-fragile approach. The tick-tail recount at line
1556 runs every idle tick (~500/s) and is the overwhelming source of unnecessary
work. The mutation-path recounts are infrequent (bounded by edit rate) and
negligible at typical human interaction speeds.

Change at PatternStackService.c:1556: remove the `patSvc_countUsed()` call.
The `logical_chunks_used` value remains current from the most recent mutation or
reconciliation. The `patSvc_updateDensityLevel()` call at line 1557 continues to
use the last-known value, which is correct because no mutation has occurred since
the last recount.

All mutation paths (drainQueue, submit, drainBulk, clearTrack, clearPattern,
init, finishSceneReplace, handover) retain their existing full recounts. These
are bounded by edit rate and cover the `pat_tryAppendAutomation()` cross-module
case naturally — the next mutation-path recount after an in-place append
reconciles the count.

**Note**: the cross-module `patSvc_notifyChunkConsumed()` API entry described in
the parent plan is not needed with this approach. The full recounts at mutation
boundaries already capture any chunk consumed by `pat_tryAppendAutomation()`. If
the incremental delta approach is ever revisited, the dedicated API entry should
be added at that time.

#### Optimized reconciliation

The parent plan recommends popcounting the 256-byte backed bitmap as 64
unaligned-safe 32-bit words rather than the current per-bit loop. Apply this to
`patSvc_countUsed()` to reduce the cost of the mutation-path recounts that
remain. Each 32-bit word popcount uses the standard three-step bitmask technique
(~12 cycles/word vs. ~32 cycles for 32 individual bit tests), reducing a 2,048-
bit scan from ~2,048 cycles to ~768 cycles.

#### RAM cost

Zero. No new variables. The optimized `patSvc_countUsed()` uses no additional
storage.

#### Risk assessment

**Low risk.**

The only change is removing one call site. The value `logical_chunks_used` may be
stale by one in-place append (if `pat_tryAppendAutomation()` consumed a reserved
chunk since the last mutation recount), but this is at most a one-chunk density
classification error that self-corrects on the next mutation. No file format,
pool behavior, or trace output changes.

#### Acceptance

- `patSvc_countUsed()` is not called from the tick tail.
- `patSvc_countUsed()` uses word-level popcount for remaining call sites.
- No change to pool behavior, trace output, or file formats.
- DEV assertion (optional): compare `logical_chunks_used` against a full recount
  at low cadence (~every 2,000 ticks) to confirm no drift.

---

### Item 5 — Pattern AutoSave quiet window and maximum latency

#### Current state

`filesystem_autosavePatternDrainSchedule_tick()` at filesystem.c:24306 checks
`autosave_patternDirtyMask()`, picks the lowest dirty Scene, clears its bit, and
immediately snapshots. There is no quiet window and no maximum latency timer.

A burst of rapid semantic edits (e.g., automation recording, fast knob turns)
triggers a snapshot as soon as `patSvc_idle()` returns true after each edit,
producing unnecessary intermediate Pattern generations that are immediately stale.

#### Proposed change

Add two config.h controls:

```c
#define PATTERN_AUTOSAVE_QUIET_MS        250u
#define PATTERN_AUTOSAVE_MAX_LATENCY_MS 5000u
```

State (in filesystem.c, alongside the existing drain scheduler state):
- `uint32_t pat_autosave_last_semantic_us` — global timestamp of the most recent
  semantic Pattern mutation. Reset by `autosave_markPatternDirty()`.
- `uint32_t pat_autosave_first_dirty_us` — timestamp when
  `autosave_patternDirtyMask()` first became nonzero. Reset when mask reaches
  zero.
- `uint8_t pat_autosave_scene_cursor` — rotating cursor for fairness.

**Global timestamp rationale**: only one Pattern Scene can be the active mutation
target at a time (`service_scene` in PatternStackService.c), so quiet is a global
property. When the user edits Scene A, only Scene A's dirty bit is set and the
global timestamp reflects Scene A's edits. When the user switches to Scene B and
edits it, Scene A's dirty bit remains set (its quiet window has already elapsed),
and the global timestamp now reflects Scene B's edits.

In `filesystem_autosavePatternDrainSchedule_tick()`:
1. If mask is nonzero and `first_dirty_us` is zero, record now.
2. Compute `max_elapsed = timebase_tim2Delta(now, first_dirty_us)`. If
   ≥ `MAX_LATENCY_MS * 1000u`, proceed to drain the cursor-selected Scene.
3. Otherwise, check `timebase_tim2Delta(now, last_semantic_us)
   >= QUIET_MS * 1000u`. If met, proceed. If not, defer.
4. On successful drain, advance the cursor. When all dirty bits are drained and
   mask becomes zero, reset `first_dirty_us` to zero.

The rotating cursor advances on each successful drain. This ensures that a
continuously edited active Scene cannot starve other Scenes that became dirty
earlier.

**Scene-change exit conditions**: when the user changes the active Scene, the
outgoing Scene's dirty mask bit is already set (from its last semantic edit).
The quiet window timer reflects the outgoing Scene's last edit timestamp. Since
no further edits arrive for the outgoing Scene, the quiet window naturally
elapses and the drain fires. The service handover ensures all queued mutations
are drained before the new Scene becomes the mutation target. Non-semantic
mutations are deferred until the new Scene's repair epoch starts.

The quiet window does **not** apply to non-semantic Pattern drain
(`filesystem_autosaveNonSemanticPatternDrainSchedule_tick()`). Maintenance work
runs only when no other operations are in-flight, is bounded by the CPU budget
(Pass 2), and is allowed to consume its assigned CPU slack while higher-priority
background operations are idle.

#### Timestamp source

`autosave_markPatternDirty()` (Autosave.c:145) is the single semantic mutation
funnel. Add a `timebase_tim2Now()` call there to record the global last-semantic
timestamp. This function is already under PRIMASK and reachable from ISR context;
`timebase_tim2Now()` is safe from any context.

The filesystem scheduler reads the timestamp on each pass. No additional
synchronization is needed — the filesystem scheduler runs from foreground context
and the timestamp is a single 32-bit word (atomic read on ARM Cortex-M).

#### RAM cost

9 bytes total: two `uint32_t` timestamps + one `uint8_t` cursor.

#### Risk assessment

**Low risk.**

The mechanism is straightforward timer-based deferral. The quiet window never
drops or discards work — it only delays the snapshot until edits pause. The max
latency timer ensures convergence even under sustained editing.

- **Interaction with `patSvc_idle()`**: the drain already gates on service
  quiescence. The quiet window adds an additional deferral. During sustained
  editing where the service is rarely idle AND the quiet window keeps resetting,
  the max latency timer takes precedence.
- **First-dirty-us tracking**: handles the mask becoming zero (all Scenes
  drained) and then becoming nonzero again correctly by resetting on zero.

#### Acceptance

- A rapid sequence of Pattern edits defers the snapshot until 250ms of quiet.
- Under sustained editing, the snapshot fires at 5000ms regardless.
- Scene switch preserves the outgoing Scene's dirty state and drains it after
  the quiet window elapses.
- Non-semantic drain is not affected by the quiet window.
- No Scene is starved by the active Scene's continuous edits.

---

### Pass 1 test

After implementing 3.1, 3.2, and 5:

1. Verify `autosave_maskHasDirty()` returns constant-time in clean steady state.
   CPU monitor should show measurable idle reduction compared to pre-pass
   baseline.
2. Verify `patSvc_countUsed()` is not called from the tick tail. Pattern trace
   output should remain identical for equivalent editing sequences.
3. Verify Pattern AutoSave quiet window: rapid edits should produce fewer
   intermediate PAT4 generations. A single edit followed by 250ms of inactivity
   should produce exactly one generation. Sustained editing should produce a
   generation at 5000ms intervals.
4. Verify scene change drains the outgoing Scene's Pattern correctly.
5. Verify DEV assertions (dirty count, optional occupancy) show no mismatch.

---

## Pass 2

### Item 4A — Budget primitive + repair epoch gating

#### Proposed change

Add to config.h:

```c
#define BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING  25u
#define BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED  50u
```

2.5% allowance during playback, 5% while stopped. Zero means unlimited (for
debugging). The refill rate switches based on the sequencer's running state.

Budget state (in filesystem.c):

```c
static uint32_t budget_last_refill_us;
static int32_t  budget_credit_us;
```

Public API:

```c
uint8_t filesystem_backgroundBudgetAvailable(void);
void    filesystem_backgroundBudgetCharge(uint32_t start_us);
void    filesystem_backgroundBudgetRefill(void);
```

Budget lives in filesystem.c because that is where the scheduler ladder and the
majority of budgeted work live. PatternStackService.c calls the public query/
charge API for repair epoch gating. A future refactor may extract the budget into
its own module if filesystem.c continues to grow (see SCOPING_TARGETS.md).

Refill logic:
- On each call to `filesystem_backgroundBudgetRefill()` (called at the top of
  the filesystem scheduler main loop):
  - Compute elapsed milliseconds since last refill.
  - Add `elapsed_ms * rate` to credit, where rate is
    `BACKGROUND_CPU_BUDGET_US_PER_MS_PLAYING` if the sequencer is running,
    `BACKGROUND_CPU_BUDGET_US_PER_MS_STOPPED` otherwise.
  - Cap positive credit at one millisecond's allowance (prevents idle
    accumulation).

Gate the repair epoch in `patSvc_tick()`: before the repair while-loop at
PatternStackService.c:1570, check `filesystem_backgroundBudgetAvailable()`. If
exhausted, skip repair this tick. After each repair step that does work, call
`filesystem_backgroundBudgetCharge(start_us)`.

#### RAM cost

8 bytes (two 32-bit words).

#### Risk assessment

**Low risk.** The repair scan is already bounded by entry count. Adding a time
gate is a single check with no side effects. If the budget is exhausted, repair
sleeps until the next refill — identical to the cursor reaching
`PATSVC_ADDRESS_COUNT`.

#### Acceptance

- Repair epoch respects the budget and does not exceed ~2.5% CPU during playback.
- Repair still converges (reaches sleep) within a reasonable number of passes.

---

### Item 4B — Scalar AutoSave drain phase budgeting

#### Proposed change

Gate the classification loop in filesystem.c phase 56 (the `CLASSIFY/CAPTURE`
bounded scan) with `filesystem_backgroundBudgetAvailable()`. Check the budget
before each iteration of the existing `examined` loop. If exhausted, yield the
phase — return from the phase handler with the current scan offset retained for
the next pass (the existing partial-progress mechanism already supports this).

Similarly, gate the CRC/transform phases that do per-byte work over the full
payload.

Must NOT gate:
- Phase completion (the state transition after all work is classified/captured)
- File sync, close, or flush boundaries
- Error recovery or file rollback paths
- Any boundary where the file facade is already committed to an operation

After each budgeted work slice, call
`filesystem_backgroundBudgetCharge(start_us)`.

#### Risk assessment

**Medium risk.** The scalar drain state machine is large (~800 lines, phases
50-61). The classification loop (phase 56) is the primary target and already has
a per-iteration bound (`examined` count), so adding a time-based gate is
structurally similar to what exists. Care needed to ensure a budget-deferred
classification does not hold the file facade indefinitely — but the existing
`examined` limit already creates multi-pass behavior, so this is a proven
pattern.

#### Acceptance

- Scalar drain classification respects the budget.
- Save latency increases measurably but remains bounded by the scalar payload
  size and the budget rate.
- No change to file format, CRC, or A/B generation protocol.

---

### Item 4C — Pattern AutoSave drain phase budgeting

#### Proposed change

Gate the staging/CRC work at the start of each 512-byte write chunk with
`filesystem_backgroundBudgetAvailable()`. If exhausted, defer the chunk to the
next scheduler pass.

Must NOT gate:
- The `pat_snapshotScene()` copy itself (must be atomic once started)
- The completed transaction boundary
- Any sync/close/error path
- AsyncFATFS/SD polling needed to advance an admitted write

After each budgeted staging/CRC slice, call
`filesystem_backgroundBudgetCharge(start_us)`.

#### Risk assessment

**Medium risk.** Similar to 4B. The Pattern drain writes 512-byte chunks.
Deferring one chunk's staging start does not change the file protocol — the write
simply arrives at the next scheduler pass.

#### Acceptance

- Pattern drain staging/CRC respects the budget.
- Pattern AutoSave converges to a complete PAT4 file under the budget.
- No change to file format, CRC, or PAT4 generation protocol.

---

### Trace requirements for Pass 2

Add diagnostic trace records sufficient to prove the budget system is working as
intended. The specific trace format (stage code, bit packing, per-class vs.
aggregate) is implementation-discretionary — what matters is that the
`tools/decode_devlogs.py` decoder can report:

1. Total charged microseconds per work class (repair, scalar drain, Pattern
   drain) over a time window.
2. Number of denied (budget-exhausted) slices per work class.
3. Maximum single-slice duration.

These records provide the evidence that the configured CPU allowance is binding
and that no single slice is unexpectedly large. Existing PatternTrace and
AutoSaveTrace records remain unchanged.

---

### Pass 2 test

After implementing 4A, 4B, 4C:

1. Verify CPU monitor shows background CPU usage bounded to ~2.5% during
   playback and ~5% while stopped.
2. Verify trace shows budget enforcement: denied slices when work exceeds the
   allowance, charged microseconds converging to the configured rate.
3. Verify save latency is bounded and acceptable. The quiet window (250ms) plus
   budget-limited drain should produce a measurable but finite total latency
   from edit to durable save.
4. Verify audio queue pressure and underrun counts do not increase.
5. Verify repair epoch still converges (reaches sleep) within a reasonable
   timeframe.
6. Verify no regression in Pass 1 behaviors.

---

## Exit verification

### Item 6 — Snapshot measurement (measurement only)

Instrument `pat_snapshotScene()` (PatternData.c:96) with `timebase_tim2Now()`
before and after the `memcpy()`. Record elapsed microseconds in a PatternTrace
record or DEV variable. Capture max and typical duration over several minutes
of active use with the full system running (items 1-5 applied).

Expected result: the 10,519-byte `memcpy()` takes <30µs on the STM32F765 at
216 MHz. This is not expected to be a meaningful peak. No chunking will be
implemented.

If the snapshot duration is unexpectedly large, investigate bus contention or
alignment issues rather than adding a chunked copy protocol.

### Final exit criteria

1. Clean AutoSave ON is statistically indistinguishable from OFF except for
   constant-time gates and low-rate trace/budget work.
2. After one finite repair epoch, an idle Pattern produces zero further
   relocations and zero new PAT4 generations.
3. No repeated `M/R` chain moves the same block back and forth (already verified
   by items 1-2).
4. Dense multi-track writes produce no queue/capacity/fragmentation drops while
   reclaimable space exists.
5. CPU monitor shows bounded background CPU usage during playback.
6. Power interruption still leaves one valid scalar A/B generation and one valid
   Pattern A/B generation for every affected Scene.

---

## Resolved decisions

These questions from the initial plan draft are now closed:

1. **Popcount implementation (item 3.1)**: 256-byte ROM LUT. ~4 cycles per
   lookup inside the existing PRIMASK section.

2. **Cross-module notification (item 3.2)**: moot. The simplified tick-tail-
   removal approach retains full recounts at mutation boundaries, which naturally
   catch any chunk consumed by `pat_tryAppendAutomation()`. No
   `patSvc_notifyChunkConsumed()` API entry is needed. If the incremental delta
   approach is ever revisited, add a dedicated public function at that time.

3. **Budget primitive ownership (item 4)**: filesystem.c, with a public query/
   charge API for PatternStackService.c. See SCOPING_TARGETS.md note about
   potential future extraction into a dedicated module.

4. **Per-Scene vs. global quiet window (item 5)**: global. Only one Pattern can
   be edited at a time, so quiet is a global property. Scene-change exit
   conditions are handled by the service handover mechanism plus natural quiet
   window elapsed time.

5. **Non-semantic drain quiet window (item 5)**: no. Maintenance is bounded by
   CPU budget and runs only when no higher-priority work is pending. It is
   allowed to consume its assigned CPU slack.

6. **Budget value (item 4)**: 2.5% during playback (25µs/ms), 5% while stopped
   (50µs/ms). No A/B testing needed. Trace provides evidence.

7. **`fs_autosave_setup_failed` (OFF-to-ON)**: deferred to a separate session.

8. **Trace format (item 4)**: implementation-discretionary. Must be decodable
   and prove the system works. Details settled during implementation.

9. **Snapshot chunking (item 6)**: discouraged. Measurement only.

10. **Item 3.2 approach**: tick-tail removal only (non-fragile, high-benefit).
    Full incremental delta approach not implemented.

---

## Pass 1 implementation review

Implementation schedule: `S069_ATS_PAT_BOUNDED_PASS1_IMPLEMENT.md`.
Build: `text=449,476`, `data=404`, `bss=291,724`.

### Item 3.1 — verified

- `popcount8_lut[256]` added at Autosave.c:64 as `static const` ROM. Values
  verified correct (0 through 8 for all 256 entries).
- `autosave_dirty_count` declared as `static volatile uint16_t` at :97,
  placed immediately after `autosave_dirty_mask[]`.
- `autosave_maskByteOr()`: reads old byte, computes
  `fresh = bits & (uint8_t)~old`, increments count by `popcount8_lut[fresh]`.
  All inside the existing PRIMASK section. Recovery merge and rollback re-OR
  paths produce zero fresh-bit delta for already-set bits — idempotent as
  required.
- `autosave_maskBitTake()`: decrements count by 1 when `was_set`, inside the
  existing PRIMASK section. Balances the OR-side increment.
- `autosave_discardDirtyMask()`: zeroes `autosave_dirty_count` alongside the
  memset and Pattern mask clears.
- `autosave_maskHasDirty()`: body replaced with
  `return (uint8_t)(autosave_dirty_count != 0u)`. O(1).
- DEV audit (gated by `DEV_MODE_LOGGING`): every 1,000th call runs a full
  popcount of all 3,856 mask bytes under a coherent PRIMASK snapshot and
  compares against the maintained count. On mismatch, emits trace stage `Z`
  with maintained and scanned counts packed into the value field. The PRIMASK
  coherence is an improvement over the schedule — prevents a concurrent
  ISR mutation from causing a false mismatch during the scan.
- AutosaveTrace.h: `AUTOSAVE_TRACE_STAGE_DIRTY_COUNT_MISMATCH = 'Z'` added
  with descriptive comment.
- `tools/decode_devlogs.py`: `Z` stage decoder added — extracts
  `maintained_count` (bits 0..15) and `full_scan_count` (bits 16..31).
- Autosave.h: comment block for the mask API section updated to note the
  constant-time count-based test.

No change to any file format, CRC, wire mask, or recovery behavior.

### Item 3.2 — verified

- `patSvc_countUsed()`: rewritten from 2,048-iteration per-bit loop to
  64-iteration word-level popcount: `memcpy` + `__builtin_popcount` per
  `uint32_t`. Matches the established `pat_poolUsagePercent()` precedent in
  PatternData.c. `PATSVC_POOL_CHUNKS / 32u` = 64 words = 256 bytes, covering
  exactly the used portion of the 512-byte bitmap.
- Tick-tail removal: only the `patSvc_countUsed()` call at the former
  line 1556 is removed. `patSvc_sampleRepairBudget()` and
  `patSvc_updateDensityLevel()` are retained and use the cached
  `logical_chunks_used` from the most recent mutation-boundary recount.
- All 10 mutation-boundary call sites are untouched: drainQueue (success and
  failure classification), submit, drainBulk, clearTrack, clearPattern
  (assigns 0 directly), init, finishSceneReplace,
  removeTrackAutomationByTarget, and scene-match recheck.
- PatternStackService.h: `patSvc_tick()` comment updated to document the
  cached-occupancy rationale.

No change to pool behavior, allocation, trace output, or file formats.

### Item 5 — verified

- config.h: `AUTOSAVE_PATTERN_QUIET_WINDOW_MS 250u` and
  `AUTOSAVE_PATTERN_MAX_LATENCY_MS 5000u` added after the existing
  AUTOSAVE block, with descriptive comment documenting inputs, outputs, and
  the non-semantic exclusion.
- Autosave.c: `#include "timebase.h"` added for `timebase_tim2Now()`.
- `autosave_last_pattern_semantic_us` declared as `static volatile uint32_t`
  at :124, placed after `autosave_pattern_dirty_mask`.
- `autosave_markPatternDirty()`: records `timebase_tim2Now()` inside the
  existing PRIMASK section, between the mask-bit set and the HCNAMES witness
  clear. Safe from ISR context.
- `autosave_discardDirtyMask()`: resets
  `autosave_last_pattern_semantic_us = 0u` alongside the other clears.
- `autosave_lastPatternSemanticUs()`: public getter in Autosave.h (with
  comment) and Autosave.c. Returns the raw TIM2 value.
- filesystem.c: `fs_pattern_first_dirty_us` (uint32_t) and
  `fs_pattern_scene_cursor` (uint8_t) added alongside the existing Pattern
  drain state.
- `filesystem_autosavePatternDrainSchedule_tick()` rewritten:
  - Mask-zero check and epoch reset (`fs_pattern_first_dirty_us = 0u`) happen
    before the policy/card gates, so the epoch resets correctly even when
    gates suppress the scheduler.
  - First-dirty timestamp capture happens after gates — the 5s max-latency
    ceiling starts from when the scheduler is actually eligible to fire, not
    from when gates blocked it.
  - Max-latency override: if `elapsed_us >= MAX_LATENCY_MS * 1000u`, the
    quiet-window check is bypassed entirely.
  - Quiet-window check: if `timebase_tim2Delta(now, last_semantic_us) <
    QUIET_WINDOW_MS * 1000u`, the function defers.
  - Rotating cursor: iterates from `fs_pattern_scene_cursor` modulo
    `scene_count`, selects the first dirty candidate. Advances past the
    drained Scene after successful drain.
  - Post-drain epoch reset: if `autosave_patternDirtyMask() == 0u` after
    clearing the drained Scene's bit, `fs_pattern_first_dirty_us` resets to
    zero for a fresh measurement window on the next dirty transition.
- filesystem.h: `filesystem_tick()` comment updated to note the quiet-window
  and max-latency coalescing.
- Non-semantic drain (`filesystem_autosaveNonSemanticPatternDrainSchedule_tick`)
  is completely unchanged — it retains its independent arm/due-tick debounce
  and is not affected by the quiet window.

No change to PAT4 file format, CRC, A/B generation protocol, or
non-semantic scheduling.

### RAM / ROM cost

| Component | RAM | ROM |
|-----------|-----|-----|
| `autosave_dirty_count` | 2 B | — |
| `popcount8_lut[256]` | — | 256 B |
| `autosave_last_pattern_semantic_us` | 4 B | — |
| `fs_pattern_first_dirty_us` | 4 B | — |
| `fs_pattern_scene_cursor` | 1 B | — |
| **Total** | **11 B** | **256 B + code delta** |

### Pass 1 hardware test — 2026-09-20

**PASS.** No operation problems observed. Items 3.1, 3.2, and 5 are
hardware-accepted.

Observation: load/save menu responsiveness during playback is noticeably
improved compared to the pre-pass firmware.

**Investigation**: all five filesystem.c AutoSave/trace schedulers correctly
suppress themselves when the Load/Save menu is active (`LOAD_PAGE`/
`SAVE_PAGE` page check and `menu_isLoadSaveCommandActive()` check). However,
`patSvc_tick()` — called unconditionally at 500 Hz from timebase.c — has
**no** Load/Save menu gate. Its repair while-loop runs every idle tick
regardless of menu state. Queue drain and handover also run, but those are
bounded by edit rate and correct. The repair epoch is the remaining CPU
consumer during Load/Save browsing. Noted in `SCOPING_TARGETS.md` § Session
069 deferred items; to be coordinated with Pass 2's budget primitive (4A).
