# Initial Bank Load Boot Failure — Fix Plan

## Evidence

`SD_CARD/bootlog.bin` and `SD_CARD/bootlog2.bin` both contain exactly `BANKLOAD`. They are two preserved records of a ten-second boot watchdog expiry after the initial Bank payload reader took ownership.

The normal boot path, including the complete `000 Full` load, is expected to complete within ten seconds. Treat these as actual hangs. Keep the existing ten-second `BANKLOAD` deadline; do not hide the problem by extending, re-arming, or repeatedly resetting that timeout.

## Current path

Boot reloads the Bank index and calls `preset_loadBank(boot_bank_slot, 0xffffu)`. After selected-Bank child-name repair, `filesystem_repairNames_tick()` arms `BANKLOAD` and enters `filesystem_loadBankDirectory_tick()`.

For `000 Full`, the Bank reader reads `/.hcnames`, opens `/Bank` and `000 Full`, parses `bankset.bcg`, scans its `00`–`15` child directories, and delegates every selected child to `filesystem_loadSceneDirectory_tick()`. The child reader opens/parses `sceneset.scg`, embedded Kit data, six Instrument files, Pattern, and Effects, then the Bank reader writes merged `/.hcnames` and waits for its final flush.

The current host directory contains `bankset.bcg` and all sixteen child Scene directories. That does not prove a target FAT/SPI operation completed.

## Indefinite-hang hypothesis

This Bank path is foreground-pumped rather than raw-blocking. Its state machines can nevertheless wait forever if AsyncFATFS stops producing the completion or data result the current phase requires. The concrete candidates are:

- Any phase waiting for `op_file_ready` after `afatfs_fopen_lfn()`, `afatfs_opendir_lfn()`, or `afatfs_fopen()` has been accepted but its open callback never runs. This covers HCNAMES, `/Bank`, `000 Full`, a child Scene, `bankset.bcg`, `sceneset.scg`, Kit directories/files, and Instrument files.
- Any phase waiting for `op_close_done` after `afatfs_fclose()` was accepted but `on_file_closed()` never runs.
- `filesystem_readTextLine()` returning `STORAGE_STATUS_WAIT` forever because its underlying file read makes no byte progress and has not reached EOF. This covers HCNAMES, `bankset.bcg`, Scene/Kit text, and Instrument text.
- `afatfs_findNextObject()` returning `AFATFS_OPERATION_IN_PROGRESS` forever while scanning Bank child directories or Scene children.
- `afatfs_chdir()`, `afatfs_chdirParent()`, `afatfs_fwrite()`, or final `afatfs_sync()` repeatedly declining/returning no progress in the same phase.

The source hypothesis is an AsyncFATFS/SPI completion or data-progress stall in one of these phase waits. It is not safe to infer that `000 Full` as a whole, every child Scene, or BankData ownership is faulty from the coarse `BANKLOAD` code alone.

## Dependency

First implement and test the private detail-only boot-log helper from `KITQUAR_FIX.md`:

```c
static void filesystem_bootLoggingSetDetail(const char code[8]);
```

It updates the existing eight-byte code only. It must not reset the timeout, start an operation, create another file, allocate RAM, or add a development mode. This Bank plan reuses that helper after the Kit pass has hardware evidence.

## Detailed diagnostic hook plan

When `current_op == FS_INTERNAL_OP_LOAD_BANK`, set the existing boot-log code immediately before entering a wait-capable Bank or delegated Scene sub-operation. Never call `filesystem_bootLoggingArm()` from these hooks and never refresh the `BANKLOAD` start tick.

| Code | Last operation entered |
| --- | --- |
| `BKHCREAD` | Read the pre-load root `/.hcnames` register. |
| `BKROOT  ` | Open, enter, or close root `/Bank`. |
| `BKnnnDIR` | Open or enter selected root Bank `nnn`. |
| `BKnnnSET` | Open, read, or parse `bankset.bcg` for Bank `nnn`. |
| `BKnnnSCN` | Scan child Scene directories in Bank `nnn`. |
| `BnnnSssO` | Open/reopen Bank child Scene `ss`. |
| `BnnnSssK` | Read that child's embedded Kit and `kitset.kcg`. |
| `BnnnSssI` | Read that child's Instrument-member sequence. |
| `BnnnSssP` | Read that child's Pattern/Effects stage. |
| `BKHCWRIT` | Write merged root `/.hcnames`. |
| `BKFLUSH ` | Wait for final Bank-load flush. |

