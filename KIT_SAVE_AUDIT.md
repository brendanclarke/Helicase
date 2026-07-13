# KIT_SAVE_AUDIT

## Goal

Make `Save:[Kit     ]` write the current Scene kit to the new-format
`SD_CARD/Kit/` directory layout:

- one numbered Kit folder
- one `kitset.kcg`
- six instrument files containing the current normal `[params]` endpoint and
  current `[morph]` endpoint

Also expand Kit browser slots from 128 to 999 and retain instrument source names
well enough that saving a loaded kit/instrument can produce stable kit member
filenames.

## Important Constraint Found In The Code

The firmware can scan and display long filenames, but `asyncfatfs` creates files
and directories through `fat_convertFilenameToFATStyle()`, which writes only a
single 8.3 FAT directory entry. It does not create LFN entries.

That means firmware can currently create/open reliable short names such as:

- `001SLAK` or `001SLA~1` style Kit folders
- `SLAKD1.DRM` style instrument files

It cannot yet create a true visible folder such as `001 Slak` or a true
16-character instrument filename without an asyncfatfs LFN creation pass.

Plan decision for this pass:

- Implement working Kit Save with 8.3-safe physical names.
- Retain a 16-character logical instrument stem in SceneData as requested.
- Generate save filenames from that retained stem, sanitized into 8.3-safe
  physical names, with voice-number suffixes used for uniqueness.
- Keep a note that true LFN creation is a separate filesystem capability if the
  visible on-card names must literally keep 16 characters.

This preserves functionality now without pretending the current FAT writer can
create long names.

## Starting Code Shape

- `Core/Menu/menu.c`
  - The Save page already has `Save:[Kit     ]`.
  - Name editing writes the eight-byte `preset_currentName`.
  - Confirming Save Kit calls:
    `preset_saveDrumset(menu_currentPresetNr[SAVE_TYPE_KIT], 0)`.
  - Before this pass, Kit slot selection used `uint8_t menu_currentPresetNr[]`
    and displayed through `numtostrpu()`, so it could not reach 999.

- `Core/Scene/Preset/presetManager.c/h`
  - Before this pass, `preset_saveDrumset(uint8_t presetNr, uint8_t isMorph)`
    posted `filesystem_requestSave(FS_FILE_KIT, presetNr, ...)`.
  - Before this pass, `FS_FILE_KIT` save routed to the legacy flat `.SND`
    writer.
  - Preset is the right boundary for choosing the active Scene kit to save, but
    filesystem owns directory/file streaming.

- `Core/Hardware/SD/filesystem.c/h`
  - `filesystem_saveKit_tick()` still writes:
    `name[8] + END_OF_SOUND_PARAMETERS` to a flat legacy file.
  - Normal Kit load already knows how to scan `Kit/`, enter a numbered Kit
    folder, parse `kitset.kcg`, and parse six instrument files.
  - `afatfs_mkdir()` exists and opens/creates a short-name directory.
  - `afatfs_fopen(..., "w", ...)` creates/truncates files.
  - `afatfs_funlink()` exists for files, but there is no recursive directory
    remove/rename API.

- `Core/Hardware/SD/storageTypes.c/h`
  - Parser owns the Kit and Instrument text schema.
  - There is no writer API yet.
  - The save implementation should add writer helpers here rather than hardcode
    schema text inside filesystem.

- `Core/Scene/SceneData.c/h`
  - `kit_t.instrument_display_name[slot][9]` stores only an eight-character LCD
    display stem.
  - `scene_initAll()` initializes those names to `"Empty   "`.
  - The user requirement asks for default instrument names like
    `inst_vo1`, `inst_vo2`, etc., and retention of the first 16 characters of
    loaded instrument filenames.

- `Core/Hardware/SD/kitBrowser.c/h`
  - Before this pass, the legacy compatibility map was `uint8_t kb_map[128]`.
  - It is still populated by Kit scans even though current Load UI mostly uses
    filesystem's direct scan cache.

