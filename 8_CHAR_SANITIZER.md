# 8-Character Name Sanitizer Plan

## Purpose

Canonicalize user-visible storage names before the firmware publishes `.hcindex`
files or loads a Bank. This is not a replacement for the single shared
directory-list cache. The cache remains the SRAM work surface for scanning,
sorting, and writing directory indexes.

The sanitizer exists to make resident object identity reconstructible from
slot/type plus one eight-character display name, so later work can remove the
separate resident filename/key fields described in `NAMES_SRAM_MANIFEST.md`.

## Naming Contract

- Root Instrument files are canonical when their basename is one to eight
  printable product characters and their extension matches the registered type.
  The persistent key is `<8-char-display>.<ext>`.
- Root Kit, Scene, and Bank directories are canonical when their visible
  component is `NNN <8-char-display>`, where `NNN` is the direct slot
  `000..999`.
- Bank-local Scene directories are canonical when their visible component is
  `SS <8-char-display>`, where `SS` is the direct child slot `00..15`.
- Firmware-created saves must already produce canonical names. Host-created or
  hand-edited non-canonical names are repaired by deterministic rename before
  indexing/loading.
- Duplicate canonical targets are resolved by appending decimal suffix digits
  inside the eight-character display stem. Examples: `SnareDru`, `SnareDr0`,
  `SnareD00`, `Snare000`, etc. The exact suffix-width algorithm should choose
  the shortest suffix that produces a free canonical key.

## Safety Model

FAT rename is not journaled. The available primitive,
`afatfs_renameObject_lfn()`, preserves payload clusters and children while
rewriting the directory entry name run, and `afatfs_flush()` confirms dirty
cache entries have reached the card. This gives ordered, one-object-at-a-time
repair, not strict power-loss atomicity.

Add a root marker file `/.hcrepair` before a repair rename begins. The marker
records the domain, parent path, old display component, intended new display
component, and phase. On boot, sanitizer startup must read this marker first,
rescan the physical parent, and either finish the intended rename, observe that
it already completed, or clear the marker when neither name remains. `.hcindex`
and future `.hcnames` updates must happen only after the marker is clear and
the physical repair pass has completed.

## Implementation Sequence

## Work Notes

- Implemented first sanitizer pass in `Core/Hardware/SD/filesystem.c/h`.
- Added `FS_INTERNAL_OP_REPAIR_NAMES` and public wrappers for root library,
  Instrument, and selected-Bank repair.
- Root Kit/Scene/Bank repair is now called inside
  `filesystem_createLibraryIndexBlocking()` before the scan/cache write.
- Instrument repair is now called inside `filesystem_createBootIndexBlocking()`
  before typed Instrument scans.
- Bank Load now starts the repair operation first and, on success, internally
  continues into `FS_INTERNAL_OP_LOAD_BANK` without releasing Preset's callback.
- The repair op scans one parent, renames one non-canonical object, syncs, then
  rescans from disk. It does not add a 1,000-row cache or alias table.
- Current first pass does ordered rename/sync repair but does not yet write and
  roll forward a `/.hcrepair` marker. Add that before claiming deterministic
  power-loss recovery beyond the physical FAT state after sync.
- Verification after the first implementation: `make -j2`, `make img`, and
  `git diff --check` pass. The build still reports pre-existing unused
  delete-helper warnings in `filesystem.c` and the usual libc syscall stub
  linker warnings.
- Hardware test reported a boot-screen freeze with no `/.hcrepair` file. Since
  `/.hcrepair` is not implemented in this first pass, the likely cause was the
  repair loop formatting root and Bank-local directory targets with padded
  eight-cell LCD names. Follow-up build trims trailing display spaces when
  generating repair-only FAT components, so already-canonical names like
  `000 Kit` compare equal after the next physical scan instead of being
  repaired forever.
- Hardware also briefly showed duplicate `Drum`/`Snare` directories. Audit
  found the `.hcindex` Instrument writer still using create-capable
  `mkdir_lfn()` directly for `Instrument/` and `Instrument/<Type>/`. The index
  generators now use `opendir_lfn()` first and fall back to `mkdir_lfn()` only
  after a strict miss. The root Kit/Scene/Bank `.hcindex` writer and Instrument
  Save's type-directory setup received the same open-before-create guard.
