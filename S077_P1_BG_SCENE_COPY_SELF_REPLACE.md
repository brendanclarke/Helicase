# S077 P1 — 17th Background Scene Region and Copy Snapshot Migration

General plan for Session 077. Derived from the S076 P4 self-paste
investigation, which found the current implementation correct but identified
a latent coupling: the copy/clear snapshot borrows the 9 kB name buffer,
which can hold the current pool size (8,060 B usable, 428 B margin) but will
break if the pool grows. The fix is architectural: allocate a full 17th
`pat_scene_region_t` that serves as both the AutoSave snapshot staging buffer
and the copy/clear scratch region.

---

## Background

### Current state

- **`pat_regions[16]`** — 16 live Scene Pattern regions (168,304 B SRAM1).
- **`pat_autosave_snapshot`** — one standalone `pat_scene_region_t`
  (10,519 B SRAM1). Used exclusively by `pat_snapshotScene()` /
  `pat_autosaveSnapshot()` for the Pattern AutoSave background writer.
  Immutable while a Pattern drain is in flight.
- **Copy/clear snapshot** — when a step/bar paste overlaps its own source,
  the engine borrows the 9 kB filesystem name buffer and copies the source
  blocks into it (up to 8,060 B of block data plus a 256 B lookup table and
  161 B remap). A `_Static_assert` enforces that the pool fits; margin is
  428 B.

### Problem

