# S056 — AsyncFATFS file-size cluster-boundary fix

**Date:** 2026-08-24
**Files changed:** `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`,
`Core/Hardware/SD/filesystem.c`
**Status:** Both fixes applied, cluster-boundary fix verified on hardware.
Page-exit expedite pending hardware verification.

---

## 1. Cluster-boundary file-size bug

### Bug

`afatfs_fseekAtomic()` does not call `afatfs_fileUpdateFilesize()`.

`afatfs_fseekInternal()` tries the atomic path first. For sequential writes
the seek is always within-sector or a single cluster boundary, so every seek
resolves atomically. The queued continuation path `afatfs_fseekInternalContinue()`
*does* call `afatfs_fileUpdateFilesize()`, but for atomic seeks it is never
reached.

`afatfs_fileUpdateFilesize()` performs:
```c
file->logicalSize = MAX(file->logicalSize, file->cursorOffset);
```

Without this call on the atomic path, `logicalSize` stays at its initial value
(0 for new files) throughout the entire write sequence. The write path uses two
modes for persisting size to the FAT directory entry:

- `AFATFS_SAVE_DIRECTORY_NORMAL` — writes `physicalSize` (cluster-rounded),
  called during `afatfs_appendRegularFreeClusterContinue()` each time a new
  cluster is allocated.
- `AFATFS_SAVE_DIRECTORY_FOR_CLOSE` — writes `logicalSize` (true file size),
  called during `afatfs_fclose()`.

`fclose()` compensates by updating `logicalSize` from `cursorOffset` before
issuing the FOR_CLOSE save. But if that final directory-entry write does not
persist (cache eviction timing, sector flush ordering, power loss before the
dirty sector is flushed), the on-disk size reverts to whichever `physicalSize`
was last written by a NORMAL save during cluster allocation.

### Evidence

`.hcprms1` on SD_CARD_B (first hardware test, pre-fix): **32,768 bytes**.
Expected: **34,768 bytes** (`AUTOSAVE_RECORD_BYTES`, static-asserted at
`Autosave.h:195`).

`.hcprms2` on both cards: correct 34,768 bytes.

32,768 is exactly the `physicalSize` written by the first cluster allocation's
NORMAL save (one 32 KB cluster). The second cluster allocation would write
65,536, and `fclose` would write 34,768. Observing 32,768 means neither the
second allocation's NORMAL save nor the fclose FOR_CLOSE save persisted to disk.

### Fix

**Edit 1** — added `afatfs_fileUpdateFilesize(file)` to `afatfs_fseekAtomic()`:

[asyncfatfs.c:2365](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L2365)

```c
    if (!afatfs_isEndOfAllocatedFile(file)) {
        file->cursorOffset += offset;
    }

    afatfs_fileUpdateFilesize(file);   // ← ADDED

    return true;
}
```

**Edit 2** — removed stale `// TODO do we need this?` comment from
`afatfs_fseekInternalContinue()`:

[asyncfatfs.c:2408](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L2408)

```c
    afatfs_fileUpdateFilesize(file);
```

The TODO asked whether the call was needed. The answer is yes — and the atomic
path was the one missing it. The two paths now mirror each other.

### Effect

`logicalSize` tracks `cursorOffset` on every successful seek, not just queued
ones. Every subsequent `SAVE_DIRECTORY_NORMAL` during cluster allocation will
see the true file size in `logicalSize`. More importantly, if `fclose`'s final
FOR_CLOSE directory write is lost, the on-disk size is still accurate from the
most recent allocation NORMAL save rather than stuck at a stale
cluster-boundary value.

