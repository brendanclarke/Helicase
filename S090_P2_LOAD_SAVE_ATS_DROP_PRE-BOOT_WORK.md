# S090 P2 — Load/Save Drops AutoSave; Start-Up AutoSave Work Moves Before Boot Completes

**Status:** plan (2026-10-31; revised the same day with decisions D1–D5,
§0.3). Not implemented. All §11 questions decided (D1–D5). Implementation
schedule: `S090_P2_IMPLEMENTATION.md`; it needs one RAM sign-off (1 B boot
hold, its §0.1) and narrows Q1's cut set (its §1 F1).
**This file is the authority** for S090 P2 decisions and findings.
**Follows:** S090 P1 (`S090_P1_LOAD_SAVE_SPEEDUP.md`,
`S090_P1_IMPLEMENTATION.md`), implemented and building at 684,068 B.

---

## 0. Requirements

### 0.1 User requirements (2026-10-31, verbatim)

- **R1:** "any autosave work that would have to be performed immediately
  after startup should be performed BEFORE the boot is considered complete,
  done quickly, all files, synchronously, using the full CPU, before the
  interface unlocks and audio processing starts up. No autosave work should
  be performed after boot unless the user *does* something."
- **R2:** "any autosave is dropped as quickly as possible when entering
  load/save, take only enough time to mark the autosave as incomplete and
  re-start it from the beginning when the user exits, if it is still a
  necessary autosave to perform based on what was done in load/save."
- **R3:** "Do not try to make the autosave faster or shorter … I don't think
  any further optimizations are possible without breaking the format."

### 0.3 User decisions on the first open questions (2026-10-31)

| # | Question | User decision | Applied as |
|---|---|---|---|
| D1 | Q2 (start-up failure) | "All active records should be usable after boot if there is an immediate re-boot. All boot-time work must be safe if there is powerloss before boot completes. Why would autosave fail? A failure here should not be silent." | Durability requirement R4/R5 (§0.1); failure causes listed (§2.7); a failure stops at a visible boot notice (A10); the retry policy after a failure is §11 Q2b |
| D2 | Q4 (stage bound) | Accepted, "but think if we need to increase the boot time cap. This should be something quite long; we should let the boot process finish." | No short wall-clock cut: a time-based no-progress rule (30 s without progress) plus a long overall cap (120 s), about ten times the estimated worst case (§3.4, A12) |
| D3 | Q8 (boot Scene replay) | "Agree, prefer move." | `bank_revalidateVoiceEditMasks()` (the only retained-state step of the replay) moves before the stage; `preset_startDrumsetApply()` stays after `audioCodec_init()` (runtime only, no AutoSave marks; A9) |

| D4 | Q9 (failure notice), Q10 (runtime failures) | "No notice, trace is evidence. Same for Q10, we check the trace after a couple of boots." | A10 becomes a logging-build trace record (no LCD notice, boot never waits); runtime failures stay trace-only (they already emit `T` ERROR) |
| D5 | Q1, Q2b, Q3, Q5, Q6, Q7 | "Recommendations otherwise accepted." | Q1: the scalar HCNAMES convergence may be cut at phases 70/77/72/74, never at 75–76. Q2b: no autonomous runtime retry. Q3: `settings.cfg` is written in the stage. Q5: no reuse of the ensure's validation. Q6: one logging-only settle record pair; trace flushes run in the stage. Q7: restart 250 ms after exit |

R6 is therefore met by the trace (D4): a boot-stage failure is recorded in
logging builds and is not shown on the LCD.

Derived requirements:
- **R4:** after boot completes, every active AutoSave record (scalar A/B
  pair, Pattern pairs, `/.hcnames`, `settings.cfg` if Q3) is complete and
  durable, so an immediate reboot restores from it.
- **R5:** a power loss at any point of the boot stage leaves the card in a
  state the next boot restores from, with no loss of any record that was
  valid before the stage began.
- **R6:** a boot-stage failure is never silent.

### 0.2 Standing constraints that apply

- **No added underrun risk** (S090 U19): nothing may add work to a main-loop
  pass while audio runs. R1 work runs before `audioCodec_init()`, so it
  cannot affect audio.
- **Page display unchanged** (U18): blank means busy.
- **Shared caches** (U12): no change to AutoSave/Pattern/Bank/copy-clear use
  of `hcnames_name_mirror` or `fs_list_cache_name` beyond what R1/R2 need.
- **No AutoSave format, size, CRC, capture-rate or timing change** (R3, and
  the standing note: the section-based CRC redesign is the chosen path for
  write speed; do not tune `AUTOSAVE_PARAMETER_GETS_PER_WRITE` or capture
  timing).
- **RAM:** any new byte needs sign-off (bytes, region, owner).
- **Boot:** synchronous SD polling is allowed only before audio starts
  (MEMORY.md, Boot / Init Order).
- **No backward compatibility.**

---

## 1. What the trace shows (`SD_CARD_TEST_2/asavetrc.bin`)

Ticks are milliseconds since power-on. The boot in records #176796–#176880
is the 20-second case:

| Tick | Record | Meaning |
|---|---|---|
| 1,522 | `Q` | boot reader finished |
| 3,666 | `S` | scalar writer armed; due at 8,666 (`AUTOSAVE_WRITER_INTERVAL_MS` 5,000) |
| **8,666** | `A` | **parameter drain admitted; it owns the SD facade** |
| **14,028** | `O` Kit REQUEST branch A | **user opens Load/Save (Kit)**; request refused, deferred |
| 22,863 / 22,980 | `O` | retries, still refused |
| 31,236 | `V` | the drain finishes validating both records (winner A, gen 751) |
| 32,402 | `M` | mask merged: "post-merge canonical mask dirty=0" |
| **32,407** | `T` DONE | **drain finishes: 23.7 s on the card, nothing written** |
| 32,407 → 32,411 | `O` | `.hcnames` served from the RAM mirror in 4 ms (P1 S2); Kit index read starts |

