# Autosave single canonical dirty record

## Status

Implemented in source on 2026-08-01. Static/build verification is complete;
hardware/SD acceptance testing remains pending.

This plan was derived directly from the current implementations in:

- `Core/Bank/Scene/Autosave.c`
- `Core/Bank/Scene/Autosave.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `config.h`
- `Makefile` and the current linker map, only for build/RAM verification

No historical planning document, log, specification, or archived proposal was
used to determine the changes below.

## One narrowly scoped outcome

Replace the transaction-owned `updated_mask` with one persistent 3,856-byte
canonical dirty mask owned by `Autosave.c`.

The runtime writer will:

1. validate the two files and select the current winner exactly as it does now;
2. OR the winner's on-file mask into the canonical SRAM mask;
3. drain and clear bits in that one canonical mask;
4. make one transformed winner-to-target copy while calculating the CRC from
   the exact transformed bytes;
5. sync the copied record while its commit byte and stored CRC are zero;
6. write and sync the calculated CRC after the record copy is durable;
7. write `AUTOSAVE_HEADER_COMMIT_VALID` last and enter the existing final sync;
8. retain the canonical mask after the filesystem operation ends.

In this document, **post-commit CRC write** means that the CRC is calculated
during the one record commit/copy stream and physically written after that
stream is complete. It does **not** mean making the record valid before its CRC
exists. The `0xA5` commit-valid marker remains the final publication step.

## Explicit exclusions

This implementation pass does not:

- add any Bank, Scene, Kit, Instrument, Menu, MIDI, or Preset dirty-marking
  calls;
- add the eventual public parameter-change bit-set interface;
- change which payload offsets `autosave_getLivePayloadByte()` can resolve;
- change the 1,536-get cap, 256-bit foreground scan cap, five-second debounce,
  or 250 ms continuation interval;
- change the autosave file layout, sizes, filenames, header fields, generation
  selection, CRC polynomial, or least-significant-bit-first mask convention;
- make the files complete Bank registers;
- change initial file creation or no-valid-record recovery formatting;
- change Load/Save menu suspension;
- dual-use the 9 KB names cache;
- modify `.hcprms1`, `.hcprms2`, or any file under `SD_CARD/`.

The canonical mask will therefore receive new dirty work only from an existing
file mask during this test pass. Parameter-change producers are a later,
separately reviewed change.

## Exact current condition being corrected

### Current mask ownership

`filesystem.c` currently declares:

```c
typedef struct {
    uint8_t updated_mask[AUTOSAVE_MASK_BYTES];
    uint16_t payload_offsets[AUTOSAVE_PARAMETER_GETS_PER_WRITE];
    uint8_t payload_values[AUTOSAVE_PARAMETER_GETS_PER_WRITE];
} filesystem_autosave_parameter_cache_t;
```

Phase 0 clears that entire structure. Phase 54 reads the winner mask directly
into `updated_mask`. Phase 56 clears drained bits there. Phase 22 reduces its
remaining state to the scalar `continue_pending`, and the next operation clears
and reloads the mask again.

That makes the file the owner and SRAM a temporary copy. The intended direction
is the reverse: SRAM owns the live dirty record, while a file mask carries
incomplete work across power loss.

### Current two-stream CRC transaction

Phases 6-9 currently read and transform all 34,768 bytes only to precompute the
target CRC. Phases 10-16 then reopen the winner and transform all 34,768 bytes a
second time to create the inactive target. Both passes depend on the same mask
remaining immutable.

A persistent canonical mask is deliberately mutable between foreground ticks.
The precompute pass must therefore be removed. The CRC must instead consume the
same staged transformed bytes used by the one physical copy.

## Required invariants

1. There is exactly one 3,856-byte dirty-mask allocation in SRAM.
2. That allocation has static lifetime and is owned by `Autosave.c`.
3. Starting or completing a filesystem transaction never zeroes or discards
   the canonical mask.
4. Reading a valid file mask ORs bits into the canonical mask. It never assigns
   over it and never clears a canonical bit.
5. Classification clears only the addressed canonical bit after its current
   live value has been captured, or after the existing getter reports that the
   format cell has no live owner.
6. Patch offsets and values remain stable transaction-local data.
7. The transformed target receives mask bytes from the canonical mask at the
   moment each mask chunk is staged.
8. CRC accumulation consumes the exact transformed chunk before its commit byte
   is physically forced to zero for the unpublished target.
9. The copied target is synced with stored CRC zero and commit zero.
10. The calculated CRC is written and synced only after that copy is durable.
11. The valid commit marker is written last. Until then, validation must reject
    the inactive target and retain the old winner.
12. A successful empty-mask check performs no target removal, generation
    increment, record write, or flush.
13. A failed transaction restores every captured live patch bit to the
    canonical mask before returning an error.
14. The current 8,464-byte combined mask/patch SRAM budget does not grow.

## SRAM ownership and size

The present cache is exactly:

```text
updated_mask:                 3,856 bytes
payload_offsets: 1,536 * 2 = 3,072 bytes
payload_values:  1,536 * 1 = 1,536 bytes
combined:                    8,464 bytes
```

After this change:

```text
Autosave.c canonical mask:    3,856 bytes
filesystem.c patch offsets:   3,072 bytes
filesystem.c patch values:    1,536 bytes
combined:                    8,464 bytes
```

The current built image reports 78,444 bytes of BSS. This pass must not increase
that value. Alignment may move symbols in the linker map, but the two relevant
allocations must total exactly 8,464 bytes.

## File-by-file implementation plan

### 1. `Core/Bank/Scene/Autosave.c` — own the canonical mask

Add one file-static array:

```c
static uint8_t autosave_dirty_mask[AUTOSAVE_MASK_BYTES];
```

What it does: provides the only retained dirty-mask storage. Static BSS
initialization makes it empty at processor reset; it is not reset at the start
or end of an autosave transaction.

Why it must exist: dirty work must survive reuse of the filesystem operation
workspace and patch cache. Moving the existing allocation changes ownership
without increasing SRAM.

Inputs: file-mask recovery bits merged by the filesystem drain and bit clears
performed by the existing classifier.

Outputs: the persistent mask inspected by scheduling, classification, target
transformation, and error restoration.

Affiliates: the mask access functions below and
`filesystem_autosaveParameterDrain_tick()`.

Add a static assertion that the canonical array is exactly
`AUTOSAVE_MASK_BYTES`. Keep the existing wire-level assertion that 3,856 mask
bytes cover all 30,848 payload bytes.

Update the module comment, which currently says the retained mask is owned by
`filesystem.c`. It must state that `Autosave.c` owns the canonical mask while
`filesystem.c` owns AsyncFATFS handles, scheduling, and transaction-local
patches.

### 2. `Core/Bank/Scene/Autosave.c` — replace caller-owned mask operations

Change the current mask helpers so callers no longer supply an arbitrary mask
pointer:

- `autosave_maskHasDirty(void)` scans `autosave_dirty_mask`.
- `autosave_maskBitIsSet(uint16_t payload_offset)` reads the canonical bit.
- `autosave_maskBitClear(uint16_t payload_offset)` clears the canonical bit.

What this does: makes all reads and writes resolve to the same retained record.

Why it must exist: retaining APIs that accept a mutable caller-owned mask would
continue to permit a second mask owner and would leave the old direction
encoded in the interface.

Inputs: a payload-relative offset for bit operations.

Outputs: one dirty/non-dirty result or one bounded canonical-bit clear.

Affiliates: drain phases 55/56, the completion callback, and the transformed
copy helper.

Preserve the current bounds behavior: out-of-range reads return zero and
out-of-range clears do nothing.

### 3. `Core/Bank/Scene/Autosave.c` — merge recovered file-mask chunks

Add a bounded function that accepts a mask-byte offset, source bytes, and byte
count, then ORs those bytes into `autosave_dirty_mask`.

What it does: imports incomplete work from the selected valid file without
overwriting work already retained in SRAM.

Why it must exist: direct `fread()` into the canonical array would replace
current state. OR is the required recovery direction: either SRAM or the file
may say a payload byte still needs storage.

Inputs:

- mask-relative byte offset `0..AUTOSAVE_MASK_BYTES-1`;
- a caller-owned chunk read through the existing 512-byte `staging_buf`;
- the accepted AsyncFATFS byte count.

Outputs: every set source bit is set in the canonical mask; existing canonical
bits and bytes outside the supplied interval are unchanged.

Affiliates: runtime drain phase 54 and `AUTOSAVE_MASK_BYTES`.

Reject null or out-of-range intervals without touching the mask. Do not add a
whole-mask load, copy, reset, or replace API.

### 4. `Core/Bank/Scene/Autosave.c` — restore captured bits on transaction error

Add a bounded function that sets canonical bits from the transaction's sorted
payload-offset array and count.

What it does: re-dirties each successfully captured live parameter if the
target transaction terminates before a successful commit.

Why it must exist: phase 56 clears a bit as soon as it captures a stable value.
Once SRAM is canonical, an error cannot rely solely on re-reading the old file
later; SRAM-only dirty work must remain represented immediately.

Inputs: `payload_offsets` and the valid `patch_count` already retained by
`filesystem.c`.

Outputs: all bounded captured offsets are set in the canonical mask. Duplicate
or already-set bits are harmless because setting is idempotent.

Affiliates: `filesystem_autosaveWriterFinishErrorNow()` and the transaction
patch cache.

This is an internal transaction rollback operation, not the later public
parameter-change marking API.

### 5. `Core/Bank/Scene/Autosave.c` — make the transform use canonical state

Remove the `updated_mask` parameter from `autosave_transformDrainChunk()`.
When a streamed chunk intersects the mask region, copy the intersecting bytes
from `autosave_dirty_mask` directly.

Also narrow the header transform used by this function so the one-pass runtime
copy produces the prospective final logical header with:

- next generation;
- next probe counter;
- stored CRC bytes zero;
- commit byte logically `AUTOSAVE_HEADER_COMMIT_VALID` for CRC calculation.

What it does: produces one prospective final chunk from the winner, canonical
mask, and stable payload patches.

Why it must exist: the transform should no longer pretend it is replaying an
immutable caller-owned mask through separate CRC and output passes. It will be
called once per source chunk, and CRC will be computed from that result.

Inputs: source chunk and absolute position, next generation/probe, sorted patch
offset/value arrays, patch count, and monotonic patch cursor.

Outputs: prospective final record bytes. The caller subsequently forces only
the physical commit cell to zero before writing the unpublished target.

Affiliates: CRC helpers and runtime copy phase in `filesystem.c`.

Initial-record formatting and validation must not be changed. Their existing
CRC rule—stored CRC bytes are treated as zero—already matches this transaction.

### 6. `Core/Bank/Scene/Autosave.h` — expose only canonical-mask operations

Update declarations and their adjacent contract comments to match changes
2-5:

- remove caller-owned mask parameters from has/test/clear;
- declare the bounded OR-merge function;
- declare the captured-offset rollback function;
- remove `updated_mask`, `crc32c`, and `commit_value` inputs from the runtime
  transform if those header values become fixed by the one-pass contract.

What it does: makes the ownership direction visible at compile time.

Why it must exist: `filesystem.c` must not retain an API that can substitute,
clear, or transform a second mask.

Inputs/outputs: exactly the canonical operations described above.

Affiliates: `Autosave.c` and the runtime writer in `filesystem.c`.

Update the file header, which currently says this module does not own a retained
mask. State explicitly that it owns no filesystem handles or scheduling but
does own the single retained dirty record.

Do not add a general raw mutable-mask accessor.

### 7. `Core/Hardware/SD/filesystem.c` — shrink the patch cache

Remove `updated_mask` from `filesystem_autosave_parameter_cache_t`. Leave only:

```c
uint16_t payload_offsets[AUTOSAVE_PARAMETER_GETS_PER_WRITE];
uint8_t payload_values[AUTOSAVE_PARAMETER_GETS_PER_WRITE];
```

What it does: leaves filesystem ownership limited to stable transaction-local
patches.

Why it must exist: retaining the old array would create the forbidden second
mask and waste 3,856 bytes.

Inputs: captured gets from phase 56.

Outputs: a 4,608-byte immutable patch list used by the one copy stream.

Affiliates: the cache assertions, config comments, Autosave transform, and
error rollback.

Remove the `updated_mask` size assertion. Retain the offset/value count
assertions and the 9 KB ceiling; add or update an assertion that the remaining
patch cache is exactly 4,608 bytes.

Update the surrounding cache and writer-state comments to remove “two passes,”
“temporary mask,” and “immutable mask input.”

### 8. `Core/Hardware/SD/filesystem.c` — stop clearing the dirty record

In drain phase 0, continue zeroing `op_autosave_writer` and the reduced patch
cache. Do not call any canonical-mask reset and do not clear storage owned by
`Autosave.c`.

What it does: initializes transaction progress without erasing retained work.

Why it must exist: transaction start is not a Bank reset and must never destroy
the canonical record.

Inputs: a newly scheduled autosave operation.

Outputs: clean scalar/patch state with canonical dirty state unchanged.

Affiliates: `filesystem_autosaveWriterSchedule_tick()` and phase 54.

### 9. `Core/Hardware/SD/filesystem.c` — OR the winner mask into SRAM

Keep phases 50-53: reopen the selected winner and seek to
`AUTOSAVE_MASK_OFFSET`.

Change phase 54 to read at most 512 bytes into `staging_buf`, then pass the
accepted bytes and current `mask_bytes_read` to the Autosave OR-merge function.
Advance `mask_bytes_read` only after the merge call.

What it does: streams the winner's completeness record into the one canonical
mask without allocating another buffer.

Why it must exist: the old direct `fread()` overwrites SRAM and encodes file
ownership. The staging buffer is already available and bounds each foreground
call.

Inputs: valid winner handle and on-file mask interval.

Outputs: canonical mask equals its prior state OR the complete winner mask.

Affiliates: phases 50-55, `staging_buf`, and the new merge API.

Change phase 55's empty test and phase 56's bit test/clear calls to the
canonical no-pointer APIs. Preserve the existing immediate read-only completion
when the merged canonical mask is empty.

### 10. `Core/Hardware/SD/filesystem.c` — remove the prospective CRC pass

Delete the current behavior of phases 6-9:

- opening the winner solely for CRC;
- reading and transforming the entire record solely for CRC;
- closing it;
- reopening it for the copy.

After classification, proceed directly to the existing copy-source open.
Initialize `target_crc32c` with `autosave_recordCrcBegin()` and reset
`patch_cursor` once before the copy stream.

What it does: removes one complete 34,768-byte SD read and the dependency on an
immutable mask between two passes.

Why it must exist: a live canonical mask may gain bits at any foreground tick;
CRC must describe the actual staged output rather than an earlier projection.

Inputs: selected winner, current canonical mask, and stable patch list.

Outputs: one source open and one transformed copy stream.

Affiliates: copy-source phases, `autosave_recordCrcBegin()`, and the transformed
chunk helper.

Phase numbers may be compacted only if every transition, error code, and
adjacent phase comment is updated together. No unrelated filesystem state
machine may be renumbered.

### 11. `Core/Hardware/SD/filesystem.c` — calculate CRC from copied chunks

For each source chunk in the copy phase:

1. transform it once using the canonical mask and captured patches;
2. update `target_crc32c` from that prospective final chunk;
3. if the chunk intersects `AUTOSAVE_HEADER_COMMIT_OFFSET`, replace that one
   staged byte with zero;
4. write that unpublished chunk to the target, retaining the existing partial
   `fwrite()` retry behavior;
5. after the final chunk has been accepted, finish the CRC accumulator.

What it does: guarantees the calculated CRC represents the exact transformed
generation, probe, mask, and payload bytes selected during the physical copy.

Why it must exist: CRC and copy can no longer disagree due to a changing
canonical mask. The logical CRC includes the final `0xA5` commit value, while
the physical target remains invalid during construction.

Inputs: each transformed `staging_buf` interval and its absolute record offset.

Outputs: a calculated final CRC plus a complete target containing CRC zero and
commit zero.

Affiliates: `autosave_recordCrcUpdate()`,
`autosave_recordCrcFinish()`, `autosave_transformDrainChunk()`, and the existing
source/target close path.

The CRC field needs no special staging mutation: the existing CRC update helper
already treats record bytes 12-15 as zero, and the transform supplies zero in
those cells until post-copy publication.

### 12. `Core/Hardware/SD/filesystem.c` — publish CRC after the copy

Preserve the existing close of both source and target and the existing private
`afatfs_sync()` that makes the invalid copy durable.

After that sync:

1. reopen the inactive target with `"r+"`;
2. asynchronously seek to `AUTOSAVE_HEADER_CRC32C_OFFSET`;
3. verify queued seek completion with `afatfs_ftell()`, exactly as the existing
   commit seek does;
4. place the calculated CRC into four `staging_buf` bytes in little-endian
   order;
5. require all four bytes to be accepted before closing the handle;
6. wait for the close callback;
7. call a second private `afatfs_sync()` and wait until the CRC is durable.

What it does: installs the checksum only after every checksummed data byte is
durable.

Why it must exist: a precomputed CRC pass is being removed, and publishing the
CRC before the copy is stable could leave it describing sectors that have not
yet reached the card.

Inputs: final `target_crc32c` and the inactive target filename.

Outputs: a still-invalid target with durable final data and durable final CRC,
but commit byte zero.

Affiliates: AsyncFATFS open/seek/tell/write/close callbacks and the commit
publication phases below.

Zero-byte/asynchronous back-pressure must retry. A proven full-volume condition
must enter the existing error-close path rather than wait forever.

### 13. `Core/Hardware/SD/filesystem.c` — retain commit-last publication

After the CRC sync, reopen the same target with `"r+"`, seek and verify
`AUTOSAVE_HEADER_COMMIT_OFFSET`, write one
`AUTOSAVE_HEADER_COMMIT_VALID` byte, and close it.

Then call `filesystem_finish(FS_STATUS_DONE)` so the existing shared final
flush persists the validity marker before the completion callback runs.

What it does: atomically publishes the new generation only after its data and
CRC are durable.

Why it must exist: power loss during copy or CRC publication must leave the
inactive target invalid while preserving the old valid winner.

Inputs: the target already carrying durable data/CRC.

Outputs: a new CRC-valid, committed generation after the final flush.

Affiliates: `AUTOSAVE_HEADER_COMMIT_OFFSET`, validation, winner selection, and
`filesystem_flushFinish_tick()`.

Add distinct adjacent phase comments for data sync, CRC open/seek/write/close,
CRC sync, commit open/seek/write/close, and final flush handoff. Do not combine
CRC and commit into one unsynced handle lifecycle.

### 14. `Core/Hardware/SD/filesystem.c` — preserve canonical work on errors

Before `filesystem_autosaveWriterFinishErrorNow()` publishes
`FS_STATUS_ERROR`, call the captured-offset rollback function using the current
patch list and `patch_count`.

What it does: reverses phase 56's early bit clears when the transaction fails.

Why it must exist: after this change, the SRAM record is canonical and cannot
depend on the next file read to reconstruct SRAM-only work.

Inputs: transaction-local captured offsets. Before capture, `patch_count` is
zero, so the call is harmless.

Outputs: every captured live parameter remains dirty after error cleanup.

Affiliates: error phases 40-43 and all direct calls to the error helper.

Do not restore bits on successful commit. Nonexistent cells have no patch
offset and therefore are not restored; the unchanged valid winner still holds
their old dirty bits if the transaction fails, and they will be reclassified
on the next attempt.

### 15. `Core/Hardware/SD/filesystem.c` — schedule from canonical state

Remove `continue_pending` from
`filesystem_autosave_writer_state_t` and remove phase 22's mask-to-scalar
conversion.

On successful autonomous-writer completion, call
`autosave_maskHasDirty()` directly:

- dirty canonical mask: use
  `AUTOSAVE_WRITER_CONTINUATION_INTERVAL_MS`;
- empty canonical mask: use `AUTOSAVE_WRITER_INTERVAL_MS`;
- error: use `AUTOSAVE_WRITER_INTERVAL_MS`, even though rollback may have made
  the canonical mask dirty.

What it does: lets the persistent owner itself determine whether bounded work
remains.

Why it must exist: reducing the result to a transaction-local scalar repeats
the ownership mistake and can miss bits set during the final flush.

Inputs: completion status and canonical mask after the final sync.

Outputs: unchanged five-second/250 ms scheduling policy.

Affiliates: `filesystem_autosaveWriterCompleted()` and the background scheduler.

The terminal writer phase should only close the commit handle and enter
`filesystem_finish(FS_STATUS_DONE)`; it must not clear the canonical mask.

### 16. `Core/Hardware/SD/filesystem.h` — correct the public ownership comment

No function signature changes are required in this header.

Update the adjacent documentation for
`filesystem_ensureAutosaveFilesBlocking()` so it no longer says the runtime
drain uses a separate complete-mask cache or reloads an empty file mask as the
owner.

What it does: documents that runtime validation merges file completeness bits
into one retained Autosave-owned mask and that transaction patches remain
filesystem-owned.

Why it must exist: the existing header describes the temporary direction being
removed.

Inputs/outputs: documentation only; the blocking create-only function remains
unchanged.

Affiliates: `Autosave.h` and the runtime writer.

### 17. `config.h` — correct cache/CRC comments without changing values

Keep all four current autosave configuration values unchanged.

Update adjacent comments to state:

- the 1,536-get limit creates a 4,608-byte filesystem patch cache;
- the separate Autosave-owned canonical mask is 3,856 bytes;
- combined storage remains 8,464 bytes;
- captured patches are prepared before the one copy stream, not before two CRC
  and copy passes;
- continuation observes the retained canonical mask after successful final
  flush.

What it does: aligns configuration documentation with the new ownership and
one-pass transaction.

Why it must exist: the current comments explicitly name the winner's on-card
mask as input and describe separate CRC/copy passes.

Inputs/outputs: comment-only change; timing and caps are byte-for-byte
unchanged.

Affiliates: the two cache allocations and completion callback.

## Exact runtime transaction after the change

```text
validate A and B
        |
