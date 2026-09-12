# Session 064 — Pattern AutoSave Implementation Schedule

**Source**: `S064_DYNAMIC_PATTERN_AUTOSAVE.md` (9-step plan)
**Prerequisite**: Session 063 complete, hardware-verified on `dev-ph4-pattern`

---

## Change Index

| Step | Files | Net SRAM | Summary |
|------|-------|----------|---------|
| 1 | Autosave.h | 0 | HCNAMES 129→145, format version 1→2 |
| 2 | Autosave.h, Autosave.c | +2 | Pattern dirty mask and API |
| 3 | PatternData.h, PatternData.c | +10,519 | Snapshot region and accessor |
| 4 | PatternData.c | 0 | `pat_snapshotScene()` implementation |
| 5 | filesystem.c | ~+48 | Pattern drain state machine |
| 6 | filesystem.c | 0 | Scheduler integration |
| 7 | filesystem.c | 0 | Boot reader |
| 8 | Autosave.c | 0 | Wire `markSceneWithPatternDirty` |
| 9 | filesystem.c, Autosave.c | 0 | HCNAMES pattern row lifecycle |
| X | sequencer.h, sequencer.c | 0 | Prerequisite: extern declarations |

---

## Step X — Prerequisite: Expose Sequencer Record/Erase Flags

The drain guard (Step 6) must read `seq_recordActive` and `seq_eraseActive`
from `filesystem.c`. These are file-scope globals in `sequencer.c` with no
`extern` declarations. The existing pattern (`seq_activePattern` etc. at
sequencer.h:25-27) uses extern.

### X.1 — sequencer.c:109-111 — MODIFY

**Current** (lines 109, 111):
```c
uint8_t seq_recordActive = 0;
uint8_t seq_eraseActive=0;
```

No change to the definitions themselves. They are already non-static file-scope
globals, so extern declarations are sufficient.

### X.2 — sequencer.h — ADD after line 27

Add extern declarations alongside the existing group:

```c
/*
 * Live recording and erasing activity flags.
 *
 * What: nonzero while the front-panel RECORD or ERASE mode is active.
 * Why: the Pattern AutoSave drain scheduler must skip snapshot capture
 * while either flag is set, because seq_tick() writes to pat_regions[]
 * during recording/erasing and a concurrent main-loop memcpy would
 * produce a torn snapshot. Both flags are set exclusively by main-loop
 * button handlers, so they cannot transition true during a main-loop
 * drain check. Inputs/outputs: written by seq_setRecordMode() and
 * seq_setEraseMode(); read by filesystem.c's pattern drain guard.
 * Affiliates: PatternData mutation paths and S064 Pattern AutoSave.
 */
extern uint8_t seq_recordActive;
extern uint8_t seq_eraseActive;
```

**Location**: sequencer.h after line 27 (after `extern uint8_t seq_resetBarOnPatternChange;`)

---

## Step 1 — HCNAMES Expansion in AutoSave Record

Expand the AutoSave parameter record's HCNAMES row count from 129 to 145 to
match the filesystem's `FS_RESIDENT_NAMES_ROW_COUNT`. Bump the format version
so the boot reader rejects old-format files cleanly.

### 1.1 — Autosave.h:37 — MODIFY

**Current**: `#define AUTOSAVE_HEADER_FORMAT_VERSION 1u`
**New**: `#define AUTOSAVE_HEADER_FORMAT_VERSION 2u`

```
/*
 * Why bumped to 2: Session 064 expands HCNAMES from 129 to 145 rows,
 * adding Pattern provenance. This changes payload geometry, mask size,
 * and record size. The boot reader uses version to reject pre-S064
 * records, triggering fresh initial creation. Affiliates:
 * autosave_streamValidationUpdate() header parse and
 * filesystem_autosaveBootReaderBlocking().
 */
```

### 1.2 — Autosave.h:80 — MODIFY

**Current**: `#define AUTOSAVE_HCNAMES_ROW_COUNT 129u`
**New**: `#define AUTOSAVE_HCNAMES_ROW_COUNT 145u`

```
/*
 * What: total logical HCNAMES rows in the AutoSave record, now aligned
 * with filesystem's FS_RESIDENT_NAMES_ROW_COUNT. Why: Session 063
 * expanded the filesystem register to 145 rows (adding 16 Pattern
 * rows); the AutoSave record must carry the same provenance set so
 * boot-reader source comparison works for Pattern AutoSave files.
 * Inputs/outputs: compile-time constant consumed by record format
 * functions, static asserts, and HCNAMES row iterators. Affiliates:
 * autosave_formatInitialChunk(), autosave_initialRecordCrcUpdate(),
 * resident_names array parameters, filesystem.c HCNAMES handling.
 */
```

### 1.3 — Autosave.h after line 90 — ADD

Add `AUTOSAVE_HCNAMES_PATTERN_BASE` constant:

```c
/*
 * HCNAMES Pattern row base for AutoSave provenance.
 *
 * What: row 129 is the first of 16 per-Scene Pattern identity rows,
 * matching filesystem.c's FS_RESIDENT_NAMES_PATTERN_BASE. Why: Pattern
 * AutoSave drain and boot reader must address source/name provenance
 * for each Scene's pattern independently of its Scene/Kit/Instrument
 * rows. Inputs/outputs: compile-time constant; one row per Scene.
 * Affiliates: autosave_markPatternDirty(), pattern drain HCNAMES
 * publication, and boot reader source comparison.
 */
#define AUTOSAVE_HCNAMES_PATTERN_BASE \
    (AUTOSAVE_HCNAMES_INSTRUMENT_BASE + \
     (AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT))
```

### 1.4 — Autosave.h:49-62 — MODIFY derived constants

The mask and payload sizes change because `AUTOSAVE_HCNAMES_ROW_COUNT` flows
through `autosave_formatInitialChunk()` and `autosave_initialRecordCrcUpdate()`
via the `resident_names` array parameter — but the wire payload geometry
(`AUTOSAVE_MASK_BYTES`, `AUTOSAVE_PAYLOAD_BYTES`, `AUTOSAVE_RECORD_BYTES`) is
**not** derived from HCNAMES rows. These constants describe the
Bank+16×Scene payload and are unchanged at 30,848/3,856/34,768.

