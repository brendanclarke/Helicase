# Phase A — AutoSave Writer Speedup: Implementation Recipe

## Goal

Reduce continuation-cycle cost from ~965 bounded ticks to ~393 by:
1. Caching the validated winner across continuation cycles (skip phases 0–5)
2. Skipping the on-card mask re-read when winner is cached (skip phases 50–55)
3. Combining the CRC and commit byte reopens into one open/close/sync cycle
   (merge phases 57–64 into a single seek–write–seek–write sequence)

No functional change to the autosave record format, dirty mask, or
scheduling cadence. First-drain-after-boot retains the full validation path.

---

## Files Changed

| File | Nature of change |
|---|---|
| `Core/Hardware/SD/filesystem.c` | All runtime changes (workspace, phases, scheduling, lifecycle) |
| `config.h` | No change (constants unchanged) |
| `Core/Bank/Scene/Autosave.h` | No change |
| `Core/Bank/Scene/Autosave.c` | No change |

## Implementation Notes (live)

- 2026-09-01: Read `MEMORY.md`, the Session 059 handoff, and the current
  AutoSave/Development Modes references. The live writer matches the recipe's
  phase layout: phase 0 fully resets the operation workspace, phases 1–5
  validate both records, phases 50–55 import the winner mask, phases 56–66
  perform the bounded transformed copy/publication setup, and phases 57–61
  are the separate CRC-close/sync/reopen path targeted for removal.
- 2026-09-01: The worktree has only the two pre-existing untracked planning
  documents (`S060PHASE_A_WRITER_SPEEDUP.md` and
  `S060_AUTOSAVE_UPDATE_READER_PREP.md`); no tracked source changes were
  present before implementation.
- 2026-09-01: Implemented the four-static, 7-byte winner cache in
  `filesystem.c`. Phase 0 consumes it after restoring the committed target's
  index/generation/probe, sets `have_winner`/Bank-match state, emits the normal
  `VALIDATED` trace with bit 3 set, and jumps to phase 56. Cache invalidation is
  present at card/remount, boot ensure, OFF, setup failure, writer error,
  scheduler OFF, and no-resident-Bank boundaries; clean completion also clears
  it. The canonical Autosave mask and all record formats remain unchanged.
- 2026-09-01: Merged CRC and commit publication on the existing `r+` handle:
  phase 21 now transitions directly to phase 62, phases 57–61 are removed,
  and phase 22 remains the final shared sync boundary. Updated the adjacent
  state-machine contract comments to document the single final CRC/A5 sync.
- 2026-09-01: Added a recovery guard during review: a successful no-valid-
  record rebuild may leave dirty SRAM work but has no selected `winner_*`
  tuple. That case remains armed for a 250 ms continuation while keeping the
  cache clear, so the next cycle performs full validation; only a real
  transformed copy-forward completion populates the cache.
- 2026-09-01: The first clean build completed successfully with the existing
  toolchain warnings only; after the recovery guard was added, the final
  incremental rebuild reports `text=385244`, `data=404`, `bss=96184`.
  `make img` produced a 385,648-byte payload and a 385,664-byte packaged
  `build/LXRV2_lxr02.img`. Static phase and symbol checks passed; hardware
  continuation/error fixtures remain to be exercised and are not claimed by
  this source/build verification.

---

## Change 1: Add Winner-Cache Statics

**File:** `Core/Hardware/SD/filesystem.c`
**Location:** After the existing cadence statics at line 1789 (after
`fs_autosave_page_suppressed`)

**What:** Add four static variables that persist across operation-stage reuse.
They cache the identity of the last successfully committed autosave target so
that continuation cycles can skip full dual-record CRC validation.

**Why:** The workspace (`op_autosave_writer`) is zeroed at phase 0 of every
drain cycle and lives inside the `fs_stage_workspace` union. Cached winner
state must survive that zeroing and must also survive reuse of the union by
other operation types (Kit load, Scene save, etc.). External statics are the
only safe location.

