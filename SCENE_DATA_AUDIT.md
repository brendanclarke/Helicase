# Scene Data And Scene Load/Save Plan

Planning turn only. No firmware behavior changes are made by this file.

This plan is based on the current code as the source of truth. The files under
`knowledge_files/specification_reference/` describe the intended filesystem
shape and module boundaries, but the implementation details below come from the
live `Core/` tree.

The older `SCENE_SAVE_LOAD_AUDIT.md` is background only. It is stale in several
important places: `FS_FILE_SCENE`, Scene scan/load request APIs,
`storage_sceneset*()`, placeholder effect parsing, and a staged Scene load
state machine already exist in code.

## User Requirements Captured

- Keep the current STEP-mode MIDI settings page. Scene MIDI channel/note
  access there is correct and should not move.
- Add three per-voice Scene settings:
  - `audio_out`: move this out of `kitset.kcg` and into Scene settings.
  - `fx_send_amount`.
  - `fader_setting`.
- Display those per-voice Scene settings as extra screens after the VOICE-mode
  `mix` SELECT subpage's instrument screens.
- Leave room for up to eight per-voice Scene settings, i.e. two extra Scene
  screens of four cells each.
- Change the `mix` SELECT subpage marker rules:
  - first instrument screen always shows `^` in the upper-right corner;
  - later instrument screens show `*`;
  - no `mix` instrument screen ever shows `<`;
  - Scene setting screens always show `+`.
- Boot should load root Scene slot `000` as the initializing Scene and Kit
  instead of loading root Kit slot `000`.
- Promote Scene Load and Scene Save in the Load/Save menu. Scene folders contain
  `sceneset.scg`, `Kit <name>/`, dummy `effects.fx`, and thin `pattern.pat`.

## Current Code Reality

### Scene Data

- `Core/Scene/SceneData.h` defines one resident `scene_t` today
  (`SCENE_COUNT == 1`), but the API is indexed for future Bank work.
- `scene_t` owns `display_name`, `scene_settings_t settings`,
  `PatternSet pattern`, and embedded `kit_t kit`.
- `scene_settings_t` currently owns `morph_amount`,
  `voice_morph_amount[6]`, `voice_decimation_all`,
  `midi_channel[NUM_TRACKS]`, and `midi_note[NUM_TRACKS]`.
- `kit_settings_t` currently owns `audio_out[6]` plus generated
  slot-6/track-7 decay endpoints.
- Therefore the routing change is an ownership migration from
  `scene->kit.settings.audio_out[slot]` to
  `scene->settings.audio_out[slot]`. Generated slot-6/track-7 decay remains
  Kit-owned.

### Menu Voice Pages

- VOICE cells resolve through `menu_resolveCellAbsolute()` and
  `menu_resolveCell()` in `Core/Menu/menu.c`.
- Instrument-owned cells come from `instrument_menu_page_t`, with 16 descriptor
  cells per SELECT subpage. `MENU_COMPACT_SCREEN_CELLS` is 4, so the current
  instrument-only limit is four screens per SELECT subpage.
- The `mix` SELECT subpage is currently the last row in every instrument menu
  table and normally has one instrument screen: `vol`, `pan`, decimation, and
  drive/routing-like cells, depending on the instrument.
- `checkScrollSign()` currently treats every VOICE SELECT subpage the same:
  first screen `>`, middle screens `*`, final screen `<`.
- `MENU_CELL_KIT_SETTING` already proves the right pattern for non-descriptor
  cells: a menu cell can borrow descriptor-like display/dtype behavior while
  committing to Scene/Kit data instead of fake instrument descriptors.

### STEP MIDI Settings

- `menuPages.h` `SEQ_PAGE` subpage 0 still exposes length, scale, MIDI channel,
  MIDI note, and shuffle.
- `menu_sendEditedParameter()` routes `PAR_TRACK_MIDI_CHAN` and
  `PAR_TRACK_MIDI_NOTE` into `scene_setTrackMidiChannel()` and
  `scene_setTrackMidiNote()`.
- Keep this unchanged. It is already Scene-owned and in the right UI place.

### Scene Filesystem

- `storageTypes.h/c` already defines `STORAGE_ROOT_SCENE`,
  `STORAGE_SCENESET_FILENAME`, `STORAGE_SCENE_MAX_SLOTS`,
  `storage_sceneset_t`, `storage_scenesetInit()`,
  `storage_scenesetParseLine()`, `storage_scenesetFinalize()`, and placeholder
  effect validation.
- `filesystem.c` already has `filesystem_requestScanScenes()`,
  `filesystem_sceneSlotExists()`, `filesystem_sceneSlotName()`,
  `filesystem_requestLoadSceneForScenes()`, and
  `filesystem_loadSceneDirectory_tick()`.
- Scene Load stages into `op_staged_scene`, validates `sceneset.scg`, discovers
  an embedded `Kit ` directory, loads `kitset.kcg` plus six instruments, reads
  the current binary bridge `pattern.pat`, validates the first `.fx` file, and
  commits to selected resident Scenes only at the end.
- `PRESET_OP_SCENE_LOAD` exists and `preset_loadSceneForScenes()` posts the
  filesystem request, but `menu_pollPresetStatus()` has no Scene-load
  completion branch.
- Scene Save does not exist yet: there is no `PRESET_OP_SCENE_SAVE`, no
  `preset_saveScene()`, no `filesystem_requestSaveSceneDirectory()`, and no
  `FS_INTERNAL_OP_SAVE_SCENE`.

### Boot

`main.c` currently initializes storage before audio like this:

1. `scene_initAll()`.
2. Mount SD.
3. `menu_init()`.
4. `filesystem_requestScanKits()`.
5. `filesystem_requestScanScenes()`.
6. `filesystem_requestScanInstruments()`.
7. `preset_loadKitForScenes(0, 1u)`.
8. `menu_pollPresetStatus()` applies the Kit.
9. `preset_loadGlobals()`.

The boot change is to call `preset_loadSceneForScenes(0, 1u)` at step 7 after
adding the missing `PRESET_OP_SCENE_LOAD` completion/apply path.

## Implementation Plan

### 1. Move Per-Voice Scene Settings Into SceneData

Files:

- `Core/Scene/SceneData.h`
- `Core/Scene/SceneData.c`
- `Core/Hardware/SD/filesystem.c`
- `Core/Scene/Preset/presetManager.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`

#### `Core/Scene/SceneData.h`

Change `scene_settings_t` to add:

```c
uint8_t audio_out[INSTRUMENT_SLOT_COUNT];
uint8_t fx_send_amount[INSTRUMENT_SLOT_COUNT];
uint8_t fader_setting[INSTRUMENT_SLOT_COUNT];
```

Contracts:

- `audio_out[slot]` is the retained per-voice output route. Input domain is the
  current mixer route enum, 0..`MIXER_ROUTING_DAC2_R`; invalid values clamp to
  the stereo default at the Preset/runtime boundary.
- `fx_send_amount[slot]` is the future per-voice FX send amount in the same
  compact 0..127 domain as most menu amounts. Until FX routing exists, it is
  retained and saved but has no DSP output.
- `fader_setting[slot]` is the future fader topology mode. Use 0..2:
  `0 = pre/normal`, `1 = post`, `2 = fx`. Until the fader/FX model exists, it
  is retained and saved but has no DSP output.

Remove `audio_out[INSTRUMENT_SLOT_COUNT]` from `kit_settings_t`. Update the
comments on `kit_settings_t` and `kit_t` so they no longer say routing is
Kit-owned or comes from `kitset.kcg`. Keep:

```c
uint8_t slot6_track7_amp_envelope_decay;
uint8_t slot6_track7_morph_amp_envelope_decay;
```

in `kit_settings_t`, because generated slot-6/track-7 decay is still tied to
the loaded kit voice layout.

Add accessors:

```c
void scene_setVoiceAudioOut(uint8_t scene_index, uint8_t slot, uint8_t route);
uint8_t scene_getVoiceAudioOut(uint8_t scene_index, uint8_t slot);
void scene_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot, uint8_t amount);
uint8_t scene_getVoiceFxSendAmount(uint8_t scene_index, uint8_t slot);
void scene_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot, uint8_t mode);
uint8_t scene_getVoiceFaderSetting(uint8_t scene_index, uint8_t slot);
```

Why the accessors must exist:

- Menu and Preset should not learn the layout of `scene_settings_t`.
- Storage parsers need one bounded owner API for retained writes.
- Future Bank work can raise `SCENE_COUNT` without chasing direct array writes.

#### `Core/Scene/SceneData.c`

Add a tiny default helper:

```c
static uint8_t scene_defaultVoiceAudioOut(uint8_t slot)
{
    if (slot == 0u)
        return 2u;
    if (slot == 5u)
        return 1u;
    return 0u;
}
```

This preserves the current Slak/root-kit boot behavior: voice 1 routes to route
2, voices 2..5 to route 0, and voice 6 to route 1.

Update `scene_initAll()`:

- It already loops `scene_index = 0..SCENE_COUNT-1`.
- Inside that loop, add a second slot loop over
  `slot = 0..INSTRUMENT_SLOT_COUNT-1`.
- For each slot, set:
  - `settings.audio_out[slot] = scene_defaultVoiceAudioOut(slot)`;
  - `settings.fx_send_amount[slot] = 0`;
  - `settings.fader_setting[slot] = 0`.
- Keep the existing track loop for MIDI defaults:
  `midi_channel[track] = track + 1`.
- Keep the existing instrument reset loop. Do not fold the new Scene settings
  into `instrumentManager_resetSlot()`, because they are not instrument type
  data.

Implement the accessors:

- Every setter first calls `scene_get(scene_index)` and returns on NULL.
- Every setter checks `slot < INSTRUMENT_SLOT_COUNT`.
- `scene_setVoiceAudioOut()` stores the raw route after clamping to the menu
  route domain if possible. If this file cannot include mixer route constants
  without pulling DSP dependencies into SceneData, store the byte and let Preset
  clamp at apply time. The preferred low-coupling version is: SceneData enforces
  slot validity, Preset enforces DSP route validity.
- `scene_setVoiceFxSendAmount()` clamps values above 127 to 127.
- `scene_setVoiceFaderSetting()` clamps values above 2 to 2.
- Getters return safe defaults for invalid coordinates:
  - audio out: `scene_defaultVoiceAudioOut(slot)` when only the Scene is invalid
    and `MIXER_ROUTING_DAC1_STEREO`-equivalent default for invalid slot;
  - FX send: 0;
  - fader setting: 0.

#### `Core/Hardware/SD/filesystem.c`

Update `filesystem_initStagedScene()` with the same Scene-setting defaults as
`scene_initAll()`.

Important loop details:

- This function currently has `slot` and `track` locals.
- The `track` loop initializes `midi_channel[track]` and `midi_note[track]`.
- Extend the existing `slot` loop, which already resets instruments and source
  names, to also initialize:
  - `scene->settings.audio_out[slot]`;
  - `scene->settings.fx_send_amount[slot]`;
  - `scene->settings.fader_setting[slot]`.
- Do not call `scene_initAll()` from here. `filesystem_initStagedScene()` works
  on private `op_staged_scene`, not a resident Scene index.

Also initialize staged Pattern defaults here after adding the PatternData helper
described in section 5. A zeroed `PatternSet` is not a valid empty pattern
because it leaves step probabilities, notes, and track lengths at zero.

#### `Core/Scene/Preset/presetManager.c/h`

Update `preset_applyKitAudioRouting()`:

- Input remains `(scene_index, slot)`.
- Replace:

```c
route = scene->kit.settings.audio_out[slot];
```

with:

```c
route = scene->settings.audio_out[slot];
```

- Keep the clamp:

```c
if (route > MIXER_ROUTING_DAC2_R)
    route = MIXER_ROUTING_DAC1_STEREO;
```

- Keep the active-Scene guard before writing `mixer_audioRouting[slot]`.

Add Preset setter APIs:

```c
uint8_t preset_setVoiceAudioOut(uint8_t scene_index, uint8_t slot, uint8_t route);
uint8_t preset_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot, uint8_t amount);
uint8_t preset_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot, uint8_t mode);
```

Why these must be in Preset:

- Menu edits are user-facing retained setting changes, not direct struct writes.
- `audio_out` has a runtime side effect: active Scene mixer routing changes
  immediately.
- `fx_send_amount` and `fader_setting` have no runtime backend yet, but keeping
  their setter boundary in Preset prevents Menu from owning future DSP apply.

Setter behavior:

- `preset_setVoiceAudioOut()` validates the Scene with `scene_get()`, validates
  `slot`, clamps `route`, calls `scene_setVoiceAudioOut()`, then calls
  `preset_applyKitAudioRouting(scene_index, slot)` if the Scene is active.
- `preset_setVoiceFxSendAmount()` validates and stores via SceneData. It should
  explicitly no-op runtime for now with a comment that future FX routing attaches
  here.
- `preset_setVoiceFaderSetting()` validates and stores via SceneData. It should
  explicitly no-op runtime for now with a comment that future fader topology
  attaches here.

`preset_applySceneSettings()` should not overwrite the new settings. It can
remain focused on Morph mirrors and global decimation until the FX/fader runtime
exists. Audio routing is per-voice and still applies through
`preset_applyKitVoice()` / `preset_tickDrumsetApply()`.

