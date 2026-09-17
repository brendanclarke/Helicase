# S067 — Pattern Stack Service: Detailed Implementation Plan

## Purpose

This document is the authoritative implementation plan for the Session 067
dynamic pattern stack service and pool usage monitor. It synthesises
`S067_DYN_PAT_STACK_SERVICE.md` (the original unified-service design) and
`S067_DYN_PAT_SERVICE_PLAN_FOLLOW-UP.md` (the code-grounded assessment and
lead-review decisions), resolving their differences into concrete
implementation steps grounded in the actual state of the S066 repository.

Where the follow-up's corrections are justified by source evidence, they are
adopted. Where the follow-up introduces unnecessary complexity or
over-engineering, the simpler original design is retained with the specific
correction noted. The lead-review binding decisions from Section 16 of the
follow-up are incorporated as settled architecture.

---

## Binding architectural constraints (from lead review)

1. **One mutation target.** Exactly one Scene Pattern may be mutated by the
   stack service at a time. Any number of Patterns may be read concurrently.

2. **Instant playback switch.** `seq_activePattern` changes immediately at a
   master boundary in TIM3 (`sequencer.c:681`). The playback switch never
   waits for the stack service.

3. **Mutation-target handover.** `service_scene` tracks
   `seq_activePattern`. When `seq_activePattern` changes (TIM3 instant
   playback switch), the service detects the mismatch on its next tick,
   closes stack-event admission, drains accepted work, switches target,
   recounts pool state, and reopens admission. Changeover is fast
   (bounded ~160ms worst case) but not instant. During changeover, edits
   are rejected; the sequencer is already playing the new scene.

4. **Static trigger bits are not stack events.** `pat_setStepActive()`,
   `pat_toggleStep()`, and Euclidean `pat_setStepActive()` remain direct
   static-array operations outside the service. Pool allocation, block
   content, bitmap, specials-present, and offset-address changes belong to
   the service.

5. **TIM3 does not mutate pool/bitmap state.** Live erase
   (`sequencer.c:519`) currently calls `pat_eraseStep()` from TIM3 ISR
   context. Under S067, live erase publishes a `DELETE_DYNAMIC` queue event
   instead. The immediate trigger-bit clear happens at ISR time; pool
   reclamation is deferred.

6. **Lazy gaps.** Existing and newly loaded blocks are valid with zero
   trailing gap. Blocks are always allocated at exactly their logical size.
   Tier 1 adds gaps lazily via relocation. Gap chunks are free space in the
   bitmap, not owned by any block. No PAT4 format change.

7. **Pool-use = occupancy bits.** The widget reports set bits in the 2,048
   backed chunks. All occupied chunks are logical block content.

---

## Part B — Pattern Pool Usage Monitor

Part B has zero coupling to Part A and is implemented and hardware-tested
first. It provides immediate diagnostic value and validates the bitmap-reading
code path that Tier 2 compaction will also use.

### Design

Settings-menu widget showing percentage of the active Scene's dynamic pool
that is occupied. Short name `pts`, category `Pattern`, long name `StoreUse`.
Click-in view shows "Pattern StoreUse". Display as `pts:NN` (2-digit 0–99),
matching `cpu:NN`.

Active Scene only (`seq_activePattern`). Computed **once on menu entry** into
the settings/Global page and retained in a static `uint8_t`. No periodic
refresh, no rolling average. Pattern editing and scene changes don't occur
while the settings menu is accessed.

### Implementation

1. **`pat_poolUsagePercent(scene)` in PatternData.c.** Count set bits in the
   first `PAT_STACK_SIZE` bytes of the Scene's bitmap (the backed range = 256
   bytes at current sizing = 2,048 chunks). Divide by `PAT_STACK_SIZE * 8`,
   multiply by 100.

   The bitmap field in `pat_scene_region_t` lives inside a `__packed` struct.
   Avoid casting the `uint8_t[]` to `uint32_t *` directly — use `memcpy` to
   load each 4-byte word into a local `uint32_t`, then apply
   `__builtin_popcount`. This is safe regardless of alignment, cheap at
   menu-entry time (~256 cycles worst case), and avoids an unaligned-access
   assumption.

   ```c
   uint8_t pat_poolUsagePercent(uint8_t scene) {
       const uint8_t *bm = pat_sceneRegion(scene)->bitmap;
       uint32_t used = 0;
       uint32_t word;
       for (int i = 0; i < PAT_STACK_SIZE / 4; i++) {
           memcpy(&word, &bm[i * 4], 4);
           used += __builtin_popcount(word);
       }
       uint8_t pct = (uint8_t)((used * 100u) / (PAT_STACK_SIZE * 8u));
       return pct > 99u ? 99u : pct;
   }
   ```

   Display saturation: 99 means 99–100% occupancy. This is intentional.

2. **Menu integration.** Take the empty slot at position 7 on Global subpage 2
   (`menuPages.h:43–44`): replace `TEXT_EMPTY`/`PAR_NONE` with
   `TEXT_PAT_STORE_USE`/`PAR_PAT_STORE_USE`.

   Add `menu_patStoreUseWidgetVisible()` modeled on
   `menu_cpuUseWidgetVisible()` (`menu.c:9802`). Unlike the CPU widget, this
   widget has **no** rolling average, no `menu_cpuUseSamples[]` array, and no
   periodic refresh. It reads the retained `uint8_t` directly.

