# Session 069 — Pattern Stack Service Bounded CPU Convergence

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Date**: 2026-09-20
**Branch**: `dev-ph5-effects` at commit `9627f70`
**Previous session**: [068_SESSION_HANDOFF_LOG.md](068_SESSION_HANDOFF_LOG.md)

---

## End of session block

```
DATE: 2026-09-20
SESSION GOAL: Implement the bounded-CPU convergence plan for Pattern Stack
Service maintenance and AutoSave scheduling — decouple physical pool
relocation from semantic dirtiness, replace perpetual Tier 1/Tier 2
defragmentation with owned trailing-slack repair and reactive-only
compaction, make scalar dirty detection O(1), add Pattern AutoSave quiet
window and max latency scheduling, and gate all background work under a
shared elapsed-time CPU budget.

COMPLETED: All seven phases implemented, source/build verified, all
hardware-accepted. Pass 2 (CPU budget, Load/Save repair gate) validated
2026-09-22.

  Phase 1: Non-semantic Pattern maintenance restriction
  Phase 2: Owned trailing-slack reservation with density hysteresis
  Phase 3: Reactive-only compaction (periodic Tier 2 deleted)
  Phase 4: Finite bounded repair epoch
  Phase 5: O(1) scalar dirty predicate via maintained population count
  Phase 6: Pattern AutoSave quiet window and max latency scheduling
  Phase 7: Background elapsed-time CPU budget with Load/Save repair gate

VERIFIED ON HARDWARE:
  Non-semantic restriction: PASS
  Slack/reactive compaction: PASS
  Pass 1 (O(1) dirty, quiet window, popcount): PASS (2026-09-20)
  Pass 2 (CPU budget, Load/Save gate): PASS (2026-09-22)

CHANGES THIS SESSION:
- PatternStackService.c: reservation image, repair epoch rewrite, reactive
  compaction, density hysteresis, popcount optimization, idle recount
  removed, direct-path parity fix, Load/Save repair gate, budget query
- PatternStackService.h: patSvc_isChunkReserved/consumeReservation decls
- PatternData.c: reservation consumption in pat_tryAppendAutomation,
  pat_markPoolMutationDirty retired from service path
- Autosave.c: non-semantic dirty mask, dirty count, popcount8 LUT,
  maskByteOr popcount increment, maskBitTake decrement, maskHasDirty O(1),
  DEV Z audit, last_pattern_semantic_us timestamp
- Autosave.h: new declarations, quiet window/max latency constants
- filesystem.c: non-semantic scheduler, quiet window/max latency scheduling,
  rotating Scene cursor, budget primitive, budget integration, H trace
- PatternTrace.h: V/L/D stage codes
- AutosaveTrace.h: Z/H stage codes
- config.h: repair scan idle/busy, reservation thresholds, quiet window/
  max latency constants

KNOWN ISSUES INTRODUCED: None identified.
KNOWN ISSUES RESOLVED:
- Pattern maintenance CPU churn at idle (Tier 1/Tier 2 self-chasing loop)
- Layout-only pool relocations triggering semantic AutoSave writes
- O(n) dirty-mask full scan on every scheduler tick
- Pattern AutoSave writing immediately on every mutation (no coalescing)
- No CPU budget for background work during playback

NEXT SESSION RECOMMENDED GOAL: S070 general systems check and review
before Phase 5 Effects work. Item 6 (snapshot measurement of
pat_snapshotScene()) is not started; blocked on Pass 2 completion, now
unblocked.

BLOCKERS: None. S069 is fully hardware-validated.

CRITICAL REMINDERS FOR NEXT SESSION:
- Pass 2 hardware validation is still pending — test CPU budget behavior
  during playback and verify Load/Save menu responsiveness with repair gate
- The reservation image is NOT persisted in PAT4 — it is rebuilt lazily
  after every init/handover/filesystem replacement
- autosave_dirty_count must stay in sync with the mask — any new mask
  mutation path must use autosave_maskByteOr/autosave_maskBitTake, never
  raw bit manipulation
- Non-semantic scheduler is lowest priority — it must never preempt
  semantic Pattern AutoSave, scalar AutoSave, trace flush, or settings
- Background budget module is in filesystem.c — extract to BackgroundBudget.c
  if filesystem.c grows further
```