### 2. Migrate `sceneset.scg` And `kitset.kcg` Schemas

Files:

- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`
- `tools/populate_scene_directory.py`
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`

#### `Core/Hardware/SD/storageTypes.h`

Extend `storage_sceneset_t`:

```c
uint8_t seen_audio_out;
uint8_t seen_fx_send_amount;
uint8_t seen_fader_setting;
```

These should be optional compatibility bits, not required fields. Missing
settings preserve defaults established by `filesystem_initStagedScene()`.

Change `storage_kitset_t`:

- Keep `seen_audio_out_mask` only as a legacy-compatibility observation.
- Add:

```c
uint8_t legacy_audio_out[STORAGE_KIT_SLOT_COUNT];
```

- Update the struct comments: `kitset.kcg` now requires only `type` and `file`
  for each slot. `audio_out` may be read from old files but is no longer a
  required or newly emitted Kit field.

Add a helper declaration for legacy fallback:

```c
uint8_t storage_kitsetHasCompleteLegacyAudioOut(const storage_kitset_t *kit);
const uint8_t *storage_kitsetLegacyAudioOut(const storage_kitset_t *kit);
```

The fallback is used only by Scene Load when an old `sceneset.scg` lacks
`audio_out`.

#### `Core/Hardware/SD/storageTypes.c` - sceneset parsing

Extend `storage_scenesetParseLine()` to parse:

```text
audio_out=<six comma-separated 0..5 values>
fx_send_amount=<six comma-separated 0..127 values>
fader_setting=<six comma-separated 0..2 values>
```

Implementation details:

- Use `storage_parseCsvU8()` for all three arrays.
- `audio_out` should use max `MIXER_ROUTING_DAC2_R` if that constant is already
  visible here. If adding the include would create an unwanted DSP dependency,
  use literal `5u` with a comment that it matches `outputNames[0][0]` and the
  current mixer route range.
- `fx_send_amount` uses max 127.
- `fader_setting` uses max 2.
- On parse success, set the corresponding `seen_*` flag.
- Do not require these flags in `storage_scenesetFinalize()`. Required fields
  remain `format`, `version`, and `name`.

Important CSV behavior:

- The parser must reject short or long lists. Six values are required because
  these are per-instrument-slot settings, not per-track settings.
- The CSV loop should be allowed to store directly into
  `target_scene->settings.<field>`.
- Because defaults are already initialized before parsing, unknown or absent
  future fields leave the staged Scene in a valid state.

#### `Core/Hardware/SD/storageTypes.c` - kitset compatibility

Update `storage_kitsetParseLine()`:

- Keep accepting `audio_out=` under `[slotN]`.
- Stop writing it to `target_kit->settings.audio_out[parsed]`, because that
  field will no longer exist.
- Instead, parse into `kit->legacy_audio_out[parsed]` and set
  `seen_audio_out_mask |= (1u << parsed)`.

Update `storage_kitsetFinalize()`:

- Keep:

```c
const uint8_t all_slots = (uint8_t)((1u << STORAGE_KIT_SLOT_COUNT) - 1u);
```

- Require only:

```c
kit->seen_type_mask == all_slots
kit->seen_file_mask == all_slots
```

- Do not require `seen_audio_out_mask == all_slots`.
- Keep the loop over `i = 0..STORAGE_KIT_SLOT_COUNT-1` that validates each
  filename extension against its declared type.

Why the mask math matters:

- `STORAGE_KIT_SLOT_COUNT` is 6, so `(1u << 6) - 1u` yields `0x3f`, one bit for
  each required slot.
- Removing `seen_audio_out_mask` from the required comparison makes old and new
  kitsets both loadable.
- Retaining the mask lets Scene Load distinguish "complete old routing data"
  from "partial hand-edited routing data".

#### `Core/Hardware/SD/filesystem.c` - legacy routing fallback

In `filesystem_loadSceneDirectory_tick()`, after embedded `kitset.kcg`
finalizes successfully and before the staged Scene commits:

- If `op_sceneset_state.seen_audio_out == 0` and
  `storage_kitsetHasCompleteLegacyAudioOut(&op_kitset) != 0`, copy the six
  legacy values into `op_staged_scene.settings.audio_out[slot]`.
- Use a loop shaped like:

```c
const uint8_t *legacy_audio = storage_kitsetLegacyAudioOut(&op_kitset);
for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
    uint8_t route = legacy_audio[slot];
    if (route > 5u)
        route = filesystem_defaultVoiceAudioOut(slot);
    op_staged_scene.settings.audio_out[slot] = route;
}
```

Because SceneData accessors require resident Scene indices, this staging copy
will probably be a direct write plus a local clamp. Keep the direct write local
to filesystem staging; do not add fake resident indices.

Do not apply old Kit `audio_out` during root Kit Load. Root Kit Load should
replace instrument membership and descriptor images, while retaining the active
Scene's routing settings.

#### `Core/Hardware/SD/filesystem.c` - kitset writer

Update `filesystem_nextKitsetLine()` so new `kitset.kcg` files never emit
`audio_out`.

Current per-slot math:

```c
index = index - 5;
slot = index / 5;
field = index % 5;
```

Current fields are:

0. `[slotN]`
1. `type=...`
2. `file=...`
3. `audio_out=...`
4. blank line

New per-slot math should be:

```c
index = index - 5;
slot = index / 4;
field = index % 4;
```

New fields are:

0. `[slotN]`
1. `type=...`
2. `file=...`
3. blank line

The generated slot-6/track-7 decay lines at line indexes 2 and 3 remain
unchanged because those are still Kit-owned generated endpoint values.

#### `Core/Hardware/SD/filesystem.c` or `storageTypes.c` - sceneset writer

Add a streamed `sceneset.scg` formatter. Preferred ownership is `storageTypes.c`
because it already owns the sceneset parser, but a `filesystem_nextScenesetLine()`
using the existing local formatter helpers is acceptable if kept small and
documented.

Emitted lines:

```text
format=helicase.sceneset
version=1
morph_amount=<0..255>
voice_morph_amount=<six values>
voice_decimation_all=<0..127>
midi_channel=<seven values>
midi_note=<seven values>
audio_out=<six values>
fx_send_amount=<six values>
fader_setting=<six values>
```

Important loop details:

- The six-value lines loop `slot = 0..INSTRUMENT_SLOT_COUNT-1`.
- The seven-value lines loop `track = 0..NUM_TRACKS-1`.
- CSV formatting should append commas only between values:
  `if (i != 0) append ','; append value`.
- Do not emit `name`. Scene identity is the containing `Scene/<NNN Name>/`
  directory, and embedded Kit identity is the `Kit <name>/` child directory. A
  file must never store its own name.

