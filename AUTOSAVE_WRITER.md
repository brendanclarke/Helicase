# Autosave Writer — dummy-counter implementation plan

## Scope of this implementation

This milestone adds one autonomous, background-only writer. Every successful
transaction copies the newest valid autosave record to the other file and
changes only two header fields:

| Header range | Field | Change on a successful transaction |
| --- | --- | --- |
| `8..11` | generation (`uint32_t`, little-endian) | increment by one; this selects the ping-pong winner |
| `16` | probe counter (`uint8_t`) | increment modulo 256; visible dummy-write evidence only |

The mutation mask and every payload byte are copied unchanged. There is no
dirty ledger, parameter serialization, overlay/apply reader, menu action,
runtime audio/DSP mutation, or cache-mask lease in this milestone.

The writer must never block. It starts and advances only in the existing
foreground `filesystem_tick()` path, which is already called once per main-loop
pass after the normal Menu/Preset service. It must not add a loop that waits
for an AsyncFATFS callback, SD sector, seek, close, or flush after audio has
started.

## Current-code facts this plan relies on

This plan is based on the current source, not an earlier autosave draft:

- `filesystem.c` is the sole caller of `afatfs_poll()` and owns the one
  `status` / `current_op` operation slot. `filesystem_start()` returns false
  while `FS_STATUS_BUSY`; public load/save requests rely on that behavior.
- `filesystem_tick()` polls AsyncFATFS first. It currently returns immediately
  unless `status == FS_STATUS_BUSY`, then dispatches `current_op` through one
  switch. The autosave scheduler must run only in the `FS_STATUS_IDLE` branch;
  it must not consume a Menu/Preset `DONE` or `ERROR` state that still awaits
  that caller's `filesystem_ack()`.
- `filesystem_finish(FS_STATUS_DONE)` changes the operation to
  `FS_INTERNAL_OP_FLUSH_FINISH`; `filesystem_flushFinish_tick()` repeatedly
  calls `afatfs_sync()` until data, FAT, directory, and pending sector writes
  are durable, then calls `filesystem_complete()` and its callback.
- `staging_buf[512]` is the only shared bulk buffer. AsyncFATFS reads/writes
  may accept fewer bytes than requested, so all byte progress must advance by
  the returned count, never by requested size.
- `afatfs_fseek()` can return SUCCESS, IN_PROGRESS, or FAILURE. There is no
  caller callback for this public seek API; a state must wait for `afatfs_ftell`
  to report the requested cursor before writing the final commit byte.
- The current `Autosave.c` defines a 23,248-byte record, full-record CRC32C,
  a deterministic initial formatter, and an in-memory test validator. It has
  no streaming validation API, no probe-byte symbol, no header transformer,
  and no generation comparison helper.
- `menu.h` already exports `menu_activePage`; the `PageNames` enum already
  exposes `LOAD_PAGE` and `SAVE_PAGE`. `filesystem.c` already includes
  `menu.h`. No new Menu predicate, Menu state, or Menu source change is needed
  for the dummy-writer gate.
- `fs_stage_workspace` is an existing 2,048-byte union used only by the one
  serialized filesystem operation. Generic `filesystem_start()` deliberately
  does not clear it. Add a typed autosave-writer member to that union and
  initialize it only when this operation starts; this provides operation state
  without a second record or mutation-mask allocation.
- `fs_list_cache_name[1000][9]` is the existing 9,000-byte library/HCNAMES
  cache. The normal dummy write does not borrow it. Only the no-valid-record
  recovery path temporarily uses it to read HCNAMES and rebuild baselines.

## On-card control-header addition

Add this named layout constant in `Autosave.h`:

```c
#define AUTOSAVE_HEADER_PROBE_COUNTER_OFFSET 16u
```

Byte 16 is already inside the reserved, CRC-covered `16..63` header range.
Existing fixtures already contain zero there and their CRC definition already
covers it, so defining the field does not by itself alter either file. The
initial formatter must explicitly document that its zero-filled header gives a
new baseline a probe value of zero.