**Last boot (#176881…):**
- The user opened Load/Save at 4,895, before the drain was due.
- The request waited 1.2 s for an unrecorded facade owner (§2.3 lists the
  candidates); the index read started at 6,115.
- The drain was never admitted, because the page was open.

**Conclusions:**
- The 23.7 s operation was a **recovery drain that found nothing to write**.
  It re-validated both A/B records (2 × 34,768 B) and read the winner's mask,
  at one 16-byte burst per main-loop pass while playing.
- Opening Load/Save did not stop it: the page guard blocks admission only.

---

## 2. Facts from the code

### 2.1 Every boot schedules a post-boot recovery drain (R1's main target)

- `filesystem_ensureAutosaveFilesBlocking()` (`filesystem.c` ≈29030–29036)
  ends with `fs_autosave_recovery_pending = 1u`. Its comment: "one
  recovery-pending attempt is armed lazily by the idle scheduler after the
  full configured interval even when SRAM starts clean. That attempt validates
  the winner and OR-merges interrupted file work; successful clean completion
  then disarms all recurring file activity."
- `filesystem_autosaveWriterSchedule_tick()` (≈27010) arms the writer when
  `fs_autosave_recovery_pending || autosave_maskHasDirty()`, then starts
  `FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN` after the 5 s deadline.
- **The first drain after boot has no cached winner.**
  `fs_autosave_winner_cached` is 0, so it runs the full dual-record
  validation (phases 0–5), then the mask phases 50–56. It reaches the copy
  phases (10–22, 62–67) only if dirt remains. In the trace none did.
- **The boot ensure already validated the winner** before audio
  (`V` at tick 1,371/1,330), but the runtime drain does not reuse that
  result. Reusing it would be an optimization, excluded by R3 (§11 Q5).

### 2.2 Other autonomous work after boot

| Work | Started by | After boot without user action? |
|---|---|---|
| Scalar recovery drain | `fs_autosave_recovery_pending` from the boot ensure | **yes, every boot** (§2.1) |
| Runtime AutoSave setup (`FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES`) | `filesystem_enableRuntimeSettingsWrites()` when `!fs_autosave_writer_boot_ready` | only when the boot ensure did not run or failed; not in the normal case |
| Pattern repair epoch (RAM relocations) | `patSvc_tick()` from `timebase_serviceFrontPanel()` at 500 Hz; budget-gated; suppressed on Load/Save pages | **yes**: a fresh reservation image waits for its first epoch after `patSvc_init()` (the `H` repair slices at ticks 5,375/5,382) |
| Non-semantic Pattern drain (`.patNNa/b`) | relocations in `patSvc_relocateIndex()`, `patSvc_repairStep()`, `patSvc_reactiveStep()`, `patSvc_exclusiveMove()` mark `autosave_markNonSemanticPatternDirty()` | **yes, if** the boot epoch relocates anything |
| Semantic Pattern drain | `autosave_patternDirtyMask()` | only if boot loading leaves a Pattern dirty (none in this trace) |
| HCNAMES convergence rewrite | inside the scalar drain (phases 70–77) when refreshed rows are captured | only as part of a drain |
| `settings.cfg` write | `filesystem_markSettingsDirty()` from the boot Bank load; a fresh debounce at `filesystem_enableRuntimeSettingsWrites()` | **yes**, about 1 s after boot (a candidate for the 1.2 s wait in the last boot). Classified "not AutoSave" in code (D24) |
| `midilrn.cfg` write | MIDI Learn edits | no |
| Deferred HCNAMES flush | Menu dirty rows at Load/Save exit | no (user action) |
| Dev trace flushes (`asavetrc.bin`, `pattrace.bin`) | pending trace records, 500/1000 ms | yes, logging builds only; not AutoSave |

### 2.3 Scheduling and gates

- **The Load/Save page guard stops admission only.** The scalar scheduler
  (≈27080), both Pattern schedulers (≈26885, ≈27254) and the repair epoch
  check `LOAD_PAGE`/`SAVE_PAGE` before *starting*. A drain already running
  continues to its end.
- **Background CPU budget:** the scalar drain (phases 13 and 56,
  ≈9203/9384) and the Pattern drain (≈15961) stop a slice when
  `filesystem_backgroundBudgetAvailable()` is false. The budget refills at
  2.5 % playing / 5 % stopped (`filesystem_backgroundBudgetRefill()`,
  ≈27683). A boot-time run would therefore be throttled to 5 % unless it
  bypasses the budget (R1: "using the full CPU").
- **Debounce deadlines:** the writer due tick (5 s, 250 ms continuation) and
  the non-semantic arm/due tick delay every drain even when the facade is
  idle.

### 2.4 The existing abort mechanism (R2's basis)

`filesystem_setAutosaveYield()` (≈26343) sets `fs_autosave_yield`, used by
every Bank load (S082 R6/D1, assessment A1/A2):

- **Scalar drain:** `filesystem_autosaveDrainAbortSafe()` allows an abort
  only at phases with no AsyncFATFS callback outstanding: 0, 1, 3, 5, 10, 13,
  16, 17, 19, 21, 30, 32, 34, 36, 38, 39, 44, 50, 52, 54, 56, 62, 64, 65,
  66, 67. The abort goes through `filesystem_autosaveWriterFinishError()`: it
  closes handles (phases 40–43) and restores the captured dirty bits
  (`autosave_maskRestoreCaptured()`, ≈8407).
- **Pattern drain** (≈16349–16380):
  - phases 0–1 finish ERROR at once;
  - phases 3–6 and 8 jump to the close at phase 9;
  - phases 2 (open pending) and 7 (seek) wait;
  - phases 9–10 and the HCNAMES Pattern follow-on are never cut.
- **Never cut:** the scalar HCNAMES convergence (70–77) and the Pattern
  HCNAMES follow-on. An abort between the live remove and the temp rename
  would lose `/.hcnames` (R6).
- **After an abort:**
  - the writer's error branch re-arms it with the 5 s deadline and clears
    `fs_autosave_winner_cached`, so the retry starts from phase 0 with full
    validation;
  - `fs_autosave_recovery_pending` is not cleared;
  - the Pattern completions re-mark their dirty bit.

### 2.5 Why an abort cannot lose a valid record

- **Copy phases (10–22, 62–67):** the winner is only the *source*. The
  target is the inactive A/B file, which is removed, recreated and committed
  by its valid-marker byte written last (phase 64). An abort before the
  commit leaves a torn inactive file, which the next drain repairs; after the
  commit, either record is a valid winner.
- **Recovery phases (25, 30–39, 44):** they run only when *no* record is
  valid. Aborting leaves that state unchanged.
- **Pattern files** are ping-pong pairs (`.patNNa/b`); the partner keeps the
  last valid file.
- **The S082 note** "Recovery writes may leave both A/B files invalid —
  accepted because the old Bank's records are ignored after the load" was
  written for Bank loads. For Load/Save entry it still holds, because
  recovery starts only when both are already invalid.

### 2.6 Boot order (main.c)

| Line | Step |
|---|---|
| ≈1277 | Pattern AutoSave boot reader |
| ≈1292 | `filesystem_ensureAutosaveFilesBlocking()` (sets recovery pending) |
| ≈1308 | `filesystem_enableRuntimeSettingsWrites()` (settings debounce; runtime setup gate) |
| 1351 / 1367 | `boot_filesystem_done:` → `filesystem_bootLoggingEnd()` (ends the boot deadline policy) |
| 1382 | `patSvc_init()` |
| ≈1390–1400 | `copyClear_init()`, `liveRec_init()` |
| 1412–1413 | `boot_showFilesystemStage(14u)`, **`audioCodec_init()`** |
| ≈1415 | replay of the boot Scene through the runtime Scene switch |
| 1471–1473 | `sequencerTimer_init()`, main loop |

The boot Scene replay after `audioCodec_init()` is two calls:
- **`bank_revalidateVoiceEditMasks()`:** a RAM repair of the directional VOICE
  edit masks. It marks `AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK` only when it
  changes something (`BankData.c` setters ≈399–508).
- **`preset_startDrumsetApply()`:** the live runtime apply. Its comment
  records that "hardware proved pre-audio target installation differs from
  the live target-edit path", so it must stay after the codec starts. The
  drumset/single-voice apply path (`presetManager.c` ≈1700–2330) makes no
  SceneData, Bank or AutoSave writes.

### 2.7 Why AutoSave work can fail (D1)

On a healthy card none of these is expected; each indicates a card or
filesystem problem, or a detector firing.

1. **SD transport errors** on the bit-banged bus: a command not accepted, a
   data-token or CRC error, or a card busy that never ends.
   - AsyncFATFS reports the operation failed, or enters
     `AFATFS_FILESYSTEM_STATE_FATAL`.
2. **Card full** (`afatfs_isFull()`): the inactive target, a Pattern file or
   `.hcnamtmp` cannot be allocated. The writers check the sticky full flag
   and close without touching the live file.
3. **Directory anomalies:**
   - duplicate same-name entries, refused rather than guessed (`HNDup`);
   - a register whose `#types` header is invalid during recovery (scalar
     phase 44, then the 45/46 remove path);
   - a target whose remove or create fails.
4. **A stall detector firing** (`DEV_STALL_DETECTION`): a phase unchanged for
   its tick threshold aborts with a named error (`Flsh`, the drain observer,
   and others). This is either a real hang, or a false trip of a poll-count
   threshold under fast pre-audio polling (§2.8).
5. **The logging-build boot deadline:** ten seconds per armed operation
   (`filesystem_bootLoggingArm()`); compiled out without
   `DEV_MODE_LOGGING`.

**Not failures:** a torn or invalid record, which the drain repairs; a
Bank-identity mismatch, which is a policy outcome (the records are ignored
and a fresh baseline is written).

**How failures surface today:** AutoSave failures are trace-only (MEMORY.md
S074: "self-recovering failures → trace only"). Nothing reaches the LCD, and
the writer simply retries five seconds later.

### 2.8 Existing bounds on boot work (D2)

- **Boot deadline:** ten seconds per armed operation, logging builds only;
  `filesystem_bootLoggingTimedOut()` returns 0 without logging. Not a product
  guard.
- **Stall detectors** count *polls* without phase change (thresholds 20,000–
  50,000), not time. Before audio the loop polls much faster than at
  runtime, so the same threshold is a much shorter time. The boot ladder's
  current operations survive it, but a drain's long card-busy phase (sync,
  allocation) has never run under pre-audio polling. A false abort there
  would turn a slow card into a boot failure (Risk 11).

