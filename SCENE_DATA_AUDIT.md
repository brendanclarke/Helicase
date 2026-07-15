# Scene Data And Scene Load/Save Plan

Planning turn only. No firmware behavior changes are made by this file.

This audit is based on the current code, not only the specification docs. The
older `SCENE_SAVE_LOAD_AUDIT.md` is useful background, but parts of it are now
stale: `FS_FILE_SCENE`, Scene scan/load request APIs, `storage_sceneset*()`,
placeholder effect parsing, and a staged Scene load state machine already exist
in the live tree.

## User Requirements Captured

- Keep the current STEP-mode MIDI settings page. Scene MIDI channel/note
  access there is still correct and should not move.
- Add three per-voice Scene settings:
  - `audio_out`: move this out of `kitset.kcg` and into Scene settings.
  - `fx_send_amount`.
  - `fader_setting`.
- Display these per-voice Scene settings as extra screens after the VOICE-mode
  `mix` SELECT subpage's instrument screens.
- Leave room for up to eight per-voice Scene settings, i.e. two extra Scene
  screens of four cells each.
- The `mix` SELECT subpage marker rules change:
  - first instrument screen always shows `^` in the upper-right corner;
  - later instrument screens show `*`;
  - no `mix` instrument screen ever shows `<`;
  - Scene setting screens always show `+`, regardless of count or position.
- Boot should load root Scene slot `000` as the initializing Scene and Kit,
  instead of loading root Kit slot `000`.
- Next phase after Scene data/menu work: promote Scene Load and Scene Save in
  the Load/Save menu. Scene folders contain `sceneset.scg`, `Kit <name>/`,
  dummy `effects.fx`, and thin `pattern.pat`.

## Current Code Reality

### Scene Data

- `Core/Scene/SceneData.h` defines one resident `scene_t` today
  (`SCENE_COUNT == 1`).
- `scene_t` already owns:
  - `display_name`;
  - `scene_settings_t settings`;
  - `PatternSet pattern`;
  - embedded `kit_t kit`.
- `scene_settings_t` currently contains:
  - `morph_amount`;
  - `voice_morph_amount[6]`;
  - `voice_decimation_all`;
  - `midi_channel[NUM_TRACKS]`;
  - `midi_note[NUM_TRACKS]`.
- `kit_settings_t` currently contains:
  - `audio_out[6]`;
  - generated slot-6 track-7 decay endpoint fields.
- Therefore the `audio_out` move is an in-memory ownership move from
  `scene->kit.settings.audio_out[]` to `scene->settings.audio_out[]`.

### Current Menu Voice Pages

- VOICE pages resolve through `menu_resolveCellAbsolute()` in
  `Core/Menu/menu.c`.
- Instrument-owned page cells come from `instrument_menu_page_t`, which holds
  16 descriptor cells per SELECT subpage.
- `MENU_COMPACT_SCREEN_CELLS` is 4, so each SELECT subpage can currently show
  up to four instrument screens.
- The last instrument menu row is the `mix` subpage in every current
  instrument table:
  - Drum: vol/pan/decimation/drive.
  - Snare/Cymbal/HiHat follow the same descriptor-page pattern.
- `checkScrollSign()` currently returns `>` on first screen, `*` on middle
  screens, and `<` on final screen for every VOICE SELECT subpage with
  multiple screens.
- `menu_cell_kind_t` currently has `MENU_CELL_KIT_SETTING` for the generated
  non-Choke track-7 decay. This is the right pattern to extend: Scene settings
  should be another Menu-owned cell kind, not fake instrument descriptors.

### STEP MIDI Settings

- `menuPages.h` `SEQ_PAGE` subpage 0 still exposes:
  - length;
  - scale;
  - MIDI channel;
  - MIDI note;
  - shuffle on the second half.
