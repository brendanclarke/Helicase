# S068 AutoSave and Pattern-service bounded-CPU plan

## Revised conclusion

The first change should not be a general scheduler. The Pattern service must
first stop manufacturing work.

The current oscillation has three concrete causes:

1. `patSvc_relocateIndex(..., gap, ...)` searches for a run large enough for
   the logical block plus a trailing gap, but marks only the logical block in
   the bitmap. The alleged gap remains ordinary free space. Neither
   `pat_poolAlloc()` nor another maintenance relocation can distinguish it
   from unowned free space.
2. Proactive Tier 2 deliberately packs blocks downward with no gap. It
   therefore removes the condition Tier 1 has just created. Tier 2 then resets
   the Tier 1 cursor, guaranteeing another sweep.
3. Tier 1 and ordinary allocation also search from the low end. Tier 1 can
   consume a free gap left for a block it processed earlier. Disabling Tier 2
   alone would reduce the churn, but would not make the present gap policy a
   stable invariant.

Every successful relocation then calls `pat_markPoolMutationDirty()`. This
turns a physical-layout optimization into a semantic Pattern mutation, clears
the Scene's card-clean authority, and asks Pattern AutoSave to serialize the
new layout. The captured 4,587 Tier 1 moves and 958 Tier 2 moves are therefore
both CPU work and new filesystem work.

The intended order is:

1. separate physical relocation from semantic dirtiness;
2. give slack real temporary ownership and remove periodic compaction;
3. remove the two clean-state recounts;
4. measure the remaining work;
5. add one elapsed-time budget around background-only CPU slices;
6. retest AutoSave OFF-to-ON after the system can converge.

The already tested S068 input-ring and chase-light fixes are the baseline for
this plan. This document does not propose changing them.

## Corrections and scope clarifications

### Pool-count arithmetic

The backed pool contains 2,048 four-byte chunks, not 4,096. The bitmap has
4,096 representable bits, but its upper half describes the unbacked address
range and is initialized occupied. `patSvc_countUsed()` scans
`PAT_STACK_SIZE * 8 == 2,048` bits.

At the 500 Hz service rate, the idle recount is therefore about 1,024,000
bitmap tests per second. This is still unnecessary; it is half the estimate in
the first version of this audit.

### O(1) state does not replace serialization or CRC

The constant-time recommendation applies only to two questions asked before
or between transactions:

- does the scalar canonical mask contain any dirty bit?;
- how many logical Pattern chunks are currently occupied?

It does **not** skip, abbreviate, or make incremental the CRC of an admitted
file transaction:

- each scalar `.hcprms` generation still transforms and CRCs all 34,768 bytes;
- each Pattern generation still snapshots the complete 10,519-byte resident
  region, writes the complete 10,656-byte PAT4 file, and CRCs every file byte;
- the existing immutable snapshot and A/B publication rules remain unchanged.

The previous failed attempt to make AutoSave better by subdividing its file
work is therefore not being proposed again.

### Development logging

`DEV_MODE_LOGGING`, the 2,048-record AutoSaveTrace ring, per-byte `D` records,
and Pattern relocation records remain enabled and unchanged for the remainder
of the scoped feature work. They are part of the measurement environment.
Only a low-rate aggregate budget record is proposed below; no current record
is removed or coalesced.

### Pattern snapshot

The one-call 10,519-byte `pat_snapshotScene()` copy remains unchanged through
items 1-5. Its duration will be measured. Chunking it is item 6 only, and is
not justified unless the post-fix trace shows that this single copy remains a
material peak.

## Required invariants

The implementation should be reviewed against these invariants rather than
against a particular number of maintenance stages:

1. A semantic edit dirties Pattern AutoSave exactly once; moving identical
   bytes to another pool offset does not.
2. Accepted user and future live-record writes outrank every proactive
   maintenance action.
3. With no semantic edits and no blocked allocation, relocation reaches sleep
   and remains asleep. A timer alone never wakes it.
4. A step with available trailing slack can grow without a pool-wide search.
5. A true fragmented-allocation failure can invoke reactive compaction; an
   arbitrary 100 ms interval cannot.
6. No background CPU policy changes the complete bytes, CRC, atomicity, or
   recovery behavior of either AutoSave format.

## Implementation plan