---

## 3. Change A — start-up AutoSave work before boot completes (R1)

### 3.1 Design

Add one pre-audio **AutoSave settle stage** between `liveRec_init()` and
`boot_showFilesystemStage(14u)` / `audioCodec_init()`. It is placed after
`patSvc_init()` because the Pattern service must exist for the repair epoch
and the Pattern drains.

The stage **reuses the existing schedulers and operations unchanged** and
drives them synchronously until no AutoSave work remains:

```
settle loop (pre-audio, blocking):
    repeat:
        patSvc_tick()            # advances the repair epoch (and queue work)
        filesystem_tick()        # runs the existing AutoSave schedulers + ops
    until facade idle
      and no scalar work   (recovery_pending == 0, mask clean)
      and no Pattern work  (semantic and non-semantic masks empty)
      and repair epoch finished (no relocation left that could mark dirt)
      and every class has stopped retrying (§3.2 A5)
```

Three things differ from runtime, and only during the stage:
- **A2 full CPU:** the background budget does not apply.
  `filesystem_backgroundBudgetAvailable()` reports available while audio has
  not been started (a read-only check of the codec's initialized state). The
  repair epoch's budget gate (`patSvc_repairBudget()`) uses the same API.
- **A3 no debounce:** the writer's due tick and the non-semantic arm/due
  tick are treated as already due, so each drain starts as soon as the
  facade is idle.
- **A4 the repair epoch runs to completion** in the loop, so any relocation
  it makes is drained in the same stage instead of five seconds after audio
  starts.

**After the stage:**
- `fs_autosave_recovery_pending` is 0, the dirty masks are empty, and the
  repair epoch is idle. The runtime schedulers therefore have nothing to arm.
- **The next AutoSave write happens only after a mutation mark.** Every
  owner setter (edits, loads, MIDI writes, live record, copy/clear, Pattern
  edits) marks through Autosave.c, which is what R1 means by "the user does
  something".
- **The P1 S3 assumption still holds:** the `.hcnames` mirror stays VALID
  when audio starts (the drains end VALID or leave it INVALID only on
  failure).

### 3.2 Items

| # | Change | Where (expected) |
|---|---|---|
| A1 | The settle stage: one blocking wrapper, e.g. `filesystem_settleAutosaveBlocking()`, called from main.c before stage 14; it loops `patSvc_tick()` + `filesystem_tick()` until the §3.1 condition holds | `main.c`, `filesystem.c/h` |
| A2 | Budget bypass while audio has not started (read-only codec state; no new RAM if an accessor of `audio_hw_initialized` is added) | `filesystem.c` (`filesystem_backgroundBudgetAvailable()`), `AudioCodecManager.c/h` |
| A3 | Debounce bypass during the stage (the wrapper sets the due ticks to "now" before each pass, or the schedulers treat "audio not started" as due) | `filesystem.c` |
| A4 | A Pattern service query "repair epoch finished / nothing to relocate", so the loop knows when the epoch has ended | `PatternStackService.c/h` |
| A5 | Error policy: one attempt per work class. A failed class keeps its owed state (dirty bits, `recovery_pending`), is reported by A10, and is not retried inside the loop. Runtime retry policy: §11 Q2b | `filesystem.c` |
| A6 | `settings.cfg` pending from the boot Bank load is written in the same stage (§11 Q3) | `filesystem.c` |
| A7 | Boot screen stage number for the settle (DEV_MODE_DIAGNOSTIC display only) | `main.c` |
| A8 | Optional logging-only witness of the stage's start/end ticks to measure boot cost (§11 Q6) | `AutosaveTrace.h`, decoder |
| A9 | **Move** `bank_revalidateVoiceEditMasks()` from after `audioCodec_init()` to before the stage (D3), so any edit-mask repair it makes is written by the stage. `preset_startDrumsetApply()` stays after the codec start | `main.c` |
| A10 | **Failure notice (D1, R6):** if any class failed or the stage hit its bound, boot stops before `audioCodec_init()` on an LCD notice naming the class and the filesystem's named error code (`fs_error_code`), and waits for acknowledgement before continuing (form: §11 Q9). Logging builds also write the failure through the existing boot-failure log path | `main.c`, `filesystem.c/h` |
| A11 | **Durability rule (R4/R5):** the stage ends only with an idle facade, after every operation's final `afatfs_sync()` (the DONE path through `FS_INTERNAL_OP_FLUSH_FINISH`). It never ends with an operation in progress, so it never leaves a write half done (§3.5) | `filesystem.c` |
| A12 | **Bound (D2):** during the stage, the poll-count stall abort is replaced by a time-based no-progress rule (TIM2): an operation whose phase and byte cursor have not moved for 30 s is abandoned through its error close down. Plus one overall cap of 120 s for the whole stage. Both report through A10 (§3.4) | `filesystem.c` |

### 3.3 Expected cost

- **Boot time grows** by the settle stage. Before audio, the card is polled
  as fast as the loop can run. For comparison, the boot ensure (one full
  validation) took under 1 s between the MIDI Learn load and the `V` record.
  The recovery drain re-validates both records and reads the mask, so expect
  on the order of 1–2 s per boot with a clean card. It is longer when a write,
  a Pattern drain or an HCNAMES convergence is actually needed. To be
  measured (A8).
- **Runtime:** none of the 23.7 s post-boot drain remains.

### 3.4 Bounding the stage without cutting legitimate work (D2)

**Estimated worst case of legitimate work before audio:**
- **Pre-audio throughput:** about 90 KB/s read including overhead (the boot
  ensure read and validated about 70 KB in about 0.8 s, `V` at 1,371 after
  the MIDI Learn load at 575).
- **Scalar drain** with a real write: validate 2 × 34,768 B, write 34,768 B,
  CRC pass, syncs, HCNAMES convergence (2 KB temp, rename). About 2–3 s.
- **All 16 Scenes' Pattern drains** (10,656 B each, about 170 KB written with
  file truncation and syncs). About 4–8 s, since writes are slower than reads.
