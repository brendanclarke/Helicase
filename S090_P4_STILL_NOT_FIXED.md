# S090 P4 — Load/Save Still Slow; Scene Save Still Does Not Complete

**Status:** decided and implemented 2026-11-01 as S090 P5
(`S090_P5_IMPLEMENTATION.md`, which records the answers to §7 and the
deviations: F3 not built; F4 a production watchdog). Hardware test pending.
**Evidence:** `SD_CARD_TEST_5/` (card after the S090 P3 build, image
689,372 B), decoded with `tools/diagnostic_tools/decode_devlogs.py`.
**Follows:** `S090_P3_LOAD_SAVE_DEFECT_CORRECTION.md` (its §9 directory-tail
trim did not fix the user-visible symptoms).
**Baseline:** working tree of 2026-11-01 on `dev-ph7-voice_expansion` (HEAD
`ad4da190` + uncommitted S090 P2/P3). Line numbers are for that tree.

---

## 0. Report (user, 2026-11-01, verbatim)

> no change. Files at 'SD_CARD_TEST_5'.
> - changing between menu types is still much too slow, about 5-6 seconds
>   between types
> - Scene save did not complete still.

**Logged, not to be changed now (M1):**

> I need to have the first endless pot behave the same as the encoder would if
> it were selecting the load/save type. It doesn't it scrolls through ALL the
> load/save types instead of just the types for the whole system, or the types
> for the currently selected track button.

- **Where:** knob 0 in the Load/Save pot handler (`Core/Menu/menu.c` ≈11788)
  steps `menu_loadSaveLogicalCount()` / `menu_loadSaveApplyLogicalPosition()`,
  a combined list. The encoder's type row uses
  `menu_nextRestoredLoadSaveType()` (≈7052) at top level and the nested
  Instrument type step (≈9760) when a track button is selected.
- **Required:** pot 1 on the type row gives the same type sequence as the
  encoder in the same context.
- **Also:** pots are refused while a read runs (P1 decision). M1 must keep
  that, or explicitly admit pot type steps the way P1 admits encoder type steps.

---

## 1. What SD_CARD_TEST_5 shows

### 1.1 Boot (P2/P3 behave as specified)

| Tick | Record | Meaning |
|---|---|---|
| 569 | `d` | SD root repaired: 920 trailing deleted entries cut, marker after 102 live entries (P3 T1 **pass**) |
| 3,091 → 3,793 | `s` START/END | settle: one real scalar write (Scene 4 case-2 reload marked dirty), 702 ms, outcome ok |
| after 3,793 | — | no AutoSave record after boot (P2 P1 **pass**) |

