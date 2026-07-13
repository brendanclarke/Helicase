# Scene Save/Load Audit

## Status

Planning and review data only. No firmware code has been changed for Scene
Save/Load yet.

Created review fixtures:

- `SD_CARD/Scene/001 Slak/`
- `tools/populate_scene_directory.py`

The generator currently converts only `SD_CARD/Kit/001 Slak/` by default. It
can later convert all numbered Kit folders with `--all`.

## Binding Behavior

Root `Scene/` is a library/pool like root `Kit/` and root `Instrument/`. It is
not Bank autosave storage.

Add Scene as the next Load/Save type after Kit and KitMrp:

```text
Load:[Kit     ]
Load:[KitMrp  ]
Load:[Scene   ]
```

Scene Load differs from Kit Load:

- Kit Load remains instant-on-encoder-scroll.
- Scene Load does not load on encoder scroll.
- Scene Load requires explicit OK on the cursor `>`.

Scene Save and Kit Save both need explicit OK/OW confirmation text:

- `OK` means the selected operation will not overwrite any occupied library
  slot according to the save-placement rules.
- `OW` means confirming will overwrite at least one occupied slot.
- Kit Save should gain the same explicit `OK`/`OW` display as Scene Save/Load.

SEQ buttons are Scene-slot toggles for both load and save contexts:

- Load: selected library Kit/Scene is loaded into every selected resident Bank
  Scene slot. Today `SCENE_COUNT == 1`, so only slot 1 is physically available,
  but the API and UI should keep the 16-slot mask semantics.
- Save: selected resident Bank Scene slots decide what is saved into the root
  library.

Scene Save placement:

- The selected root Scene library slot is where the lowest-numbered selected
  resident Scene gets saved.
- Additional selected resident Scenes save to subsequent root library slots.
- If a subsequent slot is occupied but its `sceneset.scg` scene name matches
  the resident Scene being saved, that occupied slot may be reused.
- If any occupied non-matching slot would be overwritten, the confirmation text
  changes from `OK` to `OW`.

Apply the same save-confirmation schema to Kit Save:

- Kit Save is currently active-Scene-only in firmware because resident Banks do
  not exist yet. The Save UI should still use the selected Scene-slot mask so
  the later multi-Scene Bank case lands cleanly.
- Lowest-numbered selected resident Scene saves to the selected root Kit slot.
- Additional selected resident Scenes save to subsequent root Kit slots.
- Reusing a slot with a matching saved name is allowed without `OW`.
- Overwriting any occupied non-matching slot changes confirmation text to `OW`.

## Scene Folder Format

A root Scene library folder is numbered:

```text
Scene/
  001 Slak/
```

The folder contains:

```text
Scene/001 Slak/
  sceneset.scg
  Kit Slak/
    kitset.kcg
    slakd1.drm
    slakd2.drm
    slakd3.drm
    slaks1.snr
    slakc1.cym
    slakh1.hat
  pattern.pat
  effects.fx
```

`sceneset.scg` validates the folder and stores Scene-level settings. It does
not store the Kit directory name.

The embedded Kit is whichever directory entry comes up first whose display name
starts with `Kit ` and whose contents include a valid `kitset.kcg`. The loaded
Kit name is whatever follows the space after `Kit ` in that directory name.

The pattern is whichever `.pat` file comes up first and validates as the
current bridge Pattern format.

The effect is whichever `.fx` file comes up first and validates. Every Scene
should have an effect file. Until real effects exist, generated Scenes use a
placeholder `effects.fx`:

```text
format=helicase.effect
version=1
placeholder=1
```

The user is free to move `.pat`, `.fx`, and `Kit*` directories between Scene
folders. The loader must discover by type/validity, not by hardcoded names such
as `pattern.pat`, `effect.fx`, or a Kit name stored in `sceneset.scg`.

Current generated `sceneset.scg`:

```text
format=helicase.sceneset
version=1
name=Slak
morph_amount=0
voice_morph_amount=0,0,0,0,0,0
voice_decimation_all=127
midi_channel=1,2,3,4,5,6,7
midi_note=63,63,63,63,63,63,63
```

