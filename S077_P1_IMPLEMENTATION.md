# S077 P1 — Implementation Schedule

Full code-level implementation plan for the 17th background Scene region and
copy/clear snapshot migration.  Derived from `S077_P1_BG_SCENE_COPY_SELF_REPLACE.md`
and a deep-dive audit of every call site, data layout, and static assert.

---

## Change Legend

| Tag | Meaning |
|-----|---------|
| **ADD** | New code (function, variable, define, static assert, declaration) |
| **REMOVE** | Delete existing code |
| **MODIFY** | Edit existing code in place |

---

## Step 1 — Allocate the 17th Region (PatternData)

### 1A. Replace the standalone snapshot with `pat_background_region`

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 1A-1 | 59–69 | **MODIFY** | Replace the `pat_autosave_snapshot` variable and its comment block with `pat_background_region`. |

**Old (lines 59–69):**
```c
/*
 * Pattern AutoSave snapshot staging buffer.
 *
 * What: one standalone pat_scene_region_t separate from pat_regions[]. Why:
 * the background writer snapshots one Scene in the main loop, then streams
 * it over many filesystem ticks without reading data that recording/erasing
 * may later change. SRAM cost: 10,519 bytes in SRAM1 .bss. Lifetime: static;
 * written by pat_snapshotScene() and read by pat_autosaveSnapshot(). Owner:
 * PatternData.c exclusively. Affiliate: filesystem.c Pattern drain writer.
 */
static pat_scene_region_t pat_autosave_snapshot;
```

**New:**
```c
/*
 * Background Scene region (S077).
 *
 * What: one additional pat_scene_region_t outside pat_regions[SCENE_COUNT].
 * Not a playable Scene: scene_indexValid(), pat_patternValid(), the sequencer,
 * the UI, PERF and Bank all continue to bound at SCENE_COUNT (16). It is
 * reached only through its named accessors. Why: serves three purposes that
 * must never overlap with live Scenes — (1) AutoSave snapshot staging for the
 * Pattern drain writer, (2) copy/clear scratch pool for overlapping pastes
 * that would otherwise need to borrow the 9 kB name buffer for block data,
 * and (3) future Bank Load staging. SRAM cost: 10,519 bytes in SRAM1 .bss
 * (same footprint as the removed pat_autosave_snapshot; net zero). Lifetime:
 * static. Owner: PatternData.c exclusively. Accessors: pat_snapshotScene(),
 * pat_autosaveSnapshot(), pat_backgroundPoolMut(). Affiliates: filesystem.c
 * Pattern drain writer, copyClearService.c overlapping paste engine.
 *
 * Ownership contract:
 *   User A                                 | User B                  | Prevented by
 *   AutoSave Pattern drain (reads ph 2–6)  | Copy/clear snapshot     | New drains: copyClear_backgroundSuspended().
 *                                          |                         | In-flight drain: filesystem_patternSnapshotInUse() gate.
 *   AutoSave Pattern drain                 | Bank Load staging       | (future) Bank Load suspends AutoSave.
 *   Copy/clear snapshot                    | Bank Load staging       | (future) copy/clear refused while menu_storageBusy.
 *   Boot reader rollback copy              | any                     | Boot only; runs before copy/clear or AutoSave can start.
 */
static pat_scene_region_t pat_background_region;
```

### 1B. Update `pat_snapshotScene()` to use the new region

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 1B-1 | 88–102 | **MODIFY** | Update the comment block and memcpy target. |

**Old (lines 96–102):**
```c
void pat_snapshotScene(uint8_t scene_index)
{
    if (!scene_indexValid(scene_index))
        return;
    memcpy(&pat_autosave_snapshot, &pat_regions[scene_index],
           sizeof(pat_scene_region_t));
}
```

**New:**
```c
void pat_snapshotScene(uint8_t scene_index)
{
    if (!scene_indexValid(scene_index))
        return;
    memcpy(&pat_background_region, &pat_regions[scene_index],
           sizeof(pat_scene_region_t));
}
```

Update the function's preceding comment block (lines 88–95) to reference
`pat_background_region` instead of `pat_autosave_snapshot` and to note that the
copy/clear paste engine must check `filesystem_patternSnapshotInUse()` before
calling this function, since the drain may still be reading the same buffer.

### 1C. Update `pat_autosaveSnapshot()` to use the new region

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 1C-1 | 104–114 | **MODIFY** | Update the comment and return target. |

**Old (lines 111–114):**
```c
const pat_scene_region_t *pat_autosaveSnapshot(void)
{
    return &pat_autosave_snapshot;
}
```

**New:**
```c
const pat_scene_region_t *pat_autosaveSnapshot(void)
{
    return &pat_background_region;
}
```

Update the preceding comment (lines 104–110) to reference `pat_background_region`.

### 1D. Add `pat_backgroundPoolMut()` accessor

