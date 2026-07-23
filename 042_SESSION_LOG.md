# Session 042 Running Log

DATE: 2026-07-20

SESSION GOAL: Retire the legacy `kitBrowser` compatibility bridge, remove the
resident generalized SRAM name cache, and replace it with a root `.hcnames`
representation while preserving current Load/Save behavior and the
single-context asynchronous filesystem contract.

SESSION STATE: Pre-implementation audit and plan. No production source has
been changed in this session; this file is the first running-log entry.

## Evidence reviewed

The session brief was read first from `SESSION_042_PRE_PLAN.md`. The current
project memory and previous handoffs were then checked:

- `MEMORY.md`
- `knowledge_files/log_archive/040_SESSION_HANDOFF_LOG.md`
- `knowledge_files/log_archive/041_SESSION_HANDOFF_LOG.md`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- `knowledge_files/specification_reference/SRAM_DTCM_MANIFEST.md`
- `NAMES_SRAM_MANIFEST.md`
- `SCOPING_TARGETS.md`
- `knowledge_files/drafts/LEGACY_PARAM_RETIRE.md`

The active-tab path `knowledge_files/specification_reference/MEMORY_AUDIT.md`
does not exist in this checkout. The current source-derived memory document is
`knowledge_files/specification_reference/SRAM_DTCM_MANIFEST.md`; the final
documentation pass must reconcile that path/reference rather than silently
claiming that the missing audit was reviewed.

## Current code findings

### KitBrowser bridge

The bridge is source-linked but has no live application caller:

- `Makefile:74` adds `Core/Hardware/SD/kitBrowser.c` to the build.
- `Core/Hardware/SD/filesystem.c:74` includes `kitBrowser.h`.
- `Core/Hardware/SD/kitBrowser.c:58-64` declares `kb_map[1000]`,
  `kb_numKits`, `kb_mapIndex`, `kb_dirty`, `kb_name_pending`, and
  `kb_kitName[9]`.
- `Core/Hardware/SD/kitBrowser.c:119-188` implements the old browser API.
- `Core/Hardware/SD/kitBrowser.h:49-54` exports the four `kitBrowser_*`
  functions and the 1,000-entry map limit.
- `Core/Hardware/SD/filesystem.c:796-804` imports `kb_map` and `kb_numKits`
  as externals.
- `Core/Hardware/SD/filesystem.c:2745-2763` writes the compatibility map.
- `Core/Hardware/SD/filesystem.c:2811` and `2863` invoke that writer from Kit
  directory discovery.
- `Core/Hardware/SD/filesystem.c:13398-13408` clears `kb_numKits` when a Kit
  scan begins.

An exact repository-wide symbol audit found declarations/definitions only for
`kitBrowser_init`, `kitBrowser_encoderDelta`, `kitBrowser_tick`, and
`kitBrowser_getCurrentKit`; no Menu, Preset, or `main.c` call remains. The
existing Load page uses the newer direct accessors at
`Core/Menu/menu.c:2778-2804` and `4972-4982`.

The generic name-read API is not all dead: `presetManager.c:1696-1721`
still calls `filesystem_requestLoadName()` for legacy name-header file types.
Therefore the bridge removal must not delete `filesystem_requestLoadName()`,
`filesystem_loadedName()`, or `FS_INTERNAL_OP_LOAD_NAME` wholesale. Only the
KitBrowser-specific Kit path and its documentation/client claim are removed;
the legacy Preset path remains until its own file-type migration retires it.

The current linked image confirms the practical SRAM target:

```text
kb_map      0x7d0 = 2,000 B
kb_numKits  0x2   =     2 B
kb_dirty    0x1   =     1 B
kb_name_pending  0x1   = 1 B
linked bridge total       2,004 B
```

`kb_mapIndex` and `kb_kitName` are present in source but dead-stripped from the
current ELF. The source-level bridge accounting remains 2,013 B as recorded by
`NAMES_SRAM_MANIFEST.md`; the post-change measurement must use the linked ELF.