Current generated `pattern.pat` is the existing thin bridge binary format:

- 8-byte display name.
- One live pattern.
- Seven tracks.
- 128 bridge steps per track.
- 9-byte packed step records.
- Per-track main-step masks.
- One PatternSetting record.
- Per-track length bytes.
- Per-track rotation/scale extension.
- Per-track shuffle extension.

The generated file is 8116 bytes. This is provisional and should be replaced
when the dynamic-stack Pattern format lands.

## Current Code Reality

`Core/Menu/menu.h`

- `SAVE_TYPE_KIT` and `SAVE_TYPE_KIT_MORPH` are the only numbered library
  types before `SAVE_TYPE_GLO`.
- `NUM_PRESET_LOCATIONS` is currently `2`, matching Kit and KitMrp as separate
  UI entries.
- `menu_currentPresetNr[NUM_PRESET_LOCATIONS]` stores numbered browser cursors.
- `SAVE_STATE_OK` exists, but there is no separate overwrite display state.

`Core/Menu/menu.c`

- `menu_requestCurrentLoadSaveSelection()` starts Kit Load immediately when
  `loadKitOnLoadPage` is true. KitMrp also loads immediately on scroll.
- `menu_repaintLoadSavePage()` renders `OK` on Save for numbered entries and
  on Load only for Settings/Samples. Kit Load currently hides OK.
- `menu_handleLoadSaveMenu()` posts Kit Save from the OK branch, but ordinary
  Kit Load does not use OK.
- `menu_loadSceneButtonPressed()` only consumes SEQ buttons on `LOAD_PAGE`.
  Kit Load toggles `menu_kitLoadSceneMask`; Instrument Load selects one
  destination Scene.
- `menu_refreshLoadSceneLeds()` is also Load-only and blinks selected Kit
  targets or Instrument destination.
- `menu_pollPresetStatus()` handles `PRESET_OP_KIT_LOAD` by starting the
  chunked Scene/Kit runtime apply cursor. Scene Load should land as a new
  completed op and use the same active-Scene apply boundary when it loads the
  active Scene.

`Core/Hardware/frontPanel/buttonHandler.c`

- `processPress()` calls `menu_loadSceneButtonPressed()` before normal SEQ
  step handling.
- `buttonHandler_loadSceneSeqPressedMask` remembers consumed SEQ presses until
  release so a Load-owned SEQ press does not fall through into step release
  behavior.
- Save-page SEQ ownership will need the same consume/release behavior, either
  by expanding `menu_loadSceneButtonPressed()` into a load/save selector API or
  by adding a sibling accessor and reusing the same pressed mask.

`Core/Scene/Preset/presetManager.c/h`

- Preset owns async operation status and maps filesystem completion callbacks
  into `PRESET_OP_*` values consumed by Menu.
- `preset_loadKitForScenes(slot, scene_mask)` already posts a staged Kit load
  into every selected Scene.
- `preset_saveDrumset(slot, isMorph)` saves only the active Scene Kit today.
- There is no Scene load/save request API yet.
- After active Scene data changes, `preset_startDrumsetApply()` applies Scene
  settings and six Kit voices through bounded foreground work.

`Core/Hardware/SD/filesystem.h/c`

- `fs_file_type_t` has no `FS_FILE_SCENE` or `FS_FILE_EFFECT`.
- `fs_internal_op_t` has no Scene scan/load/save ops.
- The Kit scan cache is dedicated to root `Kit/`: `kit_slot_present`,
  `kit_slot_name`, and `kit_slot_open_name`.
- `filesystem_requestScanKits()` clears and fills the Kit cache only.
- `filesystem_requestLoadKitForScenes()` validates a Scene mask and commits a
  fully parsed staged Kit to selected resident Scenes.
- `filesystem_loadKitDirectory_tick()` assumes current directory shape
  `Kit/<NNN Name>/kitset.kcg`.
- `filesystem_saveKitDirectory_tick()` writes only the active Scene Kit to
  `Kit/<NNN Name>/`.
