# S067 — Dynamic Pattern Stack Service Agent & Pool Usage Monitor

## Session goal

Implement two features from `SCOPING_TARGETS.md` Phase 4:

1. **Pattern dynamic stack defragmentation service agent** — background service
   that maintains pool health via micro-relocation (slack refill), global
   compaction, and bounded per-tick work.
2. **Pattern pool usage monitor** — a settings-menu widget (like the existing CPU
   monitor) showing the percentage of the current Scene's dynamic pattern pool
   that is occupied.

---

## Part A — Unified Pattern Stack Service Agent

### Current state

`PatternData.c` uses a deterministic first-fit allocator with no compaction
(§3 of `PATTERN_DYNAMIC_STACK.md`). Blocks are contiguous; free clears exactly
the occupied span. Once a pool fragment develops (e.g. from erasing a step that
sat between two others, or from growing a block that forces relocation), the gap
is never reclaimed. Over a long editing session the pool can reach a state where
total free bytes are sufficient for a new block but no single contiguous run is
large enough. The allocator currently returns failure in this case and the
edit/automation-write is silently dropped.

The existing pool writers (`pat_writeStepAutomation()`,
`pat_removeStepAutomation()`, `pat_writeSpecials()`, `pat_eraseStep()`,
`pat_clearTrack()`, `pat_clearPattern()`) each directly manipulate pool bytes,
bitmap state, and address entries. There is no coordination layer between them,
and no mechanism to defer a write that cannot be performed immediately (e.g.
because of pool exhaustion or a concurrent snapshot).

### Architecture — Unified Stack Service

The stack service is a **single module in `PatternData.c`** that owns **all**
pool mutation paths: the existing step writers, micro-relocation, and global
compaction. It presents a unified API to callers and maintains a **single
edit event buffer** that holds any edit operation that momentarily cannot be
performed via direct write.

This unification is required because multiple conditions can block direct
pool writes (pool exhaustion, mid-relocation, multi-edit drain), and having
separate buffers for separate agents creates fragile coordination. A single
service with a single edit event buffer drains the same queue regardless of
what condition blocked the direct write.

#### Dispatch model — synchronous with edit buffer fallback

The service executes write requests **immediately in the caller's context**
(direct write). The caller invokes e.g.
`pat_service_writeAutomation(scene, track, step, param, value)`, and the
service performs the write inline and returns success. This preserves existing
behavior: menu edits remain instant.

If direct write is unavailable — pool exhaustion, or the service is mid-
relocation — the request is queued in the **edit event buffer** and the caller
receives a "deferred" status. The buffer drains on the next service tick when
direct write is available again.

#### AutoSave snapshot coordination (existing mechanism, no new allocation)

`pat_snapshotScene()` copies the entire `pat_scene_region_t` into a dedicated
10,519-byte staging buffer (`pat_autosave_snapshot`, `PatternData.c:61`). The
SD drain reads this staging buffer, not the live pool. Both the snapshot and
any pool write are foreground-only, so they **cannot interleave** — the
`memcpy` is atomic from the foreground's perspective.

The existing coordination is in `filesystem_autosavePatternDrainSchedule_tick()`
at `filesystem.c:24060`: it already defers the snapshot when pool-mutating
activity is in progress (`seq_recordActive || seq_eraseActive`). The unified
service plugs into this same pattern: the scheduler additionally checks
whether the service has pending buffered work (non-empty edit buffer) before
calling `pat_snapshotScene()`. **The snapshot waits for writes to be idle;
writes never need to suspend for the snapshot.** No suspension flag, no write
buffering, and no new filesystem.c API are required for this case.

#### Edit event buffer

A fixed-size ring buffer of **single edit events** for operations that could
not be performed via direct write. The buffer holds user-facing edit
operations only — service-internal maintenance (relocation, compaction) is
**not** stored in the buffer; those operations are what *block* direct write
and *cause* buffering.

Sources that feed the buffer:

1. **Pool-exhaustion fallback.** A write that fails allocation is queued; the
   service tick runs compaction to free space, then drains buffered entries
   via direct write.
2. **Mid-relocation fallback.** If the service is performing a relocation
   (write-new/swap/free-old sequence) when a new edit arrives, the edit is
   buffered until the relocation completes.
3. **Multi-edit drain overflow.** Bulk operations (clear track, clear pattern)
   use their own bounded async drain that feeds single events at a bounded
   rate per tick. Those events go direct-write when available, buffer when
   not.

**Entry format — 4 bytes per entry:**

| Bits | Width | Content |
|------|-------|---------|
| [15:6] | 10 | Step identity (`track * 128 + step`, 0..895) |
| [5:0] | 6 | Operation type (up to 64 types) |
| [31:16] | 16 | Packed data (operation-dependent) |

**Operation types:**