**File:** `Core/Bank/Scene/Pattern/PatternData.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 1D-1 | after 114 | **ADD** | New public function. |

```c
/*
 * Mutable pointer to the background region's pool bytes.
 *
 * What: returns the raw pool array of pat_background_region for use as
 * copy/clear scratch storage during an overlapping paste. The pool is exactly
 * PAT_STACK_SIZE * 32 bytes (8,192 B today), the same size as every live
 * Scene pool. Why: the overlapping paste formerly borrowed the 9 kB name
 * buffer for block data; this accessor removes that dependency and scales
 * automatically with PAT_STACK_SIZE. The caller writes retargeted source
 * blocks contiguously from byte 0. The swap reservation and bitmap within
 * the background region are irrelevant for raw scratch; only the pool bytes
 * are used. Inputs: none. Outputs: a non-NULL mutable pointer valid for the
 * static lifetime. The caller must ensure no concurrent reader (see the
 * filesystem_patternSnapshotInUse() gate). Affiliates: copyClearService.c
 * overlapping paste engine, pat_snapshotScene(), pat_autosaveSnapshot().
 */
uint8_t *pat_backgroundPoolMut(void)
{
    return pat_background_region.pool;
}
```

### 1E. Declare `pat_backgroundPoolMut()` in the header

**File:** `Core/Bank/Scene/Pattern/PatternData.h`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 1E-1 | after 427 | **ADD** | Public declaration. |

After the `pat_autosaveSnapshot()` declaration (line 427), add:

```c
/*
 * Mutable pointer to the background region's pool (S077).
 *
 * What: the raw PAT_STACK_SIZE * 32 pool bytes of the background region,
 * for use as copy/clear scratch storage during an overlapping paste. Why:
 * replaces the 9 kB name-buffer borrow for block data; the pool is always
 * large enough (same type as the source pool). The caller must ensure no
 * concurrent snapshot reader (filesystem_patternSnapshotInUse() gate).
 * Inputs: none. Outputs: non-NULL mutable pointer (static lifetime).
 * Affiliates: copyClearService.c, pat_snapshotScene(), pat_autosaveSnapshot().
 */
uint8_t *pat_backgroundPoolMut(void);
```

---

## Step 2 — Migrate AutoSave Snapshot (no code change beyond Step 1)

Steps 1B and 1C already migrate the snapshot.  The filesystem Pattern drain
writer calls `pat_autosaveSnapshot()` (unchanged API) and receives a pointer
into `pat_background_region` instead of the removed `pat_autosave_snapshot`.
The `pat_snapshotScene()` callers (filesystem.c semantic/non-semantic drain
schedulers at lines 24616 and 24961, and the boot reader at line 28649) are
unchanged.

The boot reader at filesystem.c:28649–28655 uses `pat_snapshotScene()` as a
rollback copy and then `pat_autosaveSnapshot()` to restore on failure — both
now operate on `pat_background_region`.  Boot runs before copy/clear or
AutoSave can start, so there is no contention.

**No file changes in this step.**

---

## Step 3 — Migrate Copy/Clear Snapshot to Background Region Pool

### 3A. Add the static paste source table

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3A-1 | after 95 | **ADD** | Static paste source table. |

After the existing `ccSvc_buf` declaration (line 95), add:

```c
/*
 * Paste source table for overlapping pastes (S077).
 *
 * What: a 128-entry uint16_t array that replaces the cast into the borrowed
 * name buffer at CC_SCRATCH_TABLE_OFFSET. Each entry encodes one source step:
 * bit 15 is the source trigger (CC_SNAP_TRIGGER), bit 14 announces stored
 * block data (CC_SNAP_BLOCK), and bits 11..0 are a four-byte-chunk offset
 * into the background region's pool (CC_SNAP_OFFSET). Why: the table must
 * outlive the name-buffer borrow (which is now shorter) and must accommodate
 * a future PAT_STACK_SIZE of 512 (4,063 allocatable chunks). SRAM cost:
 * 256 bytes in SRAM1 .bss (approved). Lifetime: static; valid from the
 * SNAPSHOT phase of an overlapping paste until the PLACE phase completes.
 * Owner: copyClearService.c exclusively. Affiliates: ccSvc_table(),
 * ccSvc_snapBlock(), ccSvc_runPatternPaste().
 */
static uint16_t ccSvc_snapTable[NUM_STEPS];
```

### 3B. Widen `CC_SNAP_OFFSET` mask

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3B-1 | 354 | **MODIFY** | Widen from 11 bits to 12 bits. |

**Old (line 354):**
```c
#define CC_SNAP_OFFSET   0x07FFu
```

**New:**
```c
#define CC_SNAP_OFFSET   0x0FFFu
```

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3B-2 | after 354 | **ADD** | Static assert for future pool sizes. |

```c
/*
 * Offset field width guard (S077).
 *
 * What: CC_SNAP_OFFSET must represent every allocatable chunk index. Why:
 * at PAT_STACK_SIZE 256 the maximum is 2,015 chunks (fits in 11 bits), but
 * at PAT_STACK_SIZE 512 it is 4,063 chunks and would wrap in the old 11-bit
 * mask. The 12-bit field (0x0FFF, max 4,095) covers both. Bits 12–13 remain
 * free between CC_SNAP_BLOCK (bit 14) and CC_SNAP_OFFSET (bits 11..0).
 * Inputs: PAT_POOL_ALLOC_CHUNKS. Affiliate: config.h pool geometry.
 */