The existing CRC rule remains exactly:

- Calculate CRC32C over all 23,248 record bytes.
- Treat the stored-CRC field `12..15` as zero during calculation.
- Include magic, format version, commit marker, generation, probe counter,
  mask, payload, and all remaining padding.

`/.hcprms1` starts at generation 1/probe 0 and `/.hcprms2` at generation
0/probe 0. A normal first writer transaction therefore makes B generation
2/probe 1; then A becomes generation 3/probe 2, and so on.

## Cadence, ownership, and terminal behavior

Add the one tuning setting in `config.h` beside other millisecond constants:

```c
#define AUTOSAVE_WRITER_INTERVAL_MS 5000u
```

The setting must be documented as an inter-transaction minimum, not a promise
of a write at an exact wall-clock instant. `time_sysTick` is a 1 kHz wrapping
`uint16_t`; use unsigned subtraction and keep this interval below half of its
wrap range.

### Exact scheduler placement

Add a private `filesystem_autosaveWriterSchedule_tick()` in `filesystem.c`.
At the existing `if (status != FS_STATUS_BUSY) return;` boundary in
`filesystem_tick()`, replace the unconditional return with this behavior:

1. If status is `FS_STATUS_IDLE`, call the scheduler once.
2. The scheduler may call private `filesystem_start()` for its own internal
   operation, but only when all eligibility rules pass.
3. If status is still not `FS_STATUS_BUSY`, return as it does today.
4. Otherwise enter the existing switch and dispatch the writer in the same
   tick. Its first phase may only initialize/open; it may not spin.

The scheduler starts an operation only when:

1. `bank_hasResidentBank()` is true.
2. `menu_activePage` is neither `LOAD_PAGE` nor `SAVE_PAGE`.
3. status is `FS_STATUS_IDLE` and AsyncFATFS has reached its normal ready
   state through the polling already performed at the start of the tick.
4. The due interval has elapsed.

When the interval expires while a normal request or either Load/Save page owns
the system, no request is queued and no missed interval count is retained. The
due state remains eligible, so one writer begins when the owner is gone and
the facade becomes idle. The existing Menu request APIs return failure while a
writer is busy; this milestone preserves that current behavior rather than
adding a Menu request queue or preempting an in-flight commit.

### Scheduler state and completion callback

Add the smallest explicit persistent scheduler state in `filesystem.c`:

- a `uint16_t` next-due tick; and
- a `uint8_t` armed flag.

These three bytes are necessary because neither `fs_last_idle_poll_tick` nor
the operation-local workspace can correctly retain cadence across idle ticks.
Do not reuse `fs_last_idle_poll_tick`: it controls the existing 5 ms idle
AsyncFATFS poll policy. Record the exact BSS delta in the implementation note
and build result.

Start the internal operation with a private completion callback. At callback
time `filesystem_complete()` has already set terminal status and cleared
`current_op`, so the callback shall:

1. set next due to `time_sysTick + AUTOSAVE_WRITER_INTERVAL_MS`;
2. leave no queue or dirty state behind; and
3. call `filesystem_ack()` only for this autonomous terminal result.

This keeps `DONE`/`ERROR` from becoming visible as a stale terminal status to
unrelated Menu/Preset code. An autosave error retries no sooner than the next
interval and never overwrites the record selected valid during that failed
attempt.

## Operation-local state and memory ownership

Define a small `filesystem_autosave_writer_state_t` before
`filesystem_stage_workspace_t`, then add it as a typed union member alongside
`kit_stage`, `instrument_stage`, and `scene_stage`. It must be no larger than
the existing `FS_STAGE_CACHE_BYTES` 2,048-byte workspace and must contain only
the scalar/pointer state needed while this one operation owns the facade:

- the current `autosave_stream_validation_t`;
- an AsyncFATFS pointer for the concurrently-open target during copy-forward;
- selected winner index, generation, and probe;
- calculated target CRC32C;
- read/write chunk offsets and partial-write progress;
- seek/close/sync subphase latches needed by this state machine.