An earlier short boot (#177261–#177272) ends at the boot reader summary with
no `d` or `s` record. It was probably powered off during boot before its trace
flushed (to confirm, §7 Q1).

### 1.2 Load/Save use (≈21 s → ≈75 s)

- **Kit REQUEST records** at 21.1, 22.2, 23.0, 23.2, 24.3, 25.4, 36.6, 36.9,
  62.0, 62.2 s.
- **Cancelled reads:** five `E LOAD_LIBRARY_INDEX phase 9` records and one
  `E SCAN_BANK_SCENES phase 16`. These are the P1 type-step cancels (phase 9 =
  "cancelled, return to root"), not faults.
- **No record times the reads.** The trace has no start/end record for a Menu
  index read, so the 5–6 s cannot be split into its parts from the card (F5).

### 1.3 Scene Save to slot 035

| Object on the card | State | Meaning |
|---|---|---|
| `/Scene/035 FluxMax/` | `Empty.pat`, `Kit Flux/` (7 files), `effects.fx`, `sceneset.scg`, identical to 033 | payload complete |
| `/.hcnames` | present, row 7 = `FluxMax 035 R`; no `.hcnamtmp` | **HCNAMES publish complete.** The P3 trim fixed the publish tail. |
| `/Scene/.hcindex` | row 33 = `FluxMax` (new), **row 35 empty** | the post-save index rewrite never ran |

- **Row 33 is not from this save.** Boot stages 4–9 (`main.c` ≈820–960)
  rescan every library root and rewrite every `.hcindex` on every boot. That
  added row 33 from the Test 4 save to 033.
- **The trace stops at ≈75.5 s** (the last `H` report at wrap + 9,964 ms).
  The trace flushes only on an idle facade, so the facade was busy from then
  until power-off, and every record of the Save was lost in RAM.
- **Where the save stopped:** after the HCNAMES publish and before the index
  write finished. The remaining chain
  (`filesystem_finish()` → `filesystem_startLibraryIndexRebuild()`,
  `filesystem.c` ≈4239/4470) is:
  1. FLUSH_FINISH (`afatfs_sync()`);
  2. `SCAN_SCENES` (`filesystem_scanScenes_tick()` ≈23158);
  3. `CREATE_LIBRARY_INDEX` (`filesystem_createLibraryIndex_tick()` ≈5393);
  4. FLUSH_FINISH again, then the Menu callback.
- **Still unknown:** whether it was very slow or truly stuck. Steps 2 and 3
  have **no stall bound** (only FLUSH_FINISH and HCNAMES have one), so a hang
  there would be a permanent `...`. A slow chain looks identical until it
  ends (§7 Q1).

---

## 2. Cause: one row or entry per main-loop pass, at about 460 passes/s

### 2.1 The rate limit

- While the facade is BUSY with audio running, `filesystem_tick()` makes
  **one** `afatfs_poll()` and **one** operation tick per main-loop pass
  (`filesystem.c` ≈28358). One poll clocks `SDCARD_BURST_SIZE` = 16 bytes.
- **Measured** (P3 §9.1, from trace timings of Instrument index reads): about
  **400–500 passes/s** while playing, which is about 2.2 ms per pass, one audio
  period.
- **SPI is not the limit.** The bit-bang runs without delays (`spi_sd.c`
  `bb_slow` = 0). A 16-byte burst costs about 20–30 µs, so SD work uses about
  1 % of the CPU.
- **The two existing speed-ups do not apply here:**
  - `fs_fast_drain_active` (four polls per pass) runs only with the codec
    suspended, i.e. stopped playback (`menu.c` 1962/1979);
  - `main_serviceBackgroundLoadIdle()` (extra ticks while both audio slots are
    full, S082 P2 fix 3, user-confirmed "works fine") runs only during a
    background Bank load (`main.c` ≈249).

### 2.2 Text work is limited per pass too, not only SD bytes

The new finding is that work which needs no SD access at all also waits for
the next main-loop pass:

| Loop | Code | Per pass | Cost of one call |
|---|---|---|---|
| Index read (Load/Save list) | `filesystem_loadLibraryIndex_tick()` phase 7 → `filesystem_readTextLine()` (≈12326) | **one row**, at most 16 bytes | Kit/Scene/Bank/Pattern `.hcindex` = **1,000 rows** (mostly empty) → ≥1,000 passes ≈ **2.2 s**, but only 3 sectors (≈100 polls) of SD data |
| Index write (after every Kit/Scene/Bank/Pattern Save) | `filesystem_createLibraryIndex_tick()` phase 6 | one row | 1,000 rows → ≈2.2 s |
| Scene scan (after Scene Save) | `filesystem_scanScenes_tick()` phase 4 | one directory entry | /Scene: 67 folders with LFNs ≈ 200 entries → ≈0.5 s plus ~7 sector reads |
| HCNAMES read/write (every Save, every name flush) | `filesystem_residentNames_tick()` | one row (to verify) | 161 rows read + 161 written |
| Save payload text writers (`.scg`, `.kcg`, Instrument files, `.fx`) | per-line writers (to audit) | one line (to verify) | hundreds of lines per Scene Save |

### 2.3 Estimated cost of one type change while playing (Load:Scene)

| Step | Passes (estimate) |
|---|---|
| Cancel tail of the previous type's read (close, chdir root) | 10–70 |
| `opendir` `/Scene` in root (root now ~7 sectors after the trim) | up to ~240 |
| `fopen` `.hcindex` in `/Scene` (~200 entries ≈ 7 sectors) | up to ~240 |
| **1,000 rows, one per pass**, plus 3 data sectors | **~1,100** |
| Close, chdir root | few |
| **Total** | **≈1,600 passes ≈ 3.5 s** |

- **Extra work per type:**
  - Bank: the child preview (`SCAN_BANK_SCENES`);
  - Kit: the Kit name session;
  - leaving Kit with dirty names: a resident-name flush, which is a full
    `.hcnames` rewrite (`menu_endResidentNameScratchSession()`, `menu.c`
    ≈16290).
- That accounts for the reported 5–6 s.
- **Accuracy:** the per-step figures are estimates. F5 measures them.

### 2.4 Estimated cost of a Scene Save while playing

- **Payload:** ≈21 KB written (≈1,300 polls of data), plus mkdir/create
  lookups and line-per-pass text writers.
- **Then:** HCNAMES (≈330 row passes plus temp/sync/remove/rename), flush,
  Scene scan (≈200 entry passes plus sectors), index write (≈1,000 row passes
  plus open/truncate/close), flush.
- **Total:** on the order of **5,000–10,000 passes ≈ 11–22 s** before the
  `...` clears.
- **P3 §9.1's "≈7 s"** came from a host replay that counted SD polls but not
  the per-row passes.

---

## 3. Fixes

The order follows certainty of gain and audio risk. F1 and F4 are
deterministic and do not touch audio scheduling. F2 adds SD time only in
windows where the audio queue is already full. F3 is held for a decision.

### F1 — Batch text rows per operation tick (no audio-path change)

| # | Change | Where |
|---|---|---|
| F1-1 | `.hcindex` read: parse every row in the bytes already cached, up to **512 bytes per tick**. Return WAIT only when `afatfs_fread()` has no cached data. | `filesystem_loadLibraryIndex_tick()` phase 7; a batched form of `filesystem_readTextLine()` (the shared helper stays unchanged for its other callers) |
| F1-2 | `.hcindex` write: hand AsyncFATFS as many rows per tick as `afatfs_fwrite()` accepts, up to 512 bytes | `filesystem_createLibraryIndex_tick()` phase 6 |
| F1-3 | Scene/Kit/Bank/Pattern scans: take up to **32 directory entries per tick** while `afatfs_findNext()` returns them without IN_PROGRESS | the four `filesystem_scan*_tick()` phase 4 |
| F1-4 | HCNAMES read/write rows: the same batching, if the audit confirms one row per tick | `filesystem_residentNames_tick()`, mirror read |
| F1-5 | **Audit** every Load/Save-chain loop that does one row, line or entry per tick (Kit/Scene/Bank/Instrument save and load writers and readers, Bank preview); batch those whose data is already cached | `filesystem.c` |

- **Why it is safe:**
  - each tick stays bounded (≤512 bytes of byte-wise parsing, about 5–10 µs);
  - SD access stays asynchronous and goes through AsyncFATFS's cache;
  - the result is the same bytes in the same order.
- **Expected gain:**
  - index read: ≈1,100 → ≈110 passes;
  - Load:Scene type change: ≈3.5 s → ≈1.3 s;
  - Scene Save: drops by about the row and entry counts in §2.2.
- **RAM:** 0 B (locals).

### F2 — Use the full-queue window for Menu Load/Save work

- **What:** generalise `main_serviceBackgroundLoadIdle()`, which today serves
  background Bank loads only. It also runs while a **Menu-owned** operation
  holds the facade:
  - a Load/Save command is active;
  - a Menu index or name read is running;
  - a Bank preview is running;
  - any operation started from `LOAD_PAGE`/`SAVE_PAGE` (Save chain, HCNAMES
    update, flush, rebuild).
- **Excluded:** AutoSave drains (P2 drops them on those pages) and trace
  flushes.
- **How:**
  - same loop and same `BG_LOAD_IDLE_FS_BUDGET_US` = 200 µs;
  - same test before each step (codec running, facade BUSY,
    `audioCodec_queueFreeSlots() == 0`);
  - `filesystem_tickUncounted()`, so stall counters are unchanged.
  - The new condition comes from a Menu accessor (e.g.
    `menu_loadSaveStorageWaiting()`), 0 B.
- **Why it is safe:** it is the S082 mechanism, hardware-confirmed. It runs
  only while both render slots are full, so a render waits for at most one
  bounded filesystem step, the same bound as the regular per-pass tick.
- **Gain:** depends on the slack per audio period, which the CPU widget shows
  (`audioCodec_getQueueFreePercent()`). Each extra step is about 25–50 µs, so
  every 0.5 ms of slack per period adds about 10–20 steps per period, against
  1 today. Measured by F5 (§7 Q3).
- **Supersedes S082 W5** ("no other load speed-ups") for Load/Save browsing and
  Save, if the user confirms (§7 Q2).

### F3 — More SD polls per busy pass while playing (decision; not proposed by default)

- **What:** while F2's condition holds, use `FS_FAST_DRAIN_POLL_PASSES` (four
  polls) per regular pass, as stopped playback already does.
