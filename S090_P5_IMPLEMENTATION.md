# S090 P5 — Implementation: Load/Save Speed and No-Freeze

**Status:** implemented 2026-11-01 on `dev-ph7-voice_expansion`. The first
image **froze at boot** (SD_CARD_TEST_6, §9); fixed and re-built. Hardware
test pending (§6).
**Plan:** `S090_P4_STILL_NOT_FIXED.md` (cause analysis §1–§2, fixes §3).
**Decisions (user, 2026-11-01):**

| # | Answer | Applied as |
|---|---|---|
| Q1 | "Play was running, well over 20s. Guarantee stuck, it is unacceptably long in any case." | F4 is a production watchdog (every build), not DEV-only |
| Q2 | "We can try adding maybe 50% more to the process budget for load/save." | F1 per-tick budget 15 µs (about half of one 16-byte SD burst); F2 idle window 300 µs (200 µs × 1.5) |
| Q3 | CPU widget "99-100" | The widget is the % of time the audio queue has a free slot (`audioCodec_getQueueFreePercent()`), so the render uses almost the whole period. F2's full-queue window rarely opens, and F3 (extra SD polls on every pass) was **not** built |
| Q4 | "as much speed up as possible that can never break audio or freeze" | Speed comes from doing the same work in fewer passes (F1), not from more SD time per pass; F4 guarantees release |
| Q5 | RAM approved; make the changes; document here | §4 |

---

## 1. What changed

### F1 — Rows, lines and entries are batched per tick

Each loop below used to handle **one** row, line or directory entry per
main-loop pass. It now loops inside one tick:
- only over data AsyncFATFS already holds;
- stopping at the first wait for the card, at end of file, on an error, or
  when `filesystem_batchHasTime()` says the tick's budget is spent.

The first item of every tick is always handled, as before.

| Loop | Function / phase | Typical items |
|---|---|---|
| Load/Save list read | `filesystem_loadLibraryIndex_tick()` phase 7 | 1,000 rows (Kit/Scene/Bank/Pattern) |
| Instrument list read | `filesystem_loadInstrumentIndex_tick()` phase 17 | 40–120 rows |
| List write after every Save | `filesystem_createLibraryIndex_tick()` phase 6 | 1,000 rows |
| HCNAMES read | `filesystem_residentNames_tick()` phase 2 | 161 rows |
| HCNAMES write | `filesystem_residentNames_tick()` phase 5 | 161 rows |
| Every text save file (`.scg`, `.kcg`, Instrument files, `.fx`, `.bcg`, `settings.cfg`, `midilrn.cfg`) | `filesystem_writeTextLine()` (16 callers, unchanged) | tens to hundreds of lines |
| Library scans (after Saves, at boot) | `filesystem_batchScanTick()` wrapping `filesystem_scanKits/Scenes/Banks/Patterns_tick()` phase 4 | ~200 directory entries in `/Scene` |

**Budget:**
- `FS_LOADSAVE_BATCH_BUDGET_US` = **15 µs** (`config.h`) once audio runs;
- `FS_PREAUDIO_BATCH_BUDGET_US` = 500 µs before audio (boot), where there is
  nothing to protect.

A pass therefore grows by at most 15 µs plus one item.

**How the pieces work:**
- **Text writer:** `filesystem_writeTextLine()` keeps its contract (1 = come
  back, 0 = done). Its old body is now `filesystem_writeTextLineStep()`, so
  the callers are unchanged.
- **Scan wrapper:** repeats a scan tick while it stays in phase 4 and its
  finder (`op_finder` or `op_object_finder`) moved. An unmoved finder means
  the entry is not cached yet, and the wrapper stops.

**Expected effect (estimates from P4 §2.3):**
- a Load:Scene type change drops by about 1,000 passes (≈2.2 s while playing)
  to roughly 1.3 s, which is mostly directory lookups;
- a Scene Save drops by about 1,000 + 330 + 200 + its text-line passes.

F5 measures the real figures.

### F2 — The full-queue window also serves Load/Save

- `main_serviceBackgroundLoadIdle()` (`main.c`) also runs while
  `menu_loadSaveStorageWaiting()` is true (Load/Save page open or a Load/Save
  command active), with `LOADSAVE_IDLE_FS_BUDGET_US` = **300 µs**.
