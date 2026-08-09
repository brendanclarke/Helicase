# Kit Quarantine Boot Failure — Fix Plan

## Evidence

`SD_CARD/bootlog3.bin` contains exactly `KITQUAR `. The ten-second boot watchdog expired while root Kit validation/quarantine was active. This happens before the root Bank index reload and before `000 Full` is requested, so it is not a Bank-load failure.

Normal boot, including the Bank load, completes well inside ten seconds. Treat this as a real hang, not as a slow valid traversal. Keep the existing ten-second deadline for the entire `KITQUAR` operation.

## Current path

Boot stage 4 calls `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`. It repairs root Kit names, arms `KITQUAR `, runs `filesystem_quarantineKitLibraryBlocking()`, then scans `/Kit` and writes `.hcindex`.

The active quarantine enters `/Kit`, finds every numbered Kit, enters it, parses `kitset.kcg`, opens/closes its six declared Instrument members, and renames an invalid Kit to `err...` before restarting the root scan.

## Why the current log is insufficient

`KITQUAR ` covers root directory entry, root iteration, a particular Kit directory, `kitset.kcg`, every Instrument file, close operations, and quarantine rename/sync. The log does not identify the stopped primitive or the Kit slot.

The active path holds at most three AsyncFATFS application handles: root Kit directory, current Kit directory, and one payload file. `asyncfatfs.h` documents a five-handle application pool (the current-directory object is separate), so this path is below the pool limit. Preserve that ownership in this diagnostic pass; a handle-count change is not justified by the timeout record.

## Indefinite-hang hypothesis

The quarantine path is synchronous only at the wrapper level. Its helpers repeatedly poll AsyncFATFS until a request is accepted or a callback/result arrives. Without the boot watchdog, any of these waits can continue indefinitely when the SD/FAT state machine stops advancing:

- `filesystem_blockOpenDirLfn()` waiting for `/Kit` or a numbered Kit directory to become openable, or for `filesystem_blockOpenCb()` to set `block_file_ready`.
- `filesystem_blockFindNextObject()` repeatedly receiving `AFATFS_OPERATION_IN_PROGRESS` while iterating `/Kit`.
- `filesystem_blockOpen()` / `filesystem_blockOpenLfn()` waiting for `kitset.kcg` or a declared Instrument file to open, or for its open callback.
- `filesystem_blockRead()` repeatedly receiving zero bytes while EOF is false during `kitset.kcg` streaming.
- `filesystem_blockClose()` waiting for `filesystem_blockCloseCb()` after closing a directory or member file.
- `filesystem_blockRename()` waiting for its rename callback or for `afatfs_sync()` after an invalid Kit is quarantined.
- `filesystem_blockChdir()` repeatedly failing to enter/return from a directory.

The strongest source-level hypothesis is therefore an AsyncFATFS/SPI progress stall in one of these wait loops, rather than a Kit parser loop or a handle-pool over-allocation. The present `KITQUAR ` record cannot distinguish them.

## Fix: detail-only logger hook

Add a private helper beside `filesystem_bootLoggingArm()` in `Core/Hardware/SD/filesystem.c`:

```c
static void filesystem_bootLoggingSetDetail(const char code[8]);
```

When `DEV_MODE_LOGGING` is enabled and a normal boot operation is armed, it copies exactly eight bytes into the existing `fs_boot_logging_code`. It must not reset `fs_boot_logging_started_tick`, re-arm the timer, modify filesystem ownership, write a file, or allocate RAM. With `DEV_MODE_LOGGING == 0`, it compiles to a no-op.

`KITQUAR` remains one ten-second operation. The existing timeout recovery writes the last detail code to the same `bootlog.bin`; there is no second file, mode, record format, or deadline policy.

## Detail-code placement

Instrument only `filesystem_quarantineKitLibraryBlocking()` and `filesystem_validateCurrentKitBlocking()`.

| Code | Last operation entered |
| --- | --- |
| `KQROOT  ` | Return to FAT root, open `/Kit`, or enter `/Kit`. |
| `KQSCAN  ` | Find the next `/Kit` entry. |
| `KQnnnDIR` | Open, enter, return from, or close Kit slot `nnn`. |
| `KQnnnKST` | Open or parse `kitset.kcg` for Kit `nnn`. |
| `KQnnnI0x` | Open or close Instrument member `x` (0–5) for Kit `nnn`. |
| `KQnnnREN` | Rename/sync invalid Kit `nnn`. |

`nnn` is the parsed three-digit Kit slot. Example: `KQ004I03` identifies Instrument member 3 of Kit 004. Format the code in a local eight-byte array only; add no static diagnostic state.

