# Boot Filesystem Timeout Logging Plan

## Status and scope

This began as a diagnostic-only implementation plan. The targeted plan is now
implemented; the implementation record and verification results are appended
below.

The rejected directory-scan plan is out of scope. This plan does not change
directory parsing, VFAT repair, rename behavior, autosave format, FAT handling,
or any storage payload. Its only purpose is to identify the last boot
filesystem operation that failed to complete within ten seconds.

The requested behavior is:

1. Replace the current development-mode feature flag with `DEV_LOGGING`.
2. Set `DEV_LOGGING` to `1`.
3. Before each boot filesystem operation, copy one exact eight-byte ASCII code
   into diagnostic SRAM.
4. Give each boot filesystem operation a ten-second deadline.
5. If the deadline expires, stop the remaining SD boot sequence.
6. Write the captured eight bytes to `/bootlog.bin`.
7. Continue booting the audio/runtime path rather than remaining on the splash
   screen.

`/bootlog.bin` will contain exactly eight bytes. It will not contain a NUL,
newline, timestamp, phase dump, or historical records. A later timeout replaces
the previous file. A successful boot leaves an existing log untouched.

## Important constraint in the current code

The filesystem facade and asyncfatfs have single-owner operation state. When a
boot operation times out:

- `filesystem_status()` is normally still `FS_STATUS_BUSY`;
- `current_op`, open file handles, cached directory sectors, and possibly an SD
  block transfer still belong to the timed-out operation;
- `filesystem_start()` refuses a second operation while the facade is busy;
- starting a normal `/bootlog.bin` write at that point is unsafe.

Therefore the timeout path cannot simply break one loop and call the ordinary
file writer. The diagnostic path must first abandon the timed-out operation
without flushing it, reset the SD transfer shim, remount the card, and then
perform a separately bounded log write.

This recovery is necessarily best-effort. If the card or filesystem cannot be
remounted—the exact system being diagnosed—firmware cannot guarantee creation
of a FAT file. The recovery/log attempt must have its own ten-second ceiling and
must never replace one boot hang with another. Whether or not the log write
succeeds, firmware proceeds to audio initialization after the attempt.

No dirty-abandon behavior is compiled when `DEV_LOGGING == 0`.

## Exact current boot path

The current pre-audio filesystem sequence in `main.c` is:

1. `filesystem_initCardAndMountBlocking()`.
2. `filesystem_requestScanKits()` plus a direct busy loop.
3. `filesystem_createLibraryIndexBlocking(KIT)`.
4. `filesystem_requestScanScenes()` plus a direct busy loop.
5. `filesystem_createLibraryIndexBlocking(SCENE)`.
6. `filesystem_requestScanBanks()` plus a direct busy loop.
7. `filesystem_createLibraryIndexBlocking(BANK)`.
8. `filesystem_createBootIndexBlocking()` for Instrument indexes.
9. `filesystem_requestLoadBankIndex()` plus a direct busy loop.
10. A Bank Load request, or Scene-index/Kit-index fallback selection.
11. A Preset busy loop that pumps Bank/Scene/Kit payload loading.
12. Globals Load through another Preset busy loop.
13. `filesystem_ensureAutosaveFilesBlocking()` when a Bank became resident.

Several blocking wrappers in `filesystem.c` contain their own
`while (status == FS_STATUS_BUSY) filesystem_tick()` loops. Kit quarantine and
payload validation also contain private blocking loops that call
`filesystem_blockPoll()`, which calls `afatfs_poll()` directly rather than
`filesystem_tick()`.

`time_sysTick` is already available in `main.c` and `filesystem.c`. It advances
at 1 kHz from TIM6 and is a wrapping `uint16_t`. A 10,000 ms interval is safely
below its 32,768 ms half-range, so the existing unsigned-subtraction timing
pattern is valid.

The current `CONFIG_DEV_MODE` active preprocessor use is confined to `main.c`.
It enables OLED boot-stage/phase diagnostics. Mentions in `menu.c` are comments,
not active feature gating. `DEBUG_CRASH_MODE` is a separate feature and is not
part of this change.