3. **Compute-on-entry.** When the Global settings page is entered, call
   `pat_poolUsagePercent(seq_activePattern)` and store the result in
   `static uint8_t menu_patStoreUsePercent`. The widget reads this byte on
   every repaint. No recomputation during the menu session. A Scene switch
   while the Global page is open leaves the retained value unchanged.

4. **Display.** `menu_formatPatStoreUsePercent3()` and
   `menu_formatPatStoreUsePercent4()` matching the CPU widget format helpers.
   `getMenuItemNameForValue()` renders `pts:NN` for the row view and
   `"Pattern StoreUse"` with the `NN%` value for click-in.

### Part B RAM cost

| Item | Bytes | Region |
|------|------:|--------|
| `menu_patStoreUsePercent` | 1 | .bss (SRAM1) |

**+1 byte SRAM1.**

---

## Part A — Unified Pattern Stack Service

### Current state (from source)

- `PatternData.c` uses a synchronous first-fit allocator (`pat_poolAlloc`,
  line 194) with no compaction. `pat_poolFree` (line 235) releases chunks and
  zeroes pool bytes. No mechanism to recover fragmentation.

- Pool is 8,192 bytes (PAT_STACK_SIZE=256, 2,048 four-byte chunks) per Scene,
  with a 512-byte bitmap (256 backed + 256 permanently occupied for future
  expansion to 512 units).

- The existing writer `pat_writeDynamic()` (line 444) handles same-size
  in-place rewrite, new allocation, and shrink-in-place for removals.

- **Publication ordering issue (confirmed).** `pat_writeDynamic()` lines
  522–528 free the old block *before* publishing the new address. TIM3 can
  preempt after the free (line 525) but before the address write (line 526),
  read the old address, and consume zeroed pool bytes. Similarly,
  `pat_eraseStep()` (line 685) frees pool bytes before writing
  `PAT_ADDR_SENTINEL`. This must be corrected as the first implementation
  gate.

- **TIM3 pool mutation.** `seq_advanceTrackStep()` calls `pat_eraseStep()`
  from ISR context when `seq_eraseActive` is set (`sequencer.c:519`). This is
  the only ISR pool mutation path. Under S067 it becomes a queued event.

- **Scene identity.** The viewed pattern is always the active pattern —
  there is no separate "viewed vs. playing" distinction yet. The service's
  single mutation target tracks `seq_activePattern`. Future per-track
  pattern assignment (`seq_perTrackPattern[7]`) will allow tracks to play
  different scenes, but that is post-S067.

- **Euclidean generation** (`EuklidGenerator.c:283`) calls `pat_clearTrack()`
  then immediately programs trigger bits via `pat_setStepActive()`. The
  trigger-bit writes are static array operations and survive a deferred
  pool-clear.

- **AutoSave scheduler** (`filesystem.c:24060`) already defers snapshots
  when `seq_recordActive || seq_eraseActive`. The service adds
  `patSvc_idle()` to this guard.

### File pair and naming

The service lives in a new file pair: `PatternStackService.c` /
`PatternStackService.h` in `Core/Bank/Scene/Pattern/`. All public service
functions use the `patSvc_*` prefix. The raw data layer in `PatternData.c`
retains its `pat_*` prefix and internal-linkage pool manipulation functions.
The service calls into PatternData's internal functions but owns the routing,
queueing, gap maintenance, and compaction logic.

### Architecture: one private executor, two admission paths

Following the lead-review decision (follow-up §16.2): the service owns ALL
pool mutations through a single private executor. Two admission paths reach
it:

1. **Foreground synchronous path.** When the service is idle (queue empty,
   no bulk cursor, no relocation) and the edit targets the service scene,
   foreground menu edits call the private executor directly. The executor
   runs inline, the existing Boolean return semantics are preserved, and
   menu code changes only in call target (`pat_*` → `patSvc_*`).

2. **Queue path.** When the service is busy (queued or bulk work pending),
   foreground edits are **enqueued** rather than rejected. TIM3-originated
   stack operations (live erase, future live recording) always use this
   path. Events are published as compact 4-byte entries into a 64-entry
   SPSC ring buffer. The 500Hz service tick drains the queue through the
   same private executor. Bulk operations (clear track, clear pattern,
   track-wide target removal) enter as FIFO barriers and drain bounded
   per tick.

   Queued foreground edits return 1 (optimistic success). At 500Hz drain
   and human editing rates, the queued edit executes well before the next
   user interaction. If a queued edit fails on execution (pool exhaustion),
   it is dropped and traced — the caller has already moved on.

   Edits are silently dropped only when the buffer is full (64 entries).
   Buffer overflow is traced but requires no UX indication.

This is not two mutation owners. The service's internal executor is the only
code that changes pool bytes, bitmap bits, and offset-address fields. It is
called synchronously for foreground edits when idle, and by the 500Hz tick
for queued work. Callers never touch pool state directly.

### Gate 1 — Correct publication ordering

Before any service infrastructure, fix the publication primitive that all
later work depends on.

**Current bug:** `pat_writeDynamic()` performs free-old then publish-new.
The correct sequence is:

1. Read current complete address entry (trigger + specials + offset).
2. Allocate disjoint new span.
3. Write complete new block at new offset.
4. Re-read trigger bit under short PRIMASK, compose new address entry with
   latest trigger bit + PAT_ADDR_SPECIALS_BIT + new offset.
5. Atomically publish the new 16-bit address (single aligned `STRH`).
6. Free old span only after publication.

The trigger-bit re-read at step 4 is necessary because trigger bits and the
pool offset share the same 16-bit halfword. Without it, a `pat_setStepActive`
or `pat_toggleStep` from TIM3 between steps 1 and 5 would be silently
overwritten. The critical section is extremely narrow: one load, one OR, one
store — well under 1μs at 216MHz.

**Same-size in-place rewrite** (the `old_chunks == new_chunks` fast path at
line 491) is retained for automation value updates where only the value byte
changes. The current implementation clears and rewrites the entire block
(`pat_blockWrite` at line 316 calls `memset` zero), which is technically
unsafe under TIM3 preemption. There are two options:

- **Option A (simple):** Remove the in-place path; always use
  write-new/swap/free-old, even for same-size. Cost: one extra alloc+free
  cycle per automation value update. This is the safer choice.
- **Option B (optimized):** Restrict in-place to single-byte automation
  value overwrites where the value's position within the block is known and
  the write is a single byte store. This is the fast path needed for future
  live recording. Can be added as a later optimization after the service is
  working.

**Decision:** Option A. Remove the in-place rewrite path entirely; always
use write-new/swap/free-old for same-size block updates. The Gate 6 writer
in-place *append* path (extending a block into an adjacent gap) is a
separate, safe operation — it does not rewrite existing content.

**`pat_eraseStep()` fix:** Detach address first (write `PAT_ADDR_SENTINEL`
with latest trigger bit preserved using the same short PRIMASK re-read),
then free the old pool allocation using the locally captured offset.

**Verification:** Test playback during repeated automation add/grow/shrink/
erase cycles. The sequencer must never read zeroed or partially-written pool
bytes.

### Gate 2 — Service dispatcher and foreground routing

Wrap the existing pool writers in the service dispatcher. This is a routing
refactor, not new pool manipulation code. The service lives in
`PatternStackService.c`; the raw pool functions stay in `PatternData.c`.

**Internal functions (PatternData.c) become non-public:**

- `pat_writeDynamic()` — called by `patSvc_writeStepAutomation()`
- `pat_writeSpecials()` — called by `patSvc_setStepNote()` etc.
- `pat_eraseStep()` internal pool logic — called by queue drain
- `pat_clearTrack()` — called by bulk barrier drain
- `pat_clearPattern()` → uses `pat_initScene()` directly (unchanged)

**Service state (PatternStackService.c):**

```c
static uint8_t service_scene;           // current mutation target
static uint8_t service_open;            // admission gate
```

**Per-track pattern stub (sequencer.c):**

```c
uint8_t seq_perTrackPattern[7];         // +7 bytes SRAM1, approved
```

All entries initialised to `seq_activePattern` at boot and on scene switch.
No per-track assignment logic yet — all tracks always play the active scene.
The array exists so the data path is in place for future per-track pattern
assignment (can reference the hidden 17th scene).

**Public API routing:**

```c
uint8_t patSvc_writeStepAutomation(scene, track, step, target, value) {
    if (!service_open || scene != service_scene)
        return 0;  // rejected: wrong scene or service closed
    if (patSvc_idleForDirectWrite())
        return pat_writeDynamic(scene, track, step, ...);  // sync path
    return patSvc_enqueue(WRITE_AUTOMATION, ...);  // queue path, return 1
}
```

Menu callers change call target from `pat_*` to `patSvc_*`. When the service
is idle, execution is synchronous with the existing return semantics. When
the service is busy, the edit is queued and returns 1 (optimistic success).
Edits for the wrong scene return 0 (rejected).

**Service scene initialization:** At boot, after `pat_initScene()` for all
scenes, set `service_scene = seq_activePattern` and `service_open = 1`.

**`patSvc_idle()` query:** True when: queue empty, no bulk cursor active,
no Tier 1/2 relocation in progress, and no handover pending. Exposed for the
autosave scheduler.

**AutoSave integration:** Add `&& patSvc_idle()` to the existing guard at
`filesystem.c:24075`. The service scene is the only scene with deferred work;
non-service scenes are trivially stable.

### Gate 3 — Edit event queue and TIM3 handoff

**Queue format — 4 bytes per entry:**

| Bits | Width | Content |
|------|-------|---------|
| 5..0 | 6 | Operation type |
| 15..6 | 10 | Step identity (track × 128 + step) |
| 31..16 | 16 | Packed payload (operation-dependent) |

Scene identity is implicit — the queue targets `service_scene`. Events
offered for any other scene are rejected and traced.

**Operations:**

| Type | Payload |
|------|---------|
| `DELETE_DYNAMIC` | unused (live erase pool reclamation) |
| `SET_NOTE_VELOCITY` | note7 in bits 22..16, velocity7 in bits 29..23 |
| `CLEAR_TRACK_BARRIER` | track in bits 18..16 |
| `REMOVE_TRACK_TARGET_BARRIER` | target9 in bits 24..16 |

