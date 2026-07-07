# Kit Directory Load Audit

## Goal

Implement kit loading from the new Phase 2 SD card layout:

```text
SD_CARD/
  Kit/
    001 Slak/
      kitset.kcg
      slakd1.drm
      slakd2.drm
      slakd3.drm
      slaks1.snr
      slakc1.cym
      slakh1.hat
```

The immediate target is the existing menu kit-load workflow. Selecting a kit from
the load menu should load `Kit/NNN <kit name>/kitset.kcg` plus the six instrument
files listed there, instead of loading the legacy flat `Pxxx.SND` file. The
scanner accepts either a space or underscore separator after the three-digit slot
ID, but generated folders now use the space convention.

This document is intentionally a plan only. No firmware code changes are made in
this pass.

## Current Decisions

- Instrument files own sound parameters for their voice, including `volume` and
  `pan`.
- `kitset.kcg` owns kit slot membership, instrument filenames, instrument types,
  `audio_out`, and kit-level metadata.
- MIDI note and MIDI channel settings do not belong in `kitset.kcg`; they should
  live in scene settings later.
- Users should not be expected to hand-author a valid kit folder. `kitset.kcg`
  hardcodes the filenames that define the kit. Users may copy instrument files out
  to the root `Instrument/` pool.
- The first generated kit directory is `Kit/001 Slak/`, and the first instrument
  is `slakd1.drm`.
- Any functions introduced in `storageTypes.c/.h`, or moved there from
  `filesystem.c`, must use the `storage_` prefix. This keeps the layer boundary
  visible at call sites.
- Instruments should eventually contain their own morph data. For the first
  directory-load implementation, do not design the full morph-file format yet.
  If an instrument does not contain explicit morph data, the loader should copy
  the loaded main parameters into the morph parameter buffer as a fallback.

## Existing Code Path

### Boot

`main.c` currently performs this sequence before audio starts:

1. `filesystem_initCardAndMountBlocking()`
2. `menu_init()`
3. `filesystem_requestScanKits(NULL)` and blocking `filesystem_tick()`
4. `preset_loadDrumset(0, 0)` and blocking `filesystem_tick()`
5. `menu_pollPresetStatus()` to apply the loaded kit
6. `preset_loadGlobals()`

The boot kit load still targets slot 0, which maps to legacy `p000.snd` through
`filesystem_makeFilename()`.

### Menu

`menu.c` still treats the load/save UI as a flat numbered-slot browser:

- `menu_currentPresetNr[SAVE_TYPE_KIT]` holds the selected kit slot.
- `menu_requestCurrentLoadSaveSelection()` calls `preset_loadDrumset(slot, 0)` on
  the load page when the selected type is kit.
- `menu_handleLoadSaveMenu()` increments/decrements a slot number from 0 to 125.
- `menu_repaintLoadSavePage()` displays the slot number and `preset_currentName`.
- `menu_pollPresetStatus()` handles `PRESET_OP_KIT_LOAD`, normalizes mod target
  fields, and starts the chunked sound apply.

There is currently a special behavior for kits and morph kits: on the load page,
scrolling to a different kit slot can start a kit load immediately rather than
only loading the name for display. This behavior predates the new folder model.
For the first directory-loader implementation, the conservative choice is to
preserve the existing behavior for `SAVE_TYPE_KIT` so the UI contract changes as
little as possible. A later load/save UI redesign can switch to explicit OK.

### Preset Manager

`preset_loadDrumset(uint8_t presetNr, uint8_t isMorph)` maps:

- normal kit load -> `FS_FILE_KIT`, `FS_INTERNAL_OP_LOAD_KIT`
- morph kit load -> `FS_FILE_MORPH`, `FS_INTERNAL_OP_LOAD_MORPH`

The filesystem writes loaded kit bytes directly into:

- `parameter_values[]` for normal kits
- `parameters2[]` for morph kits

After completion, menu code applies modulation routing one voice at a time via
`preset_startDrumsetApply()` / `preset_tickDrumsetApply()`.

### Filesystem

`filesystem.c` currently has one large responsibility set:

- SD/card mount and unsupported FAT detection
- public async operation facade
- path/name construction for legacy flat files
- payload serialization for kits, morph kits, patterns, globals, all/performance
  containers
- directory scanning for kit slots
- blocking sample/loop import and WAV parsing

The current kit load is in `filesystem_loadKit_tick()`:

1. Build `pNNN.snd`
2. Open it
3. Read 8-byte name into `preset_currentName`
4. Read `END_OF_SOUND_PARAMETERS` bytes into `parameter_values[]` or
   `parameters2[]`
5. Close
6. Report done

The current kit scan is in `filesystem_scanKits_tick()`:

- scans slots 0..127
- tries to open `pNNN.snd`
- writes present slots directly into `kitBrowser.c` globals `kb_map[]` and
  `kb_numKits`

