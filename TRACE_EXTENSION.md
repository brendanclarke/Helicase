# TRACE_EXTENSION.md - Increase Retained Autosave Trace Records (64 -> 2048)

## 1. Purpose

This document is the exact, phased implementation plan for temporarily raising
the retained autosave lifecycle trace from 64 records to 2048 records, and for
making the flush/scheduler pipeline drain that larger ring correctly.

The goal is diagnostic: a single whole-object Load (Kit, root Scene, or the
selected children of a Bank Load) emits far more than 64 `D`/`I` records in one
synchronous call, so the interesting early records are overwritten before the
background flush can publish them. A 2048-record ring holds those bursts whole
until the filesystem can append them to `/asavetrc.bin`.

The RAM expansion is understood and approved for this diagnostic pass only.
The plan records the exact byte cost and the exact revert path back to 64.

## 2. Current geometry and coupling

Current facts, confirmed from source:

- `AutosaveTrace.h` hard-codes `AUTOSAVE_TRACE_RECORD_COUNT 64u`.
- `AutosaveTrace.h` fixes `AUTOSAVE_TRACE_RECORD_BYTES 8u`.
- `AutosaveTrace.c` allocates a static ring of
  `AUTOSAVE_TRACE_RECORD_COUNT x AUTOSAVE_TRACE_RECORD_BYTES` bytes inside
  `#if DEV_MODE_LOGGING`, plus three `uint16_t` cursors.
- `filesystem.c` owns one shared `static uint8_t staging_buf[512]` and
  serializes trace records into it. 64 x 8 bytes = 512 bytes, so today the ring
  capacity and one flush batch are intentionally identical.
- `filesystem_autosaveTraceFlush_tick()` phase 0 caps the per-operation record
  snapshot to `AUTOSAVE_TRACE_RECORD_COUNT`, then serializes that whole
  snapshot into `staging_buf`. If the ring were simply raised to 2048 without
  changing this cap, the serializer would write 16,384 bytes into a 512-byte
  buffer.
- The background scheduler admits at most one trace append per
  `AUTOSAVE_TRACE_FLUSH_INTERVAL_MS` (500 ms). With a 2048-record ring, a full
  drain would otherwise take 32 intervals (~16 seconds).
- `filesystem_autosaveTraceFlushBlocking()` (the bench power-cycle helper)
  starts exactly one flush operation. It must be changed to drain all pending
  batches.

## 3. RAM impact

Only in a `DEV_MODE_LOGGING=1` build; a logging-off build allocates no ring and
no cursors.

| Quantity | Default (64) | Temporary (2048) |
| --- | --- | --- |
| Record array | 64 x 8 = 512 B | 2048 x 8 = 16,384 B |
| Cursors (3 x uint16_t) | 6 B | 6 B |
| Total retained | 518 B | 16,390 B |
| Net increase | - | +15,872 B |

Region: the ring is an uninitialized `static`, so it lands in normal SRAM1
`.bss` (not DTCM and not a DMA section). The temporary net `.bss` increase is
exactly 15,872 bytes. `SRAM_MANIFEST.md` must be updated during close-out.

## 4. Complete change inventory

Summary of edit sites:

| ID | File | Change |
| --- | --- | --- |
| E1 | `config.h` | add `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u` and the effective `AUTOSAVE_TRACE_RECORD_COUNT` |
| E2 | `Core/Bank/Scene/AutosaveTrace.h` | make `AUTOSAVE_TRACE_RECORD_COUNT` a guarded fallback instead of a hard-coded value |
| E3 | `Core/Bank/Scene/AutosaveTrace.c` | include `config.h` before `AutosaveTrace.h`; update retained-storage comment |
| E4 | `Core/Hardware/SD/filesystem.c` | add `AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS` and a staging-buffer static assertion |
| E5 | `Core/Hardware/SD/filesystem.c` | cap each flush snapshot to the batch size, not the ring size |
| E6 | `Core/Hardware/SD/filesystem.c` | re-arm the background scheduler immediately when a full batch remains |
| E7 | `Core/Hardware/SD/filesystem.c` | make the blocking flush helper drain all pending batches |
| E8 | `config.h` | temporarily set the effective count to 2048 (the approved expansion) |

