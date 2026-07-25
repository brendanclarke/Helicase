# Session 042 Handoff Log

**Project**: LXR-02 firmware port (STM32F765VIH6)  
**Session goal**: Retire duplicate resident name/key caches, make `/.hcnames`
authoritative, separate browser caching from payload staging, preserve
mask-selective Bank operations, and stabilize Kit/Scene/Instrument Load/Save.  
**Last session summary**: Session 041 established the single 1,000-row
`.hcindex` cache and prepared the name-cache disposal work.  
**Working repository**: `/Users/bc/Helicase Project/Helicase-check-fs/Helicase`,
branch `dev-ph3-fsfix`; worktree contains the Session 042 implementation and
documentation changes.  
**Constraints today**: preserve the exact name/cache contract; do not add
speculative storage or restore rejected staging designs.

## End of session

```
DATE: 2026-07-25
SESSION GOAL: Make HCNAMES authoritative, dispose duplicate name/key caches,
separate index caching from non-Pattern payload staging, preserve selective
Bank Load behavior, and correct Instrument/Kit/Scene menu transactions.
COMPLETED: Implemented the fixed 129-row /.hcnames register; reduced resident
musical identity to one Bank, one Scene, one Kit, and six Instrument names;
kept one 9,000-byte shared index/HCNAMES cache; separated it from a 2,048-byte
aligned non-Pattern stage; removed Bank child arrays, File/Dir list caches,
recursive delete stacks, and KitBrowser storage; expanded asyncfatfs to five
handles; corrected Bank handle lifetimes; implemented the hidden .hctmp
Instrument Load kit source; corrected index-first scrolling and rapid-backspin
selection; and reconciled the permanent specifications. Instrument Load is now
working correctly on hardware.
VERIFIED ON HARDWARE: yes for boot completion after the Bank-root/Kit-handle
lifetime correction and for the current Instrument Load behavior. The final
rapid-backspin correction was build-verified; the user subsequently confirmed
Instrument Load works properly.

CHANGES THIS SESSION:
- Core/Bank/Scene/SceneData.*: removed resident Scene/Kit/Instrument display
  names and retained filename stems.
- Core/Hardware/SD/filesystem.*: added HCNAMES transactions and identity APIs;
  separated the 9,000-byte cache from the 2,048-byte stage; changed Scene
  staging/Pattern ordering; added Bank masking and one-child rescans; added
  canonical repair and typed .hctmp operations; removed linked diagnostic
  list/delete-walker storage.
- Core/Menu/menu.c: moved Kit/Instrument/Scene identity handling to HCNAMES,
  deferred family-exit writes, extension-free Instrument display, and the
  reversible kit row.
- Core/Bank/Scene/Preset/* and storageTypes.*: preserved typed staging,
  descriptor-keyed parsing, and ordered runtime Instrument commit.
- Core/Hardware/SD/asyncfatfs/*: five application handles and explicit
  directory-handle lifetime rules.
- config.h/main.c: developer boot observers are gated by CONFIG_DEV_MODE;
  normal boot does not snapshot HCNAMES from partial resident SRAM.
- Documentation: recovered plans/logs were consolidated into this handoff,
  the permanent filesystem/module/asyncfatfs/SRAM specifications, MEMORY.md,
  and the final cache/name manifest.

KNOWN ISSUES INTRODUCED: HCNAMES is unversioned text and its full rewrite is
ordered rather than crash-atomic; Pattern loading remains intentionally
non-atomic after Scene settings/Kit commit; Effect persistence is a placeholder;
the hidden .hctmp file may remain dirty on SD; 107 bytes of unreachable
diagnostic Menu UI state remain linked. No new firmware issue is known from the
final Instrument Load behavior.
KNOWN ISSUES RESOLVED: boot handle exhaustion at Bank repair phase 43/substep
20; blank Kit HCNAMES rows after Kit Save; per-scroll name latency; extension
leakage in short Instrument names; stale/incorrect Instrument kit restoration;
rapid encoder backspin locking at kit; blank/corrupt `.hcindex` caused by the
rejected shared cache/staging union; duplicate resident name/key storage.

NEXT SESSION RECOMMENDED GOAL: Hardware-soak the now-working Instrument Load
path and decide the future versioned/recoverable HCNAMES and Pattern formats
before changing their storage contracts.
BLOCKERS: Pattern redesign is required before atomic Pattern Load can be
claimed. HCNAMES recovery/journaling and crash-safe Bank promotion remain
future decisions.

CRITICAL REMINDERS FOR NEXT SESSION:
- Keep `fs_list_cache_name[1000][9]` and `fs_stage_workspace[2048]` separate.
- Keep exactly 81 bytes of active musical identity: BankData Bank name plus
  filesystem Scene/Kit/six-Instrument rows.
- Bank Load is always mask-selective; an empty selected/present intersection
  must not mean “load all.”
- Pattern is not staged in the 2,048-byte union.
- `.hctmp.<ext>` is excluded from Instrument scan, repair, and `.hcindex`.
- Close explicit asyncfatfs directory handles after `afatfs_chdir()` when the
  copied `currentDirectory` state is sufficient.
- `EXTI_IMR` must remain cleared at the top of `main()`.
```