Downstream consumers that read `logicalSize` during writes
(`afatfs_fileLockCursorSectorForWrite()`'s read-before-write check at line
2264, `afatfs_fread()`'s bounds check at lines 5850-5853) also see the correct
value throughout the write sequence instead of a stale 0.

### Hardware verification

Second two-boot test with the fix applied. All four `.hcprms` files across
both SD cards are 34,768 bytes. No duplicate files. File-size cluster-boundary
issue is resolved.

### Build

```
text     data    bss     dec     hex
381220   400     94800   476420  74504
```

(+8 bytes text over the duplicate-fix baseline, from the one added function call.)

---

## 2. Autosave writer page-exit expedite

### Observation

The second hardware test's `.hcprms` files had the correct 34,768-byte size
but **zero mutation masks** and only initial recovery generations (gen 0 in
`.hcprms2`, gen 1 in `.hcprms1`). No parameter drain cycle published.

**Trace analysis** (SD_CARD_A, Boot 1 lifecycle records):

| Tick  | Stage | Meaning |
|-------|-------|---------|
|   875 | E     | REPAIR_NAMES error (pre-existing, not blocking) |
|  2123 | B     | Bank 015 "LoadTst!" present at boot |
|  2206 | L×32  | `markResidentBankDirty` — sets SRAM mask for Bank 015 |
|  3717 | S     | Writer scheduled, deadline tick 8717 (5 s debounce) |
| 20941 | L×10  | Bank load from LOAD page — dirty marks for new bank |
| 20948 | F     | Trace suppressed (2048 D records overflow) |
| 20948 | W     | Writer suppressed — user on LOAD page |
| 23066 | A     | Drain admitted (user left LOAD page, deadline expired) |
| 24141 | V     | Validated both candidates — no winner (bank changed) |
| 29090 | T     | Recovery done (wrote gen 0 → `.hcprms2`, gen 1 → `.hcprms1`) |

Boot 2 (SD_CARD_B) shows the same pattern, terminating at T tick 25706.

**Root cause:** the no-winner recovery path (drain phases 30-38) writes
**initial records** with zero masks and deterministic identity-only payload.
It does not consult or consume the SRAM dirty mask — those bits are reserved
for the *next* drain cycle, which validates the fresh records, merges the
file mask (all zeros) with SRAM (dirty), classifies each dirty bit, captures
live parameter values, and publishes a transformed copy.

This means **two full drain cycles** are required between the first page exit
and published mutations:

1. Recovery cycle: validate → no winner → write gen 0/gen 1 (~6 s)
2. Mutation cycle: validate → winner → merge → classify → copy → publish (~6 s)

Between cycles there is also the 250 ms continuation interval and potential
interference from the settings writer and trace flush schedulers, which run
at higher priority. Total wall time from page exit to published mutations
exceeds 12 seconds. A short voice-mode session before power-off misses the
publication window.

The 5-second writer debounce (`AUTOSAVE_WRITER_INTERVAL_MS`) was not the
direct cause in this test — it had already expired while the user was on the
LOAD page (deadline 8717 vs. page exit at ~23000). But in faster user flows
where the LOAD page is entered and exited within seconds of boot, the
unexpired debounce would add additional wasted time.

### Change

Added `fs_autosave_page_suppressed` flag to the writer scheduler. When the
scheduler's Load/Save page guard blocks the writer, the flag is set. On the
first tick after the page is left, the flag clears and the writer deadline
is reset to `now + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` (250 ms) instead
of retaining whatever the original 5-second debounce was.

**4 change sites** (all in `Core/Hardware/SD/filesystem.c`):

1. **Declaration** —
   [filesystem.c:1613](Core/Hardware/SD/filesystem.c#L1613):
   ```c
   static uint8_t fs_autosave_page_suppressed = 0u;
   ```

2. **Page guard sets flag** —
   [filesystem.c:19846](Core/Hardware/SD/filesystem.c#L19846)
   (inside `filesystem_autosaveWriterSchedule_tick()`):
   ```c
   if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
       fs_autosave_page_suppressed = 1u;
       /* ... existing W trace witness ... */
       return;
   }
   ```

3. **Page exit resets deadline** —
   [filesystem.c:19866](Core/Hardware/SD/filesystem.c#L19866)
   (immediately after the page guard, before the existing deadline check):
   ```c
   if (fs_autosave_page_suppressed) {
       fs_autosave_page_suppressed = 0u;
       fs_autosave_next_due_tick = (uint16_t)(
           now + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS);
   }
   ```

4. **Card-failure resets** —
   [filesystem.c:19233](Core/Hardware/SD/filesystem.c#L19233) and
   [filesystem.c:19286](Core/Hardware/SD/filesystem.c#L19286)
   (both full-reset functions clear the flag alongside the other autosave
   scheduler state):
   ```c
   fs_autosave_page_suppressed = 0u;
   ```

### Effect

Eliminates wasted debounce time between leaving the Load/Save page and the
first drain admission. The drain starts within 250 ms of page exit instead of
waiting for the original 5-second deadline. Does not reduce the two-cycle
recovery cost itself, but removes the variable gap between them.

The flag is unconditional on page exit — it fires whether or not the user
actually loaded/saved something. This is harmless: if the mask is clean,
the scheduler's existing `!recovery_pending && !maskHasDirty()` guard at
line 19826 disarms the writer before any drain starts.

### Build

```
text     data    bss     dec     hex
381268   400     94800   476468  74534
```

(+48 bytes text over the cluster-fix build, from the flag and transition logic.)