### 4.1 E1 - config.h: claim the default and effective record count

File: `config.h`
Location: immediately before `#define AUTOSAVE_TRACE_FLUSH_INTERVAL_MS 500u`
(line 255).

What it does:

Introduces the trace-ring capacity as a board configuration value.
`AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT` records the shipped/default capacity (64),
and `AUTOSAVE_TRACE_RECORD_COUNT` is the effective capacity consumed by the
trace ring.

Why it must exist:

The capacity currently lives in `AutosaveTrace.h`, where a diagnostic
experiment would have to edit a leaf header to change it. Moving the effective
value to `config.h` makes the temporary 2048 expansion a one-line, clearly
marked configuration change, and keeps a named 64 default for the revert.

Inputs:

None. These are preprocessor constants.

Outputs:

`AUTOSAVE_TRACE_RECORD_COUNT` becomes visible to any translation unit that
includes `config.h` before `AutosaveTrace.h` (which E3 makes true for the ring
owner).

Exact edit:

```c
/*
 * Retained autosave-trace ring capacity.
 *
 * DEFAULT: 64 records = 512 bytes, the same size as one filesystem flush
 * batch. TEMPORARY: 2048 records = 16,384 bytes, an explicitly approved
 * logging-only expansion that prevents whole-object Load bursts from wrapping
 * the ring before the background flush can drain it. Restore the effective
 * value to AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT when the experiment is done.
 * Ownership: AutosaveTrace.c allocates the ring only when DEV_MODE_LOGGING is 1.
 */
#define AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u
#define AUTOSAVE_TRACE_RECORD_COUNT AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT
```

### 4.2 E2 - AutosaveTrace.h: make the count a guarded fallback

File: `Core/Bank/Scene/AutosaveTrace.h`
Location: replace the hard-coded count block at lines 28-35.

What it does:

Removes `#define AUTOSAVE_TRACE_RECORD_COUNT 64u` and replaces it with a
`#ifndef` fallback that keeps the header self-contained at 64 when `config.h`
has not been included first.

Why it must exist:

`AutosaveTrace.c` (after E3) and `filesystem.c` include `config.h` before this
header, so they will see the config.h effective value. Other translation units
that include this header first still get a harmless 64 fallback; they only use
the stage enum and `autosaveTrace_record()`, never the capacity.

Inputs:

Optionally `AUTOSAVE_TRACE_RECORD_COUNT` from `config.h`.

Outputs:

A single source of truth for the ring capacity at the point the ring is
actually allocated, without forcing `config.h` into every consumer.

Exact edit:

```c
/*
 * Retained ring capacity is owned by config.h (AUTOSAVE_TRACE_RECORD_COUNT).
 * The fallback below keeps this header self-contained for translation units
 * that include it before config.h. The ring may exceed one flush batch;
 * filesystem.c serializes and drains it in AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS
 * chunks, so the capacity no longer has to equal the 512-byte staging buffer.
 */
#ifndef AUTOSAVE_TRACE_RECORD_COUNT
#define AUTOSAVE_TRACE_RECORD_COUNT  64u
#endif
#define AUTOSAVE_TRACE_FILENAME      "asavetrc.bin"
```

### 4.3 E3 - AutosaveTrace.c: include config.h first and update the storage comment

File: `Core/Bank/Scene/AutosaveTrace.c`
Location: lines 8-11 (include order) and lines 13-20 (retained-storage comment).

What it does:

Moves `#include "config.h"` ahead of `#include "AutosaveTrace.h"` so the
ring-owning translation unit observes the config.h effective count, and
rewrites the fixed "64 * 8 bytes ... 518 bytes" comment to derive from the
macro.

Why it must exist:

If `AutosaveTrace.h` is included first, its `#ifndef` fallback defines 64 and
`config.h`'s 2048 would be ignored for the actual ring allocation, silently
building a 64-record ring while the flush code believes the ring is 2048. The
reorder is mandatory, not cosmetic.

Inputs:

`config.h` (effective `AUTOSAVE_TRACE_RECORD_COUNT`).

Outputs:

The ring array uses the configured capacity in a logging build; production
builds still have no ring because the array remains inside `#if DEV_MODE_LOGGING`.

Exact edit:

```c
#include "config.h"
#include "AutosaveTrace.h"
#include "timebase.h"
```

And:

```c
/*
 * Retained diagnostic storage: AUTOSAVE_TRACE_RECORD_COUNT * 8 bytes plus
 * three uint16_t cursors. Default 64 records = 518 bytes; the approved
 * temporary 2048 records = 16,390 bytes. These objects exist only in a
 * logging build; the #else stubs below retain neither the ring nor any trace
 * cursor in production.
 */
```

### 4.4 E4 - filesystem.c: add a flush batch size and a staging-buffer assertion

File: `Core/Hardware/SD/filesystem.c`
Location: immediately after `static uint8_t staging_buf[512];` (line 406).

What it does:

Adds `AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS` as the maximum number of trace
records serialized into `staging_buf` in one append operation, plus a
compile-time assertion that `staging_buf` holds a whole number of records.

Why it must exist:

The ring capacity and the flush batch are now decoupled. The ring can be 2048,
but each append may still only copy `sizeof(staging_buf) / 8` records into the
shared buffer. The assertion catches any future change to `staging_buf` or the
record size that would break the coupling at compile time instead of at
runtime.

Inputs:

`sizeof(staging_buf)` (512) and `AUTOSAVE_TRACE_RECORD_BYTES` (8).

Outputs:

`AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS` = 64, used by E5/E6/E7.

Exact edit:

```c
static uint8_t staging_buf[512];

/*
 * Maximum trace records serialized per append operation. The ring may hold
 * far more than this, so the flush drains it in bounded 512-byte batches
 * rather than requiring the retained capacity to equal the staging buffer.
 */
#define AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS \
    ((uint16_t)(sizeof(staging_buf) / AUTOSAVE_TRACE_RECORD_BYTES))

_Static_assert((sizeof(staging_buf) % AUTOSAVE_TRACE_RECORD_BYTES) == 0u,
               "staging_buf must hold whole autosave trace records");
```

### 4.5 E5 - filesystem.c: cap each flush snapshot to the batch size

File: `Core/Hardware/SD/filesystem.c`
Location: `filesystem_autosaveTraceFlush_tick()` phase 0, lines 4085-4087.

What it does:

Replaces the phase-0 cap of `AUTOSAVE_TRACE_RECORD_COUNT` with
`AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS`, so one append never serializes more than
64 records (512 bytes) into `staging_buf`, regardless of ring capacity.

Why it must exist:

This is the buffer-overflow guard. Without it, raising the ring to 2048 would
make the serializer write 16,384 bytes into a 512-byte array. The rest of the
flush state machine is already batch-shaped: it opens once, streams
`op_write_line_len` bytes, closes, syncs, then advances the flush cursor by the
phase-0 snapshot. After this change, each DONE append removes exactly one batch
and leaves the remainder pending for the next append.

Inputs:

`autosaveTrace_pendingCount()` (up to 2048) and
`AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS` (64).

Outputs:

`op_stream_index` is clamped to 64 and `op_write_line_len` becomes at most 512.

Exact edit:

```c
        op_stream_index = autosaveTrace_pendingCount();
        if (op_stream_index > AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
            op_stream_index = AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS;
```

Also update the serializer contract comment (lines 4044-4052) from "capped by
the 64-record ring" to "capped by the flush batch":

```c
/*
 * Serialize the pending AutosaveTrace prefix into the shared 512-byte buffer.
 *
 * Inputs: a pending-count snapshot capped by AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS.
 * Output: byte_count receives its exact eight-byte-record length and staging_buf
 * contains records in oldest-first order. Why: trace owns no filesystem buffer,
 * while one 64-record batch (512 bytes) fits the existing one-operation staging
 * buffer without introducing another retained allocation. The ring may hold
 * more records; repeated flushes drain it. Affiliate:
 * filesystem_autosaveTraceFlush_tick().
 */
```

### 4.6 E6 - filesystem.c: re-arm the scheduler when a full batch remains

