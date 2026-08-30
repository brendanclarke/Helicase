# Session 059 Instrument Load failure — implementation plan

Date: 2026-08-30

Status: implementation complete; static/build verification complete; media and
product acceptance pending. The investigation and repair schedule remain
binding; production changes are confined to the typed Instrument index
producer/consumer, its existing rebuild handoff, Menu's direct completion
callback, and the read-only host validator described below.

## Outcome and repair boundary

Instrument Load is failing before an Instrument payload is parsed. The repair
belongs in the typed Instrument index producer/consumer and its direct Menu
completion callback. It does **not** require another AsyncFATFS allocator
change, an Instrument serializer/parser change, a new cache, or a change to the
saved `goad1  -.drm` file.

The first repair must deliver all of the following as one complete change:

1. A physical Instrument scan must never publish `.hctmp.<ext>` or an empty
   display stem into a typed `.hcindex`.
2. A missing or structurally corrupt typed `.hcindex` must be recoverable at
   runtime. Entering Instrument Load/InstrumentMrp/Instrument Save must scan
   only the selected physical type directory, rewrite its `.hcindex`, keep the
   rebuilt cache resident, and then invoke the original callback.
3. A real read/scan/write error must still finish as an error. It must not be
   converted into a successful empty list.
4. The Menu callback must acknowledge every direct filesystem terminal result,
   must never dispatch a deferred Instrument payload after an index failure,
   and must show the existing filesystem error overlay for an unrecoverable
   failure.
5. The repair must add no retained name array and no persistent SRAM. It must
   reuse the existing 9,000-byte cache, operation fields, callback slot, and
   one-type scan/index rebuild chain.

The boot-time reason that three typed indexes were absent cannot be recovered
from a copied mounted directory tree: `main.c` currently discards the ordinary
zero result from `filesystem_createBootIndexBlocking()`, and the runtime trace
begins after that boot decision. Runtime self-healing is therefore required
even after the scan producer is fixed. It makes missing boot output a
recoverable metadata condition instead of a blank, unusable browser.

## Evidence and direct failure chain

The supplied `SD_CARD_LOAD_SAVE_TEST_RESULT/` image proves two independent
index failures:

| Evidence | Meaning |
| --- | --- |
| `Instrument/Drum/.hcindex` exists, but the Snare, Cymbal, and HiHat indexes do not | Selecting those types reaches `FS_INTERNAL_OP_LOAD_INDEX` phase 12 with no opened `.hcindex`. The trace records that failure repeatedly. |
| Drum contains 99 normal `.drm` objects plus `.hctmp.drm`; its index contains 100 rows | The hidden temporary object was counted as a browser object. |
| Drum index row 0 is eight spaces followed by LF | `filesystem_loadInstrumentIndex_tick()` currently treats it as valid because `field_len` is eight, even though the field has no meaningful character. |
| The first Drum pool selection is therefore the all-space row | `storage_makeSavedInstrumentDisplayFilename()` trims an all-space stem to its fallback `none`, then constructs `none.drm`. |
| No `none.drm` exists | `filesystem_loadInstrument_tick()` receives `op_file == NULL` in phase 12 and finishes in hexadecimal phase `0x10`; the displayed code is exactly `InsL10`. |
| `goad1  -.drm` is a 1,368-byte text Instrument with the expected format/version/type, parameter, and morph sections | Instrument Save and serialization completed; this file is not the cause of the browser failure. |
| The Instrument Save lifecycle trace is REQUEST -> CREATE_RESULT success -> SOURCE_STAGED success -> FINISH success | The new file and the post-save Drum index rewrite both reached their intended durable completion boundaries. |
| The selected root HCNAMES Instrument row is nonblank | The captured resident `kit` identity is not the source of the blank pool row. Do not rewrite the HCNAMES/Menu identity architecture in this fix. |

The exact FAT name representation that bypassed the current exclusion is not
preserved by the host copy. The present scan checks only a case-sensitive
`strncmp(display_name, ".hctmp.", 7)`. The fix must therefore defend both the
reserved identity and the derived cache key:

- compare the reserved temporary component case-insensitively in both the FAT
  display name and open/short-alias name where available; and
- reject any classified Instrument object whose derived eight-cell stem is
  empty/all spaces before it can mutate the cache.

These guards are intentionally independent. The second is the authoritative
protection against a dot-prefixed implementation file even if AsyncFATFS
reports it through an unexpected LFN/alias spelling.

## Binding behavior after the fix

- A typed index is a compact, strictly sorted list of zero to 1,000 usable
  Instrument stems. Unlike Kit/Scene/Bank slot indexes, blank rows have no
  positional meaning and are never valid.
- The hidden `.hctmp.<ext>` file is implementation state for the reversible
  `kit` row. It is never a browser row, never a user load target, and never part
  of the typed index count.
- The `.hcindex` read remains the fast path. Physical scanning happens only
  when that file is absent or structurally invalid.
- Recovery is transparent to callers: one accepted request produces one final
  callback after either the ordinary read or the scan -> index write -> final
  sync chain. No intermediate error/success callback is exposed.
- A low-level read error is not classified as metadata corruption. It closes
  the file, returns to root, and reports `FS_STATUS_ERROR`; it must not overwrite
  a possibly good index after an I/O fault.
- A true empty type directory may rebuild an empty index. Its browser remains
  empty without an error. Re-entering that type may rescan because an empty
  index has no retained proof that the physical directory is still empty; this
  is acceptable and avoids adding SRAM or an on-card format marker.
- Successful recovery leaves `fs_list_cache_kind`, `fs_list_cache_type`, the
  sorted rows, and `fs_list_cache_count` ready for the Menu. The index writer
  must not dispose that cache before the original callback.
- Normal Instrument Load and InstrumentMrp use the same repaired typed-list
  contract. Their existing difference—normal Load owns the reversible temp
  source while Morph Load does not—remains unchanged.

## Production source-change schedule

### 1. `Core/Hardware/SD/filesystem.c`: centralize reserved/blank Instrument filtering