- Pattern load/save already has a current bridge binary serializer, but it is
  tied to the active Scene through `scene_getActiveIndex()`.
- `filesystem_writeTextLine()` can stream line-based text files from
  storageTypes writers without staging whole files in RAM.

`Core/Hardware/SD/storageTypes.c/h`

- Owns the Kit and Instrument text schema.
- `STORAGE_ROOT_KIT`, `STORAGE_ROOT_INSTRUMENT`, and
  `STORAGE_KITSET_FILENAME` are the only root/schema constants relevant to
  this feature today.
- `storage_parseNumberedFolder()` parses numbered folders for Kit and can be
  reused for Scene library slots because Scene uses the same `NNN Name`
  convention.
- No `sceneset.scg` parser/writer exists yet.
- Formatting helpers such as `storage_formatAssignmentU16()` are static, so
  new sceneset formatting should live in storageTypes rather than duplicating
  assignment string code in filesystem.c.

`Core/Scene/SceneData.c/h`

- `SCENE_COUNT` is currently `1`, but APIs are indexed for future Banks.
- `scene_settings_t` already owns `morph_amount`,
  `voice_morph_amount[6]`, `voice_decimation_all`,
  `midi_channel[NUM_TRACKS]`, and `midi_note[NUM_TRACKS]`.
- `scene_initAll()` defaults `voice_decimation_all` to 127 and MIDI channels
  to 1..7. It does not currently default `midi_note[]` to 63.
- Kit source display/stem metadata exists for instrument filenames, but there
  is no retained Scene name field.

## Implementation Plan

### 1. Keep Review Fixture In Sync

Files:

- `tools/populate_scene_directory.py`
- `SD_CARD/Scene/001 Slak/`

Required changes already applied in this planning pass:

- Remove the `kit=` key from generated `sceneset.scg`.
- Generate `effects.fx` for every Scene.
- Preserve the `Kit <name>/` embedded directory shape.

Verification:

- `SD_CARD/Scene/001 Slak/sceneset.scg` has no Kit reference.
- `SD_CARD/Scene/001 Slak/effects.fx` exists and has a v1 placeholder guard.
- `SD_CARD/Scene/001 Slak/pattern.pat` remains 8116 bytes.

### 2. Add Scene and Effect Storage Schema Helpers

Files:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`

Add constants:

- `STORAGE_ROOT_SCENE` = `"Scene"`.
- `STORAGE_SCENESET_FILENAME` = `"sceneset.scg"`.
- `STORAGE_SCENE_MAX_SLOTS` = `999u`.
- `STORAGE_SCENE_DISPLAY_NAME_LEN` can alias `STORAGE_KIT_DISPLAY_NAME_LEN`
  unless the UI later needs a different width.
- `STORAGE_EFFECT_PLACEHOLDER_FORMAT` or a direct parser for
  `format=helicase.effect`.

Add `storage_sceneset_t` parse state:

- Inputs: one text line at a time from filesystem.
- Outputs: a caller-owned `scene_t` settings record plus a parsed eight-char
  Scene display name.
- Required fields for v1 validation: `format=helicase.sceneset`,
  `version=1`, and `name=<text>`.
- Optional settings: `morph_amount`, `voice_morph_amount`,
  `voice_decimation_all`, `midi_channel`, `midi_note`.
- Missing optional settings should keep SceneData defaults for compatibility.

Add public functions:

- `storage_scenesetInit(storage_sceneset_t *state)`.
- `storage_scenesetParseLine(storage_sceneset_t *state, const char *line,
  scene_t *target_scene, char display_name[8])`.
- `storage_scenesetFinalize(const storage_sceneset_t *state)`.
- `storage_formatScenesetLine(char *dst, uint16_t cap, const scene_t *scene,
  const char display_name[8], uint16_t line_index)`.
- `storage_effectPlaceholderValidateLine()` or an effect parse state if we want
  the placeholder `.fx` file to be validated in the same incremental style.
- `storage_formatEffectPlaceholderLine()`.

Comment requirements:

- In `.h`, each new struct/function gets a block explaining inputs, outputs,
  required fields, optional fields, and callers.
- In `.c`, comment the comma-list parsing loops for six morph amounts and
  seven MIDI values. These loops are easy to get off-by-one and should explain
  how invalid counts fail versus missing optional keys.
- Comment the important state bits in `storage_sceneset_t`, especially
  `seen_format`, `seen_version`, and `seen_name`.

### 3. Add Scene Library Browser Cache

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`