## Recovery source coverage

The following recovered documents were reviewed and their durable facts are
incorporated below. Their intermediate proposals are retained only as history:

- `HCNAMES_IMPLEMENTATION.md`: HCNAMES format, boot freeze diagnostics,
  handle-lifetime correction, Instrument/Kit/Scene workflows, and final
  combined-menu behavior.
- `NAMES_SRAM_MANIFEST.md`: original resident-name/cache inventory, the 81-byte
  final identity contract, operation-local scratch distinctions, and disposed
  Scene/Kit/Instrument fields.
- `8_CHAR_SANITIZER.md`: canonical eight-cell display/key rules, numeric
  suffixing, typed extensions, and the one-candidate rename/flush/rescan rule.
- `DRUM_HCINDEX_CACHE.md`: typed Instrument index behavior, blank rows versus
  sorted Instrument rows, and the cache ownership constraints.
- `DISPOSE_INST_NAME_CACHE.md`: the rejected per-Instrument/per-Scene cache
  designs and the requirement that HCNAMES, not SRAM arrays, is authoritative.
- `042_NAMES_CACHE_FINAL_DISPOSAL.md`: implementation plan, exact structural
  removals, separate staging design, Bank mask rules, and comment requirements.
- `FINAL_CACHE+NAMES_MANIFEST.md`: final linked-size manifest, operation-local
  filename buffers, five handles, and the residual 107-byte diagnostic UI.
- `042_SESSION_LOG.md`: chronological implementation, hardware diagnostics,
  rejected experiments, and final closeout.

## Final HCNAMES and index contract

`/.hcnames` is fixed-order, newline-delimited, unversioned root metadata with
129 logical rows:

| Rows | Meaning |
|---:|---|
| 0 | Bank name |
| 1..16 | resident Scene names |
| 17..32 | resident Kit names |
| 33..128 | six Instrument names for each resident Scene |

Each value is at most eight printable characters. The card representation
trims trailing spaces; the SRAM representation is eight cells plus NUL. A
targeted update borrows the first 129 rows of the one 9,000-byte cache, reads
and preserves all rows, overlays only successful action-owned rows, rewrites
the variable-length file, closes, and passes the normal flush gate. Missing
HCNAMES can be bootstrapped with blank rows plus only the owned update.

Normal boot no longer regenerates HCNAMES from resident SRAM. This is required
because Scene names are not fields of `scene_t`; a mask-selective Bank Load
cannot reconstruct unselected Scene names without erasing valid card rows.
The public blocking snapshot wrapper remains dormant compatibility/bootstrap
API, not a normal boot step.

`.hcindex` remains the browser/index authority for directory traversal:

- Kit, root Scene, and root Bank indexes are 1,000 slot-ordered rows with
  blank rows preserved.
- Each typed Instrument index contains up to 1,000 sorted filename rows.
- The cache domain is one at a time; it is never duplicated per type/library.
- Instrument display helpers strip `.drm`, `.snr`, `.cym`, and `.hat`; the
  extension is selected from registry type and never appears in the menu name.

## Exact SRAM contract

| Object | Size | Final role |
|---|---:|---|
| `fs_list_cache_name[1000][9]` | 9,000 B | sole `.hcindex`/`.hcnames` cache |
| `fs_stage_workspace` | 2,048 B | aligned Kit, Instrument candidate, or Scene settings+Kit stage |
| `bank_display_name` | 9 B | one Bank name |
| `fs_identity_name[8][9]` | 72 B | one Scene, one Kit, six Instruments |
| `staging_buf` | 512 B | existing stream scratch |
| five asyncfatfs slots | +656 B | accepted capacity over three slots |
| `menu_instrumentTempName` | 9 B | temporary `kit` label while `.hctmp` is valid |

The active musical identity is exactly 81 bytes. There are no names, stems,
or file keys in resident Scene/Kit structs. `storage_kitset_t.op_kitset` is a
312-byte request-local parser manifest containing six 49-byte `file=` cells;
it is not resident file-key storage. A transient 49-byte filename buffer is
allowed for asynchronous rename/create/open lifetimes.

The independent stage is sized for 512 parameter cells, three byte images,
Scene/Kit metadata, and a 384-byte future non-Pattern Effect reserve. Pattern
is deliberately absent. Scene settings plus embedded Kit commit first; Pattern
then streams directly into the final Scene slot. Pattern failure does not roll
back that earlier commit.

Residual linked state that must not be misreported as disposed:

- `menu_testEditName[49]`;
- `menu_testResultName[49]`;
- nine result bytes/timer/kind/flags;
- total 107 B of unreachable compatibility UI state.