### 3. Add VOICE `mix` Scene Setting Screens

Files:

- `Core/Menu/menu.c`
- `Core/Menu/menu.h`
- `Core/Menu/MenuText.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Scene/Preset/presetManager.h`

No changes are needed in the instrument descriptor tables. Do not add fake
descriptor rows to every `*Parameters.c` file.

#### `Core/Menu/menu.c` - cell model

Add:

```c
MENU_CELL_SCENE_SETTING
```

to `menu_cell_kind_t`.

Add:

```c
typedef enum {
    MENU_SCENE_SETTING_AUDIO_OUT = 0,
    MENU_SCENE_SETTING_FX_SEND_AMOUNT,
    MENU_SCENE_SETTING_FADER_SETTING,
    MENU_SCENE_SETTING_COUNT
} menu_scene_setting_kind_t;
```

Extend `menu_cell_t`:

```c
uint8_t scene_setting;
```

Add a static descriptor table:

```c
typedef struct {
    uint8_t kind;
    const char short_name[4];
    const char category[9];
    const char long_name[9];
    uint8_t dtype;
    uint16_t min_value;
    uint16_t max_value;
} menu_scene_setting_descriptor_t;
```

Initial rows:

- `audio_out`: short `"out"`, category `"Scene"`, long `"AudioOut"`,
  dtype `DTYPE_MENU | (MENU_AUDIO_OUT << 4)`, min 0, max 5.
- `fx_send_amount`: short `"fxs"`, category `"Scene"`, long `"FxSend"`,
  dtype `DTYPE_0B127`, min 0, max 127.
- `fader_setting`: short `"fad"`, category `"Scene"`, long `"Fader"`,
  dtype can be `DTYPE_0B15` with custom formatting, min 0, max 2.

Do not add a new `MENU_*` id for fader mode unless the dtype packing is widened.
`DTYPE_MENU` stores menu ids in the high nibble and ids 0..15 are already used.
This is the same reason `DTYPE_LFO_POLARITY` exists as a dedicated dtype.

Suggested compact fader labels:

- 0: `"pre"`
- 1: `"pst"`
- 2: `"fx "`

#### `Core/Menu/menu.c` - screen math

Add constants:

```c
#define MENU_VOICE_MIX_SUBPAGE (NUM_SUB_PAGES - 1u)
#define MENU_SCENE_SETTING_CELLS 8u
#define MENU_SCENE_SETTING_SCREENS \
    (MENU_SCENE_SETTING_CELLS / MENU_COMPACT_SCREEN_CELLS)
```

Keep `MENU_VOICE_SUBPAGE_SCREENS` as the instrument descriptor screen count:

```c
INSTRUMENT_MENU_PAGE_CELLS / MENU_COMPACT_SCREEN_CELLS
```

Add helper concepts:

- `menu_voiceInstrumentScreenExists(subPage, screen)`: current
  descriptor-backed existence logic.
- `menu_voiceInstrumentScreenCount(subPage)`: loops from screen 0 up to
  `MENU_VOICE_SUBPAGE_SCREENS - 1` and counts existing instrument screens.
  It should stop at the first missing screen because SELECT cycling is linear.
- `menu_voiceSceneSettingScreenExists(scene_screen)`: maps
  `scene_setting_index = scene_screen * 4 + column` and returns nonzero if any
  index is below `MENU_SCENE_SETTING_COUNT`.
- `menu_voiceTotalScreenExists(subPage, screen)`: for non-mix, delegates to
  instrument screens; for mix, screens below the instrument count are
  instrument screens, and later screens are Scene setting screens.

Important math:

- Instrument absolute position remains:

```c
absolute = screen * MENU_COMPACT_SCREEN_CELLS
         + (position % MENU_COMPACT_SCREEN_CELLS);
```

- Mix Scene-setting index is:

```c
scene_screen = screen - instrument_screen_count;
scene_index = scene_screen * MENU_COMPACT_SCREEN_CELLS
            + (position % MENU_COMPACT_SCREEN_CELLS);
```

- If `scene_index >= MENU_SCENE_SETTING_COUNT`, the cell is empty.

Update these helpers to use total-screen logic:

- `menu_voiceSubPageScreenExists()`
- `menu_voiceFirstSelectableColumn()`
- `menu_voiceVisiblePosition()`
- `menu_switchSubPage()`
- `menu_resetActiveParameter()`
- `menu_moveToMenuItem()`

The important behavior is that a same-SELECT press on the mix row cycles:

1. every existing instrument screen;
2. each existing Scene setting screen;
3. back to screen 0.

Pressing a different SELECT button still enters that subpage at screen 0.

#### `Core/Menu/menu.c` - cell resolution

Update `menu_resolveCellAbsolute()`:

- For normal VOICE cells, keep current descriptor resolution.
- For mix Scene screens, return `MENU_CELL_SCENE_SETTING` with:
  - `cell.slot = menu_voicePageToSlot(menu_activePage)`;
  - `cell.scene_setting = scene descriptor kind`;
  - `cell.descriptor = NULL`;
  - `cell.descriptor_index = INSTRUMENT_MENU_EMPTY`.

Do not let Scene settings pass through `instrumentManager_voicePageDescriptorIndex()`.
These are Scene settings, not instrument parameters.

#### `Core/Menu/menu.c` - value display, clamp, commit

Update:

- `menu_cellDtype()`
- `menu_cellDisplayValue()`
- `menu_cellCommitValue()`
- `menu_clampCellValue()`
- `menu_formatCellValue3()`
- full-name display paths in `menu_repaintGeneric()`
- short label display paths in `menu_repaintGeneric()`
- endless-pot edit paths that currently include
  `MENU_CELL_INSTRUMENT || MENU_CELL_KIT_SETTING`

Behavior:

- `menu_cellDtype()` returns the scene-setting descriptor dtype.
- `menu_cellDisplayValue()` reads:
  - `scene_getVoiceAudioOut(scene_getActiveIndex(), cell->slot)`;
  - `scene_getVoiceFxSendAmount(...)`;
  - `scene_getVoiceFaderSetting(...)`.
- `menu_clampCellValue()` should clamp Scene setting cells by descriptor
  `min_value` and `max_value` after normal dtype clamp. This is necessary
  because `fader_setting` uses a narrow 0..2 range without a packed menu table.
- `menu_formatCellValue3()` should special-case fader setting labels. Audio out
  can use the existing `MENU_AUDIO_OUT` value table and FX send can use the
  normal numeric formatter.
- `menu_cellCommitValue()` calls the new Preset setters. It must not write
  SceneData directly.

Full-name display:

- Instrument and generated Kit cells use descriptor category/long name.
- Scene setting cells should copy `category` and `long_name` from the new table.
- The compact top-row label should copy `short_name`.

