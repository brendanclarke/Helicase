# Session 090 — Handoff Log

**Date:** 2026-10-31 / 2026-11-01
**Branch:** `dev-ph7-voice_expansion`
**Base commit:** `7f8b5d07` (S089 closeout) + uncommitted S089 files
**Tip at close:** all S090 work is uncommitted atop `7f8b5d07`
**Build (at P5 close, logging on):** `text 690,120`, `data 420`, `bss 413,028`
**Flash image:** 690,556 B (**headroom 63,108 B**)
**ITCM:** 15,240 / 16,384 B (unchanged)
**DTCM statics:** 4,372 B (unchanged)
**FX buffer margin:** 3,808 B (unchanged)

---

## End of session block

```
DATE: 2026-11-01
SESSION GOAL: Fix Load/Save speed while playing — blank names for many seconds
  after entering Load/Save, slow type changes, Scene Save freeze.
COMPLETED:
  - P1: RAM instrument snapshot (129 B) replaces .hctmp file write+read cycle;
    mirror-served .hcnames reads (VALID mirror skips the card); same-type index
    reuse (skip re-read on re-entry); type-step cancel (cancel running index
    read on type change). Temp file API and Morph helper removed.
  - P2: boot settle stage (stage 13) drains all start-up AutoSave work before
    audio starts, at full CPU. Load/Save entry drops running AutoSave drains.
    autosave_boot_hold blocks schedulers after a settle failure until first
    user mutation. HCNAMES convergence safe-cut at phases 70/77/72 only.
  - P3: three defects fixed (Save-page names not filled on completion, input
    locked during index read, Scene Save freeze from 922 trailing deleted FAT
    directory entries). Boot-time directory tail trim added. 50,000-poll stall
    bound on HCNAMES/Flush Finish. Stall snapshot records open AsyncFATFS
    handle count.
  - P4: root cause analysis (one row/line/entry per main-loop pass at ~460
    passes/s while playing).
  - P5: batch ticking (rows/lines/entries loop within one tick, budget 15 µs
    playing / 500 µs pre-audio). Full-queue Load/Save window (300 µs).
    Runtime watchdog (12 s, every build). Trace stage 'w' (facade owner
    transitions). Boot freeze defect found and fixed (infinite trace-flush
    loop from F5's own FLUSH_FINISH->idle records).
VERIFIED ON HARDWARE:
  - P1: not tested (implemented 2026-10-31, pending)
  - P2: settle stage and AutoSave drop behaviour confirmed from SD_CARD_TEST_5
    trace records (boot s START/END, no post-boot AutoSave)
  - P3: directory tail trim confirmed from SD_CARD_TEST_5 trace ('d' record,
    920 entries cut); the save and speed improvements were NOT confirmed (the
    per-row-per-pass bottleneck masked them)
  - P5: not tested (the first P5 image froze at boot — SD_CARD_TEST_6 — due
    to the trace-flush loop; fixed and rebuilt, hardware pending)

CHANGES THIS SESSION:
  - config.h: FS_SETTLE_NOPROGRESS_MS, FS_SETTLE_OVERALL_MS,
    FS_LOADSAVE_BATCH_BUDGET_US, FS_PREAUDIO_BATCH_BUDGET_US,
    FS_LOADSAVE_WATCHDOG_MS, LOADSAVE_IDLE_FS_BUDGET_US
  - Core/Bank/Scene/Preset/presetManager.h: pm_instrument_snapshot[] decl,
    preset_snapshotInstrument(), preset_snapshotCellRestored(),
    preset_restoreInstrumentMorphSnapshot(),
    preset_armInstrumentMorphCommit() decls; temp API removed
  - Core/Bank/Scene/Preset/presetManager.c: snapshot capture/restore/morph
    implementation; temp file functions removed; restore staging on stack
  - Core/Hardware/SD/filesystem.h: filesystem_browseReadRunning(),
    filesystem_browseReadCancel(), filesystem_settleAutosaveBlocking(),
    filesystem_trimDirectoryTailsBlocking(), filesystem_batchHasTime(),
    filesystem_loadSaveWatchdog(), filesystem_traceOwnerTransition(),
    filesystem_recordStallSnapshot(site) decls; temp file API removed
  - Core/Hardware/SD/filesystem.c: S1 snapshot browse-read API; S2 mirror-
    served reads (VALID guard); S4 index reuse (fs_list_cache_valid tag);
    S5 type-step cancel (phase 9 cancel paths for library/instrument/bank);
    settle stage blocking loop; AutoSave drop on Load/Save entry; directory
    tail trim (blocking boot pump); batch ticking (batched loops for index
    read/write, HCNAMES read/write, text writer, scan wrapper); watchdog
    (signature, mark, trip handler); trace owner transitions; stall snapshot
    with site parameter; temp file removal
  - Core/Hardware/SD/storageTypes.h/.c: Morph snapshot projection removed
  - Core/Hardware/SD/asyncfatfs/asyncfatfs.h/.c: afatfsTrimTail_t,
    afatfs_trimDirectoryTailBegin/Continue(); afatfsStallSnapshot_t.open_files
    and afatfs_getStallSnapshot() count
  - Core/Hardware/SD/asyncfatfs/sdcard_lxr02.h/.c: refused_run counter (2 B)
    in sdcardTransportSnapshot_t
  - Core/Bank/Scene/Autosave.h/.c: autosave_boot_hold (1 B); hold release in
    the two scalar funnels, semantic Pattern funnel, and mutation-tracking
    enable
  - Core/Bank/Scene/AutosaveTrace.h: stage 's' (settle), stage 'w' (facade
    owner), stage 'd' (directory trim); sites 11 (HCNAMES stall) and 12
    (watchdog); part 2 layout with open-files field
  - Core/Bank/Scene/Pattern/PatternStackService.h/.c:
    patSvc_repairEpochIdle() accessor
  - Core/Hardware/AudioCodecManager.h/.c: audioCodec_isInitialized() accessor
  - Core/Menu/menu.h/.c: menu_loadSaveStorageWaiting(); D1 Save name
    completion; D2 browse-nav gate (clicks, cursor moves, non-loading number
    turns admitted during index reads); type-step while busy; library index
    slot name blanking during reads; helpers (menu_loadSaveSlotIndexKind,
    menu_libraryIndexResident, menu_loadSaveSlotIndexReady,
    menu_blankLoadSaveNameWhileUnloaded)
  - main.c: boot stage 13 (settle call, edit-mask move); directory tail trim
    call; main_serviceBackgroundLoadIdle() serves Load/Save
  - tools/diagnostic_tools/decode_devlogs.py: 's' settle branch, 'w' owner
    branch with durations, 'd' directory trim branch, site 11/12 handling,
    part 2 handle count

KNOWN ISSUES INTRODUCED:
  - Scene Save hang root cause not yet identified (C3-3 awaits watchdog
    error code from hardware — the watchdog will now show the stuck
    operation and phase, or the save will complete)
  - M1: pot 1 on Load/Save type row should give the same type sequence as
    the encoder (logged, not built)
  - Hardware test of the full P5 build pending

KNOWN ISSUES RESOLVED:
  - Blank names for seconds after entering Load/Save while playing (P1 +
    P5 batch ticking)
  - 5-6 second type changes in Load/Save (P1 type-step cancel + P5 batch
    ticking)
  - 23 s recovery drain blocking Load/Save after boot (P2 settle stage)
  - Save-page names not filled by index completion (P3 D1)
  - Input locked during index read after type change (P3 D2)
  - Scene Save appearing to freeze (P3 D3 directory trim + P5 watchdog)
  - Boot freeze from trace-flush self-scheduling loop (P5 §9)

NEXT SESSION RECOMMENDED GOAL: Hardware test of the P5 build (SD_CARD_TEST_7).
  If T4 reports a W... error code, use it and the site-12 snapshot to identify
  and fix the Scene Save hang root cause (C3-3). If saves complete, close out
  S090 and the Load/Save speed-up work.
BLOCKERS: Hardware verification requires flashing build/LXRV2_lxr02.img (the
  P5 logging-on build, 690,556 B).

CRITICAL REMINDERS FOR NEXT SESSION:
  - CPU ceiling is binding (S089): no per-sample DSP cost increase
  - The watchdog (F4) is a production feature (every build), not DEV-only
  - Trace stage 'w' transitions must only be written for watchdog-covered
    (counted) owners — never for trace flushes, AutoSave or Flush Finish
    (the P5 §9 boot freeze)
  - HCNAMES convergence safe-cut phases are 70/77/72 ONLY (NOT 74 — a temp
    at 74 has 161 rows and would be promoted by boot)
  - The boot settle stage is synchronous (blocking SD polling) and runs
    BEFORE audioCodec_init() — no ISR-based code runs during it
  - Directory tail trim runs at boot before any .hcnames or index reads
```

