# S094 P3 — Memory Allocation Speed-ups

- **Status:** **implemented (user, 2026-11-11): A, B, C and D**, all in one
  build. The step-by-step record is in §12. The device test (§9) is still
  to run.
- **Source:** `DRAFT_SRAM_ALLOCATION_SPEEDUPS.md` (2026-11-10), re-checked
  against today's tree and link (§1). That draft records the plan "A + B at
  16 sectors, 6,156 B" (user, 2026-11-10).
- **Context:** S094 P2 changed how SRAM behaves (`S094_P2_ALKALINE_SAVINGS.md`
  §13, §17, §22). This document folds the draft in on that basis, and adds
  one new item that the cache fix makes possible (D, §5).
- **Binding rules that apply:**
  - every new or enlarged RAM allocation needs user sign-off, with bytes,
    region and owner (`MEMORY.md` §RAM and CPU Budget);
  - free SRAM1 is earmarked for future Pattern data;
  - free DTCM is earmarked for delay lines;
  - "Names in RAM (S090 P1)": copy/clear uses of the name cache change only
    if a speed-up directly needs it (user U12). Proposal A is that case and
    needs an explicit yes;
  - DEV logging allocations exist only in a `DEV_MODE_LOGGING` build;
  - no step of this plan edits `MEMORY.md` or `knowledge_files/` (§11 lists
    what goes out of date).

---

## 0. Summary

**Implemented (§12): the draft's A, C and B, 6,156 B of SRAM1, plus D.**
Built and host-tested; the device session (§9) is next.

| | Change | SRAM1 | What it speeds up |
|---|---|---:|---|
| **A** | A dedicated copy/clear name-remap scratch, so copy/clear stops borrowing the 9,000 B name cache | **+1,932 B** | the paste name phase; Load/Save entry after a copy; AutoSave, settings and Load requests no longer refused while copy holds the loan |
| **C** | Regular-file data reads marked "discardable" in the AsyncFATFS cache | **0 B** | makes B work: streamed file data stops evicting directory and FAT sectors |
| **B** | AsyncFATFS sector cache 8 → **16** sectors | **+4,224 B** | every directory lookup and FAT walk that re-reads the card today: type steps, Load/Save entry, Bank-load child opens, HCNAMES temp/rename, AutoSave opens |
| | **Total** | **+6,156 B** | |

- **D, a write-back stack (§5), is in the same image (user).** 0 B. It
  speeds up the voices' spill stores. The next run therefore measures
  `QADD` (P2 §22) and D together.
- **The S094 P2 cache fix strengthens the case for B and C.** SRAM data is
  now D-cached, so the CPU part of each filesystem step got faster: P2
  §20.3 shows `LOAD_INDEX` falling from 41 to 10 ms. What remains is mostly
  the number of sectors read, and that is what B and C reduce.
- **None of A, B or C touches a DSP path.** The probe (P2 §14) checks
  this: the voice and mixer cycles on the next run should match run 2 apart
  from `QADD`.
- **SRAM1 after A + B:** **87,404 B free** in the DEV link (measured,
  §12.7). That still leaves room for the Pattern reservation at
  `PAT_STACK_SIZE` 384 (69,632 B).

---

## 1. What changed since the draft

### 1.1 SRAM is D-cached (S094 P2, AS-6)

- **The configuration:** MPU region 0 is now non-shareable write-through
  (`clocks.c`). The D-cache covers everything in SRAM1 and SRAM2:
  - `fs_list_cache_name`, the `afatfs` sector cache and its descriptors;
  - `fs_stage_workspace` and the parsers' buffers;
  - the stack.
- **Coherency:** unchanged for the filesystem. The SD path moves its data
  with the CPU (no DMA), so the cache always sees it. Write-through with no
  write-allocate means the SD shim's 16-byte stores into a sector buffer
  update memory and allocate no lines; only the parser's reads allocate.
- **The effect on B:** a larger sector cache means fewer sectors read.
  Fewer reads also mean fewer cache lines displaced from the audio working
  set between blocks. The next run's probe peaks during Load/Save show
  whether that interaction is visible (§9, T9).

