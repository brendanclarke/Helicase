# Session 057 Bank Save stall: cause and precise fix

**Status: CLOSED -- hardware accepted 2026-08-28.**

## Verdict

The reported Bank Save was not stalling because an instrument serializer emitted
32 KiB, because the AsyncFATFS handle pool was exhausted, or because the FAT
cache/SRAM was too small. The pre-fix code terminated it through
`FS_BANK_TOTAL_TICK_LIMIT` after almost exactly 39.3 seconds.

The limit was expressed in calls to `filesystem_tick()`, but its comments and
chosen value treated those calls as if they were milliseconds. On the tested
firmware the foreground loop calls `filesystem_tick()` about 7,600 times per
second. Consequently:

```
300,000 foreground polls / approximately 7,600 polls per second
    = approximately 39.5 seconds
```

That is too short for a 16-scene Bank Save, which is currently taking roughly
7.7--8.3 seconds per scene. The watchdog therefore always interrupts the fifth
scene at an arbitrary asynchronous filesystem phase. The 32,768-byte all-`FF`
snare file is an effect of that interruption, not its cause.

The implemented targeted fix is:

1. Remove the poll-count hard abort from Bank Save and the adjacent Bank Load
   case that uses the same invalid time base.
2. Keep the existing Bank/Scene phase observers trace-only: they must never call
   `filesystem_finish(ERROR)` while asynchronous callbacks may remain live.
3. Restore `AFATFS_MAX_OPEN_FILES` from the diagnostic value 8 to its original
   value 5. No cache or SRAM expansion is necessary; this removes 564 bytes of
   handle payload.

No replacement duration counter or other diagnostic layer is part of this fix.

The hardware acceptance run completed a full 16-Scene save to Bank slot 019.
The user-observed wall time was approximately two and a half minutes, and the
final trace segment reached `Bank FINISH slot=19 FAIL=0` after all sixteen child
create results. The copied SD result contains the complete, normally sized Bank
tree with none of the provisional/corrupt file signatures from the pre-fix
tests. The correctness defect is therefore closed. Save performance remains
unacceptably slow, but it is separate Session 058 work documented in
`S058_BANK_LOAD_SPEEDUP_PROPOSAL.md`; it is not evidence that the Session 057
stall fix is incomplete.

## Implementation record

The code change was made in Session 057 as follows:

- `Core/Hardware/SD/filesystem.c`: deleted `FS_BANK_TOTAL_TICK_LIMIT`,
  `op_bank_total_ticks`, both request-time counter resets, and both dispatch-time
  hard-abort blocks. Bank Load and Bank Save now remain owned by their native
  state machines through completion and the normal final sync.