---

## 1. Session 090 P1 — Load/Save Speed-Up

### 1.1 Background

The user reported that after booting, pressing PLAY, and opening Load/Save,
the file-name rows stayed blank for many seconds. Cycling between types
(Kit, Scene, Bank, Pattern) each took 5–6 seconds.

The root cause was a cascade of blocking SD operations:
- The `.hctmp.<ext>` hidden file write at Load/Save entry (a full instrument
  text save + read, many seconds at one SD burst per main-loop pass)
- The `.hcnames` resident-name register read (161 rows × 9 bytes, ~2.7 s)
- The library index read (~2.4 s per 1,000-row index at one row per pass)
- AutoSave recovery drains blocking the SD facade for 20+ seconds after boot

### 1.2 What changed (plan: `S090_P1_LOAD_SAVE_SPEEDUP.md`)

**S1 — Instrument kit snapshot (129 B).**
`pm_instrument_snapshot[129]` in `presetManager.c` (SRAM1 `.bss`). Captured
by `preset_snapshotInstrument()` at Load/Save entry; restored by
`preset_snapshotCellRestored()` on Load/Save exit, which walks every
descriptor row and restores Normal + Morphable Morph cells. A staging slot
(196 B on the stack) holds the restore image. The old `.hctmp.<ext>` temp
file API (`presetManager_startInstrumentTemp()`,
`presetManager_startInstrumentLoadTemporary()`,
`presetManager_startInstrumentRemoveTemporary()`) and the `storageTypes.c`
Morph helper projection were removed.