Locations:

- beside `filesystem_instrumentTypeFromFilename()` and
  `filesystem_copyInstrumentStemDisplay()`;
- `filesystem_recordInstrumentFile()`; and
- the existing post-save cache replacement path which ultimately delegates to
  `filesystem_recordInstrumentFile()`.

Required change:

- Add a private case-insensitive predicate for the firmware-owned
  `.hctmp.<ext>` component. Test both `display_name` and `open_name` before a
  scanned object is admitted. The predicate must recognize the registered
  type's exact reserved component and the corresponding reserved short-alias
  family if only an alias is available; it must not hide arbitrary user files
  merely because they contain `hctmp` elsewhere in the stem.
- Add a private predicate for an eight-cell Instrument display stem which
  returns true only when at least one printable non-space character exists.
- In `filesystem_recordInstrumentFile()`, classify the type, reject the
  reserved temp object, derive the display stem, and reject a blank stem **all
  before** clearing, retagging, sorting, or incrementing the shared cache.
- Retain the current casefold duplicate policy, alphabetic insertion order,
  1,000-row ceiling, registry-owned extension classification, and post-save
  insertion behavior.
- Do not special-case the captured `goad1  -` name. Interior spaces and a
  trailing hyphen are valid meaningful stem characters and must survive.

Documentation-in-place block for the reserved-name predicate:

```c
/*
 * Recognize the firmware-owned reversible Instrument temporary object.
 *
 * What: Compares one scanned FAT display/open component with the selected
 * registry type's `.hctmp.<ext>` name using FAT-style ASCII case folding. If
 * AsyncFATFS exposes only the generated short alias, the reserved alias family
 * is recognized as the same implementation object.
 *
 * Why: `.hctmp` is the durable source for returning to the nested Load `kit`
 * row, not a pool Instrument. A case-sensitive display-name-only test allowed
 * that object to enter Drum's `.hcindex`, where its dot-leading stem collapsed
 * to eight spaces and later selected `none.drm`.
 *
 * Inputs: display name, open/short name, and the already classified registry
 * Instrument type. Outputs: nonzero only for the reserved temporary object;
 * no cache or filesystem state changes.
 *
 * Affiliates: filesystem_makeInstrumentTemporaryFilename(),
 * filesystem_recordInstrumentFile(), the single-type Instrument scanner, and
 * filesystem_requestSave/LoadInstrumentTemp().
 */
```

Documentation-in-place block for the derived-stem invariant:

```c
/*
 * Reject an Instrument object which has no usable browser/load key.
 *
 * What: Examines the fixed eight-character display stem after
 * filesystem_copyInstrumentStemDisplay() and requires at least one printable
 * non-space character.
 *
 * Why: Typed indexes are compact key lists; blank rows have no slot meaning.
 * This is the final producer-side guard for dot-prefixed implementation files
 * or unexpected FAT name representations and prevents the filename builder's
 * intentional `none` fallback from becoming a selectable pool filename.
 *
 * Inputs: one padded, NUL-terminated nine-byte stem. Output: validity only;
 * invalid input is ignored before cache kind/count/order can change.
 *
 * Affiliates: filesystem_copyInstrumentStemDisplay(),
 * filesystem_recordInstrumentFile(), filesystem_createBootIndex_tick(), and
 * filesystem_loadInstrumentIndex_tick()'s matching consumer validation.
 */
```

### 2. `Core/Hardware/SD/filesystem.c`: validate typed index rows instead of accepting byte count as identity

Location: `filesystem_loadInstrumentIndex_tick()`, especially phases 11–15.

Required change:

- Initialize the typed read as an empty cache only after `.hcindex` opens.
- For each line, parse the existing name field without changing the on-card
  format, then reject the index as corrupt if the field:
  - is empty or all spaces;
  - contains no usable printable display character;
  - cannot fit the eight-character Instrument stem contract;
  - would exceed 1,000 compact rows; or
  - is not strictly increasing under the same casefold-then-case ordering and
    duplicate identity policy used by `filesystem_recordInstrumentFile()`.
- Keep shorter legacy rows readable by padding through
  `storage_copyDisplayName()`; do not require exactly eight physical bytes when
  the meaningful stem is otherwise valid.
- Treat EOF with zero valid rows as a recovery candidate. A physical one-type
  scan determines whether this is a legitimate empty namespace or a stale
  empty index.
- Split the close/return phases by outcome so the FSM cannot repeat the current
  bug in which a text read error closes the file and phase 15 unconditionally
  calls `filesystem_finish(FS_STATUS_DONE)`.
- Use distinct phase paths for:
  1. valid EOF -> close -> root -> `FS_STATUS_DONE`;
  2. structural corruption -> close -> root -> recovery handoff;
  3. missing `.hcindex` -> root -> recovery handoff; and
  4. real read/close/chdir failure -> root where possible ->
     `FS_STATUS_ERROR`.
- Clear any partially read rows before starting recovery. The existing
  one-type scan helper already does this; do not publish a valid prefix from a
  corrupt index.

Use FSM phase identity, or an existing operation scratch scalar whose lifetime
is dead during index reads, to distinguish these outcomes. Do not add a new
file-scope byte solely for recovery state.

Documentation-in-place block for the row validator/read state machine:

```c
/*
 * Validate one compact typed-Instrument index row before publishing it.
 *
 * What: Accepts a usable one-to-eight-character stem, normalizes it into the
 * fixed display cell, and requires strict sorted/unique order against the
 * preceding accepted row. Blank, overlong, duplicate, unsorted, or otherwise
 * unusable rows classify the complete `.hcindex` as corrupt.
 *
 * Why: Instrument rows are keys, not numbered placeholders. The previous
 * `field_len > 0` test accepted eight spaces, exposed that row as pool 000,
 * and turned it into `none.<ext>` at payload-open time. Rejecting only that
 * single spelling would leave the same failure available through another
 * malformed key.
 *
 * Inputs: the text field, its physical length, selected Instrument type, the
 * active cache count, and (when present) the previous normalized row.
 * Outputs: one normalized cache row and incremented compact count on success,
 * or a corruption result without publishing the current row.
 *
 * Affiliates: filesystem_readTextLine(), storage_copyDisplayName(),
 * filesystem_compareInstrumentDisplayName(),
 * filesystem_recordInstrumentFile(), and the recovery handoff below.
 */
```