#### `Core/Menu/menu.c` - marker rules

Update `checkScrollSign()`:

- For non-VOICE pages, keep current behavior.
- For VOICE pages where the active SELECT subpage is not `mix`, keep current
  `>`, `*`, `<` behavior.
- For `mix`:
  - compute `instrument_screen_count`;
  - if `screen < instrument_screen_count`:
    - return `'^'` for `screen == 0`;
    - return `'*'` for `screen > 0`;
  - otherwise return `'+'`.

Do not return `<` for `mix`.

#### `Core/Menu/MenuText.h`

No new global `TEXT_*` enum is required if the Scene setting table carries its
own three display strings. If the implementation chooses to integrate with
`valueNames[]`, add new short/category/long strings carefully and update
`NUM_NAMES`; however the table-local approach is lower risk and avoids touching
static descriptor text for non-static cells.

### 4. Complete Scene Load Apply And Boot From Scene 000

Files:

- `Core/Menu/menu.c`
- `main.c`
- `Core/Scene/Preset/presetManager.c`
- `Core/Scene/Preset/presetManager.h`

#### `Core/Menu/menu.c` - Scene Load OK starts busy

In `menu_handleLoadSaveMenu()`, the Load-page `SAVE_TYPE_SCENE` OK branch
currently clears busy only on failure. Mirror the other accepted filesystem
requests:

```c
if (preset_loadSceneForScenes(slot, menu_kitLoadSceneMask))
    menu_storageBusy = 1u;
else
    menu_storageBusy = 0u;
```

Why:

- Scene Load is an explicit OK action.
- While the filesystem state machine is parsing a multi-file Scene, encoder and
  load/save gestures must be locked the same way Kit/Instrument loads are.

#### `Core/Menu/menu.c` - `PRESET_OP_SCENE_LOAD`

Add a `PRESET_OP_SCENE_LOAD` case in `menu_pollPresetStatus()`.

Recommended behavior:

```c
case PRESET_OP_SCENE_LOAD:
    if ((menu_activePage == LOAD_PAGE || menu_activePage == SAVE_PAGE) &&
        !menu_isLoadSaveSelectionCurrent()) {
        retrySelectionAfterAck = 1u;
        retrySelectionLoadKit = 0u;
        break;
    }
    menu_startSoundApply(1u, 0u, 1u, 0u, 1u, 0u, 1u, 0u,
                         FS_STALE_WARNING_NONE);
    menu_refreshLoadSceneLeds();
    break;
```

The exact `menu_startSoundApply()` arguments should be checked against its
current signature, but the required effects are:

- Apply Scene settings via `preset_applySceneSettings()`.
- Apply all six kit voices through the chunked drumset apply cursor.
- Apply all six audio routes through `preset_applyKitAudioRouting()`, which now
  reads `scene->settings.audio_out`.
- Request Pattern UI mirror refresh, because Scene Load replaced
  `scene->pattern`.
- Clear `menu_storageBusy` through the existing sound-apply completion path.
- Repaint after the apply starts/completes as current Kit Load does.

Do not use the old flat `parameter_values[]` normalizer for Scene Load. Scene
Load commits descriptor-backed SceneData, so the descriptor runtime path must
remain the owner.

#### `main.c`

Replace:

```c
preset_loadKitForScenes(0, 1u);
```

with:

```c
preset_loadSceneForScenes(0, 1u);
```

Keep the existing synchronous polling loop:

```c
while (preset_getStatus() == PRESET_LOAD_IN_PROGRESS)
    filesystem_tick();
menu_pollPresetStatus();
```

Recommended boot failure policy:

- Do not silently fall back to Kit 000 during this development phase.
- If Scene 000 is absent or invalid, let the filesystem error overlay surface
  the failure. Silent fallback would hide broken Scene fixtures exactly when
  this migration needs validation.

Keep root Kit and Instrument scans at boot for browser readiness.

### 5. Define Thin `pattern.pat` And Initialize Staged Patterns Correctly

Files:

- `Core/Scene/Pattern/PatternData.h`
- `Core/Scene/Pattern/PatternData.c`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/filesystem.c`
- `tools/populate_scene_directory.py`

The current Scene loader expects the old 8116-byte binary bridge pattern. The
new Scene folder target asks for a thin `pattern.pat`. To make that real rather
than a fixture-only rename, add a guarded text stub for this phase:

```text
format=helicase.pattern
version=1
placeholder=1
```

Meaning for this phase:

- The file validates the Scene's Pattern child.
- It intentionally carries no step data.
- Loading it leaves the staged PatternSet at PatternData's default empty
  pattern.
- Later dynamic Pattern work can replace this schema with real pattern events.

#### `Core/Scene/Pattern/PatternData.h/c`

Add a PatternData-owned initializer for arbitrary PatternSet storage:

```c
void pat_initPatternSet(PatternSet *pattern_set, uint8_t next_pattern);
```

Implementation detail:

- This helper should contain the defaulting logic, not filesystem.c.
- `pat_initScene(scene_index)` should call it for the resident Scene's
  `scene->pattern`.
- `filesystem_initStagedScene()` should call it for `op_staged_scene.pattern`.

Important loops:

- Outer track loop: `track = 0..NUM_TRACKS-1`.
- Inner step loop: `step = 0..NUM_STEPS-1`.
- For each step, write the same defaults currently in `pat_resetStep()`:
  - note = `PAT_DEFAULT_NOTE`;
  - param1Nr/param2Nr = `INSTRUMENT_PARAM_INVALID`;
  - param1Val/param2Val = 0;
  - prob = 127;
  - volume = 100 with no `STEP_ACTIVE_MASK`.
- Per track:
  - `pat_mainSteps[track] = 0`;
  - `length = PAT_DEFAULT_TRACK_LENGTH`;
  - `rotate = 0`;
  - `scale = TRACK_SCALE_OFF`;
  - `shuffle = 0`.
- Pattern settings:
  - `changeBar = 0`;
  - `nextPattern = next_pattern`.

Why this must exist:

- A zeroed `PatternSet` has invalid musical defaults: probability 0, note 0,
  track length 0, and scale 0.
- Thin pattern load needs a valid empty pattern without duplicating PatternData
  internals in the filesystem.

#### `Core/Hardware/SD/storageTypes.h/c`

Add a tiny parser/formatter next to the effect placeholder schema:

```c
typedef struct {
    uint8_t seen_format;
    uint8_t seen_version;
    uint8_t seen_placeholder;
} storage_pattern_stub_state_t;
```

Functions:

```c
void storage_patternStubStateInit(storage_pattern_stub_state_t *state);
storage_status_t storage_patternStubParseLine(storage_pattern_stub_state_t *state,
                                              const char *line);