**S2 — Mirror-served `.hcnames` reads.**
When `hcnames_mirror_valid == FS_HCNAMES_MIRROR_VALID`, the Load/Save entry
path, `kit` row restore, and copy/clear remap read from
`hcnames_name_mirror[]` in RAM, skipping the SD card entirely. VALID means
the mirror has been loaded or written since boot and no interrupted write is
in progress. The mirror is always VALID after a successful boot
(`filesystem_ensureAutosaveFilesBlocking()` reads `.hcnames` and sets VALID
on close), so the first Load/Save entry after boot is served from RAM.

**S4 — Same-type index reuse.**
Re-entering Load/Save on the same type skips the index read when
`fs_list_cache_valid` is set and the cached type matches. The flag is
cleared by any Save, any scan, any type change, and on exit (per U16/Q3:
6a-i only, not 6a-ii).

**S5 — Type-step cancel.**
Changing the Load/Save type while an index read runs cancels the old read
(via phase 9 cancel paths in the library index reader, instrument index
reader, and bank preview). The encoder type row is admitted immediately.
Clicks and cursor moves between the type and number rows are admitted during
reads. Number turns on types that trigger loads (Kit/KitMrp Load, nested
Instrument Load) are refused until complete. The list loads for the type the
user settles on, not every type they pass through.

**S3 (boot seed) — no code needed.** The boot ensure operation already
reads `.hcnames` and sets the mirror VALID, so the first entry is served
from RAM.

**S6 (prompt retry) — no code needed.** The deferred dispatcher already
fires on the first idle poll.

### 1.3 User decisions (binding)

| # | Rule | Why |
|---|---|---|
| U7 | No library index pool; one browser cache only | each slot-numbered index can be up to ~9 KB; a pool that fits one test card is not a design |
| U12 | Do not change shared-cache users unless the speedup needs it | AutoSave, Pattern, Bank, copy/clear uses of both caches unchanged |
| U18 | Page display unchanged; blank means busy | names stay blank while their read runs |
| U19 | No added underrun risk; no work added to a main-loop pass | every change removes or cancels card work |
| U20 | One implementation; no side quests | trace format, decoder, AutosaveTrace.h unchanged by P1 |

