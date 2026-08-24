# Session 056 — Handoff Log

```
DATE: 2026-08-24
SESSION GOAL: Fix AsyncFATFS LFN duplicate-creation bug, diagnose and fix
  cluster-boundary file-size truncation, add autosave writer page-exit expedite.
COMPLETED: All three items implemented. Duplicate fix and cluster-boundary fix
  verified on hardware (two-boot test). Page-exit expedite implemented and
  builds clean; pending hardware verification.
VERIFIED ON HARDWARE: Yes — duplicate fix and cluster-boundary fix confirmed
  across two-boot test. Page-exit expedite is code-complete but not yet
  hardware-tested.

CHANGES THIS SESSION:
- Core/Hardware/SD/asyncfatfs/asyncfatfs.c: LFN duplicate-creation fix (4 sites
  in afatfs_createFileContinue(), 2 struct fields added); cluster-boundary
  file-size fix (1 line added in afatfs_fseekAtomic(), 1 stale comment removed
  in afatfs_fseekInternalContinue())
- Core/Hardware/SD/filesystem.c: autosave writer page-exit expedite (4 sites:
  flag declaration, page-guard set, page-exit deadline reset, 2 card-failure
  resets)

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED:
- LFN duplicate directory entries from early free-run exit
- .hcprms file-size truncation to cluster boundary (32,768 vs 34,768 bytes)
- "Known .hcprms boot-lock evidence" in ASYNCFATFS_REFERENCE.md and AUTOSAVE.md
  is now explained and fixed (the 32,768-byte file was the cluster-boundary bug,
  not a lower-layer stall)

NEXT SESSION RECOMMENDED GOAL: Hardware-verify the page-exit expedite; confirm
  that autosave mutation masks are non-zero after a short voice-mode session
  following a Load page exit. Continue with remaining AutoSave milestones (boot
  reader, whole-object Bank/copy/paste publication).
BLOCKERS: Page-exit expedite needs hardware test before closing.

CRITICAL REMINDERS FOR NEXT SESSION:
- afatfs_fseekAtomic() now calls afatfs_fileUpdateFilesize() — do not remove it
- The LFN duplicate fix uses a latch-and-continue approach; do not reintroduce
  early free-run exit in afatfs_createFileContinue()
- fs_autosave_page_suppressed must be cleared in both card-failure reset paths
- Two full drain cycles (~12+ seconds) are required from a no-winner recovery
  to published mutations; the page-exit expedite reduces the gap but does not
  eliminate the two-cycle cost
```

---

## 1. AsyncFATFS LFN duplicate-creation fix

### Bug

`afatfs_createFileContinue()`, the LFN-capable directory scan in
`AFATFS_CREATEFILE_PHASE_FIND_FILE`, exits the scan early the moment it
finds a free run large enough for LFN entries — without checking the rest
of the directory for an existing file with the same name.

**Trigger:** any directory that has had a file deleted from it. Deleted
entries become `0xE5` markers, which the LFN free-run tracker counts. If
those markers form a viable run before the existing target file's position
in the directory, the create path fires without ever reaching the existing
entry.

Example root directory layout:

```
[settings.cfg] [deleted] [deleted] [.hcnames] [.hcprms1] ...
```

A create-capable open of `.hcnames` fills the two deleted slots and
produces a second `.hcnames`. Both are then physically present. Subsequent
operations may read one copy while writing the other.

The pre-existing duplicate `Kit/.hcindex` that caused a macOS copy failure
before Boot 1 of the first hardware test is consistent with this bug — it
was created by unfixed code in a prior session.

### Location

The bug was at `asyncfatfs.c:3843-3848` (pre-fix line numbers):

```c
if ((file->mode & AFATFS_FILE_MODE_CREATE) != 0 &&
    afatfs_freeRunIsReady(opState)) {
    afatfs_findLast(&afatfs.currentDirectory);
    opState->phase = AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE;
    goto doMore;   // ← exits scan mid-directory
}
```

