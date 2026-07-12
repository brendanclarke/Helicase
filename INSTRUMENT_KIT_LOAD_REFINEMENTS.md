# Instrument / Kit Load Refinements

Detailed implementation plan for the current Load/Save menu cleanup and the
Scene-aware Kit/Instrument Load behavior. This document is based on the live
implementation after `INSTRUMENT_LOAD_MENU_FIX.md`, not on the filesystem or
scoping reference documents.

No production code is changed by this planning pass.

## Confirmed Current State

- `Core/Menu/menu.h` still exposes seven shared `SAVE_TYPE_*` values:
  Kit, Pattern, MorphKit, Perform, All, Settings, and Samples.
- `menu_handleLoadSaveMenu()` uses that shared enum for both pages. It can still
  initiate legacy Pattern/Morph/Performance/All load or save paths even though
  those formats are no longer valid product options.
- Root Kit browsing is immediate: moving a Kit number calls
  `preset_loadDrumset()` through `menu_requestCurrentLoadSaveSelection(1)`.
- The Instrument Load submode has separate type/file browser state, but it
  currently calls `menu_instrumentLoadRequestSelection()` after a type change.
  It therefore loads a root `Instrument/` file merely by changing the type.
- The current nested Instrument Load repaint correctly follows
  `menu_saveOptions.state` and `editModeActive`, as fixed in
  `INSTRUMENT_LOAD_MENU_FIX.md`.
- `SceneData` currently allocates `SCENE_COUNT == 1`, but its accessors are
  already Scene-indexed. `PatternData` stores a `PatternSet` inside each
  `scene_t` and validates its first coordinate through `scene_indexValid()`.
- `PatternData` has active-step accessors for one step but does not yet expose
  a Scene-level "any active step" query.
- The SEQ buttons are currently intercepted in `buttonHandler.c:processPress()`
  and always routed to step/roll behavior. The SEQ LED aliases are
  `LED_SEQ1..LED_SEQ16`, mapped to the physical STEP row.
- `kit_instrument_slot_t` deliberately stores type and parameter images only.
  It does not retain the source filename or display stem from `kitset.kcg`, so
  Instrument Load cannot currently display the selected slot's Kit instrument
  name.
- Directory Kit loading and root Instrument loading parse directly into
  `scene_getActiveIndex()`. A multi-Scene Kit target selection therefore needs
  an explicit parse/apply boundary before the UI can honestly target more than
  the active Scene.

## Required Behavior

| Context | SEQ button meaning | SEQ LED base state | Blink state | Load action |
|---|---|---|---|---|
| Load: Kit | Toggle a valid Scene in the Kit target set. | On only when that Scene has at least one active Pattern step. | Every selected Kit target, including the active Scene by default. | Encoder-selected Kit is loaded into every selected target Scene. |
| Load: Instrument | Select exactly one valid destination Scene. | On only when that Scene has at least one active Pattern step. | Selected/current active Scene. | A lower-row root Instrument file scroll loads one file into one slot of that Scene. |
| Load: Settings or Samples | Retain existing meaning; SEQ row is not a Scene selector. | Cleared. | Cleared. | Existing Settings/Samples behavior. |
| Save page | No Scene Load controls. | Cleared. | Cleared. | Save behavior remains for the subsequent save-function phase. |

For the current one-Scene build, only SEQ1 is valid and it begins selected.
The implementation must nevertheless loop over `SCENE_COUNT` and use a
16-bit target mask so raising `SCENE_COUNT` later does not require rewriting
Load-menu button or LED ownership.

Instrument Load entry and browsing behavior is:

1. Pressing a VOICE button from Load enters Instrument Load for that slot.
2. The top type row is immediately selected and shown with brackets, so
   `editModeActive = 1` and `menu_saveOptions.state = SAVE_STATE_EDIT_TYPE`.
3. The bottom row initially reads `kit <stem>` using the current Kit source
   display stem for that destination slot. It has no root-pool number.
4. Changing the top-row type updates only the type label and assignment-policy
   eligibility. It does not load a file and does not replace the bottom row.
5. The first lower-row encoder movement resolves an item from the selected
   root Instrument type, replaces the retained `kit <stem>` or previous pool
   display with `[###]<stem>`, and immediately loads that file.
6. Later type changes retain the currently displayed `kit <stem>` or previous
   `[###]<stem>` display until the lower row is moved again. They never cause
   an implicit load.

## Implementation Order

1. Reduce Load/Save option identity to the valid visible options and remove
   stale menu request/dispatch branches.