Documentation-in-place block beside the outcome-specific close phases:

```c
/*
 * Preserve the reason the typed-index reader is leaving the fast path.
 *
 * What: Uses separate resumable close/return phases for a valid index,
 * recoverable metadata corruption, and a real I/O failure.
 *
 * Why: Async close and chdir can require multiple foreground polls, but their
 * eventual terminal action must remain stable. The former shared phase always
 * finished DONE, silently publishing a partial/empty cache after read errors.
 *
 * Inputs: open result, line-reader status, structural validation result, EOF,
 * and asynchronous close/chdir completion. Outputs: exactly one of ordinary
 * success, one recovery handoff, or terminal error.
 *
 * Affiliates: filesystem_loadInstrumentIndex_tick(), filesystem_finish(),
 * filesystem_beginInstrumentIndexRecovery(), and Menu's index callback.
 */
```

### 3. `Core/Hardware/SD/filesystem.c`: hand a missing/corrupt read into the existing durable rebuild chain

Locations:

- private declarations near `filesystem_loadInstrumentIndex_tick()`;
- beside `filesystem_startInstrumentIndexRebuildScan()` and
  `filesystem_completeLibraryIndexRebuild()`; and
- the recovery terminal phases added to
  `filesystem_loadInstrumentIndex_tick()`.

Required change:

- Add one private `filesystem_beginInstrumentIndexRecovery()` helper. Call it
  only after the original index handle is closed and the working directory has
  returned to root.
- Move the original direct-load `completion_callback` into
  `op_library_index_rebuild_callback`, clear `completion_callback`, set
  `op_library_index_rebuild_kind = FS_NAME_CACHE_INSTRUMENT`, and retain the
  request's `op_instrument_index_type`.
- Release the current `FS_INTERNAL_OP_LOAD_INDEX` ownership to IDLE/NONE
  without publishing a terminal callback, then call the existing
  `filesystem_startInstrumentIndexRebuildScan(
  filesystem_libraryIndexRebuildScanComplete)`.
- Reuse the existing chain exactly as follows:

```text
selected-type physical scan
    -> filesystem_libraryIndexRebuildScanComplete()
    -> FS_INTERNAL_OP_CREATE_BOOT_INDEX for that selected type only
    -> filesystem_libraryIndexRebuildWriteComplete()
    -> final afatfs_sync()
    -> filesystem_completeLibraryIndexRebuild()
    -> original Menu callback exactly once
```

- If the scan cannot start, use the existing named index error path and call
  `filesystem_completeLibraryIndexRebuild(FS_STATUS_ERROR)` exactly once.
- If scanning or writing fails, preserve that terminal error; do not retry in
  a loop and do not call the original callback early.
- Confirm that the writer leaves the rebuilt typed cache resident. Do not add
  a post-write `.hcindex` reread.
- Keep `op_library_index_rebuild_pending` clear: this repair is an immediate
  handoff from a read, not the deferred after-flush rebuild used by Save.

Documentation-in-place block for the handoff helper:

```c
/*
 * Replace an unusable typed `.hcindex` through the existing one-type rebuild.
 *
 * What: Transfers the accepted LOAD_INDEX request's callback into the library
 * rebuild owner, releases the current operation without completing it, scans
 * the selected Instrument directory, rewrites `.hcindex`, and lets the normal
 * rebuild completion publish one final result.
 *
 * Why: Missing boot metadata and structurally corrupt indexes must not become
 * an empty browser, but a caller must still see one asynchronous request and
 * one callback. Reusing the Save rebuild chain preserves physical rescan,
 * durable close/sync, error propagation, and the single 9,000-byte cache.
 *
 * Inputs: op_instrument_index_type, the partially consumed shared cache, and
 * completion_callback from filesystem_requestLoadInstrumentIndex(). Outputs:
 * either a busy one-type scan with the original callback parked, or one final
 * ERROR callback if recovery cannot start. No Scene/DSP/HCNAMES state changes.
 *
 * Affiliates: filesystem_startInstrumentIndexRebuildScan(),
 * filesystem_libraryIndexRebuildScanComplete(),
 * filesystem_createBootIndex_tick(),
 * filesystem_completeLibraryIndexRebuild(), and
 * filesystem_loadInstrumentIndex_tick().
 */
```

### 4. `Core/Menu/menu.c`: make typed-index completion terminal-safe

Location: `menu_instrumentIndexLoadComplete()`.

Required change:

- Snapshot `filesystem_status() == FS_STATUS_DONE` at callback entry before
  changing any Menu state.
- Acknowledge the direct filesystem facade on every terminal callback. Follow
  the established direct-index pattern used by
  `menu_loadCommandFinalIndexComplete()`: success calls `filesystem_ack()`;
  the failure path may call it before the overlay or delegate the effective
  acknowledgement to `menu_showFilesystemErrorOverlay()`, but it must not
  leave DONE/ERROR parked.
- On error:
  - clear the unusable shared name cache;
  - clear `menu_deferSelectionRequest` so no payload request is dispatched from
    a failed/partial list;
  - release `menu_storageBusy`;
  - retain the current nested page, selected voice/type, resident `kit` label,
    and reversible temp snapshot; and
  - show the existing filesystem error overlay and return immediately.
- On success, retain the existing clamp, shown-index reconciliation,
  coalesced newest-selection request, busy ownership, and repaint behavior.
- Do not auto-select or auto-load a different file merely because recovery
  changed the compact list order. Clamp the cursor first, then use the same
  explicit/deferred-selection behavior already present.

Documentation-in-place block for the callback:

```c
/*
 * Complete one typed Instrument index request, including transparent rebuild.
 *
 * What: Snapshots the terminal filesystem result, releases the direct facade,
 * and publishes the rebuilt/read cache only on success. Failure discards the
 * unusable cache, cancels deferred payload selection, releases Menu input, and
 * opens the existing filesystem error overlay.
 *
 * Why: This callback bypasses Preset's normal acknowledgement owner. Leaving
 * DONE/ERROR parked blocks the idle-only AutoSave and trace schedulers, while
 * the previous unconditional clamp/deferred path could attempt an Instrument
 * load from a partial or empty cache after `.hcindex` failed.
 *
 * Inputs: filesystem_status(), current nested Load/Save mode, selected
 * voice/type/source, and menu_deferSelectionRequest. Outputs: on success, one
 * safe typed browser and optional coalesced selection; on failure, no payload
 * request and one visible error with the filesystem facade returned to IDLE.
 *
 * Affiliates: menu_requestInstrumentIndexLoad(),
 * menu_instrumentLoadRequestSelection(), menu_instrumentLoadClampIndex(),
 * menu_showFilesystemErrorOverlay(), filesystem_ack(), and both Instrument
 * Load render paths.
 */
```

### 5. `Core/Hardware/SD/filesystem.h`: publish the recovery contract

Location: the comment above `filesystem_requestLoadInstrumentIndex()`.

No signature change is required. Update the public contract to say that:

- the normal path reads the selected type's `.hcindex`;
- an absent or structurally invalid index transparently triggers one selected-
  type physical scan and durable rewrite;
- the completion callback runs once after the read or complete repair chain;
- a real I/O/scan/write failure returns `FS_STATUS_ERROR` through that same
  callback; and
- the request remains asynchronous and uses the one shared cache.

Documentation-in-place block:

```c
/*
 * Load or repair one registered Instrument type's browser index.
 *
 * Inputs: registered type and optional completion callback. Output: the one
 * shared cache contains that type's compact sorted stems. The fast path reads
 * `.hcindex`; a missing or structurally invalid file transparently performs a
 * selected-type physical scan and durable rewrite. The callback runs exactly
 * once after either path and observes ERROR for genuine read/scan/write faults.
 * The request performs no blocking runtime SD work and is refused only for an
 * invalid type or busy facade.
 */
```

### 6. `tools/verify_instrument_indexes.py`: add a read-only card validator

Required change:

Add a host-side validator with one positional argument: the mounted/copied SD
root. It must never modify the tree. For every Instrument registry directory
known by the current product (`Drum/.drm`, `Snare/.snr`, `Cymbal/.cym`, and
`HiHat/.hat`), it must:

- report a missing `.hcindex`;
- parse every line without stripping meaningful interior spaces;
- reject empty/all-space, over-eight-character, reserved-temp, duplicate, and
  non-sorted rows;
- exclude `.hcindex` and `.hctmp.<ext>` from the physical user-file set;
- compare case-insensitively and require every indexed stem to resolve to
  exactly one physical typed file;
- report physical user files absent from the index and index rows with no
  physical file; and
- exit nonzero on any mismatch, printing the type and row/file involved.

The supplied `SD_CARD_LOAD_SAVE_TEST_RESULT/` must be kept as a failing fixture:
it should report the three missing indexes and Drum row 1 as blank/reserved.
After firmware recovery is captured into a fresh directory, the same command
must exit zero.

Documentation-in-place module block:

```python
"""Validate compact typed Instrument indexes against a copied SD tree.

Inputs: one card-root path containing Instrument/<registry-directory>.
Outputs: actionable per-type diagnostics and exit status 0 only when each
.hcindex is sorted, unique, free of reserved/blank rows, and exactly represents
the physical user Instrument files. The tool is read-only. It mirrors firmware
index identity rules so captured-card acceptance can detect the `.hctmp`/blank
row regression without relying on front-panel behavior alone.
"""
```

## Project documentation updates after implementation

These are part of the implementation commit because the typed-index contract
changes from “read-only failure” to “read fast path with repair.”

### `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`

Update the typed `.hcindex` sections to record compact nonblank row validity,
reserved `.hctmp` exclusion in both name representations, missing/corrupt
runtime self-healing, one-callback durability, and Menu acknowledgement/error
behavior. Do not change the slot-preserving blank-row rules for Kit, Scene, or
Bank indexes.

Documentation block to incorporate:

```text
Typed Instrument index recovery: a typed row is a load key and therefore must
contain a usable stem; it is not a numbered placeholder. The reader uses
.hcindex as its fast path, but absence or structural corruption transfers the
same accepted request into a selected-type physical scan and durable index
rewrite. `.hctmp.<ext>` and blank derived stems are excluded at scan time. The
original completion callback runs once after the read or repair chain.
```

### `MEMORY.md`

Add the Session 059 finding and final verified result: captured missing typed
indexes, `.hctmp`-derived blank Drum row, `none.drm`/`InsL10` chain, repair
files, card tests, and measured SRAM/image figures.

### `S059_ASYNCFATFS_PHASE_ONE.md` and `S059_ASYNCFATFS_SPEEDUP.md`

Record this Instrument Load regression as a product-matrix finding discovered
during S059 acceptance. State that the fix is in the filesystem index layer
unless later raw FAT evidence proves an allocator defect. Preserve the measured
Bank Save speedup and Phase One AsyncFATFS acceptance separately.

### `knowledge_files/specification_reference/SRAM_MANIFEST.md`

Record rebuilt `text/data/bss`, relevant symbol sizes, and that this fix adds
zero retained cache/state bytes. Any unexpected `.bss` increase is a stop
condition until its exact owner is identified and approved.

`knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md` needs no
semantic update for this fix because no AsyncFATFS contract changes. A brief
cross-reference may be added only if useful; do not describe index recovery as
an allocator feature.

## Explicit non-changes and deferred evidence

