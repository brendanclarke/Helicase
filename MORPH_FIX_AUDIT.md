# Morph Fix Audit

Date: 2026-07-11

## Summary

There is a clear PERF-menu Morph failure.

The descriptor morph endpoint edit path is mostly wired correctly: `SHIFT+VOICE`
instrument edits read and write `SceneData`'s
`morph_instrument_parameters[]`, and `preset_setInstrumentParameter()` requests
a morph rebuild after either endpoint changes. The flexible instrument-slot
requirement is already respected there because the path uses descriptor indexes
and `INSTRUMENT_PARAM_FLAG_MORPHABLE`, not hardcoded parameter lists.

The active PERF Morph amount does not reliably drive that worker. The PERF
page edits `PAR_MORPH`, but the static menu commit path routes it through the
legacy sound-parameter/MIDI-CC path instead of `preset_morph()`. That means the
displayed Morph value can change while the Scene-owned descriptor morph worker
never receives a request.

There is also a second scale bug inside the worker: Morph is now a 0..255
user-facing parameter, but `presetMorph_request()` clamps it to 127 and the
interpolator is documented and implemented as a 0..127 position. Even when the
worker is called, full Morph can only reach roughly halfway to the endpoint.

## Trace

### PERF Morph Menu Cell

`Core/Menu/menuPages.h` exposes PERF Morph as a static menu cell:

```c
/* PERFORMANCE_PAGE */
{TEXT_ROLL_SPEED,TEXT_X_FADE,..., PAR_ROLL,PAR_MORPH,...}
```

`Core/Scene/Preset/ParameterArray.h` currently defines:

```c
PAR_NONE = 0,
END_OF_SOUND_PARAMETERS,
PAR_ROLL = END_OF_SOUND_PARAMETERS,
PAR_MORPH,
```

So `PAR_MORPH` is a small flat non-instrument id, value 2.

### Static Menu Commit Path

Static cells commit through `menu_cellCommitValue()`:

```c
*paramValue = (uint8_t)value;
menu_sendEditedParameter(cell->static_param, *paramValue);
```

`menu_sendEditedParameter()` then calls `menu_sendSoundParameter()` for normal
static edits:

```c
menu_sendSoundParameter(paramNr, value);
```

`menu_sendSoundParameter()` currently checks `paramNr < 128` before checking
`END_OF_SOUND_PARAMETERS`:

```c
if (paramNr < 128)
    preset_applySoundParameter(paramNr, value, 1);
else if (paramNr < END_OF_SOUND_PARAMETERS)
    preset_applySoundParameter(paramNr, value, 1);
else
    menu_parseGlobalParam(paramNr, value);
```

Because `PAR_MORPH == 2`, PERF Morph is treated as a legacy sound parameter.
It does not reach the existing `menu_parseGlobalParam(PAR_MORPH)` case:

```c
case PAR_MORPH:
    preset_morph(value);
    break;
```

### Legacy MIDI CC Apply

`preset_applySoundParameter(PAR_MORPH, value, 1)` writes
`parameter_values[PAR_MORPH]`, then emits a fake `MIDI_CC` to
`midiParser_ccHandler()`. The MIDI CC handler's `MIDI_CC` branch has no Morph
case for this parameter. It falls through to default and only calls:

```c
modNode_originalValueChanged(paramNr);
```

That does not call `preset_morph()` and does not request
`presetMorphEngine`.

### Descriptor Endpoint Edit Path

The new `SHIFT+VOICE` descriptor path is different and looks broadly correct:

- `menu_cellDisplayValue()` reads `morph_instrument_parameters[]` when
  `voiceModeShowMorph` is set.
- `menu_cellCommitValue()` calls `preset_setInstrumentParameter()` with
  `INSTRUMENT_IMAGE_MORPH`.
- `preset_setInstrumentParameter()` writes the selected descriptor image and
  calls `presetMorph_request(scene_index, scene->settings.morph_amount)`.

That explains the observed behavior: morph endpoint edits can appear retained,
but moving the PERF Morph amount does not necessarily apply them because the
PERF amount edit is routed away from the descriptor morph worker.

### Worker Scale

`presetMorph_request()` clamps the amount:

```c
if (morph_amount > 127u)
    morph_amount = 127u;
```

`presetMorph_interpolate()` is also documented as 0..127:

```c
 * Inputs are two bytes plus a 0..127 position
```

This conflicts with the Phase 3 contract: Morph is a 0..255 parameter in the
menu and Scene setting. At `PAR_MORPH = 255`, the worker stores and applies
127, not the endpoint.

There is also a descending-endpoint rounding hazard in the current math if it
were simply allowed to receive 255: for `a=127`, `b=0`, and `position=255`,
the old `/256` rounding can produce 1 instead of exact 0. The fixed 0..255
interpolator should special-case exact endpoints or use `/255` math.

### MIDI CC1 Morph

Incoming CC1 still maps:

```c
const uint8_t morph = (uint8_t)(value << 1);
```