## Eight-byte operation codes

Codes are fixed arrays, never C strings. Every declaration must be exactly
eight initialized characters, and every copy/write must use a byte count of
eight rather than `strlen()`.

The initial code table will be:

| Code | Operation |
|---|---|
| `MOUNTSD ` | SD initialization and asyncfatfs mount |
| `KITSCAN ` | Root Kit physical scan |
| `SCNSCAN ` | Root Scene physical scan |
| `BNKSCAN ` | Root Bank physical scan |
| `NAMEREPR` | Boot name-repair operation |
| `KITQUAR ` | Blocking Kit validation/quarantine pass |
| `LIBINDEX` | Root Kit/Scene/Bank `.hcindex` write |
| `INSSCAN ` | One Instrument-type physical scan |
| `INSINDEX` | One Instrument-type `.hcindex` write |
| `BIDXLOAD` | Root Bank `.hcindex` load |
| `SIDXLOAD` | Root Scene `.hcindex` load |
| `KIDXLOAD` | Root Kit `.hcindex` load |
| `BANKLOAD` | Initial Bank payload load |
| `SCNELOAD` | Initial root/Bank-local Scene payload load |
| `KITLOAD ` | Initial root/embedded Kit payload load |
| `GLOBLOAD` | Globals/settings load |
| `ASENSURE` | Boot autosave-file existence/creation pass |
| `HCNAMES ` | A boot HCNAMES operation if re-enabled/called |
| `FSFLUSH ` | Final asyncfatfs persistence gate |

`BOOTLOG ` is reserved for the recovery write itself, but it must never replace
the captured culprit code. Recovery uses separate deadline state.

The mapping is deliberately operation-level. It does not encode filesystem
phase numbers, slot numbers, filenames, or directory-entry information.

## File-by-file changes

### 1. `config.h`

Remove the `CONFIG_DEV_MODE` definition and its outdated two-part comment.

Add:

```c
#define DEV_LOGGING 1
#define BOOT_FILESYSTEM_TIMEOUT_MS 10000u
```

The adjacent comment block must describe:

- what `DEV_LOGGING` compiles in;
- why it is separate from `DEBUG_CRASH_MODE`;
- that it enables boot filesystem operation codes, timeout enforcement,
  best-effort `/bootlog.bin` recovery, and the existing boot OLED diagnostics;
- that it changes boot-time filesystem behavior and must normally be disabled
  after diagnosis;
- that `BOOT_FILESYSTEM_TIMEOUT_MS` must remain below 32,768 because
  `time_sysTick` is a wrapping 16-bit millisecond counter.

What this change does: replaces the vague development-mode switch with the
specific diagnostic capability requested and sets it on for the next build.

Why it must exist: logging, timeout checks, and dirty recovery must be removable
from normal firmware with one explicit feature flag.

Inputs: build-time values.

Outputs: compile-time inclusion of boot logging and a 10,000 ms operation
deadline.

Affiliates: `main.c`, `filesystem.c`, `filesystem.h`,
`sdcard_lxr02.c`, and `sdcard_lxr02.h`.

`DEBUG_CRASH_MODE` remains unchanged because it is unrelated to filesystem boot
logging.

### 2. `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.h`

Add one always-declared diagnostic entry point:

```c
void sdcard_abortTransferForBootLog(void);
```

Keep the declaration present in all builds. Do not include application
`config.h` from this low-level header. The implementation becomes a no-op when
logging is disabled, avoiding conditional prototype mismatches and keeping the
dependency direction explicit.

What this change does: exposes the minimum SD-shim reset required before
discarding asyncfatfs state.

Why it must exist: `afatfs_destroy(true)` clears filesystem callbacks and cache
state but does not reset `sdcard_lxr02.c`'s private transfer state. A later
callback into discarded asyncfatfs memory must not occur during the remount/log
attempt.

Inputs: the current private SD transfer state.

Outputs: the SD shim is idle with no retained transfer callback.

Affiliates: `filesystem_prepareBootLogRecovery()`,
`afatfs_destroy(true)`, `SD_init()`, and the SPI chip-select state.

### 3. `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c`