| Type | Data encoding |
|------|---------------|
| ADD_MODIFY_AUTOMATION | `(value7 << 9) \| target9` — same packed format as pool block (`PatternData.c:329`) |
| ADD_MODIFY_SPECIAL | byte 0 = flag (`PAT_SPECIAL_NOTE_BIT` / `VEL_BIT` / `PROB_BIT`), byte 1 = value |
| REMOVE_AUTOMATION | `target9` in low 9 bits, high 7 bits unused |
| REMOVE_SPECIAL | flag in byte 0 (which special to remove), byte 1 unused |
| REMOVE_ALL_AUTOMATION | data unused |
| REMOVE_ALL_SPECIALS | data unused |
| DELETE_STEP | data unused |

**64 entries × 4 bytes = 256 bytes.** This provides generous headroom for
any realistic burst scenario. The common path is synchronous (direct write);
the buffer fills only during pool exhaustion or mid-relocation. Worst-case
analysis: fast editing during pool exhaustion generates ~1 entry per service
tick (foreground is single-threaded); compaction frees space within a few
ticks, allowing drain. 64 entries absorbs multiple seconds of sustained
exhaustion.

**Scene target:** the buffer writes to the **active edit scene**, which is
tracked by the service (not stored per-entry). The scene target is always
known from `seq_activePattern`.

**Scene-switch protocol:** on active scene change, the service (1) stops
accepting new edit events, (2) drains the edit buffer to completion,
(3) completes any in-progress multi-edit drain, (4) switches the active edit
scene target, and (5) re-enables the buffer. At 1+ entries per tick, 64
entries drain in ≤128ms — imperceptible.

**Stale detection:** on dequeue, the service checks the step's current
address entry. If the step no longer exists (`PAT_ADDR_SENTINEL`) or the
block's back-reference doesn't match, the entry is silently discarded. No
per-entry stale address is stored — the 4-byte entry is self-contained.

**Multi-edit bounded drain:** bulk operations (clear track = up to 128 steps,
clear pattern = up to 896 steps) are not single buffer entries. Each has its
own async progress state inside the service (operation type + current
track/step cursor). The drain executes a bounded number of single-step
operations per service tick, using direct write when available or falling back
to the edit buffer. This keeps per-tick work bounded and avoids blocking the
foreground for the duration of a bulk clear.

**Visibility to the autosave scheduler:** the edit buffer exposes a
`pat_serviceBufferEmpty()` query (also true when no multi-edit drain is
in progress). The filesystem Pattern drain scheduler
(`filesystem_autosavePatternDrainSchedule_tick()`) checks this alongside
`seq_recordActive || seq_eraseActive` before calling `pat_snapshotScene()`.
This ensures the snapshot is never taken while a deferred write, relocation,
or bulk drain is pending.

#### Service API

The unified service replaces direct pool manipulation. Callers continue to
use the same function signatures, but the implementation routes through the
service dispatcher.

**Single-edit operations** (direct write with edit-buffer fallback):

- `pat_writeStepAutomation(scene, track, step, param, value)` → service
- `pat_removeStepAutomation(scene, track, step, param)` → service
- `pat_writeNote(scene, track, step, note)` → service (via specials)
- `pat_writeVelocity(scene, track, step, velocity)` → service (via specials)
- `pat_writeProbability(scene, track, step, prob)` → service (via specials)
- `pat_eraseStep(scene, track, step)` → service

**Multi-edit operations** (bounded async drain, not single buffer entries):

- `pat_clearTrack(scene, track)` → service starts bounded drain
- `pat_clearPattern(scene)` → service starts bounded drain

Callers in `menu.c`, `copyClearTools.c`, `sequencer.c`, and
`EuklidGenerator.c` require **no changes** — the function signatures are
unchanged. The internal implementation changes from direct pool manipulation
to service dispatch.

**Excluded from service routing:** `filesystem.c` Pattern load/save, which
replaces entire `pat_scene_region_t` contents via `pat_sceneRegionMut()`.
These are bulk region operations that occur during dedicated load/save
commands where no concurrent edits are possible. The service owns
incremental mutations; bulk replacement remains direct.

#### Tier 1 — Micro-relocation (slack refill)

Service-internal maintenance, **not an edit buffer entry**. Relocation is
what *blocks* direct write and *causes* buffering, not what goes into the
buffer.

When a write causes a block to grow beyond its allocated span (slack
exhausted), the write itself succeeds synchronously (the existing
write-new/swap-pointer/free-old inline relocation in `pat_writeBlockInternal()`
handles it). The service then notes that the block's slack is depleted.

On subsequent service ticks, when the edit buffer is empty and no multi-edit
drain is in progress, the service scans for blocks with no trailing slack and
relocates one per tick — allocate a new span with fresh slack, copy the
block, swap the pointer, free the old span.

This is the same write-new/swap-pointer/free-old discipline described in
`SCOPING_TARGETS.md` §4.3. One relocation per service tick, bounded. During
a relocation, incoming edit requests go to the edit buffer rather than direct
write (the pool is mid-mutation).

#### Tier 2 — Global compaction