- It uses the S082 rules unchanged: it runs only while both render slots are
  full and re-tests before each step, so a render is never delayed by more
  than one filesystem step.
- With the widget at 99–100 % the window rarely opens. It is kept because it
  costs nothing when closed and helps lighter Scenes.

### F4 — Runtime Load/Save watchdog (every build)

`filesystem_loadSaveWatchdog()` runs in `filesystem_tick()` before the
operation switch, after audio has started.

- **Covered operations:** Flush Finish, both index writers, all HCNAMES
  operations except the copy/clear remap, all Saves, all scans (including the
  Bank preview and Instrument scans), and the index, name and register reads.
- **Not covered:**
  - AutoSave drains and trace flushes (budget-paced, with their own
    observers);
  - loads that apply runtime state;
  - settings and MIDI Learn writes;
  - boot work.
- **Progress signature:** a one-byte fold of the owner, phase,
  `op_item_offset`, `op_bytes_done`, `op_stream_index`,
  `op_write_line_index`, `fs_list_cache_count` and the SD block of the
  current or last transfer. The SD byte offset and transfer state are left
  out on purpose: a transfer retried forever cycles them without progress.
- **Trip:** no signature change for `FS_LOADSAVE_WATCHDOG_MS` = **12 s**,
  timed in 64 ms units in one byte. The SD driver's longest legitimate wait is
  5 s.
- **On a trip:**
  1. logging builds write four `X` site-12 snapshot records;
  2. `op_file`, `op_kit_slot_dir` and `op_kit_root_dir` are closed without a
     callback;
  3. `fs_error_code` becomes `W` + the operation's error prefix + phase hex
     (e.g. `WScnSc04`, `WFlush00`, `WLIdx05`), which Menu's error overlay
     shows;
  4. `filesystem_finish(FS_STATUS_ERROR)` runs. That runs the parked callback
     chain, including a rebuild's original Save callback, so `...` ends and the
     facade is released.
- **Accepted residue:** an AsyncFATFS open still in flight can complete
  later. Each operation clears `op_file_ready` before its own open, and the
  next boot starts clean.
- **Unchanged:** the DEV poll-count detectors (HCNAMES 50,000, Flush 50,000,
  Bank preview 20,000, Save sites). At about 460 passes/s they fire later than
  the watchdog.

### F5 — Timing and hang evidence (logging builds)

- **New trace stage `w`** (`AUTOSAVE_TRACE_STAGE_FS_OWNER`): one record per
  facade owner change, written before and after the operation switch. It
  includes the direct hand-offs that bypass `filesystem_start()` (Save →
  HCNAMES → Flush → scan → index write → Flush). The decoder prints how long
  the previous owner held the facade.
- Transitions to or from a trace flush are not written, otherwise each flush
  would queue records for its own flush.
- **The stall snapshot** (`filesystem_recordStallSnapshot(site)`, formerly
  `filesystem_residentNamesRecordStall()`) now has a site parameter.
  Part 2 also carries the **open AsyncFATFS handle count** (bits 29..31,
  wait_ms narrowed to 13 bits), read by `afatfs_getStallSnapshot()` from
  `openFiles[]`.
- **Why the handle count:** both the Scene scan (phase 1, `afatfs_fopen`)
  and the index writer (phase 0, `afatfs_opendir_lfn`) simply retry when an
  open is refused. With all five handle slots taken, that is a silent
  forever-wait. It is the leading hypothesis for the Scene Save hang (§3).

---

## 2. Files