**No change** to lines 49-62. The HCNAMES rows affect only the identity
arrays passed to format/CRC functions, not the record wire size. The
existing resident_names parameter dimensions in function signatures
(lines 297, 321) change via the macro expansion — no code edit needed.

### 1.5 — Autosave.h:262-264 — MODIFY static assert

**Current**:
```c
_Static_assert(AUTOSAVE_HCNAMES_INSTRUMENT_BASE +
                   (AUTOSAVE_SCENE_COUNT * AUTOSAVE_INSTRUMENTS_PER_KIT) ==
                   AUTOSAVE_HCNAMES_ROW_COUNT,
               "autosave name mapping must consume all HCNAMES rows");
```

This assert enforces that Instruments are the last row group. With Pattern
rows appended, the assert must change:

**New**:
```c
_Static_assert(AUTOSAVE_HCNAMES_PATTERN_BASE +
                   AUTOSAVE_SCENE_COUNT ==
                   AUTOSAVE_HCNAMES_ROW_COUNT,
               "autosave name mapping must consume all HCNAMES rows");
```

### 1.6 — Autosave.h:419 — MODIFY objectFullyCaptured range check

**Current docstring** (line 419): `Input: HCNAMES row 0..128.`
**New docstring**: `Input: HCNAMES row 0..144.`

The function body in Autosave.c must also be updated — see step 1.8.

### 1.7 — Autosave.h:455 — MODIFY markSourceDirty range check

**Current docstring** (line 455): `Input: HCNAMES row 0..128.`
**New docstring**: `Input: HCNAMES row 0..144.`

### 1.8 — Autosave.c: autosave_objectFullyCaptured() — MODIFY

At Autosave.c:1805, the function maps HCNAMES rows to payload scope intervals.
Currently handles rows 0..128 (Bank, Scene, Kit, Instrument). Must add
Pattern row handling (rows 129..144).

Pattern rows have **no payload scope** in the parameter record — Pattern
data is persisted separately. For Pattern HCNAMES rows, `objectFullyCaptured()`
should return 1 (always "captured" from the parameter record perspective),
since Pattern dirty tracking is independent.

**Add** after the Instrument handler, before the function's final return:

```c
    /* Pattern HCNAMES rows (129..144) have no payload in the scalar
     * parameter record — Pattern data is persisted separately through
     * per-Scene Pattern AutoSave files. Report these rows as always
     * captured so the parameter drain's HCNAMES refreshed-flag logic
     * does not block on non-existent payload bits. */
    if (hcnames_row >= AUTOSAVE_HCNAMES_PATTERN_BASE &&
        hcnames_row < AUTOSAVE_HCNAMES_ROW_COUNT)
        return 1u;
```

### 1.9 — Autosave.c: autosave_markSourceDirty() — MODIFY

Pattern rows have no source field in the parameter record payload. The
function must accept rows 129..144 as no-ops (like Bank row 0) rather than
indexing out of bounds.

**Current guard** (approximate): checks `hcnames_row >= AUTOSAVE_HCNAMES_ROW_COUNT`.
**Add** an explicit Pattern-row guard:

```c
    /* Pattern HCNAMES rows carry their source provenance in the Pattern
     * AutoSave file header, not in the scalar parameter record. Marking
     * them dirty in the parameter mask is a no-op. */
    if (hcnames_row >= AUTOSAVE_HCNAMES_PATTERN_BASE)
        return;
```

### 1.10 — filesystem.c:138 — MODIFY comment

**Current** (line 138): `AutoSave's independent wire image remains 129 rows for S063.`
**New**: `AutoSave's independent wire image is now also 145 rows (S064).`

### 1.11 — config.h — no change

No new constants needed in config.h for Step 1.

---

## Step 2 — Pattern Dirty Mask

Add a 16-bit bitmask in Autosave.c tracking which Scenes have pending
Pattern changes. This is separate from the per-byte parameter record mask.

### 2.1 — Autosave.c after line 77 — ADD

```c
/*
 * Per-Scene Pattern dirty register.
 *
 * What: one bit per resident Scene, set when any Pattern mutation occurs
 * for that Scene. Why: Pattern data is not in the scalar parameter
 * record, so the per-byte autosave_dirty_mask[] cannot track it. The
 * Pattern AutoSave drain in filesystem.c reads this mask to decide
 * which Scenes need a new .patNNx file written. Clearing happens
 * per-Scene after a successful drain write. Inputs: set by
 * autosave_markPatternDirty() from any mutation path; cleared by
 * autosave_clearPatternDirty() after drain. Outputs: read by
 * autosave_patternDirtyMask(). Volatile because ISR-reachable
 * mutation paths (MIDI record) may set bits concurrently with main-loop
 * reads — however, the drain guard ensures no ISR writes during the
 * actual snapshot. Lifetime: static BSS, cleared at processor reset.
 * Affiliates: filesystem.c pattern drain scheduler, Autosave.h API.
 */
static volatile uint16_t autosave_pattern_dirty_mask;
```

### 2.2 — Autosave.c — ADD three functions (after the dirty mask declaration)