Service-internal maintenance (like Tier 1, not an edit buffer entry).
Periodic housekeeping that consolidates scattered free chunks into larger
contiguous runs. Runs only when:
- No higher-priority work is pending (edit buffer empty, no multi-edit drain,
  no Tier 1 relocation in progress)
- The pool's fragmentation metric exceeds the threshold
- The compaction interval has elapsed (settable via `config.h`)

**Algorithm**: walk the bitmap from the top of the pool downward, find the
highest occupied block that has free space below it. Read the block's 2-byte
header to extract the 10-bit back-reference (`track = backref / 128`,
`step = backref % 128`). Relocate it to the lowest free span that fits.
One block per pass, bounded. This gradually pushes occupied blocks toward
the bottom and free space toward the top.

**Bounded work**: at most one relocation per tick. The scan is cheap
(bitmap word tests with CTZ). The relocation follows the same
write-new/swap-pointer/free-old protocol.

**Trace logging**: compaction events (block relocated from→to, trigger metric
value) are logged in the pattern trace so timing and frequency can be
monitored and the compaction interval adjusted if needed.

### Invariants (from `SCOPING_TARGETS.md` §4.3)

1. **Single-owner pool access.** The unified stack service is the **sole**
   code path that mutates pool bytes, bitmap state, and address entries.
   All existing writers route through the service. The only exception is
   `filesystem.c` bulk region replacement during Pattern load/save, which
   occurs under exclusive foreground access with no concurrent edits.

2. **Stale-request resolution.** On dequeue the service checks the step's
   current address entry. If the step no longer exists (`PAT_ADDR_SENTINEL`)
   or the block's back-reference doesn't match, the request is discarded.
   This catches erases and clears that occurred while the request was
   buffered. No per-entry stale address is stored — the 4-byte entry is
   self-contained; validation uses the live address array.

3. **Bitmap visibility timing.** A chunk is marked occupied only as part of
   the same operation that makes it real (fully written, step-ID set, ready to
   be pointed at). The defragmenter only encounters blocks with valid step-ID
   back-references.

### Decisions taken

| # | Question | Decision |
|---|----------|----------|
| D1 | **Who owns pool writes?** | **B (unified service)** — a single module owns ALL pool mutations with a single edit event buffer for deferred operations. The existing direct writers become internal implementations called by the service dispatcher. Multiple conditions can block direct writes (pool exhaustion, mid-relocation, multi-edit drain) — a single coordinated buffer avoids separate agents with separate queues. |
| D2 | **Slack reservation** | **Conditional by block type (refined by F2).** Automation blocks (`auto_count > 0`): 2-chunk (8 bytes) preferred, 1-chunk (4 bytes) fallback. Specials-only blocks: single-chunk slack only. When the first automation entry is added, the block is reallocated with 2-chunk slack. Rationale: the D2 menu-page 4-parameter scenario applies to automation writes, not specials. This saves ~448 bytes of pool capacity for 112 specials-only steps. |
| D3 | **Compaction trigger metric** | **B (largest contiguous free run < threshold).** Threshold is settable in `config.h` as `PAT_COMPACT_FREE_RUN_THRESHOLD` (default 8 chunks = 32 bytes). |
| D4 | **Tier 2 compaction rate** | **B (every Nth tick).** Interval settable in `config.h` as `PAT_COMPACT_INTERVAL_MS` (default 100ms = 50 ticks at 500Hz). Compaction events logged in pattern trace. |
| D5 | **Edit event buffer** | **64 entries × 4 bytes = 256 bytes.** Fixed-size ring buffer of single edit events. Each entry: 10-bit step identity + 6-bit operation type + 16-bit packed data. The buffer holds user-facing edit operations only; service-internal maintenance (relocation, compaction) is not stored in the buffer. Multi-edit operations (clear track, clear pattern) use their own bounded async drain, feeding single events into the buffer at a bounded rate per tick. Scene is implicit (active edit scene, tracked by the service). |
| D6 | **AutoSave snapshot interaction** | **Uses the existing coordination mechanism.** The filesystem Pattern drain scheduler (`filesystem_autosavePatternDrainSchedule_tick()`) already defers snapshots when pool-mutating activity is active (`seq_recordActive \|\| seq_eraseActive`). The service adds one additional check: `pat_serviceBufferEmpty()` (true when edit buffer is empty AND no multi-edit drain is in progress). Both snapshot and service are foreground-only, so they cannot interleave. The snapshot copies to a dedicated staging buffer; the SD drain reads the copy. No suspension flag or write-deferral needed for this case. |

**RAM approved**: +256 bytes SRAM1 for the edit event buffer, plus service
overhead (see RAM budget below).

### Risks