### Existing shared cache and `.hcindex` behavior

The current generalized SRAM cache is defined at
`Core/Hardware/SD/filesystem.c:571-599`:

```text
fs_list_cache_kind       1 B
fs_list_cache_type       1 B
fs_list_cache_count      2 B
fs_list_cache_name[1000][9]  9,000 B
```

Its lifecycle is implemented by:

- `filesystem_clearNameCacheStorage()` at `filesystem.c:651-657`;
- `filesystem_prepareLibraryNameCache()` at `filesystem.c:681-692`;
- direct-domain borrowing at `filesystem.c:703-727`;
- Instrument index loading at `filesystem.c:2122-2293`;
- root `.hcindex` writing at `filesystem.c:2295-2419`;
- root `.hcindex` loading at `filesystem.c:2457-2580`;
- public domain accessors at `filesystem.c:14019-14267`;
- Menu index entry and invalidation at `menu.c:2830-2888`,
  `3907-3921`, and `6800-6803`.

`.hcindex` semantics are not interchangeable across domains:

- Kit, root Scene, and root Bank indexes contain exactly 1,000 slot rows;
  blank rows are retained and row number is the direct `000..999` slot.
- Instrument indexes contain sorted non-blank rows for one registered type;
  the active type is carried separately by the cache tag.
- Kit/Scene/Bank Save completion already holds the original callback while a
  physical rescan and complete `.hcindex` rewrite run. The chain is at
  `filesystem.c:2421-2455`, with save phases setting the refresh kind at
  `filesystem.c:8012-8014`, `9015-9018`, and the corresponding Bank path.
- Boot currently scans/writes the root indexes and Instrument indexes in
  `main.c:303-373`, then reloads `/Bank/.hcindex` at `main.c:392-401` because
  Instrument index generation disposes the one shared cache.

FAT display names and short aliases remain distinct. The relevant current
  scan/open boundary is documented in `ASYNCFATFS_REFERENCE.md:81-105` and
  implemented through LFN object iteration in `filesystem.c`. The new registry
  must persist only the display value; returned SFN aliases remain operation
  scratch.

### Resident-name audit

The source-derived current sizes are confirmed in
`knowledge_files/specification_reference/SRAM_DTCM_MANIFEST.md:180-210` and
`NAMES_SRAM_MANIFEST.md:7-25`:

- `scene.display_name`: 9 B per resident Scene, 144 B across 16 Scenes.
- `kit.display_name`: 9 B per resident Kit, 144 B across 16 Scenes.
- `kit.instrument_display_name[6][9]`: 864 B across 16 Scenes.
- `kit.instrument_stem[6][17]`: 1,632 B across 16 Scenes.
- `bank_display_name`: 9 B.
- `preset_currentName`: 8 B of active Save/Load editor state.

The uses are not interchangeable:

- Scene/Kit setters and accessors are in `SceneData.c:147-223` and declared
  in `SceneData.h:280-320`.
- Kit member source names are normalized by `SceneData.c:226-284` from
  `kitset.kcg file=` entries; `storageTypes.h:121-147` and
  `FILESYSTEM_SPEC.md:547-608` make that file the Kit membership authority.
- Instrument Load commits the staged source stem only after the payload is
  valid at `presetManager.c:1361-1371`.
- Instrument Save seeds the editable filename from the resident display field
  at `menu.c:3166-3197` and `menu.c:4848-4874`.
- Kit/Scene/Bank Save regenerates member filenames from the resident stem at
  `filesystem.c:6729-6747`, `13110-13140`, and `13143-13190`.
- Normal Kit Load copies the on-card Kit folder name into resident Kit state at
  `filesystem.c:3717-3756`; Scene Load captures the embedded Kit directory at
  `filesystem.c:4297-4307`; Scene Load captures the root Scene name at
  `filesystem.c:4984-5006`.
