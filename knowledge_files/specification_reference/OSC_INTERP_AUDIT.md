# OSC_INTERP_AUDIT.md

Session 036 note: this is a historical feature audit for oscillator waveform
interpolation. Its persistence note was updated in Session 040: oscillator
interpolation is an allowlisted root settings.cfg value, not a legacy glo.cfg
dependency. Current filesystem policy lives in FILESYSTEM_SPEC.md.

## Goal
Add simple/fast/dirty oscillator waveform interpolation for modulation-driven waveform automation, with a global ON/OFF setting that persists in global save/load.

## Implementation Status (2026-05-14)
Implemented in code and build-verified.

### Completed
1. Added a new global parameter: `PAR_OSC_WAVE_INTERP`.
2. Added menu/UI exposure on Global page (`MENU_MIDI_PAGE`, subpage 2).
3. Added global runtime control path:
- `menu_parseGlobalParam(PAR_OSC_WAVE_INTERP, value)` now calls `modNode_setWaveInterpEnabled(value)`.
4. Added modulation runtime state in `modulationNode.c`:
- global enable flag,
- per-audio-block generation stamp.
5. Added per-oscillator interpolation state in `OscInfo`:
- `waveInterpNext`,
- `waveInterpFrac`,
- `waveInterpGeneration`.
6. Added waveform-target-aware modulation behavior in `modNode_updateValue()`:
- When interpolation is enabled and target is an oscillator waveform param, modulation captures fractional waveform position and stores blend state for adjacent waveform IDs.
7. Added render-time interpolation in `Oscillator.c`:
- block and single-sample paths,
- FM and non-FM paths,
- active only when generation/tag/frac checks pass.
8. Interpolation domain now covers all oscillator waveform IDs, including user sample waveform IDs.
9. Build verification:
- `make -j4` passes.

## Save/Load Persistence (Current Global Settings)
This requirement is satisfied.

Why:
1. `PAR_OSC_WAVE_INTERP` is in the global parameter domain.
2. The strict keyed settings.cfg schema writes and reads the allowlisted key
   `osc_wave_interp` in filesystem_saveGlobals_tick() and
   filesystem_loadGlobals_tick().
3. On apply, menu global application calls menu_parseGlobalParam() for the
   loaded value, which re-applies the modulation runtime toggle.

Result:
- The one-second keyed `settings.cfg` writer stores the interpolation setting.
- Boot loading of `settings.cfg` restores and reapplies it; legacy `GLO.CFG` /
  `glo.cfg` is not the current persistence path.

## File-Level Changes
1. `Core/Bank/Scene/Preset/ParameterArray.h`
- Added `PAR_OSC_WAVE_INTERP` in global params.

2. `Core/Menu/menu.h`
- Added `TEXT_OSC_INTERP` name token.
- Added `SHORT_OSC_INTERP` / `LONG_OSC_INTERP` enums.

3. `Core/Menu/MenuText.h`
- Added short label `"wip"`.
- Added long label `"Osc Interp"`.

4. `Core/Menu/menuPages.h`
- Added parameter on global page row 2:
- `TEXT_OSC_INTERP` ↔ `PAR_OSC_WAVE_INTERP`.

5. `Core/Menu/menu.c`
- Added dtype entry: `PAR_OSC_WAVE_INTERP -> DTYPE_ON_OFF`.
- Added `valueNames[]` entry for `TEXT_OSC_INTERP`.
- Added global parse case calling `modNode_setWaveInterpEnabled()`.
- Set default to OFF in `menu_init()`.

6. `Core/DSPAudio/modulationNode.h`
- Added API:
- `modNode_setWaveInterpEnabled()`
- `modNode_getWaveInterpEnabled()`
- `modNode_getWaveInterpGeneration()`

7. `Core/DSPAudio/modulationNode.c`
- Added global interpolation enable + generation state.
- Incremented generation each `modNode_resetTargets()` block.
- Added oscillator-wave-target mapping logic.
- Added interpolation-state write path in `TYPE_UINT8` modulation case for full waveform range.

8. `Core/DSPAudio/Oscillator.h`
- Added interpolation fields to `OscInfo`.

9. `Core/DSPAudio/Oscillator.c`
- Added interpolation-active checks.
- Added generic waveform-eval blend helpers using existing block render paths.
- Added FM/non-FM blend paths for block + single sample functions.

## Runtime Behavior Notes
1. Interpolation is linear (`a + frac * (b - a)`) between adjacent waveform IDs.
2. Blend works across all waveform categories (periodic, noise, crash, user samples).
3. This is intentionally "dirty": no spectral matching and no class-aware transition shaping.

## Risks / Notes After Implementation
1. CPU cost increases when interpolation is active for waveform-modulated oscillators.
2. Cross-class transitions (for example periodic↔noise↔sample) can sound rough by design in this simple version.
3. Existing compiler warnings unrelated to this feature remain (same class as prior baseline).

## Validation Checklist
1. `make -j4` passes. ✅
2. Verify on hardware:
- Set LFO destination to waveform param,
- test transitions across periodic/noise/sample waveform regions,
- compare OFF vs ON behavior.
3. Verify persistence:
- set ON, save globals, power cycle/load globals, confirm ON restored.