1. **ISR interaction.** `seq_advanceTrackStep()` in TIM3 reads address entries
   and pool bytes. A Tier 1/2 relocation that swaps the address `STRH` is
   atomic (aligned 16-bit), but the TIM3 reader could see the new address while
   the pool still contains old content at the new offset (if the write-new step
   hasn't completed). **Mitigation**: the service writes the full new block
   before swapping the address. The ISR reads pool bytes only after
   dereferencing the address, so it sees either the old block (old address) or
   the fully written new block (new address), never a partial. The old block
   remains valid until the address swap, so the ISR can read from it safely.
   This is the same argument that makes the existing write-new/swap/free-old
   pattern correct.

2. **Pool exhaustion during relocation.** The service needs to allocate new
   space before freeing old space. If the pool is so full that there's no room
   for new+slack, the relocation is deferred. **Mitigation**: compaction
   consolidates adjacent free chunks to make room. In the pathological case
   (pool is 100% full with no free chunks at all), the service can do
   nothing — but this is also the case where no growth is possible anyway, so
   it's self-consistent. User edits arriving during a relocation go to the
   edit buffer and drain when the relocation completes.

3. **Stale buffer entries after step erase.** A buffered step might be erased
   by the user before the service processes it. **Mitigation**: the service
   checks the step's current address entry on dequeue. If the address is
   `PAT_ADDR_SENTINEL` or the back-reference doesn't match, discard the entry.

4. **Scene switch during active buffer.** The edit buffer may hold entries
   for the old scene when the user switches scenes. **Mitigation**: the
   scene-switch protocol drains the buffer and completes any multi-edit drain
   before switching the active edit scene target. At 1+ entries per tick,
   64 entries drain in ≤128ms.

### RAM budget

| Item | Bytes | Region | Lifetime |
|------|------:|--------|----------|
| Edit event buffer (64 × 4B) | 256 | .bss (SRAM1) | static |
| Ring buffer head/tail | 2 | .bss (SRAM1) | static |
| Compaction state (scan position, target) | 4 | .bss (SRAM1) | static |
| Multi-edit drain state (op type + cursor) | 4 | .bss (SRAM1) | static |
| **Total** | **266** | | |

Per-entry cost: 4 bytes (10-bit step identity + 6-bit operation type + 16-bit
packed data). 64 entries, power-of-2 for ring buffer index masking. No
per-entry stale address — validation uses the live address array on dequeue.

The service exposes `pat_serviceBufferEmpty()` (true when the ring buffer is
empty AND no multi-edit drain is in progress) so the autosave scheduler can
wait for pending operations.

**+266 bytes SRAM1 for Part A. Buffer allocation (256 bytes) approved.**

### Implementation sketch

```
pat_service_writeAutomation(scene, track, step, target, value):
  if service is mid-relocation or pool-exhausted:
    enqueue(ADD_MODIFY_AUTOMATION, track*128+step, (value7<<9)|target9)
    return DEFERRED
  result = pat_writeBlockInternal(...)  // existing logic, with slack
  if result == POOL_EXHAUSTED:
    enqueue(ADD_MODIFY_AUTOMATION, track*128+step, (value7<<9)|target9)
    return DEFERRED
  mark pattern dirty
  return OK

pat_service_tick():                     // called from 500Hz cadence
  // Priority 1: drain edit buffer
  if buffer not empty:
    entry = peek head
    step_id = entry.destination >> 6
    if step address is PAT_ADDR_SENTINEL: discard, continue
    attempt direct write for entry
    if success: dequeue, mark dirty
    else: break (pool still full, try compaction next)
    return

  // Priority 2: multi-edit drain (bounded N per tick)
  if multi-edit drain in progress:
    execute next N single-step operations via direct write
    if direct write fails: enqueue to edit buffer
    if drain complete: clear drain state
    return

  // Priority 3: Tier 1 micro-relocation (slack refill)
  if scan finds block with no trailing slack:
    relocate one block (write-new/swap-pointer/free-old with slack)
    log to pattern trace
    return

  // Priority 4: Tier 2 compaction (every PAT_COMPACT_INTERVAL_MS)
  if compaction interval elapsed:
    scan bitmap for largest free run
    if largest free run >= PAT_COMPACT_FREE_RUN_THRESHOLD: return
    find highest occupied block with free below
    read 10-bit backref from block header
    track = backref / 128, step = backref % 128
    relocate one block (write-new/swap-pointer/free-old)
    log to pattern trace
    mark pattern dirty
```

---

## Part B — Pattern Pool Usage Monitor

### Design

A settings-menu widget showing the percentage of the active Scene's dynamic
pattern pool that is occupied. Short name `pts`, category `Pattern`, long name
`StoreUse` — so the single-parameter click-in view shows "Pattern StoreUse".
Display as `pts:NN` (2-digit 0-99), matching the existing `cpu:NN` format.

Active Scene only. The value is computed **once on menu entry** into the
settings menu and retained in a static `uint8_t` for display. No continuous
polling, no rolling average, no periodic refresh. Since pattern editing and
scene changes are not intended to occur while the user accesses the settings
menu, the value does not need to update during the menu session.

### Implementation

1. **Computation**: `pat_poolUsagePercent(scene)` — count set bits in the
   first 256 bytes of the Scene's bitmap (the backed range), divide by 2048,
   multiply by 100. Result is 0..100, clamped to 99 for display. Uses
   `__builtin_popcount` on 32-bit words for speed (~128 cycles on Cortex-M7).

   ```c
   uint8_t pat_poolUsagePercent(uint8_t scene) {
       const uint8_t *bm = pat_sceneRegion(scene)->bitmap;
       uint32_t used = 0;
       for (int i = 0; i < 64; i++)
           used += __builtin_popcount(((const uint32_t *)bm)[i]);
       return (uint8_t)((used * 100u) / 2048u);
   }
   ```

2. **Menu integration**: add `TEXT_PAT_STORE_USE` / `PAR_PAT_STORE_USE` to the
   global settings page (`menuPages.h:43`), taking the empty slot at position 7
   (currently `TEXT_EMPTY`/`PAR_NONE`). A new `menu_patStoreUseWidgetVisible()`
   modeled on `menu_cpuUseWidgetVisible()` gates display. Unlike the CPU widget,
   this widget does NOT use a rolling average or periodic refresh — it reads the
   retained `uint8_t` directly.

3. **Compute-on-entry**: when the settings menu is entered, call
   `pat_poolUsagePercent(seq_activePattern)` and store the result in
   `menu_patStoreUsePercent` (1 byte static). The widget reads this byte on
   every repaint. No recomputation during the menu session.

4. **RAM cost**: 1 byte SRAM1 (.bss) for the retained display value.

5. **Active Scene selector**: `seq_activePattern` (the sequencer's playing
   Scene, which is what the ISR reads from).

### Decisions taken

| # | Question | Decision |
|---|----------|----------|
| M1 | **Naming** | Short `pts`, category `Pattern`, long `StoreUse`. Click-in shows "Pattern StoreUse". |
| M2 | **Active scene or all scenes?** | Active scene only. |
| M3 | **High-water mark?** | No. Single compute-on-entry, retain the number, display it. |

---

## Ambiguities and open items

1. **Live-record interaction (from §4.3a).** The unified service is designed to
   support live recording's real-time-safe path (in-place value overwrites need
   no service; only new-parameter first-writes consume slack). Live recording
   itself is not implemented yet and is not part of S067. The service's
   reserved-slack mechanism, edit event buffer, and scene-switch protocol are
   forward-compatible.

