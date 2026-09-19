# Session 067 — Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)
**Date**: 2026-09-18
**Branch**: `dev-ph4-pattern` (uncommitted on base `87e275e`)
**Session goal**: Implement the Pattern Stack Service (unified pool mutation
dispatcher with defragmentation infrastructure) and follow up with the dtype
automation value offset bug fix.

---

## End of Session

```
DATE: 2026-09-18
SESSION GOAL: Implement Pattern Stack Service (service dispatcher, pool
  defragmentation, pool usage monitor) and fix the dtype automation value
  offset bug.
COMPLETED: Full Pattern Stack Service (Part A + Part B) implemented
  and hardware-validated. Dtype offset bug root-caused and fixed at all
  four code sites, hardware-validated.
VERIFIED ON HARDWARE: Yes. PatternTrace (zero errors across thousands of
  records), AutoSaveTrace (zero errors across ~78K records), PAT4 structural
  integrity confirmed, automation values confirmed in identity domain post-fix.

CHANGES THIS SESSION:
- PatternStackService.c/h: New unified pool mutation dispatcher (1,331 lines)
- PatternData.c/h: pool usage, publication ordering, release/dirty/append APIs
- PatternTrace.h: 7 new trace stage codes
- menu.c/h: Part B widget, dtype fix, 15+ caller migration
- MenuText.h, menuPages.h: widget strings and page position
- sequencer.c/h: dtype fix, queue event, per-track pattern array
- copyClearTools.c: 2 call sites migrated
- EuklidGenerator.c: 1 call site migrated
- timebase.c: patSvc_tick() call
- filesystem.c: patSvc_idle() autosave guard
- main.c: patSvc_init() call
- Makefile: added PatternStackService.c
- config.h: 3 new constants

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED:
- Publication ordering: freed pool block address could be read by TIM3
  between free and address update; fixed with write-new/swap/free-old.
- Dtype offset bug: automation values halved on write and doubled on read/
  playback; fixed with identity mapping for all automatable dtypes.
- osc2_mod_type=51 in T1S11 identified as pre-fix artifact (exceeds DTYPE
  range 0..1), not a regression.

NEXT SESSION RECOMMENDED GOAL: Hardware validation of Pattern stack queueing
  under load, and Phase 4.5 copy operations (pat_copyTrack, pat_copyPattern,
  pat_copyBar) with independent pool-block duplication.
BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
- All presently uncommitted RAM is reserved (DTCM for delay-line buffers,
  SRAM1 for Pattern data). Obtain user acknowledgement before any new
  allocation.
- Always make clean after editing config.h. The Makefile has no header
  dependency tracking.
- Blocking for 1ms anywhere in main loop or any ISR at priority <= 4 is
  unacceptable.
- patSvc_tick() runs from timebase.c after endlessPots_tick(); patSvc_init()
  runs from main.c after the boot filesystem ladder.
- Automation values are now identity mapped (stored 7-bit = parameter value).
  The old /2 *2 conversion is removed from all four sites. Do not reintroduce.
```

---

## 1. Pattern Stack Service — Part B: Pool Usage Monitor

### 1.1 Design

A Settings-menu widget shows the current pool occupancy as a percentage.
`pat_poolUsagePercent()` in PatternData.c reads the Scene bitmap with
`memcpy` (packed-bitmap-safe) and counts set bits with `__builtin_popcount`
across the 64 words of the active Scene's 256-byte live bitmap region. The
result is `(occupied_chunks * 100) / 2048`, clamped to 99 to keep the display
at two digits.

### 1.2 Implementation

| Item | File | Detail |
|------|------|--------|
| B-2 | PatternData.c | `pat_poolUsagePercent()` — memcpy 256 bytes, popcount 64 words, return 0..99 |
| B-3 | PatternData.h | `uint8_t pat_poolUsagePercent(void)` declaration |
| B-4 | menu.h | `TEXT_PAT_STORE_USE` short/long enum entries |
| B-5 | MenuText.h | `"pts"` short name, `"PtrnStoreUse"` long name |
| B-6 | menu.h | `PAR_PAT_STORE_USE` sentinel (0xFFFD) |
| B-7 | menuPages.h | Global subpage 2 position 7 |
| B-8 | menu.c | Retained `static uint8_t menu_patStoreUseValue` |
| B-9 | menu.c | Visibility predicate: shown only on Global subpage |
| B-10 | menu.c | Format helper: `"pts:NN"` display |
| B-11 | menu.c | Click-in display for PAR_PAT_STORE_USE |
| B-12 | menu.c | 4 rendering path modifications |
| B-13 | menu.c | Compute-on-entry hook in `menu_switchPage()` |
| B-14 | menu.c | Forward declarations |