Future live recording adds `ADD_MODIFY_AUTOMATION` and composite events.
The 6-bit operation field supports up to 64 types.

**Queue structure:**

```c
static uint32_t service_queue[64];
static uint8_t  service_queue_prod;    // monotonic wrapping producer cursor
static uint8_t  service_queue_cons;    // monotonic wrapping consumer cursor
```

Unsigned distance `prod - cons` identifies count. Array index uses
`cursor & 63`. All 64 entries are usable (no empty-slot convention).

**Interrupt-protected publication** following the PatternTrace PRIMASK model:

```c
static uint8_t patSvc_enqueue(uint32_t event) {
    uint8_t dist;
    __asm volatile("cpsid i" ::: "memory");
    dist = service_queue_prod - service_queue_cons;
    if (dist >= 64u) {
        __asm volatile("cpsie i" ::: "memory");
        patternTrace_record(PAT_TRACE_STAGE_QUEUE_OVERFLOW, ...);
        return 0;
    }
    service_queue[service_queue_prod & 63u] = event;
    service_queue_prod++;
    __asm volatile("cpsie i" ::: "memory");
    return 1;
}
```

The critical section is: read distance, conditional store, increment cursor.
At most ~5 instructions, well under 1μs at 216MHz. No pool scan, allocation,
or block copy ever runs with interrupts disabled.

**Address publication also needs a short PRIMASK section** (Gate 1): the
trigger bit and pool offset share a halfword, so the re-read/compose/store
must exclude a concurrent `pat_setStepActive()` or `pat_toggleStep()` from
TIM3.

**TIM3 live erase becomes:**

```c
// In seq_advanceTrackStep(), replacing the current pat_eraseStep() call:
if (seq_eraseActive && track == menu_getActiveVoice()) {
    // Immediate trigger-bit clear (static array op, not a stack event)
    pat_setStepActive(seq_activePattern, track, step, 0);
    // Queue pool reclamation for foreground drain
    patSvc_enqueueErase(seq_activePattern, track, step);
}
```

`patSvc_enqueueErase` validates scene == service_scene, packs a
`DELETE_DYNAMIC` event, and calls `patSvc_enqueue`. If the scene doesn't
match, the event is dropped and traced.

**Queue drain (in `patSvc_tick()`):**

- Peek head entry.
- For `DELETE_DYNAMIC`: read the step's current address. If
  `PAT_ADDR_SENTINEL` already (step was erased again), discard. Otherwise,
  capture offset, detach address (write sentinel with latest trigger), free
  old allocation.
- For other operations: dispatch through the private executor.
- Advance consumer cursor on success.
- On allocation failure: classify as capacity-blocked or
  fragmentation-blocked (see Gate 7).

**Stale detection on dequeue:**

- Adds to `PAT_ADDR_SENTINEL` are **valid** (creating a new block for an
  empty step).
- Removes/deletes are **idempotent** when already absent.
- Back-reference validation catches structural corruption, not ordering.
  Under strict FIFO, ordering is guaranteed by construction; a per-step
  generation counter is unnecessary.

### Gate 4 — Bulk operations as bounded barriers

Track clear and track-wide target removal use the queue as FIFO barriers
with service-local cursors, not hundreds of individual queue entries.
Pattern clear (`pat_initScene()`) remains synchronous — it is a full region
memset, fast enough to run inline without bounding.

**Protocol for track clear:**

1. **At request time:** Clear affected trigger bits immediately (static array
   operation, safe from any context). Enqueue one barrier event
   (`CLEAR_TRACK_BARRIER`).

2. **When the barrier reaches the queue head:** The service starts its
   internal drain cursor. Each tick, the cursor processes a bounded number
   of steps (e.g. 8 per tick for track clear = 16 ticks to complete 128
   steps, ~32ms at 500Hz).

3. **Each cursor step:** Read the current complete address. Atomically publish
   `latest_trigger_bit | PAT_ADDR_SENTINEL`. Retire the captured old
   allocation (pool free + bitmap clear).

4. **Why trigger bits are preserved:** The trigger bit is re-read at cursor
   execution time, not captured at request time. If Euclidean generation sets
   a trigger bit after the clear was requested but before the cursor reaches
   that step, the trigger survives. Pool reclamation removes only the old
   dynamic content.

**Euclidean compatibility:** `euklid_generate()` calls `patSvc_clearTrack()`
then immediately calls `pat_setStepActive()` for the new pattern. Under the
barrier protocol:

- `patSvc_clearTrack()` immediately clears all trigger bits, then enqueues
  a barrier.
- `pat_setStepActive()` immediately sets the new trigger bits.
- The barrier drain later reclaims old pool blocks, re-reading trigger bits
  at execution time. New triggers survive.
- Euclidean generation sets only trigger bits, not pool content, so there is
  no ordering conflict with the pool drain.

**Track-wide target removal:** `pat_removeTrackAutomationByTarget()` becomes
a barrier + bounded cursor that calls `pat_removeStepAutomation()` for one
step at a time, bounded per tick. This is currently a synchronous 128-step
loop.

**Bulk state:**

```c
static uint8_t  bulk_op;              // 0=none, or operation type
static uint8_t  bulk_track;           // affected track (or 0xFF for pattern)
static uint16_t bulk_target;          // for target removal
static uint8_t  bulk_step_cursor;     // 0..127 progress
static uint8_t  bulk_track_cursor;    // reserved for future multi-track ops
```