2. **Copy operations (Phase 4.5).** `pat_copyTrack`, `pat_copyPattern`, and
   `pat_copyBar` are still no-ops. Copy would duplicate blocks and rebuild
   bitmap ownership from scratch — a bulk operation like Pattern load, not an
   incremental service request. Copy operations would drain the edit buffer
   and complete any multi-edit drain (`pat_serviceBufferEmpty()` guard),
   perform the bulk copy, then resume normal service ticking.

3. **CGRAM overlay coexistence.** The pool monitor is a settings-page widget
   and doesn't interact with the S066 VOICE overlay's CGRAM underline cache.
   No conflict.

4. **Probability-applies-to-automation (S066 bug).** The chaselight and
   probability bugs identified in S066 are tracked in `SCOPING_TARGETS.md` and
   are independent of the stack service. If probability is changed to suppress
   automation (not just trigger), the service's relocation logic is unaffected —
   it operates on stored data, not playback decisions.

5. **Existing direct pool writers are now internal to the service.** Under
   decision D1 (unified service), `pat_writeStepAutomation()`,
   `pat_removeStepAutomation()`, `pat_writeSpecials()`, `pat_eraseStep()`,
   `pat_clearTrack()`, and `pat_clearPattern()` become internal implementations
   called by the service dispatcher. Their function signatures are preserved for
   external callers; the routing through the service is transparent.

6. **`pat_sceneRegionMut()` scope.** The service needs mutable access to the
   pool and bitmap. It uses `pat_sceneRegionMut()` and calls `pat_markSceneDirty()`
   afterward, maintaining the existing AutoSave ownership chain.

---

## Implementation order

1. Add `pat_poolUsagePercent()` to PatternData.c.
2. Add `TEXT_PAT_STORE_USE`/`PAR_PAT_STORE_USE` to `menuPages.h` slot 7,
   `menu_patStoreUsePercent` static, compute-on-settings-entry hook, and
   `menu_patStoreUseWidgetVisible()`.
3. Hardware-test the pool widget.
4. Refactor existing pool writers into service-internal functions: add the
   unified service dispatcher and synchronous fast path. Wire
   `pat_serviceBufferEmpty()` into the autosave drain guard chain.
   Existing callers unchanged; behavior identical.
5. Add the edit event buffer (64 × 4-byte ring buffer, operation type
   dispatch, stale detection on dequeue). Wire deferred-write path for
   pool-exhaustion and mid-relocation cases.