**Inputs:** Set by the drain completion callback after a successful commit.
**Outputs:** Read by phase 0 to decide whether to skip validation.
**Affiliates:** `filesystem_autosaveWriterCompleted()`,
`filesystem_autosaveParameterDrain_tick()` phase 0,
`filesystem_setAutosaveEnabled()`, `filesystem_initAfterCardReady()`,
`filesystem_ensureAutosaveFilesBlocking()`, `on_fatfs_card_init_failure()`.

**Add after line 1789:**
```c
/*
 * Winner cache for continuation-cycle validation bypass.
 *
 * What: caches the index, generation, and probe of the last successfully
 * committed autosave target so continuation cycles (250 ms apart within the
 * same Bank session) can skip the full dual-record CRC validation (phases
 * 1–5) and the on-card mask re-read (phases 50–55).
 *
 * Why: dual-record validation consumes ~544 CRC ticks plus four file
 * open/close cycles — the single largest per-cycle cost — and is entirely
 * redundant when the writer itself just committed the target and the card
 * has not been removed. SD card removal while powered is not part of the
 * product contract.
 *
 * Inputs: set by filesystem_autosaveWriterCompleted() on DONE with remaining
 * dirty bits. Outputs: read by phase 0 to restore winner identity and skip
 * to mask classification. Invalidated on any error, policy transition, boot,
 * remount, or Bank-session loss. Affiliates: drain phase 0,
 * filesystem_setAutosaveEnabled(), filesystem_initAfterCardReady(),
 * filesystem_ensureAutosaveFilesBlocking(), on_fatfs_card_init_failure().
 */
static uint8_t  fs_autosave_winner_cached = 0u;
static uint8_t  fs_autosave_cached_winner_index = 0u;
static uint32_t fs_autosave_cached_winner_generation = 0u;
static uint8_t  fs_autosave_cached_winner_probe = 0u;
```

**SRAM cost:** 7 bytes (normal `.bss`).

---

## Change 2: Invalidate Winner Cache at All Lifecycle Reset Points

Each of the following locations already clears `fs_autosave_writer_armed`,
`fs_autosave_recovery_pending`, and similar lifecycle statics. Add
`fs_autosave_winner_cached = 0u;` at each site.

### 2a. `on_fatfs_card_init_failure()` — card timeout/remount reset

**Location:** Line ~21272, among the autosave lifecycle clears

**After the existing clear of `fs_autosave_recovery_pending` (line 21274),
add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** Card failure invalidates all cached SD state. A remount may present
different files.

### 2b. `filesystem_initAfterCardReady()` — fresh mount

**Location:** Line ~21337, among the autosave lifecycle clears

**After the existing clear of `fs_autosave_recovery_pending` (line 21337),
add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** A new card mount must not carry cached winner identity from a prior
card session.

### 2c. `filesystem_ensureAutosaveFilesBlocking()` — boot ensure wrapper

**Location:** Line ~22976, among the boot lifecycle clears

**After the existing clear of `fs_autosave_recovery_pending` (line 22976),
add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** Boot re-creates/validates the pair from scratch; any prior cached
winner is stale.

### 2d. `filesystem_setAutosaveEnabled()` — OFF transition

**Location:** Line ~21450, in the `if (!enabled)` branch

**After the existing clear of `fs_autosave_writer_boot_ready` (line 21452),
add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** Disabling autosave invalidates all autosave state. A later re-enable
must re-validate.

### 2e. `filesystem_autosaveWriterCompleted()` — error path

**Location:** Line ~21764, in the `else` (error) branch

**After `fs_autosave_writer_armed = 1u;` (line 21767), add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** An error may indicate SD I/O failure; the on-card state is no longer
known. The next drain must re-validate both candidates.

### 2f. `filesystem_autosaveWriterSchedule_tick()` — no-Bank revocation

**Location:** Line ~21852, in the `!bank_hasResidentBank()` branch

**After the existing clear of `fs_autosave_writer_boot_ready` (line 21854),
add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** Bank-session loss makes the cached winner identity meaningless.

### 2g. `filesystem_autosaveWriterSchedule_tick()` — OFF fall-through