- Save editor seeding uses resident object identity at `menu.c:5780-5809`.
- `preset_currentName` is edited and serialized as active UI/legacy payload
  state at `presetManager.c:54`, `1696-1721`, and filesystem load/save paths;
  it is not equivalent to the persistent browser registry.

Conclusion: removing the KitBrowser bridge is mechanically safe after the
source cleanup below. Removing resident names is a second, conditional design
pass. It requires a compact provenance token or an operation-local resolver
that can recover the exact Kit member filename after a root Instrument Load,
Kit Load, Scene Load, or Bank-local Scene Load. A blind deletion of
`instrument_stem` would break later Kit Save filename generation.

## Proposed `.hcnames` contract to validate before implementation

The replacement should be a root file `/.hcnames`, written and read by
`filesystem.c` as a streamed fixed-record registry. `.hcindex` remains during
the transition as a directory-local repair/index artifact until the new
registry has passed recovery tests.

Proposed version 1 layout:

```text
Header: magic[8] = "HCNAMES1"
        version   = 1
        record_size = 12
        reserved/header validation fields
        instrument_count[4], one count for Drum/Snare/Cymbal/HiHat

Record: domain       u8  (KIT, SCENE, BANK, INSTRUMENT)
        subtype      u8  (instrument type for INSTRUMENT; zero for roots)
        ordinal       u16 little-endian
        display_name  8 printable bytes, space padded, never an SFN alias
```

Record rules:

1. Kit, root Scene, and root Bank each have 1,000 records with ordinal 0..999.
   A blank display record is a preserved empty slot; it is not omitted.
2. Each Instrument type has `0..1000` records with contiguous ordinal 0..N-1
   in the same sorted order used by its physical scan and `.hcindex`.
3. The domain and subtype are validated on every record. A reader rejects the
   complete file if the header, expected record count, ordinal sequence, file
   length, printable-name rule, or domain/type ordering is invalid.
4. A missing, truncated, malformed, duplicate, or stale registry is not a
   partially usable cache. The reader marks the registry invalid and queues a
   physical rebuild. A selected row is loadable only after its physical
   directory/file open succeeds; a failed open invalidates that registry view
   and schedules repair.
5. The registry stores display names only. LFN display components remain the
   user-facing identity; SFN aliases returned by asyncfatfs remain one-operation
   open keys and never become `.hcnames` values.
6. Reader state owns only the current record buffer, selected domain/type,
   ordinal, generation token, and one bounded callback context. It must not
   recreate `fs_list_cache_name[1000][9]`, a presence bitmap, or any per-domain
   table.

The fixed records permit bounded asynchronous seek/read of the selected row.
If implementation testing shows that the available seek behavior makes random
row access too expensive, the fallback is a streamed sequential reader with a
generation-tagged cancellation path; a large SRAM mirror is prohibited.

The physical scan remains authoritative for repair. The implementation must
either update fixed registry offsets as objects are discovered or use a bounded
two-pass/slot-probe state machine; it must not retain an unordered 1,000-name
array merely to sort it before writing `.hcnames`.

## Implementation plan with comment-ready change descriptions

Each item below identifies the concrete source boundary, the required change,
and text that can be adapted directly into adjacent `.c` and `.h` comments.

### 1. Remove the dead KitBrowser compatibility bridge

Files and symbols:

- Delete `Core/Hardware/SD/kitBrowser.c` and `Core/Hardware/SD/kitBrowser.h`
  after the final repository-wide symbol audit.
- Remove `Makefile:74`'s `kitBrowser.c` source entry.
- Remove `filesystem.c:74`'s `kitBrowser.h` include.
- Remove `filesystem.c:796-804`'s `kb_map`/`kb_numKits` extern declarations.
- Delete `filesystem_noteKitBrowserSlot()` at `filesystem.c:2745-2763`.
- Delete its calls at `filesystem.c:2811` and `2863`.
- Rewrite the Kit scan comments at `filesystem.c:2814-2821` and
  `10428-10447` to describe only the shared/next `.hcnames` registry and
  physical slot authority.
