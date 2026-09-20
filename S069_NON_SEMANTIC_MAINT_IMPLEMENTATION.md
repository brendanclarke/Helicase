# S069 — Non-semantic Pattern maintenance restriction: Implementation schedule

## Status

Implemented in product code; hardware verification remains pending.
Implements `S069_NON_SEMANTIC_PAT_MAINT_RESTRICTION_CLAUDE.md` (the plan).

---

## Change summary

Seven ordered implementation steps across six source/header files. Each step
is testable in isolation; steps 1–3 form a minimal split that can be verified
before the scheduler work in steps 4–7 lands. Total new allocation: 5 bytes
SRAM1 `.bss` — 2 bytes for the `Autosave.c`-owned mask and 3 bytes for the
`filesystem.c`-owned debounce state.

## Implementation notes — 2026-09-20

- Steps 1–3 are implemented: the relocation executor now marks only the new
  non-semantic eligibility mask, `pat_markPoolMutationDirty()` and its public
  declaration are retired, and the established semantic Pattern dirty path
  remains unchanged for real edits.
- Steps 4–7 are implemented: the mask API is owned by `Autosave.c`, policy
  discard clears it, the filesystem has a separate 5-byte debounce/eligibility
  state, and the new lowest-priority scheduler rung reuses the existing PAT4
  snapshot/A-B/CRC/HCNAMES transaction. Failed non-semantic drains restore only
  the non-semantic bit; they do not fabricate semantic dirtiness or clear the
  HCNAMES witness.
- The current source had additional writer-arm lifecycle resets beyond the
  line table in this schedule. The new non-semantic arm is also cleared on
  card-facade destruction, fresh card mount, and boot setup start/success so a
  stale deadline cannot cross a filesystem or Bank session. It remains
  intentionally uncleared on ordinary scalar-idle ticks.
- A clean `make` completed after the header changes, and the final incremental
  `make` after lifecycle-reset edits linked `text=448,580`, `data=412`,
  `bss=291,196`; `arm-none-eabi-nm` confirmed the new SRAM objects as 2 bytes
  (`autosave_nonsemantic_pattern_dirty_mask`), 2 bytes
  (`fs_nonsemantic_pattern_next_due_tick`), and 1 byte
  (`fs_nonsemantic_pattern_armed`). `make img` produced the 448,992-byte
  firmware payload in `build/lxr02.bin` and the 449,008-byte wrapped image
  `build/LXRV2_lxr02.img`. Existing unrelated compiler/linker warnings remain
  unchanged.
- Hardware fixtures for relocation-only dirtiness, running/stopped debounce,
  HCNAMES witness behavior, failure retry, and power interruption remain to be
  exercised; this implementation pass provides source/build verification only.

---

## Step 1 — Retire the semantic dirty call from physical relocation

### 1a. PatternStackService.c — remove `pat_markPoolMutationDirty()` call

**File**: `Core/Bank/Scene/Pattern/PatternStackService.c`
**Line**: 432
**Action**: Remove

Remove the line:
```c
    pat_markPoolMutationDirty(scene);
```

This single call is the only path through which a physical relocation reaches
the semantic dirty boundary (`pat_markSceneDirty()` → both
`bank_invalidateSdCleanScene()` and `autosave_markPatternDirty()`, the latter
of which also clears the HCNAMES Pattern refreshed witness). After removal, a
completed physical relocation no longer touches card-clean, semantic Pattern
AutoSave dirty, or HCNAMES state.

The replacement call (non-semantic mark) is added in step 3a below once its
target function exists.

### 1b. PatternData.c — retire `pat_markPoolMutationDirty()` body

**File**: `Core/Bank/Scene/Pattern/PatternData.c`
**Lines**: 79–94
**Action**: Remove

Remove the complete function definition and its preceding comment block
(lines 79–94):

```c
/*
 * Mark a service-owned relocation through PatternData's established dirty
 * boundary.
 * ...
 */
void pat_markPoolMutationDirty(uint8_t scene_index)
{
    if (scene_indexValid(scene_index))
        pat_markSceneDirty(scene_index);
}
```

