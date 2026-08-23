# Session 056 — Boot hang after the §11/§12 HCNAMES work

Card capture: `SD_CARD3/`. The firmware on the card is **byte-identical to
the current `build/LXRV2_lxr02.img`**, so this analysis applies to exactly
the tree in the working directory.

Reported symptom: frozen splash ("Sonic Potions…"), **no LEDs lit**, **no
button response**, no audio.

Findings only; no code changed by this document.

> **Note on a correction.** An earlier draft of this document argued that the
> boot ladder had completed and the runtime loop was alive. The symptom
> detail above disproves that, and the reasoning is corrected in §3. I have
> left the correction visible rather than quietly rewriting, because the
> mistake is instructive: it came from treating one trace record as proof of
> a code path when a second path could also produce it.

## 1. What I can state with confidence

**The runtime main loop was never reached. Boot is stuck in the Preset
completion pump at [main.c:866](main.c#L866).**

**What I cannot determine from this capture: *why* Preset never completed.**
The boot ladder emits no trace records, and Preset emits none either, so the
six seconds between the Bank Load finishing and the last record contain no
evidence about Preset's state. §6 sets out what to add so the next attempt
answers it. I am not going to guess at the cause.

## 2. Why the main loop was never reached

The runtime loop at [main.c:1059](main.c#L1059) is a flat, ungated sequence.
Every iteration unconditionally calls `led_processSeqLedState()`,
`timebase_serviceFrontPanel()`, `led_tickHandler()`,
`buttonHandler_processEvents()`, `buttonHandler_tick()`,
`menu_pollPresetStatus()` and `filesystem_tick()`. There is no
`menu_storageBusy` gate, no early `continue`, nothing conditional at all.

If that loop were executing, LEDs would tick and buttons would resolve.
Neither did. Therefore the loop never ran, and everything before it —
`audioCodec_init()`, `preset_startDrumsetApply()`, `menu_start()`,
`sequencerTimer_init()` — was never reached either.

That answers your open question: **audio was never initialised.** The
absence of sound is not a playback failure; `audioCodec_init()`
([main.c:1013](main.c#L1013)) is downstream of the stall.

## 3. Correction: why the AutoSave records do *not* prove the loop was alive

The trace's last five records are a complete AutoSave writer lifecycle:

```
S@2011 (armed, 5 s debounce → 7011)   A@7011   V@8043   M@8108   T@8108
```

I previously argued this required the runtime loop, because producing it
needs `filesystem_tick()` called repeatedly with the facade **idle**. The
second half is right; the first is not. `filesystem_tick()` is also called
from every blocking boot pump, and the AutoSave scheduler runs from inside
`filesystem_tick()` whenever `status == FS_STATUS_IDLE`
([filesystem.c:19787](Core/Hardware/SD/filesystem.c#L19787)).

The distinguishing detail is *which* pump. Boot has two shapes:

| Pump shape | Can it produce S→A→V→M→T? |
|---|---|
| `while (filesystem_status() == FS_STATUS_BUSY) filesystem_tick();` — used by the scan/index/mount steps | **No.** It exits the instant status leaves BUSY, and while BUSY the AutoSave scheduler cannot run at all (it requires IDLE). |
| `while (preset_getStatus() == PRESET_LOAD_IN_PROGRESS && !filesystem_bootLoggingTimedOut()) { … filesystem_tick(); }` — [main.c:866](main.c#L866) | **Yes.** Its exit condition is unrelated to facade status, so it happily spins with the facade idle — exactly the state the AutoSave scheduler needs to arm, wait out a five-second debounce, admit a drain, and terminate. |

I also wrongly asserted that `fs_autosave_writer_boot_ready` could only be
set by `filesystem_ensureAutosaveFilesBlocking()`, and used that to place
boot past main.c:932. It is not true: the idle scheduler starts
`FS_INTERNAL_OP_ENSURE_AUTOSAVE_FILES` autonomously
([filesystem.c:20417-20428](Core/Hardware/SD/filesystem.c#L20417)) and the
same `filesystem_autosaveSetupCompleted()` callback sets the flag. So the
whole S→T sequence is fully explained from inside the main.c:866 pump, with
boot never advancing past it.

## 4. Why nothing else recorded the stall

- **No `bootlog.bin`, despite `DEV_MODE_LOGGING = 1`.**
  `filesystem_bootLoggingTimedOut()` returns a latch that is only set while
  an operation is *armed* with a ten-second deadline. The Bank Load's
  `BANKLOAD` arm was consumed when it completed `DONE` at t=1840; from then
  on nothing is armed, no deadline can expire, and the latch can never be
  set. **The main.c:866 pump therefore has no working exit at all** — its
  second condition is permanently false. It is an unbounded spin, not a
  timeout.
- **`DEV_MODE_DIAGNOSTIC = 0`** ([config.h:68](config.h#L68)), so
  `boot_showFilesystemStage()` and `boot_showActiveFilesystemDiagnostic()`
  — which the stuck pump calls on every iteration — displayed nothing. With
  it enabled, that pump would have been printing the live operation and
  phase to the LCD the entire time. This is the single cheapest instrument
  that would have identified the stall immediately, and it already exists.
- **The trace flush still worked** (it runs from `filesystem_tick()` when
  idle), which is why 231 records reached the card from inside a hung boot.

## 5. What the capture *did* establish, and it is significant

The Bank Load itself succeeded and the new probe returned a result that
settles Defect B's location.

`K` at t=1840 carries **flags = 0x05** — `STATUS_DONE` **and**
`HCNAMES_VERIFIED`, slot 15 — while `SD_CARD3/.hcnames` is byte-identical to
both previous cards: row 0 is still `LoadTst<TAB>012` and the file is still
**1344 bytes**. A correct row 0 for this Load would read `LoadTst!<TAB>015`,
one byte longer, making the file 1345 bytes.

Both facts are simultaneously true in exactly one way: at the moment phase
85 streamed the register, `fs_list_cache_name[0]` already held `LoadTst` and
`fs_resident_source[0]` already held 12. The writer faithfully wrote back the
old image, and the probe faithfully confirmed the match.

| §11.9 predicted outcome | status |
|---|---|
| `H` records → intra-operation interference | **excluded** (zero `H` records) |
| VERIFIED set + register correct → defect closed | **excluded** (register unchanged) |
| VERIFIED clear → storage layer / duplicate entry | **excluded** (VERIFIED is *set*) |

The unanticipated fourth outcome — **VERIFIED set with the register
unchanged** — means the write path, the flush, the LFN open and the probe
are all proven healthy, and **Defect B is upstream of the write**: the Bank
commit's row-0 staging never reaches the arrays, without tripping either
refusal witness. The `B` record at t=1823 proves the commit block executed.
That narrows Defect B from "somewhere in the register path" to "between the
commit block and phase 85", which is what §12 was landed to find out.

## 6. Additional evidence: the stall is timing-sensitive

Enabling `DEV_MODE_DIAGNOSTIC` is **rejected as a diagnostic** and is not
part of any plan below. Its LCD writes slow the boot ladder enough that this
stall does not reproduce; boot instead proceeds further and times out at a
different point.

That is not merely an inconvenience, it is evidence, and it should be
recorded as such: **the failure is timing-sensitive.** A cooperative state
machine that stalls at one execution speed and not another is not a simple
missing call — it points at an ordering or re-entrancy race. The most
likely candidates are the chained settings save started from inside
`filesystem_complete()` in `on_bank_load_complete()`, and the callback
ordering around Bank Load's new terminal phases. Any instrument that
changes boot timing will therefore destroy the thing it is meant to measure.

**Requirement, restated:** the boot ladder must record its progress **to a
file**, at negligible time cost, in a form that survives a boot which never
ends.

---

## 7. Plan — record the boot ladder to `asavetrc.bin`

### 7.0 Why this channel, and not a new log file

There is already a proven-good file writer that worked *from inside this
exact hang*: the AutoSave trace ring and its background append to
`/asavetrc.bin`. 231 records reached the card during a boot that never
completed. Its properties are precisely what is needed:

- **Time-driven, not event-driven.** `filesystem_autosaveTraceSchedule_tick()`
  ([filesystem.c:20651-20670](Core/Hardware/SD/filesystem.c#L20651)) appends
  whenever `autosaveTrace_pendingCount() != 0`, the facade is idle, and
  `AUTOSAVE_TRACE_FLUSH_INTERVAL_MS` (500 ms,
  [config.h:339](config.h#L339)) has elapsed. It does not wait for an
  operation, a completion, or a boot stage.
- **Driven from `filesystem_tick()`** — but only from its **idle** branch
  ([filesystem.c:20924](Core/Hardware/SD/filesystem.c#L20924)). It therefore
  appends from inside a pump that waits with the facade idle, which is why
  231 records reached the card from inside the Session 056 hang, and it
  **cannot** append while a pump waits with the facade BUSY. In that case
  heartbeats buffer in RAM and reach the card only after the operation unwinds.
  See §9.5(b).
- **Cheap at the producer.** `autosaveTrace_record()` is a ring-buffer store:
  no I/O, no formatting, no LCD. It cannot perturb boot timing the way
  `DEV_MODE_DIAGNOSTIC` does.
- **Already sized.** The ring is 2048 records
  ([config.h:330](config.h#L330)); this boot used 231. The additions below
  cost ~40 more.
- **Already decodable.** `tools/decode_devlogs.py` renders the file; two new
  stages are two table entries.

So: no new file, no new writer, no new flush path, no new SRAM buffer. The
work is entirely *producing records that do not currently exist*.

One gate to be aware of: the scheduler defers while
`menu_isLoadSaveCommandActive()` and emits an `F` record once per episode
([filesystem.c:20630-20648](Core/Hardware/SD/filesystem.c#L20630)). This
capture contains **zero `F` records**, so that gate was open throughout the
hang and is not a risk for boot-ladder tracing.

### 7.1 New stage `'Z'` — boot ladder progress and wait heartbeat

*File:* `Core/Bank/Scene/AutosaveTrace.h`

The decisive missing signal is not "which stage was entered" but "which
stage was entered **and never left**". A pump that spins forever emits
nothing today. A periodic heartbeat from inside the pump converts that
silence into a stream of records that the 500 ms flush carries to the card.

```c
    /*
     * Z: pre-audio boot ladder progress and wait heartbeat.
     *
     * What: one record at entry and exit of every blocking boot pump and at
     * every numbered ladder stage, plus a periodic heartbeat emitted from
     * inside a pump while it is still waiting. Why: the boot ladder in main.c
     * produced no trace records of any kind, so a boot that stalled in a
     * pump for six seconds and then never finished was indistinguishable in
     * asavetrc.bin from an idle device -- see S056_BOOT_HANG_FOLLOWUP.md
     * sections 3 and 4. The heartbeat is what makes an unbounded spin
     * visible: a stuck pump produces one record every AUTOSAVE_TRACE_BOOT_
     * HEARTBEAT_MS, each carrying the predicate it is still waiting on.
     *
     * This stage must never be produced by LCD or display code. Boot timing
     * is part of the failure (section 6), so the producer is a ring store
     * only.
     *
     * flags: bits 0..1 select the event -- 0 ENTER, 1 EXIT, 2 HEARTBEAT,
     * 3 ABORT (the pump's own deadline expired). Bits 2..6 carry the ladder
     * site id (0..31). Bit 7 is reserved and zero.
     *
     * value32: current_op in bits 0..7, op_phase in bits 8..15, the site's
     * own wait predicate in bits 16..23 (for the Preset pump this is
     * preset_getStatus()), and filesystem_status() in bits 24..31.
     */
    AUTOSAVE_TRACE_STAGE_BOOT_LADDER = 'Z',
```

with the layout constants:

```c
#define AUTOSAVE_TRACE_BOOT_EVENT_MASK        0x03u
#define AUTOSAVE_TRACE_BOOT_EVENT_ENTER       0u
#define AUTOSAVE_TRACE_BOOT_EVENT_EXIT        1u
#define AUTOSAVE_TRACE_BOOT_EVENT_HEARTBEAT   2u
#define AUTOSAVE_TRACE_BOOT_EVENT_ABORT       3u
#define AUTOSAVE_TRACE_BOOT_SITE_SHIFT        2u
#define AUTOSAVE_TRACE_BOOT_SITE_MASK         (0x1fu << AUTOSAVE_TRACE_BOOT_SITE_SHIFT)
#define AUTOSAVE_TRACE_BOOT_OP_SHIFT          0u
#define AUTOSAVE_TRACE_BOOT_PHASE_SHIFT       8u
#define AUTOSAVE_TRACE_BOOT_PREDICATE_SHIFT   16u
#define AUTOSAVE_TRACE_BOOT_FSSTATUS_SHIFT    24u
```

Site ids should reuse the numbering `boot_showFilesystemStage()` already
uses (1..14) so the two schemes never disagree, with 15..31 reserved for
pumps that have no numbered stage.

### 7.2 New stage `'Q'` — Preset state transitions

Preset is the subsystem that stalled and it is completely untraced. Every
`pm_status` assignment should emit one record through a single helper.

```c
    /*
     * Q: Preset status transition witness.
     *
     * What: one record every time pm_status changes, carrying the new
     * status, the completed operation and result, the pending chained Bank
     * operation, and the live facade status. Why: the boot ladder waits on
     * preset_getStatus() (main.c:866) and Preset publishes no evidence at
     * all, so a boot that hung on that predicate left nothing to show
     * whether on_bank_settings_flush_complete() ever ran, whether
     * filesystem_requestSave(FS_FILE_SETTINGS, ...) was accepted, or which
     * branch of on_bank_load_complete() was taken. Emitted from one helper
     * beside pm_status so no future assignment can silently escape it.
     *
     * flags: bits 0..3 the new pm_status; bit 4 pm_completed_ok; bit 5 set
     * when the transition happened inside a filesystem completion callback
     * (i.e. re-entrantly). Bits 6..7 reserved.
     *
     * value32: pm_completed_op in bits 0..7, pm_pending_bank_op in bits
     * 8..15, pm_request_slot in bits 16..23, filesystem_status() in bits
     * 24..31.
     */
    AUTOSAVE_TRACE_STAGE_PRESET_STATE = 'Q',
```

Both letters are free: the file currently uses
`D I J N L H R K U W F G B S A V M C P T X O E` plus retired `Y`.

### 7.3 Producer — one helper, called from main.c

*File:* `main.c` (or a small `boot_trace.h` beside it)

```c
static uint16_t boot_trace_last_heartbeat_tick;

static void boot_traceLadder(uint8_t event, uint8_t site, uint8_t predicate)
{
    /*
     * Emit one boot-ladder record without touching the display or the card.
     *
     * What: packs the caller's event/site with the live filesystem operation,
     * phase, status and the caller's own wait predicate into one ring store.
     * Why: the pre-audio ladder is the only part of the firmware that can
     * spin forever, and it is the only part that records nothing. This is
     * deliberately a ring store and nothing else: DEV_MODE_DIAGNOSTIC's LCD
     * writes are excluded because they change boot timing enough that the
     * stall under investigation stops reproducing (section 6).
     *
     * Inputs: event kind, ladder site id, and the caller's predicate byte.
     * Outputs: exactly one trace record; the background 500 ms append carries
     * it to /asavetrc.bin. No file is opened here and no SRAM is allocated
     * beyond the heartbeat tick below.
     *
     * Affiliates: AUTOSAVE_TRACE_STAGE_BOOT_LADDER, filesystem_getBootDiagnostic(),
     * filesystem_autosaveTraceSchedule_tick(), and S056_BOOT_HANG_FOLLOWUP.md
     * section 7.
     */
    uint8_t op = 0u, phase = 0u;

    filesystem_getBootDiagnostic(&op, &phase);
    autosaveTrace_record(
        AUTOSAVE_TRACE_STAGE_BOOT_LADDER,
        (uint8_t)((event & AUTOSAVE_TRACE_BOOT_EVENT_MASK) |
                  ((site << AUTOSAVE_TRACE_BOOT_SITE_SHIFT) &
                   AUTOSAVE_TRACE_BOOT_SITE_MASK)),
        ((uint32_t)op << AUTOSAVE_TRACE_BOOT_OP_SHIFT) |
        ((uint32_t)phase << AUTOSAVE_TRACE_BOOT_PHASE_SHIFT) |
        ((uint32_t)predicate << AUTOSAVE_TRACE_BOOT_PREDICATE_SHIFT) |
        ((uint32_t)filesystem_status() << AUTOSAVE_TRACE_BOOT_FSSTATUS_SHIFT));
}

static void boot_traceWaitHeartbeat(uint8_t site, uint8_t predicate)
{
    /*
     * Emit at most one heartbeat per AUTOSAVE_TRACE_BOOT_HEARTBEAT_MS from
     * inside a blocking boot pump.
     *
     * Why: a pump that never exits is silent today. One record per interval
     * turns that silence into a durable, timestamped stream showing the pump
     * still spinning and what it is still waiting on. The interval must stay
     * shorter than AUTOSAVE_TRACE_FLUSH_INTERVAL_MS (500 ms) so at least one
     * heartbeat exists in every flush batch, and long enough that the ring
     * cannot be dominated by heartbeats during a normal fast boot.
     *
     * Inputs: the site id and predicate. Output: at most one record per
     * interval. State: one 16-bit tick, the only SRAM this plan adds.
     */
    uint16_t now = time_sysTick;

    if ((uint16_t)(now - boot_trace_last_heartbeat_tick) <
        AUTOSAVE_TRACE_BOOT_HEARTBEAT_MS) {
        return;
    }
    boot_trace_last_heartbeat_tick = now;
    boot_traceLadder(AUTOSAVE_TRACE_BOOT_EVENT_HEARTBEAT, site, predicate);
}
```

`AUTOSAVE_TRACE_BOOT_HEARTBEAT_MS` in `config.h`, suggested **250u**, with a
comment stating the two constraints above.

### 7.4 Call sites

| Site | main.c | Records to add |
|---|---|---|
| every `boot_showFilesystemStage(N)` | 7, 8, 9, 10, 11, 12, 14 | one `ENTER` with site = N |
| `filesystem_requestScanKits` pump | 568 | `ENTER` / heartbeat / `EXIT` |
| `filesystem_createLibraryIndexBlocking` × 3 | 599, 646, 689 | `ENTER` / `EXIT` |
| `filesystem_requestScanScenes` pump | 626 | `ENTER` / heartbeat / `EXIT` |
| `filesystem_requestScanBanks` pump | 669 | `ENTER` / heartbeat / `EXIT` |
| `filesystem_createBootIndexBlocking` | 711 | `ENTER` / `EXIT` |
| `filesystem_requestLoadBankIndex` pump | 767 | `ENTER` / heartbeat / `EXIT` |
| **`preset_getStatus()` pump** | **866** | **`ENTER` / heartbeat(predicate = `preset_getStatus()`) / `EXIT` / `ABORT`** |
| `filesystem_ensureAutosaveFilesBlocking` | 932 | `ENTER` / `EXIT` |
| `boot_filesystem_done` | 992 | one `EXIT` with site 14 |

Inside each `while` body, add exactly one `boot_traceWaitHeartbeat(site, predicate)`
call next to the existing `filesystem_tick()`. For status-based pumps the
predicate is `(uint8_t)filesystem_status()`; for main.c:866 it is
`(uint8_t)preset_getStatus()`.

### 7.5 Bound the unbounded pump, and record the abort

This is a correctness fix, not only instrumentation. As §4 establishes,
[main.c:866](main.c#L866)'s second guard —
`!filesystem_bootLoggingTimedOut()` — cannot ever become true once the Bank
Load's arm is consumed, so the pump has **no working exit**.

Give it an independent deadline measured from its own entry, using
`time_sysTick` directly rather than the boot-logger latch:

```c
    uint16_t pump_started = time_sysTick;

    boot_traceLadder(AUTOSAVE_TRACE_BOOT_EVENT_ENTER, 12u,
                     (uint8_t)preset_getStatus());
    while (preset_getStatus() == PRESET_LOAD_IN_PROGRESS) {
        /*
         * An independent deadline, because the boot logger's latch cannot
         * arm here. filesystem_bootLoggingTimedOut() only reports a timeout
         * while an operation holds an armed ten-second budget; the Bank Load
         * consumed its arm when it completed DONE, so from that point this
         * loop had no exit at all and spun until power-off. The deadline
         * below is owned by the pump itself and cannot be defeated that way.
         * On expiry the ABORT record names the site, the live operation and
         * phase, and the Preset status that never advanced -- then the
         * existing failure path takes over, so a stall degrades the boot
         * instead of bricking it. See S056_BOOT_HANG_FOLLOWUP.md section 4.
         */
        if ((uint16_t)(time_sysTick - pump_started) >= BOOT_PRESET_WAIT_MS) {
            boot_traceLadder(AUTOSAVE_TRACE_BOOT_EVENT_ABORT, 12u,
                             (uint8_t)preset_getStatus());
            goto boot_filesystem_failure;
        }
        boot_traceWaitHeartbeat(12u, (uint8_t)preset_getStatus());
        filesystem_tick();
    }
    boot_traceLadder(AUTOSAVE_TRACE_BOOT_EVENT_EXIT, 12u,
                     (uint8_t)preset_getStatus());
```

`BOOT_PRESET_WAIT_MS` in `config.h`, suggested **15000u** — comfortably
longer than the ten-second operation budget so it can never pre-empt a slow
but healthy load, and short enough to fail visibly.

The same audit applies to every other pump in the ladder: each should carry
its own entry-relative deadline and `ABORT` record. The status-based ones
are far less exposed (they exit as soon as the facade leaves BUSY) but the
uniformity is worth more than the few lines it costs.

### 7.6 Guarantee the evidence reaches the card on the way out

`boot_filesystem_failure` currently makes a bounded `bootlog.bin` attempt.
Add a synchronous trace drain before it, using the helper that already
exists:

```c
        /*
         * Drain the ring before the failure path can halt or reset. The
         * background 500 ms append normally suffices, but a failure route
         * must not depend on another scheduler pass ever happening.
         * filesystem_autosaveTraceFlushBlocking() acknowledges terminal
         * status itself, so it cannot leave the schedulers wedged.
         */
        (void)filesystem_autosaveTraceFlushBlocking();
```

`filesystem_autosaveTraceFlushBlocking()`
([filesystem.c:21387](Core/Hardware/SD/filesystem.c#L21387)) is exactly this
primitive and needs no change.

### 7.7 Decoder and documentation

- `tools/decode_devlogs.py` and `tools/devlog_unpack.py`: add `'Z'` and
  `'Q'` to `STAGE_ENUM` / `STAGE_PRODUCER` and render them. `'Z'` should
  print as e.g.
  `boot site=12 HEARTBEAT op=LDBK phase=97 preset=1 fs=BUSY`, and the
  decoder should additionally emit a summary line at end of file naming any
  site with an `ENTER` and no matching `EXIT` — that one line is the whole
  answer for a hang like this one.
- `DEV_MODES.md`: the two new stages, the stage-letter list, and an explicit
  note that `DEV_MODE_DIAGNOSTIC` is **not** a substitute because it
  perturbs boot timing (§6).

### 7.8 Cost

| Item | SRAM | Notes |
|---|---|---|
| `boot_trace_last_heartbeat_tick` | **2 bytes** | the only new persistent storage in this plan |
| `'Z'` / `'Q'` records | 0 | existing 2048-record ring, currently using 231 |
| producer helpers | 0 | stack-only |
| bounded pump deadlines | 0 | one `uint16_t` local per pump |
| decoder / docs | 0 | |

**Total new SRAM: 2 bytes.** Flash: roughly +300-500 bytes. Expected record
volume: ~20 `ENTER`/`EXIT`, ~10 `Q`, and heartbeats only where a pump
actually waits — a healthy boot adds well under 50 records to the 231
already produced. A *stuck* boot adds four per second indefinitely, which is
the point, and the ring's wrap behaviour keeps the most recent 2048.

### 7.9 What this would have told us about *this* capture

| Question, unanswerable today | Record that answers it |
|---|---|
| Did boot reach the runtime loop? | `Z` site 14 `EXIT` present or absent |
| Which pump was spinning? | repeating `Z` HEARTBEAT records, site id |
| What was it waiting on? | the predicate byte — `preset_getStatus() == PRESET_LOAD_IN_PROGRESS` |
| For how long? | heartbeat tick values, from entry to the last flush |
| Did Preset ever change state after the Bank Load? | `Q` records after t=1840, or their absence |
| Was the chained settings save accepted? | `Q` with `pm_pending_bank_op` set, and the facade status byte |
| Did `on_bank_settings_flush_complete()` run? | the `Q` it would emit |
| Was the transition re-entrant? | `Q` flags bit 5 |

Every one of those is a ring store costing microseconds, which is the
property that matters given §6.

### 7.10 Recommended sequence

1. Land §7.1-§7.4 (the two stages, the producer, the call sites) and
   §7.6 (the drain on the failure path). Reboot. This alone should name the
   stuck site and its predicate.
2. Land §7.5 (bounded pumps) in the same commit if convenient — it is a
   correctness fix independent of this investigation, and it converts any
   future stall from a brick into a logged, degraded boot.
3. Land §7.7 so the capture is readable without manual struct unpacking.
4. Only then return to Defect B with §5's narrowed target: unconditional
   bracket records around the two staging calls in the Bank commit block,
   capturing `fs_list_cache_name[0]`'s first byte and `fs_resident_source[0]`
   immediately after each.

**Do not revert §11/§12 to chase this.** §5 shows the new probe produced the
most useful result of the investigation on its first run, and nothing in the
capture implicates the register code in the hang.

## 8. Implementation notes — 2026-08-23

The §7 plan is now implemented in the working tree.

- Added trace stage `Z` for numbered pre-audio boot-ladder entry, exit,
  heartbeat, and abort events. The producer is a ring-only helper in `main.c`;
  it never calls the LCD or opens a file. Waiting pumps emit at most one
  heartbeat every 250 ms, below the existing 500 ms trace append cadence.
- Added trace stage `Q` through one `preset_setStatus()` helper. All live
  `pm_status` transitions now pass through it, including filesystem-completion
  callback transitions. The record joins Preset status/result, pending Bank
  bridge, request slot, facade status, and callback re-entrancy.
- Replaced the unbounded pre-audio `preset_getStatus()` loop with an independent
  15-second deadline. Its `Z ABORT` goes through the existing bounded boot
  failure path instead of waiting forever for the consumed filesystem timeout
  latch. The ordinary filesystem-status pumps now use the same entry-relative
  20-second envelope while preserving their existing filesystem timeout checks.
- Added explicit `Z` exits/aborts around the numbered boot stages and a
  synchronous `filesystem_autosaveTraceFlushBlocking()` attempt before boot
  failure recovery. Stage 14 is emitted only at the common pre-audio completion
  boundary, after all failure/normal routes converge.
- Extended `decode_devlogs.py` and `devlog_unpack.py` for `Z`/`Q`. The full
  decoder now reports `BOOT LADDER INCOMPLETE` with any site that entered but
  never exited or aborted; the compact decoder uses the same field tables.
  `DEV_MODES.md` documents both stages and explicitly rejects screen
  diagnostics as a timing-neutral substitute.
- The only new retained state is the logging-only
  `boot_trace_last_heartbeat_tick` (`uint16_t`, 2 bytes, `main.c`). No state is
  added when `DEV_MODE_LOGGING == 0`; the existing trace ring and append path
  are reused.

Validation completed on 2026-08-23: `make clean && make` and `make img` both
passed; the linked image reports `text=382356`, `data=400`, `bss=94744`, and
`boot_trace_last_heartbeat_tick` is exactly 2 bytes. Both decoder scripts
parsed the existing `SD_CARD2/asavetrc.bin`; Python syntax and synthetic Z/Q
trace-summary tests passed; no direct `pm_status` assignment remains outside
the initializer and `preset_setStatus()`; and `git diff --check` passed.
Hardware reproduction remains required; the instrumentation itself must not
be treated as proof that the original timing-sensitive hang is fixed.

## 9. Review of the §7 implementation — 2026-08-23

Read-only review against §7. Build artefacts regenerated; no source changed
by this review.

### 9.1 Verified correct

| §7 item | Landed | Check performed |
|---|---|---|
| `'Z'` stage + layout constants | `AutosaveTrace.h` | present with full doc block |
| `'Q'` stage + layout constants | `AutosaveTrace.h` | present |
| `boot_traceLadder()` | [main.c:250](main.c#L250) | ring store only; no LCD, no file, no retry |
| `boot_traceWaitHeartbeat()` | [main.c:267](main.c#L267) | 250 ms, below the 500 ms append cadence |
| `preset_setStatus()` | [presetManager.c:178](Core/Bank/Scene/Preset/presetManager.c#L178) | **the only `pm_status` assignment in the file** is line 199 inside this helper; the sole other occurrence is the initialiser at line 61 |
| bounded Preset pump | `boot_waitPresetPump()`, [main.c:324](main.c#L324) | entry-relative deadline, `ABORT` record, returns to the failure path |
| bounded status pumps | `boot_waitFilesystemPump()`, [main.c:289](main.c#L289) | same shape, retains the existing logger check as an `||` |
| failure-path drain | [main.c:1204](main.c#L1204) | `filesystem_autosaveTraceFlushBlocking()` before recovery |
| stage 14 `EXIT` | [main.c:1230](main.c#L1230) | emitted at the converged boundary *before* `filesystem_bootLoggingEnd()` — this is the record that proves the ladder finished |
| decoder summary | [decode_devlogs.py:832](tools/decode_devlogs.py#L832) | `BOOT LADDER INCOMPLETE` line present |

**The single most important thing to get right was got right:**
`boot_waitFilesystemPump()` and `boot_waitPresetPump()` are **outside**
`#if DEV_MODE_LOGGING`. The deadlines are a correctness fix, not a
diagnostic, and they apply in every build. Had they been inside the guard, a
production build would still contain the unbounded spin that caused this
hang.

Two supporting details confirm the cost claims:

- `autosaveTrace_record()` compiles to an empty stub when tracing is off
  ([AutosaveTrace.c:140](Core/Bank/Scene/AutosaveTrace.c#L140)), so leaving
  `boot_traceLadder()` unguarded costs nothing in a logging-off build.
- `BOOT_FILESYSTEM_PUMP_WAIT_MS` = `BOOT_FILESYSTEM_TIMEOUT_MS` = 20000
  ([config.h:119](config.h#L119)) and `BOOT_PRESET_WAIT_MS` = 15000. Both
  exceed the ten-second armed-operation budget, so neither can pre-empt a
  slow but healthy load, and both are well under the 16-bit half-range
  (32768), so the wrap-safe `(uint16_t)(now - started) >= timeout` form is
  correct.

**Build:** clean `make clean && make`, no warnings from any changed file.
`text=382356` (+616), `data=400`, `bss=94744`.

**On the SRAM figure:** `nm` reports
`20021146 00000002 b boot_trace_last_heartbeat_tick` — 2 bytes in `.bss`,
guarded by `#if DEV_MODE_LOGGING` and therefore absent from a logging-off
build. `bss` reads *unchanged* at 94744 because those 2 bytes fit inside
existing alignment padding. Both statements in §8 are accurate; they only
look contradictory. The decoder was re-run against `SD_CARD3/asavetrc.bin`
and parses it without error (and correctly prints no
`BOOT LADDER INCOMPLETE`, since that capture predates the `Z` stage).

### 9.2 One real coverage gap

**Nine blocking pumps inside `filesystem.c` remain unbounded**
([filesystem.c:21342, 21360, 21396, 21466, 21583, 21611, 21661, 21799, 21816](Core/Hardware/SD/filesystem.c#L21342)).
They are the bodies of the `…Blocking()` wrappers — including
`filesystem_createLibraryIndexBlocking()`,
`filesystem_createBootIndexBlocking()` and
`filesystem_ensureAutosaveFilesBlocking()`, all of which the boot ladder
calls directly. Each is a bare

```c
    while (status == FS_STATUS_BUSY)
        filesystem_tick();
```

with no deadline and **no `filesystem_bootLoggingTimedOut()` check either**.

`boot_waitFilesystemPump()` wraps the *request-style* pumps in main.c; it
does not and cannot wrap these, because the loop lives inside the callee.
So if a filesystem state machine stalls with the facade BUSY, control never
returns to main.c, the new bound is never reached, and the boot hangs
exactly as before.

This is the same class of hole as §4, one layer down, and it is the *other*
plausible failure mode: §4's stall was a stuck **Preset**; this would be a
stuck **filesystem phase**. Risk is lower — a status pump exits the moment
the facade leaves BUSY — but "lower" is what was said about the Preset pump
too, before it hung a boot.

**Recommendation:** give each of those nine loops the same entry-relative
deadline and `Z ABORT` record. It is the same three lines per site, needs no
new state (one `uint16_t` local each), and completes what §7.5 asked for.

### 9.3 Three notes, none blocking

- **Heartbeat state is shared across sites.** `boot_trace_last_heartbeat_tick`
  is a single static, so after site A emits a heartbeat, site B's first
  heartbeat can be suppressed for up to 250 ms after B is entered. No
  information is lost — `ENTER` is always recorded unconditionally — but a
  short-lived pump may show `ENTER`/`EXIT` with no heartbeat between them.
  Worth knowing when reading a capture; not worth a per-site array.
- **`preset_setStatus()` early-returns when the status is unchanged.**
  Behaviour is identical (assigning the same value to `pm_status` is a
  no-op), but a repeated transition *into the same state* emits no `Q`
  record. If Preset ever re-completes the same status, that will be
  invisible in the trace. Acceptable; just do not read "no `Q`" as "no
  completion attempt".
- **Site 2 uses the 20 s envelope for a Preset wait**
  ([main.c:693](main.c#L693): `boot_waitPresetPump(2u, BOOT_FILESYSTEM_PUMP_WAIT_MS)`)
  while site 12 uses `BOOT_PRESET_WAIT_MS` (15 s). Both are defensible for
  their contexts, but the asymmetry is undocumented at the call site and
  invites a future "consistency" edit in the wrong direction. One comment
  fixes it.

### 9.4 Standing caveat

§8's closing sentence is the right one and bears repeating: **this
instrumentation is not a fix.** The stall is timing-sensitive (§6), and
adding ~30 ring stores per boot changes boot timing slightly — less than the
LCD path did, but not by zero. If the next boot completes, that is not proof
the defect is gone; it needs several boots, and the `Z`/`Q` records from a
*successful* boot are themselves the baseline against which a future stalled
boot is read. Capture and keep a good boot's trace before assuming anything.

### 9.5 Correction to §9.2 — what the gap actually is

§9.2 called the nine unbounded `filesystem.c` pumps a hang mechanism. That is
wrong for the build you are running, and it missed the part that actually
matters. Both halves checked directly:

**(a) Can they hang the boot? Not in this build.**

`filesystem_tick()` calls `filesystem_bootLoggingPollDeadline()` as its very
first action ([filesystem.c:20877](Core/Hardware/SD/filesystem.c#L20877)),
and on expiry that function does:

```c
    fs_boot_logging_timed_out = 1u;
    fs_boot_logging_armed = 0u;
    status = FS_STATUS_ERROR;
```

Its own comment states the purpose plainly: *"immediate cooperative unwind…
to unwind facade-owned busy loops."* Every operation started through
`filesystem_start()` is armed, so a filesystem phase that stalls inside one
of those nine pumps has `status` forced to `FS_STATUS_ERROR` after
`BOOT_FILESYSTEM_TIMEOUT_MS` (20 s), `while (status == FS_STATUS_BUSY)`
exits, and control returns to main.c. **They are bounded — by a pre-existing
mechanism, not by anything §7 added.**

Two real limits on that, neither of which §9.2 identified:

- **It is compiled out when `DEV_MODE_LOGGING == 0`** (`#else return 0u;`).
  In a logging-off build those nine pumps have no deadline, no logger check
  and no unwind: they spin forever. So the hazard is real, but it is a
  *production-build* hazard, and it is precisely the hazard main.c's ladder
  also had. That is what makes §9.1's finding — the new `boot_wait*Pump()`
  bounds sitting **outside** the `#if` — the load-bearing part of this
  change rather than a detail.
- The poll is cooperative: *"a C/driver call that never returns cannot be
  preempted by this cooperative check."* A lockup below the facade, inside
  asyncfatfs or the SD driver, is unbounded in **every** build. Nothing in
  §7 or §9 addresses that, and nothing can except a hardware watchdog
  (`DEV_LOGGING_IWDG`).

**(b) Can they prevent a boot failure being logged? Yes — and this is the
real finding.**

`filesystem_autosaveTraceFlushSchedule_tick()` is called **only** from the
idle branch of `filesystem_tick()`
([filesystem.c:20924](Core/Hardware/SD/filesystem.c#L20924)):

```c
    if (status == FS_STATUS_IDLE)
        filesystem_autosaveTraceFlushSchedule_tick();
```

So **while any pump spins with the facade BUSY, the trace ring cannot flush
at all.** Heartbeats accumulate in RAM and reach the card only once the
facade goes idle again.

This qualifies §7.0's premise. "The ring's 500 ms append survives this class
of hang" is true for the **Preset** pump — which waits with the facade *idle*
between callbacks, which is exactly why 231 records reached the card from
inside the Session 056 hang. It is **not** true for a BUSY-stuck filesystem
pump. In that failure mode the evidence still arrives in a logging build, but
only through this chain:

> 20 s armed deadline expires → `status = FS_STATUS_ERROR` → pump exits →
> `boot_waitFilesystemPump()` records `Z ABORT` → failure path runs
> `filesystem_autosaveTraceFlushBlocking()` → records reach the card.

Up to twenty seconds of heartbeats are buffered rather than streamed, and the
whole chain depends on the force-ERROR unwind — i.e. on `DEV_MODE_LOGGING`
being 1. It works, but not for the reason §7.0 gave.

**Revised recommendation.** Bounding those nine pumps is **not** urgent for
the build under test, and §9.2 should not be read as blocking. It is worth
doing for logging-off builds, at low priority. The genuinely useful item from
this check is documentation: §7.0's claim that the append survives a hang
must be qualified as *"survives a hang that leaves the facade idle"*, because
that distinction determines whether heartbeats stream or merely buffer — and
it is the difference between reading a stalled boot live and reading it only
after a twenty-second unwind.

## 10. Implementation plan — remediate §9.2 / §9.5

Exactly the changes required to close what §9.2 and §9.5 identified, and
nothing else. Sixteen edits across four files. **No new SRAM, no new config
constant, no new trace stage, no decoder change.**

### 10.0 Scope

Two defects are in scope, both from §9.5:

- **R1** — the nine `while (status == FS_STATUS_BUSY) filesystem_tick();`
  loops inside `filesystem.c` are unbounded when `DEV_MODE_LOGGING == 0`,
  because their only current bound is
  `filesystem_bootLoggingPollDeadline()`'s force-`ERROR` unwind, which
  compiles to `return 0u;` in that build.
- **R2** — §7.0 of this document asserts the trace append "survives this
  class of hang" without qualification. It survives a hang that leaves the
  facade **idle**; it cannot append at all while the facade is BUSY.

Explicitly **not** in scope, and no change is proposed for it: a lockup
below the facade, inside asyncfatfs or the SD driver. §9.5 established that
the cooperative deadline poll cannot preempt a call that never returns, and
that only a hardware watchdog (`DEV_LOGGING_IWDG`) addresses it. That is a
separate decision, not a remediation of §9.2/§9.5.

Two facts established while scoping, which the plan depends on:

1. **No blocking wrapper is called at runtime.** Every live caller of the
   seven `…Blocking()` wrappers is inside main.c's pre-audio ladder
   (main.c:747, 808, 858, 886, 1143, 1204). Three wrappers —
   `filesystem_repairLibraryNamesBlocking()`,
   `filesystem_repairInstrumentNamesBlocking()`,
   `filesystem_writeResidentNamesBlocking()` — have **no callers at all**.
   Therefore a boot-scale deadline in these loops cannot change runtime
   behaviour, and no boot-window flag is needed to scope it.
2. **All nine loops share one post-loop shape**, so a single helper suffices
   and no per-site cleanup has to move:
   ```c
       while (status == FS_STATUS_BUSY)
           filesystem_tick();
       if (status != FS_STATUS_DONE) {
           filesystem_ack();
           /* site-specific cleanup */
           return 0u;
       }
       filesystem_ack();
   ```
   Forcing `status = FS_STATUS_ERROR` on expiry therefore routes straight
   into each site's existing failure branch. Nothing else needs editing
   inside `filesystem.c`.

---

### 10.1 Change 1 — add the bounded pump helper

*File:* `Core/Hardware/SD/filesystem.c`
*Placement:* immediately above `filesystem_createBootIndexBlocking()`
(the first user, ~line 21320), with no forward declaration required.

```c
static uint8_t filesystem_pumpBlockingOperation(void)
{
    uint16_t started = time_sysTick;

    /*
     * Drive one facade-owned blocking wrapper to a terminal state, bounded.
     *
     * What: replaces the bare `while (status == FS_STATUS_BUSY)
     * filesystem_tick();` body used by every …Blocking() wrapper in this
     * file. On expiry it forces status to FS_STATUS_ERROR and returns zero,
     * so the caller's existing `if (status != FS_STATUS_DONE)` branch runs
     * its normal acknowledge-and-clean-up path unchanged.
     *
     * Why it must exist: those loops previously had no bound of their own.
     * They were bounded only as a side effect of
     * filesystem_bootLoggingPollDeadline(), which forces the same
     * FS_STATUS_ERROR unwind -- but that function is compiled out entirely
     * when DEV_MODE_LOGGING == 0. In a logging-off build a filesystem state
     * machine that stalled with the facade BUSY would spin here forever,
     * inside the callee, where main.c's boot_waitFilesystemPump() bound can
     * never be reached because control never returns to main.c. That is the
     * production-build form of the Session 056 boot hang, one layer down.
     * See S056_BOOT_HANG_FOLLOWUP.md sections 9.2 and 9.5.
     *
     * This deliberately duplicates the logging poll's policy rather than
     * replacing it: the logger additionally latches fs_boot_logging_timed_out,
     * freezes the ASENSURE capsule, and preserves the retained operation code,
     * none of which belong in a build without logging. When both are active
     * the logger fires first (it is polled at the top of filesystem_tick())
     * and this helper never reaches its own deadline, so behaviour in the
     * current build is bit-for-bit unchanged.
     *
     * Inputs: the module-scope `status`, `time_sysTick`, and
     * BOOT_FILESYSTEM_PUMP_WAIT_MS. Output: nonzero when the operation
     * reached a terminal status on its own; zero when this deadline forced
     * it. State: one 16-bit stack local; no static storage is added.
     *
     * Like the logging poll, this is cooperative: it cannot preempt a C or
     * driver call that never returns. It bounds a stalled facade phase, not
     * a locked-up SD driver.
     *
     * Affiliates: filesystem_bootLoggingPollDeadline(), every …Blocking()
     * wrapper below, and main.c's boot_waitFilesystemPump().
     */
    while (status == FS_STATUS_BUSY) {
        if ((uint16_t)(time_sysTick - started) >=
            BOOT_FILESYSTEM_PUMP_WAIT_MS) {
            status = FS_STATUS_ERROR;
            return 0u;
        }
        filesystem_tick();
    }
    return 1u;
}
```

`BOOT_FILESYSTEM_PUMP_WAIT_MS` already exists
([config.h:135](config.h#L135)) and already equals
`BOOT_FILESYSTEM_TIMEOUT_MS` (20000). Reusing it is deliberate: it makes the
new bound identical to the deadline the logging build already enforces, so
this change adds no new policy — only build-independence.

### 10.2 Changes 2-9 — replace the eight uniform loops

*File:* `Core/Hardware/SD/filesystem.c`

Each of these is the same one-line substitution. The `(void)` cast is
correct at every site: the helper's return value carries no information the
following `if (status != FS_STATUS_DONE)` does not already test.

```c
-       while (status == FS_STATUS_BUSY)
-           filesystem_tick();
+       (void)filesystem_pumpBlockingOperation();
```

| # | Line | Enclosing wrapper | Live caller |
|---|---|---|---|
| 2 | 21342 | `filesystem_createBootIndexBlocking()` | main.c:886 |
| 3 | 21360 | `filesystem_createBootIndexBlocking()` | main.c:886 |
| 4 | 21396 | `filesystem_autosaveTraceFlushBlocking()` | main.c:1204 |
| 5 | 21466 | `filesystem_ensureAutosaveFilesBlocking()` | main.c:1143 |
| 6 | 21583 | `filesystem_repairLibraryNamesBlocking()` | **none — unreachable** |
| 7 | 21611 | `filesystem_repairInstrumentNamesBlocking()` | **none — unreachable** |
| 8 | 21799 | `filesystem_createLibraryIndexBlocking()` | main.c:747, 808, 858 |
| 9 | 21816 | `filesystem_createLibraryIndexBlocking()` | main.c:747, 808, 858 |

Changes 6 and 7 are in wrappers with no callers. They are included because
leaving two of nine identical loops unbounded is how this class of gap
reappears: the next caller added to a repair wrapper would silently inherit
the old unbounded shape. They cost one line each and change no live path.

### 10.3 Change 10 — the ninth loop, which has a body

*File:* `Core/Hardware/SD/filesystem.c`, `filesystem_writeResidentNamesBlocking()`,
line 21661

This loop cannot use the helper: its body calls `diagnostic_cb` on every
iteration. Add the same guard inline, keeping that body intact:

```c
     while (status == FS_STATUS_BUSY) {
+        /*
+         * Same bound as filesystem_pumpBlockingOperation(), inline because
+         * this wrapper's loop body owns a per-iteration diagnostic callback.
+         * Why it must exist: without it this loop is unbounded in a
+         * DEV_MODE_LOGGING == 0 build, exactly as the eight loops converted
+         * above were. Forcing FS_STATUS_ERROR routes into the existing
+         * `if (status != FS_STATUS_DONE)` branch below, which already
+         * reports through diagnostic_cb(6u, …) and acknowledges.
+         */
+        if ((uint16_t)(time_sysTick - pump_started) >=
+            BOOT_FILESYSTEM_PUMP_WAIT_MS) {
+            status = FS_STATUS_ERROR;
+            break;
+        }
         if (diagnostic_cb) {
             …
         }
         filesystem_tick();
     }
```

with `uint16_t pump_started = time_sysTick;` declared immediately before the
loop.

### 10.4 Changes 11-14 — make the new failure observable at four call sites

*File:* `main.c`

This is the half of R1 that bounding alone does not fix. Four of the six
live call sites discard the wrapper's return value and test only
`filesystem_bootLoggingTimedOut()`. That latch is **always zero when
`DEV_MODE_LOGGING == 0`**, so in exactly the build R1 is about, the new
deadline would fire, the wrapper would return 0, and boot would continue as
though the step had succeeded — trading an unbounded hang for a silent
corruption of the boot ladder's assumptions. That is not an acceptable
exchange, so each site must observe the result.

**The existing fatal-versus-continue policy at each site must be preserved.**
These sites discard the return deliberately in some cases (an empty or
unbuildable library is not necessarily a boot failure). The requirement is
only that the outcome stops being *invisible*.

| # | Line | Call | Required change |
|---|---|---|---|
| 11 | 808 | `(void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_SCENE)` | capture the result; on zero emit `boot_traceLadder(AUTOSAVE_TRACE_BOOT_EVENT_ABORT, 6u, (uint8_t)filesystem_status())` before the existing `EXIT`/continue |
| 12 | 858 | `(void)filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_BANK)` | same, site id 8 |
| 13 | 886 | `(void)filesystem_createBootIndexBlocking()` | same, site id 9 |
| 14 | 1143 | `(void)filesystem_ensureAutosaveFilesBlocking()` | same; **this site currently emits no `Z` record at all**, so it needs an `ENTER` before the call and an `EXIT`-or-`ABORT` after it, matching the shape used at 747-766 |

Each gets the same explanatory comment:

```c
        /*
         * Observe the wrapper's own bound, not just the logger latch.
         *
         * filesystem_bootLoggingTimedOut() is always zero when
         * DEV_MODE_LOGGING == 0, so it cannot report
         * filesystem_pumpBlockingOperation()'s deadline. Without this check
         * a logging-off build would continue the ladder after a step that
         * never completed. The branch below keeps this site's existing
         * fatal-versus-continue policy unchanged; it only makes the outcome
         * visible in the trace. See S056_BOOT_HANG_FOLLOWUP.md section 10.4.
         */
```

Site main.c:747 already tests the return and needs no change. Site
main.c:1204 discards it correctly — that call is the best-effort drain on
the failure path itself, and a failed drain has nowhere left to report to.

### 10.5 Change 15 — correct §7.0 of this document

*File:* `S056_BOOT_HANG_FOLLOWUP.md`, §7.0, second bullet

Replace

> **Driven from `filesystem_tick()`**, which every blocking boot pump calls
> — including the one that is stuck. That is *why* it kept working.

with a statement that carries the qualification §9.5(b) established:

> **Driven from `filesystem_tick()`** — but only from its **idle** branch
> ([filesystem.c:20924](Core/Hardware/SD/filesystem.c#L20924)). It therefore
> appends from inside a pump that waits with the facade idle, which is why
> 231 records reached the card from inside the Session 056 hang, and it
> **cannot** append while a pump waits with the facade BUSY. In that case
> heartbeats buffer in RAM and reach the card only after the operation
> unwinds. See §9.5(b).

This must exist because §7.0 is the section a future reader consults when
deciding whether the trace can be trusted during a hang, and as written it
promises more than the mechanism delivers.

### 10.6 Change 16 — record the same qualification in DEV_MODES.md

*File:* `knowledge_files/specification_reference/DEV_MODES.md`, the `Z`
stage paragraph

Append one sentence:

> `Z` heartbeats are appended only while the filesystem facade is idle. A
> pump that waits with the facade BUSY buffers its heartbeats in the RAM
> ring until the operation reaches a terminal state; the tick values remain
> correct, but the records arrive in one batch rather than streaming.

This must exist because DEV_MODES.md is the format authority a reader
consults when interpreting a capture, and a batched arrival would otherwise
look like the heartbeat interval had failed.

---

### 10.7 What deliberately does not change

- **No new SRAM.** The helper's deadline is a stack local; changes 11-14 use
  existing locals. Total delta: 0 bytes.
- **No new config constant.** `BOOT_FILESYSTEM_PUMP_WAIT_MS` already exists
  and is deliberately reused so the new bound matches the deadline a logging
  build already enforces.
- **No new trace stage and no decoder change.**
  `AUTOSAVE_TRACE_BOOT_EVENT_ABORT` already exists and is already decoded;
  changes 11-14 emit it from sites that currently emit nothing or only
  `EXIT`.
- **No `Z` record emitted from `filesystem.c`.** `boot_traceLadder()` is
  static to main.c and must stay there: the ladder's site numbering is a
  main.c concept, and every wrapper failure already surfaces at a main.c
  call site that can record it. Exporting the helper would create a second
  producer of the same stage with no owner of the site id.
- **No behaviour change in the current build.** With `DEV_MODE_LOGGING == 1`
  the logger's poll runs first, at the top of `filesystem_tick()`, and
  forces `FS_STATUS_ERROR` at the same 20 s budget. The new helper's own
  deadline is therefore never reached, and changes 11-14 fire only on a path
  that previously hung.
- **Nothing about the driver-level lockup** (§10.0).

### 10.8 Verification

1. `make clean && make` — required, `filesystem.c` and `main.c` both change
   and there is no header dependency tracking. Expect `bss` unchanged at
   94744 and `text` up by roughly 150-250 bytes.
2. `grep -n "while (status == FS_STATUS_BUSY)" Core/Hardware/SD/filesystem.c`
   — must return **exactly one** hit, the guarded loop in
   `filesystem_writeResidentNamesBlocking()` (change 10). Any other hit is a
   missed site.
3. `grep -n "(void)filesystem_.*Blocking(" main.c` — must return exactly one
   hit, main.c:1204, the deliberate best-effort drain.
4. Build once with `DEV_MODE_LOGGING 0` to confirm it compiles and that
   `filesystem_pumpBlockingOperation()` is not optimised away — this is the
   build the whole change exists for, and it is not otherwise exercised.
   Revert the flag afterwards.
5. Boot on hardware and confirm the ladder still completes normally: a
   healthy boot must show the same `Z ENTER`/`EXIT` pairs as before, with no
   `ABORT`, and no change in boot duration beyond noise. §6's timing
   sensitivity means a measurable slowdown here is itself a finding.

## 11. Implementation notes — 2026-08-23 — Section 10

Section 10 is now implemented.

- Added `filesystem_pumpBlockingOperation()` in `filesystem.c`. The eight
  uniform facade-owned blocking loops now use its entry-relative
  `BOOT_FILESYSTEM_PUMP_WAIT_MS` deadline; expiry forces the existing
  `FS_STATUS_ERROR` cleanup path. The diagnostic-aware `/.hcnames` wrapper
  keeps its callback loop inline with the same bound.
- Added the Section 10 header contract beside the blocking-wrapper API: this
  is a stack-only facade bound, independent of `DEV_MODE_LOGGING`, and cannot
  preempt a lower-level SD-driver call that never returns. No SRAM, config
  constant, trace stage, or decoder change was added.
- The Scene index, Bank index, Instrument index, and optional AutoSave ensure
  call sites now observe wrapper failure independently of
  `filesystem_bootLoggingTimedOut()`. Their existing fatal-versus-continue
  policies are unchanged; failed wrapper results produce `Z ABORT` evidence.
  The optional ensure is bracketed by the existing stage-14 `Z` boundary.
- Corrected §7.0 and `DEV_MODES.md`: trace appends run only from the
  filesystem facade's idle branch. BUSY-pump heartbeats buffer in the ring and
  arrive after terminal unwind rather than streaming during the BUSY wait.
- Static checks before the required rebuild show exactly one live bare
  `while (status == FS_STATUS_BUSY)` loop, the diagnostic-aware HCNAMES loop;
  all other facade loops route through the helper. The existing failure-path
  bootlog call remains intentionally best-effort alongside the trace drain.

Validation and the logging-off build check remain pending below; hardware
reproduction remains required.

## 12. Review of the §10 implementation — 2026-08-23

Read-only review against §10, including the two validation steps §11 left
pending. Build artefacts regenerated; no source changed by this review.
`config.h` was temporarily flipped for step 4 below and restored — verified
byte-identical afterwards.

### 12.1 All sixteen changes verified

| §10 change | Landed | Evidence |
|---|---|---|
| 1 — `filesystem_pumpBlockingOperation()` | [filesystem.c:21293](Core/Hardware/SD/filesystem.c#L21293) | present with the full contract comment |
| 2-9 — eight uniform loops | 21388, 21405, 21440, 21509, 21625, 21652, 21855, 21871 | exactly eight `(void)filesystem_pumpBlockingOperation();` call sites |
| 10 — inline guard, diagnostic loop | [filesystem.c:21703](Core/Hardware/SD/filesystem.c#L21703) | `pump_started` declared before the loop; deadline check placed *before* the `diagnostic_cb` body so an expiry cannot spend another iteration on the callback |
| 11 — Scene index | main.c:808 | `uint8_t scene_index_ok = …` |
| 12 — Bank index | main.c:871 | `uint8_t bank_index_ok = …` |
| 13 — Instrument index | main.c:908 | `uint8_t instrument_index_ok = …` |
| 14 — AutoSave ensure | main.c:1186-1197 | `uint8_t autosave_setup_ok = …`, `Z ABORT` site 14 on zero |
| 15 — §7.0 correction | this file, §7.0 second bullet | now states "only from its **idle** branch" |
| 16 — DEV_MODES.md caveat | DEV_MODES.md:343-345 | the batching sentence is present |

**Verification step 2 passes exactly.**
`grep -n "while (status == FS_STATUS_BUSY)" Core/Hardware/SD/filesystem.c`
returns **one** hit — line 21703, the guarded diagnostic loop. Every other
facade pump routes through the helper.

**Verification step 3 needs its expected result corrected, not the code.**
The grep returns **two** hits, not one: main.c:1258
(`filesystem_autosaveTraceFlushBlocking()`, the expected best-effort drain)
and main.c:1262 (`filesystem_writeBootFailureLogBlocking()`). §10.8 was
written before that second wrapper was accounted for. It is **correct** to
discard its result and correct that it is not in the §10.2 table: its entire
body is inside `#if DEV_MODE_LOGGING`
([filesystem.c:20103](Core/Hardware/SD/filesystem.c#L20103)), so it does not
exist in the build R1 targets, and its own pump
([filesystem.c:20165](Core/Hardware/SD/filesystem.c#L20165)) is already
bounded by `fs_boot_logging_recovery_failed`, which the recovery branch of
`filesystem_bootLoggingPollDeadline()` sets on its own deadline. No change
is needed; §10.8's step 3 should read "exactly two hits, both on the failure
path".

### 12.2 Both pending validations now run

**`make clean && make` (shipping configuration, `DEV_MODE_LOGGING = 1`):**
clean, no errors, no new warnings.

```
text 382484 (+128)   data 400   bss 94744 (unchanged)
```

`+128` bytes of flash sits at the low end of §10.8's predicted 150-250, and
`bss` is unchanged as required — the helper's deadline really is a stack
local.

**Step 4, the logging-off build** — the one this whole change exists for,
and the one nothing else exercises. `DEV_MODE_LOGGING` was temporarily set
to `0`, rebuilt, then restored:

```
text 373628   data 396   bss 78276
```

Two things this proves:

- **The bound survives.** `nm` reports
  `080374f0 T filesystem_pumpBlockingOperation.isra.0` — the helper is
  emitted, not optimised away, in exactly the build where it is the *only*
  thing standing between a stalled facade phase and an unbounded spin.
- **The logging-only state really is logging-only.** `bss` drops by 16,468
  bytes (the 2048-record ring at 16,384 plus the boot-trace tick and the
  logger's own state), confirming `boot_trace_last_heartbeat_tick` is absent
  from that build as §8 claimed.

`config.h` was then restored and confirmed byte-identical to its pre-check
state, and the shipping configuration rebuilt: `text=382484`, `bss=94744`,
`build/LXRV2_lxr02.img` regenerated at 382884 bytes.

### 12.3 One implementation choice better than the plan

§10.3 showed the inline guard without specifying its position relative to
the `diagnostic_cb` body. The implementation places the deadline check
**first**, so an expiry returns immediately rather than spending one more
iteration inside a display callback. Given §6 — that LCD work is what makes
this stall stop reproducing — that ordering is the right one and worth
keeping if this block is ever edited again.

### 12.4 Two loops observed but out of scope

Neither is a §9.2/§9.5 defect; both are recorded so a future audit does not
have to rediscover them.

- **[filesystem.c:20165](Core/Hardware/SD/filesystem.c#L20165)** —
  `while (status == FS_STATUS_BUSY && !fs_boot_logging_recovery_failed)`,
  inside `filesystem_writeBootFailureLogBlocking()`. Bounded by the recovery
  latch, and the whole function is `#if DEV_MODE_LOGGING`. Correct as-is.
- **[filesystem.c:17613](Core/Hardware/SD/filesystem.c#L17613)** —
  `while (!op_rename_done) { filesystem_blockPoll(); if (!filesystem_blockFsOk()) return 0u; }`
  in the blocking rename helper. Its exit depends on
  `filesystem_blockFsOk()` eventually reporting a fault; it carries no
  deadline of its own. This is a pre-existing blocking primitive outside the
  boot ladder and outside §10's scope, but it is the same *shape* as the
  loops just fixed, and it is the only remaining unbounded-by-inspection
  wait in `filesystem.c`. Worth a look if a future hang implicates rename.

### 12.5 Status

§10 is complete and correct. Both defects it targeted are closed: R1's nine
loops are bounded in every build, with the four call sites that would
otherwise have swallowed the new failure now recording it; R2's two
documentation sites carry the idle-branch qualification.

The standing caveat from §9.4 is unchanged and still governs: **none of this
is a fix for the original hang.** It bounds the failure, makes it visible,
and adds ~128 bytes of flash. Whether the Session 056 stall still occurs is
a hardware question, and §9.4's advice holds — capture and keep a
*successful* boot's `Z`/`Q` trace as the baseline before drawing any
conclusion from a boot that completes.

## 13. SD_CARD4 — the instrumentation captured nothing, and that is the finding

Card firmware is byte-identical to the current `build/LXRV2_lxr02.img`, so
this is the §10 tree. Symptom unchanged: boot hang and timeout.

### 13.1 The measurement

**Every file on `SD_CARD4/` is byte-identical to `SD_CARD3/`:**

```
asavetrc.bin   IDENTICAL to SD_CARD3
.hcnames       IDENTICAL to SD_CARD3
.hcprms1       IDENTICAL to SD_CARD3
.hcprms2       IDENTICAL to SD_CARD3
settings.cfg   IDENTICAL to SD_CARD3
bootlog.bin    absent
```

`asavetrc.bin` did not grow by a single 8-byte record. Not one `Z`, not one
`Q`, nothing. **No trace flush completed at any point in this boot**, and the
failure path never wrote either.

### 13.2 What the absence proves

Both flush routes — the background 500 ms scheduler and the failure path's
`filesystem_autosaveTraceFlushBlocking()` — require `filesystem_start()` to
succeed, which requires the facade not BUSY, the card mounted, and afatfs
READY. None of that was ever true for long enough.

That excludes the SD_CARD3 failure outright. **That hang left the facade
idle** — which is exactly why it produced 231 records — and §12 confirmed
the `Z`/`Q` producers, the pump bounds and the drain are all present and
build correctly. This boot produced zero. The failure has **moved to a zone
where the trace channel structurally cannot work.**

It also largely excludes a stall inside any *bounded* facade pump. §10's
20 s force-`ERROR` would release it, main.c would record a `Z ABORT` and
continue, the facade would go idle, and either the background flush or the
failure-path drain would write. Nothing was written.

### 13.3 What remains — three zones, one shared property

All three share the property that `filesystem_tick()` never gets an idle
window, so the trace scheduler never runs:

1. **Before or during mount.** Nothing can be written by construction. Note
   that a *clean* mount failure does not hang — main.c falls through to the
   no-card branch and boots — so this would have to be a stall *inside*
   `filesystem_initCardAndMountBlocking()`.
2. **Inside a `filesystem_blockPoll()`-driven helper.** `filesystem_blockPoll()`
   ([filesystem.c:17497](Core/Hardware/SD/filesystem.c#L17497)) polls the
   deadline and calls `afatfs_poll()` — but it does **not** call the trace
   flush scheduler. Kit quarantine, blocking open/read, and the blocking
   rename all run here. Time spent in this zone is invisible by design.
3. **A raw blocking call that never returns**, below the facade — SD_init's
   command loop, the bit-bang SPI byte clocking, or a fixed
   `timebase_holdPreAudioMs()` hold. §10.0 excluded this class explicitly:
   no cooperative deadline can preempt a call that never returns.

### 13.4 The codebase already documents this exact signature

`config.h`'s `DEV_LOGGING_IWDG` block ([config.h:122](config.h#L122)) is
about precisely this failure class, and describes the evidence signature we
are looking at:

> a raw blocking call (SD_init()'s command loop, the bit-bang SPI byte
> clocking, or a fixed `timebase_holdPreAudioMs()` hold) that never returns
> skips every cooperative poll, so nothing after it ever runs to notice or
> log it.

and, of the regression that first shipped this feature:

> It produced **no timeout, no bootlog, and no trace**, because it ran before
> the card was mounted and the watchdog itself had not started.

That is a three-for-three match with SD_CARD4.

### 13.5 Why power-cycling destroys the only remaining evidence

This matters and is easy to miss. The retained capsule
(`fs_devwdg_capsule`, magic + last boot-log code) lives in SRAM2 and
survives a **reset**. It does not survive a **power cycle**. Every
hang-and-power-off so far has therefore discarded the one piece of state
that could name the stalled operation.

`DEV_LOGGING_IWDG = 1` closes exactly that: the independent watchdog is fed
from `filesystem_tick()` and `filesystem_blockPoll()` — both foreground-only.
If the foreground stops calling either, the IWDG resets the MCU on its own
free-running hardware timer, with no dependence on interrupts or software
state. On the next boot `RCC_CSR`'s `IWDGRSTF` identifies the reset cause,
and `filesystem_devIwdgBootCheck()` writes the retained code to `bootlog.bin`
through the existing `filesystem_writeBootFailureLogBlocking()` path. **No
new on-card format, no new writer, no new SRAM.**

Note the coverage this gives against §13.3: zone 3 is caught because the
feed stops; zones 1 and 2 are caught because they are *also* foreground —
`filesystem_blockPoll()` feeds the watchdog, so a bounded-but-silent
blockPoll loop keeps the device alive and will still fail its cooperative
deadline, while a genuinely stuck one stops feeding and resets.

### 13.6 Recommendation

Enable `DEV_LOGGING_IWDG` for one hang-hunting session — which is what its
own documentation says it is for:

> Enable it deliberately, for a hang-hunting session, not as a standing
> default.

Two conditions the config block itself sets, both of which should be
honoured rather than skipped:

- **Shorten `DEV_LOGGING_IWDG_EXPIRE`** ([config.h:194](config.h#L194),
  currently `120000u`). Two minutes is a long wait per attempt; the boot
  ladder's own envelope is 20 s.
- **Re-run its audit.** The block requires confirming that every foreground
  path which can legitimately exceed the ~32.8 s IWDG period still reaches a
  feed. Both known pumps are covered; the audit must be repeated for
  anything long-running added since — in this session that means the §10
  helper (`filesystem_pumpBlockingOperation()` calls `filesystem_tick()`, so
  it feeds ✓) and `boot_waitFilesystemPump()` / `boot_waitPresetPump()`
  (both call `filesystem_tick()` ✓).

Revert the flag afterwards. The block records that the *first* version of
this feature hung the splash itself; it states that specific hang cannot
recur, but off-by-default is the deliberate policy.

### 13.7 Correction to §10.7

§10.7 claimed "no behaviour change in the current build", reasoning that the
logger's poll always fires first at the same 20 s budget. That is not
strictly true: `filesystem_bootLoggingPollDeadline()` returns early when
`!fs_boot_logging_armed` ([filesystem.c:2836](Core/Hardware/SD/filesystem.c#L2836)),
whereas `filesystem_pumpBlockingOperation()`'s deadline is unconditional. A
facade left BUSY with no armed operation is now bounded where it previously
was not.

That is the intended direction and I do not think it is harmful — an
unarmed BUSY facade at boot is already a fault — but the claim as written
was too strong and should not be relied on when reading this change set.

### 13.8 Whether §10 caused the change in failure mode

**Unknowable from this capture, and I am not going to assert it either way.**
§6 established the stall is timing-sensitive; §10 added ~128 bytes of flash
and a comparison per pump iteration, which is enough to move a race. It is
equally possible this is the same defect presenting differently, or a second
one. The one thing the capture does establish is that whatever happened this
time did not leave the facade idle, and SD_CARD3's did.

That distinction is worth keeping: it means **§7's channel is not a general
boot-hang instrument** — it is an instrument for hangs that leave the facade
idle. §9.5(b) predicted this blind spot in the abstract; SD_CARD4 is it
occurring in practice, one boot later.

### 13.9 Do not revert

Nothing in this capture implicates §7, §10 or §11/§12's register work. §12
verified all of it builds and behaves as specified, including the
logging-off build the bounds exist for. Reverting would remove the `Z`/`Q`
evidence that will be needed the moment the failure lands back in a zone
where the facade goes idle — and would remove the pump bounds that stop that
class of hang from being unbounded at all.

## 14. SD_CARD5 — the watchdog named the stalled operation: `LIBINDEX`

`DEV_LOGGING_IWDG = 1` produced exactly what §13.6 predicted it would.

### 14.1 The measurement

```
$ xxd SD_CARD5/bootlog.bin
00000000: 4c49 4249 4e44 4558                      LIBINDEX
```

Eight bytes, the retained boot-log operation code. Everything else on the
card is still byte-identical to SD_CARD4: `asavetrc.bin` unchanged (still no
`Z`/`Q` records), `.hcnames`, `.hcprms1/2`, `settings.cfg` unchanged, and
**every `.hcindex` unchanged** — `Bank/`, `Kit/`, `Scene/`, and all four
`Instrument/*` indexes.

Firmware provenance confirmed: rebuilding the current tree produces a
`build/LXRV2_lxr02.img` byte-identical to the card's. All instrumentation is
intact — 9 `filesystem_pumpBlockingOperation()` references, 37 boot-trace
call sites, both `Z`/`Q` stages, and the IWDG capsule linked into retained
SRAM2 (`fs_devwdg_capsule` at `0x2007c000`, in `.devwdg_noinit`).

### 14.2 What `LIBINDEX` identifies

`"LIBINDEX"` is the boot-log code for `FS_INTERNAL_OP_CREATE_LIBRARY_INDEX`
([filesystem.c:2590](Core/Hardware/SD/filesystem.c#L2590)), armed by
`filesystem_start()`. So the stalled operation is the numbered-library
`.hcindex` builder, reached through
`filesystem_createLibraryIndexBlocking()`.

Two things the code being `LIBINDEX` — and not something else — rules out:

- **Name repair and Kit quarantine both completed.** That wrapper arms
  `NAMEREPR` for `filesystem_repairLibraryNamesBlocking()` and `KITQUAR ` for
  `filesystem_quarantineKitLibraryBlocking()` before it starts the index
  operation. Either would have overwritten the capsule had it been the
  stalling step. The blocking-rename loop I flagged in §12.4 lives inside
  quarantine, so **it is exonerated**.
- **The preceding library scan completed.** It arms its own code too.

### 14.3 Narrowing inside the operation

`filesystem_createLibraryIndex_tick()`
([filesystem.c:3972](Core/Hardware/SD/filesystem.c#L3972)) is an eight-phase
chain:

| phase | action |
|---|---|
| 0 | `afatfs_chdir(NULL)` + open the library directory |
| 1 | wait for that open |
| 2 | `afatfs_chdir()` into it |
| 3 | close the directory handle |
| 4 | wait close, then **open `.hcindex` with `"w"`** |
| 5 | wait index open |
| 6 | write slot-ordered rows |
| 7 | wait index close |

**Phase 4 is the first phase that touches an index file, and `"w"` truncates
on open.** Every `.hcindex` on the card is byte-identical to SD_CARD4 — not
truncated, not zero-length, not partially written. Therefore the stall is in
**phases 0-3**: the chdir-root / open-library-directory / chdir-in /
close-handle sequence, before any index file is opened.

Which library: stage 4 (Kit) is the first `filesystem_createLibraryIndexBlocking()`
call in the ladder, and nothing after it left any trace. Kit is the strong
default, though the capsule alone cannot distinguish Kit from Scene or Bank —
§14.6 closes that.

Phases 0, 2 and 3 all use the retry idiom `if (!afatfs_…()) return;`, so a
primitive that never reports success spins that phase indefinitely. That is
the shape of this stall.

### 14.4 Why the trace still captured nothing — a real defect in the evidence path

This is worth stating separately, because it explains the SD_CARD4/SD_CARD5
asymmetry and it is fixable.

Once the cooperative deadline latches, `filesystem_tick()` becomes a no-op
**permanently**:

```c
    /* filesystem_bootLoggingPollDeadline() */
    if (!fs_boot_logging_armed)
        return fs_boot_logging_timed_out;     /* stays 1 forever */

    /* filesystem_tick(), first statement */
    if (filesystem_bootLoggingPollDeadline())
        return;
```

After a timeout the latch is set and the arm is cleared, so every subsequent
`filesystem_tick()` returns at its first line. The facade can never advance
again — **including the trace drain**, whose pump therefore spins until
§10's helper aborts it at 20 s and returns 0, writing nothing.

`filesystem_writeBootFailureLogBlocking()` escapes this only because it sets
`fs_boot_logging_recovery = 1u`
([filesystem.c:20134](Core/Hardware/SD/filesystem.c#L20134)), which routes
the poll down its separate recovery branch.

So: **`bootlog.bin` can be written after a cooperative timeout; the trace
ring can never be.** §7's channel and the boot logger's timeout are mutually
exclusive by construction. That was not understood when §7.6 added the drain
to the failure path — the drain is dead code on exactly the path it was
added for.

### 14.5 Where this leaves the three earlier captures

| capture | stalled at | facade | evidence produced |
|---|---|---|---|
| SD_CARD3 (§7 build) | Preset pump, main.c:866, t≈1840+ | **idle** | 231 trace records, no bootlog |
| SD_CARD4 (§10 build) | unknown | not idle | **nothing** |
| SD_CARD5 (§10 + IWDG) | `LIBINDEX` phases 0-3, boot stage 4 | BUSY | bootlog only |

The failure has moved **earlier** each time — from a Preset wait after a
completed Bank Load, to a library-index directory open at stage 4, long
before the Bank Load runs. §6's timing sensitivity predicts exactly this
kind of migration, and it means SD_CARD3's Preset stall and this one may or
may not be the same defect. **I am not going to claim they are.**

What is now certain: the current stall is **not** in the HCNAMES register
work (§11/§12), **not** in Preset, and **not** in the Bank Load. Stage 4
precedes all of them.

### 14.6 Next step — two changes, both small

**(a) Make the trace drain survive the timeout latch.** Without this, every
cooperative timeout from here on yields a bare eight-byte code and nothing
else. The fix mirrors what `filesystem_writeBootFailureLogBlocking()`
already does: set `fs_boot_logging_recovery = 1u` around the drain in
`boot_filesystem_failure`, or give `filesystem_autosaveTraceFlushBlocking()`
the same recovery framing internally. That single change converts the next
capture from one word into the full `Z` ladder — including the site id, which
resolves Kit vs Scene vs Bank immediately, and the `op`/`phase` bytes, which
resolve *which* of phases 0-3 is spinning.

**(b) Add a `SetDetail` per phase in `filesystem_createLibraryIndex_tick()`.**
The operation currently arms one code for all eight phases, which is why the
capsule says only `LIBINDEX`. Four labels — `LIXOPEN `, `LIXWAIT `,
`LIXCHDIR`, `LIXCLOSE` — would let the *existing* IWDG capsule name the exact
stalled phase with no other change, and `filesystem_bootLoggingSetDetail()`
explicitly does not reset the enclosing deadline, so it cannot mask a
timeout.

With (a) and (b), one more boot identifies the stalled primitive exactly.

### 14.7 Housekeeping

- `DEV_LOGGING_IWDG_EXPIRE` is still `120000u`. §13.6 recommended shortening
  it; the reset fired anyway, so this is not blocking, but a 20-25 s value
  would make each attempt far quicker.
- Keep `DEV_LOGGING_IWDG = 1` until this is closed — it is the only reason
  this capture produced anything at all — then revert it per its own policy.
- The current tree builds at `text=381876`, `data=400`, `bss=94756`. `bss` is
  +12 over §12's 94744, which is the IWDG capsule and its three state bytes;
  `text` is *lower* than §12's 382484 purely from LTO inlining differences
  introduced by the new `#if DEV_LOGGING_IWDG` paths, not from any revert —
  all §7/§10 instrumentation is verified present above.

## 15. Implementation plan — make the next capture conclusive

Expansion of §14.6. Six changes across four files. **No new SRAM, no new
config constant, no new trace stage, no new on-card file format.**

### 15.0 What each half buys, and why both are needed

The capsule currently yields one word, `LIBINDEX`. That names the operation
but not the library, not the phase, and not the primitive. Two independent
evidence channels each close part of that gap, and they fail in different
circumstances, which is why both are worth having:

- **Change 1-3 (the drain)** restores the `Z`/`Q` trace on a cooperative
  timeout. It gives the **site id** (Kit vs Scene vs Bank), the full ladder
  history, and the tick timeline. It works only if the boot reaches the
  failure path at all.
- **Change 4-6 (phase details)** makes the *existing* IWDG capsule name the
  exact phase. It works even when nothing reaches the failure path — the
  case that produced SD_CARD4's zero-evidence capture — because the capsule
  is written on every `SetDetail` and survives the reset.

### 15.1 Verified facts this plan rests on

Each was checked directly against the source, not assumed:

1. `filesystem_bootLoggingSetDetail()`
   ([filesystem.c:2953](Core/Hardware/SD/filesystem.c#L2953)) writes the
   eight-byte code to **both** `fs_boot_logging_code` and, under
   `#if DEV_LOGGING_IWDG`, `fs_devwdg_capsule.code`. **Change 4-6 therefore
   needs no capsule work of its own.**
2. `SetDetail` is gated on `fs_boot_logging_armed`. `LIBINDEX` is armed by
   `filesystem_start()`, so details from inside its tick are accepted.
3. `SetDetail` explicitly does **not** reset the enclosing deadline
   (its own doc comment says so), so per-phase labelling cannot mask a
   cooperative timeout.
4. `filesystem_writeBootFailureLogBlocking()`
   ([filesystem.c:20131](Core/Hardware/SD/filesystem.c#L20131)) **refuses to
   run if `fs_boot_logging_recovery` is already set**, and resets
   `fs_boot_logging_recovery_failed = 0u` when it starts its own episode.
   Change 1 must therefore clear the flag on exit — and, because that reset
   exists, a failed drain episode cannot poison the bootlog write.
5. That same function performs `afatfs_destroy(true)` and a full remount.
   **The drain must stay ordered before it**, which it already is
   ([main.c:1258](main.c#L1258) vs 1262).
6. `op_library_index_kind` ([filesystem.c:1344](Core/Hardware/SD/filesystem.c#L1344),
   assigned at 3497) is the authoritative library selector and is already
   read by the tick at 3974 to choose the root path.

---

### 15.2 Change 1 — recovery-framed drain wrapper

*File:* `Core/Hardware/SD/filesystem.c`
*Placement:* immediately after `filesystem_autosaveTraceFlushBlocking()`
(~line 21400), so the two sit together.

```c
uint8_t filesystem_autosaveTraceFlushAfterBootFailureBlocking(void)
{
#if DEV_MODE_LOGGING
    uint8_t drained;

    /*
     * Drain the trace ring on a boot route that has already timed out.
     *
     * What: wraps filesystem_autosaveTraceFlushBlocking() in the same
     * recovery framing filesystem_writeBootFailureLogBlocking() uses, so the
     * append can still run after the cooperative boot deadline has latched.
     *
     * Why it must exist: once fs_boot_logging_timed_out is set and the
     * operation arm is cleared, filesystem_bootLoggingPollDeadline() returns
     * that latch for every subsequent call, and filesystem_tick()'s first
     * statement is `if (filesystem_bootLoggingPollDeadline()) return;`. The
     * facade can therefore never advance again, and the drain added to the
     * boot failure path spins until its own pump bound expires and writes
     * nothing. That is why SD_CARD5 produced an eight-byte bootlog.bin and
     * zero trace records: bootlog escapes only because it enters recovery
     * mode, which routes the poll down its separate branch. See
     * S056_BOOT_HANG_FOLLOWUP.md section 14.4.
     *
     * Inputs: the module-scope logging latches and the pending ring. Output:
     * nonzero when every pending batch reached its terminal boundary. The
     * recovery episode is opened and closed here so
     * filesystem_writeBootFailureLogBlocking(), which refuses to start while
     * fs_boot_logging_recovery is set, still runs afterwards; its own entry
     * additionally resets fs_boot_logging_recovery_failed, so a drain that
     * exhausts this episode cannot cost the bootlog write.
     *
     * A terminal-but-unacknowledged facade is acknowledged first: the
     * underlying helper refuses to start while status is BUSY and cannot
     * start a new operation from DONE/ERROR.
     *
     * State: none. This adds no SRAM and no new file; the ring, the append
     * operation, and `/asavetrc.bin` are all pre-existing.
     *
     * Affiliates: filesystem_autosaveTraceFlushBlocking(),
     * filesystem_bootLoggingPollDeadline(),
     * filesystem_writeBootFailureLogBlocking(), and main.c's
     * boot_filesystem_failure label.
     */
    if (!fs_boot_logging_active || fs_boot_logging_recovery)
        return 0u;
    if (status == FS_STATUS_DONE || status == FS_STATUS_ERROR)
        filesystem_ack();

    fs_boot_logging_recovery = 1u;
    fs_boot_logging_recovery_failed = 0u;
    fs_boot_logging_recovery_started_tick = time_sysTick;

    drained = filesystem_autosaveTraceFlushBlocking();

    fs_boot_logging_recovery = 0u;
    fs_boot_logging_recovery_failed = 0u;
    return drained;
#else
    return 0u;
#endif
}
```

**Why a wrapper and not an edit to `filesystem_autosaveTraceFlushBlocking()`:**
that function is documented as a bench-harness primitive with an explicit
"an idle facade" precondition and is callable outside boot. Giving it
recovery semantics unconditionally would change its contract everywhere.
The wrapper confines the new behaviour to the one route that needs it.

### 15.3 Change 2 — declaration

*File:* `Core/Hardware/SD/filesystem.h`, beside the existing
`filesystem_autosaveTraceFlushBlocking()` declaration.

```c
/*
 * Drain the trace ring from a boot route whose cooperative deadline has
 * already latched.
 *
 * Use this, not filesystem_autosaveTraceFlushBlocking(), anywhere downstream
 * of a boot timeout: after the latch, filesystem_tick() short-circuits and
 * the plain helper can never make progress. Opens and closes its own boot
 * recovery episode, so filesystem_writeBootFailureLogBlocking() may still run
 * afterwards. Returns zero outside a logging build. Affiliate:
 * S056_BOOT_HANG_FOLLOWUP.md section 14.4.
 */
uint8_t filesystem_autosaveTraceFlushAfterBootFailureBlocking(void);
```

### 15.4 Change 3 — the failure-path call site

*File:* `main.c`, [line 1258](main.c#L1258)

```c
-        (void)filesystem_autosaveTraceFlushBlocking();
+        (void)filesystem_autosaveTraceFlushAfterBootFailureBlocking();
```

Extend the existing comment above it with the reason, so the next reader does
not "simplify" it back:

```c
         * The recovery-framed variant is required, not stylistic: this label
         * is reachable only after the cooperative deadline has latched, and
         * after that latch filesystem_tick() returns immediately on every
         * call. The plain blocking drain cannot make progress here and
         * silently writes nothing — which is exactly what SD_CARD5 recorded.
```

Ordering is unchanged and must stay unchanged: the drain precedes
`filesystem_writeBootFailureLogBlocking()`, which tears down and remounts the
card (§15.1 fact 5).

### 15.5 Change 4 — per-phase detail helper

*File:* `Core/Hardware/SD/filesystem.c`
*Placement:* immediately above `filesystem_createLibraryIndex_tick()`
([line 3972](Core/Hardware/SD/filesystem.c#L3972)).

```c
static void filesystem_libraryIndexDetail(const char label[8])
{
    /*
     * Retain which library-index phase is currently executing.
     *
     * What: patches the caller's eight-byte label with the active library
     * letter and publishes it through filesystem_bootLoggingSetDetail(),
     * which writes both the retained boot-log code and, in a DEV_LOGGING_IWDG
     * build, the SRAM2 watchdog capsule.
     *
     * Why it must exist: FS_INTERNAL_OP_CREATE_LIBRARY_INDEX arms one code,
     * "LIBINDEX", for all eight of its phases. SD_CARD5's watchdog capsule
     * therefore identified the operation but not the library, not the phase,
     * and not the primitive that stalled — the whole eight-phase chain
     * collapsed to one word. Per-phase detail is the cheapest way to resolve
     * that, because SetDetail already updates the capsule and already leaves
     * the enclosing deadline untouched, so labelling cannot mask a timeout.
     *
     * Inputs: an exactly-eight-byte label whose fourth byte is a placeholder,
     * plus op_library_index_kind. Output: one retained code; no I/O, no
     * allocation, no phase change. Called on every entry to a phase including
     * retries, which is intentional: the capsule must always hold the phase
     * that was current when the foreground stopped, not the last one entered.
     *
     * Affiliates: filesystem_bootLoggingSetDetail(),
     * filesystem_createLibraryIndex_tick(), DEV_MODES.md's boot-code table,
     * and S056_BOOT_HANG_FOLLOWUP.md section 14.3.
     */
    char detail[8];

    memcpy(detail, label, sizeof(detail));
    detail[3] = (op_library_index_kind == FS_NAME_CACHE_KIT)   ? 'K'
              : (op_library_index_kind == FS_NAME_CACHE_SCENE) ? 'S'
              : (op_library_index_kind == FS_NAME_CACHE_BANK)  ? 'B'
                                                               : '?';
    filesystem_bootLoggingSetDetail(detail);
}
```

Eight bytes of stack, no static storage.

### 15.6 Change 5 — label every phase

*File:* `Core/Hardware/SD/filesystem.c`, `filesystem_createLibraryIndex_tick()`

One call as the first statement of each `case`, before any existing work:

| phase | line | label | meaning |
|---|---|---|---|
| 0 | [3983](Core/Hardware/SD/filesystem.c#L3983) | `LIX?ROOT` | chdir root, open the library directory |
| 1 | [3999](Core/Hardware/SD/filesystem.c#L3999) | `LIX?WAIT` | waiting for that open to report |
| 2 | [4027](Core/Hardware/SD/filesystem.c#L4027) | `LIX?ENTR` | chdir into the library directory |
| 3 | [4033](Core/Hardware/SD/filesystem.c#L4033) | `LIX?CLOS` | close the directory handle |
| 4 | [4039](Core/Hardware/SD/filesystem.c#L4039) | `LIX?IOPN` | wait close, open `.hcindex` `"w"` |
| 5 | [4056](Core/Hardware/SD/filesystem.c#L4056) | `LIX?IWAI` | wait index open |
| 6 | [4066](Core/Hardware/SD/filesystem.c#L4066) | `LIX?ROWS` | stream slot-ordered rows |
| 7 | [4089](Core/Hardware/SD/filesystem.c#L4089) | `LIX?DONE` | wait index close |

`?` is the placeholder patched to `K`, `S`, `B` or `?` by change 4. Every
label is exactly eight characters, as `SetDetail` requires — it copies eight
bytes and does not read a terminator.

Example, phase 0:

```c
     case 0: /* RETURN ROOT + OPEN LIBRARY DIRECTORY */
+        filesystem_libraryIndexDetail("LIX?ROOT");
         if (!afatfs_chdir(NULL)) {
```

**§14.3 established the stall is in phases 0-3**, so those four are the ones
that matter; the other four are included because a four-of-eight labelling
would leave the same ambiguity if the stall moves again, and because they
cost one line each.

### 15.7 Change 6 — publish the code table

*File:* `knowledge_files/specification_reference/DEV_MODES.md`, the
`bootlog.bin` section

An eight-byte code is useless without a table, and this is the file a reader
consults with a capture in hand. Add:

> **Library-index phase codes.** `FS_INTERNAL_OP_CREATE_LIBRARY_INDEX` arms
> `LIBINDEX` and then reports its current phase as `LIX<L><PHASE>`, where
> `<L>` is the library — `K` Kit, `S` Scene, `B` Bank, `?` unset — and
> `<PHASE>` is one of `ROOT` (chdir root and open the library directory),
> `WAIT` (waiting for that open), `ENTR` (chdir into it), `CLOS` (close the
> directory handle), `IOPN` (open `.hcindex` for write), `IWAI` (wait index
> open), `ROWS` (stream slot rows), `DONE` (wait index close). A bare
> `LIBINDEX` with no `LIX…` refinement means the operation was armed but no
> phase ran. Codes `ROOT` through `CLOS` precede any write to `.hcindex`, so
> a capture showing one of them cannot have truncated an index file.

Also add one line to the `Z` stage paragraph noting that after a cooperative
boot timeout the trace is drained through
`filesystem_autosaveTraceFlushAfterBootFailureBlocking()`, not the plain
blocking helper.

---

### 15.8 What deliberately does not change

- **No new SRAM.** Change 4 uses an eight-byte stack local; change 1 uses
  existing statics. The IWDG capsule is untouched — §15.1 fact 1 is what
  makes a capsule change unnecessary.
- **No new config constant.** The drain reuses the existing recovery
  deadline; the labels reuse the existing eight-byte code field.
- **No new trace stage, no decoder change.** `Z`/`Q` already exist and are
  already decoded; this plan only makes them reachable on the failure path.
- **No change to `filesystem_autosaveTraceFlushBlocking()`** — §15.2 explains
  why the wrapper exists instead.
- **No change to the failure-path ordering** — §15.1 fact 5.
- **No labelling of other boot operations.** `INSINDEX`, `SCANKITS`,
  `NAMEREPR`, `KITQUAR ` and the rest keep single codes. If the stall moves
  to one of them the same six-line pattern applies, but speculatively
  labelling every operation is scope creep.

### 15.9 Cost

| Item | SRAM | Flash |
|---|---|---|
| Change 1 wrapper | 0 | ~60 B |
| Changes 2, 3 | 0 | 0 |
| Change 4 helper | 0 (8 B stack) | ~60 B |
| Change 5, eight calls | 0 | ~80 B |
| Change 6 | 0 | 0 |

**Total: 0 bytes SRAM, roughly +200 bytes flash.**

Timing impact, which §6 makes a real consideration: each `SetDetail` is a
guard test plus two eight-byte `memcpy`s, executed once per tick per phase.
That is roughly two orders of magnitude below the LCD writes that were shown
to move this stall, and the drain wrapper runs only after the boot has
already failed.

### 15.10 Verification

1. `make clean && make` — required; `filesystem.h` changes. Expect `bss`
   unchanged at **94756** and `text` up by roughly 200 bytes from 381876.
2. `grep -n "filesystem_autosaveTraceFlushBlocking()" main.c` — must return
   **no** hits; the only caller left should be the new wrapper inside
   `filesystem.c`.
3. `grep -c "filesystem_libraryIndexDetail" Core/Hardware/SD/filesystem.c` —
   must return **9** (one definition, eight call sites).
4. Confirm every label is exactly eight characters. A short literal would
   make `SetDetail`'s eight-byte `memcpy` read past the string.
5. Keep `DEV_LOGGING_IWDG = 1` for this test. Shortening
   `DEV_LOGGING_IWDG_EXPIRE` to ~25000u is recommended so each attempt costs
   seconds rather than two minutes.

### 15.11 How to read the next capture

| `bootlog.bin` | `asavetrc.bin` | Reading |
|---|---|---|
| `LIXKROOT` / `LIXKWAIT` / `LIXKENTR` / `LIXKCLOS` | any | Kit library directory open/enter/close is the stalled primitive — confirms §14.3 and names the phase |
| same with `S` or `B` | any | the stall is the Scene or Bank index, not Kit; §14.3's "Kit by default" was wrong |
| `LIXKIOPN` or later | any | the stall is at or past the `.hcindex` open — expect a truncated index file, which no capture has yet shown |
| bare `LIBINDEX` | any | the operation was armed but no phase ran — the stall is in `filesystem_start()` or the dispatcher, not the state machine |
| any | **grew, contains `Z`** | change 1 worked: read the site id for the library, and the `Z` tick timeline for how long each ladder stage took |
| any | still unchanged | change 1 did not fire — the boot never reached `boot_filesystem_failure`, so this was a watchdog reset from a true lockup, not a cooperative timeout |

The last row is the important one: it is the single check that distinguishes
**"a cooperative deadline fired and boot took the failure path"** from
**"the foreground stopped and the watchdog reset the part"**. Every capture
so far has been ambiguous on exactly that point.
