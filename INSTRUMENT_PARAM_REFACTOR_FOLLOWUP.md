# Instrument Parameter Refactor - Follow-up Findings

## Status

Investigation only. No firmware source changes are made in this pass.

The root Kit directory files are being opened and parsed through the new
descriptor path, but the loaded values are not yet connected to the live
instrument parameter path. The current code is split between two worlds:

- `filesystem_loadKitDirectory_tick()` now writes directory-kit data into the
  active `scene_t` kit/instrument slots.
- Menu, Preset, Morph, and DSP apply still mostly operate from the old flat
  `parameter_values[]` / `parameters2[]` buffers and legacy `PAR_*` IDs.

That mismatch is the main reason Kit folders can appear not to read into
instrument parameters correctly.

## Findings

### 1. Directory kit loading no longer writes instrument bytes into the flat arrays

`Core/Hardware/SD/filesystem.c` still has comments saying directory Kit loading
fills `parameter_values[]` and `parameters2[]`, but the actual implementation
now targets `SceneData`:

- `kitset.kcg` audio routes are parsed into
  `&scene_get(scene_getActiveIndex())->kit`
  at `Core/Hardware/SD/filesystem.c:1258`.
- Each instrument slot is reset through `instrumentManager_resetSlot()` at
  `Core/Hardware/SD/filesystem.c:1311`.
- Instrument file lines are parsed into
  `scene_instrumentSlot(scene_getActiveIndex(), op_instrument_slot)` at
  `Core/Hardware/SD/filesystem.c:1352`.
- `storage_instrumentParseLine()` writes descriptor-owned keys into
  `slot->parameter_images.instrument_parameters[local]` or
  `slot->parameter_images.morph_instrument_parameters[local]` at
  `Core/Hardware/SD/storageTypes.c:471`.

So, if the UI or DSP apply path is inspected through `parameter_values[]`, it
will look as if the kit did not load even though the Scene-owned slot may have
been populated.

### 2. Load completion still applies the old flat buffers, not the Scene images

After `PRESET_OP_KIT_LOAD`, `menu_pollPresetStatus()` still normalizes and
applies `parameter_values[]`:

- `menu_normalizeSoundModTargets(parameter_values)` at
  `Core/Menu/menu.c:2341`.
- `menu_startSoundApply(...)` at `Core/Menu/menu.c:2342`.

The chunked apply path then reaches `preset_tickDrumsetApply()`, but that only
applies velocity/LFO routing using legacy flat slots:

- `modTargets[parameter_values[PAR_VEL_DEST_1 + voice]]` at
  `Core/Scene/Preset/presetManager.c:358`.
- `parameter_values[PAR_VOICE_LFO1 + voice]` and
  `modTargets[parameter_values[PAR_TARGET_LFO1 + voice]]` at
  `Core/Scene/Preset/presetManager.c:368` and `:373`.

It does not iterate the loaded `scene->kit.instruments[slot].parameter_images`
and does not apply their image values to the DSP voices. This is the practical
runtime break.

### 3. The new typed Preset API is declared but not implemented in the live build

`presetManager.h` declares the exact APIs needed for the new handoff:

- `preset_setInstrumentParameter()`
- `preset_setSupplementalParameter()`
- `preset_applyInstrumentRuntimeValue()`
- `preset_applyKitAudioRouting()`
- `preset_applySceneSettings()`

See `Core/Scene/Preset/presetManager.h:130`.

`rg` found no definitions for these functions. The only current caller of
`preset_applyInstrumentRuntimeValue()` is `presetMorphEngine.c`, but that file
is not listed in `Makefile` `SRCS` (`Makefile:90` includes `presetManager.c`
and `ParameterArray.c`, then skips `presetMorphEngine.c`). Because the file is
not built, the missing symbol is currently hidden.

This explains why `make -B` still links: the source that would expose the
missing runtime apply function is outside the build.

### 4. The new morph engine exists but the old morph engine is still active

`Core/Scene/Preset/presetMorphEngine.c` correctly reads:

- `instrument_parameters[local]`
- `morph_instrument_parameters[local]`
- writes `morph_interpolation[local]`
- calls `preset_applyInstrumentRuntimeValue()`

See `Core/Scene/Preset/presetMorphEngine.c:108`.

However, because this file is not in `Makefile`, live firmware still uses
`preset_morph()` / `preset_morphTick()` in `presetManager.c`, which interpolate
`parameter_values[]` and `parameters2[]` by legacy flat index. That means loaded
Scene morph endpoints are also not part of the active runtime path.

### 5. Converted supplemental target values are still legacy bytes