- **The repair epoch** is RAM work, well under a second. **`settings.cfg`**
  (if Q3) is small.
- **Total worst case:** about 10–12 s; typical clean boot about 1–2 s.

**The rule chosen:**
- **No wall-clock cut** of an operation that is still making progress.
  Progress is a phase change or a moved byte cursor, measured with TIM2, not
  with poll counts.
- **An operation is abandoned only after 30 s with no progress.** A card that
  works slowly still finishes.
- **One overall cap of 120 s** for the whole stage, about ten times the
  estimated worst case. It exists only so that a card that keeps making tiny
  progress forever cannot hold boot indefinitely.
- **When either limit fires:** the running operation goes through its
  existing error close down (no callback stranded, no live file left half
  replaced). Its owed state stays, and A10 reports it.
- **Why not the existing detectors as they are:** they count polls, and
  pre-audio polling is much faster, so 20,000–50,000 polls can be a short
  time (§2.8). They stay unchanged at runtime.

### 3.5 Durability: an immediate reboot and a power loss during the stage (R4, R5)

The stage introduces **no new write protocol**. It runs the existing
operations, each already crash-safe:

| Record | Protocol | After the stage | Power loss during the stage |
|---|---|---|---|
| Scalar A/B (`.hcprms1/2`) | the target is the inactive file; the valid marker byte is written last and synced | the newest complete record is synced | the winner is untouched until the marker commits a newer one; a torn target is repaired next time |
| Scalar recovery (both invalid) | rebuild from the baseline | both records valid | both stay invalid, as they already were; the next boot repeats the recovery |
| Pattern pairs (`.patNNa/b`) | the inactive partner is rewritten; the other stays valid | the newest pair member is synced | the partner keeps the last valid file |
| `/.hcnames` (convergence) | `.hcnamtmp` written and synced, live removed, temp renamed, final sync | live register synced, mirror VALID | the boot prelude in `filesystem_ensureAutosaveFiles_tick()` promotes a complete `.hcnamtmp` or discards an incomplete one |
| `settings.cfg` (if Q3) | the existing temp-file safe write | synced | the old file or the recoverable temp survives |

