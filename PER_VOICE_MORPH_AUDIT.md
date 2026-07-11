# Per-Voice Morph Audit

Date: 2026-07-11

## Goal

Split Morph into Scene-retained per-voice values without creating a second Morph
engine.

The active Morph engine should always operate per voice. The current overall
Morph control remains a Scene setting and a PERF-page control, but its runtime
meaning becomes "set all six per-voice Morph values to this amount, then queue
all six voices for Morph rebuild."

No Morph path may hardcode parameter lists. Instrument slots remain freely
interchangeable. The worker must continue to ask the current slot's instrument
registry/descriptors which cells are morphable.

## Requested PERF Layout

The PERFORMANCE page should become one static eight-cell row shown through the
existing four-cell compact-page toggle:

```text
sub-page 1: mrp 1vm 2vm 3vm
sub-page 2: 4vm 5vm 6vm srt
```

`rol` / `PAR_ROLL` is removed from PERF.

Keep the existing text for overall Morph:

- short: `mrp`
- category: existing Morph category/text
- long name: `Morph`

Keep the existing text for global Scene-stored decimation:

- short: `srt`
- long name: existing `SampleRt`

Add six per-voice Morph menu text entries:

- short names: `1vm`, `2vm`, `3vm`, `4vm`, `5vm`, `6vm`
- category: `Voice`
- long names: `1 Morph`, `2 Morph`, `3 Morph`, `4 Morph`, `5 Morph`,
  `6 Morph`

## Current Code Trace

### SceneData

`Core/Scene/SceneData.h` currently stores:

```c
uint8_t morph_amount;
uint8_t voice_decimation_all;
uint8_t midi_channel[NUM_TRACKS];
uint8_t midi_note[NUM_TRACKS];
```

There is no retained per-voice Morph storage yet.

`scene_initAll()` currently clears all Scene bytes, sets
`voice_decimation_all = 127`, and initializes `midi_channel[track]` to
`track + 1`.

### PERF Menu

`Core/Menu/menuPages.h` currently exposes PERF as:

```c
{ TEXT_ROLL_SPEED, TEXT_X_FADE, ..., PAR_ROLL, PAR_MORPH, ... }
```

Static pages already support eight cells per row, with columns `0..3` shown on
the first compact screen and columns `4..7` shown on the second. Therefore the
requested PERF layout does not require a new static-page framework.

### Parameter IDs / Dtypes

`Core/Scene/Preset/ParameterArray.h` currently has:

```c
PAR_ROLL = END_OF_SOUND_PARAMETERS,
PAR_MORPH,
```

`Core/Menu/menu.c::parameter_dtypes[]` currently assigns:

```c
[PAR_ROLL] = DTYPE_MENU|(MENU_ROLL_RATES<<4),
[PAR_MORPH] = DTYPE_0B255,
```

There are no flat ids for per-voice Morph or Scene global decimation.

### Current Morph Apply

`preset_morph(morph)` writes:

```c
parameter_values[PAR_MORPH] = morph;
presetMorph_request(scene_getActiveIndex(), morph);
```

`presetMorph_request(scene, amount)` stores the one global Scene amount and
queues one worker pass over all slots using that same amount.

`presetMorph_tick()` scans slots and descriptor indexes:

```c
descriptor = instrumentManager_descriptor(instrument->type, local);
if (!descriptor || !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
    continue;
```

This descriptor-driven scan is correct and must be preserved.

### Endpoint Edits

`preset_setInstrumentParameter(..., image, value, ...)` writes either:

- `instrument_parameters[descriptor_index]`
- `morph_instrument_parameters[descriptor_index]`

Then it queues:

```c
presetMorph_request(scene_index, scene ? scene->settings.morph_amount : 0u);
```

After per-voice Morph, this must queue only the edited slot using that slot's
per-voice Morph amount.

### MIDI CC1

