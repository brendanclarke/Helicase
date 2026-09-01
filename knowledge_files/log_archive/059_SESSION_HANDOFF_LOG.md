# Session 059 — Handoff Log

```
DATE: 2026-08-30 to 2026-08-31
BRANCH: dev-ph3-autosave-ph5
SOURCE HISTORY: 53a7676 (Phase One plan), 3dc9a4b (Gate A and Instrument
  repair), d28f8f9 (direct Instrument-index open), 4067099 (comment wording
  correction), 0c90434 (Phase Two plan), plus the uncommitted Gate B and
  documentation closeout.
SESSION GOAL: Remove AsyncFATFS directory-create overhead without changing
  product formats or workflows; investigate and repair the Instrument Load
  regression found during testing; complete the sensible second directory-
  initialization gate.
COMPLETED:
  - Gate A: terminator-aware short create, LFN create, and same-parent rename.
  - Typed Instrument index producer validation, consumer validation, selected-
    type repair, terminal-safe Menu completion, and a read-only validator.
  - Typed Instrument index direct-open fast path; no preliminary directory
    enumeration when `.hcindex` is healthy.
  - Gate B: appended directory clusters initialize only their first sector;
    later sectors remain hidden until Gate A clears them before exposure.
VERIFIED ON HARDWARE:
  - Gate A: partial. A stopped-playback Bank Save fell from the Session 058
    approximately 30-second result to about 10 seconds. Kit Load, Kit Save,
    Scene Load/Save, and Instrument Save were reported working. Instrument
    Load exposed the separate index defect described below.
  - Instrument repair/direct-open change: not retested on hardware.
  - Gate B: deliberately not tested on hardware. Source/build review found no
    expected problem; hardware/media acceptance is not claimed.
KNOWN ISSUES INTRODUCED: None identified by source review or builds.
KNOWN ISSUES RESOLVED:
  - Repeated create/rename scans to physical directory exhaustion after 0x00.
  - Redundant full-cluster initialization of new directory clusters.
  - Blank/reserved typed Instrument rows and missing/corrupt typed indexes
    producing empty browsers and InsL10.
  - Redundant physical Instrument-directory prescan before a healthy index.
NEXT SESSION RECOMMENDED GOAL: Continue the current product roadmap. Session
  059 hardware testing is optional deferred validation, not a suspected-fault
  investigation.
BLOCKERS: None.
```

---

## 1. Disposable Session 059 documents

This log preserves the durable content of the four root documents the user
plans to delete:

| Document | Durable content retained here |
| --- | --- |
| `S059_ASYNCFATFS_SPEEDUP.md` | Evidence, two-gate decision, compatibility rules, projected I/O saving, final disposition |
| `S059_ASYNCFATFS_PHASE_ONE.md` | Exact Gate-A reservation/publication design, implementation, build and hardware status |
| `S059_INST_LOAD_FIX.md` | Card evidence, direct failure chain, typed-index repair, direct-open correction, verification status |
| `S059_ASYNCFATFS_PHASE_TWO.md` | Exact Gate-B initialization change, future Pattern assessment, SRAM constraint, final review |

Current behavior is authoritative in `ASYNCFATFS_REFERENCE.md`,
`FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`, and `SRAM_MANIFEST.md`.

## 2. Initial evidence and two-gate decision

The Session 058 stopped-playback fast drain reduced a full Bank Save to about
30 seconds, but AsyncFATFS still did avoidable directory work.
`SD_CARD_BANK_NOPLAY_SAVE/Bank/046 Full` contained 161 files and 144,801
logical bytes. Their payloads occupied at least 353 512-byte sectors. A full
Bank replacement creates 16 Scene and 16 embedded Kit directories. On the
observed 32 KiB-cluster card, each directory cluster has 64 sectors, so the old
initializer queued at least 2,048 zero-sector writes for those 32 directories.