| File | Change |
|---|---|
| `config.h` | `LOADSAVE_IDLE_FS_BUDGET_US`, `FS_LOADSAVE_BATCH_BUDGET_US`, `FS_PREAUDIO_BATCH_BUDGET_US`, `FS_LOADSAVE_WATCHDOG_MS` |
| `Core/Hardware/SD/filesystem.c` | `filesystem_batchHasTime()`; batched loops (§1 F1); `filesystem_writeTextLineStep()`; `filesystem_batchScanTick()`; `filesystem_loadSaveWatchdogCovers/Signature()`, `filesystem_loadSaveWatchdog()`; `filesystem_traceOwnerTransition()`; `filesystem_recordStallSnapshot(site)` |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c/.h` | `afatfsStallSnapshot_t.open_files` and its count |
| `Core/Bank/Scene/AutosaveTrace.h` | stage `w`; site 12; part 2 layout note |
| `Core/Menu/menu.c/.h` | `menu_loadSaveStorageWaiting()` |
| `main.c` | `main_serviceBackgroundLoadIdle()` serves Load/Save |
| `tools/diagnostic_tools/decode_devlogs.py` | `w` branch with durations; site 12; part 2 handle count |
| Docs | `FILESYSTEM_SPEC.md` (session note and binding bullet), `DEV_MODES.md` (`w`, sites 11/12), `STORAGE_SRAM_MANIFEST.md`, `MEMORY.md`, `S090_P4_STILL_NOT_FIXED.md` status, `090_SESSION_HANDOFF_LOG.md` |

## 3. The Scene Save hang: what is and is not known

- **Proven from the TEST_5 card (P4 §1.3):** the payload and the HCNAMES
  publish completed. `/Scene/.hcindex` is byte-identical to the boot rewrite.
  AsyncFATFS writes unlocked dirty sectors on every poll, so the index writer
  never reached its `"w"` open (no truncation reached the card).
- **So the stall was in one of:**
  - the first Flush Finish;
  - `SCAN_SCENES`;
  - `CREATE_LIBRARY_INDEX` phases 0–4.
- **Ruled out by reading the code:**
  - `afatfs_flush()` cannot wait on a locked sector (it skips locked ones);
  - the new P1 cancel paths (library index, Instrument index, Bank preview)
    close everything they open.
- **Not proven:**
  - the leading hypothesis is that an open is refused forever, by handle
    exhaustion or by a `currentDirectory` that stays busy;
  - an AsyncFATFS cache-slot starvation remains possible.
- **What the next test shows:**
  - the error overlay code names the operation and phase, even with logging
    off;
  - the site-12 snapshot names the gate: open handles, cache slots, cwd busy,
    SD state.
- **C3-3 (the root-cause fix) follows from that code.**

## 4. RAM and flash

| Item | Bytes | Region / owner | Build |
|---|---|---|---|
| `fs_ls_wd_signature`, `fs_ls_wd_mark` | 2 | SRAM1 `.bss`, `filesystem.c` | every build (F4, approved) |
| `fs_trace_owner_op`, `fs_trace_owner_phase` | 2 | SRAM1 `.bss`, `filesystem.c` | `DEV_MODE_LOGGING` only (F5, approved ≤6 B) |

- **DEV link:** text 690,072, data 420, bss 413,028 (P3: 688,936 / 420 /
  413,020; +8 bss is the 4 B plus alignment).
- **Flash:** 690,492 of 753,664 B (headroom 63,172 B); image 690,508 B.
- **ITCM/DTCM/FXBUF:** unchanged (15,240 / 4,372 / margin 3,808).
- **Logging and stall detection off** (scratch copy): builds, no warning in
  the changed files; text 671,072, bss 396,104.

## 5. Verification done

- **Build:** `make all && make img` exit 0. The only warnings are the
  pre-existing unused-function/unused-parameter set.
- **Host test of the batched list read:**
  - **Setup:** the exact `filesystem_readTextLine()` and phase-7 loop source,
    extracted from `filesystem.c`, driven by a fake AsyncFATFS that releases
    the file in random 1–512-byte chunks with "not cached yet" gaps, and a
    budget that ends every 1–40 items.
  - **Files:** the four TEST_5 `.hcindex` files (1,000 rows each), 200 trials
    per file.
  - **Result:** identical cache contents to a direct parse, exactly one
    close, final phase 8 — **pass**.
- **Decoder:** compiles; synthetic `w` and site-12 records decode correctly.
- **Not run:** hardware.
- **Not host-tested:**
  - the writer, HCNAMES and scan loops (same pattern, reviewed);
  - the watchdog (needs the facade).

## 6. Hardware test (PLAY running unless stated; logging build)

| # | Test | Pass |
|---|---|---|
| T1 | Load page: step Kit → Scene → Bank → Pattern, waiting for names each time; note the time per step | clearly faster than 5–6 s; the trace `w` lines give each `LOAD_LIBRARY_INDEX` duration |
| T2 | Save page, same | same |
| T3 | Nested Instrument Load: step types | faster than before |
| T4 | **Scene Save to a new slot** | either it completes (`/.hcnames`, `.hcindex` row present), **or** within ≈12 s of the last progress the error overlay shows `W…`. **Write down the code on the screen.** Then wait 10 s and power off |
| T5 | T4 ×3, and a save over an existing slot | each completes or ends with a `W…` code; never a permanent `...` |
| T6 | Kit, Bank, Pattern Save | complete, or end with a code |
| T7 | Listen throughout | no dropouts (watch the CPU widget: it should not change during Load/Save) |
| T8 | Stopped playback T1/T4 | not slower than before |
| T9 | Leave a page during a save | no change in behaviour |

**Bring back:** the `W…` code(s) from the screen and the card
(`asavetrc.bin`). The `w` records and the site-12 snapshot decide the
root-cause fix.

## 7. Deviations from the P4 plan

1. **F3 not built.** Q3 shows no free render time while playing, so extra
   SD polls on every pass could cause dropouts (Q4).
2. **F4 covers more than scans and index writes.**
   - It covers every Load/Save-chained operation, uses one generic
     time-based rule instead of per-step bounds, and runs in every build
     (Q1).
   - Its RAM is the approved 2 B: a one-byte signature plus a one-byte
     64 ms clock instead of a 16-bit tick.
3. **F5 adds no counters.** The `w` records' own ticks time each step, so
   it uses 2 B (logging only) instead of the ≤6 B approved.
4. **The pre-audio batch budget (500 µs)** was not in the plan. It applies
   only before `audioCodec_init()`, so boot index work also speeds up.

## 8. Carried, not done

- **M1** (pot 1 Load/Save type list): logged in P4 §0; unchanged.
- **C3-3, the root-cause fix of the Scene Save hang:** waits for the T4
  code and snapshot.
- **Directory-lookup cost:** the remaining type-change time is root and type
  folder lookups at about 34 passes per sector. Caching the folder positions
  is a possible next step if T1 is still too slow.

## 9. Defect: boot freeze on the first P5 image (SD_CARD_TEST_6)

**Symptom:** the device froze at boot. There was no `bootlog.bin`, and the
trace gained 4,377 `w` records, almost all `FLUSH_FINISH -> idle (DONE)`,
about every 11 ms for 52 s.

**Cause (mine; F5 as first built):**
- `filesystem_traceOwnerTransition()` skipped transitions to or from the two
  trace-flush operations, but a trace flush ends through Flush Finish.
- So its `FLUSH_FINISH -> idle` edge was recorded. That record left the trace
  dirty, which started the next trace flush, and so on forever.
- **Effect on boot:**
  - all boot work completed normally up to the boot reader summary
    (1,694 ms);
  - the next boot step's facade request was then refused as busy forever,
    because the scheduler restarted a trace flush on every idle tick;
  - every flush re-armed the boot logging deadline, so the 10 s boot timeout
    never fired and no bootlog was written.
- Nothing else from P5 appears in the evidence: every scan, index write and
  read before the loop completed in milliseconds.

**Fix:**
- Only transitions with a counted owner on either side are written. Counted
  means a watchdog-covered Load/Save operation, never a trace flush, AutoSave
  work or Flush Finish (`filesystem_traceOwnerCounted()`).
- Flush Finish is attributed to the operation that entered it: one record,
  then that operation shows as the previous owner with last phase 0xFF.
- An uncounted operation's flush therefore writes nothing, so a record can
  never cause the next trace flush.

**Verification:**
- Host test: the real `filesystem_traceOwnerTransition()` and its covered
  list, extracted from `filesystem.c`, run against a model in which any
  pending record starts a trace flush:
  - trace flush, Pattern trace flush, AutoSave drain and an orphan flush:
    one flush, 0 records;
  - a Scene Save chain (Save, HCNAMES, flush, scan, flush, index, flush):
    8 records, and its trace flush runs once.
- Build: text 690,120, bss 413,028; image 690,556 B. The logging-off build
  is unchanged (text 671,072).

**Process lesson:** any trace record written from inside
`filesystem_tick()` must be checked for whether it can schedule the trace
flush that writes it. That check was missing from §5.