**Location:** Line ~21831, in the `if (!fs_autosave_enabled)` branch

**After `fs_autosave_writer_boot_ready = 0u;` (line 21831), add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** Same as 2d — defensive clear on the scheduler's own policy path.

### 2h. `filesystem_autosaveSetupCompleted()` — setup failure

**Location:** Line ~21797, in the failure branch

**After `fs_autosave_writer_armed = 0u;` (line 21797), add:**
```c
fs_autosave_winner_cached = 0u;
```

**Why:** Setup failure means the pair may not be in a known state.

**Total change 2 sites: 8 one-line insertions.**

---

## Change 3: Populate Winner Cache on Successful Continuation

**File:** `Core/Hardware/SD/filesystem.c`
**Location:** `filesystem_autosaveWriterCompleted()`, line ~21755, in the
`if (status == FS_STATUS_DONE)` branch

**What:** After a successful drain with remaining dirty bits (the continuation
path), populate the winner cache with the identity of the newly committed
target. Also clear the cache when the drain completes cleanly (no remaining
dirty bits) since no continuation will follow.

**Why:** The newly committed target is at index `winner_index ^ 1` with
generation `winner_generation + 1` and probe `winner_probe + 1`. These values
are still live in `op_autosave_writer` at this point — the completion
callback fires before the workspace is reused.

**Inputs:** `op_autosave_writer.winner_index`, `.winner_generation`,
`.winner_probe` — the values from the just-completed drain.
**Outputs:** The four cache statics, ready for the next drain's phase 0.
**Affiliates:** Phase 0 (Change 4), all invalidation sites (Change 2).

**Replace the existing DONE branch (lines 21755–21763) with:**
```c
    if (status == FS_STATUS_DONE) {
        fs_autosave_recovery_pending = 0u;
        if (autosave_maskHasDirty()) {
            /*
             * Cache the committed target as the next continuation's winner.
             *
             * What: records the newly published target's index, generation,
             * and probe so the next drain cycle can skip dual-record CRC
             * validation (phases 1–5) and on-card mask re-read (phases 50–55).
             *
             * Why: the writer just committed this target and knows its exact
             * identity. Re-validating it 250 ms later via full CRC streaming
             * costs ~544 ticks and four file open/close cycles — the dominant
             * per-cycle expense — with no information gain while the card
             * remains inserted.
             *
             * Inputs: op_autosave_writer fields from the just-completed drain.
             * Outputs: four statics that survive workspace zeroing. The cached
             * winner_index is the TARGET (winner ^ 1), not the source.
             * Affiliates: drain phase 0, all lifecycle invalidation sites.
             */
            fs_autosave_cached_winner_index =
                (uint8_t)(op_autosave_writer.winner_index ^ 1u);
            fs_autosave_cached_winner_generation =
                op_autosave_writer.winner_generation + 1u;
            fs_autosave_cached_winner_probe =
                (uint8_t)(op_autosave_writer.winner_probe + 1u);
            fs_autosave_winner_cached = 1u;
            fs_autosave_next_due_tick = (uint16_t)(
                time_sysTick + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS);
            fs_autosave_writer_armed = 1u;
        } else {
            fs_autosave_winner_cached = 0u;
            fs_autosave_writer_armed = 0u;
        }
    }
```

---

## Change 4: Phase 0 — Skip Validation on Cached Continuation

**File:** `Core/Hardware/SD/filesystem.c`
**Location:** `filesystem_autosaveParameterDrain_tick()`, case 0 (line ~6362)

**What:** After zeroing the workspace and parameter cache (unchanged), check
`fs_autosave_winner_cached`. If set, restore the cached winner identity into
the workspace, clear the cache flag, and jump directly to the mask
classification phase (56) rather than to validation (phase 1). Also skip the
on-card mask re-read (phases 50–55) because the canonical SRAM mask already
contains all dirty bits — the on-card mask is only useful for recovering
interrupted work from a previous power cycle.

**Why:** This is the core speedup. Continuation cycles save ~544 CRC ticks
plus 4 file open/close cycles (validation) plus ~8 ticks plus 1 file
open/close cycle (mask re-read).

