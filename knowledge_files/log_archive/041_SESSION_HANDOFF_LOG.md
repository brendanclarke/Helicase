# Session 041 Handoff — Generalized `.hcindex` Name Cache, Bank Boot Repair, And Documentation Closeout

DATE: 2026-07-19

SESSION GOAL: Extend the `.hcindex`/generalized-name-cache architecture to
Bank, repair Bank boot/index generation and initial loading, remove redundant
per-library SRAM name arrays, verify the complete 1,000-entry shared cache
model, and close the session documentation with authoritative filesystem and
memory records.

COMPLETED:

- Bank now uses the same root-library `.hcindex` flow as Kit and root Scene.
  `/Bank/.hcindex` is generated from the physical `Bank/` directory and is
  loaded into the shared cache before root Bank browsing or initial Bank
  selection. Bank-local Scene children remain inside the selected Bank and do
  not enter the root Bank index.
- The implementation has exactly one SRAM display-name cache:
  `fs_list_cache_name[1000][9]`, 9,000 bytes. It is reused by one active
  Instrument type, root Kit, root Scene, or root Bank. It is never multiplied
  by Instrument type or library type.
- Instrument indexes are directory-local and registry-owned:
  `Instrument/Drum/.hcindex`, `Snare/.hcindex`, `Cymbal/.hcindex`, and
  `HiHat/.hcindex`. Registry metadata supplies each type's storage directory,
  so the same folder metadata drives scan, index generation, index loading,
  Instrument Load, and Instrument Save.
- Instrument rows are alphabetically sorted display names and may use all
  1,000 cache rows. The former 128-file-per-type limit is gone.
- Kit, root Scene, and root Bank indexes are slot ordered from 000 through
  999. Each row contains only the display name; blank rows are preserved and
  the row number supplies the `NNN ` component when a full folder key is
  reconstructed. A non-blank row is the slot-existence record.
- Load/Save menu lifecycle is cache-safe: changing a type or leaving a
  Load/Save browser disposes the shared cache; entering a type loads only its
  selected `.hcindex`. The active cache is tagged with its current domain/type
  so a stale cache cannot be used for a different browser.
- Kit, root Scene, and root Bank Save now wait for the physical parent
  directory write and final FAT flush, rescan that parent, and rewrite the
  complete slot-ordered `.hcindex` before the original Save completion callback
  is released. Menu then refreshes the current Save slot's displayed name from
  the refreshed shared cache, so a newly created or renamed directory is
  visible without rebooting.
- Boot generates the root Kit, Scene, and Bank indexes, then generates the four
  Instrument indexes one type at a time. Each Instrument generation disposes
  the shared cache; boot therefore reloads `/Bank/.hcindex` before initial Bank
  selection. If Bank is unavailable, the fallback path can reload root Scene or
  Kit index data as appropriate.
- The old dedicated Kit, root Scene, and root Bank arrays are removed. Each
  formerly contained a 1,000-byte presence array, a 9,000-byte display-name
  array, and a 13,000-byte alias array: 23,000 bytes per library and 69,000
  bytes combined. No linked symbols for those arrays remain.
- The legacy `kitBrowser` compatibility bridge remains temporarily for older
  clients. Its `kb_map[1000]` consumes 2,000 bytes; `kb_numKits` and
  `kb_mapIndex` consume 4 bytes; `kb_kitName[9]` consumes 9 bytes. The bridge
  totals 2,013 bytes and is not a second names cache. Its removal is the next
  cleanup task.
- FAT short aliases and longer Instrument source stems are not retained as
  per-slot browser caches. They remain only in operation-local state or staged
  resident metadata when a load/save transaction needs them.
- The restored pre-session filesystem specification was used to recreate the
  zero-byte `FILESYSTEM_SPEC.md`. It now contains the full product filesystem,
  instrument/kit text-storage contract, Scene/Bank ownership, menu reachability,
  async save/load behavior, draft Pattern persistence, and the current Session
  041 cache/index lifecycle. It explicitly supersedes the old opaque root
  `.hcindex` marker description.
- `MEMORY_AUDIT.md` now separates the historical Session 023 measurements from
  the current linked-image measurement. `MODULE_INTERCHANGE_SPEC.md` and
  `ASYNCFATFS_REFERENCE.md` describe the shared display-only cache, operation-
  local alias rule, Bank save refresh, and Bank boot reload.