`nnn` is a zero-padded Bank slot and `ss` is a zero-padded child Scene slot. Examples: `BK000SET`, `B000S12I`, and `BKHCWRIT`. Construct dynamic values in existing local/scratch memory only.

### Source placement

In `filesystem_loadBankDirectory_tick()` add the detail at these boundaries:

- phases 0/80/81/82: `BKHCREAD`;
- phases 1–7: `BKROOT  ` and `BKnnnDIR`;
- phases 8–12: `BKnnnSET`;
- phases 15–17: `BKnnnSCN`;
- phases 21–31: `BnnnSssO`;
- phases 83–86: `BKHCWRIT`.

In `filesystem_loadSceneDirectory_tick()`, set `BnnnSssO`, `K`, `I`, and `P` only when the current operation is `FS_INTERNAL_OP_LOAD_BANK`. Do not change root-Scene loading. On handoff to the shared flush operation, select `BKFLUSH ` instead of `FSFLUSH ` while preserving the existing behaviour that starts one fresh, separate ten-second flush deadline.

Set detail once when a particular pending action starts. Do not set it at the top of a state-machine case on every tick, because a stuck operation must retain the code that named it. Do not alter Bank masks, parsing, callbacks, BankData, SceneData, or load order in this pass.

## Hardware test

1. Complete and test `KITQUAR_FIX.md` first.
2. Build this Bank detail pass with `DEV_MODE_LOGGING == 1` and `DEV_MODE_DIAGNOSTIC == 0`.
3. Cold-boot repeatedly with the normal `0xffffu` Bank mask and unchanged card content.
4. Preserve `bootlog.bin` after any failure. Firmware intentionally overwrites that one name on a later timeout.
5. The failure record must name one `BK...`/`B...` detail stage rather than broad `BANKLOAD`.
6. Rebuild with `DEV_MODE_LOGGING == 0` and confirm no diagnostic file, RAM allocation, or Bank behavior remains.

## Result interpretation

- `BKHCREAD`, `BKHCWRIT`, or `BKFLUSH ` points to name-register or flush work, not Bank payload parsing.
- `BK000DIR` or `BK000SET` points to `/Bank/000 Full` or `bankset.bcg`.
- `BK000SCN` points to Bank-child directory iteration.
- `B000SssO`, `K`, `I`, or `P` identifies one child Scene and file family. Investigate that one path before changing another.

The diagnostic identifies the next isolated source fix. It does not claim to cure the underlying hang.

## Other quarantine and boot-operation audit

Only root Kit quarantine is active. The recursive Bank-tree quarantine helpers are inside `#if 0`; they cannot cause the current `BANKLOAD` failures.

Other boot operations also have coarse ten-second codes and can have analogous wait-for-callback, read, finder, write, or sync stalls: `NAMEREPR`, `KITSCAN `, `SCNSCAN `, `BNKSCAN `, `LIBINDEX`, `INSSCAN `, `INSINDEX`, `BIDXLOAD`, `SIDXLOAD`, `KIDXLOAD`, `HCNAMES `, and `ASENSURE`.

Do not add detailed hooks to all of them now. Extend the proven detail-only helper only after a timeout record identifies one of those operations.

## Deep implementation map and documentation-in-place

This is a diagnostic refinement, not a transport or load-policy fix. The
watchdog records identify only the outer Bank operation. The foreground state
machine continued to run long enough to trip the watchdog, while
`sdcard_lxr02.c` has bounded token/write-busy retries whenever its poll runs.
Do not alter SPI timing, AsyncFATFS retry behaviour, Bank masks, parser rules,
callback ordering, or fallback policy without a repeated detail record.

Known non-timeout failure is also a logging event. The implementation uses the
bounded `filesystem_writeBootFailureLogBlocking()` path specified in
`KITQUAR_FIX.md`: it remounts once and writes the existing retained eight-byte
detail to the same `/bootlog.bin` after either a watchdog timeout or a
caller-confirmed boot failure. It adds no logger RAM/file format and is absent
when `DEV_MODE_LOGGING == 0`.

### A. Reuse the private hook; do not expose or duplicate it

`KITQUAR_FIX.md` first adds the private
`filesystem_bootLoggingSetDetail(const char code[8])` helper. This Bank pass
reuses it exactly: it writes only the existing eight-byte retained code while a
normal boot deadline is already armed and never starts a deadline.