**Why it can be removed outright**: this function has exactly one caller
(`PatternStackService.c:432`, removed in step 1a). The real-edit dirty
boundary `pat_markSceneDirty()` remains unchanged with its 14 other callers.

### 1c. PatternData.h — retire `pat_markPoolMutationDirty()` declaration

**File**: `Core/Bank/Scene/Pattern/PatternData.h`
**Line**: 175
**Action**: Remove

Remove the declaration and its preceding comment block (lines 168–175):

```c
/*
 * What: expose the established Pattern dirty boundary without exposing the
 * allocator or its bitmap helpers. Why: Tier 1/2 relocation changes live pool
 * offsets and must be included in card-clean and Pattern AutoSave ownership.
 * Inputs: resident Scene index. Output: the existing dirty registers are
 * invalidated. Affiliate: PatternStackService.c relocation executor.
 */
void pat_markPoolMutationDirty(uint8_t scene_index);
```

---

## Step 2 — Add the non-semantic eligibility mask (Autosave.c / Autosave.h)

### 2a. Autosave.c — static variable

**File**: `Core/Bank/Scene/Autosave.c`
**After line**: 89 (immediately after `static volatile uint16_t autosave_pattern_dirty_mask;`)
**Action**: Add

```c
/*
 * Per-Scene eligibility indicator for non-semantic Pattern AutoSave.
 *
 * What: one bit per Scene, set when a physical pool relocation completes,
 * cleared when a non-semantic AutoSave drain consumes it. Why: the scheduler
 * must distinguish "a real Pattern edit is pending"
 * (autosave_pattern_dirty_mask) from "only a physical relocation happened" so
 * the non-semantic rung runs at lower priority and only when nothing semantic
 * is pending. Inputs/outputs: set by autosave_markNonSemanticPatternDirty(),
 * read by autosave_nonSemanticPatternDirtyMask(), cleared per-Scene by
 * autosave_clearNonSemanticPatternDirty() and wholesale by
 * autosave_discardDirtyMask(). Lifetime: static SRAM1 .bss, cleared at
 * processor reset or policy discard. This mask does NOT clear the HCNAMES
 * refreshed witness — physical relocation is non-semantic and must never
 * touch HCNAMES provenance. Affiliate: filesystem.c non-semantic scheduler.
 */
static volatile uint16_t autosave_nonsemantic_pattern_dirty_mask;
```

**RAM cost**: 2 bytes SRAM1 `.bss`. Approved per plan follow-up #5.

### 2b. Autosave.c — mark function

**File**: `Core/Bank/Scene/Autosave.c`
**After**: `autosave_clearPatternDirty()` (after line 173)
**Action**: Add

```c
/*
 * Record one completed physical pool relocation in the non-semantic mask.
 *
 * What: sets the Scene's bit in autosave_nonsemantic_pattern_dirty_mask
 * without touching card-clean, semantic Pattern dirty, or the HCNAMES
 * refreshed witness. Why: a physical relocation changes pool-block addresses
 * and bitmap runs but does not change musical content; it must be
 * distinguishable from a real edit so the scheduler can run it at strictly
 * lower priority. Input: scene_index 0..15. Output: one atomically set bit
 * while mutation tracking is enabled; ignored otherwise. Affiliates:
 * PatternStackService.c relocation executor, filesystem.c non-semantic
 * scheduler.
 */
void autosave_markNonSemanticPatternDirty(uint8_t scene_index)
{
    uint32_t primask;

    if (!autosave_mutation_tracking_enabled || scene_index >= SCENE_COUNT)
        return;
    primask = autosave_irqSave();
    autosave_nonsemantic_pattern_dirty_mask |= (uint16_t)(1u << scene_index);
    autosave_irqRestore(primask);
}
```

### 2c. Autosave.c — read function