### 1.4 Files and sizes (P1 only)

| File | Entries |
|---|---:|
| `Core/Bank/Scene/Preset/presetManager.h` | 4 |
| `Core/Bank/Scene/Preset/presetManager.c` | 7 |
| `Core/Hardware/SD/filesystem.h` | 3 |
| `Core/Hardware/SD/filesystem.c` | 34 |
| `Core/Hardware/SD/storageTypes.h` | 1 |
| `Core/Hardware/SD/storageTypes.c` | 1 |
| `Core/Menu/menu.c` | 31 |

Build after P1: text 683,616 / data 420 / bss 413,020; image 684,036 B.
Delta from baseline: text +104, data −4, bss +136.
`pm_instrument_snapshot` is 0x81 (129 B). All temp file symbols absent.

### 1.5 RAM (P1)

| Item | Bytes | Region | Owner | Build |
|---|---:|---|---|---|
| `pm_instrument_snapshot` | 129 | SRAM1 `.bss` | `presetManager.c` | all |
| stale flag (proposed for removal; gates kept instead) | 0 | — | — | — |
| restore staging | 196 | stack (foreground, during restore only) | `presetManager.c` | all |

---

## 2. Session 090 P2 — AutoSave Settle Stage and Load/Save Drop

### 2.1 Background

SD_CARD_TEST_2 showed a 23.7 s recovery drain after boot that owned the SD
facade. The drain re-validated both A/B records (2 × 34,768 B) and read the
winner's mask, at one 16-byte burst per main-loop pass while playing. When
the user opened Load/Save during this drain, the request waited until it
finished. A separate boot showed Load/Save blocked for 1.2 s by an
unidentified facade owner.

### 2.2 What changed

**Change A — Boot settle stage (stage 13).**
`filesystem_settleAutosaveBlocking()` runs before `audioCodec_init()`,
synchronously pumping `afatfs_poll()` + `filesystem_tick()` +
`autosave_tick()` at full CPU until all pending AutoSave classes complete.
No audio ISR runs during this stage, so the budget is unlimited and each
loop iteration is microseconds, not milliseconds.

Classes settled: scalar convergence (the recovery drain), Pattern
convergence, `settings.cfg`, `/.hcnames` publish. Trace flushes also run in
the loop (D5/Q6).

Bounds:
- `FS_SETTLE_NOPROGRESS_MS` = 30,000 (30 s without any poll-count progress)
- `FS_SETTLE_OVERALL_MS` = 120,000 (120 s total, ~10× worst-case estimate)

On failure: `autosave_boot_hold` (1 B, `Autosave.c`) is set, blocking all
three AutoSave schedulers until the first user-caused mutation. Release
points: `autosave_markPayloadOffsetDirty()`,
`autosave_maskPayloadRangeDirty()`, `autosave_markPatternDirty()`,
`autosave_setMutationTrackingEnabled()`. A failed settle is recorded with
trace stage `s` (logging builds only; no LCD notice, per D4).

**Change B — Load/Save drops AutoSave.**
Entering the Load/Save page fires the existing safe abort on any running
AutoSave drain. The HCNAMES convergence is cut only at phases 70, 77 and
72-while-rows-remain (NOT 74: a temp at phase 74 has 161 rows and would be
promoted by the boot prelude to a corrupt live register). Pattern drains
abort on the same page condition. Restart is 250 ms after exit, only if
still needed (existing behaviour).

**Boot order change:** `bank_revalidateVoiceEditMasks()` moved before the
settle stage (was after `audioCodec_init()`); `preset_startDrumsetApply()`
stays after `audioCodec_init()` (runtime only, no AutoSave marks).

### 2.3 Files and sizes (P2 only)

