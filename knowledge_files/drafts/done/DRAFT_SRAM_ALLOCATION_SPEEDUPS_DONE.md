# DRAFT: SRAM allocation for storage speed-ups

Status: **draft proposal for user decision.** No code has been changed.
Measured on `dev-ph7-alkaline`, `build/lxr02.elf` of 2026-11-10 (DEV link).
Every RAM figure below needs user sign-off under the RAM policy
(`MEMORY.md` §RAM and CPU Budget; `STORAGE_SRAM_MANIFEST.md` §10).

---

## 1. Recommendation

Do **not** add a second general-purpose 7–12 kB scratch buffer. Spend
**6,156 B of SRAM1 (A + B)** on two owners, each with one job:

| | Change | SRAM1 `.bss` | What it speeds up |
|---|---|---:|---|
| A | A dedicated copy/clear name-remap scratch, so copy/clear no longer borrows the 9,000 B name cache | **+1,932 B** | Copy/paste name phase; Load/Save entry after a copy; AutoSave/settings/Load requests no longer refused while copy holds the loan |
| B | AsyncFATFS sector cache 8 → **16** sectors | **+4,224 B** | Every directory lookup and FAT walk that currently re-reads the card: type steps, Load/Save entry, Bank load child opens, HCNAMES temp/rename sequences, AutoSave opens |
| C | Mark regular-file data reads "discardable" in the AsyncFATFS cache | **0 B** | Makes B work: streamed file data no longer pushes directory and FAT sectors out of the cache |
| | **Total (A + B)** | **+6,156 B** | |

**Plan (user, 2026-11-10): A + B at 16 sectors, 6,156 B.** 24 sectors
(+8,448 B for B, 10,380 B in total) is kept as a later option, taken only if
the measurements in §9 still show folder lookups after 16.

Why not one shared second buffer: the 9,000 B array is slow to work with
because it is *shared*. Every borrower needs a loan, an idle facade, a
refusal gate and a cache clear afterwards. A second shared buffer would bring
back the same gates. The SD card is also the hard limit. A transfer moves 16
bytes per `sdcard_poll()`, and S090 measured about 34 main-loop passes per
512 B sector while playing. More RAM cannot make a sector arrive faster. It
helps only where it **avoids a sector read** (B, C) or **removes a wait on
another owner** (A).

---

## 2. What the 9,000 B buffer does today

`fs_list_cache_name[1000][9]` (`filesystem.c:1068`, SRAM1, 9,000 B,
4-byte aligned since S093 P8).

| User | Bytes it needs | How it gets them | Cost to everyone else |
|---|---:|---|---|
| Load/Save browser (`.hcindex` names; owner) | 9,000 | Owns the array; cleared on every type change or entry, then re-read | None — this is its job |
| Copy/clear name remap (S075/S077) | **2,027** (remap 161 @0, original names 1,449 @256, original sources 322 @1,705) | `filesystem_borrowNameCacheScratch()` (`filesystem.c:1995`) borrows **all 9,000 B**, only while the facade is IDLE and no Load/Save command is active | See §3.1 |
| Sample/wavetable install (S093 P8) | 4,096 (one 2,048-sample int16 frame) | Takes the same loan inside `filesystem_installUserContentBlocking()` (`filesystem.c:~25735`) | None in practice: audio is suspended and the facade is BUSY for the whole install |
| HCNAMES | — | Has its own `hcnames_name_mirror` (1,449 B) since Option 1C | — |
| AutoSave | — | **Does not borrow it.** AutoSave uses `fs_stage_workspace` (2,048 B), `fs_autosave_parameter_cache` (4,608 B), `staging_buf` (512 B) and `pat_background_region` | AutoSave contends with copy/clear only through the single facade (§3.1) |

So the "many functions share it" pressure comes almost entirely from
copy/clear. It borrows the whole array to use 22 % of it.

---

## 3. Where the time goes

### 3.1 Copy/clear and the loan

From `copyClearService.c` and `copyOps.c`:

- Kit, Instrument, Scene and Pattern-name paste jobs reach a final name phase
  (`ccCopy_runInstrument()` phase 2, `ccCopy_runKit()` phase 2, and others).
  They return `CC_RUN_WAIT` until `ccSvc_namesReady()` can borrow the buffer.
  The borrow fails while **any** facade operation is running (for example an
  AutoSave drain that started before the gesture, or a settings write) and
  while `menu_isLoadSaveCommandActive()` (S082 D53). The job queue stalls
  behind that wait.
- While the loan is held, `filesystem_start()` (`filesystem.c:~29320`)
  refuses every operation except `FS_INTERNAL_OP_UPDATE_HCNAMES_COPY`. As
  `COPYCLEAR_UTILITIES.md` §11.7 says: "Entering LOAD/SAVE right after
  releasing COPY therefore waits until the buffer is returned."