**File**: `Core/Bank/Scene/Autosave.c`
**After**: the mark function added in 2b
**Action**: Add

```c
/*
 * Read the pending non-semantic Pattern Scene mask without consuming bits.
 *
 * Input: none. Output: one bit per Scene requiring a non-semantic Pattern
 * drain. Why: filesystem.c's non-semantic scheduler chooses the next Scene
 * only when no semantic or parameter work is pending. Affiliate:
 * filesystem_autosaveNonSemanticPatternDrainSchedule_tick().
 */
uint16_t autosave_nonSemanticPatternDirtyMask(void)
{
    return autosave_nonsemantic_pattern_dirty_mask;
}
```

### 2d. Autosave.c — clear function

**File**: `Core/Bank/Scene/Autosave.c`
**After**: the read function added in 2c
**Action**: Add

```c
/*
 * Clear one non-semantic Pattern dirty bit after its durable transaction.
 *
 * Input: scene_index 0..15. Output: one atomically cleared bit; a relocation
 * arriving after this boundary can set it again for the next drain. Why: the
 * scheduler consumes one Scene's bit before snapshot and restores it on
 * failure; on success the bit stays clear until a later relocation. Affiliate:
 * filesystem.c non-semantic Pattern drain scheduler and completion callback.
 */
void autosave_clearNonSemanticPatternDirty(uint8_t scene_index)
{
    uint32_t primask;

    if (scene_index >= SCENE_COUNT)
        return;
    primask = autosave_irqSave();
    autosave_nonsemantic_pattern_dirty_mask &=
        (uint16_t)~(1u << scene_index);
    autosave_irqRestore(primask);
}
```

### 2e. Autosave.c — discard path

**File**: `Core/Bank/Scene/Autosave.c`
**Function**: `autosave_discardDirtyMask()` (line 1270)
**Line**: 1282 (after `autosave_pattern_dirty_mask = 0u;`)
**Action**: Add

```c
    autosave_nonsemantic_pattern_dirty_mask = 0u;
```

**Why**: every policy discard that clears `autosave_pattern_dirty_mask` must
also clear the non-semantic mask. The five `autosave_discardDirtyMask()` call
sites in `filesystem.c` (lines 23460, 23775, 23947, 23977, 25523) all
represent AutoSave OFF, Bank-session loss, or deferred policy transitions that
invalidate any pending work — non-semantic work included. No individual call
site needs modification; they all route through this one function.

### 2f. Autosave.h — declarations

**File**: `Core/Bank/Scene/Autosave.h`
**After line**: 560 (after `void autosave_clearPatternDirty(uint8_t scene_index);`)
**Action**: Add

```c
/*
 * Non-semantic Pattern dirty mask: one bit per Scene for physical-relocation-
 * only changes that do not represent musical edits.
 *
 * What: set, read, and clear one bit per resident Scene. Why: physical pool
 * relocations change block addresses and bitmap runs but not musical content;
 * a separate mask lets the scheduler run non-semantic writes at strictly lower
 * priority than semantic Pattern and parameter AutoSave. The mark function
 * does NOT clear the HCNAMES refreshed witness or the Bank card-clean bit —
 * relocations must never touch those registers. Inputs/outputs: scene_index
 * 0..15 for set/clear; the getter returns the full 16-bit pending mask.
 * Affiliates: PatternStackService.c relocation executor and filesystem.c
 * non-semantic Pattern drain scheduling.
 */
void autosave_markNonSemanticPatternDirty(uint8_t scene_index);
uint16_t autosave_nonSemanticPatternDirtyMask(void);
void autosave_clearNonSemanticPatternDirty(uint8_t scene_index);
```

---

## Step 3 — Wire the relocation call site to the new mask

### 3a. PatternStackService.c — add include + replacement call

**File**: `Core/Bank/Scene/Pattern/PatternStackService.c`
**Includes** (top of file, after existing includes): add `#include "Autosave.h"`
**Line**: 432 (the line removed in step 1a)
**Action**: Add

