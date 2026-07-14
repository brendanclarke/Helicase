# Load/Save Follow-up Plan

## Purpose

This document logs the current state of the load/save operations we actually
need after the asyncfatfs long-name/case-sensitive expansion. It is not a new
filesystem design document. Its job is to separate:

- operations that are still compiled and callable in code;
- operations currently reachable from the panel Load/Save page;
- operations that were written before the asyncfatfs shift and now require
  retest or repair before they can be trusted again;
- operations that do not yet exist as real production features.

## Immediate Answer

Yes, the older musical load/save operations are still largely in place at the
`Preset` and `filesystem` layers.

The important caveat is reachability: the main Load/Save menu type cycler is
currently clamped to the generic asyncfatfs diagnostic entries:

- `Load:[File    ]`
- `Load:[Dir     ]`
- `Save:[File    ]`
- `Save:[Dir     ]`

The older `Kit`, `KitMrp`, `Scene`, `Settings`, and `Samples` enum values still
exist in `Core/Menu/menu.h`, and their dispatch branches still exist in
`Core/Menu/menu.c`, but `menu_handleLoadSaveMenu()` currently cycles only
between `SAVE_TYPE_FILE` and `SAVE_TYPE_DIR`, and `menu_resetSaveParameters()`
forces stale musical entries back to `SAVE_TYPE_FILE`. That was intentional
during asyncfatfs validation so the diagnostic surface could be tested without
accidentally invoking older save paths.

## Current Menu Reachability

### Active Now

These are the only top-level Load/Save entries intentionally reachable from the
normal type editor:

| Menu entry | Entry point | Filesystem path | Current status |
|---|---|---|---|
| `Load:[File]` | `preset_loadTestFile()` | `filesystem_requestLoadTestFile()` | Hardware-tested on root files; working. |
| `Load:[Dir]` | `preset_loadTestDir()` | `filesystem_requestLoadTestDir()` | Hardware-tested; dot-prefixed files correctly remain eligible. |
| `Save:[File]` | `preset_saveTestFile()` | `filesystem_requestSaveTestFile()` | Hardware-tested; writes exact visible name and synced payload. |
| `Save:[Dir]` | `preset_saveTestDir()` | `filesystem_requestSaveTestDir()` | Hardware-tested; creates directory and same-name child file. |

### Present But Gated From Normal Type Cycling

These menu branches and lower-level operations still exist, but should be
treated as pending retest after asyncfatfs changed the storage foundation:

| Operation | Present code path | Current reachability | Notes |
|---|---|---|---|
| Normal Kit Load | `preset_loadKitForScenes()` -> `filesystem_requestLoadKitForScenes()` | Gated from top-level type cycling | Loads `Kit/<NNN Name>/kitset.kcg` plus six instrument files. Needs retest against LFN/case-sensitive open. |
| Kit Morph Load | `preset_loadKitMorphForScenes()` -> `filesystem_requestLoadKitMorphForScenes()` | Gated from top-level type cycling | Stages normal Kit directory, then Preset copies same-type normal endpoints into morph endpoints. Load path exists; save counterpart is not implemented. |
| Normal Kit Save | `preset_saveDrumset(..., 0)` -> `filesystem_requestSaveKitDirectory()` | Gated from top-level type cycling | Directory writer exists and now calls LFN-aware asyncfatfs APIs. It was the original broken foundation that motivated the expansion and needs full retest before promotion. |
| Scene Load | `preset_loadSceneForScenes()` -> `filesystem_requestLoadSceneForScenes()` | Gated from top-level type cycling | Staged directory load exists for root `Scene/` slots. Requires retest after LFN/case-sensitive asyncfatfs changes. |
| Scene Save | `preset_saveScene()` -> `filesystem_requestSaveSceneDirectory()` | Gated from top-level type cycling | Directory writer exists. Requires retest with updated asyncfatfs, especially folder/file case and overwrite behavior. |
| Globals Load | `preset_loadGlobals()` -> `filesystem_requestLoad(FS_FILE_GLOBALS)` | Gated from top-level type cycling | Raw `glo.cfg` load path still exists. Diagnostic `Load:[File] GLO.CFG` proved low-level read works, but production apply path still needs a quick retest. |
| Globals Save | `preset_saveGlobals()` -> `filesystem_requestSave(FS_FILE_GLOBALS)` | Gated from top-level type cycling | Raw `glo.cfg` save path still exists. Needs retest after sync and case-preservation changes. |
| Samples Load | `menu_loadSamplesModal()` -> blocking sample install path | Gated from top-level type cycling | The modal branch still exists. It is not part of the File/Dir diagnostics and needs a separate sample-load retest. |

## Still Available Outside Top-level Save Type Cycling

Instrument Load is not a simple top-level `SAVE_TYPE_*` branch. It is a nested
Load UI entered from a destination voice/slot workflow, and its core code still
exists:

| Operation | Entry point | Filesystem path | Notes |
|---|---|---|---|
| Instrument Load | `preset_loadInstrument()` | `filesystem_requestLoadInstrument()` | Stages one root `Instrument/` file and commits transactionally. Needs retest with new object scanning/open behavior. |
| Instrument Morph Load | `preset_loadInstrumentMorph()` | `filesystem_requestLoadInstrument()` with morph completion | Same file loader, Preset copies same-type normal values into morph endpoints. Needs retest after Instrument Load. |

There is still no standalone production Instrument Save in the current code.
Kit Save writes kit-member instrument files, but that is not the same as a root
`Instrument/` save feature.

