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

## Part A — Defragmentation Service Agent

### Current state

`PatternData.c` uses a deterministic first-fit allocator with no compaction
(§3 of `PATTERN_DYNAMIC_STACK.md`). Blocks are contiguous; free clears exactly
the occupied span. Once a pool fragment develops (e.g. from erasing a step that
sat between two others, or from growing a block that forces relocation), the gap
is never reclaimed. Over a long editing session the pool can reach a state where
total free bytes are sufficient for a new block but no single contiguous run is
large enough. The allocator currently returns failure in this case and the
edit/automation-write is silently dropped.

### Architecture

The service agent is a **polled state machine** called from the main-loop
foreground service, bounded to a small fixed amount of work per tick (matching
the project's burst-reduction discipline from `BURST_REDUCTION.md` /
Session 027). It has two tiers:

#### Tier 1 — Micro-relocation (slack refill)

When a step's block grows and its trailing slack is exhausted, the step is
queued for a slack refill:

1. Allocate a new span (old block size + 4 bytes slack, rounded to chunks).
2. Copy the existing block into the new span.
3. Atomically swap the 16-bit address entry to the new offset.
4. Free the old span in the bitmap.

This is the same write-new/swap-pointer/free-old discipline described in
`SCOPING_TARGETS.md` §4.3. The work is scoped to a single step, so it is cheap
enough to run reactively — one or two micro-relocations per foreground service
tick.

**Queue**: a small ring buffer of `(scene, track, step)` tuples. Only the
currently playing Scene's entries are actionable (cross-scene safety invariant).
Duplicate detection: if a step is already queued, skip it.

#### Tier 2 — Global compaction

Periodic housekeeping that consolidates scattered free chunks into larger
contiguous runs. Runs only when the pool's fragmentation metric exceeds a
threshold and no Tier 1 work is pending.

**Algorithm**: walk the bitmap from the top of the pool downward, find the
highest occupied block that has free space below it, relocate it to the lowest
free span that fits. One block per pass, bounded. This gradually pushes
occupied blocks toward the bottom of the pool and free space toward the top.

**Bounded work**: at most one relocation per tick. The scan itself is cheap
(bitmap word tests with CTZ). The relocation follows the same
write-new/swap-pointer/free-old protocol.

### Invariants (from `SCOPING_TARGETS.md` §4.3)

1. **Single-owner pool access.** The service agent is the only code path that
   mutates pool bytes or bitmap state (apart from the existing PatternData
   write helpers, which must be unified into this service or explicitly
   coordinated).

2. **Stale-request resolution.** A per-step generation counter, incremented on
   every edit and checked before applying a service response, prevents a late
   relocation from clobbering a newer edit.

3. **Bitmap visibility timing.** A chunk is marked occupied only as part of
   the same operation that makes it real (fully written, step-ID set, ready to
   be pointed at). The defragmenter only encounters blocks with valid step-ID
   back-references.

### Decisions needed before implementation

| # | Question | Options | Recommendation |
|---|----------|---------|----------------|
| D1 | **Who owns pool writes?** Should PatternData's existing `pat_writeStepAutomation()` etc. continue to write directly, with the service as a separate relocator? Or should all pool mutations be funneled through the service? | A: direct + service; B: service-only | **A (direct + service)** — the existing direct writers are working and tested. The service adds relocation-only. Existing writers and the service coordinate through the bitmap (both respect it as single-source-of-truth for occupancy) and the generation counter (service checks before applying). |
| D2 | **Slack reservation on every write or only on relocation?** | A: every `pat_write*` call appends 4B slack; B: only relocations add slack | **A** — always reserve a 4-byte slack chunk after each block. This absorbs one in-place growth (an added automation entry) before requiring relocation. Cost: one extra chunk per block worst case. Benefit: the common case of adding a second automation never triggers relocation. |
| D3 | **Compaction trigger metric** | A: free-chunk count exceeds N; B: largest-free-run < M chunks; C: periodic timer | **B** — largest contiguous free run dropping below a threshold (e.g. 8 chunks = 32 bytes, enough for a reasonably large block) triggers compaction. Easy to compute from bitmap scan. |
| D4 | **Tier 2 rate** | A: every tick; B: every Nth tick; C: idle-only | **B** — every 100ms (50 ticks at 500Hz service rate). Compaction is background housekeeping, not urgent. |
| D5 | **Queue size for Tier 1** | A: fixed 8; B: fixed 16; C: dynamic | **A** — 8 entries. At musical tempos the gap between step edits that exhaust slack is large. 8 is generous. Each entry is 3 bytes (scene, track*128+step as uint16_t) = 24 bytes. |
| D6 | **Interaction with AutoSave snapshot** | Must the service quiesce during Pattern AutoSave admission? | **Yes.** The snapshot is a `memcpy` of the entire `pat_scene_region_t`. If a relocation is mid-swap during the copy, the snapshot can contain an inconsistent address/pool pair. Gate: the service checks `autosave_patternSnapshotInFlight()` and skips its tick. |

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
   for new+slack, the relocation fails. **Mitigation**: the service skips the
   step, leaves it queued, and tries again after other frees. Compaction can
   still consolidate adjacent free chunks to make room. In the pathological
   case (pool is 100% full with no free chunks at all), the service can do
   nothing — but this is also the case where no growth is possible anyway, so
   it's self-consistent.