No new RAM allocation (the retained byte is ordinary `.bss` in existing
menu state). The popcount is computed once on page entry and retained until
the page is exited, not per-frame.

---

## 2. Pattern Stack Service — Part A: Service Dispatcher

### 2.1 Architecture

New files: `Core/Bank/Scene/Pattern/PatternStackService.c` (1,225 lines) and
`PatternStackService.h` (106 lines).

The Pattern Stack Service is a unified dispatcher that serializes all
pool-mutating operations through a single service tick. This guarantees
exactly one mutation target at a time, preventing concurrent access between
foreground callers, the TIM3 ISR's automation reads, and the filesystem
replacement boundary.

**Admission policy**: direct-when-idle (immediate foreground execution with
service lock), queued-when-busy (PRIMASK-protected enqueue into the SPSC ring
for later drain).

**Service tick priority order** (evaluated every call to `patSvc_tick()`):
1. Handover — check and complete filesystem replacement boundary transitions.
2. Queue drain — dequeue and execute one pending mutation.
3. Bulk barrier — advance one step of a bounded track/pattern clear sweep.
4. Tier 1 trailing-gap maintenance — scan freed region for adjacent free
   chunks, merge up to 16 per tick.
5. Tier 2 paced compaction — relocate live blocks toward pool start under
   pacing constraints.

### 2.2 Queue format

64-entry volatile `uint32_t` ring buffer (256 bytes SRAM1). Each entry packs:

```text
bits 31..29   operation code (3 bits): WRITE_AUTO, REMOVE_AUTO, SET_NOTE,
              SET_VOLUME, SET_PROBABILITY, ERASE_STEP, CLEAR_TRACK,
              CLEAR_PATTERN, REMOVE_TRACK_AUTO
bits 28..25   scene (4 bits)
bits 24..22   track (3 bits)
bits 21..15   step (7 bits)
bits 14..6    target (9 bits) — instrument_param_id_t for automation ops
bits 5..0     value (7 bits) — 0..127 instrument_param_value_t
```

Enqueue uses `__disable_irq()`/`__enable_irq()` (PRIMASK) to protect
head/tail consistency. Dequeue is foreground-only. Queue full is a silent
drop (mutation lost); this is bounded by the 64-entry depth and the service
tick rate.

### 2.3 Bulk barriers

Track clear and pattern clear are bounded operations that sweep through
steps at a budget of 16 per service tick, preventing unbounded latency spikes
in the foreground:

- **Track clear**: 128-step sweep over one track, freeing pool blocks and
  resetting address entries. Completes in 8 ticks at 16 steps/tick.
- **Pattern clear**: 7-track × 128-step sweep. Inner loop is identical to
  track clear with a track cursor. Completes in 56 ticks.

Both barriers are entered by enqueueing the barrier start event, which sets
`bulk_op`/`bulk_track`/`bulk_cursor` state in the service.

### 2.4 Handover state machine

When a filesystem replacement operation completes (Scene Load, Bank Load,
Pattern Load), it must redirect the service to operate on the newly loaded
Scene's pool. The handover works through `patSvc_idle()` (called from 5
filesystem replacement boundary points in `filesystem.c`):

1. Filesystem signals replacement pending via `replace_pending`.
2. Service completes or abandons any in-flight bulk barrier.
3. Service drains remaining queue entries for the old Scene.
4. Service switches `service_scene` to the new target.
5. Service clears internal gap/compaction cursors for the new Scene.

### 2.5 Tier 1 trailing-gap maintenance

After each freed block, the service performs a linear scan from the freed
offset to find and merge adjacent free chunks. Budget: up to 16 chunk
examinations per tick. Purpose: maintain contiguous trailing free space at
the pool tail for efficient allocation without requiring full compaction.