`kitBrowser.c` itself appears effectively unused by the current menu. The only
live references to `kitBrowser_*()` are its own declarations/definitions. The
current load/save page uses `menu_currentPresetNr[]` and `preset_loadDrumset()`
directly.

## asyncfatfs Directory Constraints

`afatfs_fopen()` does not support path strings. It opens a filename in the current
working directory. The asyncfatfs comment says paths in the filename are not
supported.

Available primitives:

- `afatfs_fopen(".", "r", cb)` opens the current directory.
- `afatfs_fopen("Kit", "r", cb)` opens a directory entry by name if the current
  directory is root.
- `afatfs_chdir(handle)` changes the current working directory to an opened
  directory handle.
- `afatfs_chdir(NULL)` changes back to root.
- `afatfs_findFirst()` / `afatfs_findNext()` / `afatfs_findLast()` can iterate a
  directory.
- `afatfs_mkdir()` exists but is not needed for this first load-only path.

Therefore the new kit loader must be a directory-walking state machine, not a
single `afatfs_fopen("Kit/001_Slak/kitset.kcg", ...)`.

## Generated Kit Format Today

The generated text files are currently simple ASCII key/value files.

Example `kitset.kcg`:

```text
format=helicase.kitset
version=1
kit_name=Slak
source_name=Slak
source_file=P000.SND
legacy_slot=0
voice_decimation_all=127

[slot1]
type=drm
file=slakd1.drm
audio_out=0
...
```

Example instrument:

```text
format=helicase.instrument
version=1
type=drm
slot=1
kit_name=Slak
source_name=Slak
source_file=P000.SND

[params]
osc_wave=0
coarse=23
fine=127
...
volume=127
pan=66
```

The first loader can parse this format directly. It does not need a general INI
parser, but it should use one bounded line buffer and explicit key tables rather
than ad hoc substring matching across the whole file.

## Required Data Mapping

### Kitset Fields

`kitset.kcg` should load:

- display name into `preset_currentName`, padded/truncated to 8 chars
- `voice_decimation_all` into `PAR_VOICE_DECIMATION_ALL`
- for each slot 1..6:
  - `type` must match the expected extension or the loader fails
  - `file` is the instrument filename to open
  - `audio_out` maps to `PAR_AUDIO_OUT1`..`PAR_AUDIO_OUT6`

`kitset.kcg` should not load:

- MIDI note
- MIDI channel
- scene-level data
- pattern/effect data

### Instrument Files

Each instrument file loads one voice's sound parameters into `parameter_values[]`
or `parameters2[]`, depending on whether the operation is a normal kit or morph
load.

Slot mapping:

- slot 1 -> `.drm` -> voice 1 parameter IDs
- slot 2 -> `.drm` -> voice 2 parameter IDs
- slot 3 -> `.drm` -> voice 3 parameter IDs
- slot 4 -> `.snr` -> voice 4/snare parameter IDs
- slot 5 -> `.cym` -> voice 5/cymbal parameter IDs
- slot 6 -> `.hat` -> voice 6/hi-hat parameter IDs

`volume` and `pan` stay in the instrument files and map to `PAR_VOLn` and
`PAR_PANn`.

MIDI note/channel values from the legacy `.SND` conversion should be ignored in
the kit loader because they are moving to scene settings.

### Morph Kits

Instruments should eventually contain their own morph data, but that full format
belongs with the save-function pass. Do not block the directory kit loader on
that design.

For this pass, implement a fallback rule:

- normal kit directory loads populate `parameter_values[]`
- if the instrument file contains explicit morph data later, it should populate
  `parameters2[]`
- if no explicit morph data is detected, copy the loaded values for that
  instrument into the corresponding morph parameter range in `parameters2[]`

This makes the normal kit loader produce a complete main+morph parameter set even
before instrument-level morph storage is implemented. It also avoids imposing a
separate morph-folder convention now.

`FS_FILE_MORPH` can remain on the legacy flat `.snd` path until the save pass or
scene/bank redesign gives it a clearer role. The important requirement for the
new directory loader is that a loaded kit does not leave stale morph parameters
behind when the new instrument files have no morph section.

## Proposed `storageTypes.c/.h` Split

Create `Core/Hardware/SD/storageTypes.h` and `Core/Hardware/SD/storageTypes.c`.

Purpose: define storage formats, path/folder naming rules, parser tables, and
payload encode/decode helpers. It should not own SD card state and should not call
`afatfs_*` directly.

`filesystem.c` should remain the access/pump layer:

- mount card
- open/close/read/write/chdir/findNext state machines
- dispatch one active operation at a time
- expose public request APIs

`storageTypes` should own:

- root directory names:
  - `Kit`
  - later `Bank`, `Scene`, `Pattern`, `Sample`, `Wavetable`, `Effect`,
    `Instrument`
- fixed file names:
  - `kitset.kcg`
  - later `settings.cfg`, `bankset.bcg`, `sceneset.scg`, `pattern.pat`,
    `effect.fx`
