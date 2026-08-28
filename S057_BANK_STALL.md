# S057 Bank Save Stall — Diagnostic Assessment

## 1. Background

Bank Save was exhibiting a screen freeze on hardware: the LCD showed a static `...` indicator during bank operations with no way to tell whether the operation was progressing or hung. The user's only option was to reboot, which corrupted partially-written bank data. The CPU was confirmed NOT locked (LED modes and playback continued to work), ruling out an AFATFS infinite loop and pointing to an operational stall or extreme slowness.

## 2. Remediations Applied This Session

Three changes were implemented and the build was verified clean (380,364 text, 94,856 BSS, no new warnings).

### 2.1 Progress Indicator (menu.c)

Replaced the static `...` display in `menu_paintLoadSaveConfirmation()` with a live `NN.` indicator during bank operations. Queries `filesystem_bankChildCursor()` on each paint cycle to show the two-digit child scene number (00-15) currently being processed. Non-bank operations still show `...`.

### 2.2 Progress Query API (filesystem.c, filesystem.h)

New function `filesystem_bankChildCursor()` returns the live 0-based child slot (0-15) during bank load/save operations, or 0xFF otherwise. Pure read-only accessor of existing `op_bank_child_cursor` state — no new state variables.

### 2.3 Total-Duration Watchdog (filesystem.c)

New counter `op_bank_total_ticks` (8 bytes BSS), incremented once per `filesystem_tick()` call while a bank operation is the active owner. Aborts with named error code `BkTo` plus a diagnostic trace record if the operation exceeds `FS_BANK_TOTAL_TICK_LIMIT` (300,000 ticks, approximately 1-5 minutes depending on main loop frequency). Counter is reset by both `filesystem_requestSaveBank()` and `filesystem_requestLoadBank()`. The watchdog check runs immediately before dispatching to the bank state machine tick function for both load and save.

This fills a gap the per-phase stall detectors cannot cover: an operation that progresses through phases slowly enough that no single phase exceeds its 20,000-tick threshold, but the total operation time is unreasonable.

## 3. Hardware Test Results (SD_CARD_POST_TEST_2)

### 3.1 User-Observed Behaviour

- Kit and Scene load/save: **working correctly**.
- Bank save on slot 009: **screen shows `00.` but never advances**. The progress indicator IS displayed (confirming the menu code change works and `filesystem_bankChildCursor()` returns a valid value), but it stays at `00.` for the entire operation.
- Bank save consistently terminates with **`BkTo00` error** — the total-duration watchdog fires and aborts the operation. This confirms the watchdog is functioning correctly.
- The user attempted bank saves to three different slots. All three failed identically.

### 3.2 On-Disk State After Testing

Three new bank directories were created by the failed save attempts. None existed in the first test dump (SD_CARD_POST_TEST).

| Bank Slot | Children on Disk | Complete Children | Incomplete Child | Notes |
|-----------|-----------------|-------------------|-----------------|-------|
| **009** (overwrite) | 16 (pre-existing) | 00-03 rewritten OK | 04 Brezel: directory exists, **0 files** | 05-15 untouched from prior Bank Load test. Old child 04's contents were deleted but new contents never written. |
| **010** (new) | 5 | 00-03 | 04 Brezel: 4 of ~10 files | Save was mid-file-write when timeout fired. |
| **016** (new) | 5 | 00-03 | 04: 5 of ~10 files | Same pattern. |
| **017** (new) | 5 | 00-03 | 04: 4 of ~10 files | Same pattern. |

Every bank save attempt completes exactly 4 full children and stalls partway through the 5th.

### 3.3 Bank 009 Scene-Level Comparison

Comparing POST_TEST (before this build) to POST_TEST_2 (after):

| Scene | POST_TEST | POST_TEST_2 | Interpretation |
|-------|-----------|-------------|---------------|
| 00 Barf | 10 files | 10 files | Rewritten or untouched (Jan 1 1970 dates) |
| 01-03 | 10 files each | 10 files each | Same |
| **04 Brezel** | **10 files** | **0 files (empty dir)** | **Old data deleted; new data never written** |
| 05-09 | 10 files each | 10 files each | Untouched (save never reached these) |
| 10 Electro | 2 files | 2 files | Already incomplete from prior session |
| 11-12 | 10 files each | 10 files each | Untouched |
| 13 808ceebe | 5 files | 5 files | Already incomplete from prior session |
| 14-15 | 10/9 files | 10/9 files | Original Bank Load data |