- `menu_sendEditedParameter()` routes `PAR_TRACK_MIDI_CHAN` and
  `PAR_TRACK_MIDI_NOTE` into `scene_setTrackMidiChannel()` and
  `scene_setTrackMidiNote()`.
- Keep this as-is.

### Current Scene Filesystem Code

- `storageTypes.h/c` already defines:
  - `STORAGE_ROOT_SCENE`;
  - `STORAGE_SCENESET_FILENAME`;
  - `STORAGE_SCENE_MAX_SLOTS`;
  - `storage_sceneset_t`;
  - `storage_scenesetInit()`;
  - `storage_scenesetParseLine()`;
  - `storage_scenesetFinalize()`;
  - `storage_effect*()` placeholder validation.
- `filesystem.c` already has:
  - `filesystem_requestScanScenes()`;
  - `filesystem_sceneSlotExists()`;
  - `filesystem_sceneSlotName()`;
  - `filesystem_requestLoadSceneForScenes()`;
  - `filesystem_loadSceneDirectory_tick()`.
- The current Scene load state machine stages into `op_staged_scene`, validates
  `sceneset.scg`, discovers an embedded `Kit ` directory, loads
  `kitset.kcg` plus six instruments, reads the existing binary bridge
  `pattern.pat`, validates the placeholder `.fx`, and commits the staged Scene
  to the selected resident Scene mask only at the end.
- `preset_loadSceneForScenes()` already posts `filesystem_requestLoadSceneForScenes()`.
- `menu_handleLoadSaveMenu()` already posts `preset_loadSceneForScenes()` on
  Load:[Scene] OK.
- Missing: `menu_pollPresetStatus()` has no `PRESET_OP_SCENE_LOAD` case. Scene
  load can complete, but there is no explicit Scene-load apply path.
- Missing: Scene Save request/API/state machine is not present.

### Current Boot

`main.c` currently does this before audio starts:

1. `scene_initAll()`.
2. Mount SD.
3. `menu_init()`.
4. `filesystem_requestScanKits()`.
5. `filesystem_requestScanScenes()`.
6. `filesystem_requestScanInstruments()`.
7. `preset_loadKitForScenes(0, 1u)`.
8. `menu_pollPresetStatus()` applies the Kit.
9. `preset_loadGlobals()`.

Boot already scans Scenes before loading Kit 000. The requested boot change is
to call `preset_loadSceneForScenes(0, 1u)` instead of
`preset_loadKitForScenes(0, 1u)`, after adding the missing
`PRESET_OP_SCENE_LOAD` completion/apply case.

## Default Scene Folder Audit

Current folder:

```text
SD_CARD/Scene/000 Slak/
  .DS_Store
  effects.fx
  sceneset.scg
  pattern.pat
  Kit Slak/
    kitset.kcg
    slakd1.drm
    slakd2.drm
    slakd3.drm
    slaks1.snr
    slakc1.cym
    slakh1.hat
```

Current `sceneset.scg`:

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

Current problems to correct before testing boot Scene load:

- Remove `.DS_Store` from the SD fixture. asyncfatfs now exposes dot files as
  real objects; product scans can ignore them, but fixtures should stay clean.
- `sceneset.scg` is missing the new per-voice Scene settings:
  - `audio_out=2,0,0,0,0,1` based on the current embedded `kitset.kcg`;
  - `fx_send_amount=0,0,0,0,0,0` initially;
  - `fader_setting=0,0,0,0,0,0` initially, with 0 = normal/pre-FX.
- Embedded `Kit Slak/kitset.kcg` still contains per-slot `audio_out=` lines.
  Once the parser/writer migration lands, those lines should be removed from
  Scene embedded Kits and from root Kit files generated by new firmware.
- `pattern.pat` is currently an 8116-byte binary bridge file (`file` reports
  `data`). The requested next Scene format wants a thin `pattern.pat`; define
  and generate that before calling the fixture "correct" for the new plan.
- `effects.fx` is a valid current placeholder:

```text
format=helicase.effect
version=1
placeholder=1
```

Use `effects.fx` as the emitted dummy filename for now. The loader should keep
discovering the first valid `.fx` file by extension rather than hardcoding only
that name.

## Phase 1: Move Per-Voice Scene Settings Into SceneData

Files:

- `Core/Scene/SceneData.h`
- `Core/Scene/SceneData.c`
- `Core/Scene/Preset/presetManager.c/h`
- callers currently reading `scene->kit.settings.audio_out[]`

Plan:

1. Extend `scene_settings_t`:

```c
uint8_t audio_out[INSTRUMENT_SLOT_COUNT];       /* 0..MIXER_ROUTING_DAC2_R */
uint8_t fx_send_amount[INSTRUMENT_SLOT_COUNT];  /* 0..127 */
uint8_t fader_setting[INSTRUMENT_SLOT_COUNT];   /* 0..2 */
```

2. Remove `audio_out[]` from `kit_settings_t`.

3. Add SceneData accessors:

- `scene_setVoiceAudioOut(scene, slot, route)`;
- `scene_getVoiceAudioOut(scene, slot)`;
- `scene_setVoiceFxSendAmount(scene, slot, amount)`;
- `scene_getVoiceFxSendAmount(scene, slot)`;
- `scene_setVoiceFaderSetting(scene, slot, mode)`;
- `scene_getVoiceFaderSetting(scene, slot)`.

4. Initialize defaults in both `scene_initAll()` and
   `filesystem_initStagedScene()`:

- audio out defaults should match current boot kit behavior:
  - slot 1 = 2;
  - slots 2-5 = 0;
  - slot 6 = 1.
- fx send amount = 0.
- fader setting = 0.

5. Update `preset_applyKitAudioRouting()` to read
   `scene->settings.audio_out[slot]`.

6. Search and replace remaining `kit.settings.audio_out` uses. Current key
   owners are `storage_kitsetParseLine()`, `preset_applyKitAudioRouting()`,
   and save/format code.

7. Keep generated slot-6 track-7 decay in `kit_settings_t`; that value is still
   kit/voice-layout-specific and is not part of the new Scene settings request.

## Phase 2: sceneset.scg Schema Migration

