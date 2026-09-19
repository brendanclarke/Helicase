# S068 AutoSave and Pattern-service bounded-CPU audit

## Executive finding

Both subsystems are nominally cooperative, but two clean-state scans and one
feedback loop defeat the intended bounded-background behavior:

1. Clean scalar AutoSave scans all 3,856 dirty-mask bytes on every idle
   `filesystem_tick()` call.
2. The Pattern service recounts all 4,096 bitmap chunks on every 500 Hz service
   pass, even though it already retains `logical_chunks_used`.
3. Tier 1 gap creation and Tier 2 compaction repeatedly undo one another. Each
   layout-only relocation is reported as a Pattern content mutation, causing
   Pattern AutoSave and diagnostic trace I/O.

The third issue couples the systems: maintenance manufactures AutoSave work,
and AutoSave persists pool layouts that the next maintenance pass immediately
changes. This is the principal burst amplifier.

## Evidence

### Clean scalar AutoSave hot path

When AutoSave is enabled, ready, recovered, and clean,
`filesystem_autosaveWriterSchedule_tick()` calls `autosave_maskHasDirty()`.
That function linearly checks 3,856 bytes before returning false. The scheduler
is called whenever the facade is idle, not at the five-second writer cadence.
The source documents approximately 7,600 `filesystem_tick()` calls per second
on hardware; a continuously clean writer can therefore perform approximately
29.3 million mask-byte tests per second. OFF returns before this scan, matching
the observed 4-5% CPU difference between ON and OFF.

### Pattern service hot path

`patSvc_tick()` runs at the front-panel service cadence of 500 Hz. Once queue,
bulk, and reactive work are empty, it calls `patSvc_countUsed()` before every
Tier 1 step. That function tests all 4,096 bitmap chunks, or approximately
2.05 million bitmap bit tests per second before Tier 1/Tier 2 work is counted.
Most of those results are identical to the retained count from the preceding
pass.

### Maintenance oscillation

Tier 1 relocates a logical block into `logical_chunks + gap` free chunks but
marks only the logical block chunks occupied. The trailing “reserved” gap
remains ordinary free bitmap space. A later Tier 1 allocation can consume it,
so the service cannot establish stable per-block slack with the current bitmap
representation.

Tier 2 then moves blocks toward lower offsets every 100 ms and resets the Tier
1 cursor. Tier 1 may move those blocks outward again to obtain a gap. The two
policies can oscillate even when no user data changes.

The supplied Pattern trace is direct evidence:

- 4,587 successful Tier 1 moves (`M`);
- 958 successful Tier 2 moves (`R`);
- no queue, capacity, fragmentation, fallback, pending-buffer, or wrong-Scene
  failure record;
- only 36 distinct stage/Scene/step combinations across 5,545 moves;
- Scene 8 track 2 step 2 alone moved 795 times; Scene 8 track 1 step 5 moved
  943 times.

This is repeated housekeeping of the same small working set, not pressure from
thousands of distinct edits.

### False Pattern dirtiness

Every successful relocation calls `pat_markPoolMutationDirty()`, which both
invalidates Scene card-clean authority and sets the Pattern AutoSave dirty bit.
A relocation changes addresses, bitmap, and byte placement, but not the
Pattern's musical meaning. Persisting it is optional optimization, not user
data durability.

The captured card shows the result: Scene 8's PAT4 pair reached generations
538/539 and Scene 10 reached 323/324. This scale is consistent with maintenance
feedback, and PatternTrace itself generates more low-priority SD work for every
move.

## Burst sources beyond the steady scans

- Re-enable calls `autosave_markResidentBankDirty()`. In a populated Bank this
  marks many thousands of scalar bits and every present Pattern. In DEV builds,
  each scalar bit also emits a `D` trace record. The fixture contains 148,176
  `D` records and reports trace-ring drops; diagnostic traffic is materially
  affecting the workload being diagnosed.
- AutoSave CPU budgets are per main-loop call, not per elapsed-time slice.
  Bounds such as 256 mask positions and 128 CRC bytes still execute in rapid
  succession at thousands of calls per second, producing short concentrated
  CPU phases rather than a steady time-based trickle.
- `pat_snapshotScene()` copies the complete 10,519-byte resident region in one
  foreground call. This is probably acceptable in isolation but becomes a
  visible burst when it follows trace and scalar backlog.
- Pattern AutoSave is the last background filesystem claimant. Its latency can
  grow without any explicit fairness or maximum-delay bound when settings,
  trace, and scalar work remain active.

## Recommended changes, in order

### 1. Stop treating physical relocation as a semantic mutation