`op_file` remains the primary open file: candidate reader, winner reader, or
the reopened target commit file depending on phase. The typed stage member
holds the second file pointer only during the source-read/target-write copy
phase. The generic request initializer already resets `op_file`, byte counters,
open/close latches, stream index, item offset, and line-write counters; the
writer phase zero must explicitly initialize its stage member because generic
request setup intentionally preserves the stage workspace.

Do not borrow `fs_list_cache_name` for the ordinary dummy writer, do not add a
23,248-byte image, and do not add a 2,576-byte mask buffer. The only permanent
BSS increase in this milestone is the three-byte cadence state (subject to
normal compiler alignment, which the build measurement records).

## Autosave module changes

### `Core/Bank/Scene/Autosave.h`

1. Add the probe-counter offset at byte 16, with a comment that it is a
   diagnostic write witness rather than a winner selector.
2. Define a caller-owned streaming validation structure. It contains the
   running CRC state, record byte count, parsed stored CRC, parsed generation,
   parsed probe value, parsed Bank name, and header-valid/error flags—never a
   record buffer.
3. Declare `begin`, `update`, and `finish` validation functions. `update`
   receives an absolute record offset and the accepted byte range so callers
   can process partial AsyncFATFS reads correctly.
4. Declare a wrap-safe generation comparison helper. Equal generations remain
   a caller-level tie resolved in favor of Record A.
5. Declare an in-place-safe chunk transformation helper. It receives source
   bytes, absolute offset, next generation, next probe, final CRC, and desired
   commit byte, and overwrites only header fields that intersect the chunk.
6. Retain `autosave_validateRecord()` as the whole-image test helper, but make
   it use the same begin/update/finish logic so test and streaming rules cannot
   diverge.

Every new declaration receives a code-adjacent contract comment naming its
inputs, outputs, CRC treatment, and storage ownership.

### `Core/Bank/Scene/Autosave.c`

1. Refactor the existing private CRC32C update primitive into the streaming
   implementation; it stays table-free and creates no static workspace.
2. Parse header/payload identity cells while chunks pass through `update`:
   magic `HCPR`, version 1, commit byte, little-endian stored CRC, generation,
   probe, and the eight zero-padded bytes at `AUTOSAVE_BANK_NAME_OFFSET`.
   Reject a noncontiguous/out-of-range stream, a short/long final byte count,
   invalid magic/version/commit, CRC mismatch, or a stored Bank name that does
   not equal the current `bank_displayName()`.
3. When updating CRC, substitute zero only at absolute offsets `12..15`.
4. Implement the signed-difference, wrap-safe generation comparator and
   document its two-record/less-than-half-range precondition.
5. Implement the transformer with no record reconstruction. It copies its
   input range in place and substitutes only generation, probe, CRC bytes, and
   commit marker where those header offsets intersect the range. It leaves mask
   and payload byte-for-byte untouched.
6. Update the initial-byte formatter comment to state probe zero explicitly.

No filesystem include, timer, dirty flag, cache ownership, or static/global
writer state belongs in this module.

## Filesystem writer changes

### Operation and diagnostics

1. Add `FS_INTERNAL_OP_AUTOSAVE_DUMMY_WRITE` immediately beside the existing
   autosave creation operation, with a comment distinguishing runtime rewrite
   from boot-only create-if-missing behavior.
2. Add its state-machine prototype and dispatcher case in `filesystem_tick()`.
3. Add its error-prefix case to `filesystem_errorPrefix()` so a failed
   background operation does not report the generic `Fs` prefix.
4. Add the scheduler and private completion callback described above. Neither
   appears in `filesystem.h`; no external caller starts autosave work.

### Normal transaction: select, transform, commit

The private `filesystem_autosaveDummyWrite_tick()` uses these explicit phases.
Every open waits for `on_file_opened`, every close waits for
`on_file_closed`, and every read/write retries only the unaccepted range on a
later `filesystem_tick()`.