- Remove the `kb_numKits = 0` reset at `filesystem.c:13403-13406`.
- Update `filesystem.h:258-268` so Kit scan output no longer promises a
  compatibility view; preserve the public scan API for filesystem/Menu users.
- Update `filesystem.h:421-428` and the `filesystem.c:245-266` loaded-name
  comments so `presetManager.c`, not KitBrowser, is the legacy name-reader
  client.

Comment-ready rationale for `.c`:

```text
/* Kit directory discovery now publishes only filesystem-owned slot/display
 * identity. The former kb_map bridge duplicated Kit occupancy for an API with
 * no live caller; retaining it would keep a 1,000-entry SRAM allocation and
 * allow a second browser view to diverge from the direct slot or streamed
 * registry view. FAT aliases remain operation-local and are not recreated here. */
```

Comment-ready rationale for `.h`:

```text
/* Kit scans expose one filesystem-owned result contract. Menu and Preset use
 * the selected domain/slot accessors or streamed row requests; callers must not
 * expect a legacy kitBrowser map, add a parallel presence array, or retain an
 * SFN alias as display identity. */
```

Verification gate: `rg` must find no `kitBrowser`, `KITBROWSER`, `kb_map`,
`kb_numKits`, `kb_mapIndex`, or `kb_kitName` in live source/build inputs. The
only remaining `filesystem_requestLoadName()` users must be the audited legacy
Preset path.

### 2. Add the `.hcnames` format and bounded state machine contract

Primary files:

- `Core/Hardware/SD/filesystem.h`: add the domain/type/ordinal contract,
  registry status/generation types, streamed row request/completion accessors,
  and rebuild/repair requests. Keep this as the public facade; do not expose
  asyncfatfs handles or a cache pointer.
- `Core/Hardware/SD/filesystem.c`: add the magic/version/record constants,
  fixed-record encoder/validator, one-record read/write scratch, registry
  generation token, and new `FS_INTERNAL_OP_*` states near the existing
  operation enum at `filesystem.c:120-171`.
- Extend the existing `filesystem_tick()` dispatch at `filesystem.c:12621-12747`
  with foreground-pumped registry operations.

The parser must reject incomplete files as a whole, preserve blank root rows,
validate Instrument type isolation and sorted ordinals, and distinguish a
display name from an alias. A request captures domain/type/ordinal and a
generation token. A completion is ignored when any of those no longer match
the visible Menu selection.

Comment-ready rationale for `.c`:

```text
/* .hcnames is deliberately streamed: it replaces the resident 1,000-row name
 * array without creating another per-domain table. Every record carries its
 * domain, Instrument subtype when applicable, direct/sorted ordinal, and only
 * the printable display component. The parser accepts no partial registry,
 * because a truncated row must never turn a nonexistent slot into a loadable
 * product. */
```

Comment-ready rationale for `.h`:

```text
/* Registry requests identify one domain/type/ordinal and one generation. The
 * generation is part of the public async contract: encoder movement, type
 * changes, and cache disposal can invalidate a result before its SD read
 * completes, so clients must apply a name only when the captured identity still
 * matches the visible selection. */
```

### 3. Replace cache population with physical rebuild/publication

Primary code boundaries:

- Replace the cache-dependent scan/index sequence behind
  `filesystem_requestScanKits()`, `filesystem_requestScanScenes()`,
  `filesystem_requestScanBanks()`, and `filesystem_requestScanInstruments()`
  at `filesystem.c:13398-13438` with registry-aware scan/rebuild states.
- Preserve `.hcindex` generation and validation initially. It remains a
  directory-local repair artifact and transition diagnostic until `.hcnames`
  has equivalent recovery behavior.