Set detail immediately before a wait-capable call begins. Do not overwrite it during a wait loop. Set the next code only after the next potentially blocking action begins. Do not call `filesystem_bootLoggingArm()` from substeps and do not refresh the deadline after any success.

## Test

1. Build with `DEV_MODE_LOGGING == 1`, `DEV_MODE_DIAGNOSTIC == 0`.
2. Preserve the current logs. Firmware overwrites only `bootlog.bin`; rename or copy it after each failed boot.
3. Cold-boot repeatedly without changing Kit data.
4. On failure, record the exact `KQ...` code and investigate only that named primitive and Kit slot.
5. Build with `DEV_MODE_LOGGING == 0`; confirm no boot-log write, no diagnostic RAM, and unchanged boot behavior.

## Result interpretation

- `KQSCAN  `: root `/Kit` directory iteration.
- `KQnnnDIR`: the named Kit directory open/chdir/close path.
- `KQnnnKST`: `kitset.kcg` read/parse path.
- `KQnnnI0x`: the declared Instrument member path.
- `KQnnnREN`: quarantine rename/sync or collision path.

The detail code identifies the next isolated investigation; it does not itself fix the hang.

## Other quarantine operations

Root Kit quarantine is the only active quarantine traversal. The recursive Bank-tree quarantine helpers are inside `#if 0` and cannot cause these failures. If they are ever re-enabled, apply this same detail-only timeout design first.

`NAMEREPR`, scans, index writers/readers, HCNAMES, and autosave ensure are not quarantine operations, but each still has a coarse ten-second boot code. Do not instrument them in this pass; add detail only after one of them produces a timeout record.

## Deep implementation map and documentation-in-place

This is the complete implementation surface for the first KITQUAR pass. The
plan intentionally changes no AsyncFATFS or SD-driver source: `sdcard_lxr02.c`
has bounded token/write-busy retries whenever it is polled, and the observed
watchdog proves the foreground loop kept returning. The evidence therefore
supports locating the non-advancing high-level wait, not changing low-level
timeouts speculatively.

### A. Detail hook: one private `filesystem.c` change, no public API

| File / location | Required change | Comment text to place with the change |
| --- | --- | --- |
| `Core/Hardware/SD/filesystem.c`, next to `filesystem_bootLoggingArm()` | Add `static void filesystem_bootLoggingSetDetail(const char code[8]);`. With `DEV_MODE_LOGGING`, it copies exactly eight bytes to the existing `fs_boot_logging_code` only if the normal boot logger is active, armed, not timed out, and not in recovery. With logging disabled it is a no-op. | `/* Update the retained substep label without changing the enclosing boot-operation deadline. Inputs: exactly eight bytes and an already armed normal boot logger. Output: only fs_boot_logging_code changes. Why: timeout recovery needs the last wait-capable primitive, while every successful substep must remain inside KITQUAR's original ten-second budget. This helper does not arm, disarm, reset a tick, poll, allocate, call FAT, or write a file. */` |
| `Core/Hardware/SD/filesystem.h`, boot logger block | No declaration or data member. Keep the helper private: a public detail API could be invoked outside an armed boot operation and would make deadline ownership unclear. Optionally clarify the existing public `filesystem_bootLoggingArm()` comment. | `/* Public Arm starts a new operation deadline; private filesystem.c detail capture may change only the retained eight-byte label inside that deadline. */` |

Only `filesystem_bootLoggingArm()` may write `fs_boot_logging_started_tick`.
The new hook must never call Arm. It reuses existing RAM, creates no second log
file, and cannot alter `fs_boot_logging_armed`.

### B. Prevent I/O abort from being misclassified as an invalid Kit

There is a source-proven safety issue independent of the hang hypothesis.
`filesystem_validateCurrentKitBlocking()` returns `0` for both malformed Kit
content and a blocking helper that stopped because the watchdog/FAT-ready check
failed. `filesystem_quarantineKitLibraryBlocking()` treats any zero as proof
that the Kit is invalid and proceeds toward `err...` rename. The result must be
split before a timed-out or no-longer-ready filesystem can mutate user data.