2. Add the PatternData Scene activity query.
3. Add Scene-target state and LED rendering for the Load page, then intercept
   SEQ button presses only in that context.
4. Make Kit loading accept the selected Scene mask through a staged Kit parse
   and apply the active Scene only to runtime DSP.
5. Make root Instrument loading accept one explicit Scene destination.
6. Retain Kit instrument display stems in Scene Kit metadata.
7. Rework the Instrument Load display/browse state machine around a retained
   bottom-row source, then build and exercise the one-Scene behavior.

## 1. Remove Invalid Load/Save Options

Files:

- `Core/Menu/menu.h`
- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.c`
- `Core/Scene/Preset/presetManager.h` only if menu-only legacy wrappers become
  otherwise unused after the call-site audit.

### 1.1 Replace the shared visible-option enum

Replace the current contiguous values for Pattern, MorphKit, Perform, and All
with a three-option visible menu identity:

- `SAVE_TYPE_KIT = 0`
- `SAVE_TYPE_GLO`
- `SAVE_TYPE_SAMPLES`
- `NUM_SAVE_TYPES`

Keep the current type names for Kit, Settings, and Samples so the existing
load/save rendering code stays readable. Change `NUM_PRESET_LOCATIONS` from
five to one because Kit is the only remaining numbered Load/Save location.

Code-adjacent comment required above the enum in `menu.h`:

- What: define only the currently valid user-facing Load/Save choices.
- Why: Pattern, legacy MorphKit, Perform, and All have no valid current file
  model and must no longer be selectable merely because obsolete filesystem
  operations still exist.
- Why this is not a generic file-type enum: `fs_file_type_t` remains the
  filesystem's internal compatibility vocabulary; this enum is specifically
  the Menu's visible choice set.
- Inputs/outputs: Menu stores one enum value in `menu_saveOptions.what`; the
  value determines render text, navigation bounds, and valid request dispatch.
- Clients/affiliates: `menu_handleLoadSaveMenu()`,
  `menu_repaintLoadSavePage()`, `menu_requestCurrentLoadSaveSelection()`, and
  Preset request bookkeeping.

### 1.2 Remove invalid type labels, request paths, and completion cases

In `menu.c`:

- Remove `Pattern`, `MorphKit`, `Perform`, and `All` labels from
  `menu_repaintLoadSavePage()`.
- Simplify its numbered-bottom-row condition so only Kit has a numbered
  location/name. Settings and Samples retain the existing non-numbered UI.
- Remove the special MorphKit rules from Load-page OK/cursor navigation.
- In `menu_requestCurrentLoadSaveSelection()`, make Kit the only immediate
  directory-kit request. Settings keeps `preset_loadName()`/globals behavior as
  appropriate; Samples retains its modal load command.
- Remove the Pattern/Morph/Performance/All branches from the button-click
  dispatch in `menu_handleLoadSaveMenu()`.
- Clamp type stepping against `NUM_SAVE_TYPES - 1`, not the old Samples
  position in a larger enum.
- Remove stale completion handling from `menu_pollPresetStatus()` only when
  it is exclusively reachable from the removed menu paths. Preserve unrelated
  boot or diagnostic behavior until the call-site audit proves it dead.

Code-adjacent comment required above the reduced request/dispatch switch:

- What: route only valid visible menu choices to Kit, Settings, or Samples
  operations.
- Why: keeping a dead case in an encoder-driven visible selector allows the
  user to reach unsupported storage workflows and makes future Save work start
  from the wrong contract.
- Why it cannot be folded into repaint: repaint only hides text; request and
  completion paths are independent state transitions that must stop accepting
  invalid selections.
- Inputs/outputs: `menu_saveOptions.what`, page, encoder/OK state; outputs are
  a valid Preset/filesystem request or no request for unavailable Save actions.
- Clients/accessors/affiliates: `preset_loadDrumset()`,
  `preset_loadGlobals()`, `menu_loadSamplesModal()`,
  `menu_pollPresetStatus()`, and `menu_currentPresetNr`.

### 1.3 Preserve backend code unless it loses every caller

Do not delete `filesystem.c` serializers or Preset APIs for Pattern/Morph/All
solely because this menu stops exposing them. First run a repository-wide
call-site audit after removing the UI references. Remove a backend operation
only if no boot path, migration path, test, or other owner still calls it.
This refinement is intentionally about invalid Load/Save options, not a
destructive file-format purge immediately before the new save implementation.

If a Preset function becomes unreferenced, delete its declaration, callback,
operation enum entry, and helper mapping together; do not retain a thin dead
wrapper just to preserve old menu nomenclature.

## 2. Add PatternData Scene Activity Query

Files:

- `Core/Scene/Pattern/PatternData.h`
- `Core/Scene/Pattern/PatternData.c`

Add:

```c
uint8_t pat_sceneHasActiveSteps(uint8_t scene_index);
```

Implementation requirements:

- Validate the Scene with `pat_patternValid(scene_index)` before accessing
  storage.
- Borrow the one `scene_t` through `scene_getConst(scene_index)`.
- Scan `NUM_TRACKS` by `NUM_STEPS` and return nonzero as soon as any
  `Step.volume` carries `STEP_ACTIVE_MASK`.
- Do not derive the answer from `pat_mainSteps`: it is explicitly a legacy
  16-step compatibility mask and playback/recording use the complete Step
  records.
- Do not make the caller loop through `pat_isStepActive()`. That would repeat
  bounds/pointer work in menu/LED code and would expose the current 7 by 128
  storage shape outside PatternData.

Code-adjacent comment required above the declaration and implementation:

- What: return whether a Scene's complete PatternData record contains any
  active sequencer step.
- Why: Load-menu Scene LEDs need a stable Scene-level activity answer while
  PatternData remains the owner of the underlying layout.
- Why separate: this is not a thin alias for `pat_isStepActive()`; it owns the
  nested scan, chooses the authoritative Step active bit over the bridge mask,
  and hides a storage shape scheduled to change during later Scene/Bank work.
- Inputs: Scene index.
- Outputs: boolean activity result; invalid Scene returns false.
- Clients/accessors/affiliates: Load-menu Scene LED refresh,
  `scene_getConst()`, `PatternSet.pat_subStepPattern`, `STEP_ACTIVE_MASK`, and
  future Scene browser summaries.

Code-adjacent loop comment required immediately before the nested scan:

- Explain that the scan covers all seven trigger tracks and all 128 bridge
  steps, exits on the first active bit, and is foreground-only UI work rather
  than a sequencer-timing operation.

## 3. Scene Selection State and SEQ LED Presentation

Files:

- `Core/Menu/menu.h`
- `Core/Menu/menu.c`
- `Core/Hardware/frontPanel/buttonHandler.c`
- No new `ledHandler` API is needed.

### 3.1 Keep Load-specific Scene state in Menu

Add private Menu state:

```c
static uint16_t menu_kitLoadSceneMask;
static uint8_t menu_instrumentLoadScene;
```

`menu_kitLoadSceneMask` is a bit per valid resident Scene. Initialize it to the
active Scene bit whenever Kit Load becomes active. `menu_instrumentLoadScene`
initializes to `scene_getActiveIndex()` whenever Instrument Load is entered.
The physical row supports sixteen buttons, so use `uint16_t` and construct a
valid mask with a loop; do not use `(1u << SCENE_COUNT) - 1u`, which is invalid
when the future count reaches sixteen.

Add public Menu entry point:

```c
uint8_t menu_loadSceneButtonPressed(uint8_t scene_index);
```

It returns nonzero only when the current context consumes a SEQ button:

- `LOAD_PAGE` + Kit visible type: toggle `scene_index` in the Kit mask.
- `LOAD_PAGE` + `menu_instrumentLoadActive`: replace the one Instrument
  destination Scene selection.
- any other page/type: return zero and leave normal step/roll behavior alone.

For the current one-Scene build, the active Scene is the only valid target.
The function must still validate with `scene_indexValid()` rather than test a
literal zero. When `SCENE_COUNT` grows, the same handler becomes the one
selection boundary for the already-wired 16 physical SEQ buttons.

Code-adjacent comment required above the state and public function:

- What: retain Kit multi-target selection and Instrument single-target
  selection independently of Sequencer step gestures.
- Why: the physical SEQ row is temporarily repurposed as Scene controls only
  in Load contexts; buttonHandler cannot safely own this data because Menu
  owns the Load submode and request timing.
- Why two fields: Kit Load genuinely permits several target Scenes, while
  Instrument Load explicitly permits one Scene and one Instrument at a time.
  A shared bitmask would allow invalid Instrument multi-loads; a single value
  would discard Kit's toggle semantics.
- Inputs/outputs: zero-based physical Scene index; retained selection state;
  return value tells buttonHandler whether normal sequencer action is skipped.
- Clients/accessors/affiliates: buttonHandler SEQ press routing,
  `scene_indexValid()`, `scene_getActiveIndex()`, Kit request dispatch, and
  Load-row LED refresh.

### 3.2 Add one internal Load Scene LED refresh helper

Add a private `menu_refreshLoadSceneLeds()` that:

1. Clears `LED_SEQ1..LED_SEQ16` base values and their old blink state.
2. Returns after clearing unless `menu_activePage == LOAD_PAGE` and either the
   visible type is Kit or Instrument Load is active.
3. Loops from Scene zero while both `scene_index < SCENE_COUNT` and
   `scene_index < 16`.
4. Sets each base LED on only when `pat_sceneHasActiveSteps(scene_index)` is
   true.
5. Starts blinking every selected bit of `menu_kitLoadSceneMask` in Kit mode;
   starts blinking exactly `menu_instrumentLoadScene` in Instrument mode.
6. Ensures the active Scene begins selected on entering Kit Load and is the
   initial Instrument destination, satisfying the active-Scene blink rule in
   the current one-Scene system.

Use existing `led_clearSequencerLeds()`, `led_setValue()`,
`led_setBlinkLed()`, and `led_clearAllBlinkLeds()` carefully. Do not change
`ledHandler`'s general blink model: Load menus only need to compose current
base LED values with the established persistent blink slots.

Call the helper after all Load-context transitions that can change its result:

- entering or leaving `LOAD_PAGE` in `menu_switchPage()`;
- type selection changes in the normal Load branch;
- Instrument Load entry, destination-slot changes, and exit;
- a consumed Scene button press;
- Kit/Instrument load completion that changes current Scene Kit metadata.

Code-adjacent comment required above the helper and before its Scene loop:

- What: paint Scene occupancy and current Load targets onto the SEQ row.
- Why: the LED row must derive its base state from PatternData on every Load
  context refresh; it cannot reuse the sequencer chase/step LEDs that represent
  a different UI meaning.
- Why separate from `menu_repaintLoadSavePage()`: LCD rendering can run for
  incremental refreshes and should remain free of shift-register side effects.
  The LED helper has a distinct hardware-facing ownership and is also called
  after button-only Scene selection changes.
- Inputs/outputs: current Load type/submode, Scene selection state, PatternData
  activity; outputs are SEQ row base values plus blink membership.
- Clients/accessors/affiliates: page switch, encoder type changes,
  `menu_loadSceneButtonPressed()`, `pat_sceneHasActiveSteps()`, and LED APIs.
- Loop comment: explain the dual `SCENE_COUNT`/16 limit and why it avoids both
  invalid resident Scene access now and invalid physical LED access later.

### 3.3 Intercept SEQ buttons before step/roll handling

In `buttonHandler.c:processPress()`, keep `btn_to_seq()` as the physical
decoder but call `menu_loadSceneButtonPressed()` before
`buttonHandler_seqButtonPressed()`. If Menu returns nonzero, return from
`processPress()` so no step selection, timer action, roll, or shift behavior
runs for that press.

In `processRelease()`, apply the same context-aware consumption rule before
`buttonHandler_seqButtonReleased()` so a Load-menu Scene press cannot create a
release-side step toggle or roll stop. Either expose a read-only Menu predicate
for this exact context or make `menu_loadSceneButtonPressed()` establish an
existing per-button consume bit in buttonHandler. Prefer a dedicated
`menu_loadSceneButtonsAreActive()` predicate only if both press and release
need the same read-only context check; otherwise do not add a thin public
accessor.

Code-adjacent comment required above this early routing block:

- What: temporarily reinterpret SEQ presses/releases as Load Scene controls.
- Why: `processPress()` currently routes every SEQ button to Pattern step or
  performance roll code before any menu-specific decision can occur.
- Why it cannot live in `buttonHandler_seqButtonPressed()`: that function owns
  normal sequencer gestures and would need knowledge of Menu's nested
  Instrument Load state. The early Menu query preserves a narrow, reversible
  ownership boundary.
- Inputs/outputs: physical SEQ index; Menu consumed/not-consumed result;
  output either updates Scene selection/LEDs or continues unchanged to normal
  sequencer behavior.
- Clients/accessors/affiliates: `btn_to_seq()`, Menu Load APIs, shift state,
  press/release pairing, step/roll handlers, and LED refresh.

## 4. Load a Kit Into Every Selected Scene

Files:

- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Scene/SceneData.h` only if a small explicit Kit-copy helper proves
  necessary after the ownership audit.

