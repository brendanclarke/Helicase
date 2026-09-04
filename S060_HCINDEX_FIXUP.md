# S060 — .hcindex Fixup Plan

## Problem Statement

Only `Instrument/Drum/.hcindex` is generated at boot. The remaining three
Instrument type directories (`Snare/`, `Cymbal/`, `HiHat/`) consistently
lack `.hcindex` across all five SD card captures, even though all four
directories exist and contain instrument files.

Kit, Scene, and Bank root `.hcindex` files are present and correct in all
captures — only the Instrument per-type indexes are affected.

## Policy (from user specification)

1. **Boot**: all `.hcindex` files must be checked and regenerated at boot.
2. **Runtime**: a runtime path may only rewrite `.hcindex` when the names
   of that library's contents may have changed. Current triggers are save
   and invalidation; future triggers that change library content names
   are equally legitimate.

---

## Boot Name Truncation

The boot .hcindex pipeline enforces 8-character instrument filename stems
at two layers, both keyed on `STORAGE_KIT_DISPLAY_NAME_LEN` (8):

### Layer 1 — On-disk rename (repair step)

`filesystem_repairInstrumentNamesBlocking()` (filesystem.c:24136) runs
*before* the scan/index loop. For each type directory it enumerates every
file, extracts the first 8 characters before the extension via
`filesystem_copyInstrumentStemDisplay()` (filesystem.c:9048), builds a
canonical `<stem>.<ext>` filename via
`filesystem_makeCanonicalInstrumentName()` (filesystem.c:9488), and if the
physical filename doesn't match byte-for-byte, **renames the file on the
SD card** via `afatfs_renameObject_lfn()` (filesystem.c:8241). Collisions
are resolved with a suffixed stem (`op_repair_suffix`).

Example: a host-created `LongInstrumentName.drm` is renamed to
`LongInst.drm` on disk before the scan ever runs.

### Layer 2 — Cache insertion (scan step)

`filesystem_recordInstrumentFile()` (filesystem.c:9335) calls
`filesystem_copyInstrumentStemDisplay()` (filesystem.c:9379) which copies
at most 8 characters before the dot:

```c
while (i < STORAGE_KIT_DISPLAY_NAME_LEN &&
       filename[i] != '\0' &&
       filename[i] != '.') {
    stem[i] = filename[i];
    i++;
}
```

Then `storage_copyDisplayName()` pads to exactly 8 characters. The index
writer (`createBootIndex_tick` phase 11) writes these 8-character cached
stems to `.hcindex` via `filesystem_cachedInstrumentName()`.

Layer 1 normalizes the disk first; layer 2 truncates again as a safety net.

---

## Root Cause Analysis

### Boot indexer architecture

`filesystem_createBootIndexBlocking()` (filesystem.c:23830-23912) is a
synchronous wrapper that iterates all 4 registry types in order:

```
for i = 0..3:
    1. SCAN_INSTRUMENTS  → populate cache for type[i]
    2. CREATE_BOOT_INDEX → write .hcindex from cache for type[i]
    if either step returns non-DONE → return 0 (bail)
```

Called from `main.c:727` with `(void)` — the return value is discarded,
so a failure after the first type is completely silent.

### Why Drum succeeds and later types fail

Registry order: Drum (0), Snare (1), Cymbal (2), HiHat (3).

The Drum iteration completes both scan and index-write successfully.
The second iteration (Snare) then fails, causing the wrapper to `return 0`
and skip all remaining types. Since the return value is cast to `(void)`,
no error is observable.

### Candidate failure mechanisms — code-level assessment

#### Candidate 1: Shared-variable carryover between iterations

**Assessment: MOSTLY ELIMINATED.**

`filesystem_start()` (filesystem.c:23686) performs a comprehensive reset of
all operation-local variables on every call: `op_phase=0`, `op_file=NULL`,
`op_file_ready=false`, `op_close_done=false`, `op_close_status=FS_STATUS_DONE`,
`op_bytes_done=0`, `op_item_offset=0`, plus ~20 more fields. The blocking
wrapper explicitly sets the per-iteration variables
(`op_instrument_scan_registry_index`, `op_instrument_index_type`,
`fs_list_cache_type`) and calls `filesystem_clearInstrumentCacheStorage()`
which resets `fs_list_cache_kind=FS_NAME_CACHE_NONE`, `fs_list_cache_type`,
and `fs_list_cache_count`.