6. Add multi-edit bounded drain for `pat_clearTrack()` and
   `pat_clearPattern()`: async progress state, bounded N per tick, direct
   write with buffer fallback.
7. Add scene-switch protocol: drain buffer, complete multi-edit drain,
   switch active edit scene target.
8. Add conditional slack reservation (D2 + F2) to `pat_writeBlockInternal()`:
   `PAT_SLACK_DOUBLE` for automation blocks, `PAT_SLACK_SINGLE` for specials.
9. Add Tier 1 micro-relocation: service-internal scan for blocks with no
   trailing slack, one relocation per tick when buffer is empty.
10. Add Tier 2 compaction: bitmap scan for largest free run, one-block
    relocator, `config.h` threshold and interval. Add pattern trace events
    (F3): compaction, micro-relocation, and slack fallback.
11. Hardware-test with a dense editing session (many adds/erases to fragment
    the pool, verify the service recovers space and the pool widget reflects
    it).

---

## Build budget target

| Metric | S066 baseline | S067 target delta |
|--------|--------------|-------------------|
| text | 439,396 | +~600-900 (unified service + multi-edit drain + widget) |
| data | 412 | 0 |
| bss | 290,852 | +267 (service 266 + widget 1, approved) |

---

## Pre-Implementation Assessment

### Part B assessment — Pool Usage Monitor (decisions applied)

The pool usage monitor is straightforward and low-risk. The existing CPU-use
widget (`PAR_RUNTIME_CPU_USE` on `menuPages.h:44`, `menu.c:9802-9869`) is a
clean template. Key differences from the CPU widget:

1. **Menu integration is simple.** The global settings page (`menuPages.h:43`)
   has one empty slot at position 7 (`TEXT_EMPTY`/`PAR_NONE`). The new
   `TEXT_PAT_STORE_USE`/`PAR_PAT_STORE_USE` takes that slot. A new
   `menu_patStoreUseWidgetVisible()` modeled on `menu_cpuUseWidgetVisible()`
   gates display. No page restructuring needed.

2. **Computation is cheap.** ~128 cycles on Cortex-M7. Negligible.

3. **No averaging, no periodic refresh.** Unlike the CPU widget's 10-sample
   rolling average and 500ms refresh, the pool widget computes once on
   settings-menu entry and retains the `uint8_t` result. The value is stable
   (changes only on manual edits, not during menu access). The CPU widget's
   `menu_cpuUseSamples[]`/`menu_cpuUseSampleIndex`/etc. infrastructure is NOT
   copied — just one `uint8_t menu_patStoreUsePercent`.

4. **1 byte RAM cost.** The retained display value. Approved.

5. **Active Scene selector**: `seq_activePattern`.

6. **Naming**: short `pts`, long `StoreUse`, category `Pattern`. Click-in
   shows "Pattern StoreUse".

**Recommendation:** implement Part B first as a standalone deliverable. It has
zero coupling to Part A and provides immediate diagnostic value during Part A
development.

### Part A assessment — Unified Stack Service Agent (decisions applied)

The unified service architecture is a bigger refactor than the original
"direct + service" plan, but it is the right foundation. The existing writers
are already correct and ISR-safe; the refactor wraps them in a dispatch layer
without changing their internal logic. Key points:

#### What the unified design gets right

1. **Single-owner pool access (SCOPING_TARGETS §4.3 invariant 1).** The
   unified service is the only code path that mutates pool bytes, bitmap, and
   address entries. This is a stronger guarantee than "direct + service," where
   two independent agents share the bitmap and must coordinate via generation
   counters. Stale detection is now a simple address-entry check on dequeue
   (no generation counter needed).

2. **The write-new/swap-pointer/free-old protocol is already proven.** Every
   existing block writer in `PatternData.c:450-530` (`pat_writeBlockInternal()`)
   follows this discipline. The service keeps these as internal implementations;
   it adds a dispatch/buffer layer on top, not new pool manipulation code.

3. **The ISR safety argument (Risk 1) is unchanged.** `seq_advanceTrackStep()`
   at `sequencer.c:504` reads the address entry, then pool bytes. The aligned
   16-bit read/write is atomic. The sequencer sees either the old or new block,
   never a partial. The unified service preserves this invariant because all
   pool mutations follow the same write-new/swap-pointer/free-old protocol.

4. **AutoSave snapshot uses existing coordination.** `pat_snapshotScene()`
   copies to a dedicated staging buffer (`pat_autosave_snapshot`,
   `PatternData.c:61`). The SD drain reads the staging buffer, not the live
   pool. Both the snapshot and the service are foreground-only — they cannot
   interleave. The existing `filesystem_autosavePatternDrainSchedule_tick()`
   already defers snapshots during `seq_recordActive || seq_eraseActive`; the
   service adds `pat_serviceBufferEmpty()` to that check. No suspension flag,
   no new filesystem.c API, no write-deferral needed for this case.