Scene 04 is the definitive evidence of the stall boundary: the per-child delete-then-write cycle completed the delete but never wrote the replacement.

## 4. Trace File Analysis (asavetrc.bin)

138,456 bytes = 17,307 records. Ring capacity 2,048 records (configured), no ring overflow during bank saves.

### 4.1 Record Type Summary

| Stage | Count | Description |
|-------|-------|-------------|
| D (DIRTY) | 16,872 | Autosave dirty marks (normal background) |
| I (INSTRUMENT_MARK) | 211 | Instrument autosave lifecycle |
| L (LOAD_MARK) | 78 | Kit/Scene load marks |
| O (SAVE_LIFECYCLE) | 39 | **Save lifecycle checkpoints** |
| E (OPERATION_ERROR) | 3 | **One per failed bank save** |
| X (PHASE_STALL) | 3 | **One per failed bank save** |
| B (BANK_PRESENT) | 3 | Bank present-mask snapshots |
| Other (S,A,V,M,C,P,T,R,W,F,G) | various | Normal autosave pipeline and suppression |

### 4.2 Standalone Saves — Successful

One standalone Scene save (slot 5) completed with full lifecycle chain:
```
SCENE REQUEST → SCENE DELETE_RESULT → SCENE CREATE_RESULT → SCENE SOURCE_STAGED → SCENE FINISH
```

One standalone Kit save (slot 38) also completed with full lifecycle chain. Both confirm the per-element save state machine works correctly outside of bank context.

### 4.3 Bank Save Attempts — All Three Failed Identically

**Attempt 1 — Bank slot 9 (overwriting existing bank):**
```
O  BANK   REQUEST         slot=9    t=1781
O  BANK   REQUEST         slot=9    t=1781    (duplicate record — Menu + filesystem)
O  SCENE  CREATE_RESULT   slot=9    t=4295    ← child directory created (child 0)
O  SCENE  CREATE_RESULT   slot=9    t=13719   ← child 1, +9,424 ticks
O  SCENE  CREATE_RESULT   slot=9    t=22621   ← child 2, +8,902 ticks
O  SCENE  CREATE_RESULT   slot=9    t=31440   ← child 3, +8,819 ticks
O  SCENE  CREATE_RESULT   slot=9    t=40278   ← child 4, +8,838 ticks
X  PHASE_STALL site=BANK_ENTRY phase=12  t=41132   ← BkTo watchdog fired
E  OPERATION_ERROR  op=21(SAVE_BANK) phase=12 slot=9  t=41132
```

**Attempt 2 — Bank slot 17 (new bank):**
```
O  BANK   REQUEST         slot=17   t=16098
5× SCENE  CREATE_RESULT   slot=17   t=19989..51656  (~8,000 tick spacing)
X  PHASE_STALL site=BANK_ENTRY phase=24  t=55198   ← BkTo watchdog fired
E  OPERATION_ERROR  op=21(SAVE_BANK) phase=24 slot=17  t=55198
```

**Attempt 3 — Bank slot 16 (new bank):**
```
O  BANK   REQUEST         slot=16   t=20204
5× SCENE  CREATE_RESULT   slot=16   t=23912..55493  (~8,000 tick spacing)
X  PHASE_STALL site=BANK_ENTRY phase=24  t=59489   ← BkTo watchdog fired
E  OPERATION_ERROR  op=21(SAVE_BANK) phase=24 slot=16  t=59489
```

### 4.4 Key Observations from Trace

1. **Exactly 5 SCENE CREATE_RESULT records per attempt.** Every attempt creates 5 child scene directories before stalling. The 5th child's directory is created but its file contents are never fully written. The 6th child is never started.