`op_kit_root_dir` is NOT reset by `filesystem_start()`, but all normal
completion paths in both the scan tick (phase 11: `op_kit_root_dir = NULL`)
and index-write tick (phase 9: `op_kit_root_dir = NULL`) set it to NULL
before reaching their terminal phases. No stale shared variable could be
identified that would cause a failure.

#### Candidate 2: CWD state mismatch

**Assessment: MOSTLY ELIMINATED by asyncfatfs analysis.**

Both the scan and index-write tick functions return CWD to root via
`afatfs_chdir(NULL)` before calling `filesystem_finish()`. The
index-write uses `afatfs_chdirParent()` (phase 12) to go from the type
subdirectory back to `/Instrument/`, then `afatfs_chdir(NULL)` (phase 14)
to return to root. The next operation's phase 0 also calls
`afatfs_chdir(NULL)` redundantly.

**asyncfatfs analysis** (asyncfatfs.c:6091-6123): `afatfs_chdir(NULL)`
creates a completely fresh root directory handle from scratch —
`afatfs_initFileHandle()` zeroes the struct, then `firstCluster` is set
directly from `afatfs.rootDirectoryCluster` and the cursor is seeked to
0. No stale state from a previous operation can carry over. Similarly,
`afatfs_chdirParent()` (asyncfatfs.c:6141) reads the ".." FAT entry from
the current directory's first sector, producing a fresh handle.

CWD itself cannot be wrong after `afatfs_chdir(NULL)`. The subsequent
directory search (`afatfs_createFileInternal` → `afatfs_findFirst` +
`afatfs_findNext`) resets `currentDirectory`'s cursor to offset 0 and
iterates entries, reading sectors through the cache. Since the Drum
`.hcindex` write only modifies Drum-directory sectors, FAT sectors, and
`.hcindex` data sectors — never the root or `/Instrument/` directory
sectors — those sectors remain valid in cache (IN_SYNC after flush) and
would be correctly re-read if evicted.

**File handle pool** (5 handles): `afatfs_fcloseContinue()` (asyncfatfs.c:
5939) sets `file->type = AFATFS_FILE_TYPE_NONE` before invoking the close
callback, so handles are always freed before the next operation begins.
After Drum's cycle (scan open/close + index open/write/close), all
temporary handles are released.

**Remaining runtime-only scenarios**: Two edge cases cannot be verified by
static analysis alone: (a) FAT cluster-chain traversal may behave
differently on the second iteration if the Drum `.hcindex` allocation
extended or modified FAT table sectors that are later needed for the
Snare directory walk; (b) SD card-specific timing — if the sync gate
returns true before the card has fully committed all writes, a subsequent
read could see stale data. Both are low-probability but untestable
without runtime instrumentation.

#### Candidate 3: Case sensitivity divergence

**Assessment: RED HERRING in isolation.**

The scan tick opens `/Instrument/` with `AFATFS_MATCH_CASE_SENSITIVE`
(filesystem.c:19636), while the boot index tick opens it with
`AFATFS_MATCH_CASE_INSENSITIVE` (filesystem.c:3984). However, both succeed
for the first iteration (Drum), and nothing about the Drum write cycle
changes the `/Instrument/` directory entry's case representation on disk.

The case-sensitivity inconsistency is real and worth normalizing (all other
callers use `CASE_INSENSITIVE` for `/Instrument/`), but it cannot explain
why the second iteration fails while the first succeeds. It could, however,
be a contributing factor if asyncfatfs internally caches directory lookups
with match-mode sensitivity, where the alternating SENSITIVE→INSENSITIVE→
SENSITIVE pattern across operations confuses the cache. This remains
speculative without asyncfatfs internals.

#### Candidate 4: `filesystem_start()` precondition failure

**Assessment: ELIMINATED.**

`filesystem_start()` has exactly one precondition (filesystem.c:23691):
```c
if (status == FS_STATUS_BUSY) return false;
```

`filesystem_ack()` (filesystem.c:23675) unconditionally transitions
`status` from DONE/ERROR to IDLE. Since the blocking wrapper always calls
`filesystem_ack()` before the next `filesystem_start()`, the status is
always IDLE and the precondition is always met.

