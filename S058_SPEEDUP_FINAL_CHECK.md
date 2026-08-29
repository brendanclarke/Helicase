# Session 058 load/save speedup final check

Date: 2026-08-29

## Final decision

No further load or save speedup is proposed for implementation from
`S058_BANK_LOAD_SPEEDUP_PROPOSAL.md` or `S058_BANK_SPEEDUP_REVIEW.md`.

Bank Load works correctly and is explicitly out of scope. Do not change its
request, traversal, parsing, commit, completion, clean-authority, or UI paths.

## Original Option 3: implemented, slower, and reverted

Option 3 attempted to accelerate Bank Save by proving that an existing Scene
tree was canonical and then rewriting its ten files in their existing clusters.
Hardware testing measured the resulting Bank Save at approximately 15 seconds
slower than the Option 2 baseline, so the implementation was reverted.

Post-revert inspection found that the implementation speculatively attempted
the retained rewrite for every selected dirty child. When its strict proof
rejected a child, it paid for some or all of the following before still running
the complete normal save:

- an exact Scene-child lookup;
- full Scene and embedded-Kit directory scans;
- additional open, close, and directory transitions;
- a return to root and reopening of `/Bank/` and the target Bank; and
- the unchanged recursive delete, recreation, ten-file write, and flush.

The implementation also omitted the proposal's Scene/Kit/Instrument rename
handling. Any desired identity or Instrument-type filename difference therefore
caused rejection and fallback. All rejection conditions were collapsed into
the single `op_bank_inplace_bad` byte, so the historical hardware run cannot
identify which exact predicate rejected. The evidence proves that speculative
rejection added work; it does not prove that a successfully admitted retained
rewrite was intrinsically slower.

The reverted `afatfs_finalizeRetainedSize()` was also incomplete as a safe
general primitive: it assigned the new logical size without enforcing writable
normal-file mode, a legal first cluster, one-cluster capacity, cursor/size
agreement, or prevention of allocation beyond the retained cluster. It must not
be restored independently.

## Option 3B: proposed and rejected

Option 3B was considered as a narrower correction. It would have retained a
volatile, current-mount witness that this firmware had already created a given
Bank Scene layout. Only a later dirty save of that same witnessed child, with
unchanged Scene/Kit/Instrument names and types, would attempt the retained-file
rewrite. Fresh, legacy, renamed, different-Bank, remounted, and force-save cases
would have gone directly through the existing delete/recreate path without the
old speculative probe.

Option 3B is rejected and must not be implemented.

Its witness would start empty after every boot or mount. Consequently it would
save **no time at all on the first full Bank Save after a fresh boot**, which is
the workflow that needs improvement. It could help only a later same-Bank save
during the same mounted session after a successful earlier save established the
witness and payload edits subsequently made one or more Scenes dirty.

That benefit is too narrow and substantially overlaps Option 2:

- unchanged, card-proven Scenes are already skipped entirely by Option 2;
- Option 3B could accelerate only the remaining dirty children whose physical
  names/types had not changed; and
- fresh-boot, fresh-target, remount, rename/type-change, and force-save work
  would retain current timing.

The required layout authority, fingerprints, alias table, retained-write mode,
failure semantics, diagnostics, and hardware matrix are not justified for a
speedup that cannot improve the target fresh-boot full-save case. The detailed
Option 3B implementation plan has therefore been removed from this document.

## Closed paths

Do not restore Option 3 or implement Option 3B. Do not change Bank Load. The
previously rejected pack-file formats, larger caches/foreground bursts,
whole-file staging, compression, and hardware-SPI/DMA proposals also remain
closed. Any future Bank Save optimization must demonstrate a credible reduction
in the first full Save after boot before receiving an implementation plan.

## Stopped-playback filesystem ceiling investigation

### Conclusion

The stopped-playback filesystem can probably be driven somewhat harder than it
is now, but the fixed four-poll drain is no longer the most important Bank Save
bottleneck. The more important finding is an avoidable interaction between new
directory initialization and the LFN create scanner: each small Scene/Kit
directory is initialized as a full 32 KiB cluster, then LFN creation repeatedly
scans that entire cluster and removes its end marker. That produces megabytes of
directory traffic for a Bank whose actual file payload is only about 141 KiB.