File: `Core/Hardware/SD/filesystem.c`
Location: `filesystem_autosaveTraceFlushCompleted()` (lines 19585-19593).

What it does:

After a successful trace append is acknowledged back to `IDLE`, the completion
callback clears the 500 ms cadence deadline if at least one full batch remains
pending.

Why it must exist:

With the 2048 ring, a full drain is 32 batches. Leaving the 500 ms interval
between every batch would take ~16 seconds and could still let a very large
burst wrap before older batches are reached. Clearing the deadline lets the
next idle tick start the next batch immediately, so the backlog drains over
successive idle passes. Settings persistence and the autosave writer still run
first on each tick, so this only consumes facade time that would otherwise be
idle. The deadline is cleared only after `FS_STATUS_DONE`, so a failing append
does not hot-loop retries.

Inputs:

Terminal `status` (`FS_STATUS_DONE` or `FS_STATUS_ERROR`) and
`autosaveTrace_pendingCount()`.

Outputs:

`fs_autosave_trace_next_due_tick` is reset to the zero sentinel when more than
one batch remains; otherwise the existing 500 ms cadence is retained.

Exact edit:

```c
static void filesystem_autosaveTraceFlushCompleted(void)
{
#if DEV_MODE_LOGGING
    /*
     * If at least one full batch remains after a successful append, clear the
     * cadence deadline so the next idle tick can append the following batch
     * immediately. This lets a 2048-record ring drain over successive idle
     * passes instead of one batch every AUTOSAVE_TRACE_FLUSH_INTERVAL_MS.
     * Settings and the autosave writer claim the facade before the trace
     * scheduler each tick, so this only consumes otherwise-idle facade time.
     * A failed append keeps its 500 ms retry cadence rather than hot-looping.
     */
    if (status == FS_STATUS_DONE &&
        autosaveTrace_pendingCount() >= AUTOSAVE_TRACE_FLUSH_BATCH_RECORDS)
        fs_autosave_trace_next_due_tick = 0u;
#endif
    filesystem_ack();
}
```

### 4.7 E7 - filesystem.c: drain all pending batches in the blocking helper

File: `Core/Hardware/SD/filesystem.c`
Location: `filesystem_autosaveTraceFlushBlocking()` (lines 20169-20192).

What it does:

Changes the bench power-cycle helper from one flush operation into a loop that
starts one batch, pumps the filesystem to terminal status, acknowledges it, and
repeats until `autosaveTrace_pendingCount()` reaches zero or an error occurs.

Why it must exist:

A single `FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH` now publishes only one 512-byte
batch. A caller that removes power after the old single-operation return would
leave up to 1984 records in RAM. The helper's contract is "all pending records
are durable", so it must loop over every batch.

Inputs:

An idle facade and the current pending count.

Outputs:

Nonzero only after every pending record has passed the close/sync gate; zero on
busy, start failure, or any batch error.

Exact edit:

```c
uint8_t filesystem_autosaveTraceFlushBlocking(void)
{
    while (autosaveTrace_pendingCount() != 0u) {
        if (status == FS_STATUS_BUSY ||
            !filesystem_start(FS_INTERNAL_OP_AUTOSAVE_TRACE_FLUSH,
                              FS_FILE_SETTINGS, 0u, NULL)) {
            return 0u;
        }
        while (status == FS_STATUS_BUSY)
            filesystem_tick();
        if (status != FS_STATUS_DONE) {
            filesystem_ack();
            return 0u;
        }
        filesystem_ack();
    }
    return 1u;
}
```

Update the `filesystem.h` contract comment for this helper (lines 258-269) to
say it flushes every pending batch rather than a single append.

### 4.8 E8 - config.h: temporary increase to 2048

File: `config.h`
Location: the effective-count line added in E1.

What it does:

Points the effective `AUTOSAVE_TRACE_RECORD_COUNT` at 2048 instead of the
default 64 for the approved diagnostic pass.

Why it must exist:

This is the actual temporary expansion the plan exists for. Keeping the
`_DEFAULT` constant at 64 means the revert is a one-line change back to the
default value.

Inputs:

None. Preprocessor constant.

Outputs:

A 2048-record ring in logging builds, with the flush/scheduler changes above
handling the larger pending set.

Exact edit:

```c
#define AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u
#define AUTOSAVE_TRACE_RECORD_COUNT 2048u /* TEMPORARY approved expansion */
```

Revert to:

```c
#define AUTOSAVE_TRACE_RECORD_COUNT AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT
```

## 5. Dependency and phase ordering

- E1 must precede E2/E3 (the config-owned effective value must exist).
- E2 must precede E3 (the guarded fallback must exist before the include
  reorder).
- E3 is mandatory for the 2048 value to reach the ring owner.
- E4 must precede E5/E6/E7 (the batch macro and assertion must exist).
- E8 is the last functional change; it activates the larger ring.
- E1-E7 are harmless at 64 records, so they can be validated together before
  E8 raises the capacity.

## 5.1 Implementation notes (2026-08-14)

- Applied E1-E8: capacity is config-owned and temporarily set to 2048, the
  header retains a 64 fallback, and the ring owner now includes `config.h`
  first. The exact diagnostic allocation is +15,872 bytes in logging-on
  SRAM1 `.bss`; no production-build ring allocation is introduced.
- Added the 64-record/512-byte batch bound and compile-time divisibility check,
  changed flush snapshots to that batch bound, re-armed completed full-batch
  drains, and made the blocking helper drain all pending batches.
- Build checks are intentionally not run because the embedded toolchain is
  unavailable in this environment. Static source checks and diff validation
  will be recorded after the edits.
- Static verification completed: the configured count is 2048, the fallback
  remains 64, the flush cap is derived as 64 records from the 512-byte staging
  buffer, and `git diff --check` passes for all tracked source changes. The
  untracked planning document has no trailing whitespace.

## 6. Implementation phases

### Phase 1 - make capacity configurable (E1-E3)

1. E1: add the default and effective count to `config.h`.
2. E2: add the guarded fallback to `AutosaveTrace.h`.
3. E3: reorder `AutosaveTrace.c` includes and update its storage comment.

Build at the default 64 to prove the refactor is neutral:

```powershell
make clean
make
```

### Phase 2 - make the flush batch-safe (E4-E7)

1. E4: add the batch macro and static assertion after `staging_buf`.
2. E5: cap phase 0 to the batch size.
3. E6: re-arm the completion callback on a full remaining batch.
4. E7: loop the blocking helper over all batches.

Build at the default 64 again:

```powershell
make clean
make
```

### Phase 3 - temporary increase to 2048 (E8)

Apply E8, then build and record sizes:

```powershell
make clean
make
arm-none-eabi-size build/lxr02.elf
```

Expected: `.bss` grows by about 15,872 bytes versus the Phase 1/2 logging-on
build.

### Phase 4 - logging-off verification

Temporarily set `DEV_MODE_LOGGING` to 0, rebuild, and confirm the ring remains
absent:

```powershell
# edit config.h: #define DEV_MODE_LOGGING 0
make clean
make
arm-none-eabi-nm build/lxr02.elf | Select-String 'autosave_trace_records'
# restore config.h: #define DEV_MODE_LOGGING 1
```

The symbol query must return nothing, and `.bss` must return to its logging-off
baseline.

## 7. Hardware verification

1. Flash the logging-on 2048 build with AutoSave enabled.
2. Perform one Scene Load while staying on `Load:[Scene   ]`.
3. After the command completes (or after leaving the page), copy the card and
   inspect `/asavetrc.bin`.
4. Confirm the file grew by multiple 512-byte batches and contains the
   `L` KIND_SCENE/KIND_KIT records plus the earlier `D`/`I` records that the
   64-record ring would have wrapped.
5. Confirm `autosaveTrace_droppedCount()` (via any existing diagnostic hook or
   a temporary bench readout) stays 0 for a single Scene Load whose burst is
   below 2048 records.
6. Confirm the background scheduler drains the backlog while settings and the
   autosave writer still reach their normal `A/V/M/C/P/T` publication with no
   `FsErr`/glitch.
7. Flash the logging-off build and confirm the firmware runs normally with no
   trace RAM and no `/asavetrc.bin` write.