### 1. Make physical relocation non-semantic

Keep the relocation transaction itself unchanged:

1. reserve the destination;
2. copy the complete logical block;
3. publish the new address under the short PRIMASK boundary;
4. release the old allocation.

Remove only the call from this transaction to
`pat_markPoolMutationDirty()`. The public helper can then be retired if it has
no semantic caller. Tier 1 and reactive compaction should continue to emit
their existing PatternTrace witnesses.

This is safe because address offset, pool placement, and bitmap placement are
not musical state. A later explicit save or semantic AutoSave may serialize
whichever valid layout exists at its snapshot boundary. If power is lost
first, the prior valid PAT4 layout reloads with the same triggers, specials,
automation, and settings; only the placement optimization is lost.

This is the smallest and highest-value change. It immediately breaks the
maintenance-to-AutoSave feedback loop, even before maintenance itself is
improved.

Acceptance for this boundary:

- force relocations without making a user edit;
- verify PatternTrace records the moves;
- verify the Pattern dirty mask, PAT4 generation, HCNAMES refreshed bit, and
  Bank card-clean witness do not change merely because of those moves;
- compare a semantic PAT4 decode before and after relocation.

### 2. Replace the Tier 1/Tier 2 loop with owned slack plus reactive compaction

#### 2.1 Why the current bitmap cannot reserve a gap

A zero bit means allocatable. Calling such a bit “reserved” in comments does
not reserve it. A hard one-chunk guarantee requires one of these:

- mark it occupied in the persisted allocation bitmap and add a way to
  distinguish slack ownership from a logical block; or
- keep a separate transient reservation bitmap consulted by every allocator.

The second option is simpler and does not alter PAT4. Recommended geometry is
one active-Scene bitset: 2,048 bits = 256 bytes. It belongs with Pattern pool
allocation state, is cleared/rebuilt on service handover or Scene replacement,
and is not serialized because losing slack across reboot loses no music.

This is a new 256-byte SRAM1 allocation owned by Pattern storage. It requires
the project's explicit RAM approval before implementation. Without that
approval, use the directional soft-gap fallback described below and document
that it is an optimization, not a reservation guarantee.

#### 2.2 One-chunk ownership rule

Use exactly one four-byte trailing slack chunk as the normal target. This is
enough for two two-byte automation entries. The reservation map, not the
logical bitmap, owns that chunk:

- ordinary allocation rejects a chunk if either its logical bitmap bit or its
  reservation bit is set;
- freeing or relocating a block clears its owned trailing reservation;
- an append that crosses a chunk boundary may consume its own reservation,
  turning it into a logical bitmap chunk;
- after consumption, maintenance attempts to reserve the next trailing chunk;
- semantic capacity wins under pressure: if a write cannot proceed while
  reservations exist, reactive work may release reservations and retry rather
  than dropping the musical edit.

Tier 1 becomes a finite **slack repair** pass, not a perpetual relocation
policy. For each address entry it does one of three things:

1. trailing chunk is already reserved for this block: no work;
2. trailing chunk is truly free: reserve it in place, with no byte move;
3. trailing chunk is unavailable: relocate that one block only if a
   `logical + 1` run exists, then reserve the final chunk.

After one complete no-progress or completed pass, Tier 1 sleeps. Wake it only
on a semantic pool mutation, a reservation consumed/reclaimed event, service
handover, or filesystem replacement. Do not wake it from elapsed time.

#### 2.3 Remove proactive Tier 2

Delete the periodic `PAT_COMPACT_INTERVAL_MS` path. Tier 2 survives only as
reactive recovery for a queue head that failed allocation even though total
reclaimable space is sufficient.

Reactive priority is:

1. retry the accepted write using ordinary free space;
2. reclaim expendable slack if that makes the write possible;
3. move at most the configured number of address candidates toward a
   contiguous layout;
4. retry the same queue head;
5. report capacity only when logical capacity is genuinely insufficient, or
   fragmentation only after a complete bounded recovery pass makes no
   progress.

No reactive move should restart a perpetual global sweep. It may request one
later Tier 1 slack-repair epoch after the accepted edit has committed.

#### 2.4 Preserve a simple no-new-RAM fallback

If the 256-byte reservation map is not approved, the minimum stable fallback
is:

- ordinary semantic allocation remains low-first;
- Tier 1 searches from the high end for `logical + 1` runs;
- Tier 2 remains reactive only;
- one completed Tier 1 epoch sleeps until a mutation wakes it.

This directional separation prevents Tier 1 from immediately consuming gaps
it created earlier and leaves a large low-end allocation frontier. The gaps
remain reclaimable ordinary free bits, however, so this is **soft slack** and
must not be described as a hard reservation for live recording.

#### 2.5 Direct failure must enter the same recovery path

Today a direct-when-idle mutation that cannot allocate can return failure
without gaining the queued path's reactive recovery. Keep the fast direct
attempt, but if it fails specifically because total capacity exists and the
largest run is too small, enqueue/retain that same event and report accepted
only if the queue owns it. Invalid targets and genuine capacity exhaustion
still fail immediately.

This avoids a future live-record edit being lost merely because it arrived at
the direct rather than queued boundary.

#### 2.6 Live-record throughput constraint

Slack addresses allocation latency, not request throughput. The current
service drains at most one queued event per 500 Hz service pass. A future
recorder that emits note, velocity, and several automation writes for several
tracks as separate queue records can exceed 500 queued operations per second
at high tempo.

Before live record lands, use one or both of these policies:

- update an already-present automation value through a dedicated safe
  in-place value-byte fast path; this is the common streaming case and needs
  neither allocation nor maintenance;
- coalesce all changes for one track/step boundary into one service commit, or
  add a small configurable multi-event drain while audio queue pressure is
  safe.

Do not enlarge the queue as a substitute for matching sustained producer and
consumer rates. The synthetic acceptance workload should include seven tracks
at 16th-note resolution with note, velocity, and multiple automation changes,
even though the actual recorder is not implemented yet.

### 3. Remove clean-state scans without changing file work

#### 3.1 Scalar dirty predicate

Add an exact dirty-bit count beside `autosave_dirty_mask[]`:

- `autosave_maskByteOr()` increments it by the popcount of bits that changed
  from zero to one;
- `autosave_maskBitTake()` decrements it only when it actually takes a set bit;
- recovery merge and rollback already pass through the OR helper;
- discard sets both mask and count to zero;
- `autosave_maskHasDirty()` becomes `count != 0`;
- DEV builds periodically assert the count against a full scan at a low
  cadence, never on every scheduler pass.

The maximum count is 30,848 bits, so `uint16_t` is sufficient. This is a new
two-byte SRAM allocation and needs explicit approval.

Expected saving: the clean writer currently examines all 3,856 volatile mask
bytes on roughly 7,600 idle filesystem calls per second, about 29.3 million
byte inspections per second. The count removes all of those from the clean
steady state. The observed 4-5 percentage-point AutoSave ON/OFF difference is
the hardware estimate to beat; the expected result is to recover most of that
difference, not to reduce CRC time during a real save.

`autosave_objectFullyCaptured()` remains a scoped object check and should not
be replaced by the global count.

#### 3.2 Pattern logical occupancy

Keep the existing `logical_chunks_used` variable and maintain it from the
changed step's old/new block sizes:

- before a single-step mutation, read its old logical chunk count;
- after successful commit, read the new count and apply the signed delta;
- block relocation changes no logical count;
- clear Pattern sets zero;
- track barriers subtract each released step's old count as they advance;
- init, handover, and filesystem replacement perform a full reconciliation.

The rare reconciliation should popcount the 256 backed bitmap bytes as 64
unaligned-safe 32-bit words, as `pat_poolUsagePercent()` already does, rather
than testing 2,048 bits individually.

Expected saving: remove about 1.024 million bitmap tests per second while the
service is idle, plus the full recount after every ordinary mutation. The new
per-step delta is constant work. No new retained RAM is required because
`logical_chunks_used` already exists.

If a separate reservation bitmap is approved, `logical_chunks_used` continues
to count musical allocation only. Reservation pressure is measured separately
and never makes the UI claim that semantic data consumed more pool bytes than
it did.

### 4. Add one elapsed-time budget for background-only CPU

Do this after items 1-3 so the budget controls useful finite work rather than
hiding an infinite maintenance loop.

Add one `config.h` control, initially tuned by hardware measurement:

```c
#define BACKGROUND_CPU_BUDGET_US_PER_MS 50u
```

`50` is a starting test value, representing a 5% long-run foreground CPU
allowance. It is not a correctness constant. Hardware A/B testing should try
at least 50, 100, and disabled/unlimited before selecting the default. Define
zero as disabled/unlimited so the comparison does not require removing call
sites.

Use `timebase_tim2Now()` to charge actual elapsed microseconds. Refill by the
configured amount for each elapsed millisecond, cap positive credit at one
millisecond's allowance, and carry negative overshoot as debt. Capping credit
prevents several idle seconds from authorizing one large catch-up burst. Each
existing per-call byte/address limit remains as the maximum indivisible slice.

Budgeted work:

- runtime OFF-to-ON full-Bank dirty seeding, advanced by retained scope cursor;
- scalar AutoSave mask classification and CRC/transform CPU slices;
- Pattern AutoSave staging/CRC CPU at the start of a new 512-byte write chunk;
- proactive Tier 1 slack repair;
- later, the Pattern snapshot only if item 6 proves it needs gating/chunking.

Not budgeted:

- audio rendering or ISR work;
- AsyncFATFS/SD polling needed to advance an admitted transaction;
- foreground Load/Save;
- an accepted Pattern mutation or bulk user command;
- reactive compaction required to complete an already accepted mutation;
- completion, close, sync, error, or rollback boundaries.

This distinction matters for live record: the budget may postpone restoring
slack, but it may not postpone or discard the write that consumed it.

The budget changes timing only. Scalar and Pattern files remain fully
serialized and fully CRC-covered. If the budget is continuously exhausted,
durability latency increases; the maximum-latency policy below bounds that
separately.

Runtime re-enable needs one small state-machine refinement. Today
`filesystem_autosaveSetupCompleted()` enables tracking and calls
`autosave_markResidentBankDirty()` synchronously. On a full Bank that one call
walks all implemented scopes and emits every existing per-byte `D` record.
Keep those records, but advance the seed by existing ownership scopes (Bank
fields, Scene settings/effect, Kit, then one Instrument at a time) under a
retained cursor. Tracking stays enabled while the seed advances, ordinary new
mutations continue to OR into the same canonical mask, and writer admission is
held until the seed-complete boundary. This changes no dirty semantics and
turns re-enable's largest non-file CPU burst into measurable slices without
reintroducing byte-level file chunking.

#### Trace measurement

Keep all current DEV records. Add one aggregate `Z` budget record per active
work class per one-second window, not one record per slice:

- flags bits 0..2: class (`scalar`, `Pattern file`, `Pattern maintenance`,
  `snapshot/other`);
- flags bits 3..7: denied-slice count, saturating at 31;
- value bits 0..19: charged microseconds in the window;
- value bits 20..31: maximum single-slice microseconds, saturating at 4,095.

Update `tools/decode_devlogs.py` for this record. The trace demonstrates that
the configured allowance is binding when expected, that deferral is finite,
and that no individual slice is unexpectedly large. Existing PatternTrace
`M`/`R` records remain the proof that idle relocation stopped.

Budget state is a small new retained allocation; aggregate counters are
DEV-only. Exact linked bytes and sections must be listed for RAM approval
before implementation.

### 5. Pattern AutoSave quiet window and maximum latency

Use two `config.h` timing controls after the shared budget exists:

```c
#define PATTERN_AUTOSAVE_QUIET_MS        250u
#define PATTERN_AUTOSAVE_MAX_LATENCY_MS 5000u
```

The values above are starting points for hardware testing. A semantic Pattern
mutation restarts the short quiet deadline. Pattern AutoSave waits for that
quiet boundary so it does not snapshot a file that immediate edits will make
stale. The oldest pending semantic dirty transition also starts a maximum
deadline; once reached, the active/oldest eligible Scene gets a save even if
edits continue, provided the service is at a safe mutation boundary.

Re-enable's full-Bank Pattern seed is one backlog episode, not sixteen
independent five-second waits. Retain fairness with a rotating Scene cursor;
prefer the active Scene once, then resume the rotation so a continuously
edited active Pattern cannot starve the other fifteen.