2. **No SCENE DELETE_RESULT, SOURCE_STAGED, or FINISH records during bank saves.** This is expected: the bank save's per-child delete happens at the bank level (phases 20-21, not the scene writer's DELETE_RESULT path), and SOURCE_STAGED/FINISH are only emitted by the standalone scene save completion path, not the bank-delegated path (which returns to bank phase 12 at scene phase 37).

3. **Consistent ~8,800 tick spacing between CREATE_RESULTs.** Each child takes approximately 8,800 ticks for directory creation through file writing. Five children consume ~44,000 ticks total. The BkTo threshold is 300,000 ticks. The operation stalls for approximately 256,000 ticks (the remaining time until watchdog) while stuck on the 5th or 6th child.

4. **tick16 wrap math confirms BkTo as the termination cause.** For attempt 1: BANK REQUEST at tick16=1781, timeout at tick16=41132. Total elapsed = 41132 - 1781 = 39,351 ticks (tick16 space). Predicted: 300,000 mod 65536 = 37,856. Delta of ~1,495 ticks is consistent with the REQUEST record being emitted slightly before the counter starts.

5. **Stall phase differs between attempts.** Phase 12 (bank save cursor advance) in attempt 1; phase 24 (instrument file open wait inside scene writer) in attempts 2-3. The varying phase indicates the timeout fires at whatever state machine phase happens to be active when the 300,000 limit hits, not at a specific stuck phase. The underlying stall is upstream of any particular phase.

6. **The X (PHASE_STALL) record is emitted by the BkTo watchdog**, not by the separate per-phase stall detector (BkSt). The watchdog code at `filesystem_tick()` writes the X record with `AUTOSAVE_TRACE_PHASE_STALL_SITE_BANK_ENTRY` immediately before calling `filesystem_finish(FS_STATUS_ERROR)`. The E record follows from `filesystem_complete()`. Same tick for both records confirms they're the same event.

### 4.5 Bank Present Mask Progression

| Record Index | Present Mask | Interpretation |
|-------------|-------------|----------------|
| 4102 (before bank saves) | 0x7FFF | Scenes 0-14 present (15 children) |
| 6185 (after some activity) | 0x7BFF | Scenes 0-9, 11-12, 14 (scene 10 missing) |
| 13181 (before first bank save) | 0x5BFF | Scenes 0-9, 11-12, 14 (scenes 10, 13 missing) |

The present mask shows 13-15 children eligible for save. Well above the 5-child stall boundary.

## 5. Progress Indicator Analysis

The user reports `00.` displayed throughout the operation, never advancing to `01.`, `02.`, etc. Since `filesystem_bankChildCursor()` returns `op_bank_child_cursor`, and the bank save sets this variable at:

- **Phase 11** (initial scan): finds first set bit in `op_bank_scene_save_mask`, sets cursor (starts at 0 for a full bank)
- **Phase 12** (cursor advance): scans for next set bit, sets cursor to the next child number

The cursor SHOULD advance: 0 → 1 → 2 → 3 → 4 as children are processed. Each child takes ~8,800 ticks at a main loop rate of ~1-5 kHz = 1.8 to 8.8 seconds per child. The display should visibly update.

**Possible explanations for the cursor staying at 00:**

1. **The 5 CREATE_RESULT records might not represent 5 different children.** The O record encodes `op_slot` (the bank slot number, e.g. 9), not the child cursor value. If a retry/loop bug causes the same child (0) to be processed 5 times, the cursor would stay at 0 and 5 CREATE_RESULT records would still be emitted — but Bank 009 scene 04's empty directory and Bank 010-017's 5-child directory trees would be harder to explain.

2. **The cursor advances too briefly to be observed.** At ~8,800 ticks per child, each cursor value is active for ~2-9 seconds. This should be visible, but the LCD update rate (TIM7 at 5 kHz) repaints from `editDisplayBuffer` very rapidly — the cursor value would appear on screen if `menu_paintLoadSaveConfirmation()` is called during the cursor's active window.

3. **menu_paintLoadSaveConfirmation() is not being called during child transitions.** The menu paint path runs from `menu_repaint()` → `menu_paintLoadSaveConfirmation()` on every foreground loop pass while `menu_loadSaveCommandActive` is true. Code review confirmed this in the prior session. Unless a different repaint path is active.

