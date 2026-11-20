# S099 P3 — AutoSave starved by the diagnostic trace (2026-11-19)

## 0. The rule (binding, permanent)

**Development logging, tracing, probes and every other diagnostic must NEVER
take priority over a core system function.**

Core functions include:
- AutoSave (the scalar parameter drain, the Pattern drains);
- settings, MIDI Learn and HCNAMES persistence;
- Load/Save;
- audio, the sequencer and the user interface.

Diagnostics run **only** in time that no core function wants. In practice:
1. **Last in the queue.** In any scheduler, queue or arbitration ladder, a
   diagnostic claimant comes after every core claimant. No exceptions, and no
   "just one batch first" special cases.
2. **Never block, delay or reorder core work.** A diagnostic must not do so to
   protect its own evidence. If the trace loses records, it counts and reports
   the loss (stage G). Losing trace records is acceptable; losing user data is
   not.
3. **A logging build must behave like a release build for every core
   function.** DEV_MODE_LOGGING may *add* work in idle time. It must never
   change *when* or *whether* core work happens.
4. **Review every new diagnostic against rules 1–3 before it ships.** The
   question to answer, in writing, is: *can this ever make a core function
   wait?* If yes, it does not ship.

This rule outranks any diagnostic design note, including the one removed here
(§3).

## 1. What the user saw

"NOTHING IS AUTOSAVING." The card is `SD_CARD_ADV_2/`. The firmware is
730,248 B, a DEV_MODE_LOGGING build (`config.h` `DEV_MODE_LOGGING 1`), and
identical to the build tree at the time.

## 2. Evidence (`SD_CARD_ADV_2/asavetrc.bin`)

**Method:**
- decoded with `tools/diagnostic_tools/decode_devlogs.py`;
- the session is records 1,280,440–1,388,260, the part appended since the
  previous card;
- the boots were found from the boot-reader records (stage Q).

| Boot (records) | Dirty marks (D) | Writer armed (S) | Drains admitted (A) | Published (P) | Notes |
|---|---:|---:|---:|---:|---|
| 1 (1,280,534–) | 6,129 | 20 | 27 | 26 | works, but armed-to-admitted waits of 4–16 s |
| 2 (1,310,999–) | 2,933 | 9 | 19 | 19 | works until record 1,337,653; then no edits |
| 3 (1,363,904–) | 1,168 | 1 | 1 (boot) | 0 | armed at tick 24,700, due 29,700; **never admitted** in the following 16 s; then held on the Load/Save page (W) |
| 4 (1,368,520–) | 3,700 | **0** | 1 (boot) | 1 (boot only) | edits marked dirty, writer **never armed** |
| 5 (1,384,376–) | 1,141 | 1 | 2 | 0 | one drain ended in error cleanup (phase 41), then held (W) |

**The previous card (`SD_CARD_ADV/`, the earlier firmware, also a logging
build) already showed it.** The armed-to-admitted waits were 5.4, 7.7, 7.7,
8.5, 9.3, 10.4, 12.0, 16.5, 22.2, 22.3, 25.6 and **52.9 s**, against a 5 s
debounce. Autosave was being starved there too, only less visibly.

**What the filesystem was doing in the gap.** In boot 3, between the arm and
the Load/Save visit:
- no filesystem operation other than the trace flush ran;
- every one-second main-loop pass probe (stage f) reports
  `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` as the operation in progress;
- across all five boots, 82–91 % of the pass probes land inside a trace
  flush.

## 3. Root cause

`filesystem_tick()` hands the one filesystem facade to background claimants in
a fixed order, each gated on `status == FS_STATUS_IDLE`. Before this fix the
order was:

1. MIDI Learn writer
2. settings writer
3. **AutoSave Trace flush (diagnostic)**
4. **PatternTrace flush (diagnostic)**
5. deferred HCNAMES flush
6. **AutoSave scalar writer** (arms the debounce; starts the parameter drain)
7. AutoSave Pattern drain
8. AutoSave non-semantic Pattern drain

An earlier session put the trace (3, 4) ahead of the writer (6) on purpose,
with this comment: "Persist a pending diagnostic batch before an eligible
AutoSave drain … a pending logging-build trace has one append first." The
aim was to make a J/I witness batch durable before a long first drain.

**The failure.** A logging build *always* has pending trace records:
- the S094 P2 cycle probe alone writes 16 records every 1.5 s;
- the pass probe writes one every second;
- when a batch is full, the flush completion sets the next due tick to 0 (an
  immediate continuation);
- each flush appends to an 11 MB `asavetrc.bin`.