- extension/type rules:
  - `.drm`, `.snr`, `.cym`, `.hat`
- numbered folder parsing:
  - detect `NNN_`
  - convert displayed slot number to zero-based/one-based model consistently
  - extract display name after the underscore
  - validate range
- kitset parser tables:
  - required top-level keys
  - required `[slotN]` keys
  - mapping `slotN.audio_out` -> `PAR_AUDIO_OUTn`
  - mapping slot filenames/types to instrument type
- instrument parser tables:
  - `drm` key -> per-slot parameter IDs
  - `snr` key -> parameter IDs
  - `cym` key -> parameter IDs
  - `hat` key -> parameter IDs
- text parsing helpers:
  - trim CR/LF
  - split `key=value`
  - parse unsigned decimal 0..255
  - parse section names like `[slot1]` and `[params]`
  - compare keys without dynamic allocation
  - sanitize loaded display name to 8 characters
- payload apply helpers:
  - apply one parsed kitset key/value to a target parameter buffer
  - apply one parsed instrument key/value to a target parameter buffer
  - validate required keys/sections at EOF

Suggested public API shape:

```c
typedef enum {
    STORAGE_STATUS_OK = 0,
    STORAGE_STATUS_INVALID_FORMAT,
    STORAGE_STATUS_UNSUPPORTED_VERSION,
    STORAGE_STATUS_BAD_TYPE,
    STORAGE_STATUS_BAD_SLOT,
    STORAGE_STATUS_BAD_VALUE,
    STORAGE_STATUS_MISSING_REQUIRED,
} storage_status_t;

typedef enum {
    STORAGE_INSTRUMENT_DRM = 0,
    STORAGE_INSTRUMENT_SNR,
    STORAGE_INSTRUMENT_CYM,
    STORAGE_INSTRUMENT_HAT,
} storage_instrument_type_t;

#define STORAGE_KIT_SLOT_COUNT 6u
#define STORAGE_KIT_FILENAME_MAX 13u
#define STORAGE_KIT_DISPLAY_NAME_LEN 8u

typedef struct {
    char display_name[STORAGE_KIT_DISPLAY_NAME_LEN];
    char instrument_file[STORAGE_KIT_SLOT_COUNT][STORAGE_KIT_FILENAME_MAX];
    storage_instrument_type_t instrument_type[STORAGE_KIT_SLOT_COUNT];
    uint8_t seen_slot_mask;
    uint8_t seen_audio_out_mask;
} storage_kitset_t;

void storage_kitsetInit(storage_kitset_t *kit);
storage_status_t storage_kitsetParseLine(storage_kitset_t *kit,
                                         const char *line,
                                         uint8_t *target_values);
storage_status_t storage_kitsetFinalize(const storage_kitset_t *kit);

storage_status_t storage_instrumentParseLine(storage_instrument_type_t type,
                                             uint8_t slot,
                                             const char *line,
                                             uint8_t *target_values);

uint8_t storage_parseNumberedFolder(const char *name,
                                    uint8_t *slot,
                                    char display[STORAGE_KIT_DISPLAY_NAME_LEN]);
void storage_makeKitFolderName(char *dst, uint8_t slot,
                               const char *known_name_if_cached);
```

The final exact function names can differ, but the boundary should stay: pure
format/path semantics in `storageTypes`, SD operation state in `filesystem`.
Every function exposed by, or moved into, this layer should use the `storage_`
prefix.

### Why Not Put Parsing in `filesystem.c`

`filesystem.c` is already too long and currently mixes multiple layers:

- file access
- serialized format definitions
- format compatibility policy
- path generation
- sample manifest sorting
- UI-facing loaded-name state

Adding directory kit parsing inline would make it harder to later add `Bank`,
`Scene`, root `Instrument`, and backup/dot-file behavior. A split now keeps the
new file model testable and readable.

## Later `backupSystem.c/.h` Boundary

Do not implement this in the kit loader pass, but design the split so it has an
obvious home later.

Future `backupSystem` should own:

- dot-file names
- `.tmp` write names
- write-live-via-temp behavior
- restore live file from dot-shadow
- create/update dot-shadow on explicit save
- stale/dirty flags and idle debounce policy

It should call filesystem access primitives, but it should not parse kitset or
instrument payloads. It should sit above `filesystem` and below scene/preset
ownership.

The first kit-directory loader should avoid embedding dot-file assumptions in the
parser or path code. It should simply open current live files.

## Proposed `filesystem.c` Documentation Pass

Before adding the directory loader, do a documentation pass over existing static
functions. This should be comments only unless a signature must move later.

The goal is not decorative comments. Each comment should answer:

- what layer owns this function
- who calls it
- what state it mutates
- whether it is async tick-only, boot-blocking-only, or pure helper
- what its failure mode means
- any real-time constraints