4. **The save mask has only bit 0 set** (only child 0 is selected). This would make cursor=0 the entire time. But this conflicts with the on-disk evidence showing 5 child directories in banks 010/016/017.

This is an unresolved question requiring either: (a) adding the cursor value to the O(CREATE_RESULT) record's value field, or (b) adding a dedicated trace record per cursor advance at phase 12.

## 6. Root Cause Hypothesis: File Handle Exhaustion

### 6.1 The Numerical Coincidence

`AFATFS_MAX_OPEN_FILES = 5` (asyncfatfs.c:57). The bank save stalls after exactly 5 children. This 5-for-5 match is too precise to ignore.

### 6.2 Mechanism

Each bank child requires the following file handle interactions during phases 13-22/8-37:

**Navigation (phases 13-19, for children 2+):**
- Phase 13: `afatfs_opendir_lfn("Bank")` — allocates handle
- Phase 14: `afatfs_chdir(handle)` — uses handle, then...
- Phase 15: `afatfs_fclose(handle)` — releases handle
- Phase 16: `afatfs_opendir(slot_dir)` — allocates handle
- Phase 17: `afatfs_chdir(handle)` — uses handle
- Phase 18: `afatfs_fclose(handle)` — releases handle

**Scene writing (phases 8-37):**
- Phase 8: `afatfs_mkdir_lfn` — allocates handle for child directory
- Phase 9: CREATE_RESULT recorded; `op_kit_slot_dir = handle`
- Phase 10: `afatfs_chdir(handle)`, then `afatfs_fclose(handle)` — releases
- Phases 12-36: Sequential file writes (sceneset.scg, Kit dir, instruments, pattern, effects) — each opens, writes, closes one handle at a time

If any of these closes fails silently — the close callback fires but the AFATFS slot is not actually freed — one handle leaks per child. After 5 children, all 5 slots are exhausted. The 6th child's first `afatfs_opendir_lfn` at phase 13 (or `afatfs_mkdir_lfn` at phase 8 if navigation isn't needed) returns false or stalls forever because no slot is available.

### 6.3 Verification Path

The hypothesis can be tested by:

1. **Instrumenting the AFATFS open file count.** Add a query function to asyncfatfs.c that counts non-NONE entries in `openFiles[]` and log it before/after each child in the bank save loop.

2. **Adding a trace record at cursor advance (phase 12).** This would confirm whether the cursor actually advances through 0→1→2→3→4 or stays at 0.

3. **Checking `afatfs_fclose` return values.** Some close sites check `on_file_closed` callback; others may not verify the handle was actually released.

### 6.4 Alternative Hypothesis: CWD Stack Issue