_Static_assert(PAT_POOL_ALLOC_CHUNKS <= CC_SNAP_OFFSET + 1u,
               "CC_SNAP_OFFSET must cover every allocatable chunk");
```

### 3C. Redirect `ccSvc_table()` to the static table

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3C-1 | 356–359 | **MODIFY** | Return static table instead of cast into name buffer. |

**Old (lines 356–359):**
```c
static uint16_t *ccSvc_table(void)
{
    return (uint16_t *)(void *)&ccSvc_buf[CC_SCRATCH_TABLE_OFFSET];
}
```

**New:**
```c
/*
 * Paste source table accessor (S077).
 *
 * What: returns the static 128-entry table that replaced the cast into the
 * name buffer. Why: the table is now independent of the name-buffer borrow;
 * the overlapping paste engine writes it during SNAPSHOT and reads it during
 * PLACE. Inputs: none. Outputs: pointer to ccSvc_snapTable[]. Affiliates:
 * ccSvc_runPatternPaste(), ccSvc_sourceBlock().
 */
static uint16_t *ccSvc_table(void)
{
    return ccSvc_snapTable;
}
```

### 3D. Redirect `ccSvc_snapBlock()` to the background pool

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3D-1 | 362–368 | **MODIFY** | Read from background pool instead of name buffer. |

**Old (lines 362–368):**
```c
static const uint8_t *ccSvc_snapBlock(uint16_t entry)
{
    if ((entry & CC_SNAP_BLOCK) == 0u)
        return 0;
    return &ccSvc_buf[CC_SCRATCH_BLOCK_OFFSET +
                      (uint16_t)(entry & CC_SNAP_OFFSET) * 4u];
}
```

**New:**
```c
/*
 * Snapshot block accessor (S077).
 *
 * What: returns a pointer into the background region's pool where the
 * overlapping-paste SNAPSHOT phase stored one retargeted source block. Why:
 * the pool is the same type and size as every live Scene pool, so it can
 * hold any set of source blocks without the name-buffer size constraint.
 * The offset field (CC_SNAP_OFFSET) is a four-byte-chunk index from pool
 * byte 0. Inputs: one uint16_t table entry. Outputs: const pointer to the
 * block bytes, or NULL when CC_SNAP_BLOCK is not set. Affiliates:
 * ccSvc_sourceBlock(), pat_backgroundPoolMut().
 */
static const uint8_t *ccSvc_snapBlock(uint16_t entry)
{
    if ((entry & CC_SNAP_BLOCK) == 0u)
        return 0;
    return &pat_backgroundPoolMut()[(uint16_t)(entry & CC_SNAP_OFFSET) * 4u];
}
```

Note: this reads from the same pool that was written during SNAPSHOT; the
`pat_backgroundPoolMut()` call is a non-NULL static pointer, no allocation.

### 3E. Redirect SNAPSHOT phase writes to the background pool

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3E-1 | 641–642 | **MODIFY** | Write source blocks to background pool instead of name buffer. |

**Old (lines 641–642 in `ccSvc_runPatternPaste`, case 1, SNAPSHOT):**
```c
                memcpy(&ccSvc_buf[CC_SCRATCH_BLOCK_OFFSET +
                                  (uint16_t)run->aux * 4u], block, bytes);
```

**New:**
```c
                memcpy(&pat_backgroundPoolMut()[(uint16_t)run->aux * 4u],
                       block, bytes);
```

### 3F. Remove `ccSvc_ensureScratch()` call from the overlap path

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3F-1 | 588–591 | **MODIFY** | Remove the name-buffer borrow for overlapping pastes. |

**Old (lines 588–591 in `ccSvc_runPatternPaste`, case 0):**
```c
        if (ccSvc_pasteOverlaps(job, &g)) {
            run->phase = 1u;
            if (!ccSvc_ensureScratch())
                return CC_RUN_WAIT;
        }
```

**New:**
```c
        if (ccSvc_pasteOverlaps(job, &g)) {
            run->phase = 1u;
            if (filesystem_patternSnapshotInUse())
                return CC_RUN_WAIT;
        }