The old create scan also treated `0x00` and `0xE5` alike: it retired each
terminator and continued toward allocated EOF. This dirtied and revisited most
of a fresh directory cluster even though FAT defines the first `0x00` as the
end of the live namespace.

The work was split deliberately:

1. Gate A made all current directory-entry writers preserve and use the FAT
   terminator while retaining full-cluster initialization.
2. Gate B, implemented only after Gate A was reviewed and exercised, removed
   the now-redundant initialization of sectors hidden after that terminator.

This kept Gate B independently revertible. No Bank state machine, serializer,
file format, SD burst, cache size, polling cadence, or playback-dependent FAT
branch was added.

## 3. Binding FAT directory rules

The implemented rules are:

- `0x00` ends the live namespace. Collision scans stop there.
- `0xE5` is reusable but does not prove absence. Scanning continues so a later
  live display-name or alias collision cannot be duplicated.
- SFN and LFN/SFN runs remain within one 16-entry directory sector.
- A deleted-run write touches only the selected entries.
- A run consuming `0x00` also writes one complete replacement zero entry.
- If run plus marker does not fit, the complete run moves to entry zero of the
  next logical sector reached through the directory cursor and FAT chain.
- The moved target sector is cleared, populated, and persisted before the old
  marker-through-sector-end tail is changed to `0xE5`.
- A directory with no terminator uses a proven deleted run or extends at
  allocated EOF. A full FAT16 fixed root fails normally.
- Completed create/rename remains non-transactional across arbitrary power
  loss; the change prevents stale post-marker exposure but does not add a
  journal.

The target-before-tail barrier is why moving `0x00` is not itself considered a
major risk. Before retirement the old marker remains authoritative; after
retirement the new sector and replacement marker have already reached media.

## 4. Gate A implementation

### 4.1 Private state and phases

`Core/Hardware/SD/asyncfatfs/asyncfatfs.c` now has one shared run origin:

- `AFATFS_DIRECTORY_RUN_ORIGIN_NONE`
- `AFATFS_DIRECTORY_RUN_ORIGIN_DELETED`
- `AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_LOCAL`
- `AFATFS_DIRECTORY_RUN_ORIGIN_TERMINATOR_MOVED`

`afatfsCreateFile_t` reuses its previous free-run byte/pointer budget as:

- `requestedEntryCount`: one for SFN, `lfnEntryCount + 1` for LFN/SFN;
- `deletedRunLength`;
- `reservationOrigin`;
- `scanRunStart`: current deleted run, later reusable as the saved old marker;
- `selectedRunStart`: latched deleted run or final target.

The create phases are `INITIAL`, `FIND_FILE`, `SEEK_NEXT_SECTOR`,
`PREPARE_TARGET_SECTOR`, `CREATE_NEW_FILE`, `CREATE_NEW_LFN_FILE`,
`WAIT_TARGET_PERSISTENCE`, `RETIRE_OLD_TERMINATOR_TAIL`, `SUCCESS`, and
`FAILURE`. Rename uses `INITIAL`, `FIND_SOURCE`, `LOAD_SOURCE_ENTRY`,
`PREPARE_NEW_NAME`, `COLLISION_SCAN_BEGIN`, `COLLISION_SCAN`,
`PREPARE_NEW_RUN_SEEK`, `PREPARE_NEW_RUN_TARGET`,
`WAIT_TARGET_PERSISTENCE`, `RETIRE_OLD_TERMINATOR_TAIL`, `WRITE_NEW_RUN`,
`RETIRE_OLD_RUN`, and `FINISH`. `afatfsRenameObject_t` continues to embed
`afatfsCreateFile_t`; no second allocator state was added.

Compile-time ceilings enforce:

```text
sizeof(afatfsCreateFile_t)   = 144
sizeof(afatfsFile_t)         = 188
sizeof(afatfsRenameObject_t) = 552
```

### 4.2 Shared reservation helpers

The old LFN-specific free-run helpers and independent short-entry allocator
were replaced by:

- `afatfs_resetDirectoryRunReservation()`: clears reservation progress without
  destroying request name, alias, callback, or requested count.
- `afatfs_noteDeletedDirectoryEntry()`: tracks sector-local `0xE5` runs and
  latches only the first adequate candidate while collision scanning continues.
- `afatfs_selectDirectoryRunAtTerminator()`: prefers the latched deleted run,
  otherwise selects a local terminator run with marker room or records a moved
  terminator.
- `afatfs_prepareDirectoryRunTarget()`: advances through the logical FAT chain,
  extends when required, obtains WRITE cache ownership, clears all 512 target
  bytes, and selects entry zero.
- `afatfs_directoryRunTargetPersistence()`: accepts an IN_SYNC cache descriptor
  or an already-evicted descriptor as proof that the target reached media;
  DIRTY/WRITING continues to yield.
- `afatfs_retireDirectoryTerminatorTail()`: after that proof, changes the saved
  marker and every following entry in its sector to deleted.

The persistence helper uses existing cache descriptors and adds no retained
witness. Descriptor absence is valid only because a dirty cache block cannot
be evicted before synchronization.

### 4.3 Short and LFN create/open

`afatfs_createFileContinue()` now:

- initializes one shared reservation for short and LFN requests;
- continues through deleted holes and all live entries;
- ends absence/collision scanning at the first `0x00`;
- fails open-only requests there;
- selects deleted, local-marker, or moved-marker placement for create requests;
- resets reservation and LFN scan state on each deterministic `~N` alias
  restart; and
- handles physical exhaustion without a terminator by using a proven hole or
  extending an eligible directory.

`afatfs_createShortDirectoryEntry()` writes the selected SFN directly. It
zeros the following entry only for a terminator-owned selection.

`afatfs_createLongDirectoryEntries()` retains the existing VFAT fragment order,
checksum, SFN alias, case bits, timestamps, attributes, and SFN publication. It
also zeros the following entry only for a terminator-owned selection. Neither
writer performs an allocation rescan.

New directory create still hands its original callback to
`afatfs_handoffCreatedDirectoryToInitializer()` after the name run is safe.

### 4.4 Rename

Same-parent `afatfs_renameObject_lfn()` now uses the same reservation rules.
It still:

- validates and copies the source object/SFN;
- excludes the source from collision checks;
- prefers a valid in-place rewrite, then a deleted run, local marker, or moved
  marker;
- preserves cluster, size, attributes, timestamps, and children;
- writes the new complete name before deleting the old complete name run; and
- returns structured results and publishes `openNameOut` only on OK.

A moved rename additionally waits for the target, retires the old terminator
tail, then retires the source name run. Power loss may still leave both old and
new names, as before.

### 4.5 Gate-A build and hardware result

The Gate-A logging-on link reported:

```text
text=383,956  data=404  bss=96,160
.text=371,024  .bss=89,488
afatfs=6,984
```

All three layout assertions and `git diff --check` passed. Warnings were the
existing AsyncFATFS unused parameter, unused static helpers, nano syscall, and
LTO warnings.

The user observed a stopped-playback Bank Save at about 10 seconds, versus the
approximately 30-second Session 058 result. The copied output is retained in
`SD_CARD_AFAT_RESULT/`. This is a user observation, not a controlled two-trial
benchmark or a Gate-B measurement.

The same hardware pass reported:

- Kit Load appeared correct. Rapid list scrolling once produced filesystem
  errors, but the error did not reproduce after reboot; the user suspected an
  invalid Kit sanitize/rename attempt and explicitly deferred it.
- Kit Save worked.
- Scene Load and Scene Save appeared correct.
- Instrument Save worked and produced
  `SD_CARD_LOAD_SAVE_TEST_RESULT/Instrument/Drum/goad1  -.drm`.
- Instrument Load and InstrumentMrp browsing failed, leading to the separate
  repair below.

No raw-sector inspection, host FAT check, controlled payload-hash comparison,
FAT16 run, fragmented-chain fixture, or repeatable timing pair was completed.