```c
/*
 * Mark one Scene's Pattern dirty for AutoSave drain.
 *
 * Input: scene_index 0..15. Output: the corresponding bit is atomically
 * set in the pattern dirty mask when mutation tracking is enabled. Why:
 * every Pattern mutation path must funnel through here so the drain
 * scheduler knows which Scenes to write. The atomic OR uses the same
 * PRIMASK save/restore as the parameter mask. Affiliates:
 * bank_invalidateSdCleanScene(), autosave_markSceneWithPatternDirty().
 */
void autosave_markPatternDirty(uint8_t scene_index)
{
    uint32_t primask;

    if (!autosave_mutation_tracking_enabled || scene_index >= SCENE_COUNT)
        return;
    primask = autosave_irqSave();
    autosave_pattern_dirty_mask |= (uint16_t)(1u << scene_index);
    autosave_irqRestore(primask);
}

/*
 * Read the full 16-bit Pattern dirty mask.
 *
 * Input: none. Output: current mask value (each set bit = one Scene
 * needs a Pattern drain). Why: filesystem.c's drain scheduler polls
 * this to decide whether to start a Pattern drain cycle. No bits are
 * cleared; the scheduler uses autosave_clearPatternDirty() per Scene
 * after a successful write. Affiliates: pattern drain scheduler.
 */
uint16_t autosave_patternDirtyMask(void)
{
    return autosave_pattern_dirty_mask;
}

/*
 * Clear one Scene's Pattern dirty bit after a successful drain write.
 *
 * Input: scene_index 0..15. Output: the corresponding bit is atomically
 * cleared. Why: the drain has written a valid .patNNx file for this
 * Scene, so it no longer needs draining. Atomic clear preserves any
 * concurrent re-dirty from a later mutation during the drain's file
 * close. Affiliates: filesystem.c pattern drain completion path.
 */
void autosave_clearPatternDirty(uint8_t scene_index)
{
    uint32_t primask;

    if (scene_index >= SCENE_COUNT)
        return;
    primask = autosave_irqSave();
    autosave_pattern_dirty_mask &= (uint16_t)~(1u << scene_index);
    autosave_irqRestore(primask);
}
```

### 2.3 — Autosave.c: autosave_discardDirtyMask() — MODIFY

Find the existing function and add pattern mask clearing:

```c
    autosave_pattern_dirty_mask = 0u;
```

Add at the end of the function body, alongside the existing parameter mask
clear loop.

### 2.4 — Autosave.h — ADD declarations after line 509

After `autosave_markResidentBankDirty()` declaration:

```c
/*
 * Pattern-specific dirty tracking API.
 *
 * What: set, read, and clear the per-Scene Pattern dirty mask. Why:
 * Pattern data lives outside the scalar parameter record and needs its
 * own dirty register. The mask is separate from the 3,856-byte per-byte
 * parameter mask. Inputs/outputs: scene_index 0..15 for set/clear;
 * full 16-bit mask for read. Affiliates: filesystem.c pattern drain
 * scheduler, autosave_markSceneWithPatternDirty().
 */
void autosave_markPatternDirty(uint8_t scene_index);
uint16_t autosave_patternDirtyMask(void);
void autosave_clearPatternDirty(uint8_t scene_index);
```

---

## Step 3 — Snapshot Region

Allocate a standalone `pat_scene_region_t` in PatternData.c for coherent
drain snapshot capture.

### 3.1 — PatternData.c after line 48 — ADD

After `static pat_scene_region_t pat_regions[SCENE_COUNT];`:

```c
/*
 * Pattern AutoSave snapshot staging buffer.
 *
 * What: one standalone pat_scene_region_t separate from pat_regions[].
 * Why: the drain writer copies a live Scene region here in the main
 * loop, then streams the file from this snapshot across multiple
 * scheduler ticks without blocking main-loop pattern edits or risking
 * ISR-concurrent writes. This is deliberately not a 17th entry in
 * pat_regions[] to keep SCENE_COUNT at 16 and avoid off-by-one risk
 * in all index-bounded loops. SRAM cost: 10,519 bytes in SRAM1 .bss.
 * Lifetime: static, written by pat_snapshotScene(), read by
 * pat_autosaveSnapshot(). Owner: PatternData.c exclusively.
 * Affiliates: filesystem.c pattern drain writer.
 */
static pat_scene_region_t pat_autosave_snapshot;
```

### 3.2 — PatternData.h after line 213 (before `#endif`) — ADD

```c
/*
 * Pattern AutoSave snapshot operations.
 *
 * pat_snapshotScene(): copies one live resident Scene region into the
 * internal snapshot buffer. Input: scene_index 0..15. Output: the
 * snapshot buffer contains a coherent copy. The caller (filesystem.c
 * drain scheduler) must ensure seq_recordActive and seq_eraseActive
 * are both false before calling — no ISR masking is performed.
 *
 * pat_autosaveSnapshot(): returns a const pointer to the snapshot
 * buffer. Input: none. Output: pointer valid until the next
 * pat_snapshotScene() call. The drain writer streams the file from
 * this pointer. Affiliates: filesystem.c pattern drain state machine.
 */
void pat_snapshotScene(uint8_t scene_index);
const pat_scene_region_t *pat_autosaveSnapshot(void);
```

---

## Step 4 — Snapshot Copy Implementation

### 4.1 — PatternData.c — ADD (after pat_autosave_snapshot declaration)

```c
void pat_snapshotScene(uint8_t scene_index)
{
    if (!scene_indexValid(scene_index))
        return;
    memcpy(&pat_autosave_snapshot, &pat_regions[scene_index],
           sizeof(pat_scene_region_t));
}

const pat_scene_region_t *pat_autosaveSnapshot(void)
{
    return &pat_autosave_snapshot;
}
```

No BASEPRI mask. The drain scheduler (Step 6) guards the call behind
`!seq_recordActive && !seq_eraseActive`. Duration: ~25-40 µs for 10,519
bytes SRAM1→SRAM1 at M7 speeds.

---

## Step 5 — Pattern AutoSave File Writer (State Machine)

### 5.1 — filesystem.c:~270 (fs_internal_op_t enum) — ADD

After `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` (line 270):

```c
    /*
     * Runtime, background-only per-Scene Pattern drain.
     *
     * What: writes one complete v4 PAT4 image for a single dirty Scene
     * into its alternating .patNNa/.patNNb ping-pong file. Why: Pattern
     * data is too large for the scalar parameter record (10,519 bytes
     * per Scene × 16 = 168 KB) and must be persisted separately. The
     * state machine snapshots the Scene region, builds and streams the
     * v4 header, address array, bitmap, and pool, writes the CRC, and
     * closes the file. One Scene per facade admission. Inputs: a
     * scene index from the pattern drain scheduler and the snapshot
     * buffer. Outputs: one valid .patNNx file with incremented
     * generation. Affiliates: pat_snapshotScene(), Autosave.c pattern
     * dirty mask, and the pattern drain scheduler.
     */
    FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN,
```