```

The overlapping paste no longer borrows the name buffer for block data. It
waits only for the Pattern drain snapshot gate (Step 4).

### 3G. Remove `CC_SCRATCH_TABLE_OFFSET` and `CC_SCRATCH_BLOCK_OFFSET`

**File:** `Core/Menu/CopyClear/copyClearService.h`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3G-1 | 106–107 | **REMOVE** | Delete the two offset defines. |

**Remove:**
```c
#define CC_SCRATCH_TABLE_OFFSET   256u
#define CC_SCRATCH_BLOCK_OFFSET   512u
```

These are no longer used: the table is a static array and the blocks are
stored in the background pool. `CC_SCRATCH_REMAP_OFFSET` (line 105) stays
because the remap is still in the borrowed name buffer (Step 5).

### 3H. Remove the `_Static_assert` tying pool to name buffer

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3H-1 | 41–43 | **REMOVE** | Delete the block-fits-name-buffer assert. |

**Remove:**
```c
_Static_assert(CC_SCRATCH_BLOCK_OFFSET + PAT_POOL_ALLOC_CHUNKS * 4u <=
                   FS_NAME_SCRATCH_BYTES,
               "copy/clear scratch must fit the name buffer");
```

The remap assert (lines 38–40) stays because the remap is still in the name
buffer.

### 3I. Update `ccSvc_scratch()` layout comment

**File:** `Core/Menu/CopyClear/copyClearService.h`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3I-1 | 91–101 | **MODIFY** | Update the borrowed-buffer comment to reflect the remap-only layout. |

**Old (lines 91–101):**
```c
/*
 * Borrowed 9 kB name buffer (S075, F1-I).
 *
 * What: the filesystem's name cache, lent to copy/clear only when it is used:
 * by a step/bar paste that overlaps its own source (snapshot), and by the
 * name remap and its end-of-operation HCNAMES write. Clears, pot clears,
 * whole-Pattern copy/reset, non-overlapping pastes and Scene-level data
 * commits never wait for it. ccSvc_scratch() returns NULL while not borrowed.
 * Layout (spec §9.3): [0..160] HCNAMES row remap (0xFF = unchanged);
 * [256..511] paste source table; [512..] source blocks. Accessors:
 * copyClearService.c, copyOps.c.
 */
```

**New:**
```c
/*
 * Borrowed 9 kB name buffer (S075 F1-I; S077 reduced scope).
 *
 * What: the filesystem's name cache, lent to copy/clear only for the HCNAMES
 * row remap and the end-of-operation name write. Source block data and the
 * paste source table are no longer stored here (S077: blocks use the
 * background region's pool; the table is a static uint16_t[128]). Clears,
 * pot clears, whole-Pattern copy/reset, non-overlapping pastes and Scene-level
 * data commits never wait for it. ccSvc_scratch() returns NULL while not
 * borrowed. Layout: [0..160] HCNAMES row remap (0xFF = unchanged). Accessors:
 * copyClearService.c, copyOps.c.
 */
```

### 3J. Add `#include "PatternData.h"` if not already present

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3J-1 | 18 | **VERIFY** | Already includes `PatternData.h` at line 18 — no change needed. |

### 3K. Add `#include "filesystem.h"` if not already present

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 3K-1 | 20 | **VERIFY** | Already includes `filesystem.h` at line 20 — no change needed. |

---

## Step 4 — Snapshot Gate and Mutual Exclusion

### 4A. Add `filesystem_patternSnapshotInUse()` function

**File:** `Core/Hardware/SD/filesystem.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 4A-1 | near 25820 (after `filesystem_status()`) | **ADD** | New gate query function. |

```c
/*
 * Pattern drain snapshot-reading gate (S077).
 *
 * What: returns nonzero while the Pattern drain state machine is actively
 * reading the background region snapshot. The drain reads the snapshot in
 * phases 2–6 (header build, address array, bitmap, pool) and does not read
 * it in phases 0–1 (chdir, fopen) or 7+ (CRC, close, HCNAMES update). Why:
 * the overlapping paste copies source blocks into the same background
 * region's pool; it must not overwrite pool bytes while the drain is
 * streaming them to disk. The existing copyClear_backgroundSuspended() gate
 * prevents NEW drains from starting, but an in-flight drain that began
 * before the copy/clear session started may still be reading. This function
 * fills that gap. Both ccSvc_tick() and filesystem_tick() are cooperative
 * main-loop code (called from timebase_serviceFrontPanel()), so there is no
 * interrupt race on the 10.5 kB snapshot copy. Inputs: current_op,
 * op_phase (module-static). Outputs: nonzero while the snapshot is being
 * read. Affiliates: copyClearService.c overlapping-paste path-selection gate,
 * pat_snapshotScene(), pat_autosaveSnapshot().
 */