## 5. Instrument Load failure: evidence and root cause

The failure occurred before Instrument payload parsing. The saved
`goad1  -.drm` file is a valid 1,368-byte descriptor-keyed Instrument text file
and was not opened by the failing browser selection.

`SD_CARD_LOAD_SAVE_TEST_RESULT/` proved:

- `Instrument/Drum/.hcindex` existed;
- Snare, Cymbal, and HiHat `.hcindex` files were absent;
- Drum contained 99 user `.drm` files plus `.hctmp.drm`, while its index had
  100 rows;
- Drum row 1 was eight spaces followed by LF.

The old producer filtered only a case-sensitive display spelling of `.hctmp`.
The old consumer accepted any nonzero field length, including eight spaces.
Selecting that row reached `storage_makeSavedInstrumentDisplayFilename()`,
whose intentional blank-stem fallback produced `none.drm`. That file did not
exist, so `filesystem_loadInstrument_tick()` completed at hexadecimal phase
`0x10`, displayed as `InsL10`.

The missing boot indexes could not be diagnosed retrospectively: the boot
blocking wrapper discards its ordinary zero result and runtime tracing begins
later. Boot regenerates all `.hcindex` files, but runtime recovery remains
useful for an interrupted boot write, host-edited/corrupt metadata, or a later
failed rewrite. This was kept as a rare fallback rather than made boot-fatal.

No evidence implicated the Gate-A allocator, Instrument serializer, HCNAMES,
Preset commit, or DSP apply path. Those layers were left unchanged.

## 6. Instrument typed-index repair

### 6.1 Producer filtering

`Core/Hardware/SD/filesystem.c` added:

- `filesystem_isReservedInstrumentTemporaryName()`: case-insensitive exact
  `.hctmp.<ext>` recognition plus `HCTMP.EXT` and `HCTMP~N.EXT` short-alias
  recognition for the selected registered type.
- `filesystem_instrumentStemIsUsable()`: requires at least one printable,
  non-space character in the normalized eight-cell stem.
- `filesystem_instrumentStemIsReservedTemporary()`: applies the same reserved
  rule to a stem read from `.hcindex`.

`filesystem_recordInstrumentFile()` now rejects reserved display/open names and
blank derived stems before retagging, sorting, incrementing, or otherwise
mutating the shared cache. It retains the 1,000-row ceiling, registered
extension classification, casefold duplicate identity, deterministic order,
and post-save insertion. Meaningful spaces and hyphens in `goad1  -` remain
valid.

The same reserved predicate is used by physical Instrument scans and post-save
cache replacement so boot and runtime cannot regenerate the hidden row.

### 6.2 Consumer validation

`filesystem_validateInstrumentIndexRow()` treats typed rows as compact keys,
not numbered placeholders. It rejects:

- empty or all-space rows;
- non-printable characters;
- stems longer than eight characters;
- reserved temporary names/aliases;
- more than 1,000 rows;
- folded duplicates; and
- non-strict casefold-then-case ordering.

Shorter valid legacy rows are normalized into the fixed display cell. A bad
row invalidates the complete index; an accepted prefix is not published.

`filesystem_instrumentIndexFATFatal()` reads the existing AsyncFATFS diagnostic
snapshot to distinguish missing/corrupt metadata from the global fatal FAT/SD
state without adding recovery storage.

### 6.3 Selected-type recovery

`filesystem_beginInstrumentIndexRecovery()` transfers the original accepted
request callback to the existing library-index rebuild owner, releases the
LOAD_INDEX operation without an intermediate completion, and starts exactly
one selected-type scan:

```text
physical selected-type scan
  -> filesystem_libraryIndexRebuildScanComplete()
  -> FS_INTERNAL_OP_CREATE_BOOT_INDEX for that type
  -> filesystem_libraryIndexRebuildWriteComplete()
  -> final afatfs_sync()
  -> filesystem_completeLibraryIndexRebuild()
  -> original callback once
```

