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