- Replace or adapt `filesystem_createLibraryIndex_tick()` at
  `filesystem.c:2295-2419` and the Instrument index writer so the physical
  scan can publish `.hcnames` without a resident cache. The implementation must
  use direct fixed-record writes or bounded slot probes, not a replacement
  9,000-byte scratch mirror.
- Update `main.c:303-373` to build/validate `.hcnames` during the existing
  pre-audio boot window. Preserve the explicit Bank registry reload/selection
  ordering at `main.c:392-401`, or document the new equivalent generation
  handoff if a full registry makes that reload unnecessary.
- Keep `filesystem_tick()` as the sole runtime pump. No directory walk or
  registry rebuild may block the main loop after audio starts.

Comment-ready rationale for `.c`:

```text
/* Physical directory state is the repair authority. This foreground state
 * machine scans the real LFN objects, publishes complete registry records, and
 * keeps the legacy .hcindex artifact available during migration. The save/boot
 * path must never infer occupancy from a stale registry or hold an unordered
 * 1,000-entry SRAM table just to regain slot order. */
```

Comment-ready rationale for `.h`:

```text
/* Rebuild requests are asynchronous and serialized with every other SD
 * operation. Completion means the physical scan, registry write, close, and
 * required flush boundary have finished; callers may not release a Save
 * callback or advertise a newly saved slot before that publication point. */
```

### 4. Migrate Menu browsing to streamed current-row lookup

Primary code boundaries:

- Replace direct `filesystem_kitSlotName()`, `filesystem_sceneSlotName()`,
  `filesystem_bankSlotName()`, and `filesystem_instrumentName()` cache reads
  at `filesystem.c:14019-14267` with current-row state backed by `.hcnames`.
- Update `menu_requestCurrentLoadSaveSelection()` at `menu.c:2712-2811` to
  request a row for every visible domain/slot/type change, including blank root
  slots. Keep the existing explicit-OK behavior for Scene/Bank and Kit/KitMrp
  semantics.
- Update `menu_libraryIndexLoadComplete()` and
  `menu_requestLibraryIndexLoad()` at `menu.c:2846-2888` to wait for a valid
  registry generation/row rather than declaring a 1,000-row cache ready.
- Update `menu_repaintLoadSavePage()` at `menu.c:4688-4995` to paint only the
  current bounded display string and selected identity.
- Preserve entry/type-change/exit invalidation at `menu.c:3907-3921`,
  `3637`, `5670`, `6243`, `6532`, `6575`, `6733`, and `6800-6803`, but change
  it from clearing a large name array to invalidating the current lookup token.
- On fast encoder movement, cancel/ignore old results by generation plus
  domain/type/ordinal comparison; a completed read for another row must never
  repaint the current LCD.

Comment-ready rationale for `.c`:

```text
/* Menu owns only the currently visible name and selection identity. The SD
 * row read is asynchronous and generation-tagged so rapid encoder movement
 * cannot paint a result from another slot, library, or Instrument type. Empty
 * numbered rows remain valid display state but are not loadable until physical
 * existence validation succeeds. */
```

Comment-ready rationale for `.h`:

```text
/* This accessor returns a bounded current-row result, not a persistent name
 * array. The caller must retain the requested domain/type/ordinal and generation
 * only for the active operation; it must not copy the API into a per-slot cache
 * or treat a display string as an asyncfatfs open alias. */
```

### 5. Migrate payload Load/Save and Save refresh ordering

The current payload code reads the shared cache directly in several places:

- Kit Load name validation/copy around `filesystem.c:3486-3567` and
  `3717-3756`.
- Scene Load display capture around `filesystem.c:3923-3929` and
  `4984-5006`.
- Bank Load display capture around `filesystem.c:5135`, `5510`, `5657`, and
  `5362-5465`.