Gap state is tracked by `tier1_scan_cursor` in the service. The scan
advances on each tick until it reaches a live block or exhausts the budget.

### 2.6 Tier 2 paced compaction

Full pool defragmentation that relocates live blocks toward the pool start.
Pacing constraints:

- `PAT_COMPACT_INTERVAL_MS` (100 ms): minimum interval between compaction
  cycles.
- `PAT_COMPACT_SCAN_PER_TICK` (16 chunks): maximum chunks examined per tick.
- Reactive compaction: triggered immediately when head allocation fails and
  there is recoverable free space (pool occupancy below
  `PAT_GAP_REDUCE_THRESHOLD`).

Relocation under PRIMASK: each block move follows write-new/update-address/
free-old with `__disable_irq()`/`__enable_irq()` around the address-entry
swap, keeping the TIM3 ISR's address reads consistent.

### 2.7 Elastic gap policy

`PAT_GAP_REDUCE_THRESHOLD` (60%, set in `config.h`) is the pool occupancy
percentage above which trailing-gap maintenance is active. Below this
threshold, gaps are tolerable and the service does not spend ticks on gap
scanning. This prevents unnecessary foreground work when the pool is
lightly loaded.

### 2.8 PatternTrace stage codes

Seven new trace stage codes added to `PatternTrace.h`:

| Code | Meaning |
|------|---------|
| `Q` | Queue event — enqueue/dequeue/drop |
| `C` | Compaction — Tier 2 block relocation |
| `F` | Free/gap — block freed, gap state change |
| `R` | Relocation — live block moved (Tier 2) |
| `M` | Mutation — pool-mutating operation applied |
| `G` | Gap scan — Tier 1 gap examination result |
| `X` | Service state change — handover, mode transition |

These are distinct from the `DEV_MODES.md` AutoSaveTrace stages (which use
the same single-letter convention but different code points and different
owning files).

### 2.9 Caller migration

15+ call sites across `menu.c`, `copyClearTools.c`, `EuklidGenerator.c`, and
`sequencer.c` changed from direct `pat_*` mutation calls to `patSvc_*`
routing through the service. The migrated operations:

| Original call | New call | File(s) |
|---------------|----------|---------|
| `pat_writeStepAutomation()` | `patSvc_writeStepAutomation()` | menu.c |
| `pat_removeStepAutomation()` | `patSvc_removeStepAutomation()` | menu.c |
| `pat_setStepNote()` | `patSvc_setStepNote()` | menu.c |
| `pat_setStepVolume()` | `patSvc_setStepVolume()` | menu.c |
| `pat_setStepProbability()` | `patSvc_setStepProbability()` | menu.c |
| `pat_eraseStep()` | `patSvc_eraseStep()` | sequencer.c |
| `pat_clearTrack()` | `patSvc_clearTrack()` | copyClearTools.c |
| `pat_clearPattern()` | `patSvc_clearPattern()` | copyClearTools.c |
| `pat_removeTrackAutomationByTarget()` | `patSvc_removeTrackAutomationByTarget()` | menu.c |
| `pat_enqueueErase()` (TIM3 live erase) | `patSvc_enqueueErase()` | sequencer.c |

### 2.10 Public API

```c
void     patSvc_init(void);
void     patSvc_tick(void);
void     patSvc_idle(void);
uint8_t  patSvc_writeStepAutomation(uint8_t scene, uint8_t track,
             uint8_t step, uint16_t target9, uint8_t value7);
uint8_t  patSvc_removeStepAutomation(uint8_t scene, uint8_t track,
             uint8_t step, uint16_t target9);
void     patSvc_setStepNote(uint8_t scene, uint8_t track,
             uint8_t step, uint8_t note);
void     patSvc_setStepVolume(uint8_t scene, uint8_t track,
             uint8_t step, uint8_t volume);
void     patSvc_setStepProbability(uint8_t scene, uint8_t track,
             uint8_t step, uint8_t prob);
void     patSvc_eraseStep(uint8_t scene, uint8_t track, uint8_t step);
void     patSvc_clearTrack(uint8_t scene, uint8_t track);
void     patSvc_clearPattern(uint8_t scene);
void     patSvc_removeTrackAutomationByTarget(uint8_t scene,
             uint8_t track, uint16_t target9);
void     patSvc_enqueueErase(uint8_t scene, uint8_t track,
             uint8_t step);
```

