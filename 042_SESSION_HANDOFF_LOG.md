# Session 042 Handoff Log

Date closed: 2026-07-25

## Purpose

Session 042 completed the resident-name/cache disposal begun after the
generalized `.hcindex` work. The session introduced root `/.hcnames` as the
authoritative resident-name register, removed duplicate names and file stems
from the sixteen resident Scenes, separated name browsing from payload staging,
preserved mask-selective Bank semantics, and eliminated several remaining
multi-entry caches.

The session also diagnosed boot freezes caused by asyncfatfs handle lifetime,
expanded the application handle pool from three to five, and implemented a
reversible `kit` row for Instrument Load using one hidden temporary file rather
than an SRAM copy of the original parameters.

This handoff is deliberately code-verified. Historical plans described several
intermediate designs that were implemented and later removed. The final
architecture below follows the current source and linked ELF, not those
superseded proposals.

## Final build and verification state

The last completed validation was:

```text
make -j1
make img
git diff --check -- <Session 042 source and retained-document paths>
```

The build completed with only the existing newlib `_close`, `_lseek`, `_read`,
and `_write` syscall warnings; there were no compiler errors. The scoped
whitespace check passed. A whole-worktree `git diff --check` still reports
space-padded rows in the binary/test-card fixture `SD_CARD/Bank/.hcindex`;
that card data was not rewritten during documentation closeout.

Final linked summary:

```text
text       373,328 B
data           408 B
aggregate bss 259,368 B
```

Physical writable sections:

```text
.dma_nocache     3,100 B  SRAM1
.data              408 B  SRAM1
.bss            249,552 B  SRAM1
.dtcm            35,168 B  DTCM initialized
.dtcmz            6,716 B  DTCM zero-initialized
```

Therefore:

```text
SRAM1 static use = 253,060 B
DTCM static use  =  41,884 B
```

The generated firmware payload is 373,736 bytes and the packaged
`build/LXRV2_lxr02.img` is 373,752 bytes including its 16-byte image header.

The final rapid-backspin Menu correction compiled and packaged successfully.
There was no subsequent hardware result before session close, so it is
build-verified and source-reasoned but not yet hardware-confirmed.

## Final on-card name architecture

### `.hcindex`

Directory-local `.hcindex` files remain the browser indexes:

```text
/Kit/.hcindex
/Scene/.hcindex
/Bank/.hcindex
/Instrument/Drum/.hcindex
/Instrument/Snare/.hcindex
/Instrument/Cymbal/.hcindex
/Instrument/HiHat/.hcindex
```

Kit, Scene, and Bank indexes contain exactly 1,000 logical rows. Row number is
the direct `000..999` slot, and absent slots remain blank. The row stores only
the eight-character display portion; the three-digit prefix is reconstructed
from the slot.

Instrument indexes contain up to 1,000 sorted entries for one registry type.
Their cache rows are stems used for display and key construction. Instrument
type/extension and registry directory are separate registry metadata.

There is one physical browser/register cache:

```c
fs_list_cache_name[1000][9]
```

It is exactly 9,000 bytes. Its tag says whether it currently owns a Kit, Scene,
Bank, typed Instrument, or HCNAMES view. It is never multiplied by library or
Instrument type.

### `/.hcnames`

`/.hcnames` is fixed-order newline-delimited text with 129 logical rows:

| Zero-based row | Meaning |
|---:|---|
| 0 | Active resident Bank name |
| 1..16 | Resident Scene 0..15 names |
| 17..32 | Embedded Kit name for resident Scene 0..15 |
| 33..128 | Six Instrument names per Scene: `33 + scene * 6 + voice` |

Each logical value is at most eight printable display characters. The in-RAM
cache representation is space-padded and NUL-terminated; file output trims
trailing cells and preserves blank rows as blank lines.

The file is unversioned in this first implementation. Changing one name can
change physical line length, so targeted updates cannot overwrite a fixed byte
range. They:

1. borrow the 9,000-byte cache;
2. read all 129 logical rows;
3. replace only the rows owned by the successful action;
4. reopen/truncate the file;
5. stream all rows;
6. close and pass through the normal filesystem flush gate.

If a targeted update finds no HCNAMES file, it starts with 129 blank rows,
overlays only the successfully changed rows, and creates the file.

Normal boot does not regenerate HCNAMES. The former boot snapshot call was
removed after Scene names left SRAM, because a mask-selective boot Bank Load
cannot reconstruct unselected Scene names and would erase valid rows. The
public `filesystem_writeResidentNamesBlocking()` wrapper still exists, but it
is not called by the normal boot sequence.

### Active SRAM identity rows

The active logical identity interface has nine rows:

```text
Bank
Scene
Kit
Instrument 1
Instrument 2
Instrument 3
Instrument 4
Instrument 5
Instrument 6
```

Physical ownership is:

| Symbol/owner | Bytes | Contents |
|---|---:|---|
| `bank_display_name` in BankData | 9 | sole Bank identity copy |
| `fs_identity_name[8][9]` | 72 | Scene, Kit, and six Instruments |
| Total active musical identity | 81 | exact fixed identity block |

`FS_IDENTITY_BANK_ROW` aliases BankData; it does not allocate a second Bank
row. `filesystem_setIdentityName()`, `filesystem_identityName()`,
`filesystem_identityNameMutable()`, and `filesystem_clearIdentityNames()` are
the cross-module ownership boundary.

`preset_currentName[8]` still exists as the legacy/current Load/Save character
editor field. It is UI transaction state, not another retained resident object
name.

No display name, filename, or file stem remains inside `scene_t`, `kit_t`, or
`kit_instrument_slot_t`.

## Final payload staging architecture

The proposed 9,000-byte name/staging union was rejected after hardware exposed
alias lifetime failures. Final storage is independent:

```text
fs_list_cache_name     9,000 B  browser index or HCNAMES only
fs_stage_workspace     2,048 B  one non-Pattern validation stage
staging_buf              512 B  existing stream/bulk-write scratch
```

`fs_stage_workspace` is an aligned union with these mutually exclusive views:

- one `kit_t`;
- one `kit_instrument_slot_t` candidate;
- one Scene stage containing `scene_settings_t` plus `kit_t`;
- raw 2,048-byte capacity.

Compile-time assertions enforce:

- the name cache is exactly 9,000 bytes;
- six voices times 64 current parameter cells fit below the 512-cell planning
  limit;
- three byte images for 512 cells consume 1,536 bytes;
- 384 bytes remain reserved in the size calculation for a future Effect stage;
- the union fits in 2,048 bytes and has `kit_t` alignment.

Pattern is intentionally absent from this union. Scene Load validates and
commits Scene settings plus embedded Kit first, then reads Pattern directly
into final Scene SRAM. Pattern failure after that point is non-atomic and does
not roll back the already committed settings/Kit. That is an explicit interim
decision pending the Pattern data redesign.

Current linked resident shapes after name removal are:

```text
kit_instrument_slot_t  193 B
kit_t                1,160 B
scene_settings_t        40 B
PatternSet          10,796 B
scene_t             11,996 B
scenes[16]         191,936 B
```

## Menu name and cache workflows

### Combined Kit/Instrument family

On entry for one resident Scene:

1. HCNAMES borrows the 9,000-byte cache.
2. Menu copies the Scene's Kit row and six Instrument rows into the single
   filesystem identity block.
3. The requested `/Kit/.hcindex` or typed Instrument `.hcindex` replaces the
   HCNAMES cache view.
4. The selected index remains resident while payload parsing and runtime apply
   use the separate 2,048-byte stage.

Menu stores only:

- source Scene coordinate;
- validity flag;
- dirty Scene mask;
- active type/row/slot control fields.