Missing, empty, or structurally invalid metadata uses this chain. A genuine
read, close, scan, write, or fatal FAT/SD failure remains `FS_STATUS_ERROR` and
does not authorize metadata replacement. A truly empty type may rewrite an
empty index and can be rescanned on later entry; no persistent empty-proof bit
was added.

### 6.4 Menu completion and public contract

`menu_instrumentIndexLoadComplete()` now snapshots terminal success, calls
`filesystem_ack()` for both terminal outcomes, and publishes/clamps the cache
only on success. Failure clears the unusable cache and deferred selection,
releases `menu_storageBusy`, shows the existing filesystem error overlay, and
cannot dispatch an Instrument payload from partial metadata.

`filesystem.h` documents that
`filesystem_requestLoadInstrumentIndex(type, cb)` reads a healthy index or
transparently performs one selected-type scan/write/sync repair, invokes its
callback once, and returns real I/O failures as errors. No signature changed.

### 6.5 Read-only host validator

`tools/verify_instrument_indexes.py` validates all four registered type
directories. It is read-only and checks:

- index presence;
- ASCII, nonblank, one-to-eight-character rows;
- reserved temporary exclusion, including alias forms;
- strict sorted uniqueness;
- registered extension and usable physical stems; and
- exact case-insensitive row-to-one-physical-file correspondence.

Against `SD_CARD_LOAD_SAVE_TEST_RESULT/`, it exits 1 with exactly four known
defects: Drum row 1 all-space and the missing Snare, Cymbal, and HiHat indexes.
Against `SD_CARD_AFAT_RESULT/`, it reports the four missing typed indexes.

### 6.6 First repair build

The clean logging-on repair build reported:

```text
text=385,380  data=404  bss=96,176
.text=372,448  .bss=89,504
build/lxr02.bin=385,784
afatfs=6,984  fs_list_cache_name=9,000
```

`make clean && make`, `make img`, `git diff --check`, Python syntax/runtime
checks, and the expected failing fixture run passed. No retained cache, array,
or operation-state allocation was added.

## 7. Direct typed-index fast-path correction

Review of the first repair found that a healthy request still enumerated the
complete selected type directory merely to prove `.hcindex` existed, then
opened and read the index. That defeated the index's purpose and made the
roughly 100-file Drum directory the slowest normal case.

The first appended plan for this correction was rejected and deleted because
it did not identify exact code-file edits. It was rewritten to specify every
removed phase and replacement block in `filesystem_loadInstrumentIndex_tick()`.

The implemented correction changes only `Core/Hardware/SD/filesystem.c`:

- phases 9–11 close the copied type-directory handle and open `.hcindex`
  directly;
- a nonfatal NULL open goes to selected-type recovery;
- a fatal NULL open goes to terminal error without scan/write;
- a real handle enters existing row validation;
- empty/corrupt opened files use the existing close/recovery phases;
- real opened-index read errors use the existing close/error phases; and
- the old presence iterator and phases 12, 13, and 15 were removed.

The healthy path is `8 -> 9 -> 10 -> 11 -> 17`. No
`afatfs_findFirstObject()`, `afatfs_findNextObject()`,
`afatfs_findLastObject()`, `op_object_finder`, or `op_object` remains in this
FSM.

The missing/corrupt recovery and fatal-error classification were retained as
uncommon but valid fallbacks. The user authorized only the direct performance
correction; no additional fallback or validator change was folded into it.

The direct-open build reported:

```text
text=385,580  data=404  bss=96,176
build/lxr02.bin=385,984
build/LXRV2_lxr02.img=386,000
```

One review wording was corrected: direct open does not make lookup strictly
O(index bytes), because `afatfs_fopen_lfn()` still performs the required name
lookup. The accurate claim is that the redundant preliminary physical
directory walk is gone. No executable code changed for that correction.

## 8. Gate B implementation

### 8.1 Directory extension