Functions needing comments or comment upgrades:

- `filesystem_desc()` - descriptor lookup for legacy flat file types.
- `filesystem_isPowerOfTwoU8()` - FAT geometry helper.
- `filesystem_hasFatBootSignature()` - FAT/MBR signature helper.
- `filesystem_isPlausibleFatVolume()` - mount preflight helper.
- `filesystem_sectorLooksLikeExFat()` - unsupported-card detection.
- `filesystem_metaPaddingIsFF()` - all/performance meta validator.
- `filesystem_metaHasStoredGlobalsLen()` - inferred globals-length validator.
- `filesystem_resetGlobalsToDefaults()` - globals safe-default baseline.
- `filesystem_sanitizeLoadedGlobals()` - post-load global clamp.
- `filesystem_applyGlobalsPrefix()` - trusted globals prefix copy.
- `filesystem_applyLegacy22Globals()` - legacy compatibility case.
- `filesystem_staleGlobalsPrefixLimit()` - stale fallback boundary.
- `filesystem_staleMetaPrefixLen()` - all-file stale meta inference.
- `filesystem_applyStaleGlobalsFallback()` - warning/fallback behavior.
- `filesystem_detectUnsupportedCardLayout()` - boot-only raw-sector card check.
- `filesystem_makeFilename()` - legacy flat 8.3 path construction; should be
  marked as legacy and not used for new directory paths.
- `filesystem_morphSaveUsesBase()` - morph save exception list.
- `filesystem_patternStepAddress()` - stream index -> PatternData coordinates.
- `filesystem_patternTrackAddress()` - stream index -> pattern/track.
- `filesystem_packStep()` / `filesystem_unpackStep()` - legacy pattern record
  byte order.
- `filesystem_writeStreamChunk()` / `filesystem_readStreamChunk()` - partial
  async record transfer helpers.
- `filesystem_finish()` - operation completion/one-shot callback handoff.
- `filesystem_loadKit_tick()` - legacy flat `.snd` kit loader; should be marked
  legacy before replacing or splitting it.
- `filesystem_saveKit_tick()` - legacy flat `.snd` kit/morph saver.
- `filesystem_savePattern_tick()` - streamed pattern writer.
- `filesystem_loadPattern_tick()` - streamed pattern reader and staging logic.
- `filesystem_saveContainer_tick()` - legacy `.all`/`.prf` writer.
- `filesystem_loadContainer_tick()` - legacy `.all`/`.prf` reader.
- `filesystem_loadGlobals_tick()` - legacy `glo.cfg` reader.
- `filesystem_saveGlobals_tick()` - legacy `glo.cfg` writer.
- `filesystem_scanKits_tick()` - legacy flat `Pxxx.SND` scanner; to be replaced
  or redirected for `Kit/NNN_<name>`.
- `filesystem_loadName_tick()` - legacy 8-byte header name reader; to be
  replaced or specialized for kit directories.
- Blocking sample helpers from `filesystem_blockOpenCb()` through
  `filesystem_installSampleFolderBlocking()` - should be explicitly marked
  boot/menu blocking sample-install path, not general async filesystem facade.
- Public functions from `filesystem_initAfterCardReady()` through
  `filesystem_requestLoadName()` - each should document sync/async behavior and
  allowed caller context.

This pass can happen either immediately before the code changes or as the first
commit of the implementation session. It should not alter behavior.

## Implementation Plan

The first code implementation should run through Step 8 only. That creates enough
firmware behavior to test directory kit scanning, menu loading, boot loading, and
main-to-morph fallback. Steps 9 and 10 stay documented here, but Step 9 should
not be implemented in the first code pass.

### Step 1 - Add `storageTypes.c/.h`

Files:

- add `Core/Hardware/SD/storageTypes.h`
- add `Core/Hardware/SD/storageTypes.c`
- update `Makefile` `SRCS`

Content:

- type enums and parser status enum
- constants for root `Kit`, `kitset.kcg`, slot count, max filename length
- parser state structs
- kitset line parser
- instrument line parser
- instrument morph-detection state, even if the only implemented behavior is
  "no morph data seen"
- numbered folder parser
- mapping tables from text keys to parameter IDs

Constraints:

- every function in this layer uses the `storage_` prefix
- no dynamic memory
- bounded line buffers
- no recursion
- no blocking SD access inside `storageTypes`
- no includes of `asyncfatfs.h`

### Step 2 - Introduce Directory Kit State in `filesystem.c`

Add state for directory traversal and parsing:

- `afatfsFilePtr_t op_dir_file`
- `afatfsFilePtr_t op_kit_root_dir`
- `afatfsFilePtr_t op_kit_slot_dir`
- `afatfsFinder_t op_finder`
- `storage_kitset_t op_kitset`
- storage/instrument parser state needed to know whether explicit morph data was
  seen for the current instrument