**Inputs:** `fs_autosave_winner_cached` and the three cached value statics.
**Outputs:** Workspace fields `winner_index`, `winner_generation`,
`winner_probe`, `have_winner`, `winner_bank_match` are populated.
`fs_autosave_winner_cached` is cleared (consumed — prevents double-use if
the drain errors and retries).
**Affiliates:** Change 3 (sets the cache), Change 2 (invalidates it).

**Replace the existing case 0 (lines 6363–6378) with:**
```c
    case 0: /* INITIALIZE ONE OPERATION-LOCAL A/B VALIDATION PASS */
        /*
         * Reset operation progress and transaction-local patches only.
         *
         * Inputs: a newly accepted private filesystem operation. Outputs: no
         * stale patch offset or captured value can leak from a prior
         * generation. Autosave.c's canonical mask is deliberately untouched:
         * starting a filesystem transaction is not a dirty-record reset. The
         * name cache also remains untouched by this ordinary path.
         */
        memset(&op_autosave_writer, 0, sizeof(op_autosave_writer));
        memset(&fs_autosave_parameter_cache, 0,
               sizeof(fs_autosave_parameter_cache));
        if (fs_autosave_winner_cached) {
            /*
             * Continuation bypass: skip dual-record CRC validation and
             * on-card mask re-read.
             *
             * What: restores the cached winner identity from the previous
             * successful drain and jumps directly to mask classification
             * (phase 56), skipping phases 1–5 (validation) and 50–55
             * (mask re-read).
             *
             * Why: the writer itself committed this target 250 ms ago and
             * the card has not been removed (not a product contract). The
             * canonical SRAM mask already contains all dirty bits — the
             * on-card mask only recovers interrupted work from a prior boot.
             *
             * Inputs: four cached statics set by the previous drain's
             * completion callback. Outputs: workspace fields populated as
             * though validation selected this winner. Cache flag consumed
             * to prevent double-use on error retry.
             *
             * Affiliates: filesystem_autosaveWriterCompleted() (populates
             * cache), all lifecycle invalidation sites (clear cache).
             */
            op_autosave_writer.winner_index =
                fs_autosave_cached_winner_index;
            op_autosave_writer.winner_generation =
                fs_autosave_cached_winner_generation;
            op_autosave_writer.winner_probe =
                fs_autosave_cached_winner_probe;
            op_autosave_writer.have_winner = 1u;
            op_autosave_writer.winner_bank_match = 1u;
            fs_autosave_winner_cached = 0u;
            /*
             * VALIDATED trace for the cached path uses the same stage as
             * the full validation path, so trace readers do not need a
             * separate stage. flags bit 3 distinguishes the cached path.
             */
            autosaveTrace_record(
                AUTOSAVE_TRACE_STAGE_VALIDATED,
                (uint8_t)(1u |
                          (op_autosave_writer.winner_index << 1u) |
                          8u),
                op_autosave_writer.winner_generation);
            op_autosave_writer.payload_scan_offset = 0u;
            op_autosave_writer.patch_count = 0u;
            op_phase = 56u;
        } else {
            op_autosave_writer.candidate_index = 0u;
            op_phase = 1u;
        }
        return;
```

**Key invariant:** `fs_autosave_winner_cached` is consumed (cleared) here.
If this drain errors, the next retry will take the full validation path
(cache is already 0). The cache is only repopulated on the next successful
DONE + dirty completion.

---

## Change 5: Combine CRC and Commit Byte Reopens

### Current flow (6 phases, 2 open/close/sync cycles):

```
Phase 57: close CRC handle
Phase 58: wait close
Phase 59: sync (make CRC durable)
Phase 60: reopen target for commit byte
Phase 61: wait open
Phase 62: seek to commit offset
Phase 63: wait seek
Phase 64: write commit byte
Phase 65: close commit handle
Phase 22: wait close → filesystem_finish()
```

### New flow (fewer phases, 1 open/close/sync cycle):