So MIDI input 127 maps to 254, not 255. This is not the primary PERF-menu
failure, but it violates the current Morph contract and should be fixed in the
same pass.

## Proposed Code Changes

### 1. Fix Static Menu Routing For Flat Non-Instrument Parameters

File: `Core/Menu/menu.c`

Change `menu_sendSoundParameter()` so the sound-parameter test uses
`END_OF_SOUND_PARAMETERS` first and only routes true legacy sound ids to
`preset_applySoundParameter()`.

Proposed behavior:

- `paramNr < END_OF_SOUND_PARAMETERS`: legacy flat sound parameter, call
  `preset_applySoundParameter()`.
- everything else: flat performance, pattern, generator, MIDI, trigger, and
  global parameters call `menu_parseGlobalParam()`.

Why this must exist:

- `ParameterArray.h` now explicitly says instrument sound parameters no longer
  live in the flat namespace.
- `PAR_MORPH`, `PAR_ROLL`, pattern controls, generator controls, and globals
  are flat non-instrument parameters even though their numeric ids are below
  128.
- The old `paramNr < 128` check silently bypasses `menu_parseGlobalParam()` for
  these ids and prevents their typed side effects.

Inputs:

- `paramNr`: flat `ParameterArray.h` parameter id.
- `value`: already clamped menu value.

Outputs:

- True legacy sound ids go through Preset's legacy sound apply.
- Non-sound flat ids go through their owner-specific switch in
  `menu_parseGlobalParam()`.

Accessors, clients, affiliates:

- Clients: `menu_sendEditedParameter()` from encoder and endless-pot edits.
- Affiliates: `parameter_dtypes[]`, `menuPages.h`, `preset_applySoundParameter()`,
  `menu_parseGlobalParam()`, PatternData, Sequencer, SOM/Euklid, MIDI parser,
  and `preset_morph()`.

Comment text to place next to the change:

```c
/*
 * Route flat menu parameters by ownership, not by MIDI CC packing range.
 *
 * Instrument sound parameters no longer live in ParameterArray's flat
 * namespace; descriptor-backed voice edits use preset_setInstrumentParameter().
 * The remaining flat ids include PERF Morph/Roll, Pattern, generator, MIDI,
 * trigger, and globals. Many of those ids are numerically below 128, so the old
 * "param < 128 means sound CC" test bypassed menu_parseGlobalParam() and lost
 * owner-specific side effects such as preset_morph().
 *
 * Inputs: canonical flat ParameterArray id and clamped byte value.
 * Outputs: only true legacy sound ids use preset_applySoundParameter(); all
 * non-sound flat ids run their typed owner path in menu_parseGlobalParam().
 */
```

### 2. Make The Morph Worker 0..255 End-To-End

Files:

- `Core/Scene/Preset/presetMorphEngine.c`
- `Core/Scene/Preset/presetMorphEngine.h`
- optionally `Core/Scene/SceneData.h` comment text for `morph_amount`

Required changes:

- Remove the `morph_amount > 127` clamp.
- Update comments and names from 0..127 to 0..255.
- Replace the interpolator with a 0..255 exact-endpoint function.

Recommended interpolation contract:

- amount `0` returns main endpoint exactly.
- amount `255` returns morph endpoint exactly.
- amounts `1..254` linearly interpolate with rounding.
- endpoint values come from descriptor image storage, not a hardcoded list.

Example implementation shape:

```c
static uint16_t presetMorph_interpolate(uint16_t a, uint16_t b,
                                        uint8_t amount)
{
    int32_t numerator;

    if (amount == 0u)
        return a;
    if (amount == 255u)
        return b;

    numerator = (int32_t)a * 255 +
                ((int32_t)b - (int32_t)a) * amount;
    numerator += 127;
    if (numerator < 0)
        return 0u;
    return (uint16_t)(numerator / 255);
}
```

For current descriptors the returned value will still fit in a byte because
menu/storage endpoint writes are byte-valued, but using `uint16_t` matches the
Scene image arrays and avoids baking in a future descriptor-size assumption.

Why this must exist:

- The user-facing Morph parameter is `DTYPE_0B255`.
- `scene_settings_t.morph_amount` is a byte and can store 0..255.
- MIDI CC needs special 7-bit to 8-bit endpoint mapping.
- Clamping the worker at 127 makes full Morph apply only about half of the
stored endpoint distance.

Inputs:

- active Scene index
- 0..255 morph amount
- per-slot descriptor endpoint images

Outputs:

- `morph_interpolation[]` is rebuilt for morphable descriptors.
- active Scene runtime receives the interpolated descriptor value through
  `preset_applyInstrumentRuntimeValue()`.

Accessors, clients, affiliates:

- Clients: `preset_morph()`, `preset_setInstrumentParameter()`,
  `preset_applySceneSettings()`, `preset_startDrumsetApply()`, and boot kit
  apply.
- Affiliates: `SceneData` image arrays, `InstrumentManager` descriptor flags,
  `InstrumentManager_writeRuntime()`, Menu PERF Morph, MIDI CC1 Morph, Kit
  `[params]`/`[morph]` load.