There is no seven-name Menu array. The seven strings are the filesystem
identity rows themselves.

Normal Kit Load/Save updates the Kit plus all six Instrument identity rows and
marks the affected Scene mask dirty. Normal Instrument Load/Save updates its
one Instrument identity row. Morph Load/Save preserves resident identity.

Leaving the combined family performs at most one HCNAMES rewrite. A clean
session performs none. Changing the selected resident Scene is an old-session
exit/new-session entry boundary so dirty rows are flushed before identities
are replaced.

### Scene menu

`scene_t` no longer has a display-name field. Scene Load/Save entry reads only
the selected resident Scene HCNAMES row, copies it into the single Scene
identity row/editor path, and then loads `/Scene/.hcindex`.

A successful root Scene Load updates the Scene name for every destination bit
in its immutable request mask. Scene Save updates its source Scene row. The
Scene HCNAMES updater restores `/Scene/.hcindex` before releasing the original
completion.

### Bank Load

Bank Load remains strictly mask-selective.

- The request mask is intersected with physically present child Scenes.
- An empty intersection loads no children; it does not fall back to all
  present children.
- Every selected child is rescanned and resolved one at a time.
- Settings, embedded Kit, and non-Pattern Scene data stage and commit for that
  child.
- Only after a child commits are its one Scene, one Kit, and six Instrument
  HCNAMES rows overlaid.
- Unselected resident Scene payload, name rows, and presence state remain
  unchanged.
- The active Bank row changes only when Bank metadata successfully commits.
- The register is written once and `/Bank/.hcindex` is rebuilt/restored before
  Preset sees completion.

The former arrays:

```text
op_bank_child_name[16][9]
op_bank_child_open_name[16][13]
op_bank_child_present[16]
```

were removed. A 16-bit present mask remains because it is occupancy/selection
state, not a name or key cache.

### Bank Save

Bank Save reads HCNAMES once before constructing its temporary tree. It obtains
each selected Scene, Kit, and Instrument identity from that cache, derives
components only for the immediate save step, promotes the completed temporary
Bank directory, updates the Bank row, writes HCNAMES once, and restores the
root Bank index.

Bank Save uses a temporary sibling and old sibling promotion sequence, but FAT
rename is not journaled. This is staged replacement, not a crash-recoverable
transaction.

## Instrument display, load, save, and reversible `kit` row

### Display/key rule

Instrument user names are at most eight stem characters plus `.` and the
registry-owned three-character extension. Menu and HCNAMES must never show the
extension.

`filesystem_copyInstrumentDisplayName()` copies only the padded stem from a
typed index row. This corrected the short-name display bug where `.snr`,
`.drm`, `.cym`, or `.hat` appeared after a short stem.

Normal Instrument leaves are derived from the fixed identity/index stem and
Instrument type extension. No per-resident Instrument filename or stem is
stored in SceneData.

The active `storage_kitset_t` parser does retain six LFN-sized `file=` values
while one Kit or Scene is being parsed. The linked `op_kitset` object is 312
bytes. These values are operation-local schema references needed to open the
six files named by that specific `kitset.kcg`; they are not resident Scene
keys, an `.hcindex` cache, or a per-library lookup table.

### Hidden temporary source

Normal Instrument Load has a synthetic `kit` row immediately above pool row
`000`. The abandoned SRAM two-image preview was replaced by:

```text
Instrument/<type>/.hctmp.<ext>
```

Entry behavior:

1. Copy the current HCNAMES Instrument identity into
   `menu_instrumentTempName[9]`.
2. Capture its type and validity.
3. Save the current Scene/voice through the normal Instrument serializer to the
   typed `.hctmp.<ext>`.
4. Do not update `.hcindex`, HCNAMES, or the typed browser cache.

Restore behavior:

1. Decrementing from `000` selects `kit`.
2. The exact hidden component is passed directly to the normal Instrument
   loader; it must not pass through the normal display-stem formatter.