#### A. Validate A then B by streaming

For target index 0 (`/.hcprms1`) and then 1 (`/.hcprms2`):

1. Return to root and open its exact LFN in read mode. Failure makes only that
   candidate invalid; it is not a writer error yet.
2. Clear the stage validation state and set byte count/offset to zero.
3. Read up to `sizeof(staging_buf)` bytes. Feed only accepted bytes to
   `autosave_streamValidationUpdate()`, advance by the returned read count,
   and retry later when AsyncFATFS returns zero before EOF.
4. At EOF, finalize validation and compare its captured zero-padded Bank name
   with the current `bank_displayName()`. The candidate is valid only at
   exactly `AUTOSAVE_RECORD_BYTES` with valid header, CRC32C, and Bank name.
5. Save the best valid candidate's index/generation/probe in the stage member.
   Replace it only if the new generation is wrap-safely newer; equal generation
   keeps A because it was examined first.

The reader never applies a byte to BankData/SceneData. The Bank-name gate is
the current format's active-Bank identity because the Bank slot was
intentionally removed. If one record validates for that Bank, the other is the
safe rewrite target even if it is malformed, missing, or belongs to another
Bank.

#### B. No valid record: flush and regenerate both

If neither candidate is valid, do not return an autosave error. Perform the
requested recovery sequence instead:

1. Read `/.hcnames` into the existing cache with the current
   `filesystem_prepareResidentNamesCache()`, `filesystem_readTextLine()`, and
   `filesystem_cacheResidentName()` helpers. A missing/unreadable HCNAMES is
   a recovery error; write neither register before that failure.
2. Overwrite `/.hcprms2` with the existing `autosave_formatInitialChunk()`
   stream at generation 0/probe 0, close it, and wait directly in this private
   state machine until `afatfs_sync()` succeeds.
3. Overwrite `/.hcprms1` with the same formatter at generation 1/probe 0,
   close it, and finish through the ordinary final `filesystem_finish(DONE)`
   flush gate. Thus B becomes durable before A is made the current baseline.
4. Clear/invalidate the temporary HCNAMES cache before terminal completion.
   The current code treats this cache as a temporary cache domain; no Load/Save
   page was allowed to own it while recovery ran.

This recovery intentionally does not call
`filesystem_ensureAutosaveFilesBlocking()`: that function pumps synchronously
and preserves existing targets, while recovery must remain asynchronous and
overwrite invalid targets. It also does not reuse the ordinary copy-forward
path because there is no valid source to copy.

#### C. Calculate final target CRC from the selected winner

If a winner exists, reopen it read-only and stream it through `staging_buf`
once. Transform each accepted chunk in place using:

```text
next_generation = winner_generation + 1
next_probe      = winner_probe + 1 (uint8 wrap)
CRC field        = zero while calculating
commit byte      = 0xA5 while calculating
```

Feed transformed bytes into a CRC accumulator. This produces the complete
new-record CRC before the inactive target is opened for truncating write. It
uses no HCNAMES cache and preserves all source mask/payload bytes.

#### D. Copy winner to inactive target with invalid commit

1. Reopen the winner for read and the other record for `"w"` write/truncate;
   use `op_file` for the reader and the stage's second handle for the writer.
2. For each source chunk, transform in place with next generation/probe and
   final CRC but with commit byte `0x00`, then feed `afatfs_fwrite()` until that
   full transformed chunk is accepted. Do not read the next source chunk until
   the corresponding output chunk is completely accepted.
3. Close reader, then close writer. The selected winner is untouched for the
   entire process; only the inactive record has been truncated/replaced.
4. In a private `WAIT_INVALID_SYNC` phase, call `afatfs_sync()` once per tick
   until it returns true. This is an intermediate durability barrier, not
   `filesystem_finish()`, so it must not publish DONE or invoke the callback.

#### E. Write the commit marker last