`midiParser_setMorphFromModWheel()` maps incoming global-channel CC1 from 7-bit
to 8-bit Morph and calls `preset_morph(morph)`.

The channel-MIDI path currently only treats CC1 specially on the global channel.
Voice MIDI channels are used for note matching, but not for CC1 Morph.

Current voice channel storage has no unassigned/off state: menu/display values
are clamped to `1..16`. Therefore "if one is assigned" currently means "if the
voice has a valid stored channel," and all voices are valid by default unless a
separate off/unassigned channel feature is added later.

## Required Code Changes

### 1. SceneData: Retain Per-Voice Morph

Files:

- `Core/Scene/SceneData.h`
- `Core/Scene/SceneData.c`

Add:

```c
uint8_t voice_morph_amount[INSTRUMENT_SLOT_COUNT];
```

to `scene_settings_t`.

Keep:

```c
uint8_t morph_amount;
```

as the global/overall Scene Morph amount.

Initialize all per-voice values to `0` in `scene_initAll()`. This happens
automatically through `memset`, but the implementation pass should either leave
that implicit and comment it, or explicitly write the six cells for readability.

Add SceneData accessors if the implementation needs a clean ownership boundary:

```c
void scene_setVoiceMorphAmount(uint8_t scene_index, uint8_t slot, uint8_t amount);
uint8_t scene_getVoiceMorphAmount(uint8_t scene_index, uint8_t slot);
void scene_setAllVoiceMorphAmounts(uint8_t scene_index, uint8_t amount);
```

Do not make these one-line wrappers unless the call sites clearly benefit from
the boundary. The strongest reason to add them is to keep clamping/range and
Scene-validity checks beside the storage owner instead of duplicating those
checks in Menu, MIDI, Preset, and future Scene file load.

Comment text to place beside the new field/accessors:

```c
/*
 * Per-voice Morph amounts, 0..255.
 *
 * These are Scene settings, not Kit or instrument-file data. The six values
 * select how far each swappable instrument slot is interpolated between its
 * main endpoint image and morph endpoint image. The overall PERF Morph field
 * remains as the visible bulk-set value, but runtime Morph is always applied
 * from these per-slot amounts.
 *
 * Clients: PERF per-voice Morph controls, MIDI CC1 on a voice channel,
 * preset_morph() bulk-set, preset_setInstrumentParameter() endpoint refresh,
 * and future sceneset.scg load/save. The values are indexed by slot, not by
 * instrument type, so changing the instrument in a slot does not require a
 * hardcoded parameter map.
 */
```

### 2. ParameterArray: Add Flat PERF Parameters

File:

- `Core/Scene/Preset/ParameterArray.h`

Add six flat ids for per-voice Morph and one flat id for global Scene decimation.
Recommended placement is directly after `PAR_MORPH`:

```c
PAR_MORPH,
PAR_VOICE1_MORPH,
PAR_VOICE2_MORPH,
PAR_VOICE3_MORPH,
PAR_VOICE4_MORPH,
PAR_VOICE5_MORPH,
PAR_VOICE6_MORPH,
PAR_VOICE_DECIMATION_ALL,
```

Leave `PAR_ROLL` in the enum because other pages still reference it, but remove
it from PERF.

Why this must exist:

- PERF cells are static flat menu cells, not instrument descriptors.
- Overall Morph, per-voice Morph, and global decimation are Scene settings.
- They should continue through `menu_parseGlobalParam()`, not through
  descriptor voice pages or legacy sound ids.

### 3. Menu Text: Add Six Voice Morph Labels

Files:

- `Core/Menu/menu.h`
- `Core/Menu/MenuText.h`
- `Core/Menu/menu.c`

Add six `TEXT_VOICE*_MORPH` entries to `enum NamesEnum` before `NUM_NAMES`.

Add six short-name enum values and strings:

```text
1vm 2vm 3vm 4vm 5vm 6vm
```

Add six long-name enum values and strings:

```text
1 Morph
2 Morph
3 Morph
4 Morph
5 Morph
6 Morph
```

Add six `valueNames[]` entries:

```c
{SHORT_VOICE1_MORPH, CAT_VOICE, LONG_1_MORPH},
...
{SHORT_VOICE6_MORPH, CAT_VOICE, LONG_6_MORPH},
```

Why this must exist:

- The requested per-voice controls are static PERF controls, not
  ParamDescriptors.
- The single-parameter view for these cells comes from `valueNames[]`, not from
  an instrument descriptor table.

Inputs/outputs:

- Input: `TEXT_VOICE*_MORPH` cell ids in `menuPages.h`.
- Output: compact PERF labels render `1vm..6vm`; single-parameter view renders
  `Voice   1 Morph` etc.

### 4. Menu Page Layout

File:

- `Core/Menu/menuPages.h`

Change PERFORMANCE_PAGE row to:

```c
{ TEXT_X_FADE, TEXT_VOICE1_MORPH, TEXT_VOICE2_MORPH, TEXT_VOICE3_MORPH,
  TEXT_VOICE4_MORPH, TEXT_VOICE5_MORPH, TEXT_VOICE6_MORPH, TEXT_SAMPLE_RATE,
  PAR_MORPH, PAR_VOICE1_MORPH, PAR_VOICE2_MORPH, PAR_VOICE3_MORPH,
  PAR_VOICE4_MORPH, PAR_VOICE5_MORPH, PAR_VOICE6_MORPH,
  PAR_VOICE_DECIMATION_ALL },
```

This uses the existing static-page second-screen behavior. No new page reader is
required.

Update the existing PERF comment. It currently says PERF keeps
roll/morph/sample-rate controls. It should say PERF exposes overall Morph,
per-voice Morph, and Scene global decimation; Roll is intentionally absent.

### 5. Dtypes And Morph Knob Scaling

File:

- `Core/Menu/menu.c`

Add dtype entries:

```c
[PAR_VOICE1_MORPH] = DTYPE_0B255,
...
[PAR_VOICE6_MORPH] = DTYPE_0B255,
[PAR_VOICE_DECIMATION_ALL] = DTYPE_0B127,
```

Replace the one-off `cell.static_param == PAR_MORPH` check in
`menu_updateEndlessPotScales()` with a helper such as:

```c
static uint8_t menu_paramIsMorphAmount(uint16_t paramNr);
```

The helper should return nonzero for `PAR_MORPH` and `PAR_VOICE1_MORPH` through
`PAR_VOICE6_MORPH`.

Why this helper should exist:

- Overall Morph already has special endless-pot acceleration.
- Per-voice Morph is the same user-facing 0..255 Morph control contract.
- Keeping the test in one helper avoids copy/paste ranges in the knob path,
  MIDI notification path, or future display refresh code.

Comment text:

```c
/*
 * Identify flat Scene Morph amount controls.
 *
 * Inputs: canonical ParameterArray id. Output: nonzero for the overall Morph
 * bulk-set control and the six per-slot Morph controls. Clients use this to
 * keep encoder/endless-pot behavior consistent across Morph controls without
 * teaching Menu about instrument descriptors or slot parameter lists.
 */
```

### 6. Menu Commit: Global Morph Bulk-Set And Per-Voice Morph

File:

- `Core/Menu/menu.c`

Extend `menu_parseGlobalParam()`:

```c
case PAR_MORPH:
    preset_morph(value);
    break;

case PAR_VOICE1_MORPH:
...
case PAR_VOICE6_MORPH:
    preset_morphVoice((uint8_t)(paramNr - PAR_VOICE1_MORPH), value);
    break;

case PAR_VOICE_DECIMATION_ALL:
    preset_setVoiceDecimationAll(scene_getActiveIndex(), value);
    break;
```