uint8_t filesystem_patternSnapshotInUse(void)
{
    return (uint8_t)(current_op == FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN &&
                     op_phase >= 2u && op_phase <= 6u);
}
```

### 4B. Declare `filesystem_patternSnapshotInUse()` in the header

**File:** `Core/Hardware/SD/filesystem.h`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 4B-1 | after 378 (after `filesystem_patternAutosaveOnLoad`) | **ADD** | Public declaration. |

```c
/*
 * Pattern drain snapshot-reading gate (S077).
 *
 * What: nonzero while the Pattern drain is reading the background region
 * snapshot (phases 2–6). Why: an overlapping paste must not overwrite pool
 * bytes while the drain streams them. The paste returns CC_RUN_WAIT at
 * path selection while this is true. Inputs: internal drain state. Output:
 * boolean. Affiliates: copyClearService.c, pat_snapshotScene(),
 * filesystem_autosavePatternDrain_tick().
 */
uint8_t filesystem_patternSnapshotInUse(void);
```

### 4C. Add trace event for snapshot gate wait

**File:** `Core/Bank/Scene/AutosaveTrace.h`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 4C-1 | after 532 (after `CC_EVT_SUSPEND`) | **ADD** | New trace event code. |

```c
#define AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE 0x51u
```

### 4D. Add gate-wait trace record in the overlapping paste path

**File:** `Core/Menu/CopyClear/copyClearService.c`

| # | Line | Tag | Change |
|---|------|-----|--------|
| 4D-1 | at the new gate (change 3F-1) | **ADD** | Trace the gate wait. |

Extend the gate code from Step 3F to record the wait:

```c
        if (ccSvc_pasteOverlaps(job, &g)) {
            run->phase = 1u;
            if (filesystem_patternSnapshotInUse()) {
#if DEV_MODE_LOGGING
                CC_TRACE_SAT16(ccSvc_traceState.scratch_wait);
                ccTrace(AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE, 0u);
#endif
                return CC_RUN_WAIT;
            }
        }
```

The `scratch_wait` field is reused for this gate wait (it previously counted
name-buffer wait ticks; the snapshot path no longer waits for the name buffer).

---

## Step 5 — Name Buffer Borrowing (Reduced Scope)

The name-buffer borrow stays for the 161 B remap and the HCNAMES write. The
three existing borrow sites:

1. **`ccSvc_namesReady()` (line 1156–1158)** — unchanged. The executors call
   this before recording name remaps.
2. **`ccSvc_tick()` HCNAMES-write branch (line 1524)** — unchanged. Borrows
   for the name write.
3. **`ccSvc_ensureScratch()` in the overlap path (line 590)** — **removed**
   by Step 3F. The overlapping paste no longer borrows the name buffer.

The existing `FS_NAME_CACHE_COPYCLEAR` gate, the filesystem refusal gate while
lent, and the Load/Save entry wait are unchanged. The borrow is now shorter:
it starts only at the end-of-operation name phase (or when an executor
records names), not at paste path selection.

**Side effect:** an overlapping paste no longer waits for an idle filesystem
facade (scalar AutoSave, trace, settings writes). It waits only for the
Pattern drain snapshot gate (Step 4).

**No additional file changes in this step** — all changes are already covered
by Steps 3F, 3G, 3H, and 3I.

---

## Step 6 — Build, Test and Document

### 6A. Build verification

| # | Command | Expected |
|---|---------|----------|
| 6A-1 | `make all && make img` | Clean build. |
| 6A-2 | `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf` | BSS +256 B (the static `ccSvc_snapTable[128]`; the region is net zero because `pat_background_region` replaces `pat_autosave_snapshot` at identical size). |

### 6B. Documentation updates

**File:** `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md`

| # | Section | Tag | Change |
|---|---------|-----|--------|
| 6B-1 | §11.7 (name buffer loan) | **MODIFY** | Update scratch layout table: remove paste source table and source blocks rows; note that the borrow is now for the 161 B remap and HCNAMES write only. |
| 6B-2 | §12.5 (paste engine) | **MODIFY** | Update sub-phase 0 (PATH/BUFFER) to note that overlapping pastes wait on `filesystem_patternSnapshotInUse()` instead of `ccSvc_ensureScratch()`. |
| 6B-3 | §17 (resources) | **MODIFY** | Add the 256 B static paste source table. Remove the "up to 8,572 of 9,000" name-buffer block-data row. Add the background region pool as the scratch target. |
| 6B-4 | §18 (known limits) | **MODIFY** | Update the "no data clipboard" rationale to note the background pool replaces the name-buffer borrow for block data. |

**File:** `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md`

| # | Section | Tag | Change |
|---|---------|-----|--------|
| 6B-5 | §1 (region/snapshot description) | **MODIFY** | Replace the `pat_autosave_snapshot` paragraph with the `pat_background_region` description and its three purposes. |

**File:** `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`

| # | Section | Tag | Change |
|---|---------|-----|--------|
| 6B-6 | §8.2 (PatternData.c entries) | **MODIFY** | Replace the `pat_autosave_snapshot` row (10,519 B) with `pat_background_region` (10,519 B); update the purpose text. |
| 6B-7 | §8.2 (copyClearService.c entries) | **ADD** | Add `ccSvc_snapTable[128]` (256 B) row. |
| 6B-8 | §5 (SRAM1 totals) | **MODIFY** | Update static BSS total (+256 B) and free SRAM1 (−256 B). |

---

## Summary of All Changed Files

| File | Changes |
|------|---------|
| `Core/Bank/Scene/Pattern/PatternData.c` | 1A-1, 1B-1, 1C-1, 1D-1 |
| `Core/Bank/Scene/Pattern/PatternData.h` | 1E-1 |
| `Core/Menu/CopyClear/copyClearService.c` | 3A-1, 3B-1, 3B-2, 3C-1, 3D-1, 3E-1, 3F-1, 3H-1, 4D-1 |
| `Core/Menu/CopyClear/copyClearService.h` | 3G-1, 3I-1 |
| `Core/Hardware/SD/filesystem.c` | 4A-1 |
| `Core/Hardware/SD/filesystem.h` | 4B-1 |
| `Core/Bank/Scene/AutosaveTrace.h` | 4C-1 |
| `knowledge_files/specification_reference/COPYCLEAR_UTILITIES.md` | 6B-1, 6B-2, 6B-3, 6B-4 |
| `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md` | 6B-5 |
| `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | 6B-6, 6B-7, 6B-8 |