### 5.2 — filesystem.c — ADD static state variables (near line 1940-1984)

After the existing parameter drain statics:

```c
/*
 * Pattern AutoSave drain retained state.
 *
 * What: the scene index, current generation per Scene, and A/B file
 * selection for the pattern drain state machine. Why: the drain must
 * alternate between .patNNa and .patNNb files and increment the
 * generation counter. The generation array persists across drain cycles
 * so the next drain for the same Scene knows which file to target.
 * Inputs: set by the boot reader (from existing file generations) or
 * initialized to zero. Outputs: consumed by the drain state machine
 * for file selection and header generation field. Lifetime: static BSS,
 * valid after boot reader completes. Owner: filesystem.c exclusively.
 * SRAM cost: 16 × 4 = 64 bytes for generations + 2 bytes for scene
 * index and current file selector = ~66 bytes. Reduced to ~48 bytes
 * with uint16_t generations (adequate for alternating A/B).
 */
static uint32_t fs_pattern_generation[SCENE_COUNT];
static uint8_t  fs_pattern_drain_scene;
```

### 5.3 — filesystem.c — ADD pattern drain tick function

New function `filesystem_autosavePatternDrain_tick()`. This is modeled on
the existing `filesystem_saveSceneDirectory_tick()` pattern write flow
(phases 30-82) but simplified because:
- No directory navigation (root file)
- No Scene hierarchy (single file)
- No HCNAMES multi-row update (one row)
- Source is snapshot buffer, not live region

**State machine phases**:
1. **Phase 0**: Open `.patNNx` for write (compute filename from scene
   index and generation parity: generation even → 'a', odd → 'b')
2. **Phase 1**: Wait for file open, build v4 header from snapshot,
   compute header CRC, begin streaming header
3. **Phase 2**: Stream header bytes (160 B, using staging_buf)
4. **Phase 3**: Stream address array (1,792 B), bitmap (512 B), pool
   (8,192 B) from snapshot — reuse the three-section streaming pattern
   from the existing pattern save writer (phase 32 pattern)
5. **Phase 4**: Seek to CRC offset, write finalized CRC (4 bytes)
6. **Phase 5**: Close file
7. **Phase 6**: Clear dirty bit, update HCNAMES pattern row, ack facade

The function reuses existing helpers:
- `filesystem_patternBuildHeader()` (line 2764)
- `filesystem_patternCrcFeed()` (line 2742)
- `autosave_recordCrcBegin()` / `autosave_recordCrcFinish()`
- `afatfs_fopen()` / `afatfs_fwrite()` / `afatfs_fseek()` / `afatfs_fclose()`

**File naming helper** (inline or static):

```c
/*
 * Build the 8.3 hidden filename for one Pattern AutoSave file.
 *
 * What: writes ".patNNx" into an 8-byte buffer where NN is the
 * zero-padded Scene index and x is 'a' or 'b' based on generation
 * parity. Why: each Scene has two ping-pong files; the generation
 * counter determines which is the next write target. Inputs:
 * scene_index 0..15, generation counter. Output: null-terminated
 * filename in dst. Affiliates: pattern drain open and boot reader.
 */
static void filesystem_patternAutosaveFilename(
    char dst[8], uint8_t scene_index, uint32_t generation)
{
    dst[0] = '.';
    dst[1] = 'p';
    dst[2] = 'a';
    dst[3] = 't';
    dst[4] = (char)('0' + (scene_index / 10u));
    dst[5] = (char)('0' + (scene_index % 10u));
    dst[6] = (generation & 1u) ? 'b' : 'a';
    dst[7] = '\0';
}
```

**Generation management**:
- `fs_pattern_generation[scene]` starts at 0 after boot with no valid
  pattern autosave files.
- Boot reader (Step 7) populates it from the winning file's generation.
- Each drain increments: `fs_pattern_generation[scene]++` before opening
  the target file.
- Generation 0 means "library save / never autosaved". AutoSave starts
  at generation 1. Even generation writes to 'a', odd to 'b'.

**Completion callback**:

```c
/*
 * Release the pattern drain's facade ownership.
 *
 * What: clears the pattern dirty bit for the drained Scene, updates
 * the HCNAMES pattern row refreshed flag, and acks the facade.
 * Inputs: terminal drain status. Outputs: on success, the Scene's
 * dirty bit is cleared and HCNAMES updated; on error, the dirty bit
 * stays set for retry next cycle. Why: a failed write must not clear
 * the dirty bit, so the Scene is retried. Affiliates: pattern drain
 * scheduler and HCNAMES lifecycle.
 */
static void filesystem_autosavePatternDrainCompleted(void);
```

### 5.4 — filesystem.c dispatch table (~line 23975-23980) — ADD

After the `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` case:

```c
    case FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN:
        filesystem_autosavePatternDrain_tick();
        break;
```

---

## Step 6 — Pattern Drain Scheduler Integration

### 6.1 — filesystem.c — ADD forward declaration (near line 1433-1435)

```c
static void filesystem_autosavePatternDrainSchedule_tick(void);
```

### 6.2 — filesystem.c — ADD scheduler function

After `filesystem_autosaveWriterSchedule_tick()` (after line 23495):