Comment text to place next to the worker/interpolator:

```c
/*
 * Interpolate descriptor-owned Morph endpoints with the current 0..255 Morph
 * contract.
 *
 * Inputs: main endpoint, morph endpoint, and user-facing Morph amount where
 * 0 is exactly main and 255 is exactly morph. Output: rounded descriptor image
 * value to write into morph_interpolation[] and, for the active Scene, into the
 * runtime DSP binding.
 *
 * Why this uses descriptor images rather than parameter lists: instrument slot
 * types are swappable, and each instrument registry entry owns which
 * descriptor cells are morphable. The worker therefore scans descriptor flags
 * for the current slot type instead of naming drum/snare/cymbal/hihat
 * parameters here.
 */
```

### 3. Preserve The Current Descriptor-Based Morph Scan

File: `Core/Scene/Preset/presetMorphEngine.c`

Do not add hardcoded parameter lists.

The current scan is the right shape:

```c
descriptor = instrumentManager_descriptor(instrument->type, local);
if (!descriptor ||
    !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
    continue;
}
```

Keep this registry-driven. It allows each slot's current instrument type to
decide which cells participate in Morph.

Possible cleanup:

- Change the local `value` in `presetMorph_tick()` from `uint16_t value` fed by
  a `uint8_t` interpolator to a true `uint16_t` result.
- If `preset_applyInstrumentRuntimeValue()` remains byte-valued, clamp/cast at
  that boundary with a comment that current morphable runtime cells are
  byte-domain. Longer term, consider widening `preset_applyInstrumentRuntimeValue()`
  to `uint16_t` because `InstrumentManager_writeRuntime()` already accepts
  `uint16_t`.

### 4. Fix MIDI CC1 Morph Endpoint Mapping

File: `Core/MIDI/MidiParser.c`

Change:

```c
const uint8_t morph = (uint8_t)(value << 1);
```

to:

```c
const uint8_t morph = (value == 127u) ? 255u : (uint8_t)(value * 2u);
```

Why this must exist:

- MIDI CC is 7-bit, Morph storage/UI is 8-bit.
- Without the special case, CC1 can reach only 254.
- This is explicitly part of the Phase 3 Morph contract.

Comment text:

```c
/*
 * Map incoming 7-bit CC1 to the 8-bit Morph amount.
 *
 * Inputs: MIDI CC value 0..127. Output: Morph amount 0..255. Values 0..126
 * double cleanly; 127 is a special endpoint case so external controllers can
 * reach the exact morph endpoint. This path is incoming MIDI only; do not add
 * CC1 handling to midiParser_ccHandler(), which is also used by internal
 * parameter dispatch.
 */
```

### 5. Update Comments Around `scene_settings_t::morph_amount`

File: `Core/Scene/SceneData.h`

Add comment text:

```c
/*
 * Scene-level global Morph amount, 0..255.
 *
 * This is the visible PERF Morph value and the default amount used when
 * descriptor endpoint edits request a rebuild. Per-voice Morph will be added
 * later; this field remains the global amount and incoming global CC1 should
 * overwrite future per-voice amounts according to the Phase 3 plan.
 */
uint8_t morph_amount;
```

## Testing Steps If/After Fixed

1. Load a directory Kit with `[params]` and `[morph]` endpoints.
2. Pick one audible descriptor parameter on voice 1, such as oscillator coarse
   pitch, filter frequency, or volume.
3. Set main endpoint low in normal VOICE mode.
4. Enter `SHIFT+VOICE` morph endpoint mode and set the same parameter high.
5. Return to PERF Morph and sweep:
   - Morph `0`: should match the main endpoint exactly.
   - Morph `128`: should be close to halfway.
   - Morph `255`: should match the morph endpoint exactly.
6. Repeat with endpoints reversed, high main and low morph. Morph `255` should
   reach the low endpoint exactly, not stop one value above it.
7. While Morph is nonzero, edit the morph endpoint again through `SHIFT+VOICE`.
   The sound should update after the worker drains without needing a kit reload.
8. Test a slot whose instrument type differs from voice 1, for example Snare,
   Cymbal, and Hihat. The morph worker should follow descriptor flags for the
   slot's current instrument type without special-case lists.
9. Send MIDI CC1 values `0`, `64`, and `127` on the global channel:
   - `0` should set Morph `0`.
   - `64` should set Morph `128`.
   - `127` should set Morph `255`.

## Current Confidence

High confidence there are real code failures:

- PERF Morph takes the wrong menu commit route and does not call
  `preset_morph()`.
- The worker clamps the amount to 127 and cannot reach the 0..255 endpoint.
- MIDI CC1 cannot reach 255.

Medium confidence that `SHIFT+VOICE` endpoint storage is already basically
correct. The audit found it writes descriptor-indexed morph images and requests
a rebuild, which matches the intended flexible instrument-slot design. It
should be retested after the PERF routing and 0..255 worker fixes.