---

## §1 Session goal and context

Session 069 implemented the bounded-CPU convergence plan described in
`S069_ATS_PAT_BOUNDED_CLAUDE.md`. The motivation was the Session 068
finding that Pattern Stack Service maintenance and AutoSave scheduling
manufactured continuous CPU work and Pattern file churn even at idle:

1. Tier 2 proactive compaction removed the trailing gap that Tier 1 just
   created, causing a perpetual chase loop.
2. Every successful physical relocation (same bytes, new pool offset) called
   `pat_markPoolMutationDirty()`, turning a layout optimization into a
   spurious semantic Pattern AutoSave trigger.
3. Two O(n) clean-state full-bitmap scans ran on every scheduler tick.
4. Pattern AutoSave admitted immediately on any dirty bit with no
   coalescing.
5. No elapsed-time budget bounded background work during playback.

The implementation was structured as seven phases executed in four
sequential work orders:

| Work order | Plan document | Implementation document |
|------------|---------------|----------------------|
| 1 | `S069_NON_SEMANTIC_PAT_MAINT_RESTRICTION_CLAUDE.md` | `S069_NON_SEMANTIC_MAINT_IMPLEMENTATION.md` (7 steps) |
| 2 | `S069_SLACK_REACTIVE_COMPACTION_CLAUDE.md` | `S069_SLACK_REACTIVE_COMPACTION_IMPLEMENTATION.md` (15 steps) |
| 3-4 | `S069_ATS_PAT_BOUNDED_CLAUDE.md` | `S069_ATS_PAT_BOUNDED_PASS1_IMPLEMENT.md` (16+3 changes) |
| 5-7 | `S069_ATS_PAT_BOUNDED_CLAUDE.md` | `S069_ATS_PAT_BOUNDED_PASS2_IMPLEMENT.md` (16+3 changes) |

Two additional plan documents were examined but not used:
- `S069_ATS_PROBLEMS.md`: Diagnosed AutoSave CPU spikes during playback.
  Root cause (perpetual Tier 1/Tier 2 chasing and layout-only dirty
  marking) was addressed by Phases 1-4.
- `S069_ATS_PAT_BOUNDED_CPU.md`: Earlier bounded-CPU draft, superseded by
  the authoritative `S069_ATS_PAT_BOUNDED_CLAUDE.md` master plan.

---

## §2 Non-semantic Pattern maintenance restriction (Phase 1)

### Problem

`pat_markPoolMutationDirty()` was called after every physical pool block
relocation performed by the Pattern Stack Service. This treated layout-only
changes (same semantic content, different pool offset) as semantic Pattern
mutations, triggering Pattern AutoSave to write a new PAT4 generation for
content that had not changed.

### Solution

Decouple physical relocation from semantic AutoSave dirtiness:

1. **Retire `pat_markPoolMutationDirty()` as the service's dirty path.**
   Service relocations now call `autosave_markNonSemanticPatternDirty(scene)`
   instead, writing to a separate 16-bit mask
   (`autosave_nonsemantic_pattern_dirty_mask`) that does not trigger the
   semantic Pattern AutoSave scheduler.

2. **New lowest-priority scheduler rung in `filesystem.c`.** The
   non-semantic Pattern scheduler runs only after all higher-priority work
   (settings, trace flush, scalar AutoSave, semantic Pattern AutoSave)
   declines. It uses its own arm/due-tick debounce
   (`fs_nonsemantic_pattern_arm_flag`, `fs_nonsemantic_pattern_due_tick`)
   and selects a non-active-first Scene ordering to minimize interference
   with user editing.

3. **Semantic mutations unchanged.** All user-facing Pattern edits
   (step toggle, note/velocity/probability change, automation add/remove,
   track/pattern clear) continue through the existing
   `autosave_markPatternDirty()` path.

### RAM