| File / location | Required change | Comment text to place with the change |
| --- | --- | --- |
| `Core/Hardware/SD/filesystem.c`, private helper area | Add a small private formatter for `nnn` (`op_slot`) and `ss` (`op_bank_child_cursor`) that writes a caller-local eight-byte code. It uses no static buffer, allocation, `sprintf`, FAT call, or clock access. | `/* Format the current Bank/child coordinate into the fixed-width boot-log vocabulary. Inputs: captured operation slots. Output: a caller-local eight-byte label only. Why: recovery needs the pending boundary without consuming permanent SRAM or changing Bank ownership. */` |
| `Core/Hardware/SD/filesystem.h` | No API or structure change. The formatter and detail hook remain private. Optionally clarify that public Arm starts an operation deadline and fine detail is private. | `/* Public Arm starts an operation deadline; private filesystem.c detail capture changes only the retained label inside that deadline. */` |

### B. Exact Bank loader hook sites

Add a detail immediately before a wait-capable request/read/write/finder or its
close/return boundary begins. Do not put it at the top of a state-machine case
which runs every tick: the retained code must name the action already pending.

| `filesystem_loadBankDirectory_tick()` phases | Code | Set before | Comment text to place with the change |
| --- | --- | --- | --- |
| 0, 80–82 | `BKHCREAD` | pre-load `/.hcnames` open, text stream, and close | `/* Record the HCNAMES preload boundary before a wait-capable open/read/close. This changes only the timeout label; it neither restarts BANKLOAD nor makes a missing or invalid register successful. */` |
| 1–4 | `BKROOT  ` | root `/Bank` open, chdir, and close | `/* Record the root-Bank directory boundary before the pending AsyncFATFS action. Keep this label until another action starts so a timeout identifies the callback/progress wait. */` |
| 5–7 | `BKnnnDIR` | selected root Bank `nnn` open and chdir | `/* Record the selected Bank directory boundary only. This hook does not alter the cached key, slot validation, or BankData. */` |
| 8–12 | `BKnnnSET` | `bankset.bcg` open, text stream/parse, and close | `/* Bankset diagnostics identify its file boundary only. A malformed bankset retains the existing FS_STATUS_ERROR route and is never reclassified by logging. */` |
| 15–17 | `BKnnnSCN` | child finder, scan-handle close, and root-return boundary | `/* Identify Bank-child enumeration separately from payload loading. The hook cannot change child presence, selected masks, active-child choice, or empty-Bank semantics. */` |
| 21–31 | `BnnnSssO` | reopening `/Bank`, reopening `nnn`, finding child `ss`, close, and child directory open | `/* Record the concrete selected-child discovery/open boundary. Do not update this label on every poll and do not change child ordering or masks while diagnosing. */` |
| 83–86 | `BKHCWRIT` | final merged `/.hcnames` open, stream, close, and root return | `/* The Bank-owned register write is a load completion boundary. Its label must not cause a short write, failed close, or failed chdir to bypass FS_STATUS_ERROR. */` |

### C. Exact delegated Scene loader hook sites

`filesystem_loadSceneDirectory_tick()` is shared. Add the following only when
both `current_op == FS_INTERNAL_OP_LOAD_BANK` and `op_bank_payload_active` are
true. Root Scene Load must retain its current code, timing, and data behaviour.

| Source phases | Code | Set before | Comment text to place with the change |
| --- | --- | --- | --- |
| Bank handoff/open at Bank phases 18/31, and Scene phases 8–11 | `BnnnSssO` | entering/opening/scanning the selected child Scene and closing its scan handle | `/* This shared Scene state is executing as a selected Bank child. Record its open/discovery boundary without changing Scene parsing, payload-active ownership, or the root-Scene path. */` |
| Scene phases 12–26 | `BnnnSssK` | `sceneset.scg` open/read/close; embedded Kit open/chdir/close; embedded `kitset.kcg` open/read/close | `/* Keep Scene metadata and embedded-Kit work under one child detail code. The hook is observational: every malformed-file branch still sets op_close_status to FS_STATUS_ERROR. */` |
| Scene phases 27–32 | `BnnnSssI` | each Instrument member open/read/close | `/* Name the child Instrument sequence before its pending file operation. Do not advance op_instrument_slot or commit staged data as a side effect of logging. */` |
| Scene phases 33–60 and Bank-return phase 72 | `BnnnSssP` | parent transition; Pattern open/read/close; Effects open/read/close; return to Bank | `/* Pattern, Effects, and parent return are the last per-child I/O family. A timeout here preserves normal error/commit rules and cannot make a partial child successful. */` |