## 8. Close-out and revert

1. Update `SRAM_MANIFEST.md` with the temporary logging-on ring size if a
   manifest snapshot is produced during the experiment.
2. When the diagnostic is complete, apply the E8 revert so the effective count
   returns to `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT` (64), rebuild, and re-check
   `.bss` returns to the pre-experiment logging-on baseline.
3. Keep E1-E7 in place: at 64 records they are behaviorally neutral and make
   future capacity changes a single config edit.

## 9. Risks

| # | Severity | Risk | Impact | Mitigation |
| --- | --- | --- | --- | --- |
| R1 | High | E3 include-order regression. If `config.h` is not included before `AutosaveTrace.h` in the ring owner, the ring silently stays 64 while `filesystem.c` sees 2048. | Flush code and ring disagree; a large pending count would be capped correctly by the batch size but the ring would wrap early. | Apply E3 exactly; add a `_Static_assert` in `AutosaveTrace.c` comparing `AUTOSAVE_TRACE_RECORD_COUNT` against the expected configured value if extra safety is wanted. |
| R2 | Medium | E6 continuous re-arm increases trace append frequency while a full batch remains. | Diagnostic I/O could contend with the autosave writer during a very large backlog. Settings still runs first each tick, and re-arm happens only after DONE, but writer latency may rise under sustained bursts. | If writer starvation is observed, restore the unconditional 500 ms cadence by dropping the E6 deadline reset, or add a short inter-batch delay. |
| R3 | Medium | `/asavetrc.bin` grows faster. A full 2048-record drain appends 16,384 bytes versus 512 bytes today. | Card space and copy time increase during the experiment. | Treat the larger file as temporary bench output; delete or retain the card fixture as needed. |
| R4 | Low | The 2048 value is temporary and must be reverted. | Leaving it in place permanently would consume approved-for-experiment RAM beyond the experiment. | E8 revert is a one-line change; verify `.bss` after revert. |
| R5 | Low | Logging-off build still compiles with `AUTOSAVE_TRACE_RECORD_COUNT` 2048 in `config.h`. | None functionally: the ring array is inside `#if DEV_MODE_LOGGING`, so no RAM is allocated. | Confirm with the Phase 4 symbol/size check. |
| R6 | Informational | `uint16_t` cursors remain sufficient. | None: 2048 records is far below the 65,535 wrapping bound. | No change required. |
| R7 | Informational | `filesystem_autosaveTraceFlushBlocking()` now loops and can block longer. | Only the bench helper blocks; it is not a runtime path. | Accept for test convenience. |

## 10. Verification matrix

| Check | Phase | Expected result |
| --- | --- | --- |
| Configurable default build | 1 | `make` succeeds, 64-record ring, no behavior change |
| Batch-safe flush build | 2 | `make` succeeds, `_Static_assert` passes |
| 2048 logging-on build | 3 | `.bss` +15,872 B vs Phase 1/2 |
| Logging-off build | 4 | `make` succeeds, no `autosave_trace_records` symbol |
| Scene Load burst survives | 7 | earlier `D`/`I` and terminal `L` present in `/asavetrc.bin` |
| Dropped count for one burst | 7 | 0 when burst <= 2048 |
| Writer/settings priority | 7 | `A/V/M/C/P/T` publication with no glitch |
| Revert to default | 8 | `.bss` returns to pre-experiment logging-on baseline |

## 11. Execution review (2026-08-14)

### 11.1 Status

E1-E8 are applied and match the intended behavior on source inspection:

- `config.h` now owns `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT 64u` and the
  effective `AUTOSAVE_TRACE_RECORD_COUNT 2048u` (temporary).
- `AutosaveTrace.h` has the guarded 64-record fallback.
- `AutosaveTrace.c` includes `config.h` before `AutosaveTrace.h`, so the ring
  owner actually sees 2048.
- `filesystem.c` has the batch macro, the `_Static_assert`, the batch cap in
  phase 0, the DONE-only re-arm in the completion callback, and the looped
  blocking helper.