## Legacy Operations Still Compiled

The older flat/container operations remain compiled through
`presetManager.c` and `filesystem_requestLoad()/filesystem_requestSave()`:

| Operation | Status |
|---|---|
| Pattern load/save | Present through `FS_FILE_PATTERN`; not current focus. |
| All load/save | Present through `FS_FILE_ALL`; legacy container path. |
| Performance load/save | Present through `FS_FILE_PERFORMANCE`; legacy container path. |
| Legacy Morph load/save | Present through `FS_FILE_MORPH`; distinct from KitMrp directory load. |

These should not be assumed validated by the asyncfatfs File/Dir test pass.
They need their own pass if they remain part of the product surface.

## Important Current Policy

Dot-prefixed files and directories are real filesystem objects.

The diagnostic menus must not hide names beginning with `.`. If `.DS_Store`,
`._foo`, or another dot-prefixed file sorts first in a directory, `Load:[Dir]`
should read that file. That behavior is correct for this phase because the goal
is exact filesystem behavior, not a user-friendly filtered browser.

The only entries asyncfatfs object scans should hide are structural FAT records:

- deleted directory entries;
- VFAT fragment entries;
- volume labels;
- structural `.` / `..` directory entries.

## Promotion Plan Back To Real Load/Save

### Step 1: Freeze The Diagnostic Baseline

Keep `File` and `Dir` as the only active top-level entries until the following
are considered stable:

- exact-case root file scan;
- exact-case root directory scan;
- exact-case root file open;
- exact-case root directory open;
- exact-case root file create/overwrite;
- exact-case root directory create/open;
- same-name child file create inside a created directory;
- close + sync persistence visible after power-off/card removal.

Current hardware evidence is positive for this baseline.

### Step 2: Retest Production Reads Before Production Writes

Re-enable or manually call read paths before save paths:

1. Globals Load: `glo.cfg`.
2. Kit Load: several existing `Kit/<NNN Name>/` folders, including one with
   mixed case and spaces.
3. KitMrp Load: same Kit folders, verifying morph endpoint copy and no type
   replacement.
4. Scene Load: existing `Scene/<NNN Name>/` folders, including embedded Kit,
   pattern, and effect placeholders.
5. Instrument Load: each type from root `Instrument/`.
6. InstrumentMrp Load: same-type only, no type replacement.
7. Samples Load: `samples/` and `loops/`.

### Step 3: Retest Production Writes One Writer At A Time

Only after reads prove that scan/open behavior is stable, retest writes:

1. Globals Save: verify `glo.cfg` case and contents.
2. Kit Save: verify folder name, `kitset.kcg`, six instrument member files,
   `[params]`, `[morph]`, and post-power-cycle discoverability.
3. Scene Save: verify root Scene folder, `sceneset.scg`, embedded Kit folder,
   pattern/effect files, and post-power-cycle discoverability.

Do not promote Morph Save until its semantics are implemented deliberately.
There is currently KitMrp load behavior, but not the requested `Save:[KitMrp]`
operation that flips current interpolation parameters into normal endpoints and
normal endpoints into morph endpoints.

### Step 4: Restore Menu Type Cycling Deliberately

Once an operation passes its retest, widen the type cycler in
`menu_handleLoadSaveMenu()` and adjust `menu_resetSaveParameters()` so that
only validated entries are reachable.

Recommended staged order:

1. Keep `File` / `Dir`.
2. Add `Settings` after Globals Load/Save retest.
3. Add `Kit` load only.
4. Add `Kit` save after writer retest.
5. Add `KitMrp` load.
6. Add `Scene` load.
7. Add `Scene` save.
8. Add `Samples` load.
9. Add future `KitMrp` save only after its own implementation/audit.

## Open Questions To Track

- Should top-level production browsers include dot-prefixed files exactly like
  the diagnostics, or should only product-specific scanners filter product
  extensions/naming conventions? Current filesystem policy says asyncfatfs must
  not filter; product scanners can choose stricter rules locally.
- Do Kit/Scene writers need a real recursive replacement primitive before they
  are considered production-ready, or is overwrite-in-place of known children
  sufficient for the next hardware pass?
- Should `kitset.kcg` and all future system filenames be lower-case on write?
  This was previously requested and should remain part of the production writer
  retest criteria.
- Should legacy Pattern/All/Performance/Morph remain visible at all once the
  Scene/Bank architecture is implemented?

## Current Bottom Line

The real load/save machinery was not deleted. It is still present below the
temporary diagnostic surface. The next work is not to rediscover it, but to
promote it back into reachability only after each production operation is
retested against the new asyncfatfs behavior.

## Detailed Plan: Restore Kit + Instrument Load/Save

### Scope Of This Pass

This pass should restore the production Kit and Instrument workflows on top of
the new asyncfatfs behavior:

- top-level `Load:[Kit]`;
- top-level `Save:[Kit]`;
- nested Instrument Load from the Load page;
- nested/root Instrument Save from the Save page.

This pass should not restore Scene, Settings, Samples, Pattern, All,
Performance, or legacy Morph menu reachability. Those remain separate
promotion steps. It should also not implement `Save:[KitMrp]`; Kit Morph Load
exists, but Morph Save has distinct endpoint-flip semantics and needs its own
audit.

### Implementation Notes: Kit + Instrument Pass

Status: implemented and build-checked.

Important slot-policy correction from hardware/UI review:

- `000` is now a real library slot for every numbered file type.
- Kit and Scene folder displays use direct `000..999` slot identity.
- This is separate from Instrument file internals: instrument serializer/parser
  voice coordinates remain one-based `1..6`, because those numbers identify kit
  voices and LFO self-target context, not root library slots.

Files touched in this pass:

- `Core/Hardware/SD/filesystem.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Menu/menu.c`
- `Core/Menu/menu.h`
- `Core/Hardware/frontPanel/buttonHandler.c`

Implemented filesystem changes:

- Added `FS_INTERNAL_OP_SAVE_INSTRUMENT`.
- Added root Instrument Save scratch fields for source Scene, source voice
  slot, captured type, exact visible display filename, and returned short alias.
- Added `filesystem_requestSaveInstrument()`.
- Added `filesystem_saveInstrument_tick()`.
- The writer creates/opens `Instrument/` through `afatfs_mkdir_lfn()` with
  `AFATFS_MATCH_CASE_SENSITIVE`.
- The writer opens the target file through `afatfs_fopen_lfn()` so mixed-case
  and long visible names are preserved.
- The writer streams the resident source instrument with
  `storage_formatInstrumentLine()` via the same bounded text-line writer used
  by Kit Save.
- The writer passes `source_slot + 1` as the file voice coordinate. That is
  intentionally still one-based because Instrument files encode kit voice
  identity, not root library slot identity.
- On successful close and root return, the writer updates the Instrument browser
  cache through `filesystem_updateInstrumentCacheAfterSave()` so a saved file
  can be loaded without requiring an immediate rescan.

Implemented Preset changes:

- Added `PRESET_OP_INSTRUMENT_SAVE`.
- Added `on_instrument_save_complete()`.
- Added `preset_saveInstrument()`.
- Changed `preset_saveDrumset()` from `void` to `uint8_t` so Menu can set its
  busy lock only when Kit Save was actually accepted by filesystem.

Implemented Menu/ButtonHandler changes:

- Restored the top-level Load/Save type cycler as a whitelist:
  `File -> Dir -> Kit -> File`.
- Scene, Settings, Samples, KitMrp, and legacy container entries remain
  compiled but gated from normal type cycling.
- `menu_resetSaveParameters()` now preserves the restored whitelist and clears
  nested Instrument state; it no longer forces every musical entry back to
  `File`.
- `Save:[Kit]` now sets `menu_storageBusy` only when
  `preset_saveDrumset(..., 0)` accepts the request, and save completion clears
  the busy flag.
- Voice buttons on the Save page now enter nested Instrument Save mode.
- Nested Instrument Save shows `Save:[Type]` on the top row, an
  eight-character editable stem on the bottom row, and `ok` at the right.
- OK in nested Instrument Save calls `preset_saveInstrument()` with the
  selected source Scene, source voice slot, and edited stem.
- SEQ buttons can change the source Scene while nested Instrument Save is
  active; the editable stem reseeds from the newly selected resident slot.
- ButtonHandler comments were updated because the same voice-button interception
  now covers Instrument Load destinations and Instrument Save sources.

Adjacent comment coverage added:

- Public headers document the restored Kit direct-slot semantics and the new
  root Instrument Save request.
- `filesystem.c` documents the save scratch fields, the state-machine phases,
  the LFN/case-sensitive root and file open choices, the bounded text streaming,
  and the reason `source_slot + 1` is still correct for Instrument file voice
  numbering.
- `presetManager.c/.h` document Instrument Save completion and acceptance
  behavior.
- `menu.c/.h` document the restored type whitelist, nested Instrument Save
  editor, busy-lock acceptance, and `000..999` display semantics.
- `buttonHandler.c` documents the widened Load/Save voice-button interception.

Build verification:

- Ran `make`.
- Result: success.
- Existing linker warnings remain about unimplemented nano-lib syscall stubs
  (`_close`, `_lseek`, `_read`, `_write`); these are not introduced by this
  pass.
- Ran `git diff --check`.
- Result: clean.

### Current Code State From The Dive

Kit Load is present:

- Menu dispatch exists in `menu_requestCurrentLoadSaveSelection()` for
  `SAVE_TYPE_KIT`.
- Preset entry point exists as `preset_loadKitForScenes()`.
- Filesystem entry point exists as `filesystem_requestLoadKitForScenes()`.
- State machine exists as `filesystem_loadKitDirectory_tick()`.
- Completion handling exists in `menu_pollPresetStatus()` under
  `PRESET_OP_KIT_LOAD`.

Kit Save is present:

- Menu dispatch exists under `SAVE_TYPE_KIT` on the Save page.
- Preset entry point exists as `preset_saveDrumset(..., 0)`.
- Filesystem entry point exists as `filesystem_requestSaveKitDirectory()`.
- State machine exists as `filesystem_saveKitDirectory_tick()`.
- Completion handling exists under `PRESET_OP_KIT_SAVE`.

Instrument Load is present:

- Button routing calls `menu_loadInstrumentVoicePressed()` from VOICE button
  handling.
- Nested Load UI exists through `menu_instrumentLoadActive`.
- Preset entry points exist as `preset_loadInstrument()` and
  `preset_loadInstrumentMorph()`.
- Filesystem entry point exists as `filesystem_requestLoadInstrument()`.
- State machine exists as `filesystem_loadInstrument_tick()`.
- Completion handling exists under `PRESET_OP_INSTRUMENT_LOAD` and
  `PRESET_OP_INSTRUMENT_MORPH_LOAD`.