storage_status_t storage_patternStubFinalize(const storage_pattern_stub_state_t *state);
uint8_t storage_formatPatternStubLine(char *dst, uint16_t capacity,
                                      uint16_t line_index);
```

The parser mirrors the effect placeholder:

- `format` must equal `helicase.pattern`.
- `version` must equal 1.
- `placeholder` must equal 1.
- Unknown keys are ignored for forward compatibility.

#### `Core/Hardware/SD/filesystem.c`

Change Scene Load pattern handling:

- When opening `op_scene_pattern_open_name`, read enough to decide whether the
  file is text stub or legacy binary.
- Simple option: attempt text line parsing first. If the first bytes do not
  match a valid key/value text line, fall back to the current binary bridge
  reader.
- Better state-machine option: add an explicit phase that reads the first
  small chunk, detects `format=`, then dispatches either text-stub phases or
  existing binary phases.

Compatibility:

- Keep binary bridge load support for existing `SD_CARD/Scene/001 Slak` and any
  user cards created by `tools/populate_scene_directory.py` before this pass.
- New Scene Save writes only the text stub.

Add Scene Save writer phases for `pattern.pat` using
`storage_formatPatternStubLine()`.

### 6. Add Scene Save

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Menu/menu.c`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/storageTypes.c`

#### Preset API

Add:

```c
uint8_t preset_saveScene(uint16_t slot,
                         uint8_t source_scene,
                         const char display_name[8]);
```

Add enum:

```c
PRESET_OP_SCENE_SAVE
```

Add callback:

```c
static void on_scene_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_SCENE_SAVE);
}
```

`preset_saveScene()` behavior mirrors `preset_saveDrumset()`:

- Call `filesystem_ack()`.
- Set `pm_status = PRESET_LOAD_IN_PROGRESS`.
- Set `pm_completed_op = PRESET_OP_NONE`.
- Set `pm_request_slot = slot`.
- Set `pm_request_type = SAVE_TYPE_SCENE`.
- Call `filesystem_requestSaveSceneDirectory()`.
- Restore `pm_status = PRESET_IDLE` on request rejection.

#### Filesystem public API

Add:

```c
bool filesystem_requestSaveSceneDirectory(uint16_t slot,
                                          uint8_t source_scene,
                                          const char display_name[8],
                                          fs_completion_cb_t cb);
```

Update the generic descriptor path:

- `fs_file_descs[FS_FILE_SCENE].supports_save` becomes 1.
- `filesystem_requestSave(FS_FILE_SCENE, ...)` can either call the new
  directory request with active Scene/current name when a caller exists, or
  remain false if all production calls go through `preset_saveScene()`. The
  important requirement is that the explicit public Scene Save API exists.

#### Filesystem internal state

Add internal op:

```c
FS_INTERNAL_OP_SAVE_SCENE
```

Add error-code text:

```c
case FS_INTERNAL_OP_SAVE_SCENE: return "ScnS";
```

Add dispatch in `filesystem_tick()`:

```c
case FS_INTERNAL_OP_SAVE_SCENE:
    filesystem_saveSceneDirectory_tick();
    break;
```

Add request-time state:

```c
static uint8_t op_scene_save_source_scene;
static char op_save_scene_dir_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_scene_dir_open_name[AFATFS_SHORT_FILENAME_MAX];
static char op_save_scene_kit_dir_display_name[AFATFS_LONG_FILENAME_MAX + 1u];
static char op_save_scene_kit_dir_open_name[AFATFS_SHORT_FILENAME_MAX];
```

Reuse where safe:

- `op_save_kit_member_display_file[6]` can be reused for embedded instrument
  filenames because the filesystem is single-operation.
- `op_write_line_*` can be reused for sceneset, kitset, instrument, pattern
  stub, and effect placeholder text streaming.

Reset all new fields in `filesystem_start()`.

#### Scene directory deletion

Current Kit Save deletes every physical directory matching the target Kit slot
before writing the replacement. Scene Save needs the same policy under
`/Scene/`.

Plan:

- Generalize `filesystem_deleteKitSlotDirectoriesStart/tick()` into a helper
  that accepts a root name/cache kind, or add a Scene-specific copy if
  generalizing risks the already-tested Kit path.
- The scan loop must parse visible directory names with
  `storage_parseNumberedFolder()`.
- It must delete all matches where parsed `slot == op_slot`, not only the name
  currently cached in `scene_slot_open_name[op_slot]`.

Why:

- FAT cards can contain duplicate physical folders like `000 Slak` and
  `000 Test`.
- The scan cache exposes only one display entry, but overwrite semantics must
  replace the whole numbered slot.

#### `filesystem_saveSceneDirectory_tick()`

State-machine outline:

1. Validate `source_scene`, `slot < STORAGE_SCENE_MAX_SLOTS`, and non-null
   `display_name`.
2. `chdir(NULL)` to root.
3. Create/open root `Scene/` with `afatfs_mkdir_lfn()`.
4. `chdir` into root `Scene/`, close the root handle.
5. Delete every physical directory for the target Scene slot.
6. Create `Scene/NNN Name/`.
7. `chdir` into the new Scene directory.
8. Write `sceneset.scg`.
9. Create `Kit <kit name>/`.
10. `chdir` into the embedded Kit directory.
11. Write embedded `kitset.kcg` without `audio_out`.
12. Loop over six instruments:
    - open the generated member filename;
    - stream `storage_formatInstrumentLineView()`;
    - close;
    - increment `op_instrument_slot`.
13. Return to the Scene directory.
14. Write thin `pattern.pat` text stub.
15. Write dummy `effects.fx`.
16. Return to root.
17. Update the Scene scan cache with the saved directory.
18. Store the resident Scene display name with `scene_setSceneDisplayName()`.
19. Finish through `filesystem_finish(FS_STATUS_DONE)` so flush handling runs.

Important variable/loop details:

- `op_scene_save_source_scene` is captured at request time so menu movement
  cannot retarget the save.
- `op_save_scene_dir_display_name` is built as `NNN ` plus eight display cells,
  same as Kit folders.
- The embedded Kit directory name should be `Kit ` plus the resident Kit
  display name from `scene_kitDisplayName(source_scene)`. If the Kit name is
  all spaces, still create `Kit         ` or sanitize to `Kit SceneKit`; decide
  during implementation and document it. Preferred for fidelity is preserving
  the blank display name, because blank is a valid resident name elsewhere.
- Instrument filename generation uses the same loop as Kit Save:

```c
for (voice = 0u; voice < STORAGE_KIT_SLOT_COUNT; voice++)
    storage_makeSavedInstrumentDisplayFilename(..., voice + 1u, 1u);