1. Open the new inactive target as `"r+"`; this existing AsyncFATFS mode gives
   read/write access without truncation.
2. Call `afatfs_fseek(file, AUTOSAVE_HEADER_COMMIT_OFFSET, AFATFS_SEEK_SET)`.
   On IN_PROGRESS, wait across ticks until `afatfs_ftell()` reports that exact
   position; on FAILURE, terminate the operation as an error with no retry in
   the same transaction.
3. Set `staging_buf[0]` to `AUTOSAVE_HEADER_COMMIT_VALID` and write exactly
   one byte, retrying only until AsyncFATFS accepts it.
4. Close the file and call `filesystem_finish(FS_STATUS_DONE)`. The existing
   `FS_INTERNAL_OP_FLUSH_FINISH` path provides the final flush and invokes the
   scheduler callback.

A power loss before phase E leaves an invalid commit marker, so validation
selects the old winner. A loss after the final byte but before final flush is
still protected by CRC and AsyncFATFS's final durability boundary; this must
be hardware-tested rather than assumed.

### Existing boot creator

`filesystem_ensureAutosaveFiles_tick()` remains create-only and continues to
leave existing records untouched. Update only its code-adjacent comments and
its formatter invocation if needed for the named probe field; generated
records already have probe zero through the current zero-fill behavior. Do not
route runtime repair through this boot-only operation.

## Cache and future mutation cap (deliberately deferred)

The ordinary dummy writer does not borrow `fs_list_cache_name`, even while the
Load/Save gate is closed. The later payload writer may use that 9 KB allocation
as a flat 2,576-byte mutation-mask lease only when it owns the filesystem and
`menu_activePage` is neither Load nor Save. At release—success and error—it
will invalidate the name-cache domain; the next Load/Save workflow reloads the
needed index instead of reading mask bytes as display names.

The future writer must snapshot a bounded subset of dirty mutations into that
leased mask and leave the rest pending. The cap's unit (byte, parameter cell,
or logical record) and value have not been chosen. This dummy milestone must
not add a placeholder cap macro, cache lease, dirty bit, or mutation producer;
its copy-forward transaction is only structured so that such bounded draining
can replace the probe-only transform later.

## Exact file-change inventory

| File | Required change | Why it is required |
| --- | --- | --- |
| `AUTOSAVE_WRITER.md` | Maintain this source-audited execution, memory, recovery, and test contract during implementation. | The writer has multiple persistence boundaries and must remain reviewable. |
| `config.h` | Add `AUTOSAVE_WRITER_INTERVAL_MS 5000u` with a wrap-range comment. | Centralizes the only cadence policy. |
| `Core/Bank/Scene/Autosave.h` | Add probe offset, streaming-validation type/API, generation comparison, and transform API comments. | Gives filesystem one canonical wire/CRC contract without retaining data. |
| `Core/Bank/Scene/Autosave.c` | Implement/refactor streaming validation, CRC update, generation compare, chunk transformation, and initial probe-zero comments. | Avoids duplicate header arithmetic and enables sector streaming without SRAM images. |
| `Core/Hardware/SD/filesystem.c` | Add three-byte scheduler state, stage-union writer state, internal operation/diagnostic/dispatcher/scheduler/callback, full asynchronous validate-copy-commit-recover state machine, and creation-comment updates. | This is the sole AsyncFATFS owner and already owns the buffer, callbacks, operation status, and flush boundary. |
| `Core/Hardware/SD/filesystem.h` | Document the boot creator's terminal full-volume behavior; no runtime writer start API is exposed. | The blocking wrapper must state that allocation exhaustion returns failure rather than trapping boot, while the writer remains autonomous. |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` and `.h` | Make regular free-cluster allocation wrap around the FAT and document the sticky, exhaustive `afatfs_isFull()` result. | A last-allocation hint may optimize placement but must not hide free clusters before the hint or make callers retry a false-full result forever. |
| `Core/Menu/menu.c` and `Core/Menu/menu.h` | No change planned. | Existing exported `menu_activePage` plus `LOAD_PAGE`/`SAVE_PAGE` is the exact read-only gate required. |
| `main.c` | No change planned. | Its existing foreground call to `filesystem_tick()` provides all runtime progress. |
| `Makefile` | No change planned. | `Autosave.c` is already in `SRCS`. |
| `SD_CARD/.hcprms1`, `SD_CARD/.hcprms2` | No byte change required for merely naming offset 16; verify both remain valid with probe zero. | The byte is already zero and included by the unchanged CRC definition. |