The decimation function name is only a recommendation. The important point is
that global decimation should be a Scene setting and should update the runtime
global decimator (`mixer_decimation_rate[6]`) with the same shaping used by the
legacy `VOICE_DECIMATION_ALL` MIDI CC path.

Why this must exist:

- Overall Morph should set all six per-voice amounts.
- Per-voice Morph should update only one slot's amount.
- Global decimation is a Scene setting and should not live only as a transient
  MIDI CC side effect.

Inputs/outputs:

- Inputs: static PERF cell parameter id and clamped byte value.
- Outputs: Scene settings are retained, `parameter_values[]` mirrors stay in
  sync for visible PERF cells, and Preset/Morph applies the affected voice set.

### 7. Preset Public API: Split Bulk And Voice Morph Setters

Files:

- `Core/Scene/Preset/presetManager.h`
- `Core/Scene/Preset/presetManager.c`

Keep:

```c
void preset_morph(uint8_t morph);
```

but redefine its behavior:

- set `scene->settings.morph_amount = morph`
- set all six `scene->settings.voice_morph_amount[slot] = morph`
- update `parameter_values[PAR_MORPH]`
- update `parameter_values[PAR_VOICE1_MORPH..PAR_VOICE6_MORPH]`
- queue all six voices in the Morph worker

Add:

```c
void preset_morphVoice(uint8_t slot, uint8_t morph);
```

Behavior:

- validate `slot < INSTRUMENT_SLOT_COUNT`
- set `scene->settings.voice_morph_amount[slot] = morph`
- update that voice's `parameter_values[PAR_VOICE*_MORPH]` mirror
- queue only that voice in the Morph worker

Do not update `scene->settings.morph_amount` from per-voice edits. The global
Morph value is the last bulk-set value, not an average or inferred summary of
the six voice values.

Add or update Scene decimation Preset API:

```c
void preset_setVoiceDecimationAll(uint8_t scene_index, uint8_t value);
```

This should retain `scene->settings.voice_decimation_all`, update the flat menu
mirror for `PAR_VOICE_DECIMATION_ALL`, and apply `mixer_decimation_rate[6]`.

Comment text for `preset_morph()`:

```c
/*
 * Set overall Scene Morph by writing all per-voice Morph amounts.
 *
 * Inputs: user-facing 0..255 Morph amount from PERF, global MIDI CC1, or Scene
 * load. Outputs: the Scene global mirror and all six per-slot Morph amounts are
 * retained, flat PERF menu mirrors are updated, and the Morph worker is queued
 * for every instrument slot. Runtime Morph is still per voice; this function is
 * only the bulk-set operation that gives the user one overall control.
 */
```

Comment text for `preset_morphVoice()`:

```c
/*
 * Set one Scene voice's Morph amount.
 *
 * Inputs: zero-based instrument slot and 0..255 Morph amount. Output: only that
 * slot's Scene Morph amount and PERF mirror are updated, and only that slot is
 * queued for descriptor Morph interpolation. This function exists separately
 * from preset_morph() because global Morph is a bulk-set operation while MIDI
 * CC1 on a voice channel and PERF 1vm..6vm edits must preserve the other five
 * slot amounts.
 */
```

### 8. Preset Endpoint Edits Must Queue The Edited Slot

File:

- `Core/Scene/Preset/presetManager.c`

In `preset_setInstrumentParameter()`:

Current zero-Morph immediate apply test:

```c
scene && scene->settings.morph_amount == 0u
```

must become:

```c
scene && scene->settings.voice_morph_amount[slot] == 0u
```

Current refresh:

```c
presetMorph_request(scene_index, scene ? scene->settings.morph_amount : 0u);
```

must become a voice rebuild request:

```c
presetMorph_requestVoice(scene_index, slot);
```

Why this must exist:

- Endpoint edits are slot-local.
- If voice 2 has Morph 200 and voice 5 has Morph 0, editing a voice 2 endpoint
  should rebuild voice 2 at 200 without touching voice 5.