```

- The instrument writer context uses `scene->settings.voice_morph_amount[slot]`
  and normal save mode, not Morph Save mode.
- `op_write_line_index` is reset to 0 before every text file.
- `filesystem_writeTextLine()` writes one line in chunks; it may require
  multiple ticks if `afatfs_fwrite()` accepts only part of the line.

#### Scene scan cache after save

Add or reuse:

```c
filesystem_recordSavedSceneDirectory(op_save_scene_dir_display_name,
                                     op_save_scene_dir_open_name);
```

It should mirror `filesystem_recordSavedKitDirectory()`:

- Parse the numbered folder.
- Mark `scene_slot_present[slot] = 1`.
- Store display name and open alias.

### 7. Promote Save:[Scene] In Menu

Files:

- `Core/Menu/menu.c`

Change `menu_loadSaveTypeIsRestored()`:

- On Save page, return true for `SAVE_TYPE_SCENE` once the writer exists.

Change `menu_loadSaveSaveTypes[]`:

```c
static const uint8_t menu_loadSaveSaveTypes[] = {
    SAVE_TYPE_FILE,
    SAVE_TYPE_DIR,
    SAVE_TYPE_SIMPLE_DIR,
    SAVE_TYPE_KIT,
    SAVE_TYPE_KIT_MORPH,
    SAVE_TYPE_SCENE
};
```

Update `menu_currentSaveWouldOverwrite()`:

- If `menu_saveOptions.what == SAVE_TYPE_SCENE`, return
  `filesystem_sceneSlotExists(menu_currentPresetNr[SAVE_TYPE_SCENE])`.

Update Save-page OK dispatch:

- Add `SAVE_TYPE_SCENE`:

```c
if (preset_saveScene(menu_currentPresetNr[SAVE_TYPE_SCENE],
                     scene_getActiveIndex(),
                     preset_currentName))
    menu_storageBusy = 1u;
```

Update name seeding:

- When moving from preset number to name editing on Save:[Scene], seed:

```c
memcpy(preset_currentName,
       scene_sceneDisplayName(scene_getActiveIndex()),
       8u);
```

- Do not use `scene_kitDisplayName()` and do not use
  `filesystem_sceneSlotName()`. The former names the embedded Kit; the latter
  names the target library slot, not the resident Scene.

Update save completion cleanup:

- Add `PRESET_OP_SCENE_SAVE` to the shared save completion group in
  `menu_pollPresetStatus()`.
- On success, clear `menu_storageBusy` and reset Save UI like Kit Save.
- The resident Scene name should be updated by filesystem/Preset only after the
  filesystem save succeeds.

### 8. Update Fixtures And Generator

Files:

- `SD_CARD/Scene/000 Slak/sceneset.scg`
- `SD_CARD/Scene/000 Slak/Kit Slak/kitset.kcg`
- `SD_CARD/Scene/000 Slak/pattern.pat`
- `SD_CARD/Scene/000 Slak/effects.fx`
- `SD_CARD/Scene/001 Slak/...` if kept as a compatibility fixture
- `tools/populate_scene_directory.py`

Fixture changes for `000 Slak`:

- Remove `SD_CARD/Scene/.DS_Store`.
- Remove `SD_CARD/Scene/000 Slak/.DS_Store`.
- Add to `sceneset.scg`:

```text
audio_out=2,0,0,0,0,1
fx_send_amount=0,0,0,0,0,0
fader_setting=0,0,0,0,0,0
```

- Remove all `audio_out=` lines from embedded `Kit Slak/kitset.kcg`.
- Replace binary `pattern.pat` with:

```text
format=helicase.pattern
version=1
placeholder=1
```

- Keep `effects.fx`:

```text
format=helicase.effect
version=1
placeholder=1
```

Generator changes:

- `make_sceneset()` should include the three new Scene setting lines.
- When converting an existing Kit, parse its six `audio_out` values before
  removing them from the copied embedded `kitset.kcg`.
- `make_empty_bridge_pattern()` should be replaced or supplemented with
  `make_thin_pattern_stub()`.
- The tool should not create `.DS_Store` files, but the fixture cleanup is still
  needed because host-created dot files are real asyncfatfs objects.

### 9. Update Specification And Boundary Docs

Files:

- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`
- `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md`
- `MEMORY.md` after implementation, not during this planning-only turn

Required doc updates:

- `scene_settings_t` includes `audio_out[6]`, `fx_send_amount[6]`, and
  `fader_setting[6]`.
- `kitset.kcg` no longer owns or requires `audio_out`.
- Old `audio_out` in `kitset.kcg` is compatibility-only and only used as a
  Scene-load fallback when `sceneset.scg` lacks `audio_out`.
- New Scene Save writes:
  - `sceneset.scg`;
  - embedded `Kit <name>/kitset.kcg`;
  - six embedded instrument files;
  - thin placeholder `pattern.pat`;
  - placeholder `effects.fx`.
- Boot loads root Scene slot `000`.
- Menu owner boundary: VOICE `mix` Scene setting cells commit through Preset,
  not through descriptor images and not through direct Menu writes.

## Verification Checklist

1. Build with `make`.
2. Boot with `SD_CARD/Scene/000 Slak/`.
3. Confirm boot no longer depends on root `Kit/000`.
4. Confirm Scene 000 load validates `sceneset.scg`, embedded Kit, thin
   `pattern.pat`, and `effects.fx`.
5. Confirm loaded audio routing matches `audio_out=2,0,0,0,0,1`.
6. Confirm root Kit Load preserves current Scene routing instead of importing
   `audio_out` from old Kit files.
7. VOICE `mix` page:
   - first instrument screen marker is `^`;
   - later instrument screens use `*`;
   - Scene setting screens use `+`;
   - no `mix` screen shows `<`;
   - non-mix subpages keep existing marker behavior.
8. Edit audio out from a Scene setting screen and confirm active mixer routing
   changes immediately.
9. Edit FX send amount and fader setting and confirm retained SceneData values
   change.
10. Confirm SHIFT+VOICE Morph endpoint editing does not affect Scene settings.
11. Confirm STEP MIDI channel/note page still edits Scene settings.
12. Load:[Scene] slot scrolling changes display only; OK starts load and locks
    `menu_storageBusy`.
13. Save:[Scene] writes a clean folder:
    - `sceneset.scg` contains all Scene setting lines;
    - embedded `kitset.kcg` has no `audio_out`;
    - `pattern.pat` is the thin stub;
    - `effects.fx` is the placeholder.
14. Reboot after Save:[Scene] and confirm the saved Scene loads.

## Open Decisions

- Fader mode display strings. Recommended compact strings are `pre`, `pst`,
  and `fx `.