Standalone Instrument Save is absent:

- There is no `PRESET_OP_INSTRUMENT_SAVE`.
- There is no `preset_saveInstrument()`.
- There is no `filesystem_requestSaveInstrument()`.
- There is no `FS_INTERNAL_OP_SAVE_INSTRUMENT`.
- There is no root `Instrument/` writer state machine.
- The needed text serializer already exists:
  `storage_formatInstrumentLine()`.
- The needed filename/display helper already exists:
  `storage_makeSavedInstrumentDisplayFilename()`.

### Asyncfatfs Migration Principle

The production code should use asyncfatfs as the single filename authority.

Current Kit and Instrument scans still walk raw FAT entries with
`afatfs_findNext()` and reconstruct LFN text with filesystem-local helpers.
That duplicates logic now centralized in `afatfs_findNextObject()`. Before
menu promotion, Kit and Instrument scans should be moved to the object iterator
so diagnostics and production browsers agree on:

- exact display case;
- short alias;
- file/directory classification;
- LFN checksum validation;
- structural FAT filtering.

The product scanners can still apply product policy after receiving objects:
Kit scan accepts numbered directories; Instrument scan accepts files with
registered instrument extensions. The asyncfatfs layer itself must not hide
ordinary dot-prefixed names.

### Code Change Plan: Root Name Constants

Change `Core/Hardware/SD/storageTypes.h`:

- Replace `STORAGE_ROOT_INSTRUMENT "INSTRU~1"` with
  `STORAGE_ROOT_INSTRUMENT "Instrument"`.
- Keep `STORAGE_ROOT_KIT "Kit"` and `STORAGE_ROOT_SCENE "Scene"`.
- Update the leading storage constants comment to say these literals are exact
  display components, not short aliases.

Comment text to place adjacent to the constants:

```c
/*
 * Root directory literals are exact display components.
 *
 * asyncfatfs now preserves and matches case through SFN case bits and VFAT LFN
 * entries, so production code should ask for "Instrument" rather than the old
 * compatibility alias "INSTRU~1". Callers that need to open these roots should
 * use the LFN-aware directory APIs when the operation is part of the new
 * production storage surface.
 */
```

Why this must happen:

- `INSTRU~1` is an alias workaround from before exact LFN open existed.
- Keeping it would make Instrument Load/Save depend on host-generated alias
  spelling instead of the intended on-card directory name.

Affiliates:

- `filesystem_scanInstruments_tick()`;
- `filesystem_loadInstrument_tick()`;
- new `filesystem_saveInstrument_tick()`;
- generated `SD_CARD/Instrument/` fixtures.

### Code Change Plan: Kit Scan

Change `filesystem_scanKits_tick()` in `Core/Hardware/SD/filesystem.c`:

- Open root `Kit` using `afatfs_opendir_lfn(STORAGE_ROOT_KIT,
  AFATFS_MATCH_CASE_SENSITIVE, ...)` or continue using `afatfs_fopen()` only if
  the scan remains explicitly compatibility-mode. Recommendation: use
  `afatfs_opendir_lfn()` so the root is matched as the exact display component.
- Replace raw `afatfs_findFirst()` / `afatfs_findNext()` plus
  `filesystem_dirLfnAppendEntry()` with `afatfs_findFirstObject()` /
  `afatfs_findNextObject()`.
- For each returned object:
  - require `object.kind == AFATFS_OBJECT_DIRECTORY`;
  - pass `object.displayName` to `filesystem_recordKitDirectory()`;
  - pass `object.shortName` as the cached open alias, or cache enough display
    text to reopen the selected folder by exact display component.
- Keep missing root `Kit/` as successful empty scan.

Comment text to place at the scanner loop:

```c
/*
 * Scan Kit/ through the asyncfatfs object iterator.
 *
 * Inputs: concrete file/directory objects with displayName already resolved
 * from VFAT LFN fragments or SFN case bits. Output: only numbered directory
 * components become Kit browser slots. The product-level numbered-folder
 * parser is the filter; asyncfatfs has already handled structural FAT records,
 * and it intentionally does not hide ordinary dot-prefixed names.
 */
```

Loop comment to place inside the `FIND_NEXT` phase:

```c
/*
 * One public object may consume several raw FAT entries.
 *
 * The iterator can return IN_PROGRESS while cache sectors are unavailable, so
 * this loop must advance only after SUCCESS. A NONE object means end of
 * directory, not an empty slot; missing Kit slot semantics are handled by the
 * numbered-folder cache after scanning completes.
 */
```

Why this must happen:

- The current Kit scan has a second LFN parser that can drift from asyncfatfs.
- Re-exposed Kit Load must display and select the same names proven by
  `Load:[Dir]`.

Affiliates:

- `filesystem_recordKitDirectory()`;
- `filesystem_kitSlotExists()`;
- `filesystem_kitSlotName()`;
- `kitBrowser.c` compatibility map;
- `menu_requestCurrentLoadSaveSelection()`.

### Code Change Plan: Kit Load

Change `filesystem_loadKitDirectory_tick()`:

- Open root `Kit/` by exact display component with `afatfs_opendir_lfn()`.
- Reopen the selected kit directory using the cache result from the updated Kit
  scan. If the cache stores only the short alias, document that this is a
  scanned-object identity open, not a user text match. If the cache stores the
  full display component, use `afatfs_opendir_lfn()` case-sensitively.