- `uint8_t op_kit_slot_index`
- `uint8_t op_line_len`
- `char op_line_buf[...]`
- `char op_selected_kit_dir[13]` or long-name equivalent if LFN support is
  implemented for folder display

Need to decide whether the first implementation supports only FAT 8.3 directory
names or also long file names. The generated folders like `024_SeaWaked` exceed
8.3, and macOS/FAT will create LFN entries. Therefore the loader should support
LFN scanning for kit folders, or the generated SD tree should be renamed to strict
8.3. Since the spec uses `001_<name>`, LFN support is the correct direction.

Useful existing code:

- `filesystem_lfnReset()`
- `filesystem_lfnAppendEntry()`
- `filesystem_applyFatShortNameCase()`
- `fat_convertFATStyleToFilename()`

These currently live in the sample section. Move or duplicate the generic LFN
helpers into `storageTypes` or an internal filesystem directory-helper section so
kit scanning can reuse them. Prefer moving pure name helpers into `storageTypes`.

### Step 3 - Replace Kit Scan

Replace `filesystem_scanKits_tick()` behavior:

Current:

- iterate slots 0..127
- open `pNNN.snd`
- write present flat slots into `kb_map[]`

New:

- open root `Kit` directory
- iterate entries with `afatfs_findNext()`
- accept directories matching `NNN_<name>`
- for each matching directory:
  - optionally verify `kitset.kcg` exists, or defer validation to load
  - record slot and display name in a new kit browser/map owned by filesystem or
    `kitBrowser`
- missing numbers remain empty in the load UI

Recommended data model:

- stop writing directly into `kitBrowser.c` globals from `filesystem.c`
- either:
  - move kit scan map into filesystem as public read-only accessors, or
  - add `kitBrowser_reset()` / `kitBrowser_registerKit(slot, name)` functions
    and keep the map private to `kitBrowser.c`

Because the current menu does not use `kitBrowser_*()` at all, the cleaner short
term is to keep scan results in filesystem and provide:

```c
uint8_t filesystem_kitSlotExists(uint8_t zero_based_slot);
const char *filesystem_kitSlotName(uint8_t zero_based_slot);
```

Then the load/save UI can show names without posting a file read for every scroll.
The longer-term load/save UI redesign can decide whether `kitBrowser.c` survives.

Slot numbering choice:

- filesystem public APIs currently use zero-based slots (`0` -> `P000.SND`)
- new folders are one-based (`001_Slak`)
- for least menu churn, keep the public request slot zero-based for now:
  - `slot 0` maps to folder prefix `001_`
  - displayed number can remain `000` temporarily or be updated to `001`

Recommendation: update display to one-based for kits as part of this change,
because folder prefixes are one-based and the user-facing spec says `001_<name>`.
Internally, convert at the filesystem boundary.

### Step 4 - Replace Kit Name Load

Current `filesystem_loadName_tick()` reads the first 8 bytes from `Pxxx.SND`.

For `FS_FILE_KIT`, name load should no longer open a file payload. Options:

1. Use the scan cache's folder display name (`001_Slak` -> `Slak`) and complete
   immediately.
2. Open `kitset.kcg` and parse `kit_name`.

Recommended first implementation: use scan cache. It avoids extra directory
walking while the user scrolls and keeps `filesystem_requestLoadName()` cheap.
Full validation still happens during actual load.

If scan cache is unavailable, fall back to opening `Kit/NNN_*/kitset.kcg` and
parsing `kit_name`, or return `Empty`.

### Step 5 - Add Directory Kit Loader State Machine

Replace or branch `filesystem_loadKit_tick()` for `FS_FILE_KIT`.

Recommended structure:

- keep old `filesystem_loadKit_tick()` renamed conceptually as
  `filesystem_loadLegacySoundFile_tick()` for `FS_FILE_MORPH` and any temporary
  legacy compatibility
- add `filesystem_loadKitDirectory_tick()` for `FS_FILE_KIT`

High-level phases:

1. `ROOT`
   - `afatfs_chdir(NULL)`
2. `OPEN_KIT_ROOT`
   - `afatfs_fopen("Kit", "r", on_file_opened)`
3. `WAIT_KIT_ROOT`
   - fail with `Empty`/error if missing
4. `CHDIR_KIT_ROOT`
   - `afatfs_chdir(op_kit_root_dir)`
5. `FIND_KIT_SLOT`
   - iterate directory entries until folder prefix matches requested slot
   - support LFN display names
   - store exact openable short filename or LFN filename as needed
6. `OPEN_KIT_DIR`
   - open the matched folder name
7. `WAIT_KIT_DIR`
8. `CHDIR_KIT_DIR`
9. `OPEN_KITSET`
   - open `kitset.kcg`
10. `READ_KITSET_LINES`
    - stream bytes into bounded line buffer
    - call `storage_kitsetParseLine()`