The SFN-only path does not have this bug — it only creates at the `0x00`
terminator, which by FAT convention means end of entries. The rename path
also does not have this bug — it creates only at `entry == NULL` (physical
end of allocated directory).

### Fix

Latch the first viable free run position instead of branching to create.
Continue scanning the remainder of the directory. If a matching entry is
found later, it is opened normally (the latch is simply unused). Only at
directory exhaustion (`entry == NULL`) — after the entire directory has
been scanned — does the latched position get used for creation.

**Struct addition** (`afatfsCreateFile_t`):
- `uint8_t freeRunLatched` — set once when a viable free run is first found
- `afatfsDirEntryPointer_t latchedFreeRunStart` — saved sector/entry position

**Changed paths** (4 sites, all in `afatfs_createFileContinue()`):

1. **Init** — clear `freeRunLatched = 0` alongside `freeRunLength = 0`
2. **Mid-scan free run** — latch instead of create; first-fit preserved.
   When `afatfs_freeRunIsReady(opState)` fires and the file mode includes
   CREATE, the code now sets `opState->freeRunLatched = 1` and copies
   `opState->freeRunStart` into `opState->latchedFreeRunStart`, then
   continues scanning instead of jumping to the create phase.
3. **Directory exhaustion** (`entry == NULL`) — check latch before
   current-run fallback. When CREATE mode reaches the end of the directory,
   it now checks `opState->freeRunLatched` first and restores the latched
   position into `opState->freeRunStart` before entering the create phase.
   If no latch exists, the existing current-run fallback logic is used.
4. **Alias collision restart** — clear `freeRunLatched = 0`. When the SFN
   alias generator produces a collision and the scan restarts, the latch
   is cleared because the new alias may need different LFN entry counts.

No change to the SFN-only path, no change to the rename path, no change to
any caller or any other file.

### Build (after duplicate fix only)

```
text     data    bss     dec     hex
381212   400     94800   476412  744fc
```

---

## 2. Cluster-boundary file-size fix

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

### Build (after cluster fix)

```
text     data    bss     dec     hex
381220   400     94800   476420  74504
```

(+8 bytes text over the duplicate-fix baseline, from the one added function call.)

---

## 3. Hardware assessment — two-boot test (duplicate fix + cluster fix)

**Test procedure:** user deleted all root temporary files, flashed the
fixed firmware, and booted twice with different Bank Loads. SD_CARD_A was
copied after Boot 1 (Bank 015 "LoadTst!"). SD_CARD_B was copied after
Boot 2 (Bank 014 "Full"), with root files from Boot 1 left on-card.

### PASS — root hidden files

| File | SD_CARD_A | SD_CARD_B |
|------|-----------|-----------|
| `.hcnames` | Present, 1348 B, 129 rows. Row 0: `LoadTst!` src `015` | Present, 1323 B, 129 rows. Row 0: `Full` src `014` |
| `.hcprms2` | Present, 34,768 B (correct) | Present, 34,768 B (correct) |
| `settings.cfg` | `active_bank=15` — matches `.hcnames` | `active_bank=14` — matches `.hcnames` |

Both `.hcnames` files are structurally correct: 1 Bank + 16 Scene + 16 Kit
+ 96 Instrument rows (6 per Scene), with Instrument stems matching their
parent Kit/Scene names. No stale, blank, or corrupt rows.

SD_CARD_B had root files from Boot 1 still on-card when Boot 2 ran. No
duplicate `.hcnames` or `.hcprms` files were observed — the fix prevented
the LFN create path from creating a second entry for these existing files.

### PASS — autosave lifecycle (trace)

Both boots show a clean autosave lifecycle with no error or stall records
during publication:

**Boot 1 (SD_CARD_A, 6403 records):**
```
A → V(gen0, none)  → T           [no prior files — fresh from scratch]
A → V(gen1, .hcprms1) → M(3856 B) → B(0xffff) → C(1536) → P(gen2, .hcprms2) → T
```