Implement `sdcard_abortTransferForBootLog()` next to
`sdcard_getState()`.

Add `#include "config.h"` in this `.c` file so only the implementation consumes
`DEV_LOGGING`; the public SD shim header remains independent of application
configuration.

When `DEV_LOGGING == 1`, it must:

- deassert SD chip select;
- emit the normal idle dummy clocks already used after block completion;
- set the private state machine to `SDCARD_STATE_IDLE`;
- clear `xfer_buffer`, `xfer_block`, `xfer_offset`, `retry_count`,
  `xfer_callback`, `xfer_callbackData`, and `xfer_operation`;
- invoke no completion callback.

When `DEV_LOGGING == 0`, it must compile as a harmless no-op or be removed by
the preprocessor.

What this change does: prevents an abandoned read/write from later calling
into asyncfatfs state that has been discarded.

Why it must exist: the timeout path intentionally stops normal ownership and
cannot wait indefinitely for the same transfer that caused the timeout.

Inputs: an optional in-progress CMD17/CMD24 transfer.

Outputs/effects: transport state is abandoned; an incomplete sector write may
remain incomplete on the card. The subsequent SD initialization/remount is
responsible for reestablishing protocol state.

Affiliates: `sdcard_poll()`, `SD_CS_DEASSERT`, `SPI_transmit()`,
`afatfs_destroy(true)`, and boot-log remount.

This function is diagnostic and destructive to the in-flight operation. Its
comment must not describe it as a general runtime cancellation API.

### 4. `Core/Hardware/SD/filesystem.h`

Add public boot-logging declarations:

```c
void filesystem_bootLoggingBegin(void);
void filesystem_bootLoggingArm(const char code[8]);
uint8_t filesystem_bootLoggingTimedOut(void);
const uint8_t *filesystem_bootLoggingCode(void);
uint8_t filesystem_writeBootTimeoutLogBlocking(void);
void filesystem_bootLoggingEnd(void);
```

When `DEV_LOGGING == 0`, the implementation may provide no-op functions rather
than spreading preprocessor branches through `main.c`.

The public contract must specify:

- `Begin` enables only the pre-audio diagnostic window;
- `Arm` copies exactly eight bytes and starts a fresh per-operation deadline;
- `TimedOut` is observational;
- `Code` returns the captured eight-byte buffer and is valid through recovery;
- `writeBootTimeoutLogBlocking` abandons the timed-out owner, remounts, and
  attempts one bounded root-file write;
- `End` permanently disables boot timeout behavior before audio/runtime
  filesystem work starts;
- no function is safe to invoke from an ISR;
- logging failure does not prevent boot from continuing.

What this change does: gives `main.c` a narrow boot-diagnostic interface without
exposing private filesystem or asyncfatfs state.

Why it must exist: timeout ownership and recovery belong at the filesystem
facade, while `main.c` owns the decision to abandon the remaining boot ladder.

Inputs: operation codes and boot sequencing.

Outputs: timeout state and one best-effort log result.

Affiliates: `main.c`, the filesystem tick/poll paths, and the internal log
writer.

Update the existing boot diagnostic comments to refer to `DEV_LOGGING` rather
than `CONFIG_DEV_MODE`.

### 5. `Core/Hardware/SD/filesystem.c`

Add the LXR-02 SD shim header include needed for
`sdcard_abortTransferForBootLog()`. `filesystem.c` already includes `config.h`
and `timebase.h`, so no additional timing/configuration include is required.

#### 5.1 Add boot logger state

Under `#if DEV_LOGGING`, add:

- `uint8_t fs_boot_logging_active`;
- `uint8_t fs_boot_logging_timed_out`;
- `uint8_t fs_boot_logging_recovery`;
- `uint8_t fs_boot_logging_recovery_failed`;
- `uint16_t fs_boot_logging_started_tick`;
- `uint16_t fs_boot_logging_recovery_started_tick`;
- `uint8_t fs_boot_logging_code[8]`.

Do not allocate a filename string, history array, timestamp record, or second
copy of the eight-byte code. Use the existing operation filename literals and
streaming scratch for the file write.

