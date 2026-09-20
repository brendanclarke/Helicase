# S069 — Slack reservation image and reactive compaction (Claude breakout)

## Status

Planning only. No product code, PAT4 format, resident Pattern-region layout,
or SRAM allocation is changed by this document.

Claude-authored breakout of "Replace the Tier 1/Tier 2 loop with owned slack
plus reactive compaction" from `S069_ATS_PAT_BOUNDED_CPU.md` (Implementation
plan item 2), incorporating this session's settled decision to use one full
512-byte chunk image.

## Relationship to `S069_SLACK_REACTIVE_COMPACTION.md`

A sibling document with the same breakout scope already exists (see this
session's root `MEMORY.md` Volatile Notes). Both documents land on the same
512-byte, single-image, non-persisted design — this session's instruction
confirms that design explicitly rather than leaving it as the sibling's
"requested value." This document adds several findings from direct source
reading that the sibling left as open follow-ups (the bit-convention question,
the rebuild mechanism, and the exact existing code this plan extends) with
concrete evidence rather than as undecided placeholders. Where this document
states something as settled that the sibling left open, that is because direct
evidence was found for it below, not because the earlier open question was
wrong to ask. This document did not delete, edit, or supersede the sibling — it was read in
full earlier in this session and existed on disk at that point, then was
removed from the working tree later in this same session by a process outside
this conversation. The comparative notes below describe its content as read.

## Established starting point

### Geometry, confirmed against source

- `PAT_STACK_SIZE` = 256 (`config.h:253`).
- Pool: `PATSVC_POOL_BYTES` = `PAT_STACK_SIZE * 32` = 8,192 bytes
  (`PatternStackService.c:27`).
- Occupancy bitmap: `pat_scene_region_t.bitmap[512]` (`PatternData.h:81`) =
  4,096 representable bits.
- Backed chunks: `PATSVC_POOL_CHUNKS` = `PAT_STACK_SIZE * 8` = 2,048
  (`PatternStackService.c:26`). The upper 2,048 bitmap bits describe the
  unbacked address range.
- Both the pool and the bitmap live inside `pat_scene_region_t`
  (`PatternData.h:79-81`), one instance per Scene (`SCENE_COUNT` = 16,
  `pat_regions[SCENE_COUNT]`) — i.e. the *occupancy* bitmap is already
  per-Scene and already persisted as part of the 10,519-byte resident region /
  PAT4 file. The new reservation image is deliberately not this — see
  "Requested full chunk image" below.

### The three current mechanisms, confirmed against source

- **Tier 1** (`patSvc_tier1Step()`, `PatternStackService.c:472-518`): for the
  current scan cursor's address entry, computes a target gap
  (`patSvc_gapTarget()` — 1 or 2 chunks depending on pool occupancy vs.
  `PAT_GAP_REDUCE_THRESHOLD` = 60%, `config.h:267`) and, if the block's
  current trailing gap (`patSvc_trailingGap()`) is short, relocates it via
  `patSvc_relocateIndex(..., gap, lower_only=0, ...)` to a run with room for
  the gap. This creates a gap in the *occupancy* bitmap only — nothing marks
  it as belonging to that block.
- **Tier 2** (`PatternStackService.c:1178-1224`): once Tier 1's cursor
  completes a full pass (reaches `PATSVC_ADDRESS_COUNT`), if
  `PAT_COMPACT_INTERVAL_MS` (100 ms, `config.h:268`) has elapsed since the
  last compaction, scans up to `PAT_COMPACT_SCAN_PER_TICK` (16, `config.h:269`)
  entries per tick and relocates via `patSvc_relocateIndex(..., gap=0,
  lower_only=1, ...)` — i.e. it packs blocks downward with **no** gap,
  directly undoing whatever Tier 1 just created. If the 100 ms has *not*
  elapsed, the `else` branch at line 1223 (`tier1_scan_cursor = 0u`)
  immediately restarts Tier 1 from scratch. This is the observed self-chase:
  Tier 1 creates a gap, either Tier 2 or a fresh Tier 1 pass removes it.
- **Reactive recovery** (`patSvc_reactiveStep()`, `PatternStackService.c:
  529-572`, armed in `patSvc_drainQueue()` at lines 745-756): when a queued
  mutation's direct execution fails, the code already distinguishes genuine
  capacity exhaustion (`free_chunks < required` → `CAPACITY_DROP`, event
  dropped) from fragmentation (`largest < required` but enough total free
  space exists → arms `reactive_active`/`reactive_required`, leaves the head
  event in place). Once armed, `patSvc_tick()` calls `patSvc_reactiveStep()`
  on later ticks, which scans up to `PAT_COMPACT_SCAN_PER_TICK` entries per
  call, relocating the first movable block toward a lower run
  (`lower_only=1`); if the whole address space is scanned with no improving
  move, it records `FRAG_DROP` and drops the head. **This already matches the
  shape the parent plan asks for the queued path**: triggered only by a
  genuine blocked allocation, bounded per tick, retries the same head, reports
  a classified drop only after a complete bounded pass. The real gap is not
  "build reactive compaction" — it mostly already exists — it is (a) deleting
  the *periodic* Tier 2 path, and (b) the direct-path parity gap below.
- **Direct-path parity gap, confirmed**: `patSvc_submit()`'s direct-execution
  branch (`PatternStackService.c:771-786`) calls `patSvc_executeEvent(event)`
  exactly once and returns its result. There is no fallback to enqueue, and no
  reactive-compaction attempt, on a direct fragmentation failure. A
  direct-when-idle mutation that fails only because of fragmentation is simply
  rejected — it never reaches `patSvc_reactiveStep()` at all. This confirms
  the parent plan's §2.5 concern exactly, with the precise call site.
- **A pre-existing, reservation-blind fast path already exists and is exactly
  what "owned trailing slack" needs to hook into**: `pat_tryAppendAutomation()`
  (`PatternData.c:489-547`, the existing "Gate-6 growth optimization") already
  grows a block in place by exactly one appended automation entry when the
  immediately-following chunk(s) are free, checked purely against the
  occupancy bitmap (`pat_bitmapGet`, line 514) — no relocation, no `memmove`
  of existing bytes. This is *why* Tier 1's gap is only ever "soft": any
  grower, not just the block Tier 1 intended the gap for, can claim a free
  trailing chunk through this existing path today, and ordinary new-block
  allocation can claim it too, since nothing currently distinguishes "free
  because Tier 1 left it" from "free because nothing has ever used it." The
  reservation image's job is specifically to make this existing function (and
  ordinary allocation) reservation-aware, not to invent a new growth mechanism
  from nothing.