```c
/*
 * Schedule one per-Scene Pattern drain when the facade is idle.
 *
 * What: checks autosave_patternDirtyMask(), picks the lowest dirty
 * Scene, verifies seq_recordActive and seq_eraseActive are both false,
 * snapshots the Scene, and starts the pattern drain state machine.
 * Why: Pattern drain runs at lower priority than parameter drain and
 * trace flush — it only claims the facade when both have declined.
 * One Scene per cycle keeps foreground latency low; multiple dirty
 * Scenes drain across successive cycles in ascending order.
 *
 * Inputs: autosave enabled, boot ready, idle facade, no active
 * recording/erasing. Outputs: at most one FS_INTERNAL_OP_AUTOSAVE_
 * PATTERN_DRAIN start per tick. Affiliates: Autosave.c dirty mask,
 * pat_snapshotScene(), sequencer flags, and the drain state machine.
 */
static void filesystem_autosavePatternDrainSchedule_tick(void)
{
    uint16_t mask;
    uint8_t scene;

    if (!fs_autosave_enabled || !fs_autosave_writer_boot_ready)
        return;
    if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
        return;
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;

    mask = autosave_patternDirtyMask();
    if (mask == 0u)
        return;

    /* Pick lowest dirty Scene. */
    for (scene = 0u; scene < SCENE_COUNT; scene++) {
        if (mask & (uint16_t)(1u << scene))
            break;
    }
    if (scene >= SCENE_COUNT)
        return;

    /* Drain safety: skip while recording or erasing is active. */
    if (seq_recordActive || seq_eraseActive)
        return;

    /* Snapshot the Scene into the staging buffer. */
    pat_snapshotScene(scene);
    fs_pattern_drain_scene = scene;

    /* Increment generation and start the drain. */
    fs_pattern_generation[scene]++;

    if (filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN,
                         FS_FILE_SETTINGS, 0u,
                         filesystem_autosavePatternDrainCompleted)) {
        /* started */
    }
}
```

### 6.3 — filesystem.c: filesystem_tick idle scheduler (~line 23943-23944) — ADD

After the parameter drain scheduler call:

```c
    if (status == FS_STATUS_IDLE)
        filesystem_autosavePatternDrainSchedule_tick();
```

This goes **after** `filesystem_autosaveWriterSchedule_tick()` so that
parameter drain has priority over pattern drain. The scheduling order is:

1. Settings persistence (existing)
2. Trace flush (existing, DEV_MODE_LOGGING only)
3. Parameter drain (existing)
4. **Pattern drain (new)**

### 6.4 — filesystem.c — ADD sequencer.h include

filesystem.c must `#include "sequencer.h"` for `seq_recordActive` and
`seq_eraseActive`. Add to the include block near the top of filesystem.c.

Verify no circular dependency: sequencer.h includes PatternData.h and
globals.h. filesystem.c already includes PatternData.h. No conflict.

### 6.5 — filesystem.c — ADD PatternData.h include

filesystem.c must `#include "PatternData.h"` for `pat_snapshotScene()`
and `pat_autosaveSnapshot()`. Check if already included.

Verify: search existing includes at top of filesystem.c.

---

## Step 7 — Pattern AutoSave Boot Reader

### 7.1 — filesystem.c — ADD boot reader function

After `filesystem_autosaveBootReaderBlocking()` (line 27101+):

```c
/*
 * Boot-blocking Pattern AutoSave reader.
 *
 * What: for each present Scene, attempts to read .patNNa and .patNNb,
 * validates each file's v4 header and CRC, picks the valid file with
 * the highest generation (A wins ties), and streams the winner into
 * pat_sceneRegionMut(scene). Why: Pattern AutoSave files persist
 * pattern edits between power cycles; this reader restores them at
 * boot before the user can interact with patterns.
 *
 * Ordering: runs AFTER the parameter record boot reader has restored
 * Bank/Scene/Kit state AND after the Scene directory Pattern files
 * have been read. The AutoSave Pattern file wins over the Scene
 * directory Pattern file when:
 *   1. The AutoSave file has generation > 0 (library saves write 0)
 *   2. The HCNAMES Pattern row source matches
 * Otherwise the Scene directory version is authoritative.
 *
 * Inputs: fs_boot_winner.valid, HCNAMES mirror populated,
 * pat_regions[] already initialized from Scene directory or defaults.
 * Outputs: pat_regions[] updated for Scenes with valid AutoSave
 * files; fs_pattern_generation[] populated for the drain writer.
 * Affiliates: filesystem_autosaveBootReaderBlocking(), pat_initScene(),
 * pat_sceneRegionMut(), the v4 header validators.
 *
 * SRAM: uses staging_buf for header reads and CRC validation.
 * No additional static SRAM allocation.
 */
void filesystem_patternAutosaveBootReaderBlocking(void);
```

This function:
1. Iterates over Scenes 0..15
2. For each Scene, tries to open `.patNNa` and `.patNNb`
3. Validates each with `filesystem_patternHeaderValid()` + full CRC check
4. Picks the winner (highest generation, A wins ties on equal generation)
5. Checks HCNAMES Pattern row source — if source is `@` (AutoSave) and
   generation > 0, reads the file into `pat_sceneRegionMut(scene)`
6. Populates `fs_pattern_generation[scene]` from the winner's generation
7. If neither file is valid, leaves the Scene at its current state (Scene
   directory load or `pat_initScene()` defaults)

### 7.2 — filesystem.h — ADD declaration

```c
/*
 * Boot-blocking Pattern AutoSave reader.
 *
 * What: restores per-Scene Pattern data from .patNNa/.patNNb ping-pong
 * files at boot. Must be called after the parameter record boot reader
 * and Scene directory Pattern files have been read. Inputs: mounted
 * filesystem, HCNAMES populated. Outputs: pat_regions[] updated for
 * Scenes with valid AutoSave pattern files. Affiliates:
 * filesystem_autosaveBootReaderBlocking(), main.c boot sequence.
 */
void filesystem_patternAutosaveBootReaderBlocking(void);
```

### 7.3 — main.c boot sequence — ADD call

After the existing `filesystem_autosaveBootReaderBlocking()` call (and
after Scene directory Pattern loads), add:

```c
    filesystem_patternAutosaveBootReaderBlocking();
```

Verify exact location in main.c during implementation.

---

## Step 8 — Wire `autosave_markSceneWithPatternDirty`

### 8.1 — Autosave.c:1722-1734 — MODIFY

**Current**:
```c
void autosave_markSceneWithPatternDirty(uint8_t scene_index)
{
    autosave_markSceneWithoutPatternDirty(scene_index);
    /* TODO: mark Pattern only after Pattern persistence has a defined owner. */
}
```

**New**:
```c
void autosave_markSceneWithPatternDirty(uint8_t scene_index)
{
    autosave_markSceneWithoutPatternDirty(scene_index);
    autosave_markPatternDirty(scene_index);
}
```