What this change does: retains the currently armed code and both normal/recovery
deadlines in a small fixed SRAM record.

Why it must exist: the culprit code must survive destruction/reinitialization
of asyncfatfs and generic filesystem operation scratch.

Inputs: `filesystem_bootLoggingArm()` and `time_sysTick`.

Outputs: timeout/recovery state available to all boot polling paths.

Affiliates: `filesystem_tick()`, `filesystem_blockPoll()`,
`filesystem_start()`, and the bootlog writer.

#### 5.2 Implement arm, deadline, and code mapping helpers

Implement:

- exact eight-byte copy in `filesystem_bootLoggingArm()`;
- wrapping elapsed-time comparison against
  `BOOT_FILESYSTEM_TIMEOUT_MS`;
- a private `filesystem_bootLogCodeForOperation()` mapping
  `fs_internal_op_t` plus `op_file_type` to the code table above;
- a private `filesystem_bootLoggingPollDeadline()` used by both foreground
  pump paths.

`filesystem_start()` must arm the mapped code immediately before it publishes
`FS_STATUS_BUSY`. This automatically covers internal operations started inside
blocking wrappers as well as requests posted by Preset.

`filesystem_finish()` must arm `FSFLUSH ` immediately before switching
`current_op` to `FS_INTERNAL_OP_FLUSH_FINISH`.

Direct internal handoffs that bypass `filesystem_start()` must arm their new
code explicitly. The exact current boot-relevant handoff is repaired Bank-name
phase 43 assigning `FS_INTERNAL_OP_LOAD_BANK`. Any other direct assignment
found by the implementation search must either be classified as runtime-only
or receive the same explicit call.

The mount wrapper must explicitly arm `MOUNTSD ` because mounting happens
before `filesystem_start()` can run.

The blocking Kit quarantine call inside
`filesystem_createLibraryIndexBlocking(KIT)` must explicitly arm `KITQUAR `
because it calls raw blocking helpers after the preceding repair operation has
completed.

What this change does: captures the last actual operation, including nested
repair/index/flush work, rather than only a broad stage number from `main.c`.

Why it must exist: the blocking index builders contain several independent
filesystem operations hidden behind one C call.

Inputs: private operation enum, file type, and explicit mount/quarantine
boundaries.

Outputs: one armed eight-byte code and a reset ten-second deadline.

Affiliates: `filesystem_start()`, `filesystem_finish()`,
`filesystem_createLibraryIndexBlocking()`,
`filesystem_repairNames_tick()`, and the boot mount wrapper.

#### 5.3 Make both polling paths observe the deadline

At the beginning of `filesystem_tick()`:

1. check the boot deadline;
2. if it expired, set `fs_boot_logging_timed_out`;
3. set facade status to `FS_STATUS_ERROR` so facade-owned blocking wrappers
   leave their `while (status == BUSY)` loops;
4. return without another `afatfs_poll()` or state-machine phase.

Do not call `filesystem_finish()` on timeout, because that would enter the
normal flush path for an operation being deliberately abandoned.

Update `filesystem_blockPoll()` to call the same deadline helper before
`afatfs_poll()`. Update `filesystem_blockFsOk()` to return false after a boot
timeout. This unwinds private open/read/rename/quarantine loops that do not use
`filesystem_tick()`.

Update the asyncfatfs initialization loop in
`filesystem_initCardAndMountBlocking()` so timeout is a mount failure rather
than another iteration.

What this change does: covers the two real progress mechanisms used during
boot.

Why it must exist: instrument/library wrappers pump through
`filesystem_tick()`, while Kit validation/quarantine pumps asyncfatfs directly.
Covering only one leaves an unbounded boot path.

Inputs: active flag, start tick, current millisecond tick, and recovery mode.

Outputs: continued progress before the deadline or one latched timeout.

Affiliates: all boot blocking wrappers, raw blocking helpers, mount, and
`main.c` Preset wait loops.

A C function that itself never returns cannot be preempted by this cooperative
watchdog. The current SD command initialization functions have their own
bounded command loops; the filesystem/asyncfatfs boot paths otherwise advance
in polling steps. This limitation must be documented in the code.