All implementation changes in `.c` and `.h` files require the same
code-adjacent What/Why/Input/Output comments used by the existing autosave and
filesystem code.

## Acceptance checks

1. Build with `make -j2 && make img`, run `git diff --check`, and compare text,
   data, and BSS. Confirm the stage workspace remains within 2,048 bytes and
   no record/mask-sized allocation appears.
2. With A=gen1/probe0 and B=gen0/probe0, remain outside Load/Save for one
   interval. Verify ordinary foreground work continues and B becomes
   gen2/probe1 with a valid CRC; A is byte-for-byte unchanged.
3. At the following interval verify A becomes gen3/probe2, then alternating
   target ownership continues.
4. Enter Load or Save before an interval and remain there. Verify no writer
   starts, no cache is cleared, and no deferred-write burst occurs on exit.
5. Begin a normal filesystem request before an interval. Verify scheduler does
   not start while status is busy; it starts only after status becomes idle and
   the page gate permits it.
6. Reboot after any completed transaction. The next writer validates both
   files and continues from the actual highest valid generation/probe.
7. Corrupt one file's payload, CRC, magic, commit marker, or length. Verify the
   surviving valid record wins and only the other file is rewritten.
8. Corrupt or remove both records. Verify writer reads HCNAMES, flushes a
   regenerated B=gen0/probe0, then regenerates current A=gen1/probe0. If
   HCNAMES cannot be read, verify neither target is opened for overwrite.
9. Force/observe errors in source open/read, target write, target reopen,
   seek, marker write, and either sync barrier. Verify no synchronous retry,
   no Menu status acknowledgment, and preservation of the last valid winner.
10. Interrupt power/card access during target copy and before final marker;
    on the next attempt, verify the old winner validates. Test after the final
    marker/final-sync boundary separately on target hardware.

## Implementation record — 2026-07-29

Implemented this dummy-counter milestone exactly as the streaming design above.

- `config.h` now defines the five-second, wrap-safe minimum writer interval.
- `Autosave.h/.c` now own the probe offset, incremental CRC32C, streaming
  validation, Bank-name identity check, modulo-generation comparison, and
  in-place header-only transform. The transform never changes the mutation
  mask or payload.
- `filesystem.c` now schedules the private operation only while a Bank is
  resident, AsyncFATFS is ready, and Load/Save is not active. A private
  completion callback acknowledges only the autonomous result and rearms the
  next interval.
- The operation validates both files in 512-byte chunks, chooses the newest
  valid matching-Bank generation (A wins ties), calculates the next record's
  CRC in a separate streamed pass, copy-forwards the winner to the inactive
  file with commit clear, syncs, then seeks and writes the valid commit marker
  last. All close, seek, read, write, and sync progress is foreground-pumped;
  no phase waits synchronously.
- If neither file validates for the active Bank, recovery loads HCNAMES and
  regenerates B generation 0 then A generation 1, flushing B before A. Error
  exits close any still-open source/target handles asynchronously before they
  publish their acknowledged error result.

No payload writer, dirty/mutation-mask cache lease, mutation cap, reader, or
Menu API was added.

### Verification completed

- `make -j2` and `make img` completed successfully. The resulting image is
  `build/LXRV2_lxr02.img`; final firmware size is text 358,820 bytes, data
  396 bytes, BSS 69,948 bytes. The 2,048-byte stage allocation remains fixed;
  the 2,048-byte stage allocation remains fixed and there is no record- or
  mask-sized allocation.