### Most likely root cause

**Static analysis at both filesystem.c and asyncfatfs.c levels has not
identified a clear bug.** All four candidate mechanisms have been
assessed through code-level investigation:

- Candidate 1 (shared-variable carryover): MOSTLY ELIMINATED
- Candidate 2 (CWD state mismatch): MOSTLY ELIMINATED
- Candidate 3 (case sensitivity): RED HERRING in isolation
- Candidate 4 (start() precondition): ELIMINATED

The asyncfatfs layer's CWD management, cache coherency, file handle
lifecycle, and directory search logic all appear correct under static
analysis. The two remaining plausible failure modes are runtime-only:
FAT table sector interactions between Drum's `.hcindex` allocation and
subsequent directory reads, and SD card timing edge cases where sync
returns before writes are fully committed.

#### Candidate 5: Boot logging sticky timeout (DEV_MODE_LOGGING builds)

**Assessment: ELIMINATED.**

`DEV_MODE_LOGGING` is 1 (config.h:88) with a 20-second per-operation
deadline (config.h:145). The sticky timeout mechanism exists (line 3094:
`return fs_boot_logging_timed_out` kills all subsequent ticks) but boot
completes well under 20 seconds total, so no individual operation can
exceed its deadline. The timeout cannot be the root cause.

#### Candidate 6: macOS AppleDouble files misclassified as instruments

**Assessment: CONFIRMED — root cause.**

macOS creates hidden `._<name>.<ext>` AppleDouble resource-fork files on
FAT volumes. These files carry valid instrument extensions (`.drm`,
`.snr`, `.cym`, `.hat`) and pass `instrumentManager_filenameMatchesType()`
because that function only checks suffix equality. However, their
display names start with `.`, which causes
`filesystem_copyInstrumentStemDisplay()` to produce an empty stem (the
copy loop stops at the first `.`).

The **repair step** (`filesystem_repairBuildCandidate()`, filesystem.c:
9615) has no unusable-stem guard: it builds the fallback canonical name
`inst.<ext>` for every `._` file, then tries to rename `._foo.drm` →
`inst.drm`. The first such rename succeeds, producing a 4096-byte file
of macOS metadata masquerading as an instrument. The second `._` file
produces the same canonical `inst.drm`, collides, and the repair fails.
When `filesystem_repairInstrumentNamesBlocking()` returns 0 (line
23853), the blocking wrapper bails before any scan or .hcindex write.

The **scan step** (`filesystem_recordInstrumentFile()`, filesystem.c:
9380) already calls `filesystem_instrumentStemIsUsable()` which rejects
empty stems. The repair was missing this guard.

Evidence:
- `SD_CARD_HCIDX/Instrument/Drum/inst.drm` is 4096 bytes (one FAT
  cluster of macOS AppleDouble metadata content: `com.apple.quarantine`,
  resource fork data)
- The directory contains 97 `._*.drm` files alongside 97 real `.drm`
  instrument files
- No instrument `.hcindex` files are generated (the repair failure
  kills the entire pipeline before any scan/index)
- Kit/Scene/Bank `.hcindex` files are unaffected (they run before the
  instrument pipeline)

### Fix

1. **Add unusable-stem guard to repair** (filesystem.c:9660): call
   `filesystem_instrumentStemIsUsable()` after extracting the stem,
   before building the canonical name. `._` files produce empty stems
   and are skipped — left untouched on the card, invisible to the
   browser.

2. **Inter-iteration settle barrier** (filesystem.c:23906): retained
   from the earlier investigation as defense-in-depth between
   scan/index iterations.

3. **Case sensitivity cleanup** (filesystem.c:19636): normalize the
   scan's `/Instrument/` open from CASE_SENSITIVE to CASE_INSENSITIVE.

---

## Implementation Plan

### A. Boot: ensure all .hcindex files are regenerated

#### A1. Add inter-iteration settle barrier (filesystem.c:23830)

Add an explicit asyncfatfs settle cycle between each iteration of the
blocking wrapper's loop, immediately after `filesystem_ack()` and
before the next `filesystem_start()`:

```c
/* Settle asyncfatfs between iterations so each type starts from
 * the same clean state Drum sees on the first iteration. */
afatfs_chdir(NULL);
while (!afatfs_sync()) {
    afatfs_poll();
}
```