### 4.1 Carry the Kit Scene mask through Menu, Preset, and filesystem

Replace the Kit-only call made by
`menu_requestCurrentLoadSaveSelection(1)` with a Kit request that includes
`menu_kitLoadSceneMask`. Do not make the generic `filesystem_requestLoad()`
grow hidden Kit-specific arguments. Add a typed Kit request instead, for
example:

```c
uint8_t preset_loadKitForScenes(uint8_t kit_slot, uint16_t scene_mask);
bool filesystem_requestLoadKitForScenes(uint8_t kit_slot,
                                        uint16_t scene_mask,
                                        fs_completion_cb_t cb);
```

Rename or remove the old `preset_loadDrumset(..., isMorph)` menu-facing path
only after auditing boot and remaining compatibility callers. The desired end
state is that current directory Kit Load takes an explicit target mask; legacy
Morph loading, if retained temporarily as non-UI compatibility code, must not
pretend to accept the same Scene-mask semantics.

Code-adjacent comment required above the new request APIs and their Preset
request context:

- What: carry a validated set of target Scenes with one selected Kit folder.
- Why: a Kit browser slot alone no longer says where the parsed Kit belongs;
  the Load-menu SEQ toggles are part of the request contract.
- Why separate from generic load: all filesystem file types use the generic
  single-byte numbered slot, while only a Kit directory needs a 16-bit Scene
  destination mask and later staged fan-out.