```c
    autosave_markNonSemanticPatternDirty(scene);
```

**Why**: the relocation transaction's completed state now flows into the
non-semantic eligibility mask instead of the semantic dirty boundary. The
include is needed because `PatternStackService.c` does not currently include
`Autosave.h` — it previously reached the dirty path through
`PatternData.h`'s `pat_markPoolMutationDirty()` declaration, which is now
retired.

At this point the split is mechanically complete: relocations no longer
semantically dirty, and the non-semantic mask accumulates their work. Steps
4–7 below add the scheduler rung that actually drains it.

---

## Step 4 — Add scheduler state and debounce variables (filesystem.c)

### 4a. filesystem.c — static variables

**File**: `Core/Hardware/SD/filesystem.c`
**After line**: 1942 (after `static uint8_t fs_autosave_writer_armed = 0u;`)
**Action**: Add

```c
/*
 * Non-semantic Pattern AutoSave arm/due-tick debounce state.
 *
 * What: one armed flag and one wrapping millisecond deadline, structurally
 * identical to the scalar writer's fs_autosave_writer_armed /
 * fs_autosave_next_due_tick idiom. Why: both running and stopped conditions
 * use the arm/due-tick shape — arm once when the non-semantic mask becomes
 * nonzero and all higher-priority work is idle, re-check eligibility at the
 * due tick, start only then. This debounce prevents tight thrash if
 * eligibility flickers across a few ticks and provides a natural spacing
 * constraint between successive non-semantic writes. Inputs: time_sysTick and
 * the non-semantic eligibility mask. Outputs: at most one debounced start per
 * eligibility episode. Lifetime: static filesystem.c-owned; cleared alongside
 * the existing writer arm flags on AutoSave OFF and Bank-session loss.
 * Affiliates: filesystem_autosaveNonSemanticPatternDrainSchedule_tick().
 */
static uint16_t fs_nonsemantic_pattern_next_due_tick = 0u;
static uint8_t fs_nonsemantic_pattern_armed = 0u;
```

**RAM cost**: 3 bytes SRAM1 `.bss`.

### 4b. filesystem.c — clear arm on AutoSave OFF

The arm flag must be cleared on every path that clears
`fs_autosave_writer_armed`. These sites are:

| Location | Context | Action |
|---|---|---|
| `filesystem_setAutosaveEnabled()` OFF branch (line 23450) | Policy OFF | Add `fs_nonsemantic_pattern_armed = 0u;` after `fs_autosave_writer_armed = 0u;` |
| `filesystem_autosaveSetupCompleted()` failure (line 23901) | Setup failed | Add `fs_nonsemantic_pattern_armed = 0u;` after `fs_autosave_writer_armed = 0u;` |
| `filesystem_autosaveWriterSchedule_tick()` OFF guard (line 23938) | Defensive OFF | Add `fs_nonsemantic_pattern_armed = 0u;` after `fs_autosave_writer_armed = 0u;` |
| `filesystem_autosaveWriterSchedule_tick()` no-Bank guard (line 23966) | Bank-session loss | Add `fs_nonsemantic_pattern_armed = 0u;` after `fs_autosave_writer_armed = 0u;` |

All four insertions are a single line: `fs_nonsemantic_pattern_armed = 0u;`
placed immediately after the corresponding `fs_autosave_writer_armed = 0u;`.

The remaining `fs_autosave_writer_armed = 0u;` sites (lines 23214, 23296,
23778, 23830–23849, 23913) are scalar-writer completion/arming transitions
that do not represent policy or session boundaries — the non-semantic arm
does not need to track those.

---

## Step 5 — Add the non-semantic completion callback (filesystem.c)

### 5a. filesystem.c — forward declaration

**File**: `Core/Hardware/SD/filesystem.c`
**After line**: 1464 (after `static void filesystem_autosavePatternDrainCompleted(void);`)
**Action**: Add

```c
static void filesystem_autosaveNonSemanticPatternDrainCompleted(void);
```