Files touched: `config.h`, `Core/Hardware/AudioCodecManager.c/.h`,
`Core/Bank/Scene/Pattern/PatternStackService.c/.h`,
`Core/Bank/Scene/Autosave.c/.h`, `Core/Bank/Scene/AutosaveTrace.h`,
`Core/Hardware/SD/filesystem.c/.h`, `main.c`,
`tools/diagnostic_tools/decode_devlogs.py`.

Build after P2: text 684,968 / data 420 / bss 413,020; image 685,388 B.
Delta from P1: text +1,352, bss unchanged (the 1 B boot hold landed in
alignment padding).

### 2.4 RAM (P2)

| Item | Bytes | Region | Owner | Build |
|---|---:|---|---|---|
| `autosave_boot_hold` | 1 | SRAM1 `.bss` (in alignment padding) | `Autosave.c` | all |

---

## 3. Session 090 P3 — Defect Correction

### 3.1 Background

SD_CARD_TEST_3 (the first hardware test of the P1+P2 build) revealed three
defects.

### 3.2 Defects and corrections

**D1 — Save-page names not filled by index completion.**
The Save page paints `preset_currentName`, but the index completion
(`menu_libraryIndexLoadComplete()`) only copied the slot name for Kit Load.
Scene/Bank/Pattern Save names stayed blank until a number turn.
**Fix (C1-1):** the completion now copies the current slot name on the Save
page for Scene/Bank/Pattern.

**D2 — Input locked during an index read after a type change.**
The P1 encoder gate admitted only edit-mode type turns during an index read.
Clicks, cursor moves and number turns were refused.
**Fix (C2-1..C2-6):** clicks and cursor moves between the type and number
rows are admitted during reads. Number turns are admitted except where they
trigger loads (Kit/KitMrp Load, nested Instrument Load). A busy repaint
blanks names while unloaded. Library slot-name accessors return blank during
a running Menu index read.

**D3 — Scene Save freeze from FAT directory bloat.**
Root cause (proven by host replay with realistic SD cost on the SD_CARD_TEST_4
card): the card's root directory held 922 trailing deleted entries with no
end-of-directory marker. Every FAT lookup that missed early had to read all
64 sectors at 16 bytes per main-loop pass. The HCNAMES update after a Scene
Save took 8,296 polls (~18+ s while playing) instead of 1,187 polls (~2.6 s).
The save was slow, not hung; it was powered off mid-update.

**Fix — Boot-time directory tail trim (C3):**
`asyncfatfs_trimDirectoryTail()` scans a directory to its marker or
allocated end; if deleted entries follow the last live entry, it writes an
end-of-directory marker (one sector write). `filesystem_trimDirectoryTailsBlocking()`
trims root, `/Scene`, `/Kit`, `/Bank`, `/Pattern`, `/Instrument`, and
every `/Instrument` subdirectory (enumerated; future instrument types are
covered automatically; bound 64). Runs at boot before any `.hcnames` or
index reads. One-time repair; subsequent boots skip clean directories.

**Safety net — C3-1 stall bound:** 50,000-poll stall detector for HCNAMES
and Flush Finish operations (DEV builds). At ~460 passes/s while playing
this is ~2 minutes; it fires before the watchdog.

**Safety net — C3-2 stall snapshot:** `filesystem_recordStallSnapshot(site)`
writes four `X` records. Part 2 now carries the open AsyncFATFS handle count
(bits 29..31 of the part 2 word, `afatfs_getStallSnapshot()`). The SD
transport snapshot includes `refused_run` (consecutive refused READ/WRITE
commands, 2 B in `sdcard_lxr02.c`).

### 3.3 Files and sizes (P3 only)

Files: `Core/Menu/menu.c`, `Core/Hardware/SD/filesystem.c/.h`,
`Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h`,
`Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h`,
`Core/Bank/Scene/AutosaveTrace.h`, `main.c`,
`tools/diagnostic_tools/decode_devlogs.py`.

Build after P3: text 688,936 / data 420 / bss 413,020; image 689,372 B.
Logging off: text 668,856, bss 396,116.

### 3.4 RAM (P3)

| Item | Bytes | Region | Owner | Build |
|---|---:|---|---|---|
| `refused_run` in `sdcardTransportSnapshot_t` | 2 | SRAM1 `.bss` | `sdcard_lxr02.c` | all |
| trim state | 0 | stack (boot only) | `filesystem.c` | — |