The former 64-entry filesystem lists, Bank child name/alias/present arrays,
recursive firmware delete stacks, KitBrowser map, per-Scene names/stems, and
the seven-name Menu cache are not linked final storage.

## Load/Save behavior

### Kit and Instrument family

On entry, one Scene's Kit plus six Instrument names are read from HCNAMES into
the existing identity rows. The requested `.hcindex` then occupies the shared
cache while scrolling and payload I/O proceed. Normal Kit Load/Save updates
all seven rows for each affected Scene; normal Instrument Load/Save updates one
voice row. Morph operations preserve identity. Dirty rows are written once on
family exit, not per scroll.

### Instrument Load `kit` row

Instrument Load now works correctly. Entry or voice change saves the current
voice as `Instrument/<type>/.hctmp.<ext>` and retains its eight-cell name in
the one nine-byte temporary label. `kit` appears above pool row `000`.
Returning to `kit` parses that exact hidden file through the normal single
Instrument candidate stage and the existing ordered Preset runtime apply.
The hidden file is not indexed, repaired, or written to HCNAMES. Its SRAM
validity/name is invalidated on Scene, voice, type, load-type, mode, nested
exit, or family exit. Rapid negative encoder movement clamps at `kit`, clears
stale deferred pool selection, and now permits scrolling down again.

### Scene Load

Scene settings and embedded Kit validate in the 2,048-byte stage and commit to
selected resident Scenes. Pattern reads directly into the first final Scene
PatternSet, then mirrors only after a successful direct read. Effects remain a
placeholder. The transaction is intentionally not Pattern-atomic.

### Bank Load/Save

Bank Load is strictly mask-selective. The requested mask intersects discovered
children; an empty intersection loads no children. Each selected child is
rescanned one at a time. Unselected Scene payloads, HCNAMES rows, and presence
state remain untouched. Bank Save reads HCNAMES once, constructs selected
children through the staging workspace, promotes a temporary sibling, writes
the relevant HCNAMES rows once, and rebuilds/restores `/Bank/.hcindex`.
FAT rename/promotion is not crash-journaled.

## Boot freeze chronology and correction

The HCNAMES writer initially froze before its hook appeared. Boot milestones
identified `Boot +11 / FS +1`, then `FOp +1 / FPhs +42`, `+43`, and finally
`FPhs +43 / FSub +20`. The final coordinate was asyncfatfs handle exhaustion:
Bank root, selected Bank, and Scene handles were retained while opening the
embedded Kit. The targeted correction closes the Bank-root handle after the
selected Bank is opened, then closes the explicit Kit handle after `chdir()`
copies its state into `currentDirectory`. No name policy or rescan-loop change
was inferred from the diagnostic. Boot then completed on hardware.

The pool was later expanded from three to five `afatfsFile_t` slots. Each slot
is 328 B, so the accepted increase is 656 B. This is concurrency headroom, not
permission to leak directory handles.

## Rejected or superseded approaches

- The 9,000-byte cache/staging union erased active `.hcindex` rows and caused
  blank indexes, `KitL00`, and scroll failures. It was removed; cache and stage
  are permanently separate.
- A two-Instrument-image SRAM preview was implemented experimentally, then
  removed. The original Instrument is retained only in `.hctmp.<ext>`.
- A tri-state Bank quarantine result experiment did not change the freeze and
  was reverted.
- Per-action HCNAMES rewrites made scrolling slow. The combined family session
  now reads on entry and writes once at exit.
- A Menu Instrument diagnostic observer obscured the UI, disturbed audio, and
  did not reproduce the freeze; it was removed. Developer boot observers remain
  only behind `CONFIG_DEV_MODE`.
- File/Dir/sDir list caches and menu entries were retired. Compatibility APIs
  return empty/failure without starting asyncfatfs work; the 107-byte residual
  UI state is not a restored diagnostic cache.

## Source/API and documentation record

All implementation changes are documented adjacent to their declarations or
definitions. The durable API/module references were reconciled in:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`;
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`;
- `knowledge_files/specification_reference/ASYNCFATFS_REFERENCE.md`;
- `knowledge_files/specification_reference/SRAM_DTCM_MANIFEST.md`;
- `FINAL_CACHE+NAMES_MANIFEST.md`;
- `MEMORY.md`.

The current linked build is `text 373,328`, `data 408`, aggregate `bss
259,368`; SRAM1 static use is 253,060 B and DTCM static use is 41,884 B. The
packaged image is 373,752 B including its 16-byte header.

## Remaining cautions

- Hardware-soak Instrument Load after the confirmed working correction,
  especially rapid `kit -> pool -> fast decrement -> kit -> pool` movement.
- Do not claim HCNAMES versioning, `.hcrepair` recovery, Pattern atomicity, or
  Bank crash-safe promotion until those designs exist.
- Archive this file before deleting the recovered working documents.