Later queue events stay behind the barrier until the cursor completes. The
service tick processes bulk work when the barrier is at the queue head.

### Gate 5 — Mutation-target handover

The service tick detects `seq_activePattern != service_scene` and initiates
automatic changeover:

1. Close admission (`service_open = 0`). New queue events and foreground
   edits are rejected/traced.
2. Drain remaining queue events for the old scene.
3. Complete any bulk barrier in progress.
4. Wait for any Tier 1/2 relocation to complete (at most one per tick).
5. Re-read `seq_activePattern` (it may have changed again during drain).
   Set `service_scene = seq_activePattern`.
6. Recount `logical_chunks_used` for the new scene (bitmap popcount).
   Reset gap maintenance scan cursor.
7. Reopen admission (`service_open = 1`).

If `seq_activePattern` changes again during the drain (rapid scene
switching), step 5 catches the latest value. The service always converges
to the current active pattern.

**Handover latency bound:** With at most 64 queue entries drained at 1+ per
tick (2ms), worst case is ~128ms. Bulk barriers add at most 128 steps / 8
per tick = 32ms for a track clear. Total worst case: ~160ms.

Playback scene changes are instant (TIM3) and never wait for the service.
The changeover is fast but not instant — the service needs a few ticks to
drain its buffer and close out the old scene.

### Gate 6 — Gap maintenance and elastic gap policy

**Gap model (replaces conditional slack + is_slack bitmap):**

Blocks are allocated at exactly their logical size — no trailing slack
chunks, no allocation-span metadata. The service maintains free-chunk gaps
after blocks by relocating them, distributing free space as trailing gaps
rather than consolidated runs. The writer (foreground) opportunistically
grows into adjacent free chunks when extending a block (adding automation
or specials). If no adjacent free chunk exists, the writer falls back to
write-new/swap/free-old — the same relocation path used today for
different-size blocks. The gap reduces how often this fallback is needed;
it does not change what happens when no adjacent space exists.

**Why this replaces the 256-byte `is_slack` bitmap:** The original plan
tracked which occupied bitmap chunks were trailing slack using a separate
256-byte per-chunk attribute map. Under the gap model, gaps are ordinary
free space — the pool bitmap already tracks them. Blocks always occupy
exactly their logical chunks. The allocator frees exactly the logical
chunks on deletion. No span metadata, no reconstruction, no per-chunk
attribute map. This saves 256 bytes SRAM1.

**Elastic gap policy:** The desired gap size varies with pool occupancy.
A running `logical_chunks_used` counter (`uint16_t`, +1 on alloc, -1 on
free) tracks logical block occupancy. Occupancy is measured as logical
chunks only — gaps are free and not counted.

| Occupancy | Gap target | Rationale |
|-----------|-----------|-----------|
| Below `PAT_GAP_REDUCE_THRESHOLD` | 2 chunks | Absorbs typical menu edit burst (4 automation additions) without relocation |
| At or above `PAT_GAP_REDUCE_THRESHOLD` | 1 chunk | Preserves capacity; absorbs single-parameter additions |

At very high occupancy (pool nearly full), the service naturally stops
creating even 1-chunk gaps because there is no space to relocate into. No
separate "compact mode" flag is needed — the gap target remains 1, and
allocation failures cause the service to advance its cursor and move on.

```c
#define PAT_GAP_REDUCE_THRESHOLD  60u  // percent occupancy; above this, target = 1
```

**Writer growth path:** When the foreground writer needs to grow a block
(e.g., adding an automation entry to an existing block):

1. Check if the chunk(s) immediately after the block's last logical chunk
   are free in the bitmap.
2. If free: mark occupied in bitmap, write the new data in the newly
   claimed chunk(s), update the block header (auto_count). The address
   entry base offset does not change. Increment `logical_chunks_used`.
3. If not free: fall back to write-new/swap/free-old. Allocate a new
   span elsewhere at the expanded logical size, write the complete
   expanded block, swap the address under PRIMASK, free the old chunks.

Step 2 is an in-place append — the existing block content is not
rewritten, only extended. This is distinct from the in-place *rewrite*
removed in Gate 1 (Option A), which zeroed and rewrote the entire block.
TIM3 safety: new data bytes are written first, then the header
auto_count is incremented (a single byte store in the low byte of the
2-byte header). The sequencer reads auto_count to determine block
extent; seeing the old count means it reads one fewer entry (safe — the
new entry is beyond its read range), seeing the new count means it reads
the new entry (safe — the data is already written before the count
update).

**Interaction with the first-fit allocator:** `pat_poolAlloc()` can
allocate gap chunks for new blocks since they are genuinely free. This
means gaps can be "stolen" by new allocations. The service re-creates
them on its next sweep. Gaps are a best-effort performance optimization,
not a hard guarantee.

**Lazy initialization:** Blocks loaded from PAT4 or existing from before
S067 have no trailing gaps. The service adds gaps lazily via Tier 1
relocation. The `logical_chunks_used` counter is initialized from a
bitmap popcount at boot and on target acquisition.

### Gate 7 — Tier 1 gap maintenance