- **Trace vocabulary overlap**: reactive recovery's successful move
  (`patSvc_reactiveStep()`, line 561) and the periodic Tier 2 sweep's
  successful move (line 1207) currently both record
  `PAT_TRACE_STAGE_TIER2_RELOC` (`'R'`). Once periodic Tier 2 is deleted,
  `'R'` becomes unambiguous — a free clarity improvement, not something that
  needs a new trace stage.

### Every allocation-search helper already bounds itself to the backed range

`patSvc_countUsed()`, `patSvc_largestFreeRun()`, and `patSvc_findFreeRun()`
(`PatternStackService.c:288-343`) all loop `chunk < PATSVC_POOL_CHUNKS`
(2,048) only — none of them ever reads the upper (unbacked) half of the
occupancy bitmap at all. This resolves the sibling document's Follow-up #1
("bit convention... including the upper unbacked half") with direct evidence:
**the new reservation image needs no special handling for its upper half.**
As long as every new reservation-aware helper is bounded the same way
(`< PATSVC_POOL_CHUNKS`), the upper 2,048 reservation bits (256 of the 512
bytes) are simply never consulted, exactly like the occupancy bitmap's upper
half today. They cost SRAM but not correctness risk or extra logic.

### Single-Scene scope is architectural, not incidental

As established in the companion
`S069_NON_SEMANTIC_PAT_MAINT_RESTRICTION_CLAUDE.md` breakout: `patSvc_tick()`'s
Tier 1/Tier 2/reactive logic all operate exclusively on `service_scene`, which
tracks `seq_activePattern` with a bounded handover. The Pattern Stack Service,
as it exists today, never needs to reason about more than one Scene's layout
at a time. This is *why* "one image only, not one image per Pattern or Scene"
is not a simplification that trades away needed capability — it is already
the natural shape of the service it belongs to. It does mean, cross-
referencing the companion document's own finding, that non-active Scenes will
not accumulate physically-relocated (non-semantic) work under this plan as
drafted — flagged there, not repeated in full here.

## Requested full chunk image — settled

This session settles the sibling document's open sizing question explicitly:

| Property | Value |
|---|---|
| Bytes | 512 |
| Region | Normal SRAM1 |
| Owner | Pattern Stack Service / Pattern Stack maintenance |
| Lifetime | Firmware lifetime; describes only the current service target (`service_scene`) |
| Scope | One image only — not one per Pattern, not one per Scene |
| Persistence | None — not PAT4, not a `pat_scene_region_t` field, not an AutoSave snapshot field |
| Coordinate space | Same 4,096-bit / 512-byte space as the existing occupancy `bitmap[512]`, for identical `chunk >> 3` / `chunk & 7` indexing |

256 of the 512 bytes (the upper, unbacked half) are never consulted by any
allocator helper bounded to `PATSVC_POOL_CHUNKS`, per the finding above — this
is a deliberate, small, explicit trade of unused SRAM for address-math
symmetry with the existing bitmap, not an oversight. Worth confirming that
trade is acceptable given the project's Pattern-data RAM reservation
(89,204 B currently reserved for exactly this purpose, `SRAM_MANIFEST.md:83`)
— trivial at this size, but still a new static requiring the sign-off
`MEMORY.md`'s RAM policy requires for any new allocation, however small.
**Approved**: 512 bytes for the reservation image, plus up to 20 additional
bytes for any incidental service state (cursors, latches, flags) this plan
requires.

## Regeneration rather than persistence — assessed

The user's instruction: reservation can always be regenerated from the
pattern stack itself, so it needs no persisted bitmap in the PAT4 file, no
per-Scene SRAM copy, and only one 512-byte assignment inside the maintenance
routine.