3. The file parses into the one Instrument candidate stage.
4. Preset commits/applies it through the normal bounded Instrument transaction.
5. The saved nine-byte label is restored to the active identity row.

The hidden file is excluded from:

- typed Instrument index insertion;
- the boot Instrument canonical-name repair pass.

The file may remain on SD after exit. Its SRAM session name/type/validity is
cleared when the user leaves the menu, changes Scene, changes voice, changes
Instrument type, changes load/morph mode, or otherwise invalidates the nested
session. A new entry overwrites the file for that type.

### Encoder and async completion rules

Instrument pool scrolling is allowed while an older immutable request drains.
The cursor and Preset request coordinates are therefore different ownership
domains:

- Menu cursor state may move immediately.
- An accepted Preset/filesystem request keeps immutable Scene/voice/type/index
  coordinates until completion.
- `menu_instrumentTempOperationPending` tags accepted temporary save/load work;
  completion must not infer operation identity from the mutable cursor.
- Pool-to-pool movement coalesces only the latest desired row through
  `menu_deferSelectionRequest`.
- A negative detent while already on `kit` is a strict no-op; only the real
  `000` to `kit` transition posts one restore.
- Reaching `kit` cancels any stale deferred pool request because `kit` is
  restored through the temporary completion chain, not the pool retry path.

The last rule is the final rapid-backspin correction. Without it, a fast turn
could leave an obsolete pool retry armed after the correct temporary restore,
causing the idle dispatcher to reassert storage work with the cursor on `kit`
and make number input appear frozen.

## Name canonicalization and repair

Canonical forms are:

```text
root Kit/Scene/Bank: NNN <up to 8 display characters>
Bank-local Scene:    SS <up to 8 display characters>
Instrument file:     <up to 8 stem characters>.<typed extension>
```

Repair runs:

- inside `filesystem_createLibraryIndexBlocking()` before Kit, Scene, or Bank
  scanning/index publication;
- inside `filesystem_createBootIndexBlocking()` before typed Instrument scans;
- as the internal preflight of `filesystem_requestLoadBank()` before Bank
  payload loading.

Repair scans one parent, selects one noncanonical object, closes iterator
ownership, renames the object, calls `afatfs_sync()`, and rescans from disk.
There is no per-directory name/alias table.

Canonical comparison trims trailing LCD padding before generating a FAT
component. Duplicate targets retry with decimal suffix digits overwriting the
shortest tail inside the eight-character stem.

The exact hidden `.hctmp.<ext>` component is reserved and skipped.

`/.hcrepair` was planned but not implemented. Repair therefore provides ordered
rename/sync behavior only and cannot claim deterministic roll-forward after
power loss.

Root Kit directories and selected Bank embedded Kits receive an additional
quarantine/validation pass. Invalid Kit trees are made non-loadable rather than
published. A root Scene blocking quarantine prototype was removed after it
caused a boot hang; a future implementation must be foreground-pumped.

## asyncfatfs handle lifetime and capacity

The original application handle pool had three `afatfsFile_t` slots. Boot
diagnostics isolated a freeze at Bank repair phase 43/substep 20: Bank root,
selected Bank, and Scene handles were all retained while the embedded Kit open
requested a fourth slot.

The targeted lifetime fix:

- close Bank root immediately after the selected Bank handle opens;
- after `afatfs_chdir(kit_handle)`, close the explicit Kit handle because
  `currentDirectory` already owns the copied directory state;
- leave slots available for `kitset.kcg` and member files.

`currentDirectory` is not one of the application handle slots. Entering a
directory does not require retaining its explicit open handle.

The pool was subsequently expanded from three to five slots as accepted
concurrency headroom. `sizeof(afatfsFile_t)` is 328 bytes, so the two extra
slots add exactly 656 zero-initialized SRAM1 bytes. The expansion does not
replace correct handle closure.