**Requirements on the stage itself:**
- **It completes or abandons each operation before moving on**, so no two
  writes overlap.
- **It ends only with an idle facade after the final sync (A11).** An
  immediate reboot after the boot screen clears therefore finds every record
  complete.
- **An abandoned operation (A12) uses the error close down,** which closes
  handles and never leaves the live file of a temp/rename pair removed (the
  HCNAMES live-remove/rename window, phases 75–76, is never cut). After an
  abandon, every record that was valid before the stage is still valid (R5),
  and the failure is reported (A10).

---

## 4. Change B — Load/Save drops AutoSave (R2)

### 4.1 Design

**Entering a Load/Save page asks every running AutoSave drain to stop**,
using the existing yield abort:
- the scalar drain's abort check (≈8828) and the Pattern drain's abort check
  (≈16370) also fire while `menu_activePage` is `LOAD_PAGE` or `SAVE_PAGE`;
- the same allow-lists and error close-downs apply unchanged.

The start of new drains is already blocked on those pages (§2.3), so nothing
restarts while the user is there.

### 4.2 Items

| # | Change | Where (expected) |
|---|---|---|
| B1 | Abort condition: `fs_autosave_yield` **or** a Load/Save page is open, in both drain ticks | `filesystem.c` |
| B2 | "Mark incomplete": already provided. The torn inactive file is simply not the winner, and the captured dirty bits are restored (`autosave_maskRestoreCaptured()`), so the work is still recorded as owed. No new state | none |
| B3 | "Restart from the beginning on exit": already provided. The error branch clears the winner cache, so the retry starts at phase 0. On exit, the scheduler's `fs_autosave_page_suppressed` path schedules it after `AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` (250 ms). Pattern bits restart through their schedulers | none (verify) |
| B4 | "Only if still necessary": already provided. The restart condition is `recovery_pending || mask dirty` (scalar) and the Pattern masks. A Bank Replace discards the mask (D25), a Merge re-marks, and Kit/Instrument/Scene loads mark what they changed | none (verify) |
| B5 | The Menu request that found the facade busy is retried on the first idle pass after the drop (existing dispatcher, P1 D2) | none |
| B6 | HCNAMES convergence (70–77) and the Pattern HCNAMES follow-on stay uncut unless §11 Q1 allows a cut before the live remove | `filesystem.c` (only if Q1 = yes) |

### 4.3 How long the drop takes

The drop is not instant. It waits for the next allowed phase plus the close
down.
- **Waiting for an allowed phase:** an open, close, remove or seek callback,
  typically tens to hundreds of ms while playing, because each step is
  bit-banged one burst per pass.
- **The close down:** closing the target can flush dirty cache sectors (up to
  8 × 512 B).