**Total: 10 files, 20 discrete changes.**

---

## RAM Budget

| Item | Region | Bytes | Notes |
|------|--------|-------|-------|
| `pat_background_region` | SRAM1 .bss | 10,519 | Replaces `pat_autosave_snapshot` (10,519 B). Net zero. |
| `ccSvc_snapTable[128]` | SRAM1 .bss | 256 | New static table. Approved +256 B. |
| **Net** | | **+256** | |

---

## Test Plan

| # | Test | Expected |
|---|------|----------|
| T1 | Overlapping step paste on the same track | Snapshot uses the background pool; paste completes correctly. |
| T2 | Overlapping paste started while a Pattern drain is in flight | Paste waits (CC_RUN_WAIT); drain completes; paste then completes correctly. The trace shows the gate wait (event 0x51). |
| T3 | Non-overlapping paste | Unchanged (live path, no scratch, no gate wait). |
| T4 | AutoSave Pattern drain after a copy operation | The background region holds the snapshot, not stale copy data. |
| T5 | Power-cycle after paste + AutoSave | Pattern content survives (PAT4 file valid). |
| T6 | HCNAMES write after paste | Name remap still works (borrowed name buffer, remap-only). |

---

## Progress Log (S077 session notes)

### Step 1 - allocate the 17th region (PatternData) - DONE
- **1A** replaced `pat_autosave_snapshot` with `static pat_scene_region_t
  pat_background_region;` and the new comment block (three purposes plus the
  ownership contract). [`PatternData.c` L59-86]
- **1B/1C** `pat_snapshotScene()` and `pat_autosaveSnapshot()` now copy from
  and return `pat_background_region`.
- **1D/1E** added `uint8_t *pat_backgroundPoolMut(void)` (returns
  `pat_background_region.pool`) and declared it in the header.
- No caller changes were needed: filesystem.c and the boot reader keep using
  the unchanged accessor names (Step 2 confirmed there is no other reference
  to the removed variable anywhere in `Core/`).

### Step 2 - AutoSave snapshot migration - DONE
- No code beyond Step 1. The drain writer's `pat_autosaveSnapshot()` call and
  the boot reader's snapshot/restore pair now point into the background
  region. Boot runs before copy/clear or AutoSave, so there is no contention.

### Step 3 - copy/clear snapshot -> background pool - DONE
- **3A** added `static uint16_t ccSvc_snapTable[NUM_STEPS];` (128 entries,
  256 B SRAM1).
- **3B** widened `CC_SNAP_OFFSET` `0x07FF` -> `0x0FFF` and added
  `_Static_assert(PAT_POOL_ALLOC_CHUNKS <= CC_SNAP_OFFSET + 1u)`.
- **3C/3D** `ccSvc_table()` returns the static table; `ccSvc_snapBlock()`
  indexes `pat_backgroundPoolMut()`.
- **3E** the SNAPSHOT phase writes retargeted blocks into the background pool.
- **3F** overlap path selection waits on `filesystem_patternSnapshotInUse()`
  instead of `ccSvc_ensureScratch()`.
- **3G** removed `CC_SCRATCH_TABLE_OFFSET` and `CC_SCRATCH_BLOCK_OFFSET`.
- **3H** removed the pool-fits-name-buffer assert (see deviation 1).
- **3I** rewrote the `ccSvc_scratch()` comment: the loan is remap-only.
- **3J/3K** `PatternData.h` and `filesystem.h` were already included.

### Step 4 - snapshot gate and mutual exclusion - DONE
- **4A/4B** added `filesystem_patternSnapshotInUse()` in filesystem.c and its
  declaration in filesystem.h: true while `current_op ==
  FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN && op_phase >= 2u && op_phase <= 6u`.
  Verified against the drain: phase 2 builds the header from the snapshot,
  phases 4-6 stream address/bitmap/pool, phases 0-1 and 7+ do not read it.