## Behavior To Implement

### Save Kit

When the user confirms `Save:[Kit     ]`:

1. The selected slot is 0-based internally and can be 0..998.
2. The visible folder number is 1..999.
3. Firmware creates/opens the Kit root folder if needed.
4. Firmware creates/opens the target Kit folder.
5. Firmware writes `kitset.kcg`.
6. Firmware writes six instrument files into that folder.
7. Each instrument file contains:
   - `format=helicase.instrument`
   - `version=1`
   - `type=<drm|snr|cym|hat>`
   - `[params]` using current `instrument_parameters[]`
   - `[morph]` using current `morph_instrument_parameters[]`
8. Existing files with the same generated names are truncated/replaced.
9. Extra old files in the folder can remain for this pass, because kitset.kcg
   names the six authoritative files. Removing unknown stale files would require
   a broader directory cleanup policy.

### 999 Kit Slots

Kit slot selection expands from 128 to 999:

- `STORAGE_KIT_MAX_SLOTS` becomes `999u`.
- Kit slot indices become `uint16_t` wherever they represent a Kit browser slot.
- The Load/Save UI clamps Kit and KitMrp numbered entries to `998`.
- Display formatting remains three digits, `001` through `999`.

Voice slot indices stay `uint8_t`. Only Kit browser slots widen.

### Instrument Name Retention

Add a 16-character retained instrument stem per kit slot.

Defaults:

- slot 1: `inst_vo1`
- slot 2: `inst_vo2`
- slot 3: `inst_vo3`
- slot 4: `inst_vo4`
- slot 5: `inst_vo5`
- slot 6: `inst_vo6`

When a Kit loads:

- For each `file=` entry in `kitset.kcg`, retain the first 16 stem characters
  before the extension.
- Also derive the existing eight-character display stem from that retained name.

When an individual Instrument loads:

- Retain the first 16 stem characters from the selected Instrument filename.
- Also update the existing eight-character display stem.

When a Scene or Bank loader later lands:

- Its kit/member metadata should populate the same retained 16-character stems.

When saving a Kit:

- Generate each instrument filename from the retained stem.
- Sanitize to FAT-safe filename characters.
- Ensure duplicates are resolved by appending the voice number to the stem.
- Ensure the extension matches the current slot type.

Because the current FAT writer is 8.3-only, the actual physical filename must
fit 8 characters plus extension. A safe first-pass rule:

- Use up to six sanitized base characters plus a two-character voice suffix:
  `1`..`6` or `v1`..`v6`.
- If two generated names still collide after sanitization/truncation, force the
  last two base characters to the voice suffix.

Example physical filenames:

- retained `slakd1` on voice 1, Drum -> `SLAKD1.DRM`
- retained duplicate `bass` on voice 1 and 2 -> `BASSV1.DRM`,
  `BASSV2.DRM`

## Code Plan

### 1. `Core/Hardware/SD/storageTypes.h`

Change Kit slot max:

```c
#define STORAGE_KIT_MAX_SLOTS 999u
```

Add a retained name length:

```c
#define STORAGE_INSTRUMENT_STEM_LEN 16u
```

Change `storage_parseNumberedFolder()` to return a `uint16_t` slot:

```c
uint8_t storage_parseNumberedFolder(const char *name,
                                    uint16_t *zero_based_slot,
                                    char display[STORAGE_KIT_DISPLAY_NAME_LEN]);
```

Add writer helpers:

```c
const char *storage_instrumentTypeToText(storage_instrument_type_t type);
const char *storage_instrumentTypeExtension(storage_instrument_type_t type);
void storage_copyInstrumentStem16(char dst[STORAGE_INSTRUMENT_STEM_LEN + 1u],
                                  const char *filename);
void storage_makeSavedInstrumentFilename(
    char dst[STORAGE_KIT_FILENAME_MAX],
    const char stem[STORAGE_INSTRUMENT_STEM_LEN + 1u],
    storage_instrument_type_t type,
    uint8_t one_based_voice,
    uint8_t force_voice_suffix);
uint8_t storage_writeKitsetLine(char *dst, uint16_t capacity,
                                const kit_t *kit,
                                uint8_t line_index);
uint8_t storage_writeInstrumentLine(char *dst, uint16_t capacity,
                                    const kit_instrument_slot_t *instrument,
                                    storage_instrument_type_t type,
                                    uint8_t one_based_voice,
                                    uint8_t morph_section,
                                    uint16_t line_index);
```

Exact signatures can be adjusted during implementation, but the ownership should
stay the same: storageTypes owns schema text and filename sanitization;
filesystem owns streaming and async file operations.

Comment block for the 999 change:

```c
/*
 * Kit folders are numbered directory entries, not legacy file slots.
 *
 * The old 128 limit came from P000.SND..P127.SND. New-format Kit/ folders are
 * addressed by a three-digit 001..999 prefix, so the storage boundary exposes
 * 999 slots and callers that hold Kit browser positions must use uint16_t.
 */
```

Comment block for instrument stems:

```c
/*
 * Retained instrument stems are save metadata, not DSP parameters.
 *
 * The first 16 stem characters survive Kit/Instrument load so Kit Save can
 * regenerate meaningful member filenames. The LCD display remains the existing
 * eight-character field; this longer stem is for storage identity only.
 */
```

Comment block for writer helpers:

```c
/*
 * Text save helpers mirror the parser-owned schema.
 *
 * Filesystem streams one line at a time, but storageTypes owns which keys are
 * emitted and how descriptor-indexed images become [params]/[morph] text.
 * Keeping writers next to parsers prevents save from drifting away from the
 * accepted load grammar.
 */
```

### 2. `Core/Hardware/SD/storageTypes.c`

Update `storage_parseNumberedFolder()`:

- Parse 001..999.
- Store `number - 1u` in a `uint16_t`.
- Keep the same visible-name sanitization.

Add `storage_instrumentTypeToText()` and extension helpers:

- Return `drm`, `snr`, `cym`, `hat`.
- Unknown returns `NULL`.

Add stem helpers:

- Copy at most 16 chars before `.`.
- Accept printable ASCII.
- Replace illegal FAT/save characters with `_` or skip them.
- Keep an eight-character display derivation for existing LCD code.

Add writer line generation:

- `kitset.kcg` order should match generated SD_CARD files:
  - `format=helicase.kitset`
  - `version=1`
  - `slot6_track7_amp_envelope_decay=<main>`
  - `slot6_track7_morph_amp_envelope_decay=<morph>`
  - blank
  - `[slot1]`, `type=...`, `file=...`, `audio_out=...`, blank
  - repeat through slot 6
- Instrument file order should match `tools/convert_legacy_kits.py`:
  - metadata
  - blank
  - `[params]`
  - descriptor file-key/value lines for main endpoint
  - blank
  - `[morph]`
  - morphable descriptor file-key/value lines for morph endpoint
  - blank

Special writer rules:

- Supplemental target selectors should be written in `[params]` only, matching
  parser behavior.
- Morph section writes only descriptors flagged morphable.
- LFO target voice writes `"self"` when the stored target voice equals the
  one-based source voice slot, matching the already-landed load behavior.
- LFO/velocity target parameter values are 16-bit and must be emitted as
  decimal `uint16_t`, not narrowed to `uint8_t`.

Comment block for LFO self save:

```c
/*
 * Save the file-only LFO self token at the storage boundary.
 *
 * SceneData stores ordinary numeric voice selectors. When a selector points
 * back to the instrument's own one-based slot, writing "self" preserves the
 * intent across future Kit/Instrument loads into a different slot. The token is
 * never stored in SceneData or exposed as a parameter value.
 */
```

### 3. `Core/Scene/SceneData.h`

Add a long retained stem beside the existing LCD display name:

```c
char instrument_stem[INSTRUMENT_SLOT_COUNT][STORAGE_INSTRUMENT_STEM_LEN + 1u];
```

This creates an include concern because `SceneData.h` currently includes
`InstrumentManager.h`, not `storageTypes.h`. Avoid introducing a circular
dependency by moving the stem length constant to a small shared header or by
defining a Scene-owned equivalent such as:

```c
#define SCENE_INSTRUMENT_STEM_LEN 16u
```

Preferred: define `SCENE_INSTRUMENT_STEM_LEN` in `SceneData.h`, and have
storage helpers accept that size or assert it equals the storage constant.

Update the comment above instrument names to distinguish:

- `instrument_display_name`: eight-character LCD display/cache
- `instrument_stem`: 16-character save filename source

Comment block:

```c
/*
 * Instrument source names are retained separately for display and save.
 *
 * display_name is the eight-character LCD field. stem keeps the first 16
 * filename stem characters loaded from Kit/Instrument files so a later Kit Save
 * can regenerate useful member filenames. Neither field is a DSP parameter,
 * and neither is editable from the UI yet.
 */
```

### 4. `Core/Scene/SceneData.c`

Initialize default names in `scene_initAll()`:

- display can be the first eight chars of the same default, padded.
- stem defaults to `inst_vo1`..`inst_vo6`.

Add a helper:

```c
void scene_setInstrumentSourceName(uint8_t scene_index,
                                   uint8_t slot,
                                   const char *filename_or_stem);
```

The helper:

- stores first 16 stem chars
- updates eight-char display
- handles invalid scene/slot as no-op

Comment block:

```c
/*
 * Retain one instrument source stem for later Kit Save.
 *
 * Inputs may be a filename with extension or a raw stem. Output updates both
 * the 16-character save stem and the eight-character LCD display name. Central
 * ownership avoids Kit load, Instrument load, and future Scene/Bank load
 * deriving subtly different names from the same file.
 */
```

### 5. `Core/Hardware/SD/filesystem.c`: 999 Slot Scan

Widen Kit slot storage:

- `op_slot` from `uint8_t` to `uint16_t` if it represents file/browser slot.
- `kit_slot_present[999]`
- `kit_slot_name[999][9]`
- `kit_slot_open_name[999][13]`
- `filesystem_kitSlotExists(uint16_t)`
- `filesystem_kitSlotName(uint16_t)`
- `filesystem_requestLoadKitForScenes(uint16_t, ...)`
- `filesystem_requestLoadKitMorphForScenes(uint16_t, ...)`

Keep instrument slot counters as `uint8_t`.

Update `filesystem_recordKitShortAlias()` and
`filesystem_recordKitDirectory()` to use `uint16_t slot`.

Update `kb_map` compatibility to store `uint16_t` or, if kitBrowser is now
effectively legacy/dead, stop populating it beyond its capacity and document
that Load UI uses filesystem's direct scan cache.

Comment block:

```c
/*
 * Kit browser slots are 0..998 even though voice slots remain byte-sized.
 *
 * New-format Kit folders use a three-digit directory prefix, so filesystem
 * scan/request code keeps Kit slot indices as uint16_t. Any uint8_t here would
 * wrap folders 257..999 back onto earlier slots.
 */
```

### 6. `Core/Hardware/SD/filesystem.c`: Retain Names On Load

During Kit load:

- When `storage_kitsetParseLine()` sees `file=...`, it currently writes
  `instrument_display_name`.
- Move this into `scene_setInstrumentSourceName()` or add storage parser output
  that captures both long stem and short display.
- The staged kit must contain stems before it is committed to Scene.

During root Instrument load:

- Current staging has `op_staged_instrument_display_name[9]`.
- Add `op_staged_instrument_stem[17]`.
- Populate it from the selected display filename, preferably LFN display name
  when available, otherwise open short name.
- `preset_startInstrumentApply()` copies both display and stem into the
  destination kit slot.

Comment block:

```c
/*
 * Stage both Instrument payload and source filename stem.
 *
 * The parsed descriptor image is independent from the file identity, but Kit
 * Save needs the file stem later to regenerate member filenames. Filesystem
 * captures the stem beside the staged payload and Preset copies it only when
 * the staged Instrument commit succeeds.
 */
```

### 7. `Core/Scene/Preset/presetManager.h/c`

Widen Kit slot arguments:

- `preset_getRequestSlot()` may need to return `uint16_t`, or add
  `preset_getRequestKitSlot()` so pattern/global legacy byte callers do not
  churn unnecessarily.
- `preset_loadKitForScenes(uint16_t presetNr, ...)`
- `preset_loadKitMorphForScenes(uint16_t presetNr, ...)`
- `preset_saveDrumset(uint16_t presetNr, uint8_t isMorph)` for Kit save.

For legacy Pattern/All/Performance calls, keep byte slots if they remain legacy
bridge formats.

Update `preset_saveDrumset()`:

- If `isMorph == 0`, post new-format Kit directory save.
- If `isMorph == 1`, keep the legacy morph save path until removed.
- Set `pm_request_type = SAVE_TYPE_KIT`.
- Store the widened kit slot in the new request slot field.

Add copy of staged instrument stem in `preset_startInstrumentApply()`.

Comment block:

```c
/*
 * Normal Kit Save now targets the directory Kit format.
 *
 * The old flat .SND writer remains only for legacy morph compatibility. Saving
 * SAVE_TYPE_KIT streams the active Scene kit through the new storage schema so
 * [params] and [morph] endpoints round-trip with the directory Kit loader.
 */
```

### 8. `Core/Hardware/SD/filesystem.h/c`: Save Request API

Option A, minimal churn:

- Keep `filesystem_requestSave(fs_file_type_t type, uint16_t slot, cb)`.
- `FS_FILE_KIT` routes to a new directory save state machine.
- `FS_FILE_MORPH` routes to the old flat morph writer.

Option B, clearer:

```c
bool filesystem_requestSaveKitDirectory(uint16_t slot, fs_completion_cb_t cb);
```

Recommended: Option B, because the current `filesystem_requestSave()` still
serves legacy flat formats and `FS_FILE_KIT` now means different things for
save than the old writer did.

Comment block:

```c
/*
 * Post a new-format Kit directory save.
 *
 * Inputs: 0-based Kit folder slot and completion callback. Output: an async
 * operation that creates/opens Kit/<NNN name>/, writes kitset.kcg, and writes
 * six instrument files from the active Scene kit. Legacy flat save remains
 * separate so directory save cannot accidentally emit Pxxx.SND bytes.
 */
```

### 9. `Core/Hardware/SD/filesystem.c`: Directory Kit Save State Machine

Add `FS_INTERNAL_OP_SAVE_KIT_DIRECTORY` or repurpose `FS_INTERNAL_OP_SAVE_KIT`
after moving old flat kit save behind `FS_INTERNAL_OP_SAVE_LEGACY_KIT`.

Recommended phases:

1. `CHDIR_ROOT`
2. `MKDIR_OR_OPEN Kit`
3. `CHDIR Kit`
4. `MKDIR_OR_OPEN target folder`
5. `CHDIR target folder`
6. prepare generated instrument filenames and de-duplicate
7. open/truncate `kitset.kcg`
8. stream kitset lines
9. close `kitset.kcg`
10. for each slot:
    - open/truncate generated instrument filename
    - stream instrument lines
    - close
11. chdir root
12. finish

Folder naming:

- Preferred visible name if LFN creation is not implemented:
  short physical folder name such as `001SLAK`.
- It must still scan back through `filesystem_recordKitShortAlias()`.
- The display name cache should be updated immediately after save completes:
  `kit_slot_present[slot] = 1`, `kit_slot_name[slot] = preset_currentName`,
  `kit_slot_open_name[slot] = generated folder short name`.