The practical conclusions are:

- `FS_FAST_DRAIN_POLL_PASSES == 4` is an initial tuning value, not a proven
  hardware maximum. Eight or sixteen stopped-only polls may reduce time while
  transfer bytes, rather than card program-busy time, dominate.
- Increasing the poll count cannot remove SD-card program time or the current
  redundant directory traffic. A further four-times reduction from poll tuning
  alone is not credible.
- The first targeted implementation should be a FAT-terminator-aware LFN create
  fast path. It directly improves the required fresh-boot/full-save workflow,
  does not depend on volatile clean authority, and does not alter Bank Load.
- Whole-cluster lazy initialization is a second, larger optimization. It can
  remove almost all of the remaining directory-zero writes, but it needs a
  careful cross-sector terminator protocol and should not be combined with the
  first change.

The present approximately 30-second result is therefore not the practical
limit. A defensible engineering expectation is roughly **8--15 seconds** after
the terminator-scan fix while retaining full-cluster zero initialization, and
potentially **several seconds** after a separately proven lazy-initialization
change. These are traffic-derived planning ranges, not measured acceptance
figures. The exact lower bound is card-dependent and cannot be claimed until
block counts and program-busy time are measured on hardware.

### What the current stopped mode actually accelerates

`Core/Hardware/SD/filesystem.c` sets
`FS_FAST_DRAIN_POLL_PASSES` to four. While Menu owns a suspended codec and the
filesystem is busy, one foreground `filesystem_tick()` therefore calls
`afatfs_poll()` four consecutive times. The facade operation switch still runs
only once afterward.

`Core/Hardware/SD/asyncfatfs/sdcard_lxr02.c` transfers sixteen data bytes per
`sdcard_poll()` in `READING_DATA` or `WRITING_DATA`. Four lower-layer passes can
therefore clock at most 64 data bytes per outer filesystem tick. A 512-byte
sector needs 32 data-state polls, plus command/token/CRC and, for a write, an
unbounded number of polls until the card releases program busy. The recently
corrected 1,000 ms read-token and 5,000 ms write-busy deadlines use
`time_sysTick`, so increasing foreground poll density no longer shortens those
protocol deadlines.

The source documents about 7,600 `filesystem_tick()` calls per second in the
previous hardware workload. At that reference cadence, four 16-byte data polls
have an ideal payload ceiling of:

```
7,600 outer ticks/s x 4 polls/tick x 16 bytes/poll = 486,400 bytes/s
```

That is not a measured stopped-mode throughput: suspending audio changes the
main-loop cadence, and many polls transfer only a token/busy byte or perform
cache/FAT state work. It does show why four is not a physical limit. Eight or
sixteen passes can raise the scheduler ceiling until the bit-banged SPI loop or
the card's internal program time becomes dominant.

Do not change `SDCARD_BURST_SIZE` globally. The same sixteen-byte burst is used
outside this stopped-only owner and bounds the contiguous foreground latency
seen by playback, MIDI, the front panel, and the UI. More repeated sixteen-byte
polls under the existing stopped-mode gate preserve those preemption points.
Do not add an ISR/TIM5 caller: the SD shim explicitly requires one non-reentrant
context.

### Static traffic count for Bank 046

The checked `SD_CARD_BANK_NOPLAY_SAVE/Bank/046 Full` tree contains 161 files and
144,801 logical bytes. Rounded to the sectors each file actually touches, those
payloads require 353 512-byte sector writes: 65 one-sector files and 96
three-sector Instrument files.

A full replacement of an existing Bank child recreates sixteen Scene
directories and sixteen embedded Kit directories. The Bank directory itself is
reused; a brand-new target adds one more new directory. On the tested 32 KiB
cluster volume, each new directory owns 64 sectors.

`afatfs_extendSubdirectoryContinue()` currently clears all 64 sectors of every
new directory cluster before its mkdir callback completes. The normal existing-
target Bank Save therefore has this unavoidable traffic before considering FAT
and other metadata:

```
32 new directories x 64 sectors = 2,048 directory-zero writes
file payload                         353 payload writes
                                      --------------------
minimum initial/payload traffic     2,401 sector writes
                                   1,229,312 bytes (1.17 MiB)
```

A brand-new Bank target adds another 64-sector directory initialization. This
count excludes recursive deletion, free/allocate FAT updates, parent directory
entries and LFNs, size publication, cache flushes, `bankset.bcg`, HCNAMES,
`.hcindex`, and all reads.

### Newly identified dominant waste: LFN scans destroy the terminator

Every Scene/Kit object in this writer is opened through the LFN-capable API,
including 8.3-compatible names such as `sceneset.scg`, `kitset.kcg`,
`pattern.pat`, and `effects.fx`. For one Scene subtree that is four LFN create
scans in the Scene directory and seven in its Kit directory, or eleven scans.

The create scan in `afatfs_createFileContinue()` correctly records a contiguous
free run, but it does not treat a FAT `0x00` directory terminator as the end of
the live namespace. In create mode it calls
`afatfs_retireDirectoryTerminator()` for each zero entry and continues scanning
to the physical end of the allocated directory. On a newly zeroed 64-sector
cluster the first LFN creation consequently changes essentially every unused
entry from `0x00` to deleted marker `0xE5`, dirtying the entire cluster a second
time. Later creates find no terminator and scan the complete cluster again to
prove absence/collision before using the already-latched deleted run.

For the 32 recreated child directories alone, the deterministic work is:

```
16 Scenes x 11 LFN scans x 64 sectors = 11,264 directory-sector visits
32 first LFN scans x 64 sectors       =  2,048 extra dirty-sector writes
```

With only eight cache sectors, most of the repeated 64-sector sequential scan
visits must become physical reads. The figures omit the selected Bank
directory's own LFN scans and deletion of the previous tree, so they are
conservative. They explain how a tiny logical payload can still take about 30
seconds and why merely changing serializer buffers or retained file clusters
misses the dominant fresh-save cost.

At the 486,400-byte/s reference scheduler ceiling, the 11,264 scan sectors,
4,096 directory-sector writes (initial zero plus terminator retirement), and
353 payload sectors represent about 7.7 MiB and over sixteen seconds of ideal
data-byte clocking before command, token, card-busy, FAT, deletion, and final
sync time. The actual stopped cadence is not measured, so this is an explanatory
reference calculation, not a hardware timing assertion.

### Targeted first fix: stop LFN creation at the FAT terminator

This is a proposal only; no source change is authorized by this investigation.
It should be implemented and tested independently of drain-count tuning and
lazy directory initialization.

#### `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`: create-operation state

Add one private persisted terminator-run mode to `afatfsCreateFile_t`. It must
distinguish an ordinary deleted-entry run from a run selected after the live
directory terminator, including the case where an insufficient sector tail is
retired and the new run begins at entry zero of the following logical sector.

The adjacent code contract should say:

- **What:** records whether `afatfs_createLongDirectoryEntries()` must write a
  new `0x00` entry after the LFN/SFN run and whether the scanner is already
  beyond the old logical terminator while rolling to the next sector.
- **Why:** consuming the old terminator without publishing the next one can
  expose stale bytes after the newly created object; keeping a terminator lets
  later creates stop instead of scanning the entire allocated cluster.
- **Inputs:** the create scanner's current entry, required
  `lfnEntryCount + 1` run length, sector-local capacity, and create mode.
- **Outputs:** ordinary mode for a deleted-entry run whose existing terminator
  remains; terminator-owned mode for a run that consumes/moves the terminator
  and reserves one following entry in the selected sector; and a bounded
  past-terminator mode while advancing over an insufficient sector tail.
- **Lifetime/affiliates:** initialize/reset with the create scan and every alias
  collision restart; consume in `afatfs_createLongDirectoryEntries()`; private
  to `afatfsCreateFile_t`, `afatfs_noteFreeDirectoryEntry()`,
  `afatfs_retireDirectoryTerminator()`, and the create phases. No public header
  or API change is required.

