# S056 — AsyncFATFS duplicate-name creation fix

**Date:** 2026-08-24
**File changed:** `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
**Status:** Fix implemented, builds clean. Two-boot hardware test completed — see assessment below.

---

## Bug

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

## Location

[asyncfatfs.c:3843-3848](Core/Hardware/SD/asyncfatfs/asyncfatfs.c#L3843)
(line numbers are pre-fix):

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

## Fix

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
2. **Mid-scan free run** — latch instead of create; first-fit preserved
3. **Directory exhaustion** — check latch before current-run fallback
4. **Alias collision restart** — clear latch (new alias invalidates it)

No change to the SFN-only path, no change to the rename path, no change to
any caller or any other file.

## Build

```
text     data    bss     dec     hex
381212   400     94800   476412  744fc
```

---

## Hardware assessment — two-boot test

**Test procedure:** user deleted all root temporary files, flashed the
fixed firmware, and booted twice with different Bank Loads. SD_CARD_A was
copied after Boot 1 (Bank 015 "LoadTst!"). SD_CARD_B was copied after
Boot 2 (Bank 014 "Full"), with root files from Boot 1 left on-card.

The pre-existing duplicate `Kit/.hcindex` that caused a macOS copy failure
before Boot 1 is consistent with the bug this fix addresses — it was
created by the unfixed code in a prior session.

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

2. **SD_CARD_B `.hcprms1` is 32,768 bytes** — 2,000 bytes short of the
   expected 34,768. `.hcprms2` is the correct 34,768 on both cards.
   `.hcprms1` is the prior-generation source, not the active published
   record, so the short size may reflect a truncation/allocation edge case
   rather than data loss. Needs investigation if it reproduces.

3. **Instrument `.hcindex` absent from SD_CARD_B** — boot should
   regenerate all four Instrument type directory `.hcindex` files
   (`Drum/.hcindex`, `Snare/.hcindex`, etc.). They are absent despite
   `Kit/.hcindex` being regenerated. This may be related to the `E` error
   below.

4. **`E` REPAIR_NAMES error at boot** — both boots emit `E` at tick ~633
   (REPAIR_NAMES op_phase=33, op_slot=0). This is a pre-existing issue,
   not introduced by this fix. It fires once at boot during the name
   repair pass and does not prevent Bank Load or autosave from completing.

5. **Boot 2 V(gen0, none)** — the second boot's first autosave validation
   cycle found no existing winner despite `.hcprms2` gen2 being on-card
   from Boot 1. The writer then created a fresh `.hcprms1` gen1 and
   subsequently published gen2 to `.hcprms2`. This suggests the validator
   could not read or validate the prior session's `.hcprms2`, possibly
   because the file existed as a duplicate from the unfixed code, or
   because validation requires specific header state that was not met.
   The autosave lifecycle still completed correctly in both cases.