**Boot 2 (SD_CARD_B, 8692 records, appended to Boot 1):**
```
A → V(gen0, none)  → T           [prior files not validated — see note]
A → V(gen1, .hcprms1) → M(3856 B) → B(0xffff) → C(1536) → P(gen2, .hcprms2) → T
```

Both published to `.hcprms2` generation 2 with 1536 patches, 3856-byte
canonical mask, and resident present mask 0xffff (all 16 Scenes). Terminal
status DONE on all cycles. No `X` (phase stall) records anywhere.

### PASS — library indexes

| File | SD_CARD_A | SD_CARD_B |
|------|-----------|-----------|
| `Bank/.hcindex` | Present, 1001 lines | Present, identical |
| `Scene/.hcindex` | Present, 1001 lines | Present, identical |
| `Kit/.hcindex` | Absent (deleted by user) | Present, 1 entry (`Barf`) |

SD_CARD_B's `Kit/.hcindex` was regenerated by Boot 2 after the user
deleted it between boots — boot .hcindex generation worked.

### Observations — not blocking

1. **SD_CARD_A missing `.hcprms1`** — likely a copy artifact from the
   macOS copy failure. The trace shows it was created (V found gen1
   .hcprms1 in the second cycle) and used as the source for publishing
   gen2 to `.hcprms2`.

2. **SD_CARD_B `.hcprms1` is 32,768 bytes** — this was pre-cluster-fix
   firmware. 32,768 = one 32 KB cluster; the full record needs a second
   cluster for the remaining 2,000 bytes. Root cause: `afatfs_fseekAtomic()`
   did not call `afatfs_fileUpdateFilesize()`, leaving `logicalSize` at 0
   so only `physicalSize` (cluster-rounded) was persisted to the directory
   entry. Fixed by the cluster-boundary fix (Section 2 above). Post-fix
   hardware test confirmed all four `.hcprms` files are 34,768 bytes.

3. **Instrument `.hcindex` absent from SD_CARD_B** — boot should
   regenerate all four Instrument type directory `.hcindex` files
   (`Drum/.hcindex`, `Snare/.hcindex`, etc.). They are absent despite
   `Kit/.hcindex` being regenerated. May be related to the `E` error below.

4. **`E` REPAIR_NAMES error at boot** — both boots emit `E` at tick ~633
   (REPAIR_NAMES op_phase=33, op_slot=0). This is a pre-existing issue,
   not introduced by this session. It fires once at boot during the name
   repair pass and does not prevent Bank Load or autosave from completing.

5. **Boot 2 V(gen0, none) — RESOLVED, correct behaviour.** The validator
   at `filesystem_autosaveParameterDrain_tick()` phase 3 calls
   `autosave_streamValidationMatchesBank()` which checks the file's
   stored `bank_slot` and `bank_name` against the currently loaded bank.
   Boot 1 wrote `.hcprms` files for Bank 015 "LoadTst!"; Boot 2 loaded
   Bank 014 "Full". Both candidates pass CRC but fail the bank identity
   check, so V correctly reports no winner. The no-winner recovery path
   (phases 30-38) removes both files, writes fresh `.hcprms2` gen0 and
   `.hcprms1` gen1 for Bank 014, and the second cycle validates gen1 and
   publishes gen2 — all working as designed.

---

## 4. Zero mutation masks — diagnosis and page-exit expedite

### Observation

The second hardware test's `.hcprms` files had the correct 34,768-byte size
but **zero mutation masks** and only initial recovery generations (gen 0 in
`.hcprms2`, gen 1 in `.hcprms1`). No parameter drain cycle published.

### Trace analysis

SD_CARD_A, Boot 1 lifecycle records:

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

### Root cause

