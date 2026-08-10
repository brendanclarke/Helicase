# HCPRMS Boot-Lock Diagnosis and Logging Plan

## Purpose and status

This document preserves the current evidence, the leading failure theory, and
the minimum diagnostic design needed if boot creation of `/.hcprms1` or
`/.hcprms2` locks again after rollback and targeted reimplementation.

This is a diagnosis and instrumentation plan, not an authorization to apply a
speculative FAT fix. The next code change must collect the state that separates
an AutoSave application-state error, an AsyncFATFS cluster-extension lock, a
cache deadlock, and an SD transport stall.

The CRC scheduling problem and this boot write lock are related only because
they occur in the same AutoSave setup transaction. They have different
evidence:

- Whole-record CRC work monopolized the cooperative loop and must be
  byte-bounded as specified by `SETTINGS_BANK_LOAD_REIMPLEMENT.md`.
- The 32,768-byte file is evidence of an SD/FAT write-extension failure after
  formatted data had already been generated. CRC chunking must not be claimed
  as a conclusive fix for that failure.

## Captured card evidence

The copied post-failure card state contains:

```text
SD_CARD/.hcprms1  34,768 bytes
SD_CARD/.hcprms2  32,768 bytes
SD_CARD/devlog.bin absent
```

`tools/inspect_autosave_fixture.py SD_CARD` reports:

```text
.hcprms1: VALID, generation=1, commit=0xa5, CRC=0x756a3060, mask_bits=0
.hcprms2: INVALID because size=32768, expected=34768
devlog.bin: absent
```

The first 128 bytes show that the partial B record has a normal HCPR header:

```text
.hcprms1 generation = 1, stored CRC = 0x756a3060
.hcprms2 generation = 0, stored CRC = 0xb7b9678e
```

A byte comparison through the end of the partial file finds only the expected
generation and stored-CRC header differences. Every other byte in the
32,768-byte common prefix matches the valid A record.

This establishes the following:

1. `/.hcprms2` was not an unrelated duplicate, empty create, or random short
   write. It is the deterministic generation-0 initial-record stream.
2. The application formatter and write cursor progressed through exactly
   32,768 bytes.
3. The failure occurred before the remaining 2,000 bytes became durable and
   before close/sync could publish the required 34,768-byte size.
4. The exact 32 KiB endpoint is strongly suggestive of a FAT cluster boundary.
   The actual card's `sectorsPerCluster` was not captured, so this remains a
   strong inference rather than a proven geometry fact.
5. Absence of `devlog.bin` means the post-timeout recovery logger also failed
   or was unable to make its result durable. It does not mean the primary
   timeout was absent.

## Current write path at the failure boundary

The current AutoSave ensure state machine in
`Core/Hardware/SD/filesystem.c` performs these relevant steps:

1. Read `/.hcnames` into the existing shared name cache.
2. Scan the root case-insensitively for each hidden target.
3. Leave any matching existing target untouched.
4. Prepare the initial-record CRC.
5. Open only a proven-missing target.
6. Format one staging chunk at a time.
7. Call `afatfs_fwrite()` repeatedly and track partial writes.
8. Close the new file and pass through the common flush/sync gate.

At the end of an allocated cluster, `afatfs_fwrite()` reaches this AsyncFATFS
path:

```text
afatfs_fwrite()
  -> afatfs_fileLockCursorSectorForWrite()
     -> afatfs_appendFreeCluster()
        -> afatfs_appendRegularFreeCluster()
           -> afatfs_appendRegularFreeClusterContinue()
```

The regular-cluster extension has these observable subphases:

```text
FIND_FREESPACE
UPDATE_FAT1
UPDATE_FAT2
UPDATE_FILE_DIRECTORY
COMPLETE
FAILURE
```

While extension is in progress, `afatfs_fwrite()` legitimately returns zero.
The application retries on later filesystem ticks. It can distinguish only an
explicit sticky `afatfs_isFull()` result; it currently cannot tell which
allocation, cache, or SD subphase has stopped progressing.

The boot wrapper continues pumping while the facade remains busy. The logging
build's ten-second watchdog eventually sets the facade status to error, then
the boot recovery path abandons the in-flight SD/AsyncFATFS state, remounts,
and attempts to write the retained failure label. That second path also
depends on the same card, transport, FAT implementation, root scan, and log
open/create logic.

## Leading theory

### Primary hypothesis: second-cluster extension stopped making progress

The strongest current hypothesis is:

1. `/.hcprms2` received its first allocated cluster successfully.
2. Its logical write cursor reached byte 32,768.
3. The next `afatfs_fwrite()` needed another regular cluster.
4. `afatfs_appendRegularFreeClusterContinue()` or one of its cache/SD
   dependencies remained in progress indefinitely rather than returning
   success or explicit failure.
5. Repeated application-level writes therefore returned zero with
   `afatfs_isFull()` still false.
6. The boot watchdog expired while the application remained in the same
   AutoSave ensure write phase.

The precise lock could be in any of these lower-level boundaries:

- `FIND_FREESPACE`: FAT search cursor or FAT-sector read never advances.
- `UPDATE_FAT1`: the new cluster's end-of-chain entry cannot be written.
- `UPDATE_FAT2`: the prior cluster cannot be linked to the new cluster.
- `UPDATE_FILE_DIRECTORY`: the file's directory entry cannot be updated.
- Cache ownership: a dirty or locked sector required by the extension never
  becomes available.
- SD transport: a read/write command or card-busy wait never completes, so its
  cache callback never releases the dependent AsyncFATFS phase.

The existing wrapped free-cluster search fix prevents a last-allocation hint
from falsely declaring the card full at the physical end of the FAT. It does
not prove that every cache/transport continuation inside allocation can make
progress. The new failure also ended at 32 KiB, while the earlier allocator
failure was observed at 16 KiB; actual card cluster geometry must be captured
rather than inferred from the earlier incident.

### Secondary hypothesis: transport/cache stall presents as an allocator lock

The allocator may be waiting correctly while the real failure is beneath it.
For example, `UPDATE_FAT1` can remain current because its dirty FAT sector is
waiting for an SD write completion. Logging only the append phase would then
misidentify the owner. The diagnostic must capture AsyncFATFS cache summary and
the private SD transport state at the same timeout instant.

### Less-supported explanations

The following explanations do not fit the captured evidence as well:

- **CRC generation caused the truncation:** the partial file already contains
  the precomputed header and a correct-looking formatted stream through 32
  KiB. The current creation path calculates CRC before opening the target.
  Whole-record CRC still causes unacceptable scheduler latency, but it does
  not explain an exact write-boundary truncation.
- **The two hidden names collided before creation:** `/.hcprms2` was opened and
  received 32 KiB of its own generation-0 stream. A pre-open alias collision
  does not explain that progress.
- **The card was positively full:** the ensure writer has an explicit
  zero-write plus `afatfs_isFull()` error path. A genuine full result should
  close the partial file and release boot rather than remain busy for ten
  seconds. The full flag still needs to be captured to verify this assumption.
- **Power was removed during an otherwise healthy write:** the observed boot
  timeout preceded inspection. A power cut can produce a partial file, but it
  does not explain the reported in-session timeout by itself.

## Why the existing failure log was insufficient

The operation-level boot label can identify `ASENSURE`, but it cannot identify:

- A versus B target;
- ensure state-machine phase;
- application bytes completed;
- the result of the last `afatfs_fwrite()`;
- file cursor/logical/physical size;
- active AsyncFATFS file operation;
- cluster append subphase and FAT search position;
- cache lock/dirty/write state;
- SD transport state, block, offset, or retry count.

The logger also attempts persistence only after abandoning and remounting the
same card that failed. In this capture no durable log file appeared. Possible
reasons include:

- the card/transport was still unhealthy after reset;
- remount exceeded its independent deadline;
- the duplicate-safe root proof or DEVLOG open/create failed;
- the log write or sync failed;
- the diagnostic consolidation path itself introduced a regression.

Because the recovery path currently returns only success/failure to `main.c`
and then boot continues, an absent file does not distinguish these cases.

## Diagnostic design

### Principles

The diagnostic must obey all of these constraints:

1. It is compiled only under `DEV_MODE_LOGGING`.
2. It performs no file I/O while AutoSave ensure owns the facade.
3. It adds no delay, pacing interval, LCD output, retry loop, or write to the
   observed transaction.
4. It records transitions and one latest-state capsule in SRAM, not every
   filesystem tick.
5. It snapshots low-level state before aborting SD transfer state or destroying
   AsyncFATFS.
6. It uses the single approved logging sink and never creates
   `hcprmslog.bin`, a numbered fallback, or another producer-owned file.
7. It never converts an AutoSave, logger, remount, or sync failure into success.
8. It adds no retained storage in logging-off builds.