### 1.2 Load/Save is already faster

From the `w` owner records (P2 §20.3; medians, indicative, the Scenes
differ):

| Operation | Uncached | Cached (run 2) |
|---|---:|---:|
| `LOAD_INDEX` | 41 ms | 10 ms |
| `LOAD_LIBRARY_INDEX` | 8 ms | 0 ms |
| `SCAN_BANK_SCENES` | 403 ms | 192 ms |
| `UPDATE_HCNAMES_KIT` | 699 ms | 121 ms |

- **Why:** the main loop now gets about 19 % of the CPU instead of 5 %.
  Each `sdcard_poll()` pass comes round sooner, so each sector arrives
  sooner.
- **What is left:** the sector count. The draft's reasoning still holds:
  more RAM cannot make one sector arrive faster. It helps only where it
  **avoids a sector read** (B, C) or **removes a wait on another owner**
  (A).
- **Run 2 is the "before"** for the timing tests in §9: it is the first run
  with the cache fix, and it already contains these operations.

### 1.3 References re-checked (today's tree)

| Draft reference | Today |
|---|---|
| `fs_list_cache_name[1000][9]`, 9,000 B | `filesystem.c:1068`; 9,000 B at 0x20024DFC (`nm`) |
| `filesystem_borrowNameCacheScratch()` / `…Return…()` | `filesystem.c:1995` / `:2008` |
| `filesystem_cacheCopyClearRemap()` | `filesystem.c:7151`; it returns early unless `fs_name_cache_borrowed` |
| The `filesystem_start()` refusal while lent | `filesystem.c:29321` (all but `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY`) |
| `filesystem_requestCopyResidentNames()` | `filesystem.c:34683`; it refuses unless `fs_name_cache_borrowed` |
| Install loan | `filesystem.c:25735-25739`, tag `FS_NAME_CACHE_COPYCLEAR` |
| `ccSvc_ensureScratch()` | `copyClearService.c:193`; `menu_isLoadSaveCommandActive()` wait (S082 D53) |
| Scratch users | `copyClearService.c:1274-1317, 1440, 1587`; teardown return `:1771-1773`; reset `:1341` |
| `AFATFS_NUM_CACHE_SECTORS 8` | `asyncfatfs.c:35` |
| `afatfs_allocateCacheSector()` | `asyncfatfs.c:1148` |
| `afatfs_fileRetainCursorSectorForRead()` | `asyncfatfs.c:2309`; directory scans reach it from `afatfs_findNext()` (`:2671`) |
| Discardable bit set on first cache | `asyncfatfs.c:1351` |
| `afatfs` static | 6,984 B (0x1B48) at 0x200232B4 |
| Stall record part 1 ("cache slots (8 total)") | `filesystem.c:7296-7304` |
| Decoder part 1 ("cache (of 8)") | `decode_devlogs.py:256-274`, `decode_hcnames_stall()` |

All hold. Line numbers have moved slightly since the draft, but no
behaviour has changed.

---

## 2. Proposal A — dedicated copy/clear scratch (+1,932 B)

**Unchanged from the draft (§2-§4 there).** Copy/clear borrows the whole
9,000 B browser cache to use 2,027 B of it. The loan costs everyone else:
- every paste job's name phase waits for an idle facade and for no Load/Save
  command to be running;
- `filesystem_start()` refuses every other operation while the loan is held;
- the browser index is cleared on return and must be re-read;
- a failed borrow counts toward the 2 s name-retry budget.

**The object** (owner `filesystem.c`, SRAM1 `.bss`, firmware lifetime):

```c
typedef struct {
    uint16_t sources[FS_HCNAMES_ROW_COUNT];                              /* 322 B */
    char names[FS_HCNAMES_ROW_COUNT][STORAGE_KIT_DISPLAY_NAME_LEN + 1u]; /* 1,449 B */
    uint8_t remap[FS_HCNAMES_ROW_COUNT];                                 /* 161 B */
} filesystem_copy_clear_scratch_t;                                       /* 1,932 B */
```

`uint16_t` comes first, so there is no padding: 1,932 B, a multiple of 4.