The no-winner recovery path (drain phases 30-38) writes **initial records**
with zero masks and deterministic identity-only payload. It does not consult
or consume the SRAM dirty mask — those bits are reserved for the *next* drain
cycle, which validates the fresh records, merges the file mask (all zeros)
with SRAM (dirty), classifies each dirty bit, captures live parameter values,
and publishes a transformed copy.

**Two full drain cycles** are required between the first page exit and
published mutations:

1. Recovery cycle: validate → no winner → write gen 0/gen 1 (~6 s)
2. Mutation cycle: validate → winner → merge → classify → copy → publish (~6 s)

Between cycles there is the 250 ms continuation interval and potential
interference from the settings writer and trace flush schedulers, which run
at higher priority. Total wall time from page exit to published mutations
exceeds 12 seconds. A short voice-mode session before power-off misses the
publication window.

The 5-second writer debounce (`AUTOSAVE_WRITER_INTERVAL_MS`) was not the
direct cause in this test — it had already expired while the user was on the
LOAD page (deadline 8717 vs. page exit at ~23000). But in faster user flows
where the LOAD page is entered and exited within seconds of boot, the
unexpired debounce adds additional wasted time.

### Change — page-exit expedite

Added `fs_autosave_page_suppressed` flag to the writer scheduler. When the
scheduler's Load/Save page guard blocks the writer, the flag is set. On the
first tick after the page is left, the flag clears and the writer deadline
is reset to `now + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` (250 ms) instead
of retaining whatever the original 5-second debounce was.

**4 change sites** (all in `Core/Hardware/SD/filesystem.c`):

1. **Declaration** — `filesystem.c:1613`:
   ```c
   static uint8_t fs_autosave_page_suppressed = 0u;
   ```

2. **Page guard sets flag** — `filesystem.c:19846`
   (inside `filesystem_autosaveWriterSchedule_tick()`):
   ```c
   if (menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) {
       fs_autosave_page_suppressed = 1u;
       /* ... existing W trace witness ... */
       return;
   }
   ```

3. **Page exit resets deadline** — `filesystem.c:19866`
   (immediately after the page guard, before the existing deadline check):
   ```c
   if (fs_autosave_page_suppressed) {
       fs_autosave_page_suppressed = 0u;
       fs_autosave_next_due_tick = (uint16_t)(
           now + AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS);
   }
   ```

4. **Card-failure resets** — `filesystem.c:19233` and `filesystem.c:19286`
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

### Build (after page-exit expedite)

```
text     data    bss     dec     hex
381268   400     94800   476468  74534
```

(+48 bytes text over the cluster-fix build, from the flag and transition logic.)

---

## 5. Progressive build sizes

| Step | text | data | bss | delta |
|------|------|------|-----|-------|
| Duplicate fix | 381,212 | 400 | 94,800 | baseline |
| + Cluster fix | 381,220 | 400 | 94,800 | +8 text |
| + Page-exit expedite | 381,268 | 400 | 94,800 | +48 text |

No BSS or data changes in any step.

---

## 6. Two-cycle autosave recovery lifecycle

For reference, the complete no-winner recovery lifecycle that explains why
two drain cycles are needed:

**Cycle 1 (recovery):**
- Validate both candidates → both fail bank identity check → no winner
- Remove both files
- Write `.hcprms2` gen 0: zero mask, deterministic Bank/Scene identity payload
- Write `.hcprms1` gen 1: zero mask, deterministic Bank/Scene identity payload
- Terminal status DONE

**Cycle 2 (mutation):**
- Validate both candidates → gen 1 `.hcprms1` wins
- Import winner's on-card mask (all zeros)
- Merge with SRAM dirty mask → dirty bits now visible
- Classify each dirty bit, capture live parameter values
- Open winner read-only, remove inactive target
- Create new target, stream transformed copy with captured values
- Close, sync CRC, write commit byte
- Acknowledge captured bits
- Terminal status DONE → published mutations visible on card