- This keeps the flexible descriptor model: the slot is known, the descriptor
  index is known, and the worker still discovers morphable cells from the
  current instrument type.

### 9. Morph Engine: Make Worker Per-Voice

Files:

- `Core/Scene/Preset/presetMorphEngine.h`
- `Core/Scene/Preset/presetMorphEngine.c`

Recommended public API:

```c
void presetMorph_requestVoice(uint8_t scene_index, uint8_t slot);
void presetMorph_requestAll(uint8_t scene_index);
uint8_t presetMorph_tick(void);
void presetMorph_rebuildScene(uint8_t scene_index);
```

`presetMorph_requestAll()` should queue all six voices using the already stored
per-voice Scene amounts. `presetMorph_requestVoice()` should also queue from
already stored Scene data. Neither worker request function should mutate
`scene_settings_t`; keeping storage mutation in `preset_morph()`,
`preset_morphVoice()`, SceneData accessors, and future Scene-file load avoids
having two owners write per-voice Scene settings.

Recommended worker state:

```c
typedef struct {
    uint8_t scene_index;
    uint8_t slot;
    uint8_t descriptor_index;
    uint8_t requested_mask;
    uint8_t pass_mask;
    uint8_t pass_amount[INSTRUMENT_SLOT_COUNT];
    uint8_t active;
} preset_morph_worker_t;
```

Behavior:

1. `presetMorph_requestVoice()` validates the Scene/slot and sets the
   corresponding bit in `requested_mask`.
2. If the worker is idle, begin a pass.
3. Beginning a pass copies `requested_mask` to `pass_mask`, clears those bits
   from `requested_mask`, snapshots the current six per-voice amounts into
   `pass_amount[]`, sets `slot` to the first requested slot, and sets
   `descriptor_index = 0`.
4. `presetMorph_tick()` processes at most one morphable descriptor cell for the
   current slot per call.
5. When a slot is finished, clear its bit in `pass_mask` and advance to the next
   requested slot.
6. When `pass_mask` is empty, start another pass if `requested_mask` is nonzero;
   otherwise go idle.

Why masks are better than generation counters here:

- The old engine had one amount and one pass over all slots.
- The new engine needs to coalesce requests independently per slot.
- A mask lets a PERF 3vm edit rebuild only voice 3, while global Morph sets all
  six bits.
- If a voice receives a new amount while it is already being processed, the bit
  remains queued in `requested_mask` for a complete follow-up pass of that
  voice.

Preserve this descriptor scan exactly in spirit:

```c
descriptor = instrumentManager_descriptor(instrument->type, local);
if (!descriptor || !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
    continue;
```

Do not add drum/snare/cymbal/hihat parameter lists.

Comment text for the worker:

```c
/*
 * Per-voice Morph work queue.
 *
 * Runtime Morph is slot-owned: each instrument slot has its own 0..255 amount,
 * and the overall Morph control merely writes all six slot amounts. The worker
 * therefore queues dirty slots by bitmask and snapshots each slot's amount at
 * the start of a pass. A request that arrives mid-pass sets the requested bit
 * for a later complete pass instead of changing the amount halfway through the
 * current slot.
 *
 * Inputs: Scene index and zero-based instrument slot. The amount is read from
 * Scene-retained per-slot Morph settings when a pass begins. Outputs:
 * morph_interpolation[] is rebuilt for descriptor cells flagged morphable, and
 * the active Scene's runtime DSP binding is updated through
 * preset_applyInstrumentRuntimeValue(). Instrument membership is dynamic, so
 * descriptor flags from InstrumentManager remain the only list of morphable
 * cells.
 */
```

### 10. Preset Scene Settings Apply

File:

- `Core/Scene/Preset/presetManager.c`

`preset_applySceneSettings(scene_index)` should mirror:

```c
parameter_values[PAR_MORPH] = scene->settings.morph_amount;
parameter_values[PAR_VOICE1_MORPH + slot] =
    scene->settings.voice_morph_amount[slot];
parameter_values[PAR_VOICE_DECIMATION_ALL] =
    scene->settings.voice_decimation_all;
```

Then queue/rebuild all voices through the Morph worker.

Important distinction:

- Scene load should preserve whatever per-voice values the Scene stored.
- Applying the Scene should not call `preset_morph(scene->settings.morph_amount)`
  if that helper bulk-sets all six voices. Doing so would erase distinct
  per-voice Morph values on every Scene apply.

Recommended shape:

```c
preset_syncSceneMorphMirrors(scene);
presetMorph_rebuildScene(scene_index);
preset_setVoiceDecimationAll(scene_index, scene->settings.voice_decimation_all);
```

The exact helper names can differ; the ownership rule cannot.

### 11. MIDI CC1: Voice Channel Dispatch

File:

- `Core/MIDI/MidiParser.c`

Add a per-voice CC1 path in the incoming channel-MIDI branch.

Required behavior:

- CC1 on the global MIDI channel calls `preset_morph(morph)` and sets all six
  per-voice Morph values.
- CC1 on a voice MIDI channel calls `preset_morphVoice(voice, morph)`.
- Use the same 7-bit to 8-bit mapping as global Morph:

```c
(value == 127u) ? 255u : (uint8_t)(value * 2u)
```

Recommended tie-break:

1. If `msg.data1 != CC_MOD_WHEEL`, keep the existing handler.
2. If channel matches the assigned global channel and the global handler can
   process the CC, apply global Morph.
3. Otherwise, if the channel matches one or more voice channels, apply to those
   voice slots.

Why global-first is required:

- The global MIDI channel is the broader command channel and should claim
  parameters it is assigned to and can process.
- Per-voice Morph remains available when the incoming channel is not claimed by
  the global handler, including channels dedicated to voice control.

Conflict to be aware of:

- The phrase "if one is assigned" implies a possible unassigned voice channel,
  but current channel storage clamps every voice to `1..16` and defaults all
  voices to assigned channels. Supporting true unassigned channels would require
  a separate MIDI channel dtype/menu/storage change, probably using `0 = off`.
  This audit assumes the current assigned-by-default model remains in place.

Automation note:

- The current global-channel CC path calls `seq_recordAutomation()` before
  handling CC1. Decide during implementation whether per-voice Morph CC should
  record automation now. Phase 5 contains MIDI/automation rework, so the minimal
  Phase 3 change should prioritize runtime Morph behavior and avoid inventing a
  new automation storage path.

Comment text:

```c
/*
 * Route incoming CC1 to global or per-voice Morph.
 *
 * Inputs: incoming channel and 7-bit CC value. Output: 8-bit Morph amount is
 * applied first to the global Morph bulk-set channel when that channel matches
 * and can process CC1. Only otherwise does the event fall through to matching
 * voice slots. Current Scene channel storage has no explicit unassigned state,
 * so this provision can be revised during the later MIDI rework.
 */
```

### 12. Scene File Spec / Future Save-Load Shape

No Scene folder save/load exists yet, but the new data must be reflected in the
future `sceneset.scg` shape.

Recommended `sceneset.scg` keys:

```ini
[settings]
morph=0
voice1_morph=0
voice2_morph=0
voice3_morph=0
voice4_morph=0
voice5_morph=0
voice6_morph=0
voice_decimation_all=127
voice1_midi_channel=1
...
voice6_midi_channel=6
voice1_midi_note=36
...
voice6_midi_note=41
```

Load fallback policy:

- Missing `morph` defaults to `0`.
- Missing per-voice Morph keys default to the loaded/global `morph` value, not
  necessarily `0`. This preserves old Scenes as "all voices follow global
  Morph."
- Missing `voice_decimation_all` defaults to `127`.

Save policy:

- Always save both global `morph` and the six per-voice Morph values.
- Do not save `morph_interpolation[]`; it remains runtime-derived.
- Do not save Morph amount in Kit or instrument files. Kit/instrument files own
  Morph endpoints, not the Scene's current Morph amounts.

## Implementation Order

1. Add SceneData storage and comments.
2. Add flat parameter ids, dtype entries, menu text, and PERF layout.
3. Add Preset APIs for global bulk Morph, voice Morph, and global decimation.
4. Rework Morph worker to queue dirty slots by bitmask while preserving the
   descriptor-driven morphable scan.
5. Update endpoint-edit refresh to request only the edited slot at that slot's
   per-voice Morph amount.
6. Update Scene settings apply so it mirrors values without bulk-overwriting
   per-voice Morph.
7. Add voice-channel CC1 dispatch.
8. Build and hardware-test PERF and MIDI paths.

## Test Plan

1. Load a Kit with distinct `[params]` and `[morph]` endpoints.
2. Set PERF `mrp` to `0`. Confirm all `1vm..6vm` display `0` and the main
   endpoints sound.
3. Set PERF `mrp` to `255`. Confirm all `1vm..6vm` display `255` and all
   voices hit their morph endpoints.
4. Set `2vm` to `0` while other voices remain `255`. Confirm only voice 2
   returns to its main endpoint.
5. Edit a morph endpoint through `SHIFT+VOICE` for voice 2 while `2vm` is
   nonzero. Confirm only voice 2 rebuilds and updates audibly.
6. Change the instrument type in a slot, then test Morph again. Confirm the
   worker follows that slot's current descriptors and does not assume drum
   parameter indexes.
7. Send CC1 on a voice channel. Confirm only that voice's Morph changes.
8. Send CC1 on the global channel. Confirm all six per-voice Morph values
   change together.
9. Set PERF `srt`. Confirm Scene global decimation is retained and
   `mixer_decimation_rate[6]` changes.

## Open Conflicts / Decisions

- Current voice MIDI channels are always assigned because storage clamps to
  `1..16`. If true "unassigned voice channel" behavior is required, add an
  explicit off state as a separate MIDI-channel UI/storage change.
- If a channel is both a voice channel and the global channel, the global
  channel wins for CC1 because it is assigned and can process Morph.
- `preset_getMorphValue()` is still an old helper that interpolates
  `parameter_values[]` and `parameters2[]`. It is not the active descriptor
  worker path, but it should either be updated for per-voice semantics or
  documented/deprecated during implementation if no current caller needs it.

## Implementation Notes - 2026-07-11

Implemented the Phase 3 per-voice Morph runtime/menu/MIDI pass.

### Scene Settings

Added `scene_settings_t::voice_morph_amount[INSTRUMENT_SLOT_COUNT]` in
`Core/Scene/SceneData.h`.

Added SceneData accessors:

- `scene_setVoiceMorphAmount(scene, slot, amount)`
- `scene_getVoiceMorphAmount(scene, slot)`
- `scene_setAllVoiceMorphAmounts(scene, amount)`

The new field is explicitly Scene-owned. It is not Kit data and is not stored in
instrument files. `morph_amount` remains the overall PERF `mrp` mirror and the
last bulk-set amount; runtime interpolation now uses the per-slot amounts.

### Flat PERF Parameters And Text

Added flat Scene/PERF parameters:

- `PAR_VOICE1_MORPH` through `PAR_VOICE6_MORPH`
- `PAR_VOICE_DECIMATION_ALL`

Implementation note: these were added after the established global ids rather
than directly after `PAR_MORPH`, so existing Pattern/MIDI/global parameter ids
are not renumbered. The six voice Morph ids are still contiguous, so
`PAR_VOICE1_MORPH + slot` remains valid.

Added static PERF text:

- compact labels `1vm..6vm`
- single-parameter long names `1 Morph..6 Morph`
- category `Voice`

Updated PERFORMANCE_PAGE to:

```text
mrp 1vm 2vm 3vm | 4vm 5vm 6vm srt
```