`seq_recordActive`/`seq_eraseActive` and future live recording still require a
coherent snapshot boundary. A maximum deadline must not override an unsafe
concurrent-copy condition. When live record is implemented, its service
commit/revision boundary must provide a safe instant or a revision-checked
retry; that decision is not hidden inside this CPU pass.

### 6. Measure the snapshot before changing it

After items 1-5, record the maximum and typical duration of
`pat_snapshotScene()`, the maximum main-loop interval, audio queue pressure,
and underrun count.

Leave the one-call copy intact if it is not a meaningful peak. If it is still
problematic, make only the RAM copy cooperative:

- copy bounded regions into the same immutable snapshot;
- retain a semantic revision at start;
- restart if that revision changes before completion;
- prevent proactive relocation from running during the copy;
- admit the PAT4 transaction only after one coherent snapshot is complete.

The later writer still serializes and CRCs the complete snapshot. Direct
streaming from mutable live Pattern storage remains prohibited.

## AutoSave OFF-to-ON retest after convergence fixes

The current source still contains a correct success path: runtime enable
ensures the A/B files, enables tracking, and calls
`autosave_markResidentBankDirty()`, which seeds scalar and Pattern work. The
suspected general “enable never rearms” bug should therefore be retested only
after layout churn and clean-state scans no longer distort timing.

Add low-rate lifecycle records for policy OFF, policy ON, ensure admitted,
ensure success/failure, tracking enabled, full-Bank seed complete, scalar
dirty count, and Pattern dirty mask. This adds observability; it does not
replace current `D` records.

The full-Bank seed itself should use the scoped, budgeted cursor described in
item 4. Its completion record is the point at which either scalar or Pattern
writer admission may begin. Pattern dirty bits may already be present, but the
ordinary scheduler priority and quiet-window rules apply only after the whole
seed boundary is complete. An OFF transition during seeding cancels the cursor,
disables tracking, and uses the existing safe discard rules.

Run the existing `AS-ENABLE` matrix with these specific cases:

1. AutoSave OFF; change one scalar and one Pattern in a low Scene and a high
   Scene while playback never stops; turn AutoSave ON.
2. Repeat while the Pattern service has just consumed/refilled slack.
3. Repeat while Load/Save temporarily owns or suppresses the facade.
4. Turn OFF during an active scalar transaction and during an active Pattern
   transaction; then re-enable after each reaches its safe boundary.
5. Reboot only after trace shows setup success, seed counts, scalar terminal
   success, and every seeded Pattern bit drained. Verify semantic values and
   A/B generations, not merely file timestamps.
6. Separately inject one ensure failure and verify the setup-failure behavior.

Pass criteria for the suspected feature:

- tracking becomes enabled after ensure success;
- OFF-mode scalar and Pattern edits are present after reboot;
- scalar dirty count and Pattern dirty mask converge to zero;
- relocation alone creates neither new dirtiness nor new PAT4 generations;
- low and high Scene writes have a measured, bounded latency.

If these pass, close the general OFF-to-ON suspicion. The independent source-
proven liveness weakness remains: one runtime ensure error latches
`fs_autosave_setup_failed`, leaves the UI showing ON, and has no automatic
retry. Treat that as a separate media-error policy: expose it and provide a
bounded retry or explicit retry action. It is not caused by CPU contention.

## Implementation and test order

1. Capture a short current-build baseline: clean ON/OFF CPU, relocation rate,
   Pattern generations, main-loop peak, queue overflow, and AutoSave latency.
2. Apply the physical-versus-semantic dirty split.
3. Replace periodic Tier 2 and perpetual Tier 1 with owned-slack repair plus
   reactive-only compaction (or the documented soft fallback if RAM is not
   approved).
4. Add the scalar exact dirty count and Pattern incremental occupancy.
5. Re-run the baseline. This identifies what was fixed without a scheduler
   masking the result.
6. Add the configurable elapsed-time budget and aggregate `Z` trace.
7. Add Pattern quiet/max-latency scheduling and run dense synthetic traffic.
8. Run the complete `AS-ENABLE` matrix and decide whether the suspected
   feature is closed.
9. Revisit snapshot chunking only if the trace still identifies it as the
   dominant single CPU slice.

## Acceptance metrics

- Clean AutoSave ON is statistically indistinguishable from OFF except for
  constant-time gates and low-rate trace work.