`PAT_STACK_SIZE` (currently 256 units = 8,192 B pool) is a compile-time
define documented as expandable ("changing this to 512 later expands the pool
without changing the address-entry or bitmap representation"). Free SRAM1 is
reserved for future Pattern data. If the pool grows past ~8,488 B the static
assert fails and the copy snapshot has nowhere to go — the 9 kB name buffer
is a fixed, unrelated allocation.

### Proposed fix

Replace the standalone `pat_autosave_snapshot` with a full 17th entry in
`pat_regions` (or equivalent). This region:

1. **Is the AutoSave snapshot** — `pat_snapshotScene()` copies into it as
   today; `pat_autosaveSnapshot()` returns it as today. No change to the
   filesystem Pattern drain writer.
2. **Is the copy/clear scratch region** — an overlapping paste copies its
   source blocks into the 17th region's pool instead of the name buffer.
   The pool is always large enough (it is the same size as the source pool).
   The copy engine no longer borrows the name buffer for block data.
3. **Is the future Bank Load staging area** — when Bank Load needs to hold
   the playing Scene's Pattern while loading a new Bank, this region serves
   that purpose (already planned, not yet implemented).

Net RAM change: **+256 B** (user-approved). The 17th region (10,519 B)
replaces the standalone snapshot (10,519 B) at exactly the same size; the
256 B is the new static paste source table (Q2).

---

## Decisions (pre-implementation review, S077)

| # | Decision |
|---|---|
| Q1 | **Option B** — keep `pat_regions[SCENE_COUNT]`; add a separate named `pat_background_region`. |
| Q2 | **Option (b)** — the 128-entry paste source table becomes a static `uint16_t[128]` (256 B, SRAM1) in `copyClearService.c`. +256 B approved. |
| Q3 | **Option (a)** — keep the name-buffer borrow for the 161 B HCNAMES remap only. Load/Save also dual-uses the buffer (`.hcindex`, `.hcnames`), so the use stays consistent. |
| Q4 | Confirmed — sequential source capture between queued pastes stays accepted behaviour. |
| Q5 | Region sizes confirmed identical (same `pat_scene_region_t` type). One further limit found and fixed this session: the table offset field (see Step 3). |
| Q6 | **Wait, and measure.** The paste waits explicitly for a Pattern drain to leave its snapshot-reading phases. A trace records the wait. Abandoning the drain is deferred (see Q6). |
| Q7 | Confirmed — Bank Load staging is future work; Load/Save is mutually exclusive with AutoSave and copy/clear. |

---

## Plan

### Step 1 — Allocate the 17th region

- Keep `pat_regions[SCENE_COUNT]`. Add
  `static pat_scene_region_t pat_background_region;` in `PatternData.c`
  (SRAM1 .bss, 10,519 B, static lifetime, owner `PatternData.c`).
- Remove `pat_autosave_snapshot`.
- The 17th region is **not a playable Scene**: `scene_indexValid()`,
  `pat_patternValid()`, the sequencer, the UI, PERF and Bank all continue
  to bound at `SCENE_COUNT` (16). It is reached only through its named
  accessors.
- Accessors:
  - `pat_snapshotScene()` / `pat_autosaveSnapshot()` — names unchanged.
  - New `uint8_t *pat_backgroundPoolMut(void)` — the pool bytes, for
    copy/clear scratch use only.

### Step 2 — Migrate AutoSave snapshot

- `pat_snapshotScene()` copies into `pat_background_region`;
  `pat_autosaveSnapshot()` returns it.
- The filesystem Pattern drain writer is unchanged apart from the new
  snapshot-in-use query (Step 4).
- The boot reader (`filesystem_patternAutosaveBootReaderBlocking()`) keeps
  using the snapshot as its rollback copy; boot only.

### Step 3 — Migrate copy/clear snapshot to the 17th region's pool

- `ccSvc_pasteOverlaps()` detection is unchanged.
- An overlapping paste no longer calls `ccSvc_ensureScratch()` at path
  selection. Block data is copied into `pat_backgroundPoolMut()` (the full
  pool, 8,192 B today; the swap reservation and bitmap are irrelevant for
  raw scratch).
- The paste source table moves to a static `uint16_t ccSvc_snapTable[128]`
  (256 B, naturally aligned; replaces the cast into the byte buffer).
- **Table offset width.** `CC_SNAP_OFFSET` is 11 bits (max 2,047 chunks).
  Today's maximum is 2,015 chunks, but at `PAT_STACK_SIZE 512` it is 4,063
  and would wrap silently. Widen the mask to `0x0FFF` (bits 12–13 are free;
  `CC_SNAP_BLOCK` = 0x4000, `CC_SNAP_TRIGGER` = 0x8000) and add
  `_Static_assert(PAT_POOL_ALLOC_CHUNKS <= CC_SNAP_OFFSET + 1u)`.
- The HCNAMES row remap (161 B) stays in the borrowed name buffer, which is
  now borrowed only for the end-of-operation name phase
  (`ccSvc_namesReady()` and the final HCNAMES write).
- Remove `CC_SCRATCH_TABLE_OFFSET`, `CC_SCRATCH_BLOCK_OFFSET` and the
  `_Static_assert` tying `PAT_POOL_ALLOC_CHUNKS` to `FS_NAME_SCRATCH_BYTES`.
  Update the `ccSvc_scratch()` layout comment to the remap only.
- Side effect: an overlapping paste no longer waits for an idle filesystem
  facade (scalar AutoSave, trace, settings writes). It waits only for the
  Pattern drain snapshot gate (Step 4).

### Step 4 — Snapshot gate and mutual exclusion

**Finding.** `patSvc_beginExclusive()` does not wait for the filesystem
Pattern drain, and the suspension gate only stops new writers from being
admitted ("a writer already running finishes normally"). Today the overlapping
paste is serialised only by accident: borrowing the name buffer requires
`status == FS_STATUS_IDLE`. Step 3 removes that borrow, so an explicit gate is
required.

**Gate.** The Pattern drain reads the snapshot in phases 2–6 (header,
address array, bitmap, pool) and not in phases 7–10 (CRC, close, HCNAMES
update). Add `uint8_t filesystem_patternSnapshotInUse(void)`: true while
`FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN` is the current op and `op_phase <= 6`.
The paste returns `CC_RUN_WAIT` at path selection while it is true. Both
loops are cooperative main-loop code (`ccSvc_tick()` via
`timebase_serviceFrontPanel()`, and `filesystem_tick()`), so there is no
interrupt race on the 10.5 kB snapshot copy.

**Measure.** In logging builds, record the gate wait (ticks) in a copy/clear
trace record, alongside the existing scratch-wait record. Trigger bits are
already written at press time through the early trigger masks, so the wait
delays only specials/automation placement.

**Ownership contract** (document in `PatternData.c` and the spec):

| User A | User B | Prevented by |
|---|---|---|
| AutoSave Pattern drain (reads phases 2–6) | Copy/clear snapshot | New drains: `copyClear_backgroundSuspended()`. In-flight drain: `filesystem_patternSnapshotInUse()` gate |
| AutoSave Pattern drain | Bank Load staging | (future) Bank Load suspends AutoSave by the same mechanism |
| Copy/clear snapshot | Bank Load staging | (future) copy/clear refused while Load/Save owns the UI (`menu_storageBusy`) |
| Boot reader rollback copy | any | Boot only; runs before copy/clear or AutoSave can start |

### Step 5 — Name buffer borrowing

Per Q3(a), the borrow stays for the 161 B remap and the HCNAMES write.
`FS_NAME_CACHE_COPYCLEAR`, the filesystem refusal gate while lent and the
Load/Save entry wait are unchanged, but the borrow is now shorter.

### Step 6 — Build, test and document

- `make all && make img` — expect BSS +256 B.
- `link_budget.py` — confirm flash and SRAM totals.
- Overlapping step paste on the same track: snapshot uses the 17th pool.
- Overlapping paste started while a Pattern drain is in flight: paste waits,
  then completes correctly; the trace shows the wait duration; the drain's
  file is valid.
- Non-overlapping paste: unchanged (live path, no scratch).
- AutoSave Pattern drain after a copy operation: the 17th region holds the
  snapshot, not stale copy data.
- Power-cycle after paste + AutoSave: Pattern content survives.
- Docs: `COPYCLEAR_UTILITIES.md` (§11.7, §12.5, §17, §18),
  `PATTERN_DYNAMIC_STACK.md`, `STORAGE_SRAM_MANIFEST.md`.

---

## Deferred

### Q6 follow-up — abandon an in-flight drain

Only if the measured gate waits are noticeable. Jump the drain to its close
phase with an error status; the completion callback already re-marks the
Scene dirty. **Required with it:** roll `fs_pattern_generation[scene]` back
by one. The A/B slot is chosen by generation parity, so without the rollback
the retry overwrites the slot holding the only valid copy, and a power loss
during the retry loses the Pattern AutoSave. (The existing I/O-error path has
the same exposure; abandoning would make it routine.)

### Q7 — Bank Load staging

Future work. The ownership contract above already covers it.