5 bytes SRAM1 `.bss`: 2-byte mask + 1-byte arm flag + 2-byte due tick.

### Hardware validation

PASS. Verified that physical relocations no longer trigger semantic Pattern
AutoSave generations. Non-semantic scheduler drains layout-only changes at
lowest priority after all semantic work completes.

---

## §3 Owned trailing-slack reservation (Phase 2)

### Problem

Blocks in the dynamic pool had no reserved trailing space for in-place
growth. Every automation append required a full block relocation
(allocate-new/copy/swap/free-old), even when adjacent free space existed.

### Solution

A 512-byte bit-packed reservation image (`reservation_image[]` in
PatternStackService.c) with the same geometry as the occupancy bitmap:

- **Repair pass creates reservations.** When the finite repair epoch
  scans an occupied block, it marks the immediately following chunk as
  reserved trailing slack (if that chunk is free and unreserved).

- **Gate-6 growth consumes reservations.** `pat_tryAppendAutomation()`
  checks `patSvc_isChunkReserved()` before attempting in-place growth.
  On success, `patSvc_consumeReservation()` clears the reservation bit.

- **Reactive recovery reclaims surplus.** When an allocation is blocked
  by fragmentation, the reactive recovery pass may reclaim surplus
  reservations (reservations whose owning block no longer needs them)
  to satisfy the allocation — but only when the density latch is inactive.

- **Density hysteresis.** Reservations are disabled at or above
  `PAT_RESERVATION_REDUCE_THRESHOLD` (70% occupancy) and re-enabled
  below `PAT_RESERVATION_RESTORE_THRESHOLD` (50%). This prevents
  reservation overhead from consuming pool capacity at high occupancy.

- **Not persisted.** The reservation image is not included in PAT4
  payloads. It is cleared and lazily rebuilt at init, handover completion,
  and filesystem replacement. During the rebuild window, allocation falls
  back to the occupancy bitmap alone.

### RAM

512 bytes reservation image + 3 bytes policy/rebuild flags.

---

## §4 Reactive-only compaction (Phase 3)

### Problem

Periodic Tier 2 compaction swept the pool on a timer, relocating blocks
toward pool start. This created a chase loop with Tier 1 trailing-gap
maintenance: Tier 2 removed the gap Tier 1 just created, Tier 1 detected
the missing gap and recreated it, Tier 2 ran again and removed it.

### Solution

Delete the periodic Tier 2 sweep entirely. Compaction is now reactive:

- Triggered only by a blocked allocation (fragmentation failure) at
  `patSvc_submit()` direct path or queue drain.
- Two-pass search: first pass respects reservations (will not reclaim
  reserved trailing-slack chunks); second pass reclaims surplus
  reservations only when the density latch is inactive.
- The former elastic-gap policy is retired.
- `PAT_COMPACT_SCAN_PER_TICK` (16 address entries) is retained only as
  the reactive-recovery per-tick scan bound.

### RAM

No additional RAM. Former Tier 2 state repurposed for reactive recovery.

---

## §5 Finite bounded repair epoch (Phase 4)

### Problem

The repair cursor ran continuously, scanning all address entries every
pass and immediately starting a new pass on completion. Combined with
the Tier 1/Tier 2 chase, this meant the service never reached a quiescent
state.

### Solution

The repair cursor sleeps at `PATSVC_ADDRESS_COUNT` (the sentinel value
above the valid address range) between epochs. It wakes only on specific
events:

- Pool mutation (any `patSvc_submit()` call)
- Reservation consumed (Gate-6 growth used a trailing-slack chunk)
- Density restore (occupancy dropped below 50%, re-enabling reservations)
- Handover (filesystem replacement boundary)
- Filesystem replacement (Scene Load, Bank Load, Pattern Load)

Adaptive per-tick budget:
- `PAT_REPAIR_SCAN_IDLE = 16` address entries when no AutoSave work pending
- `PAT_REPAIR_SCAN_BUSY = 4` under AutoSave I/O pressure