Because there is no recursive directory cleanup:

- Do not delete unknown old instrument files in this pass.
- `kitset.kcg` defines the authoritative six current instrument files.
- Existing same-name files are truncated by `"w"`.

Comment block for no recursive cleanup:

```c
/*
 * Directory Kit Save overwrites authoritative files, not the whole folder.
 *
 * asyncfatfs has file truncate/unlink but no recursive directory replace. The
 * save path therefore writes kitset.kcg and the six filenames it references.
 * Stale unreferenced files may remain in the folder, but the loader ignores
 * them because kitset.kcg is authoritative.
 */
```

### 10. `Core/Hardware/SD/filesystem.c`: Streaming Writers

Do not build whole instrument files in RAM.

Use a line buffer:

```c
static char op_write_line_buf[FS_TEXT_LINE_MAX];
static uint16_t op_write_line_len;
static uint16_t op_write_line_offset;
static uint16_t op_write_line_index;
```

Each tick:

- ask storageTypes for the next line
- write as much as `afatfs_fwrite()` accepts
- advance to next line when fully written

Comment block:

```c
/*
 * Stream text save files one line at a time.
 *
 * Kit instruments can be larger than the generic 512-byte staging buffer, and
 * save must keep foreground work bounded like the existing pattern writer. The
 * filesystem state machine owns write offsets while storageTypes owns line
 * contents.
 */
```

### 11. `Core/Menu/menu.h/c`: 999 Slot UI

Change:

- `menu_currentPresetNr[]` from `uint8_t` to `uint16_t`.
- `menu_requestCurrentLoadSaveSelection()` local `slot` to `uint16_t`.
- Kit/KitMrp max preset to `998`.
- Display formatting to show `001..999`.

Add a helper:

```c
static void menu_formatPresetNumber3(char *dst, uint16_t zero_based_slot);
```

Do not change `numtostrpu()` globally; it is used for byte-sized parameter
values elsewhere.

Comment block:

```c
/*
 * Format Kit folder numbers independently from byte-valued parameters.
 *
 * Kit slots now span 001..999, while numtostrpu() remains a uint8_t helper for
 * menu parameter values. This helper keeps Load/Save folder display from
 * wrapping above 255.
 */
```

Save confirmation remains:

```c
preset_saveDrumset(menu_currentPresetNr[SAVE_TYPE_KIT], 0);
```

but now passes a `uint16_t`.

### 12. `Core/Hardware/SD/kitBrowser.c/h`

Either:

- widen `KITBROWSER_MAX_KITS`, `kb_map[]`, `kb_mapIndex`, and return types to
  `uint16_t`, or
- mark kitBrowser as legacy 128-slot compatibility and stop using it for
  current Load/Save UI.

Recommended: widen it to avoid hidden overflow while filesystem still populates
`kb_map`.

Comment block:

```c
/*
 * Keep the compatibility kit browser aligned with directory Kit slots.
 *
 * The current Load page reads filesystem's scan cache directly, but filesystem
 * still populates kb_map for older clients. Widening the map prevents scanned
 * folders above 255 from wrapping into the wrong legacy browser entry.
 */
```

### 13. Generated SD Tree / Tooling

Update `tools/convert_legacy_kits.py` only if needed to keep generated examples
aligned with the writer's filename policy.

Likely changes:

- Use the same filename sanitizer/dedup suffix policy intended for firmware.
- Optionally generate kit folders up to 999 if source material ever does.

No existing `SD_CARD/Kit` files need content changes just to enable firmware
save, unless we decide to migrate names or add metadata fields.