**Changes (implementation detail added to the draft's list):**
1. **`filesystem.c`:** the static, a validity flag (`fs_cc_scratch_active`,
   1 B, inside the same allocation's alignment slack or counted separately),
   and one accessor pair:
   - `filesystem_copyClearScratchBegin()` clears the remap to "none" and
     sets the flag. It always succeeds;
   - `filesystem_copyClearScratchEnd()` clears the flag;
   - `filesystem_copyClearRemap()` returns the remap array.
2. **`filesystem_cacheCopyClearRemap()`** reads the struct's `names` and
   `sources` instead of casting `fs_list_cache_name`. Its early return
   tests `fs_cc_scratch_active`.
3. **`filesystem_requestCopyResidentNames()`** tests `fs_cc_scratch_active`
   instead of `fs_name_cache_borrowed`.
4. **`copyClearService.c`:**
   - `ccSvc_ensureScratch()` calls `…Begin()` once per interaction. It drops
     the `menu_isLoadSaveCommandActive()` wait (D53 protected the shared
     array, which is no longer used), and the `scratch_wait` trace keeps
     its event with a wait of 0.
   - `ccSvc_buf` points at the dedicated remap.
   - The teardown (`:1771`) calls `…End()` in place of
     `filesystem_returnNameCacheScratch()`.
5. **The loan, `FS_NAME_CACHE_COPYCLEAR`, `fs_name_cache_borrowed` and the
   `filesystem_start()` refusal stay only for the sample/wavetable install**
   (`filesystem.c:25735`). It runs with audio suspended and adds no wait
   anyone sees.
6. **Unchanged:**
   - `copyClear_backgroundSuspended()` still holds AutoSave, settings, trace
     flushes and the repair epoch during an interaction (user, S075);
   - the `HNcU` write still needs the facade and keeps its retry budget.

   Only the recording of names stops waiting.

**Rejected (0 B):** the remap in `fs_stage_workspace`. Once the loan is
gone, that union is live during exactly the operations a copy must stop
blocking: Load parsers, the AutoSave writer, HCNAMES regeneration.

---

## 3. Proposal C — discardable hint for regular-file data (0 B)

**Unchanged from the draft (§6 there).** In
`afatfs_fileRetainCursorSectorForRead()` (`asyncfatfs.c:2309`), add
`AFATFS_CACHE_DISCARDABLE` when the file is a regular file:
`file->type == AFATFS_FILE_TYPE_NORMAL` and
`(file->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0`.

- **Directories must be excluded.** Directory scans go through the same
  function (`afatfs_findNext()`), and marking their sectors discardable
  would do the opposite of what is wanted.
- **When the bit is set:** only when a sector is first cached
  (`asyncfatfs.c:1351`). A sector first read as a directory sector keeps
  LRU treatment.
- **The read cursor stays safe:** a retained sector is never evicted,
  discardable or not.
- **Risk:** a data sector read again after a seek back is evicted first.
  The line parsers have not yet been checked for seek-backs. That check is
  part of step 2 of the schedule (§8), plus T5.

C goes in before or with B, so that B is never measured without it.

---

## 4. Proposal B — AsyncFATFS cache 8 → 16 sectors (+4,224 B)

**Unchanged from the draft (§5 there).**
- **The size:** `afatfs` grows from 6,984 to 11,208 B (8 × (512 + 16)).
- **Why 16:**
  - with five handle pins and the current-directory retain, 10 slots stay
    free for directory and FAT sectors (2 today);
  - with C and one or two handles open (the usual case), about 12-13 stay
    free.

**Code coupled to the count:**
- `asyncfatfs.c` uses the constant everywhere. The index fields are
  `int8_t`; `retainCount` is per slot.
- The stall and boot-capsule snapshots count with `uint8_t`.
- **Must change: the DEV stall record part 1** (`filesystem.c:7296-7304`).
  It packs six counts and two indices into 4-bit fields, which wrap at 16
  (`cache_empty` is 16 at boot). The new 32-bit layout, which also covers
  24 sectors:

| Bits | Field | Width |
|---|---|---:|
| 0-4 | locked | 5 |
| 5-9 | retained | 5 |
| 10-14 | dirty | 5 |
| 15-19 | empty | 5 |
| 20 | reading (0/1) | 1 |
| 21 | writing (0/1) | 1 |
| 22-26 | cwd retain + 1 | 5 |
| 27-31 | cwd lock + 1 | 5 |

  Reading and writing fit in one bit each, because the SD shim accepts one
  transfer at a time: `sdcard_readBlock()` and `sdcard_writeBlock()` refuse
  unless IDLE.

- **The decoder** (`decode_hcnames_stall()` part 1) changes with it, and its
  label becomes "cache (of 16)".

**Cost:**
- The allocation and flush scans grow from 8 to 16 compares per call.
  They run in the foreground only; no DSP cost.
- More slots can hold more dirty sectors between syncs. The durability
  points are unchanged: every facade operation ends with `afatfs_flush()`
  and a final sync, and the A/B validators still probe exact length.

**24 sectors (+4,224 B more)** stays a later option. It is decided only if
T6/T7 still show folder-lookup time at 16.

---

## 5. New, optional: D — write-back stack region (0 B)

**What:** MPU region 2 over SRAM2 (0x2007C000, 16 KB, the stack), Normal
write-back write-allocate, non-shareable: TEX 001, C 1, B 1, S 0. Region 0
stays write-through for SRAM1.

**Why:** the voices' spill stores. The Alkaline loop makes 18 stack stores
per sample (P2 §13). Under write-through each one goes to the bus through
the write buffer. Under write-back it stays in the cache.

**Size:** unknown. Spill loads already hit the cache since AS-6; only the
stores change. A guess is 0-5 % of the Alkaline's render, smaller for the
drum types. It would be measured with the probe.

**Safe because:**
- no DMA touches SRAM2;
- nothing else shares the stack.

**One caveat: the `DEV_LOGGING_IWDG` capsule** (`.devwdg_noinit`, SRAM2
base, empty while `DEV_LOGGING_IWDG` is 0). The capsule is written just
before a reset. With write-back, the write could still sit in the cache
when the reset comes, and be lost. The fix is either:
- to disable region 2's first 2 KB sub-region (SRD bit 0), so the capsule
  falls back to region 0's write-through; or
- to clean the D-cache before the deliberate reset.

The first needs no code on the reset path.

**When:** in the image after the next one. The next run then measures
`QADD` alone, and the run after measures D alone, both on the same Scenes.

---

## 6. Not taken

| Option | Verdict |
|---|---|
| A second 9,000 B browser cache | **No** (draft §7): the small form of the failed "cache every library index in RAM" (S090); B + C remove the lookup cost of every type change for less |
| A general second scratch shared by copy/clear, AutoSave, etc. | **No:** it brings back the loan and its gates |
| Larger `staging_buf` / `sample_io_buf` | **No gain:** the card moves 16 B per poll; buffer size does not change the sector count |
| Moving the install's wavetable frame out of the name cache | **No gain anyone sees:** audio is suspended during an install |
| Hot audio data into DTCM | **Not needed now:** SRAM is cached (P2 §13). Free DTCM above the FX arena's minimum is 3,776 B and stays earmarked for delay lines |
| SRAM2 for buffers | **No:** it is the stack, and its high-water mark has not been measured |
| Multi-block SD writes | **Out of scope:** the shim has no CMD25 |

---

## 7. RAM request (for sign-off)

| Owner | Object | Region | Bytes | Lifetime |
|---|---|---|---:|---|
| `filesystem.c` | `filesystem_copy_clear_scratch_t` + `fs_cc_scratch_active` (new) | SRAM1 `.bss` | 1,932 (+1, plus alignment) | firmware; contents valid for one copy/clear interaction |
| `asyncfatfs.c` | `afatfs.cache[]` + `cacheDescriptor[]`, 8 → 16 slots | SRAM1 `.bss` | 4,224 | firmware |
| | **Total (A + B)** | | **6,156 (+1)** | |

D, C and the stall-layout change allocate nothing.

**SRAM1 headroom** (today's DEV link, `nm`):
- `_ebss` is 0x2006527C and SRAM1 ends at 0x2007C000: **93,572 B free**.
  That is 96 B less than the draft's figure; the difference is the S094 P2
  probe (94 B, DEV only, AS-8).
- The figure includes the temporary 16,384 B `autosave_trace_records` ring,
  which is DEV only.
- **After A + B: 87,404 B free** (DEV link, measured: `_ebss`
  0x20066A94), or about 103,880 B in a logging-off build. The 12 B beyond
  the 6,156 B are the 1 B flag and alignment.
- **The Pattern reservation:** `PAT_STACK_SIZE` 256 → 384 needs 69,632 B. It
  still fits after A + B, with 17,784 B to spare in the DEV link. 512 did
  not fit before this proposal and still does not.

---

## 8. Implementation schedule (one build)

| Step | Change | Files | Check |
|---|---|---|---|
| 1 | A: the scratch struct, flag and accessors; the remap reader and `requestCopyResidentNames()` switched to the flag | `filesystem.c`, `filesystem.h` | builds; `_Static_assert` on 1,932 B |
| 2 | A: copy/clear moved onto the dedicated scratch; the D53 wait removed from `ccSvc_ensureScratch()`; teardown calls `…End()` | `copyClearService.c` | the loan remains only on the install path: no copy/clear caller of `filesystem_borrowNameCacheScratch()` (grep) |
| 3 | C: discardable hint for regular-file reads, directories excluded. Also check the line parsers for seek-backs and record the result | `asyncfatfs.c` | grep the seek calls in the Load, index and HCNAMES parsers |
| 4 | B: `AFATFS_NUM_CACHE_SECTORS` 16 | `asyncfatfs.c` | `afatfs` 11,208 B (`nm`) |
| 5 | B: stall record part 1 in the 32-bit layout (§4); the decoder updated | `filesystem.c`, `tools/diagnostic_tools/decode_devlogs.py` | decoder reads a synthetic record |
| 6 | Build and ledger | — | `make all && make img`; `link_budget.py`; SRAM1 free ≈ 87,416 B; host `make -C tools/dsp_test all` |

---

## 9. Device test (one session, on the build from §8)

**Before:** run 2 (P2 §17, §20.3), the first run with the SRAM cache.
**After:** this session. The probe is still in the image, so the same
trace also gives `QADD`'s effect on the voices.

| # | Test | Pass |
|---|---|---|
| T1 | Copy a Kit or Instrument while an AutoSave drain runs | the job finishes without waiting for the drain; the names are right after a power cycle |
| T2 | Browse Kits, copy, re-enter Load/Save | no `.hcindex` re-read (no `...` busy phase) |
| T3 | A chained or swapped paste | it resolves to the original names |
| T4 | Load:[Samples] wavetable install | still works through its loan |
| T5 | Index, payload and Bank loads | identical results to before |
| T6 | Load/Save type steps while playing | `LOAD_INDEX` / `LOAD_LIBRARY_INDEX` medians against run 2 (10 ms / 0 ms) |
| T7 | Bank load and Scene Save | `SCAN_BANK_SCENES` and `UPDATE_HCNAMES_*` medians against run 2 (192 / 121 ms) |
| T8 | A forced stall record | decodes with the new part 1 layout |
| T9 | Worst Scene playing during T6/T7 | probe window peaks no higher than run 2's while Load/Save runs (§1.1) |
| T10 | The worst Scene (F) playing for a minute | the voice renders and adds fall against run 2: `QADD` and D together. No crash or audio fault over the session: the stack is now write-back |

**Decision after the session:** if T6/T7 still show folder-lookup time at
16 sectors, propose 24 sectors (+4,224 B) for a separate sign-off.
Otherwise stop at 16.

---

## 10. Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| P3-1 | Proposal A (+1,932 B SRAM1): dedicated copy/clear scratch, the U12 exception | **Done (user, §12.1-12.2)** |
| P3-2 | Proposal C (0 B): discardable regular-file reads | **Done (user, §12.3)**, with the seek-back check |
| P3-3 | Proposal B (+4,224 B SRAM1): 16 cache sectors, with the stall-record layout | **Done (user, §12.4-12.5)** |
| P3-4 | A, B and C together in one build and one device session (§8, §9) | **Done:** one build, with D (user) |
| P3-5 | D: write-back stack region (0 B) | **Done (user, §12.6)**, in the same image; sub-region 0 disabled for the IWDG capsule |
| P3-6 | 24 sectors | **Later,** only if T6/T7 call for it |

---

## 11. Statements this would make out of date (for the user to log, if adopted)

From the draft (§10 there), unchanged:
- `MEMORY.md` §Storage / Filesystem, Copy/clear: "Name cache lent for 161 B
  HCNAMES remap."
- `MEMORY.md` §Storage / Filesystem, Names in RAM (S090 P1): the copy/clear
  clause (A).
- `STORAGE_SRAM_MANIFEST.md` §8.2:
  - the `fs_list_cache_name` row (the loan);
  - the `afatfs` row (6,984 B);
  - a new copy/clear scratch row;
  - the header totals.
- `COPYCLEAR_UTILITIES.md`: §11.7 (the loan), the §2 "9 kB name buffer" row,
  and the §17 "Name buffer | 161 of 9,000" row.
- `FILESYSTEM_SPEC.md` "Copy/clear: name-buffer loan and the `HNcU` update
  (S075)".
- `MODULE_INTERCHANGE_SPEC.md` line 28 ("the filesystem name-buffer loan").
- `ASYNCFATFS_REFERENCE.md`: the cache size and the discardable rule for
  regular-file reads.
- `DEV_MODES.md`: the stall record part 1 layout.

New in P3:
- `DRAFT_SRAM_ALLOCATION_SPEEDUPS.md`: superseded by this document once
  adopted.
- If D is adopted:
  - `Core/Hardware/clocks.c`'s MPU comment (a third region);
  - `MEMORY.md`'s memory-map lines (SRAM2: the stack, write-back);
  - `CPU_USE_DSP_AUDIT.md` (a new item next to AS-6).

---

## 12. Implementation record (user: A, B, C and D now, 2026-11-11)

The user asked for all four in one build. That includes D, which §5 had
planned for the image after. So the next run measures `QADD` and D
together: the probe cannot separate the two.

### 12.1 Step 1 — A, filesystem side (`filesystem.c`, `filesystem.h`)

- **New static `fs_cc_scratch`** (`fs_copy_clear_scratch_t`: `sources[161]`
  uint16, `names[161][9]`, `remap[161]`; 1,932 B) and **`fs_cc_scratch_active`**
  (1 B).
  - They are declared right after `fs_name_cache_borrowed`, with a comment
    block covering what, why, validity, accessors and affiliates.
  - `_Static_assert`s pin 1,932 B and the snapshot sizes against
    `hcnames_name_mirror` and `fs_resident_source`.
- **New `filesystem_copyClearRemapBegin()` / `filesystem_copyClearRemapEnd()`:**
  - Begin clears the remap to 0xFF, sets the flag and returns the remap. It
    always succeeds.
  - End clears the flag and the remap.
  - The contract is in `filesystem.h`, next to
    `filesystem_requestCopyResidentNames()`.
- **`filesystem_cacheCopyClearRemap()`** reads `fs_cc_scratch` (remap,
  names, sources) instead of casting `fs_list_cache_name`, and returns early
  unless `fs_cc_scratch_active`. The `FS_COPY_*_OFFSET` macros and their
  assert are gone.
- **`filesystem_requestCopyResidentNames()`** tests `fs_cc_scratch_active`
  in place of `fs_name_cache_borrowed`.
- **`filesystem_borrowNameCacheScratch()`** is removed: copy/clear was its
  only caller, and the install takes the loan inline.
  - `filesystem_returnNameCacheScratch()` stays for the install.
  - The install's inline loan now resets the DEV "one refusal witness per
    loan" latch, which the removed function used to reset.
- **Comments brought up to date** (the loan is now install-only):
  - the `FS_NAME_CACHE_COPYCLEAR` tag;
  - `filesystem_clearNameCacheStorage()`;
  - `filesystem_prepareLibraryNameCache()`;
  - the `filesystem_start()` refusal, now a backstop, since the install
    holds the facade BUSY;
  - the install's loan block;
  - the `FS_NAME_SCRATCH_BYTES` contract in `filesystem.h`;
  - the size assert's message.

### 12.2 Step 2 — A, copy/clear side (`copyClearService.c`, `copyClearService.h`)

- **`ccSvc_ensureScratch()`** calls `filesystem_copyClearRemapBegin()` once
  per interaction and always succeeds.
  - Its two waits are removed: the idle-facade borrow and the
    `menu_isLoadSaveCommandActive()` hold (S082 P3 D53). D53 protected the
    shared 9 kB array from the Bank load's final index restore, and the
    dedicated scratch shares nothing.
  - The comment block explains both removals. The `SCRATCH` trace event is
    kept as it was.
- **The teardown** calls `filesystem_copyClearRemapEnd()` in place of
  `filesystem_returnNameCacheScratch()`.
- **The remap assert** is now `CC_SCRATCH_REMAP_OFFSET == 0u`: `ccSvc_buf`
  is the remap itself. It used to fit-check against `FS_NAME_SCRATCH_BYTES`.
- **Unchanged:**
  - the HCNAMES write still needs the facade
    (`filesystem_requestCopyResidentNames()`);
  - the write still never gives up because of a Load/Save command (the D53
    clause in the retry budget at the teardown stays, since it is about the
    facade, not the array);
  - `copyClear_backgroundSuspended()`;
  - every `ccSvc_namesReady()` caller in `copyOps.c`, which simply no longer
    sees a wait.
- **Comments brought up to date:**
  - the `ccSvc_buf` declaration;
  - `ccSvc_namesReady()`;
  - the `ccSvc_scratch()` contract;
  - the name-remap block;
  - the module header in `copyClearService.h`;
  - "the borrowed table" in the snapshot comment.

  The S077 history notes ("replaced the cast into the borrowed name
  buffer") are left as history.

### 12.3 Step 3 — C (`asyncfatfs.c`)

- **The change:** `afatfs_fileRetainCursorSectorForRead()` adds
  `AFATFS_CACHE_DISCARDABLE` to its read flags when
  `file->type == AFATFS_FILE_TYPE_NORMAL` and the file has no
  `FAT_FILE_ATTRIBUTE_DIRECTORY` attribute. The comment block covers what,
  why, safety and affiliates.
- **Directories are excluded twice over.** `type` is set to DIRECTORY or
  NORMAL from the directory entry at open (`asyncfatfs.c:3582`); the
  attribute test is kept as a second guard, as the draft specified.
- **The seek-back check (draft T5's static part).** Only the read path
  (`afatfs_fread()` and the directory scan) uses this function. Of the ten
  `afatfs_fseek` / `filesystem_blockSeek` sites in `filesystem.c`:
  - **Write path, not affected** (`afatfs_fileLockCursorSectorForWrite()`):
    the AutoSave writer (`:9403`, `:9850`, `:9919`) and the Pattern CRC
    patches (`:16616`, `:16810`, `:22177`).
  - **Read path, forward only:** the `.pat` header skip (`:17394`), the
    AutoSave payload (`:30822`), and the install's chunk walk (`:25315`).
  - **Read path, may seek back:**
    - the boot AutoSave reader (`:32772` Scene sections, then `:33058` the
      Bank section);
    - the install's frame seek (`:25426`).

    Both are one-off and modal. The worst cost is one extra sector read;
    correctness is unaffected, because an evicted sector is read again from
    the card.

### 12.4 Step 4 — B, the cache size (`asyncfatfs.c`)

- `AFATFS_NUM_CACHE_SECTORS` 8 → **16**, with a comment block covering what
  (11,208 B, +4,224 B), why (2 → 10 free slots in the worst case) and the
  coupled code.
- **Re-checked for coupling:**
  - every loop and assert in `asyncfatfs.c` uses the constant;
  - the index fields are `int8_t`;
  - `afatfsStallSnapshot_t` and the boot-capsule snapshot store whole
    `uint8_t` counts (`filesystem.c` capsule `cache[1..6]`), so 16 fits;
  - the `asyncfatfs.h` comment refers to the constant by name.

  The only field that wraps is the DEV stall record (step 5).

### 12.5 Step 5 — B, the stall record and its decoder

- **`filesystem.c` part 1:** the new 32-bit layout of §4. The comment block
  carries the layout and why reading and writing are one bit each.
  - Counts are masked to 5 bits (good to 31).
  - Reading and writing are stored as "non-zero".
- **`decode_devlogs.py`** `decode_hcnames_stall()` part 1 reads the new
  layout and is labelled "cache (of 16)".
- **Checked with synthetic values:**
  - (locked 3, retained 2, dirty 1, empty 10, reading 1, cwd retain 4)
    decodes exactly;
  - the boot case `empty = 16`, which the old layout wrote as 0, now decodes
    as 16.

### 12.6 Step 6 — D, the write-back stack (`clocks.c`)

- **MPU region 2:**
  - RBAR 0x2007C000;
  - RASR `(3UL << 24) | (1UL << 19) | (1UL << 17) | (1UL << 16) |
    (0x01UL << 8) | (13UL << 1) | 1UL`: AP 011, TEX 001, C 1, B 1, S 0,
    XN 0, SRD 0x01, SIZE 13 (16 KB), enabled;
  - configured between region 1 and the MPU enable.
- **Comments:** a block comment on the region (what, why, safety, inputs,
  outputs, accessors, affiliates) and a Region 2 entry in the MPU header
  comment.
- **Safety checks made before the change:**
  - **No DMA in SRAM2:** all three DMA buffers are in `.dma_nocache`, region
    1.
  - **No whole-cache maintenance at runtime:** the only D-cache maintenance
    in the tree is `DCIMVAC` by address over flash ranges (`memtest.c:281`,
    `sampleFlash.c:189`). The only set/way operation is the boot invalidate
    in `sysclk_init()`, which runs before the D-cache is enabled; the
    bootloader leaves it off (Session 012 log). So no dirty stack line can
    be discarded.
  - **The IWDG capsule:** `.devwdg_noinit` is at most 32 B at the SRAM2 base
    (linker assert) and lies inside sub-region 0, which is disabled. It
    keeps region 0's write-through, so a write just before a reset reaches
    SRAM.
- **The cost of the disabled sub-region:** the lowest 2 KB of the stack
  (only touched at depths over 14 KB) stays write-through.

### 12.7 Step 7 — build and tests

- **`make all && make img`:**
  - `build/LXRV2_lxr02.img` is 687,028 B;
  - flash headroom 66,636 B;
  - ITCM 13,984 / 16,384 B (unchanged).
  - The image is a logging build, so the P2 probe is in it.
- **RAM, measured with `nm`:**

| Object | Before | After |
|---|---:|---:|
| `afatfs` | 6,984 B | **11,208 B** (+4,224) |
| `fs_cc_scratch` | — | **1,932 B** |
| `fs_cc_scratch_active` | — | 1 B |
| SRAM1 free (`_ebss` → 0x2007C000) | 93,572 B | **87,404 B** (−6,168: 6,156 + the flag + alignment) |

- **The MPU words in the image** (the `sysclk_init()` literal pool):

| Region | RBAR | RASR |
|---|---|---|
| 0 | 0x20000000 | 0x03020027 |
| 1 | 0x20020000 | 0x130C0017 |
| 2 | **0x2007C000** | **0x030B011B** |

- **Warnings:** one, `unused parameter 'eraseCount'` in
  `afatfs_cacheSector()`. It predates this work: the parameter is used only
  under `#ifdef AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT`, which is not
  defined. It shows now because `asyncfatfs.c` was recompiled.
- **Host:** `make -C tools/dsp_test all` exits 0. The six FAIL lines are
  `voice_control_break`'s expected failures.
- **Decoder:** `decode_devlogs.py` reads run 2's trace without error. There
  are no stall records in it, so the new part 1 layout is checked only by
  the synthetic values of §12.5.
- **Not testable on the host:** A, B, C and D need the device session (§9,
  T1-T10).
