# S060 Phase A Post-Fixes Assessment

Firmware under test: Phase A + boot fix (commit dcbd400 + leading-dot fix).
Test card image: `SD_CARD_PHASE_A_2/`.

## Reported Symptoms

1. **ERR HNkL01** immediately on entering Kit or Scene load menus (not Bank Load/Save).
2. **No autosave records** (`.hcprms1`/`.hcprms2`) created on card despite `autosave=1` in settings.

## Root Cause Analysis

Two bugs produce the observed symptoms. Both are pre-existing (not introduced by dcbd400).

### Bug 1: Bank Load phase 83 missing `afatfs_chdir(NULL)` (CONFIRMED, FIXED)

**Location**: `filesystem.c` Bank Load state machine, phase 83 (line ~12508).

**Mechanism**: Bank Load navigates into `/Bank/NNN/` at phases 3 and 7 via
`afatfs_chdir()`. After committing bank metadata and emitting the BANK_PRESENT
trace (record 97), it transitions unconditionally to phase 83 to write the
merged HCNAMES register. Phase 83 opens `.hcnames` with "w" mode but does NOT
return CWD to root first. The file is created/written in `/Bank/NNN/` instead
of `/`.

Phase 86 (close) calls `afatfs_chdir(NULL)` — but by then the file is already
created in the wrong directory.

**Evidence on SD_CARD_PHASE_A_2**:
- Two NEW `.hcnames` files in Bank subdirectories not present on the pre-test
  card (`Bank/006 Fullstea/.hcnames` 1327B, `Bank/045 Full/.hcnames` 1342B).
- Total: 15 Bank-subdir `.hcnames` files vs. 13 on pre-test card.
- The new files are in new-format 2-column layout matching what phase 85 writes.

**Fix applied**: Added `if (!afatfs_chdir(NULL)) return;` before the
`afatfs_fopen_lfn()` call in phase 83, matching the pattern used by phase 0,
phase 86, and every other HCNAMES opener.

```c
case 83:
    hcnames_mirror_valid = FS_HCNAMES_MIRROR_INVALID;
    filesystem_bootLoggingSetDetail("BKHCWRIT");
    if (!afatfs_chdir(NULL))       /* <-- ADDED */
        return;                    /* <-- ADDED */
    op_file_ready = false;
    op_file = NULL;
    if (!afatfs_fopen_lfn(FS_RESIDENT_NAMES_FILENAME, ...))
```

### Bug 2: SFN-only root `.hcnames` invisible to `afatfs_fopen_lfn()` (CONFIRMED, NOT YET FIXED)

**Location**: `asyncfatfs.c` LFN directory scan in `afatfs_createFileContinue()`.

**Mechanism**: The root `.hcnames` on the pre-test card was created by old
firmware using `afatfs_fopen()` (short-name API). This produced an SFN-only
directory entry with alias `        HCN` (8-space base, extension HCN) and no
LFN chain entries.

The new firmware opens `.hcnames` exclusively through `afatfs_fopen_lfn()`,
which:
1. Generates SFN alias `HCNAMES    ` for `.hcnames` (leading-dot fix).
2. Scans directory entries: for each SFN entry with no LFN chain, generates a
   display name via `fat_convertFATStyleToFilename()`.
3. For on-disk `        HCN`, the display name is `.HCN` (4 chars).
4. Compares: `fat_compareDisplayName(".HCN", ".hcnames")` fails (length mismatch).
5. File not found; callback receives NULL.

The old and new firmware disagree on the SFN representation of `.hcnames`:

| Firmware | API | SFN Alias | Display Name |
|----------|-----|-----------|--------------|
| Old | `afatfs_fopen()` via `fat_convertFilenameToFATStyle()` | `        HCN` | `.HCN` |
| New | `afatfs_fopen_lfn()` via `afatfs_generateShortAlias()` | `HCNAMES    ` | `HCNAMES` |

This is a migration-only bug: it affects cards initialized by old firmware and
first booted with new firmware. Once a code path creates a new root `.hcnames`
with LFN entries, subsequent opens work and the problem does not recur.