After writing CRC at offset 12 (phase 21), instead of closing, seek to the
commit byte offset (5) and write it in the same open session, then close and
sync once.

**Why:** The commit-last contract is preserved because neither the CRC nor
the commit byte is durable until the single final sync. The CRC is written
first, the commit byte second, and both become durable together. A power loss
before the sync leaves the commit byte at zero (invalid), which is the
correct failure mode — identical to the current two-sync approach.

**The key insight:** The current two-sync design (sync after CRC, then
separately sync after commit) provides an extra guarantee: CRC is durable
before commit. But this only matters if there could be a power loss between
the CRC sync and the commit sync that somehow leaves the commit byte at A5
without the CRC being durable. Since both are in the same 512-byte sector
(offsets 5 and 12 are both in the first sector), the AsyncFATFS cache will
write them together anyway. The extra sync is pure overhead.

### 5a. Modify phase 21 exit

**File:** `Core/Hardware/SD/filesystem.c`
**Location:** Phase 21 (line ~7013), after CRC write completes

**Currently at line 7013–7015:**
```c
        if (op_autosave_writer.chunk_written < 4u)
            return;
        op_phase = 57u;
```

**Replace the exit transition with:**
```c
        if (op_autosave_writer.chunk_written < 4u)
            return;
        /*
         * CRC written; proceed to commit seek in the same open session.
         *
         * What: instead of closing, syncing, and reopening the target for
         * the commit byte, reuse the current handle. Seek backward to the
         * commit byte offset and write it before the single final close
         * and sync.
         *
         * Why: the commit byte (offset 5) and CRC (offset 12) are in the
         * same 512-byte FAT sector, so they share a single cache flush.
         * Eliminating one open/close/sync cycle saves ~50–100 ms of SD
         * latency per drain. The commit-last contract holds because the
         * commit byte is not durable until the final sync after both writes.
         *
         * Inputs: op_file is the same "r+" handle used for CRC at offset 12.
         * Outputs: seek to offset 5, write one A5 byte, then close and sync.
         * Affiliates: phase 62 (commit seek), phase 64 (commit write),
         * phase 65 (close), phase 22 (final sync via filesystem_finish).
         */
        op_phase = 62u;
```

### 5b. Remove phases 57–61

**Location:** Phases 57, 58, 59, 60, 61 (lines ~7018–7068)

**What:** Delete these five cases entirely. They are:
- Phase 57: close CRC handle (no longer needed — handle stays open)
- Phase 58: wait CRC close
- Phase 59: sync CRC to card (no longer needed — single sync at end)
- Phase 60: reopen target for commit byte (no longer needed — same handle)
- Phase 61: wait commit-byte open

**Why:** The combined flow reuses the handle from the CRC write. The seek
and write phases (62–64) remain unchanged — they already handle the commit
byte write. Phase 65 (close) and 22 (final sync via `filesystem_finish()`)
remain unchanged.

**Verify no other code references these phase numbers:** Search for
`op_phase = 57u`, `op_phase = 58u`, `op_phase = 59u`, `op_phase = 60u`,
`op_phase = 61u` in `filesystem_autosaveParameterDrain_tick()`. These
transitions exist only within the phases being removed, so no external code
reaches them.

### 5c. Verify phase 62–65 and 22 remain unchanged

Phases 62–65 and 22 need **no modification**. They already:
- Phase 62: seek to `AUTOSAVE_HEADER_COMMIT_OFFSET` (5)
- Phase 63: wait seek (ftell verification)
- Phase 64: write one `AUTOSAVE_HEADER_COMMIT_VALID` (A5) byte
- Phase 65: close handle via `afatfs_fclose(op_file, on_file_closed)` →
  phase 22
- Phase 22: wait close, trace PUBLISHED, hand to `filesystem_finish()` for
  final sync

The `op_file` handle is the same one used for CRC write — no reassignment
needed since we no longer close and reopen between CRC and commit.

---

## Change Summary Table