So the trace flush took the facade in nearly every idle pass. Once it has the
facade, `status` is BUSY, so claimants 5–8 are not even called in that pass:
- **the writer never got to arm** (boot 4: no S record at all);
- **an armed, due drain was never admitted** (boot 3);
- the Pattern drains starved in the same way.

"One append first" had become "always first". That is a diagnostic taking
priority over saving the user's data, the exact thing §0 forbids.

**A release build (`DEV_MODE_LOGGING 0`) is not affected:** the trace
schedulers compile to nothing. That does not make this acceptable; see §0
rule 3.

## 4. The fix (`Core/Hardware/SD/filesystem.c`, `filesystem_tick()`)

The two diagnostic flushes are moved to the end of the ladder, after every core
claimant:

1. MIDI Learn writer
2. settings writer
3. deferred HCNAMES flush
4. **AutoSave scalar writer**
5. AutoSave Pattern drain
6. AutoSave non-semantic Pattern drain
7. AutoSave Trace flush (diagnostic): only if nothing above took the facade
8. PatternTrace flush (diagnostic): likewise

- **Code:** the trace and PatternTrace calls, and the comment that justified
  their old place, are removed from before HCNAMES. They are re-added, with
  unchanged gates, after `filesystem_autosaveNonSemanticPatternDrainSchedule_tick()`.
  The writer's comment now records why (§3) and points here.
- **Unchanged:**
  - every gate, debounce, deadline, file format and drain state machine;
  - the trace still never runs on a Load/Save page or during a command;
  - its ring keeps filling and counts drops (stage G).
- **The cost moves to the diagnostics:** while AutoSave is busy the trace
  waits, and a long drain may drop some trace records, which the G record
  reports. That is the intended trade.

**Build:** `make img` writes **730,304 B**, with no new warning (the
`filesystem.c` warnings listed by the full rebuild are the standing
unused-function ones). `make -C tools/dsp_test all` exits 0 with 87 breaks
caught.

## 5. Audit of the other diagnostic paths (this session)

| Site | What it does | Verdict |
|---|---|---|
| `filesystem_autosaveTraceFlushSchedule_tick()` | starts a trace append when records are pending and the interval is due | fixed by its place in the ladder (§4) |
| `filesystem_patternTraceFlushSchedule_tick()` | the same for PatternTrace | fixed by its place in the ladder (§4) |
| `filesystem_autosaveTraceFlushCompleted()` | a full batch sets the next due to 0 (immediate continuation) | acceptable now: with the trace last, a continuation can only use passes no core claimant wants |
| `filesystem_autosaveTraceFlushBlocking()` | bench helper that pumps the ring to the card | not called by the runtime; must stay bench-only |
| `filesystem_passProbe()` | records the longest main-loop pass; skips trace flushes | records only; takes no facade |
| `mixer_probeEnd()` (S094 P2 cycle probe) | 16 records per 2,048 blocks | records only; this volume is what kept the ring non-empty |

**Not yet audited:** every other `#if DEV_MODE_LOGGING` block for waits,
retries or ordering that touches core work (the menu's Load/Save
instrumentation, the boot log). Rule §0 applies to them, and they should be
checked the same way.

## 6. Also seen on this card (separate from the starvation)

- **Boot 4's boot drain captured 1,104 patches** (`case2_scene_mask=0x0010`,
  Scene 4). That is the recovery path re-marking Scene 4 after its
  Instrument slot 2 had been replaced with an Advanced Alkaline. The alx
  row count also changed this session (38 → 35, S099 P2 §10). It completed
  and published (generation 2,823); no data was lost there.
- **Boot 5's drain error (stage E, `AUTOSAVE_PARAMETER_DRAIN` phase 41)** is
  the error cleanup. The record does not say which earlier step failed. A
  Kit-page entry (stage O) came 4 ms before it. Under the old order the
  following retry would also have been starved. Re-check on the next card.

## 7. To verify on the device

1. Flash the 730,304 B image with logging on, edit parameters, and wait about
   5 s. Expect S, A, V, M, C, P and T records each time, with the
   armed-to-admitted wait close to the 5 s debounce (not 5–53 s).
2. Power-cycle and confirm the edits came back.
3. Visit the Instrument Load/Save page, leave it, edit again: the writer must
   still arm and publish (boots 3 and 4 failed here).
4. Expect more G (trace dropped) records than before. That is the correct
   trade.

## 8. Statements now out of date (for the user)

- **`MEMORY.md`:** add rule §0 to the AutoSave / DEV logging notes. I have not
  edited `MEMORY.md` (the project rule).
