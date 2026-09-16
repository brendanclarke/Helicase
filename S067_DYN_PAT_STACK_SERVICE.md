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