## Files Expected To Change

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Scene/SceneData.h`
- `Core/Scene/SceneData.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Menu/menu.h`
- `Core/Menu/menu.c`
- `Core/Hardware/SD/kitBrowser.h`
- `Core/Hardware/SD/kitBrowser.c`

Possible tooling alignment:

- `tools/convert_legacy_kits.py`

## Verification Plan

1. Build:
   - `make -j4`
   - `git diff --check`

2. 999-slot UI:
   - Load page Kit scroll reaches `999`.
   - Save page Kit scroll reaches `999`.
   - Values above `255` do not wrap.

3. Save a default kit:
   - Boot without loading an Instrument.
   - Save to an empty slot.
   - Confirm folder is created.
   - Confirm `kitset.kcg` references six default names derived from
     `inst_vo1`..`inst_vo6`.

4. Save a loaded kit:
   - Load an existing Kit.
   - Save it to a different slot.
   - Power-cycle or rescan.
   - Load the saved slot.
   - Confirm normal endpoints, morph endpoints, audio routing, and generated
     slot-6/track-7 decay round-trip.

5. Save after individual Instrument load:
   - Load one root Instrument into a slot.
   - Save Kit.
   - Confirm that slot's saved instrument filename derives from the loaded
     Instrument filename stem.

6. Duplicate instrument names:
   - Force two slots to share the same retained stem.
   - Save Kit.
   - Confirm generated filenames are unique and kitset references the unique
     names.

7. LFO self preservation:
   - Set an LFO target voice to its own slot.
   - Save Kit.
   - Confirm saved file writes `lfo_target_voice=self` for that pair.
   - Reload into a different slot via Instrument Load and confirm `self`
     resolves to the destination slot.

8. Regression:
   - Existing Kit Load still reads old generated `SD_CARD/Kit` examples.
   - KitMrp still stages/loads morph endpoints.
   - Instrument Load still updates retained display/source names.

## Open Decision

True visible 16-character instrument filenames and `NNN Name` Kit folders
created directly by firmware require asyncfatfs long-filename creation support.
This plan makes Save Kit functional with 8.3-safe physical names while retaining
the requested first-16-character logical stem for future LFN support.

## Work Notes

- Added Scene-owned 16-character instrument stems alongside the existing
  eight-character LCD names. Defaults now initialize through a central helper as
  `inst_vo1`..`inst_vo6`, and Kit/Instrument loaders can update display/save
  names through the same SceneData boundary.
- Began storageTypes save support: widened numbered Kit folder parsing to
  001..999, added type/extension conversion, 8.3-safe saved instrument filename
  generation, kitset/instrument line formatters, and storage-boundary `self`
  emission for LFO target voice selectors.
- Implemented the async Kit directory save state machine in filesystem: normal
  `Save:[Kit     ]` now creates/opens `Kit/<NNNxxxxx>/`, writes `kitset.kcg`,
  then streams six descriptor-generated instrument files from the active Scene
  kit. `Save:[KitMrp  ]` remains on the legacy morph `.SND` path.
- Widened Kit slot plumbing to `uint16_t` through filesystem, presetManager,
  menu, and kitBrowser so numbered Kit folders can address 001..999 without
  wrapping at 255. The menu display now formats three digits for the selected
  Kit slot.
- Wired retained Instrument source names through load and save: SceneData owns
  default `inst_vo1`..`inst_vo6` stems, kitset parsing records member filenames,
  and individual Instrument Load commits the selected filename stem only after
  the staged payload is accepted.
- Reworked the new text serializers to avoid `snprintf`/stdio after the first
  build pulled in embedded heap/syscall requirements. storageTypes now formats
  the small fixed schema lines with bounded local helpers, and the rebuild links
  successfully.
- Corrected instrument-file save sequencing so each saved Instrument emits the
  metadata header once, followed by one `[params]` section and one `[morph]`
  section. Filesystem streams the whole Instrument file in one line-indexed pass
  while storageTypes owns the descriptor section boundary.

## Verification Notes

- `make -B -j4` completed successfully after the public Kit slot width changes;
  this forced stale objects to rebuild against the updated headers.
- `make -j4` completed successfully after the final formatter/comment polish.
  The remaining linker messages are the existing nosys `_close`/`_lseek`/`_read`
  /`_write` warnings, not new Kit Save failures.