The eight-byte record cannot safely encode every parser phase. The `O/K/I/P`
families identify a Bank, child, and file family without new persistent state.
For example, `B000S12I` narrows the next investigation to Instrument sequence
for child 12 of Bank 000.

### D. Correct final-flush label selection in `filesystem_finish()`

`filesystem_finish(FS_STATUS_DONE)` currently starts an internal
`FS_INTERNAL_OP_FLUSH_FINISH`, arms a fresh `FSFLUSH ` deadline, and waits for
`afatfs_sync()`. Bank Load keeps that separate operation/deadline, but labels
it `BKFLUSH ` when—and only when—the prior owner was `FS_INTERNAL_OP_LOAD_BANK`.

| File / location | Required change | Comment text to place with the change |
| --- | --- | --- |
| `Core/Hardware/SD/filesystem.c`, `filesystem_finish()` | Capture `current_op` before overwriting it with `FS_INTERNAL_OP_FLUSH_FINISH`. Select `BKFLUSH ` for prior Load Bank, otherwise preserve `FSFLUSH ` exactly. Invoke existing `filesystem_bootLoggingArm()` once, retaining the fresh separate flush deadline. | `/* Preserve Bank-load provenance across the mandatory final flush. Inputs: the operation which completed logical work. Output: a fresh BKFLUSH deadline only for Load Bank; all other owners retain FSFLUSH. This selects a label only: a stalled sync still times out and an error never enters this DONE-only path. */` |
| `Core/Hardware/SD/filesystem.h` | No API change. Optional comment-only clarification that final flushes are internal operation boundaries, not caller-controlled logger labels. | `/* Final flushes are internal operation boundaries; the public boot logger exposes timeout state rather than a caller-controlled flush label. */` |

`BKFLUSH` does **not** retain the original `BANKLOAD` timer. It has the
established fresh ten-second timeout of the final flush operation; only the
eight-byte label differs.

### E. Consume Bank-load failure before Menu can acknowledge it

The normal asynchronous Bank path already preserves failures in
`filesystem_complete()` → `on_bank_load_complete()` → Preset’s
`pm_completed_ok`. At boot, however, `main.c` currently calls
`menu_pollPresetStatus()` immediately after the load wait. The plan must test
the completion result first, then send a confirmed failure to the bounded boot
failure writer so it cannot be hidden by acknowledgement or misread as a
valid empty Bank.

| File / location | Required change | Comment text to place with the change |
| --- | --- | --- |
| `main.c`, initial `filesystem_requestLoadBankIndex(NULL)` wait | Before `filesystem_ack()`, require `filesystem_status() == FS_STATUS_DONE`. A non-timeout error branches to shared boot filesystem failure, retaining its current coarse/detail code. | `/* A failed Bank-index read is not an empty index. Check terminal status before acknowledgement so boot failure logging retains the originating storage boundary. */` |
| `main.c`, `preset_loadBank(boot_bank_slot, 0xffffu)` | Check the request return value. If the filesystem does not accept the request, branch to the shared boot failure path rather than entering the Preset wait with no load owner. | `/* A rejected Bank-load request has no callback to report it later. Treat it as an explicit boot filesystem failure rather than silently continuing the fallback ladder. */` |
| `main.c`, after the `PRESET_LOAD_IN_PROGRESS` wait and before `menu_pollPresetStatus()` | If Preset completed `PRESET_OP_BANK_LOAD` with `!preset_getCompletedOk()`, branch to shared boot failure before Menu acknowledgement. A successful empty Bank retains `completed_ok == 1` and still follows the existing fallback. | `/* Distinguish failed Bank Load from successful empty Bank before Menu consumes the completion. Failure writes the retained diagnostic code; only a successful empty container may start the ordinary Scene/Kit fallback. */` |
| `main.c`, shared boot timeout/failure cleanup | Use `filesystem_writeBootFailureLogBlocking()` from the common confirmed-failure cleanup described in `KITQUAR_FIX.md`. Preserve the existing rule that recovery result never gates startup. | `/* Timeout and confirmed non-timeout boot storage failure share one bounded DEV_MODE_LOGGING record path. Do not retry the failed Bank operation or apply its Preset stage during cleanup. */` |
| `Core/Hardware/SD/filesystem.c/.h` | Apply the failure-writer rename/generalization exactly once as planned in `KITQUAR_FIX.md`; no Bank-specific second writer or state flag. | `/* One retained eight-byte code and one bounded bootlog recovery path serve all boot storage failures. */` |