- **Worst case:** entering while the HCNAMES convergence runs. That rewrite
  of about 2 KB, with its temp/sync/remove/rename, is not cut today (R6) and
  can take seconds while playing (§11 Q1).

---

## 5. Out of scope

- Any change to AutoSave format, record size, CRC, validation, capture rate,
  `AUTOSAVE_PARAMETER_GETS_PER_WRITE` or writer intervals (R3).
- Reusing the boot ensure's validation to skip the recovery drain's
  validation (an optimization; §11 Q5).
- P1 behaviour (snapshot, mirror reads, index reuse, type steps).
- The LSR-01 flush on VOICE change (P1 D12).
- `midilrn.cfg`, the deferred HCNAMES flush and dev trace flushes, unless
  §11 says otherwise.

---

## 6. Risks

1. **Longer boot on every power-up** (§3.3). A card that needs a real write
   or several Pattern drains makes boot noticeably longer. It is bounded only
   by the work itself (R1 accepts this).
2. **A hung card blocks boot.**
   - The stage runs after `filesystem_bootLoggingEnd()`, so the boot
     deadline policy does not cover it.
   - **Mitigated by A12 (D2):** 30 s without progress abandons the
     operation, and the 120 s overall cap ends the stage. Both report through
     A10.
   - **Residual:** a card that never completes an AsyncFATFS callback is
     abandoned only after 30 s, so boot can take up to 120 s in the worst
     case before the notice. That is accepted ("let the boot process
     finish").
3. **Repair epoch completion.** The epoch is designed to be finite, but its
   end must be observable (A4). A query that never reports "done" would hang
   the stage, so A4 must be finite by construction and also bounded by Q4.
4. **Dirt created after the stage but before the user acts.**
   - **The replay's retained-state step moves before the stage (A9, D3).**
     The live apply that stays after `audioCodec_init()` makes no AutoSave
     marks (§2.6).
   - **Residual:** `menu_start()`, `sequencerTimer_init()` and the first main
     loop passes have not been audited for marks. The diagnostic-only
     `DEV_EFFECT_FORCE_TYPE` hook marks AutoSave by design (diagnostic builds
     only).
   - **Check:** test P1 (no `S`/`A` record before the first user action) is
     the gate.
5. **The budget bypass must never apply at runtime.** It is keyed to "audio
   not started". A later `audioCodec_suspend()` (no-playback storage
   commands) must not count as "not started", or Scene/Bank/Pattern commands
   would run drains unthrottled. The check must use the initialized state,
   not the suspended state.
6. **Drop latency is not zero** (§4.3); the HCNAMES convergence window is the
   long tail unless Q1 allows a cut.
7. **An abort repeated on every Load/Save entry.** If the user keeps
   entering Load/Save before a drain finishes, it never finishes, and each
   restart re-validates in full (no cache after an error). The work stays
   owed (dirty bits kept), so nothing is lost, but the on-card record can
   lag. Power loss in that period loses only the unwritten edits, as today
   between drains.
8. **A recovery abort when both records are already invalid.** It leaves no
   valid record until the restart completes, which is the same state as
   before the recovery began (§2.5). Power loss then means the next boot falls
   back to the Bank, as it would have anyway.
9. **The non-semantic drain's ERROR completion re-marks its bit**, so a
   repeated drop re-queues it. Same as Risk 7.
10. **Shared facade during the stage:** the trace flush schedulers also run
    in the loop in logging builds. That is harmless, but they lengthen boot
    slightly (§11 Q6).
11. **False stall aborts under fast pre-audio polling.** The existing
    detectors count polls (§2.8). A12 replaces them with the time-based rule
    *during the stage only*; if that switch were incomplete, a slow card
    could turn into a boot failure notice. Test P13 covers a slow card.
12. **The no-progress rule must see real progress.** "Progress" must include
    the byte cursors of streaming phases (copy, Pattern stream, HCNAMES
    stream), not only phase changes. Otherwise a long, healthy streaming phase
    could be taken for a stall. The drain's existing progress-aware observer
    (S074) is the model.
13. **A blocking notice delays boot until someone acknowledges it.** With no
    one at the instrument, it does not start playing after a card failure.
    That is the intended trade (R6); the alternative is §11 Q9.
14. **Owed work after a failure** stays owed. Whether it is retried
    autonomously is §11 Q2b. The records that were valid before the stage
    stay valid either way (§3.5).

---

## 7. Ambiguities (with the assumption this plan makes)

1. **What counts as "autosave work".**
   - **Assumed:** the scalar drain (including recovery and its HCNAMES
     convergence), both Pattern drains, and the Pattern repair epoch (it is
     RAM work, but its relocations create Pattern drains).
   - **Not assumed:** `settings.cfg`, `midilrn.cfg`, the deferred HCNAMES
     flush, dev trace flushes. `settings.cfg` is a decision (Q3).
2. **"Unless the user does something".** **Assumed:** any AutoSave mutation
   mark counts, including MIDI CC/PC from external gear and edits made by
   automation-free owner setters. Step automation and other runtime overlays
   are excluded from marking already (AUTOSAVE.md), so playback alone does
   not cause writes.
3. **"Would have to be performed immediately after startup".** **Assumed:**
   whatever the runtime would otherwise do without user action after boot,
   i.e. everything in §2.2 marked "yes". The recovery drain counts even when
   it finds nothing to write, because it holds the card.
4. **"Done quickly … using the full CPU".** **Assumed:** pre-audio polling at
   loop speed with the budget and debounce bypassed. No change to how much
   work a drain does (R3).