## Cache and scratch disposal

Removed:

- KitBrowser `kb_map` and controls: 2,004 linked bytes.
- Per-Scene Scene name: 144 bytes.
- Per-Scene Kit name and six Instrument display names/stems: 2,640 bytes.
- Menu seven-name array and independent Scene-name string; strings moved into
  the sole identity block.
- Bank-local child name/alias/presence arrays: 368 declared bytes.
- File/Dir root object list caches: 6,240-byte list payload. The normal type
  cycle and filesystem/Preset compatibility calls no longer expose or populate
  those lists.
- Firmware recursive-delete name/alias stacks: 558 bytes.
- Six preformatted Kit-save member filename components; save derives one
  component in `op_filename_component[49]`.
- Dedicated Kit, Instrument, and full Scene stage symbols.

Retained intentionally:

| Storage | Linked bytes | Reason |
|---|---:|---|
| `fs_list_cache_name` | 9,000 | sole index/HCNAMES cache |
| `fs_stage_workspace` | 2,048 | one non-Pattern validated payload |
| Bank + filesystem identity rows | 81 | active Bank/Scene/Kit/six-Instrument names |
| `menu_instrumentTempName` | 9 | label paired with current `.hctmp` session |
| `staging_buf` | 512 | existing bulk stream scratch |
| `op_line_buf` | 160 | text read line |
| `op_write_line_buf` | 160 | text write line |
| `op_lfn_name` | 80 | one FAT LFN iterator accumulator |
| `op_filename_component` | 49 | one derived member leaf |
| `op_kitset` | 312 | one active six-member manifest parse |
| request display/open buffers | 9/13/49 each | asynchronous arguments whose lifetime spans ticks |
| `afatfs` | 7,344 | async FAT state including five application handles |

Residual diagnostic qualification:

- `menu_testEditName[49]`, `menu_testResultName[49]`, and nine bytes of result
  bytes/timer/kind/flags still link: 107 bytes total. They are unreachable
  compatibility UI state, not musical names or multi-entry caches, but they are
  real SRAM and were not disposed.
- Boot diagnostic observer APIs also remain in source. Production passes NULL
  callbacks because `CONFIG_DEV_MODE` is zero.

Request display/open strings are operation scratch, not authoritative names.
They remain because asyncfatfs request arguments and returned aliases must
survive across foreground-pumped phases. The current source still has distinct
scratch for operations that require simultaneous source/destination names,
notably Bank promotion and canonical rename.

## File-by-file implementation summary

### `Core/Bank/Scene/SceneData.h` and `.c`

- Removed `scene_t.display_name`.
- Removed `kit_t.display_name`.
- Removed six Instrument display names and six retained filename stems.
- Removed their setters/accessors and file-name normalization helpers.
- Kept only playable Scene settings, PatternSet, Kit settings, Instrument type,
  and three 64-byte parameter images per voice.
- Added/updated comments declaring filesystem HCNAMES/identity APIs as the
  only name authority.

### `Core/Hardware/SD/storageTypes.h` and `.c`

- Kept storage parsing independent of asyncfatfs.
- Updated Instrument filename generation to accept the authoritative
  fixed-width identity stem rather than Scene-retained stems.
- Kept strict typed extension validation and display-name normalization.
- Kept `storage_kitset_t` as one-operation manifest state containing the six
  member filenames supplied by `kitset.kcg`.
- Preserved descriptor-keyed Instrument serialization and normal/Morph write
  projections.

### `Core/Hardware/SD/filesystem.h` and `.c`

- Added HCNAMES fixed-row read/update operations and identity-row accessors.
- Added Scene/Kit/Instrument resident-name APIs which borrow the single cache.
- Added canonical name-repair wrappers and Bank repair/load chaining.
- Added independent 9,000-byte cache and 2,048-byte aligned stage with static
  size/alignment assertions.