- `git diff --check` passed.
- A small host-side stream test validated each checked-in fixture in uneven
  chunks, verified its Bank name, copy-transformed it to generation 2/probe 1,
  verified the transformed CRC, and confirmed a flipped probe byte fails CRC
  validation. Both `SD_CARD/.hcprms1` and `SD_CARD/.hcprms2` passed.
- The target hardware cadence, page-gate, partial-write, corruption recovery,
  and power-loss checks in the acceptance list remain required before this is
  considered field-validated.

### Follow-up: duplicate hidden-record cleanup

Card observation showed that a case-sensitive LFN write-open could leave more
than one physical `.hcprms2` directory entry. The writer now treats the two
hidden filenames according to FAT's case-insensitive namespace. Before it
creates the inactive copy, it uses AsyncFATFS's asynchronous
`afatfs_removeObjects_lfn(..., AFATFS_MATCH_CASE_INSENSITIVE,
AFATFS_REMOVE_FILES_ONLY, ...)` to retire every same-folded target-file
variant; the separately validated active winner remains open and untouched.
Recovery applies the same cleanup before rebuilding each invalid target. Thus
the next completed autosave should leave exactly one `.hcprms1` and one
`.hcprms2`; no direct synchronous directory operation or name-cache lease was
added.

### Follow-up: boot ownership gate

The runtime scheduler is now disabled from reset until
`filesystem_ensureAutosaveFilesBlocking()` has created/verified and flushed
the boot pair successfully. A Bank can become resident well before the full
pre-audio boot ladder finishes; without this gate, its elapsed five-second
cadence could start runtime recovery while those boot operations still owned
the intended creation sequence. Failure leaves autosave inactive rather than
attempting a recovery write during boot. The first normal writer interval
starts only after this successful handoff.

### Follow-up: leading-dot SFN alias collision

The shared LFN alias generator interpreted the leading dot in both
`.hcprms1` and `.hcprms2` as an extension separator, producing the same initial
short alias `FILE.HCP`. That was a real duplicate/collision defect, but the
subsequent 16,384-byte hardware fixture proved it was not the remaining boot
freeze: A itself had stopped at one card cluster, before B creation began.
Leading dots remain corrected as hidden-name prefixes; only a later dot
separates an extension. The preserved VFAT display names remain
`.hcprms1`/`.hcprms2`, while their physical aliases are independently derived
as `HCPRMS1` and `HCPRMS2`.

### Follow-up: multi-cluster creation freeze

The copied failing `.hcprms1` is exactly 16,384 bytes, the card's cluster
size. This locates the stall at regular-file extension into a second cluster,
not at the existence scan or `.hcprms2` open.

AsyncFATFS used `lastClusterAllocated` as the beginning of a single
hint-to-volume-end free-cluster scan. It did not wrap to cluster 2. Reaching
the FAT end therefore set the sticky `filesystemFull` flag even when free
clusters existed before the hint. Once set, `afatfs_fwrite()` returned zero
forever, while the blocking creator treated every zero as temporary
back-pressure.

Regular allocation now performs two bounded asynchronous passes:
`[hint, volume end)` followed, when necessary, by
`[cluster 2, hint)`. Only exhaustion of both ranges declares the volume full.
The boot creator also distinguishes a zero-byte retry from that terminal full
result, closes the partial file asynchronously, and returns an error so boot
can continue. The runtime copy-forward and recovery writers use the same
terminal check and existing handle-cleanup phases. All new `.c` and `.h`
changes carry code-adjacent What/Why/Input/Output comments.

The updated firmware builds successfully with text 358,948 bytes, data
396 bytes, and BSS 69,948 bytes. `make img` produced the 359,344-byte
`build/LXRV2_lxr02.img`, and `git diff --check` passed. The supplied failing
fixture remains intentionally untouched at 16,384 bytes so it continues to
document the observed one-cluster stop; `.hcprms2` is absent in that captured
card state.