- **4C** added `AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE 0x51u` (AutosaveTrace.h)
  and the matching decoder name in `tools/decode_devlogs.py`.
- **4D** the gate records one `SNAPSHOT_GATE` trace record per wait episode
  (see deviation 2).
- Confirmed the new-drain side of the contract: `copyClear_backgroundSuspended()`
  is consulted by the background-writer scheduler that admits "both Pattern
  AutoSave drains" (filesystem.c ~L25568), so no drain starts during a
  copy/clear session; the gate covers a drain already in flight.

### Step 5 - name buffer borrowing (reduced scope) - DONE
- The borrow remains for the 161 B remap and the HCNAMES write; the overlap
  path no longer borrows. `ccSvc_namesReady()` (copyOps.c Scene executors) and
  the names-dirty branch in `ccSvc_tick()` are unchanged, and
  `fs_list_cache_name`'s [0..160] remap offsets (0-161) are unchanged.

### Step 6 - build, test and document - DONE (build + docs); hardware pending
- `make all`: clean. New warnings: none. The only warnings are pre-existing
  (unrelated filesystem.c unused functions; `PatternData.c` packed-member
  address at the pre-existing `pat_addrPtr()`).
- `make img`: OK, `build/LXRV2_lxr02.img` (payload 536,456 B).
- `arm-none-eabi-size`: `text=536,040`, `data=416`, `bss=427,008`.
- New symbols: `ccSvc_snapTable` 0x100 (256 B), `pat_background_region`
  0x2917 (10,519 B).
- `link_budget.py`: flash 536,456 / 753,664 (headroom 217,208 B); ITCM 4,168;
  DTCM statics 4,472; FXBUF margin 3,712.
- Docs updated: `COPYCLEAR_UTILITIES.md` §11.7, §12.5, §15.2, §15.3, §17, §18;
  `PATTERN_DYNAMIC_STACK.md` §1; `STORAGE_SRAM_MANIFEST.md` §5, §8.2, §11.

### Deviations / decisions taken during implementation
1. **§3H assert had to be retargeted, not left as written.** The plan kept the
   remap assert referencing `CC_SCRATCH_TABLE_OFFSET`, but change 3G removes
   that define, so the file would not compile. Retargeted it to
   `FS_NAME_SCRATCH_BYTES` with the message "HCNAMES remap must fit the
   borrowed name buffer"; the invariant (the 161-row remap fits the 9,000 B
   loan) is preserved.
2. **§4D trace shape.** The plan's snippet emitted a 0x51 record on every
   gated tick with value 0, which would flood the bounded DEV trace ring
   (2,048 records) during a multi-hundred-millisecond drain and bury the rest
   of the operation. Implemented one record per wait episode carrying the
   accumulated tick count, emitted when the gate clears (reusing the existing
   `scratch_wait` counter, which the snapshot path no longer uses). This is
   what "record the gate wait (ticks)" and test T2 ask for.
3. **Measured `.bss` delta is +264 B, not +256 B.** Only 256 B of new storage
   is declared (`ccSvc_snapTable`; the region is net zero against the removed
   snapshot), so the extra 8 B is section alignment/LTO placement. Recorded
   the measured totals (SRAM1 `.bss` 293,356; SRAM1 total static 296,872;
   free 79,960) in `STORAGE_SRAM_MANIFEST.md` §5 rather than the plan's
   rounded +256.

### Not done here
- **T1-T6 are hardware tests** (SD card + LXR-02). They were not run in this
  session; the trace event 0x51 and the wait behaviour are ready for T2.

---

## Post-Implementation Assessment

Verified all changed files against the plan. Every planned change is present and
correct; nothing was omitted and nothing was added beyond the three documented
deviations. Summary of findings by area:

### 1. PatternData.c / .h — background region (Steps 1–2)

- `pat_autosave_snapshot` is fully replaced by `pat_background_region`
  (same type, same `.bss` footprint). The old name does not appear anywhere in
  the codebase.
- `pat_snapshotScene()` copies into `pat_background_region`; `pat_autosaveSnapshot()`
  returns it. Both are structurally identical to the pre-change code, differing
  only in the target name.
- `pat_backgroundPoolMut()` returns `pat_background_region.pool` — correct: the
  pool field is the raw `uint8_t[PAT_STACK_SIZE * 32]` array, and the accessor
  hands out only the pool, not the address array or bitmap.
- The ownership contract comment (lines 60–84) accurately describes every
  current and future user of the region, the mutual-exclusion mechanisms, and
  the boot-only rollback path.

### 2. copyClearService.c / .h — snap table and pool scratch (Step 3)

- `ccSvc_snapTable[NUM_STEPS]` (line 109, 256 B) replaces the cast into the
  borrowed name buffer. Naturally aligned as a static `uint16_t[]`.