#### `afatfs_createFileContinue()`: terminator decision

In the LFN create branch, distinguish deleted entries (`0xE5`) from the first
terminator (`0x00`) instead of treating both as an endless free scan.

The adjacent code contract should say:

- **What:** on the first terminator, finish collision/absence scanning and
  choose either an already-latched deleted run or a sector-local run beginning
  at the terminator. Prefer the current sector when it has room for all LFN
  fragments, the SFN, and one replacement terminator. If it does not, retire
  only the old terminator and remaining tail entries, advance through the
  directory's logical cursor to the next sector, and select a run at entry zero
  there with room for the replacement terminator.
- **Why:** FAT defines `0x00` as end of live directory entries, so no valid
  collision exists after it. Scanning and retiring the rest of a 32 KiB
  cluster is redundant and destroys the marker that future scans need.
- **Inputs:** `file->mode & AFATFS_FILE_MODE_CREATE`, `freeRunLatched`, current
  finder sector/index, `lfnEntryCount`, and the 16-entry sector boundary.
- **Outputs:** transition directly to
  `AFATFS_CREATEFILE_PHASE_CREATE_NEW_LFN_FILE` with the chosen run and
  replacement-terminator flag. Read-only/open-only behavior and all live-entry
  matching before the terminator remain unchanged.
- **Sector-boundary rule:** once the old terminator is seen, bytes after it are
  logically unused even if a host left stale nonzero data there. Do not parse
  them as live objects or resume collision checking. Convert only the skipped
  old-sector tail to deleted entries, obtain the next logical sector through
  the directory cursor/FAT chain, and create wholly within that sector. Do not
  assume the next sector is physically contiguous and do not let one LFN/SFN
  run cross a sector.
- **Affiliates:** `afatfs_freeRunIsReady()`,
  `afatfs_retireDirectoryTerminator()`, alias-collision restart, and
  `AFATFS_FILES_PER_DIRECTORY_SECTOR`.

If a suitable deleted run was latched before the terminator, select it and
leave the terminator untouched. The terminator proves that no matching object
or alias can occur later. This is both faster and preserves the existing
directory end marker.

#### `afatfs_createLongDirectoryEntries()`: publish the next terminator

After writing the LFN fragments and final SFN into a terminator-owned run,
clear the immediately following 32-byte directory entry in the same cached
sector. The scanner must have moved a too-late run to the next sector first, so
this writer never needs to dirty two sectors or follow the FAT chain itself.

The adjacent code contract should say:

- **What:** writes one complete zero directory entry after the new SFN only
  when the create-state flag says the selected run consumed the prior
  terminator.
- **Why:** the new object moves the logical end of directory. An explicit next
  terminator prevents old/stale sector contents from becoming visible and
  makes the following create terminate quickly.
- **Inputs:** the guaranteed sector-local run, `lfnEntryCount`, and the private
  replacement flag.
- **Outputs:** one cached dirty sector containing LFN fragments, SFN, and a
  valid following `0x00` end marker; clear/retire the flag before success or
  failure completion.
- **Affiliates:** `afatfs_cacheSector()`,
  `afatfs_cacheSectorMarkDirty()`, `afatfs_createFileContinue()`, and object
  enumeration's existing stop-at-terminator rule.

Do not zero the following entry when a deleted run before live entries was
selected; doing so would truncate the visible directory. This condition is why
the ownership flag must be explicit rather than inferred from the final entry
index.

No `filesystem.c`, `filesystem.h`, Menu, Bank Load, serializer, on-card schema,
or SD transport change belongs in this fix. The expected fresh full-save gain
comes from removing the repeated 64-sector scans and the second full-cluster
write, so unlike Option 3B it applies immediately after boot and on a fresh
target.

### Second-stage option: lazy initialization of directory sectors

After the terminator fast path is proven, a separate AsyncFATFS change could
stop zeroing all 64 sectors merely because FAT allocated a 32 KiB directory
cluster. A new directory initially needs only its first sector initialized with
`.` / `..` / terminator. Under the current always-LFN create API, a completed
Scene directory uses ten entries and retains its terminator in sector one. A
completed Kit uses all sixteen entries in sector one, so its sixth Instrument
creation must lazily clear sector two and place the replacement terminator
there.