5. **"Before the interface unlocks".** **Assumed:** before
   `audioCodec_init()` and the main loop. Buttons and encoder are not serviced
   then; the LCD shows the boot screen.
6. **"Dropped as quickly as possible".** **Assumed:** at the next
   callback-free phase, through the existing close down, never in a way that
   can strand a callback or lose `/.hcnames`. The HCNAMES convergence cut is
   a decision (Q1).
7. **"Mark the autosave as incomplete".** **Assumed:** no new on-card or RAM
   marker. The A/B validity rule plus the restored dirty bits already record
   it. A torn inactive file is the "incomplete" mark.
8. **"Re-start it from the beginning when the user exits".**
   - **Assumed:** the existing 250 ms continuation after exit, starting at
     phase 0 with full validation.
   - **Alternative:** wait the full 5 s interval after exit (Q7).
9. **Which pages count as "load/save".** **Assumed:** `LOAD_PAGE` and
   `SAVE_PAGE` (top level and nested Instrument, which share the pages). Not
   the copy/clear menu, which already suspends AutoSave admission.
10. **Background Bank load (S082 P3).** The user may leave the page while it
    runs. **Assumed:** unchanged; `fs_autosave_yield` keeps AutoSave
    suspended until the load ends.
11. **AutoSave OFF or no resident Bank at boot.** **Assumed:** the stage does
    nothing. Turning AutoSave ON later or loading a Bank is a user action,
    and its runtime setup/recovery then runs at runtime as today.
12. **"A failure here should not be silent".** **Assumed:** this applies to
    the boot stage. Runtime AutoSave failures (drains started by user edits)
    stay trace-only as today, unless §11 Q10 says otherwise.
13. **"All active records".** **Assumed:** the scalar A/B pair, the Pattern
    pairs, `/.hcnames`, and `settings.cfg` if Q3 includes it. `midilrn.cfg`
    and the dev trace files are not AutoSave records.

---

## 8. Verification

**Static:** `make all && make img`; no new warnings; `link_budget.py`
before/after; the RAM delta equals §9; logging-off build clean.

**Hardware (logging on, card with a valid A/B pair):**

| # | Test | Pass |
|---|---|---|
| P1 | Boot, wait 60 s on any page, PLAY running | no `S`/`A`/Pattern drain record after audio start; trace flushes only |
| P2 | Boot → PLAY → Load/Save at once | names after one index read (no wait for a drain) |
| P3 | Boot time with a clean card | measured (A8 or stopwatch); recorded |
| P4 | Make edits, wait for the drain to start (`A`), open Load/Save | drop within the latency of §4.3; names appear after the drop plus one index read; a `T` ERROR record |
| P5 | Exit after P4 without changes | the drain restarts about 250 ms after exit and completes (`T` DONE); the record is current |
| P6 | P4, then a Bank Replace on the page | after exit, no drain for the replaced dirty mask (D25) |
| P7 | P4, then Kit loads on the page | after exit, one drain covering the loads |
| P8 | Edit a Pattern, open Load/Save during its drain | Pattern drain dropped, re-run after exit; `.patNNa/b` pair valid |
| P9 | Power off during a dropped-and-restarting drain | next boot restores the last valid record (no loss beyond unwritten edits) |
| P10 | Card with a torn record (pull power mid-drain), boot | the settle stage repairs it before audio; no post-boot drain |
| P11 | AutoSave OFF; no Bank | boot unchanged; no settle work |
| P12 | Listening throughout | no underrun (U19) |
| P13 | Slow/old card, dirty state at power-off (edits not yet saved), boot | the stage finishes (no false abort); boot time recorded |
| P14 | Boot, then power off right after the boot screen clears, boot again | identical restore: every record from the stage is valid (R4) |
| P15 | Power off repeatedly at different moments *during* the stage (dirty state each time), boot each time | every boot restores; no record that was valid before is lost (R5); a torn record is repaired by the next stage |
| P16 | Force a failure (e.g. a nearly full card so the inactive target cannot be created) | boot stops on the failure notice with the class and error code (R6); after acknowledgement, boot continues with the previous valid record |

---

## 9. RAM and flash

- **Expected RAM: 0 B** if "audio not started" is read from the codec's
  existing `audio_hw_initialized` through a new accessor, and the stage keeps
  its loop state on the stack. That includes A12's last phase, last cursor
  and TIM2 timestamps, the 120 s start time and A10's failed-class record,
  all locals of the blocking wrapper, which lives in `filesystem.c` and can
  read the operation statics directly.
- **Otherwise:** at most 1 B for a "boot settle active" flag. It needs
  sign-off; the owner would be `filesystem.c`, SRAM1 `.bss`.
- **Flash:** small (one wrapper, two abort conditions, one Pattern query, one
  budget check); measured at implementation.

## 10. Documents to update (at close-out)

- **`AUTOSAVE.md`:**
  - boot settle stage;
  - Load/Save drop (the abort trigger list gains "Load/Save page open");
  - no autonomous post-boot work.
- **`FILESYSTEM_SPEC.md`:** Load/Save binding notes (AutoSave dropped on
  entry, restarted after exit).
- **`MODULE_INTERCHANGE_SPEC.md`:** Boot / Init Order (the new stage between
  `liveRec_init()` and `audioCodec_init()`).
- **`MEMORY.md`:** Boot / Init Order line, AutoSave invariants.
- **`DEV_MODES.md` / decoder:** only if A8 adds a record.
- **Session log:** `knowledge_files/log_archive/090_SESSION_HANDOFF_LOG.md`
  (P2 section).

