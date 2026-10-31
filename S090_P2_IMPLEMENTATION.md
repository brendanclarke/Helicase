# S090 P2 — Load/Save Drops AutoSave; Start-Up AutoSave Work Before Boot Completes: Implementation Schedule

**Plan:** `S090_P2_LOAD_SAVE_ATS_DROP_PRE-BOOT_WORK.md` (decisions D1–D5).
**Baseline:** the working tree of 2026-10-31 on `dev-ph7-voice_expansion`:
HEAD `7f8b5d07` plus the uncommitted S090 P1 implementation and its A1–A3
fixes. Every line number below is for that tree. Its image is text 683,648 /
data 420 / bss 413,020; payload 684,068 B.
**Status:** not implemented.
**Pre-verified:** every change below was applied to a copy of that tree in
the session scratchpad and built with the firmware flags, logging on and off
(§13). The decoder change was compiled and run on synthetic `'s'` records.
The code blocks are generated from those same verified edits.

---

## 0. Decisions applied and one approval needed

### 0.1 RAM sign-off needed before implementation

| Item | Bytes | Region | Owner | Lifetime | Why |
|---|---:|---|---|---|---|
| `autosave_boot_hold` | **1** | SRAM1 `.bss` | `Autosave.c` | set at the end of boot if start-up work is still owed; cleared by the first user mutation | Q2b ("no autonomous retry") cannot be met without it (§1 F2) |

In the verified build this byte landed in existing alignment padding: the
`.bss` section size is unchanged (413,020 B). It is still a new variable, so it
needs your approval. Everything else is stack-local or reuses existing state
(§1 F4).

### 0.2 Decisions

| # | Decision (user) | Applied as |
|---|---|---|
| R1 | All start-up AutoSave work before boot completes, synchronously, full CPU; none after boot without a user action | boot stage 13 `filesystem_settleAutosaveBlocking()` (F-10, M-1); budget lifted before audio (F-8, F-9); hold (V-1..V-7, F-5..F-7) |
| R2 | Drop AutoSave on Load/Save entry; restart on exit only if still needed | the existing safe abort fires on a Load/Save page (F-2..F-4); restart and necessity are existing behaviour (§1 F6) |
| R3 | No AutoSave optimization | no change to any drain's work, format, CRC, validation or intervals |
| D1/R4/R5 | Records usable on an immediate reboot; power-loss safe | the stage ends only with an idle facade after each operation's final sync; it runs only the existing safe-write operations (§1 F7) |
| D2 | Long bound | 30 s without progress / 120 s cap (C-1, F-10) |
| D3 | Move the replay's retained-state step | `bank_revalidateVoiceEditMasks()` before the stage (M-1, M-2) |
| D4 | No notice; trace is the evidence (also for runtime failures) | `'s'` START/FAIL/END records (T-1, T-2, F-10, D-1..D-3); runtime failures keep their existing `T` ERROR record |
| D5/Q1 | Cut the HCNAMES convergence before the live register is touched | phases 70, 77 and 72 while rows remain (F-2); **74 excluded** (§1 F1) |
| D5/Q2b | No autonomous retry after a failed settle | the boot hold (§1 F2) |
| D5/Q3 | `settings.cfg` in the stage | SETTINGS class (F-10) |
| D5/Q5 | No reuse of the ensure's validation | the existing drain runs as it is |
| D5/Q6 | Settle trace record; trace flushes run in the stage | T-1/T-2; the normal flush rungs of `filesystem_tick()` run in the loop |
| D5/Q7 | Restart 250 ms after exit | existing continuation (no change) |

## 1. Findings from the deep-dive

**F1. Q1's phase list is narrowed: 74 is excluded.**
- **A cut at 74 is unsafe.** At phase 74 the temp `.hcnamtmp` is complete
  (all 161 rows, closed, about to be synced) and the live `/.hcnames` is
  intact. A cut there leaves both on the card.
- **The next boot would promote that temp.** The boot prelude in
  `filesystem_ensureAutosaveFiles_tick()` promotes any temp that has the
  `#types` header plus 161 rows. This temp carries refreshed-bit (`R`)
  suppression for a record commit whose marker byte may not yet be durable:
  the convergence runs before the operation's final sync, and an ERROR finish
  skips that sync.
- **The safe set is 70, 77, and 72 while rows remain.** In each case the temp
  holds fewer than 161 rows, so the prelude discards it, and the live register
  is untouched. Phase 73 waits for a close, 75/76 for remove/rename; none is
  cut.
- **Remaining wait on entry:** up to one close + sync + remove + rename, about
  1 s while playing.

**F2. Q2b needs one byte of state.**
- **Without it, the runtime retries anyway.** The schedulers decide only from
  `fs_autosave_recovery_pending || autosave_maskHasDirty()` and the Pattern
  masks. After a failed settle the owed state remains, so the writer would
  start 5 s after audio, which is the behaviour R1 forbids.
- **The hold blocks the three AutoSave schedulers** until the first
  user-caused mutation.
- **Release points:**
  - the two scalar funnels: `autosave_markPayloadOffsetDirty()` and
    `autosave_maskPayloadRangeDirty()`, which every owner setter and every
    load passes through;
  - `autosave_markPatternDirty()` (semantic Pattern edits);
  - `autosave_setMutationTrackingEnabled()` (an AutoSave or Bank lifecycle
    change).
- **Correctly not release points:**
  - `autosave_maskRestoreCaptured()`, a drain's error restore, uses
    `autosave_maskByteOr()` directly and never reaches a funnel;
  - `autosave_markNonSemanticPatternDirty()` comes from repair relocations,
    not from the user;
  - the Pattern drain's error re-mark (`autosave_markPatternDirty()`) can
    happen only while drains run, i.e. before the hold is set or after it has
    been released.
- **After release,** the owed work is written together with the new change by
  the normal debounce. The next boot retries the stage anyway.

**F3. Q4's boot-log re-arm is not used; the existing detectors are.**
- **No boot-log re-arm.** The boot logger ends at
  `filesystem_bootLoggingEnd()` (main.c 1367). Its armed operations carry a
  ten-second *abandon* policy that would cut long but healthy work, which is
  the opposite of D2. The settle stage's own `'s'` records are the evidence
  (D4).
- **Stall handling reuses what exists:**
  - while progressing, the stage ticks with `filesystem_tickUncounted()`, so
    the poll-count stall detectors do not count fast pre-audio polls;
  - after 30 s without progress it raises the Bank-load yield (both drains
    abort at their safe phases) and switches to counted ticks, so the
    existing detectors (scalar drain, final flush, settings and MIDI Learn
    writers) can fire and abort through their designed close-downs.
- **The Pattern drain, the HCNAMES update path and the trace flushes have no
  stall detector.** For them the drain abort hooks are the unwind path.
- **If nothing moves for another 30 s,** or 30 s after the 120 s cap, the
  stage ends and boot continues with that one operation still owning the
  facade (`BUSY` flag in the trace). It finishes or aborts at runtime.
  Forcing an operation to complete while a callback is due is never done.

**F4. Failure detection without new state.**
- **The status is gone by the time the stage looks.** Completion callbacks
  acknowledge the facade themselves, so the stage reads the state each
  callback leaves behind (`filesystem_settleClassifyEnd()`):
  - **scalar drain:** the re-armed due time; ERROR is 5,000 ms ahead, a DONE
    continuation 250 ms;
  - **runtime setup:** `fs_autosave_setup_failed`;
  - **Pattern drain:** the drained Scene's bit is re-marked, in the class the
    drain consumed when it started (recorded at start);
  - **settings:** `fs_settings_dirty` is set again.
- **One attempt:** the first failure stops admission, and the stage ends at
  the next idle facade. R4/R5 are unaffected, because records that were valid
  stay valid.

**F5. The budget must also not be charged before audio.**
- **The problem:** credit is signed (`filesystem_backgroundBudgetCharge()`).
  Charging the whole settle stage would leave a deficit that suppressed every
  budgeted runtime job (drains, repair) long after audio starts.
- **The fix:** F-9 skips the deduction before audio, and the DEV accounting
  still records the slices.
- **Keyed on the codec's lifetime init flag** (`audio_hw_initialized`), not on
  its suspend flag. Audio-suspended storage commands at runtime stay budgeted
  (plan Risk 5).

**F6. R2's restart and necessity are existing behaviour (verified).**
- **On abort:**
  - the scalar error close-down restores the captured dirty bits
    (`autosave_maskRestoreCaptured()`, `filesystem.c` 8407);
  - the writer's error branch clears the winner cache, so the retry starts at
    phase 0 with full validation;
  - `recovery_pending` is kept;
  - the Pattern completions re-mark their bit.
- **On exit:** the scheduler, page-suppressed while on the page, schedules
  the restart `AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS` (250 ms) after exit.