#### 5.4 Add an internal eight-byte bootlog writer

Add `FS_INTERNAL_OP_WRITE_BOOT_LOG` to `fs_internal_op_t`, its dispatcher case
in `filesystem_tick()`, and error prefix `BLog`.

Implement a small state machine:

1. change to the FAT root;
2. open `/bootlog.bin` with `afatfs_fopen_lfn("bootlog.bin", "w",
   AFATFS_MATCH_CASE_SENSITIVE, ...)`;
3. wait for the file handle;
4. stream exactly eight bytes from `fs_boot_logging_code`, retaining partial
   progress in `op_bytes_done`;
5. close the file;
6. wait for close;
7. finish through the normal `filesystem_finish(FS_STATUS_DONE)` sync gate.

The file is truncated/replaced on each successful timeout log. It contains no
terminator or newline.

What this change does: writes the requested fixed payload using the same
asynchronous file/close/sync behavior as other filesystem-owned writes.

Why it must exist: direct ad-hoc FAT writes from `main.c` would violate the
single-owner facade and could report completion before the sector was durable.

Inputs: the preserved eight-byte culprit code.

Outputs: an exactly eight-byte root file after successful close and sync.

Affiliates: `on_file_opened`, `on_file_closed`, `afatfs_fwrite()`,
`filesystem_finish()`, and `afatfs_sync()`.

#### 5.5 Add diagnostic abandon, remount, and bounded log recovery

Implement a private `filesystem_prepareBootLogRecovery()` used only by
`filesystem_writeBootTimeoutLogBlocking()`:

1. preserve `fs_boot_logging_code`;
2. set recovery mode so internal starts cannot overwrite the culprit code;
3. call `sdcard_abortTransferForBootLog()`;
4. call `afatfs_destroy(true)` to discard dirty cache/open-handle state without
   waiting for the timed-out owner;
5. clear facade status, operation, callbacks, and boot-only operation pointers
   through one dedicated reset helper;
6. rerun the existing slow-SPI settle, `SD_init()`, fast-SPI selection,
   `afatfs_init()`, and mount polling;
7. apply a fresh ten-second recovery deadline;
8. if mount succeeds, run the internal bootlog writer with the same recovery
   deadline policy;
9. if remount or write fails/times out, abort/destroy once more and return
   failure without retrying.

The recovery path must not call the public normal mount wrapper recursively
while the original timeout flag is still active. Factor the existing mount
steps into a private helper that accepts normal-boot versus recovery deadline
state.

Do not flush or close the timed-out operation before dirty destruction. Those
actions can wait on the same condition that timed out and would defeat the
diagnostic escape.

What this change does: obtains a clean filesystem ownership context in which a
normal root file can be written.

Why it must exist: the timed-out operation cannot coexist with a second facade
write.

Inputs: latched timeout, culprit code, current SD/asyncfatfs/facade state.

Outputs: nonzero only when `bootlog.bin` was closed and synced; otherwise zero
after a bounded attempt.

Affiliates: SD transfer abort, `afatfs_destroy(true)`, card initialization,
mount result state, internal bootlog writer, and `main.c` timeout cleanup.

The code-adjacent warning must state that dirty abandon may leave the operation
that timed out partially represented on the card. This is acceptable only in a
logging build whose purpose is to capture that failing operation.

#### 5.6 Disable logging before runtime

`filesystem_bootLoggingEnd()` must:

- clear the active/deadline/recovery flags;
- leave `fs_boot_logging_code` unchanged until overwritten by a later boot;
- ensure runtime calls to `filesystem_start()` do not arm codes or enforce
  deadlines.

What this change does: confines the ten-second abort policy to pre-audio boot.

Why it must exist: normal runtime loads/saves may legitimately be deferred by
UI ownership or card latency and must not be dirty-abandoned by a boot
diagnostic.

Inputs: completion or abandonment of the boot filesystem ladder.

Outputs: ordinary runtime filesystem semantics.

Affiliates: `main.c` immediately before `audioCodec_init()` and the autonomous
autosave scheduler.