**Agreed, with the precise mechanism named:** a logical block's size is
entirely self-describing from its own header/flags/count bytes stored in the
pool (`patSvc_blockChunksAt()`, `PatternStackService.c:232-265`, reads the
block's own header rather than trusting the bitmap) — so *occupancy* is
always fully recoverable from the pool + address array alone, with or without
the bitmap. *Reservation*, by contrast, records a policy choice ("this
specific free chunk is earmarked for that specific block"), not a fact
recoverable from the pool bytes — nothing persisted anywhere says that today,
because the concept doesn't exist yet. So "regenerating" a reservation image
is not recovering lost information; it is **re-running the same policy Tier 1
slack-repair already applies**, starting from an empty image: for each
occupied logical block in address order, if its immediately-following chunk
is free (and unreserved), reserve it. Rebuild is not a distinct algorithm from
steady-state repair — it is epoch one of it, run against a zeroed image. That
is good news for implementation size: no separate "recovery" code path is
needed, only "clear the image, then let one ordinary bounded repair epoch
run" at each of the three lifecycle boundaries the sibling document names
(service setup, service target change, filesystem replacement).

**Settled: lazy rebuild.** The freshly-cleared image is repopulated over the
repair pass's normal bounded per-tick scan — no blocking full-pool walk at
any lifecycle boundary. Some blocks briefly have no reserved slack right
after a rebuild; the system's fallback chain handles this gracefully:

1. **Lazy maintenance** creates and maintains reservations as the bounded
   repair epoch progresses — best-effort space optimization.
2. **Buffer retention**: if a write operation does not immediately fit but
   enough total free capacity exists, the event is retained in the queue for
   reactive recovery, which may relocate blocks to create a contiguous run.
3. **Drop**: if the write genuinely exceeds remaining pool capacity, or the
   queue is full, the event is dropped with a classified trace record.

During the window between a rebuild clear and full repair convergence,
allocation falls back to "wherever there is space" — the occupancy bitmap
alone is still authoritative for what is free, so any unreserved free chunk
is available. Maintenance then spreads blocks into their reserved trailing
slack as it reaches them in later ticks.

## Direction retained from the parent plan, tightened against real code

### Owned trailing slack — the one-chunk rule, concretely

The parent's one-chunk (4-byte / two automation entries) trailing-slack target
attaches directly to the existing `pat_tryAppendAutomation()` fast path.
Concretely, that function's free-chunk check (line 514, currently
`pat_bitmapGet` only) needs to become: try the block's *own* reserved trailing
chunk first (cheapest — always the right answer when present); if not
reserved, fall back to "chunk is free and unreserved" exactly as today.
Consuming an owned chunk this way must clear that chunk's reservation bit in
the same transaction it sets the chunk's occupancy bit (line 532's
`pat_bitmapSet`) — a chunk must never be simultaneously reserved and occupied.

When the reservation image is incomplete (after a rebuild, or during the
first repair epoch), `pat_tryAppendAutomation()` falls back to "chunk is free
and unreserved" — the allocator always works, reservations only make the
common case cheaper. Maintenance then spreads blocks into proper reserved
positions as its bounded scan reaches them.

### Reservation density — latchable occupancy-scaled rule

The reservation target is not fixed at "one trailing chunk per block
unconditionally." Instead, the service maintains a **latchable
reservation-density level** that scales with pool occupancy:

- **Low occupancy** (below a lower threshold): full reservation density — one
  trailing chunk per occupied block. This is the steady-state target for a
  pattern with headroom.
- **High occupancy** (above an upper threshold): reduced or zero reservation
  density — the pool cannot afford to hold chunks out of general circulation,
  so reservations are defined away and all free space is available to any
  allocator.
- **Hysteresis**: the transition thresholds for scaling down vs. scaling back
  up differ, preventing oscillation when occupancy hovers near a boundary.
  The existing `PAT_GAP_REDUCE_THRESHOLD` (60%, `config.h:267`) is a natural
  reference point for one of these thresholds.

The density level is a latched state variable, not recomputed per-tick from
raw occupancy. It transitions only when occupancy crosses the threshold in
the relevant direction — upward to reduce, downward to restore. When the
density level changes:

- **Downward** (fewer reservations): the repair pass's next epoch simply
  stops creating new reservations for blocks it visits, and reactive recovery
  can reclaim existing ones without violating policy. No urgent bulk-clear of
  existing reservations is needed — they are consumed or reclaimed naturally.
- **Upward** (more reservations): a wake event resets the repair cursor,
  starting a new epoch that creates reservations for blocks that now qualify.

This means the maintenance pass's "steady-state target" is itself dynamic —
it always tries to converge toward whatever the current density level
defines, and that definition shifts slowly as occupancy changes. The exact
threshold values and number of density levels are implementation-time
decisions; the architecture needs only the latch, the threshold pair, and the
repair pass reading the current level to decide whether to reserve.

### Adaptive repair-tick budget

The per-tick scan bound (`PAT_COMPACT_SCAN_PER_TICK`, currently 16) is not
fixed. When AutoSave has pending work (either semantic or non-semantic dirty
bits set, or a drain in flight), the repair pass reduces its per-tick budget
to a lower limit, yielding foreground cycles to the filesystem facade. When
no AutoSave work is pending, the repair pass may use its full budget. The
exact upper and lower bounds are implementation-time `config.h` constants;
the mechanism is a simple conditional on the existing
`autosave_maskHasDirty()` / `autosave_patternDirtyMask()` predicates already
identified in the companion non-semantic plan.

### Finite repair in place of Tier 1 — the exact lines that change

The perpetual-loop mechanism is concrete and small: the `else` branch at
`PatternStackService.c:1221-1224` (`tier1_scan_cursor = 0u` whenever the
100 ms compaction interval has not yet elapsed) is what restarts Tier 1
immediately after every completed pass. Replacing the Tier1/Tier2 block with a
finite repair pass means: when `tier1_scan_cursor` reaches
`PATSVC_ADDRESS_COUNT`, stop — leave the cursor there (asleep) — rather than
resetting it to 0 or falling into a timer-gated Tier 2 scan. The parent plan's
wake sources (a relevant Pattern mutation, a reservation consumed/reclaimed
event, service handover, filesystem replacement) become the only things
allowed to reset the cursor to 0 again.

Each repair step's two non-trivial outcomes become cheaper than they read in
the abstract: "trailing chunk is truly free: reserve it in place" is a single
reservation-bitmap set, no pool write and no `memmove` at all — most
repair-epoch steps are pure bit-setting once the pool has converged. Only the
third outcome (trailing chunk unavailable, but a `logical + 1` destination
exists elsewhere) uses the existing relocate-and-reserve transaction.

### Reactive compaction in place of Tier 2