select newest valid matching winner
        |
reopen winner and stream its mask through 512-byte staging chunks
        |
canonical SRAM mask |= winner file mask
        |
canonical empty? ---- yes ---> complete read-only; ordinary 5 s cadence
        |
       no
        |
capture <= 1,536 live bytes and clear those canonical bits
        |
open winner once + create inactive target
        |
transform each source chunk once
        |
CRC-update prospective final chunk
        |
physically write chunk with CRC=0 and commit=0
        |
close source/target + sync invalid copy
        |
write CRC32C + close + sync
        |
write commit 0xA5 + close + existing final sync
        |
canonical still dirty? -- yes --> 250 ms continuation
        |
       no
        |
ordinary 5 s cadence
```

## Power-loss and error results

| Interruption point | Inactive target | Old winner | Next boot/runtime result |
|---|---|---|---|
| During transformed copy | commit `0`, CRC `0`/partial | unchanged and valid | inactive rejected; old winner selected |
| After invalid-copy sync, before CRC | commit `0`, CRC `0` | unchanged and valid | inactive rejected; old winner selected |
| During CRC write | commit `0`, CRC partial/final | unchanged and valid | inactive rejected; old winner selected |
| After CRC sync, before commit | commit `0`, correct CRC | unchanged and valid | inactive rejected; old winner selected |
| During/following commit write | commit may be `0` or `A5` | unchanged and valid | at least old winner validates; new target wins only if complete and CRC-valid |
| Any explicit writer error | captured bits restored in SRAM | unchanged unless a fully valid target already exists | ordinary retry cadence; no dirty work discarded |

## Verification before hardware

### Source/static verification

1. `rg` finds exactly one `AUTOSAVE_MASK_BYTES` array allocation.
2. No `updated_mask` symbol or struct member remains.
3. No runtime transformed-CRC-only source pass remains.
4. `autosave_transformDrainChunk()` has exactly one runtime call site.
5. No transaction-start or completion path clears the canonical mask.
6. Winner-mask import uses OR merge through `staging_buf`.
7. CRC is written at `AUTOSAVE_HEADER_CRC32C_OFFSET` only after the invalid
   copy sync.
8. Commit is written at `AUTOSAVE_HEADER_COMMIT_OFFSET` only after the CRC
   sync.
9. `git diff --check` reports no whitespace errors.

### Build/RAM verification

1. Run the normal `Makefile` build.
2. Require a clean compile with no stale mask-function signature warnings.
3. Run `arm-none-eabi-size build/lxr02.elf`.
4. Require BSS to remain at or below the current 78,444-byte baseline.
5. Inspect `build/lxr02.map` and require:
   - Autosave canonical mask: exactly 3,856 bytes;
   - filesystem patch cache: exactly 4,608 bytes;
   - combined total: exactly 8,464 bytes.

## Hardware/SD acceptance tests

No parameter-change detection work begins until all tests below pass.

### Test 1 — existing dirty fixture drains correctly

Start with two valid matching records containing known dirty bits and known
payload values. Run one transaction.

Require:

- exactly one peer generation advances;
- generation and probe advance once;
- no more than 1,536 live gets are captured;
- captured payload bytes match resident state;
- captured/nonexistent bits are clear in the committed target;
- unprocessed bits remain set;
- the target is exactly 34,768 bytes and its independently recalculated CRC32C
  matches its stored little-endian CRC.

### Test 2 — continuation retains canonical backlog

Use a mask requiring more than one bounded transaction. Let the unit continue
without reboot.

Require:

- subsequent work starts on the 250 ms continuation cadence;
- the canonical remainder is not zeroed when the next operation starts;
- file bits merge without replacing that remainder;
- generations alternate A/B until the mask drains;
- every committed generation validates independently.

### Test 3 — empty canonical/file mask performs no write

After the mask fully drains, leave the device running for at least two ordinary
five-second intervals.

Require both files to retain identical generation, probe, size, CRC, and
modification content. Validation reads are permitted; peer removal, creation,
write, and flush are not.

### Test 4 — CRC publication ordering

Interrupt power separately during:

1. record copy;
2. the first invalid-copy sync;
3. CRC write/sync;
4. commit write/final sync.

After each interruption, require at least one original/current record to
validate. A target with commit zero must never be selected, even if its CRC is
complete. A target with commit `A5` may win only when its independent CRC and
exact size validate.

### Test 5 — restart resumes file-carried incomplete work

Power off after one successful partial drain, then reboot.

Require the committed winner's remaining file bits to OR into the reset-empty
canonical BSS mask and continue draining without restoring already-cleared
bits.

### Test 6 — no-valid-record recovery remains unchanged

Provide two invalid records and boot with a resident Bank.

Require the existing recovery path to regenerate one `.hcprms1` and one
`.hcprms2`, with the existing baseline generations, exact sizes, names,
zero masks, commit markers, and valid CRCs. The new runtime one-pass CRC path
must not alter the initial-record formatter or recovery CRC calculation.

### Test 7 — duplicate-target prevention remains unchanged

Run multiple dirty generations and inspect the card directory.

Require exactly one case-insensitive object for `.hcprms1` and exactly one for
`.hcprms2`. The existing inactive-variant retirement must still occur before
target creation.

## Acceptance boundary

This pass is complete only when:

- there is one persistent Autosave-owned mask and no filesystem-owned mask;
- file masks merge into it rather than replace it;
- the runtime writer performs one transformed record-copy read;
- CRC is accumulated from that copy, then written and synced afterward;
- the valid commit marker remains last;
- empty masks produce no writes;
- error cleanup cannot discard captured dirty work;
- SRAM usage does not increase;
- all seven hardware/SD tests pass.

Only after this boundary is accepted should a new plan add retained-parameter
change producers.

## Implementation notes — 2026-08-01

### Source changes completed

- `Core/Bank/Scene/Autosave.c` now owns the only 3,856-byte mask as
  `autosave_dirty_mask`. The module provides bounded file-mask OR merge,
  canonical has/test/clear operations, and captured-offset rollback. Its
  transform reads the canonical mask directly and produces a prospective final
  header with CRC zero and logical commit `0xA5`.
- `Core/Bank/Scene/Autosave.h` exposes only those canonical operations. No raw
  mutable mask pointer or second-mask interface was added.
- `Core/Hardware/SD/filesystem.c` removed `updated_mask` and
  `continue_pending`. Its remaining patch cache contains only 1,536 uint16
  offsets and 1,536 byte values.
- Winner mask reads now stream through the existing 512-byte `staging_buf` and
  OR into the canonical mask. Operation initialization clears scalar/patch
  state only.
- Phase 56 now drains canonical bits. Both get-cap exits proceed directly to the
  copy-source open; the former complete CRC-only pass was removed.
- The one copy stream transforms each chunk, updates CRC from those exact
  prospective final bytes, and then physically clears only the commit byte
  before writing the target.
- CRC finalization, source close, and inactive-target close now have distinct
  phases: finalization occurs exactly once, and rejected asynchronous close
  requests are retried without waiting for a callback that was never queued.
- The target data is closed and synced with CRC/commit zero. The calculated
  four-byte little-endian CRC is then written, closed, and synced. The commit
  byte is opened/searched/written/closed separately and the existing final
  success flush remains the last durability gate.
- Error publication restores every captured live offset before returning
  `FS_STATUS_ERROR`. Successful completion reads canonical dirty state directly
  to choose 250 ms continuation versus the ordinary five-second interval.
- `filesystem.h` and `config.h` comments now describe canonical Autosave
  ownership, the 4,608-byte patch cache, the one copy stream, and post-copy CRC
  publication. Configuration values were not changed.

### Verification completed

- A forced serialized full rebuild completed and linked successfully. The
  build emitted only pre-existing warnings in unrelated compatibility/library
  code; the modified Autosave code introduced no compiler diagnostic.
- Final size:

  ```text
     text    data     bss     dec     hex
   363628     396   78444  442468   6c064
  ```

- BSS remains exactly 78,444 bytes, matching the pre-change baseline.
- `arm-none-eabi-nm -S --size-sort build/lxr02.elf` reports:

  ```text
  autosave_dirty_mask          0x0f10 = 3,856 bytes
  fs_autosave_parameter_cache  0x1200 = 4,608 bytes
  combined                              8,464 bytes
  ```

- Static searches confirm:
  - exactly one `AUTOSAVE_MASK_BYTES` static array allocation;
  - no `updated_mask` source symbol;
  - no `continue_pending` source symbol;
  - one runtime call to `autosave_transformDrainChunk()`;
  - CRC offset is published before the separate commit-offset phases.
- `git diff --check` passes.
- No file under `SD_CARD/` was modified.

### Hardware status

The seven hardware/SD acceptance tests above remain open for the user's retest.
No parameter-change detection work is included or considered approved by this
implementation.

### Hardware result — 2026-08-01, approximately 15-second partial drain

The user booted with the fully dirty generation-1/generation-0 pair in
`SD_CARD/.hcprms1` and `.hcprms2`, played/changing viewed Scenes for roughly 15
seconds, powered off, and copied the results to `SD_CARD/hcprms_post/`.

- Both returned files are exactly 34,768 bytes, carry commit `0xA5`, and pass an
  independent full-record CRC32C calculation.
- `.hcprms2` is generation 4/probe 3 with 12,461 dirty bits. It has completely
  drained Bank and Scenes 0-8, then partially drained Scene 9.
- `.hcprms1` is the newer generation 5/probe 4 with 6,620 dirty bits. It has
  completely drained Bank and Scenes 0-11, then partially drained Scene 12;
  Scenes 13-15 remain fully dirty.
- From the fully dirty input to generation 4, 18,387 bits cleared and 2,592
  payload bytes changed. From generation 4 to generation 5, another 5,841 bits
  cleared and 931 payload bytes changed.
- No bit changed from clear back to dirty in this test. No payload byte changed
  if its source-generation bit was already clear, and no changed payload byte
  remained dirty in the output generation.
- Bank output is slot 0, name `Full`, Scene-present mask `0xffff`, active Scene
  6, and VOICE-edit mask `0x0040`, matching `000 Full/bankset.bcg`. The drained
  40-byte Scene setting blocks inspected through Scene 11 match their
  `sceneset.scg` values, including decimation 127, MIDI channels 1-7, and MIDI
  notes 63.

Conclusion: this is a valid interrupted/partial bounded drain with four
successful post-input generations, not CRC corruption, mask loss, or a
clear-without-payload regression. A later run must remain powered long enough
for the remaining 6,620 bits to be classified/drained before the empty-mask
no-write test can be evaluated.
