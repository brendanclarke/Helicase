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

Net RAM change: **zero** — the 17th region (10,519 B) replaces the
standalone snapshot (10,519 B) at exactly the same size.

---

## Plan

### Step 1 — Allocate the 17th region

- Extend `pat_regions` from `[SCENE_COUNT]` to `[SCENE_COUNT + 1]`, or
  keep the array at 16 and add a named 17th member (design decision below).
- The 17th region is **not a playable Scene**: `scene_indexValid()`,
  `pat_patternValid()`, the sequencer, the UI, PERF, and Bank all continue
  to bound at `SCENE_COUNT` (16). Only callers that explicitly name the
  background index may use it.
- Remove `pat_autosave_snapshot`.
- Define a constant for the background index (e.g.
  `PAT_BACKGROUND_SCENE_INDEX` or `PAT_SCRATCH_INDEX`).

### Step 2 — Migrate AutoSave snapshot

- `pat_snapshotScene()` copies into `pat_regions[PAT_BACKGROUND_SCENE_INDEX]`
  instead of `pat_autosave_snapshot`.
- `pat_autosaveSnapshot()` returns a pointer to that region.
- The filesystem Pattern drain writer (`filesystem.c`) calls the same
  accessor and is unchanged.
- The copy/clear suspension gate already prevents AutoSave from starting a
  Pattern drain while an operation is active, so the two uses (AutoSave
  snapshot and copy scratch) cannot collide in time.

### Step 3 — Migrate copy/clear snapshot to the 17th region's pool

- `ccSvc_pasteOverlaps()` detection is unchanged.
- When overlap is detected, the engine copies source blocks into the 17th
  region's pool area (8,192 B, all of it available — the swap block
  reservation and allocator bitmap are irrelevant since this is raw scratch
  storage, not an active pool).
- The 256 B lookup table (source step → offset/trigger descriptor) moves
  into the 17th region's address array (1,792 B available, only 256 B
  needed). Or it stays in the name buffer's remap area — design decision
  below.
- The HCNAMES row remap (161 B) stays in the name buffer, since names are
  still written through the filesystem and the remap is needed at
  end-of-operation time, not during the snapshot.
- **The name buffer is no longer borrowed for block data.** It is still
  borrowed lazily for the end-of-operation name write (161 B remap +
  HCNAMES read/overlay). This decouples the pool size from the name buffer
  size.
- Remove the `_Static_assert` that ties `PAT_POOL_ALLOC_CHUNKS` to
  `FS_NAME_SCRATCH_BYTES`.
- Update the scratch layout constants (`CC_SCRATCH_*`) and the spec
  (COPYCLEAR_UTILITIES.md §11.7, §12.5, §17, §18).

### Step 4 — Verify mutual exclusion

The 17th region has three potential users. Verify that no two can overlap:

| User A | User B | Prevented by |
|---|---|---|
| AutoSave Pattern snapshot | Copy/clear snapshot | `copyClear_backgroundSuspended()` prevents AutoSave Pattern drain admission while any copy/clear operation is active |
| AutoSave Pattern snapshot | Bank Load staging | (future) Bank Load would suspend AutoSave, same mechanism |
| Copy/clear snapshot | Bank Load staging | (future) Copy/clear is refused while Load/Save owns the UI (`OP_REFUSED` storage busy); Bank Load holds the UI |

Document these invariants as the 17th region's ownership contract.

### Step 5 — Borrowing policy for the name buffer

After the migration, the name buffer is borrowed only for the HCNAMES remap
(161 B) during the end-of-operation name write, not for block data. Decide
whether the borrow is still needed at all or whether the remap can live
elsewhere (the 17th region has 1,792 B of address array available). If the
borrow is eliminated entirely, the filesystem refusal gate
(`FS_NAME_CACHE_COPYCLEAR`) and the Load/Save entry wait are simplified.

### Step 6 — Build and test

- `make all && make img` — verify zero net RAM change.
- `link_budget.py` — confirm flash and SRAM totals.
- Overlapping step paste on the same track: snapshot uses the 17th pool.
- Non-overlapping paste: unchanged (live path, no scratch).
- AutoSave Pattern drain after a copy operation: the 17th region holds the
  snapshot, not stale copy data.
- Power-cycle after paste + AutoSave: Pattern content survives.

---

## Open Questions and Architectural Decisions

### Q1. Array extension vs. named member