`storage_instrumentParseLine()` treats `velo_mod_dest` and `lfo_target_param`
as `uint16_t` canonical instrument target IDs and stores them into
`slot->supplemental.velocity_target_param` / `lfo_target_param`
(`Core/Hardware/SD/storageTypes.c:451`).

But `tools/convert_legacy_kits.py` still emits every value by copying the
legacy `PAR_*` payload byte unchanged:

```text
values.append((renames[key], payload_value(payload, param_values, param_name)))
```

See `tools/convert_legacy_kits.py:436`.

That is correct for ordinary byte-domain instrument parameters, but not for
`velo_mod_dest` and `lfo_target_param` after the refactor. Those two fields need
conversion from legacy `modTargets[]` index values to canonical flat
`slot * 64 + local_param` IDs. Otherwise supplemental routing will be wrong
even after the Scene image apply path is fixed.

### 6. Directory load is not transactional

The loader resets each destination slot before reading its instrument file
(`Core/Hardware/SD/filesystem.c:1311`). If a later instrument file in the same
kit fails, earlier slots have already been overwritten in the active Scene.

`INSTRUMENT_PARAM_REFACTOR.md` calls for staged or Scene-targeted loading rather
than half-mutating the active kit. This does not explain the normal "values not
visible" symptom by itself, but it is a correctness risk when testing malformed
or partially copied Kit folders.

## Likely Root Cause

The parser side has moved to Scene-owned descriptor storage, but the consumer
side has not moved with it. The active firmware still expects loaded sound data
to be in `parameter_values[]`/`parameters2[]`, while directory Kit loading now
places instrument file data in `scenes[active].kit.instruments[*]`.

In short: the Kit folders are likely being parsed into the new owner, but the
rest of the instrument parameter system still reads and applies the old owner.

## Recommended Fix Order

1. Implement the typed Preset functions declared in `presetManager.h`, especially
   `preset_applyInstrumentRuntimeValue()` and the bounded Scene/Kit apply path.
2. Add `Core/Scene/Preset/presetMorphEngine.c` to the build once
   `preset_applyInstrumentRuntimeValue()` exists, and replace the old flat
   `preset_morph()` path for instrument images.
3. Change `PRESET_OP_KIT_LOAD` completion to rebuild morph interpolation from
   the loaded Scene images and apply descriptor-owned values from the active
   Scene, rather than normalizing/applying `parameter_values[]`.
4. Update `tools/convert_legacy_kits.py` so `velo_mod_dest` and
   `lfo_target_param` are converted to canonical instrument IDs instead of
   copied as legacy mod-target indices.
5. Add a small validation test or host-side harness that loads one generated
   Kit folder, verifies all six Scene slots match the text files, then verifies
   the active apply path receives the same values.
6. Once the new path is active, remove or quarantine stale comments and legacy
   directory-kit claims about `parameter_values[]` so future debugging does not
   follow the wrong owner.

## Verification Performed

- Read project context in `MEMORY.md`, Phase 2 scope in `SCOPING_TARGETS.md`,
  and the current refactor/audit docs.
- Traced root Kit loading through `filesystem.c`, `storageTypes.c`,
  `SceneData.c`, `InstrumentManager.c`, `presetManager.c`, and `menu.c`.
- Checked representative SD data:
  `SD_CARD/Kit/001 Slak/kitset.kcg` and
  `SD_CARD/Kit/001 Slak/slakc1.cym`.
- Ran `make -B`; the current source set builds, but only because
  `presetMorphEngine.c` is not included in `Makefile`.

## Implementation Work Log

### 2026-07-10 - Scene-to-runtime bridge implementation started

Goal: make root `Kit/NNN Name/` loads land in the live instrument parameter
path, not only in the new `scene_t` owner. The first repair pass keeps the
existing legacy MIDI/`ParameterArray` DSP application backend as a compatibility
adapter, because replacing every DSP binding directly is larger than the load
bug itself. The new ownership rule is still Scene-first: directory kits parse
into `scene_t`, Preset applies from `scene_t`, and the flat `parameter_values[]`
mirror is updated only as an affiliate needed by current Menu/DSP code.

### 2026-07-10 - Scene-to-runtime bridge implemented

Changed `Core/Scene/Preset/presetManager.c/.h`:

- Added the missing typed Preset API definitions declared in the header:
  `preset_setInstrumentParameter()`, `preset_setSupplementalParameter()`,
  `preset_applyInstrumentRuntimeValue()`, `preset_applyKitAudioRouting()`, and
  `preset_applySceneSettings()`.