- After one finite maintenance epoch, an idle Pattern produces zero further
  relocations and zero new PAT4 generations.
- No repeated `M/R` chain moves the same block back and forth.
- Scalar trace reports roughly the configured long-run CPU allowance when
  saturated; denied counts rise without audio underruns or unbounded save
  latency.
- Dense multi-track writes produce no queue/capacity/fragmentation drop while
  reclaimable space exists.
- Record maximum service-queue depth, button-event depth, background charged
  time, maximum slice, audio queue-free percentage, underruns, and time from
  semantic dirty mark to durable generation.
- Power interruption still leaves one valid scalar A/B generation and one
  valid Pattern A/B generation for every affected Scene.

No `Core/` product code is changed by this planning revision.

---

## Pass 2 implementation review and hardware test (2026-09-22)

### Implementation

Pass 2 implemented items 4A, 4B, 4C from the authoritative
`S069_ATS_PAT_BOUNDED_CLAUDE.md` master plan, plus the deferred
`SCOPING_TARGETS.md` finding that `patSvc_tick()` repair runs during
Load/Save menu.

19 changes (16 primary C01–C16, 3 auxiliary A01–A03) across 8 files:

| File | Changes | Summary |
|------|---------|---------|
| `config.h` | C01 | Budget rate constants: 25µs/ms playing, 50µs/ms stopped |
| `filesystem.c` | C02–C06, C11–C13 | Budget struct, refill/charge/available/deny API, H trace, phase 56/13/patternWrite budget gates |
| `filesystem.h` | C07 | Public budget API declarations, work-class constants |
| `PatternStackService.c` | C08–C10 | Load/Save repair gate, repair budget gate with per-step charge |
| `PatternStackService.h` | A02 | Comment update for budget + menu gates |
| `AutosaveTrace.h` | C14 | `AUTOSAVE_TRACE_STAGE_BUDGET_REPORT = 'H'` |
| `tools/decode_devlogs.py` | C16 | H record decoder |
| `SCOPING_TARGETS.md` | A01 | Deferred item marked addressed |

Six improvements over the implementation schedule:

1. Struct-based state (`struct budget_state`) instead of flat statics
2. Rate helper `filesystem_backgroundBudgetRate()` selecting rate from `seq_isRunning()`
3. Zero-rate bypass: config rate 0 admits all work (debugging override)
4. Public `filesystem_backgroundBudgetDeny()` for PatternStackService.c cross-module access
5. Phase 56 charges on patch-count-full exit path, not just loop completion
6. Phase 13 charges on zero-byte fread (staging still consumed CPU time)

### Hardware test

**PASS** (2026-09-22). Test card output at `SD_CARD_S069_OUT/`.

Trace file: `asavetrc.bin` (978,392 bytes, 222 H budget report records
across 74 emission periods).

**Error records:** 0 `E` (operation error), 0 `Z` (dirty-count mismatch).
Pass 1 invariant holds.

**Completed saves:** 180 `P` (published) records — scalar saves converging.

**Budget report summary (H records, per work class):**

| Class | Max slice (µs) | Charged per 5s window | Denied per 5s window |
|-------|---------------|----------------------|---------------------|
| Repair (0) | 2–9 | 0ms (rounds down) | 0–122 |
| Scalar (1) | 67–89 | 9–40ms | 6,000–15,500 |
| Pattern (2) | 143–164 | 1–5ms | 553–2,123 |

**Aggregate CPU:** Peak ~45ms per 5s window = **0.9% CPU**, well within
the 2.5% playing budget (25µs/ms) and 5% stopped budget (50µs/ms).

**Budget is binding:** Denied counts across all three classes confirm
work is being throttled. When Pattern drain is active and consuming
budget, repair gets denied (visible in periods where repair
denied_count=25–122 while Pattern denied_count=1,068–2,067). The shared
pool correctly prevents concurrent classes from exceeding the aggregate
allowance.

**No single slice exceeds 164µs.** All three budget trace metrics the
plan required are present and correct:

1. Total charged microseconds per work class — confirmed
2. Number of denied slices per work class — confirmed
3. Maximum single-slice duration — confirmed

All saves converge, no errors, no dirty-count mismatches, budget
enforcement is active and correctly arbitrating between concurrent
work classes.
