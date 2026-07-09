# Session 030 Handoff Log - Phase 2 Filesystem Start and Directory Kit Load

DATE: 2026-07-07

SESSION GOAL: Begin Phase 2 SD-card filesystem work. Write the new filesystem
spec, generate the new root `Kit/` directory from legacy `.SND` kits, implement
menu/startup kit loading from the new directory format, document the plan and
code changes, and update permanent project knowledge at session end.

COMPLETED: Wrote `FILESYSTEM_SPEC.md`, updated `SCOPING_TARGETS.md` to the
current Phase 2 filesystem model, generated `SD_CARD/Kit/` from the legacy
`Pxxx.SND` kits, implemented a new `storageTypes.c/h` storage-format parsing
layer, changed normal kit load to read `Kit/NNN Name/kitset.kcg` plus six
instrument files, fixed directory discovery in `asyncfatfs`/`filesystem.c`,
audited parameter landing against the converted kits, changed numbered folder
convention to preferred space separator with underscore compatibility, and added
detailed code comments to the changed code paths.

VERIFIED ON HARDWARE: Partly. User reported that menu and init kit load worked
after the directory discovery fixes. The final FAT short-alias fallback for
space-named folders built cleanly but still needs a direct hardware smoke test.

CHANGES THIS SESSION:
- `FILESYSTEM_SPEC.md`: Created the Phase 2 SD card layout spec. It defines
  root directories `Bank`, `Scene`, `Kit`, `Pattern`, `Sample`, `Wavetable`,
  `Effect`, `Instrument`; root `settings.cfg`; numbered folder behavior; bank,
  scene, kit, wavetable, pool, and user-copy rules. It now documents preferred
  numbered folder names as `NNN Name`, with `NNN_Name` accepted for
  compatibility.
- `SCOPING_TARGETS.md`: Updated Phase 2 to the current filesystem shape:
  `settings.cfg`, root library directories, numbered `Bank`/`Scene`/`Kit`/
  `Wavetable` folders, guard/config files `bankset.bcg`, `sceneset.scg`,
  `kitset.kcg`, and the future debounced autosave/dot-file plan. Also corrected
  Phase 1 text to reflect that Preset already moved to `Core/Scene/Preset`.
- `KIT_DIR_LOAD_AUDIT.md`: Created and maintained the detailed implementation
  audit for directory kit loading. It records design decisions, implementation
  phases, discovery fixes, parameter-mapping verification, detailed commenting
  pass, folder separator change, and the final short-alias fallback.
- `tools/convert_legacy_kits.py`: Added converter from root `Pxxx.SND` files to
  new `SD_CARD/Kit/NNN Kit Name/` directories. It writes `kitset.kcg` and six
  instrument files per kit. Instrument filenames use the first six alphanumeric
  chars of the kit name plus voice suffixes such as `slakd1.drm`. The generator
  now preserves spaces in folder names and emits the preferred `NNN Kit Name`
  convention.
- `SD_CARD/Kit/`: Generated the converted kit library. The tree now uses space
  folder names such as `001 Slak`, `004 Moch to`, etc. Each folder contains
  `kitset.kcg` plus six instrument files. The reduced `kitset.kcg` schema is a
  kit-folder guard plus six voice-slot manifest sections with instrument type,
  filename, and audio output; MIDI note/channel settings are intentionally
  absent.
- `Core/Hardware/SD/storageTypes.h`: Added the public storage-format boundary
  for Phase 2 kit loading. It declares storage constants, status codes,
  instrument type enum, kitset parse state, instrument parse state, parser
  functions, numbered-folder parsing, display-name copying, filename copying,
  instrument type conversion, filename/type validation, and morph fallback.
  Every public comment now explains why the symbol exists, inputs, outputs, and
  clients.
- `Core/Hardware/SD/storageTypes.c`: Added pure storage-format logic with no
  `asyncfatfs` calls. It owns the text schemas for `kitset.kcg` and instrument
  files, maps instrument keys to `ParameterArray` enum slots, validates file
  guards/version/type/slot/required fields, writes parsed bytes into
  `parameter_values[]`/`parameters2[]`, and copies main instrument values into
  morph values when no `[morph]` section exists. It also owns numbered folder
  parsing for `NNN Name` and compatibility `NNN_Name`.