---

## 11. Open architectural questions (decision needed)

**Q1. May the drop cut the HCNAMES convergence before the live register is
touched?**
- **Today:** phases 70–77 (scalar) and the Pattern HCNAMES follow-on are
  never cut (R6).
- **A cut is safe before phase 75 (live remove).** Before that, only
  `.hcnamtmp` has been written. The live `/.hcnames` is intact, and the boot
  prelude discards an incomplete temp. The callback-free phases there are 70,
  77, 72 and 74.
- **Cost of a cut:** the mirror stays INVALID (it was invalidated at the
  convergence start), so the next entry pays one `.hcnames` card read. The
  refreshed bits are kept for the retry.
- **Recommendation:** yes. Allow the cut at 70/77/72/74 through a close-down
  of the temp handle; keep 75–76 uncut. It removes the long tail of §4.3.

**Q2. What happens when start-up work fails?** — **Decided (D1).**
Records must be usable on an immediate reboot (R4), the stage must be safe
under power loss (R5), and a failure must not be silent (R6). Applied as
§2.7 (causes), §3.5 (durability), A10 (notice), A11 (end only after the
final sync).

**Q2b. After a reported boot-stage failure, may the runtime retry the owed
work by itself?**
- **Recommendation:** no autonomous retry.
  - The user has seen the notice, and R1 forbids post-boot work without a
    user action.
  - The owed state (dirty bits, `recovery_pending`) stays. The next user
    mutation arms the writer, whose first drain validates in full and retries
    the write; the next boot retries the stage.
  - The records that were valid before the stage stay valid (§3.5).
- **Alternative:** one runtime retry after the notice is acknowledged. It
  would run while playing, which is the behaviour P2 removes.

**Q3. Is the `settings.cfg` write pending from the boot Bank load part of the
stage?**
- **The code calls it "not AutoSave" (D24),** but it is an autonomous write
  about 1 s after boot, and a likely owner of the 1.2 s wait in the last trace.
- **Recommendation:** yes, write it in the stage. It is one small file and
  removes the last routine post-boot write. `midilrn.cfg` is not pending at
  boot and needs no change.

**Q4. What bounds the stage if the card misbehaves?** — **Decided (D2),
with a long bound.**
- **No short cut:**
  - an operation is abandoned only after 30 s with no progress (TIM2, phase
    or byte cursor);
  - the whole stage is capped at 120 s, about ten times the estimated worst
    case of legitimate work (§3.4);
  - both report through A10.
- **The poll-count stall detectors are bypassed only inside the stage**
  (they would trip early under fast pre-audio polling; §2.8).
- **Logging builds:** the stage's operations also carry boot log codes, so a
  failure leaves a boot failure token; their ten-second logging deadline is
  raised to the same 30 s no-progress rule for these operations.
- **The 30 s / 120 s values** are to be confirmed against P3/P13
  measurements before release; raise them if a real card needs it.

**Q5. Should the stage reuse the boot ensure's validation instead of
re-validating?**
- **The ensure already validated the winner** a moment earlier, and the
  recovery drain validates both records again.
- **Recommendation:** no, per R3. Run the existing drain as it is. Revisit
  only if P3 shows the boot cost is unacceptable, and then as a separate
  decision.

**Q6. Add a logging-only boot settle witness (start/end ticks, classes run,
failures)?**
- **Recommendation:** yes, one record pair in `AutosaveTrace.h` with the
  decoder updated, logging builds only. It is the only way to measure P3/P10
  on the device, and it compiles out with logging off.
- **Decide also** whether the dev trace flushes may run inside the stage.
  Recommendation: yes (they are short and keep the trace complete).

**Q7. When should a dropped drain restart after Load/Save exit?**
- **Recommendation:** the existing 250 ms continuation. It is "re-start when
  the user exits", needs no change, and finishes the owed work soonest.
- **Alternative:** the full 5 s interval, if quick back-and-forth between
  Load/Save and other pages makes repeated restarts audible or annoying
  (Risk 7).

**Q8. What if the boot Scene replay after `audioCodec_init()` marks
AutoSave dirt (Risk 4)?** — **Decided (D3): move.**
- The replay's only retained-state step, `bank_revalidateVoiceEditMasks()`,
  moves before the stage (A9).
- `preset_startDrumsetApply()` stays after `audioCodec_init()`: it is
  runtime-only, makes no AutoSave marks, and must run live (hardware-proven,
  §2.6).
- Test P1 confirms no mark remains before the first user action.

**Q9. What form does the boot failure notice take (A10)?**
- **Recommendation:** a blocking LCD notice before audio starts.
  - **Line 1:** `AutoSave ERR`. **Line 2:** the class (`prm`, `pat NN`,
    `hcn`, `cfg`) and the filesystem's named error code.
  - Boot continues when BAR1 is pressed, the same precedent as the flash
    image check's `Img BAD s:… waits for BAR1`.
  - Not silent, and the user decides when to continue.
- **Alternative:** a timed notice (e.g. 5 s, like the "Unsupported card"
  warning) that continues by itself. It is less safe to miss, but boot never
  waits on a person (Risk 13).

**Q10. Should runtime AutoSave failures (user-triggered drains) also stop
being silent?**
- **Today** they are trace-only, and the writer retries.
- **Recommendation:** out of P2 scope; decide separately.
  - A runtime notice would need a non-blocking display path (a Menu error
    overlay), and a policy for repeated failures.
  - Raised here because D1's "a failure should not be silent" might be meant
    generally.