---

## 4. Session 090 P4 — Root Cause Analysis: Still Slow

### 4.1 Background

SD_CARD_TEST_5 (the P3 build) showed type changes still 5–6 s, and a Scene
Save still did not complete. The P3 directory tail trim had fired
successfully (920 entries cut), and the P2 settle stage worked (one real
scalar write, 702 ms), but the user-visible speed was unchanged.

### 4.2 Root cause

Every row, line and directory entry in the filesystem was processed one per
main-loop pass. At ~460 passes/s while playing:
- 1,000-row index: ~2.2 s just for the data loop
- 161-row HCNAMES: ~350 ms
- Text file lines, scan entries: additional seconds per operation

The P1 changes eliminated the most expensive single operations (temp file,
mirror read, index reread), but the per-row-per-pass iteration made every
remaining operation proportional to its row count.

### 4.3 User decisions (P4/P5)

| # | Decision |
|---|---|
| Q1 | "Guarantee stuck, it is unacceptably long in any case." → F4 is a production watchdog (every build), not DEV-only |
| Q2 | "50% more budget" → F1 15 µs playing budget, F2 300 µs idle window |
| Q3 | CPU widget 99–100% → F3 (extra SD polls) not built |
| Q4 | "As much speed up as possible that can never break audio or freeze" |

### 4.4 The Scene Save hang: what is known and not known

- The payload and HCNAMES publish completed on the TEST_5 card.
- The `/Scene/.hcindex` was not rewritten (row 35 empty), so the stall was
  in one of: the first Flush Finish, `SCAN_SCENES`, or
  `CREATE_LIBRARY_INDEX` phases 0–4.
- **Leading hypothesis:** an `afatfs_fopen` or `afatfs_opendir_lfn` refused
  forever due to handle exhaustion (all 5 slots taken) or a busy
  `currentDirectory`. Not proven.
- The P5 watchdog and stall snapshot will show the stuck operation and phase
  on the next hardware test.

---

## 5. Session 090 P5 — Batch Ticking, Watchdog and Trace

### 5.1 F1 — Batch ticking

Every row/line/entry loop now iterates within a single tick, stopping at the
first SD wait, end-of-file, error, or budget expiry
(`filesystem_batchHasTime()`).

| Loop | Function / phase | Typical items |
|---|---|---|
| Library index read | `filesystem_loadLibraryIndex_tick()` phase 7 | 1,000 rows |
| Instrument index read | `filesystem_loadInstrumentIndex_tick()` phase 17 | 40–120 rows |
| List write after Save | `filesystem_createLibraryIndex_tick()` phase 6 | 1,000 rows |
| HCNAMES read | `filesystem_residentNames_tick()` phase 2 | 161 rows |
| HCNAMES write | `filesystem_residentNames_tick()` phase 5 | 161 rows |
| Text save files | `filesystem_writeTextLine()` → `filesystem_writeTextLineStep()` | tens–hundreds of lines |
| Library scans | `filesystem_batchScanTick()` phase 4 | ~200 directory entries |

Budget:
- `FS_LOADSAVE_BATCH_BUDGET_US` = **15 µs** while audio runs (about half
  of one 16-byte SD burst)
- `FS_PREAUDIO_BATCH_BUDGET_US` = **500 µs** before audio (boot), where
  there is nothing to protect

The text writer keeps its contract (`filesystem_writeTextLine()` returns
1 = come back, 0 = done); its old body is `filesystem_writeTextLineStep()`,
so all 16 callers are unchanged. The scan wrapper repeats a scan tick while
it stays in phase 4 and its finder moved (unmoved = entry not cached yet).

### 5.2 F2 — Full-queue Load/Save window

`main_serviceBackgroundLoadIdle()` also runs while
`menu_loadSaveStorageWaiting()` is true, with `LOADSAVE_IDLE_FS_BUDGET_US` =
300 µs. It uses the S082 rules: runs only while both render slots are full,
re-tests before each step. With the CPU widget at 99–100% the window rarely
opens.

### 5.3 F4 — Runtime watchdog (every build)