5. **Conditional slack (D2 + F2) balances capacity against relocation
   frequency.** Specials-only blocks: 1-chunk slack (2 chunks total, 2× base
   cost). Automation blocks: 2-chunk preferred, 1-chunk fallback. For 112
   specials-only steps, 1-chunk slack costs 896 bytes (10.9% of pool) vs.
   1,344 bytes (16.4%) for unconditional 2-chunk. The 2-chunk cost is paid
   only for automation blocks where the multi-parameter editing scenario
   actually applies.

#### Structural concerns resolved by the unified design

1. **Service call site (former Q1).** The service tick runs from the 500Hz
   `timebase_serviceFrontPanel()` cadence (option b). The service dispatcher
   (synchronous fast path) runs in the caller's context at call time; the
   service tick handles four priorities: (1) edit buffer drain, (2) multi-edit
   drain, (3) Tier 1 micro-relocation, (4) Tier 2 compaction. This is
   consistent with D4's compaction rate.

2. **File placement (former Q2).** The service lives in `PatternData.c` where
   it has natural access to the static pool/bitmap helpers. The existing
   writers become `static` internal functions; the service API is the new
   public interface.

3. **Snapshot guard (former Q3).** No new flag or filesystem.c API needed.
   The existing drain scheduler defers snapshots during active pool mutations;
   the service adds `pat_serviceBufferEmpty()` to that existing guard chain.

4. **Active scene selector (former concern 3).** The service uses
   `seq_activePattern` for the cross-scene safety invariant, matching what
   the ISR reads from.

5. **Back-reference scan (former concern 6).** Now explicit in the Tier 2
   algorithm description: read the 10-bit backref from the block header,
   compute `track = backref / 128`, `step = backref % 128`.

#### Remaining capacity concern

**Conditional slack (D2 + F2) affects `pat_blockChunks()`.** The allocation
size calculation takes a `slack_level` parameter: `PAT_SLACK_SINGLE` (1 chunk,
for specials-only blocks) or `PAT_SLACK_DOUBLE` (2 chunks, for blocks with
automation). The caller passes the appropriate level based on the block's
`auto_count`. For `PAT_SLACK_DOUBLE`, the 2-chunk→1-chunk fallback applies
when the larger allocation fails without relocation. This affects every path
through `pat_writeBlockInternal()`: `pat_writeSpecials()`,
`pat_writeStepAutomation()`, and `pat_removeStepAutomation()`, each selecting
the slack level from the block's content type.

---

## Resolved Questions

These questions from the pre-implementation assessment are now resolved by the
decisions taken above. Retained for design-rationale traceability.

### Service placement and scheduling

**Q1. Main-loop call site.** → **Resolved: (b) 500Hz foreground cadence.**
The service tick (`pat_service_tick()`) runs from
`timebase_serviceFrontPanel()`. The synchronous fast path runs in the caller's
context; only buffer drain and compaction run at 500Hz.

**Q2. File placement.** → **Resolved: `PatternData.c`.** The unified service
lives in `PatternData.c` where it has natural access to the static pool/bitmap
helpers. The existing writers become `static` internal functions.

### AutoSave coordination

**Q3. Snapshot guard.** → **Resolved: uses existing scheduler coordination.**
`filesystem_autosavePatternDrainSchedule_tick()` already defers snapshots when
`seq_recordActive || seq_eraseActive`. The service adds one check:
`pat_serviceBufferEmpty()`. No new flag or filesystem.c API needed. The
snapshot copies to a dedicated staging buffer; the SD drain reads the copy.

**Q4. Snapshot race.** → **Resolved: safe by construction.** Both the service
and `pat_snapshotScene()` are foreground-only and cannot interleave. The SD
drain reads the staging buffer, not the live pool. No runtime guard is needed
for the drain phase.

### Slack reservation

**Q5. Unconditional vs. conditional slack.** → **Resolved: conditional by
block type (D2 + F2).** Blocks with automation (`auto_count > 0`) get
2-chunk preferred slack with 1-chunk fallback. Specials-only blocks get
single-chunk slack (the minimum alignment unit). When the first automation
entry is added to a specials-only block, it is reallocated with 2-chunk
slack. This saves ~448 bytes of pool capacity for 112 specials-only steps
while preserving the D2 benefit for the automation path that needs it.

**Q6. Defer slack to live recording?** → **Resolved: implement now.** The
slack mechanism is part of the unified service foundation. Slack gives the
service a reactive signal: blocks with depleted slack are candidates for
Tier 1 micro-relocation on subsequent service ticks. Without slack, the
service has no signal for proactive maintenance. The capacity cost is
acceptable (specials-only blocks use 1-chunk slack; automation blocks use
2-chunk preferred with 1-chunk fallback).

### Pool capacity and fragmentation

**Q7. Compaction threshold.** → **Resolved: settable via `config.h`.** Default
8 chunks (32 bytes). `PAT_COMPACT_FREE_RUN_THRESHOLD` in `config.h`. Can be
tuned after hardware testing.