- Do not modify `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` or
  `asyncfatfs.h` in this repair. The runtime trace shows successful index
  creation after Instrument Save and gives no raw FAT-sector evidence tying
  `InsL10` to the S059 terminator allocator.
- Do not modify `storageTypes.c` Instrument parsing/serialization or special-
  case `goad1  -.drm`; the saved file is structurally valid and was never
  opened by the failing selection.
- Do not modify Preset/DSP apply logic. The failure occurs before payload
  parsing and apply.
- Do not rewrite HCNAMES or the nested `kit` identity model. The captured
  resident identity row is present and normal Instrument Load's temp snapshot
  is a required reversible workflow.
- Do not fold the user's intermittent Kit sanitize/rename report into this
  patch. It was not reproducible and the user explicitly deferred it.
- Do not make `main.c`'s discarded boot-index return fatal in this repair.
  Doing so would abandon the later initial Bank load on a metadata failure
  that the new runtime path can repair. Capture a raw FAT image plus
  `/bootlog.bin`/operation detail if typed boot generation still fails after
  the producer fix; then schedule boot observability/retry independently.

## Implementation order

1. Preserve the supplied card tree and decoded trace as the failing evidence
   baseline; run the new host validator and save its expected failures.
2. Add the producer-side reserved/blank guards and verify a physical Drum scan
   produces 99 user rows from the supplied 100 `.drm` directory objects.
3. Add typed-row structural validation and the distinct valid/recover/error
   FSM exits.
4. Add the callback-transfer recovery helper using the existing one-type
   scan/write chain; verify callback count and facade state with temporary
   trace markers if necessary, then remove any debug-only instrumentation.
5. Fix the Menu terminal callback and verify no deferred payload can start on
   an unrecoverable index failure.
6. Update the header and specification/reference/session documents.
7. Build, measure SRAM/image output, run static checks, then perform the card
   matrix below.

## Verification and acceptance matrix

### Static and host checks

- `git diff --check` passes.
- A clean firmware build and image build pass with no new warnings.
- Compare `size`, map, `afatfs`, filesystem cache, and Menu retained-state
  symbols against the pre-fix build. `.bss` must not grow for this repair.
- The new validator fails the supplied capture for exactly the known defects:
  absent Snare/Cymbal/HiHat indexes and Drum's blank first row.
- A repaired capture passes with four indexes, no blank/reserved rows, strict
  sorted uniqueness, and exact physical-file correspondence.
- Re-run `tools/decode_devlogs.py` on the acceptance trace and retain the
  decoded output beside the card capture.

### On-card recovery cases

1. Start with the supplied card contents. Enter Drum Instrument Load. The
   existing bad index must be detected, Drum must scan/rewrite, and pool 000
   must display the first real Drum name. No `InsL10` occurs.
2. Enter Snare, Cymbal, and HiHat. Each missing index must transparently scan
   and rewrite once, then show a scrollable list without `IdxL0c` or a blank
   browser.
3. Delete one repaired `.hcindex`, reboot, then enter that type. Runtime entry
   must restore the file and browser without manual cleanup.
4. Replace one index's first row with eight spaces. Entry must rebuild it; the
   resulting file must not contain the blank row or `.hctmp` identity.
5. Inject a genuine read failure/card removal. The index request must report an
   error overlay, issue no payload request, return the facade to IDLE after
   acknowledgement, and leave the nested page dismissible/retryable. It must
   not rewrite the index on a read-I/O classification.

### Normal and Morph Instrument workflows

- Normal Instrument Load opens with the resident `kit <name>` row intact,
  moves from `kit` to pool 000, scrolls repeatedly in both directions, loads a
  selected file, and returns to `kit` through the reversible temp file.
- Explicitly select and load `goad1  -`; verify its name remains meaningful,
  its `.drm` opens, the payload applies, and no `none.drm` fallback is used.
- InstrumentMrp produces the same typed list, scrolls without `InsL10`, loads
  morph data under its existing compatibility rules, and preserves its normal
  non-temp semantics.
- Switch repeatedly among voices, all four registry types, and normal/Morph
  modes. Every accepted index request gets one callback; no old-type cache row
  is displayed or loaded.
- Scroll quickly while an index read/rebuild is busy. Coalescing must select
  only the newest valid row after completion; no busy latch or deferred request
  remains stuck.

### Save/regression workflows

- Save a new Drum Instrument, then confirm the durable Drum index contains the
  new stem exactly once and excludes `.hctmp.drm`.
- Re-load that newly saved Instrument in normal and Morph modes as applicable.
- Repeat one Instrument Save for another type to prove the selected-type
  rebuild is registry-generic.
- Recheck Kit Load/Save, Scene Load/Save, and Bank Save. Their slot-ordered
  index behavior and the measured S059 Bank Save speedup must remain unchanged.
- Test with playback stopped and running. Foreground SD work must remain
  asynchronous after boot, audio must not acquire filesystem ownership, and
  AutoSave/trace draining must resume after every terminal callback.
- Power-cycle/remount the card and run host filesystem checks. All four typed
  indexes and saved Instruments must remain discoverable and clean.

## Completion criteria

This fix is complete only when the original supplied card self-recovers without
manual index creation, both Instrument Load modes can scroll and load real
files, `goad1  -` reloads, no blank/reserved row can be regenerated by boot or
post-save scanning, an unrecoverable error remains visible and acknowledged,
and the build confirms zero retained SRAM growth. A front-panel list that works
only after switching voices/types repeatedly is not acceptance.

## Implementation notes / verification record

- 2026-08-30: Read `MEMORY.md`, this complete repair schedule, the Session 040
  handoff, and the relevant typed-index/cache sections of
  `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`. The existing
  worktree contains the prior Session 059 AsyncFATFS changes and supplied
  failing card fixtures; those files are preserved and are outside this
  repair's source boundary.
- 2026-08-30: Baseline inspection confirmed the four source defects described
  above: Instrument scans use a case-sensitive display-only `.hctmp` filter,
  blank derived stems can enter the shared cache, index rows are accepted on
  byte count alone, and the direct Menu callback does not snapshot/acknowledge
  a terminal result or cancel deferred payload selection on error.