- Inputs/outputs: zero-based Kit browser slot, Scene bitmask, callback; output
  is one asynchronous directory read and a completion tied to that exact
  request.
- Clients/accessors/affiliates: Menu Kit encoder browsing, Preset status,
  filesystem Kit state machine, `scene_indexValid()`, and active-scene runtime
  apply.

### 4.2 Parse once into a filesystem-private staged `kit_t`

The current directory loader writes every parsed `kitset.kcg` field and
instrument descriptor directly into `scene_get(scene_getActiveIndex())->kit`.
Replace that active-Scene destination with one private filesystem `kit_t`
staging object for the duration of a Kit directory operation.

At the state-machine start:

- validate that the requested mask contains at least one valid Scene bit;
- reset the staged Kit's six slots through `instrumentManager_resetSlot()` as
  the existing loader does for live Scene slots;
- pass `&op_staged_kit` to `storage_kitsetParseLine()`,
  `storage_instrumentParseLine()`, and the morph fallback helper.

After all six instrument files finalize successfully, loop over the valid,
selected Scene bits and copy the complete staged `kit_t` into each
`scene_get(scene_index)->kit`. Do not copy PatternData or Scene settings: a
Kit load changes only the embedded Kit payload, while each Scene keeps its own
pattern, MIDI track settings, and performance settings.