- `filesystem.h`'s blocking-helper contract now says "every pending batch".
- `git diff --check` passes (only the repository's normal LF-to-CRLF notices).

### 11.2 How it went

The functional shape is correct. The staging-buffer overflow risk that
motivated E4/E5 is closed: one append can serialize at most 64 records into the
512-byte buffer regardless of the 2048-record ring. The re-arm path is gated on
`FS_STATUS_DONE`, so a failed append cannot hot-loop. The ring/cursor math needs
no change for 2048 because `uint16_t` cursors stay well below their 65,535
wrapping bound.

### 11.3 Further findings and risks

| # | Severity | Finding | Impact | Action |
| --- | --- | --- | --- | --- |
| F1 | High | Macro redefinition. `Autosave.c` and `menu.c` still include `AutosaveTrace.h` before `config.h`, so the header defines the 64 fallback and then `config.h` redefines `AUTOSAVE_TRACE_RECORD_COUNT` to 2048. | Two `"AUTOSAVE_TRACE_RECORD_COUNT" redefined` warnings; would fail a future `-Werror` build. Functionally harmless today because those TUs never read the capacity, but it makes the "single config-owned value" contract fragile. | Reorder those two files so `config.h` precedes `AutosaveTrace.h` (exact edits below). |
| F2 | High | Still not compile-verified. `arm-none-eabi-gcc` is not installed in this environment, so the warnings in F1 are identified statically, not observed. | A real build is required to confirm the `_Static_assert`, the redefinition warnings, and size deltas. | Run `make clean && make` on the real toolchain before hardware; also build `DEV_MODE_LOGGING=0`. |
| F3 | Medium | E6 re-arm raises trace append frequency while a full batch remains. | Diagnostic I/O can occupy more idle facade time during large backlogs; settings still runs first each tick, but the autosave writer runs after trace. | Watch for writer latency under sustained bursts; if observed, restore the unconditional 500 ms cadence or add a small inter-batch delay. |
| F4 | Medium | The 2048 expansion is temporary and must be reverted. | Leaving it in place permanently would retain experiment-only RAM. | Revert the effective count to `AUTOSAVE_TRACE_RECORD_COUNT_DEFAULT` after the experiment and re-check `.bss`. |
| F5 | Low | `/asavetrc.bin` grows up to 16,384 bytes per full drain. | Card space and copy time during the experiment. | Treat as temporary bench output. |
| F6 | Low | `filesystem_autosaveTraceFlushSchedule_tick()`'s comment still describes the 500 ms cadence without mentioning the E6 re-arm. | Minor documentation drift. | Update the scheduler comment when the re-arm behavior is confirmed. |

### 11.4 Recommended fix for F1

Apply the same include ordering used in `AutosaveTrace.c` and `filesystem.c`.

In `Core/Bank/Scene/Autosave.c` (currently `AutosaveTrace.h` then `config.h`):

```c
#include "Autosave.h"
/* Supplies DEV_MODE_LOGGING and the trace-ring capacity before AutosaveTrace.h. */
#include "config.h"
#include "AutosaveTrace.h"

#include "BankData.h"
#include "SceneData.h"
#include "InstrumentManager.h"
```

In `Core/Menu/menu.c` (currently `AutosaveTrace.h` then `config.h`):

```c
#include "SceneModTargets.h"
#include "config.h"
#include "AutosaveTrace.h"
#include "EuklidGenerator.h"
#include "SomGenerator.h"
```

After this reorder, every translation unit that includes both headers sees
`config.h` first, so `AutosaveTrace.h` skips its fallback and no redefinition
occurs. `presetManager.c` does not include `config.h` and therefore keeps the
harmless 64 fallback; no change is required there.

### 11.5 F1 fix applied (2026-08-14)

The include reorder from section 11.4 has been applied:

- `Core/Bank/Scene/Autosave.c` now includes `config.h` before `AutosaveTrace.h`.
- `Core/Menu/menu.c` now includes `config.h` before `AutosaveTrace.h`.

This removes the `AUTOSAVE_TRACE_RECORD_COUNT` redefinition sequence in those two
translation units. Every TU that includes both headers now sees `config.h`
first, so `AutosaveTrace.h` skips its 64 fallback and no macro redefinition
occurs. `git diff --check` remains clean.