- 2026-08-30: Implemented the producer guards, strict compact-row consumer,
  selected-type runtime recovery handoff, and terminal-safe Menu callback.
  Missing/empty/structurally invalid `.hcindex` files now scan and rewrite only
  the selected type through the existing durable chain; fatal FAT/SD conditions
  remain ERROR. The shared 9,000-byte cache and existing operation fields are
  reused, with no new retained list or recovery state. No AsyncFATFS source was
  changed for this repair.
- 2026-08-30: Added read-only `tools/verify_instrument_indexes.py`. Against
  `SD_CARD_LOAD_SAVE_TEST_RESULT/` it reports exactly four expected defects:
  Drum row 1 is all spaces, and Snare/Cymbal/HiHat `.hcindex` files are absent.
  The alternate `SD_CARD_AFAT_RESULT/` capture reports its four missing typed
  indexes. Both runs exit nonzero as intended; a valid capture returns PASS.
- 2026-08-30: Final clean `make clean && make` and `make img` pass after the
  typed-index close-path audit. The logging-on link reports `text=385,380 B`,
  `data=404 B`, `bss=96,176 B`, normal `.bss=89,504 B`, `.text=372,448 B`,
  and `build/lxr02.bin=385,784 B`. Linked owner checks report
  `afatfs=6,984 B` and `fs_list_cache_name=9,000 B`; no new retained cache,
  array, or operation-state allocation was added. `git diff --check` passes and
  Python 3.9 validation passes its syntax/runtime check with `-B`.
- 2026-08-30: Updated `FILESYSTEM_SPEC.md`, `SRAM_MANIFEST.md`, `MEMORY.md`,
  and the two AsyncFATFS Session 059 plans to preserve the product-layer
  regression finding and the boundary between this fix and Gate A/Gate B.
  Front-panel, reboot/remount, injected-card-failure, FAT-checker, logging-off,
  and full hardware/product acceptance remain pending; this workspace does
  not claim those completion criteria.

## Direct `.hcindex` fast-path correction — remove the unconditional directory prescan

Status: implementation recipe only. The line numbers below identify the
current `Core/Hardware/SD/filesystem.c` as inspected on 2026-08-30. They will
shift when the edits are applied, so the named function, phase labels, and
quoted current blocks are the authoritative anchors.

### Code-file scope

Change exactly one code file:

- `Core/Hardware/SD/filesystem.c`

Do not change:

- `Core/Hardware/SD/filesystem.h`;
- `Core/Menu/menu.c`;
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c` or `.h`;
- `tools/verify_instrument_indexes.py`; or
- any other production/test code for this correction.

The public request contract already describes a fast `.hcindex` read with
missing/corrupt selected-type recovery. No signature, callback, cache, retained
state, error code, or on-card format change is required.

### Code change 1 — replace the function-level contract comment

File and current location:

- `Core/Hardware/SD/filesystem.c`, current lines 4215–4231;
- the complete comment immediately above
  `static void filesystem_loadInstrumentIndex_tick(void)`.

Delete the current comment beginning:

```c
/*
 * Load or repair one registry-defined Instrument `.hcindex`.
 *
 * What: the fast path proves that the selected type owns a real `.hcindex`,
```

and ending:

```c
 * state. Clients: Menu's normal and Morph Instrument entry paths.
 */
```

Replace that complete comment, without changing the function declaration or
body at this step, with:

```c
/*
 * Load or repair one registry-defined Instrument `.hcindex`.
 *
 * What: the fast path enters the selected type directory, releases the
 * redundant opened-directory handle, and opens `.hcindex` directly. A valid
 * file is read into the compact sorted typed cache. A nonfatal NULL open,
 * empty file, or structurally invalid row transfers the same accepted request
 * into the existing selected-type scan -> index-write -> sync chain. A fatal
 * FAT/SD condition returns ERROR without starting recovery.
 *
 * Why: `.hcindex` exists to avoid enumerating every physical Instrument during
 * normal Menu entry and voice/type/mode transitions. The former preliminary
 * presence scan made every healthy load O(directory entries), duplicating the
 * expensive work that belongs only to missing/corrupt recovery. Direct open
 * restores the intended O(index bytes) fast path while the existing fatal
 * snapshot preserves the boundary between missing metadata and real I/O
 * failure.
 *
 * Inputs: op_instrument_index_type, the registry-owned directory name, the
 * single shared name cache, and the accepted request's completion callback.
 * Outputs/effects: exactly one complete typed cache or one final ERROR
 * callback. Successful direct read and selected-type recovery still invoke the
 * original callback exactly once; no Scene, DSP, HCNAMES, or retained-state
 * ownership changes.
 *
 * Affiliates: filesystem_requestLoadInstrumentIndex(),
 * filesystem_validateInstrumentIndexRow(),
 * filesystem_instrumentIndexFATFatal(),
 * filesystem_beginInstrumentIndexRecovery(),
 * filesystem_completeLibraryIndexRebuild(), and
 * menu_instrumentIndexLoadComplete().
 */