`AFATFS_EXTEND_SUBDIRECTORY_PHASE_WRITE_SECTORS` was renamed privately to
`AFATFS_EXTEND_SUBDIRECTORY_PHASE_INITIALIZE_FIRST_SECTOR`.
`afatfsExtendSubdirectory_t` retained its existing append sub-operation, phase,
parent cluster, and callback; no field was added.

The sole-use `afatfs_fileGetCursorClusterAndSector()` helper was deleted.

After `afatfs_appendRegularFreeClusterContinue()` succeeds,
`afatfs_extendSubdirectoryContinue()` now:

1. maps the appended-cluster cursor with
   `afatfs_fileGetCursorPhysicalSector()`;
2. obtains the sector with `AFATFS_CACHE_WRITE` and no stale-media read;
3. clears all 512 bytes;
4. writes `.` and `..` only when this is a non-root child directory's first
   cluster (`directoryEntryPos.sectorNumberPhysical != 0` and
   `cursorOffset == 0`); and
5. completes through the existing success/callback path.

A new child leaves entry 2 as `0x00`. A later appended cluster leaves entry 0
as `0x00`. The old per-sector loop, physical `+1`, forward seeks, sector
counter, and final rewind are gone. FAT allocation and `physicalSize` remain
cluster-sized, and the cursor remains at the appended cluster start.

`AFATFS_CACHE_WRITE` already marks the descriptor dirty; no second dirty call
was added. Cache busy/failure behavior and the final caller-visible sync
boundary remain unchanged.

### 8.2 Gate-A composition

`afatfs_prepareDirectoryRunTarget()` was deliberately not changed
functionally. Before a marker enters any later sector it:

- follows the logical FAT cursor;
- extends when allocated EOF is reached;
- obtains WRITE ownership;
- clears all 512 bytes; and
- selects entry zero.

The marker writer then publishes the new run and replacement `0x00`, waits for
target persistence, and retires the old marker tail. All current raw directory
consumers in `asyncfatfs.c` and `filesystem.c` were checked to stop at
`fat_isDirectoryEntryTerminator()` before treating post-marker bytes as live.

Comments beside the append allocator, extension state, extension entry point,
target preparation, mkdir handoff, and no-terminator create path were updated
to state this final contract. The public `asyncfatfs.h` directory contract was
updated, but no declaration, result, enum, signature, or caller obligation
changed.

### 8.3 Future approximately 20 KiB Scene Pattern

A future 20 KiB `pattern.pat` does not alter Gate B. It remains one directory
entry run; approximately 40 payload sectors per Scene use the unchanged
regular-file path. Across 16 Scenes this may add roughly 640 payload-sector
writes and reduce the percentage timing improvement, but it does not change
directory occupancy unless the format adds files or longer names.

### 8.4 Retained SRAM

Phase One and Phase Two both add zero retained SRAM. Gate A reused the existing
create/rename operation union. Gate B reused the extension state and the
existing sector cache. No Pattern-reserved SRAM1 or delay-line-reserved DTCM
was consumed.

### 8.5 Final build and review

A forced full rebuild (`make -B`), `make img`, `git diff --check`, section
inspection, symbol inspection, and stale-token searches passed:

```text
arm-none-eabi-size:
  text=385,420  data=404  bss=96,176

size -A:
  .text=372,488
  .itcm=3,768
  .dma_nocache=3,100
  .data=404
  .bss=89,504
  .dtcm=8,708
  .dtcmz=3,572
  .devwdg_noinit=0

symbols:
  afatfs=6,984 (0x1b48)
  fs_list_cache_name=9,000 (0x2328)

images:
  build/lxr02.bin=385,824
  build/LXRV2_lxr02.img=385,840
```

No new warning was observed; only the recorded pre-existing unused-parameter,
unused-static, packed-USB, nano-syscall, and serial-LTO warnings remained.