- On return the array is cleared, so the next Load/Save entry re-reads its
  `.hcindex` even when the user had just browsed that library.
- The name write gives up after `CC_NAME_RETRY_TICKS` (2 s) of refusals
  outside a Load/Save command. A failed borrow counts toward those 2 s.

None of these waits exist for any reason except that the remap lives in
someone else's array.

### 3.2 The AsyncFATFS cache

`AFATFS_NUM_CACHE_SECTORS 8` (`asyncfatfs.c:35`): 8 × 512 B data plus 8 × 16 B
descriptors. Descriptor size was measured with a scratch compile of
`asyncfatfs.c`: `sizeof(afatfsCacheBlockDescriptor_t)` = 16 and
`sizeof(afatfs)` = 6,984, matching the link.

- Eviction is LRU among in-sync, unlocked, unretained slots, with
  "discardable" slots preferred (`afatfs_allocateCacheSector()`,
  `asyncfatfs.c:1148`).
- File data reads use `AFATFS_CACHE_READ | AFATFS_CACHE_RETAIN` with **no**
  discardable hint (`afatfs_fileRetainCursorSectorForRead()`,
  `asyncfatfs.c:2334`). Reading one index or payload file therefore cycles
  its data sectors through the LRU and evicts every directory and FAT sector.
- Each of the five open handles can pin one slot (read-retain or write-lock).
  The current directory also retains one. With 8 slots, as few as 2–3 are
  left for directory and FAT sectors during a multi-file operation.
- `090_SESSION_HANDOFF_LOG.md` §7.2 already names this as the remaining
  type-change cost: "root and type folder lookups at ~34 passes per sector.
  Caching folder positions is a possible next step." A larger cache with
  directory sectors kept resident is the general form of that next step. It
  needs no folder-position bookkeeping and no new validity rule, because the
  AsyncFATFS cache is already coherent with every write the library makes.

---

## 4. Proposal A — dedicated copy/clear scratch (+1,932 B)

**Object:** one static in `filesystem.c`, SRAM1 `.bss`, firmware lifetime.
Its contents are valid from the first `ccSvc_nameCopy()` of an interaction
until `ccSvc_namesWritten()`.

```c
typedef struct {
    uint16_t sources[FS_HCNAMES_ROW_COUNT];                       /* 322 B */
    char names[FS_HCNAMES_ROW_COUNT][STORAGE_KIT_DISPLAY_NAME_LEN + 1u]; /* 1,449 B */
    uint8_t remap[FS_HCNAMES_ROW_COUNT];                          /* 161 B */
} filesystem_copy_clear_scratch_t;                                /* 1,932 B */
```

The layout puts `uint16_t` first, so the struct has no padding
(322 + 1,449 + 161 = 1,932, a multiple of 4). Owner `filesystem.c`, because
`filesystem_cacheCopyClearRemap()` (the only reader of the names/sources
snapshot) lives there. Copy/clear reaches the remap through one accessor.

**Changes:**

1. `ccSvc_ensureScratch()` returns the dedicated remap. It always succeeds,
   with no IDLE requirement and no `menu_isLoadSaveCommandActive()` wait.
   Name recording (`ccSvc_nameCopy()`) never waits.
2. `filesystem_cacheCopyClearRemap()` reads the dedicated struct instead of
   casting `fs_list_cache_name`.
3. Copy/clear no longer calls `filesystem_borrowNameCacheScratch()` /
   `filesystem_returnNameCacheScratch()`. The browser cache survives a copy.
4. The loan, the `FS_NAME_CACHE_COPYCLEAR` tag and the `filesystem_start()`
   refusal stay **only** for the sample/wavetable install. The install has
   no reason to move: it runs with audio suspended and adds no wait anyone
   sees.
5. Unchanged: `copyClear_backgroundSuspended()` still suspends AutoSave,
   settings, trace flushes and the repair epoch during an interaction (user
   requirement, S075). The `HNcU` write still needs the facade and keeps its
   retry budget. Only the *recording* of names stops waiting.

**Effect:** paste jobs stop stalling at their name phase. A copy no longer
blocks a Load/Save entry, and Load/Save does not re-read its index after a
copy. Requests from other owners are no longer refused because of the loan.

**Policy note:** `MEMORY.md` "Names in RAM (S090 P1)" says not to change
copy/clear uses of either cache "unless the speed-up directly needs it (user
U12)". This change is that speed-up, but it needs your explicit yes.

**Rejected alternative (0 B):** placing the remap in `fs_stage_workspace`.
Once the loan is gone, that union is live during the very operations a copy
must no longer block: Load parsers, the AutoSave writer, and HCNAMES
regeneration (`op_regen`).