| File / location | Required change | Comment text to place with the change |
| --- | --- | --- |
| `Core/Hardware/SD/filesystem.c`, before `filesystem_validateCurrentKitBlocking()` | Add private `fs_kit_validation_result_t`: `VALID`, `INVALID_CONTENT`, `IO_ABORT`; change the validator’s return type and signature to accept the already parsed `uint16_t kit_slot`. Pass that slot from the numbered-Kit branch. | `/* Keep malformed content distinct from an interrupted FAT operation. The caller supplies the already validated Kit slot so detail logging can name the same object without global diagnostic state. INVALID_CONTENT authorizes err... quarantine; IO_ABORT stops traversal because no parser conclusion was reached. */` |
| `filesystem_validateCurrentKitBlocking()` | Return `INVALID_CONTENT` for parse/finalize failure, overlong line, noncanonical member stem, or a missing `kitset.kcg`/member when `filesystem_blockFsOk()` remains true. Return `IO_ABORT` whenever a block helper stops with `!filesystem_blockFsOk()`. After the byte-reader loop, distinguish normal EOF from `!filesystem_blockFsOk()` before treating the stream as malformed. Return `VALID` only after all six opens and closes succeed. | `/* A ready-filesystem NULL open is a missing required member and therefore a content result. A timeout or lost FAT-ready state is an I/O abort, not evidence that the Kit is malformed. */` |
| `filesystem_quarantineKitLibraryBlocking()` | Use a private traversal result (`OK` / `IO_ABORT`). Rename and restart only for `INVALID_CONTENT`. Propagate root/Kit open, finder, chdir, close, rename, and final root-return aborts as `IO_ABORT`. Retain the existing successful no-`/Kit` case only when FAT remains ready. | `/* Rename only after validation proved an on-card format violation. If the FAT operation aborts, leave the original directory intact and stop; I/O failure is not authorization to quarantine user data. */` |
| `filesystem_createLibraryIndexBlocking()` | Consume the explicit quarantine result. On `IO_ABORT`, preserve the current detail code and return zero before clearing/rebuilding cache or publishing an index. The existing `filesystem_bootLoggingOperationDone()` still follows return from the outer traversal; no substep disarms early. | `/* A failed Kit quarantine cannot be represented as a valid empty library. Stop before cache publication so the caller cannot mistake partial traversal for a completed index. */` |
| `main.c`, stage 4 | Replace `(void)filesystem_createLibraryIndexBlocking(...)` with an explicit result. Retain the current timeout branch first. For a non-timeout zero result, branch to the explicit boot-failure logger below rather than continuing to Scene scan; do not add a new screen or dev mode. | `/* Do not discard the Kit-index result at boot. A non-timeout I/O abort is neither a successful index nor a content quarantine, so later scans must not conceal it. */` |
| `Core/Hardware/SD/filesystem.h`, existing `filesystem_createLibraryIndexBlocking()` contract comment | No API change. Clarify that zero means no trustworthy index was produced, including interrupted Kit quarantine; callers must not treat zero as a successful empty library. | `/* Zero means the blocking pass did not produce a trustworthy cache; in particular, an interrupted Kit-quarantine pass is not a valid empty library. */` |

Do not change the globally used `filesystem_blockRead()` signature. It is also
used by active sample-install code. The validator can inspect `afatfs_feof()`
and `filesystem_blockFsOk()` after its own read loop; widening that helper
would change unrelated behaviour without evidence.

### C. Make an ordinary boot failure durable too

Before this implementation, `filesystem_writeBootTimeoutLogBlocking()` returned
without writing unless `fs_boot_logging_timed_out` was set. That original
timeout-only contract would have hidden the explicit stage-4 zero result above.
The implemented `filesystem_writeBootFailureLogBlocking()` reuses its bounded
dirty-abandon/remount/write sequence as a DEV_MODE_LOGGING-only **boot failure**
writer; it adds neither a second file nor a persistent logger flag.

| File / location | Required change | Comment text to place with the change |
| --- | --- | --- |
| `Core/Hardware/SD/filesystem.c`, current `filesystem_writeBootTimeoutLogBlocking()` | Generalize/rename it to `filesystem_writeBootFailureLogBlocking()`. Permit the call while the boot logging window is active from a confirmed timeout or explicit boot failure. It writes the already retained detail code using the same single `/bootlog.bin`, bounded recovery/remount, and no-retry behaviour. | `/* Make one bounded best-effort boot-failure record. Inputs: an already retained operation/detail code and a caller-confirmed boot failure. Output: the existing eight-byte bootlog after dirty recovery/remount when possible. Why: an ordinary failed boot pass must not be silently acknowledged merely because the watchdog did not expire. This DEV_MODE_LOGGING path never gates continuation. */` |
| `Core/Hardware/SD/filesystem.h` | Replace the public timeout-writer declaration/comment with `filesystem_writeBootFailureLogBlocking()`. It remains boot/main-context only. | `/* Write the retained boot failure code after either watchdog timeout or a caller-confirmed boot filesystem failure; recovery is bounded and never gates startup. */` |
| `main.c`, timeout cleanup and new `boot_filesystem_failure` label | Let timeout and explicit non-timeout failure share the existing observer/Preset cleanup, then call `filesystem_writeBootFailureLogBlocking()`. Do not call it from a successful boot path. | `/* Both timeout and confirmed ordinary boot filesystem failure terminate the storage ladder. Preserve the retained code and make one bounded DEV_MODE_LOGGING record; recovery failure never blocks startup. */` |

