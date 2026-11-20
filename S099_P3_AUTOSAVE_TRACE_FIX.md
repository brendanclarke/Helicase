# S099 P3 — AutoSave: trace starvation and save latency (2026-11-19 to 2026-11-20)

Line numbers are for the tree at commit `dd81f19c` ("ats fixes, needs
test"); `85c24c7c` touched none of these files. The work is in two commits:

| Commit | Contents |
|---|---|
| `a0ebcf08` (2026-11-19 23:44) | Fix 1 (§4): trace flushes moved to the end of the `filesystem_tick()` ladder |
| `dd81f19c` (2026-11-20 08:20) | Fix 2 (§9), fix 3 (§10), the VOICE step-edit return fix (§11) |

Status: **fixes 1 and 2 are confirmed on the device (§10.2); fix 3 and the
§11 fix are built but not yet tested on the device (§13).**

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
5. **(Added in §9 and §10.) Core persistence is designed and measured
   against its real deadline:** *an edit is durable within 5 s*. Every
   throttle, cap and debounce in its path is sized from measured cost, and
   the end-to-end time is measured on the device before a fix is called done.
6. **(Added in §10.) Being last in the queue is not enough.** A diagnostic
   must not even *start* while core work is pending, because once started it
   holds the one shared resource until it finishes.

This rule outranks any diagnostic design note, including the one removed here
(§3).

## Legend: trace stages used below

Decoded with `tools/diagnostic_tools/decode_devlogs.py <asavetrc.bin>`.

| Stage | Meaning | Producer |
|---|---|---|
| D | a parameter marked dirty | `autosave_markPayloadOffsetDirty()` |
| S | writer armed; value = deadline tick | `filesystem_autosaveWriterSchedule_tick()` (`filesystem.c:27908`) |
| A | parameter drain admitted (owns the facade) | `filesystem_autosaveWriterSchedule_tick()` (`filesystem.c:27944`) |
| V | candidate validated; flag bit 3 (0x08) = cached winner | `filesystem_autosaveParameterDrain_tick()` |
| M | mask merged | the same |
| C | patch list captured | `filesystem_autosaveTraceCaptured()` |
| P | published; value = new generation | the same drain |
| T | terminal | `filesystem_autosaveWriterCompleted()` |
| W | writer held on a Load/Save page | writer schedule tick |
| E | drain error | drain error cleanup |
| G | trace records dropped | AutosaveTrace.c |
| H | 5 s background-budget report per class | `filesystem_backgroundBudgetRefill()` (`filesystem.c:28491`) |
| f | longest main-loop pass of the second, and the op and phase then | `filesystem_passProbe()` (`filesystem.c:29035`) |
| w | facade owner change | `filesystem_traceOwnerTransition()` (`filesystem.c:28873`) |
| Q, s, m, p | boot reader, boot settle, MIDI Learn file, CPU probe | various |

Ticks are `time_sysTick` milliseconds since boot.

## 1. What the user saw (first report)

"NOTHING IS AUTOSAVING." The card is `SD_CARD_ADV_2/`. The firmware is
730,248 B, a DEV_MODE_LOGGING build (`config.h:88` `#define DEV_MODE_LOGGING
1`), and was identical to the build tree at the time.

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
debounce. AutoSave was being starved there too, only less visibly.

**What the filesystem was doing in the gap.** In boot 3, between the arm and
the Load/Save visit:
- no filesystem operation other than the trace flush ran;
- every one-second main-loop pass probe (stage f) reports
  `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` as the operation in progress;
- across all five boots, 82–91 % of the pass probes land inside a trace
  flush.

## 3. Root cause 1: the trace came before the writer

`filesystem_tick()` (`filesystem.c:29097`) hands the one filesystem facade
to background claimants in a fixed order. Each claimant is gated on `status
== FS_STATUS_IDLE`, so the first one that calls `filesystem_start()` takes
the facade for that pass. Before fix 1 the order was:

1. MIDI Learn writer
2. settings writer
3. **AutoSave Trace flush (diagnostic)**
4. **PatternTrace flush (diagnostic)**
5. deferred HCNAMES flush
6. **AutoSave scalar writer** (arms the debounce; starts the parameter drain)
7. AutoSave Pattern drain
8. AutoSave non-semantic Pattern drain

Session 048 (commit `599b992b`) put the trace (3, 4) ahead of the writer
(6) on purpose. The removed comment read: "Persist a pending diagnostic
batch before an eligible AutoSave drain … a pending logging-build trace has
one append first." The aim was to make a J/I witness batch durable before a
long first drain.

**The failure.** A logging build *always* has pending trace records:
- the S094 P2 cycle probe (`mixer_probeEnd()`) alone writes 16 records every
  1.5 s;
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
schedulers compile to nothing (their bodies are inside `#if
DEV_MODE_LOGGING`). That does not make this acceptable; see §0 rule 3.

## 4. Fix 1 (commit `a0ebcf08`; `Core/Hardware/SD/filesystem.c`, `filesystem_tick()`)

The two diagnostic flushes are moved to the end of the ladder, after every
core claimant. Current lines:

| # | Claimant | Line | Gate |
|---|---|---:|---|
| 1 | `filesystem_midiLearnWriterSchedule_tick()` | 29209 | `status == FS_STATUS_IDLE && !cc_suspended` |
| 2 | `filesystem_settingsWriterSchedule_tick()` | 29211 | the same |
| 3 | `menu_triggerDeferredHcnamesFlush()` | 29227 | the same, plus `menu_hasResidentNameDirtyMask()` |
| – | `filesystem_backgroundBudgetRefill()` | 29237 | always (not a claimant) |
| 4 | **`filesystem_autosaveWriterSchedule_tick()`** | 29263 | the same, plus `!scene_backgroundExists()` |
| 5 | `filesystem_autosavePatternDrainSchedule_tick()` | 29267 | the same, plus `!scene_backgroundExists()` |
| 6 | `filesystem_autosaveNonSemanticPatternDrainSchedule_tick()` | 29273 | the same, plus `!scene_backgroundExists()` |
| 7 | `filesystem_autosaveTraceFlushSchedule_tick()` (diagnostic) | 29282 | `status == FS_STATUS_IDLE && !cc_suspended` |
| 8 | `filesystem_patternTraceFlushSchedule_tick()` (diagnostic) | 29284 | the same |

**Code changes:**
- Removed from between the settings writer and HCNAMES:
  - the trace and PatternTrace calls;
  - the 17-line comment that justified their place ("Persist a pending
    diagnostic batch before an eligible AutoSave drain…").
- Re-added after `filesystem_autosaveNonSemanticPatternDrainSchedule_tick()`,
  with unchanged gates. The new comment is at `filesystem.c:29275`: "The
  diagnostic trace flushes are the last claimants (S099 P3)…".
- The writer's comment (`filesystem.c:29239`) is rewritten. It now records
  the starvation (§3), the `SD_CARD_ADV_2` evidence and the 5–53 s waits
  on the earlier card.

**Unchanged by fix 1:**
- every gate, debounce, deadline, file format and drain state machine;
- the trace still never runs on a Load/Save page or during a command (the
  `menu_isLoadSaveCommandActive()` gates in both trace schedulers);
- its ring keeps filling and counts drops (stage G).

**The cost moves to the diagnostics.** While AutoSave is busy the trace
waits, and a long drain may drop some trace records, which the G record
reports. That is the intended trade.

**Build:** `make img` wrote **730,304 B** (730,320 B on disk), with no new
warning. The `filesystem.c` warnings in a full rebuild are the standing
unused-function ones. `make -C tools/dsp_test all` exited 0 with 87 breaks
caught.

**Device result (§9, `SD_CARD_ATS_3`):** the writer armed at the first edit.
Fix 1 worked, but it was not enough (§9, §10).

## 5. Audit of the other diagnostic paths

| Site | What it does | Verdict |
|---|---|---|
| `filesystem_autosaveTraceFlushSchedule_tick()` (`filesystem.c:28201`) | starts a trace append when records are pending and the interval is due | last in the ladder (§4); since fix 3 it does not start while a save is pending (§10) |
| `filesystem_patternTraceFlushSchedule_tick()` (`filesystem.c:6265`) | the same for PatternTrace | the same |
| `filesystem_autosaveTraceFlushCompleted()` | a full batch sets the next due to 0 (immediate continuation) | acceptable now: a continuation cannot start while a save is pending |
| `filesystem_autosaveTraceFlushBlocking()` | bench helper that pumps the ring to the card | not called by the runtime; must stay bench-only |
| `filesystem_passProbe()` (`filesystem.c:29035`) | records the longest main-loop pass; skips trace flushes | records only; takes no facade |
| `filesystem_traceOwnerTransition()` (`filesystem.c:28873`) | records facade owner changes (w) | records only |
| `mixer_probeEnd()` (S094 P2 cycle probe) | 16 records per 2,048 blocks | records only; this volume is what kept the ring non-empty |

**Not yet audited:** every other `#if DEV_MODE_LOGGING` block, for waits,
retries or ordering that touch core work (the menu's Load/Save
instrumentation, the boot log). Rule §0 applies to them, and they should be
checked the same way.

## 6. Also seen on `SD_CARD_ADV_2` (separate from the starvation)

- **Boot 4's boot drain captured 1,104 patches** (`case2_scene_mask=0x0010`,
  Scene 4). That is the recovery path re-marking Scene 4 after its
  Instrument slot 2 had been replaced with an Advanced Alkaline. The alx
  row count also changed this session (38 → 35, S099 P2 §10). It completed
  and published (generation 2,823); no data was lost there.
- **Boot 5's drain error (stage E, `AUTOSAVE_PARAMETER_DRAIN` phase 41)** is
  the error cleanup. The record does not say which earlier step failed. A
  Kit-page entry (stage O) came 4 ms before it. Under the old order the
  following retry would also have been starved. **Still open:** no later
  card has shown an E record (`SD_CARD_ATS_4`: none, and no G either).

## 7. Device check for fix 1 (superseded by §14)

The test written for fix 1 (flash the 730,304 B image, edit, wait about 5 s,
expect S, A, V, M, C, P, T) assumed the 5 s debounce. §9 showed that a 5 s
debounce cannot meet a 5 s deadline. Use §14.

## 8. Statements now out of date (for the user)

- **`MEMORY.md`:** add rule §0 (including rules 5 and 6) to the AutoSave /
  DEV logging notes. I have not edited `MEMORY.md` (the project rule).
- **The S090 P5 / S097 P3 notes on trace-before-writer ordering** (the
  removed "pre-drain diagnostic witness") are superseded by §4 and §10.
- **Any note that says the AutoSave debounce is 5 s:** it is 1 s since §10
  (`config.h:569`).

## 9. Second failure (`SD_CARD_ATS_3`): the fixed order was not enough

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

### 9.1 Fix 2 (commit `dd81f19c`)

**1. `AUTOSAVE_CRC_BYTES_PER_TICK` 128 → 512** (`config.h:690`; the
reason is in the comment at `config.h:683–688`).
- 512 is the whole `staging_buf`. The existing `_Static_assert`
  (`filesystem.c:1229–1231`, `AUTOSAVE_CRC_BYTES_PER_TICK <=
  sizeof(staging_buf)`) still holds.
- `filesystem_autosaveCrcChunkBytes()` (`filesystem.c:8053`) returns
  `min(remaining, AUTOSAVE_CRC_BYTES_PER_TICK)`. Every caller reads into
  `staging_buf` or works byte by byte (checked).
- The CRC is bitwise CRC32C, about 30 cycles a byte, so a pass gains about
  70 µs, against main-loop passes of 2–6 ms.
- This cuts validation, copy, boot validation and recovery to 4× fewer
  passes.

**2. The validated-winner cache is kept after a clean drain**
(`filesystem_autosaveWriterCompleted()`, `filesystem.c:27400`; the clean
branch is `filesystem.c:27502–27543`, comment `27503–27524`, code
`27525–27542`).

Before, this branch was one line, `fs_autosave_winner_cached = 0u;`. Now:

```c
if (op_autosave_writer.have_winner) {
    const uint8_t published = (uint8_t)(op_autosave_writer.stream_offset >=
                                        AUTOSAVE_RECORD_BYTES);

    fs_autosave_cached_winner_index = published
        ? (uint8_t)(op_autosave_writer.winner_index ^ 1u)
        : op_autosave_writer.winner_index;
    fs_autosave_cached_winner_generation = published
        ? op_autosave_writer.winner_generation + 1u
        : op_autosave_writer.winner_generation;
    fs_autosave_cached_winner_probe = published
        ? (uint8_t)(op_autosave_writer.winner_probe + 1u)
        : op_autosave_writer.winner_probe;
    fs_autosave_winner_cached = 1u;
} else {
    fs_autosave_winner_cached = 0u;
}
fs_autosave_writer_armed = 0u;
```

- **The cache variables** are `fs_autosave_winner_cached`
  (`filesystem.c:2480`), `fs_autosave_cached_winner_index` (`2481`),
  `fs_autosave_cached_winner_generation` (`2482`) and
  `fs_autosave_cached_winner_probe` (`2483`). They existed already; no RAM
  was added.
- **The consumer** is drain phase 0 (`filesystem.c:9245–9278`). After
  `memset(&op_autosave_writer, 0, …)` (`9245`), if the cache is set
  (`9248`), it loads the winner from the cache, clears the cache (`9278`),
  records V with flag 0x08, and goes straight to phase 56. Validation
  (phases 1–5) is skipped.
- **After a drain that published,** the cache is the new target: `winner ^
  1`, generation + 1, probe + 1. This is the same tuple the continuation
  path (`filesystem.c:27483–27489`) already caches.
- **After a drain that published nothing,** for example the boot-settle
  drain with an empty mask, the cache is the winner just validated in full.
  The card is unchanged.
- **"Published" is `stream_offset >= AUTOSAVE_RECORD_BYTES`.**
  - `AUTOSAVE_RECORD_BYTES` = 64 header + 3,856 mask + 30,848 payload =
    34,768 B (`Autosave.h:72`).
  - `stream_offset` is zeroed by the phase 0 `memset` (`9245`), set to 0 at
    phase 12 (`9732`), and advanced only by the phase 13 copy (`9840`).
  - Phases 33, 39, 35 and 38 (`10358`, `10389`, `10394`, `10446`, `10494`,
    `10520`) also write it, but those are no-valid-record recovery phases.
    Phase 5 enters them only when `have_winner == 0` (`filesystem.c:9440`:
    `op_phase = op_autosave_writer.have_winner ? 50u : 30u;`), and that case
    takes the `else` branch above, which clears the cache. **Checked in
    this pass:** the marker is read only when it is valid.
  - `patch_count` is not used as the marker: a capture with no live patch
    still goes through phase 10 and publishes.
  - A drain that ends in ERROR takes the error branch (`27544–27553`),
    which clears the cache.
- **Every other lifecycle path still clears the cache:**
  - `filesystem_resetFacadeForBootLogRecovery()` (`26779`);
  - `filesystem_initAfterCardReady()` (`26869`);
  - `filesystem_patternAutosaveOnLoad()` (`27030`);
  - the AutoSave-OFF terminal path in `filesystem_autosaveWriterCompleted()`
    (`27454`) and the no-winner continuation (`27497`);
  - the error branch (`27549`);
  - `filesystem_autosaveSetupCompleted()` failure (`27761`);
  - AutoSave OFF (`27807`) and Bank loss (`27837`) in
    `filesystem_autosaveWriterSchedule_tick()`;
  - `filesystem_ensureAutosaveFilesBlocking()` (`30269`).
- **Why it is safe:** only this writer writes the `.hcprms` records.
  Removing the card while powered is outside the product contract; this is
  the same rule the continuation path already relies on.

**3. `AUTOSAVE_WRITER_INTERVAL_MS` 5,000 → 2,000 ms** (`config.h`; §10
then cut it to 1,000).
- The debounce runs from the first change (the writer arms once and does
  not re-arm on later edits: `filesystem.c:27897–27910`). The save takes
  time after it, so a 5 s debounce alone exceeded the user's 5 s.
- The same constant spaces the retry after an error (`filesystem.c:27550`)
  and the non-semantic Pattern drain's arm. The 250 ms continuation
  (`AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS`, `config.h:598`) is
  unchanged.

### 9.2 What fix 2 predicted, and what the device measured

- **Predicted:** about 3–4 s from an edit to durable (debounce 2 s, cached
  validation about 0, capture and copy about 1–1.5 s).
- **Measured on `SD_CARD_ATS_4` (§10): 14.2 s.** The cached validation
  worked (0 ms). The prediction was wrong because I assumed the copy was
  limited by the CRC chunk. It is limited by the SD driver's poll rate
  (§10.3). I did not check the driver before giving the estimate.

**The Pattern side** (the step velocity) is a separate drain:
- a 250 ms quiet window and a 5 s ceiling (`AUTOSAVE_PATTERN_QUIET_WINDOW_MS`
  and `AUTOSAVE_PATTERN_MAX_LATENCY_MS`, `config.h:722–723`);
- it writes without the CRC chunk limit (10,656 B per `.patNN` record).

**Build:** `make img` wrote 730,368 B (730,384 B on disk). `make -C
tools/dsp_test all` exited 0 with 87 breaks caught. There is no AutoSave
host test in the suite.

**The S097 filesystem optimisation is not the cause, and rolling it back
would not fix this:**
- all three causes predate S097 by about 325 commits:
  - trace-first ordering: session 048, commit `599b992b`;
  - the 128 B CRC chunk: commit `aca846aa`;
  - the 5 s debounce: commit `8008ddba`;
- S097 P2 is commit `04c5c004`;
- its `-Os` filesystem build (`FS_OS=1`) is opt-in. `FS_OS ?= 0` is the
  Makefile default, and every image built this session used `FS_OS=0`.
- The fourth cause (one SD poll per pass, §10) is also older than S097: the
  single `afatfs_poll()` per busy pass and the 16-byte burst
  (`sdcard_lxr02.c:96`) are pre-existing design.

## 10. Third failure (`SD_CARD_ATS_4`): still about 14 s per save

### 10.1 The card

- **Image:** `SD_CARD_ATS_4/LXRV2_lxr02.img` is 730,384 B and
  `cmp`-identical to the fix-2 build.
- **Trace:** records after ATS_3's end (about 1,393,206) are new. The
  sessions are:

| Boot (first record) | Image (from the S deadline) | Edits | Outcome |
|---|---|---|---|
| 1,393,231 | fix 1 (deadline = arm + 5,000) | D at 17,212; S at 19,667 (2.45 s after D) | lost: power-off before the deadline at 24,667 |
| 1,393,486 | fix 1 (+5,000) | D at 18,945; S at 21,154 (2.2 s after D) | lost |
| 1,393,817 | – | none | – |
| 1,394,011 | **fix 2** (deadline = arm + 2,000) | D at 17,414 | **saved: generation 2,827 published at tick 31,666** |
| 1,394,471 | fix 2 | boot reads generation 2,827 (record 1,394,565); D × 6 at 41,321–41,417 (`normal[4]`, payload offset 8,977); the trace ends at 41,417 | lost: the next boot (1,395,118) still reads 2,827 |
| 1,395,024 | fix 2 | none | – |

So the user ran the test twice on the fix-1 image, then on the fix-2 image.
No G (dropped) and no E (error) records appear in the new part.

### 10.2 The fix-2 save, step by step (boot 1,394,011)

| Record | Tick | Stage | Gap | What it shows |
|---|---:|---|---:|---|
| 1,394,270 | 17,414 | D | | Scene 4, instrument[2], `normal[3]` (payload offset 8,976) |
| 1,394,287 | 18,672 | f | | longest pass 21,856 µs, **at `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` phase 1** |
| 1,394,288 | 18,676 | S | **1.26 s after D** | armed; deadline 20,676 (= +2,000: the fix-2 image) |
| 1,394,305–7 | 20,426 | H | | scalar class charged 0 ms, denied 0: the drain had not started |
| 1,394,356 | 23,909 | f | | longest pass 24,033 µs, **at `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` phase 2** |
| 1,394,357 | 23,909 | A | **3.23 s after the deadline** | drain admitted |
| 1,394,358 | 23,909 | V | 0 ms | flags 0x0b: cached=1, winner B (`.hcprms2`), generation 2,826. **Fix 2's cache worked.** |
| 1,394,359 | 24,466 | C | 557 ms | patch_count=1, budget_exhausted=0 |
| 1,394,378 | 25,428 | H | | scalar: charged 5 ms, denied 27, max slice 73 µs |
| 1,394,397–436 | 26,912–30,917 | f | | every second: longest pass about 4.3 ms, at `AUTOSAVE_PARAMETER_DRAIN` **phase 13** (value 0x0d07….) |
| 1,394,434 | 30,429 | H | | scalar: charged 28 ms, denied 65, max slice 157 µs |
| 1,394,453 | 31,666 | P | **7.20 s after C** | target A (`.hcprms1`), generation 2,827 |
| 1,394,454 | 31,666 | f | | drain ended; longest `filesystem_tick()` 405 µs, in phase 24 |
| 1,394,455 | 31,761 | T | 95 ms | DONE (final sync) |

**Edit to durable: 14.35 s.**

Pass-probe owners across the fix-2 sessions (records ≥ 1,394,011): 16
`AUTOSAVE_TRACE_FLUSH`, 7 `AUTOSAVE_PARAMETER_DRAIN`, 3
`UPDATE_HCNAMES_PATTERN`, 1 `AUTOSAVE_PATTERN_DRAIN`. The trace flush was
the most common facade owner.

**The Pattern drain ran** on this card: w records 1,394,573 (tick 2,878),
1,394,978 (tick 38,235) and 1,395,125 (tick 2,769) show
`AUTOSAVE_PATTERN_DRAIN` handing over to `UPDATE_HCNAMES_PATTERN`. The trace
does not record which steps a Pattern drain wrote. Whether the user's
velocity edit survived cannot be told from it; the user's report says it
did not.

### 10.3 Root cause 3

**1. The trace flush still blocked the writer.** Moving it to the end of the
ladder (§4) only means it *starts* after the writer has declined. In the
passes where the writer has nothing to do yet, the trace flush starts:
- it is due every 500 ms (`AUTOSAVE_TRACE_FLUSH_INTERVAL_MS`,
  `config.h:708`), or immediately after a full batch;
- each flush seeks the end of a multi-megabyte `asavetrc.bin` and appends,
  at one poll per pass (point 2), so it holds the facade for hundreds of ms;
- while it holds the facade, `status` is BUSY and the writer schedule tick
  is not called at all, so it can neither arm nor start a due drain;
- the result: **arm 1.26 s late, start 3.23 s late** (§10.2, both pass
  probes name the trace flush).

**2. Core storage was polled once per main-loop pass, and each poll moves 16
bytes.**
- `filesystem_tick()` called `afatfs_poll()` once per busy pass
  (`filesystem.c:29129–29134`; four times only during a Menu-owned
  stopped-playback command, `FS_FAST_DRAIN_POLL_PASSES`, `filesystem.c:2289`).
- The SD driver is bit-banged SPI. One `sdcard_poll()` clocks
  `SDCARD_BURST_SIZE` = 16 bytes (`sdcard_lxr02.c:43`, `96`, the burst loops
  at `397` and `426`). A 512 B sector is 32 polls.
- At 4–24 ms per main-loop pass, a sector took 130 ms or more. The copy
  reads 68 sectors and writes 68 (34,768 B each way) plus FAT and directory
  work: **7.2 s**.

**3. The capture scan walked 30,848 bits one at a time under a 50 µs cap.**
- Phase 56 (`filesystem.c:9578`) calls `autosave_maskBitTake()` for every
  payload offset (`AUTOSAVE_PAYLOAD_BYTES` = 128 + 16 × 1,920 = 30,848,
  `Autosave.h:69`). Each call takes and restores an interrupt lock
  (`Autosave.c:2619`).
- It is bounded by `AUTOSAVE_MASK_BITS_PER_TICK` 256 (`config.h:670`) and by
  the shared budget before every iteration (`filesystem.c:9604`).
- The budget (`config.h:742–743`): 50 µs per ms stopped, 30 playing.
  `filesystem_backgroundBudgetRefill()` caps credit at one millisecond's
  worth (`filesystem.c:28513`), so a pass can spend at most about 50 µs.
  That is about 180 bits a pass: **557 ms**.

### 10.4 Fix 3 (commit `dd81f19c`)

**1. No trace flush starts while any save is pending.**
- New `filesystem_coreSavePending()` (`filesystem.c:28189`; comment
  `28180–28188`; forward declaration `1730`; both inside `#if
  DEV_MODE_LOGGING`, because only the DEV trace schedulers use it and a
  release build would otherwise warn "defined but not used"):

  ```c
  static uint8_t filesystem_coreSavePending(void)
  {
      return (uint8_t)(autosave_maskHasDirty() ||
                       fs_autosave_writer_armed ||
                       fs_autosave_recovery_pending ||
                       autosave_patternDirtyMask() != 0u ||
                       autosave_nonSemanticPatternDirtyMask() != 0u ||
                       fs_settings_dirty ||
                       menu_hasResidentNameDirtyMask());
  }
  ```

  | Term | Owner | Covers |
  |---|---|---|
  | `autosave_maskHasDirty()` | Autosave.c | a scalar edit not yet captured (the window before S arms) |
  | `fs_autosave_writer_armed` (`filesystem.c:2377`) | writer | armed, waiting for the deadline or a continuation |
  | `fs_autosave_recovery_pending` (`2436`) | writer | boot recovery not yet run |
  | `autosave_patternDirtyMask()` | Autosave.c | a step edit (the user's velocity) |
  | `autosave_nonSemanticPatternDirtyMask()` | Autosave.c | Pattern relocation |
  | `fs_settings_dirty` (`2321`) | settings writer | `settings.cfg` |
  | `menu_hasResidentNameDirtyMask()` | menu.c | deferred HCNAMES |

- **AutoSave trace gate:** `filesystem_autosaveTraceFlushSchedule_tick()`
  (`filesystem.c:28201`). After the `autosaveTrace_pendingCount() == 0u`
  return, a new block (comment `28243–28249`, code `28250–28251`) returns
  while `filesystem_coreSavePending()` is true.
- **PatternTrace gate:** `filesystem_patternTraceFlushSchedule_tick()`
  (`filesystem.c:6265`). The new lines `6272–6274` return while it is true.
- **Effect:** while an edit is pending, the trace ring keeps filling in RAM
  and flushes after the save. If it fills first it drops records and says so
  (G). A trace flush already running when the edit arrives is not cancelled;
  it finishes first. Point 2 makes that faster.

**2. Full-rate SD polling for core storage.**
- New `#define AUTOSAVE_DRAIN_POLL_BUDGET_US 1000u` (`config.h:585`, comment
  `572–584`).
- `filesystem_tick()` (`filesystem.c:29135–29154`), inside the existing
  busy-poll branch, after the existing one-or-four `afatfs_poll()` loop
  (`29129–29134`):

  ```c
  if (status == FS_STATUS_BUSY &&
      (current_op == FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN ||
       current_op == FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN ||
       current_op == FS_INTERNAL_OP_FLUSH_FINISH ||
       current_op == FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH ||
       current_op == FS_INTERNAL_OP_PATTERN_TRACE_FLUSH)) {
      const uint32_t poll_start_us = timebase_tim2Now();

      while (timebase_tim2Delta(timebase_tim2Now(), poll_start_us) <
             AUTOSAVE_DRAIN_POLL_BUDGET_US)
          afatfs_poll();
  }
  ```

  | Op | Why it is in the list |
  |---|---|
  | `AUTOSAVE_PARAMETER_DRAIN` | the scalar save (7.2 s copy) |
  | `AUTOSAVE_PATTERN_DRAIN` | the step-edit save |
  | `FLUSH_FINISH` | the final sync after every op (`filesystem.c:4394`), including both drains; it is core work for every owner |
  | `AUTOSAVE_TRACE_FLUSH`, `PATTERN_TRACE_FLUSH` | only an *in-flight* flush (none starts while a save is pending): finishing it faster hands the facade back to the save sooner. This gives the trace no priority. |

- **What does not change:**
  - The op's `_tick()` still runs once per pass, so phase-stall detection,
    callbacks, the watchdog and the deadlines see the same cadence. This is
    the same principle as the existing fast-drain loop.
  - SD protocol timeouts are measured in milliseconds, not polls
    (`SDCARD_TOKEN_TIMEOUT_MS` 1,000 and `SDCARD_BUSY_TIMEOUT_MS` 5,000,
    `sdcard_lxr02.c:97–98`; the file comment at `60–69` says extra polls
    cannot shorten a protocol deadline).
  - Audio renders from DMA interrupts. The longer main-loop pass (up to
    +1 ms against an existing 4–24 ms) delays only foreground work (UI,
    MIDI parsing).
- **Expected rate (not measured):** if a 16-byte bit-banged poll costs about
  25–35 µs, 1 ms is about one sector. The phase 13 budget gate
  (`filesystem.c:9797`) is then the next limit: a 512 B CRC slice costs up
  to 157 µs (H record 1,394,434), so after one slice the credit is about
  −107 µs and refills in about 2 ms (stopped) or 3.6 ms (playing). That is
  about one chunk per 4 ms pass. Estimate: copy 0.5–1 s.

**3. Phase 56 skips clean mask bytes.**
- New `autosave_maskByteClean()` (`Autosave.c:2650–2655`; declaration and
  comment `Autosave.h:560–569`):

  ```c
  uint8_t autosave_maskByteClean(uint16_t payload_offset)
  {
      if (payload_offset >= AUTOSAVE_PAYLOAD_BYTES)
          return 1u;
      return (uint8_t)(autosave_dirty_mask[payload_offset >> 3u] == 0u);
  }
  ```

  It reads `autosave_dirty_mask[]` (`Autosave.c:161`, `static volatile
  uint8_t`) with no lock. A single-byte read is atomic on Cortex-M7.
- **The use** in phase 56 (`filesystem.c:9619–9630`), after the budget
  check and the patch-capacity check, before the bit take:

  ```c
  if ((payload_offset & 7u) == 0u &&
      payload_offset + 8u <= AUTOSAVE_PAYLOAD_BYTES &&
      autosave_maskByteClean(payload_offset)) {
      op_autosave_writer.payload_scan_offset =
          (uint16_t)(payload_offset + 8u);
      examined = (uint16_t)(examined + 8u);
      continue;
  }
  ```

- **Why it is correct:**
  - It only skips on a byte boundary, and only a whole byte that is inside
    the payload.
  - If an interrupt sets a bit in that byte just after the read, the bit
    stays set. `autosave_maskHasDirty()` then stays true, and the writer's
    continuation (`filesystem.c:27499`, 250 ms) captures it. This is exactly
    what already happens to a bit set behind the scan cursor.
  - `examined` may overshoot 256 by up to 7. The loop bound is `examined <
    AUTOSAVE_MASK_BITS_PER_TICK`, so that only ends the pass's slice a
    little early.
  - The dirty population count is untouched: nothing is taken.
- **Expected:** 3,856 byte reads for a mostly clean mask, instead of 30,848
  locked bit takes. Tens of ms.

**4. `AUTOSAVE_WRITER_INTERVAL_MS` 2,000 → 1,000 ms** (`config.h:569`;
history in the comment at `config.h:559–568`). The debounce is the largest
fixed term left. It also spaces the error retry and the non-semantic
Pattern drain arm.

### 10.5 Expected timing after fix 3 (not yet measured)

| Step | Expected | Was (ATS_4) |
|---|---:|---:|
| Edit → armed | a few ms (at worst, one in-flight trace flush) | 1.26 s |
| Armed → admitted | 1.0 s | 5.23 s |
| Validation | 0 (cached) | 0 |
| Capture | tens of ms | 557 ms |
| Copy and commit | 0.5–1 s | 7.2 s |
| Final sync | under 100 ms | 95 ms |
| **Edit → durable** | **about 2 s** | **14.35 s** |

A step edit adds the Pattern drain after the scalar drain, because the
writer claimant comes first in the ladder: about 0.3–0.5 s more for 10,656
B at full poll rate (estimate).

**Build (fix 3 alone):** `make img` wrote 730,808 B.

## 11. VOICE step-edit return page (commit `dd81f19c`)

This is not AutoSave work. It was fixed in the same pass and is logged here
so the commit is fully described.

**The user:** "when I shift+hold a step in voice mode and then release it, it
still returns to the voice mode aeg page when it should return to the last
page I was on."

**Root cause:**
- The menu cursor is one byte, `menuIndex` (`menu.c:1501`, `static uint8_t`):
  subpage in bits 3–5 and column in bits 0–2 (`MASK_PAGE 0xf8`,
  `MASK_PARAMETER 0x07`, `PAGE_SHIFT 3`, `NUM_SUB_PAGES 8`; `menu.h:26–29`).
  The maximum is 63.
- Opening the VOICE step edit (`buttonHandler_voiceStepEditPress()`,
  `buttonHandler.c:1902`) switches to `SEQ_PAGE` and calls
  `menu_showStepEditPage()` (`buttonHandler.c:1916` → `menu.c:20935`), which
  sets `menuIndex = (uint8_t)(1u << PAGE_SHIFT);` (`menu.c:20946`), that is
  subpage 1.
- Closing it (`buttonHandler_voiceStepEditRelease()`,
  `buttonHandler.c:1927`) called `menu_switchPage(bh_voiceStepPrevPage)`.
  The voice-page case of `menu_switchPage()` (`menu.c:19949`, `default: /*
  voice pages */`) does not touch `menuIndex`, so the voice page opened on
  subpage 1, which is AEG.
- `menu_switchSubPage()` (`menu.c:19440`) cannot be used to restore: on a
  voice page, asking for the current subpage cycles to its next screen
  (the `subPageNr == activePage` branch, `menu.c:19475`; contract comment
  `19463–19469`).

**Fix:**
1. **New accessors in `menu.c`** (`20741–20757`; declared in `menu.h:801–802`):

   ```c
   uint8_t menu_getMenuIndex(void)    { return menuIndex; }
   void menu_restoreMenuIndex(uint8_t index)
   {
       lockPotentiometerFetch();
       editModeActive = 0;
       menuIndex = index;
       menu_endlessPotMappingChanged();
       va_refreshAutomationLeds();
       menu_repaintAll();
   }
   ```

   This mirrors what `menu_switchSubPage()` does around its `menuIndex`
   write (`lockPotentiometerFetch()`, `editModeActive = 0`,
   `menu_endlessPotMappingChanged()`, `va_refreshAutomationLeds()`), plus a
   repaint. `va_refreshAutomationLeds()` is the static function at
   `menu.c:3223`.
2. **`bh_voiceStepEdit` now stores the cursor to restore** (`buttonHandler.c:1899`).
   It is 0 when closed; otherwise it is `menuIndex + 1`, which fits because
   `menuIndex <= 63`. No RAM is added (the header comment at
   `buttonHandler.c:1892–1896` says so). Every existing `if
   (bh_voiceStepEdit)` test and `bh_voiceStepEdit = 0u` close (`1957`,
   `2001`, `2094`, the mode-change close after `2180`) keeps its meaning.
   - **Press, first step** (`buttonHandler.c:1907`):
     `bh_voiceStepEdit = (uint8_t)(menu_getMenuIndex() + 1u);`. It was
     `= 1u`. It is read before `menu_switchPage(SEQ_PAGE)`, so it is the
     voice page's cursor.
   - **Release, last step** (`buttonHandler.c:1935–1943`):
     `prev_index = bh_voiceStepEdit - 1u` is taken before the flag is
     cleared. After `menu_switchPage(bh_voiceStepPrevPage)`, if the page
     really changed (`menu_activePage == bh_voiceStepPrevPage`; a busy
     `menu_switchPage()` can defer), it calls
     `menu_restoreMenuIndex(prev_index)`.
3. **Unchanged:**
   - joining steps (`buttonHandler.c:1957`, `2001`);
   - held-step flashing;
   - a mode change while the edit is open (`buttonHandler.c:2180`) still
     ends the edit without a restore, because the new mode's page replaces
     it.

**Not verified:**
- The device test.
- The order "release SHIFT, then the step". The release path
  (`buttonHandler.c:2094`) runs whether SHIFT is held or not. If a SHIFT
  release elsewhere switches page while the step is still held, the later
  step release still restores the saved cursor on the saved page. I have
  not traced that path.

**Build (fix 3 + §11):** `make img` writes `build/LXRV2_lxr02.img`,
**730,936 B ("Written"), 730,952 B on disk**. `make -C tools/dsp_test all`
exits 0 (`chorus tables OK`). There are no new warnings in `filesystem.c`,
`menu.c`, `buttonHandler.c` or `Autosave.c`.

## 12. Index of every change

| File | Lines (now) | Change | Fix |
|---|---|---|---|
| `Core/Hardware/SD/filesystem.c` | 29239–29262 | writer comment rewritten | 1 |
| 〃 | 29275–29284 | trace flushes re-added last, with comment | 1 |
| 〃 | (removed, formerly after the settings writer) | trace flushes and their comment | 1 |
| 〃 | 27502–27543 | clean-drain branch keeps the winner cache | 2 |
| 〃 | 1730 (+ `#if`/`#endif` 1729, 1731) | forward declaration of `filesystem_coreSavePending()` | 3 |
| 〃 | 6272–6274 | PatternTrace flush save gate | 3 |
| 〃 | 9619–9630 | phase 56 clean-byte skip | 3 |
| 〃 | 28179–28199 | `filesystem_coreSavePending()` | 3 |
| 〃 | 28243–28251 | AutoSave Trace flush save gate | 3 |
| 〃 | 29135–29154 | full-rate SD polling for core ops | 3 |
| `config.h` | 559–569 | `AUTOSAVE_WRITER_INTERVAL_MS` 5,000 → 2,000 → 1,000, history comment | 2, 3 |
| 〃 | 571–585 | `AUTOSAVE_DRAIN_POLL_BUDGET_US 1000u` (new) | 3 |
| 〃 | 683–690 | `AUTOSAVE_CRC_BYTES_PER_TICK` 128 → 512, comment | 2 |
| `Core/Bank/Scene/Autosave.c` | 2650–2655 | `autosave_maskByteClean()` (new) | 3 |
| `Core/Bank/Scene/Autosave.h` | 560–569 | its declaration and contract | 3 |
| `Core/Menu/menu.c` | 20741–20757 | `menu_getMenuIndex()`, `menu_restoreMenuIndex()` (new) | §11 |
| `Core/Menu/menu.h` | 801–802 | their declarations | §11 |
| `Core/Hardware/frontPanel/buttonHandler.c` | 1892–1896 | state comment | §11 |
| 〃 | 1907 | save the cursor on open | §11 |
| 〃 | 1935, 1941–1943 | restore it on the last release | §11 |

**RAM:** none added (fix 2 reuses the existing cache variables; fix 3 adds
only code and a compile-time constant; §11 reuses `bh_voiceStepEdit`).
**Flash:** 730,248 B before fix 1 → 730,936 B now (written sizes).

## 13. Evidence: what is proven and what is not

| Claim | Status | Evidence |
|---|---|---|
| Trace-first ordering starved the writer | **proven** | ADV_2: boot 4 had no S; boot 3 armed but never admitted; 82–91 % of pass probes inside trace flushes |
| Fix 1 lets the writer arm | **proven on device** | ATS_3: S at the first edit (tick 14,188) |
| A 5 s debounce plus the 128 B chunk cannot meet 5 s | **proven** | §9 arithmetic from measured drain times |
| Fix 2's cache skips validation | **proven on device** | ATS_4 V record 1,394,358: flags 0x0b, 0 ms |
| Fix 2 published a runtime edit that survived reboot | **proven on device** | ATS_4 P at 31,666 (generation 2,827); the next boot validates 2,827 (record 1,394,565) |
| A save still took 14.35 s | **proven** | §10.2 |
| The trace flush delayed the arm and the start | **proven** | both pass probes (records 1,394,287, 1,394,356) name `AUTOSAVE_TRACE_FLUSH`; the H report at 20,426 shows the drain had not started |
| The copy is limited by 16-byte polls | **strongly supported** | driver source (`sdcard_lxr02.c:43`, `96`); phase 13 for 5 s at about 4.3 ms passes; 7.2 s matches |
| Capture limited by the bit walk under the budget | **supported** | 557 ms measured; about 180 bits per pass from the budget cap |
| Fix 3 gets an edit durable in about 2 s | **not measured** | §10.5 is an estimate; the poll cost per 16 bytes has not been measured |
| §11 returns to the previous subpage | **not tested** | built only |
| The step velocity edit is saved | **not known** | the Pattern drain ran on ATS_4, but the trace does not identify the steps it wrote |

There is no host test for AutoSave timing. The device is the only gate.

## 14. Device test for fix 3 and §11

Flash `build/LXRV2_lxr02.img` (730,952 B on disk), with logging on.

1. **Scalar edit:** boot, change one parameter (slide on voice 4), wait 5 s,
   power off, boot. The change must be there.
2. **Step edit:** the same, with one step's velocity.
3. **Both together,** as in the user's original test.
4. **After the Instrument Load/Save page:** visit it, leave, edit, wait 5 s,
   power off, boot.
5. **§11:** in VOICE mode, go to a subpage other than AEG, hold SHIFT and a
   step, release both (in either order); the same subpage must return.

Then bring the card. In the trace, each save should show:
- S within a few ms of the first D;
- A about 1,000 ms after S;
- V with flag 0x08 (cached) after the first save of a boot;
- C within about 100 ms of A;
- P within about 1 s of C;
- T within about 100 ms of P;
- f probes during the drain naming phase 13 for at most about 1 s;
- no `AUTOSAVE_TRACE_FLUSH` probe between a D and its T;
- a Pattern drain (w: `AUTOSAVE_PATTERN_DRAIN`) after a step edit.

If any of these fails, the step that failed is the next cause.

## 15. Open items

- **§5:** the other `#if DEV_MODE_LOGGING` blocks are not yet audited
  against §0.
- **§6:** the ADV_2 boot 5 drain error (phase 41) is unexplained; no later
  card has repeated it.
- **An in-flight trace flush is not cancelled.** If the device shows that
  one flush still costs more than a few hundred ms at full poll rate, the
  next step is to make the trace flush abort, or never start while any
  edit is likely (for example only after N s with no edits).
- **The Pattern drain waits behind the scalar drain** in the ladder. If the
  step edit is still late, give it its own deadline or run it first when its
  quiet window has passed.
- **`MEMORY.md`** needs rule §0 (5 and 6 included) and the new constants
  (§8). I have not edited it.