### 6. `main.c`

#### 6.1 Replace the development flag

Change the existing `#if CONFIG_DEV_MODE` block and comments to
`#if DEV_LOGGING`.

Retain the current OLED stage/operation/substep diagnostics under the new flag.
They are already observational and useful alongside the persistent code. No new
screen or menu item is added.

What this change does: makes one specific diagnostic flag own both the existing
boot display clues and new persistent logging.

Why it must exist: `CONFIG_DEV_MODE` is being replaced, and leaving the old
conditional would break the build.

Inputs: `DEV_LOGGING`.

Outputs: diagnostic boot display and logger enabled together.

Affiliates: `boot_showFilesystemStage()`,
`boot_showActiveFilesystemDiagnostic()`, and boot substep callbacks.

#### 6.2 Start and stop the logging window

Call `filesystem_bootLoggingBegin()` immediately before stage 1 mount.

Call `filesystem_bootLoggingEnd()` on every path immediately before the common
stage 14/audio initialization boundary, including:

- normal SD boot completion;
- no-card/mount failure;
- timed-out boot and completed/failed log recovery.

What this change does: clearly separates pre-audio diagnostic policy from the
runtime main loop.

Why it must exist: `filesystem_start()` is shared by boot, Menu, Preset, and
autosave operations.

Inputs: boot sequence entry/exit.

Outputs: watchdog active only during boot filesystem work.

Affiliates: stage 1, stage 14, `audioCodec_init()`, and runtime
`filesystem_tick()`.

#### 6.3 Make direct main wait loops break on timeout

Update every direct boot wait:

- Kit scan;
- Scene scan;
- Bank scan;
- Bank/Scene/Kit index loads;
- the two-pass initial Preset payload loop;
- Globals Load.

Facade-status waits will normally leave automatically because
`filesystem_tick()` publishes `FS_STATUS_ERROR` at timeout. Preset-status waits
must additionally test `!filesystem_bootLoggingTimedOut()`, because Preset may
remain `PRESET_LOAD_IN_PROGRESS` after its filesystem callback is abandoned.

After every blocking wrapper call, test the same timeout flag before beginning
the next boot operation.

What this change does: gives the cooperative watchdog a route out of every
main-owned boot loop and nested blocking call.

Why it must exist: setting a timeout flag alone does not alter
`preset_getStatus()`.

Inputs: latched timeout state.

Outputs: normal continuation or one jump to timeout cleanup.

Affiliates: boot stages 2-13, Preset status, and all blocking filesystem
wrappers.

#### 6.4 Add one timeout cleanup path

Use one clearly labeled boot-filesystem timeout cleanup path rather than
duplicating recovery after every operation.

On timeout:

1. unregister the boot substep diagnostic callback;
2. call `preset_ackStatus()` if Preset remains non-idle, preventing the runtime
   Menu poller from waiting on an abandoned request;
3. call `filesystem_writeBootTimeoutLogBlocking()`;
4. ignore its return for boot-continuation purposes, while preserving it for
   OLED/error diagnostics if desired;
5. skip all remaining index, payload, Globals, and autosave boot work;
6. end boot logging;
7. continue to stage 14 and `audioCodec_init()`.

Do not call `menu_pollPresetStatus()` for the abandoned request, because it may
apply incomplete staged data or start a fallback filesystem request.

What this change does: implements “break from the boot process” as one
controlled exit from SD bootstrap, not a return from `main()`.

Why it must exist: the instrument must still reach its normal runtime/audio
loop after diagnostic recovery.

Inputs: timeout flag and preserved code.

Outputs: best-effort `/bootlog.bin`, no further boot SD operations, runtime
startup.

Affiliates: Preset status lifecycle, filesystem diagnostic callback, resident
Bank/autosave authorization, and audio initialization.

## Files deliberately unchanged

- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`: existing
  `afatfs_destroy(true)` already supplies the dirty filesystem-state discard.
  No directory, rename, cache, or FAT algorithm change is planned.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`: the existing destroy declaration
  is sufficient.