### 5b. filesystem.c — completion callback definition

**File**: `Core/Hardware/SD/filesystem.c`
**After**: `filesystem_autosavePatternDrainCompleted()` (after line 23874)
**Action**: Add

```c
/*
 * Terminal callback for a non-semantic Pattern AutoSave drain.
 *
 * What: preserves the non-semantic eligibility bit on I/O failure; on success
 * the bit was already consumed at scheduling time and stays clear. Why: unlike
 * a failed semantic write, there is no data-loss risk — the on-card PAT4
 * remains musically valid regardless of outcome. Failure simply leaves the bit
 * set for retry on the next opportunity. Input: terminal status from the
 * shared Pattern drain state machine (filesystem_autosavePatternDrain_tick).
 * Output: the non-semantic bit is restored on failure and the facade is
 * acknowledged. Affiliates:
 * filesystem_autosaveNonSemanticPatternDrainSchedule_tick() and
 * Autosave.c non-semantic mask.
 */
static void filesystem_autosaveNonSemanticPatternDrainCompleted(void)
{
    if (status != FS_STATUS_DONE)
        autosave_markNonSemanticPatternDirty(fs_pattern_drain_scene);
    filesystem_ack();
}
```

**Why this is separate from `filesystem_autosavePatternDrainCompleted()`**:
the semantic callback restores the semantic bit via
`autosave_markPatternDirty()`, which has the side effect of clearing the
HCNAMES refreshed witness. The non-semantic callback must restore only the
non-semantic bit, without touching HCNAMES state.

---

## Step 6 — Add the non-semantic scheduler rung (filesystem.c)

### 6a. filesystem.c — scheduler function definition

**File**: `Core/Hardware/SD/filesystem.c`
**After**: the completion callback added in step 5b
**Action**: Add