- `CC_SNAP_OFFSET` widened from `0x07FFu` to `0x0FFFu`; `_Static_assert` guards
  that `PAT_POOL_ALLOC_CHUNKS` fits. At `PAT_STACK_SIZE 256` the maximum chunk
  index is 2,015; the 12-bit field holds 4,095, leaving room for a future 512
  pool. Bits 12–13 are free between `CC_SNAP_OFFSET` and `CC_SNAP_BLOCK` (bit 14).
  Correct.
- SNAPSHOT phase (case 1, line 666): block data is `memcpy`'d into
  `pat_backgroundPoolMut()` at `run->aux * 4` — the same four-byte-chunk
  addressing the old path used. The `aux` accumulator advances by `bytes / 4`.
  The `ccSvc_table()` write uses the static table. Both are correct.
- `ccSvc_snapBlock()` reads back from `pat_backgroundPoolMut()` at the stored
  offset. The block accessor in `ccSvc_sourceBlock()` (called from the PLACE
  phase) is unchanged apart from the new table/pool source. Correct.
- `CC_SCRATCH_TABLE_OFFSET` and `CC_SCRATCH_BLOCK_OFFSET` are removed from the
  header. `CC_SCRATCH_REMAP_OFFSET` (0) and the remap layout comment remain.
  The assert protecting the remap-fits-loan invariant was retargeted to
  `FS_NAME_SCRATCH_BYTES` (Deviation 1) — sound, since the define it formerly
  referenced no longer exists.
- The header's `ccSvc_scratch()` comment (lines 91–101) accurately documents the
  reduced scope: remap and HCNAMES only, no block data or table.

### 3. filesystem.c / .h — snapshot gate (Step 4)

- `filesystem_patternSnapshotInUse()` (line 25840): returns nonzero when
  `current_op == FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN && op_phase >= 2 && op_phase <= 6`.
  This matches the drain state machine exactly: phases 2–6 read the snapshot
  (header build, address array write, bitmap write, pool streaming); phases 0–1
  are chdir/fopen; phases 7+ are CRC/close/HCNAMES. The `>= 2` lower bound is
  correct (phase 2 is the first snapshot reader).
- The gate is checked at paste path selection (line 641) before any pool write,
  and the paste returns `CC_RUN_WAIT` while it is true. The gate re-checks on
  every tick until the drain advances past phase 6. This is safe: both
  `ccSvc_tick()` and `filesystem_tick()` are called from the same cooperative
  main-loop path, so no interrupt race on the 10.5 kB region.

### 4. Trace event (Step 4 continued)

- `AUTOSAVE_TRACE_CC_EVT_SNAPSHOT_GATE` (0x51) is defined in `AutosaveTrace.h`
  line 533, fitting in the copy/clear event namespace (0x50 is SUSPEND).
- Deviation 2 (one record per wait episode, not per tick) is the right call.
  The accumulated tick count in `scratch_wait` gives the same diagnostic
  information — how long the paste waited — without consuming a trace slot per
  tick. The `scratch_wait` counter is incremented with `CC_TRACE_SAT16` on every
  gated tick, then emitted and cleared when the gate opens. The counter is
  reused from its prior role (name-buffer scratch wait), which the overlap path
  no longer reaches; no naming conflict.

### 5. Build and RAM

- Build is clean; flash payload 536,456 B.
- Measured BSS delta is +264 B. Only 256 B of new storage is declared
  (`ccSvc_snapTable`). The extra 8 B is alignment padding, which varies with
  LTO placement and is not actionable. Deviation 3 documents the measured
  totals correctly.
- The 10,519 B `pat_background_region` replaces the 10,519 B
  `pat_autosave_snapshot` at net zero — confirmed by the symbol sizes in the
  progress log (0x2917 = 10,519 for both old and new).

### 6. Deviation assessment

All three deviations are sound engineering decisions, not regressions:

1. **Assert retarget** — the plan's original assert would not compile after
   removing its referenced define. The replacement preserves the same invariant
   (remap fits loan) with a define that exists.
2. **Trace shape** — one record per episode is the standard pattern used by
   every other copy/clear wait trace (scratch wait, claim wait, fs-refused
   wait). Emitting per-tick would be an anomaly.
3. **BSS +264 vs +256** — 8 B of alignment overhead on a 427 kB `.bss` section
   is not material. The measured totals are recorded accurately.

### 7. Remaining work

- **T1–T6 hardware tests.** The code is ready; the tests require an LXR-02 with
  SD card. T2 (overlapping paste during an in-flight drain) is the critical
  new-behaviour test; T1 (normal overlapping paste) and T3–T6 are regression
  checks.
- **Q6 follow-up** (abandon an in-flight drain). Deferred; only needed if T2
  shows the gate wait is user-perceptible.

### Verdict

Implementation matches the plan. All changes are correct, complete, and safe to
test on hardware.