When queue and bulk work are idle, the 500Hz tick scans for blocks that
would benefit from a trailing gap and relocates one per tick.

**Candidate identification:** A round-robin address-array scan cursor
(`tier1_scan_cursor`, 10-bit, 0–895) checks one entry per tick. A block
is eligible if the number of free chunks immediately following it is less
than the current gap target (determined by `logical_chunks_used` vs.
`PAT_GAP_REDUCE_THRESHOLD`).

**Relocation protocol (write-new/swap/free-old):**

1. Compute required run: `logical_chunks + gap_target`.
2. Find a free run of that size in the pool via first-fit scan.
3. Copy logical block content to the new location (occupy
   `logical_chunks` in the bitmap).
4. PRIMASK: re-read trigger bit, compose new address, publish.
5. Free old logical chunks.

The gap chunks at the new location remain free in the bitmap — they ARE
the gap. No slack tracking needed.

**Failure policy:**
- If allocation of `logical_chunks + gap_target` fails: try
  `logical_chunks + 1` (minimum useful gap). If that also fails, advance
  the cursor and move on. Don't pin maintenance to an unsatisfiable block.
- Tier 1 failure never enters the edit queue and cannot starve user work,
  AutoSave, or mutation-target handover.
- A complete 896-entry sweep with no successful relocations means the pool
  is too full for gap maintenance. This is fine — gaps are a performance
  optimization for future live recording, not a correctness requirement.

**Convergence:** Each block is relocated at most once per full sweep (the
cursor advances past it). A relocated block with a fresh gap is not
re-eligible until its gap is consumed by a writer growth or stolen by a
new allocation. The service cannot dirty-loop on maintenance.

### Gate 8 — Tier 2 global compaction

Periodic housekeeping that consolidates scattered free chunks. Runs only
when: no queue work, no bulk barrier, no Tier 1 work pending, and the
compaction interval has elapsed (`PAT_COMPACT_INTERVAL_MS`, default 100ms,
settable in `config.h`).

**Algorithm:** Enumerate block bases from validated address entries (not
arbitrary bitmap chunks — the bitmap cannot distinguish block starts from
interior chunks). Find a higher occupied block that could be relocated to a
lower free run to improve bottom packing.

**Candidate scan:** Address-array pass split across ticks using a retained
scan cursor. Each tick inspects up to `PAT_COMPACT_SCAN_PER_TICK` entries
(default 16, settable in `config.h`). At most one validated relocation per
eligible tick. Trace the scan cost per tick for hardware measurement.

**Reactive compaction (blocked-head recovery):**

When a queue head fails allocation:

1. If `total_free_chunks < required_chunks`: drop the event, trace capacity
   exhaustion. Compaction cannot help.
2. If `total_free >= required` but `largest_run < required`: retain the head,
   enter reactive compaction mode. Perform one improving relocation per tick,
   retry the head after each success.
3. If a complete candidate pass makes no progress: drop the event, trace
   unresolvable fragmentation.

This resolves the priority inversion from the original plan where compaction
could never run while the queue was non-empty.

**An irrecoverable queued edit is dropped and traced.** It must not
permanently block `patSvc_idle()`, starve AutoSave, or prevent
mutation-target handover.

**Config constants:**

```c
#define PAT_COMPACT_INTERVAL_MS        100u  // settable
#define PAT_COMPACT_FREE_RUN_THRESHOLD   8u  // chunks, default 32 bytes
#define PAT_COMPACT_SCAN_PER_TICK       16u  // entries per tick (adjustable)
```

### Service call site

`patSvc_tick()` is called from the 500Hz `timebase_serviceFrontPanel()`
cadence (`timebase.c:160`), after the existing LED/button SPI exchange and
encoder/pot ticks. The service tick is one additional function call per 2ms
foreground pass.

**Maintenance speed.** Background maintenance (Tier 1 gap maintenance, Tier 2
compaction) runs always, regardless of playback state. During playback, work
per tick is bounded conservatively to avoid impacting the foreground cadence.
When the sequencer is stopped (`!seq_running`), the per-tick bound can be
relaxed (e.g., process 2× the normal scan count) since there is no TIM3 ISR
contention. Config constants control both bounds.

### Service tick priority order

```
patSvc_tick():
  // Priority 0: mutation-target handover
  if (seq_activePattern != service_scene && handover conditions met):
    advance handover state machine
    return

  // Priority 1: drain edit queue (one per tick)
  if (queue not empty && not bulk-active):
    peek head
    if head is barrier: begin bulk drain, return
    if stale (DELETE_DYNAMIC to already-sentinel step): discard, return
    attempt execution through private executor
    if success: advance consumer cursor, return
    if allocation failure: classify and handle (reactive compaction or drop)
    return

  // Priority 2: bulk barrier drain (bounded N per tick)
  if (bulk_op active):
    execute next N single-step operations
    if bulk complete: clear bulk state, advance consumer past barrier
    return

  // Priority 3: Tier 1 gap maintenance (one per tick)
  if (tier1_scan_cursor has not completed full sweep):
    inspect one address entry
    if gap < target: relocate one block to create gap
    return

  // Priority 4: Tier 2 compaction (at configured interval)
  if (compaction_interval elapsed):
    if reactive_compaction_mode: attempt one improving relocation, retry head
    else: run one background compaction step
    return
```