3. **Stale queue entries after step erase.** A queued step might be erased by
   the user before the service processes it. **Mitigation**: the service
   validates the step's address entry before acting. If the address is
   `PAT_ADDR_SENTINEL` or the back-reference doesn't match, skip it.

### RAM budget

| Item | Bytes | Region | Lifetime |
|------|------:|--------|----------|
| Tier 1 queue (8 × 3B) | 24 | .bss (SRAM1) | static |
| Queue head/tail/count | 3 | .bss (SRAM1) | static |
| Compaction state (current scan position, target) | 4 | .bss (SRAM1) | static |
| Per-step generation counter | *see below* | | |
| **Total (excluding generation)** | **31** | | |

**Generation counter**: the cheapest option is a single `uint8_t` per step
(7 × 128 = 896 bytes). This is expensive. Alternative: a single global
generation counter (`uint16_t`, 2 bytes) that the service checks against the
step's address entry — if the address changed since the request was queued,
the request is stale. This is nearly as safe and costs 2 bytes instead of 896.
**Recommendation**: use the global-generation approach (2 bytes). The service
records the step's address at queue time and compares on dequeue. If different,
skip.

**Total with generation: 33 bytes.** Requires user RAM approval per policy.

### Implementation sketch

```
pat_defragService_tick():
  if autosave snapshot in flight: return
  if tier1 queue not empty:
    entry = dequeue
    if entry.scene != active scene: discard, return
    validate address, back-ref, bitmap
    allocate new span (old size + slack)
    if allocation fails: re-queue, return
    memcpy old block to new span
    atomic STRH new address
    free old span in bitmap
    mark pattern dirty
    return
  if tier2 due (100ms elapsed):
    scan bitmap for fragmentation metric
    if largest free run >= threshold: return (healthy)
    find highest occupied block with free below
    relocate one block (same protocol as tier1)
    mark pattern dirty
```

---

## Part B — Pattern Pool Usage Monitor

### Design

A settings-menu widget identical in style to the existing CPU usage display
(`audioCodec_getQueueFreePercent()` / the `cpu` global parameter). Shows the
percentage of the current Scene's pool that is occupied, as a 0-99 display.

### Implementation