This ensures:
- CWD is reset to root outside the state machine
- All dirty sectors from the previous .hcindex write are confirmed
  written (not just queued)
- The 8-sector cache is in a settled IN_SYNC state
- No in-flight DMA or pending file operations carry over
- The boot logging deadline cannot be inherited from a prior
  operation's flush

Each type's scan + index-write then starts from the same filesystem
state that Drum has on the very first iteration.

#### A2. Normalize case sensitivity (filesystem.c:19636)

Change the scan tick's root `/Instrument/` open from
`AFATFS_MATCH_CASE_SENSITIVE` to `AFATFS_MATCH_CASE_INSENSITIVE` to
match all other callers. This is a code-hygiene fix (Candidate 3) that
removes a gratuitous inconsistency — the scan is the only caller that
uses CASE_SENSITIVE for this path.

#### A3. Stop discarding the return value (main.c:727)

**Current**: `(void)filesystem_createBootIndexBlocking();`

**Change**: Check the return value and, at minimum, set an error flag or
log a boot error code so that a failed boot index is observable in
diagnostics. Since boot indexing runs before audio, a retry or fallback
is acceptable here.

#### A4. Runtime fallback remains (B5, no change)

The Load Menu recovery path (`filesystem_beginInstrumentIndexRecovery()`,
filesystem.c:3812) already regenerates a missing/corrupt `.hcindex` on
first Menu entry for that type. This provides a runtime safety net if
any boot .hcindex generation still fails after A1. No changes needed.

### B. Runtime: audit all .hcindex modification paths

The following paths currently modify `.hcindex` files. Each is triggered
by a save or by recovery from a missing/corrupt index (which is itself an
invalidation-equivalent event — the library's content names may have
changed). No additional runtime paths write .hcindex.

#### B1. Save-triggered Instrument .hcindex rebuild

- **Site**: filesystem.c:14120-14121 (Instrument Save terminal phase)
- **Trigger**: Successful Instrument Save
- **Mechanism**: Arms `op_library_index_rebuild_kind = FS_NAME_CACHE_INSTRUMENT`
  and `op_library_index_rebuild_pending = 1u`. After the save's final
  `filesystem_finish(DONE)` → flush → sync, `filesystem_flushFinish_tick()`
  (line 3945) picks up the pending rebuild and calls
  `filesystem_startLibraryIndexRebuild()` (line 3839) → one-type
  scan + index-write for the saved instrument's type.

  **Status**: Correct. Library content names may have changed.

#### B2. Save-triggered Kit .hcindex rebuild

- **Site**: filesystem.c:15846-15847 (Kit Save terminal phase)
- **Trigger**: Successful Kit Save
- **Mechanism**: Same pending-rebuild pattern as B1, kind = `FS_NAME_CACHE_KIT`.
  Triggers `filesystem_requestScanKits()` → `createLibraryIndex_tick()`
  for the `/Kit/.hcindex`.

  **Status**: Correct. Library content names may have changed.

#### B3. Save-triggered Bank .hcindex rebuild

- **Site**: filesystem.c:16787-16791 (Bank Save terminal phase)
- **Trigger**: Successful Bank Save
- **Mechanism**: Same pending-rebuild pattern, kind = `FS_NAME_CACHE_BANK`
  or `FS_NAME_CACHE_BANK_DIRECT_WRITE`. The direct-write path skips the
  scan when `filesystem_savedBankIndexCanWriteDirectly()` validates the
  retained cache.

  **Status**: Correct. Library content names may have changed.

#### B4. Save-triggered Scene .hcindex rebuild

- **Site**: filesystem.c:17370-17371 (Scene Save terminal phase)
- **Trigger**: Successful Scene Save
- **Mechanism**: Same pending-rebuild pattern, kind = `FS_NAME_CACHE_SCENE`.

  **Status**: Correct. Library content names may have changed.

#### B5. Runtime Instrument .hcindex recovery (invalidation equivalent)

- **Site**: filesystem.c:3812 `filesystem_beginInstrumentIndexRecovery()`
- **Trigger**: A `loadInstrumentIndex_tick()` (filesystem.c:4290)
  encounters a missing/corrupt/empty `.hcindex` during Menu's browser
  entry or voice/type/mode transition