### ISR safety argument (unchanged from S062)

`seq_advanceTrackStep()` at `sequencer.c:504` reads the 16-bit address entry,
then reads pool bytes at the dereferenced offset. The aligned halfword
read/write is atomic on Cortex-M7. The sequencer sees either the old complete
address (reading the old complete block) or the new complete address (reading
the new complete block), never a partial.

The service writes the complete new block **before** publishing the new
address. The old block remains valid until the address swap. An ISR that
starts reading the old block before foreground swaps the address will finish
reading the old block before foreground resumes (foreground cannot run while
TIM3 is executing). This is the standard write-new/swap/free-old safety
argument.

### AutoSave coordination

`filesystem_autosavePatternDrainSchedule_tick()` at `filesystem.c:24060`
already defers snapshots when `seq_recordActive || seq_eraseActive`. The
service adds:

```c
if (!patSvc_idle())
    return;
```

`patSvc_idle()` is scene-aware:
- A non-service scene is trivially stable.
- The service scene is stable only when queue empty, no bulk cursor, no
  relocation active, and no handover closing it.
- Irrecoverable work is dropped/traced, so stability cannot remain false
  forever.

**Maintenance dirty marking:** Tier 1/2 relocations change address, bitmap,
and pool byte offsets. They must call `pat_markSceneDirty()` so AutoSave
persists the updated allocator layout. Without this, a power loss could
restore the pre-maintenance layout, losing consolidation work (functionally
harmless but wasteful).

**Convergence:** A Tier 1 sweep with no eligible candidates, or a Tier 2
pass with no improving moves, does not call `pat_markSceneDirty()` and does
not trigger an unnecessary AutoSave cycle.

### Filesystem bulk replacement boundary

Filesystem Pattern load/save replaces entire `pat_scene_region_t` contents
via `pat_sceneRegionMut()`. When targeting the service scene:

1. Close admission.
2. Drain queue, complete bulk barrier.
3. Apply the bulk region replacement.
4. Recount `logical_chunks_used` from the new bitmap. Reset gap
   maintenance scan cursor.
5. Reopen admission.

For non-service scenes: no deferred work exists, so the existing direct
replacement is safe.

### Trace events

Add the following stages to PatternTrace:

| Stage | Meaning |
|-------|---------|
| `Q` | Queue overflow (newly offered event dropped) |
| `C` | Capacity exhaustion (queued event dropped, compaction can't help) |
| `F` | Unresolvable fragmentation (queued event dropped after failed compaction) |
| `R` | Tier 2 compaction relocation |
| `M` | Tier 1 gap maintenance relocation |
| `G` | Gap fallback (target-gap failed, minimum 1-chunk gap used) |
| `X` | Service scene rejected (event for wrong scene dropped) |

For relocation records, `value32` packs:

```
bits  0..9   step identity (track*128 + step)
bits 10..20  old chunk index
bits 21..31  new chunk index
```

The `flags` byte carries the 4-bit service scene and event-specific metadata.
Routine successful direct writes are not traced.

---

## RAM budget

| Item | Bytes | Region | Owner |
|------|------:|--------|-------|
| Edit event queue (64 × 4B) | 256 | .bss (SRAM1) | PatternStackService.c |
| Producer/consumer cursors | 2 | .bss (SRAM1) | PatternStackService.c |
| Service scene + flags | 2 | .bss (SRAM1) | PatternStackService.c |
| Bulk operation state | 4 | .bss (SRAM1) | PatternStackService.c |
| Tier 1/2 cursors and recovery state | 6 | .bss (SRAM1) | PatternStackService.c |
| Logical chunks used counter | 2 | .bss (SRAM1) | PatternStackService.c |
| Per-track pattern array | 7 | .bss (SRAM1) | sequencer.c |
| Pool widget retained value | 1 | .bss (SRAM1) | menu.c |
| **Total** | **280** | | |

**Approved: +280 bytes SRAM1, ceiling +300.** The gap-maintenance model
eliminates the 256-byte `is_slack` bitmap from the earlier revision. The
per-track pattern array (`seq_perTrackPattern[7]`) is a stub for future
per-track scene assignment — all entries equal `seq_activePattern` for now.

---

## Build budget target

| Metric | S066 baseline | S067 target delta |
|--------|--------------|-------------------|
| text | 439,396 | +~800–1,200 (service + queue + bulk + widget) |
| data | 412 | 0 |
| bss | 290,852 | +280 (approved, ceiling +300) |

The text estimate is larger than the original +600–900 because of the queue
publication protocol, PRIMASK sections, reactive compaction, bulk barrier
drain, and gap maintenance logic. Exact deltas to be measured from clean
linked images.

---

## Implementation order

1. **Part B: Pool usage monitor.** Add `pat_poolUsagePercent()`, menu
   integration, compute-on-entry hook. Hardware-test empty, partly-used, and
   dense scenes.

2. **Gate 1: Fix publication ordering.** Correct `pat_writeDynamic()` and
   `pat_eraseStep()` to detach-address-first, free-old-last. Add PRIMASK
   trigger-bit preservation. Remove or restrict in-place block rewrite.
   Hardware-test playback during repeated automation growth/shrink/erase.

3. **Gate 2: Service dispatcher.** Create `PatternStackService.c/.h` with
   `patSvc_*` prefix. Wrap existing writers in service dispatch. Foreground
   synchronous path with scene guard; queue when busy. Wire
   `patSvc_idle()` into autosave scheduler. Update callers to `patSvc_*`.

4. **Gate 3: Edit queue.** Add 64-entry ring buffer with PRIMASK-protected
   publication. Convert `pat_eraseStep()` TIM3 call to queue event. Add
   drain to service tick. Test live erase during playback.

5. **Gate 4: Bulk barriers.** Convert `pat_clearTrack()` to barrier+cursor
   protocol. `pat_clearPattern()` stays synchronous (memset). Convert
   `pat_removeTrackAutomationByTarget()` to barrier. Test Euclidean
   clear-then-fill ordering. Test Menu clear operations.

6. **Gate 5: Mutation-target handover.** Implement automatic changeover
   when `seq_activePattern` changes. Test rapid scene switching during
   playback with pending queue work.

7. **Gate 6: Gap maintenance.** Add elastic gap policy with
   `PAT_GAP_REDUCE_THRESHOLD`. Add `logical_chunks_used` running counter.
   Add writer in-place growth path (append into adjacent free chunk).
   Verify counter agrees with bitmap popcount across add/remove/clear
   cycles.

8. **Gate 7: Tier 1 gap maintenance.** Address-array scan cursor. One
   gap-creating relocation per idle tick. Fallback policy. Convergence
   test.

9. **Gate 8: Tier 2 compaction.** Address-array candidate enumeration.
   Reactive compaction for blocked heads. Background bottom-packing.
   Config constants. Trace events.

10. **Hardware stress test.** Repeated add/remove/grow/shrink cycles. Playback
    during relocation and erase. Scene switching with pending work.
    Euclidean generation and clear operations. AutoSave admission after
    maintenance. PAT4 save/load and hidden A/B restore after compaction.
    Pool widget agreement with host-side bitmap count.
    `logical_chunks_used` agreement with bitmap popcount after each
    operation type. Writer in-place append path vs. write-new/swap/free-old
    fallback under gap-present and gap-absent conditions.

---

## Resolved design decisions

All follow-up items from the initial plan revision are resolved. Decisions
are captured here for reference; the plan body above reflects them.

1. **RAM: approved.** +280 bytes SRAM1 (ceiling +300). Gap-maintenance
   model eliminated the 256-byte `is_slack` bitmap. Per-track pattern
   array adds +7 bytes.

2. **In-place rewrite: removed (Option A).** Always write-new/swap/free-old
   for same-size block updates. The safe in-place *append* path (Gate 6)
   is a separate, safe operation that does not rewrite existing content.

3. **Scene model: viewed = active = service.** No separate viewed vs.
   playing distinction. `service_scene` tracks `seq_activePattern`
   automatically. `seq_perTrackPattern[7]` stub array allocated (+7 bytes)
   — all entries equal `seq_activePattern` for now. Future per-track
   assignment can reference the hidden 17th scene.

4. **Foreground edits queue when busy.** Edits are enqueued (not rejected)
   when queued or bulk work is pending. Return 1 (optimistic success).
   Silently dropped only when the buffer is full (64 entries) — traced,
   no UX indication required.

5. **Bulk clear: pattern clear stays synchronous** (memset via
   `pat_initScene()`), track clear becomes bounded async barrier.
   `copyClearTools` stale-reference window is acceptable — cleared by UI
   transition. Copy/clear operations will be reworked in a later session.

6. **Compaction scan: split across ticks now.** Retained scan cursor,
   `PAT_COMPACT_SCAN_PER_TICK` (default 16) in `config.h`. Trace the
   scan cost per tick for hardware measurement.

7. **Header byte order: big-endian.** Block header is written high byte
   first (`PatternData.c:317`). The `pat_blockWrite()` comment (line 289)
   already documents "big-endian header and little-endian automation
   words." `PATTERN_DYNAMIC_STACK.md` should be updated to match the
   implemented order. This is documentation only — reader and writer agree.

8. **Maintenance: always active.** Runs at all times regardless of
   playback state. Bounded conservatively during playback; can run faster
   (2× scan bound) when sequencer is stopped (`!seq_running`).

9. **Callers: verified from source.** Complete pool mutation caller set
   confirmed by grep — the list below matches actual call sites. Service
   lives in `PatternStackService.c/.h` with `patSvc_*` prefix:

   - `patSvc_writeStepAutomation()` — menu.c
   - `patSvc_removeStepAutomation()` — menu.c
   - `patSvc_setStepNote()` — menu.c
   - `patSvc_setStepVolume()` — menu.c
   - `patSvc_setStepProbability()` — menu.c
   - `patSvc_eraseStep()` — sequencer.c (TIM3 via queue), menu.c
   - `patSvc_clearTrack()` — copyClearTools.c, EuklidGenerator.c
   - `patSvc_clearPattern()` — copyClearTools.c
   - `patSvc_removeTrackAutomationByTarget()` — menu.c
   - `pat_sceneRegionMut()` — filesystem.c (bulk load/save, excluded from
     service routing but needs the quiesce boundary from Gate 2)

10. **Pool expansion: no S067 concern.** No service-local slack bitmap to
    expand. Service state scales to larger pool with no additional RAM.