- **Only if still needed:** the restart requires
  `recovery_pending || mask dirty`, or a Pattern bit. A Bank Replace discards
  the mask (D25); loads mark what they changed.

**F7. Durability (R4/R5) holds by construction.**
- **No new write protocol.** The stage only drives existing operations.
- **It ends with an idle facade,** after each DONE operation's final
  `afatfs_sync()` (`FS_INTERNAL_OP_FLUSH_FINISH`). The single exception is the
  F3 `BUSY` exit on a hung card.
- **Power loss is covered by each operation's own protocol:** A/B marker last,
  Pattern ping-pong, HCNAMES temp/rename with boot-prelude recovery, and the
  `settings.cfg` temp write.

**F8. Gates open during the stage.**
- `menu_init()` ends on `VOICE1_PAGE` (menu.c 20160), so the page gates in
  the schedulers, the repair epoch and the new drop are open.
- `copyClear_init()` has run (no suspension), and `patSvc_init()` has opened
  the service with a fresh epoch (`tier1_scan_cursor` 0).

**F9. AutoSave OFF or no Bank.** The AutoSave classes report nothing, using
the same gates as the schedulers. The repair epoch still runs before audio;
it is RAM work and now simply finishes earlier. With tracking off, its
relocations make no marks.

**F10. Two narrow behaviours to know.**
- **A failed `settings.cfg` write is not held.** It is not AutoSave in code
  (D24), so it retries at runtime after its 1 s debounce.
- **The stall path can leave the repair epoch unfinished.** If the stage
  stalls before the epoch ends, the epoch continues at runtime, and its
  relocations' non-semantic marks are held only if the hold was set.

**F11. Measured (scratch build, §13).**

| | Baseline | Patched | Delta |
|---|---:|---:|---:|
| text | 683,648 | 684,968 | +1,320 |
| data | 420 | 420 | 0 |
| bss | 413,020 | 413,020 | 0 (the 1 B hold landed in padding) |
| image payload | 684,068 | 685,388 | **+1,320 B** (68,276 B headroom) |
| ITCM / DTCM statics | 15,240 / 4,372 | 15,240 / 4,372 | 0 |

- **Logging off:** text 668,488, bss 396,176.
- **Stack:** the wrapper's locals (about 40 B) during the stage only.

## 2. How to apply

- **One implementation.** All changes in §3–§9 go in together.
- **Line numbers** are original working-tree lines; apply each file from the
  bottom up.
- **Kinds:**
  - **MODIFY** replaces the listed lines;
  - **REMOVE** deletes them;
  - **ADD** inserts the block before the listed line, which is quoted
    unchanged.
- **Blocks are exact:** a block that ends with an empty line includes that
  empty line, which separates the new code from the line after it.
- **Then:** `make all && make img`, compare with §10, and run the hardware
  tests in §10.2.

### 2.1 Change index

| Item | What | Changes |
|---|---|---|
| A1/A3/A5/A11/A12 | Settle stage | C-1, H-1, F-10, M-1 |
| A2 | Budget lifted before audio | A-1, A-2, F-1, F-8, F-9 |
| A4 | Repair epoch completion query | S-1, S-2 |
| A7 | Boot stage 13 | M-1 |
| A9 | Edit-mask repair moved | M-1, M-2 |
| R2/Q1 | Load/Save drop | F-2, F-3, F-4 |
| Q2b | Boot hold | V-1..V-7, F-5, F-6, F-7 |
| Q6/D4 | `'s'` trace | T-1, T-2, D-1, D-2, D-3 |

## 3. `config.h`

A12 bounds.

#### C-1 — ADD before line 564

A12. The three settle-stage bounds (30 s no progress, 120 s cap, 1 s error-due discriminator).

Anchor (line 564, unchanged): `(blank)`

New block:

```c
/*
 * Pre-audio AutoSave settle stage bounds (S090 P2, user decision D2).
 *
 * What: filesystem_settleAutosaveBlocking() drives every start-up AutoSave
 * operation to completion before audioCodec_init(). These three values bound
 * it without cutting legitimate work:
 * - BOOT_SETTLE_NO_PROGRESS_MS: an operation whose progress signature (phase,
 *   byte and row cursors, pending classes) has not changed for this long is
 *   treated as stalled. The stage then stops admitting work and lets the
 *   existing stall detectors and abort hooks unwind it; if nothing moves for
 *   another such window, boot continues with the operation still owning the
 *   facade (it finishes or aborts at runtime).
 * - BOOT_SETTLE_CAP_MS: the whole stage, about ten times the estimated worst
 *   case of legitimate pre-audio work (~10-12 s: a full scalar drain plus
 *   sixteen Pattern drains). It exists only so a card that keeps making tiny
 *   progress forever cannot hold boot indefinitely ("let the boot process
 *   finish").
 * - BOOT_SETTLE_ERROR_DUE_MS: how the stage tells a failed scalar drain from a
 *   successful one that still has work. filesystem_autosaveWriterCompleted()
 *   re-arms an ERROR with AUTOSAVE_WRITER_INTERVAL_MS (5,000 ms) and a DONE
 *   with remaining dirt with AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS (250 ms);
 *   1,000 ms lies safely between the two.
 * Why time, not polls: the existing stall detectors count polls, and the
 * pre-audio loop polls far faster than the runtime loop, so a poll threshold
 * would be a much shorter time there (S090 P2 §2.8).
 * Affiliates: filesystem_settleAutosaveBlocking(), main.c boot stage 13.
 * The 16-bit time_sysTick comparison limit (32,768 ms) does not apply: the
 * stage measures these with the 32-bit TIM2 microsecond counter.
 */
#define BOOT_SETTLE_NO_PROGRESS_MS  30000u
#define BOOT_SETTLE_CAP_MS         120000u
#define BOOT_SETTLE_ERROR_DUE_MS     1000u

```

## 4. `Core/Hardware/AudioCodecManager.h` and `.c`

A2: the read-only "audio started" query (no RAM).

### `Core/Hardware/AudioCodecManager.h`

#### A-1 — ADD before line 104

A2. Declare `audioCodec_isInitialized()`.

Anchor (line 104, unchanged): `(blank)`

New block:

```c
/*
 * Report whether audioCodec_init() has run (S090 P2).
 *
 * What: returns the existing audio_hw_initialized flag: 0 from reset until
 * audioCodec_init() completes, 1 afterwards for the rest of the session.
 * Unlike audioCodec_isSuspended(), a later audioCodec_suspend() (no-playback
 * storage commands, sample installs) does not clear it.
 * Why: the pre-audio AutoSave settle stage runs with the background CPU
 * budget lifted ("using the full CPU", user R1). Keying that on this flag
 * makes the bypass impossible at runtime, including during audio-suspended
 * storage commands, which keep their budgeted behaviour. No new RAM.
 * Accessors: filesystem_backgroundBudgetAvailable(),
 * filesystem_backgroundBudgetCharge().
 */
uint8_t audioCodec_isInitialized(void);

```

### `Core/Hardware/AudioCodecManager.c`

#### A-2 — ADD before line 698

A2. Define it (reads the existing `audio_hw_initialized`; no RAM).

Anchor (line 698, unchanged): `(blank)`

New block:

```c
uint8_t audioCodec_isInitialized(void)
{
    /*
     * Read-only view of the lifetime init flag (S090 P2; contract in
     * AudioCodecManager.h). audio_hw_initialized is set once at the end of
     * audioCodec_init() and never cleared, so 0 means "boot has not started
     * audio yet".
     */
    return audio_hw_initialized;
}

```

## 5. `Core/Bank/Scene/Pattern/PatternStackService.h` and `.c`

A4: the repair epoch completion query.

### `Core/Bank/Scene/Pattern/PatternStackService.h`

#### S-1 — ADD before line 52

A4. Declare `patSvc_repairEpochIdle()`.

Anchor (line 52, unchanged): `(blank)`

New block:

```c
/*
 * Report whether the bounded repair epoch has finished (S090 P2).
 *
 * What: nonzero when the service is open (no handover), has no reactive,
 * bulk or queued work, no rebuild is pending, and the tier-1 repair cursor
 * has walked every address entry. Finite by construction: the cursor only
 * advances, through PATSVC_ADDRESS_COUNT entries, and only a mutation,
 * handover or replacement (none of which happens before audio starts)
 * restarts it.
 * Why: the pre-audio AutoSave settle stage runs the repair epoch to its end
 * so any relocation it makes (each marks non-semantic Pattern AutoSave work)
 * is written before boot completes instead of seconds after audio starts.
 * Accessors: filesystem_settleAutosaveBlocking() (through
 * filesystem_settlePendingClasses()). Affiliates: patSvc_tick() repair section.
 */
uint8_t patSvc_repairEpochIdle(void);

```

### `Core/Bank/Scene/Pattern/PatternStackService.c`

#### S-2 — ADD before line 1701