- `Core/Bank/Scene/Autosave.c/.h`: no format or writer change.
- `Core/Bank/Scene/Preset/presetManager.c/.h`: the existing
  `preset_ackStatus()` is sufficient to clear an abandoned boot request.
- `Core/Menu/menu.c`: its `CONFIG_DEV_MODE` occurrences are comments only;
  update those comments to `DEV_LOGGING` only if the implementation-wide search
  confirms they still describe the active configuration.
- All directory scan/name-repair code: no attempted fix.

## Verification

### Static/build checks

1. Run `make`.
2. Run `git diff --check`.
3. Confirm no active or comment reference to `CONFIG_DEV_MODE` remains.
4. Confirm `DEV_LOGGING` is `1`.
5. Confirm `BOOT_FILESYSTEM_TIMEOUT_MS` is exactly `10000u`.
6. Confirm every operation-code initializer has exactly eight characters.
7. Confirm the bootlog writer passes literal length `8u` to
   `afatfs_fwrite()`.
8. Confirm `filesystem_bootLoggingEnd()` runs before every route to runtime
   audio.

### Normal boot

1. Boot with a healthy copied test card.
2. Confirm the complete filesystem ladder still reaches audio.
3. Confirm no new `bootlog.bin` is created solely because logging is enabled.
4. If an older `bootlog.bin` exists, confirm successful boot leaves its eight
   bytes unchanged.
5. Confirm existing OLED stage/phase diagnostics remain available under
   `DEV_LOGGING`.

### Forced timeout

Use a temporary, uncommitted diagnostic stall in one polling state at a time.
Do not manufacture filesystem corruption for this test.

For each selected operation:

1. confirm the operation code becomes the expected eight bytes in SRAM;
2. confirm the timeout occurs at approximately ten seconds;
3. confirm the remaining boot filesystem ladder is skipped;
4. confirm recovery/remount is attempted only once;
5. confirm audio/runtime startup continues;
6. remove the card and inspect:

   ```text
   bootlog.bin size: 8 bytes
   contents: exact expected ASCII code
   ```

Test at least:

- mount (`MOUNTSD `);
- root Kit scan (`KITSCAN `);
- name repair (`NAMEREPR`);
- library index write (`LIBINDEX`);
- initial Bank payload load (`BANKLOAD`);
- final flush (`FSFLUSH `);
- autosave ensure (`ASENSURE`).

### Recovery failure

Force card removal immediately after the primary timeout.

Expected behavior:

- remount/logging fails within its own ten-second ceiling;
- firmware does not retry indefinitely;
- audio/runtime boot still continues;
- no claim is made that `bootlog.bin` was written.

### Logging disabled

Build once with `DEV_LOGGING 0`.

Confirm:

- no timeout/dirty-abandon behavior remains active;
- no root bootlog write is attempted;
- existing boot filesystem order is unchanged;
- runtime filesystem and autosave scheduling are unchanged.

## Implementation order

1. Replace `CONFIG_DEV_MODE` with `DEV_LOGGING`, set it to `1`, and add the
   ten-second constant.
2. Add the small logger state and public arm/query/end API.
3. Map internal boot filesystem operations to exact eight-byte codes.
4. Add watchdog checks to `filesystem_tick()`, raw blocking poll, mount, and
   main Preset loops.
5. Add the eight-byte root writer.
6. Add diagnostic SD-transfer abort and dirty asyncfatfs/facade reset.
7. Add one bounded remount/write recovery function.
8. Add the single timeout cleanup route in `main.c`.
9. Verify healthy boot, forced timeouts, recovery failure, and the logging-off
   build.

Every `.c` and `.h` change must include an adjacent comment block documenting
what it does, why it exists, inputs, outputs/effects, and affiliated
functions/state, matching the project's current storage documentation style.

## Completion criteria

The implementation is complete when:

- `DEV_LOGGING` is enabled;
- the last boot filesystem operation is represented by exactly eight bytes;
- every boot filesystem polling path observes a ten-second deadline;
- timeout exits the remaining SD bootstrap;
- one bounded recovery attempt writes exactly those eight bytes to root
  `bootlog.bin`;