| # | Location | Lines | Action |
|---|---|---|---|
| 1 | `filesystem.c` after line 1789 | +16 | Add 4 winner-cache statics |
| 2a | `on_fatfs_card_init_failure()` ~21274 | +1 | Invalidate cache |
| 2b | `filesystem_initAfterCardReady()` ~21337 | +1 | Invalidate cache |
| 2c | `filesystem_ensureAutosaveFilesBlocking()` ~22976 | +1 | Invalidate cache |
| 2d | `filesystem_setAutosaveEnabled()` ~21452 | +1 | Invalidate cache |
| 2e | `filesystem_autosaveWriterCompleted()` ~21767 | +1 | Invalidate cache on error |
| 2f | `autosaveWriterSchedule_tick()` ~21854 | +1 | Invalidate on no-Bank |
| 2g | `autosaveWriterSchedule_tick()` ~21831 | +1 | Invalidate on OFF |
| 2h | `autosaveSetupCompleted()` ~21797 | +1 | Invalidate on setup failure |
| 3 | `autosaveWriterCompleted()` DONE branch ~21755–21763 | ~30 replace | Populate cache on continuation |
| 4 | Drain phase 0 ~6362–6378 | ~40 replace | Skip validation when cached |
| 5a | Phase 21 exit ~7013–7015 | ~3 replace | Redirect to phase 62 |
| 5b | Phases 57–61 ~7018–7068 | ~50 delete | Remove intermediate close/sync/reopen |

---

## Expected Output / Verification

### Behavioral verification (on hardware):

1. **First drain after boot:** Full validation path (phases 0→1→2→3→4→5→
   50→...→65→22). Trace shows VALIDATED with flags bit 3 = 0. Full CRC work,
   full mask re-read. ~965 ticks bounded work.

2. **Continuation drain (DONE + dirty):** Cached path (phases 0→56→10→...→
   21→62→63→64→65→22). Trace shows VALIDATED with flags bit 3 = 1. No CRC
   validation, no mask re-read, one fewer reopen. ~393 ticks bounded work.

3. **Error then retry:** Cache cleared on error. Next drain takes full
   validation path. Verify by inducing SD error mid-drain.

4. **Bank Load during autosave session:** Bank-session transition triggers
   `autosave_markResidentBankDirty()`. The scheduler's `!bank_hasResidentBank()`
   check clears the cache if Bank is momentarily lost. The next drain after
   re-arming takes the full validation path with Bank-mismatch seeding.

5. **AutoSave OFF/ON toggle:** OFF clears cache (Change 2d/2g). Re-enable
   goes through setup, which clears cache (Change 2h on failure, or the boot
   path clears it via 2c). Next drain validates fully.

6. **Load/Save page suppression:** Armed writer waits, gets continuation
   cadence on page exit (existing behavior). Cache validity is unaffected by
   page suppression — the cached winner is still valid if the writer was
   merely delayed, not errored.

### Trace verification:

- VALIDATED stage flags bit 3 distinguishes cached (1) from full (0) path
- MASK_MERGED stage is absent on cached continuations (phases 50–55 skipped)
- PUBLISHED stage is unchanged
- TERMINAL stage is unchanged

### SRAM impact:

| Item | Bytes |
|---|---|
| `fs_autosave_winner_cached` | 1 |
| `fs_autosave_cached_winner_index` | 1 |
| `fs_autosave_cached_winner_generation` | 4 |
| `fs_autosave_cached_winner_probe` | 1 |
| **Total new static SRAM** | **7 bytes** |

Well under the 100-byte pre-approval threshold.

### Phase removal impact:

Removing phases 57–61 removes ~50 lines of code. No other state machine uses
these phase numbers (they are local to `filesystem_autosaveParameterDrain_tick()`
and use a flat switch — phase numbers are not shared across operations).

---

## Risks and Mitigations