A4. Define it, directly after `patSvc_idle()`.

Anchor (line 1701, unchanged): `(blank)`

New block:

```c
uint8_t patSvc_repairEpochIdle(void)
{
    /*
     * The repair epoch is idle (S090 P2; contract in PatternStackService.h).
     *
     * Each term mirrors a branch of patSvc_tick() that returns before the
     * repair section, plus the section's own continuation condition:
     * - service_open && !service_handover: the epoch belongs to an open
     *   service Scene (a handover restarts it);
     * - !reactive_active, bulk_op NONE, empty queue: patSvc_tick() would serve
     *   these first and not reach the repair section;
     * - !reservation_rebuild_pending: a pending rebuild restarts the cursor;
     * - tier1_scan_cursor >= PATSVC_ADDRESS_COUNT: every address entry has
     *   been inspected, so no further relocation (and no non-semantic mark)
     *   can come from this epoch.
     */
    return (uint8_t)(service_open && !service_handover &&
                     !reactive_active &&
                     bulk_op == PATSVC_OP_NONE &&
                     patSvc_queueCount() == 0u &&
                     !reservation_rebuild_pending &&
                     tier1_scan_cursor >= PATSVC_ADDRESS_COUNT);
}

```

## 6. `Core/Bank/Scene/Autosave.c`, `Autosave.h`, `AutosaveTrace.h`

Q2b boot hold (1 B, §0.1) and its release points; Q6/D4 trace record.

### `Core/Bank/Scene/Autosave.c`

#### V-1 — ADD before line 176

Q2b. The boot-settle hold byte (1 B, needs sign-off; §0.1).

Anchor (line 176, unchanged): `/*`

New block:

```c
/*
 * Boot-settle hold (S090 P2, decision Q2b). RAM: 1 byte, SRAM1 .bss, owner
 * Autosave.c. Requires user sign-off before implementation.
 *
 * What: nonzero after the pre-audio settle stage ended with start-up AutoSave
 * work still owed (an operation failed, stalled or hit the stage cap). While
 * set, filesystem's scalar, Pattern and non-semantic Pattern schedulers start
 * nothing. The first user-caused mutation clears it: an accepted mark in
 * autosave_markPayloadOffsetDirty() or autosave_maskPayloadRangeDirty() (the
 * two scalar funnels), autosave_markPatternDirty() (semantic Pattern edits),
 * or a tracking lifecycle change (autosave_setMutationTrackingEnabled()).
 * Why: R1 forbids AutoSave work after boot "unless the user does something",
 * and Q2b chose no autonomous retry after a boot-stage failure. The owed state
 * itself (dirty bits, recovery_pending) is kept, so the first drain after a
 * user action writes it together with the new change; the next boot retries
 * the stage. autosave_maskRestoreCaptured() (a drain's error restore) and
 * autosave_markNonSemanticPatternDirty() (repair relocations) are not user
 * actions and do not clear it.
 * Lifetime: set once at the end of boot or never; cleared by the first user
 * mutation. Accessors: autosave_setBootHold(), autosave_bootHoldActive().
 * Affiliates: filesystem_settleAutosaveBlocking() and the three schedulers.
 */
static volatile uint8_t autosave_boot_hold;
```

#### V-2 — ADD before line 265

Q2b. A semantic Pattern edit releases the hold (inside the existing critical section of `autosave_markPatternDirty()`).

Anchor (line 265, unchanged): `/* Keep the dirty bit, quiet-window stamp, and HCNAMES witness clear in`

New block:

```c
    /* S090 P2: a semantic Pattern edit is a user action; release the
     * boot-settle hold so the owed and new Pattern work can be written. */
    autosave_boot_hold = 0u;
```

#### V-3 — ADD before line 419

Q2b. An accepted scalar mark releases the hold (`autosave_markPayloadOffsetDirty()`, after the mask OR).

Anchor (line 419, unchanged): `/*`

New block:

```c
    /*
     * S090 P2: an accepted scalar mark is the user "doing something" (R1).
     * Release the boot-settle hold so the scheduler may write the owed and
     * the new work together. Only marks past the tracking/range guard reach
     * this point; the drain's error restore uses autosave_maskByteOr()
     * directly and never passes here.
     */
    autosave_boot_hold = 0u;
```

#### V-4 — ADD before line 494

Q2b. An accepted range mark releases the hold (`autosave_maskPayloadRangeDirty()`, after the IRQ restore).

Anchor (line 494, unchanged): `return fresh_total;`

New block:

```c
    /* S090 P2: an accepted range mark (Scene/Bank replacement by a load) is a
     * user action; release the boot-settle hold, as the per-cell funnel does. */
    autosave_boot_hold = 0u;
```

#### V-5 — MODIFY line 1805

Q2b. A tracking lifecycle change releases the hold (`autosave_setMutationTrackingEnabled()`).

First original line (1805): `autosave_mutation_tracking_enabled = enabled ? 1u : 0u;`  
Last original line (1805): `autosave_mutation_tracking_enabled = enabled ? 1u : 0u;`

Original:

```c
    autosave_mutation_tracking_enabled = enabled ? 1u : 0u;
```

Replacement:

```c
    autosave_mutation_tracking_enabled = enabled ? 1u : 0u;
    /* S090 P2: a tracking lifecycle change (AutoSave OFF/ON, Bank lifecycle)
     * starts a new session; any boot-settle hold from an earlier one ends. */
    autosave_boot_hold = 0u;
```

#### V-6 — ADD before line 1808

Q2b. Setter and accessor for the hold.

Anchor (line 1808, unchanged): `(blank)`

New block:

```c
void autosave_setBootHold(uint8_t hold)
{
    /*
     * Set or clear the boot-settle hold (S090 P2; contract in Autosave.h).
     * Single-byte store: readers in foreground schedulers and the clearing
     * markers (some called from interrupt context) see it atomically.
     */
    autosave_boot_hold = hold ? 1u : 0u;
}

uint8_t autosave_bootHoldActive(void)
{
    /* Read the boot-settle hold (S090 P2). */
    return autosave_boot_hold;
}

```

### `Core/Bank/Scene/Autosave.h`

#### V-7 — ADD before line 529

Q2b. Declarations.

Anchor (line 529, unchanged): `/*`

New block:

```c
/*
 * Boot-settle hold (S090 P2, Q2b).
 *
 * autosave_setBootHold(1) is called by filesystem_settleAutosaveBlocking()
 * when the pre-audio stage ends with start-up AutoSave work still owed.
 * While autosave_bootHoldActive() is nonzero the filesystem scalar, Pattern
 * and non-semantic Pattern schedulers admit nothing. The first user-caused
 * mutation (an accepted scalar or semantic Pattern mark, or a tracking
 * lifecycle change) clears it. Why: no autonomous AutoSave retry after boot
 * (R1, Q2b); owed work is written with the user's next change.
 */
void autosave_setBootHold(uint8_t hold);
uint8_t autosave_bootHoldActive(void);
```

### `Core/Bank/Scene/AutosaveTrace.h`

#### T-1 — ADD before line 280

Q6/D4. Trace stage `'s'` (settle START/FAIL/END).

Anchor (line 280, unchanged): `} autosave_trace_stage_t;`

New block:

```c
    /*
     * Pre-audio AutoSave settle stage (S090 P2, decisions D4/Q6). DEV logging
     * only; no product state depends on these records.
     * flags = event:
     *   0x01 START: value32 = classes pending when the stage began
     *        (AUTOSAVE_TRACE_SETTLE_CLASS_* bits).
     *   0x03 FAIL: one operation of a class ended in error. value32 = class
     *        bit | internal op << 8 | stage elapsed ms (saturated 0xFFFF)
     *        << 16. The stage then stops admitting work (one attempt).
     *   0x02 END: upper flag bits AUTOSAVE_TRACE_SETTLE_FLAG_* (FAILED,
     *        STALL, CAP, BUSY). value32 = stage elapsed ms (saturated
     *        0xFFFFFF) | classes still pending << 24. A nonzero pending byte
     *        means the boot-settle hold was set (Q2b).
     * Why: user D4: "no notice, trace is evidence"; also measures boot cost
     * (P3). Producer: filesystem_settleAutosaveBlocking(). Consumer:
     * tools/diagnostic_tools/decode_devlogs.py ('s' branch).
     */
    AUTOSAVE_TRACE_STAGE_BOOT_SETTLE = 's',
```

#### T-2 — ADD before line 285

Q6/D4. Event, flag and class constants shared by firmware and decoder.

Anchor (line 285, unchanged): `(blank)`

New block:

```c
/* S090 P2 settle record events (low nibble of flags) and END flag bits. */
#define AUTOSAVE_TRACE_SETTLE_START          0x01u
#define AUTOSAVE_TRACE_SETTLE_END            0x02u
#define AUTOSAVE_TRACE_SETTLE_FAIL           0x03u
#define AUTOSAVE_TRACE_SETTLE_FLAG_FAILED    0x10u
#define AUTOSAVE_TRACE_SETTLE_FLAG_STALL     0x20u
#define AUTOSAVE_TRACE_SETTLE_FLAG_CAP       0x40u
#define AUTOSAVE_TRACE_SETTLE_FLAG_BUSY      0x80u
/* Work classes the settle stage drives (value bits of START/END/FAIL). */
#define AUTOSAVE_TRACE_SETTLE_CLASS_SCALAR      0x01u
#define AUTOSAVE_TRACE_SETTLE_CLASS_SETUP       0x02u
#define AUTOSAVE_TRACE_SETTLE_CLASS_PATTERN     0x04u
#define AUTOSAVE_TRACE_SETTLE_CLASS_NONSEMANTIC 0x08u
#define AUTOSAVE_TRACE_SETTLE_CLASS_REPAIR      0x10u
#define AUTOSAVE_TRACE_SETTLE_CLASS_SETTINGS    0x20u
#define AUTOSAVE_TRACE_SETTLE_CLASS_AUTOSAVE \
    (AUTOSAVE_TRACE_SETTLE_CLASS_SCALAR | AUTOSAVE_TRACE_SETTLE_CLASS_SETUP | \
     AUTOSAVE_TRACE_SETTLE_CLASS_PATTERN | \
     AUTOSAVE_TRACE_SETTLE_CLASS_NONSEMANTIC)

```

## 7. `Core/Hardware/SD/filesystem.h` and `filesystem.c`

A1-A3/A5/A11/A12 settle stage; R2/Q1 Load/Save drop; Q2b scheduler gates; A2 budget.

### `Core/Hardware/SD/filesystem.h`

#### H-1 — ADD before line 272

A1. Declare `filesystem_settleAutosaveBlocking()`.

Anchor (line 272, unchanged): `/*`

New block:

```c
/*
 * Drive all start-up AutoSave work to completion before audio (S090 P2, R1).
 *
 * What: a blocking pre-audio stage, called once from main.c after
 * patSvc_init()/copyClear_init()/liveRec_init() and before
 * audioCodec_init(). It repeatedly calls patSvc_tick() and
 * filesystem_tickUncounted() with every AutoSave debounce made due and the
 * background CPU budget lifted (audio not started), until no start-up work
 * remains: the scalar recovery/dirty drain, runtime AutoSave setup, semantic
 * and non-semantic Pattern drains, the Pattern repair epoch, and a pending
 * settings.cfg write (Q3). The existing operations run unchanged and each
 * ends with its own final sync, so every record is complete and durable when
 * this returns with an idle facade (R4); a power loss at any point is covered
 * by their existing safe-write protocols (R5).
 * Bounds (D2): BOOT_SETTLE_NO_PROGRESS_MS without progress or
 * BOOT_SETTLE_CAP_MS overall stop admission and let the running operation
 * unwind (see config.h). One attempt per operation: the first failure stops
 * admission (Q2b).
 * Output: the classes still pending (0 normally). When AutoSave work is still
 * owed, the boot-settle hold is set (autosave_setBootHold()) so nothing runs
 * after boot until the user changes something. DEV logging records 's'
 * START/FAIL/END (D4: the trace is the evidence; nothing is displayed).
 * Affiliates: filesystem_backgroundBudgetAvailable(), patSvc_repairEpochIdle(),
 * autosave_bootHoldActive(), the four AutoSave/settings schedulers.
 */
uint8_t     filesystem_settleAutosaveBlocking(void);
```

### `Core/Hardware/SD/filesystem.c`

#### F-1 — ADD before line 89

A2. Include `AudioCodecManager.h` in `filesystem.c`.

Anchor (line 89, unchanged): `#include "random.h"`

New block:

```c
/* S090 P2: audioCodec_isInitialized() keys the pre-audio budget bypass. */
#include "AudioCodecManager.h"
```

#### F-2 — ADD before line 8763

R2/Q1. `filesystem_loadSavePageOpen()` and the Load/Save cut allow-list `filesystem_autosaveDrainLoadSaveCutSafe()`, after `filesystem_autosaveDrainAbortSafe()`.

Anchor (line 8763, unchanged): `(blank)`

New block:

```c

/*
 * Report whether a Load/Save page is open (S090 P2, R2).
 *
 * What: nonzero while the Menu shows LOAD_PAGE or SAVE_PAGE (top level and
 * nested Instrument share them). Why: entering Load/Save drops a running
 * AutoSave drain so the Menu's name and list reads get the facade at once;
 * the schedulers already refuse to *start* drains on these pages, so a drop
 * cannot be followed by a restart until the user leaves. Inputs:
 * menu_activePage. Accessors: the scalar and Pattern drain abort checks.
 */
static uint8_t filesystem_loadSavePageOpen(void)
{
    return (uint8_t)(menu_activePage == LOAD_PAGE ||
                     menu_activePage == SAVE_PAGE);
}

/*
 * Report whether the scalar drain may be cut for a Load/Save drop (S090 P2).
 *
 * What: every Bank-load abort phase (filesystem_autosaveDrainAbortSafe()),
 * plus the start of the post-drain HCNAMES convergence (decision Q1):
 * - 70: before the temp register is opened (nothing outstanding);
 * - 77: header write, temp handle open, no callback outstanding;
 * - 72: row streaming, only while rows remain (op_item_offset below the row
 *   count): the temp then holds fewer than the full 161 rows.
 * The error close-down (filesystem_autosaveWriterFinishError()) closes the
 * temp handle and restores the captured dirty bits.
 * Why these and no others: the live /.hcnames is untouched until phase 75,
 * and the boot prelude in filesystem_ensureAutosaveFiles_tick() discards a
 * .hcnamtmp that lacks the #types header plus 161 rows. A cut at 74 (temp
 * complete and synced, live not yet removed) is excluded: that temp would be
 * promoted by the next boot's prelude, carrying R suppression for a commit
 * whose marker byte may not yet be durable (S090 P2 schedule finding). 73,
 * 75 and 76 wait for close/remove/rename callbacks and are never cut.
 * Cost of a convergence cut: the mirror stays INVALID (set at convergence
 * start) until the next .hcnames read; refreshed bits are kept for the retry;
 * the restored captured bits make the post-exit drain rewrite those values
 * (redundant if the committed marker landed, required if it did not).
 * Inputs: phase, op_item_offset. Accessor: filesystem_autosaveParameterDrain_tick().
 */
static uint8_t filesystem_autosaveDrainLoadSaveCutSafe(uint8_t phase)
{
    if (filesystem_autosaveDrainAbortSafe(phase))
        return 1u;
    if (phase == 70u || phase == 77u)
        return 1u;
    return (uint8_t)(phase == 72u &&
                     op_item_offset < FS_RESIDENT_NAMES_ROW_COUNT);
}
```

#### F-3 — MODIFY lines 8798–8831

R2/Q1. Scalar drain: abort on Bank-load yield (unchanged) or on a Load/Save page (new), with the comment rewritten.

First original line (8798): `/*`  
Last original line (8831): `}`

Replacement:

```c
    /*
     * Abort the scalar drain for a Bank load (S082 P1 R6, D1; assessment A1)
     * or for a Load/Save page (S090 P2, R2).
     *
     * What: while fs_autosave_yield is set, a phase with no AsyncFATFS
     * callback outstanding (filesystem_autosaveDrainAbortSafe()) routes
     * through filesystem_autosaveWriterFinishError(): open source/target
     * handles are closed, the transaction's captured dirty bits are restored,
     * and the operation ends ERROR. While a Load/Save page is open the same
     * happens at the same phases plus the start of the HCNAMES convergence
     * (filesystem_autosaveDrainLoadSaveCutSafe(), decision Q1). A phase that
     * is waiting for an open, close, remove or seek runs on until that wait
     * completes; the abort then fires at the next allowed phase, a few ms
     * later. The error close-down (40-46) is never aborted; the HCNAMES
     * publication is never cut once its temp is complete (73-76). The
     * writer's completion re-arms its retry with full validation (the winner
     * cache is cleared on ERROR).
     * Why (Bank load): the Bank is about to be replaced; the record being
     * written is stale, and the load must not wait for it (D1). Recovery
     * writes may leave both files invalid; that is accepted, because the old
     * Bank's records are ignored after the load anyway (identity mismatch,
     * D21).
     * Why (Load/Save): the user's name and list reads must get the facade at
     * once (S090 P2 R2: "dropped as quickly as possible"). Nothing valid is
     * lost: copy phases write only the inactive A/B target, and recovery runs
     * only when no record is valid. The restored dirty bits keep the work
     * owed; the scheduler (page-suppressed while on the page) restarts it
     * AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS after exit, from phase 0, and
     * only if recovery or dirt is still pending after what the user did.
     * Aborting inside a wait phase would either finish with a callback still
     * due (it would then write op_file/op_file_ready/op_remove_done inside the
     * operation that follows) or queue a second fclose() on a handle that is
     * already closing (phase 40/42 would retry it forever).
     * Inputs: fs_autosave_yield, menu_activePage, op_phase, op_item_offset.
     * Outputs: close-down phases 40-43 or ERROR; the facade becomes idle.
     * Accessors: preset_tickBackgroundBankLoad() phase 2 (sets the yield and
     * waits for the facade); Menu page changes (menu_activePage).
     * Affiliates: filesystem_autosaveDrainAbortSafe(),
     * filesystem_autosaveDrainLoadSaveCutSafe(), filesystem_loadSavePageOpen(),
     * filesystem_autosaveWriterFinishError(),
     * filesystem_autosaveWriterCompleted(), autosave_maskRestoreCaptured().
     */
    if ((fs_autosave_yield && filesystem_autosaveDrainAbortSafe(op_phase)) ||
        (filesystem_loadSavePageOpen() &&
         filesystem_autosaveDrainLoadSaveCutSafe(op_phase))) {
        filesystem_autosaveWriterFinishError();
        return;
    }
```