Once the cursor completes a full pass and returns to `PATSVC_ADDRESS_COUNT`,
no further repair work runs until a wake event. The service reaches true
quiescence when the pool is clean.

---

## §6 O(1) scalar dirty predicate (Phase 5 — Pass 1)

### Problem

`autosave_maskHasDirty()` scanned the entire 3,856-byte scalar dirty mask
on every scheduler tick to determine whether any dirty bits existed. This
O(n) scan ran even when no mutations had occurred.

### Solution

A maintained population count `autosave_dirty_count` (`uint16_t`) tracks
the exact number of set bits in the mask:

- `autosave_maskByteOr(offset, value)`: before OR-ing a byte into the
  mask, XOR with the existing byte to find fresh bits, then increment
  the count by `popcount8_lut[fresh_bits]`.
- `autosave_maskBitTake(offset, bit)`: when the writer consumes a dirty
  bit, decrement the count.
- `autosave_maskHasDirty()`: returns `autosave_dirty_count != 0`.

The 256-byte `popcount8_lut` ROM table provides O(1) byte popcount.

DEV audit: every 1,000th `autosave_maskByteOr` call computes a brute-force
popcount of the entire mask and compares against `autosave_dirty_count`.
A mismatch emits a `Z` AutosaveTrace stage record.

The only idle-tick `patSvc_countUsed()` call (the clean-occupancy recount
that ran every foreground pass) was removed. All mutation-path and lifecycle
recounts retained. `patSvc_countUsed()` itself was optimized from a
2,048-bit loop to 64-word `__builtin_popcount`.

### RAM

2 bytes (`autosave_dirty_count`) + 256 bytes ROM LUT in `.rodata`.

### Hardware validation

PASS (2026-09-20). Zero `Z` audit mismatches across the test session.
`patSvc_countUsed()` matches brute-force recount at lifecycle boundaries.

---

## §7 Pattern AutoSave quiet window (Phase 6 — Pass 1)

### Problem

Pattern AutoSave admitted immediately on any dirty bit with no coalescing.
A burst of rapid edits (e.g., adjusting automation values) triggered one
PAT4 file write per edit, wasting SD bandwidth and increasing wear.

### Solution

Two timing gates in the Pattern AutoSave scheduler:

1. **Quiet window**: 250ms (`AUTOSAVE_PATTERN_QUIET_WINDOW_MS`) silence
   after the last semantic mutation. The scheduler checks
   `autosave_last_pattern_semantic_us` (set by
   `autosave_markPatternDirty()` via `timebase_tim2Now()`) and refuses
   admission until the quiet window has elapsed. Rapid consecutive edits
   keep extending the window.

2. **Max latency**: 5,000ms (`AUTOSAVE_PATTERN_MAX_LATENCY_MS`) hard
   ceiling. If a Scene has been dirty longer than this (tracked by
   `fs_pattern_first_dirty_us`), it is admitted regardless of continued
   editing. This prevents indefinite deferral during sustained editing.

3. **Rotating Scene cursor**: `fs_pattern_scene_cursor` provides fairness
   across 16 Scenes. Each admission advances the cursor so that no single
   Scene can monopolize the writer.

### RAM

6 bytes: `autosave_last_pattern_semantic_us` (4 bytes) +
`fs_pattern_scene_cursor` (1 byte) + `fs_pattern_first_dirty_us` array
entries.

---

## §8 Background CPU budget (Phase 7 — Pass 2)

### Problem

Background work (repair, scalar drain, Pattern drain) ran unbounded on
every scheduler tick. During playback, this competed with audio rendering
and could cause timing jitter.

### Solution

A shared elapsed-time budget primitive in `filesystem.c`:

- **Refill**: `filesystem_backgroundBudgetRefill()` adds credit based on
  wall time elapsed since last refill. Budget rate: 2.5% during playback
  (25µs per ms), 5% when stopped (50µs per ms). Credit is capped at one
  millisecond of accumulated time to prevent idle buildup.

- **Charge**: `filesystem_backgroundBudgetCharge(start_us, end_us)`
  subtracts the elapsed work time from credit. Signed credit allows
  overshoot tracking: a single operation that exceeds the remaining
  budget produces negative credit, which must be recovered by future
  refills before more work is admitted.