Add API:

- `bool filesystem_requestScanScenes(fs_completion_cb_t cb)`.
- `uint8_t filesystem_sceneSlotExists(uint16_t zero_based_slot)`.
- `const char *filesystem_sceneSlotName(uint16_t zero_based_slot)`.
- Optional helper for overwrite planning:
  `uint8_t filesystem_sceneSlotNameMatches(uint16_t slot,
  const char display_name[8])`.

Add state:

- `scene_slot_present[STORAGE_SCENE_MAX_SLOTS]`.
- `scene_slot_name[STORAGE_SCENE_MAX_SLOTS][9]`.
- `scene_slot_open_name[STORAGE_SCENE_MAX_SLOTS][13]`.

Add op:

- `FS_INTERNAL_OP_SCAN_SCENES`.

Implementation shape:

- Reuse the Kit scan pattern: chdir root, open/create `Scene`, iterate entries
  with `afatfs_findFirst/findNext`, use LFN when present, fall back to short
  name with NT case flags, and call `storage_parseNumberedFolder()`.
- Store the returned display name and asyncfatfs-openable short alias.
- Missing `Scene/` should be an empty successful scan, matching Kit/Instrument
  scan behavior.

Comment requirements:

- Comment why Scene has a parallel scan cache instead of using the Kit cache:
  root Scene slots and root Kit slots have independent occupancy/name spaces.
- Comment the LFN/SFN selection loop and the invariant that UI names must come
  from actual discovered/created directory entries.

### 4. Add Scene Load Filesystem State Machine

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`

Add API:

- `bool filesystem_requestLoadSceneForScenes(uint16_t library_slot,
  uint16_t scene_mask, fs_completion_cb_t cb)`.

Add op:

- `FS_INTERNAL_OP_LOAD_SCENE`.

Inputs:

- Root Scene library slot from `scene_slot_*` cache.
- Destination resident Scene mask from Menu/Presets.

Outputs:

- A fully staged `scene_t` copied into every selected resident Scene only after
  `sceneset.scg`, one valid embedded `Kit*` directory, one valid `.pat`, and
  one valid `.fx` have all validated.

State needed:

- `scene_t op_staged_scene`.
- `uint16_t op_scene_load_scene_mask`.
- Scene root/slot directory handles.
- Found-first valid embedded kit directory alias.
- Found-first valid `.pat` open name.
- Found-first valid `.fx` open name.
- Sceneset parse state.

Discovery rules:

- Enter `Scene/`.
- Enter selected numbered Scene folder by cached alias.
- Open and parse `sceneset.scg`.
- Scan current Scene folder entries:
  - first directory whose display name starts with `Kit ` and contains a valid
    `kitset.kcg` is the embedded Kit;
  - first `.pat` file that validates as bridge pattern is the pattern;
  - first `.fx` file that validates as effect placeholder/effect schema is the
    effect.
- Do not read Kit name from `sceneset.scg`.

Important implementation detail:

- The existing `filesystem_loadKitDirectory_tick()` assumes root `Kit/<NNN>`.
  Scene Load should not force the embedded `Kit <name>/` through that function
  directly unless the Kit parser is factored into a reusable
  “load current-directory Kit folder by alias” subroutine.
- Preferred plan: factor the middle of `filesystem_loadKitDirectory_tick()`
  into reusable helpers/state phases:
  - open/parse `kitset.kcg` in the current directory;
  - read six listed instrument files into a caller-supplied `kit_t`;
  - commit policy stays outside the helper.

Pattern loading:

- The current bridge `filesystem_loadPattern_tick()` writes into active
  PatternData through `scene_getActiveIndex()`.
- Scene Load needs a version that targets `op_staged_scene.pattern`, not the
  active Scene. Either:
  - factor pack/unpack stream helpers to accept a `scene_t *`, or
  - add target-scene/staged-scene mode variables used by the existing bridge
    reader.
- Avoid loading into active memory until the whole Scene validates.

Effect loading:

- For first pass, parse placeholder `.fx` and retain no runtime effect state.
- Scene Load should still require one valid `.fx` file because Scenes always
  have effects files.

Runtime apply:

- After copying into selected resident Scenes, Preset/Menu should apply runtime
  only if the active Scene was among the selected destinations.
- Reuse `preset_startDrumsetApply()` for active Scene Kit/settings apply.
- For inactive Scenes after Banks exist, do not touch DSP runtime.

Comment requirements:

- Comment the staged-load transaction: no resident Scene changes until all
  children validate.
- Comment the “first valid” scan loops for `Kit*`, `.pat`, and `.fx`, including
  the user freedom to move these between Scenes.
- Comment why Scene Load is OK-triggered, not instant-on-scroll.
- Comment pattern staging carefully, because active PatternData writes would be
  the easiest accidental bug.

### 5. Add Scene Save Filesystem State Machine

Files:

- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`