**Impact chain**:
1. Boot Bank Load phase 0: opens root `.hcnames` "r" -> NULL.
2. Bank Load phase 80/87: probe -> ABSENT (probe also can't match `.HCN` vs
   `.hcnames`). Continues with blank name cache.
3. Bank Load phase 83 (Bug 1): writes `.hcnames` to `/Bank/NNN/` instead of
   root. Root `.hcnames` still invisible.
4. `filesystem_ensureAutosaveFilesBlocking()` phase 0/1: opens root `.hcnames`
   "r" -> NULL -> `FS_STATUS_ERROR` -> `fs_autosave_setup_failed = 1`.
   **No `.hcprms1`/`.hcprms2` created.**
5. User enters Kit/Scene load menus: `LOAD_HCNAMES_KIT`/`LOAD_HCNAMES_SCENE`
   phase 0/1 -> NULL -> ERROR -> **ERR HNkL01 / HNsL01**.
6. Eventually a Kit Save triggers `UPDATE_HCNAMES_KIT` -> probe ABSENT ->
   creates new root `.hcnames` with LFN entries. Subsequent opens succeed.

**Why fixing Bug 1 alone resolves both symptoms**: With the chdir fix, Bank
Load phase 83 writes root `.hcnames` from `/` (not `/Bank/NNN/`). Since the
probe returned ABSENT, `afatfs_fopen_lfn(".hcnames", "w")` creates a fresh
directory entry with proper LFN chain and SFN alias `HCNAMES    `. This happens
during boot, BEFORE `filesystem_ensureAutosaveFilesBlocking()` runs and before
any Kit/Scene load menu can be entered. The ensure operation then finds and
reads the new LFN entry successfully and creates `.hcprms1`/`.hcprms2`.

**Residual**: The old SFN-only entry (`        HCN`) remains as an orphan in
the root directory. It is not matched by any open or probe path and is
functionally harmless. A future cleanup or card reformat removes it.

## Trace Evidence

Decoded from `SD_CARD_PHASE_A_2/asavetrc.bin` (711 records, 5688 bytes):

| Record | Stage | Op | Phase | Tick | Interpretation |
|--------|-------|----|-------|------|----------------|
| 0 | OPERATION_ERROR | REPAIR_NAMES(37) | 33 | 1320 | Bank-name rename failure at boot (pre-existing, unrelated) |
| 97 | BANK_PRESENT | -- | -- | 2252 | First boot Bank Load completes |
| 226 | OPERATION_ERROR | ENSURE_AUTOSAVE_FILES(6) | 1 | 2347 | Root `.hcnames` open NULL -> error; no `.hcprms` created |
| 228 | OPERATION_ERROR | LOAD_HCNAMES_KIT(11) | 1 | 6234 | Kit load menu entered; root `.hcnames` not found |
| 230 | OPERATION_ERROR | LOAD_HCNAMES_KIT(11) | 1 | 22029 | Repeat Kit load menu entry |
| 232 | OPERATION_ERROR | LOAD_HCNAMES_KIT(11) | 1 | 40185 | Repeat |
| 234 | OPERATION_ERROR | LOAD_HCNAMES_KIT(11) | 1 | 43237 | Repeat |
| 235 | OPERATION_ERROR | LOAD_HCNAMES_SCENE(13) | 1 | 46406 | Scene load menu entered; same root mismatch |
| 237 | OPERATION_ERROR | LOAD_HCNAMES_KIT(11) | 1 | 50412 | Repeat |
| 238 | OPERATION_ERROR | LOAD_HCNAMES_SCENE(13) | 1 | 55571 | Repeat |
| 335 | BANK_PRESENT | -- | -- | 22847 | Second Bank Load (runtime); no errors after this point |

INSTRUMENT_MARK records show `pub=0` throughout, consistent with blank HCNAMES
cache from the ABSENT probe path (Bug 2).

All 7 LOAD_HCNAMES errors occur between records 228-238 (ticks 6234-55571).
No OPERATION_ERROR records appear after record 238, confirming that the Kit
Save between these records created a new root `.hcnames` with LFN entries,
resolving the SFN mismatch for all subsequent operations.

## SD Card Comparison