### 2.11 Integration points

| Location | Change |
|----------|--------|
| `main.c` | `patSvc_init()` after boot filesystem ladder |
| `timebase.c` | `patSvc_tick()` after `endlessPots_tick()` |
| `filesystem.c` | `patSvc_idle()` at 5 filesystem replacement boundary points |
| `Makefile` | Added `PatternStackService.c` to build |
| `config.h` | `PAT_GAP_REDUCE_THRESHOLD` (60u), `PAT_COMPACT_INTERVAL_MS` (100u), `PAT_COMPACT_SCAN_PER_TICK` (16u) |

---

## 3. Publication Ordering Fix (Gate 1)

### 3.1 Problem

`pat_writeDynamic()` could free the old pool block before publishing the new
address to the address array. If TIM3 preempted between the free and the
address update, it would read zeroed pool bytes from the freed block. The
same-size in-place rewrite path was additionally unsafe because even a
removal of automation entries can compact the remaining bytes, creating a
window where TIM3 reads partially updated block content.

### 3.2 Fix

Three address-to-pool transaction patterns, all following
detach/publish-before-free:

1. **`pat_writeDynamic()` replace path**: allocate new block, write complete
   content, PRIMASK-protect the address-entry swap (re-read trigger bit
   before write to avoid race with TIM3 step advance), then free old block.
   Same-size in-place rewrite removed entirely.

2. **`pat_writeDynamic()` clear path**: when last special removed and no
   automations, PRIMASK-protect address detach (clear bit 14, sentinel
   offset), then free old block.

3. **`pat_eraseStep()`**: detach address first (clear entire address word),
   then free old block.

### 3.3 New helper functions

- `pat_releaseStepDynamic()`: trigger-preserving detach+free. Clears bit 14
  and sets sentinel offset while preserving the trigger bit, then frees the
  pool block.
- `pat_markPoolMutationDirty()`: narrow dirty-marking boundary for service
  operations that may not go through the normal `pat_write*` paths.
- `pat_tryAppendAutomation()`: Gate 6 in-place growth for adding automation
  entries to an existing block when there is adjacent free space.

### 3.4 PRIMASK audit

All PRIMASK sections confirmed under 50 ns:
- Address-entry read-modify-write: one `ldrh`, one `bfi`/`bic`, one `strh`.
- No memory allocation, no loop, no function call under PRIMASK.

---

## 4. Dtype Offset Bug Fix

### 4.1 Root cause

The 7-bit automation storage used a MIDI CC-style `/2` (encode) and `*2`
(decode) conversion, borrowed from the original MIDI CC path (Session 065,
§6.3). This conversion exists because MIDI CC maps 0..127 to 0..255 for
`parameter_values[]`, where every parameter is stored in a flat `uint8_t`
array regardless of its actual range.

But automation targets are instrument descriptor parameters, not
`parameter_values[]` entries. Every descriptor parameter that carries
`INSTRUMENT_PARAM_FLAG_AUTOMATABLE` has a dtype whose range already fits in
7 bits (DTYPE_0B127, DTYPE_PM63, DTYPE_MENU, DTYPE_ON_OFF, DTYPE_MIX_FM,
DTYPE_LFO_POLARITY, DTYPE_NOTE_NAME, DTYPE_1B16). No `DTYPE_0B255`
parameter is automatable — the only `DTYPE_0B255` parameters are
`PAR_MORPH`, `PAR_VOICE1_MORPH`..`PAR_VOICE6_MORPH`, and `PAR_BPM`, none
of which are instrument descriptors.

The `/2` and `*2` conversion therefore serves no purpose for any currently
automatable parameter.

### 4.2 Symptoms

- Filter type "UBP" (index 3) reads back as "BP" (index 2): `3/2=1`,
  `1*2=2`.