Code-adjacent comment required above the staged Kit storage and before the
fan-out loop:

- What: parse one Kit directory into a private complete Kit payload, then copy
  it to every selected resident Scene only after the whole directory validates.
- Why: direct parsing into the active Scene cannot service multi-target Load
  selection and leaves partially overwritten live state if a later instrument
  file is malformed.
- Why this is not an all-Scene staging abstraction: only the Kit subobject is
  common to this request. Pattern and Scene settings deliberately remain in
  their target Scene owners.
- Inputs/outputs: `kitset.kcg`, six instrument files, selected Scene mask;
  outputs either no target mutation on failure or one complete Kit copy per
  valid selected Scene on success.
- Clients/accessors/affiliates: storageTypes incremental parsers,
  `scene_get()`, `instrumentManager_resetSlot()`, filesystem completion,
  Preset's active-scene apply cursor.
- Loop comment: explain that the loop visits only resident Scene indices with
  set mask bits, preserving Scene-local Pattern/settings, and must not run
  until every file has passed finalize.

### 4.3 Apply only the active Scene to DSP after completion

`preset_startDrumsetApply()` and `preset_applyKitVoice()` currently read
`scene_getActiveIndex()`. Preserve that for this refinement. When a selected
Kit target is inactive, its retained Scene Kit is updated but no DSP object is
rewritten. When the active Scene is among the targets, the normal bounded
six-slot sound apply runs once after filesystem completion.

This keeps the current audio runtime single-Scene and makes the retained
multi-Scene Load data correct ahead of the later Scene switch/runtime apply
feature. Add an explicit test in the plan/code comments so a later Scene
activation implementation knows it must call the retained Scene apply path.

## 5. Load One Instrument Into One Selected Scene

Files:

- `Core/Menu/menu.c`
- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`
- `Core/Hardware/SD/filesystem.h`
- `Core/Hardware/SD/filesystem.c`

Extend the root Instrument request contract with a Scene index:

```c
uint8_t preset_loadInstrument(uint8_t scene_index,
                              uint8_t destination_slot,
                              instrument_type_t type,
                              uint8_t browser_index);
bool filesystem_requestLoadInstrument(uint8_t scene_index,
                                      uint8_t destination_slot,
                                      instrument_type_t type,
                                      uint8_t browser_index,
                                      fs_completion_cb_t cb);
```

Store `op_instrument_load_scene` alongside the existing destination slot/type/
index state. Replace all `scene_getActiveIndex()` uses in the root Instrument
load state machine with that validated explicit Scene index. The parser still
replaces exactly one `kit.instruments[destination_slot]` record.

On completion, start the one-slot Preset apply only if the loaded Scene is
currently active. An inactive destination Scene remains retained-only until
the future Scene runtime switch applies it. This is the exact counterpart to
the Kit multi-target behavior and is necessary even though the current valid
Scene index is only zero.

Code-adjacent comment required above the added operation state and conditional
apply:

- What: bind one root Instrument file request to one Scene and one Kit slot.
- Why: Instrument Load's SEQ buttons select a single Scene; resolving the
  destination again through `scene_getActiveIndex()` would load into the wrong
  record once resident Scene count grows.
- Why separate from Kit mask requests: the user requirement is one Scene, one
  Instrument at a time. A mask would make it possible to duplicate an
  instrument into multiple Scenes accidentally and would require a different
  completion/apply contract.
- Inputs/outputs: Scene index, zero-based slot, registry type, browser index;
  output is one parsed slot replacement and, when active, one bounded runtime
  slot apply.
- Clients/accessors/affiliates: Instrument Load lower-row scroll,
  `scene_instrumentSlot()`, filesystem parser state, `preset_startInstrumentApply()`,
  and future Scene activation.

## 6. Retain Current Kit Instrument Display Stems

Files:

- `Core/Scene/SceneData.h`
- `Core/Scene/SceneData.c` if adding an accessor proves clearer than direct
  owner writes
- `Core/Hardware/SD/storageTypes.c`
- `Core/Hardware/SD/storageTypes.h`
- `Core/Hardware/SD/filesystem.c`
- `Core/Menu/menu.c`

Add display-only Kit metadata adjacent to `kit_t`, not to
`kit_instrument_slot_t`:

```c
char instrument_display_name[INSTRUMENT_SLOT_COUNT][9];
```

Each field holds exactly the padded eight-character filename stem plus a NUL
terminator for safe internal copying. It is not an open filename, a path, or a
new save authority. The actual Kit file list remains `kitset.kcg` data managed
by `storage_kitset_t`; this retained value exists solely because the live UI
needs to say which source instrument currently occupies a Scene Kit slot.

When parsing a `file=` line in `storage_kitsetParseLine()`, derive and store
the first eight printable stem characters in the target Kit metadata alongside
the parser's existing `instrument_file[]` scratch. Put the small basename/
extension/padding routine in `storageTypes.c`, because it interprets a kitset
filename and should not be duplicated in filesystem or Menu. It should be a
private helper called from that parser branch, not a public one-line accessor.

When a root Instrument file successfully replaces one slot, copy the existing
filesystem browser display stem for that selected file into the target
Scene's `kit.instrument_display_name[destination_slot]`. Do this only after
`storage_instrumentFinalize()` succeeds, so malformed files do not claim to
be the current Kit instrument.

Initialize every field to padded `Empty   ` when `scene_initAll()` clears the
resident Scene records. The staged Kit reset path must also initialize this
metadata before parsing, preventing stale names if a source kit has a shorter
or malformed filename list.

Code-adjacent comment required above the metadata field and both write sites:

- What: retain an eight-character display stem for each Kit-owned instrument
  source.
- Why: the requested Instrument Load entry display needs `kit <name>`, while
  current slot records intentionally retain no source filename and the
  filesystem parser's filename scratch dies at operation completion.
- Why it belongs in `kit_t`: it describes Kit membership/display provenance,
  not an instrument parameter and not a Scene-wide setting. Keeping it out of
  `kit_instrument_slot_t` avoids implying that an instrument file owns its
  container filename.
- Inputs/outputs: kitset `file=` text or root Instrument browser stem; output
  is padded display-only Scene Kit metadata.
- Clients/accessors/affiliates: storage kitset parser, root Instrument loader,
  staged Kit fan-out copy, Scene initialization, and Instrument Load repaint.
- Helper-loop/math comment: document the bounded eight-character stem copy,
  extension stop condition, printable/padding rules, and NUL termination.

## 7. Refine Instrument Load Entry, Rendering, and File Selection

Files:

- `Core/Menu/menu.c`
- `Core/Menu/menu.h` only for the Scene-button API in section 3

### 7.1 Add explicit bottom-row source state

Add a private enum and state sufficient to keep the displayed file identity
separate from the pending top-row type:

```c
typedef enum {
    MENU_INSTRUMENT_LOAD_SOURCE_KIT = 0,
    MENU_INSTRUMENT_LOAD_SOURCE_POOL
} menu_instrument_load_source_t;