- `Core/Hardware/SD/filesystem.h`: Added `filesystem_kitSlotExists()` and
  `filesystem_kitSlotName()` accessors for the Kit scan cache, with comments
  documenting zero-based internal slots vs. one-based SD folder prefixes.
- `Core/Hardware/SD/filesystem.c`: Added `storageTypes.h`, Kit directory scan
  cache, LFN scratch, text-line streaming scratch, parser scratch, directory kit
  load state machine, Kit scan state machine, name-load cache path for kits,
  scan-cache accessors, and initialization/reset handling for the new operation
  state. Normal `FS_INTERNAL_OP_LOAD_KIT` now dispatches to the directory kit
  loader. `FS_INTERNAL_OP_LOAD_MORPH` still dispatches to the legacy `.SND`
  loader.
- `Core/Hardware/SD/filesystem.c`: Changed `filesystem_requestScanKits()` to
  clear the new scan cache and legacy `kitBrowser` map, then scan the root
  `Kit/` directory instead of probing `P000.SND` through `P127.SND`. The scan
  records both display names and FAT short aliases because `asyncfatfs` opens by
  short name in the current directory.
- `Core/Hardware/SD/filesystem.c`: Added a FAT short-alias fallback for
  space-named folders. If LFN parsing is unavailable and a short alias such as
  `001SLA~1` is all the scanner sees, it now derives the slot from the leading
  three digits and records the alias as the open name. This preserves loadability
  even if the display name is ugly until `kitset.kcg` loads.
- `Core/Hardware/SD/asyncfatfs/asyncfatfs.c`: Updated
  `afatfs_fileLoadDirectoryEntry()` so opened handles set `file->type` from the
  actual FAT directory entry attribute. Without this, a directory opened by
  `afatfs_fopen()` could remain typed as a normal file and fail `chdir`/scan
  behavior.
- `Core/Menu/menu.c`: Updated Load page kit display to use one-based visible
  kit numbers and `filesystem_kitSlotName()` from the scan cache. The internal
  `menu_currentPresetNr[]` remains zero-based. Added detailed comments for the
  Load/Save request path, repaint path, and encoder/button handler. The function
  that initiates a root `Kit/` load from the Load page is
  `menu_requestCurrentLoadSaveSelection(1)`, which calls
  `preset_loadDrumset(slot, 0)`.
- `Makefile`: Added `Core/Hardware/SD/storageTypes.c` to the source list.
- `build/LXRV2_lxr02.img`: Existing generated image artifact changed during
  session builds. The source changes were verified with `make`; image generation
  was not the focus of the final check.
- `knowledge_files/log_archive/000_SESSION_INDEX.md`: Added Session 030 row,
  summary, and cross-session facts.
- `knowledge_files/log_archive/030_SESSION_HANDOFF_LOG.md`: Created this log.
- `knowledge_files/MODULE_INTERCHANGE_SPEC.md`: Updated to Session 030 baseline
  for `storageTypes`, filesystem Kit directory loading, and scan-cache accessors.
- `GLOBALS_STAGING_AUDIT.md`: Corrected moved Preset path references from
  `Core/Preset` to `Core/Scene/Preset`; no Phase 2 globals architecture was
  implemented this session.
- `STAGING_AUDIT.md`: Corrected moved Preset path references and added the new
  directory-kit parser/scan scratch to the audit as purposeful Phase 2
  filesystem state, not cleanup-target staging.
- `MEMORY.md`: Updated session pointer, repository tree, SD/filesystem notes,
  and added the standing reminder to comment new code at detailed contract
  level.

KNOWN ISSUES INTRODUCED:
- None known from build verification. The final FAT short-alias fallback still
  needs hardware confirmation.
- If a card exposes only FAT short aliases for `NNN Name` folders and no LFN,
  Load-page names may temporarily display an alias-derived fallback such as
  `SLA~1` until the kit is loaded and `kitset.kcg` supplies the real name. This
  is ugly but should be loadable.
- Normal kit save remains legacy/out of scope. The new directory save path,
  instrument morph persistence, bank/scene save, and dot-file backup system are
  future work.

KNOWN ISSUES RESOLVED:
- Root `Kit/` directory can now be scanned and loaded for normal kits instead of
  relying on flat `Pxxx.SND` files.
- The initial hardware symptom where every Load-menu kit slot was `Empty` was
  traced to directory handling and fixed by setting `file->type` from FAT
  attributes plus explicitly entering `Kit/` before scanning.