- **Query**: `filesystem_backgroundBudgetAvailable()` returns
  `credit > 0`. Consumers check this before starting work.

Three consumer classes use the shared budget:
1. `patSvc_tick()` repair section
2. Scalar AutoSave drain scheduler
3. Pattern AutoSave drain scheduler

DEV-only per-class elapsed-time accounting: three `uint32_t` accumulators
track cumulative microseconds spent in each class. An `H` AutosaveTrace
report emits approximately every 5 seconds (5,000 ticks at 1 kHz) with
the per-class totals and resets the accumulators.

### Load/Save repair gate

The repair section of `patSvc_tick()` is additionally suppressed when
`menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE`. This
complements the existing filesystem.c AutoSave/trace schedulers that
already self-suppress on those pages. Queue drain and handover continue;
only repair is suppressed.

### RAM

36 bytes total: 8 bytes always-on budget state (credit, last refill
timestamp) + 28 bytes DEV accounting (three class accumulators, tick
counter, accumulated refill, report cadence).

### Hardware validation

PASS (2026-09-22). Test card output at `SD_CARD_S069_OUT/`.

Trace: `asavetrc.bin` (978,392 bytes). 222 H budget report records
across 74 emission periods. Zero `E` error records, zero `Z` dirty-count
mismatches. 180 `P` published scalar saves.

Budget enforcement confirmed active and binding across all three classes:

| Class | Max slice (µs) | Charged/5s | Denied/5s |
|-------|---------------|-----------|----------|
| Repair (0) | 2–9 | 0ms | 0–122 |
| Scalar (1) | 67–89 | 9–40ms | 6,000–15,500 |
| Pattern (2) | 143–164 | 1–5ms | 553–2,123 |

Peak aggregate CPU ~0.9% (45ms per 5s window), within the 2.5% playing
budget. No single slice exceeds 164µs. Concurrent class arbitration
works: when Pattern drain consumes budget, repair is denied. All three
plan-required metrics (charged time, denied count, max slice) confirmed.

---

## §9 Direct-path parity fix

`patSvc_submit()` direct branch now classifies fragmentation failures
separately from capacity failures. When the direct path attempts allocation
and fails despite free space existing (fragmentation), it retains the
event for reactive recovery rather than silently dropping it. A new `D`
PatternTrace stage records these retained events.

---

## §10 Trace stage additions

### PatternTrace (PatternTrace.h)

| Code | Meaning | Added in |
|------|---------|----------|
| `V` | Repair created an in-place trailing reservation | S069 slack/reactive |
| `L` | Repair relocated a block to create a reservation | S069 slack/reactive |
| `D` | Direct-path mutation retained for reactive recovery | S069 direct-path fix |

### AutosaveTrace (AutosaveTrace.h)

| Code | Meaning | Added in |
|------|---------|----------|
| `Z` | Dirty count mismatch (DEV audit) | S069 Pass 1 |
| `H` | Budget report (DEV accounting) | S069 Pass 2 |

---

## §11 Build metrics

**Pass 1 baseline** (commit `1f7a772`, 2026-09-20):
`text=449,476`, `data=404`, `bss=291,724`

**Pass 2 final** (commit `9627f70`):
`text=450,140`, `data=416`, `bss=291,756`

Image: 450,572 bytes (within 480 KiB application flash boundary).

Cumulative S069 RAM growth from S068:
- Non-semantic restriction: 5 bytes SRAM1 `.bss`
- Reservation image: 512 bytes SRAM1 `.bss` + 3 bytes flags
- Pass 1: ~11 bytes SRAM1 `.bss` + 256 bytes ROM LUT `.rodata`
- Pass 2: 36 bytes (8 always-on + 28 DEV)

---

## §12 Config constants added/changed