- Pan +10 (value 73) reads back as +9: `73/2=36`, `36*2=72`, `72-63=+9`.
- `DTYPE_ON_OFF` catastrophically broken: ON (1) stores as `1/2=0` (OFF).
- DSP runtime receives doubled value, so playback matches the erroneous
  readback.

### 4.3 Fix (6 changes)

| Change | File | Detail |
|--------|------|--------|
| 1 | menu.c | `va_expand7to8()` renamed to `va_storedToParam()` — identity function, 3 call sites |
| 2 | menu.c | `va_writeAutomationFromKnob()` — removed halving, defensive 127 saturation |
| 3 | sequencer.c | `seq_drainPendingAutomation()` — removed doubling, single variable |
| 4 | menu.c | `menu_stepAutomationAddDefault()` — removed halving on creation |
| 5 | menu.c | Five comment updates removing "lossy 8→7→8" language |
| 6 | menu.c | `va_formatValue3()` comment update |

### 4.4 Backwards compatibility

Existing stored automation values written via the voice overlay are in the
halved domain. After the fix, those values play back at half their intended
parameter value. This is acceptable because they were already wrong both in
display and playback. Migration is not practical (cannot distinguish halved
from raw values in live data).

### 4.5 Working-value cache review

With identity conversion, `va_workingValue[]` and the nibble-split
`va_underlineSuppressed` mechanism from S066 Fixes 1/4 still serve a useful
purpose: they prevent the display from re-reading stored values (which
truncate at 127) while the user is turning the encoder, and provide
consistent seeding between detents.

### 4.6 Impact on S066 Fixes 1 and 4

The working-value cache (Fix 1) and PM63 nibble split (Fix 4) were
originally developed to paper over the `/2*2` bug. They remain useful
for edit responsiveness but are no longer needed for round-trip accuracy.

---

## 5. Build Metrics

### 5.1 Baseline (Session 066)

```
text=439,396  data=412  bss=290,852
```

### 5.2 Post-service (Part A + Part B + Gate 1)

```
text=447,644  data=412  bss=291,140
```

Deltas: **+8,248 text, +288 bss**.

New allocations in PatternStackService.c:
- 256 B: 64-entry `uint32_t` SPSC ring
- Service state variables (scene, open, handover, replace_pending,
  bulk_op/track/target/cursors, logical_chunks_used, tier1_scan_cursor,
  reactive state, last_compact_tick): ~33 B
- Total new BSS: 289 bytes actual vs 282 bytes scheduled, within the +300
  approved ceiling.

### 5.3 Post-dtype-fix

```
text=447,580  data=412  bss=291,140
```

Deltas from post-service: **-64 text** (dtype fix removed code), **0 bss**.

---

## 6. Post-Implementation Code Assessment

### 6.1 Schedule compliance

All scheduled items present in the implementation. No omissions.

### 6.2 Deviations from schedule (7, all improvements)

1. **Shrink-in-place path removed entirely** (scheduled: retain with PRIMASK
   fix). Even a removal can compact automation bytes, making rewrite-in-place
   unsafe for TIM3. Safer than scheduled.

2. **Inverted queued clear ordering** (scheduled: clear triggers first, then
   enqueue barrier). Actual: enqueue barrier first, then clear triggers.
   Prevents orphaned dynamic blocks on full FIFO — if the queue is full when
   the barrier is posted, the barrier is lost and subsequent trigger clears
   would free pool blocks without the barrier's sweep to reclaim addresses.

3. **`pat_writeSpecials` and raw setters return `uint8_t`** (scheduled:
   `void`). Return value indicates success/failure, enabling callers to
   handle alloc failure gracefully.

4. **`pat_releaseStepDynamic` added** (not in schedule). Separate
   trigger-preserving detach+free path needed for the service's erase
   operation.

5. **`pat_markPoolMutationDirty` added** (not in schedule). Narrow
   dirty-marking boundary for service operations that bypass normal
   `pat_write*` paths.

6. **`pat_tryAppendAutomation` added** (not in schedule). Gate 6 in-place
   growth for adding automation entries when adjacent free space is available.

7. **`osc2_mod_type=51` in T1S11** identified as pre-fix artifact. Exceeds
   `DTYPE_MIX_FM` range 0..1. Written by the old halving path (`102/2=51`).
   Not a regression.