11. `CLOSE_KITSET`
12. for slots 1..6:
    - `OPEN_INSTRUMENT`
    - `READ_INSTRUMENT_LINES`
    - `CLOSE_INSTRUMENT`
    - validate file type/slot/type compatibility
    - if no explicit morph section/data was detected, copy that slot's loaded
      main parameter range into the matching morph range in `parameters2[]`
13. `ROOT_AND_CLOSE_DIRS`
    - chdir root
    - close any open directory handles
14. `DONE`
    - set `preset_currentName`
    - `filesystem_finish(FS_STATUS_DONE)`

Failure handling:

- on missing `Kit/`, missing selected folder, missing `kitset.kcg`, malformed
  kitset, missing instrument file, malformed instrument file, or wrong type:
  - close open file handles where possible
  - return to root with `afatfs_chdir(NULL)`
  - set `preset_currentName` to `-       ` or `Empty   ` depending on whether
    the folder exists but is invalid vs. absent
  - `filesystem_finish(FS_STATUS_ERROR)`

Real-time behavior:

- parse at most one chunk/line per `filesystem_tick()` pass
- keep line buffer small, e.g. 80 or 96 bytes; generated lines are short
- do not stage all six instrument files in RAM
- no blocking loops in runtime path

### Step 6 - Preserve Normal Post-Load Apply

The loader should still populate `parameter_values[]`. It should also ensure
`parameters2[]` is valid for the loaded kit by using the instrument morph
fallback described above. Then existing code can continue to do:

- `menu_normalizeSoundModTargets(parameter_values)`
- `menu_startSoundApply(...)`
- `preset_applyVelocityModTarget()`
- `preset_applyLfoModTarget()`
- `preset_morph(parameter_values[PAR_MORPH])`

This is the least risky integration: only the file source changes, not the sound
apply pipeline.

### Step 7 - Menu Adjustments

Minimum required menu changes:

- when selected type is kit, display one-based folder slot (`001`, `002`, etc.)
  or deliberately keep zero-based with a comment explaining the temporary mismatch
- when `filesystem_kitSlotExists(slot)` is false, show `Empty`
- when selected kit slot exists, show cached display name from scan
- keep existing immediate load-on-scroll behavior for now, unless it proves too
  slow or surprising with directory kits

Potential improvement:

- on load page, make kit behave like other types: scroll name only, click OK to
  load. This is more predictable for directory loads, but it changes existing
  behavior. Defer unless requested.

### Step 8 - Boot Adjustments

Boot currently loads kit slot 0 unconditionally after scan.

For this pass:

- `preset_loadDrumset(0, 0)` should load `Kit/001_<name>/...`
- if no directory kit exists, boot should not hard-fail; it should leave defaults
  or show existing missing-card/missing-kit behavior

Later, when `settings.cfg` lands:

- boot should load the last bank from `settings.cfg`
- kit boot load will be inside scene/bank load, not a standalone root kit load

This is the final scope point for the first code implementation. At the end of
Step 8, the firmware should be buildable and testable with directory kit loading
from the menu and boot path. Directory kit saving remains intentionally out of
scope.

### Step 9 - Save Path

Do not implement directory kit save in this first loader pass unless required.

Current `preset_saveDrumset()` and `filesystem_saveKit_tick()` still write flat
`.snd` files. That will be wrong once the UI exposes save for the new layout.

Options:

- disable kit save temporarily for directory kits and show an error/no-op
- keep legacy save only for `FS_FILE_MORPH`
- implement directory kit save alongside load

Recommendation: for a load-only implementation, explicitly guard `SAVE_TYPE_KIT`
save so it does not overwrite legacy `Pxxx.SND` silently. Either disable it or
leave it unchanged only with a visible "legacy save path" comment and a follow-up
task. Silent legacy writes would be confusing after the load path changes.

### Step 10 - Tests and Verification

Firmware-level verification:

- `make`
- check `build/lxr02.map` only if new buffers are introduced

Filesystem behavior tests on hardware:

- boot with current `SD_CARD/Kit/001_Slak`
- verify boot loads `Slak` through new directory path
- open load page, select kit slots 1, 2, 3
- verify missing slot behavior after temporarily removing/renaming one kit folder
- corrupt `kitset.kcg` format and verify error path does not hang filesystem
- remove one listed instrument file and verify error path returns to root
- verify legacy `.SND` files are no longer opened for normal kit load
- verify directory kit load copies main instrument parameters into the matching
  morph parameter ranges when no explicit morph data is present
- verify legacy morph kit path still behaves as intentionally chosen
- verify audio remains stable while scrolling/loading after audio has started

Host-side sanity checks:

- script can assert every `kitset.kcg` lists six existing files
- script can assert no `midi_note`/`midi_channel` keys in kitsets
- script can assert all instrument files contain `volume` and `pan`

## Risk Areas

- **LFN folder support:** generated folder names such as `024_SeaWaked` require
  LFN handling. The existing sample scanner has partial LFN support that can be
  reused, but the kit loader must open the actual directory entry reliably.