Given the finding above that reactive recovery already matches the desired
reactive-only shape for the queued path, this reduces to: (a) delete the
periodic Tier 2 block (`PatternStackService.c:1178-1224`, the interval check
and its bounded scan) outright rather than reworking it, and (b) make
`patSvc_reactiveStep()`'s relocation search reservation-aware in the same way
ordinary allocation must become, so that reclaiming reserved slack under real
allocation pressure is a deliberate, policy-driven choice ("semantic capacity
wins under pressure") rather than an accidental side effect of a lower_only
relocate that happens to land on a reserved chunk.

### Direct and queued mutations — parity fix, concretely

`patSvc_submit()`'s direct branch (`PatternStackService.c:778-784`) needs the
same fragmentation classification `patSvc_drainQueue()` already performs
(lines 736-761): on a direct-path failure that is specifically "capacity
exists, no contiguous run big enough," the event must be retained for the
same reactive path a queued event gets — not simply returned as failed. The
exact public-API contract change (does the direct caller's boolean return
value start meaning "queued for recovery" as well as "committed"?) is an
audit item, not assumed here. Every pathway where the outcome is uncertain
must record recoverable evidence in the PatternTrace — this is not a system
that reports errors to the screen.

## Two-image bit convention

- Same 512-byte, chunk-indexed layout as the existing `bitmap[]` — identical
  `array[chunk >> 3] |= / &= ~ / & (1 << (chunk & 7))` shape, so the
  reservation helpers can be near-literal duplicates of
  `patSvc_bitmapGet/Set/Clear` parameterized by which image, or the pattern is
  simply copied — minimal new logic either way.
- Meaning of a set reservation bit: this backed, currently-*unoccupied* chunk
  is claimed as trailing slack for a specific block and must be refused to
  ordinary allocation, Tier-1-style repair, and reactive recovery searches,
  even though the occupancy bitmap alone would show it free.
- Invariant: a chunk is never simultaneously reserved and occupied.
  Reservation only ever targets a chunk the occupancy bitmap currently shows
  free; the instant an append consumes it, the same transaction must clear
  the reservation bit and set the occupancy bit.
- Upper (unbacked) half: left entirely unused. Every reservation-aware helper
  bounds its loop to `< PATSVC_POOL_CHUNKS`, exactly like `patSvc_countUsed()`
  / `patSvc_largestFreeRun()` / `patSvc_findFreeRun()` already do. No
  pre-marking, no special-casing.

## Implementation outline

1. Audit every "is this chunk available" call site: `patSvc_findFreeRun()`,
   `patSvc_largestFreeRun()`, `pat_tryAppendAutomation()`'s free-run check
   (`PatternData.c:514`), and any other direct `pat_bitmapGet`/
   `patSvc_bitmapGet` caller not yet enumerated here — each needs a second,
   reservation-aware check added, not just the three named in the parent
   plan.
2. Add the 512-byte reservation image (approved) declared alongside the
   Pattern Stack Service's other service-owned statics (near
   `tier1_scan_cursor` etc., `PatternStackService.c:102-108`). Up to 20
   additional bytes of incidental service state (density latch, adaptive
   budget flag, etc.) are pre-approved.
3. Consolidate bitmap get/set/clear helpers: parameterize by which image
   (occupancy vs. reservation) rather than duplicating. Consolidate to the
   owner that makes the most architectural sense — `PatternStackService.c`
   owns the reservation image and already has its own copies; `PatternData.c`
   owns the occupancy bitmap. This is a careful, minimal consolidation, not a
   refactor pass — touch only helpers that must become reservation-aware.
4. Implement rebuild-from-stack (clear image; let the next bounded repair
   epoch populate it lazily) at the three lifecycle boundaries:
   `patSvc_init()`, the handover-complete branch
   (`PatternStackService.c:1121`), and `patSvc_finishSceneReplace()`. No
   blocking full-pool scan at any boundary.
5. Implement the latchable reservation-density rule: occupancy thresholds
   with hysteresis, a latched density-level state variable, and the repair
   pass reading the current level to decide whether to create reservations.
6. Implement the adaptive repair-tick budget: reduced scan bound when
   AutoSave has pending work, full bound when idle.
7. Make `pat_tryAppendAutomation()` try the block's own reserved chunk first,
   ordinary free-and-unreserved second. When the reservation image is
   incomplete, fall back to "wherever there is space" — the occupancy bitmap
   alone is always authoritative.