The current project authority names `/devlog.bin` as the one logging sink. This
document does **not** authorize rebuilding the failed broad logging
consolidation as part of the CRC/Bank reimplementation. The HCPRMS producer
must remain filename-agnostic and attach to a separately verified,
duplicate-safe boot-failure append path. If rollback postpones that sink, do
not create a temporary permanent HCPRMS-specific file merely to bypass the
decision.

### One fixed failure capsule, not a ring

Reserve exactly eight eight-byte records, 64 bytes total, only when
`DEV_MODE_LOGGING` is enabled. The capsule always describes the latest active
HCPRMS ensure transaction. A valid/frozen flag may be packed into the records;
if implementation needs bytes outside the 64-byte record image, its exact RAM
cost must be stated and approved before code changes.

Use a non-printable, non-overlapping stage namespace such as `0xE0` through
`0xE7`. Printable eight-byte records remain boot labels, uppercase letters
remain AutoSave lifecycle records, and lowercase letters remain the existing
save-fix family. Final stage values must be checked against the real decoder
at implementation time.

Proposed record layouts, all little-endian:

| Stage | Remaining seven bytes |
| --- | --- |
| `0xE0` context | schema version, target A/B, ensure phase, filesystem state, file operation, append phase, flags |
| `0xE1` application progress | `op_bytes_done:u32`, current chunk length `u16`, zero-write streak `u8` |
| `0xE2` current chunk | last `fwrite` result `u16`, chunk offset `u16`, requested bytes `u16`, target generation `u8` |
| `0xE3` file cursor | cursor offset `u32`, logical size as bounded `u24` |
| `0xE4` allocation | search cluster `u32`, sectors per cluster `u8`, wrapped-search flag `u8`, allocator flags `u8` |
| `0xE5` allocation owner | previous cluster `u32`, cursor cluster as bounded `u24` |
| `0xE6` cache summary | dirty, locked, read-pending, write-pending, flush-pending, active cache index, filesystem-full flag |
| `0xE7` SD transport | SD state, operation, transfer offset `u16`, retry count `u16`, elapsed-time/result flags |

If the real card can use cluster numbers wider than the proposed bounded
fields, revise the layout before implementation. Do not silently truncate a
value needed to distinguish the failure. It is acceptable to use one more
eight-byte record after declaring the revised SRAM cost.

### Application-level hooks in `filesystem.c`

Add RAM-only hooks at these transitions in
`filesystem_ensureAutosaveFiles_tick()`:

- ensure request accepted;
- target A/B selected;
- root scan classified target present or missing;
- CRC preparation begun and completed;
- write-capable open requested and callback result received;
- each new formatted chunk begun;
- each nonzero `afatfs_fwrite()` result;
- first zero-byte write and saturation/max update for subsequent consecutive
  zero-byte writes;
- explicit filesystem-full detection;
- close requested and close callback received;
- common flush entered and terminal result published.

The hook that sees `afatfs_fwrite()` must retain both application coordinates:

```text
durable-intent offset = op_bytes_done
current chunk offset = op_write_line_offset
current chunk length = op_write_line_len
last requested count
last returned count
```

`op_bytes_done` advances only after a complete staging chunk, so logging it
alone can hide partial progress inside the current 512-byte chunk.

Do not append a diagnostic record on every write call. Update the fixed capsule
in place. A saturated zero-write streak is enough to show repeated
back-pressure without allocating a trace ring.

### Read-only AsyncFATFS snapshot hook

Add a logging-only read API in `asyncfatfs.h/.c` that copies a bounded snapshot
without polling, allocating, changing a phase, releasing a cache lock, or
starting I/O. It must be callable while the file is busy.

For the active target handle, capture:

- filesystem state and sticky full flag;
- `cursorOffset`, `logicalSize`, and `physicalSize`;
- `cursorCluster` and `cursorPreviousCluster`;
- current file operation enum;
- append-free-cluster phase;
- `previousCluster`, `searchCluster`, `searchStartCluster`, and
  `searchWrapped` when that operation is active;
- mounted `sectorsPerCluster` and calculated cluster size;
- counts of cache descriptors that are dirty, locked, reading, writing, or
  participating in an active flush;
- the active cache descriptor index when one can be identified safely.

The API must report “not applicable” sentinels when the file is not in a
regular cluster-append operation. It must not expose a pointer that becomes
invalid after `afatfs_destroy(true)`; copy values into the caller's capsule.

### Read-only SD transport snapshot hook