`filesystem_loadSaveWatchdog()` runs in `filesystem_tick()` before the
operation switch, after audio has started.

**Covered:** Flush Finish, both index writers, all HCNAMES operations
(except copy/clear remap), all Saves, all scans (Bank preview, Instrument
scans), index/name/register reads.

**Not covered:** AutoSave drains (budget-paced, own observers), trace
flushes, loads that apply runtime state, settings/MIDI Learn writes, boot
work.

**Progress signature:** one-byte fold of owner, phase, `op_item_offset`,
`op_bytes_done`, `op_stream_index`, `op_write_line_index`,
`fs_list_cache_count` and the SD block of the current/last transfer. SD byte
offset and transfer state excluded (a transfer retried forever cycles them
without progress).

**Trip:** no signature change for `FS_LOADSAVE_WATCHDOG_MS` = **12 s**,
timed in 64 ms units in one byte. On trip:
1. Logging builds write 4× `X` site-12 snapshot records
2. `op_file`, `op_kit_slot_dir`, `op_kit_root_dir` closed without callback
3. `fs_error_code` = `W` + operation prefix + phase hex (e.g. `WScnSc04`,
   `WFlush00`, `WLIdx05`)
4. `filesystem_finish(FS_STATUS_ERROR)` runs the parked callback chain

RAM: 2 B (`fs_ls_wd_signature` + `fs_ls_wd_mark`, every build).

### 5.4 F5 — Trace stage `w` (logging builds)

One record per facade owner change. Includes direct hand-offs that bypass
`filesystem_start()` (Save → HCNAMES → Flush → scan → Flush → index →
Flush). The decoder prints how long the previous owner held the facade.

**Critical rule:** only transitions with a counted (watchdog-covered) owner
on either side are written (`filesystem_traceOwnerCounted()`). Flush Finish
is attributed to the operation that entered it (one record, shown as last
phase 0xFF).

RAM: 2 B (`fs_trace_owner_op` + `fs_trace_owner_phase`, logging only).

### 5.5 P5 boot freeze defect (SD_CARD_TEST_6)

The first P5 image froze at boot. The trace gained 4,377 `w` records,
almost all `FLUSH_FINISH -> idle (DONE)`, about every 11 ms for 52 s.

**Cause:** `filesystem_traceOwnerTransition()` skipped transitions to/from
the two trace-flush operations, but a trace flush ends through Flush Finish.
Its `FLUSH_FINISH -> idle` edge was recorded. That record left the trace
dirty, scheduling the next trace flush, forever. The boot's facade was
permanently busy.

**Fix:** only transitions with a counted owner on either side are written.
An uncounted operation's flush writes nothing, so a record can never cause
the next trace flush.

**Process lesson:** any trace record written from inside `filesystem_tick()`
must be checked for whether it can schedule the trace flush that writes it.

### 5.6 Files and sizes (P5, cumulative)