---

## 5. Proposal B — AsyncFATFS cache 8 → 16 sectors (+4,224 B)

**Object:** `afatfs.cache[]` + `afatfs.cacheDescriptor[]` inside the existing
`afatfs` static (`asyncfatfs.c:799`). SRAM1 `.bss`, firmware lifetime,
owner `asyncfatfs.c`. `afatfs` grows 6,984 → 11,208 B (+8 × 528 B).

**Why 16:** the worst case, five handle pins plus the current-directory
retain, still leaves 10 slots for directory and FAT sectors (2 today). In
practice operations hold one or two handles (S057 census: none retained
between Bank children, one after each child mkdir). With C, file data
cycles through one or two slots, so about 12–13 slots stay free for
directory and FAT sectors.

**Later option, 24 sectors (+8,448 B, A + B = 10,380 B):** it pays off only
if one lookup's working set (the root directory's live sectors up to the
matching entry, one type folder, the FAT sectors) does not fit in 16. That
working set has not been measured; decide after T6/T7 (§9).

**Code coupled to the count** (every hit for `AFATFS_NUM_CACHE_SECTORS` was
checked):

- All loops and asserts in `asyncfatfs.c` use the constant. Index fields are
  `int8_t` (`writeLockedCacheIndex`, `readRetainCacheIndex`), so 16 (or 24) fits.
  `retainCount` is a 6-bit counter per slot and does not depend on the count.
- `afatfsStallSnapshot_t` and the boot-capsule snapshot count with `uint8_t`
  fields. They are fine.
- **Must change:** the DEV stall record part 1 (`filesystem.c:7297-7304`)
  packs six counts and two indices into 4-bit fields ("cache slots (8
  total)"). From 16 slots they wrap: `cache_empty` is 16 at boot (written
  as 0), and a cwd index of 15 encodes as 16 (also 0). Only 15 sectors would
  keep the current format. Proposed 32-bit layout, which also covers 24:
  locked 5, retained 5, dirty 5,
  empty 5, reading 1, writing 1, cwd retain+1 5, cwd lock+1 5. Reading and
  writing fit in one bit each because the SD shim accepts one transfer at a
  time: `sdcard_readBlock()` / `sdcard_writeBlock()` refuse unless the state
  is IDLE, and `afatfs_cacheSector()` sets READING only after an accepted
  read. Decoder: `tools/diagnostic_tools/decode_devlogs.py:270`
  (`decode_hcnames_stall()` part 1, "cache (of 8)").

**Cost:**

- The `afatfs_allocateCacheSector()` scan and the flush scans grow from 8 to
  16 compares per call. They run in the foreground only. No DSP cost changes;
  the CPU ceiling (S089 P2) is not touched.
- More slots can hold more dirty sectors between syncs. The durability
  points are unchanged: every facade operation already ends with
  `afatfs_flush()` and a final sync, and AutoSave's A/B validators still
  probe exact length. The final flush of a large Save may write a few more
  sectors in one go, which the existing per-tick batching already bounds.

---

## 6. Proposal C — discardable hint for file data (0 B)

In `afatfs_fileRetainCursorSectorForRead()` (`asyncfatfs.c:2309`), add
`AFATFS_CACHE_DISCARDABLE` when the file is a regular file:
`file->type == AFATFS_FILE_TYPE_NORMAL` and
`(file->attrib & FAT_FILE_ATTRIBUTE_DIRECTORY) == 0` (`fat_standard.h:27`).

- Directory scans use the same function (`afatfs_findNext()`,
  `asyncfatfs.c:2671`), so the test must exclude directories. Otherwise the
  change does the opposite of what is wanted.
- The discardable bit is set only when a sector is first cached
  (`asyncfatfs.c:1351`). A sector first read as a directory sector keeps LRU
  treatment.
- A retained sector is never evicted, discardable or not, so the read cursor
  is safe.
- Downside: a data sector that is read again later (a seek back) is evicted
  first. None of the line parsers was checked for seek-backs; this is item
  T5 in §9.

Without C, B helps only until one file read cycles the LRU. With C, the cache
behaves as "data streams through one or two slots, metadata stays".

---

## 7. Alternatives considered