```c
/*
 * Admit one per-Scene non-semantic Pattern AutoSave drain when all higher-
 * priority work declines.
 *
 * What: selects the lowest eligible resident Scene from the non-semantic mask,
 * verifies that no parameter or semantic Pattern work is pending, applies the
 * arm/due-tick debounce, copies the live Pattern region, advances the shared
 * A/B generation, and starts the shared whole-file writer. Why: physical pool
 * relocations change block addresses and bitmap runs but not musical content;
 * persisting the updated layout is strictly cosmetic background work that must
 * never contend with real edit persistence. The "non-active first" ordering
 * prefers Scenes other than seq_activePattern, falling back to the active
 * Scene only when no non-active candidate exists.
 *
 * Gate list (shared with the semantic pattern drain):
 *   fs_autosave_enabled, fs_autosave_runtime_ready,
 *   fs_autosave_writer_boot_ready, bank_hasResidentBank(),
 *   menu_isLoadSaveCommandActive(), LOAD_PAGE/SAVE_PAGE suppression,
 *   afatfs_getFilesystemState() == AFATFS_FILESYSTEM_STATE_READY,
 *   seq_recordActive || seq_eraseActive, patSvc_idle().
 *
 * Additional gates (non-semantic-specific):
 *   !autosave_maskHasDirty()          — no scalar parameter work pending
 *   autosave_patternDirtyMask() == 0u — no semantic Pattern work pending
 *   autosave_nonSemanticPatternDirtyMask() != 0u — at least one eligible Scene
 *
 * Debounce: both running and stopped playback conditions use the arm/due-tick
 * idiom (fs_nonsemantic_pattern_armed / fs_nonsemantic_pattern_next_due_tick).
 *
 * Inputs: Autosave.c non-semantic mask, seq_activePattern, time_sysTick.
 * Outputs: at most one Pattern drain owns the facade; non-active Scenes are
 * preferred. Affiliates: pat_snapshotScene(),
 * filesystem_autosavePatternDrain_tick(),
 * filesystem_autosaveNonSemanticPatternDrainCompleted(), and Autosave.c.
 */
static void filesystem_autosaveNonSemanticPatternDrainSchedule_tick(void)
{
    uint16_t mask;
    uint8_t scene;
    uint8_t active;
    uint32_t generation;
    uint16_t now;

    /* ---- shared gate list (identical to semantic pattern drain) ---- */
    if (!fs_autosave_enabled || !fs_autosave_runtime_ready ||
        !fs_autosave_writer_boot_ready || !bank_hasResidentBank())
        return;
    if (menu_isLoadSaveCommandActive())
        return;
    if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE)
        return;
    if (afatfs_getFilesystemState() != AFATFS_FILESYSTEM_STATE_READY)
        return;
    if (seq_recordActive || seq_eraseActive)
        return;
    if (!patSvc_idle())
        return;

    /* ---- non-semantic-specific: nothing higher pending ---- */
    if (autosave_maskHasDirty() || autosave_patternDirtyMask() != 0u)
        return;

    mask = autosave_nonSemanticPatternDirtyMask();
    if (mask == 0u) {
        fs_nonsemantic_pattern_armed = 0u;
        return;
    }

    /* ---- arm/due-tick debounce (both running and stopped conditions) ---- */
    now = time_sysTick;
    if (!fs_nonsemantic_pattern_armed) {
        fs_nonsemantic_pattern_next_due_tick = (uint16_t)(
            now + AUTOSAVE_WRITER_INTERVAL_MS);
        fs_nonsemantic_pattern_armed = 1u;
        return;
    }
    if ((uint16_t)(now - fs_nonsemantic_pattern_next_due_tick) >= 0x8000u)
        return;

    /* ---- non-active-first Scene selection ---- */
    active = seq_activePattern;
    scene = SCENE_COUNT;

    /* First pass: prefer any non-active Scene. */
    {
        uint8_t s;
        for (s = 0u; s < SCENE_COUNT && s < 16u; s++) {
            if (s == active)
                continue;
            if ((mask & (uint16_t)(1u << s)) != 0u) {
                scene = s;
                break;
            }
        }
    }
    /* Second pass: fall back to the active Scene. */
    if (scene >= SCENE_COUNT) {
        uint8_t s;
        for (s = 0u; s < SCENE_COUNT && s < 16u; s++) {
            if ((mask & (uint16_t)(1u << s)) != 0u) {
                scene = s;
                break;
            }
        }
    }
    if (scene >= SCENE_COUNT || scene >= 16u)
        return;

    /* ---- ownership handoff (same pattern as semantic drain) ---- */
    autosave_clearNonSemanticPatternDirty(scene);
    pat_snapshotScene(scene);
    fs_pattern_drain_scene = scene;
    generation = fs_pattern_generation[scene] + 1u;
    if (generation == 0u)
        generation = 1u;
    fs_pattern_generation[scene] = generation;
    if (!filesystem_start(FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN,
                          FS_FILE_SETTINGS, 0u,
                          filesystem_autosaveNonSemanticPatternDrainCompleted)) {
        autosave_markNonSemanticPatternDirty(scene);
        return;
    }
    fs_nonsemantic_pattern_armed = 0u;
    op_pattern_scene = scene;
    filesystem_patternAutosaveFilename(op_pattern_filename, scene, generation);
}
```

**Key design notes**:

- **Reuses `FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN`**: the file format, write
  state machine, phase-10 HCNAMES staging, and CRC/A/B publication are
  identical to a semantic drain. The only difference is the completion
  callback (passed to `filesystem_start()`), which restores the non-semantic
  bit on failure instead of the semantic one.

- **Reuses `AUTOSAVE_WRITER_INTERVAL_MS` (5000 ms)** for the debounce
  interval. This is a reasonable starting value; it can be given its own
  constant at implementation time if a different interval is preferred.

- **HCNAMES witness**: the shared phase-10 staging in
  `filesystem_cacheCurrentResidentPatternName()` (filesystem.c:6241-6268)
  already checks whether `autosave_patternDirtyMask()` is nonzero for the
  drained Scene — if a semantic edit arrived during the drain, the refreshed
  witness is NOT set, which is correct. It does not need to check the
  non-semantic mask because a layout-only difference between the written file
  and current SRAM does not invalidate the file's musical correctness for
  boot purposes.