Add API:

- `bool filesystem_requestSaveScene(uint16_t library_slot,
  uint8_t source_scene, fs_completion_cb_t cb)`.
- Optional batch API:
  `bool filesystem_requestSaveScenes(uint16_t first_library_slot,
  uint16_t source_scene_mask, fs_completion_cb_t cb)` if Menu should hand
  placement to filesystem rather than posting one save at a time.

Add op:

- `FS_INTERNAL_OP_SAVE_SCENE`.

Inputs:

- Target root Scene library slot.
- Source resident Scene index.
- Scene display name from the save UI, normally derived from
  `preset_currentName`.

Outputs:

- Create/open `Scene/<NNN Name>/`.
- Write `sceneset.scg`.
- Create/open embedded `Kit <name>/`.
- Write `kitset.kcg` and six instrument files using the same Kit writer logic.
- Write bridge `pattern.pat` or another chosen `.pat` name.
- Write placeholder/real `.fx` file.
- Update `scene_slot_*` cache with the actual created display/open-name pair.

Important implementation detail:

- Existing Kit Save always reads `scene_getActiveIndex()`. Scene Save needs
  Kit writer helpers that accept `const scene_t *source_scene` or
  `const kit_t *kit`, plus a target directory context.
- The embedded Kit folder name should be `Kit <kit display name>`. Until a
  retained Kit name exists, derive it from the Scene or from the eight-char
  save name, and note this as a temporary policy.
- `sceneset.scg` stores the Scene name and settings only. It must not store the
  embedded Kit directory name.

Comment requirements:

- Comment the save placement inputs separately from filesystem write inputs:
  Menu decides which root slot/source Scene pair is being saved; filesystem
  writes exactly that pair.
- Comment the embedded Kit naming temporary policy.
- Comment any loops that save multiple source Scenes if batch save lands in
  filesystem.