- Instrument Load display/stem staging at `filesystem.c:5905-5982`.
- Kit/Scene/Bank Save request-time filename generation at
  `filesystem.c:6729-6747`, `13110-13190`, and Bank setup around
  `13319-13394`.

Change each path to consume an operation-local validated row returned by the
  registry reader, then use the physical LFN/SFN open contract appropriate to
  the next step. Keep `op_*display_name`, `op_*open_name`, and staged source
  names bounded to the current transaction. Do not let them become a hidden
  1,000-row cache.

The Save refresh chain at `filesystem.c:2421-2455` must become:

```text
physical write/close/flush
  -> physical parent rescan
  -> complete .hcnames domain update or rebuild
  -> .hcindex compatibility refresh while retained
  -> current-row lookup refresh
  -> original callback release
```

Comment-ready rationale for `.c`:

```text
/* A registry display row is only a name hint. The selected directory/file must
 * still be opened through its correct LFN or operation-local SFN identity before
 * Load accepts it. Save publishes the physical directory first, then rebuilds
 * the registry and refreshes the current row before releasing the caller's
 * callback, so the UI cannot expose stale post-save identity. */
```

Comment-ready rationale for `.h`:

```text
/* Load/Save completion includes registry publication. Callers may use the
 * current-row display after completion, but must not assume that a registry
 * record alone proves a physical object exists or that its display text is a
 * short FAT open key. */
```

### 6. Retire resident names only after provenance is solved

This is a gated follow-up within Session 042, not a blind first edit.

Audit/change boundaries:

- `SceneData.h:105-129` and `197-225`: decide which resident fields remain
  object state and which become compact source identities.
- `SceneData.c:147-284` and `655-674`: replace initialization/setters only
  after all clients have a bounded source.
- `menu.c:3166-3197`, `4848-4874`, and `5780-5809`: replace Save-editor
  seeding with current-row/operation lookup or a compact provenance resolver.
- `filesystem.c:3717-3756`, `4297-4307`, `4984-5006`, and `8002-8004`:
  preserve normal-load identity updates while removing duplicate storage only
  when the loaded source can be recovered.
- `filesystem.c:6729-6747`, `13131-13139`, and `13182-13189`: prove Kit Save
  can regenerate all six member filenames after a prior Kit Load and after a
  root Instrument Load without `instrument_stem[6][17]`.
- `presetManager.c:1361-1371`: retain staged source identity through the
  Instrument commit boundary, then publish the compact provenance token if the
  design uses one.
- `BankData.c:4-116` and `BankData.h:7-23`: decide whether Bank identity can be
  resolved from root/child `.hcnames` records or must remain one resident field.

Required design gate: either add a compact source token per resident voice
that resolves through `.hcnames`/the physical Kit manifest, or retain the
field. A token must distinguish root Instrument type/file identity from Kit,
root Scene, and Bank-local Scene provenance and must define stale-source
fallback. `kitset.kcg` remains the membership authority; do not add a new
opaque self-name field to `sceneset.scg` or `kitset.kcg` merely to make the
RAM deletion convenient.

Comment-ready rationale for `.c`:

```text
/* Resident source metadata is removed only after its owning transaction can
 * recover the same display/member filename from a bounded provenance identity.
 * The name is not DSP state, but Kit Save depends on the member filename; a
 * deletion that loses that identity silently changes future saved Kit trees. */
```

Comment-ready rationale for `.h`:

```text
/* This field/token is provenance, not a browser cache. Its contract must state
 * how a loaded root Instrument, Kit, root Scene, Bank-local Scene, or default
 * source is resolved during a later Save, including the stale/missing-source
 * fallback. Until that source is bounded and asynchronous, the resident field
 * must remain allocated. */
```

`preset_currentName` should remain as the eight-byte active Save editor until
all legacy payload paths and editor transitions are separately migrated; it is
not part of the 9,000-byte generalized browser-register removal.

### 7. Documentation and source-of-truth closeout