#### F-4 — MODIFY lines 16346–16370

R2. Pattern drain: the same abort also fires on a Load/Save page.

First original line (16346): `/*`  
Last original line (16370): `if (fs_autosave_yield) {`

Replacement:

```c
    /*
     * Abort the Pattern drain for a Bank load (S082 P1 R6, D1) or for a
     * Load/Save page (S090 P2, R2).
     *
     * What: while fs_autosave_yield is set or a Load/Save page is open,
     * phases 0-1 (no handle open) finish ERROR at once; phases 3-6 and 8
     * (file open, streaming) jump to the close at phase 9 with ERROR status.
     * Phase 2 (open pending) waits for its callback and is aborted from phase
     * 3 on the next tick. Phase 7 polls afatfs_fseek(), which can stay
     * IN_PROGRESS across ticks, so it is left to finish and the abort fires at
     * phase 8 (assessment A2): a close must never be queued behind a seek
     * still walking the cluster chain. Phases 9-10 (closing) and the follow-on
     * HCNAMES Pattern update are not cut. The ERROR completion keeps the
     * Scene's dirty bit (semantic or non-semantic), and the snapshot is
     * released (filesystem_patternSnapshotInUse() covers phases 2-6 only).
     * Why: Bank load: the record being written is stale once the Bank changes
     * (D1). Load/Save: the user's reads must get the facade at once (S090 P2);
     * the re-marked bit restarts the drain after the user leaves the page.
     * Either way the ping-pong partner keeps the last valid file.
     * Inputs: fs_autosave_yield, menu_activePage, op_phase, op_file.
     * Outputs: ERROR completion after the handle closes.
     * Accessors: preset_tickBackgroundBankLoad() phase 2; Menu page changes.
     * Affiliates: filesystem_autosavePatternDrainSchedule_tick(),
     * filesystem_autosaveNonSemanticPatternDrainSchedule_tick() (same op),
     * filesystem_patternSnapshotInUse(), filesystem_loadSavePageOpen().
     */
    if (fs_autosave_yield || filesystem_loadSavePageOpen()) {
```

#### F-5 — ADD before line 26883

Q2b. Non-semantic Pattern scheduler respects the hold.

Anchor (line 26883, unchanged): `if (menu_isLoadSaveCommandActive())`

New block:

```c
    /* S090 P2 (Q2b): start-up work owed after a failed boot settle waits for
     * the user's next change (autosave_bootHoldActive()). */
    if (autosave_bootHoldActive())
        return;
```

#### F-6 — ADD before line 27109

Q2b. Scalar writer scheduler respects the hold (after the boot-ready gate).

Anchor (line 27109, unchanged): `/* Do not queue an LFN operation until the existing idle poll has mounted. */`

New block:

```c
    /*
     * S090 P2 (Q2b): after a boot settle that ended with work owed, nothing
     * is written until the user changes something (R1). The first accepted
     * mutation clears the hold in Autosave.c; the owed recovery/dirt is then
     * written together with it by the normal debounce below.
     */
    if (autosave_bootHoldActive())
        return;
```

#### F-7 — ADD before line 27242

Q2b. Semantic Pattern scheduler respects the hold (after the yield gate).

Anchor (line 27242, unchanged): `/* Reset the epoch even while policy/card gates suppress the scheduler. */`

New block:

```c
    /* S090 P2 (Q2b): no autonomous Pattern drain after a failed boot settle
     * until the user's next change (autosave_bootHoldActive()). */
    if (autosave_bootHoldActive())
        return;
```

#### F-8 — MODIFY lines 27752–27757

A2. Budget is always available before audio starts.

First original line (27752): `uint8_t filesystem_backgroundBudgetAvailable(void)`  
Last original line (27757): `}`

Original:

```c
uint8_t filesystem_backgroundBudgetAvailable(void)
{
    if (filesystem_backgroundBudgetRate() == 0u)
        return 1u;
    return (uint8_t)(budget_state.credit_us > 0);
}
```

Replacement:

```c
uint8_t filesystem_backgroundBudgetAvailable(void)
{
    /*
     * S090 P2 (R1 "using the full CPU"): before audioCodec_init() there is no
     * render to protect, so the pre-audio settle stage runs the scalar drain,
     * Pattern drains and repair epoch unthrottled. audioCodec_isInitialized()
     * never returns to 0, so this bypass cannot apply at runtime, including
     * during audio-suspended storage commands.
     */
    if (!audioCodec_isInitialized())
        return 1u;
    if (filesystem_backgroundBudgetRate() == 0u)
        return 1u;
    return (uint8_t)(budget_state.credit_us > 0);
}
```

#### F-9 — MODIFY lines 27774–27775

A2. Pre-audio slices are not charged (no runtime deficit).

First original line (27774): `if (filesystem_backgroundBudgetRate() != 0u)`  
Last original line (27775): `budget_state.credit_us -= (int32_t)elapsed;`

Original:

```c
    if (filesystem_backgroundBudgetRate() != 0u)
        budget_state.credit_us -= (int32_t)elapsed;
```

Replacement:

```c
    /*
     * S090 P2: pre-audio work is not charged. Credit is signed, so charging
     * the whole settle stage would leave a deficit that suppressed every
     * budgeted runtime job (drains, repair) for a long time after audio
     * starts. The DEV accounting below still records it for the H reports.
     */
    if (filesystem_backgroundBudgetRate() != 0u && audioCodec_isInitialized())
        budget_state.credit_us -= (int32_t)elapsed;
```

#### F-10 — ADD before line 29055

A1/A3/A5/A11/A12. The settle stage: pending classes, make-due, progress signature, end classification and the blocking wrapper, after `filesystem_ensureAutosaveFilesBlocking()`.

Anchor (line 29055, unchanged): `(blank)`

New block:

```c

/*
 * Classes of start-up work still pending (S090 P2).
 *
 * What: one AUTOSAVE_TRACE_SETTLE_CLASS_* bit per kind of work that the
 * runtime would otherwise start by itself after boot:
 * - SETUP: a runtime AutoSave ensure queued by
 *   filesystem_enableRuntimeSettingsWrites() (boot ensure did not run);
 * - SCALAR: the boot recovery drain (fs_autosave_recovery_pending) or dirt;
 * - PATTERN / NONSEMANTIC: Pattern AutoSave drains;
 * - REPAIR: the Pattern repair epoch is not finished (its relocations mark
 *   NONSEMANTIC work);
 * - SETTINGS: a settings.cfg write pending from the boot Bank load (Q3).
 * The AutoSave classes use the same policy gates as the schedulers (enabled,
 * writer authorized, Bank resident), so OFF or no Bank reports nothing.
 * Inputs: filesystem/Autosave/Pattern service state. Output: class mask.
 * Accessor: filesystem_settleAutosaveBlocking().
 */
static uint8_t filesystem_settlePendingClasses(void)
{
    uint8_t mask = 0u;

    if (fs_autosave_setup_pending)
        mask |= AUTOSAVE_TRACE_SETTLE_CLASS_SETUP;
    if (fs_autosave_enabled && fs_autosave_writer_boot_ready &&
        bank_hasResidentBank()) {
        if (fs_autosave_recovery_pending || autosave_maskHasDirty())
            mask |= AUTOSAVE_TRACE_SETTLE_CLASS_SCALAR;
        if (autosave_patternDirtyMask() != 0u)
            mask |= AUTOSAVE_TRACE_SETTLE_CLASS_PATTERN;
        if (autosave_nonSemanticPatternDirtyMask() != 0u)
            mask |= AUTOSAVE_TRACE_SETTLE_CLASS_NONSEMANTIC;
    }
    if (!patSvc_repairEpochIdle())
        mask |= AUTOSAVE_TRACE_SETTLE_CLASS_REPAIR;
    if (fs_settings_runtime_ready && fs_settings_dirty)
        mask |= AUTOSAVE_TRACE_SETTLE_CLASS_SETTINGS;
    return mask;
}

/*
 * Make every pending start-up job due now (S090 P2, A3).
 *
 * What: sets the existing debounce state so the next filesystem_tick() may
 * start the job at once:
 * - scalar writer: armed, due now (its normal 5 s first-dirty debounce);
 * - semantic Pattern drain: first-dirty time pushed back past
 *   AUTOSAVE_PATTERN_MAX_LATENCY_MS, which bypasses the quiet window exactly
 *   as the scheduler's own latency cap does;
 * - non-semantic Pattern drain: armed, due now;
 * - settings.cfg: due now.
 * Why: before audio there is no user activity for a debounce to wait out,
 * and R1 wants the work done quickly before boot completes. The schedulers
 * themselves are unchanged; only their timing inputs are set.
 * The Pattern back-dating: x = now - latency - 1, then | 1 keeps it nonzero
 * (0 means "unset"); (x | 1) <= x + 1 = now - latency, so the scheduler's
 * elapsed = now - first >= latency.
 * Accessor: filesystem_settleAutosaveBlocking() (each pass until the stage
 * stops admitting work).
 */
static void filesystem_settleMakeDue(void)
{
    uint16_t now = time_sysTick;
    uint32_t now_us = timebase_tim2Now();

    if (fs_autosave_recovery_pending || autosave_maskHasDirty()) {
        fs_autosave_writer_armed = 1u;
        fs_autosave_next_due_tick = now;
    }
    if (autosave_patternDirtyMask() != 0u) {
        uint32_t latency_us = (uint32_t)AUTOSAVE_PATTERN_MAX_LATENCY_MS * 1000u;

        fs_pattern_first_dirty_us = (uint32_t)(now_us - latency_us - 1u) | 1u;
    }
    if (autosave_nonSemanticPatternDirtyMask() != 0u) {
        fs_nonsemantic_pattern_armed = 1u;
        fs_nonsemantic_pattern_next_due_tick = now;
    }
    if (fs_settings_dirty)
        fs_settings_next_due_tick = now;
}

/*
 * One number that changes whenever start-up work makes progress (S090 P2).
 *
 * What: mixes the pending classes, facade status, operation, phase and the
 * streaming cursors every involved state machine advances (op_bytes_done,
 * op_item_offset, op_stream_index, the scalar writer's stream_offset) with a
 * multiply-add (x * 31 + field). A change in any field changes the result
 * except in a negligible collision case, whose only effect is one missed
 * progress observation.
 * Why: a long healthy streaming phase (validation read, record copy, Pattern
 * stream, HCNAMES rows) keeps one phase for a long time; phase alone would
 * look like a stall (S090 P2 Risk 12).
 * Inputs: pending (from filesystem_settlePendingClasses()). Accessor:
 * filesystem_settleAutosaveBlocking().
 */
static uint32_t filesystem_settleProgressSignature(uint8_t pending)
{
    uint32_t signature = (uint32_t)pending;

    signature = (signature * 31u) + (uint32_t)status;
    signature = (signature * 31u) + (uint32_t)current_op;
    signature = (signature * 31u) + (uint32_t)op_phase;
    signature = (signature * 31u) + op_bytes_done;
    signature = (signature * 31u) + (uint32_t)op_item_offset;
    signature = (signature * 31u) + op_stream_index;
    signature = (signature * 31u) + op_autosave_writer.stream_offset;
    return signature;
}

/*
 * Classify one finished start-up operation (S090 P2, one attempt per class).
 *
 * What: returns the class bit when the operation that just left BUSY ended
 * in error, 0 when it succeeded. Completion callbacks acknowledge the facade
 * themselves, so the terminal status is gone by the time the stage sees the
 * facade idle; each test reads the state the callback leaves behind instead:
 * - scalar drain: ERROR re-arms the writer due AUTOSAVE_WRITER_INTERVAL_MS
 *   ahead; DONE either disarms it or re-arms the 250 ms continuation. A due
 *   time more than BOOT_SETTLE_ERROR_DUE_MS ahead therefore means ERROR;
 * - runtime setup: filesystem_autosaveSetupCompleted() sets
 *   fs_autosave_setup_failed on failure;
 * - Pattern drain: its completion re-marks the drained Scene's bit
 *   (semantic or non-semantic) on failure. The stage records which class
 *   the drain consumed when it started; no other source marks Pattern work
 *   before audio once the repair epoch has run (a repair relocation that
 *   coincides with a non-semantic drain of the same Scene would be reported
 *   as a failure: the safe direction, since it only stops the stage early);
 * - settings.cfg: filesystem_settingsWriterCompleted() sets fs_settings_dirty
 *   again on failure.
 * Other operations (trace flushes, MIDI Learn) are not start-up AutoSave work
 * and return 0.
 * Inputs: the operation and Pattern class/Scene captured at start.
 * Accessor: filesystem_settleAutosaveBlocking().
 */
static uint8_t filesystem_settleClassifyEnd(fs_internal_op_t op,
                                            uint8_t pattern_class,
                                            uint8_t pattern_scene)
{
    uint16_t scene_bit = (uint16_t)(1u << (pattern_scene & 0x0Fu));

    switch (op) {
    case FS_INTERNAL_OP_AUTOSAVE_PARAMETER_DRAIN:
        /* Unsigned 16-bit distance from now to the re-armed due tick. */
        if (fs_autosave_writer_armed &&
            (uint16_t)(fs_autosave_next_due_tick - time_sysTick) >
                (uint16_t)BOOT_SETTLE_ERROR_DUE_MS)
            return AUTOSAVE_TRACE_SETTLE_CLASS_SCALAR;
        return 0u;
    case FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES:
        return fs_autosave_setup_failed ? AUTOSAVE_TRACE_SETTLE_CLASS_SETUP : 0u;
    case FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN:
        if (pattern_class == AUTOSAVE_TRACE_SETTLE_CLASS_PATTERN &&
            (autosave_patternDirtyMask() & scene_bit) != 0u)
            return AUTOSAVE_TRACE_SETTLE_CLASS_PATTERN;
        if (pattern_class == AUTOSAVE_TRACE_SETTLE_CLASS_NONSEMANTIC &&
            (autosave_nonSemanticPatternDirtyMask() & scene_bit) != 0u)
            return AUTOSAVE_TRACE_SETTLE_CLASS_NONSEMANTIC;
        return 0u;
    case FS_INTERNAL_OP_SAVE_GLOBALS:
        return fs_settings_dirty ? AUTOSAVE_TRACE_SETTLE_CLASS_SETTINGS : 0u;
    default:
        return 0u;
    }
}

uint8_t filesystem_settleAutosaveBlocking(void)
{
    /* All stage state is local (stack): no RAM beyond the 1 B boot hold. */
    const uint32_t no_progress_us = (uint32_t)BOOT_SETTLE_NO_PROGRESS_MS * 1000u;
    const uint32_t cap_us = (uint32_t)BOOT_SETTLE_CAP_MS * 1000u;
    uint32_t start_us = timebase_tim2Now();
    uint32_t progress_us = start_us;   /* last time the signature changed */
    uint32_t cap_hit_us = 0u;          /* when the overall cap fired */
    uint32_t elapsed_ms;
    uint32_t signature;
    uint8_t pending = filesystem_settlePendingClasses();
    uint8_t end_flags = 0u;            /* AUTOSAVE_TRACE_SETTLE_FLAG_* */
    uint8_t prior_yield = fs_autosave_yield;
    uint8_t was_busy = (uint8_t)(status == FS_STATUS_BUSY);
    fs_internal_op_t started_op = FS_INTERNAL_OP_NONE;
    uint8_t started_class = 0u;
    uint8_t started_scene = 0u;

    /*
     * Pre-audio AutoSave settle stage (S090 P2, R1/R4/R5; decisions D1-D5).
     *
     * What: drives the existing schedulers and operations, synchronously and
     * unthrottled, until no start-up AutoSave work is pending and the facade
     * is idle. The loop body is one patSvc_tick() (repair epoch) and one
     * filesystem tick per pass; filesystem_settleMakeDue() removes the
     * debounces; filesystem_backgroundBudgetAvailable() reports full budget
     * because audio has not started.
     * Durability: the stage only ends with an idle facade (or, after its
     * bounds, by leaving one operation running into runtime). Each completed
     * operation has passed its own final afatfs_sync(), so an immediate reboot
     * restores from complete records (R4); the operations' existing safe-write
     * protocols cover a power loss at any point (R5).
     * One attempt (Q2b): the first operation that ends in error stops
     * admission. fs_autosave_yield is raised so the scalar and semantic
     * Pattern schedulers start nothing more; the stage stops making work due,
     * so the non-semantic and settings debounces (5 s / 1 s) cannot elapse in
     * the few ms before the facade is idle and the loop ends.
     * Bounds (D2): see config.h BOOT_SETTLE_*. After a stall or the cap the
     * ticks become counted, so the existing poll-count stall detectors may
     * fire and abort a stuck operation through their designed close-downs;
     * the drains also abort at their safe phases because the yield is set.
     * Exit: the yield is restored, runtime debounces restart from rest, and
     * if AutoSave work is still owed the boot-settle hold is set so nothing
     * runs after boot until the user acts.
     * Output: classes still pending (0 normally). DEV: 's' records.
     */
    if (pending == 0u && !was_busy)
        return 0u;
    autosaveTrace_record(AUTOSAVE_TRACE_STAGE_BOOT_SETTLE,
                         AUTOSAVE_TRACE_SETTLE_START, (uint32_t)pending);
    signature = filesystem_settleProgressSignature(pending);

    for (;;) {
        uint32_t now_us;
        uint16_t pattern_before;
        uint8_t busy;

        /* Done: nothing pending (or admission stopped) and the facade idle. */
        pending = filesystem_settlePendingClasses();
        if (status != FS_STATUS_BUSY &&
            (pending == 0u || end_flags != 0u))
            break;

        /* While admitting, every pending job is due now (A3). */
        if (end_flags == 0u)
            filesystem_settleMakeDue();

        /*
         * One pass. The semantic mask is sampled first so a Pattern drain that
         * starts in this pass can be attributed: its scheduler clears the
         * consumed Scene bit at start (semantic or non-semantic).
         */
        pattern_before = autosave_patternDirtyMask();
        patSvc_tick();
        if ((end_flags & (AUTOSAVE_TRACE_SETTLE_FLAG_STALL |
                          AUTOSAVE_TRACE_SETTLE_FLAG_CAP)) != 0u)
            filesystem_tick();          /* counted: stall detectors may fire */
        else
            filesystem_tickUncounted(); /* fast pre-audio polling is not a stall */
        busy = (uint8_t)(status == FS_STATUS_BUSY);

        /* An operation started in this pass: remember what it serves. */
        if (!was_busy && busy) {
            started_op = current_op;
            started_class = 0u;
            if (started_op == FS_INTERNAL_OP_AUTOSAVE_PATTERN_DRAIN) {
                started_scene = fs_pattern_drain_scene;
                started_class =
                    ((pattern_before & (uint16_t)~autosave_patternDirtyMask()) != 0u)
                        ? AUTOSAVE_TRACE_SETTLE_CLASS_PATTERN
                        : AUTOSAVE_TRACE_SETTLE_CLASS_NONSEMANTIC;
            }
        }

        /* An operation finished in this pass: one attempt per class. */
        if (was_busy && !busy) {
            uint8_t failed = filesystem_settleClassifyEnd(started_op,
                                                          started_class,
                                                          started_scene);
            if (failed != 0u &&
                (end_flags & AUTOSAVE_TRACE_SETTLE_FLAG_FAILED) == 0u) {
                elapsed_ms = timebase_tim2Delta(timebase_tim2Now(),
                                                start_us) / 1000u;
                autosaveTrace_record(
                    AUTOSAVE_TRACE_STAGE_BOOT_SETTLE,
                    AUTOSAVE_TRACE_SETTLE_FAIL,
                    (uint32_t)failed |
                    ((uint32_t)((uint8_t)started_op) << 8u) |
                    ((elapsed_ms > 0xFFFFu ? 0xFFFFu : elapsed_ms) << 16u));
                end_flags |= AUTOSAVE_TRACE_SETTLE_FLAG_FAILED;
                filesystem_setAutosaveYield(1u);
            }
            started_op = FS_INTERNAL_OP_NONE;
        }
        was_busy = busy;

        /* Progress and bounds (D2), on the 32-bit TIM2 microsecond clock. */
        now_us = timebase_tim2Now();
        {
            uint32_t now_signature = filesystem_settleProgressSignature(
                filesystem_settlePendingClasses());

            if (now_signature != signature) {
                signature = now_signature;
                progress_us = now_us;
            }
        }
        if (timebase_tim2Delta(now_us, progress_us) >= no_progress_us) {
            if ((end_flags & AUTOSAVE_TRACE_SETTLE_FLAG_STALL) == 0u) {
                /* First stall: stop admission, unwind with detectors armed. */
                end_flags |= AUTOSAVE_TRACE_SETTLE_FLAG_STALL;
                filesystem_setAutosaveYield(1u);
                progress_us = now_us;
            } else {
                /* Nothing moved while unwinding either: leave it running. */
                end_flags |= AUTOSAVE_TRACE_SETTLE_FLAG_BUSY;
                break;
            }
        }
        if ((end_flags & AUTOSAVE_TRACE_SETTLE_FLAG_CAP) == 0u &&
            timebase_tim2Delta(now_us, start_us) >= cap_us) {
            end_flags |= AUTOSAVE_TRACE_SETTLE_FLAG_CAP;
            filesystem_setAutosaveYield(1u);
            cap_hit_us = now_us;
        } else if ((end_flags & AUTOSAVE_TRACE_SETTLE_FLAG_CAP) != 0u &&
                   status == FS_STATUS_BUSY &&
                   timebase_tim2Delta(now_us, cap_hit_us) >= no_progress_us) {
            /* After the cap the unwind gets one window, progress or not. */
            end_flags |= AUTOSAVE_TRACE_SETTLE_FLAG_BUSY;
            break;
        }
    }

    /*
     * Exit. Restore the Bank-load yield to its value on entry (0 before
     * audio). Clear the debounce state the stage forced so runtime timing
     * starts from rest: the next mutation gets the normal first-dirty delays.
     * A running operation (BUSY flag) keeps its own state and re-arms itself
     * on completion as usual.
     */
    filesystem_setAutosaveYield(prior_yield);
    pending = filesystem_settlePendingClasses();
    if (status != FS_STATUS_BUSY) {
        fs_autosave_writer_armed = 0u;
        fs_nonsemantic_pattern_armed = 0u;
        fs_pattern_first_dirty_us = 0u;
    }
    /* Q2b: owed AutoSave work waits for the user's next change. */
    if ((pending & AUTOSAVE_TRACE_SETTLE_CLASS_AUTOSAVE) != 0u)
        autosave_setBootHold(1u);
    elapsed_ms = timebase_tim2Delta(timebase_tim2Now(), start_us) / 1000u;
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_BOOT_SETTLE,
        (uint8_t)(AUTOSAVE_TRACE_SETTLE_END | end_flags),
        (elapsed_ms > 0xFFFFFFu ? 0xFFFFFFu : elapsed_ms) |
        ((uint32_t)pending << 24u));
    return pending;
}
```