### 6. Add Preset-Level Scene Operations

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`

Add enum values:

- `PRESET_OP_SCENE_LOAD`
- `PRESET_OP_SCENE_SAVE`
- `PRESET_OP_SCENE_SCAN` only if scan completion is routed through Preset.

Add public APIs:

- `uint8_t preset_loadSceneForScenes(uint16_t scene_slot,
  uint16_t scene_mask)`.
- `void preset_saveScene(uint16_t library_slot, uint8_t source_scene)`.
- Optional batch save coordinator if Menu should not post one save at a time.

Inputs:

- Library Scene slot.
- Destination/source Scene mask.

Outputs:

- Posts filesystem operation.
- Captures request type/slot/mask so Menu can reject stale completions the same
  way Kit Load does.
- On completion, Menu starts active-scene apply if needed.

Comment requirements:

- Comment why Scene Load completion is distinct from Kit Load completion even
  though both may end in `preset_startDrumsetApply()`: Scene also loads
  settings, pattern, and effect.
- Comment request-coordinate capture so async completion cannot drift if the
  user scrolls after posting OK.

### 7. Update Load/Save Menu Types and Cursors

Files:

- `Core/Menu/menu.h`
- `Core/Menu/menu.c`

Enum changes:

```c
SAVE_TYPE_KIT = 0,
SAVE_TYPE_KIT_MORPH,
SAVE_TYPE_SCENE,
SAVE_TYPE_GLO,
SAVE_TYPE_SAMPLES,
NUM_SAVE_TYPES
```

Browser storage:

- Replace `NUM_PRESET_LOCATIONS 2` with a named count that includes Scene:
  `NUM_NUMBERED_LOADSAVE_LOCATIONS 3`, or similar.
- Kit and KitMrp may still share the same browser slot value, but do not rely
  on enum ordinal coincidence without a helper.
- Add helpers:
  - `menu_numberedLocationForType(what)`
  - `menu_typeIsNumberedLibrary(what)`
  - `menu_typeUsesInstantLoadOnScroll(what)`

Display:

- Add `Scene   ` label.
- Load page:
  - Kit and KitMrp keep no OK and still load on scroll.
  - Scene shows `OK` or `OW` and only loads on click.
- Save page:
  - Kit and Scene both show `OK` or `OW`.
  - KitMrp remains load-only and is skipped on Save.

OK/OW:

- Add `menuText_ow` or local literal `"OW"`.
- Add a Menu-side save-plan/overwrite calculation:
  - Inputs: save type, selected source Scene mask, selected library slot,
    current library scan cache names.
  - Output: boolean overwrite risk.
- The calculation should be side-effect-free and called by repaint and before
  posting a save.

SEQ toggles:

- Generalize `menu_kitLoadSceneMask` to a load/save Scene mask such as
  `menu_sceneTargetMask`.
- Make SEQ buttons active for:
  - Load Kit
  - Load KitMrp
  - Load Scene
  - Save Kit
  - Save Scene
- Keep Instrument Load as its special one-destination mode.

Comment requirements:

- In `.h`, update the `menu_loadSceneButtonPressed()` comment or rename it to
  something like `menu_loadSaveSceneButtonPressed()`. Explain it consumes both
  Load and Save SEQ presses.
- In `.c`, comment the overwrite calculation loop:
  - it walks selected source Scenes in ascending order;
  - maps them to target library slots starting at the cursor slot;
  - compares occupied slot names against outgoing Scene/Kit names;
  - returns overwrite risk without mutating state.
- Comment the type-to-browser-index helper to avoid future enum drift.

### 8. Update Button Handler SEQ Routing

Files:

- `Core/Hardware/frontPanel/buttonHandler.c`

Required change:

- Rename or broaden `buttonHandler_loadSceneSeqPressedMask` to reflect
  Load/Save ownership.
- Call the generalized Menu SEQ-consumer before normal step handling for both
  Load and Save page contexts.

Inputs:

- Physical SEQ press/release.

Outputs:

- Press consumed when Menu owns a Scene target toggle.
- Release consumed if the press was consumed, preventing step erase/roll
  release behavior.

Comment requirements:

- Update the state comment to mention Save as well as Load.
- Comment the reason release consumption remains buttonHandler-owned.

### 9. Kit Save Schema Change

Files:

- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.c/h`
- `Core/Hardware/SD/filesystem.c/h`

Current behavior:

- `preset_saveDrumset(slot, 0)` saves only active Scene Kit to one root Kit
  slot.
- Save page already requires OK for Kit, but it always displays OK and does
  not use SEQ-selected source Scene slots or overwrite-aware text.

Required behavior:

- Menu computes a Kit save plan from selected Scene mask and selected root Kit
  slot.
- If only one Scene exists, behavior remains equivalent except OK/OW reflects
  real occupancy/name comparison.
- With future Banks, multiple selected Scenes save to selected slot, then next
  free/matching slots.
- Kit Save label changes to `OW` if any target Kit slot is occupied by a
  non-matching saved Kit name.

Open design point:

- Resident Scene currently does not retain a Kit name. Kit Save today derives
  root `Kit/<NNN Name>/` from `preset_currentName`. Multi-Scene Kit Save needs
  a per-Scene Kit display name or an explicit policy. The Scene Save generator
  derives embedded `Kit Slak` from the source root Kit folder, but firmware
  SceneData currently stores instrument source names only.

Recommended temporary policy:

- For `SCENE_COUNT == 1`, keep using `preset_currentName` for Kit Save name.
- Add an audit item before Banks: introduce retained Scene Kit name metadata so
  multi-Scene Kit Save can compare/save meaningful names per source Scene.

Comment requirements:

- Comment temporary active-Scene-only name policy so it is not mistaken for the
  final Bank behavior.

### 10. Tests and Manual Verification

Build-level checks:

- `make` when the toolchain is available.
- Confirm no `.h` public enum changes leave switch statements missing cases.

SD fixture checks:

- `find SD_CARD/Scene/001 Slak -maxdepth 2 -type f`.
- Confirm `sceneset.scg` has no `kit=` key.
- Confirm `effects.fx` exists.
- Confirm embedded `Kit Slak/kitset.kcg` matches root `Kit/001 Slak/kitset.kcg`.
- Confirm `pattern.pat` remains bridge-size 8116 bytes.

Firmware smoke tests once implemented:

- Load page type scroll: Kit, KitMrp, Scene, Settings, Samples.
- Kit Load still loads immediately on slot scroll.
- Scene Load slot scroll changes display only; OK starts load.
- Scene Load can target every selected Scene bit currently available.
- Save page type scroll skips KitMrp.
- Save Kit and Save Scene display OK/OW correctly.
- SEQ toggles work on Save page and do not leak to step edits on release.
- Scene Load of `001 Slak` loads settings, Kit, pattern, and placeholder effect.
- Moving/renaming `pattern.pat` and `effects.fx` still loads the first valid
  `.pat`/`.fx`.
- Moving `Kit Slak/` to another Scene still loads it as the first valid `Kit*`
  directory.

## Implementation Notes From This Pass

Files changed in this pass:

- `Core/Hardware/SD/storageTypes.h` / `storageTypes.c`
  - Added `STORAGE_ROOT_SCENE`, `STORAGE_SCENESET_FILENAME`,
    `STORAGE_SCENE_MAX_SLOTS`, and Scene/effect parser state structs.
  - Added `storage_sceneset*` helpers for `sceneset.scg`.
  - Added `storage_effect*` helpers for the first placeholder `.fx` schema.
  - Added line formatters for `sceneset.scg` and placeholder `effects.fx`.
  - Comment blocks now describe required fields, parser outputs, and the
    explicit rule that `sceneset.scg` must not store the embedded Kit name.

- `Core/Hardware/SD/filesystem.h` / `filesystem.c`
  - Added `FS_FILE_SCENE`, Scene scan APIs, Scene slot-name query APIs, Scene
    save API, and Scene load API.
  - Added a separate root `Scene/` scan cache so Load:[Scene] displays actual
    FAT directory names from the card, not metadata guesses.
  - Added boot-time `Scene/` scan in `main.c`.
  - Added Scene Save writer:
    - creates/opens `Scene/<NNN Name>/`;
    - writes `sceneset.scg`;
    - writes embedded `Kit <trimmed scene name>/`;
    - writes all six instrument text files and `kitset.kcg`;
    - writes bridge `pattern.pat`;
    - writes placeholder `effects.fx`;
    - updates the Scene scan cache from the actual created folder alias.
  - Added Scene Load reader:
    - validates selected root Scene slot from the scan cache;
    - scans the selected folder for the first `Kit*` directory, first `.pat`,
      and first `.fx`;
    - parses `sceneset.scg`, embedded Kit, bridge pattern, and placeholder
      effect into `op_staged_scene`;
    - commits the staged Scene to every selected destination Scene only after
      all required files validate.
  - Added detailed state-machine comments at the request boundary, child scan,
    pattern streaming, commit point, and Scene save name-building helper.