Add a logging-only getter in `sdcard_lxr02.h/.c` that copies, without changing:

- private `sdcard_state_t`;
- read/write operation kind;
- transfer block;
- transfer byte offset;
- retry count;
- whether a completion callback is pending.

Capture this before `sdcard_abortTransferForBootLog()` clears those fields.
The getter must not include `config.h` in the low-level public header merely to
obtain the build flag; use the project's established conditional-compilation
direction or an always-declared no-op implementation.

### Freeze point at timeout

In `filesystem_bootLoggingPollDeadline()`, when the armed operation is
`ASENSURE`, perform exactly one freeze operation before setting facade status
to error:

1. Copy application coordinates already accumulated in the capsule.
2. Copy the active-file AsyncFATFS snapshot.
3. Copy SD transport state.
4. Record elapsed time and the primary timeout flag.
5. Mark the capsule frozen so recovery code cannot overwrite it.

Only after that snapshot may the recovery path:

```text
sdcard_abortTransferForBootLog()
afatfs_destroy(true)
filesystem facade reset
SD reinitialization and remount
```

This ordering is mandatory. Capturing after abort/destroy would record the
logger recovery state rather than the failure.

### Durable append after recovery

After a successful recovery remount, append one bounded batch:

1. The existing eight-byte printable culprit label, expected to be
   `ASENSURE`.
2. The frozen 64-byte HCPRMS capsule.

The resulting 72-byte append remains a multiple of eight. Close and sync must
complete before the capsule is acknowledged. On append or sync failure, keep
the capsule marked undelivered in SRAM and return a real logger failure.

Do not choose another same-name entry, create a fallback filename, or discard
the capsule merely because recovery logging failed. If runtime continues with
a healthy mounted facade, one separately bounded, lowest-priority retry may be
considered, but it must not remount repeatedly or compete with settings,
AutoSave, or foreground work. If recovery left the card unavailable, the
physical limitation remains: firmware cannot guarantee an SD-resident log of
the failure of that same SD subsystem.

For a focused hardware capture, an already-existing, unique, host-created
`devlog.bin` whose allocated cluster has room for the 72-byte append can reduce
the chance that the recovery logger itself needs file creation or cluster
extension. This is an optional diagnostic fixture, not production behavior,
and it changes card allocation layout; compare results with an otherwise
identical card image and do not treat disappearance of the failure as proof of
a fix.

### Recovery-outcome observability

The recovery function should return a typed reason rather than one boolean,
at minimum:

```text
primary snapshot captured
SD reinit failed
remount failed or timed out
DEVLOG singleton proof failed/duplicate
DEVLOG open failed
DEVLOG write stalled/full
DEVLOG close failed
DEVLOG sync failed
snapshot durably appended
```

When the file append succeeds, include the recovery result in the last capsule
record. When it cannot succeed, retain the reason in SRAM for SWD inspection
and any safe later retry. No design can make the same failed SD card guarantee
its own durable failure report; documentation and UI must not imply otherwise.

### Host decoder

Update the one DEVLOG-aware host decoder, including
`tools/inspect_autosave_fixture.py` if it remains the current entry point, to:

- recognize `0xE0..0xE7` only in the declared order and schema version;
- decode every integer explicitly as little-endian;
- preserve and print the preceding ASCII boot label;
- calculate cluster bytes from captured sectors-per-cluster;
- state whether `op_bytes_done` is exactly at a cluster boundary;
- print raw record hex for an unknown schema rather than guessing;
- flag incomplete HCPRMS batches;
- continue treating absent DEVLOG as “logger unavailable,” not “no timeout.”

The decoder is read-only and must never repair either hidden record or the log.

## Interpreting the next capture

| Frozen state | Supported conclusion | Next investigation |
| --- | --- | --- |
| `bytes_done=32768`, append phase `FIND_FREESPACE`, search cluster unchanged | FAT search is not progressing | Trace FAT-sector cache request/completion and search bounds; do not change writer pacing |
| `UPDATE_FAT1` | New cluster EOC write is blocked | Inspect FAT copy 1 cache/write completion |
| `UPDATE_FAT2` | Link from prior cluster is blocked | Inspect prior-cluster FAT sector and dual-FAT update ordering |
| `UPDATE_FILE_DIRECTORY` | Allocation happened but directory metadata cannot commit | Inspect root directory cache ownership/save result |
| SD state is read/write/busy with unchanged offset/retry | Transport prevents AsyncFATFS progress | Add/verify bounded command or busy-response timeout in that exact SD state |
| Cache has a locked/dirty descriptor with no transport owner | Cache ownership deadlock | Trace the descriptor's owner and release path |
| `filesystemFull=1` | Explicit exhaustion path occurred | Verify full-card truth and ensure close/error path itself is not stuck |
| file operation is `NONE`, not full, but writes remain zero | Not a cluster append; cache/handle state is blocking write admission | Inspect sector lock and file cursor invariants |
| ensure phase is close/flush, not write | The 32 KiB file was only the last durable size; lock is later | Diagnose close/sync instead of allocator |
| recovery remount succeeds but DEVLOG singleton/open fails | Primary and logger failures are distinct | Fix only the proven duplicate-safe logger boundary before repeating |