- **Mechanism**: Transfers the accepted load request's callback into the
  rebuild chain, scans the one type's physical directory, and rewrites
  `.hcindex`.

  **Status**: Correct. The existing index is unusable — the library's
  authoritative content must be re-derived from the physical directory.

#### B6. Rebuild cancellation on save failure

- **Site**: filesystem.c:3610-3624 (`filesystem_finish()` non-DONE path)
- **Trigger**: A save operation fails before its rebuild runs
- **Mechanism**: Clears `op_library_index_rebuild_pending` and `kind` so
  a stale rebuild doesn't fire on a later unrelated successful operation.

  **Status**: Correct. Prevents stale rebuilds.

### C. No-change confirmation

The following paths do NOT modify `.hcindex` and should remain unchanged:

- **Load operations**: Kit/Scene/Bank/Instrument Load reads `.hcindex`
  but never writes it.
- **Autosave drain**: Writes `.hcnames` (the identity register), never
  `.hcindex`.
- **Instrument temp save/load**: `.hctmp` files explicitly bypass
  HCNAMES and `.hcindex` (documented at filesystem.h:893-898).
- **HCNAMES convergence pipeline**: Phases 70-76 in the drain state
  machine write `.hcnamtmp` → rename → `.hcnames`. No `.hcindex` contact.

---

### D. System-wide AppleDouble filter at asyncfatfs level

#### D1. Filter `._` prefixed files in `afatfs_findNextObject()` (asyncfatfs.c:2972)

**Applied.** Added a two-byte prefix check on the resolved display name
immediately after all three name-resolution paths (verified LFN,
malformed-LFN fallback, bare SFN) and before the function's final
return. When `object->id.displayName` starts with `._`, the finder
state is reset and the loop continues to the next raw entry — the same
skip pattern used for structural dot entries, deleted entries, LFN
fragments, and volume labels.

This makes macOS AppleDouble files invisible to every directory
consumer in the system: repair, scan, index, save, load, and any
future enumerator. The `._` two-character prefix is the canonical
AppleDouble signature; no product-owned dot-prefixed file uses it
(`.hcindex`, `.hcnames`, `.hcprms1`, `.hcprms2`, `.hctmp.*` all start
with `.hc`).

The repair-level guard (A1, filesystem.c:9660) is retained as
defense-in-depth — it catches unusable stems from any source, not only
`._` files.

---

## Summary of Required Changes

| # | File | Action | Description |
|---|------|--------|-------------|
| A1 | filesystem.c:9660 | DONE | Add unusable-stem guard to repair (root cause fix) |
| A2 | filesystem.c:23906 | DONE | Add settle barrier between scan/index iterations |
| A3 | filesystem.c:19636 | MODIFY | CASE_SENSITIVE → CASE_INSENSITIVE |
| A4 | main.c:727 | MODIFY | Check return value, log boot error on failure |
| A5 | filesystem.c:3812 | NO CHANGE | Load Menu recovery stays as runtime fallback |
| B1-B6 | (all sites above) | NO CHANGE | Audit confirms all existing triggers are correct |
| C | (all non-write paths) | NO CHANGE | Audit confirms no spurious .hcindex writes |
| D1 | asyncfatfs.c:2972 | DONE | System-wide `._` filter at lowest FS layer |

## Prerequisites

- D1 is the system-wide filter — makes `._` files invisible to all consumers
- A1 is the root cause fix at the repair level — retained as defense-in-depth
- A2 is the settle barrier — retained as defense-in-depth
- A3/A4 are independent improvements

## Test Procedure

1. Apply A1 fix, boot the device, pull SD card
2. Verify all 4 `.hcindex` files exist:
   `Instrument/Drum/.hcindex`, `Instrument/Snare/.hcindex`,
   `Instrument/Cymbal/.hcindex`, `Instrument/HiHat/.hcindex`
3. Verify contents match physical directory scan (each row = one
   instrument filename stem, newline-delimited, matching directory order)
4. Save one instrument of each type, pull card, verify the saved type's
   `.hcindex` was updated and the others are untouched
5. Delete one type's `.hcindex` from the card, boot, verify it was
   regenerated by the recovery path (B5) on first Menu entry for that type