- Added a private canonical-to-legacy adapter inside Preset. This maps
  `(instrument type, slot, local_param)` to the current `PAR_*` ID and then
  applies through the existing `preset_applySoundParameter()`/MIDI parser path.
  This must happen because the current DSP structs are still wired through
  `ParameterArray.c`; SceneData and InstrumentManager should not learn those
  legacy pointer bindings.
- Replaced the old loaded-kit apply cursor, which only applied velocity/LFO
  routing from `parameter_values[]`, with a Scene-owned cursor. It now applies
  Scene kit audio routing, Scene supplemental routing, and then drains one
  `presetMorphEngine` image interpolation/application per foreground pass.
- Kept `preset_morph()` and `preset_morphTick()` as compatibility wrappers, but
  redirected them to `presetMorphEngine`. Existing Menu/MIDI callers can still
  request Morph through the old names while the runtime values now come from
  `instrument_parameters[]` and `morph_instrument_parameters[]`.

Changed `Core/Menu/menu.c`:

- Removed stale `menu_normalizeSoundModTargets(parameter_values)` from the
  `PRESET_OP_KIT_LOAD` path. Root directory Kit loads no longer populate the
  flat array first, so normalizing it at completion time inspected old data.
  ALL/performance paths still use the old container serializers and were left
  on the legacy normalization path.

Changed `Makefile`:

- Added `Core/Scene/Preset/presetMorphEngine.c` to `SRCS`. This is now safe
  because `preset_applyInstrumentRuntimeValue()` exists.

Changed `Core/Hardware/SD/storageTypes.c/.h`:

- Clamped parsed `lfo_target_voice` values into the Scene-domain `1..6` range.
  Converted legacy kits can contain zero because the old firmware repaired that
  byte only during apply; with SceneData as owner, the stored supplemental value
  should be valid immediately after parsing.

Changed `tools/convert_legacy_kits.py` and regenerated `SD_CARD/Kit/`:

- Added explicit canonical local-ID and legacy `modTargets[]` compatibility
  tables.
- Converted only `velo_mod_dest` and `lfo_target_param` from legacy target
  indices to canonical `slot * 64 + local_param` IDs.
- Emitted `65535` (`INSTRUMENT_PARAM_INVALID`) for old "no target" or
  non-instrument/global targets. Ordinary byte-domain instrument values remain
  copied from the legacy payload.

Validation:

- `python3 tools/convert_legacy_kits.py` regenerated 31 root Kit folders.
- A schema/range validation pass found 31 kits, 217 files, and 0 errors.
- `make -B` succeeds with `presetMorphEngine.c` included. Existing unrelated
  warnings remain from `asyncfatfs.c`, USB packed-pointer casts, newlib syscall
  stubs, and the existing unused `filesystem_defaultTrackMidiChannel()`.

Comment/documentation follow-up:

- Updated the `preset_init()` implementation comment so it no longer claims the
  function is a no-op. It now resets Preset async state and initializes the
  Scene-owned morph/apply helper.
- Updated the public chunked-apply comment in `presetManager.h` so callers know
  `preset_tickDrumsetApply()` now advances Scene kit routing/supplemental
  affiliates and the presetMorphEngine image dump, not just the old modulation
  routing pass.

Remaining caveat:

- Kit save and ALL/performance serializers still use legacy flat arrays. This
  pass fixes root directory Kit load/application. Descriptor-driven directory
  Kit save and full container migration remain separate Phase 2 work.

### 2026-07-10 - Instrument-owned runtime descriptor cutover

Goal: rip out the false instrument-parameter identity layer and move sound
parameter meaning out of `Core/Scene/Preset/ParameterArray.c` into the
instrument files that actually own the DSP structs.

Changed `Core/DSP/Instruments/InstrumentManager.h/.c`:

- Removed the shared `INST_PARAM_*` semantic enum and the invented instrument
  dtype/source scaffolding. The generic storage cell is now the descriptor array
  index for the active instrument type.
- Added `instrument_runtime_binding_t`, where an instrument descriptor can bind
  a storage cell to an offset from that slot's runtime instrument instance.
- Added runtime slot instance lookup and direct descriptor runtime writes. Slot
  instance lookup is the only current place that knows slot 0..2 are drum
  instances, slot 3 is snare, slot 4 is cymbal, and slot 5 is hi-hat.

Changed `Core/DSP/Instruments/*/*Parameters.c`:

- Rebuilt drum/snare/cymbal/hi-hat descriptor rows as the actual source of
  truth: file key, literal short/long/category text, existing Menu dtype byte,
  flags, and runtime binding.