Do not patch multiple rows at once. The frozen state chooses the next code
boundary.

## Reproduction and test procedure

Run this only on a copied test card whose contents can be preserved.

1. Start from a valid `/.hcprms1` and remove only `/.hcprms2`, matching the
   observed create-B path.
2. Confirm free space and preserve a raw or file-level pre-test card image.
3. Build with `DEV_MODE_LOGGING=1` and the capsule enabled.
4. Boot once and do not power-cycle during the ten-second diagnostic/recovery
   window.
5. If boot continues, power down normally and copy the complete card state.
6. Record exact hidden-file sizes before any host repair tool changes the FAT.
7. Run the host AutoSave/DEVLOG decoder.
8. Preserve the entire log, both hidden records, card geometry, and firmware
   image as one fixture.
9. Repeat from the identical pre-test image only when checking reproducibility;
   do not merely delete the truncated B from a potentially altered FAT state.

Required comparisons:

- diagnostic build with the capsule;
- logging-off build, proving no capsule storage, getters, or file activity;
- optionally, identical images with and without a pre-existing unique DEVLOG,
  explicitly recognizing that this changes allocation layout.

## Implementation files, if authorized

| File | Targeted change |
| --- | --- |
| `config.h` | Diagnostic schema/budget constants under `DEV_MODE_LOGGING`; no delay or new filename |
| `Core/Hardware/SD/filesystem.c` | Application capsule updates, timeout freeze, and bounded batch handoff to the verified boot logger |
| `Core/Hardware/SD/filesystem.h` | Only externally required typed result declarations; detailed ownership comments |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` | Read-only file/allocator/cache snapshot implementation |
| `Core/Hardware/SD/asyncfatfs/asyncfatfs.h` | Snapshot types/API contract, with no mutation or polling side effects |
| `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c` | Read-only transport snapshot implementation before abort clears state |
| `Core/Hardware/SD/asyncfatfs/sdcard_lxr02.h` | Snapshot declaration or always-present no-op contract |
| DEVLOG host decoder | Decode the reserved HCPRMS record family and incomplete batches |
| `knowledge_files/specification_reference/DEV_MODES.md` | Reserve the record family and document acknowledgement/failure behavior |
| `knowledge_files/specification_reference/AUTOSAVE.md` | Link the hidden-record creation failure capsule without duplicating its format |

Every C/H change must have adjacent comments describing what is captured, why
the value is needed, when it is valid, whether it is an input or output, and
which state machine owns it. Packed fields, saturation, cluster arithmetic,
and every low-level getter require documentation in place.

## Non-goals and prohibitions

- Do not increase the ten-second primary timeout as a fix.
- Do not add a sleep or one-millisecond pacing interval.
- Do not reduce the AutoSave record size or silently accept 32,768 bytes.
- Do not mark a truncated record valid or synthesize its missing tail on load.
- Do not delete the partial file automatically before preserving evidence.
- Do not rewrite FAT allocation, cache, SD transport, and the application
  writer in one prospective patch.
- Do not perform logging I/O from inside `afatfs_fwrite()` or allocator
  continuation code.
- Do not create a dedicated HCPRMS log file or fallback name.
- Do not let logger failure mask the original AutoSave failure.
- Do not attribute the lock conclusively to CRC generation.

## Exit criteria for diagnosis

Diagnosis is complete only when one captured failure identifies all of:

- target A or B;
- application ensure phase and exact byte progress;
- last write request/result;
- actual cluster size and whether the cursor is at its boundary;
- active AsyncFATFS file operation and append subphase;
- relevant allocator cursor/flags;
- cache summary;
- SD transport state;
- primary timeout identity;
- recovery logger outcome.

Only then should a separate targeted fix be written for the demonstrated
stalled boundary.