**Q8. Metric computation timing.** → **Resolved: only on Tier 2 turn.** The
bitmap scan runs only when the compaction interval has elapsed AND no
higher-priority work (buffered writes, Tier 1) is pending. Not every tick.
Cost: ~200 cycles every `PAT_COMPACT_INTERVAL_MS` (default 100ms). Negligible.

### Resource allocation

**Q9. RAM budget.** → **Resolved: +266 bytes SRAM1 for Part A, +1 byte for
Part B = +267 total.** Edit event buffer: 64 × 4 bytes = 256 bytes (approved).
Ring buffer head/tail: 2 bytes. Compaction state: 4 bytes. Multi-edit drain
state: 4 bytes. No suspension flag needed (existing scheduler coordination).

**Q10. Flash/text budget.** → **Updated estimate: +600–900 bytes text.** The
unified service adds a dispatch layer (~150 bytes), edit buffer management
(~100 bytes), multi-edit bounded drain (~150 bytes), scene-switch protocol
(~50 bytes), and Tier 1/2 maintenance (~300 bytes) on top of the original
estimate. The pool widget adds ~100 bytes.

**Q11. CPU budget.** → **Unchanged.** ~250 cycles worst case per relocation
tick; ~200 cycles for bitmap scan every 100ms. Negligible.

### Interaction with existing code

**Q12. VOICE overlay coordination.** → **Resolved by the unified service.**
The VOICE overlay calls `pat_writeStepAutomation()`, which now routes through
the service dispatcher. All writes are serialized through the same code path.
No cross-agent coordination needed.

**Q13. Queue flush on clear.** → **Resolved: no flush needed.** Stale
detection handles it. On dequeue, the service checks the step's current
address entry; a cleared step's address is `PAT_ADDR_SENTINEL`, so the
entry is silently discarded.

**Q14. Stale entry accumulation.** → **Resolved: eager drain.** The service
tick checks the head entry's step address (live address array) before
attempting execution. Stale entries (step erased or cleared since queuing)
are discarded immediately, keeping the buffer available for new requests.

### Scope and phasing

**Q15. Tier 1 + Tier 2 or Tier 1 only?** → **Resolved: both tiers.** Tier 2
is simple (one block relocation per compaction tick), settable via `config.h`,
and logged in the pattern trace. It provides automatic recovery from
fragmentation during long editing sessions. The pool widget (Part B) gives
visibility into whether compaction is keeping up.

**Q16/Q17. Is the service needed before live recording?** → **Resolved: yes.**
The unified service is the required foundation for SCOPING_TARGETS §4.3
invariant 1 (single-owner pool access). It provides: (a) automatic
fragmentation recovery after add/erase cycles, (b) the edit event buffer
and multi-edit drain infrastructure that live recording will also use,
(c) the scene-switch protocol that ensures correct target handover. The
service's complexity is bounded (the existing writers are unchanged
internally; the dispatch layer is new but mechanical) and the pool widget
provides immediate diagnostic value.

---

## Follow-up decisions applied

### F1. RAM budget — Resolved

Edit event buffer redesigned: 64 × 4-byte entries = 256 bytes, plus 10 bytes
service state = 266 bytes total. Approved as new allocation. No suspension
flag needed (autosave uses existing scheduler coordination).

### F2. Slack allocation scope — Decided: automation-only

Two-chunk slack (D2) applies only to blocks with `auto_count > 0` or on the
first automation write. Specials-only blocks get **single-chunk slack** (the
minimum alignment unit, 4 bytes). When the first automation entry is added,
the block is reallocated with 2-chunk slack.

Capacity impact: for 112 specials-only steps, this saves ~448 bytes of pool
capacity compared to unconditional 2-chunk slack (specials-only blocks cost
2 chunks instead of 3). The D2 two-chunk benefit is preserved for the
automation path that actually needs it (menu-page 4-parameter edits and
future live recording).

Implementation: `pat_blockChunks()` takes a `slack_level` parameter:
`PAT_SLACK_SINGLE` (1 chunk, for specials-only blocks) or `PAT_SLACK_DOUBLE`
(2 chunks, for blocks with automation). The caller passes the appropriate
level based on the block's `auto_count`. The 2-chunk→1-chunk fallback (D2)
applies only to `PAT_SLACK_DOUBLE` requests.

### F3. Pattern trace events — Decided: three event types

Log the following in the pattern trace:
1. **Compaction** — block relocated from→to offset, trigger metric value,
   step coordinates
2. **Micro-relocation** — step coordinates, old→new offset, new slack level
3. **Slack fallback** — step coordinates, 2-chunk failed, 1-chunk succeeded

Skip deferred-write events (too transient for post-hoc analysis). Each event
type gets a distinct trace tag for offline filtering.

---

## Open items remaining

No blocking decisions remain. Implementation can proceed in the order
specified above. The first hardware test (step 3, pool widget) provides
immediate diagnostic value and validates the bitmap-reading code path that
Tier 2 compaction will also use.
