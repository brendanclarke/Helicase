# S098 Inter-Session Volatile Notes

## Open items carried forward from S098

- **Device matrices remain:** the P1 and P2 device test matrices in
  `S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8 have not been run on hardware.
  The assessment (A.2/A.6) confirms builds, host tests and loop gates are
  green; the device run is next.

- **D1 (voice reset for step-only change) awaiting ear test:** when only the
  steps changed between two Scenes but the instrument is identical, the voice
  still gets a full reset. Worth deciding after the device run whether the
  slot should be decided separately from the track (see assessment D1).

- **F3 withdrawn:** the edit to `078_SESSION_HANDOFF_LOG.md` was made by the
  user, not by the session. No restore needed.

## Flash budget after S098

- Default build (FS_OS=0): **720,320 B** (headroom **32,912 B**)
- With FS_OS=1: **684,160 B** (headroom **69,504 B**)
- ITCM: unchanged at 15,976 / 16,384 B (408 B free)
- SRAM1 .bss: +2 B (drumset_apply_rebind_mask, pm_bg_kind);
  .data: +1 B (instrument_apply_rebind_mask, initialised to 63)

## RAM added in S098

| Bytes | Symbol | Section | Why |
|------:|--------|---------|-----|
| 1 | `drumset_apply_rebind_mask` | .bss | sources the Scene worker rebinds when its last changed slot commits |
| 1 | `instrument_apply_rebind_mask` | .data | lets the shared rebind cursor skip sources a switch did not touch |
| 1 | `pm_bg_kind` | .bss | distinguishes Bank / Scene / Pattern on the background machine |

## New APIs added in S098

- `preset_sceneSwitchKeepMask(target, &effect_same)` — the per-track identity
  compare. Returns a 7-bit keep mask.
- `preset_prepareSceneSwitch(target, keep_mask)` — carry morph_interpolation
  and retarget the Morph worker.
- `preset_startSceneSwitchApply(keep_mask, effect_same)` — selective voice
  apply, modulation rebind and Effect rebind/activate.
- `seq_selectActivePattern(target, keep_mask, effect_same)` — signature
  changed from one arg; resets only changed tracks.
- `preset_requestBackgroundSceneLoad(slot, scene_mask, kind)` — Scene and
  Pattern Loads through the background machine.
- `instrumentManager_prepareModulationForSlotChange(changed, effect_changed)`
  — selective modulation detach.
- `presetMorph_retargetScene(target, keep_mask)` — Morph worker retarget
  across a Scene switch.

## New host test

- `scene_identity` (`tools/dsp_test/`): three laws with break proof
  (pool-offset compare must fail law 1).

## New DEV trace stage

- `'x'` (`AUTOSAVE_TRACE_STAGE_SCENE_SWITCH`): one record per executed
  whole-Scene change. flags = keep mask + Effect bit; value = target,
  previous, compare time (µs). Decoder in `decode_devlogs.py`.

## Notes for the next session

- `seq_selectActivePattern()` now takes three args (target, keep_mask,
  effect_same). Its only caller is `menu_perfSceneChangeExecute()`.
- `preset_startDrumsetApply()` still exists and is called by other paths
  (boot, copy/clear, Load apply). Only the Scene change uses
  `preset_startSceneSwitchApply()`.
- Three `_Static_assert`s in `presetManager.c` guard Scene 17 field list /
  struct size consistency. A struct change that breaks them requires updating
  the X-macro field lists.
- The F1 guard (`instrument_apply_active && rebind != 0`) widens the rebind
  to all six sources when an Instrument/Kit apply is in flight during a Scene
  switch. This preserves pre-S098 behaviour for the rare window.