- Parameter meaning is now local to the instrument file. Example: a hi-hat
  `osc2_pitch_coarse` row binds to `offsetof(HiHatVoice, modOsc.modNodeValue)`,
  not a Preset-owned `PAR_*` value.

Changed `Core/Scene/SceneData.h`:

- Removed the separate supplemental struct. Instrument slot storage is generic
  `uint16_t` cell arrays for main, morph, and interpolation images. Wider cells
  allow target IDs such as `65535` without another side channel.

Changed `Core/Scene/Preset/ParameterArray.c`:

- Deleted the instrument DSP pointer table from Preset. `ParameterArray.c` now
  only preserves the legacy flat array shell for old non-instrument callers.

Changed `Core/Scene/Preset/presetManager.c` and `presetMorphEngine.c`:

- Deleted the private canonical-to-legacy `PAR_*` adapter added in the previous
  pass.
- Runtime kit apply now resolves `slot + descriptor_index` through
  `InstrumentManager` and writes the instrument-owned runtime binding directly.
- Morph walks descriptor indices and skips rows not flagged morphable.

Changed `Core/Hardware/SD/storageTypes.c/.h`:

- Instrument file parsing now looks up a file key to a descriptor index and
  writes that generic cell. It no longer routes keys through ownership enums or
  supplemental fields.

Changed `tools/convert_legacy_kits.py` and regenerated `SD_CARD/Kit/`:

- Replaced the deleted fake local-ID table with descriptor-order key lists for
  each instrument type.
- Converted legacy modulation target indices to `slot * 64 + descriptor_index`
  IDs.

Intentional breakage / follow-up:

- Runtime value shaping is now intentionally crude for many float bindings
  (`value / 127.0f`). The old MIDI path applied special curves for pitch,
  filter frequency, pan, envelopes, and modulation graph targets. Those should
  come back as instrument-owned binding kinds or shapers, not as Preset-owned
  `PAR_*` indirection.
- Instrument Menu rendering still uses the old static `menuPages.h` /
  `parameter_dtypes[]` path. The descriptors now contain enough text/dtype
  metadata to move voice pages next.
- Legacy ALL/performance save/load and old MIDI/modulation paths still know
  about `PAR_*`. This pass deliberately stops root Kit instrument files from
  depending on that layer.

Validation:

- `make -B` succeeds after the cutover.
- `python3 tools/convert_legacy_kits.py` regenerated 31 root Kit folders.
- A schema/range validation pass found 31 kits, 186 instrument files, and 0
  errors.

### 2026-07-10 - Removed flat sound ids from ParameterArray

Changed `Core/Scene/Preset/ParameterArray.h`:

- `enum ParamEnums` no longer contains voice/instrument sound ids such as
  `PAR_COARSE1`, `PAR_FILTER_FREQ_1`, `PAR_VOICE_DECIMATION1`, or
  `PAR_MIDI_NOTE1`.
- The enum now keeps only non-sound menu/performance/pattern/global ids plus
  the `END_OF_SOUND_PARAMETERS` sentinel.
- `NUM_PARAMS` remains explicitly sized to 384. That is compatibility capacity,
  not a sound parameter list: remaining MIDI/automation paths can still carry
  descriptor-style ids above the small menu range, and shrinking the backing
  `parameter_values[]` buffer produced compiler overflow warnings.
- Increased `filesystem.c`'s existing staging buffer to 512 bytes because the
  widened compatibility capacity makes the globals span larger than the old
  320-byte assumption.

Changed `Core/Menu/menu.c` and `Core/Menu/menuPages.h`:

- Replaced the giant positional sound dtype table with designated dtype entries
  for the surviving non-sound ids.
- Blank-disabled the old static voice pages. They no longer reference deleted
  flat sound ids; instrument pages need to be rebuilt from `ParamDescriptor`
  rows.
- Removed branches for deleted voice decimation, velocity target, LFO target,
  and per-voice MIDI note parameters.

Changed `Core/Menu/Cc2Text.c`:

- Collapsed the old modulation target table to the placeholder entry. That
  table was another hardcoded copy of the deleted sound parameter list.

Changed `Core/Scene/Preset/presetManager.c` and `Core/DSPAudio/modulationNode.c`:

- Removed compatibility writes to deleted `PAR_AUDIO_OUT*` and
  `PAR_VOICE_DECIMATION*` mirrors. Mixer routing still applies directly from
  Scene kit settings.
- Made the legacy waveform-target lookup inert because it depended on deleted
  waveform `PAR_*` ids. Descriptor-owned modulation targets need a fresh
  implementation.

Validation:

- `make -B` succeeds after this cut.