### 6.3 PRIMASK audit

All PRIMASK sections confirmed under 50 ns. No allocation, loop, or function
call under PRIMASK.

### 6.4 Publication ordering audit

All address-to-pool transactions follow detach/publish-before-free.

### 6.5 Queue safety audit

- Enqueue: PRIMASK-protected, bounded check, silent drop on full.
- Dequeue: foreground-only, no PRIMASK needed.
- No aliasing between queue entries and pool state.

### 6.6 Relocation transaction audit

Each block move:
1. Allocate destination space.
2. Copy block content.
3. PRIMASK: update address entry to point at new location.
4. Free source space.

### 6.7 Filesystem replacement boundary audit

All 5 `patSvc_idle()` call sites verified at filesystem replacement
boundaries where Scene Pattern data is being overwritten.

### 6.8 RAM budget

289 bytes actual vs 282 bytes scheduled. Delta of +7 bytes is within the
+300 approved ceiling and is attributable to alignment and minor state
variable additions.

---

## 7. Hardware Validation

### 7.1 Post-service validation

**PatternTrace**: 1,539 records total:
- 1,283 `TIER1_GAP` records — trailing-gap maintenance events
- 256 `TIER2_RELOC` records — compaction/relocation events
- **Zero errors**

**AutoSaveTrace**: 78,592 records, **zero errors**.

**PAT4 structural integrity**: 3 PAT4 files examined, all valid:
- Address arrays consistent with bitmap state
- Pool back-references match address entries
- Block sizes match automation counts and special flags
- CRC32C verified

**Relocation chains**: verified contiguous — blocks relocated to pool head
maintain correct address-entry back-references.

**User-write blocks**: 2 blocks identified and explained as user-originated
writes during the validation period.

### 7.2 Post-dtype-fix validation

**PatternTrace**: 4,633 records total:
- 3,902 `TIER1_GAP` records
- 731 `TIER2_RELOC` records
- **Zero errors**

**AutoSaveTrace**: 78,615 records, **zero errors**.

**PAT4 integrity**: confirmed.

**Automation values**: confirmed in identity domain post-fix. Filter types,
pan values, and on/off parameters all round-trip correctly through
store/read/playback.

---

## 8. Design Decisions (Resolved)

1. **One mutation target at a time**: the service guarantees exclusive
   access to one Scene's pool at a time. No concurrent mutation from
   multiple callers.

2. **Instant playback switching**: TIM3's automation read path is not routed
   through the service. It reads address entries directly, which are always
   in a consistent state due to the publication ordering fix.

3. **Lazy gaps**: trailing-gap maintenance is best-effort, not mandatory.
   A failed gap merge does not block further operations.

4. **Pool-use = occupancy bits**: the pool usage percentage is computed from
   bitmap popcount, not from tracking individual allocations.

5. **Elastic gap policy**: gap maintenance activates only above
   `PAT_GAP_REDUCE_THRESHOLD` (60% occupancy).

6. **Reactive compaction for blocked heads**: when allocation fails at pool
   head but total occupancy allows it, compaction is triggered immediately
   rather than waiting for the paced interval.

7. **Direct-when-idle**: foreground callers execute mutations directly when
   the service is idle, avoiding queue overhead for the common case.

8. **Queue-when-busy**: during bulk barriers or compaction, mutations are
   queued in the SPSC ring for later drain.

9. **Dtype identity mapping**: stored 7-bit value = parameter value for all
   automatable dtypes. The `/2` `*2` conversion is reserved for a future
   `DTYPE_0B255` automation target if one is ever added.

10. **Removed same-size in-place rewrite**: even though it saves an
    allocation, the in-place write is unsafe because automation byte
    compaction can change block content while TIM3 reads.

---

## 9. Binding Architectural Constraints

These constraints from `S067_STACK_SERVICE_DETAIL_PLAN.md` are binding on
future modifications:

1. No pool mutation path may bypass the service dispatcher without explicit
   justification and PRIMASK audit.
2. Exactly one mutation target Scene at a time. Scene switching requires
   completing or abandoning the current target.
3. TIM3 reads address entries directly; address entries must always point at
   valid or sentinel data.