Writing only the first sector would remove:

```
16 Scene directories x 63 deferred sectors = 1,008 writes
16 Kit directories x 62 deferred sectors   =   992 writes
                                                ------------
total avoided directory initialization       2,000 writes (1,024,000 bytes)
```

This cannot be implemented as simply deleting the loop from
`afatfs_extendSubdirectoryContinue()`. Before a later create consumes the final
terminator in an initialized sector, AsyncFATFS must clear the next logical
directory sector and move the terminator there. That continuation must follow
the FAT cluster chain rather than assume physical contiguity, must work after a
reboot with no RAM witness, and must preserve `.`/`..` only in the first sector
of the first child cluster. Directory extension, rename, delete-tree, and host-
created media require their own tests. This is a larger correctness change and
must remain a separate second-stage proposal.

A non-code way to reduce the same cost is formatting a test card with a smaller
FAT allocation unit. For example, 4 KiB clusters reduce each full-directory
initialization from 64 sectors to eight. Reformatting is destructive and card-
specific, so it is useful as a benchmark/control, not a firmware solution or a
default instruction.

### How to establish the actual practical limit

The existing trace does not record enough transport totals to separate
scheduler starvation from card latency. Before selecting eight, sixteen, or a
cycle-budgeted stopped drain, one diagnostic build should collect:

- `sdcard_lxr02.c`: completed CMD17/CMD24 block counts; poll count by transport
  state; data bytes clocked; total/max read-token wait; total/max write-busy
  wait; and rejected/timeout counts;
- `filesystem.c`: elapsed milliseconds, outer `filesystem_tick()` calls,
  fast-drain `afatfs_poll()` calls, and the maximum/average time spent in one
  stopped drain; and
- AsyncFATFS create diagnostics: terminators encountered, full-directory scan
  sectors, terminators retired, and directory-cluster sectors initialized.

The practical floor for that card is then decomposable as:

```
SPI transfer/command time
+ accumulated read-token wait
+ accumulated write-program-busy time
+ non-overlappable FAT/cache/create/delete CPU and final sync time
```

Run the same full Bank Save at four, eight, and sixteen stopped polls. Stop
increasing the budget when elapsed time no longer falls materially or front-
panel/MIDI latency becomes objectionable. If a fixed count remains sensitive
to unrelated main-loop work, replace it with a stopped-only elapsed-cycle
budget around repeated same-context `afatfs_poll()` calls; keep the facade
operation switch once per outer tick. The DWT cycle counter already exists in
the firmware, but filesystem ownership/observation must be designed explicitly
rather than reaching into AudioCodecManager's private accounting.

### Practical limit statement

There are three different limits and they must not be conflated:

1. **Current code, poll tuning only:** four is not proven optimal, so a result
   below 30 seconds is plausible. The present directory algorithm still moves
   roughly 7.7 MiB in the conservative child-only count and waits for thousands
   of single-block program operations. Expect diminishing returns; approximately
   20 seconds is a plausible floor, not a promise.
2. **Terminator-aware LFN create with full-cluster zeroing retained:** repeated
   directory scans and about 2,048 second-pass writes disappear, while the
   2,048 mandatory initial zero writes remain. A high-single-digit to low-teens
   result is a credible target; use **8--15 seconds** as the provisional
   engineering range until the counters exist.
3. **Terminator-aware create plus safe lazy sector initialization:** the static
   initial-directory/payload minimum falls from 2,401 writes to about 401 writes
   before metadata (`16 Scene first sectors + 16 Kit first/two-sector pairs +
   353 payload sectors`). The remaining 161 creates/closes, FAT updates,
   old-tree deletion, final HCNAMES/index work, and card busy time make
   sub-second saves unrealistic. A several-second result may be possible, but a
   reliable lower number cannot be stated without hardware measurements and
   power-loss/media-compatibility tests.

Bank Load remains out of scope. None of these findings justify restoring Option
3/3B, changing the Bank schema, enlarging global SD bursts, or moving the
filesystem into an interrupt.