static menu_instrument_load_source_t menu_instrumentLoadSource;
static instrument_type_t menu_instrumentLoadShownType;
static uint8_t menu_instrumentLoadShownIndex;
```

`menu_instrumentLoadShownType/index` identify the last root-pool item actually
shown/loaded. They are deliberately separate from `menu_instrumentLoadType`
and its per-type cursor array: type may change while the bottom row must retain
its previous `kit <name>` or `[###]<name>` identity.

Do not use a copied 8-byte menu name buffer unless it is necessary for LCD
safety. In Kit source mode, read the retained Scene Kit display stem. In pool
source mode, read `filesystem_instrumentName(shown_type, shown_index)`. These
are stable cache/Scene-owned values, so duplicating them would create another
state that can go stale after an SD rescan.

Code-adjacent comment required above this state:

- What: distinguish an entry-time Kit source display from a real root-pool
  browser selection, and retain the last rendered pool identity across type
  changes.
- Why: the old one-index-per-type state cannot tell whether index zero was
  actually selected/loaded or merely clamped after entering the submode.
- Why separate from the type cursor: top-row type is a pending filter; bottom
  row is a committed/displayed source until the user explicitly scrolls it.
- Inputs/outputs: destination slot Scene metadata, pending type, browser cache
  cursor; outputs are deterministic LCD text and an immediate load only for an
  actual lower-row move.
- Clients/accessors/affiliates: entry handler, repaint, type stepper, lower
  encoder branch, filesystem Instrument cache, and Preset request.

### 7.2 Enter Instrument Load on the type row

In `menu_loadInstrumentVoicePressed()`:

- preserve the existing selected destination slot and current slot type lookup;
- initialize `menu_instrumentLoadScene` to the active Scene;
- set source to `MENU_INSTRUMENT_LOAD_SOURCE_KIT`;
- set `menu_saveOptions.state = SAVE_STATE_EDIT_TYPE` and
  `editModeActive = 1u` unconditionally;
- do not call `menu_instrumentLoadClampIndex()` merely to determine the
  entry display and do not call `menu_instrumentLoadRequestSelection()`;
- refresh the Scene LEDs and repaint.

This produces brackets around the top type label immediately, and a bottom
row formed from the retained Kit display stem.

### 7.3 Paint `kit <name>` or the retained root-pool item

Replace the current nested Instrument Load lower-row number/name rendering in
`menu_repaintLoadSavePage()` with two branches:

- Kit source: write padded `kit` at columns 0..2, a separator at column 3 or
  4, then the selected Scene's
  `kit.instrument_display_name[menu_instrumentLoadSlot]` in the eight-name
  field. Do not paint a numeric cursor or a false empty pool value.
- Pool source: render the existing bracket/arrow state, one-based display
  number saturated at 999, and `filesystem_instrumentName(shown_type,
  shown_index)`.

The top row retains the state-aware brackets/arrow from the menu-fix change.
When the file row is selected but the source remains Kit, show only the row
selection arrow/brackets around the `kit` identity; do not synthesize a root
file number.

Code-adjacent comment required above the two-branch rendering block and before
the three-digit formatting math:

- What: render either the current Kit slot source or the last root-pool source
  without conflating it with the pending type cursor.
- Why: entering or changing type must not make the UI claim that the first
  cached Instrument file is selected or loaded.
- Why separate branches: `kit <name>` has no list index, while a pool item has
  a one-based sorted ordinal with a 999 visual cap. Treating both as a numeric
  browser produces the current misleading `0 Empty` display.
- Inputs/outputs: source enum, Scene Kit display metadata, shown pool type/
  index, selection state; output is the two LCD rows.
- Clients/accessors/affiliates: Load repaint, current Scene/slot, filesystem
  cache, menu-fix cursor behavior.
- Math comment: explain the existing hundreds/tens/units extraction and that
  it is applied only in pool-source mode, with `999` as presentation saturation
  rather than a filesystem limit.

### 7.4 Make type changes non-loading and make lower scroll the commit

Change the nested Instrument Load branch of `menu_handleLoadSaveMenu()`:

- Top row in edit mode: `menu_instrumentLoadStepType(inc)` changes only the
  pending type and refreshes the display/Scene LEDs if needed. Remove the
  immediate `menu_instrumentLoadRequestSelection()` call.
- Lower row in edit mode: on nonzero encoder movement, resolve a root-pool
  selection for the current pending type. If the source is Kit or the shown
  type differs from the pending type, the first positive movement selects pool
  index zero and the first negative movement selects the final available index.
  This avoids skipping the first file because there was no prior pool cursor
  for that pending type.
- Once a pool index is resolved, store it in both the per-type cursor and
  shown type/index state, set source to `MENU_INSTRUMENT_LOAD_SOURCE_POOL`,
  then call `menu_instrumentLoadRequestSelection()` with the selected Scene.
- On later lower-row movement for the same type, add `inc` using the existing
  signed `int16_t` saturation logic. Do not request a load when clamping leaves
  the effective index unchanged.
- Empty root-pool types update no source state and request no load. The Kit
  display remains until a real pool item is selected.

Code-adjacent comment required above each top-row and lower-row branch:

- Top-row contract: type changes are filter/policy edits, not file-selection
  commits; no filesystem operation may begin there.
- Lower-row contract: the first actual file-row movement establishes a pool
  item, updates the persisted display source, and starts the immediate load.
- Why this cannot be folded into `menu_instrumentLoadStepType()`: that helper
  owns registry traversal and Advanced policy only. It has no knowledge of row
  edit intent, existing displayed source, or whether a filesystem load is
  allowed.
- Inputs/outputs/clients: encoder direction, selection row, pending type,
  shown source, cache count; outputs are source state and an optional Preset
  request; clients are encoder and repaint paths.

## 8. Validation Plan

Build checks:

1. Run `make -B` after the enum/API changes because this repository's Makefile
   has incomplete header dependency tracking.
2. Confirm no menu references remain to `SAVE_TYPE_PATTERN`, `SAVE_TYPE_MORPH`,
   `SAVE_TYPE_PERFORMANCE`, or `SAVE_TYPE_ALL`.
3. Confirm the only direct Step-array activity scan is inside PatternData's new
   Scene query; Menu and buttonHandler must call the public API.
4. Confirm all root Instrument parser writes use the explicit selected Scene,
   and directory Kit parser writes target staging until final fan-out.

Manual behavior checks with the current one-Scene build:

1. Load page type cycle exposes only Kit, Settings, and Samples. Save page
   cannot reach Pattern, MorphKit, Perform, or All.
2. In Load: Kit, SEQ1 is lit only after creating at least one active step in
   Scene 0; it blinks as the initial Kit target. SEQ2..SEQ16 do nothing and do
   not alter step/roll state while this Load context is active.
3. Leave Load: Kit for Settings/Samples or Save; SEQ LEDs return to their
   normal page-owned behavior with no stale Load blink.
4. Enter Instrument Load with a VOICE button: top type is bracketed; bottom
   reads `kit <current-slot-stem>`; no root Instrument request begins.
5. Change type repeatedly: top label changes, bottom source remains unchanged,
   and audio/slot type remain unchanged.
6. Move to the lower row and turn the encoder: first movement shows a real
   `[001]` (or final list item for negative movement), then immediately loads
   only the selected slot of Scene 0.
7. Change type after loading a pool item: the last `[###]<stem>` stays visible
   until the lower row moves again; no new Instrument file is loaded while
   editing the top row.
8. Load a Kit while Scene 0 is selected: the current Kit still loads and the
   normal active Scene bounded sound apply completes. When test scaffolding or
   future `SCENE_COUNT > 1` is available, verify selected inactive Scenes get
   retained Kit copies without altering current DSP runtime.

## Explicit Deferrals

- This plan does not implement Scene files, Bank files, or active runtime Scene
  switching. It only makes Load selection/retained data correct for the current
  one-Scene model and gives the Kit/Instrument request APIs a valid future
  destination contract.
- This plan does not implement new-format Kit or Instrument save. The retained
  instrument display stem is intentionally display metadata; the save phase
  must decide how kitset `file=` names are generated and persisted.
- This plan does not delete all legacy Pattern/Morph/All filesystem code unless
  the post-menu-removal call-site audit demonstrates that each operation has no
  remaining owner.