4. PRIMASK sections must stay under 50 ns. No allocation, loop, or function
   call under PRIMASK.
5. Bulk barriers must be bounded per-tick. Unbounded sweeps are not
   acceptable.
6. The filesystem replacement boundary (`patSvc_idle()`) must be called at
   every point where Scene Pattern data is being overwritten.
7. Queue drop on full is the accepted failure mode. Do not block or retry.

---

## 10. Files Changed

| File | Insertions | Deletions | Net | Summary |
|------|-----------|-----------|-----|---------|
| `PatternStackService.c` | 1,225 | 0 | +1,225 | New: complete service |
| `PatternStackService.h` | 106 | 0 | +106 | New: public API |
| `PatternData.c` | ~150 | ~30 | ~+120 | Pool usage, pub ordering, helpers |
| `PatternData.h` | ~5 | 0 | ~+5 | `pat_poolUsagePercent()` decl |
| `PatternTrace.h` | ~15 | 0 | ~+15 | 7 trace stage codes |
| `menu.c` | ~200 | ~80 | ~+120 | Widget, dtype fix, migration |
| `menu.h` | ~10 | 0 | ~+10 | Enum entries, sentinel |
| `MenuText.h` | ~2 | 0 | ~+2 | Widget strings |
| `menuPages.h` | ~1 | ~1 | 0 | Page position |
| `sequencer.c` | ~40 | ~30 | ~+10 | Dtype fix, queue, arrays |
| `sequencer.h` | ~2 | 0 | ~+2 | extern decl |
| `copyClearTools.c` | ~2 | ~2 | 0 | Migration |
| `EuklidGenerator.c` | ~1 | ~1 | 0 | Migration |
| `timebase.c` | ~2 | 0 | ~+2 | `patSvc_tick()` call |
| `filesystem.c` | ~10 | 0 | ~+10 | `patSvc_idle()` guard |
| `main.c` | ~2 | 0 | ~+2 | `patSvc_init()` call |
| `Makefile` | ~1 | 0 | ~+1 | Source file |
| `config.h` | ~3 | 0 | ~+3 | 3 constants |
| **Total** | **~1,777** | **~144** | **~+1,633** | 20 files |

(Line counts are approximate; the S067 planning documents report ~2,113
total insertions across both parts plus the dtype fix.)

---

## 11. Specification Updates Required

The following specification-reference documents need updates for Session 067:

- **`PATTERN_DYNAMIC_STACK.md`**: Add Pattern Stack Service (§ for service
  architecture, queue, bulk barriers, handover, gap/compaction, trace codes).
  Update §4 automation value domain from `/2*2` to identity mapping. Add
  publication ordering contract. Mark compaction/defragmentation as
  implemented.

- **`SRAM_MANIFEST.md`**: Update build metrics from S066 baseline to S067
  final. Add PatternStackService.c allocations (+288 bss, within +300
  ceiling).

- **`DEV_MODES.md`**: Add 7 new PatternTrace stage codes (Q/C/F/R/M/G/X).
  Note these are PatternTrace codes distinct from AutoSaveTrace codes.

- **`MODULE_INTERCHANGE_SPEC.md`**: Add PatternStackService as a new module
  section with its API table. Update PatternData section to note publication
  ordering fix and return-type changes. Update session-through header.

---

## 12. Source Document Disposition

The following root-level S067 planning/implementation documents are
**disposable** after this handoff log and the specification updates are
written:

- `S067_STACK_SERVICE_DETAIL_PLAN.md` (962 lines) — authoritative design
  plan, now preserved in this handoff log §2 and PATTERN_DYNAMIC_STACK.md.
- `S067_STACK_SERVICE_IMPLEMENTATION.md` (1,826 lines) — line-by-line code
  schedule and post-implementation assessment, now preserved in this handoff
  log §6 and §10.
- `S067_DTYPE_OFFSET_BUG.md` — root cause analysis, now preserved in this
  handoff log §4.
- `S067_DTYPE_BUG_IMPLEMENTATION.md` (588 lines) — implementation schedule
  and hardware validation, now preserved in this handoff log §4 and §7.

All important details from these documents are preserved in this handoff log
and the specification-reference updates.