Remove the TODO comment — Pattern persistence now has a defined owner.

### 8.2 — Autosave.c:1736-1759 — MODIFY `autosave_markResidentBankDirty()`

**Current** (line 1757):
```c
            autosave_markSceneWithoutPatternDirty(scene_index);
```

**New**:
```c
            autosave_markSceneWithPatternDirty(scene_index);
```

This makes the full re-enable dirty-mark include Pattern for every present
Scene. Previously excluded because Pattern persistence had no owner.

---

## Step 9 — HCNAMES Pattern Row Source/Refreshed Lifecycle

### 9.1 — filesystem.c: pattern drain completion — MODIFY

In the `filesystem_autosavePatternDrainCompleted()` callback (Step 5.3),
after clearing the dirty bit on success:

```c
    /* After a successful Pattern drain, set the HCNAMES Pattern row
     * source to '@' (AutoSave provenance) and mark it refreshed.
     * This follows the same lifecycle as Kit/Instrument AutoSave:
     * the refreshed flag means "the on-card copy matches SRAM." */
    filesystem_setResidentSource(
        (uint16_t)(FS_RESIDENT_NAMES_PATTERN_BASE + fs_pattern_drain_scene),
        /* '@' provenance */ ...);
    filesystem_setResidentRefreshed(
        (uint16_t)(FS_RESIDENT_NAMES_PATTERN_BASE + fs_pattern_drain_scene),
        1u);
```

The exact source value encoding for `@` (AutoSave provenance) must match
the existing convention used by the parameter drain. Verify during
implementation by reading how parameter drain calls
`filesystem_setResidentSource()`.

### 9.2 — Autosave.c: pattern dirty marker — MODIFY

In `autosave_markPatternDirty()` (Step 2.2), after setting the dirty bit,
clear the HCNAMES Pattern row refreshed flag:

```c
    /* A user mutation dirties the pattern, so the on-card copy no
     * longer matches SRAM. Clear the refreshed flag. */
    filesystem_clearResidentRefreshed(
        (uint16_t)(AUTOSAVE_HCNAMES_PATTERN_BASE + scene_index));
```

**Alternative**: if `filesystem_clearResidentRefreshed()` does not exist
as a public API, the clearing happens through the existing
`filesystem_setResidentRefreshed(..., 0u)` path. Verify during
implementation.

### 9.3 — filesystem.c: Library Pattern Load clears AutoSave generation — ADD

When a Pattern is loaded from the library into a Scene (through
`FS_INTERNAL_OP_LOAD_PATTERN` or `FS_INTERNAL_OP_LOAD_SCENE`), the
Scene's `fs_pattern_generation[]` entry must be reset to 0 so the next
AutoSave drain writes generation 1 to file 'a'. This ensures that a
library-loaded pattern starts a fresh AutoSave cycle.

Locate the Pattern Load completion path and add:

```c
    fs_pattern_generation[scene_index] = 0u;
```

Also set the dirty bit so the new library pattern is drained:

```c
    autosave_markPatternDirty(scene_index);
```

This may already happen through `bank_invalidateSdCleanScene()` → 
`autosave_markSceneWithPatternDirty()`. Verify callchain during
implementation.

---

## Step Ordering and Dependencies

```
Step X (sequencer externs) ── prerequisite for Step 6
Step 1 (HCNAMES expansion) ── standalone, no other step depends on it
Step 2 (dirty mask) ── prerequisite for Steps 5, 6, 8, 9
Step 3 (snapshot region) ── prerequisite for Steps 4, 5, 6
Step 4 (snapshot copy) ── prerequisite for Steps 5, 6
Step 5 (drain state machine) ── prerequisite for Step 6
Step 6 (scheduler integration) ── prerequisite for functional drain
Step 7 (boot reader) ── independent, can be done in any order after Step 1
Step 8 (wire dirty marker) ── depends on Step 2
Step 9 (HCNAMES lifecycle) ── depends on Steps 2, 5
```

**Recommended implementation order**:
1. Step X (sequencer externs)
2. Step 1 (HCNAMES expansion in Autosave.h/c)
3. Step 3 + 4 (snapshot region and copy)
4. Step 2 (dirty mask)
5. Step 8 (wire dirty marker)
6. Step 5 (drain state machine)
7. Step 6 (scheduler integration)
8. Step 9 (HCNAMES lifecycle)
9. Step 7 (boot reader)
10. `make clean && make` (mandatory after header changes)
11. Hardware verification against acceptance tests A-E

---

## RAM Budget Verification

| Allocation | Bytes | Region | Owner |
|------------|------:|--------|-------|
| `pat_autosave_snapshot` (Step 3) | 10,519 | SRAM1 .bss | PatternData.c |
| `autosave_pattern_dirty_mask` (Step 2) | 2 | SRAM1 .bss | Autosave.c |
| `fs_pattern_generation[16]` (Step 5) | 64 | SRAM1 .bss | filesystem.c |
| `fs_pattern_drain_scene` (Step 5) | 1 | SRAM1 .bss | filesystem.c |
| **Total Session 064** | **10,586** | **SRAM1** | |

Post-S064 SRAM1 free estimate: ~117,101 − 10,586 ≈ **106,515 B** (104.0 KB).

Note: the HCNAMES expansion (Step 1) changes `AUTOSAVE_HCNAMES_ROW_COUNT`
from 129 to 145, but the `resident_names` arrays are passed as function
parameters — they are not static allocations in Autosave.c. The static
`fs_resident_source[]` and `hcnames_name_mirror[]` in filesystem.c are
already sized at 145 rows (done in S063). No additional RAM from Step 1.

---

## Build Notes

- `make clean` is **mandatory** after any header change (Autosave.h,
  PatternData.h, sequencer.h) — the Makefile has no header dependency
  tracking.
- The format version bump (Step 1.1) means all existing `.hcprms1`/
  `.hcprms2` files become invalid on first boot. The boot reader detects
  the version mismatch and synthesizes fresh initial records. This is a
  one-time loss of unsaved parameter edits.
- The 32 new `.patNNx` files are created on first drain, not at boot.
  No ensure step is needed because the drain writer creates files with
  `"w"` mode (create-or-truncate).