This uses no new SRAM: the caller already has a control-flow branch and the
logger already owns the code/recovery fields. The dirty remount is justified
only after boot storage has already failed, and it remains compiled out with
`DEV_MODE_LOGGING == 0`.

### D. Exact diagnostic call sites

Use a stack-local eight-byte formatter (or equivalent private formatter with
no static storage, allocation, I/O, `sprintf`, or clock access) in the two Kit
functions. Set the detail once immediately before the wait-capable primitive;
never at the top of a polling loop.

| Source function / branch | Code | Set before |
| --- | --- | --- |
| `filesystem_quarantineKitLibraryBlocking()` entry, root-open branch, final return | `KQROOT  ` | `filesystem_blockChdir(NULL)`, opening/entering `/Kit`, root close, and final root return |
| root enumeration loop | `KQSCAN  ` | each `filesystem_blockFindNextObject(root, ...)` |
| numbered-Kit branch | `KQnnnDIR` | named Kit open, enter, return to `/Kit`, and Kit-directory close |
| `filesystem_validateCurrentKitBlocking()` | `KQnnnKST` | `kitset.kcg` open, stream/parse, and close |
| validator member loop | `KQnnnI0x` | member `x` open and close, `x` = 0–5 |
| invalid-content branch | `KQnnnREN` | return/close before rename and `filesystem_blockRename()` (including its required sync) |

`KQnnnDIR` deliberately combines directory open/chdir/close for one concrete
Kit. The record has only eight bytes; it still separates that Kit and directory
family from root iteration, parser/member I/O, and mutation/sync.

## Non-masking review gate

The implementation is acceptable only if all of these remain true:

- A detail call changes no status, callback, operation, phase, timer, cache,
  or filesystem ownership field.
- `IO_ABORT` cannot reach `filesystem_blockRename()`.
- Existing malformed-content cases still quarantine; logging must not turn bad
  content into a valid load.
- Any close failure that decides continuation remains a failure. Cleanup
  `(void)` closes may not be recast as successful validation.
- The stage-4 caller consumes both timeout and non-timeout failure; neither is
  silently hidden by a `(void)` cast.
- The boot failure writer runs only after a confirmed failure and writes the
  retained detail label; an ordinary failure is not silently acknowledged just
  because the watchdog did not expire.
- `DEV_MODE_LOGGING == 0` removes detail code/state/file I/O completely.

## Test sequence and evidence threshold

1. Build with logging on and diagnostics off. Verify a normal cold boot and
   valid Kit set leave card content unchanged.
2. Separately test malformed Kitset text, an overlong member stem, and a
   missing declared member. Each must still quarantine only that Kit.
3. Use a disposable fixture to test an interrupted FAT operation if possible;
   it must not rename the current Kit or publish a partial index.
4. Preserve any new `bootlog.bin` before the next failure. A repeated
   `KQ...` code, not this plan alone, is the evidence required before changing
   the named AsyncFATFS/SD primitive.
5. Rebuild with `DEV_MODE_LOGGING == 0` and confirm unchanged boot behaviour
   and no diagnostic allocation or output.

## Implementation notes

- Implemented the private non-rearming detail hook and stack-only Kit formatter
  in `filesystem.c`; no diagnostic buffer or public detail API was added.
- Implemented explicit `VALID` / `INVALID_CONTENT` / `IO_ABORT` validation and
  prevents `IO_ABORT` from reaching quarantine rename.
- Implemented the shared bounded `filesystem_writeBootFailureLogBlocking()`
  path and changed the Kit boot caller to use it for non-timeout failure.
- Firmware build passed with the project’s existing unrelated unused-function
  and newlib syscall linker warnings. A forced `DEV_MODE_LOGGING == 0` build
  also passed with BSS reduced from 78,476 to 78,444 bytes, confirming no
  persistent Kit diagnostic allocation remains in that configuration. Hardware
  verification remains required: valid, malformed, and interrupted-FAT Kit
  fixtures, plus a reproduced `KQ...` timeout record if the original failure
  recurs.