After implementation and measurements, update:

- `FILESYSTEM_SPEC.md`: final `.hcnames` byte format, domain/type/ordinal
  rules, `.hcindex` compatibility status, repair behavior, and LFN/SFN rules.
- `MODULE_INTERCHANGE_SPEC.md`: remove `kitBrowser` as an affiliate, update
  scan/index/row APIs, and document callback/generation ownership.
- `ASYNCFATFS_REFERENCE.md`: record bounded seek/read/write usage and the
  display-vs-alias rule for the registry.
- `MEMORY.md`, `NAMES_SRAM_MANIFEST.md`, and
  `SRAM_DTCM_MANIFEST.md`: exact linked symbols/bytes removed and resident-name
  fields actually retired versus retained.
- `SCOPING_TARGETS.md` and `README.md`: remove stale KitBrowser architecture
  references and keep Phase 3.7/Pattern non-goals explicit.
- `knowledge_files/specification_reference/MEMORY_AUDIT.md` only if that file
  is restored/created by an explicit documentation decision; do not invent a
  review result for the currently missing path.

## Acceptance tests

The implementation is not complete until all of the following pass:

- `rg` shows no live KitBrowser bridge symbols or build entry.
- `.hcnames` missing, empty, truncated, malformed, duplicate, and stale cases
  trigger physical rebuild; no partial record makes a nonexistent slot appear
  loadable.
- Root Kit, Scene, and Bank slots 000, 001, 255, 256, 998, and 999 preserve
  gaps and blank rows.
- Drum, Snare, Cymbal, and HiHat remain isolated; sorted Instrument rows and
  1,000-entry stress cases remain correct.
- Rapid type changes, encoder movement, menu exit/re-entry, and Save refreshes
  never paint a stale domain/slot/type name.
- New and renamed Kit, Scene, and Bank saves become visible only after physical
  write/flush, registry publication, and current-row refresh.
- LFN display names and generated SFN aliases remain distinct on host-created
  and firmware-created files.
- Kit Save after Kit Load and after root Instrument Load regenerates the same
  six member filename identities, or the implementation reports the defined
  bounded fallback rather than silently renaming them.
- Boot ordering works with missing indexes/registry and empty Bank fallback.
- `make -j2`, `make img`, `git diff --check`, `arm-none-eabi-size -A`, and
  `arm-none-eabi-nm -S --size-sort` record the expected SRAM reduction.
- Hardware tests cover all Load/Save browsers, rapid lookup movement, Save
  completion visibility, reboot/rebuild behavior, and audio stability.

## Running log

### 2026-07-20 — audit and plan started

- Read `SESSION_042_PRE_PLAN.md` before other project files.
- Confirmed the KitBrowser API has no live callers; only its build entry and
  filesystem compatibility writes remain.
- Confirmed `filesystem_requestLoadName()` remains needed by the legacy Preset
  name-load path and must not be deleted as part of bridge removal.
- Confirmed current linked ELF bridge allocation is 2,004 B, while source
  declarations account for the documented 2,013 B bridge footprint.
- Confirmed `.hcindex` is currently the only persistent name-index format and
  that the root `.hcnames` design is not implemented.
- Confirmed resident Instrument source stems are required by current Kit Save
  filename generation; removal is gated on compact provenance/lookup design.
- Created this running log. No production code changed.

### Next entry

Record the agreed `.hcnames` byte contract, then begin the bridge-only source
cleanup and add compile-time/source audits before changing browser storage.

### 2026-07-22 — Scene-name retirement and Bank-name-register audit

- The requested next step is to retire `scene_t.display_name[9]` from all 16
  resident Scene records and make root `/.hcnames` the authority for Scene
  display identity. The current allocation is 144 bytes; the intended Menu
  replacement is one 9-byte, operation-scoped Scene name scratch rather than a
  per-Scene mirror.