Option A: `pat_regions[SCENE_COUNT + 1]` with `PAT_BACKGROUND_SCENE_INDEX =
SCENE_COUNT`. Simple, uniform access. Risk: every loop that iterates
`pat_regions` must be audited to ensure it stops at `SCENE_COUNT`, not
`SCENE_COUNT + 1`.

Option B: keep `pat_regions[SCENE_COUNT]` and add a separate named
`pat_scene_region_t pat_background_region`. No accidental iteration risk.
Slightly less uniform (two accessors instead of one indexed family).

**Recommendation:** Option B is safer for this codebase, where `SCENE_COUNT`
appears in many loop bounds and the 17th region is semantically different
(never played, never saved, never UI-visible).

### Q2. Copy scratch layout in the 17th region

The snapshot engine currently stores:
- A 128-entry lookup table (`uint16_t[128]`, 256 B) mapping paste index →
  trigger flag + block offset.
- Copied block data (up to 8,060 B today).

In the 17th region, the pool (8,192 B) holds the block data with room to
spare. The lookup table could go in:
- (a) The 17th region's address array (1,792 B, only 256 B needed) — keeps
  everything in one place, no name buffer borrow needed for the snapshot at
  all.
- (b) A small static array (256 B) in `copyClearService.c` — avoids
  reinterpreting the address array as scratch. RAM cost 256 B, needs
  approval.
- (c) The name buffer remap area (still borrowed, but only 256 B + 161 B
  remap) — minimal code change but keeps the coupling.

**Decision needed.** Option (a) is the cleanest if it works: the 17th
region's address array is unused scratch space by definition, and the table
fits easily. The address entries are `uint16_t` and the table entries are
`uint16_t`, so alignment is natural.

### Q3. Name buffer borrow: keep, shrink, or eliminate?

After moving block data to the 17th region, the name buffer borrow serves
only the HCNAMES remap (161 B). Options:
- (a) Keep the borrow for the remap only — minimal change to the name write
  path; the borrow is shorter (no block-data wait).
- (b) Move the remap into the 17th region's address array alongside the
  lookup table (1,792 B available, 256 + 161 = 417 B used) — eliminates the
  borrow entirely.
- (c) Move the remap into a small static array (161 B) — eliminates the
  borrow, tiny RAM cost.

Eliminating the borrow removes the `FS_NAME_CACHE_COPYCLEAR` tag, the
filesystem refusal gate for non-copy-clear operations while lent, and the
Load/Save entry wait after a long register drain. Whether that simplification
is worth the change is a session decision.

### Q4. Snapshot-time vs. queue-time source capture

The S076 P4 investigation found that multiple queued pastes to the same track
execute sequentially, and a later paste reads source data as modified by
earlier pastes. This is documented and accepted (spec §18, "No data
clipboard"). The 17th region does not change this: it is scratch for one
paste at a time, not a persistent clipboard. Confirm this is still the
accepted behaviour.

### Q5. Future pool growth validation

After the migration, `PAT_STACK_SIZE` can grow to 512 (16,384 B pool)
without any copy/clear constraint — the 17th region's pool grows with it
automatically. Verify that the only remaining static assert on pool size is
the `pat_scene_region_t` geometry check (which is structural, not a scratch
constraint).

### Q6. Mutual exclusion: is the suspension gate strong enough?

The AutoSave ↔ copy/clear exclusion relies on
`copyClear_backgroundSuspended()`, which prevents the AutoSave scheduler
from **starting** a Pattern drain. Verify that:
- A Pattern drain already in flight when a copy operation starts has
  **finished** before the copy engine's snapshot phase begins (the
  suspension starts at the first object press, and the Pattern drain is
  bounded).
- The copy engine's snapshot into the 17th region does not race the tail end
  of a Pattern drain reading from the same 17th region.

This should already be true (the suspension gate prevents new drains, and
the copy engine waits for the exclusive boundary which also waits for
Pattern Stack Service work to drain), but the exact sequencing needs to be
verified against the code before implementation.

### Q7. Bank Load staging (future, not this session)

The 17th region is planned to hold the playing Scene's Pattern during Bank
Load. This session should not implement that, but the ownership contract
written in Step 4 must be compatible with it. The key constraint: Bank Load
holds `menu_storageBusy` and copy/clear is refused while that flag is set,
so the two cannot collide. Confirm this is sufficient and document it.