---

## Implementation Notes (2026-09-12)

- Confirmed the S063 filesystem already owns 145 physical HCNAMES rows and
  already streams complete v4 Pattern payloads through `staging_buf`; the
  scalar AutoSave record remains the fixed 34,768-byte contract. S064 changes
  the resident-name array dimensions and header version, not the scalar mask,
  payload, or record byte counts.
- Confirmed `@` is currently encoded as
  `FS_RESIDENT_SOURCE_INSTRUMENT_DIRECT` for Instrument rows only. S064 needs
  a distinct Pattern-only provenance token so Pattern `@` does not resolve as
  an Instrument source; the parser/formatter/resolver will be updated with a
  dedicated value.
- Confirmed the runtime scheduler order is settings, diagnostic trace,
  parameter drain, then the new Pattern drain. Pattern drain will reuse the
  existing facade, `staging_buf`, and Pattern CRC helpers, and will publish
  the Pattern HCNAMES row in the same durable transaction before its callback
  clears the dirty bit.
- Confirmed current PatternData mutation setters funnel card-clean invalidation
  through `bank_invalidateSdCleanScene()`, but that helper also serves
  non-Pattern mutations. PatternData therefore needs a local mutation wrapper
  that calls the existing invalidation plus the new Pattern dirty marker.
- Approved S064 retained SRAM additions are exactly: 10,519 bytes for the
  Pattern snapshot in PatternData.c, 2 bytes for the Pattern dirty mask in
  Autosave.c, 64 bytes for sixteen uint32 Pattern generations, and 1 byte for
  the active drain Scene in filesystem.c; total 10,586 bytes in SRAM1.

### Progress

- Step X: complete — sequencer exposes the existing record/erase flags.
- Step 1: complete — AutoSave format version is 2 and HCNAMES geometry is
  aligned to 145 rows while scalar record geometry remains fixed.
- Steps 2–4: complete — independent 16-bit Pattern dirty tracking, mutation
  funnels, the approved snapshot region, and snapshot accessors are implemented.
- Step 5: complete — the Pattern drain streams a full PAT4 image with a
  generation-bearing header, CRC, full-card handling, and shared HCNAMES
  publication.
- Step 6: complete — settings/trace/parameter scheduling declines before the
  lowest-priority Pattern drain; record/erase flags gate snapshot capture and
  the lowest dirty Scene is selected.
- Step 7: complete — the blocking reader validates both hidden candidates with
  exact current-size geometry, full CRC, no trailing bytes, A-wins ties, and
  only overlays when HCNAMES says Pattern `@` with generation > 0.
- Steps 8–9: complete — complete Scene/Bank loads include Pattern dirty work;
  root Pattern loads and directory-backed Scene/Bank loads reset generation;
  successful Pattern drains publish `@`/`R`, while mutations clear `R`.
- Build and static verification: final clean build, linker SRAM audit, diff
  check, stale-wording audit, and Pattern mutation-funnel audit passed.

### Implementation Notes (continued, 2026-09-12)

- The Pattern drain clears its selected bit at snapshot ownership, not in the
  final callback. A mutation during the multi-tick file stream can therefore
  re-set the bit and survive a successful HCNAMES flush; an error explicitly
  re-arms the bit. This uses no additional retained byte.
- Hidden-candidate validation requires `stack_size == PAT_STACK_SIZE`, hashes
  the fixed header, extension, address/bitmap/pool payload, and probes EOF for
  an exact file length before accepting CRC. The live Scene Pattern is only
  replaced after validation, with the prior directory/default image restored
  if the second read fails.
- A successful directory-backed Pattern replacement resets its generation
  through the filesystem API before marking the Scene complete. The boot
  reader also resets the baseline when HCNAMES Pattern provenance is not `@`,
  so stale hidden files cannot influence a fresh library/Scene/Bank source.
- The Pattern HCNAMES `@` value is distinct from Instrument `@` in the logical
  register. Parser, formatter, row validity, and resolver behavior all keep
  the token Pattern-only while preserving the existing Instrument token.

### Implementation Notes (final verification, 2026-09-12)

- Final clean firmware build passed after all header and source changes:
  `text=426,716`, `data=412`, `bss=289,964`, total `717,092` bytes.
- Linker symbols confirm only the approved S064 retained SRAM additions:
  `pat_autosave_snapshot` = 10,519 bytes,
  `autosave_pattern_dirty_mask` = 2 bytes,
  `fs_pattern_generation[16]` = 64 bytes, and
  `fs_pattern_drain_scene` = 1 byte; total = 10,586 bytes.
- Final source checks passed: `git diff --check`, stale 129-row wording
  audit, and Pattern mutation-funnel audit. The tracked firmware image removed
  by the clean build was restored unchanged from the repository index.

---

## Code Review Assessment (2026-09-12)

**Reviewer**: Claude Opus 4.6 (post-implementation diff review)
**Scope**: all 10 changed files, 1,045 insertions / 72 deletions
**Verdict**: **PASS — ready to commit**

### Step-by-step coverage