- Changed Scene staging to settings plus Kit only and moved Pattern to direct
  final-SRAM read after commit.
- Removed resident-name/stem dependencies from Kit/Scene/Bank/Instrument
  load/save.
- Added on-demand Instrument stem display copying.
- Added hidden Instrument temporary save/load requests and exact-filename path.
- Excluded `.hctmp.<ext>` from scan/index and repair.
- Reworked Bank Load to rescan one child at a time and preserve exact selective
  masks.
- Retired File/Dir diagnostic list operations as zero-work compatibility APIs.
- Removed firmware recursive-delete traversal and delegated concrete tree
  deletion to asyncfatfs.

### `Core/Bank/Scene/Preset/presetManager.h` and `.c`

- Exposed immutable Kit request Scene mask for post-completion name ownership.
- Added hidden temporary Instrument save/load operation wrappers and operation
  type.
- Preserved the existing ordered Instrument commit/apply transaction.
- Kept root Scene/Bank/Kit masks at the Preset boundary rather than letting
  Menu call filesystem internals directly.
- Converted retired File/Dir diagnostics to zero-return compatibility stubs.

### `Core/Menu/menu.c`

- Replaced Menu-owned name strings with direct use of filesystem identity rows.
- Added combined Kit/Instrument HCNAMES session coordinates and dirty mask.
- Added single-Scene HCNAMES entry behavior.
- Kept active `.hcindex` resident through separate payload staging.
- Added extension-free Instrument display helper use.
- Added hidden temporary `kit` session name/type/validity/operation state.
- Added `kit` above `000` with clamped, idempotent negative behavior.
- Corrected async completion ownership and stale deferred-selection handling.
- Removed File/Dir/sDir entries from the menu cycle.

### `config.h` and boot diagnostics

- Production ends with `CONFIG_DEV_MODE 0`.
- Existing boot-stage/operation/substep observers are compiled out at zero.
- Dev Mode no longer restores the retired File/Dir/sDir types or their list
  caches; it controls only the retained boot diagnostic observers.
- A later Instrument-menu observer was removed completely because it replaced
  the normal UI and invalidated the test.

### `main.c`

- Kept boot library/index generation ahead of audio initialization.
- Reloaded `/Bank/.hcindex` after typed Instrument index generation reused the
  sole cache, then ran the Bank/Scene/Kit fallback ladder.
- Removed the post-load resident-name snapshot call. Boot must not reconstruct
  HCNAMES from partial resident identity after a mask-selective Bank Load.
- Retained the Boot/FS, operation/phase, and substep OLED observers only behind
  `CONFIG_DEV_MODE`; production callback arguments are NULL and the observer
  calls compile to no-ops.

### `Core/Hardware/SD/asyncfatfs/asyncfatfs.h` and `.c`

- Application handle capacity is five.
- Correct traversal still closes explicit directory handles after chdir where
  their copied current-directory state is sufficient.

## Regression and diagnostic chronology

### HCNAMES writer and boot freeze

The first HCNAMES writer used a private blocking open/write path and hardware
froze. It was replaced by a standard foreground-pumped filesystem operation.
When hardware still froze and no HCNAMES observer appeared, the freeze was
proven to occur earlier in initial payload loading.

Boot diagnostics narrowed the location:

```text
Boot 11 / FS 1
FOp 1 / FPhs 42
FOp 1 / FPhs 43
FPhs 43 / FSub 20
```

This proved selected-Bank name repair/quarantine was blocked opening an
embedded Kit because the three application handles were exhausted. Closing
redundant parent/Kit handles fixed boot. A speculative tri-state result change
did not affect the stall and was reverted.

### Blank `.hcindex` regression

The first 9,000-byte cache/staging union made scan output and payload stages
alias. Generic request initialization cleared scan rows before the chained
index writer consumed them, generating blank indexes. Removing generic clears
fixed one handoff, but selected keys could still be erased when staging claimed
the union after request validation.