| Item | SD_CARD_PHASE_A (pre-test) | SD_CARD_PHASE_A_2 (post-test) |
|------|---------------------------|-------------------------------|
| Root `.hcnames` | 1850B, 4-column old format | 1327B, 2-column new format |
| `.hcprms1` | 35026B | **MISSING** |
| `.hcprms2` | 35026B | **MISSING** |
| Bank-subdir `.hcnames` count | 13 | 15 (+`006 Fullstea`, +`045 Full`) |
| `asavetrc.bin` | -- | 5688B, 711 records |

## Code Changes Summary

**One change made** in `Core/Hardware/SD/filesystem.c`:

Bank Load phase 83 (~line 12508): inserted `if (!afatfs_chdir(NULL)) return;`
before `afatfs_fopen_lfn()`. This returns CWD to root before opening
`.hcnames` for write, matching the established pattern at phase 0 and all
other HCNAMES open sites.

No other code changes. Bug 2 (SFN-only migration mismatch) is resolved as a
side effect of Bug 1's fix because Bank Load now creates a proper LFN-bearing
root `.hcnames` during boot, before ensure or any HCNAMES load runs.

## Recommendations

1. **Test the Bug 1 fix** with the same pre-test card image (or one with
   old-format SFN-only root `.hcnames`) to verify that:
   - Boot Bank Load creates root `.hcnames` (not in Bank subdir)
   - Ensure autosave succeeds (`.hcprms1`/`.hcprms2` present after boot)
   - Kit/Scene load menus produce no ERR HNkL01
2. **Consider adding ensure fallback**: The ensure autosave path has no probe
   fallback — a missing `.hcnames` is a hard error. Adding a probe+skip
   fallback (proceed to `.hcprms` creation with blank name cache) would make
   autosave resilient to any future HCNAMES failure, not just this migration
   case. Low priority since Bug 1 fix resolves the immediate symptom.
3. **Orphan cleanup**: Old SFN-only root entries are harmless but could be
   cleaned up by a future card-repair or format operation. No immediate action
   needed.
4. **Existing Bank-subdir `.hcnames` files**: The 15 spurious `.hcnames` files
   in Bank subdirectories (from this test and prior firmware versions with the
   same bug) are orphans. They consume minimal space and are not read by any
   code path. They can be manually deleted or left for a future cleanup pass.

---

## Phase A 3 Verification (Post Bug 1 Fix)

Test card image: `SD_CARD_PHASE_A_3/`.
Firmware: Bug 1 fix applied (chdir(NULL) at Bank Load phase 83).
Test procedure: deleted root `.hcprms1`, `.hcprms2`, `.hcnames` from pre-test
card, then exercised Kit Load, Bank Load, Bank Save, and Scene Load.

### Results: All Clear

**Zero OPERATION_ERROR records** across 7047 trace records (vs. 9 errors in
711 records on Phase A 2). Both reported symptoms are resolved.

### File Verification

| File | Status | Size | Notes |
|------|--------|------|-------|
| `.hcprms1` | PRESENT | 34768B | Magic `HCPR`, seq 0xA501, non-zero payload (5403B) |
| `.hcprms2` | PRESENT | 34768B | Magic `HCPR`, seq 0xA501, non-zero payload (5430B) |
| `.hcnames` | PRESENT | 1344B | 129 rows, 2-column format, row 0: `Full\t049` |
| `asavetrc.bin` | PRESENT | 56376B | 7047 records, 0 errors |
| `settings.cfg` | PRESENT | 277B | 17 lines, `autosave=1`, `active_bank=49` |

**Autosave files (`.hcprms1`/`.hcprms2`)**: Both present with `HCPR` magic
header and real parameter payload. These were MISSING on the Phase A 2 card.
Confirms that `filesystem_ensureAutosaveFilesBlocking()` completed
successfully after the Boot Bank Load created root `.hcnames` with LFN entries.

**Root `.hcnames`**: 1344 bytes, 129 rows in 2-column new format. Row 0
`Full\t049` matches `active_bank=49` in settings. Instrument names populated
across all 16 scenes. Source fields present where instruments were loaded
(e.g. row 8: `FilMod\t011`).

**Settings**: Well-formed, `format=helicase.settings`, `version=1`,
`lines=17` (matches actual line count). `autosave=1` enabled.