- Embedded Kit directory name when the resident Kit name is blank. Preserve the
  blank for consistency, or sanitize to a nonblank fallback for filesystem
  readability.
- Whether `SD_CARD/Scene/001 Slak` remains as a legacy binary-pattern
  compatibility fixture or is migrated together with `000 Slak`.
- Whether root Kit Load should ever use legacy `audio_out` as a one-time
  compatibility import. This plan says no: routing is now Scene data, and root
  Kit Load should preserve it.

## Implementation Notes

### 2026-07-15 Scene Data Ownership Pass

- Moved `audio_out[6]` out of `kit_settings_t` and into `scene_settings_t`
  alongside retained `fx_send_amount[6]` and `fader_setting[6]`.
- Added SceneData getter/setter boundaries for those three per-voice Scene
  settings. `audio_out` uses persisted route domain `0..5`; FX send clamps to
  `0..127`; fader mode clamps to `0..2`.
- Updated Preset to apply active mixer routing from SceneData instead of Kit
  settings, and added Preset setters so UI/storage code can mutate the new
  Scene fields without writing SceneData directly.
- Changed `storage_kitset_t` so legacy `audio_out` lines are parse-only side
  data. `storage_kitsetFinalize()` no longer requires those lines, and new Kit
  writers must not emit them.
- Added `audio_out`, `fx_send_amount`, and `fader_setting` parsing to
  `sceneset.scg`, with seen flags that let Scene Load distinguish explicit new
  Scene data from legacy embedded Kit fallback.
- Updated filesystem staged Scene initialization to seed Scene-owned route, FX,
  and fader defaults before optional `sceneset.scg` lines are parsed.
- Removed `audio_out` emission from `filesystem_nextKitsetLine()`. The slot
  writer now has four lines per slot: section, type, file, blank separator.
- Added the Scene Load compatibility bridge after embedded `kitset.kcg`
  finalization: if `sceneset.scg` lacks `audio_out` and the embedded Kit has a
  complete legacy six-value block, those values are imported into staged Scene
  routing with per-slot default fallback for out-of-domain bytes.
- Added `pat_initPatternSet()` so filesystem staging and thin pattern stubs can
  initialize a caller-owned `PatternSet` without mutating resident SceneData.
- Added thin text placeholder parsing/writing for `pattern.pat` and placeholder
  writing for `effects.fx`. Scene Load now probes `.pat` files for the
  `format=` prefix and accepts either the new text stub or the older binary
  bridge pattern stream.
- Added root Scene Save plumbing: filesystem request API, Preset completion,
  Menu Save:[Scene] dispatch, streamed `sceneset.scg`, embedded Kit writer,
  six embedded Instrument files, `pattern.pat`, and `effects.fx`.
- Changed boot to load root `Scene/000` through `preset_loadSceneForScenes()`
  instead of loading root `Kit/000`.
- Added VOICE `mix` Scene-setting screens after the instrument mix screens.
  The first implementation exposed all six `audio_out`, `fx_send_amount`, and
  `fader_setting` values in one shared stack; the correction below narrows this
  to the selected voice only.
- Updated `SD_CARD/Scene/000 Slak` to the new Scene-owned data shape:
  `sceneset.scg` now includes the three per-voice Scene setting lines,
  embedded `kitset.kcg` no longer contains `audio_out`, `pattern.pat` is the
  thin text stub, and `.DS_Store` was removed.
- Updated `tools/populate_scene_directory.py` to move legacy embedded Kit
  routing into `sceneset.scg`, strip `audio_out` from copied kitsets, write
  thin pattern stubs, and ignore `.DS_Store`. Updated
  `tools/convert_legacy_kits.py` so regenerated root Kit manifests stop
  emitting `audio_out`.
- Updated `MEMORY.md`, `FILESYSTEM_SPEC.md`, and `MODULE_INTERCHANGE_SPEC.md`
  to reflect Scene-owned routing/FX/fader data, current Scene Save/Load, and
  kitset `audio_out` as compatibility-only side data.

### 2026-07-15 Scene Data Retest Corrections

- Promoted `SAVE_TYPE_SCENE` into the Save-page whitelist and explicit Save
  type cycle. The Save dispatch branch already existed, but the menu could not
  reach `Save:[Scene   ]` until the type gate included it.
- Changed top-level OK/OW completion behavior so confirmed load/save operations
  return the selection to `SAVE_STATE_EDIT_TYPE` when they finish or fail.
  Scene Load now passes `resetSave=1` into `menu_startSoundApply()` only when
  launched from Load/Save, and File/Dir diagnostic result overlays reset the
  underlying cursor immediately. Live Kit/KitMrp browsing remains on the slot
  row because those loads are not OK-confirmed.
- Split VOICE `mix` Scene settings by selected voice. `MENU_SCENE_SETTING_COUNT`
  is now three cells per VOICE page, and `menu_resolveSceneSettingCell()` maps
  index `0,1,2` to the active page's `audio_out`, `fx_send_amount`, and
  `fader_setting` through `menu_voicePageToSlot(menu_activePage)`. The fourth
  compact column stays empty.
- Removed `name` from the `sceneset.scg` writer, generator, fixture, and
  required parse state. `storage_scenesetParseLine()` ignores legacy `name=`
  lines for compatibility only, but neither stores them nor requires them.
- Added `op_scene_child_display_name` to Scene Load. The child scan captures
  the display bytes after `Kit ` from the embedded Kit directory and commits
  them into `op_staged_scene.kit.display_name` after `kitset.kcg` validates, so
  booting `Scene/000 Slak/Kit Slak/` immediately seeds Save Kit's character
  editor with `Slak`.

### 2026-07-15 Scene Save Retest Corrections

- Fixed Save Scene name seeding for empty target slots. When the Save page moves
  from slot number editing into the character editor for `SAVE_TYPE_SCENE`,
  Menu now copies `scene_sceneDisplayName(scene_getActiveIndex())` into
  `preset_currentName`, matching Save Kit's resident-name behavior and avoiding
  the filesystem empty-slot sentinel `Empty`.
- Fixed boot/manual Scene Load retained Scene identity. Before copying
  `op_staged_scene` into resident SceneData, filesystem now writes
  `op_scene_display_name` from the root `Scene/<NNN Name>/` directory into
  `op_staged_scene.display_name`. This is required because `sceneset.scg` must
  not contain `name`.
- Hardened Scene Save cleanup against root-folder wipes. The shared slot-delete
  worker now has a generic start function with an `allow_short_alias` flag:
  Kit Save keeps alias fallback for old Kit folders, while Scene Save uses
  `filesystem_deleteSceneSlotDirectoriesStart()` and deletes only visible child
  names that parse as the exact requested numbered Scene slot. Other Scene
  folders and their nested Kit/pattern/effect contents are not eligible.