- **Cost:** about 60–90 µs of extra foreground time on **every** pass,
  including passes where the queue is not full. That is a real underrun risk
  when the render is near its 2.18 ms budget.
- **Only if:** F1 + F2 measured on hardware miss the target (§7 Q4).

### F4 — Bound the Save chain's unbounded steps (ends a permanent `...`)

- **What:** add a **time-based** no-progress bound (TIM6 ms) to
  `SCAN_SCENES`/`SCAN_KITS`/`SCAN_BANKS`/`SCAN_PATTERNS` and
  `CREATE_LIBRARY_INDEX`. Progress means a phase change, an entry count change
  or a byte count change; the limit is 30 s without progress, the same rule as
  P2 A12.
- **On a trip:**
  - trace record `X` (new sites);
  - named error `ScSt` / `IxSt`;
  - error close-down through `filesystem_completeLibraryIndexRebuild(ERROR)`,
    which runs the parked Menu callback, so Menu shows the error overlay and
    the facade is released;
  - the trace then flushes, so the card holds the cause.
- **Why time-based:** a poll-count bound becomes a different wall time once F2
  changes poll density (P2 §2.8). The P3 C3-1 HCNAMES bound and the
  FLUSH_FINISH bound (50,000 polls ≈ 2 min while playing) stay as they are.