## 8. `main.c`

A9 move and boot stage 13.

#### M-1 — ADD before line 1402

A9 + A1/A7. Move the edit-mask repair before the stage; boot stage 13 runs the settle.

Anchor (line 1402, unchanged): `(blank)`

New block:

```c

    /*
     * Repair the directional VOICE edit masks before the AutoSave settle stage
     * (S090 P2, decision D3: moved from after audioCodec_init()).
     *
     * What: boot has restored Scene types and VOICE edit masks through
     * AutoSave, bankset.bcg, or Scene/Kit fallback; any directional member
     * whose layout diverged is dropped before the first live edit can fan out
     * (S072 Step 10 F5). This is RAM-only retained-state work; it marks the
     * Bank edit-mask AutoSave field only when it changes something.
     * Why here: a mark made after the settle stage would start an AutoSave
     * write after boot with no user action (R1). Running it first lets the
     * stage write any such mark before audio. The live runtime replay
     * (preset_startDrumsetApply()) stays after audioCodec_init(): it makes no
     * retained or AutoSave writes, and hardware proved it must run live.
     * Affiliates: bank_revalidateVoiceEditMasks(),
     * filesystem_settleAutosaveBlocking(), preset_startDrumsetApply().
     */
    bank_revalidateVoiceEditMasks();

    /*
     * AutoSave settle stage (S090 P2, R1): boot stage 13.
     *
     * What: performs every AutoSave job the runtime would otherwise start by
     * itself after boot (the boot recovery drain, Pattern repair and drains,
     * runtime setup, a pending settings.cfg write) synchronously, with the full
     * CPU, before the interface unlocks and audio starts. Returns the classes
     * still pending; failures are recorded in the DEV trace only (user D4: no
     * notice, the trace is the evidence) and owed work then waits for the
     * user's next change (Q2b). The result is deliberately unused here.
     * Why: the post-boot recovery drain held the SD card for 23.7 s while
     * playing (S090 P2 §1) and blocked Load/Save name reads.
     * Affiliates: filesystem_settleAutosaveBlocking(), config.h BOOT_SETTLE_*.
     */
    boot_showFilesystemStage(13u);  /* pre-audio AutoSave settle */
    (void)filesystem_settleAutosaveBlocking();
```