The final review found no code-path mismatch or concrete functional defect.
The initial risk statement that directory-sector rollover was the most likely
failure was corrected: rollover is an ordinary ordered AsyncFATFS operation,
not an inherently risky one. It remains useful test coverage, but no first-
sector-boundary failure is expected.

## 9. Hardware and media status at closeout

The user chose to leave Gate B hardware testing for later. Record this as a
deliberate deferral, not a suspected defect:

- no Phase-Two physical-card run;
- no raw-sector inspection;
- no FAT16, small-cluster, or fragmented-chain fixture;
- no reboot/remount or host FAT checker after Gate B;
- no future-20-KiB Pattern fixture;
- no controlled stopped/running two-trial Bank timing or payload hashes.

Source review, callback/order tracing, forced clean build, layout assertions,
and linked-memory checks found no reason to expect normal Bank, Kit, Scene, or
Instrument workflows to fail on this build.

The Instrument repair also remains hardware-unverified. Its supplied failing
fixture and static paths are verified, but the following were not run on the
device: automatic repair of all four types, `goad1  -` reload, normal/Morph
scrolling, card-removal error injection, reboot/remount, and post-repair host
validation.

The one transient Kit list/sanitize error remains explicitly deferred because
it was not reproducible after reboot and was outside both repairs.

## 10. Files changed

### Production and tooling

- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`
  - Gate-A reservation, create/open writers, rename placement, target barrier,
    and old-tail retirement.
  - Gate-B first-sector-only directory initialization.
  - S059 layout assertions and adjacent contracts.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.h`
  - Directory create/open contract only; no declaration change.
- `Core/Hardware/SD/filesystem.c`
  - Reserved/blank Instrument filtering, typed-row validation, fatal-state
    observation, selected-type recovery handoff, and direct `.hcindex` open.
- `Core/Hardware/SD/filesystem.h`
  - Typed Instrument index load/repair contract only.
- `Core/Menu/menu.c`
  - Terminal-safe typed-index completion and deferred-payload cancellation.
- `tools/verify_instrument_indexes.py`
  - New read-only host validator.
- `build/LXRV2_lxr02.img`
  - Final packaged Session 059 image.

### Durable documentation

- `knowledge_files/log_archive/000_SESSION_INDEX.md`
- `knowledge_files/log_archive/059_SESSION_HANDOFF_LOG.md`
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- `knowledge_files/specification_reference/SRAM_MANIFEST.md`
- `MEMORY.md`

### Session working documents superseded by this closeout

- `S059_ASYNCFATFS_SPEEDUP.md`
- `S059_ASYNCFATFS_PHASE_ONE.md`
- `S059_INST_LOAD_FIX.md`
- `S059_ASYNCFATFS_PHASE_TWO.md`

## 11. Explicit non-changes and deferred ideas

Session 059 did not change:

- Bank Load/Save state machines or schemas;
- Kit, Scene, Bank, Instrument, Pattern, Effect, HCNAMES, settings, or AutoSave
  wire formats;
- regular-file allocation or payload writes;
- FAT16 fixed-root semantics;
- cache count, SD burst size, SPI, DMA, or polling ownership;
- playback/audio ownership;
- public move/copy/replace/unlink APIs; or
- crash-transactional create/rename guarantees.

Possible later optimizations require fresh profiling and separate plans:
SFN-only storage for suitable LFN calls, directory-position hints, cache or
transport changes, Bank-specific in-place rewrite, pack files, compression,
whole-file staging, and allocation-unit changes. Session 058 already rejected
the retained-cluster Bank rewrite because it was slower; do not revive it
without different evidence.

## 12. Closeout state

Session 059 is closed with Gate A, the Instrument metadata repair/direct-open
path, and Gate B retained. The source/build result is accepted. Phase Two and
the Instrument repair do not have hardware acceptance, by explicit user
choice, but no problem is expected from the reviewed implementation.

The four root Session 059 planning documents may be deleted after this log and
the specification updates are retained.

## 13. Post-closeout hardware/card verification (2026-08-31)