1. **Computation**: `pat_poolUsagePercent(scene)` — count set bits in the
   first 256 bytes of the Scene's bitmap (the backed range), divide by 2048,
   multiply by 100. Result is 0..100, clamped to 99 for display (matching the
   CPU widget's 2-digit format). Can use `__builtin_popcount` on 32-bit words
   for speed (8 words × 32 bits covers 256 bytes at 256 bits per 32 bytes).

   ```c
   uint8_t pat_poolUsagePercent(uint8_t scene) {
       const uint8_t *bm = pat_sceneRegion(scene)->bitmap;
       uint32_t used = 0;
       for (int i = 0; i < 64; i++)
           used += __builtin_popcount(((const uint32_t *)bm)[i]);
       return (uint8_t)((used * 100u) / 2048u);
   }
   ```

2. **Menu integration**: add a `pat` or `pol` entry to the global settings page
   (the same page that has the `cpu` widget). Use `DTYPE_MENU` or a read-only
   display like `cpu`. The value is computed fresh on each settings-page repaint,
   not polled continuously. Uses the active Scene index
   (`scene_getActiveIndex()`).

3. **RAM cost**: zero. Computed on demand from existing bitmap data.

4. **Display**: `pol:NN` or `pat:NN` as a 3-char short name plus 2-digit
   value. Follows the same formatting as `cpu:NN`.

### Menu page integration

The existing global settings page in `menuPages.h` has available cells. The
pool widget would be added as a new `PAR_POOL_USE` parameter with `DTYPE_MENU`
read-only display (or the same mechanism `cpu` uses). The short name `pol` and
long name `PatPool` are suggested; the user may prefer different labels.

### Decisions needed

| # | Question | Recommendation |
|---|----------|----------------|
| M1 | **Short name** (`pol`, `pat`, `pul`, `dyn`)? | `pol` (pool) — 3 chars, distinct from existing names |
| M2 | **Active scene or all scenes?** | Active scene only. Showing all 16 would require a different widget. |
| M3 | **Include high-water mark?** | Not in the first pass. Can add later as a second widget or a toggle. |

---

## Ambiguities and open items

1. **Live-record interaction (from §4.3a).** The service is designed to support
   live recording's real-time-safe path (in-place value overwrites need no
   service; only new-parameter first-writes consume slack). Live recording
   itself is not implemented yet and is not part of S067. The service's
   reserved-slack mechanism is forward-compatible with it.

2. **Copy operations (Phase 4.5).** `pat_copyTrack`, `pat_copyPattern`, and
   `pat_copyBar` are still no-ops. The service has no interaction with copy —
   copy would duplicate blocks and rebuild bitmap ownership from scratch.

3. **CGRAM overlay coexistence.** The pool monitor is a settings-page widget
   and doesn't interact with the S066 VOICE overlay's CGRAM underline cache.
   No conflict.

4. **Probability-applies-to-automation (S066 bug).** The chaselight and
   probability bugs identified in S066 are tracked in `SCOPING_TARGETS.md` and
   are independent of the defragmentation service. However, if probability is
   changed to suppress automation (not just trigger), the service's relocation
   logic is unaffected — it operates on stored data, not playback decisions.

5. **Existing direct pool writers.** `pat_writeStepAutomation()`,
   `pat_removeStepAutomation()`, `pat_writeSpecials()`, `pat_eraseStep()`,
   `pat_clearTrack()`, and `pat_clearPattern()` all mutate pool bytes and bitmap
   directly. Under decision D1 (direct + service), these continue to work as-is.
   The service's relocation adds a new mutation path that follows the same
   bitmap/address protocol. The key coordination point is that the existing
   writers must also reserve trailing slack (decision D2) so that the service's
   micro-relocation queue stays small.

6. **`pat_sceneRegionMut()` scope.** The service needs mutable access to the
   pool and bitmap. It must use `pat_sceneRegionMut()` and call the dirty helper
   afterward, maintaining the existing AutoSave ownership chain.

---

## Implementation order

1. Add `pat_poolUsagePercent()` to PatternData.
2. Add the `pol` widget to the settings menu page.
3. Hardware-test the pool widget.
4. Add the Tier 1 micro-relocation queue and service tick.
5. Add slack reservation to existing block writers.
6. Add the Tier 2 compaction scan and single-block relocator.
7. Add the AutoSave snapshot guard.
8. Hardware-test with a dense editing session (many adds/erases to fragment the
   pool, verify recovery).

---

## Build budget target

| Metric | S066 baseline | S067 target delta |
|--------|--------------|-------------------|
| text | 439,396 | +~200-400 (service + widget) |
| data | 412 | 0 |
| bss | 290,852 | +33 (service state, pending approval) |

---

## Pre-Implementation Assessment

### Part B assessment — Pool Usage Monitor

The pool usage monitor is straightforward and low-risk. The existing CPU-use
widget (`PAR_RUNTIME_CPU_USE` on `menuPages.h:44`, `menu.c:9802-9869`) is a
clean template: a read-only global-settings cell with periodic repaint, a
rolling-average display, and a custom edit-mode detail screen. Key observations:

1. **Menu integration is simple.** The global settings page (`menuPages.h:43-44`)
   has one empty slot (`TEXT_EMPTY`/`PAR_NONE` at position 7). A new
   `PAR_POOL_USE` parameter can take that slot, or the second (currently all-empty)
   page row. A new `TEXT_POOL_USE` short name and a parallel
   `menu_poolUseWidgetVisible()` modeled on `menu_cpuUseWidgetVisible()` complete
   the integration. No page restructuring needed.