- Hardware later showed duplicate `Drum`/`Snare` directories again. Boot
  Instrument `.hcindex` generation no longer creates `Instrument/` or
  `Instrument/<Type>/` at all; it treats missing folders as empty namespaces.
  Instrument Save remains the only path that may create a type folder, because
  save has a concrete file write and should be the only boot-independent
  directory-creation authority.
- Follow-up policy change: Kit internals are not repaired in place. Before Kit
  `.hcindex` publication, each root Kit directory is opened, `kitset.kcg` is
  parsed, every `file=` basename is required to fit eight characters, and every
  member filename must open in that same Kit directory. A bad root Kit is
  renamed to an `errNNN ...` component so the Kit scanner will not publish it.
- Bank Load now runs the same embedded-Kit quarantine pass after selected-Bank
  child Scene name repair and before payload load. A bad Kit inside
  `Bank/<NNN Bank>/<SS Scene>/` quarantines both the embedded Kit and its
  Bank-local Scene child, then the Bank load refuses to continue.
- A root Scene boot quarantine pass was prototyped but removed from this test
  image after a boot hang. Add that later as a foreground filesystem operation
  instead of a nested blocking tree walk.
- First-pass `/.hcnames` generation was added after the initial boot
  Bank/Scene/Kit load/fallback chain. It writes a fixed-order resident-memory
  snapshot: Bank name, sixteen Scene names, sixteen embedded Kit names, then
  sixteen groups of six Instrument display names. Blank/unloaded/default
  `none` rows serialize as blank lines.

1. Add sanitizer public API declarations in `Core/Hardware/SD/filesystem.h`.
   Planned functions:
   - `uint8_t filesystem_repairLibraryNamesBlocking(fs_library_index_kind_t kind);`
   - `uint8_t filesystem_repairInstrumentNamesBlocking(void);`
   - `bool filesystem_requestRepairBankNames(uint16_t slot, fs_completion_cb_t cb);`

2. Add internal operation types in `Core/Hardware/SD/filesystem.c` near
   `fs_internal_op_t`:
   - `FS_INTERNAL_OP_REPAIR_LIBRARY_NAMES`
   - `FS_INTERNAL_OP_REPAIR_INSTRUMENT_NAMES`
   - `FS_INTERNAL_OP_REPAIR_BANK_NAMES`

3. Add operation-local sanitizer scratch in `filesystem.c`, near the existing
   file/directory scan scratch:
   - selected repair domain/type/slot
   - current object identity and display name
   - proposed canonical display component
   - suffix counter
   - marker file phase
   - one generated short alias buffer

   Do not add per-slot name, alias, or occupancy arrays. The sanitizer scans
   one parent directory at a time and reuses the existing shared cache only
   after repair completes.

4. Implement canonical-name helpers in `filesystem.c` using existing product
   parsers/formatters from `storageTypes.c/h`:
   - root numbered directories should reuse `storage_parseNumberedFolder()`.
   - Bank-local Scene children should reuse `storage_parseBankSceneFolder()` and
     `storage_formatBankSceneDir()`.
   - Instrument files should reuse the registry extension/type checks currently
     used by `filesystem_recordInstrumentFile()`.
   - eight-cell copying should reuse `storage_copyDisplayName()`.

5. Add `.hcrepair` marker helpers in `filesystem.c`:
   - write marker
   - flush marker
   - perform rename
   - flush rename
   - delete or truncate marker
   - flush marker removal
   - continue/roll-forward existing marker at sanitizer start

6. Insert boot repair before each root `.hcindex` write in `main.c`:
   - before `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_KIT)`
   - before `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_SCENE)`
   - before `filesystem_createLibraryIndexBlocking(FS_LIBRARY_INDEX_BANK)`
   - before `filesystem_createBootIndexBlocking()`

   Each repair call must complete while audio is still stopped, then the
   existing physical scan and index generation can proceed from canonical names.

7. Insert Bank repair before loading a Bank:
   - in `filesystem_requestLoadBank()` or immediately before it in
     `preset_loadBank()`.
   - Preferred: expose and call `filesystem_requestRepairBankNames(slot, cb)` as
     an internal preflight in the Bank-load state machine, so filesystem owns
     the root Bank open, selected Bank child scan, `.hcrepair`, and eventual
     `FS_INTERNAL_OP_LOAD_BANK` continuation under the single-operation pump.