Files:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`

Plan:

1. Extend `storage_scenesetParseLine()` to parse:

```text
audio_out=<six comma-separated 0..MIXER_ROUTING_DAC2_R values>
fx_send_amount=<six comma-separated 0..127 values>
fader_setting=<six comma-separated 0..2 values>
```

2. Extend any sceneset formatter/writer added for Scene Save to emit those
   three lines.

3. Decide compatibility for old Kit files:

- Short-term: allow `audio_out=` in `kitset.kcg` as a legacy fallback only when
  Scene settings did not supply `audio_out`.
- New writes: never emit `audio_out` in `kitset.kcg`.
- Root Kit Load after the migration should not overwrite Scene routing unless
  deliberately loading a legacy Kit that still has `audio_out`. Since the user
  wants routing moved out of Kit, the safer long-term behavior is: root Kit
  load ignores missing `audio_out`, and eventually ignores present `audio_out`
  too after fixture migration.

4. Update `storage_kitset_t`:

- remove `seen_audio_out_mask` from required validation for v1 new writes, or
  make it a legacy optional field;
- update comments that currently say `kitset.kcg` owns audio routing.

5. Update fixture `SD_CARD/Scene/000 Slak/sceneset.scg` with the new lines and
   remove `audio_out` from `SD_CARD/Scene/000 Slak/Kit Slak/kitset.kcg` once
   parser compatibility is in place.

## Phase 3: VOICE Mix Subpage Scene Settings Expansion

Files:

- `Core/Menu/menu.c`
- `Core/Menu/menu.h`
- `Core/Menu/MenuText.h` and text tables if new labels are needed
- `Core/Scene/Preset/presetManager.c/h`

Current architecture to preserve:

- Do not add fake descriptors to every instrument parameter table.
- Do not change STEP-mode MIDI settings.
- Do not expand `instrument_menu_page_t` beyond 16 just to hold Scene settings.

Recommended Menu model:

1. Add a new menu cell kind:

```c
MENU_CELL_SCENE_SETTING
```

2. Add a `menu_scene_setting_kind_t` enum with room for eight per-voice Scene
   settings. Initial entries:

```c
MENU_SCENE_SETTING_AUDIO_OUT
MENU_SCENE_SETTING_FX_SEND_AMOUNT
MENU_SCENE_SETTING_FADER_SETTING
```

3. Add a small immutable Scene setting descriptor table in `menu.c`:

```c
typedef struct {
    uint8_t kind;
    const char *short_name;   /* "out", "fxs", "fad" */
    const char *category;     /* "Scene" or "Mix" */
    const char *long_name;    /* "AudioOut", "FxSend", "Fader" */
    uint8_t dtype;
    uint16_t min_value;
    uint16_t max_value;
} menu_scene_setting_descriptor_t;
```

Use existing dtype patterns where possible:

- audio out: current audio-out display/menu behavior if reusable; otherwise a
  small menu-name formatter for routing values.
- fx send amount: 0..127 numeric.
- fader setting: menu with three values:
  - 0 = pre-FX/normal;
  - 1 = post-FX;
  - 2 = FX.

4. Treat only the `mix` SELECT subpage as having appended Scene screens.
   Current `NUM_SUB_PAGES` is 8, and the current `mix` subpage is the last
   instrument row. Define:

```c
#define MENU_VOICE_MIX_SUBPAGE (NUM_SUB_PAGES - 1u)
#define MENU_SCENE_SETTING_CELLS 8u
#define MENU_SCENE_SETTING_SCREENS 2u
```

5. Update voice screen helpers:

- `menu_voiceSubPageScreenExists(subPage, screen)`:
  - for non-mix subpages, keep current 0..3 instrument-screen behavior;
  - for mix, screens 0..3 are instrument screens when they have selectable
    instrument cells;
  - mix screens after the last instrument screen map to Scene setting screens,
    up to two screens.
- `menu_voiceFirstSelectableColumn()` must work on both instrument screens and
  Scene setting screens.
- `menu_voiceVisiblePosition()` currently returns an absolute instrument cell
  0..15. For mix Scene screens, either:
  - split it into a screen resolver that returns cell kind plus local column; or
  - allow `menu_resolveCell()` to detect mix screen >= instrument screen count
    and resolve a `MENU_CELL_SCENE_SETTING`.

6. Update `menu_resolveCellAbsolute()` / `menu_resolveCell()`:

- instrument cells continue to use `instrumentManager_voicePageDescriptorIndex()`;
- `MENU_CELL_SCENE_SETTING` cells set:
  - `cell.slot = menu_voicePageToSlot(menu_activePage)`;
  - `cell.scene_setting = descriptor kind`;
  - `cell.descriptor` should not point at an instrument descriptor.

7. Update display/value/commit functions:

- `menu_cellDtype()`;
- `menu_cellDisplayValue()`;
- `menu_cellCommitValue()`;
- `menu_clampCellValue()`;
- `menu_formatCellValue3()`;
- full-name display for scene setting cells.

8. Commit scene setting edits through Preset, not direct Menu writes:

- audio out: add `preset_setVoiceAudioOut(scene, slot, route)`, update
  retained SceneData and apply active mixer routing.
- fx send amount: add retained setter now; DSP application can be a no-op until
  FX routing exists, but keep the owner boundary in Preset.
- fader setting: add retained setter now; DSP application can be a no-op until
  the fader/FX model lands.

9. Implement marker behavior in `checkScrollSign()`:

- if active page is VOICE and active SELECT subpage is not mix, preserve
  current `>`, `*`, `<` behavior.
- if active SELECT subpage is mix:
  - instrument screen 0 returns `^`;
  - later instrument screens return `*`;
  - Scene setting screens return `+`;
  - never return `<`.

10. Update `menu_switchSubPage()` cycling:

- pressing mix repeatedly should cycle through all existing mix instrument
  screens, then Scene setting screens, then back to screen 0.
- pressing a different SELECT button should enter that subpage at screen 0 as
  it does today.

## Phase 4: Boot From Scene Slot 000

Files:

- `main.c`
- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.c/h`