```c
#define PAT_REPAIR_SCAN_IDLE               16u
#define PAT_REPAIR_SCAN_BUSY                4u
#define PAT_RESERVATION_REDUCE_THRESHOLD   70u
#define PAT_RESERVATION_RESTORE_THRESHOLD  50u
#define AUTOSAVE_PATTERN_QUIET_WINDOW_MS  250u
#define AUTOSAVE_PATTERN_MAX_LATENCY_MS  5000u
```

Budget percentages (filesystem.c internal):
- Playback: 2.5% (25µs/ms)
- Stopped: 5.0% (50µs/ms)
- Accumulation cap: 1ms equivalent

---

## §13 Examined but unused plans

**`S069_ATS_PROBLEMS.md`** diagnosed AutoSave CPU spikes observed during
playback. The root cause — perpetual Tier 1/Tier 2 chasing and layout-only
dirty marking — was fully addressed by Phases 1-4 of this session. The
diagnostic approach (measuring CPU% with autosave off vs on) informed the
implementation but contributed no code.

**`S069_ATS_PAT_BOUNDED_CPU.md`** was an earlier bounded-CPU planning draft
that explored alternative approaches including a simpler "just remove
Tier 2" strategy. It was superseded by the authoritative
`S069_ATS_PAT_BOUNDED_CLAUDE.md` master plan, which incorporated the
reservation/density/reactive design and the elapsed-time budget primitive.
The CPU.md document's analysis of the Tier 1/Tier 2 oscillation was
accurate and contributed to the final plan, but its proposed solutions
were incomplete.

---

## §14 Deferred items

Items deferred from S069 implementation documents that were neither
implemented nor rejected:

1. **Background CPU budget module extraction.** The budget primitive
   (refill/charge/query API) is implemented in `filesystem.c` because the
   scheduler ladder and most budgeted work live there. If `filesystem.c`
   continues to grow or additional subsystems need budget access, extract
   to a dedicated `BackgroundBudget.c` module. Mechanical refactor, no
   behavioral change.

2. **Pattern repair epoch runs unconditionally during Load/Save menu.**
   The Pass 2 Load/Save gate addresses this for the repair section, but
   `patSvc_tick()` as a whole (including queue drain and handover) still
   runs during Load/Save. Whether queue drain should also be suppressed
   during Load/Save is an open question — currently it continues to ensure
   pending mutations from pre-Load/Save edits are completed.

3. **AutoSave OFF-to-ON re-enable convergence.** The non-semantic
   restriction and quiet window should now allow convergence testing per
   `S070_AUTOSAVE_REENABLE.md`. The suspected "ON never rearms" defect
   was not found in Session 068's assessment, but the test matrix has not
   been run with the S069 fixes in place.

4. **Per-track scale/shuffle sequencer consumption** (carried from S068).
   `track_scale` and `track_shuffle` are stored/edited/persisted but have
   no playback effect. Requires per-track PPQ tick accumulators and
   sub-step scheduling.

5. ~~**Pass 2 hardware validation.**~~ Completed 2026-09-22. PASS.

---

## §15 Files changed summary

| File | Phase | Nature of change |
|------|-------|-----------------|
| `PatternStackService.c` | 2,3,4,P1,P2 | Reservation image, finite repair, reactive compaction, popcount, budget query, Load/Save gate |
| `PatternStackService.h` | 2 | Reservation query/consume declarations |
| `PatternData.c` | 1,2 | Retired service dirty path, reservation consumption in tryAppend |
| `Autosave.c` | 1,P1 | Non-semantic mask, dirty count, popcount LUT, O(1) hasDirty, Z audit, timestamp |
| `Autosave.h` | 1,P1 | Declarations, quiet window/max latency constants |
| `filesystem.c` | 1,P1,P2 | Non-semantic scheduler, quiet window, budget primitive, H trace |
| `PatternTrace.h` | 2,3 | V/L/D stage codes |
| `AutosaveTrace.h` | P1,P2 | Z/H stage codes |
| `config.h` | 2,4,P1 | Repair scan, reservation thresholds, timing constants |

Phase key: 1=non-semantic, 2=slack/reservation, 3=reactive, 4=bounded repair,
P1=Pass 1, P2=Pass 2.