8. After repair succeeds, rescan the repaired parent before continuing:
   - root Kit/Scene/Bank repair must be followed by the existing
     `filesystem_requestScanKits()`, `filesystem_requestScanScenes()`, or
     `filesystem_requestScanBanks()` before `.hcindex` writing.
   - Instrument repair must be followed by each existing typed Instrument scan
     before its `.hcindex` write.
   - Bank pre-load repair must refresh the root Bank index/cache if the root
     Bank folder itself changed, then scan Bank-local children after opening the
     selected Bank.

## Specific Code Changes And Comment Text

### `main.c`

Change the boot block at `main.c:304-373`. Insert root-library repair calls
after each physical scan and before each `filesystem_createLibraryIndexBlocking`
call. Insert Instrument repair before `filesystem_createBootIndexBlocking()`.

Comment text for `main.c`:

```c
/*
 * Canonicalize host-created names before publishing `.hcindex`.
 *
 * Why: resident Bank/Scene/Kit identity is moving toward slot/type plus one
 * eight-character display name. A long LFN object can still be browsed by FAT,
 * but it cannot be regenerated later from bounded resident state. Boot is the
 * safe place to repair because audio is not running; after repair, the normal
 * scan/index pass observes the physical card and writes indexes only for
 * canonical objects.
 */
```

### `Core/Hardware/SD/filesystem.h`

Add public boot/runtime repair declarations near the existing index helpers at
`filesystem.h:120-132`.

Comment text for `filesystem.h`:

```c
/*
 * Repair host-created long or duplicate product names before index generation.
 *
 * Inputs: a mounted card and one product namespace. Output: all accepted
 * objects in that namespace have canonical display components that can be
 * reconstructed from slot/type plus eight display cells. The implementation
 * uses `/.hcrepair` and one-object rename/flush steps; it does not allocate a
 * parallel browser cache and it does not claim filesystem-level power-loss
 * atomicity.
 */
```

For Bank pre-load:

```c
/*
 * Repair one root Bank folder and its Bank-local Scene children before load.
 *
 * Inputs: root Bank slot from the active Bank cache. Output: the selected Bank
 * directory and its immediate `00..15` children use canonical names before the
 * Bank payload reader captures Scene/Kit provenance. This prevents resident
 * state from inheriting long or duplicate host-created keys.
 */
```

### `Core/Hardware/SD/filesystem.c` operation enum

Add sanitizer operations near the existing scan/index operations at
`filesystem.c:125-169`.

Comment text:

```c
/*
 * Name repair is a physical-card preflight for index generation and Bank Load.
 * It walks one parent directory at a time, writes `/.hcrepair`, renames one
 * object, flushes, clears the marker, and then resumes scanning. It must never
 * populate a second 1,000-entry cache; the normal scan/index pass runs after
 * repair to publish browser rows.
 */
```

### `filesystem_createLibraryIndexBlocking()`

Modify `filesystem_createLibraryIndexBlocking()` at
`filesystem.c:12937-13005` so it optionally starts the matching repair pass
before the existing scan fallback and writer.

Comment text:

```c
/*
 * Repair precedes scan because the scan cache is the index source.
 *
 * A long or duplicate host-created directory can parse into a valid slot but
 * cannot be regenerated later from an eight-character resident display field.
 * Repairing first lets the existing scan choose canonical physical names and
 * keeps `.hcindex` a faithful cache of the post-repair card, not a record of
 * names the product can no longer open by construction.
 */
```

### `filesystem_createBootIndexBlocking()`

Modify `filesystem_createBootIndexBlocking()` at `filesystem.c:12865-12935`.
Before each typed Instrument scan, repair that registry-defined
`Instrument/<Type>/` directory.

Comment text:

```c
/*
 * Instrument repair runs per registry type before the typed scan.
 *
 * The Instrument browser is alphabetically sorted, but saved Kit membership
 * should only need the eight-character stem plus the descriptor extension.
 * Host-created long stems are renamed into deterministic eight-character
 * stems before the cache is populated, so `.hcindex`, Instrument Load, and
 * later Kit Save all see the same canonical key.
 */
```

### `filesystem_scanKits_tick()`, `filesystem_scanScenes_tick()`,
`filesystem_scanBanks_tick()`

Do not make these scanners repair names directly. They should remain
read-only scanners at `filesystem.c:10400-10745`.

Comment text to add if needed:

```c
/*
 * This scanner records the physical post-repair namespace only.
 *
 * Name repair is intentionally a separate preflight so scan remains read-only
 * and can be reused by menus and save refreshes without surprising writes.
 * If a caller needs canonicalization, it must run the sanitizer before this
 * scan and then let this function rebuild the shared cache from disk.
 */
```

### `filesystem_scanInstruments_tick()` and `filesystem_recordInstrumentFile()`

Keep sorted-cache insertion in `filesystem_recordInstrumentFile()` and
`filesystem_scanInstruments_tick()` read-only. Add any canonical-only assertion
or diagnostic after repair exists, not a rename from inside the scanner.

Comment text:

```c
/*
 * Instrument scan assumes sanitizer ownership of long-name repair.
 *
 * The scanner still classifies by visible filename and extension, then sorts
 * the shared cache. It does not rename while iterating because mutation during
 * iterator ownership would make duplicate handling and `.hcrepair` recovery
 * ambiguous.
 */
```

### `preset_loadBank()`

At `Core/Bank/Scene/Preset/presetManager.c:1587-1608`, either keep the call
unchanged if repair is internal to `filesystem_requestLoadBank()`, or add a new
preset operation only if the filesystem cannot self-chain the repair preflight.

Preferred comment if left unchanged:

```c
/*
 * Bank-load name repair is owned by filesystem_requestLoadBank().
 *
 * Preset posts one logical Bank Load. Filesystem may run a repair preflight
 * before payload parsing, but Preset still observes one completion and keeps
 * the existing Bank fallback/apply policy.
 */
```

### `filesystem_requestLoadBank()` and `filesystem_loadBankDirectory_tick()`

At `filesystem.c:13255-13290`, start a Bank repair preflight before normal
`FS_INTERNAL_OP_LOAD_BANK`, or add an initial phase inside
`filesystem_loadBankDirectory_tick()` at `filesystem.c:5022-5550`.

Preferred design: keep `filesystem_requestLoadBank()` as the public request,
but have the request start `FS_INTERNAL_OP_REPAIR_BANK_NAMES`; on successful
repair, the repair state switches `current_op` to `FS_INTERNAL_OP_LOAD_BANK`
without releasing the original callback.

Comment text:

```c
/*
 * Bank Load repairs names before capturing resident provenance.
 *
 * A Bank payload can pull in sixteen Scene folders, embedded Kit folders, and
 * six Instrument member filenames per Kit. Those names are only needed one at
 * a time, and future resident state should not retain long FAT keys. Repairing
 * the selected Bank tree before parsing lets load/save reconstruct keys from
 * slot/type plus the bounded display name recorded in `.hcnames`.
 */
```

### `afatfs_renameObject_lfn()` Use

Use the existing asyncfatfs API declared at
`Core/Hardware/SD/asyncfatfs/asyncfatfs.h:194-217`. Do not change asyncfatfs
unless testing proves the current rename cannot cover one-object canonical
renames.

Comment text near the call site:

```c
/*
 * Rename preserves payload identity; `.hcrepair` preserves product intent.
 *
 * asyncfatfs rewrites the object's VFAT/SFN name run while preserving clusters,
 * size, attributes, timestamps, and children. The marker file lets the next
 * boot roll forward if power fails between marker write, rename, and marker
 * removal. This is ordered repair, not a journaled FAT transaction.
 */
```

## Open Design Details

- Define the exact suffix algorithm for duplicate canonical names. It should be
  deterministic, bounded, and independent of scan order where possible.
- Decide whether canonical repair preserves case from the source prefix or
  normalizes to a product case policy.
- Decide whether a long name that cannot be represented after suffixing is
  ignored, repaired to a generated fallback such as `ObjectNN`, or treated as a
  repair error.
- Decide whether malformed but parseable short aliases such as `001SLA~1`
  should be repaired into a normal `001 SLA~1` display component or only
  accepted as compatibility fallback.
- Decide how much `.hcrepair` marker content is text vs fixed binary. Text is
  easier to inspect on a host; fixed binary is easier to parse in bounded code.

## Non-Goals

- Do not remove the 9,000-byte shared directory-list cache.
- Do not consume `.hcnames` as a load source yet.
- Do not remove resident name fields until sanitizer behavior and `.hcnames`
  load/save provenance are hardware-tested.
- Do not claim power-loss atomicity. The goal is deterministic, idempotent
  recovery using `/.hcrepair`, not a journaling filesystem.