- `Core/Hardware/SD/filesystem.c`: removed the `BkSt` and `ScSv`
  `filesystem_finish(ERROR)` side effects. The already-present edge-triggered
  trace observers remain unchanged in number and allocation; they now only
  observe, as their surrounding comments claim.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`: restored
  `AFATFS_MAX_OPEN_FILES` from 8 to 5, with an adjacent comment recording the
  measured zero-between-children/one-at-create ownership proof and the exact
  564-byte handle-payload recovery. Together with the removed four-byte Bank
  counter and linker alignment, the verified ELF BSS reduction is 576 bytes.
- No wall-clock timeout, counter, cancellation shim, cache, handle, or other
  diagnostic state was added.

Each affected dispatch/observer/pool site now has an adjacent comment stating
both the behavior and the ownership reason it must remain this way. Build and
static verification results are recorded below after the implementation notes.

## Evidence that the watchdog, not scene data, stops the save

In the pre-fix code, `filesystem_tick()` incremented `op_bank_total_ticks` once
on every foreground pass while `FS_INTERNAL_OP_SAVE_BANK` was active. At
300,000 increments it emitted `BkTo` and called `filesystem_finish(ERROR)`.

Four independent tests terminated at the same elapsed wall time:

| Bank | Request tick | `BkTo` tick | Elapsed |
|---|---:|---:|---:|
| 009 | 1,781 | 41,132 | 39.351 s |
| 017 | 16,098 | 55,198 | 39.100 s |
| 016 | 20,204 | 59,489 | 39.285 s |
| 019, latest | 2,045 | 41,526 | 39.481 s |

The trace tick used in this table is the 1 kHz `time_sysTick`. None of these
intervals spans more than one 16-bit wrap. The resulting foreground-loop rate is
about 7,600--7,670 calls per second, fully explaining the nominal 300,000 limit.
The near-identical cutoff across different banks is not consistent with a
content-dependent serializer hang, SD-space problem, cache limit, or handle
leak.

The latest Bank 019 trace also shows normal forward progress up to the cutoff:

| Event | Time | Time since prior event |
|---|---:|---:|
| Bank request | 2.045 s | -- |
| child index 0 `CREATE_RESULT` | 5.606 s | 3.561 s |
| child index 1 `CREATE_RESULT` | 13.915 s | 8.309 s |
| child index 2 `CREATE_RESULT` | 21.725 s | 7.810 s |
| child index 3 `CREATE_RESULT` | 29.479 s | 7.754 s |
| child index 4 `CREATE_RESULT` | 37.178 s | 7.699 s |
| `BkTo` | 41.526 s | 4.348 s |

The encoded `CREATE_RESULT` values are `0x4013`, `0x4413`, `0x4813`,
`0x4c13`, and `0x5013`: child indices 0 through 4, with exactly one open
handle at each result. The separate Bank-handle census found zero handles at
each phase-20 child boundary. The cursor advances, the child cadence is stable
or improving, and the only open handle is the directory handle just created.

Thus the trace does **not** show that "child 5 gets enormously slow." More
precisely, it shows the fifth child (zero-based index 4) in phase 24 when the
unrelated 39.5-second limit expires. Phase 24 is where the state machine happens
to be waiting for an instrument-file open callback; it has been in that child's
normal work for only 4.348 seconds. At the observed cadence, 16 scenes alone
need approximately 125--135 seconds, before final bank-name/index work and the
required final sync. A 39-second hard limit cannot complete this workload.

## Why the 32,768-byte snare is not an oversized serialization

The anomalous file is:

```
SD_CARD_BANK_SV_TEST/Bank/019 Full/04 Slak/Kit Emott/emotts14.snr
```

It is exactly 32,768 bytes and every byte is `0xFF`. The corresponding source
scene file is 1,332 bytes and contains the expected text serialization. A real
oversized serializer would produce serialized text or repeated data, not one
erased cluster of `FF` bytes.

The exact size follows from AsyncFATFS operation:

- During extension, `AFATFS_SAVE_DIRECTORY_NORMAL` publishes the file's
  *physical* size. On this volume the first allocated cluster is 32,768 bytes.
- The actual logical byte count is written to the directory entry only during
  the close path with `AFATFS_SAVE_DIRECTORY_FOR_CLOSE`.
- Bank Save is force-finished while that asynchronous sequence is still in
  progress. Its normal close-time size correction and final durability sync do
  not complete.

The file is therefore a provisional one-cluster directory entry whose payload
and final logical size never became durable. It is a highly characteristic
footprint of interruption at the current watchdog boundary.

There is stronger evidence that the present abort is unsafe, not merely early.
This prior artifact:

```
SD_CARD_POST_TEST_2/Bank/016 Filltern/04 Slak/Kit Emott/emottd33.drm
```

is 512 bytes containing 64 valid `AutosaveTrace` records rather than instrument
text; that trace batch is absent from the intended trace file. The most direct
explanation is callback/global-handle cross-talk after Bank Save publishes an
error while an old asynchronous file-open operation is still alive:

1. the Bank watchdog calls `filesystem_finish(ERROR)` from an arbitrary phase;
2. the facade returns to idle without cancelling or draining the lower-level
   open/close work;
3. trace flushing begins and uses the shared global `op_file`;
4. the abandoned Bank callback later writes that same global `op_file`;
5. trace bytes are consequently directed into the orphan Bank instrument.

This matches `on_file_opened()`, which assigns the shared `op_file` and
`op_file_ready` globals, and it explains both the misplaced trace block and the
partially published 32 KiB file. Regardless of which callback wins a particular
race, `filesystem_finish(ERROR)` is not a valid cancellation operation: it does
not close the in-flight object, wait for callbacks, return to the root, or sync
dirty FAT state. The normal DONE path requests a final flush; the ERROR path
does not.

## Targeted code change

### 1. Remove the current Bank total hard abort

In `Core/Hardware/SD/filesystem.c`, the implementation removes the Bank Save
timeout block in `filesystem_tick()` that incremented `op_bank_total_ticks` and
called `filesystem_finish(ERROR)` at `FS_BANK_TOTAL_TICK_LIMIT`. The adjacent
Bank Load instance of the same broken time-base logic is removed at the same
time even though it did not terminate this Save test. Neither instance remains
as a terminal poll-count check.

The misleading `FS_BANK_TOTAL_TICK_LIMIT` definition, counter, resets, and
poll-duration comments are also removed. A foreground-pass count is workload-
and build-dependent and must never be treated as elapsed milliseconds.

For the smallest safe Session 057 fix, the Bank operation should simply be
allowed to reach its existing normal completion, which already performs the
required final flush before reporting DONE.

### 2. Do not replace the timeout without real cancellation

This implementation deliberately adds no wall-clock replacement or other
diagnostic layer. A real terminal timeout would first require a Bank-owned
cleanup state machine that can:

1. resolve or drain any outstanding AsyncFATFS callback;
2. close any file or directory handle owned by the Bank operation;
3. return AsyncFATFS to the root directory;
4. call and complete `afatfs_sync()`; and only then
5. publish ERROR and release the filesystem facade.

AsyncFATFS currently exposes no general cancellation primitive that makes an
arbitrary phase safe. Until such cleanup exists, continuing the operation is
safer than declaring it finished while its callbacks and handles remain live.

The existing per-phase `BkSt`/`ScSv` observers were therefore made trace-only.
They are not the cause of the repeatable 39-second cutoff, but removing their
blind abort side effect closes the same corruption path without adding any
observer, state, or allocation.

### 3. Restore the AsyncFATFS handle pool to five

The implementation changes:

```c
#define AFATFS_MAX_OPEN_FILES 8
```

back to:

```c
#define AFATFS_MAX_OPEN_FILES 5
```

The expansion was a useful diagnostic but did not alter the failure. The trace
now rules out handle exhaustion: handle count is zero between children and one
immediately after each child-directory creation. A target-ABI debug compile
reports the current `sizeof(afatfsFile_t)` as 188 bytes; the older 328-byte
source comment predated moving the expanded delete state out of every handle.
Removing the three diagnostic slots therefore removes 564 bytes of handle
payload. The full linked change, including deletion of `op_bank_total_ticks`
and alignment, reduces BSS from 95,424 to 94,848 bytes: 576 bytes total.

The `afatfs_countOpenHandles()` census helper can remain if useful because it
does not allocate retained SRAM. Any comment suggesting one leaked handle per
child should be removed or updated; the measured data disproves it.

## SRAM/cache decision

**No SRAM or AsyncFATFS cache expansion is required.**

There is no allocation-exhaustion signature, no rising handle count, and no
progressive slowdown. Enlarging the file pool had zero effect. Enlarging the
sector cache cannot change a foreground-loop deadline and could leave more dirty
data exposed when an unsafe abort occurs. The appropriate SRAM change is the
opposite: restore the five-handle pool, remove 564 bytes of handle payload, and
reduce final linked BSS by 576 bytes. No replacement timer state was added.

This preserves the project's normal-SRAM reservation for future Pattern work.

## Implementation verification

The final source was verified locally after the edits:

- `make -j4` rebuilt both changed translation units, linked
  `build/lxr02.elf`, and completed successfully. The compiler emitted only the
  repository's existing unrelated unused-function/unused-parameter warnings;
  the changed Bank timeout/observer code emitted no warning.
- `make img` successfully produced a 380,844-byte binary payload and the
  packaged 380,860-byte `build/LXRV2_lxr02.img`.
- `arm-none-eabi-size build/lxr02.elf` reports text 380,436, data 408, and BSS
  94,848 bytes. The pre-fix logging build reported text 380,556, data 408, and
  BSS 95,424, so the implementation removes 120 bytes of linked text and 576
  bytes of linked BSS.
- A target-ABI `-O0 -g3` compile plus GDB type inspection reports
  `sizeof(afatfsFile_t) == 188`; this is why the stale 328-byte source comment
  was corrected as part of the implementation.
- Static searches find no remaining `FS_BANK_TOTAL_TICK_LIMIT`,
  `op_bank_total_ticks`, `BkTo`, `BkSt`, or `ScSv` code in `filesystem.c`.
  Their occurrences in this document are historical descriptions only.
- `git diff --check` passes for both modified C files.

The local checks above were subsequently followed by the successful hardware
acceptance run recorded below. No further Session 057 implementation work is
required.

## Hardware acceptance result and closeout

The user tested the fixed image on hardware and reported that Bank Save
completed successfully in approximately two and a half minutes. The resulting
card copy is retained in `SD_CARD_BANK_STALL_TEST/`. Inspection identifies the
completed target as `Bank/019 LoadTst`:

- `settings.tmp` selects `active_bank=19`, root `.hcnames` associates `LoadTst`
  with direct Bank source 019, and `/Bank/.hcindex` contains the slot-019 name;
- the Bank has exactly 16 child Scene directories, numbered 00 through 15;
- every child contains three direct files (`sceneset.scg`, `pattern.pat`, and
  `effects.fx`), one embedded Kit directory, and seven Kit files;
- the resulting Bank contains the expected 161 data files: one `bankset.bcg`,
  16 Scene settings, 16 Patterns, 16 Effects, 16 Kit settings, and 96
  Instruments;
- every Instrument is normal serialized text size, from 1,314 through 1,378
  bytes. There are no 0-byte, 512-byte, or 32,768-byte files anywhere in the
  completed Bank; and
- sampled snare files contain normal `format=helicase.instrument`, version,
  type, and parameter text rather than erased-cluster bytes or trace records.

The final `asavetrc.bin` segment supplies the state-machine confirmation. It
records the Bank slot-019 request, then sixteen successful Scene
`CREATE_RESULT` records with values `0x4013`, `0x4413`, ... through `0x7c13`.
Those values advance monotonically through child indices 0--15 and retain
`FAIL=0`. After the final child, the trace records `Bank SOURCE_STAGED slot=19
FAIL=0` and `Bank FINISH slot=19 FAIL=0`. There is no phase-stall or error record
between that request and finish.

Accounting for two 16-bit tick wraps, the trace interval from the recorded Bank
request at tick 22,252 to the Bank FINISH marker is approximately 137.255
seconds. Final sync/index/UI completion accounts for the difference from the
user's approximately 2.5-minute wall-clock observation. This is fully
consistent with the pre-fix estimate and, critically, continues far beyond the
old repeatable 39.3-second false cutoff.

The card copy proves that the completed filesystem tree is host-readable and
that the interrupted-file signatures are gone. A separate power-cycle firmware
reload was not explicitly reported with this result, so this closeout does not
claim that additional action occurred. It is retained as an ordinary Bank Load
regression check for subsequent storage work, not as an open condition on the
now-confirmed Bank Save stall correction.

### Final disposition

- Root cause: incorrect foreground-poll watchdog interpreted as elapsed time.
- Fix: remove unsafe Bank hard aborts, keep existing phase observation
  non-terminal, and restore the five-handle AsyncFATFS pool.
- Hardware result: successful complete Bank Save with all 16 children and no
  corrupt/provisional output files.
- SRAM result: no expansion required; linked BSS was reduced by 576 bytes.
- Remaining issue: approximately 2.5-minute Bank Save latency, explicitly
  deferred to Session 058 speed work.

Session 057 Bank Save stall work is complete and closed.

## Changes that would not fix this issue

- Increasing `FS_BANK_TOTAL_TICK_LIMIT` while it remains a poll count only moves
  the cutoff unpredictably between builds and foreground workloads.
- Increasing `AFATFS_MAX_OPEN_FILES` hides neither a callback race nor the false
  deadline and spends reserved SRAM without evidence.
- Increasing the FAT sector cache does not affect the incorrect time base.
- Special-casing `.snr` size or reducing serialized instrument content attacks
  an interruption artifact; the serializer is producing the expected bounded
  text.
- Calling `filesystem_finish(ERROR)` at another phase without draining the
  lower layer can reproduce the orphan-file and misplaced-write corruption.

The minimal, precise Session 057 correction is therefore to remove the false
Bank hard timeout, let the existing asynchronous state machine and final sync
complete, and undo the now-disproved handle-pool expansion.