### 6b. filesystem.c — add rung to the scheduler ladder

**File**: `Core/Hardware/SD/filesystem.c`
**After line**: 24639 (`filesystem_autosavePatternDrainSchedule_tick();`)
**Action**: Add

```c
    /* Non-semantic Pattern is the final background claimant after semantic
     * Pattern AutoSave work; it runs only when no parameter or semantic
     * Pattern work is pending. */
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveNonSemanticPatternDrainSchedule_tick();
```

This is the sixth and last rung of the priority ladder:

```text
1. filesystem_settingsWriterSchedule_tick()                      (settings.cfg)
2. filesystem_autosaveTraceFlushSchedule_tick()                  (diagnostic)
3. filesystem_patternTraceFlushSchedule_tick()                   (diagnostic)
4. filesystem_autosaveWriterSchedule_tick()                      (scalar/parameter)
5. filesystem_autosavePatternDrainSchedule_tick()                (semantic Pattern)
6. filesystem_autosaveNonSemanticPatternDrainSchedule_tick()     (non-semantic Pattern) ← NEW
```

---

## Step 7 — Clear non-semantic arm on lifecycle resets (filesystem.c)

### 7a. filesystem_autosaveSetupCompleted() — success path

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 23913 (the `fs_autosave_writer_armed = 0u;` in the setup-success path)
**Action**: Add after

```c
    fs_nonsemantic_pattern_armed = 0u;
```

**Why**: a successful setup re-establishes the AutoSave session from scratch;
any prior non-semantic arm from a stale session must not carry over.

### 7b. filesystem_autosaveWriterSchedule_tick() — not-dirty-not-recovery path

**File**: `Core/Hardware/SD/filesystem.c`
**Line**: 24022 (inside the `!fs_autosave_recovery_pending && !autosave_maskHasDirty()` block)
**Action**: No change needed

The non-semantic arm is deliberately NOT cleared here. This path means "no
scalar work exists this tick" — the non-semantic scheduler has its own
independent mask check and should retain its arm state across scalar-idle
ticks.

---

## Files not changed

| File | Why |
|---|---|
| `PatternData.c` `pat_markSceneDirty()` | Unchanged; its 14 real-edit callers continue to use the full semantic dirty boundary. |
| `BankData.c` `bank_invalidateSdCleanScene()` | Not called from the non-semantic path. Relocations no longer touch card-clean or mutated-during-save. |
| `Autosave.c` `autosave_markPatternDirty()` | Unchanged; still called by `pat_markSceneDirty()` for real edits and by the semantic drain's failure re-arm. |
| `filesystem.c` `filesystem_autosavePatternDrain_tick()` | Unchanged; the shared phase-0-through-10 state machine writes the same PAT4 format for both semantic and non-semantic drains. |
| `filesystem.c` `filesystem_cacheCurrentResidentPatternName()` | Unchanged; its existing check against `autosave_patternDirtyMask()` correctly prevents setting the HCNAMES refreshed witness when a semantic edit arrived during a non-semantic drain. A new relocation during the drain (non-semantic mask re-set) does not prevent setting the witness because the written file is musically valid. |
| `filesystem.c` `filesystem_autosavePatternDrainSchedule_tick()` | Unchanged; continues to drain semantic Pattern work at its existing priority. |
| `config.h` | No new `#define` needed if `AUTOSAVE_WRITER_INTERVAL_MS` is reused for the non-semantic debounce. A separate constant may be added at implementation time if a different interval is desired. |
| `filesystem.h` | No new public declarations needed. The non-semantic scheduler and completion callback are `static` within `filesystem.c`. |

---

## Ordered change index