Remove AutoSave/card-clean invalidation from Tier 1/Tier 2 relocation. Dirty a
Pattern only when its semantic state changes: trigger, specials, automation,
track settings, Pattern settings, clear/copy, or a filesystem replacement.

A later explicit save may serialize whichever valid layout exists at its
snapshot instant. If power is lost before that, reloading the older valid
layout is semantically correct; only the maintenance optimization is lost.
This one boundary breaks the Pattern-service-to-AutoSave feedback loop without
changing user-visible behavior.

### 2. Make clean-state predicates O(1)

- Add an atomic scalar dirty-summary bit or exact dirty-bit count maintained by
  the same mark/take/merge/rollback/discard operations that own the canonical
  mask. `autosave_maskHasDirty()` then becomes O(1); retain a debug assertion
  that occasionally compares it with a full scan.
- Maintain `logical_chunks_used` incrementally from allocation/free deltas.
  Recount only at init, filesystem replacement, integrity recovery, or a DEV
  audit checkpoint. Alternatively popcount 512 bitmap bytes at those rare
  boundaries, not 4,096 individual bits at 500 Hz.

Any added retained byte/counter requires the project's normal explicit SRAM
approval. The CPU saving is large enough to justify that small, measured
allocation.

### 3. End unconditional maintenance sweeps

The current bitmap cannot distinguish free space from a block-owned reserved
gap. Do not continuously attempt to give every block a gap under that model.
Use the existing service queue/reactive path as the primary guarantee:

- perform local relocation for the block that is growing or recently changed;
- run global compaction only after allocation/fragmentation evidence, a high
  occupancy threshold, or a semantic-mutation epoch;
- after one complete no-progress sweep, sleep maintenance until a mutation or
  occupancy transition wakes it;
- do not restart Tier 1 merely because the 100 ms Tier 2 interval elapsed.

If hard per-block reservations remain a requirement, represent reserved chunks
explicitly and include them in allocation/free ownership. Leaving them marked
free cannot provide that invariant.

### 4. Share one elapsed-time background budget

Introduce a small foreground background-work arbiter, without changing the
single filesystem facade:

1. Pattern FIFO, bulk barriers, and reactive allocation recovery retain first
   priority because they complete accepted edits.
2. Settings and already admitted atomic file transactions retain their current
   ownership until their safe boundary.
3. CPU-only AutoSave classification/CRC and proactive Pattern maintenance draw
   from a common time/token budget replenished per millisecond, not per main
   loop.
4. When Pattern semantic mutations are arriving, reset a short Pattern-save
   quiet window. Save after the quiet window, but enforce a maximum latency so
   continuous recording cannot postpone durability forever.
5. While scalar AutoSave is transforming a full Bank, suspend proactive Tier
   1/Tier 2 work; queued/reactive Pattern edits still run. While the Pattern
   service is draining accepted mutations, postpone only the Pattern snapshot,
   not unrelated scalar persistence.

This trades work between the systems while preserving data semantics and
atomic file behavior.

### 5. Reduce DEV-mode self-interference

- Return `AUTOSAVE_TRACE_RECORD_COUNT` from the temporary 2,048 records to the
  documented default 64 after this investigation, unless a specific next
  capture requires it.
- Replace per-byte full-Bank `D` emission with range/count summaries for bulk
  marks. Keep fine records for isolated edits.
- Rate-limit or aggregate Pattern `M`/`R` trace events once the oscillation is
  understood. Always preserve failure records.
- Keep trace writers behind product work; during dedicated capture, report how
  much facade time trace I/O consumed.

### 6. Measure before chunking the Pattern snapshot

First apply items 1-5 and measure the maximum `pat_snapshotScene()` duration.
If the 10,519-byte copy is still a problematic peak, convert it to a bounded
copy with a semantic revision check: freeze proactive maintenance, copy chunks,
and restart if a user mutation changes the revision. Do not stream directly
from the live Pattern while it can change.

## Acceptance metrics

- AutoSave ON and clean should be statistically indistinguishable from OFF in
  CPU use except for its low-rate constant-time scheduler checks.
- With no user edits, Pattern relocation and PAT4 generation counts must stop.
- Under dense held-step automation, record the maximum main-loop interval,
  audio queue low-water mark, button-event queue high-water mark/drops, service
  queue high-water mark/drops, scalar dirty count, Pattern dirty mask, and time
  to durable generation.
- A continuous-edit stress run must show bounded maximum Pattern-save latency
  and no starvation of scalar settings, HCNAMES, or foreground Load/Save.
- Power-cut tests must still leave one valid scalar A/B generation and one
  valid Pattern A/B generation for every affected Scene.

No `Core/` code was changed in this assessment.