- The current Scene field has three distinct consumers: root Scene Load copies
  the selected `NNN Name` directory text into every destination Scene;
  Save:[Scene] seeds its editor from that retained value; and Bank Save uses it
  to create each Bank-local `SS Name` child directory. The first two can use a
  single root-HCNAMES row read. The third must use the shared 1,000-row cache
  for the complete Bank operation, as requested, because a Bank can serialize
  up to all 16 Scene names in one async transaction.
- Existing Bank Load is mask-selective. It scans all immediate `00..15` child
  directories but only parses/commits children selected by the caller’s Scene
  mask (falling back to all present children only when the requested mask is
  empty). The requested 129-name Bank register contains all 16 Scene/Kit/six
  Instrument rows. Publishing all 129 names after a partial Bank Load would
  make root HCNAMES describe children that remain unmodified in resident SRAM;
  preserving unselected names instead would require reading old HCNAMES, which
  conflicts with the requested “from the to-be-loaded Bank slot” input rule.
- No production code was changed for this follow-up before resolving that
  semantic conflict. Required decision: Bank Load either (a) always commits all
  present Bank children when it constructs the complete 129-name register, or
  (b) preserves current mask-selective payload behavior and publishes only the
  selected rows while retaining the prior rows for unselected Scenes.

### 2026-07-22 — Scene HCNAMES authority and selective Bank overlay implemented

- The semantic choice was resolved as mask-selective preservation: Bank Load
  retains all unselected resident Scene payload and HCNAMES rows. After Bank
  name repair, the operation borrows the existing 1,000-row cache for the
  complete root register, reads the prior file, and overlays only a selected
  child after its staged Scene payload has committed. Each overlay changes the
  one Scene row, one Kit row, and six Instrument rows. The Bank row changes at
  final metadata commit; one register write is followed by the normal Bank
  rescan/index rewrite so the general cache is restored before Preset sees
  completion.
- Bank Save now borrows the same register once before writing its temporary
  tree. Bank-local Scene and embedded Kit directory display names come from
  that cache; Instrument filenames continue to require the existing 16-byte
  source stems because HCNAMES deliberately retains only eight display cells.
  Promotion updates the Bank row, writes the register once, then restores the
  `/Bank/.hcindex` cache through the existing save refresh chain.
- `scene_t.display_name[9]`, its setter, and its accessor were removed from
  SceneData. The 16-record mirror released 144 bytes. Top-level Scene
  Load/Save has one Menu-owned nine-byte scratch that copies only the selected
  HCNAMES Scene row before the root Scene index replaces the shared cache; a
  source-Scene change reuses that same single scratch. Root Scene Load/Save
  now performs its own targeted HCNAMES update and index restoration before
  callback, so no per-Scene name cache exists in Menu or SceneData.
- The former normal-boot resident-SRAM HCNAMES snapshot was removed. It could
  no longer reproduce Scene names after their SRAM mirror was retired and
  would blank mask-unselected rows after boot. Targeted Scene/Bank updates
  bootstrap a missing register from blank rows plus their successfully changed
  rows instead. This is necessary to keep HCNAMES authoritative rather than
  recreating an SRAM mirror.
- `make -j1` passes. The linked image reports `.data` 408 and `.bss` 281,448;
  existing unrelated unused delete-tree warnings and standard newlib syscall
  linker warnings remain. Diff whitespace checking is clean for the changed
  source/documentation files; pre-existing trailing whitespace remains in
  `SD_CARD/Kit/.hcindex` and was not modified.
- Follow-up mask audit found one pre-existing contradiction: after intersecting
  the request mask with discovered Bank children, an empty intersection fell
  back to all present children. That fallback is removed. An absent requested
  child now loads no other child, leaves all HCNAMES Scene blocks untouched,
  and preserves the prior resident Scene-present mask. A non-empty selective
  load merges only its successfully loaded child bits into that prior mask, so
  unmasked resident Scene data, names, and availability remain in place.