| # | File | Line(s) | Action | What |
|---|---|---|---|---|
| 1a | `PatternStackService.c` | 432 | Remove | `pat_markPoolMutationDirty(scene);` |
| 1b | `PatternData.c` | 79–94 | Remove | `pat_markPoolMutationDirty()` definition + comment |
| 1c | `PatternData.h` | 168–175 | Remove | `pat_markPoolMutationDirty()` declaration + comment |
| 2a | `Autosave.c` | after 89 | Add | `static volatile uint16_t autosave_nonsemantic_pattern_dirty_mask;` + comment |
| 2b | `Autosave.c` | after 173 | Add | `autosave_markNonSemanticPatternDirty()` definition |
| 2c | `Autosave.c` | after 2b | Add | `autosave_nonSemanticPatternDirtyMask()` definition |
| 2d | `Autosave.c` | after 2c | Add | `autosave_clearNonSemanticPatternDirty()` definition |
| 2e | `Autosave.c` | 1282 | Add | `autosave_nonsemantic_pattern_dirty_mask = 0u;` in discard |
| 2f | `Autosave.h` | after 560 | Add | Three public declarations + comment block |
| 3a | `PatternStackService.c` | includes + 432 | Add | `#include "Autosave.h"` + `autosave_markNonSemanticPatternDirty(scene);` |
| 4a | `filesystem.c` | after 1942 | Add | `fs_nonsemantic_pattern_next_due_tick` + `fs_nonsemantic_pattern_armed` + comment |
| 4b | `filesystem.c` | 23450, 23901, 23938, 23966 | Add | `fs_nonsemantic_pattern_armed = 0u;` (4 sites) |
| 5a | `filesystem.c` | after 1464 | Add | Forward declaration of completion callback |
| 5b | `filesystem.c` | after 23874 | Add | `filesystem_autosaveNonSemanticPatternDrainCompleted()` definition |
| 6a | `filesystem.c` | after 5b | Add | `filesystem_autosaveNonSemanticPatternDrainSchedule_tick()` definition (~90 lines) |
| 6b | `filesystem.c` | after 24639 | Add | Sixth rung in scheduler ladder (3 lines) |
| 7a | `filesystem.c` | after 23913 | Add | `fs_nonsemantic_pattern_armed = 0u;` in setup-success |

---

## Verification sequence

### Phase A — split isolation (steps 1–3 only)

Verify with the non-semantic scheduler disabled (steps 4–7 not yet landed):

1. Force relocations (Tier 1 or reactive) with no edits.
2. Confirm `autosave_pattern_dirty_mask` is NOT set for the relocated Scene.
3. Confirm `bank_scene_sd_clean_mask` is NOT cleared for the relocated Scene.
4. Confirm the HCNAMES Pattern refreshed witness for the relocated Scene is
   unchanged.
5. Confirm `autosave_nonsemantic_pattern_dirty_mask` IS set for the relocated
   Scene.
6. Confirm a real edit (step toggle, automation append, clear, etc.) still
   produces all three semantic effects unchanged.

### Phase B — scheduler integration (steps 4–7 landed)

7. Playback running, parameter or semantic Pattern dirty: confirm non-semantic
   rung does not start (gate check).
8. Playback running, nothing higher pending, non-semantic mask set: confirm
   non-semantic AutoSave starts after the debounce interval; confirm non-active
   Scene is chosen over active Scene when both are eligible.
9. Playback stops, non-semantic mask set: confirm drain is queued and starts
   only after the debounce interval elapses with playback still stopped.
10. Playback stops, then resumes before debounce elapses: confirm drain still
    starts (condition 1 allows it while running).
11. Non-semantic AutoSave completes: confirm HCNAMES refreshed witness is set;
    confirm card-clean and mutated-during-save bits are NOT touched.
12. Non-semantic AutoSave fails (simulated I/O error): confirm non-semantic
    eligibility bit remains set for retry; confirm no semantic dirtiness is
    fabricated.
13. Power interruption at any point: confirm existing PAT4 A/B recovery
    produces a musically correct file.