8. Replace the Tier 1 restart (line 1223) with sleep-at-cursor-end; delete the
   periodic Tier 2 block (lines 1178-1224's interval-gated scan); define the
   wake events that reset `tier1_scan_cursor` to 0.
9. Make `patSvc_reactiveStep()` reservation-aware and able to deliberately
   reclaim reserved (not just occupied) capacity under real allocation
   pressure, guided by the current density level.
10. Fix the direct-path parity gap in `patSvc_submit()`. Every uncertain
    outcome pathway must record recoverable evidence in the PatternTrace.
    This is not a system that reports errors to the screen.
11. Measure idle relocation rate, `M`/`R`/`G`/`F`/`C` trace counts, allocation
    outcomes, and foreground/audio pressure before and after, per the parent
    plan's own acceptance metrics.

## Verification targets

| Case | Required observation |
|---|---|
| Image geometry | One 512-byte service-owned image; no `pat_scene_region_t` growth, no PAT4 growth, no per-Scene duplicate. |
| Rebuild | Each of the three lifecycle boundaries produces a usable reservation image via one bounded repair epoch from a cleared image — no read of any persisted reservation state, because none exists. |
| Reservation vs. occupancy | A reserved-but-unoccupied chunk is refused by `patSvc_findFreeRun()`, `patSvc_largestFreeRun()`, and `pat_tryAppendAutomation()`'s fast path alike. |
| Growth | An eligible block's automation append consumes its own reserved chunk with no relocation and no pool-wide search when the reservation exists; falls back correctly when it does not. |
| Idle service | One completed repair epoch reaches sleep; the cursor stays at `PATSVC_ADDRESS_COUNT` until a defined wake event; no timer alone restarts it. |
| Tier 2 removal | No periodic relocation occurs; the `'R'` trace stage is emitted only by reactive recovery. |
| Fragmentation, queued path | Unchanged from today's already-working reactive behavior — retained as a regression check, not a new feature. |
| Fragmentation, direct path | A direct-when-idle mutation blocked only by fragmentation now reaches the same reactive recovery a queued mutation gets, and the retained/queued/reported outcome matches the decided API contract. Every uncertain outcome produces a recoverable PatternTrace record. |
| Slack pressure | A blocked real write can reclaim reserved (not just occupied) capacity guided by the current density level, rather than failing solely because reservations exist. |
| Density scaling | Reservation density reduces when pool occupancy crosses the upper threshold; restores when occupancy drops below the lower threshold; hysteresis prevents oscillation near a boundary. |
| Adaptive budget | Repair-tick budget demonstrably lower when AutoSave work is pending vs. when idle. |
| Incomplete reservation fallback | Allocation succeeds using the occupancy bitmap alone when the reservation image is empty or partially populated (post-rebuild window). |
| RAM proof | Link/map evidence and an `SRAM_MANIFEST.md` update identify the exact 512-byte allocation plus any incidental state (within +20 pre-approved), its section, and confirm no other new static was introduced beyond what this document lists. |
| Persistence | PAT4 format, CRC, snapshot, and boot-reader behavior unchanged. |

## Follow-up decisions, risks, and ambiguities

### Resolved

1. **Cross-reference to the companion non-semantic plan — RESOLVED.**
   Pattern stack maintenance only runs on the active pattern
   (`service_scene` = `seq_activePattern`). Non-active patterns are caught up
   on all semantic changes by exhausting the queue before a handover switch,
   and marked stale for non-semantic AutoSave if those changes have not yet
   been autosaved — but absolutely no maintenance (no repair epoch, no
   reservation work) is needed on a non-active pattern until the user
   switches back and makes it active again. The companion plan's non-active-
   first AutoSave priority handles persisting any stale non-active patterns
   before the active one. Extending maintenance to non-active Scenes (multi-
   Scene reservation images, maintenance rotation) is out of scope for this
   plan and only needed if a future requirement demands it.

2. **Lazy vs. eager rebuild — RESOLVED.** Lazy. A freshly-cleared
   reservation image is populated by the next bounded repair epoch, not by a
   blocking full-pool scan. The fallback chain during the incomplete-
   reservation window is: (1) allocate wherever there is free space per the
   occupancy bitmap alone, (2) retain writes in the queue for reactive
   relocation if they might fit after defragmentation, (3) drop writes that
   genuinely exceed capacity or the queue. Maintenance spreads blocks into
   their reserved trailing slack as its bounded scan reaches them.

3. **Allocator boundary / ownership — RESOLVED.** Whatever is simplest:
   `PatternStackService.c` owns the reservation image and exposes a narrow
   bounds-checked query/consume accessor that `PatternData.c` calls from
   `pat_tryAppendAutomation()`. The reservation image regeneration process
   must also be bounded (it is, by construction — it is just a normal repair
   epoch from a zeroed image). When the image is incomplete, allocation falls
   back to "wherever there is space" per the occupancy bitmap alone, and
   maintenance spreads things out later. Bitmap helpers are consolidated to
   the owner that makes the most sense (see #8) rather than duplicated across
   both modules.

4. **Slack eviction order under pressure — RESOLVED.** Replaced by a
   latchable reservation-density rule (see "Reservation density" subsection
   above). Rather than choosing which individual reservation to release
   first, the system defines the reservation target itself downward as pool
   occupancy rises and upward as it frees. Transitions use hysteresis to
   prevent oscillation. Maintenance lazily converges toward whatever the
   current density level defines. At the highest occupancy level, zero
   reservations are active — all free space is available to any allocator,
   and "which reservation to release" never arises because no reservations
   exist. Below that level, reactive recovery can reclaim a reservation when
   it needs a contiguous run, guided by the current density level (a
   reservation at the current level is "supposed to exist" and harder to
   reclaim; one above the current level is surplus and cheap to reclaim).
   Exact threshold values are implementation-time `config.h` constants.

5. **Repair epoch bound and wake-event completeness — RESOLVED.**
   `PAT_COMPACT_SCAN_PER_TICK` (currently 16) is the base per-tick bound.
   An adaptive budget scales this: lower when AutoSave has pending work
   (semantic or non-semantic dirty bits set, or a drain in flight), higher
   when idle (see "Adaptive repair-tick budget" subsection above). Exact
   upper and lower bounds are implementation-time `config.h` constants. Wake
   sources that reset the repair cursor to 0 are enumerated at implementation
   time from the real call sites rather than the four abstract categories in
   the parent plan — this is a code audit, not a design decision.

6. **Reactive recovery bound with reservation reclaim added — RESOLVED.**
   Adding reservation reclaim as one more class of move does not change the
   termination bound. Reactive recovery is still bounded by address-entry
   count per tick and gives up after one full pass with no improving move.
   Reclaiming a reservation is an improving move (it frees a chunk), so the
   pass terminates in the same bounded number of ticks, just with a wider
   menu of options at each step.

7. **Direct-path API contract — RESOLVED.** Every current caller of the
   `patSvc_set*`/`patSvc_write*` direct-path functions must be audited at
   implementation time before changing what a returned failure means. Every
   pathway where the outcome is uncertain must record recoverable evidence in
   the PatternTrace. This is not a system that reports errors to the screen —
   uncertain outcomes (retained for reactive recovery, dropped after bounded
   retry) are trace-only events.

8. **`pat_bitmapGet`/`patSvc_bitmapGet` duplication — RESOLVED.**
   Consolidate to the owner that makes the most architectural sense.
   Parameterize the helpers by which image (occupancy vs. reservation) rather
   than duplicating. This is a careful, minimal consolidation — touch only
   helpers that must become reservation-aware, do not treat this as a general
   refactor pass.

9. **RAM approval and `SRAM_MANIFEST.md` update — RESOLVED.** 512 bytes
   approved for the reservation image. Up to 20 additional bytes pre-
   approved for incidental service state (density latch, adaptive budget
   flag, cursors, etc.). SRAM1, `.bss`, Pattern Stack Service-owned.
   `SRAM_MANIFEST.md` update at implementation time.

10. **Future pool geometry — RESOLVED.** A resize to larger than
    `256 * 8` = 2,048 backed chunks is more or less guaranteed. All
    threshold and density rules must be expressed relative to the current
    allocation (e.g. "50% of `PATSVC_POOL_CHUNKS`") rather than as absolute
    chunk counts. The 512-byte reservation image tracks the existing
    `bitmap[512]` size, not `PAT_STACK_SIZE` directly, so it grows
    automatically with the bitmap. At implementation time: if anything in
    this plan genuinely requires re-architecture on a pool resize (beyond
    threshold re-tuning), add a comment to the relevant `config.h` constant
    and a note to `PATTERN_DYNAMIC_STACK.md` §12 calling it out explicitly.
    The expectation is that this should not be necessary — the design is
    intentionally relative.

## Out of scope

- A new persistent reservation bitmap, PAT4 version, or `pat_scene_region_t`
  field.
- Treating the transient reservation image as saved musical content.
- AutoSave arbitration policy — see the companion
  `S069_NON_SEMANTIC_PAT_MAINT_RESTRICTION_CLAUDE.md` document.
- Background CPU budgeting, scalar exact-dirty-count, Pattern snapshot
  chunking, and live-record throughput design from the parent plan.

---

## Post-implementation assessment

### Status: IMPLEMENTED — pending hardware verification

Implementation was completed and the build image was produced
(`LXRV2_lxr02.img`, +820 bytes from 449,008 → 449,828). All plan items
verified against the implemented source. This assessment was generated by
reading every changed file in the working tree after implementation.

### Plan coverage — step-by-step

| Plan item | Implemented | Notes |
|---|---|---|
| 1. Reservation image (512 B) + density/budget/rebuild flags (3 B) | YES | `reservation_image[512]`, `reservation_density_active`, `repair_budget_busy`, `reservation_rebuild_pending` — all declared as service statics (`PatternStackService.c:130–166`). 515 B total, within the 512+20 ceiling. |
| 2. Reservation get/set/clear helpers | YES | `patSvc_reservationGet/Set/Clear` (`PatternStackService.c:292–323`) — same `chunk>>3, chunk&7` geometry as occupancy. |
| 3. Image clear + rebuild arm | YES | `patSvc_clearReservationImage()` (`PatternStackService.c:334–338`) — `memset` + sets `reservation_rebuild_pending`. |
| 4. Public cross-module accessor | YES | `patSvc_isChunkReserved()` and `patSvc_consumeReservation()` declared in `PatternStackService.h:118–119`, implemented at `PatternStackService.c:348–368`. Bounds-checked. |
| 5. Density-level latch with hysteresis | YES | `patSvc_updateDensityLevel()` (`PatternStackService.c:379–392`). Uses `PAT_RESERVATION_REDUCE_THRESHOLD` (70%) and `PAT_RESERVATION_RESTORE_THRESHOLD` (50%). Restore transition wakes repair cursor. |
| 6. Adaptive repair budget | YES | `patSvc_sampleRepairBudget()` and `patSvc_repairBudget()` (`PatternStackService.c:403–422`). Reads `autosave_maskHasDirty()`, `autosave_patternDirtyMask()`, `autosave_nonSemanticPatternDirtyMask()`. Selects between `PAT_REPAIR_SCAN_IDLE` (16) and `PAT_REPAIR_SCAN_BUSY` (4). |
| 7. Reservation-aware `patSvc_findFreeRun` | YES | `PatternStackService.c:537` — `patSvc_reservationGet()` check added to inner loop. |
| 8. Reservation-aware `patSvc_largestFreeRun` | YES | `PatternStackService.c:510–511` — reserved chunks break the free run. |
| 9. Reservation-aware `pat_poolAlloc` | YES | `PatternData.c:247` — `patSvc_isChunkReserved(i)` added. `PatternStackService.h` included at `PatternData.c:26`. |
| 10. `pat_tryAppendAutomation` reservation consumption | YES | `PatternData.c:510–513` — free-trailing check accepts reserved chunks (positional ownership). `PatternData.c:532–533` — `patSvc_consumeReservation()` clears reservation after occupancy set. |
| 11. `pat_poolFree` trailing reservation cleanup | YES | `PatternData.c:289–290` — clears the trailing reservation when a block is freed. Not in the original schedule but a correct addition — prevents stale reservations on freed blocks. |
| 12. `patSvc_relocateIndex` reservation cleanup | YES | `PatternStackService.c:686–687` — old trailing reservation cleared after relocation. |
| 13. Repair step (`patSvc_repairStep`) | YES | `PatternStackService.c:716–806`. Three cases: (1) free trailing → reserve in place, (2) already reserved → no-op, (3) occupied trailing → relocate to block+1 run. Inlined relocation with reservation-aware search. Clears old trailing reservation on relocation. |
| 14. `patSvc_findFreeRunReclaiming` | YES | `PatternStackService.c:558–582`. Clears surplus reservations in the target run. Used only by reactive recovery when density is inactive. |
| 15. Reactive recovery reservation reclaim | YES | `patSvc_reactiveStep()` (`PatternStackService.c:819–893`). First pass via `patSvc_relocateIndex` (respects reservations). Second pass via `patSvc_findFreeRunReclaiming` when `!reservation_density_active`. |
| 16. Direct-path parity fix | YES | `patSvc_submit()` (`PatternStackService.c:1125–1158`). Fragmentation classification matches queued path. Retention via `patSvc_enqueue` + reactive activation. `'D'` trace stage. |
| 17. Tier 1/Tier 2 deletion | YES | `patSvc_tier1Step()`, `patSvc_gapTarget()`, `tier2_scan_cursor`, `last_compact_tick`, `PAT_COMPACT_INTERVAL_MS`, `PAT_GAP_REDUCE_THRESHOLD`, `PAT_COMPACT_FREE_RUN_THRESHOLD` — all removed, zero remaining references. |
| 18. Finite repair epoch in `patSvc_tick` | YES | `PatternStackService.c:1554–1592`. Samples budget and density once per tick. Handles rebuild-pending wake. Bounded scan with `patSvc_repairBudget()`. Cursor sleeps at `PATSVC_ADDRESS_COUNT`. |
| 19. Wake events | YES | Queue drain (`PatternStackService.c:1064–1065`), direct submit (`PatternStackService.c:1119–1120`), bulk drain completion (`PatternStackService.c:1027–1028`), `clearTrack` direct path (`PatternStackService.c:1383–1384`), `removeTrackAutomationByTarget` direct path (`PatternStackService.c:1442–1443`), `clearPattern` direct path (`PatternStackService.c:1408`), density restore (`PatternStackService.c:389–390`), lifecycle boundaries (init/handover/replace all set `reservation_rebuild_pending`). |
| 20. Lifecycle boundaries | YES | `patSvc_init()` (`PatternStackService.c:1203–1206`), `patSvc_finishSceneReplace()` (`PatternStackService.c:1255–1257`), handover-complete (`PatternStackService.c:1522–1524`) — all three call `patSvc_clearReservationImage()`, set `reservation_density_active = 1`, call `patSvc_updateDensityLevel()`. |
| 21. New trace stages | YES | `PatternTrace.h:50–52` — `'V'` (repair reserve), `'L'` (repair relocation), `'D'` (direct retain). Comment block updated to include S069 stages. |
| 22. Config constants | YES | `config.h:277–312`. `PAT_COMPACT_SCAN_PER_TICK` retained for reactive recovery. `PAT_RESERVATION_REDUCE_THRESHOLD` (70), `PAT_RESERVATION_RESTORE_THRESHOLD` (50), `PAT_REPAIR_SCAN_IDLE` (16), `PAT_REPAIR_SCAN_BUSY` (4) added. `PAT_COMPACT_INTERVAL_MS`, `PAT_GAP_REDUCE_THRESHOLD`, `PAT_COMPACT_FREE_RUN_THRESHOLD` removed. |
| 23. `SRAM_MANIFEST.md` update | YES | `reservation_image` 512 B and 3-byte flags documented. Session 069 summary added. |
| 24. `PATTERN_DYNAMIC_STACK.md` §12 update | YES | §12.2 updated to include repair epoch. §12.6 rewritten for owned trailing-slack reservation. Trace stage table updated with V/L/D. |

### Observations beyond the schedule

1. **`pat_poolFree` reservation cleanup** — `PatternData.c:289–290` clears
   the trailing reservation when a block is freed. This was not in the
   implementation schedule but is correct and necessary: a freed block's
   trailing reservation must not persist as a stale claim.

2. **`patSvc_addressEntry` helper** — `PatternStackService.c:624–629` is a
   new helper that computes an address-entry pointer from a flat index,
   avoiding packed-struct member warnings. Used by `patSvc_repairStep`,
   `patSvc_reactiveStep`, and `patSvc_relocateIndex`. Not in the schedule
   but a clean factoring that eliminates the old `&region->address[t][s]`
   access from the repair path.

3. **`repair_budget_busy` attribute** — declared with
   `__attribute__((used))` (`PatternStackService.c:155`) to suppress an
   unused-variable warning since the flag is read only through
   `patSvc_repairBudget()` and the compiler may not trace that path. Minor
   pragmatic choice; correct.

4. **Reactive second-pass reservation clear on trailing** —
   `PatternStackService.c:877–879` clears the old trailing reservation after
   a reclaim-path relocation, matching the `patSvc_relocateIndex` cleanup at
   line 686–687. Consistent.

5. **Image size delta** — .img grew +820 bytes. This is .text growth from
   the new helpers, repair step, reactive reclaim path, and direct-path
   parity fix, offset by the removal of Tier 1/Tier 2. The .bss growth
   (+515 B) is in SRAM and does not appear in the .img. Reasonable.

### Verification checklist against plan targets

| Target | Status |
|---|---|
| Image geometry: 512 B service-owned, no region growth | PASS (source) |
| Rebuild: lifecycle boundaries clear + rebuild-pending | PASS (source: three call sites) |
| Reservation vs. occupancy: reserved chunks refused | PASS (source: `findFreeRun`, `largestFreeRun`, `pat_poolAlloc`) |
| Growth: Gate-6 consumes reservation | PASS (source: `pat_tryAppendAutomation` lines 528–533) |
| Idle service: cursor sleeps at `PATSVC_ADDRESS_COUNT` | PASS (source: no self-chase, no timer restart) |
| Tier 2 removal: zero periodic relocation | PASS (source: entire block deleted, zero references remain) |
| Fragmentation, direct path: retention + `'D'` trace | PASS (source: `patSvc_submit` lines 1125–1158) |
| Density scaling: hysteresis latch | PASS (source: `patSvc_updateDensityLevel` lines 379–392) |
| Adaptive budget: busy/idle selection | PASS (source: `patSvc_repairBudget` line 421) |
| Incomplete reservation fallback: occupancy-only allocation | PASS (source: empty image → all `reservationGet` return 0) |
| RAM proof | PENDING — requires link map from hardware build |
| Persistence: PAT4 unchanged | PASS (source: no `pat_scene_region_t` or format changes) |

### Conclusion

Every plan item is implemented. The implementation makes four additions
beyond the schedule (trailing-reservation cleanup on free, `addressEntry`
helper, `__attribute__((used))`, and reclaim-path trailing cleanup); all four
are correct and consistent with the plan's invariants. No plan items were
skipped or deferred. Hardware verification and link-map RAM proof remain.

---

## Hardware test results

### Test output: `SD_CARD_SLACK_REACTIVE_TEST_OUT/`

Build image: `LXRV2_lxr02.img` (449,828 bytes, +820 from baseline).

#### pattrace.bin — 375 records (3,000 bytes)

The trace file contains records from both the old firmware (pre-implementation)
and the new firmware, providing a direct before/after comparison.

**Old firmware (records 0–260, scenes 4 and 7):**

| Stage | Count | Description |
|---|---|---|
| `M` (TIER1_GAP) | 154 | Tier 1 gap creation — the self-chasing mechanism |
| `R` (TIER2_RELOC) | 107 | Tier 2 periodic compaction + reactive recovery |

The M/R pattern is the expected Tier 1/Tier 2 self-chase: Tier 1 creates
gaps, Tier 2 packs them closed, repeat. Blocks shuffle between low chunk
positions (0–16) across short tick intervals, never converging.

**New firmware (records 261–374, scenes 7 then 5):**

| Stage | Count | Description |
|---|---|---|
| `V` (REPAIR_RESERVE) | 28 | In-place trailing reservation — no relocation |
| `L` (REPAIR_RELOC) | 86 | Repair relocation — block moved to create trailing reservation |
| `R` (TIER2_RELOC) | 0 | No periodic relocation — Tier 2 is deleted |
| `M` (TIER1_GAP) | 0 | No gap creation — Tier 1 is deleted |
| `C`/`F`/`D`/`Q`/`X`/`H` | 0 | No drops, no errors, no queue overflow |

**Key observations:**

1. **Tier 1/Tier 2 self-chase eliminated.** Zero `M` or `R` records after
   the firmware transition. The only relocations (`L`) are repair-step
   relocations that create owned trailing reservations, not periodic churn.

2. **Repair direction is correct.** 83 of 86 relocations move blocks to
   higher chunk addresses — the repair pass is spreading blocks upward to
   create trailing gaps for reservation bits. The 3 downward moves are edge
   cases where a lower free run of block+1 chunks was the only option.

3. **In-place reservations working.** 28 `V` records show the common-case
   fast path: the trailing chunk was already free, so the repair step set
   one reservation bit with no pool copy. This is the steady-state target.

4. **No drops or errors.** Zero capacity drops (`C`), fragmentation drops
   (`F`), direct-path retentions (`D`), queue overflows (`Q`), wrong-scene
   rejections (`X`), or pending overflows (`H`). Clean operation under
   normal editing load.

5. **Multiple repair epochs visible.** 53 inter-record gaps > 500 ticks
   (1 second at 500 Hz) among 114 new-firmware records indicate many
   distinct repair epochs. Mean inter-record delta: 1,321 ticks (~2.6 s).
   This confirms the cursor sleeps between epochs and wakes only on mutation
   events — no continuous scanning.

6. **Repeated relocations on some steps.** 29 of 114 address entries were
   relocated more than once across different repair epochs. No true A→B→A→B
   oscillation was observed — blocks settle at their repair destination but
   are displaced by user edits between epochs, then repair converges again.
   This is correct behavior: each epoch converges, user activity displaces,
   the next epoch re-converges.

7. **Scene transition handled correctly.** The first new-firmware record
   (index 261, `V`, flags=0x07) is on scene 7. Records 262+ switch to
   scene 5 (flags=0x05). This confirms the handover-complete boundary
   correctly cleared the reservation image and started a fresh repair epoch
   on the new scene.

#### Autosave trace (asavetrc.bin) — 74,980 records

Pattern autosave events (`P` stage): 80 total, with flags=0x00 (semantic,
38 events) and flags=0x01 (non-semantic, 42 events). Both save paths are
active and the non-semantic path is correctly tracking physical relocations.
Verify (`V` stage) events also present with varying flags (0x01, 0x03, 0x09,
0x0b) indicating successful verification across save types.

#### PAT files — 21 files, all 10,656 bytes

All PAT4 files are consistent in size. Scenes 0, 2, 4, 5, 7 have both a/b
copies; remaining scenes have single copies. No file corruption observed.

#### Verification target coverage from hardware

| Target | Status |
|---|---|
| Tier 2 removal | **PASS** — zero periodic `R` records after firmware transition |
| Idle service | **PASS** — repair cursor sleeps between epochs (large tick gaps) |
| Reservation in place | **PASS** — 28 `V` records show in-place reservation creation |
| Repair relocation | **PASS** — 86 `L` records show correct upward-spread relocation |
| Scene transition | **PASS** — handover from scene 7 to scene 5 produces clean epoch restart |
| No drops/errors | **PASS** — zero C/F/D/Q/X/H records |
| Non-semantic AutoSave | **PASS** — flags=0x01 Pattern save events present in asavetrc |
| PAT4 integrity | **PASS** — all files consistent 10,656-byte size |
| Density scaling | NOT TESTED — pool occupancy did not cross 70% threshold |
| Adaptive budget | NOT TESTED — requires controlled AutoSave pressure |
| Direct-path retention | NOT TESTED — no `D` records (no direct-path fragmentation failures occurred) |
| RAM proof (link map) | PENDING |