- Continue opening `kitset.kcg` with `afatfs_fopen()` because it is a fixed
  system file literal in the current directory and asyncfatfs now preserves
  lower-case SFN display bits. If this ever becomes ambiguous, change it to
  `afatfs_fopen_lfn(STORAGE_KITSET_FILENAME, ..., CASE_SENSITIVE, ...)`.
- Continue opening kit member files from `kitset.kcg`. These file names are
  writer-owned aliases today, so `afatfs_fopen()` is acceptable. If kitset
  later stores display names instead of aliases, this phase must switch to
  `afatfs_fopen_lfn()`.

Comment text to place near selected folder open:

```c
/*
 * Open the selected Kit folder from scan-cache identity.
 *
 * The user selects a numbered Kit slot, not an arbitrary typed path. The scan
 * cache was populated from actual asyncfatfs objects, so the open target must
 * come from that cache rather than being reconstructed from the LCD name. This
 * prevents case/alias mismatches and keeps compatibility with numbered folders
 * whose display separator was accepted by storage_parseNumberedFolder().
 */
```

Comment text to place near instrument-file opens from `kitset.kcg`:

```c
/*
 * Open kit member files exactly as kitset.kcg names them.
 *
 * Current Kit Save writes asyncfatfs-returned short aliases into kitset.kcg,
 * so this open is an identity open inside the already-selected Kit directory.
 * If a future schema stores long display filenames here, this is the single
 * phase that should move to afatfs_fopen_lfn().
 */
```

Why this must happen:

- Kit Load must continue to support saved kits whose kitset member filenames
  are short aliases.
- The UI-facing slot selection should not synthesize paths.

Affiliates:

- `storage_kitsetParseLine()`;
- `storage_instrumentParseLine()`;
- `preset_loadKitForScenes()`;
- `preset_startDrumsetApply()` / Menu completion apply.

### Code Change Plan: Kit Save

Change `filesystem_saveKitDirectory_tick()`:

- Create/open root `Kit/` with `afatfs_mkdir_lfn(STORAGE_ROOT_KIT,
  AFATFS_MATCH_CASE_SENSITIVE, ...)` instead of `afatfs_mkdir()`.
- Keep the occupied-slot behavior that opens the scanned slot identity instead
  of creating a duplicate `NNN Name` directory.
- For empty slots, continue creating `NNN Name` with
  `afatfs_mkdir_lfn(..., AFATFS_MATCH_CASE_SENSITIVE, ...)`.
- Continue writing member instruments before `kitset.kcg`, because
  `kitset.kcg` stores the actual aliases returned by `afatfs_fopen_lfn()`.
- Continue using `afatfs_sync()` through the existing flush-finish path before
  reporting save completion.
- After a successful save, update the Kit scan cache from the actual created
  folder display/alias pair. Existing-slot saves must not rename the folder as
  a side effect.

Comment text to place near root Kit creation:

```c
/*
 * Create/open the root Kit directory through the LFN-aware directory path.
 *
 * "Kit" is a short display component today, but using mkdir_lfn keeps this
 * production writer on the same case-preserving path as long Kit folders and
 * prevents future root-name changes from falling back to raw uppercase SFN
 * behavior.
 */
```

Comment text to keep/expand near member instrument writes:

```c
/*
 * Write member instruments before committing kitset.kcg.
 *
 * Each afatfs_fopen_lfn() call returns the short alias selected on disk. The
 * kitset writer records those aliases only after every member file has opened
 * and streamed successfully, so a failed partial save does not publish a new
 * kitset pointing at missing or half-written instrument files.
 */
```

Important loop comment for the six-instrument write loop:

```c
/*
 * The instrument slot loop is intentionally sequential.
 *
 * asyncfatfs has a small fixed open-file pool and one currentDirectory. Saving
 * one member at a time keeps the state machine bounded, preserves the alias for
 * each slot before the next file starts, and avoids holding directory handles
 * while streaming text.
 */
```

Why this must happen:

- Kit Save already proved sensitive to directory creation/open semantics.
- The writer should exercise the new LFN directory creation path, not the old
  short-name compatibility wrapper.

Affiliates:

- `filesystem_prepareSavedInstrumentFilenames()`;
- `storage_makeSavedInstrumentDisplayFilename()`;
- `storage_formatInstrumentLine()`;
- `storage_formatKitsetLine()`;
- `menu_currentSaveWouldOverwrite()` / `OW` display.

### Code Change Plan: Instrument Scan

Change `filesystem_scanInstruments_tick()`:

- Use `afatfs_opendir_lfn(STORAGE_ROOT_INSTRUMENT,
  AFATFS_MATCH_CASE_SENSITIVE, ...)`.
- Replace raw FAT scanning and local LFN reconstruction with
  `afatfs_findNextObject()`.
- Accept only `AFATFS_OBJECT_FILE`.
- Classify the file type from `object.displayName` first, because the visible
  LFN extension is what the user sees. Fall back to `object.shortName` only if
  needed for legacy alias-only files.
- Cache:
  - eight-character display stem from `object.displayName`;
  - retained 16-character source stem from `object.displayName`;
  - open alias from `object.shortName`.

Comment text to place near classification:

```c
/*
 * Classify Instrument files by visible filename first.
 *
 * The Instrument browser is a user-facing LFN list. A host-created long file
 * may have a generated short alias that is less meaningful than its display
 * extension, so extension/type matching should prefer object.displayName and
 * fall back to object.shortName only for legacy alias-only media.
 */
```