- `Core/Menu/menu.h` / `menu.c`
  - Added `SAVE_TYPE_SCENE` and a third Load/Save browser location.
  - Load page now has `Load:[Scene   ]`; Scene Load scroll changes the display
    only and requires explicit `OK`.
  - Kit Load remains instant-on-encoder-scroll.
  - Save page skips KitMrp and offers Kit/Scene save.
  - SEQ buttons toggle destination/source Scene mask on Load and on Kit/Scene
    Save.
  - Save OK text now changes to `OW` for occupied non-matching Kit/Scene slots
    in the current single-target implementation.

- `Core/Scene/Preset/presetManager.h` / `presetManager.c`
  - Added `PRESET_OP_SCENE_LOAD`, `PRESET_OP_SCENE_SAVE`,
    `preset_loadSceneForScenes()`, and `preset_saveScene()`.
  - Routed Scene completion through existing menu preset-status polling.

- `Core/Hardware/frontPanel/buttonHandler.c`
  - Updated SEQ-button comment to cover Load/Save Scene-mask use.

- `tools/populate_scene_directory.py`
  - Generates `SD_CARD/Scene/001 Slak/` from `SD_CARD/Kit/001 Slak/`.
  - Emits `sceneset.scg` without a `kit=` key.
  - Emits placeholder `effects.fx`.
  - Emits an 8116-byte bridge `pattern.pat`.
  - Copies the root Kit as embedded `Kit Slak/`.

Known limitations after this pass:

- Scene Load discovers the first matching `Kit*`, `.pat`, and `.fx` entries and
  then validates those choices. It does not yet continue scanning to the next
  matching candidate if the first matching child is malformed. The spec phrase
  "whatever comes up first as long as it is valid" should become a retry loop
  before users start freely mixing multiple candidate children in one Scene
  folder.
- `kit_t` does not currently retain a Kit display name. Scene Load uses the
  `Kit <name>` folder as the source of truth for selecting the embedded Kit,
  but there is no resident field to remember `<name>` after load. This should
  be added deliberately before Banks/multi-Scene Kit Save need per-Scene Kit
  names.
- Multi-Scene batch Save placement is not fully implemented. Current menu code
  uses the lowest selected Scene and the selected target slot; the OK/OW label
  is single-target-aware. The future Bank pass still needs the "next free or
  matching slot, global OW if any overwrite" planner for Kit Save and Scene
  Save.
- Firmware Scene Save currently names embedded Kit folders from the Scene save
  display name because no retained Kit name exists. The generated fixture uses
  `Kit Slak`, matching the root source Kit.
- The code writes `effects.fx` and loads any valid `.fx`. The authoritative spec
  still contains singular `effect.fx` in places; decide and normalize wording
  before Phase 6 effects work.

Verification performed in this environment:

- `python3 tools/populate_scene_directory.py --overwrite` completed and
  regenerated `SD_CARD/Scene/001 Slak`.
- `SD_CARD/Scene/001 Slak/sceneset.scg` contains no `kit=` key.
- `SD_CARD/Scene/001 Slak/effects.fx` contains
  `format=helicase.effect`, `version=1`, `placeholder=1`.
- `SD_CARD/Scene/001 Slak/pattern.pat` is 8116 bytes.

Verification not performed:

- `make -j4` could not run because `/bin/bash: make: command not found`.
- `arm-none-eabi-gcc` was not found in `PATH`.
- `git diff --check` was not usable because this checkout has a large
  pre-existing CRLF/trailing-whitespace diff footprint across unrelated files.

## Open Questions Before Firmware Edits

- Should the generated placeholder be named `effects.fx` permanently, or should
  firmware save use `effect.fx` while still loading the first valid `.fx`?
- Should a Scene missing a valid `.fx` fail load, or should firmware generate a
  default empty effect in memory and flag the folder as repairable?
- What is the final retained owner for per-Scene display name and per-Scene Kit
  display name? Current `scene_t` has neither.
- Should Scene Save copy source Scene names from `sceneset.scg` once Banks
  exist, or should Save-page edited text override them?
- How should OW be shown when a multi-Scene save would both reuse matching
  slots and overwrite a later non-matching slot: likely global `OW` if any
  overwrite occurs.