| Step | Schedule requirement | Implemented | Notes |
|------|---------------------|-------------|-------|
| X | Extern `seq_recordActive`, `seq_eraseActive` in sequencer.h | Yes | Docstring, placement, and `volatile` annotation match schedule |
| 1 | HCNAMES 129→145, format version 1→2 | Yes | `AUTOSAVE_HCNAMES_ROW_COUNT` = 145, `AUTOSAVE_HEADER_FORMAT_VERSION` = 2, static assert anchors Pattern base |
| 2 | 16-bit Pattern dirty mask + API | Yes | PRIMASK-guarded set/clear/get; `filesystem_clearResidentRefreshed()` called inside the same critical section as dirty-set |
| 3 | Snapshot region + accessor | Yes | `pat_autosave_snapshot` (10,519 B SRAM1), `pat_snapshotScene()` = plain memcpy, `pat_autosaveSnapshot()` = const pointer return |
| 4 | Mutation funnel | Yes | `pat_markSceneDirty()` combines `bank_invalidateSdCleanScene()` + `autosave_markPatternDirty()`; all 14 mutation sites redirected |
| 5 | Drain state machine | Yes | Phases 0–10 plus default: open, header build/CRC/stream, three payload sections via refactored helper, CRC seek-back/write, close, HCNAMES publication |
| 6 | Scheduler integration | Yes | Lowest-priority claimant after scalar AutoSave; seq record/erase gate, menu gate, filesystem-ready gate; clear-before-copy ownership boundary |
| 7 | Boot reader | Yes | Candidate validator (exact size, full CRC, EOF probe), A-wins-tie rule, `@`-provenance guard, generation-zero guard, snapshot-and-restore on second-read failure |
| 8 | Scene/Bank load wiring | Yes | `autosave_markSceneWithPatternDirty()` replaces `WithoutPattern`; `filesystem_resetPatternAutosaveGeneration()` called before dirty marking |
| 9 | HCNAMES lifecycle | Yes | `FS_RESIDENT_SOURCE_PATTERN_AUTOSAVE` (0x1ffc) distinct from `INSTRUMENT_DIRECT`; parser/formatter/resolver/validator updated; refresh witness race handled in `filesystem_cacheCurrentResidentPatternName()` |

### Beyond-schedule additions (all justified)

1. **`pat_markSceneDirty()` local funnel** (PatternData.c): not in the original
   9-step plan but avoids false Pattern dirty bits from non-Pattern mutations
   that share `bank_invalidateSdCleanScene()`. Correct architectural decision.

2. **`filesystem_patternWriteRegionSection()`** (filesystem.c): refactored from
   `filesystem_patternWriteSection()` to accept an explicit region pointer so
   the drain can write the snapshot region instead of the live region. Minimal
   invasive change — the original callers pass the existing implicit region.

3. **`filesystem_clearResidentRefreshed()`** public API (filesystem.h/c): needed
   so `autosave_markPatternDirty()` can atomically clear the HCNAMES refresh
   witness when a mutation invalidates a just-flushed snapshot. Prevents the
   HCNAMES writer from publishing a stale `R` flag for an already-dirty Pattern.

4. **Drain race witness guard** in `filesystem_cacheCurrentResidentPatternName()`:
   checks the dirty mask before setting `R` — if a mutation arrived between
   snapshot and HCNAMES overlay, the refresh witness is cleared rather than
   falsely set. Subtle but correct.

5. **Root Pattern load generation reset** in `filesystem_loadPattern_tick()`:
   after a library Pattern replaces a resident Scene's data, its generation
   resets to 0 and the Scene is marked Pattern-dirty. This starts a fresh A/B
   epoch and ensures the replacement is captured before old hidden files could
   be considered authoritative.

6. **`filesystem_resetPatternAutosaveGeneration()`** public API: called by
   presetManager.c Scene/Bank load completion. Ensures directory-backed Pattern
   replacements start a clean generation epoch.

### Correctness observations

- **Ownership boundary**: dirty bit is cleared before snapshot memcpy in the
  scheduler, so any mutation during the multi-tick file stream re-sets the bit
  and is not lost. The completion callback re-arms the bit on any error. This
  is the same proven pattern used by the scalar AutoSave drain.

- **Boot reader safety**: candidate validation is read-only and never modifies
  live Pattern data. The winner is applied only after full CRC validation with
  exact file-size enforcement. The prior directory/default image is preserved
  via snapshot-and-restore if the second read encounters a card error.

- **HCNAMES row exclusion**: `filesystem_autosaveDrainHasRefreshWork()` and
  `filesystem_clearResidentRefreshedCaptured()` now stop at `PATTERN_BASE`,
  preventing the scalar drain from claiming ownership of Pattern rows.

- **`autosave_markSourceDirty()` guard**: Pattern rows (≥ PATTERN_BASE) return
  early, preventing the scalar source-tracking code from misinterpreting a
  Pattern HCNAMES mutation as a source change it should track.

- **`autosave_discardDirtyMask()`** clears the Pattern mask alongside the
  existing scalar mask.

- **Boot lifecycle**: `filesystem_initAfterCardReady()` and
  `filesystem_resetFacadeForBootLogRecovery()` both zero the generation array
  and drain scene index.

- **main.c integration**: Pattern boot reader runs after canonical Scene/Bank
  Pattern loads and before runtime AutoSave setup, guarded by
  `bank_hasResidentBank() && filesystem_autosaveEnabled()`.

### Potential concerns (none blocking)

1. **No explicit 32-file cleanup**: stale `.patNNx` files from a previous
   resident Bank are not deleted when a new Bank is loaded. They will be
   overwritten on the next drain cycle. This is consistent with the scalar
   AutoSave approach (`.hcprms1`/`.hcprms2` are never explicitly deleted).

2. **Generation uint32 wrap**: generation increments from 1 with a wrap guard
   (`if (generation == 0u) generation = 1u`). At one drain per second this
   would take ~136 years. Non-issue.

3. **Blocking boot reader**: `filesystem_patternAutosaveBootReaderBlocking()`
   reads up to 32 files (16 scenes × 2 candidates) during boot. Each file is
   10,656 bytes. With the bit-bang SPI this could add measurable boot time.
   Acceptable given the existing blocking boot reader precedent.

### SRAM accounting

Matches the approved budget exactly:
- `pat_autosave_snapshot`: 10,519 B (SRAM1, PatternData.c)
- `autosave_pattern_dirty_mask`: 2 B (SRAM1, Autosave.c)
- `fs_pattern_generation[16]`: 64 B (SRAM1, filesystem.c)
- `fs_pattern_drain_scene`: 1 B (SRAM1, filesystem.c)
- **Total**: 10,586 B

### Summary

All 9 scheduled steps plus the prerequisite Step X are fully implemented.
The 6 beyond-schedule additions are architecturally sound and address real
race conditions or API gaps that the original plan did not anticipate.
No blocking issues found. No unsafe patterns. No ISR-blocking paths. The
implementation is consistent with the project's existing AutoSave contracts
and the async filesystem architecture. Ready for functional testing on
hardware.