- `SCOPING_TARGETS.md` now records the implemented 16-Scene Bank structure and
  Session 041 Phase 3.5/3.6 completion without advancing into Phase 3.7
  autosave. `MEMORY.md` records the one-cache invariant, current boot sequence,
  retired allocations, and the Session 042 cleanup boundary.

VERIFIED ON HARDWARE: Yes for the Bank boot/index repair: the user confirmed
that the corrected Bank path works after the missing initial `.hcindex` issue
was fixed. The user also confirmed the post-save index refresh/display behavior
for the Kit/Scene/Bank work in the preceding implementation sequence. The
current source was additionally build-verified locally; no new hardware soak
was performed by the agent during this documentation closeout.

CHANGES THIS SESSION:

- `Core/Hardware/SD/filesystem.c`: generalized the shared cache boundary to
  Instrument, Kit, Scene, and Bank; removed the per-slot Kit/Scene/Bank arrays;
  added Bank index generation/loading and cache-tag validation; preserved blank
  slot rows; added the boot-equivalent post-save parent rescan/index rewrite
  chain; and kept aliases/stems operation-local. Detailed comments explain
  cache ownership, slot identity, index lifecycle, callback deferral, and why
  the Bank cache must be reloaded after Instrument generation.
- `Core/Hardware/SD/filesystem.h`: documented the public cache/index APIs,
  Bank index request, cache disposal/tag contract, and save-refresh callback
  lifetime. The comments explain why callers must not create parallel name
  arrays or use an alias as an LFN display identity.
- `main.c`: boot scans and writes the root Kit/Scene/Bank indexes and the
  registry-owned Instrument indexes, then reloads `/Bank/.hcindex` before the
  initial Bank request because the single cache is reused and disposed during
  Instrument index generation. The boot-only synchronous pumping boundary is
  documented.
- `Core/Menu/menu.c`: Bank was added to the shared Load/Save index lifecycle;
  type entry/change/exit invalidation and post-save current-slot display
  refresh are documented beside the relevant state transitions.
- `Core/Bank/Scene/Preset/presetManager.c` and `.h`: the root-library slot
  range and Bank-facing load/save wrappers use the direct 000..999 model and
  the shared filesystem cache contract; public comments describe staged-load
  ownership and completion ordering.
- `NAMES_SRAM_MANIFEST.md`: reconciled resident object names, the single
  9,000-byte cache, 4-byte cache tags, the 2,013-byte KitBrowser bridge, the
  69,000 bytes of retired arrays, and the 2,124 bytes of named operation
  scratch. It records that the bridge is the only remaining name-cache cleanup.
- `DISPOSE_INST_NAME_CACHE.md`: marked the former Instrument-array proposal
  complete and recorded the final 1,000-row shared-cache implementation,
  cache disposal/reload lifecycle, slot-ordered Kit/Scene behavior, and the
  replacement of persistent aliases with operation-local scratch.
- `DRUM_HCINDEX_CACHE.md`: preserved the Drum failure investigation, the cache
  desynchronization fix, registry-owned Instrument directories, per-type index
  generation/load, full all-type generalization, and final legacy-array removal.
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: reconstructed
  from `FILESYSTEM_SPEC_RESTORE.md`, then updated through Session 041 with the
  directory-local index format, one-cache SRAM rule, Bank boot sequence, save
  refresh ordering, and current load/save status.
- `knowledge_files/specification_reference/MEMORY_AUDIT.md`: added current
  linked-image sizes and cache-retirement measurements while retaining the old
  Session 023 table as historical; corrected its dating language.
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`:
  updated API ownership and private-operation notes through Session 041.
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`:
  clarified that the cache stores display components only and aliases are
  operation-local.
- `SCOPING_TARGETS.md`, `MEMORY.md`, and this session archive were updated to
  preserve the durable architecture and next-session boundary.

SRAM ACCOUNTING:

- Shared `fs_list_cache_name[1000][9]`: 9,000 bytes, one physical cache.
- Shared cache tag/count fields: 4 bytes (`kind`, `type`, `count`).
- Resident object-name metadata: 2,801 bytes total: 144 B of Kit Scene
  display names, 864 B of Kit instrument display names, 1,632 B of Kit
  instrument stems, 144 B of resident Scene display names, 9 B of Bank name,
  and 8 B of the Save editor field.
- Legacy KitBrowser bridge: 2,013 bytes total (`kb_map` 2,000 B,
  counters 4 B, `kb_kitName` 9 B).