| Risk | Mitigation |
|---|---|
| Cached winner identity wrong after undetected SD swap | Not in product contract. Boot validation catches on next power cycle. |
| Cache consumed but drain errors before commit | Cache cleared at consumption (phase 0). Next retry validates fully. |
| Race between phase 0 cache restore and timer-side dirty marking | No race: dirty marking touches only Autosave.c's canonical mask (volatile), not the winner cache. The cache describes file identity, not mask state. |
| Phase 62 seek fails on combined CRC+commit flow | Seek failure already routes through `filesystem_autosaveWriterFinishError()` — unchanged. |
| CRC and commit in same sector assumption | Both offsets (5 and 12) are in the first 512 bytes of the record. AsyncFATFS sector size is 512 bytes minimum. Even if they were in different sectors, the single final sync in `filesystem_finish()` makes both durable atomically — the commit-last contract holds because commit zero was written during the transformed copy. |

---

## Implementation Order

1. Add the four statics (Change 1)
2. Add all eight invalidation sites (Change 2a–2h) — these are safe no-ops
   before the cache is ever populated
3. Modify the completion callback to populate the cache (Change 3)
4. Modify phase 0 to use the cache (Change 4)
5. Combine CRC+commit reopens (Change 5a, 5b)
6. Build, verify on hardware

Changes 1–4 are the winner-cache speedup (validation + mask skip).
Change 5 is the reopen-combining speedup (independent, can be done separately).
Both can ship together.

---

## Post-Implementation Review

**Reviewed:** 2026-09-01, against `git diff HEAD -- Core/Hardware/SD/filesystem.c`
(+192 / −60 lines, image shrank 176 bytes to 385,664).

### Change-by-change verification

| Change | Recipe | Implementation | Verdict |
|---|---|---|---|
| 1 — statics | 4 statics after line 1789 | 4 statics after line 1789, identical types/names/inits. Comment adds a `Lifetime:` paragraph explaining why external statics are needed. | Match ✅ |
| 2 — invalidations | 8 sites | **9 sites.** 8 recipe sites + 1 additional: the `!fs_autosave_enabled` branch inside `filesystem_autosaveWriterCompleted()` (OFF-during-active-transaction path at ~21808). This is correct — the recipe's 2d/2g cover the scheduler and policy-setter OFF paths, but the completion callback also has a terminal OFF path that the recipe missed. | Improved ✅ |
| 3 — cache population | Populate on DONE+dirty, clear on DONE+clean | Adds a `have_winner` guard: only populates cache when the drain used the normal copy-forward path (phases 10–22). Clears cache when `!have_winner` (recovery rebuilt both records from HCNAMES, phases 30–39). Also clears on error. | Improved ✅ |
| 4 — phase 0 bypass | Skip to phase 56 when cached | Matches recipe. Also adds an else-branch comment explaining the non-cached fallback. | Match ✅ |
| 5a — phase 21 exit | `op_phase = 62u` | Matches recipe. Replacement comment documents the sector-sharing rationale. | Match ✅ |
| 5b — delete phases 57–61 | Remove 5 cases | All 5 removed. `grep` confirms zero remaining references to `op_phase = 57u..61u`. | Match ✅ |

### Recovery guard (deviation from recipe)

The recipe's Change 3 unconditionally populated the cache on DONE+dirty. The
implementation wraps population in `if (op_autosave_writer.have_winner)`. This
is necessary because:

- Phases 30–39 (no-valid-record recovery) rebuild both records from HCNAMES
  baseline data without selecting a source winner. `have_winner` remains 0.
- Without the guard, the completion callback would read `winner_index` (zeroed
  at phase 0, never set by recovery), `winner_generation` (also zeroed), and
  `winner_probe` (zeroed) — producing a cached tuple `{index=1, gen=1,
  probe=1}` that was never validated. The next continuation would skip
  validation and trust this synthetic identity.
- With the guard, a recovery-followed-by-continuation takes the full
  validation path, which is correct: the recovery's output records have never
  been CRC-validated as a pair.

### Workspace lifetime verification

The completion callback reads `op_autosave_writer.have_winner`,
`.winner_index`, `.winner_generation`, and `.winner_probe`. These must still
be valid when the callback fires. Verified:

1. Phase 22 calls `filesystem_finish(FS_STATUS_DONE)`.
2. `filesystem_finish()` transitions to `FS_INTERNAL_OP_FLUSH_FINISH`.
3. `filesystem_flushFinish_tick()` pumps `afatfs_sync()`, promotes HCNAMES
   mirror, and triggers optional index rebuild — **none of which access
   `fs_stage_workspace`**.
4. On sync completion, it calls `filesystem_complete(op_flush_final_status)`.
5. `filesystem_complete()` calls `completion_callback`, which is
   `filesystem_autosaveWriterCompleted()`.

The stage workspace union is not reused between phase 22 and the completion
callback. This is consistent with existing pre-change code that reads
`op_autosave_writer.winner_index` and `.winner_generation` in the PUBLISHED
trace at phase 22 (line ~7135).

### Combined CRC+commit sector analysis

Both writes target the first 512-byte sector of the record file:
- CRC at offset 12–15 (4 bytes)
- Commit at offset 5 (1 byte)

AsyncFATFS sectors are 512 bytes minimum. Both writes hit the same cached
sector. The single final sync in `filesystem_finish()` makes both durable
atomically. The commit-last invariant holds: commit zero was written during
the transformed copy (phase 13, line ~6876), so a power loss before the final
sync leaves the record invalid.

### No issues found.

The implementation is faithful to the recipe with two improvements: one
additional invalidation site (OFF-during-active-transaction) and the recovery
guard. Both are correct and close gaps the recipe did not address. Build
output is clean. No code changes outside `filesystem.c`.

---

## Boot Failure — SD_CARD_PHASE_A Diagnosis

**Symptom:** Bank failed to load at boot on the Phase A firmware.

**Root cause:** Not a Phase A regression. The SD card's root `/.hcnames` was
written by a prior firmware version using a 4-column row format:

```
Full\t050\t-\t0\n
```

The current `filesystem_cacheResidentRecord()` (line 5356) rejected any row
containing more than one tab, treating it as malformed. This rejection is
fatal because the Bank load path reads root `/.hcnames` as a preload
(phases 79–82 in `filesystem_loadBankDirectory_tick()`, line 11604→11655).
A single rejected row triggers `op_close_status = FS_STATUS_ERROR`
(line 11658) which causes `filesystem_finish(FS_STATUS_ERROR)` at line 11681,
aborting the Bank load entirely.

**Evidence from SD_CARD_PHASE_A:**

| File | Observation |
|---|---|
| `settings.cfg` | `active_bank=51`, `autosave=1` — normal |
| `Bank/051 Full0/` | 16 scenes, `bankset.bcg` well-formed — Bank itself is healthy |
| `/.hcnames` | 1,850 bytes, 129 rows, 4-column format (`name\tsource\t-\tflag`) |
| `Bank/051 Full0/.hcnames` | Same 4-column format |
| `.hcprms1` / `.hcprms2` | 35,026 bytes each (vs expected 34,768), version 0x02 (vs expected 0x01) |

All of these indicate the prior firmware used an extended `.hcnames` format
(4 columns with an extra dash and flag field) and autosave record format
(version 2, 258 bytes larger). The Phase A firmware expects 2-column
`.hcnames` and version 1 / 34,768-byte records. The `.hcnames` mismatch is
what kills the boot.

**Fix applied:** Modified `filesystem_cacheResidentRecord()` to isolate
the source token at the second tab boundary instead of rejecting the entire
row. Extra columns after the source token are silently discarded. The parser
now accepts both the current 2-column format (`name\tsource`) and the prior
4-column format (`name\tsource\t...\t...`).

**Build:** text=385,276, image=385,680 bytes. +32 bytes over the Phase A
build (the 8-byte `source_buf` and loop add negligible code).

**Remaining `.hcprms` mismatch:** The version 2 / 35,026-byte `.hcprms` files
will fail CRC validation at the first runtime drain (the streaming reader reads
more bytes than `AUTOSAVE_RECORD_BYTES`, detecting an overlong record). This
triggers the recovery path (phases 30–39), which recreates both records from
HCNAMES as version 1 / 34,768-byte baselines. This is self-healing and does
not require a separate fix — the recovery path already handles invalid records
gracefully.