- Boot/startup normal kit load now targets `Kit/001 .../` through
  `preset_loadDrumset(0, 0)` after the startup kit scan.
- MIDI note/channel settings were removed from `kitset.kcg`; they are reserved
  for future scene settings.
- Parameter mapping from converted instrument files was checked against the
  legacy `.SND` data and matched for all fields owned by the new kit/instrument
  format.

NEXT SESSION RECOMMENDED GOAL: Hardware smoke-test the final space-folder
short-alias fallback, then start the new directory-browsing menu around root
`Kit/` loading if hardware confirms discovery/load behavior. After that, the
next Phase 2 implementation target should be scene settings ownership for MIDI
notes/channels or the save path for directory kits.

BLOCKERS:
- Hardware confirmation of the final fallback behavior is still needed.
- Directory kit save format and instrument morph persistence are intentionally
  not designed/implemented yet.
- Scene settings are not implemented, so `PAR_MIDI_NOTE1..7` remain untouched by
  directory kit loads even though they still live before
  `END_OF_SOUND_PARAMETERS`.

CRITICAL REMINDERS FOR NEXT SESSION:
- Normal kit load is directory-based; morph-kit load remains legacy `.SND` until
  instrument morph save/load is designed.
- `storageTypes.c/h` is the storage-format/parser layer. Keep it free of
  `asyncfatfs` calls and keep all functions prefixed `storage_`.
- Preferred numbered folder convention is `NNN Name`; `_` is compatibility only.
  The Kit scan also tolerates FAT short aliases beginning with a valid slot ID.
- New code should be commented at detailed contract level: why the function,
  variable, or storage type exists; what it does; inputs and outputs; and
  clients/accessors/affiliates.
- Do not move MIDI notes/channels back into `kitset.kcg`; they belong in future
  scene settings.

## Detailed Notes

### Filesystem Spec Decisions

The new root layout is:

```text
Bank/
Scene/
Kit/
Pattern/
Sample/
Wavetable/
Effect/
Instrument/
settings.cfg
```

The only recognized root-level file should eventually be `settings.cfg`,
replacing `GLO.CFG`/`glo.cfg`. `settings.cfg` will hold system-level settings
and a reference to the last loaded bank. The root directories are typed pools or
containers. Root entries outside the recognized set should be ignored by normal
loaders/browsers.

Numbered folders are used by `Bank`, `Scene`, `Kit`, and `Wavetable`. The
current preferred convention is:

```text
001 Slak
002 Hard
004 Moch to
```

An underscore after the three-digit prefix remains accepted for compatibility:

```text
001_Slak
```

The numeric prefix is the slot identity and ordering key. Slots do not need to
be contiguous. Browsers should show missing numbers as `Empty` rather than
collapsing gaps.

### Generated Kit Directory Format

The generated root `Kit/` folders contain:

```text
kitset.kcg
<six instrument files>
```

`kitset.kcg` records:

- `format=helicase.kitset`
- `version=1`
- six `[slotN]` sections with `type`, `file`, and `audio_out`

MIDI note and MIDI channel settings are deliberately absent. The decision during
the session was that those settings belong to scene settings, not kit settings.
Volume and pan are instrument-owned and are stored inside the instrument files.
Audio output is kitset-owned.

Instrument files contain:

- `format=helicase.instrument`
- `version=1`
- `type`
- `[params]` field/value lines

The initial generated instrument types are `.drm`, `.snr`, `.cym`, and `.hat`.
Instruments do not yet carry a `[morph]` section. The loader fallback copies the
loaded main instrument parameters into `parameters2[]` for that instrument when
no morph data is seen.

### Parameter Ownership And Mapping

The parameter map in `storageTypes.c` is deliberately explicit: one table per
instrument voice/type combination. Drum appears three times because the same
`.drm` schema maps to different `PAR_*` enum ranges for slots 1, 2, and 3.
Snare, cymbal, and hi-hat are constrained to slots 4, 5, and 6.

Mappings verified against `tools/convert_legacy_kits.py`:

- Drum slots 1-3: 35 fields each
- Snare slot 4: 34 fields
- Cymbal slot 5: 35 fields
- Hi-hat slot 6: 35 fields

The simulated load of all generated kits matched the legacy `.SND` bytes for
the parameters owned by the new kit/instrument format. Parameters intentionally
not loaded by the new format include:

- `PAR_NONE` / `PAR_MOD_WHEEL`
- `PAR_NRPN_DATA_ENTRY_COARSE`
- `PAR_NRPN_FINE`
- `PAR_NRPN_COARSE`
- `PAR_RESERVED4`
- `PAR_MIDI_NOTE1` through `PAR_MIDI_NOTE7`

The MIDI note fields are the only semantic wrinkle because they still live
before `END_OF_SOUND_PARAMETERS`, but the new architecture says they belong in
scene settings. Until scene loading exists, directory kit loads leave them
unchanged.

### Runtime Load Path

The normal kit load path is now:

1. Startup or menu calls `filesystem_requestScanKits()`.
2. `filesystem_scanKits_tick()` enters root, opens `Kit/`, `chdir`s into it,
   scans entries, reconstructs LFNs when available, and caches:
   - slot presence
   - eight-character display name
   - FAT short open name
3. Existing `kitBrowser` compatibility globals `kb_map[]` and `kb_numKits` are
   also populated from the scan.
4. `preset_loadDrumset(slot, 0)` requests `FS_INTERNAL_OP_LOAD_KIT`.
5. `filesystem_tick()` dispatches normal kit load to
   `filesystem_loadKitDirectory_tick()`.
6. The directory loader enters `Kit/`, opens the selected kit folder by cached
   short alias, parses `kitset.kcg`, then opens each listed instrument file.
7. Parsed bytes land directly in `parameter_values[]` and `parameters2[]`.
8. `preset_currentName` is set from the kitset display name on success.

Morph load remains intentionally legacy:

- `preset_loadDrumset(slot, 1)` requests `FS_INTERNAL_OP_LOAD_MORPH`.
- `filesystem_tick()` dispatches that to the existing `.SND` loader.
- This should change only when instrument morph save/load is designed.

### Menu Behavior

The Load page kit display changed in two important ways:

- Kit slot numbers are displayed one-based to match the SD folder prefix.
- Kit names are read from the scan cache, so missing slots show `Empty`.

Internal slots remain zero-based. The menu function that initiates a normal root
`Kit/` directory load from the Load page is:

```c
menu_requestCurrentLoadSaveSelection(1)
```

When active on `LOAD_PAGE` and `SAVE_TYPE_KIT`, it calls:

```c
preset_loadDrumset(slot, 0)
```

Save page behavior remains legacy/out of scope.

### Directory Discovery Fixes

The first hardware test after the directory loader showed every kit slot as
`Empty` and boot loaded zeroed parameters. The cause was not the generated
`SD_CARD/Kit` tree. Firmware-side fixes:

- `asyncfatfs` now sets `file->type` from the matched FAT directory entry, so
  opened directory handles are actually typed as directories.
- Kit scan now explicitly:
  - returns to root
  - opens `Kit`
  - enters `Kit`
  - scans entries there
  - closes the handle
  - returns to root

After those fixes, user reported menu and init load working.

### Folder Separator Regression And Fix

The preferred folder convention changed from `NNN_Name` to `NNN Name`, preserving
spaces in the visible eight-character name. The first implementation made the
visible parser strict but broke scan behavior on hardware, because the scanner
can sometimes receive only the FAT short alias. A space folder such as
`001 Slak` may be represented as an alias like `001SLA~1`, which has no space or
underscore separator and was rejected.

The final fix keeps the visible parser strict for real names but adds a scan
fallback:

- If `storage_parseNumberedFolder(display_name, ...)` fails,
  `filesystem_recordKitShortAlias(open_name)` tries the FAT short alias.
- It accepts aliases with leading `001` through `128` and a non-empty tail.
- It records the slot and open name so the folder can be loaded.

This means display names can be less pretty when only aliases are available, but
loading should work.

### Commenting Standard Established

A late user request called out that comments on the changed code were too thin.
The resulting pass added detailed comments in all touched C/H files covering:

- why the change/function/variable/storage type exists
- what it does
- inputs and outputs
- clients/accessors/affiliates
- why the boundary belongs in that file

This should be treated as the standard for future code changes, especially new
storage, parser, filesystem, menu, and cross-module boundary code.

### Verification

Commands run successfully:

```text
make
git diff --check
```

Expected build warnings remained:

- existing `asyncfatfs.c` unused `eraseCount` warning when that file recompiles
- existing newlib syscall linker warnings for `_close`, `_lseek`, `_read`, and
  `_write`

No new build failures were introduced.