- **RAM:** 2 B (one 16-bit start tick for the observed step, SRAM1, owner
  `filesystem.c`). **Needs sign-off.** The scans and the index writer never
  run at the same time as FLUSH_FINISH or HCNAMES, so the existing
  `op_flush_stall_*` statics may be reusable: then 0 B, to be checked at
  implementation.

### F5 — Measure Load/Save timing (logging builds only)

- **What:** one trace record when each Menu-owned facade operation **starts**
  and one when it **ends**, holding: op id, kind/type, elapsed ms, main-loop
  passes, SD polls, and the facade owner it waited behind.
- **Save chain:** one checkpoint per sub-step (payload done, HCNAMES done,
  flush done, scan done, index done, final flush done).
- **Decoder:** `decode_devlogs.py` adds the stage and prints a per-operation
  timing line.
- **Why:** §2.3/§2.4 are estimates. F5 turns the next card into a measured
  breakdown and proves F1/F2's gain.
- **RAM:** ≤6 B (start tick, pass counter, poll counter), present only with
  `DEV_MODE_LOGGING`. **Needs sign-off.**
- **Limit:** records still flush only on an idle facade. A hung operation is
  recorded only after F4 ends it.

---

## 4. Out of scope

- AutoSave format, timing, budget or drains (P2 R3).
- `SDCARD_BURST_SIZE` (16 B). A larger burst has the same per-pass cost
  trade-off as F3.
- Replacing the post-save physical rescan with a direct index-row update
  (as Bank `DIRECT_WRITE` does). Possible later; after F1 the scan is small.
- The boot rewrite of every `.hcindex` (stages 4–9).
- M1 (pot 1 type list): logged only (§0).

---

## 5. Risks

