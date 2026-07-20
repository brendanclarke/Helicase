# Session 042 Pre-Plan — Retire KitBrowser Cache And De-Cache the SRAM Names Register

## Goal

Remove the remaining legacy `kitBrowser` compatibility cache and replace the
resident 1,000-row SRAM name register with a root `.hcnames` representation.
The result should preserve the current user-visible Load/Save behavior while
removing the final generalized name cache from SRAM.

This remains within Phases 3.5 and 3.6: it is filesystem/menu architecture
cleanup for the implemented Scene/Bank/Kit/Instrument libraries, not Phase 3.7
autosave work and not the future dynamic Pattern implementation.

## Current baseline

- Current browser cache: `fs_list_cache_name[1000][9]`, 9,000 bytes, exactly one
  physical cache shared by Instrument, Kit, root Scene, and root Bank.
- Cache tags/count: 4 bytes. The cache is disposed on Load/Save type changes
  and exit, then loaded from the selected directory-local `.hcindex` on entry.
- Instrument indexes: `Instrument/Drum/.hcindex`, `Snare/.hcindex`,
  `Cymbal/.hcindex`, and `HiHat/.hcindex`; sorted display rows, up to 1,000.
- Root library indexes: `/Kit/.hcindex`, `/Scene/.hcindex`, and `/Bank/.hcindex`;
  direct slot rows 000..999, blank rows preserved, row number supplies the
  `NNN ` folder prefix.
- Remaining legacy bridge: `kb_map[1000]` (2,000 B), two 16-bit controls
  (4 B), and `kb_kitName[9]` (9 B), 2,013 B total. It exists only because
  older `kitBrowser.c/h` clients still consume that API.
- Resident object names and one-operation scratch remain valid and are not
  targets for deletion merely because the browser cache is removed.

## Required investigation before implementation

1. Audit every `kitBrowser_*`, `kb_map`, `kb_numKits`, `kb_mapIndex`, and
   `kb_kitName` reference with `rg`. Classify each client as removable,
   directly migratable to filesystem slot lookup, or requiring a small
   operation-local replacement. Do not delete the bridge until all callers are
   migrated and the normal Kit/Scene/Bank paths are proven independent of it.
2. Define the `.hcnames` format from the existing `.hcindex` semantics. The
   format must identify the domain and slot/type explicitly, preserve blank
   root-library rows, preserve Instrument sorted order, and keep display names
   distinct from FAT short aliases. Do not silently assume that a single flat
   list can represent all four Instrument types and three root libraries.
3. Decide whether `.hcindex` remains as a directory-local repair/index artifact
   after `.hcnames` is introduced. The safe transition assumption is to keep
   `.hcindex` generation and validation until the new root registry is proven,
   unless the implementation establishes an equally reliable replacement.
4. Define behavior for missing, truncated, malformed, duplicate, or stale
   `.hcnames`. A physical directory scan must be able to rebuild the registry;
   a partial file must never make a nonexistent slot appear loadable.

## Proposed architecture to validate

- Store a root `/.hcnames` file as a compact, streamed registry rather than an
  SRAM array. Each record must carry enough identity to distinguish:
  Instrument type plus sorted row/object identity, or Kit/Scene/Bank plus
  direct slot. The display component is the stored user-facing value.
- Use bounded asynchronous file reads/seeks and a small line/record buffer to
  fetch the currently displayed row. Do not replace the 9,000-byte cache with
  another large static table. Menu may retain only the current display string,
  selected slot/type, generation token, and any alias needed by the active
  transaction.
- Preserve one-operation filesystem serialization. A menu scroll, type change,
  Save refresh, or index rebuild must be represented as a foreground-pumped
  state machine; no blocking runtime directory/file walk may be introduced.
- Keep directory scans as the authority for repairing `.hcnames`. On boot and
  after Save, the physical parent must be scanned and the registry rewritten
  or updated with explicit flush/commit ordering. If atomic replacement is not
  available, document the bounded failure mode rather than claiming
  crash-safe publication.
- Preserve display-name/LFN identity. FAT short aliases remain operation-local
  open aids and must never become the persistent `.hcnames` display value.

## Migration sequence

1. Audit and document all KitBrowser callers and current name-cache accessors.
2. Add the `.hcnames` parser/reader/writer contract and bounded scratch state
   in `filesystem.h/c`, with detailed comments explaining why the registry is
   streamed and how domain/slot identity is represented.
3. Add asynchronous current-row lookup and generation tagging in Menu. Verify
   that fast encoder movement discards stale results and never paints a name
   from another domain or slot.
4. Migrate Kit, root Scene, root Bank, and all four Instrument browser paths
   from the SRAM cache to the streamed registry. Preserve entry/type-change/
   exit disposal semantics during the transition; after migration there should
   be no cache allocation to dispose.
5. Migrate Save completion: physical directory write/flush, registry rebuild or
   update, then current-slot display refresh before callback release.
6. Remove `kb_map`, its counters, `kb_kitName`, and dead bridge APIs only after
   `rg` proves no clients remain. Update both `.c` and `.h` comments at every
   ownership boundary to state what was removed and why the streamed registry
   is the replacement.
7. Test boot ordering, missing indexes, malformed registry recovery, and all
   Load/Save menu transitions before claiming the SRAM register is retired.

## Test matrix

- Root Kit, Scene, and Bank slots: 000, 001, 255, 256, 998, and 999.
- Gaps before, between, and after occupied slots; empty directories and names
  containing spaces/case variations allowed by the storage sanitizer.
- Instrument Drum, Snare, Cymbal, and HiHat with empty, one-entry, 128-entry,
  and 1,000-entry stress cases; confirm sorted order and type isolation.
- Enter/exit every Load and Save type, change types rapidly, spin the encoder
  during an asynchronous lookup, and confirm stale names never appear.
- Save a new and renamed Kit, Scene, and Bank; confirm the directory, registry,
  and current display are correct immediately without reboot.
- Reboot after saves and confirm boot rebuild/reload behavior, especially Bank
  initial selection after all Instrument domains are processed.
- Remove or corrupt `.hcindex`/`.hcnames` and confirm physical rescan recovery.
- Exercise host-created long names and generated FAT short aliases; confirm
  LFN display names and SFN aliases are never confused.
- Re-run `make -j2`, `make img`, `git diff --check`, `arm-none-eabi-size -A`,
  and `arm-none-eabi-nm -S --size-sort`; verify the expected SRAM reduction.
- Hardware test all browser paths and Save completion behavior before deleting
  temporary Session 041 root notes.

## Documentation closeout

Update `FILESYSTEM_SPEC.md`, `MODULE_INTERCHANGE_SPEC.md`,
`ASYNCFATFS_REFERENCE.md`, `MEMORY_AUDIT.md`, `NAMES_SRAM_MANIFEST.md` (if it
still exists), `SCOPING_TARGETS.md`, `MEMORY.md`, and the Session 042 handoff.
Record the final `.hcnames` byte format, failure/rebuild rules, exact SRAM
symbols removed, and any compatibility decision about retaining or deleting
`.hcindex`.

## Explicit non-goals

- Do not implement Phase 3.7 debounced autosave or claim crash-recoverable
  rename/replace without the required AsyncFATFS primitives.
- Do not implement final dynamic Pattern storage, step automation, or real FX
  persistence as part of this cache migration.
- Do not add a new permanent per-domain cache, alias table, occupancy bitmap,
  or large root registry mirror in SRAM.