```

Why this code-file comment change is required:

- The existing words “the fast path proves” describe the unconditional
  physical presence scan being removed.
- The replacement explicitly documents the new direct-open fast path, the
  nonfatal-NULL recovery decision, the fatal no-write decision, and the
  unchanged one-callback/cache ownership boundary.

### Code change 2 — replace the complete phase 9–15 presence-scan/open block

File and current location:

- `Core/Hardware/SD/filesystem.c`, current lines 4322–4427;
- inside `filesystem_loadInstrumentIndex_tick()`;
- from `case 9: /* START .hcindex presence scan */` through the final `return;`
  immediately before `case 17: /* READ AND VALIDATE COMPACT ROWS */`.

Delete that entire current range. Specifically, deletion includes all of the
following current code:

- phase 9 and its “Prove missing versus I/O” comment;
- `afatfs_findFirstObject(op_kit_root_dir, &op_object_finder)`;
- all of phase 10, including `afatfs_findNextObject()`, `op_object`,
  `fat_compareDisplayName()`, and every `afatfs_findLastObject()` call;
- phase 11 “CLOSE type-directory handle, index present”;
- phase 12 “CLOSE type-directory handle, index absent”;
- phase 13 “WAIT type close, then open proven index”;
- the existing phase 14 which waits for the absent-index directory close;
- phase 15 “WAIT proven .hcindex open”; and
- the old phase transitions to 12, 13, 14, and 15.

Do not leave any `.hcindex` presence enumeration in this function. After this
replacement, `filesystem_loadInstrumentIndex_tick()` must contain no call to
`afatfs_findFirstObject()`, `afatfs_findNextObject()`, or
`afatfs_findLastObject()`.

Insert the following block at the exact deleted location, between the existing
phase 8 and existing phase 17:

```c
    case 9: /* CLOSE type-directory handle before direct index open */
        /*
         * Use `.hcindex` as the actual typed-browser fast path.
         *
         * What: After afatfs_chdir() copied the selected type directory into
         * AsyncFATFS' current-directory object, release the original opened
         * directory handle. The next phase opens the exact `.hcindex`
         * component directly; no physical directory-object enumeration occurs
         * before that open.
         *
         * Why: The index exists specifically to avoid scanning every
         * Instrument file during Menu entry and voice/type/mode transitions.
         * Enumerating the directory merely to prove `.hcindex` exists made
         * every healthy request O(directory entries) and made the roughly
         * 100-file Drum directory the worst normal case. The copied current
         * directory remains valid after this redundant handle is closed.
         *
         * Inputs: op_kit_root_dir from the completed selected-type open,
         * AsyncFATFS' current-directory copy established by phase 8, and the
         * existing on_file_closed callback.
         *
         * Outputs/effects: the redundant directory handle is asynchronously
         * released and op_close_done becomes the only gate to the direct file
         * open. No cache row, recovery callback, Instrument payload, or card
         * directory entry changes.
         *
         * Affiliates: afatfs_chdir(), afatfs_fclose(),
         * on_file_closed(), afatfs_fopen_lfn(), and the direct-open result
         * phase below.
         */
        op_close_done = false;
        if (afatfs_fclose(op_kit_root_dir, on_file_closed))
            op_phase = 10u;
        return;

    case 10: /* WAIT type close + OPEN .hcindex DIRECTLY */
        if (!op_close_done) {
            if (filesystem_instrumentIndexFATFatal())
                filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        op_kit_root_dir = NULL;
        op_file_ready = false;
        op_file = NULL;
        if (!afatfs_fopen_lfn(".hcindex", "r",
                              AFATFS_MATCH_CASE_SENSITIVE,
                              NULL, on_file_opened))
            return;
        op_phase = 11u;
        return;

    case 11: /* WAIT direct .hcindex open */
        if (!op_file_ready)
            return;
        if (op_file == NULL) {
            /*
             * Classify direct index-open completion without a prescan.
             *
             * What: A NULL handle with a ready/nonfatal AsyncFATFS volume is
             * missing metadata and enters the existing selected-type recovery
             * path. A NULL handle with AFATFS_FILESYSTEM_STATE_FATAL enters a
             * separate terminal-error phase and never starts a scan or writer.
             * A non-NULL handle continues into the existing validated reader.
             *
             * Why: Missing `.hcindex` is the recoverable condition proven by
             * the supplied card, while a fatal FAT/SD state is not authority to
             * overwrite potentially good metadata. The read-only fatal
             * snapshot separates those outcomes without paying an
             * unconditional directory walk on every healthy request.
             *
             * Inputs: op_file_ready/op_file from on_file_opened() and the
             * observational filesystem_instrumentIndexFATFatal() result.
             *
             * Outputs/effects: phase 14 for return-to-root recovery, phase 16
             * for return-to-root terminal error, or validated read phase 17.
             * The accepted request retains exactly one eventual completion
             * callback on all branches.
             *
             * Affiliates: on_file_opened(),
             * filesystem_instrumentIndexFATFatal(),
             * filesystem_beginInstrumentIndexRecovery(),
             * filesystem_finish(), and menu_instrumentIndexLoadComplete().
             */
            op_phase = filesystem_instrumentIndexFATFatal() ? 16u : 14u;
            return;
        }

        /* Initialize only after a real index handle exists. Partial rows from
         * an earlier request are never a valid prefix for this typed cache. */
        op_item_offset = 0u;
        fs_list_cache_count = 0u;
        op_line_len = 0u;
        op_phase = 17u;
        return;

    case 14: /* DIRECT OPEN MISSING: RETURN ROOT + RECOVER */
        if (!afatfs_chdir(NULL)) {
            if (filesystem_instrumentIndexFATFatal())
                filesystem_finish(FS_STATUS_ERROR);
            return;
        }
        filesystem_beginInstrumentIndexRecovery();
        return;

    case 16: /* DIRECT OPEN FATAL: RETURN ROOT + ERROR */
        /*
         * Complete a fatal direct-open result without authorizing recovery.
         *
         * What: Attempt to normalize the working directory when AsyncFATFS can
         * still make progress, then publish FS_STATUS_ERROR. If the filesystem
         * is already fatal, do not wait forever for chdir; finish the accepted
         * request immediately through its normal callback/error-overlay path.
         *
         * Why: The missing-index branch may scan and rewrite metadata, but a
         * fatal lower-layer state must never do so. A distinct phase preserves
         * that decision across foreground polls and cannot fall through to the
         * recovery handoff.
         *
         * Inputs: current AsyncFATFS working directory and the fatal-state
         * snapshot. Outputs/effects: root when available and one terminal
         * FS_STATUS_ERROR; no scan, index writer, or cache publication.
         *
         * Affiliates: afatfs_chdir(),
         * filesystem_instrumentIndexFATFatal(), filesystem_finish(), and the
         * Menu terminal callback.
         */
        if (!afatfs_chdir(NULL) &&
            !filesystem_instrumentIndexFATFatal())
            return;
        filesystem_finish(FS_STATUS_ERROR);
        return;
```

Exact behavioral effects of this replacement:

- Healthy index: phases `8 -> 9 -> 10 -> 11 -> 17`; no directory iterator is
  initialized and no physical Instrument object is visited.
- Missing index on a nonfatal volume: phases
  `8 -> 9 -> 10 -> 11 -> 14`, then the unchanged
  `filesystem_beginInstrumentIndexRecovery()` chain.
- Fatal direct-open failure: phases `8 -> 9 -> 10 -> 11 -> 16`, then one
  terminal error and no recovery writer.
- Empty or structurally invalid opened index: unchanged phase 17 selects phase
  19, which closes the real index handle and enters recovery through phase 22.
- Real read error after a successful open: unchanged phase 17 selects phase
  20, which closes the real index handle and enters terminal phase 23.

No new phase-state variable is permitted. Phase identity alone records missing
versus fatal direct-open outcomes. Phases 12, 13, and 15 become unused and must
be removed rather than retained as dead cases.

### Code change 3 — correct the existing phase 18–23 documentation and phase-20 label

File and current location:

- `Core/Hardware/SD/filesystem.c`, current lines 4472–4494 after the phase-17
  reader;
- the comment beginning `Preserve the reason the typed-index reader is leaving
  the fast path` plus the phase 18/19/20 labels immediately below it.

Do not change the close code itself:

```c
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = (uint8_t)(op_phase + 3u);
        else if (filesystem_instrumentIndexFATFatal())
            filesystem_finish(FS_STATUS_ERROR);
        return;
```

That body is correct after code change 2 because phases 18, 19, and 20 can now
only be reached after phase 11 produced a non-NULL index handle and phase 17
read it. Replace only the existing documentation block and the phase-20 label
with the following, leaving the body byte-for-byte unchanged:

```c
    /*
     * Close only an `.hcindex` file which was successfully opened.
     *
     * What: Valid EOF, structural corruption/empty metadata, and a real index
     * read error use separate phase identities while sharing one asynchronous
     * op_file close. After close, the phase offset selects DONE, recovery, or
     * ERROR through phases 21, 22, and 23 respectively.
     *
     * Why: Direct open handles absence before phase 17, so directory discovery
     * and missing-file results can never enter this group. This guarantees
     * op_file is a real opened index on every shared-close branch and preserves
     * the validated reader's outcome across an asynchronous fclose.
     *
     * Inputs: op_file from the successful direct open and the outcome selected
     * by phase 17. Outputs/effects: one scheduled close followed by exactly one
     * of valid completion, selected-type recovery, or terminal read error.
     *
     * Affiliates: filesystem_readTextLine(), afatfs_fclose(),
     * on_file_closed(), filesystem_beginInstrumentIndexRecovery(), and
     * filesystem_finish().
     */
    case 18: /* CLOSE valid index */
    case 19: /* CLOSE corrupt/empty index for recovery */
    case 20: /* CLOSE after real opened-index read I/O error */
        op_close_done = false;
        if (afatfs_fclose(op_file, on_file_closed))
            op_phase = (uint8_t)(op_phase + 3u);
        else if (filesystem_instrumentIndexFATFatal())
            filesystem_finish(FS_STATUS_ERROR);
        return;
```

Why this code-file comment/label change is required:

- The current phase-20 label says “read/scan I/O error,” but the prescan and
  its scan-error transition have been deleted.
- The new comment documents the now-provable `op_file != NULL` precondition for
  the shared close block.
- This is documentation correction required by the prescan removal; it adds no
  separate fallback behavior.

### No other code changes

Leave all of the following exactly as currently implemented:

- phases 0–8, including Instrument root/type directory open behavior;
- phase 17 row reading and validation;
- phases 21–24 terminal handling;
- `filesystem_beginInstrumentIndexRecovery()` and the complete selected-type
  scan -> writer -> sync callback chain;
- `filesystem_instrumentIndexFATFatal()`;
- producer-side `.hctmp`/blank-stem filtering;
- `menu_instrumentIndexLoadComplete()`; and
- every public API declaration.

Do not implement either of the two other review observations as part of this
change. In particular, do not edit the Python validator. Removing the prescan
also means its directory-scan failure route no longer exists inside this
function, but no independent fallback feature or additional cleanup state is
to be added.

### Focused verification for this exact code change

1. Static search: within `filesystem_loadInstrumentIndex_tick()`, there must be
   no `afatfs_findFirstObject`, `afatfs_findNextObject`,
   `afatfs_findLastObject`, `op_object_finder`, or `op_object` reference.
2. Valid Drum index: trace must show `FS_INTERNAL_OP_LOAD_INDEX` followed by
   successful completion without any intervening
   `FS_INTERNAL_OP_SCAN_INSTRUMENTS` or
   `FS_INTERNAL_OP_CREATE_BOOT_INDEX` operation.
3. Repeat the valid fast path for Snare, Cymbal, and HiHat after their indexes
   exist, then repeat during normal/Morph and voice/type switches. Directory
   population must no longer determine healthy-index entry latency.
4. Missing-index control: delete one typed `.hcindex`; direct open must produce
   nonfatal NULL, phase 14 must start exactly one selected-type scan/write/sync
   chain, and the original callback must run once with a populated cache.
5. Corrupt-index control: retain the captured all-space first row; the direct
   open must succeed, phase 17 must reject it, and existing phases 19/22 must
   rebuild it exactly once.
6. Fatal-open control: a fatal snapshot at direct-open completion must select
   phase 16, produce one ERROR callback/overlay, and start neither scan nor
   writer.
7. Build and static acceptance: clean build/image, `git diff --check`, unchanged
   `bss`, no new symbols/state, and no changes outside the one source file plus
   the normal documentation/verification record written after testing.