- Named operation scratch: 2,124 bytes total, including generic/Instrument
  scratch (106 B), Kit/Scene scratch (501 B), Bank scratch (552 B), delete
  walker scratch (558 B), parser/index buffers (400 B), and index-control
  fields (7 B). This is transient operation state, not a second cache.
- Retired dedicated Kit/Scene/Bank arrays: 69,000 bytes combined. The former
  Instrument arrays from the historical 128-row design are also removed; the
  final active Instrument browser uses the 9,000-byte shared cache.

BUILD VERIFICATION:

- `make -j2` completed successfully.
- `make img` completed successfully and produced `build/LXRV2_lxr02.img`;
  the latest reported image size was 362,768 bytes.
- `git diff --check` completed with no whitespace errors.
- `arm-none-eabi-size -A build/lxr02.elf` reported:
  `.isr_vector` 456 B, `.text` 322,968 B, `.itcm` 3,768 B,
  `.dma_nocache` 3,100 B, `.data` 408 B, `.bss` 272,932 B, `.dtcm` 35,168 B,
  and `.dtcmz` 6,716 B; total 647,083 B.
- Existing warnings remain the known asyncfatfs/unused-function warnings,
  nano syscall stubs, and the LTO serial-compilation notice. No new link or
  memory-overflow failure was observed.

KNOWN ISSUES INTRODUCED: No new functional issue is confirmed. Technical debt
remaining by design is the 2,013-byte legacy `kitBrowser` compatibility bridge,
which still maps Kit slots for older clients. The root `.hcnames` design and
bridge removal are not implemented yet. Atomic rename/replace and durable
crash-recoverable autosave promotion remain future AsyncFATFS work; Pattern
finalization and real Effect persistence also remain future work.

KNOWN ISSUES RESOLVED: Fixed the missing Bank `.hcindex` generation at boot and
the resulting initial Bank load failure; ensured Bank index reload occurs after
Instrument index generation; unified all browser domains onto one cache; removed
the old Instrument 128-entry cap; removed the dedicated Kit/Scene/Bank arrays;
made Kit/Scene/Bank Save indexes refresh before callbacks; and refreshed the
current Save display after that refresh so it cannot remain stale.

NEXT SESSION RECOMMENDED GOAL: Retire the legacy KitBrowser compatibility cache
and design/implement the root `.hcnames` file so the SRAM display-name register
is no longer resident. Begin with the pre-plan in `SESSION_042_PRE_PLAN.md`.

BLOCKERS: The `.hcnames` on-card format and whether `.hcindex` remains as a
compatibility/repair artifact must be settled before deleting the shared cache.
The replacement must preserve 000..999 gaps, four Instrument type domains,
case/LFN display identity, asynchronous single-operation behavior, and
post-save visibility. Hardware testing is required after implementation.

CRITICAL REMINDERS FOR NEXT SESSION:

- There is exactly one active name cache today; never add a per-type,
  per-library, or per-slot name/alias/presence array.
- `fs_list_cache_name[1000][9]` is a display-name cache only. A FAT short alias
  may exist in one operation's scratch, but it is not an LFN display key and
  must not be retained per slot.
- Kit/Scene/Bank rows are direct slot rows with preserved blanks. Instrument
  rows are sorted and may use all 1,000 entries.
- Dispose the shared cache on Load/Save type change and exit; reload the
  selected `.hcindex` on entry. Boot index generation is pre-audio and may be
  pumped synchronously, but runtime filesystem operations remain asynchronous.
- After a Kit, root Scene, or root Bank Save, do not release the original
  callback until the physical parent rescan, complete `.hcindex` rewrite, and
  cache/display refresh have finished.
- Bank-local Scene folders are `00..15`; root Bank folders are `000..999`.
  Reset per-child Scene discovery scratch before every delegated Bank child.
- Do not delete or revert unrelated dirty SD-card fixtures, generated images,
  or user changes while doing the next cleanup.

SOURCE RECORDS FOLDED INTO THIS HANDOFF:

`DISPOSE_INST_NAME_CACHE.md`, `DRUM_HCINDEX_CACHE.md`, and
`NAMES_SRAM_MANIFEST.md` contain the detailed design history, failure paths,
cache-size calculations, lifecycle rules, and verification checklist that were
consolidated here because those temporary root notes may be deleted later.
`FILESYSTEM_SPEC_RESTORE.md` is the preserved pre-session source used to
recover the complete product specification; it may likewise be retained as a
recovery reference until the restored specification is accepted.