**Bank-subdir `.hcnames`**: 15 files — identical count and set to Phase A 2.
No new spurious files created during Phase A 3 testing, confirming the chdir
fix prevents Bank Load from writing `.hcnames` into the Bank subdirectory.

### Trace Summary

| Metric | Phase A 2 (broken) | Phase A 3 (fixed) |
|--------|--------------------|--------------------|
| Total records | 711 | 7047 |
| OPERATION_ERROR | 9 | **0** |
| BANK_PRESENT | 3 | 2 |
| DIRTY (param changes) | — | 6824 |
| Autosave drains (ADMITTED) | 0 | 13 |
| Complete drain cycles | 0 | 13 (all TERMINAL) |
| SAVE_LIFECYCLE | — | 30 (Kit/Bank/Scene saves) |

The 13 complete autosave drain cycles (ADMITTED → VALIDATED → CAPTURED →
PUBLISHED → TERMINAL) confirm the full autosave pipeline is operational: dirty
parameters are being scheduled, validated, captured to the `.hcprms` A/B
records, published, and terminated without error.

Bank Save at slot 48 completed its full lifecycle (REQUEST → 16 Scene
CREATE_RESULT → SOURCE_STAGED → FINISH), all with `failed=False`.

### Conclusion

The single-line chdir fix at Bank Load phase 83 resolves both reported
symptoms:
1. **ERR HNkL01 eliminated**: zero LOAD_HCNAMES_KIT or LOAD_HCNAMES_SCENE
   errors across extended testing with Kit Load, Scene Load, Bank Load, and
   Bank Save operations.
2. **Autosave operational**: `.hcprms1`/`.hcprms2` created at boot, 13
   successful parameter drains recorded in trace.

Phase A is verified. The firmware is ready for the next phase.

---

## Post-Verification Notes

### Trace file growth

The `asavetrc.bin` file is append-only with no size cap (opens with `"a"` mode
each flush). It grows indefinitely during a session. This is guarded by
`#if DEV_MODE_LOGGING` — production builds write no trace file (the flush tick
is a no-op). The SRAM ring is fixed at 64 records and wraps cleanly; overflow
is counted by `autosaveTrace_droppedCount()` and reported as TRACE_DROPPED
('G') records.

The `.hcprms` A/B generation counter is `uint32_t` with wrap-safe signed-
difference comparison (`autosave_generationIsNewer()` in Autosave.c). Would
require ~2 billion drains to wrap — effectively infinite. The `.hcprms` files
themselves are fixed-size (34768B), overwritten in place each cycle.

### Autosave drain timing: Phase A vs baseline

Compared Phase A 3 trace (`SD_CARD_PHASE_A_3/asavetrc.bin`, 13 cycles) against
a pre-Phase-A baseline trace (`asavetrc.bin` in project root, 6 cycles). Trace
ticks are milliseconds (TIM6 at 1 kHz).

**Baseline (pre-Phase A)** — 6 drain cycles, all cold-validated:

| Phase | Time |
|-------|------|
| A→V (validate both .hcprms) | ~960 ms |
| V→C (capture) | ~98 ms |
| C→P (file write) | ~2002 ms |
| P→T (terminal) | ~13 ms |
| **A→T total** | **~3075 ms** |

**Phase A 3** — steady-state cycles 1-11 (cached winner):

| Phase | Time |
|-------|------|
| A→V (validate both .hcprms) | **0 ms** (cached) |
| V→C (capture) | ~29 ms |
| C→P (file write) | ~2169 ms |
| P→T (terminal) | ~20 ms |
| **A→T total** | **~2218 ms** |

Cold-validation cycles (first/last) remain ~3.5s, comparable to baseline.

**Result**: steady-state drain is ~28% faster (3.1s → 2.2s). The speedup comes
entirely from the **24-byte mounted AutoSave authorization cache** (implemented
in commit 3929745 "s060 first implementation"), which retains the A/B winner
identity across drain cycles so subsequent drains skip the dual-file validation
read. The C→P file write time is unchanged (~2.0-2.2s). The asyncfatfs changes
in dcbd400 target Bank Save performance, not autosave drain.