| Risk | Handling |
|---|---|
| F1 batching changes parse results | The same bytes in the same order; the shared reader is untouched for other callers; a host test compares cache contents before and after on the TEST_5 indexes |
| F1 tick length | ≤512 bytes of byte parsing per tick (≈5–10 µs); no SD wait inside the loop |
| F2 underruns | Runs only with both render slots full; re-tests the queue before each step; same 200 µs budget as S082; listening test V9 |
| F2 also speeds a Bank preview or name flush started from the page | Intended: those are what the user waits on |
| F4 trips on a healthy slow card | 30 s with no progress at all; progress includes entry/byte counts |
| F5 lost on power-off | Only F4 makes a hang recordable; the test asks the user to wait (V7) |

---

## 6. Verification

**Static:** `make all && make img`; no new warnings; `link_budget.py`; a
logging-off build; a host check of F1 against the TEST_5 `.hcindex` files
(identical `fs_list_cache_name` contents).

**Hardware (logging on, PLAY running unless stated):**

| # | Test | Pass |
|---|---|---|
| V1 | Load page, step Kit → Scene → Bank → Pattern, waiting for names each time | each list shows in **≤1 s** (target, §7 Q4); F5 lines show the time per step |
| V2 | V1 on the Save page | same |
| V3 | Rapid type steps (5 detents), then settle | one read for the settled type only; names ≤1 s after settling |
| V4 | Nested Instrument Load, step types | ≤1 s per type |
| V5 | Scene Save to a new slot | `...` clears in **≤5 s**; `/.hcnames` row and `/Scene/.hcindex` row present; F5 checkpoints show each sub-step |
| V6 | V5 ×5, and Scene Save over an existing slot | each completes; no `.hcnamtmp`; index rows correct |
| V7 | If any save shows `...` longer than 30 s | **wait 3 minutes before power-off**. F4 must end it with the error overlay, and the trace then holds the `X` record |
| V8 | Kit, Bank, Pattern Save | complete; index rows correct |
| V9 | Listen throughout V1–V8 | no underrun (CPU widget noted before and after) |
| V10 | Stopped playback V1/V5 | no regression (fast drain unchanged) |

---

## 7. Questions (decisions needed)

**Q1. How long did you wait on `...` before power-off, and was PLAY
running?**
- §2.4 estimates 11–22 s for a healthy Scene Save while playing.
- If you waited well past that (e.g. over a minute), a real hang in the scan
  or the index writer is likely. F4 then becomes the priority, and V7 captures
  the cause.
- Also: was the short first boot in the trace a power-off during boot?

**Q2. Does this supersede S082 W5 ("no other load speed-ups") for Load/Save
browsing and Save?**
- **Recommendation: yes.** F1 and F2 are the speed-ups this report asks for.

**Q3. What does the CPU widget show while you test (the Scene you use)?**
- It tells how much slack F2 can use. No change is needed to read it; one
  number helps predict F2's gain.

**Q4. Target times while playing.**
- **Proposed:** a type change shows names in ≤1 s; a Scene Save clears `...`
  in ≤5 s.
- If F1 + F2 miss these, decide F3.

**Q5. RAM sign-off.**
- F4: 2 B SRAM1 (`filesystem.c`), or 0 B if the existing stall statics can
  be shared.
- F5: ≤6 B, logging builds only.

**Q6. Order.**
- **Recommendation:** F1 + F4 + F5 together (deterministic, no audio-path
  change, and the next card is measured), then F2 in the same build, so one
  hardware test covers both.
- **Alternative:** F2 in a separate step, if you prefer to isolate any audio
  effect.

---

## 8. Documents to update (at close-out)

- `FILESYSTEM_SPEC.md`:
  - batched row/entry ticks (F1);
  - the Menu idle-window rule (F2);
  - scan/index bounds (F4).
- `MODULE_INTERCHANGE_SPEC.md`: `main_serviceBackgroundLoadIdle()` scope.
- `DEV_MODES.md`, `AutosaveTrace.h`, decoder: F4 sites and the F5 stage.
- `MEMORY.md`:
  - Load/Save binding note;
  - Open Items: M1 (pot 1 type list).
- `knowledge_files/log_archive/090_SESSION_HANDOFF_LOG.md`: P4 section.
- `S090_P3_LOAD_SAVE_DEFECT_CORRECTION.md` §0/§9: D3 is not closed. The trim
  fixed the publish tail; the rest of the chain is limited per pass (§2).