Request-stable selected-key capture fixed `KitL00`, but hardware still exposed
scroll/load aliasing. The union design was then abandoned. The final design
uses independent 9,000-byte name and 2,048-byte payload allocations.

### Kit HCNAMES blank row and slow names

A targeted Kit update originally reused Bank presence as the predicate for
whether the Kit row should be nonblank. A successful Kit Save is itself proof
of valid source identity, so targeted Kit rows now use the action as the
presence predicate. Normal Kit Save also reaffirms the Kit name from the
durable `/Kit/.hcindex` row before HCNAMES borrows the cache.

Per-scroll/per-action HCNAMES traffic made Kit/Instrument names slow. The final
combined family session reads names only on entry and writes only on exit,
while the independent index remains resident for immediate row display.

### Instrument `kit` preview regressions

The two-image SRAM preview initially appeared to work but violated the desired
final ownership and behaved badly under cursor overflow/session changes. It was
replaced by `.hctmp.<ext>`.

Subsequent fixes were deliberately narrow:

1. Skip `.hctmp` in boot repair and typed index scan to prevent boot loops.
2. Preserve the exact hidden filename instead of sanitizing it to `none.ext`.
3. Do not repost restore for every negative detent already clamped at `kit`.
4. Tag accepted temporary operations independently of the mutable cursor.
5. Clear a stale deferred pool selection when `kit` becomes the final desired
   row after rapid backspin.

One proposed Instrument-menu diagnostic observer was rejected after hardware:
it occupied the screen, selected the wrong test context, disturbed audio, and
did not reproduce the freeze. That observer and its calls were removed.

## Decisions explicitly not to carry forward

- Do not reintroduce the KitBrowser bridge.
- Do not add per-library, per-Instrument-type, per-Scene, or per-Bank-child
  name/key arrays.
- Do not alias the 9,000-byte name cache with payload staging.
- Do not stage Pattern in the 2,048-byte workspace.
- Do not retain an original Instrument parameter image in SRAM for the `kit`
  row; use the hidden typed temporary file.
- Do not regenerate HCNAMES from resident SRAM at boot.
- Do not treat a zero or nonmatching Bank Load mask as “load all present.”
- Do not expose File/Dir/sDir diagnostics or restore their list caches.
- Do not infer an accepted operation from the current Menu cursor.
- Do not retain explicit directory handles solely because they were used for
  `afatfs_chdir()`.
- Do not claim HCNAMES versioning, `.hcrepair` recovery, Pattern atomicity, or
  Bank promotion crash safety before those mechanisms exist.

## Remaining work and cautions

- Hardware-retest the final rapid negative Instrument backspin:
  `kit -> 000/001/... -> fast decrement -> kit -> positive increment`. Verify
  correct restore and that pool scrolling resumes.
- HCNAMES needs a future versioned/keyed format and recovery policy if its
  fixed-order first pass is replaced.
- Implement `/.hcrepair` or another durable repair journal before claiming
  power-loss roll-forward.
- Root Scene invalid-Kit quarantine needs a foreground-pumped design if it is
  reintroduced.
- Pattern persistence and load atomicity remain owned by the future Pattern
  redesign.
- Effect persistence remains a placeholder.
- Operation-local filename/alias scratch can be consolidated only where
  asyncfatfs input lifetimes and simultaneous rename source/destination needs
  permit it.
- The SD test tree contains a dirty
  `SD_CARD/Instrument/Drum/.hctmp.drm`. It is useful hardware evidence and is
  intentionally not indexed, but it is not authoritative library content.

## Permanent documentation updated at closeout

The closeout reconciles the live architecture into:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`
- `knowledge_files/specification_reference/SRAM_DTCM_MANIFEST.md`
- `MEMORY.md`

Historical Session 042 planning files may then be deleted without losing the
final architecture, regressions, rejected approaches, or remaining cautions.