- **Current-directory global state:** `afatfs_chdir()` changes a global current
  directory. Every success and failure path must return to root or later file
  operations will open in the wrong directory.
- **Open file handles:** directory handles and file handles must be closed in all
  paths. `afatfs_chdir()` copies the directory handle into currentDirectory, so
  the original handle can be closed after a successful chdir, but code must be
  explicit and consistent.
- **Single operation pump:** no nested filesystem operations from menu/preset while
  a directory load is active.
- **Line parser robustness:** text files are human-readable but can be malformed.
  Missing required keys, overlong lines, invalid values, wrong sections, and wrong
  types need deterministic failures.
- **Legacy save mismatch:** changing load without changing save can confuse users.
  The implementation should either disable kit save or document and visibly retain
  legacy save until directory save is implemented.
- **Dead `kitBrowser` path:** because `kitBrowser_*()` is currently unused, updating
  it alone will not affect the load/save page. The implementation must update the
  actual `menu.c` path.

## Suggested Commit Order

1. Comment/document `filesystem.c` existing functions, no behavior change.
2. Add `storageTypes.c/.h` with parser/path types and unit-sized helpers, wire into
   Makefile, no behavior change.
3. Add kit directory scan/name cache behind existing `filesystem_requestScanKits()`
   and `filesystem_requestLoadName(FS_FILE_KIT, ...)`, keep legacy load unchanged.
4. Add directory kit load state machine for `FS_FILE_KIT`, including main-to-morph
   fallback when instrument files contain no explicit morph data. Leave
   `FS_FILE_MORPH` legacy until the save/scene pass gives it a clearer role.
5. Adjust menu display/slot numbering and guard or disable kit save as decided.
6. Build and hardware-test boot/load behavior.

## Implementation Notes

### 2026-07-06 Pass 1

- Added `Core/Hardware/SD/storageTypes.h` and `storageTypes.c`.
- All new storage-layer functions use the `storage_` prefix.
- `storageTypes` currently owns:
  - root/file constants for `Kit` and `kitset.kcg`
  - numbered folder parsing for `NNN_<name>`
  - kitset parser state
  - instrument parser state
  - instrument type and extension validation
  - generated-file parameter key mapping for `.drm`, `.snr`, `.cym`, and `.hat`
  - main-to-morph fallback copying for instruments with no explicit morph data
- Wired `storageTypes.c` into `Makefile`.
- Added a filesystem kit slot cache:
  - `kit_slot_present[]`
  - cached 8-character display names
  - cached FAT 8.3 open names
- Replaced normal kit scanning with a `Kit/` directory scan.
  - LFN entries are used for display/parsing of `NNN_<name>`.
  - The short FAT alias from the real directory entry is cached for
    `afatfs_fopen()`, because asyncfatfs does not open path strings or LFNs.
  - `kitBrowser` globals are still populated for compatibility, although the
    load/save menu uses filesystem cache accessors.
- Added `filesystem_kitSlotExists()` and `filesystem_kitSlotName()`.
- Changed `filesystem_requestLoadName(FS_FILE_KIT, ...)` to complete from the
  scan cache instead of reading a legacy `.SND` name header.
- Added `filesystem_loadKitDirectory_tick()` for `FS_FILE_KIT`.
  - `FS_FILE_MORPH` still uses the legacy flat `.SND` loader.
  - The loader walks `Kit/`, then the cached kit folder, then parses
    `kitset.kcg`, then parses the six instrument files listed in the kitset.
  - If an instrument has no `[morph]` data, the loader copies that instrument's
    loaded main parameters into `parameters2[]`.
- Updated `filesystem_tick()` so normal kits dispatch to the directory loader
  and morph kits dispatch to the legacy loader.
- Updated the Load page display for normal kits:
  - shows one-based kit numbers (`001`, `002`, ...)
  - shows cached folder names or `Empty`
  - permits kit slots through zero-based internal slot `127`, displayed as `128`
- Save path intentionally remains legacy/out of scope for this pass.
- Verification:
  - `make` succeeds.
  - `git diff --check` succeeds.
  - Linker still emits the existing/newlib syscall warnings for `_close`,
    `_lseek`, `_read`, and `_write`; these are unrelated to the kit loader.
- Hardware still needs to confirm:
  - asyncfatfs can open the cached short aliases for long kit folder names on
    the target SD card
  - boot loads `Kit/001_Slak/`
  - scrolling the Load page shows cached names and `Empty` for missing folders
  - directory kit load leaves morph behavior sane through the fallback copy

### 2026-07-07 Discovery Fix

- Hardware symptom: with `Kit/` on the SD card root, the Load menu showed every
  kit slot as `Empty`, and boot loaded zeroed parameters. That means the scan
  cache stayed empty, so `preset_loadDrumset(0, 0)` failed before opening
  `kitset.kcg`.