#### M-2 — MODIFY lines 1431–1438

A9. Remove the old post-audio call (comment left in place).

First original line (1431): `/*`  
Last original line (1438): `bank_revalidateVoiceEditMasks();`

Original:

```c
    /*
     * Boot has restored Scene types and VOICE edit masks through AutoSave,
     * bankset.bcg, or Scene/Kit fallback. Repair any directional members whose
     * layouts diverged before the first live edit can fan out (S072 Step 10
     * F5). The deterministic RAM repair repeats next boot if tracking was not
     * yet enabled for this restore path.
     */
    bank_revalidateVoiceEditMasks();
```

Replacement:

```c
    /*
     * S090 P2 (D3): the VOICE edit-mask repair (bank_revalidateVoiceEditMasks())
     * that ran here now runs before the AutoSave settle stage, so any mark it
     * makes is written before audio starts. The live replay below stays here.
     */
```

## 9. `tools/diagnostic_tools/decode_devlogs.py`

Q6: decoder support for the `'s'` record (host tool).

#### D-1 — ADD before line 134

Q6. Decoder stage name for `'s'`.

Anchor (line 134, unchanged): `}`

New block:

```python
    # s: S090 P2 pre-audio AutoSave settle stage (START / FAIL / END).
    "s": "AUTOSAVE_TRACE_STAGE_BOOT_SETTLE",
```

#### D-2 — ADD before line 175

Q6. Decoder producer name for `'s'`.

Anchor (line 175, unchanged): `}`

New block:

```python
    "s": "filesystem_settleAutosaveBlocking() (S090 P2)",
```

#### D-3 — ADD before line 1024

Q6. Decoder branch for `'s'` (START / FAIL / END).

Anchor (line 1024, unchanged): `else:`

New block:

```python
    elif ch == "s":
        # S090 P2 settle stage; layout in AutosaveTrace.h ('s').
        classes = [(0x01, "scalar"), (0x02, "setup"), (0x04, "pattern"),
                   (0x08, "nonsemantic"), (0x10, "repair"),
                   (0x20, "settings")]

        def class_names(mask: int) -> str:
            names = [name for bit, name in classes if mask & bit]
            return ",".join(names) if names else "none"

        event = flags & 0x0F
        if event == 0x01:
            detail = (f"{enum_name}: START pending="
                      f"{class_names(value & 0xFF)}")
        elif event == 0x03:
            op = (value >> 8) & 0xFF
            op_name = (FS_INTERNAL_OPS[op] if op < len(FS_INTERNAL_OPS)
                       else f"op {op}")
            detail = (f"{enum_name}: FAIL class={class_names(value & 0xFF)} "
                      f"op={op_name} at {(value >> 16) & 0xFFFF} ms")
        elif event == 0x02:
            end_bits = [(0x10, "FAILED"), (0x20, "STALL"), (0x40, "CAP"),
                        (0x80, "BUSY")]
            outcome = [name for bit, name in end_bits if flags & bit]
            detail = (f"{enum_name}: END after {value & 0xFFFFFF} ms, "
                      f"still pending={class_names((value >> 24) & 0xFF)}"
                      f"{' (boot hold set)' if (value >> 24) & 0x0F else ''}, "
                      f"outcome={','.join(outcome) if outcome else 'ok'}")
        else:
            detail = f"{enum_name}: unknown event 0x{flags:02x}"
```

## 10. Verification

### 10.1 Static gates (after applying)

1. `make all && make img` succeeds.
2. **No new warnings** (same set as the baseline, including the seven
   pre-existing `-Wunused-function` in `filesystem.c`).
3. **`link_budget.py`:** payload 685,388 B (+1,320 B), ITCM 15,240 B, DTCM
   statics 4,372 B, FX margin 3,808 B.
4. **`arm-none-eabi-nm -S`:** `autosave_boot_hold` 1 B;
   `filesystem_settleAutosaveBlocking` present.
   `patSvc_repairEpochIdle()` and `audioCodec_isInitialized()` are inlined by
   LTO, so they have no symbol of their own.
5. **Logging off:** builds with no additional warning; text 668,488, bss
   396,176.
6. **`python3 -m py_compile tools/diagnostic_tools/decode_devlogs.py`**
   passes.

### 10.2 Hardware (logging on; read the trace after a couple of boots, D4)

| # | Test | Pass (trace) |
|---|---|---|
| P1 | Boot, wait 60 s on any page, PLAY running | `s` START…END before audio; no `S`/`A`/Pattern-drain record after it until a user edit |
| P2 | Boot → PLAY → Load/Save at once | names after one index read; no drain in the way |
| P3 | Boot with a clean card | `s` END elapsed ms recorded (boot cost) |
| P4 | Edit, wait for `A`, open Load/Save | `T` ERROR shortly after entry; names after the drop plus one index read |
| P5 | Exit after P4 without changes | the drain restarts about 250 ms after exit; `T` DONE |
| P6 | P4, then a Bank Replace on the page | no drain for the discarded mask after exit |
| P7 | P4, then Kit loads on the page | one drain after exit |
| P8 | Edit a Pattern, open Load/Save during its drain | drain dropped and re-run after exit; pair valid |
| P9 | Power off during a restarted drain | next boot restores the last valid record |
| P10 | Torn record (pull power mid-drain), boot | `s` shows the scalar class and finishes; no post-boot drain |
| P11 | AutoSave OFF; no Bank | `s` shows at most the repair class; boot otherwise unchanged |
| P12 | Listening throughout | no underrun |
| P13 | Slow/old card with dirty state | `s` END without STALL; boot time recorded |
| P14 | Boot, power off right after the boot screen clears, boot again | identical restore (R4) |
| P15 | Power off at different moments during stage 13 (dirty state), boot | every boot restores; nothing valid lost (R5) |
| P16 | Force a failure (nearly full card) | `s` FAIL + END with FAILED and still-pending classes; no AutoSave record after boot until an edit; after an edit, one drain writes owed + new work |
| P17 | Open Load/Save while the post-drain `.hcnames` convergence runs | entry waits at most for the close/sync/remove/rename tail; live `/.hcnames` intact; next entry reads the card once (mirror INVALID) |

## 11. Documents to update (at close-out)

- **`AUTOSAVE.md`:**
  - the boot settle stage (classes, bounds, one attempt, hold);
  - Load/Save as a second abort trigger (with the Q1 convergence cut set and
    the F1 exclusion);
  - no autonomous post-boot work;
  - the hold and its release points.
- **`FILESYSTEM_SPEC.md`:** Load/Save drops AutoSave on entry; restart after
  exit.
- **`MODULE_INTERCHANGE_SPEC.md`:** Boot / Init Order. The edit-mask repair
  and stage 13 now sit between `liveRec_init()` and `audioCodec_init()`.
- **`STORAGE_SRAM_MANIFEST.md`:** `autosave_boot_hold` 1 B (once approved).
- **`DEV_MODES.md`:** the `'s'` record.
- **`MEMORY.md`:**
  - Boot / Init Order;
  - AutoSave invariants (no post-boot work without a user action; Load/Save
    drops drains).
- **Session log:** `090_SESSION_HANDOFF_LOG.md` (P2 section).

## 12. Out of scope (do not touch)

- AutoSave format, record size, CRC, validation, capture rate, intervals
  (R3).
- The Pattern HCNAMES follow-on and any convergence phase from 73 onwards
  (never cut).
- `midilrn.cfg`, the deferred HCNAMES flush, dev trace flush policy.
- Runtime failure surfacing beyond the existing `T` record (D4).
- S090 P1 behaviour.

## 13. Pre-verification record (session scratchpad, 2026-10-31)

- **Tree:** a copy of the current working tree (without `.git`, the
  reference trees, `knowledge_files`, `SD_CARD*`, `build`).
- **Baseline build of the copy:** text 683,648, data 420, bss 413,020;
  payload 684,068 B; 14 distinct warnings. This matches the tree.
- **Patch:** the 30 changes of §3–§9 were applied by a script that addresses
  original line numbers bottom-up per file and asserts the boundary/anchor
  lines first. One anchor (V-2) was moved from line 266 to 265 before the
  final run, so the insertion does not split a comment. All assertions held.
- **Patched build:** `make all` exit 0; same 14 warnings; numbers as in §1
  F11.
- **Logging-off build:** exit 0, no additional warning; text 668,488, bss
  396,176.
- **Decoder:** compiles. Run on four synthetic `'s'` records (START, FAIL,
  END with FAILED and pending, END ok), it printed each correctly.
- **Not run:** hardware (§10.2). No host test exists for the boot or
  filesystem sequencing.