| File | Change |
|---|---|
| `config.h` | `LOADSAVE_IDLE_FS_BUDGET_US`, `FS_LOADSAVE_BATCH_BUDGET_US`, `FS_PREAUDIO_BATCH_BUDGET_US`, `FS_LOADSAVE_WATCHDOG_MS` |
| `Core/Hardware/SD/filesystem.c` | `filesystem_batchHasTime()`; batched loops; `filesystem_writeTextLineStep()`; `filesystem_batchScanTick()`; watchdog (covers/signature/trip); `filesystem_traceOwnerTransition()`; stall snapshot site parameter |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h` | `afatfsStallSnapshot_t.open_files` count |
| `Core/Bank/Scene/AutosaveTrace.h` | stage `w`; site 12; part 2 layout |
| `Core/Menu/menu.c/.h` | `menu_loadSaveStorageWaiting()` |
| `main.c` | `main_serviceBackgroundLoadIdle()` serves Load/Save |
| `tools/diagnostic_tools/decode_devlogs.py` | `w` with durations; site 12; part 2 handle count |

Final build (P5, all phases): text 690,120 / data 420 / bss 413,028;
image 690,556 B. Logging off: text 671,072, bss 396,104.

### 5.7 RAM (P5, cumulative from all phases)

| Item | Bytes | Region | Owner | Build |
|---|---:|---|---|---|
| `pm_instrument_snapshot` | 129 | SRAM1 `.bss` | `presetManager.c` | all |
| `autosave_boot_hold` | 1 | SRAM1 `.bss` (padding) | `Autosave.c` | all |
| `refused_run` | 2 | SRAM1 `.bss` | `sdcard_lxr02.c` | all |
| `fs_ls_wd_signature` + `fs_ls_wd_mark` | 2 | SRAM1 `.bss` | `filesystem.c` | all |
| `fs_trace_owner_op` + `fs_trace_owner_phase` | 2 | SRAM1 `.bss` | `filesystem.c` | logging only |
| restore staging | 196 | stack (foreground) | `presetManager.c` | all |
| **Static total** | **134 all** / **136 logging** | | | |

---

## 6. Verification Summary

### 6.1 Build gates (all phases)

Every phase was pre-verified in a session scratchpad copy of the working
tree, built with logging on and off, before application to the tree.
Final `make all && make img` exit 0, same pre-existing warning set.

### 6.2 Host tests

- **P1:** static proof that the RAM restore is identical to the file restore
  by construction (same cells, same values, same Scenes).
- **P3:** host replay of the card's root directory with realistic SD cost
  (16-byte bursts, ~34 polls/sector) on both the bloated and trimmed
  directories; HCNAMES poll counts match prediction.
- **P5:** the batched list-read loop (`filesystem_readTextLine()` + phase-7
  loop) driven by a fake AsyncFATFS with random chunk sizes and budget
  breaks; 200 trials per file on the four TEST_5 `.hcindex` files; identical
  cache contents, one close, final phase 8.
- **P5:** the trace owner transition function driven against a model where
  any pending record starts a trace flush: trace flush / AutoSave / orphan
  flush produce 0 records; a Scene Save chain produces 8 records and one
  trace flush.

### 6.3 Not verified

- No hardware test of P1 or P5 at session close.
- P3 hardware showed the directory trim working, but speed improvements were
  masked by the per-row-per-pass bottleneck (fixed in P5).
- The writer, HCNAMES and scan batch loops are reviewed but not host-tested
  (same pattern as the tested index reader).
- The watchdog needs the facade to test.

---

## 7. Deviations and Carried Items

### 7.1 Deviations from plans

| Plan | Deviation |
|---|---|
| P1 plan §10 Q2 | `names_stale` 1 B flag proposed for removal; existing gates used instead (0 B) |
| P4 §3 F3 | Not built: CPU widget 99–100%, no free render time for extra SD polls (Q3/Q4) |
| P4 §3 F4 | Covers more operations than planned; uses one generic time-based rule instead of per-step bounds; runs in every build (Q1) |
| P4 §3 F4 | RAM is 2 B (signature + mark) instead of tick-based counter |
| P5 | Pre-audio batch budget (500 µs) added, not in the P4 plan |

### 7.2 Carried items

- **M1 — Pot 1 Load/Save type list:** pot 0 on the type row should give
  the same type sequence as the encoder in the same context. Logged in P4 §0.
- **C3-3 — Scene Save hang root-cause fix:** the watchdog will show the
  stuck operation and phase, or the save will complete. Awaiting hardware.
- **Directory-lookup cost:** if type-change time is still too slow after
  batch ticking, the remaining time is root and type folder lookups at ~34
  passes per sector. Caching folder positions is a possible next step.

---

## 8. Build Progression

| Phase | text | data | bss | image | headroom |
|---|---:|---:|---:|---:|---:|
| Baseline (S089) | 683,512 | 424 | 412,884 | 683,936 | 69,728 |
| P1 | 683,616 | 420 | 413,020 | 684,036 | 69,628 |
| P2 | 684,968 | 420 | 413,020 | 685,388 | 68,276 |
| P3 | 688,936 | 420 | 413,020 | 689,372 | 64,292 |
| P5 (final) | 690,120 | 420 | 413,028 | 690,556 | 63,108 |
| P5 logging off | 671,072 | — | 396,104 | — | — |