Loop comment:

```c
/*
 * Keep all ordinary files visible to the product classifier.
 *
 * Dot-prefixed files are not hidden here by filesystem policy. They simply
 * fail the registered instrument extension/type check unless they are genuine
 * instrument files.
 */
```

Why this must happen:

- The current root Instrument constant is an alias.
- The current scan duplicates LFN reconstruction.
- Instrument Save will write visible mixed-case names and must be discoverable
  by the same scanner.

Affiliates:

- `filesystem_recordInstrumentFile()`;
- `filesystem_instrumentTypeFromFilename()`;
- `filesystem_instrumentName()`;
- nested Instrument Load UI.

### Code Change Plan: Instrument Load

Change `filesystem_loadInstrument_tick()`:

- Open root `Instrument/` through `afatfs_opendir_lfn()` using the new exact
  root constant.
- Continue opening the selected file by cached short alias unless the cache is
  extended to retain the full display component. This is safe because the alias
  comes from the object selected during scan.
- Keep staging behavior unchanged: reset only `op_staged_instrument`, parse
  lines into staging, copy display/stem metadata only after validation.

Comment text to place near the selected file open:

```c
/*
 * Open the selected Instrument file by scan-cache identity.
 *
 * Menu selects a typed cache index, not a freshly-entered filename. The cached
 * short alias belongs to the exact object returned by the Instrument/ scan, so
 * the open cannot drift to another same-display candidate while the load is in
 * flight. Display/stem metadata is committed only after the parser validates
 * the file.
 */
```

Why this must happen:

- Root Instrument open must stop depending on `INSTRU~1`.
- Existing transactional commit behavior is good and should not be rewritten.

Affiliates:

- `preset_startInstrumentApply()`;
- `preset_startInstrumentMorphApply()`;
- `menu_tickInstrumentApply()`;
- `scene_setInstrumentSourceName()`;
- `storage_instrumentParseLine()`.

### Code Change Plan: Add Root Instrument Save

Add filesystem API in `Core/Hardware/SD/filesystem.h/.c`:

```c
bool filesystem_requestSaveInstrument(uint8_t source_scene,
                                      uint8_t source_slot,
                                      const char *display_name,
                                      fs_completion_cb_t cb);
```

Add internal operation:

- `FS_INTERNAL_OP_SAVE_INSTRUMENT`;
- state machine `filesystem_saveInstrument_tick()`.

Recommended state machine:

1. Validate `source_scene`, `source_slot`, and source slot type.
2. Copy/sanitize the requested display filename into operation storage.
3. `chdir(NULL)`.
4. Create/open root `Instrument/` with `afatfs_mkdir_lfn("Instrument",
   CASE_SENSITIVE, ...)`.
5. `chdir(Instrument/)`.
6. Open target file with `afatfs_fopen_lfn(display_filename, "w",
   CASE_SENSITIVE, returned_alias, on_file_opened)`.
7. Stream `storage_formatInstrumentLine()` for the selected
   `kit_instrument_slot_t`.
8. Close file.
9. `chdir(NULL)`.
10. Update the per-type Instrument scan cache with the visible display name and
    returned alias, or mark the cache stale and request a rescan.
11. Finish through the existing flush/sync completion boundary.

Comment text for the new public API:

```c
/*
 * Save one resident Scene instrument as a root Instrument/ file.
 *
 * Inputs: source Scene, source slot, user-facing display filename, and
 * completion callback. Output: an async write of the same instrument text
 * schema used by Kit Save member files. The source Scene/slot is copied at
 * request time so later UI selection changes cannot retarget an in-flight
 * save. This is separate from Kit Save because root Instrument files are a
 * reusable pool, not kit membership records.
 */
```

Comment text for the writer state:

```c
/*
 * SAVE ROOT INSTRUMENT state machine.
 *
 * This writer reuses storage_formatInstrumentLine(), so root Instrument files
 * and Kit member files share one schema. Filesystem owns only directory
 * navigation, exact-case LFN creation, streaming, and cache update; storageTypes
 * owns which descriptor keys are emitted.
 */
```

Comment text for the write loop:

```c
/*
 * Stream the selected instrument one schema line at a time.
 *
 * The source pointer is read from the retained request Scene/slot, while
 * op_write_line_index is the only progression variable. afatfs_fwrite() may
 * accept partial rows, so filesystem_writeTextLine() preserves the line offset
 * and this phase must not advance until that helper reports schema completion.
 */
```

Why this must happen:

- There is no standalone Instrument Save today.
- The serializer already exists and is known to be used by Kit Save, so the new
  writer should reuse it rather than inventing another schema.

Affiliates:

- `storage_formatInstrumentLine()`;
- `storage_makeSavedInstrumentDisplayFilename()`;
- `scene_instrumentSlotConst()`;
- `scene_getConst()`;
- `filesystem_recordInstrumentFile()` or a new cache-update helper;
- `preset_saveInstrument()`;
- nested Instrument Save menu state.

### Code Change Plan: Preset Instrument Save Boundary

Change `Core/Scene/Preset/presetManager.h/.c`:

- Add `PRESET_OP_INSTRUMENT_SAVE`.
- Add callback `on_instrument_save_complete()`.
- Add:

```c
uint8_t preset_saveInstrument(uint8_t source_scene,
                              uint8_t source_slot,
                              const char *display_name);
```

Comment text for Preset:

```c
/*
 * Post one root Instrument Save request.
 *
 * Inputs: source Scene/slot and filename display component captured by Menu.
 * Output: a filesystem write operation reported as PRESET_OP_INSTRUMENT_SAVE.
 * Preset does not copy or normalize instrument data here; the source slot is
 * already resident SceneData, and filesystem/storageTypes own serialization.
 */
```

Why this must happen:

- Menu should not call filesystem directly.
- Save completion must enter the same `preset_getStatus()` /
  `menu_pollPresetStatus()` path as the rest of storage.

Affiliates:

- `preset_getCompletedOp()`;
- `menu_pollPresetStatus()`;
- `filesystem_requestSaveInstrument()`.

### Code Change Plan: Menu Type Cycling

Change `menu_handleLoadSaveMenu()`:

- Replace the File/Dir-only type toggle with a bounded production list.
- For this pass, recommended top-level list:
  - `File`;
  - `Dir`;
  - `Kit`.
- Do not add `Scene`, `Settings`, or `Samples` in this pass.
- Do not add `KitMrp` save in this pass.
- If KitMrp Load is not part of this specific hardware pass, keep it gated even
  though its code exists.

Recommended helper:

```c
static uint8_t menu_nextRestoredLoadSaveType(uint8_t what, int8_t inc);
```

This helper is justified because the list will be intentionally sparse and
different from enum order while features are promoted back in stages.

Comment text for the helper:

```c
/*
 * Step through the deliberately restored Load/Save type list.
 *
 * The enum still contains stale/future entries, but reachability is a product
 * decision made here. This helper keeps File/Dir diagnostics available while
 * adding only the production entries that have been retested against the new
 * asyncfatfs implementation.
 */
```

Change `menu_resetSaveParameters()`:

- Stop forcing every non-File/Dir type back to File.
- Clamp only to the restored list.
- Preserve Kit after a Kit save/load completion so the user can continue
  testing adjacent slots without re-entering the type.

Comment text:

```c
/*
 * Reset Load/Save cursor state without demoting validated production types.
 *
 * During asyncfatfs bring-up this function forced stale musical entries back
 * to File. Once Kit is restored, completion should keep the selected validated
 * type and only reset the row/name cursor, so repeated Kit saves and loads can
 * be tested without hidden type changes.
 */
```

### Code Change Plan: Menu Kit Dispatch

Change `menu_handleLoadSaveMenu()`:

- Load page:
  - keep Kit instant-on-scroll if that remains desired;
  - ensure explicit OK behavior is still correct if the cursor reaches `ok`;
  - preserve Scene target mask behavior.
- Save page:
  - keep `preset_saveDrumset(menu_currentPresetNr[SAVE_TYPE_KIT], 0)`;
  - set `menu_storageBusy = 1u` if the request was accepted, or add an accepted
    return value to `preset_saveDrumset()` if needed.

Recommended minor API cleanup:

- Change `preset_saveDrumset()` from `void` to `uint8_t` so Menu can reliably
  know whether the filesystem accepted the request.

Comment text for save dispatch:

```c
/*
 * Save Kit through the directory writer only after request acceptance.
 *
 * Menu owns busy-state and LCD navigation, but Preset/filesystem own whether a
 * request can start. Treating save acceptance as a boolean prevents the UI from
 * entering a false busy/completion cycle when the filesystem is already active
 * or the slot is invalid.
 */
```

### Code Change Plan: Nested Instrument Load/Save UI

Current nested Instrument Load state is named `menu_instrumentLoad*`. To add
Save without confusion, either:

1. Rename the state to neutral `menu_instrumentIo*`, or
2. Add parallel `menu_instrumentSave*` state.

Recommendation: use neutral naming only if the edit is already touching most
of the nested Instrument code. Otherwise, keep the existing Load names for
minimal churn and add an explicit `menu_instrumentSaveMode` flag with comments
that it extends the existing nested browser.

Behavior plan:

- VOICE press on `LOAD_PAGE` continues to enter Instrument Load.
- VOICE press on `SAVE_PAGE` enters Instrument Save for the selected source
  Scene/slot.
- Save mode should show the selected slot type and a filename editor seeded
  from the slot's retained `instrument_stem`.
- Save mode should not expose InstrumentMrp.
- OK in Save mode calls `preset_saveInstrument(source_scene, source_slot,
  edited_display_filename)`.
- Save mode should lock Scene/slot changes while the save request is busy, just
  like Instrument Load locks destination changes.

Comment text for the VOICE button handler:

```c
/*
 * Consume VOICE presses as Instrument file operations while Load/Save is open.
 *
 * Load mode treats the voice as a destination slot for a root Instrument/ file.
 * Save mode treats the voice as the source slot to serialize into the root
 * Instrument/ pool. ButtonHandler calls this before normal voice preview/page
 * selection so storage-owned Instrument workflows have one Menu entry point.
 */
```

Comment text for the nested save state:

```c
/*
 * Nested Instrument Save state.
 *
 * Inputs: source Scene/slot, retained source stem, and the current filename
 * editor text. Output: one root Instrument/ save request on OK. This is not a
 * Kit Save shortcut: it writes exactly one reusable instrument file and leaves
 * the resident kit unchanged.
 */
```

### Code Change Plan: Instrument Save Filename

Use `storage_makeSavedInstrumentDisplayFilename()` to seed the filename from
the source slot stem/type.

Rules:

- Preserve case and spaces where FAT permits.
- Append the descriptor-owned extension.
- Use the normal Save name editor for the visible stem.
- Ensure extension is present before calling filesystem.
- Reject or replace slash/backslash/control bytes before request start.

Important detail:

The current Load/Save name editor shows eight editable characters. Root
Instrument stems can retain sixteen characters in SceneData. First pass should
either:

- save using the first eight edited LCD characters plus extension; or
- add horizontal scrolling to the editor.

Recommendation for the first restoration pass: use the existing eight-character
editor and document the limitation. Do not add long-name horizontal editing in
the same pass unless required by hardware testing.

Comment text near filename preparation:

```c
/*
 * Seed Instrument Save from the retained source stem, then bound it to the
 * current LCD editor.
 *
 * SceneData may retain a 16-character stem, but this Load/Save editor exposes
 * only eight editable cells today. The root Instrument writer still uses the
 * LFN-capable filesystem path; this first UI pass simply limits how much of the
 * stem can be edited from the panel.
 */
```

### Code Change Plan: Completion Handling

Change `menu_pollPresetStatus()`:

- Add `PRESET_OP_INSTRUMENT_SAVE` to the save-completion group.
- If nested Instrument Save remains active, clear busy state and repaint the
  nested save view instead of always calling the generic
  `menu_resetSaveParameters()`.
- Preserve error visibility if save request fails. Production saves currently
  do not get the File/Dir `ERR` overlay; decide whether to add an error
  display for Instrument Save in the same pass or leave that as a broader
  storage-error UI task.

Comment text:

```c
/*
 * Complete nested Instrument Save without leaving Instrument mode silently.
 *
 * The save operation does not change resident SceneData, so completion only
 * clears busy state and repaints the same source slot/name context. Generic
 * save reset is still used for top-level Kit Save.
 */
```

### Code Change Plan: Header/API Comments

Update public headers beside every new or changed entry:

- `filesystem.h`
  - `filesystem_requestSaveInstrument()`;
  - updated `filesystem_requestScanInstruments()` comment;
  - updated root-name policy if constants remain in `storageTypes.h`.
- `presetManager.h`
  - `PRESET_OP_INSTRUMENT_SAVE`;
  - `preset_saveInstrument()`.
- `menu.h`
  - update VOICE press Load/Save wording;
  - document whether Instrument Save uses existing nested state names or new
    neutral names.
- `storageTypes.h`
  - update `STORAGE_ROOT_INSTRUMENT`;
  - if any filename helper semantics change, update comments there.

### Test Plan For This Pass

Build checks:

- `git diff --check`;
- `make`;
- `make img`.

Filesystem fixture checks:

- Confirm root directories are named exactly `Kit` and `Instrument`.
- Confirm `Instrument` is not opened through `INSTRU~1`.
- Confirm `kitset.kcg` remains lower-case display on desktop.
- Confirm saved instrument files preserve expected case.

Hardware tests:

1. Boot and confirm `Load:[File]` / `Load:[Dir]` still work.
2. Switch to `Load:[Kit]`, scroll through populated and empty slots.
3. Load a populated Kit into the active Scene.
4. Load a populated Kit into a non-active selected Scene via SEQ selection.
5. Save a Kit into an empty slot; power off, mount card, verify folder and
   files.
6. Save a Kit into an occupied slot; verify it overwrites referenced children
   and does not create duplicate numbered folders.
7. Enter Instrument Load from `Load:[Kit]` or Load page VOICE press; load each
   type.
8. Enter Instrument Save from Save page VOICE press; save each type to
   root `Instrument/`.
9. Power off, mount card, verify saved Instrument files and contents.
10. Reboot and confirm saved root Instrument files appear in Instrument Load.

Success criteria:

- No duplicate visible Kit folder when saving an occupied slot.
- No empty folder after Kit Save.
- Root Instrument save files appear under exact `Instrument/`.
- Saved root Instrument files can be loaded back.
- Dot-prefixed files remain visible to generic diagnostics but are ignored by
  product scanners unless they match product naming/type rules.

### Work Order

Recommended implementation order:

1. Convert Instrument root constant and Instrument scan to object iterator.
2. Convert Kit scan to object iterator.
3. Change Kit/Instrument root opens to LFN-aware exact directory APIs.
4. Retest Kit Load and Instrument Load before touching writers.
5. Update Kit Save root creation to LFN-aware exact directory API.
6. Retest Kit Save.
7. Add filesystem root Instrument Save writer.
8. Add Preset Instrument Save wrapper/completion.
9. Add nested Instrument Save menu behavior.
10. Re-enable top-level Kit reachability.
11. Run hardware tests and update this document with pass/fail notes.

### End-of-Pass Status

The implementation pass has now completed steps 1 through 10 above in code and
completed the build checks from step 11.

Hardware retest is still pending. The specific hardware retest surface is:

- File/Dir diagnostics still scan/load/save exactly as before.
- `Load:[Kit]` appears in the top-level type cycler and loads Kit folders on
  slot movement.
- `Save:[Kit]` appears in the top-level type cycler and writes the active
  Scene kit to direct slot `000..999`.
- VOICE press on the Load page enters nested Instrument Load.
- VOICE press on the Save page enters nested Instrument Save and writes one
  resident voice to root `Instrument/`.

The earlier code-change-plan sections remain in this document as the audit
trail that drove the implementation. The authoritative code state is the
implementation-notes section above plus the current source comments adjacent to
the changed code.