The user booted the Session 059 Gate-B image, loaded a Bank whose Kit failure
was allowed to exclude one child, loaded a `04x Full` Bank, and saved the
resident Bank as slot 050. The copied card is
`SD_CARD_AFAT_PHASE_2_RESULT/`. The device has no real-time clock, so FAT file
timestamps were ignored; all conclusions below use contents, structure, CRCs,
generations, and trace order.

The exercised Bank path passes:

- `Bank/050 Full` contains all 16 numbered Scene children, 16 `sceneset.scg`,
  16 `pattern.pat`, 16 `effects.fx`, 16 embedded `kitset.kcg`, and 96 referenced
  Instrument files. Every Kit manifest has six existing members of the declared
  type. The complete Bank directory has 162 files including `bankset.bcg` and
  its local `.hcnames`.
- Its payload tree is byte-identical to the existing `Bank/044 Full` tree. The
  only difference is the required local `.hcnames` Bank-source change from
  `044` to `050`. `Bank/046 Full` has the same payload and the analogous source
  difference.
- Root `/.hcnames` has 129 valid rows and is byte-identical to
  `Bank/050 Full/.hcnames`. Row 0 is `Full<TAB>050`; every Scene, embedded Kit,
  and Instrument row agrees with Bank 050's directories and Kit manifests.
- `settings.cfg` has `active_bank=50`. `Bank/050 Full/bankset.bcg` has
  `active_scene=6` and `scene_mask_voice_edit=0040`.
- `/Bank/.hcindex`, `/Kit/.hcindex`, and `/Scene/.hcindex` each contain exactly
  1,000 rows and exactly match all physical numbered directories. Their
  occupied counts are 36, 43, and 48 respectively; Bank row 050 is `Full`.
- Both AutoSave files are exactly 34,768 bytes, committed, and CRC32C-valid.
  `.hcprms1` is generation 77 and `.hcprms2` is the newer generation 78. Both
  contain Bank slot 50, name `Full`, present mask `0xffff`, active Scene 6, and
  voice-edit mask `0x0040`. `tools/verify_bank_autosave.py ... 50` reports
  PASS and cross-checks HCNAMES, settings, Bank structure, the active/first
  Scene settings, and Kit/Instrument identity against the winning record.
- The trace records the allowed failed-child load as resident mask `0xffef`
  with effective loaded mask `0x000f`. This is consistent with bit 4 being the
  unavailable/failing child while children 0..3 committed; the complete Bank
  operation continued. Later AutoSave capture sees the restored full resident
  mask `0xffff`.
- The bounded AutoSave drain validates and publishes alternating records
  through generations 73, 74, 75, 76, 77, and 78. Every publication has a
  terminal `DONE`; the final batch contains the remaining 258 patches. The
  logging ring reports 44,348 dropped diagnostic records during the large
  whole-Bank dirty burst. This is trace evidence loss only, not a product or
  filesystem error; the durable records and terminal sequence are intact.

One separate typed-Instrument metadata issue remains:

- `Instrument/Drum/.hcindex` is now valid: 99 sorted rows exactly match the 99
  visible Drum files, and `.hctmp.drm` is excluded.
- `Instrument/Snare/.hcindex`, `Instrument/Cymbal/.hcindex`, and
  `Instrument/HiHat/.hcindex` are absent even though their directories contain
  31 visible files each. `tools/verify_instrument_indexes.py` therefore reports
  exactly these three failures.
- This does not affect the Bank Load/Save result, but it does not satisfy the
  documented boot result that all four typed indexes are refreshed. Runtime
  selected-type recovery for those three types was not exercised in this test.

Verdict: the tested Gate-B Bank Load/full-Bank Save path, directory creation,
root index update, HCNAMES publication, settings persistence, and AutoSave A/B
publication operate as intended. This is hardware/card acceptance for that
path, with no evidence of a Gate-B regression. Full Session 059 Instrument-index
acceptance remains open because three typed index files are still missing.