The root Scene/Kit fallback index loads use the same acceptance and
terminal-status checks. This closes the adjacent boot-ladder gap without
altering the valid “no Scene/Kit exists” fallback decision: only a completed
successful empty index may advance to the next fallback.

## Failure-transparency review gate

Before implementation is accepted, inspect every added hook against these
invariants:

- It cannot change `status`, `current_op`, `op_phase`, `op_close_status`, any
  callback flag, Bank/Scene masks, staged data, identity, or timer start.
- It never calls `filesystem_bootLoggingArm()` and never writes
  `fs_boot_logging_started_tick`.
- Every existing Bank/Scene error branch remains an error: missing callback
  file, failed finder, malformed parser/finalizer, failed close/chdir, and
  HCNAMES formatting/write failure cannot fall into empty-Bank fallback or
  success because a detail hook was added.
- `on_bank_load_complete()` continues to update provenance/settings only for
  `FS_STATUS_DONE`; Preset’s `pm_completed_ok == 0` and Menu’s error overlay
  remain the normal non-timeout failure signal.
- At boot, a completed Bank error is checked before Menu acknowledgement and
  enters the shared bounded failure writer; it cannot masquerade as a valid
  empty Bank or disappear after `preset_ackStatus()`.
- The root Scene/Kit index requests are likewise checked for acceptance and
  `FS_STATUS_DONE` before acknowledgement; an I/O error cannot become an
  apparently empty library solely because the Bank fallback ladder is active.
- `BKFLUSH` is selected only in the DONE handoff from Load Bank. It must never
  overwrite a load error or label another operation’s flush.
- With `DEV_MODE_LOGGING == 0`, all detail calls/formatting compile out; no
  persistent RAM, file write, or functional Bank behaviour is added.

## Test sequence and evidence threshold

1. Implement and hardware-test KITQUAR first. Do not combine these two
   diagnostic passes or their tests.
2. Build Bank hooks with `DEV_MODE_LOGGING == 1` and
   `DEV_MODE_DIAGNOSTIC == 0`. Repeatedly cold-boot the normal `0xffffu` mask
   and unchanged `000 Full` bank.
3. Verify a successful Bank load has unchanged Bank/Scene data and root
   HCNAMES contents. Separately exercise a known empty Bank and malformed
   Bank/child fixture to prove existing fallback/error paths survive.
4. Preserve `bootlog.bin` immediately after each failure; the intentional
   fixed root filename is overwritten by a later timeout.
5. Change an AsyncFATFS/SD primitive only after a repeated, specific detail
   code identifies it. A single broad record is not evidence for a fix.
6. Build with `DEV_MODE_LOGGING == 0` and verify no diagnostic output, RAM,
   or Bank behaviour remains.

## Implementation notes

- Implemented the shared private detail hook usage with stack-only Bank and
  Bank-child formatters; no public detail API, extra logger buffer, or new dev
  mode was added.
- Implemented Bank root/bankset/child/HCNAMES boundaries and Bank-gated Scene
  `O`, `K`, `I`, and `P` labels. Root Scene Load retains its existing taxonomy.
- Implemented `BKFLUSH ` selection only at the existing successful Load-Bank
  final-flush handoff; it retains that flush operation’s fresh ten-second
  deadline.
- Implemented boot checks for Bank-index acceptance/terminal status, Bank-load
  request acceptance, and failed Preset completion before Menu acknowledgement.
  Applied the same acceptance/status rule to root Scene/Kit fallback index
  loads, so only a successful empty index advances the fallback ladder.
  Confirmed timeout and ordinary boot failure now share the bounded retained
  bootlog writer implemented with the Kit pass.
- Combined firmware build passed with the project’s existing unrelated
  unused-function and newlib syscall linker warnings. A forced
  `DEV_MODE_LOGGING == 0` build also passed with BSS reduced from 78,476 to
  78,444 bytes, confirming no persistent Bank diagnostic allocation remains
  in that configuration. Hardware testing remains required, sequentially after
  the Kit pass: normal/empty/malformed Bank cases and repeated cold boots to
  capture any `BK...` detail record.
