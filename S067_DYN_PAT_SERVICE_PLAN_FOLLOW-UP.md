# S067 Dynamic Pattern Service Plan — Pre-Implementation Follow-Up

## Purpose and status

This document is a read-only design assessment of
`S067_DYN_PAT_STACK_SERVICE.md` against the current Session 066 repository.
It does not replace the original plan and records no implementation decision.
Its purpose is to identify the architecture corrections and explicit decisions
needed before the recommendations can be folded back into S067.

The assessment used the current source and the following project authorities:

- `MEMORY.md`;
- `SCOPING_TARGETS.md`, especially Phase 4.3;
- `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md`;
- `knowledge_files/specification_reference/AUTOSAVE.md`;
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`;
- `knowledge_files/specification_reference/SRAM_MANIFEST.md`;
- Session 062 and Session 066 handoff records;
- current PatternData, Sequencer, Menu, Euclidean, filesystem, timebase, and
  PatternTrace source.

No source changes are proposed as already approved by this document. The
existing RAM-allocation approval policy remains binding. In particular, the
current `+267 B` estimate must be revisited if the selected architecture needs
Scene identity, allocation-span metadata, concurrency state, or wider queue
entries.

### Lead-review update

The lead developer subsequently established one binding architectural
constraint that supersedes alternatives discussed in the initial assessment:

> Exactly one Pattern may be mutated by the stack service at a time. Any
> number of Patterns may be read concurrently, including Patterns other than
> the current mutation target. Playback Scene changes never wait for the stack
> service. Mutation-target handover closes stack-event admission, finishes the
> old target's accepted work, changes the service target, and then reopens
> admission.

Section 16 records the resulting settled architecture and the smaller set of
questions that still require genuine lead input. Sections 1–14 retain the
source evidence and alternative analysis, and Section 15 retains the initial
question audit; Section 16 is authoritative
where an earlier option conflicts with the reviewed decisions.

## Executive assessment

The intended direction is sound:

- PatternData should have one coherent incremental-mutation authority;
- pool maintenance should be bounded;
- allocation failure should not silently lose an edit;
- playback should observe either the old complete block or the new complete
  block;
- fragmentation recovery should be separate from filesystem persistence;
- the pool-usage monitor is useful diagnostic support for the service.

Part B, the pool-usage monitor, is low-risk and can be implemented and tested
independently.

Part A is not ready to implement exactly as currently specified. The current
plan assumes foreground-only pool mutation, an implicit active-Scene queue,
already-correct publication ordering, and enough metadata to identify block
allocation spans from the bitmap. Those assumptions do not match the current
repository. Several resulting problems are correctness blockers rather than
implementation polish:

1. TIM3 currently mutates Pattern storage during live erase and commits Scene
   changes.
2. Existing relocation/erase ordering can expose freed pool bytes through a
   still-live address.
3. Menu editing intentionally targets a viewed Scene that may differ from the
   playing Scene.
4. Address/back-reference checks do not provide sufficient stale-request or
   request-order protection.
5. Reserved slack has no recoverable ownership or allocation-span metadata.
6. A bitmap walk cannot distinguish block starts, interior chunks, and slack.
7. The proposed service priority order can prevent compaction from running
   when a blocked queue head needs it.
8. Asynchronous clear changes the semantics assumed by Euclidean generation
   and other existing callers.
9. Existing Boolean/void APIs cannot represent the proposed deferred result
   without caller-visible ambiguity.
10. The proposed retained-state budget does not include the state required to
    resolve these issues.

These issues should be resolved in the design before the original plan is
updated or implementation begins.

---

## 1. Current execution contexts do not match the foreground-only premise

### 1.1 Live erase is a TIM3 Pattern mutation

`seq_advanceTrackStep()` runs under the TIM3 sequencer timing owner. When live
erase is active, it calls `pat_eraseStep()` directly:

```c
if (seq_eraseActive && track == menu_getActiveVoice()) {
    pat_eraseStep(seq_activePattern,
                  menu_getActiveVoice(),
                  (uint8_t)seq_stepIndex[track]);
}
```

This means pool bytes, bitmap bits, and address entries are currently mutated
from interrupt context. It also means `pat_markSceneDirty()` is reached through
the interrupt mutation path.

The S067 statement that pool writes and snapshots are both foreground-only is
therefore incomplete. The existing AutoSave guard prevents admitting a
snapshot while `seq_eraseActive` is set, but that scheduling guard does not
make all Pattern mutation foreground-only.

### 1.2 Trigger recording also changes address entries from sequencer paths

`seq_recordTrigger()` calls `pat_setStepActive()` for the active Scene. Roll
handling can call `seq_recordTrigger()` during sequencer service. Although a
trigger-bit update does not modify pool or bitmap state, it modifies the same
16-bit address entry that a relocation publishes.

Consequently, a relocation cannot safely snapshot `trigger_bits` at its start
and blindly restore those bits when publishing the new offset. A trigger set
or toggle occurring between those points can be lost.

### 1.3 Scene changes are committed by TIM3

At a master boundary, Sequencer assigns:

```c
seq_activePattern = seq_pendingPattern;
```

inside the TIM3-owned step scheduler. The proposed Scene-switch sequence—stop
accepting events, drain the queue, finish a bulk operation, switch the service
target, and then permit the Scene switch—cannot be achieved only inside a
500 Hz foreground Pattern service. It requires an explicit handshake with the
Sequencer's request/commit lifecycle, or it requires deferred events to carry
their own Scene identity so a Scene switch does not alter their destination.

### 1.4 Queue concurrency must be designed, not inferred

If live erase becomes an event producer while Menu remains another producer,
the ring is not a simple single-producer/single-consumer queue. Head/tail byte
updates and entry publication must be protected against interrupt preemption.
A short PRIMASK-protected publication is plausible, and PatternTrace already
contains an established project-local model, but the ownership and maximum
interrupt-off duration must be stated explicitly.

Alternatively, TIM3 can publish only a narrow ISR-to-foreground command into a
dedicated SPSC path, with all ordinary Menu requests staying foreground-only.
That separates the producers but introduces another retained queue or mailbox
and coordination rules.

### Required plan correction

S067 should inventory every Pattern mutation API by execution context and
state which operations remain legal from TIM3. It must then define either:

- a multi-context unified request publication protocol; or
- a dedicated ISR handoff plus foreground mutation service.

The phrase "both are foreground-only" should not remain as the concurrency
proof.

---

## 2. The current publication primitive is not yet playback-safe

### 2.1 Current relocation frees the old block before publishing the new address

The current `pat_writeDynamic()` replacement path performs:

1. allocate new;
2. write new block;
3. free and zero old block;
4. publish new address.

That is not the write-new / swap-pointer / free-old protocol described by
S067. TIM3 can preempt after step 3 but before step 4, read the old address,
and then consume the block that has just been cleared.

The correct publication order for a copy-on-write change is:

1. obtain the old address and validate its block;
2. allocate a disjoint destination span;
3. write the complete destination block;
4. atomically publish the new complete 16-bit address entry;
5. retire the old span only after publication.

Because foreground cannot run while TIM3 itself is executing, an ISR that
reads the old address before the foreground swap will finish reading the old
block before foreground resumes and frees it. An ISR after the swap will read
the complete new block. That is the actual safety argument.

### 2.2 Current erase has the same inverted ordering

`pat_eraseStep()` currently frees and zeroes the pool block and only afterward
writes `PAT_ADDR_SENTINEL` to the address entry. The address must be detached
first, with the old complete entry retained locally for the later free.

This ordering is already described correctly in Phase 4.3 of
`SCOPING_TARGETS.md`, but the current implementation and the S067 claim about
the existing writer do not match it.

### 2.3 Same-size in-place structural rewrites are a separate race

If old and new logical block sizes use the same chunk count,
`pat_writeDynamic()` rewrites the live block in place. `pat_blockWrite()`
clears the full allocation and serializes the header, specials, and automation
tail byte by byte. TIM3 can observe a partially rewritten block.

In-place overwrite is safe only for a field whose read/write atomicity and
reader behavior are explicitly guaranteed. For example, a single automation
value byte might be eligible for an intentional in-place fast path if its
location is stable and both readers and writers treat the byte as the sole
mutable field. A complete block rewrite is not equivalent.

### 2.4 Relocation must not lose a concurrent trigger-bit update

The address entry combines trigger state, the specials-present bit, and the
pool offset. A service that reads the entry early, builds a replacement, and
later writes a complete cached value can overwrite a trigger bit modified in
the interim.

The final publication needs a short critical section that re-reads the current
trigger bit immediately before composing the new entry, or a stronger
serialized mutation rule covering trigger changes too. The choice needs to be
made deliberately because `pat_setStepActive()` and `pat_toggleStep()` are
currently direct address-entry writers.

### Required plan correction

Before adding slack or compaction, introduce and verify one canonical dynamic
block publication primitive. It should define:

- destination allocation ownership;
- complete-block write timing;
- address publication timing;
- preservation of the latest trigger bit;
- retirement timing for the old span;
- which, if any, in-place field writes remain permitted;
- interrupt exclusion, if used, and its maximum bounded duration.

This should be the first Part A implementation gate, not an assumption that
the current writer is already a proven primitive.

---

## 3. Deferred requests cannot use an implicit active Scene safely

### 3.1 The public APIs already carry explicit Scene identity

Pattern editing APIs accept `scene_index`, and many Menu call sites use
`menu_getViewedPattern()`. Menu documents that the viewed Pattern/Scene can
differ from playback during performance/follow behavior.

The S067 queue format omits Scene identity and says the queue targets a service
Scene derived from `seq_activePattern`. That changes existing semantics:

- a write requested for the viewed non-playing Scene could be applied to the
  playing Scene;
- switching the playing Scene could retarget already queued work;
- rejecting non-playing edits would be a new product constraint not stated in
  the current UI contract.

### 3.2 Draining before a Scene switch is not a complete substitute

Even if Sequencer were modified to wait for the service, a queue can remain
blocked indefinitely by true capacity exhaustion. Making a musical Scene
change wait on an unresolvable storage edit is undesirable and violates the
plan's claimed bounded switch latency.

The stated `<=128 ms` bound assumes at least one successful dequeue per 2 ms
tick. It does not apply if the head requires allocation and neither compaction
nor total free capacity can satisfy it.

### Reviewed correction: one explicit mutation target

Deferred entries do not carry Scene identity. The service retains two bytes:

- `service_scene`: the Scene whose pool/address offsets the current event
  stream may mutate;
- `requested_scene`: the next mutation target requested by the application.

When those values differ, stack-event admission closes. The service completes
all already accepted queue and bulk-barrier work for `service_scene`, performs
any required allocator wrap-up, assigns `service_scene = requested_scene`,
initializes that Scene's service-local slack view, and reopens admission.

Playback switching is independent and immediate. `seq_activePattern` is not
the queue's implicit destination and does not need to wait for service
handover. Future Sequencer work may read any combination of Pattern regions;
therefore every service publication must be safe for concurrent readers even
when the serviced Scene is not the globally active playback Scene.

Stack writes arriving while admission is closed are rejected/dropped and
traced. Static trigger-bit edits remain available because they are not pool
events; the publication primitive preserves their latest value when changing
the address offset.

---

## 4. Stale detection and request ordering are underspecified

### 4.1 Sentinel rejection discards valid deferred creates

A first automation or first special written to an empty step begins with
`PAT_ADDR_SENTINEL`. If allocation fails, that create is exactly the operation
that should be deferred. Under S067's proposed dequeue test, the sentinel makes
the request look stale and it is discarded immediately.

The dequeue validator must distinguish:

- an operation that is valid when no block exists, such as ADD automation or
  ADD special;
- an operation that requires an existing target, such as removing a specific
  stored automation;
- a destructive operation that is idempotent when already absent;
- a request intentionally superseded by a later edit.

### 4.2 A matching back-reference does not establish freshness

If a step still owns the same block, its back-reference will continue to
match after many later edits. An old queued value can therefore apply after a
newer synchronous value. Address and back-reference validation catches gross
ownership corruption; it is not an edit-order version check.

Examples requiring defined behavior include:

- queued ADD target A, then REMOVE target A before the ADD drains;
- queued value 20 for target A, then value 40 for target A;
- queued DELETE_STEP, then a newly programmed step at the same coordinate;
- bulk clear in progress while the user adds a step behind its cursor;
- target replacement whose add/remove halves are separated by deferral.

### 4.3 Reviewed ordering model

Queued service work uses strict FIFO order with bulk operations represented as
barriers. Once an ISR/timing-critical stack operation is accepted into the
queue, later queued operations cannot overtake it. A bulk clear begins when its
barrier reaches the head and later events remain behind it until its bounded
cursor completes.

Foreground Menu stack edits retain their established synchronous behavior.
They call the same service-owned internal executor directly only when the
service is open, idle, and has no older queued/bulk work. If that condition is
not true, the existing call fails/rejects and the failure is traced; Menu code
and display semantics do not become pending-aware in S067.

This is a hybrid dispatch model, but not two mutation owners: the service's
internal executor is the only code that changes pool/bitmap/offset state. It is
invoked synchronously for ordinary foreground edits and by the 500 Hz drain
for queued real-time work.

The alternatives considered before lead review were:

1. **Strict FIFO after first deferral.** Once any request is deferred, every
   later mutation for that Scene also enters the queue until it is drained.
   Operations execute in request order. This avoids stale late application but
   requires reads/UI to understand pending state and can make foreground edits
   appear delayed.
2. **Per-destination coalescing.** New requests search or index pending work
   and merge last-write-wins operations where that is semantically valid.
   Adds/removes/deletes still need a defined composition table.
3. **Generation/version checking.** A request carries or refers to a version
   advanced by every edit. Superseded requests are dropped. A full per-step
   retained generation table is expensive, so a smaller transaction or queue
   sequence design would be needed.
4. **Desired-state requests.** Queue a complete desired block image or a
   service-owned desired-state delta rather than an imperative operation.
   This simplifies last-write behavior but substantially increases queue RAM
   or requires an additional staging owner.

Under the selected model, FIFO order and the no-bypass rule replace a per-step
generation table for queued events. Live address/back-reference validation is
still required for structural integrity, but it is not used as the ordering
mechanism. Operations must be defined as idempotent where absence is valid;
notably, an ADD to a sentinel step is valid and must not be discarded as
stale.

---

## 5. Slack requires recoverable allocation-span ownership

### 5.1 Logical size is not allocation size

The existing header contains a step back-reference and automation count. The
special flags and count determine the logical bytes and their rounded chunk
count. They do not say how many extra chunks were reserved as slack.

After S067 adds slack, these sizes diverge:

```text
logical chunks = rounded encoded header/specials/automation bytes
allocated chunks = logical chunks + one or two slack chunks
```

Free, relocation, validation, compaction, load, and usage inspection need the
allocated span, not only logical chunks.

### 5.2 Bitmap adjacency does not identify slack ownership

If the chunk immediately after a block is occupied, it might be:

- this block's first slack chunk;
- this block's second slack chunk;
- the first chunk of the next block; or
- an interior chunk encountered by an incorrect scan.

The bitmap contains only occupancy. Zero-filled slack is not a durable owner
marker, and a neighboring allocation may be immediately adjacent.

The double-slack to single-slack fallback makes deterministic inference from
content type impossible unless the selected level is encoded somewhere.

### 5.3 Existing free logic would leak slack

Current release paths call `pat_blockChunks(flags, auto_count)` and clear only
that logical span. If allocation starts reserving additional bitmap bits
without changing release metadata, those bits remain permanently occupied.

### Allocation-span options

The architecture options section below compares the main solutions. Whichever
is selected must be used consistently by:

- normal replacement;
- erase and clear;
- Tier 1 relocation;
- Tier 2 compaction;
- PAT4 validation/load normalization;
- pool usage accounting;
- future copy operations.

The reviewed preferred design is one service-local 256-byte `is_slack` bitmap:
one bit for each of the 2,048 backed pool chunks. It distinguishes allocated
slack from logical block chunks while only one Pattern is under mutation. On
service-target acquisition it is reconstructed by starting from the Scene's
occupancy bitmap and clearing every validated logical block span enumerated
from the address array. Remaining occupied chunks are accepted as slack only
when they form a valid zero-to-two-chunk trailing run after a live block;
anything else is an allocator invariant failure, not generic slack.

Because the bitmap is service-local rather than per Scene, target handover
must leave the old Scene reconstructable from its address array and occupancy
image. PAT4 can continue persisting the occupancy bitmap and zero-filled slack
without adding a wire field, provided load/acquisition performs the same
validation. Existing PAT4 regions begin with no inferred slack and receive it
lazily through Tier 1 relocation.

This design adds 256 B SRAM1 beyond the original S067 budget and therefore
requires renewed explicit allocation acknowledgement before implementation.

---

## 6. Tier 2 cannot find blocks by walking occupied bitmap chunks

The proposed algorithm walks downward through the bitmap, finds the highest
occupied block, and reads its header. The bitmap does not identify block bases.
An occupied bit may point to the middle of a block or to slack, so arbitrary
pool bytes would be parsed as a header.

The existing authoritative index of block starts is the address array:

- at most 896 entries per Scene;
- each non-sentinel offset identifies a candidate base;
- the header back-reference can validate the candidate against its track/step;
- duplicates, malformed offsets, or bitmap disagreement can be treated as an
  invariant failure rather than silently relocated.

A bounded address-array scan is therefore the simplest reliable source of
live blocks unless explicit per-chunk ownership metadata is added.

The scan cost is still modest at a 100 ms maintenance interval, but it is
larger than the plan's quoted 128-word bitmap scan and should be measured rather
than retaining the current cycle estimate.

---

## 7. Recovery scheduling can deadlock behind the blocked request

The implementation sketch gives edit-buffer drain highest priority. If the
head allocation fails, the tick exits. Tier 2 runs only when the edit buffer is
empty. Thus a fragmented pool can reach this loop:

```text
head request needs a contiguous run
-> allocation fails
-> service returns
-> next tick retries the same head
-> allocation fails
-> compaction never runs because the queue is non-empty
```

The service uses a recovery state, not only a priority list. A failed head
should be classified using at least:

- required allocation chunks;
- total free chunks;
- largest contiguous free run;
- whether a valid relocation candidate can improve that run.

Possible outcomes are:

- **enough total free, insufficient run:** run one bounded compaction action,
  retain the head, retry later;
- **insufficient total free:** the request is capacity-blocked, and compaction
  cannot solve it;
- **sufficient run but allocation still failed:** allocator/bitmap invariant
  failure;
- **no movable candidate without temporary space:** use the selected fallback
  policy or report terminal failure.

An irrecoverable queued request is dropped and recorded in PatternTrace. It
must not permanently keep `pat_serviceBufferEmpty()` false, starve Pattern
AutoSave, or prevent mutation-target handover.

The selected recovery rule is:

1. If `total_free < required_chunks`, drop the blocked event and trace capacity
   exhaustion; compaction cannot help.
2. If `total_free >= required_chunks` but `largest_run < required_chunks`,
   enter reactive compaction and retain the head.
3. Perform at most one validated relocation per eligible tick.
4. If a complete address-array candidate pass makes no progress, drop the
   blocked event and trace unresolvable fragmentation.
5. Retry the retained head after each successful relocation.

Background Tier 2 maintenance does not use `largest_run < 8` alone. It runs at
the configured interval only when the pool has a real hole below a higher live
block and a validated move can improve bottom packing. Reactive compaction is
driven by the actual request size. This distinguishes fragmentation from a
perfectly packed but nearly full pool.

---

## 8. Bulk clear cannot become asynchronous without caller changes

### 8.1 Euclidean generation requires synchronous clear-before-fill

`EuklidGenerator.c` clears the target track and immediately sets the generated
steps. If `pat_clearTrack()` merely starts a background cursor, later clear
passes can erase those newly generated steps.

The claim that callers need no changes is therefore false for an asynchronous
clear design.

### 8.2 Pattern clear has immediate UI assumptions

The copy/clear UI clears LEDs, calls `pat_clearPattern()` or
`pat_clearTrack()`, and invalidates the Session 066 automation search result.
If the actual clear continues afterward, UI state, held-step search, AutoSave
dirty timing, and subsequent user input need an explicit busy/completion
contract.

### 8.3 Another bulk mutator is currently omitted

`pat_removeTrackAutomationByTarget()` walks all 128 steps and calls the
single-step remover. It is used by the step-automation clear action and also
needs inclusion in the unified mutation/bounded-work decision.

### Reviewed bulk-operation protocol

Track clear, Pattern clear, and track-wide automation-target removal are
bounded service operations driven by the 500 Hz tick. Each is one FIFO barrier
plus a service-local cursor, not hundreds of queue entries.

For clear operations, the request-time foreground/ISR-safe half clears the
affected trigger bits immediately without freeing pool storage or losing the
old offsets needed by the drain. When the barrier executes, each cursor step:

1. reads the current complete address;
2. atomically publishes `latest trigger bit | PAT_ADDR_SENTINEL`;
3. retires the captured old allocation using logical-span plus slack metadata;
4. advances the bounded cursor.

This preserves trigger bits set after the clear request. Euclidean generation
can therefore call clear and immediately program its new trigger pattern: its
static trigger writes survive the later stack-reclamation pass. Any later
stack-mutating events stay behind the clear barrier and apply only after the
corresponding old stack content has been removed.

Whole-Pattern scalar/track-parameter reset timing must be included in the
barrier contract, but Menu behavior and call sites remain unchanged.

---

## 9. Public result semantics need redesign

`pat_writeStepAutomation()` returns `uint8_t` success/failure. Menu uses this
as a completed transaction result. Target replacement may:

1. add the new target;
2. remove the old target;
3. roll back if the second add fails in the full-count case.

Returning success for a merely queued operation lets the caller proceed as if
the change is resident. Returning failure can trigger rollback or suppress UI
state even though the service later applies the operation.

Void special setters have the opposite problem: they provide no way to
distinguish committed, deferred, rejected, or queue-full outcomes.

The reviewed API model is:

- existing foreground Menu APIs remain synchronous and keep their current
  Boolean/void behavior;
- they execute through the service-owned internal executor only when no older
  queued or bulk work exists;
- they are rejected rather than deferred if the service is closed/busy or
  capacity is unavailable;
- ISR/timing-critical stack mutations use the queue and have no synchronous
  Menu transaction contract;
- queue overflow and terminal capacity/fragmentation failure drop the event
  and emit distinct trace records.

Target replacement therefore does not become a deferred two-part Menu
transaction. Its existing add/remove/rollback sequence either runs
synchronously while the service is idle or receives the same immediate
failure shape it already understands.

On queue full, the newly offered event is dropped and a queue-overflow trace is
recorded. Existing accepted FIFO work is never overwritten. This is a failure
trace, distinct from routine successful deferral, so it does not conflict with
the earlier decision to avoid logging every ordinary deferred write.

---

## 10. AutoSave coordination needs a complete service-idle definition

Adding a queue/multi-edit guard to
`filesystem_autosavePatternDrainSchedule_tick()` is directionally correct.
However, the service-idle predicate must reflect the selected architecture.

It may need to include:

- deferred edit queue empty;
- no ISR handoff pending;
- no bulk mutation active;
- no relocation publication active;
- no Scene transition handoff active;
- no capacity-blocked request awaiting a terminal decision.

A synchronous foreground relocation cannot interleave with the snapshot call,
so it does not require a long-lived busy bit merely for `memcpy`. A queued or
incremental relocation does.

Maintenance-only relocation changes address, bitmap, and pool bytes in PAT4.
It therefore must mark the Scene Pattern dirty even if the musical content is
unchanged, otherwise the next boot can restore the pre-maintenance allocator
layout. The service must also converge: a Tier 1 policy that repeatedly sees
the same block as lacking slack would create an endless dirty/AutoSave cycle.

An irrecoverable queued edit must not suppress AutoSave forever. The terminal
failure policy is therefore part of persistence correctness, not merely UI
behavior.

---

## 11. Part B — pool-usage monitor assessment

The proposed Global-page position is valid. The second Global subpage has the
CPU widget and AutoSave followed by one empty cell, and traversal retains later
empty rows as its terminator.

Counting set bits in the first 256 bitmap bytes correctly covers the 2,048
backed four-byte chunks. The upper 256 bytes are intentionally permanently set
for the unbacked nominal range and must remain excluded.

The following details should be clarified in the implementation plan:

1. **Meaning of usage.** Bitmap occupancy should include reserved slack. This
   reports allocator pressure, which is the useful user-facing meaning of
   Pattern store use. It is not merely encoded musical payload size.
2. **Alignment and aliasing.** `pat_scene_region_t` is packed. Although the
   bitmap's field offset is divisible by four, the type's alignment guarantee
   should not be assumed from that fact alone. Avoid casting the byte array to
   `uint32_t *` unless the base alignment is explicitly guaranteed. Byte-wise
   popcount is cheap at menu entry, or words can be loaded with `memcpy`.
3. **Cycle estimate.** Cortex-M7 does not provide a general scalar POPCNT
   instruction comparable to CTZ. GCC may emit a helper or software sequence.
   The operation remains cheap when performed once on entry, but the quoted
   approximately 128-cycle cost should be measured rather than treated as a
   contract.
4. **Display saturation.** A completely occupied pool calculates 100 percent.
   If the UI contract is strictly `00..99`, clamp only at formatting or in the
   retained value as explicitly chosen. Document that `99` can mean 99–100%
   allocator occupancy.
5. **Entry point.** Compute on actual entry to the Global/settings page, not on
   every repaint or edit-mode transition. A Scene switch while the Global page
   is open intentionally leaves the retained value unchanged under the current
   decision.

Part B remains suitable as the first standalone implementation and hardware
test.

---

## 12. Additional consistency issues to resolve while revising S067

### 12.1 Header byte order documentation

The current implementation writes the two-byte dynamic block header high byte
first and readers reconstruct it in that order. `PATTERN_DYNAMIC_STACK.md`
describes the header as little-endian. PAT4 stores pool bytes verbatim, so this
is a wire/validator documentation mismatch even though current firmware reader
and writer agree with one another.

The service and host validators should use one formally documented order. S067
should not silently change it as part of compaction.

### 12.2 Bitmap publication invariant

`pat_poolAlloc()` currently marks the destination chunks occupied before
`pat_blockWrite()` fills them. This is safe only because the current allocator
and writer are one synchronous operation and no independent compactor scans
the bitmap concurrently. It does not literally satisfy S067's statement that a
chunk is marked occupied only when fully written and ready for publication.

With a single non-reentrant foreground service, an internal `reserved` phase
can remain invisible to the compactor without adding another bitmap state. If
interrupt producers are allowed to inspect or mutate allocator state, the
critical-section/ownership rule must prevent them from treating the temporary
reservation as a complete block.

### 12.3 Filesystem bulk replacement boundary

Filesystem Pattern load and boot restore directly replace complete regions via
`pat_sceneRegionMut()`. If deferred work can exist for that Scene, bulk apply
must first cancel, drain, or version-invalidate it. "Dedicated load/save command"
does not by itself clear an already retained service request.

This should become an explicit service quiesce/cancel boundary used by all
complete-region replacement paths.

### 12.4 Trace encoding

PatternTrace already offers a logging-only eight-byte record with one-byte
stage, one-byte flags, 16-bit tick, and 32-bit value. The proposed three event
types fit the existing facility without production RAM, but exact packing must
be defined before implementation. Old/new aligned chunk indices require eleven
bits each and step identity requires ten bits, exactly filling `value32`; the
metric and slack level can use the flags byte. This is feasible but should be
specified so decoder updates and firmware agree.

---

## 13. Recommended revised implementation gates

The following order reduces the risk of combining allocator correctness,
interrupt coordination, queuing, and compaction in one change.

### Gate 1 — independent usage widget

- implement alignment-safe backed-bitmap counting;
- add the Global-page read-only cell;
- compute once on entry;
- clean-build after header edits;
- hardware-check empty, partly used, and dense Scenes.

### Gate 2 — mutation-context inventory and canonical publication primitive

- document every Pattern mutation caller and execution context;
- correct detach-before-free and swap-before-retire ordering;
- preserve the latest trigger bit at publication;
- remove or narrowly justify live-block structural rewrites;
- test playback during repeated automation growth, shrink, and erase;
- add invariant diagnostics without adding production-only RAM.

### Gate 3 — allocation-span decision and allocator update

- select how slack level/allocation span is represented;
- make allocate, free, validate, load, and usage code agree;
- define normalization of existing PAT4 blocks with no slack;
- define whether loaded blocks receive slack immediately or lazily;
- verify no bitmap leaks across repeated add/remove cycles.

### Gate 4 — maintenance-only relocation

- enumerate candidates from address entries or explicit ownership metadata;
- relocate one block per eligible service pass;
- prove convergence and correct dirty marking;
- verify active-Scene playback sees only complete blocks;
- measure the actual 500 Hz foreground cost.

### Gate 5 — deferred real-time mutation path

- choose Scene identity and queue entry format;
- choose ISR publication protocol;
- choose FIFO/coalescing/generation semantics;
- define queue-full and irrecoverable-capacity behavior;
- update public result contracts only where deferral is actually exposed;
- add the complete AutoSave service-idle guard.

### Gate 6 — bulk operations

- measure synchronous clear/target-removal cost first;
- retain synchronous semantics if comfortably bounded;
- otherwise convert each caller to a completion-aware transaction;
- explicitly test Euclidean clear-then-fill ordering.

### Gate 7 — global compaction

- classify fragmentation using total free and largest run;
- permit recovery work while a queue head is allocation-blocked;
- use validated block bases and recoverable allocation spans;
- define the no-temporary-space case;
- verify compaction terminates and AutoSave eventually becomes eligible.

### Gate 8 — dense hardware stress and persistence

- repeated add/remove/grow/shrink cycles;
- playback during relocation and erase;
- Scene switching with pending work;
- viewed-Scene edits differing from the playing Scene;
- Euclidean generation and clear operations;
- AutoSave admission after maintenance;
- PAT4 save/load and hidden A/B restore after compaction;
- pool widget agreement with a host-side bitmap count;
- logging-on and logging-off linked-size verification.

---

## 14. Architectural options and resource implications

This section intentionally presents choices rather than selecting one. Exact
sizes must be confirmed from concrete C types and a clean linked image before
approval.

### Option A — foreground manual edits remain synchronous; queue only ISR/future live record

**Shape**

- Correct the synchronous publication primitive first.
- Manual Menu edits either commit immediately or return a real capacity
  failure; they are not deferred.
- TIM3 live erase/record requests publish compact commands to foreground.
- Maintenance runs foreground-only.
- Current clear operations remain synchronous unless measurement proves they
  need conversion.

**Advantages**

- Preserves existing Menu Boolean transaction semantics.
- Avoids pending-state display and target-replacement rollback ambiguity.
- Queue traffic is limited to timing-critical operations.
- Closest to the Phase 4.3 distinction between human-paced parameter locks and
  real-time recording.

**Risks/tradeoffs**

- A manual edit can still fail under fragmentation unless synchronous manual
  dispatch is allowed to perform one bounded recovery action.
- The service is not literally the only function through which all calls pass,
  although PatternData can still be the sole module mutating storage.
- Future live recording semantics must be integrated deliberately.

**Resource implications**

- Potentially much smaller queue than 64 general-purpose entries.
- Queue entries still need Scene identity unless restricted by a proven active-
  Scene rule.
- Requires a small ISR SPSC ring or mailbox plus cursors and overflow witness.
- No per-step generation table if ISR requests are strictly FIFO and all
  mutations that can conflict are serialized while the handoff is pending.

### Option B — fully unified FIFO service with explicit Scene-tagged requests

**Shape**

- Every incremental mutation is a request.
- The service attempts immediate commit only when no older request for the
  relevant ordering domain exists.
- Once deferred, later requests are queued and applied strictly in order.
- Each request carries Scene, step, operation, and payload.

**Advantages**

- Strong single-order model.
- Queue semantics naturally preserve add/remove ordering.
- Easier to reason about than a mixed direct/deferred path once fully adopted.

**Risks/tradeoffs**

- Existing synchronous Menu callers and UI reads need pending-aware behavior.
- Scene-tagged entries exceed the current four-byte packing for the proposed
  operation set and payload.
- Multi-producer publication still needs interrupt protection or split queues.
- A blocked head can delay unrelated Scenes unless queues or scheduling domains
  are separated.

**Resource implications**

- Five-byte packed entries would cost 320 B for 64 entries; natural alignment
  may make a naive struct 6 or 8 bytes, costing 384 or 512 B.
- A count/full flag and producer/consumer state add bytes beyond head/tail.
- Pending-aware UI may require additional state.
- Flash cost is likely higher than the current S067 estimate because all
  callers need result/order handling.

### Option C — per-Scene FIFO domains

**Shape**

- Deferred work is grouped by Scene, either through separate small queues or a
  shared queue with per-Scene heads/indexing.
- Active-Scene maintenance gets playback-aware publication; non-playing Scene
  maintenance can run without a live pool reader.

**Advantages**

- One capacity-blocked Scene need not stall all Scene edits.
- Scene switching does not retarget queued work.
- Makes viewed-versus-playing semantics explicit.

**Risks/tradeoffs**

- Considerably more state and scheduling complexity.
- Sixteen physical queues waste RAM unless very small.
- A shared entry store plus per-Scene links/indices complicates bounded removal
  and coalescing.

**Resource implications**

- Sixteen head/tail pairs alone cost at least 32 B, before entries/full state.
- Separate fixed queues multiply unused capacity.
- Shared-pool queue management adds link/index bytes per entry or scan cost.

### Option D — desired-state coalescing rather than imperative events

**Shape**

- A pending record represents the latest desired result for a step/target.
- Repeated value edits coalesce in place.
- Delete/clear supersedes older adds according to a composition table.

**Advantages**

- Excellent burst absorption for fast knob/CC changes.
- Reduces stale older-value writes.
- Can keep queue depth smaller for repeated edits of the same target.

**Risks/tradeoffs**

- Complex composition semantics for target replacement, clear, delete, and
  re-add.
- Searching 64 entries to coalesce is bounded but costs foreground/ISR time.
- A complete desired block does not fit in a compact entry.

**Resource implications**

- Compact deltas can remain near 5–6 bytes per entry.
- Fast coalescing may need a small index or hash, which consumes more RAM than
  a linear scan but reduces interrupt-off work.
- Full desired-block staging is not attractive: a step can contain up to 63
  automation entries.

### Allocation-span Option 1 — encode slack/span in the block

**Shape**

- Dedicate format bits or add a byte that records allocated slack/span.

**Advantages**

- Allocation ownership is self-describing in PAT4.
- Free and compaction remain local and efficient.

**Risks/tradeoffs**

- Header bits are currently fully assigned to ten-bit step ID and six-bit
  automation count.
- Adding a byte changes every block's logical layout and reduces pool capacity.
- Reusing reserved special-flag bits entangles structural metadata with
  musical flags and requires careful compatibility/version rules.
- PAT4 format/version and validators may need an intentional revision.

**Resource implications**

- No large retained SRAM table.
- Potentially one extra encoded byte per allocated block, rounded through the
  existing four-byte chunk geometry.
- Flash/code cost for migration and validation.

### Allocation-span Option 2 — deterministic slack with no fallback

**Shape**

- Specials-only and automation blocks always use one fixed, inferable slack
  rule. No two-to-one fallback exists.

**Advantages**

- Allocation span derives from content type.
- No new retained metadata or file-format field.
- Free and validation remain simple.

**Risks/tradeoffs**

- Higher predictable capacity cost, or less automation headroom if the chosen
  rule is conservative.
- Allocation can fail even when a smaller-slack block would have fit.
- Content transitions must consistently recompute the deterministic span.

**Resource implications**

- Zero new retained span metadata.
- Pool-capacity impact must be recalculated for realistic and worst-case Scene
  populations.

### Allocation-span Option 3 — retained per-step allocation-span table

**Shape**

- Store the allocated chunk count or slack level for every step separately.

**Advantages**

- Fast O(1) free and relocation.
- Supports fallback and future allocation policies.
- Does not consume pool bytes.

**Risks/tradeoffs**

- Must be rebuilt and validated on every PAT4 load unless added to the wire
  format.
- Becomes another allocator authority that can disagree with address/bitmap.
- Large permanent SRAM cost under this project's constrained Pattern budget.

**Resource implications**

- One byte per step is 896 B per Scene, or 14,336 B for 16 resident Scenes.
  This is far beyond the current `+267 B` session estimate.
- Bit-packing only a slack level reduces the cost but may not encode the full
  span needed for future policies.

### Allocation-span Option 4 — reconstruct span using the address array

**Shape**

- Enumerate all live block starts from the address array and infer each
  allocation boundary from starts plus bitmap/free transitions.

**Advantages**

- No new persistent table or block byte.
- Uses the existing authoritative owner map.

**Risks/tradeoffs**

- Adjacent blocks and occupied slack require a carefully proven inference
  algorithm.
- Freeing one block may require scanning up to 896 addresses.
- Corrupt/duplicate addresses make reconstruction ambiguous.
- Harder to use safely in TIM3 context; strengthens the case for foreground
  handoff of erase.

**Resource implications**

- Little retained SRAM.
- Higher bounded CPU and flash complexity.
- Likely inappropriate for frequent mutation unless cached or limited to
  maintenance/validation.

### Compaction Option 1 — address-array candidate scan

**Shape**

- Scan address entries to identify validated block bases and select a movable
  candidate.

**Advantages**

- Correct with the existing ownership model.
- Can validate back-reference and bitmap consistency before moving.

**Resource implications**

- Minimal retained state: Scene plus a 10-bit scan cursor and perhaps selected
  source/destination.
- Up to 896 address checks per full selection pass; can be split across ticks
  if measurement requires it.

### Compaction Option 2 — per-chunk ownership/start map

**Shape**

- Add metadata distinguishing free, continuation, slack, and block start.

**Advantages**

- Very fast compaction enumeration and stronger invariant checking.

**Resource implications and concern**

- Even two bits per 2,048 backed chunks cost 512 B per Scene, or 8,192 B for
  all Scenes, before any owner ID. This conflicts with the current budget and
  duplicates information already represented by address entries.
- Not recommended without a compelling measured need and explicit approval.

---

## 15. Initial open-question audit (superseded by Section 16)

This is the original audit retained for traceability. Lead responses and the
resulting engineering decisions are applied in Section 16; these entries are
not all still open.

### Concurrency and ownership

1. Should TIM3 continue to call Pattern mutation functions directly, or should
   live erase/record publish commands for foreground execution?
2. If TIM3 remains a producer, is the general queue multi-producer with short
   interrupt-protected publication, or is there a separate ISR SPSC handoff?
3. Are trigger-bit changes part of the service serialization domain, or do
   relocation publications preserve them using a short atomic re-read/write
   critical section?
4. What maximum interrupt-off duration is acceptable for address publication
   and queue insertion?

### Deferred-work identity and ordering

5. Must edits to a viewed non-playing Scene remain supported? Current source
   says yes; if that remains binding, deferred work needs Scene identity.
6. What queue-entry format and exact size will carry Scene, step, operation,
   and payload?
7. After the first deferred request, are later requests serialized strictly,
   coalesced, version-checked, or allowed to bypass it?
8. What are the composition rules for ADD→REMOVE, REMOVE→ADD, DELETE→ADD, and
   CLEAR→later edit?
9. What happens when the queue is full, and how is the failure observable?
10. What happens when total free capacity is insufficient and a queued edit can
    never succeed?

### Public API behavior

11. Do existing manual Menu writers ever return `DEFERRED`, or should manual
    edits remain synchronous while only real-time paths queue?
12. If deferred results are public, which callers must be converted from
    Boolean/void semantics to a result enum or completion-aware transaction?
13. How will target replacement remain atomic when its add/remove/rollback
    sequence can defer?

### Slack and allocation metadata

14. How is allocated span or slack level represented and recovered?
15. Is two-chunk-to-one-chunk fallback retained? If yes, where is the selected
    level encoded?
16. Do existing PAT4 blocks load with zero slack and receive it lazily, or does
    load normalize/relocate them?
17. Does adding span metadata require a PAT4 version change, and how are
    existing hidden A/B records migrated?
18. Is pool usage explicitly allocator occupancy including slack?

### Maintenance and compaction

19. Will Tier 1 candidates be tracked when slack is consumed, or rediscovered
    by a bounded address scan?
20. What prevents the same unserviceable Tier 1 candidate from being retried
    forever?
21. Is Tier 2 triggered by a required allocation size, a fragmentation ratio,
    `total_free - largest_run`, or a fixed largest-run threshold?
22. How can the service run recovery while a blocked edit remains at the queue
    head?
23. What is the fallback when compaction cannot allocate a temporary
    destination span?
24. Should maintenance run only for the playing Scene, for any Scene with
    pending work, or round-robin across all resident Scenes?

### Bulk operations and Scene transitions

25. Do `pat_clearTrack()`, `pat_clearPattern()`, and
    `pat_removeTrackAutomationByTarget()` actually exceed an acceptable
    measured foreground budget?
26. If any becomes asynchronous, which callers are converted to wait for
    completion, especially Euclidean generation?
27. Does a Scene switch ever wait for Pattern service work, or does Scene-
    tagged work make the switch independent?
28. How do complete-region filesystem loads cancel or invalidate deferred work
    for their destination Scenes?

### AutoSave and diagnostics

29. What exact states make `pat_serviceIdleForSnapshot()` true?
30. How is an irrecoverable edit retired so it cannot starve AutoSave?
31. What exact bit packing is used for the three PatternTrace event types?
32. Should queue overflow/capacity failure also be traced despite the current
    decision to omit transient deferred-write records?

### Resources and approval

33. After the above choices, what is the exact retained SRAM1 allocation by
    symbol, lifetime, and owner?
34. Does the queue have 64 usable entries, or 63 entries with a head/tail-only
    full/empty convention?
35. What is the measured foreground cost of a full 896-entry candidate scan,
    one relocation, synchronous track clear, and whole-pattern reset?
36. What are the clean linked text/data/bss deltas in logging-on and
    logging-off configurations?
37. Does the revised allocation still fit within the previously acknowledged
    amount, or is renewed explicit RAM approval required? Under the project
    policy, any increase or materially different retained owner requires that
    approval before implementation.

---

## Conclusion

The repository is ready for a Pattern maintenance service after its mutation
publication primitive and execution-context model are corrected. The safest
near-term decomposition is:

1. implement the independent usage widget;
2. repair and verify playback-safe block publication/erase ordering;
3. decide allocation-span representation;
4. implement maintenance using validated address-array block enumeration;
5. add only the deferred path whose callers and concurrency model are fully
   specified;
6. retain synchronous bulk semantics unless measurement justifies converting
   callers to explicit asynchronous transactions.

The original S067 conclusion that no blocking decisions remain should be
replaced only after the open questions above are resolved and the resulting
retained-state budget is calculated and acknowledged.

---

## 16. Lead-review decisions and selected architecture

This section supersedes the initial alternatives and questions in Sections 14
and 15 where they conflict. It incorporates the lead developer's responses and
settles the implementation-level questions that can be answered from current
source without further product direction.

### 16.1 Binding service model

1. **One mutation target.** Exactly one Scene Pattern is mutable by the stack
   service at a time. No round-robin or per-Scene mutation queues are used.
2. **Arbitrary concurrent readers.** Sequencer may eventually read any number
   or combination of Pattern regions. Readers do not participate in service
   target ownership and never wait for maintenance.
3. **Instant playback switch.** A playback Scene change happens immediately.
   It does not wait for the Pattern service.
4. **Deferred mutation-target handover.** The service retains current and
   requested Scene bytes. It closes stack-event admission, drains accepted old-
   Scene work and any bulk barrier, switches target, reconstructs slack state,
   and reopens admission.
5. **Static trigger state is not a stack event.** Trigger set/clear/toggle stays
   a direct static-array operation. Pool allocation, block content, bitmap,
   specials-present, and offset-address changes belong to the service.
6. **TIM3 does not mutate pool/bitmap state.** Live erase and future live
   recording publish stack commands. Their static trigger effect may happen
   immediately.
7. **Long operations tick and drain.** Track clear, Pattern clear, and
   track-wide target removal use a bulk barrier plus cursor rather than an
   unbounded synchronous pool walk or hundreds of queue entries.
8. **Overflow/final failure policy.** A newly offered event is dropped on queue
   full. An event that cannot be satisfied because of capacity or unresolvable
   fragmentation is also dropped. Each case gets a distinct trace record.
9. **Lazy slack.** Existing and newly loaded blocks are valid with zero slack.
   Tier 1 adds/refills slack lazily.
10. **Pool-use definition.** The widget reports occupancy bits in the 2,048
    backed chunks, including slack.
11. **Maintenance scope.** Tier 1 and Tier 2 mutate only the retained service
    Scene.

The service Scene is the active edit/mutation Scene, currently aligned with the
active Scene rule from Phase 4.3. A viewed/read-only Scene does not become a
mutation target merely because Sequencer or UI reads it. A stack mutation
offered for any other Scene is rejected and traced rather than Scene-tagged and
queued.

### 16.2 Direct writes versus queue-only writes

Use **one private executor with two admission paths**, not a queue-only design:

- Foreground manual/Menu stack edits call the internal service executor
  synchronously when the service is open and idle.
- TIM3/timing-critical work publishes queue events and returns immediately.
- Bulk operations enter the FIFO as barriers and drain from the 500 Hz tick.
- If older queued/bulk work exists, a foreground edit does not bypass it; it is
  rejected and traced through the existing immediate failure contract.

Putting every Menu write through a later 500 Hz drain would change already-
settled behavior. Current Menu code reads the block immediately after a
successful write, and target replacement uses synchronous add/remove/rollback
results. A queue-only design would require pending-aware Menu state and is
outside S067. Enqueue-and-immediately-drain in the same foreground call would
add queue machinery without changing execution.

The single-owner invariant is still satisfied because both paths call the same
private service executor. No external caller changes pool, bitmap, or offset
state directly.

### 16.3 Queue publication and the narrow critical sections

Current source produces service intents from TIM3 and foreground bulk/UI
control paths. Use one 64-entry ring with interrupt-protected publication,
following the existing PatternTrace PRIMASK model.

No pool scan, allocation, free, relocation, block copy, or trace operation runs
with interrupts disabled. Two very short critical sections remain necessary:

1. **Queue publication:** copy one four-byte event and advance its producer
   cursor without a foreground/TIM3 collision.
2. **Address publication:** re-read the latest trigger bit and store the new
   combined trigger/specials/offset halfword without allowing a trigger RMW to
   land between that read and store.

The second window is necessary even though an aligned halfword store is atomic:
the trigger and offset share that halfword. Without an atomic merge, TIM3 can
set a trigger after foreground reads the old entry but before it publishes the
new offset, and the foreground store would erase the new trigger state.

The target window is only a fixed handful of loads/stores and should measure
comfortably below one microsecond at 216 MHz. A zero-critical-section design is
acceptable only if replaced by an equivalently proven atomic merge primitive.

Use monotonically wrapping `uint8_t` producer/consumer cursors. Unsigned
distance identifies empty/full and permits all 64 entries to be usable; array
indexing uses `cursor & 63`. Queue insertion drops the new entry if distance is
already 64.

### 16.4 Event format and ordering

The four-byte entry remains sufficient because Scene is service-global:

```text
bits  0..5   operation
bits  6..15  track*128 + step
bits 16..31  operation payload
```

Use composite events where one musical action changes related block fields.
For example, recording a step with note and velocity performs:

1. immediate trigger-bit set;
2. one queued `SET_NOTE_VELOCITY` carrying both seven-bit values;
3. one service transaction applying both specials.

This is better than separate NOTE and VELOCITY events because it avoids two
block rewrites and prevents readers from seeing a needless note-only
intermediate form. Live erase immediately clears the trigger bit and enqueues
`DELETE_DYNAMIC` for that coordinate.

Queued operations execute strictly FIFO. Adds to a sentinel step are valid.
Removes/deletes are idempotent when already absent. Bulk barriers prevent later
stack events overtaking a clear. Under this no-bypass model, a per-step
generation array is unnecessary; structural address/back-reference validation
remains required but is not used as the ordering mechanism.

### 16.5 Bulk barriers and Euclidean generation

A clear request immediately performs only its bounded static trigger clear. It
does not free pool storage or discard the old offsets needed by the drain. The
queued bulk barrier later walks each affected step and publishes:

```text
latest trigger bit | PAT_ADDR_SENTINEL
```

before retiring the captured old allocation.

Euclidean generation may therefore clear a track and immediately program new
trigger bits. Later stack reclamation preserves those newly set trigger bits.
Stack events accepted after the clear stay behind its barrier and apply after
the corresponding old dynamic content is removed.

This gives the required asynchronous clear behavior without changing Menu or
Euclidean call sites. Whole-Pattern track/scalar reset must be part of the
barrier contract, with static fields reset at request time only where a later
user write must be allowed to supersede them.

### 16.6 Tier 1 maintenance

Do not retain a separate Tier 1 candidate queue. Keep a two-byte round-robin
address-array scan cursor for the service Scene. When queued/bulk work is
absent, inspect a bounded number of entries per tick. A block below its desired
slack level is eligible for one relocation.

If relocation cannot allocate the preferred span:

- automation blocks try the selected two-chunk to one-chunk fallback;
- specials-only blocks try one chunk;
- if neither fits, advance rather than pinning maintenance to that block;
- retry it only on a later full sweep or after occupancy changes.

Tier 1 failure never occupies the edit queue and cannot starve user work,
AutoSave, or mutation-target handover.

### 16.7 Tier 2 maintenance and blocked-head recovery

Enumerate block bases from validated address entries, never arbitrary occupied
bitmap chunks.

Reactive compaction uses the blocked request's real required chunk count:

1. If `total_free < required_chunks`, drop and trace capacity exhaustion.
2. If total free is sufficient but `largest_run < required_chunks`, retain the
   head and enter reactive compaction.
3. Perform at most one improving relocation per eligible tick.
4. Retry the retained head after every successful relocation.
5. If a complete candidate pass makes no progress, drop the head and trace
   unresolvable fragmentation.

Background compaction uses bottom-packing progress rather than a bare
`largest_run < 8` threshold. Move a validated higher block into a lower free
run only when the move improves consolidation. If no improving move exists,
the pass is converged. A candidate without a temporary destination fit is
skipped; a no-progress pass exits rather than spinning.

This resolves the former priority inversion: recovery compaction is allowed
while a blocked head is retained, whereas ordinary background compaction runs
only with no higher-priority work.

### 16.8 AutoSave and complete-region replacement

Expose a Scene-aware stability query rather than only
`pat_serviceBufferEmpty()`:

- a non-service Scene is stable because it cannot own pending mutations;
- the service Scene is stable only when the queue is empty, no bulk cursor,
  recovery, or relocation is active, and no handover is closing it;
- irrecoverable work is dropped/traced, so stability cannot remain false
  forever.

Complete-region filesystem replacement closes service admission when targeting
the service Scene, drains already accepted work, applies the validated region,
rebuilds/invalidates slack state, and reopens. A non-service Scene has no
deferred mutation work and can use the existing bulk apply boundary.

### 16.9 Trace decisions

Add distinct stages for:

- queue overflow;
- capacity drop;
- unresolvable-fragmentation drop;
- compaction relocation;
- Tier 1 micro-relocation;
- two-to-one slack fallback.

For relocation records, aligned chunk indices exactly fill `value32` with the
step identity:

```text
bits  0..9   step identity
bits 10..20  old chunk index
bits 21..31  new chunk index
```

The flags byte carries the four-bit service Scene plus event-specific metric or
slack information. Failure stages may store the original raw four-byte event
as `value32`. Routine successful deferral is not traced.

### 16.10 Slack-map storage decision

A one-bit-per-backed-chunk slack image solves allocation-span ownership. It
allows free/relocation to distinguish trailing slack from logical block chunks
and supports the two-to-one fallback without adding a span byte to each block.

Use one service-local 256-byte `is_slack` bitmap for the current 2,048 backed
chunks.

The upper 256 bytes of each existing 512-byte occupancy bitmap must **not** be
repurposed. They are the reserved occupancy bits for the supported
`PAT_STACK_SIZE` expansion from 256 to 512 sizing units: the pool grows from
8,192 bytes/2,048 chunks to 16,384 bytes/4,096 chunks, at which point all 512
occupancy bytes are required. Reusing that range for slack would silently
couple S067 to the current 8 KiB pool and obstruct the planned Pattern-storage
expansion.

The service-local slack bitmap has these properties:

- current cost is 256 B SRAM1, one bit per current backed chunk;
- it applies only to the one mutable service Scene;
- it is reconstructed on target acquisition by subtracting every validated
  logical block span from occupancy and accepting only valid trailing
  zero-to-two-chunk slack runs;
- target handover is possible because address plus occupancy state remains
  reconstructable;
- PAT4 geometry and version do not change;
- if the pool later expands to 16 KiB, this service-local bitmap expands to
  512 B in the same session as the pool, subject to that future exact RAM
  approval.

Pool occupancy remains the persisted allocator truth. `is_slack` is derived
service metadata used to attribute an occupied trailing chunk to its block; it
is not a second occupancy authority.

### 16.11 Provisional resource implications

| Owner | Bytes | Notes |
|---|---:|---|
| 64 x four-byte queue | 256 | Previously approved |
| service-local slack bitmap | 256 | New; one bit per current backed chunk |
| producer/consumer cursors | 2 | All 64 entries usable |
| current/requested Scene | 2 | One mutation domain, latest request wins |
| service flags/state | 2 | Admission/transition/recovery; verify concretely |
| bulk-operation state | 4 | Operation plus track/step cursor |
| maintenance/recovery cursors | 6–8 | Tier 1, Tier 2, required run/progress |
| pool widget | 1 | Previously approved |
| **Estimated total** | **529–531** | Exact linked result required |

This is provisionally 262–264 B above the original `+267 B` estimate. Packing
and state reuse may recover a few state bytes, but cannot remove the 256-byte
slack bitmap. The exact concrete allocation therefore requires renewed SRAM1
acknowledgement before implementation.

PatternTrace reuses its existing logging-only ring; no production trace ring is
added. Flash and CPU figures remain measurement targets, not approval facts.

### 16.12 Remaining lead-developer decision

The remaining lead input before folding this review into
`S067_DYN_PAT_STACK_SERVICE.md` is retained SRAM acknowledgement: after
concrete state types establish the exact clean-linked delta, approve or reject
the approximately 529–531 B total. The separate slack bitmap necessarily adds
256 B beyond the earlier design.

Everything else in the former Section 15 list is resolved above as an
engineering decision or a verification measurement rather than a product-level
question.