- The generated SD tree is correctly shaped: `SD_CARD/Kit/001_Slak/kitset.kcg`
  exists and matches the naming convention.
- Likely firmware-side cause:
  - `filesystem_scanKits_tick()` opened `Kit` and iterated that handle directly
    without explicitly entering `Kit/`.
  - `asyncfatfs` also did not update an opened handle's `file->type` from the
    matched FAT directory entry. A directory opened via `afatfs_fopen()` could
    therefore remain typed like the requested/create attribute instead of the
    actual on-card entry.
- Fixes made:
  - `afatfs_fileLoadDirectoryEntry()` now sets `file->type` from
    `FAT_FILE_ATTRIBUTE_DIRECTORY`.
  - kit scanning now explicitly:
    - `chdir` to root
    - opens `Kit`
    - `chdir`s into `Kit`
    - scans entries there
    - closes the handle
    - returns to root before finishing
- Verification:
  - `make` succeeds.
  - `git diff --check` succeeds.
  - Build output includes an existing `asyncfatfs.c` unused-parameter warning
    for `eraseCount` when that file recompiles, plus the existing/newlib syscall
    linker warnings. These are unrelated to kit discovery.

### 2026-07-07 Parameter Mapping Audit

- Compared `storageTypes.c` parameter maps against `tools/convert_legacy_kits.py`.
  The field counts and ordered enum targets match exactly:
  - drum slots 1-3: 35 fields each
  - snare slot 4: 34 fields
  - cymbal slot 5: 35 fields
  - hi-hat slot 6: 35 fields
- Simulated loading every generated `SD_CARD/Kit/*` folder and compared the
  resulting intended kit parameter bytes against the source `Pxxx.SND` files.
  Result: no mismatches across all generated kits for the parameters owned by
  the new kit/instrument format.
- Parameters loaded from `kitset.kcg`:
  - `PAR_VOICE_DECIMATION_ALL`
  - `PAR_AUDIO_OUT1` through `PAR_AUDIO_OUT6`
- Parameters loaded from instrument files:
  - all per-voice oscillator/filter/envelope/LFO/velocity/transient/volume/pan
    fields emitted by the converter for `.drm`, `.snr`, `.cym`, and `.hat`
- Sound-parameter enum slots intentionally not loaded by the kit/instrument
  format:
  - `PAR_NONE` / `PAR_MOD_WHEEL` at enum index 0
  - `PAR_NRPN_DATA_ENTRY_COARSE`
  - `PAR_NRPN_FINE`
  - `PAR_NRPN_COARSE`
  - `PAR_RESERVED4`
  - `PAR_MIDI_NOTE1` through `PAR_MIDI_NOTE7`
- The only semantic wrinkle is that `PAR_MIDI_NOTE1` through `PAR_MIDI_NOTE7`
  still live before `END_OF_SOUND_PARAMETERS`, but the new design says MIDI
  note settings belong to scene settings, not instruments or `kitset.kcg`.
  Until scene loading exists, directory kit loads intentionally leave those
  values unchanged.

### 2026-07-07 Commenting Pass

- Added detailed comments to the Phase 2 code paths touched by this loader:
  - `storageTypes.h/.c`: storage-format ownership, parser inputs/outputs,
    status codes, storage structs, parameter maps, and all public/private
    helpers added for kitset/instrument parsing
  - `filesystem.h/.c`: Kit/ scan cache accessors, directory scan/load state,
    LFN handling, text-line streaming, directory-kit load dispatch, and the
    remaining legacy `.SND` morph load path
  - `asyncfatfs.c`: why opened handles must set `file->type` from the FAT
    directory attribute and which directory clients depend on it
  - `menu.c`: the Load-page function that initiates kit loads, the cached kit
    name display path, and the slot-numbering behavior
- Re-verified after the comment pass:
  - `git diff --check` succeeds.
  - `make` succeeds.
  - Build output still includes the unrelated `eraseCount` warning in
    `asyncfatfs.c` plus the existing/newlib syscall linker warnings.

### 2026-07-07 Folder Name Separator Update

- Relaxed numbered kit-folder parsing from underscore-only `NNN_Name` to
  `NNN Name` or `NNN_Name`.
- The parser still requires a three-digit slot ID followed by either a space or
  underscore. It skips any separator run before the visible name, then preserves
  spaces inside the eight-character display name.
- Updated `tools/convert_legacy_kits.py` so regenerated kit folders use
  `NNN Kit Name`.
- Renamed the generated `SD_CARD/Kit` tree to the space convention, for example
  `SD_CARD/Kit/004 Moch to`.
- Follow-up fix: if the FAT scan exposes only the generated short alias for a
  space-named folder, such as `001SLA~1`, the scanner now derives the kit slot
  from the leading three digits and records the short alias as the open name.
  This preserves loadability even when LFN reconstruction is unavailable or
  formatter-dependent.