Plan:

1. Add `PRESET_OP_SCENE_LOAD` handling to `menu_pollPresetStatus()`.

Recommended behavior:

```c
case PRESET_OP_SCENE_LOAD:
    menu_startSoundApply(
        updateGap = 1,
        resetSave = active Load/Save page,
        repaintAll = active not Load/Save or after reset,
        startGlobals = 0,
        requestPattern = 1,
        applyPerformanceGlobals = 0,
        clearStorageBusy = 1,
        showStaleWarning = 0,
        FS_STALE_WARNING_NONE);
    menu_refreshLoadSceneLeds();
    break;
```

The important pieces are:

- full kit/runtime apply;
- `preset_applySceneSettings()` through the existing apply path;
- pattern UI mirror refresh via `requestPattern = 1`;
- `menu_storageBusy` cleared for explicit menu loads.

2. In `main.c`, replace boot Kit 000 load:

```c
preset_loadKitForScenes(0, 1u);
```

with:

```c
preset_loadSceneForScenes(0, 1u);
```

3. Boot fallback decision:

- Strict user request says boot Scene 000, not Kit 000.
- Recommended behavior for development: if Scene 000 does not exist or fails,
  show the filesystem error overlay once the LCD is available rather than
  silently falling back to Kit 000. Silent fallback would hide broken Scene
  fixtures during the next test pass.

4. Keep root Kit scan at boot for Load:[Kit] browser readiness unless memory or
   boot time becomes a problem. Scene boot needs Scene scan, and Instrument
   browser still needs Instrument scan.

## Phase 5: Correct Default Scene Files

Files:

- `SD_CARD/Scene/000 Slak/sceneset.scg`
- `SD_CARD/Scene/000 Slak/Kit Slak/kitset.kcg`
- `SD_CARD/Scene/000 Slak/pattern.pat`
- `SD_CARD/Scene/000 Slak/effects.fx`

Plan:

1. Remove `.DS_Store`.

2. Update `sceneset.scg` to include:

```text
audio_out=2,0,0,0,0,1
fx_send_amount=0,0,0,0,0,0
fader_setting=0,0,0,0,0,0
```

3. Remove `audio_out=` lines from embedded `kitset.kcg` after the parser no
   longer requires them.

4. Define the requested "thin pattern.pat" before replacing the existing binary
   bridge file. Do not call this fixture correct until `pattern.pat` is text or
   whatever thin schema is deliberately chosen.

5. Keep `effects.fx` as:

```text
format=helicase.effect
version=1
placeholder=1
```

## Phase 6: Scene Load/Save Menu Promotion

Scene Load is partly present now. Scene Save is not.

### Load:[Scene]

Already present:

- `SAVE_TYPE_SCENE` is in `menu_loadSaveLoadTypes`.
- scrolling the Scene slot changes displayed name only;
- OK calls `preset_loadSceneForScenes()`.

Needed:

1. Add `PRESET_OP_SCENE_LOAD` completion/apply handling as described above.
2. Ensure `menu_storageBusy = 1u` is set when Scene Load request is accepted.
   The current OK branch only clears busy on failure; mirror other accepted
   operations.
3. Retest with `SD_CARD/Scene/000 Slak/`.
4. Ensure Load:[Scene] uses the selected Scene mask just like Kit Load.