2. **Computation is cheap.** The bitmap is 256 active bytes (2,048 chunks).
   Reading 64 `uint32_t` words and summing `__builtin_popcount()` is ~128 cycles
   on Cortex-M7 (hardware CLZ-based popcount). The result (`used * 100 / 2048`)
   fits in `uint8_t`. This is negligible even if called on every repaint.

3. **No averaging needed.** Unlike CPU pressure (which fluctuates per render
   pass), pool usage changes only on manual edits and is stable between them.
   A rolling average would mask the user's recent edit — showing the live
   instantaneous value is more useful. The CPU widget's 10-sample average and
   500ms refresh should NOT be copied; compute on demand during repaint.

4. **Zero RAM cost.** No static state needed; compute from the live bitmap on
   each repaint. The only new state is the `PAR_POOL_USE` enum value.

5. **Active Scene selector:** `seq_activePattern` (which aliases
   `scene_getActiveIndex()` after Session 054's realignment fix) is the correct
   source. The widget shows the playing Scene's pool, not a browsed Scene.

**Recommendation:** implement Part B first as a standalone deliverable. It has
zero coupling to Part A and provides immediate diagnostic value during Part A
development.

### Part A assessment — Defragmentation Service Agent

The service agent is the more complex and architecturally sensitive feature.
The plan is well-considered and aligns with `SCOPING_TARGETS.md` §4.3's
design. Several points need resolution before implementation.

#### What the plan gets right

1. **Decision D1 (direct + service) is correct for this session.** The existing
   `pat_writeSpecials()`, `pat_writeStepAutomation()`, `pat_removeStepAutomation()`,
   `pat_eraseStep()`, and `pat_clearTrack()` at `PatternData.c:372-830` are
   working, tested, and ISR-safe (aligned 16-bit pointer swaps after complete
   writes). Funneling all writes through the service would be the SCOPING_TARGETS
   §4.3 invariant 1 design, but it requires rewriting every writer and is far
   more work than adding relocation alongside. The coordination mechanism (bitmap
   as truth, address comparison for stale detection) is sufficient for the
   service-as-relocator-only model.

2. **The write-new/swap-pointer/free-old protocol is already proven.** Every
   existing block writer in `PatternData.c:450-530` (`pat_writeBlockInternal()`)
   already follows this exact discipline: allocate new, write complete block,
   atomic `STRH` pointer swap, free old. The service adds one more consumer of
   the same protocol, not a new one.

3. **The ISR safety argument (Risk 1) is sound.** `seq_advanceTrackStep()` at
   `sequencer.c:504` reads the address entry, then reads pool bytes at that
   offset. The Cortex-M7 aligned 16-bit read is atomic. The sequencer will see
   either the old address (old block, still valid) or the new address (new block,
   fully written). There is no window where the address points at a partially
   written block, because the service writes the new block before swapping.

4. **The AutoSave snapshot guard (D6) is essential and correctly identified.**
   `pat_snapshotScene()` at `PatternData.c:88-93` is an unguarded `memcpy` of
   10,519 bytes. If a relocation is mid-swap during the copy, the snapshot
   captures an inconsistent state (old address with new-block bytes already
   written, or new address with old-block bytes not yet freed). The service
   must skip its tick while the snapshot is in-flight.

5. **Global-generation stale detection (RAM budget section) is the right
   tradeoff.** Storing the step's address at queue time and comparing on
   dequeue costs 2 bytes total versus 896 bytes for per-step counters.

#### Structural concerns and ambiguities

1. **The plan doesn't specify WHERE in the main loop the service tick runs.**
   The main loop (`main.c:161-199`) has three natural insertion points:
   (a) inside `audio_check_and_render()` alongside `seq_drainPendingAutomation()`,
   (b) in the 500Hz `timebase_serviceFrontPanel()` cadence, or (c) as a
   standalone call in the main `while(1)` loop. Option (c) runs at full main-loop
   rate (thousands of times/second); option (b) runs at 500Hz (2ms); option (a)
   runs once per DSP chunk render. The plan says "foreground service" and
   "500Hz service rate" (D4) but doesn't pin the call site. This matters because
   the service touches pool data that the ISR reads.

2. **No existing `autosave_patternSnapshotInFlight()` API exists.** The plan
   assumes one (D6), but `filesystem.c` has no public query for "is a Pattern
   snapshot being consumed." The Pattern snapshot lifecycle is internal to
   `filesystem_autosavePatternDrainSchedule_tick()` at `filesystem.c:24060-24105`
   and `filesystem_autosavePatternDrain_tick()` at `filesystem.c:14801`. The
   snapshot is "in flight" from `pat_snapshotScene()` until
   `filesystem_autosavePatternDrainCompleted()`. Adding a public query is
   straightforward (a flag set before snapshot, cleared on completion) but it's
   a filesystem.c change, not a PatternData.c change, and it must respect the
   existing facade ownership.

3. **The "active scene only" cross-scene safety invariant (SCOPING_TARGETS §4.3
   last paragraph) is implemented in the plan via queue filtering ("if
   entry.scene != active scene: discard"), but "active scene" has a subtle
   definition.** There are two scene indices in play: `seq_activePattern` (the
   sequencer's current Scene) and `scene_getActiveIndex()` (the SceneData
   active Scene). After Session 054's fix these should agree, but the service
   should use `seq_activePattern` since that's what the ISR reads from.

4. **Decision D2 (always reserve 4B slack) has a cascading impact on existing
   writers.** Every call to `pat_writeBlockInternal()` at `PatternData.c:450`,
   `pat_writeSpecials()` at `PatternData.c:556`, `pat_writeStepAutomation()` at
   `PatternData.c:631`, and `pat_removeStepAutomation()` at `PatternData.c:745`
   computes chunk count via `pat_blockChunks()`. Adding 4B slack means modifying
   `pat_blockChunks()` or each caller to request `chunks + 1`. This changes
   the pool's effective capacity (each block grows by one chunk) and must be
   carefully evaluated against the pool usage budget.

5. **The pool capacity impact of D2 is non-trivial.** Currently a minimal
   step block (note only, no automation) is 1 chunk (4 bytes). With D2's slack,
   it becomes 2 chunks (8 bytes). That's a 2× capacity cost for the common case
   of steps with only specials. For a Pattern with 112 active steps (16 per
   track × 7 tracks) each with one special, the pool usage goes from 112 chunks
   (448 bytes, 5.5%) to 224 chunks (896 bytes, 10.9%). This is still well within
   the 8,192-byte pool, but it's worth making the tradeoff explicit.

6. **Tier 2 compaction's "highest occupied block" scan needs the back-reference
   to find the block's owner.** The plan says "walk the bitmap from the top,
   find the highest occupied block." But the bitmap only says "occupied/free" —
   it doesn't say which step owns that block. Finding the owner requires reading
   the block's 2-byte header to extract the 10-bit back-reference, then computing
   `track = backref / 128`, `step = backref % 128`. This is fine but should be
   explicit in the implementation sketch.

---

## Architectural Direction Questions

These questions are organized by topic and numbered for easy reference.

### Service placement and scheduling

**Q1. Main-loop call site.** Where should `pat_defragService_tick()` be called?
Options:
- (a) Inside `audio_check_and_render()`, after `seq_drainPendingAutomation()`.
  Runs per DSP chunk but is latency-sensitive (audio render path).
- (b) From the 500Hz `timebase_serviceFrontPanel()` cadence, in the foreground.
  Runs at a predictable rate. This is what D4's "500Hz service rate" implies.
- (c) Standalone in the main `while(1)` loop, unthrottled. Runs at maximum
  foreground rate but wastes cycles when the queue is empty.
Recommendation: (b) — the 500Hz service cadence is already established for
front-panel work. One relocation per 2ms tick is bounded. The service checks
for work and returns immediately if none is pending.

**Q2. Service ownership.** Should the service live in `PatternData.c` (where it
has natural access to the static pool/bitmap helpers) or in a new file? The
existing static helpers (`pat_poolAlloc`, `pat_poolFree`, `pat_bitmapGet/Set/Clear`,
`pat_blockChunks`) are all `static` in `PatternData.c`. A separate file would
require exposing them or duplicating them. Recommendation: keep the service in
`PatternData.c` as a new section, since it's a natural extension of the allocator.

### AutoSave coordination

**Q3. Snapshot-in-flight guard API.** The plan needs
`autosave_patternSnapshotInFlight()` but it doesn't exist. Options:
- (a) Add a flag in `filesystem.c` set/cleared around the snapshot lifecycle.
  Clean but requires a filesystem.c change and a new public API.
- (b) Use an existing proxy: check `filesystem_status() != FS_STATUS_IDLE`.
  But this is too broad — it would suppress the service during ANY filesystem
  operation, not just Pattern snapshots.
- (c) Add a flag in `PatternData.c` itself: `pat_snapshotScene()` sets it,
  and a new `pat_snapshotConsumed()` clears it (called by filesystem on
  drain completion). The flag lives where the snapshot lives.
Recommendation: (a) — filesystem.c owns the snapshot lifecycle. A minimal
`uint8_t` flag plus one accessor is clean, matches the existing ownership chain,
and costs 1 byte of BSS.

**Q4. Can a relocation race with `pat_snapshotScene()`?** The snapshot is a
single `memcpy()` call (`PatternData.c:92`) with no interrupt masking. If the
500Hz foreground service relocates a block between two `memcpy` iterations (this
can't happen — `memcpy` doesn't yield to the foreground service because the
service IS foreground), the snapshot is inconsistent. **Wait — this is actually
safe by construction.** Both the service tick and `pat_snapshotScene()` are
foreground-only. They can't interleave. The guard is needed only to prevent the
service from mutating pool data while the snapshot bytes are being streamed to
SD (which takes many filesystem ticks). This should be confirmed and documented.

### Slack reservation (D2)

**Q5. Should slack be unconditional or only on relocation?** D2 says "always
reserve 4B slack chunk after each block." This penalizes every block regardless
of whether it will ever grow. An alternative:
- Reserve slack only when the block actually has automation (auto_count > 0 or
  when the first automation is added). Steps with only specials (note/velocity/
  probability) don't grow via the automation path.
- Alternatively, keep unconditional slack but make it the minimum: round up the
  payload to the next chunk boundary rather than adding a full extra chunk. In
  many cases the 4-byte chunk alignment already provides some trailing slack.
This is the most impactful capacity tradeoff in the plan.

**Q6. Is slack worth implementing before live recording?** The SCOPING_TARGETS
§4.3 slack design exists to serve live recording's real-time write path, which
is explicitly out of scope for S067. Without live recording, the only source of
block growth is manual menu edits (`pat_writeStepAutomation()` in Method 1 and
the VOICE overlay in Method 2), which are human-paced and can tolerate a
synchronous relocation. Deferring slack to the live-recording session would
keep S067 simpler and avoid the capacity cost until it's actually needed.

### Pool capacity and fragmentation metric

**Q7. What fragmentation metric triggers compaction?** D3 recommends "largest
contiguous free run < 8 chunks (32 bytes)." But what is the expected largest
block size in practice? The maximum block is 32 chunks (128 bytes, 63
automations). A typical "large" block might be 4–6 chunks (3 specials + 4–8
automations). If the threshold is 8 chunks, compaction triggers when the pool
can't fit a "large" block but can still fit several small ones. Is 8 the right
threshold, or should it scale with recent allocation sizes?

**Q8. How do we measure the fragmentation metric efficiently?** Scanning the
entire 256-byte bitmap to find the longest free run is O(2048 bits). With
32-bit word operations and CTZ it's ~64 word reads plus CTZ calls. At 500Hz
this is fine, but the plan says Tier 2 runs every 100ms (D4), not every tick.
Is the metric computed every tick (to decide whether to run Tier 2) or only
when Tier 2's turn comes? If every tick, the scan cost is paid 500 times/second
even when the pool is healthy.

### Resource allocation

**Q9. BSS budget: 33 bytes total.** The plan itemizes 24 (queue) + 3
(head/tail/count) + 4 (compaction state) + 2 (generation) = 33 bytes. This is
minimal and well within the existing SRAM1 Pattern reservation. It requires
formal approval per the RAM allocation policy. The queue entries use 3 bytes
each: `(scene, track*128+step as uint16_t)`. The stale-detection address
snapshot needs an additional 2 bytes per queue entry (8 × 2 = 16 bytes) or a
separate companion array. **The plan's 33-byte figure omits this.** With
per-entry address snapshots, the total is 33 + 16 = 49 bytes.

**Q10. Flash/text budget.** The plan estimates +200–400 bytes of text. This is
plausible: the service tick is a state machine with bitmap scan, block copy,
and pointer swap — similar in complexity to `pat_writeBlockInternal()` (~80
lines, ~200 bytes of text). The pool widget adds another ~50 bytes. The
popcount-based usage computation and its menu integration add ~100 bytes.
Total estimate: +350 bytes text seems realistic.

**Q11. CPU budget per tick.** One Tier 1 micro-relocation involves:
bitmap scan (worst case ~128 word tests = ~200 cycles), memcpy of one block
(worst case 128 bytes = ~32 cycles with word copies), `STRH` (1 cycle), bitmap
clear (a few cycles). Total: ~250 cycles worst case per tick. At 216MHz this
is ~1.2µs — negligible compared to the 2ms tick budget. Tier 2 compaction
adds a full bitmap scan for the fragmentation metric (~200 cycles) plus one
block relocation (~250 cycles) every 100ms. No CPU concern.

### Interaction with existing code

**Q12. Does the service need to coordinate with the VOICE overlay's write
path?** Session 066's VOICE overlay writes automation to up to 16 steps in one
gesture (`S065_DYN_PAT_VOICE_PARAM_UX.md` §4.6). Each write is an independent
`pat_writeStepAutomation()` call, which may trigger reallocation via
`pat_writeBlockInternal()`. If the service is relocating a block on one of
those steps at the same moment — this can't happen because both are foreground.
But the service should still skip a step if its address has changed since being
queued (the stale-detection mechanism handles this).

**Q13. Does `pat_clearTrack()` or `pat_clearPattern()` need to flush the Tier 1
queue?** If a user clears a track or pattern while step relocations are queued,
the queued entries become stale. The stale-detection mechanism (address
comparison) handles this correctly — the cleared step's address becomes
`PAT_ADDR_SENTINEL`, which won't match the queued address, so the relocation
is skipped. No explicit flush needed.

**Q14. Does `pat_eraseStep()` need to remove its entry from the queue?**
Same answer as Q13 — stale detection handles it. But if the queue is full of
stale entries, newly needed relocations can't be queued. The queue should
drain stale entries eagerly (check and discard on each tick, not just when
dequeuing).

### Scope and phasing

**Q15. Should S067 implement both Tier 1 AND Tier 2, or only Tier 1?** Tier 1
(micro-relocation) is reactive and directly useful: it reclaims fragmentation
caused by block growth. Tier 2 (global compaction) is proactive housekeeping
that consolidates scattered free space. Without live recording creating rapid
fragmentation, Tier 2 may not be needed yet. Implementing only Tier 1 reduces
scope and risk. Tier 2 could be added in a later session when live recording
creates the need for it.

**Q16. When does the Tier 1 queue actually get populated?** The plan says
"when a step's block grows and its trailing slack is exhausted." But without
slack reservation (if Q6 defers slack), there IS no slack to exhaust — the
queue trigger changes to "when `pat_poolAlloc()` had to relocate a block."
Currently `pat_writeBlockInternal()` handles relocation inline (allocate new,
copy, swap, free old). The service adds value only if existing relocations are
deferred to the service rather than handled synchronously. If manual edits
continue to relocate synchronously, what work remains for the service?

**Q17. Alternative framing: is the service actually needed yet?** The current
allocator handles all block mutations synchronously: grow, shrink, erase, clear.
Fragmentation occurs but is bounded by the pool size (8,192 bytes). The pool
usage monitor (Part B) gives the user visibility into pool pressure. The service
adds value primarily for: (a) live recording's real-time path (deferred),
(b) recovering from fragmentation after many add/erase cycles. For (b), the
user can already work around pool exhaustion by clearing unused steps. Is the
service's complexity justified before live recording makes it necessary?

---

## Summary of decisions needed

| # | Question | Depends on | Impact |
|---|----------|-----------|--------|
| Q1 | Service call site | Architecture | Determines tick rate and ISR interaction model |
| Q2 | File placement | Code organization | Static helper visibility |
| Q3 | Snapshot guard API | filesystem.c ownership | 1 byte BSS + 1 new API |
| Q5 | Slack unconditional vs. conditional | Pool capacity tradeoff | Every block grows by 4B unconditional |
| Q6 | Defer slack to live-recording session? | Scope | Simplifies S067 significantly |
| Q9 | RAM approval for 33–49 bytes | RAM policy | Required before implementation |
| Q15 | Tier 1 only vs. both tiers? | Scope and risk | Halves Part A complexity |
| Q16/Q17 | Is the service needed before live recording? | Scope | Could reduce S067 to Part B only + foundation |