- **The S090 P5 / S097 P3 notes on trace-before-writer ordering** (the
  removed "pre-drain diagnostic witness") are superseded by §4.

## 9. Second failure (`SD_CARD_ATS_3`, 2026-11-20): the fixed order was not enough

**The user's test:**
1. boot;
2. set slide on voice 4 to on;
3. change one step's velocity;
4. wait 5 s;
5. power off.

Neither change survived.

**Image:** the card holds the §4 build (`cmp`-identical).

**What the trace shows:**
- **The §4 fix worked:** the writer now arms. The edits were marked dirty
  (Scene 4, instrument slot 2, parameters 28, 0 and 27, with the knob
  still turning up to tick 17,261). The writer armed at tick 14,188, the
  first edit, with its deadline at 19,188.
- **The trace stops at tick 17,261.** The next record is the next boot.
  Records made after the last flush before power-off are lost, because the
  ring is in RAM. That includes the step edit.
- **At the next boot** the reader merged a clean on-card mask (M dirty=0):
  the drain never committed anything.

**Root cause 2: a save took far longer than 5 s.** Measured on the earlier
cards, from drain admitted (A) to published (P):

| Part | Time | Why |
|---|---:|---|
| Debounce | 5,000 ms from the **first** change | `AUTOSAVE_WRITER_INTERVAL_MS 5000u` |
| Validation of both `.hcprms` records (phases 1–5) | ≈ 3.7 s | 2 × 34,768 B at `AUTOSAVE_CRC_BYTES_PER_TICK 128u` = 544 main-loop passes; the "winner cached" shortcut was cleared after every clean drain, so it applied only to continuations |
| Copy, CRC and commit (phases 10–22) | ≈ 4.8 s | 272 passes of 128 B, then sync |
| **Total, first edit to durable** | **≈ 13–14 s** | |

An edit followed by "wait 5 s, power off" could not survive under any
filesystem order. The design never met "an edit is saved within 5 s".

**Fix 2 (`config.h`, `filesystem.c`):**
1. **`AUTOSAVE_CRC_BYTES_PER_TICK` 128 → 512,** the whole `staging_buf`; the
   existing `_Static_assert` still holds.
   - Every caller reads into `staging_buf` or works byte by byte (checked).
   - The CRC is bitwise CRC32C at about 30 cycles a byte, so a pass gains
     about 70 µs, against main-loop passes of 2–6 ms.
   - This makes validation, copy, boot validation and recovery 4× fewer
     passes.
2. **The validated-winner cache is kept after a clean drain**
   (`filesystem_autosaveWriterCompleted()`). The next save skips the
   dual-record validation:
   - after a drain that published, the cache is the new target
     (`winner ^ 1`, generation + 1, probe + 1);
   - after a drain that published nothing, it is the winner just validated;
   - "Published" is `stream_offset >= AUTOSAVE_RECORD_BYTES`. The workspace
     is zeroed at phase 0 and `stream_offset` advances only in the copy. A
     capture with no live patch still publishes, so `patch_count` is not
     used.
   - Every lifecycle invalidation path still clears the cache: AutoSave OFF,
     Bank loss or change, error, setup, recovery without a winner.
3. **`AUTOSAVE_WRITER_INTERVAL_MS` 5,000 → 2,000 ms.** The debounce runs
   from the first change, and the save takes time after it, so a 5 s
   debounce alone exceeded the user's 5 s.
   - The same constant also spaces the retry after an error (now 2 s).
   - The 250 ms continuation is unchanged.

**Expected after fix 2 (to be measured on the device):**
- first edit → debounce 2 s;
- cached validation ≈ 0;
- capture plus copy of 68 passes and the commit ≈ 1–1.5 s;
- **≈ 3–4 s from an edit to durable.** Edits made while a drain runs go
  into the 250 ms continuation (≈ +1.5 s).

**The Pattern side** (the step velocity) is a separate drain:
- a 250 ms quiet window and a 5 s ceiling (`AUTOSAVE_PATTERN_*`);
- it writes without the CRC chunk limit;
- on the earlier cards it published within about 1 s of an edit.

The trace could not show it this time (see above). Re-check it on the next
card.

**Build:** `make img` writes 730,368 B; `make -C tools/dsp_test all`
exits 0, 87 breaks caught. There is no AutoSave host test in the suite, so
the device test is the gate:
1. Edit one parameter, wait 5 s, power off, and check it is back.
2. Repeat for a step edit.
3. Repeat after visiting the Instrument Load/Save page.