`PAR_ROLL` remains in the enum because other code still references it, but it is
no longer shown on PERF.

### Menu Behavior

Added `menu_paramIsMorphAmount()` so overall Morph and the six per-voice Morph
controls share the existing Morph endless-pot acceleration behavior.

Extended `menu_parseGlobalParam()`:

- `PAR_MORPH` calls `preset_morph(value)` and bulk-sets all six voice Morphs.
- `PAR_VOICE*_MORPH` calls `preset_morphVoice(slot, value)`.
- `PAR_VOICE_DECIMATION_ALL` calls `preset_setVoiceDecimationAll()`.

Updated old interpolation refresh sites in Menu so they call
`preset_rebuildMorph()` instead of `preset_morph(parameter_values[PAR_MORPH])`.
That matters because `preset_morph()` is now intentionally destructive to the
six per-voice values.

### Preset API

Changed `preset_morph()` semantics:

- writes Scene global `morph_amount`
- writes all six `voice_morph_amount[]` values
- mirrors all seven PERF Morph values into `parameter_values[]`
- queues all six slots in the Morph worker

Added:

- `preset_morphVoice(slot, morph)`
- `preset_rebuildMorph()`
- `preset_setVoiceDecimationAll(scene, value)`

`preset_applySceneSettings()` now mirrors retained Scene settings and queues a
rebuild without calling `preset_morph()`, so future Scene loads can preserve
distinct per-voice values.

`preset_setInstrumentParameter()` now checks the edited slot's retained
per-voice Morph amount for immediate main-endpoint apply and queues only that
slot after endpoint edits.

### Morph Worker

Replaced the old single-amount/generation worker with a per-slot dirty-mask
worker:

- `requested_mask` coalesces incoming dirty slots.
- `pass_mask` owns the current bounded pass.
- `pass_amount[slot]` snapshots retained per-slot Scene amounts at pass start.
- `presetMorph_requestVoice(scene, slot)` queues one slot.
- `presetMorph_requestAll(scene)` queues all six slots.

The descriptor scan remains dynamic:

```c
instrumentManager_descriptor(instrument->type, local)
INSTRUMENT_PARAM_FLAG_MORPHABLE
```

No drum/snare/cymbal/hihat parameter lists were added.

### MIDI CC1

Global-channel CC1 still maps 7-bit input to 8-bit Morph with the 127 -> 255
endpoint special case, but now calls the bulk-set `preset_morph()`.

Added voice-channel CC1 routing:

- global-channel CC1 has first claim and calls the bulk-set `preset_morph()`
- voice channel match calls `preset_morphVoice()`
- voice processing runs only when the global channel did not claim the event

The current MIDI channel model still has no true unassigned/off state. The code
has the provision for voice-channel CC1 now; the off/unassigned behavior can be
handled in the later MIDI rework.

### Verification

`make` passes.

Observed warnings are the existing nano libc `_close/_lseek/_read/_write`
syscall stub linker warnings plus the existing LTO serial-compilation note.

### Silence Fix - SRT Default

Hardware test reported no sound after the per-voice Morph pass. The likely
failure was the new PERF `srt` flat mirror starting at zero after
`menu_init()`:

- `SceneData` correctly initializes `voice_decimation_all = 127`.
- `menu_init()` clears `parameter_values[]` to zero.
- An early/global apply path can read `parameter_values[PAR_VOICE_DECIMATION_ALL]`
  before Scene settings mirror it.
- Value `0` shapes `mixer_decimation_rate[6]` to `0`.
- With the global decimator multiplier at zero, decimated voices do not refresh
  their output sample and the unit presents as silent.

Fixed by initializing `parameter_values[PAR_VOICE_DECIMATION_ALL] = 127` in
`menu_init()` with adjacent comment text. This keeps the undefined/startup flat
mirror aligned with the retained Scene default until Scene folder save/load
exists.