| Option | Verdict |
|---|---|
| A second 9,000 B browser cache (keep the previous `.hcindex`) | **Not recommended.** It is the small form of the recorded failed approach "Caching every library index in RAM" (S090). It only helps back-and-forth between two libraries, and it needs a copy of the S090 P1 validity rules plus invalidation by the Save rebuild chain. B + C remove the directory-lookup part of *every* type change for less RAM. The index read itself (its data sectors) remains. |
| A general second scratch shared by copy/clear, AutoSave, etc. | Not recommended: it brings back the loan, the IDLE requirement and the refusal gate (§1). |
| Larger `staging_buf` (512 B) or `sample_io_buf` (516 B) | No gain: the card moves 16 B per poll; buffer size does not change the number of sector transfers. |
| Moving the install's wavetable frame out of the name cache | No gain anyone sees (audio is suspended during install). |
| DTCM | Not available: 8 B free; the arena is reserved for delay lines. |
| SRAM2 | It is the stack (no high-water mark measured). |
| Multi-block SD writes (`AFATFS_MIN_MULTIPLE_BLOCK_WRITE_COUNT`) | Not a RAM question; the shim has no CMD25. Out of scope. |

---

## 8. RAM request (for sign-off)

| Owner | Object | Region | Bytes | Lifetime |
|---|---|---|---:|---|
| `filesystem.c` | `filesystem_copy_clear_scratch_t` (new static) | SRAM1 `.bss` | 1,932 | Firmware; contents valid for one copy/clear interaction |
| `asyncfatfs.c` | `afatfs.cache[]` + `cacheDescriptor[]`, 8 → 16 slots | SRAM1 `.bss` | 4,224 | Firmware |
| | **Total (A + B)** | | **6,156** | |

Later option only, not part of this request: 16 → 24 slots, a further
+4,224 B (A + B = 10,380 B).

Alignment may add a few bytes; measure after the build with
`arm-none-eabi-size -A` and `link_budget.py`.

**SRAM1 headroom.** `_ebss` is `0x2006521C` and SRAM1 ends at `0x2007C000`,
leaving **93,668 B** free in this DEV link. That figure includes the
temporary 16,384 B `autosave_trace_records` ring. After A + B:
**≈ 87,512 B** (≈ 83,288 B if B later goes to 24).

**Pattern reservation.** Free SRAM1 is earmarked for Pattern data. Raising
`PAT_STACK_SIZE` from 256 to 384 adds 4,096 B of pool to each of the 17
regions (16 + `pat_background_region`), 69,632 B in total. That still fits
after this proposal. 512 would need 139,264 B, which did not fit before
either.

---

## 9. Phasing and tests

Each phase is measured with `make all`, `link_budget.py` and the section
ledger. None of them touches DSP paths.

- **P1 — Proposal A.** Device tests:
  - T1: copy a Kit or Instrument while an AutoSave drain is running. The job
    finishes without waiting for the drain, and the names are correct after a
    power cycle.
  - T2: browse Kits, copy, then re-enter Load/Save. No `.hcindex` re-read
    (no `...` busy phase).
  - T3: a chained or swapped paste resolves to the original names
    (`filesystem_cacheCopyClearRemap()` snapshot).
  - T4: Load:[Samples] wavetable install still works through its loan.
- **P2 — Proposal C (0 B), then Proposal B at 16 sectors.** Do C first so B
  is measured with the hint in place. Device tests:
  - T5: index, payload and Bank loads give identical results, and no parser
    re-reads data after a seek back.
  - T6: Load/Save type-step time while playing, before and after (S090 P4/P5
    procedure).
  - T7: Bank load and Scene Save HCNAMES time.
  - T8: a forced stall record decodes with the new part 1 layout.

- **Decision point after P2.** If T6/T7 still show folder-lookup time at
  16 sectors, propose 24 sectors (+4,224 B) for a separate sign-off;
  otherwise stop at 16.

Timing comparisons need the device. Comparing SD read counts on the host is
not possible with the current tools.

---

## 10. Statements this would make out of date (for you to update)

- `MEMORY.md` §Storage / Filesystem, Copy/clear: "Name cache lent for 161 B
  HCNAMES remap."
- `MEMORY.md` §Storage / Filesystem, Names in RAM (S090 P1): the copy/clear
  clause, if A is accepted.
- `STORAGE_SRAM_MANIFEST.md` §8.2: the `fs_list_cache_name` row (loan
  description), the `afatfs` row (6,984 B), a new copy/clear scratch row, and
  the header totals.
- `COPYCLEAR_UTILITIES.md` §11.7 (the whole loan section), the §2 term row
  "9 kB name buffer", and the §17 resource row "Name buffer | 161 of 9,000".
- `FILESYSTEM_SPEC.md` "Copy/clear: name-buffer loan and the `HNcU` update
  (S075)".
- `MODULE_INTERCHANGE_SPEC.md` line 28 ("the filesystem name-buffer loan").
- `ASYNCFATFS_REFERENCE.md`: the cache size and the new discardable rule for
  regular-file reads.
- `DEV_MODES.md`: the stall record part 1 layout.