AFATFS maintains a current working directory via `afatfs_chdir()`. If the CWD mechanism uses an implicit open handle (AFATFS keeps the CWD directory's cluster chain cached), each nested `chdir` without a corresponding `chdir(NULL)` back to root might consume an internal resource. The per-child navigation does chdir into nested directories:
```
Root → Bank/ → 009 LoadTst/ → 00 Barf/
```
Phase 37 does `afatfs_chdir(NULL)` (return to root) after the scene writer completes. If this chdir(NULL) doesn't fully release the nested directory state, resources accumulate per child.

## 7. What's Working

1. **The BkTo watchdog is detecting and reporting the stall.** All three attempts terminated with a clean error code and trace record rather than hanging indefinitely. The user can now see `BkTo00` instead of staring at a frozen `...` forever.

2. **The progress indicator infrastructure works.** `filesystem_bankChildCursor()` returns a valid value (the display shows `00.` not `...`). The plumbing from filesystem → menu → LCD is functioning.

3. **Standalone Kit and Scene saves are unaffected.** The per-element safe-replace design works correctly for single-element operations.

4. **The per-child delete-then-write flow itself is sound for the children it reaches.** Children 00-03 have complete, valid data on disk in every attempt.

5. **No hard CPU lock.** The system remains responsive (LED modes, playback) during the stall, confirming the issue is contained to the filesystem state machine.

## 8. What's Not Working

1. **Bank save never completes.** All three attempts failed after 5 children with BkTo timeout.

2. **Progress indicator doesn't advance.** Shows `00.` throughout — either the cursor isn't advancing or the display isn't capturing the transitions.

3. **Bank data is corrupted by the failed save.** Bank 009 scene 04 was left as an empty directory (old data deleted, new data never written). Banks 010/016/017 are partial ghosts with 5 incomplete children.

## 9. Exhaustive Code Audit — Handle Lifecycle

An exhaustive line-by-line audit of every AFATFS handle allocation and free path was performed across all three context windows of this session. Every allocation/free pair in the bank-child path was traced:

### 9.1 Paths Confirmed Correct (No Leak)

| Path | Allocates | Frees | Notes |
|------|-----------|-------|-------|
| Navigation phase 13: `afatfs_opendir_lfn("Bank")` | 1 handle | Phase 15: `afatfs_fclose()` | Clean open/close pair |
| Navigation phase 16: `afatfs_opendir(slot_dir)` | 1 handle | Phase 18: `afatfs_fclose()` | Clean open/close pair |
| Scene writer phase 8: `afatfs_mkdir_lfn` | 1 handle | Phase 10: `afatfs_fclose()` after `chdir()` | Clean |
| Scene writer phases 12-36: sequential file writes | 1 handle each | Each `afatfs_fclose()` before next open | Sequential, at most 1 at a time |
| Delete-slot scanner: `afatfs_opendir_lfn` | 1 handle | CLOSE_SCAN → DELETE_MATCH path | All exit paths verified |
| `afatfs_deleteTree` | 1 handle | `deleteTreeFinish` → `initFileHandle` | All exit paths go through `deleteTreeFinish` |
| `afatfs_createFileInternal` failure | 1 handle | `initFileHandle` on failure | Every exit calls `findLast` |
| `afatfs_fopen_lfn` allocation failure | 0 handles | `complete(NULL)` returned | No handle consumed |

### 9.2 Paths Confirmed Not Relevant

- `afatfs_chdir(handle)` — copies directory data to `afatfs.currentDirectory` via `memcpy`, does NOT allocate from pool
- `afatfs_chdir(NULL)` — calls `initFileHandle(&afatfs.currentDirectory)`, resets to root, no pool allocation
- `afatfs_fwrite` / `afatfs_fseekAtomic` / `afatfs_appendRegularFreeCluster` — operate on existing handles, do not allocate new ones
- `removeObjects` struct — has own `syntheticFile`, NOT in pool
- `renameObject` — does NOT use pool handles
- `AFATFS_USE_FREEFILE` and `AFATFS_USE_INTROSPECTIVE_LOGGING` — both disabled, no hidden handle consumers
- Settings writer, autosave writer, trace flush — only start when `status == FS_STATUS_IDLE`, cannot run during bank save

### 9.3 Audit Conclusion

No provable handle leak was found in the code paths. Every allocation has a matching free on all exit paths. The 5-for-5 numerical coincidence with `AFATFS_MAX_OPEN_FILES` remains the strongest signal, but the leak — if it exists — may be in AFATFS internal state (cache sector locks, cluster chain state) rather than in the pool scan itself, or in a timing-dependent path not visible in static analysis.

## 10. Key Insight: BkSt Per-Phase Detector Does Not Fire

The per-phase stall detector (BkSt, 20,000 tick threshold) never fires in any of the three attempts. Only BkTo (total-duration watchdog, 300,000 ticks) terminates the operation. This means:

- **No single phase stays stuck for 20,000 ticks.** The operation IS progressing through phases.
- **This rules out HARD handle exhaustion** where `afatfs_allocateFileHandle()` returns NULL and the state machine retries the same phase forever.
- **This points to PERFORMANCE DEGRADATION** — possibly cache thrashing caused by handles holding cache sector locks (`writeLockedCacheIndex`, `readRetainCacheIndex`), or SD card write buffer saturation from accumulated dirty sectors.

The stall phase varies between attempts (phase 12, 24, 24), which is consistent with a gradual slowdown rather than a specific stuck point — the BkTo timer fires at whatever phase happens to be active when the 300,000 tick budget runs out.

## 11. Code Changes — Diagnostic Build

Four changes were made to instrument and mitigate the stall. Together they produce a diagnostic build that will either:
- **Confirm handle exhaustion** (BkHd error code fires before child 5), or
- **Rule it out** (no BkHd, trace shows handle count = 0 at each child boundary, stall persists)

### 11.1 AFATFS Pool Expansion (asyncfatfs.c)

`AFATFS_MAX_OPEN_FILES` bumped from 5 to 8. Adds 984 bytes BSS (3 × 328-byte `afatfsFile_t` structs). If handle exhaustion at 5 is the cause, this moves the stall boundary to child 8 — observable proof. If the stall still occurs at child 5, handles are not the cause.

### 11.2 Handle Count Query API (asyncfatfs.c, asyncfatfs.h)

New function `afatfs_countOpenHandles()` scans the `openFiles[]` pool and returns the count of entries where `type != AFATFS_FILE_TYPE_NONE`. Pure read-only diagnostic accessor.

### 11.3 CREATE_RESULT Trace Enhancement (filesystem.c, AutosaveTrace.h)

Scene writer phase 9's CREATE_RESULT trace record now embeds two additional fields for bank save operations:
- Bits 10..13: `op_bank_child_cursor` (0-15), resolves the Section 5 ambiguity about whether the cursor actually advances
- Bits 14..15: `afatfs_countOpenHandles()` saturating at 3 (0-3), shows handle accumulation trend

New defines in AutosaveTrace.h:
- `AUTOSAVE_TRACE_SAVE_LIFECYCLE_BANK_CHILD_SHIFT` (10)
- `AUTOSAVE_TRACE_SAVE_LIFECYCLE_BANK_HANDLES_SHIFT` (14)

### 11.4 Phase 20 Handle Census (filesystem.c)

Hard check at the top of bank save phase 20 — the entry point for each new child cycle, after navigation closed its handles and before the delete scanner opens one. Calls `afatfs_countOpenHandles()` and if the count is nonzero, immediately aborts with named error code `BkHd` + hex handle count (e.g. `BkHd01` = 1 leaked handle). This fires BEFORE the stall would occur, giving an immediate LCD-visible diagnosis.

## 12. Expected Outcomes on Next Hardware Test

| Scenario | LCD Shows | Trace Evidence | Diagnosis |
|----------|-----------|----------------|-----------|
| Handle leak confirmed | `BkHd01` at child 1 | CREATE_RESULT handles field climbs 0→1→1 | One handle leaks per child; find which close is failing |
| Handle leak deferred by pool expansion | Stall at child 8 instead of 5 | 8 CREATE_RESULTs, BkTo fires | Confirms handle exhaustion, pool bump not a fix |
| Handles clean, stall persists at 5 | `BkTo` as before | Handle count = 0 at each child, 5 CREATE_RESULTs | Eliminate handle hypothesis; investigate cache/SD layer |
| Bank save completes | Success | 16 CREATE_RESULTs, handles = 0 throughout | Pool expansion was sufficient; investigate why 5 was inadequate |

## 13. Recommended Next Steps

1. **Flash and test the diagnostic build.** The four changes above are designed to produce a definitive answer on the next hardware test without requiring trace file analysis — the LCD error code is the first signal.

2. **If BkHd fires:** The leaked handle's identity can be narrowed by checking which `afatfs_fclose()` callback reports success but leaves the pool slot allocated. Add a post-close verification in the close callback.

3. **If BkTo fires with handles = 0:** Investigate AFATFS cache sector exhaustion. The cache has only `AFATFS_NUM_CACHE_SECTORS = 8` sectors (512 bytes each). If dirty sectors accumulate across children without being flushed to SD, new allocations stall waiting for cache eviction. Consider bumping `AFATFS_NUM_CACHE_SECTORS` or adding explicit cache flush between children.

4. **If bank save completes:** The pool expansion from 5→8 resolved it. Monitor whether the handle count trace shows accumulation (deferred leak) or stays at 0 (the issue was concurrent handles, not a leak).

---

*Assessment updated 2026-08-28 with exhaustive audit results and diagnostic build changes. Prior build: 380,364 text / 94,856 BSS. New build pending verification.*