### Save:[Scene]

Add the missing writer stack:

1. Preset API:

```c
uint8_t preset_saveScene(uint16_t slot,
                         uint8_t source_scene,
                         const char display_name[8]);
```

Later Bank work can widen this to a source Scene mask and sequential slot
placement. For `SCENE_COUNT == 1`, source scene is active Scene 0.

2. Filesystem API:

```c
bool filesystem_requestSaveSceneDirectory(uint16_t slot,
                                          uint8_t source_scene,
                                          const char display_name[8],
                                          fs_completion_cb_t cb);
```

3. Save state machine:

- enter root;
- create/open root `Scene/`;
- recursively delete all physical directories for the target Scene slot;
- create `Scene/NNN Name/`;
- write `sceneset.scg`;
- create `Kit <kit name>/`;
- write embedded `kitset.kcg` without `audio_out`;
- write six embedded instrument files using the existing Kit Save member-file
  writer and voice-number filename rule;
- write thin `pattern.pat`;
- write dummy `effects.fx`;
- flush before success.

4. `sceneset.scg` writer:

Emit:

```text
format=helicase.sceneset
version=1
name=<scene name>
morph_amount=<0..255>
voice_morph_amount=<six values>
voice_decimation_all=<0..127>
midi_channel=<seven values>
midi_note=<seven values>
audio_out=<six values>
fx_send_amount=<six values>
fader_setting=<six values>
```

5. Pattern writer:

- Do not reuse the old 8116-byte binary bridge unless the user explicitly
  accepts it.
- Define the thin `pattern.pat` schema first. If this is only a placeholder for
  the next Scene Save test, use a guarded text stub and make the loader accept
  that stub.

6. Menu:

- Promote `SAVE_TYPE_SCENE` into `menu_loadSaveSaveTypes` only after the writer
  is implemented.
- Save:[Scene] should seed the editable name from
  `scene_sceneDisplayName(scene_getActiveIndex())`, not Kit name and not target
  slot occupancy text.
- Save:[Scene] overwrite indicator uses `filesystem_sceneSlotExists(slot)`.
- OK posts `preset_saveScene(...)`.
- Completion should share the save cleanup path after success.

## Test Checklist For Next Chat

1. Build after adding SceneData fields and menu cells.
2. On VOICE mix subpage:
   - first mix screen marker is `^`;
   - later mix instrument screens use `*`;
   - Scene setting screens use `+`;
   - no mix screen shows `<`;
   - non-mix subpages keep current marker behavior.
3. Edit audio out on a VOICE Scene setting screen and confirm mixer routing
   changes for the active Scene.
4. Edit fx send amount/fader setting and confirm retained SceneData values
   change, even before DSP FX behavior exists.
5. Confirm STEP-mode MIDI channel/note page still works.
6. Boot with `SD_CARD/Scene/000 Slak/`; verify Scene 000, embedded Kit, Scene
   settings, pattern, and placeholder effect validate.
7. Load:[Scene] slot scrolling changes display only; OK starts load.
8. Save:[Scene] writes a clean folder with no `audio_out` in embedded
   `kitset.kcg` and with all new Scene setting lines in `sceneset.scg`.
9. Reboot and confirm Scene 000 load no longer depends on root `Kit/000`.

## Open Decisions

- Exact thin `pattern.pat` schema. The current file is binary bridge data; the
  requested thin file needs a deliberate text or compact schema before Scene
  Save can emit it.
- Final display strings for fader modes. Recommended internal values are
  `0=pre`, `1=post`, `2=fx`.
- Whether legacy `kitset.kcg audio_out=` should be ignored immediately or used
  as a temporary fallback when `sceneset.scg` lacks `audio_out`.
- Whether boot Scene 000 failure should hard-error only, or fall back to Kit
  000 during field use. For the next implementation/test pass, hard-error is
  recommended so broken Scene data is visible.