- failure to remount/write cannot hang boot;
- audio initialization is reached after either successful or failed logging;
- disabling `DEV_LOGGING` restores normal boot/runtime behavior;
- no directory-scan or storage-format fix is included.

## Implementation record — 2026-07-30

### Completed changes

- `config.h`
  - Replaced the active `CONFIG_DEV_MODE` switch with `DEV_LOGGING`.
  - Left `DEV_LOGGING` enabled at `1`.
  - Added the 10,000 ms `BOOT_FILESYSTEM_TIMEOUT_MS` boundary with the
    wrapping-`uint16_t` constraint documented beside it.

- `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c/.h`
  - Added the diagnostic-only transfer-abandon entry point.
  - The enabled implementation releases chip select, supplies idle clocks,
    clears every private transfer coordinate/callback, and invokes no stale
    completion callback.
  - The disabled implementation is a no-op, while the declaration remains
    stable for the filesystem facade.

- `Core/Hardware/SD/filesystem.c/.h`
  - Added the fixed eight-byte logger record and public begin/arm/query/write/end
    contract.
  - Added stable operation-code mapping at the actual filesystem operation
    boundary rather than at broad screen stages.
  - Added explicit arms for mount, raw Kit quarantine, the final sync gate, and
    direct internal handoffs that bypass `filesystem_start()`.
  - Added deadline observation to both `filesystem_tick()` and the private raw
    blocking FAT poll path.
  - Added `FS_INTERNAL_OP_WRITE_BOOT_LOG`, which replaces root `bootlog.bin`,
    streams exactly eight bytes, closes it, and uses the normal asyncfatfs sync
    gate.
  - Added one bounded recovery path that aborts the low-level transfer,
    destroys dirty asyncfatfs state, clears invalid facade ownership, remounts,
    and attempts the log write. Recovery failure performs no retry.
  - Recovery disables autonomous autosave authorization because the remaining
    boot filesystem ladder is skipped after a timeout.

- `main.c`
  - Moved the existing OLED diagnostics under `DEV_LOGGING`.
  - Opened the logging window immediately before mount and closed it on every
    route immediately before stage 14/audio initialization.
  - Added timeout exits after every facade-owned boot wait/wrapper.
  - Added an explicit timeout predicate to Preset-owned waits, which otherwise
    retain `PRESET_LOAD_IN_PROGRESS` after an abandoned callback.
  - Added one shared cleanup label that unregisters the substep observer,
    acknowledges abandoned Preset state, performs the bounded log attempt, and
    skips all remaining boot filesystem work.

- `Core/Menu/menu.c`
  - Updated the two stale comment references from `CONFIG_DEV_MODE` to
    `DEV_LOGGING`; no menu behavior or directory handling changed.

### Deliberately unchanged

- No directory scanner, filename parser, LFN repair, rename, or FAT algorithm
  was modified.
- No autosave file format or autosave transaction behavior was modified.
- `asyncfatfs.c/.h`, Preset Manager, Bank/Scene/Kit payload formats, and the
  archived rejected directory-scan draft were not changed.

### Verification completed

- Full clean build with `DEV_LOGGING == 0`: passed and linked.
- Restored `DEV_LOGGING == 1`.
- Full clean final build with logging enabled: passed and linked.
- Final enabled image size:
  - text: 362,196 bytes
  - data: 400 bytes
  - BSS: 69,980 bytes
- `git diff --check`: passed.
- Active source/config search found no remaining `CONFIG_DEV_MODE` reference.
- All boot operation literals used by the logger are exactly eight characters.
- The bootlog writer passes an exact `8u - op_bytes_done` byte count and never
  uses `strlen()`.

The compiler still reports pre-existing project warnings, including the unused
HCNAMES OLED callback and newlib syscall stubs; neither build produced an error.

### Hardware verification still required

No artificial stall was added to committed code, so timeout/remount behavior
has not yet been exercised on the LXR hardware. The first hardware test should
force one polling state to stop advancing, wait for the ten-second primary
deadline plus at most the ten-second recovery budget, confirm audio startup
continues, and then inspect root `bootlog.bin` for an exact eight-byte code.