**The S097 filesystem optimisation is not the cause, and rolling it back
would not fix this:**
- all three causes predate S097 by about 325 commits:
  - trace-first ordering: session 048, commit 599b992b;
  - the 128 B CRC chunk: commit aca846aa;
  - the 5 s debounce: commit 8008ddba;
- S097 P2 is commit 04c5c004;
- its `-Os` filesystem build (`FS_OS=1`) is opt-in. `FS_OS ?= 0` is the
  Makefile default, and every image built this session used `FS_OS=0`.

**Rule §0, extended:** core persistence must be designed for its real
deadline, *edit → durable within 5 s*, and measured against it. A cap that
protects audio must be sized from measured cost. 128 B per pass was about
18 µs of work, and it made saves take 13 s.

## 10. Third failure (`SD_CARD_ATS_4`, 2026-11-20): still about 14 s per save

**Image:** the card holds the fix-2 build (730,384 B, `cmp`-identical).

**What the trace shows.** Fix 2 worked as far as it went:
- the writer armed;
- the cache hit (V flags 0x0b, cached=1, validation 0 ms);
- generation 2827 was published, and the next boot read it.

The save itself still took 14.2 s:

| Step | Tick | Gap | Cause |
|---|---:|---:|---|
| Edit (D) | 17,414 | | |
| Writer armed (S) | 18,676 | 1.26 s | `AUTOSAVE_TRACE_FLUSH` owned the facade (pass probe "at FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH phase 1") |
| Deadline | 20,676 | 2.0 s | debounce |
| Admitted (A) | 23,909 | **3.2 s late** | the trace flush again ("…TRACE_FLUSH phase 2") |
| Captured (C) | 24,466 | 0.56 s | phase 56 walked all 30,848 mask bits one at a time, about 180 per pass under the 50 µs budget cap |
| Published (P) | 31,666 | **7.2 s** | phase 13 copy: one `afatfs_poll()` per main-loop pass, and the bit-banged SD driver moves **16 bytes per poll**, so 32 passes per 512 B sector |

The last session on the card changed `normal[4]` at tick 41,321, then the
trace ends. Nothing armed before power-off, and that edit was lost.

**Root cause 3:**
1. Moving the trace flush to the end of the ladder (§4) was not enough. It
   still **started** whenever the facade was idle and had trace records,
   every 500 ms, and each flush (a seek in the multi-megabyte
   `asavetrc.bin`, one poll per pass) held the facade for most of that
   interval. The writer could arm or start only in the short gaps.
2. The core drain was paced at one SD poll per pass, which is 16 bytes.

**Fix 3:**
1. **No trace flush starts while any save is pending.** The new
   `filesystem_coreSavePending()` (DEV only) checks:
   - scalar mask dirty;
   - writer armed or recovering;
   - Pattern or non-semantic Pattern mask dirty;
   - `settings.cfg` dirty;
   - resident names dirty.

   Both `filesystem_autosaveTraceFlushSchedule_tick()` and
   `filesystem_patternTraceFlushSchedule_tick()` return while it is true.
   Trace records may be dropped. Saves may not (§0).
2. **Full-rate SD polling for core storage.** `AUTOSAVE_DRAIN_POLL_BUDGET_US
   1000u` (`config.h`): while `AUTOSAVE_PARAMETER_DRAIN`,
   `AUTOSAVE_PATTERN_DRAIN` or `FLUSH_FINISH` owns the facade,
   `filesystem_tick()` repeats `afatfs_poll()` for up to 1 ms a pass, about
   one sector.
   - A trace flush already in flight when an edit arrives gets the same
     rate, only so it releases the facade sooner.
   - The op `_tick()` still runs once a pass.
   - SD protocol timeouts are in milliseconds, not polls (`sdcard_lxr02.c`).
   - Audio runs from DMA interrupts and is not affected.
3. **Phase 56 skips clean mask bytes.** It uses `autosave_maskByteClean()`
   (Autosave.c/.h), a lock-free one-byte read, and takes 8 offsets in one
   step. A bit set just after the read stays dirty for the continuation,
   the same as a bit set after the scan has passed it.
4. **`AUTOSAVE_WRITER_INTERVAL_MS` 2,000 → 1,000 ms.**

**Expected:** edit → armed at once (at worst after one in-flight trace
flush, now at full poll rate) → 1 s → capture in tens of ms → copy and
commit ≈ 0.5–1 s. **About 2 s from an edit to durable.** A device test is
still the gate.

**Build:** `make img` writes `build/LXRV2_lxr02.img`, 730,952 B on disk.
`make -C tools/dsp_test all` exits 0.
