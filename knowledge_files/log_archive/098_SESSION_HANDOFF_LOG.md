# Session 098 — Scene Switch Corrections

```
DATE: 2026-11-18
SESSION GOAL: Make Scene switches to identical content inaudible (T1); route Scene and Pattern
              Loads through Scene 17 so playback is never disrupted by an in-place write (T2);
              flash the Load/Save mode LED whenever Scene 17 exists (T3).
COMPLETED: P0 assessment and plan (all decisions taken by user); P1 per-track identity-based
           Scene switching; P2 Scene/Pattern Loads through the background machine + Scene 17 LED;
           assessment with two fixes applied (F1 carry_rebind guard, F2 comment correction).
           All code in the tree, builds and host tests green, loop gate clean.
VERIFIED ON HARDWARE: No — device matrices remain for the user (P1 and P2).

CHANGES THIS SESSION:
- Core/Sequencer/sequencer.c: seq_clearSceneAutomationDirtyFor(), seq_resetTrackForSceneChange(),
  seq_realignTrackPosition() (new); seq_selectActivePattern() rewritten to take keep_mask and
  effect_same; seq_realignTrackToMasterClock() delegates to seq_realignTrackPosition() (P1)
- Core/Sequencer/sequencer.h: seq_selectActivePattern() signature and contract updated (P1)
- Core/DSP/Instruments/InstrumentManager.c: instrumentManager_installedTouches(),
  instrumentManager_prepareModulationForSlotChange() (new) (P1)
- Core/DSP/Instruments/InstrumentManager.h: prepareModulationForSlotChange() prototype (P1)
- Core/Bank/Scene/Preset/presetMorphEngine.c: presetMorph_retargetScene() (new) (P1)
- Core/Bank/Scene/Preset/presetMorphEngine.h: retargetScene() prototype (P1)
- Core/Bank/Scene/Preset/presetManager.c: PM_ALL_SLOTS, drumset_apply_rebind_mask,
  instrument_apply_rebind_mask, preset_clearSlotStepOverrides(), shared PRESET_*_FIELDS X-macro
  lists with _Static_asserts, preset_trackStepsMatch(), preset_instrumentImageMatches(),
  preset_trackMatches(), preset_slotMatches(), preset_effectMatches(),
  preset_keptSlotsOfTracks(), preset_sceneSwitchKeepMask(), preset_prepareSceneSwitch(),
  preset_startSceneSwitchApply() (new); preset_startSceneModulationRebind() takes source_mask;
  preset_startDrumsetApply() sets drumset_apply_rebind_mask; masked rebind cursor in
  preset_tickInstrumentApply(); Scene 17 copy functions use shared lists; pm_bg_kind (new);
  preset_requestBackgroundSceneLoad() (new); preset_backgroundBankLoadRunning() Bank-only;
  phase 1 skip-quiesce for Scene/Pattern; phase 2 wait-not-drop for copy/clear; phase 4
  Scene/Pattern post with availability session; preset_bgEnd() Bank-only CC0 result;
  completion callbacks end availability session (P1/P2)
- Core/Bank/Scene/Preset/presetManager.h: PRESET_BG_KIND_*, three P1 prototypes,
  preset_requestBackgroundSceneLoad() prototype (P1/P2)
- Core/Menu/menu.c: executor decides/carries/switches/applies per track; Scene and Pattern
  Load OK through preset_requestBackgroundSceneLoad(); menu_syncQueuedSceneLed() Scene 17
  fast-blink (P1/P2)
- Core/Hardware/frontPanel/buttonHandler.c: PAR_FOLLOW-off double-click explicit realign (P1)
- Core/Hardware/SD/filesystem.c: filesystem_patternRealignPlayingTracks() comment updated (P2)
- Core/Bank/Scene/AutosaveTrace.h: AUTOSAVE_TRACE_STAGE_SCENE_SWITCH = 'x' (P1)
- tools/diagnostic_tools/decode_devlogs.py: 'x' branch added (P1)
- tools/dsp_test/Makefile: scene_identity target (P1)
- tools/dsp_test/modules/scene_identity.c: new host test (P1)

KNOWN ISSUES INTRODUCED: None.
KNOWN ISSUES RESOLVED: B1 (Scene switch to identical Scene produces audible glitches).
  Scene/Pattern Loads into the playing Scene no longer write in place while playback reads.

NEXT SESSION RECOMMENDED GOAL: Run the P1 and P2 device matrices
  (S098_P0_SCENE_SWITCH_CORRECTIONS.md §8). Then update MEMORY.md and specification documents
  per §10/T of the plan. Then new feature work or B2 (AutoSave speed).
BLOCKERS: Device matrices not yet run (no hardware in this session).

CRITICAL REMINDERS FOR NEXT SESSION:
- The three approved RAM bytes (drumset_apply_rebind_mask, instrument_apply_rebind_mask,
  pm_bg_kind) are in the tree with "approved 2026-11-18" comments.
- The assessment F1 fix (carry_rebind guard) and F2 fix (comment) are applied.
- The shared X-macro field lists (PRESET_TRACK_REGION_FIELDS etc.) and their _Static_asserts
  are the authority on what a switch compares and what Scene 17 copies. Adding a field to
  scene_settings_t, kit_settings_t or pat_scene_region_t fires the assert.
- instrument_apply_rebind_mask initialises to PM_ALL_SLOTS (63), so it is .data not .bss.
- preset_backgroundBankLoadRunning()/Locked()/Unlocked() are Bank-only. The Scene/Pattern
  Load uses the same phases but does not lock the panel or filter MIDI.
- The S097 P3 realign is a backstop, not dead code: it still covers Loads posted while
  stopped and the copy/clear reload job.
- AutoSave is suspended while Scene 17 exists (unchanged, B2 item).
```

---

## Source Documents

This log retains all relevant detail from the following documents, which are deleted
after this log is written:

- `S098_P0_SCENE_SWITCH_CORRECTIONS.md` — Assessment and plan (§1–§11)
- `S098_P0_IMPLEMENTATION.md` — Line-level schedule (A–Q), progress log, assessment

---

## Targets and Decisions

**Targets (user, 2026-11-18):**

- **T1.** An instant switch from one Scene to an identical Scene is inaudible,
  whenever it happens.
- **T2.** A Scene Load during playback that loads onto the playing Scene, or onto
  the Scene any track plays (per-track Scene playback), first copies what is
  playing to Scene 17 and moves playback there. The user then switches manually
  after the Load completes, as a Bank Load does today.
- **T3.** While playback runs from Scene 17, the Load/Save mode button LED
  flashes, always. That alone is enough to alert the user.

**Decisions (user, 2026-11-18):**

| # | Question | Decision |
|---|----------|----------|
| Q1 | With `pcr` on, does a switch to an identical Scene still restart? | **Yes.** The restart runs after the switch; identical slots get no apply. |
| Q2 | Identical and changed tracks in one switch | **One procedure, decided per track.** |
| Q3 | The S097 P3 realign | **Kept as a backstop**, comment updated. |
| Q4 | Copy/clear in phase 2 for Scene/Pattern Loads | **Wait** (bounded by `PM_BG_WAIT_LIMIT_MS`), do not drop. |
| Q5 | Availability session for running Scene/Pattern Loads | **Yes**, through Scene 17 or direct. |
| Q6 | Panel unlock during a Scene/Pattern Load | **No.** The Load-page hold stays; the Bank-only predicates stay Bank-only. |
| Q7 | AutoSave suspended while Scene 17 exists | **Kept**; a B2 item. |
| Q8 | Legato across non-identical changes (handoff) | **Not in S098.** |
| Q9 | Kit and Instrument Loads into the playing Scene | **Unchanged**, not through Scene 17. |
| Q10 | MIDI on a switch | **Program change sent; note-offs only for changed tracks.** |
| Q11 | `scn` LFO retrigger on an identical track | **No restart.** |

**RAM sign-off (approved by user, 2026-11-18, before implementation):** +3 B SRAM1.

| Bytes | Symbol | File | Phase | Why |
|------:|--------|------|-------|-----|
| 1 | `drumset_apply_rebind_mask` | `presetManager.c` | P1 | sources the Scene worker rebinds when its last changed slot commits |
| 1 | `instrument_apply_rebind_mask` | `presetManager.c` | P1 | lets the shared rebind cursor skip sources a switch did not touch |
| 1 | `pm_bg_kind` | `presetManager.c` | P2 | Load kind (Bank, Scene, Pattern) |

---

## Phase P0 — Assessment and Plan

Source: B1 (`DRAFT_ONGOING_BUGS.md`) and the future-work note in
`S097_P3_FS_OPTIMIZE_INVESTIGATION.md` §10.

### What a Scene change did before S098

`menu_perfSceneChangeExecute()` → `seq_selectActivePattern()` →
`preset_startDrumsetApply()`. Every track was reset on every change. The resets
(S1–S12 sequencer, P1–P5 preset) destroyed state that was identical between
the two Scenes:

| # | Reset | Audible effect between identical Scenes |
|---|-------|-----------------------------------------|
| S1 | play state cleared | a finished `onc`/`1fr` track restarts |
| S2 | repeat/random/condition counters cleared | `X:Y`, `fil`/`!fl` and `lst` conditions evaluate as first pass |
| S3 | roll and length window cleared | step roll cut; fill < 100 % step loses its length window |
| S4 | R12 menu override cleared | a held menu edit stops |
| S5 | automation ramps broken | running ramps end |
| S6 | epochs bumped, echo marks expired | new LiveRecord take, MIDI note-mode re-assert |
| S7 | all tracks realigned to master clock | cursors jump; random mode draws a new step; FX clock realigned |
| S8 | Colloid change mark armed for all tracks | every Colloid releases |
| S9 | all queued triggers dropped | a step fired just before the switch is lost |
| S10 | all MIDI notes cut | external notes end |
| S11 | Scene-target dirty bits cleared | transport restore stops for the outgoing Scene's automation |
| P1 | all modulation detached (LFO, velocity, Morph, Effect) | tone shift until every slot re-commits (up to 1 s) |
| P2 | FX-send/audio-out/Morph step overrides cleared | step automation stops acting |
| P3 | Effect activated with `force_all` | FX parameters rewritten, FX sequence invalidated |
| P4 | every voice slot memset and type-init at quiet/trigger | envelope from 0, filter states clear, Colloid gate/glide/intervals lost |

### The inaudible switch already in the tree

`seq_switchToBackgroundScene()` + `effects_rebindSourceScene()` (S082): changes
only which Scene playback reads from. It is inaudible because Scene 17 is an
exact copy.

### No handoff struct needed

Every piece of voicing state is stored per slot or per track, never per Scene.
Only two derived values are stored per Scene and must move with an identical
track: `morph_interpolation[]` and the Morph worker's Scene index. These are
carried by `preset_prepareSceneSwitch()`.

---

## Phase P1 — Per-Track Identity-Based Scene Switching (T1)

### Design

One switch procedure, decided per track (Q2). `preset_sceneSwitchKeepMask(n, &effect_same)` runs before any state moves. Bit `t` of the result is set when track `t` and its voice slot are identical in Scene `n` and in the Scene the track plays now. Tracks 5 and 6 (which share slot 5) are kept or changed together.

**What is compared (shared X-macro field lists):**

Per track (Pattern region): `track_length`, `track_scale`, `track_shuffle`, `track_play_mode`, `track_loop_exclude` bit, `track_auto_smooth` bit. Per track (Scene settings): `midi_channel`, `midi_note`, `track_morph_length`, `track_morph_scale`, `track_morph_shuffle`. Per step: trigger bit and block content via `pat_rawReadBlock()` (content, not pool addresses). Per slot: `type`, `instrument_parameters[]`, `morph_instrument_parameters[]` (but not `morph_interpolation[]`), `voice_morph_amount`, `audio_out`, `fx_send_amount`, `fx_send_morph`, `fader_setting`, and for slot 5 the `slot6_track7_*` Kit settings. Effect: the whole Effect record and `effect_morph_amount`.

**Not compared** (on purpose): `morph_amount` (PERF mirror), `bus_comp[]` (read live), `chain_condition`/`chain_next` (dropped by Scene 17 too), `pattern_change_bar`/`pattern_next`, names, `morph_interpolation[]` (derived).

Three `_Static_assert`s fire when `scene_settings_t` (75 B), `kit_settings_t` (2 B) or `pat_scene_region_t` changes size, so a new field gets classified.

### The switch

**Sequencer side: `seq_selectActivePattern(pattern, keep_mask, effect_same)`**

Every track reads `pattern`. Changed tracks get: play state cleared (once-mode restarts), condition counters cleared, roll and length window cleared, R12 menu override cleared, automation ramp broken, echo mark expired (`seq_resetTrackForSceneChange()`), cursor and DDA realigned to master clock (`seq_realignTrackPosition()`), Colloid change mark armed, queued triggers dropped, MIDI note ended, Scene-target dirty bits cleared for that track's targets (`seq_clearSceneAutomationDirtyFor()`).

Kept tracks keep all of S1–S11.

Epochs move only when something changed. LiveRecord ring entries are dropped only when no track is kept. FX sequence realigns only when the Effect changed. Effect overlays reset when the Effect or any track changed. Scene 17 is released when left.

**Preset side: `preset_startSceneSwitchApply(keep_tracks, effect_same)`**

For each changed slot: LFO phase captured, step overrides cleared (`preset_clearSlotStepOverrides()`), Morph pass requested, drumset worker commits it at quiet/trigger/force.

Modulation graph prepared selectively (`instrumentManager_prepareModulationForSlotChange()`): a changed source has both LFO pairs and velocity binding detached. A kept source is detached only when its supplemental target touches a changed slot or the changed Effect, or when its velocity node points directly into a changed slot. A kept source whose descriptor LFO adapter targets a changed slot joins the rebind mask but is not detached. Kept sources with bindings wholly inside kept slots are untouched: their LFO keeps modulating without a gap.

Rebind: `drumset_apply_rebind_mask` holds the sources to revisit; `instrument_apply_rebind_mask` gates the cursor in `preset_tickInstrumentApply()`. Full applies (Load, boot, copy/clear) set both to all six, as before.

An identical Effect is rebound (`effects_rebindSourceScene()`); a changed one is activated (`effects_activateScene()`). PERF mirrors always synced.

**Before the switch: `preset_prepareSceneSwitch(target, keep_tracks)`**

Copies `morph_interpolation[]` of kept slots into the target (64 B per slot). Retargets the Morph worker (`presetMorph_retargetScene()`): the worker keeps only the kept slots' request/pass bits and drops the changed ones.

### The executor (`menu_perfSceneChangeExecute()`)

```
1. keep = preset_sceneSwitchKeepMask(scene_index, &effect_same);
2. preset_prepareSceneSwitch(scene_index, keep);
3. scene_selectActive(scene_index);
   bank_selectActiveSceneForEditMask(scene_index);
   menu_setShownPattern(scene_index);
   seq_selectActivePattern(scene_index, keep, effect_same);
   seq_cancelPendingDirectChange();
   if (pcr) seq_resetToPatternStart();     // Q1: runs even for identical
4. preset_startSceneSwitchApply(keep, effect_same);
   LED refresh and repaint
```

### PERF double-click realign

The `PAR_FOLLOW`-off double-click relied on the Scene change realigning every track. With T1, identical tracks are not realigned, so an explicit `seq_realignActivePatternToMasterClock()` call was added in `buttonHandler.c`, matching the `PAR_FOLLOW`-on branch.

### Consequences

Re-pressing the playing Scene (identical by definition) is silent unless `pcr` is on. Each identical track: keeps LFO phase (`scn` retrigger does not apply, Q11), keeps finished once-mode state, keeps random-mode position, keeps condition counts. The program change is still sent; identical tracks' external MIDI notes are not cut (Q10). LiveRecord carries on when no track changes (D35).

### `seq_realignTrackPosition()` (new)

The position-only half of `seq_realignTrackToMasterClock()`. Derives step index and DDA accumulator from `seq_elapsedPpqTicks` with the track's effective length, scale and play mode (S078 §2.4.5). The two callers (the Scene switch and the full realign) share one formula. The full realign delegates to it, then runs its own side effects (epoch, override, ramp, FX realign).

### DEV trace stage `'x'`

One `'x'` record per executed Scene change. Flags bits 0..6 = keep mask (bit `t` = track `t` kept), bit 7 = Effect identical. Value32 = target Scene (bits 0..7) | outgoing active Scene (bits 8..15, 16 = Scene 17) | compare time in µs (bits 16..31, saturated at 0xFFFF). Producer: `preset_sceneSwitchKeepMask()`. Consumer: `decode_devlogs.py` (`'x'` branch).

### Host test `scene_identity`

Three laws: (1) same steps at different pool offsets match; (2) a changed block byte, trigger bit, or block-less trigger do not match; (3) two instrument slots differing only in `morph_interpolation[]` match; a changed type/Normal/Morph byte does not. Break proof: the Makefile rebuilds the fragment comparing whole address entries, and law 1 must fail.

### P1 build (FS_OS=1)

| Measure | Start-of-P1 | After P1 | Delta |
|---------|------------|----------|-------|
| text | 679,952 | 683,160 | +3,208 |
| data | 432 | 432 | 0 |
| bss section | 422,452 | 422,452 | 0 (two bytes fit padding) |
| flash headroom | 73,280 | 70,072 | −3,208 |

### P1 gate results

Loop gate: 86/86 oscillator loops exact, 58 mixer hot loops equal. ISR/render: 17/18 standalone bodies identical; `audio_check_and_render.part.0` grew 4 B (branch layout change in inlined MIDI note-off, accepted as LTO jitter). Host tests green, `scene_identity` 0 failures, break caught.

---

## Phase P2 — Scene and Pattern Loads Through Scene 17 (T2) and the LED (T3)

### Design

Scene and Pattern Loads from the Menu go through `preset_requestBackgroundSceneLoad(slot, scene_mask, kind)` into the same background machine that Bank Loads use. `pm_bg_kind` (+1 B SRAM1) distinguishes `PRESET_BG_KIND_BANK` (0), `PRESET_BG_KIND_SCENE` (1) and `PRESET_BG_KIND_PATTERN` (2).

**Phases for a Scene or Pattern Load:**

| Phase | What it does |
|-------|--------------|
| 1 | Decide whether Scene 17 is needed (transport runs and a played Scene is a destination). If not, skip to phase 4 |
| 2 | AutoSave yield and waits; copy/clear **waited for, not dropped** (Q4); dirty mask never discarded |
| 3 | Scene 17 build and switch (unchanged) |
| 4 | Posts `preset_loadSceneForScenes()` or `preset_loadPatternForScenes()`; starts availability session for destinations when transport runs (Q5) |
| 5 | Waits for `pm_status` idle |

**What stays Bank-only:** `preset_backgroundBankLoadRunning()`, `…Locked()`, `…Unlocked()` (Q6). The MIDI filter, chain gate and panel lock do not change for Scene/Pattern Loads. The MIDI CC0 result is written only for a Bank Load.

**Completion:** `on_scene_load_complete()` and `on_pattern_load_complete()` end the availability session. `preset_startLoadApply()` finds the active Scene is 16 and applies nothing.

**After the Load:** playback stays on Scene 17 until the user changes Scene. With P1, the switch back changes only the tracks whose content changed.

### The realign backstop (Q3)

`filesystem_patternRealignPlayingTracks()` does nothing while Scene 17 carries the tracks. It still acts in two cases: a Load posted while stopped (then started before the Pattern is read), and the copy/clear reload job. Only its comment changed.

### T3: the Scene 17 LED

`menu_syncQueuedSceneLed()` now fast-blinks `LED_MODE4` whenever `scene_backgroundExists()`, in every mode, transport running or stopped. The existing unlocked-Bank-load condition is kept. The fast layer sits above base and blink states; `led_setMode2()` repaints do not hide it.

### P2 build (FS_OS=1)

| Measure | After P1 | After P2 | P2 delta |
|---------|----------|----------|----------|
| text | 683,160 | 683,704 | +544 |
| flash headroom | 70,072 | 69,528 | −544 |

The three approved bytes are present as symbols. `.bss`/`.data` section totals do not move (linker padding absorbs them).

### P2 gate results

Loop gate: 86/86 exact, 58 mixer hot loops equal. ISR/render identical to P1 (the same single layout change). Host tests green.

---

## Assessment

All entries A–Q verified against the schedule. Clean builds for both `FS_OS=0` (720,320 B, headroom 32,912 B) and `FS_OS=1` (683,704 B, headroom 69,528 B). Image byte-identical to the scratch build.

### Findings

**F1 (defect, low, fixed): carry_rebind guard.** A Scene switch during an in-flight Instrument or Kit apply (which runs on the instrument worker with the drumset worker idle) could start the shared rebind cursor with a partial mask, losing bindings of the loaded Instrument's other sources. Before S098 the overwrite also happened, but with all six sources, so it was covered. Fix: `if (instrument_apply_active && rebind != 0u) carry_rebind = PM_ALL_SLOTS;` in `preset_startSceneSwitchApply()`. No RAM.

**F2 (comment, fixed):** `buttonHandler.c` J1 comment's last sentence was wrong about deferred changes. Corrected to: "With a deferred (scd) change the realign runs at the press, as in the PAR_FOLLOW-on branch; when the change fires, its changed tracks are realigned again."

**F3 and F5:** raised wrongly by the assessor and withdrawn. The user made the edit and approved the RAM.

### Build after fixes (FS_OS=1)

| Check | Result |
|-------|--------|
| text | 683,728 (+24 B) |
| flash used / total | 684,160 / 753,664 B |
| flash headroom | 69,504 B |
| Loop gate | 86/86 exact, 58 equal |
| ISR/render | 17/18 identical (same layout change); all 18 identical between assessed and fixed builds |
| Host suite | green, scene_identity 0 failures, break caught |
| Image | `build/LXRV2_lxr02.img` 684,176 B file, 684,160 B payload |

### Design observations (no change)

**D1.** A track whose steps changed but whose instrument is identical still gets its voice reset (the decided rule). Worth deciding by ear after the device run.

**D2.** Step overrides carry no writer identity. A kept track's Scene-target automation of a changed slot is cleared with that slot. A changed track's automation of a kept slot stays until the next automated step.

---

## Files Changed

| File | Change | Phase |
|------|--------|-------|
| `Core/Sequencer/sequencer.c` | `seq_clearSceneAutomationDirtyFor()`, `seq_resetTrackForSceneChange()`, `seq_realignTrackPosition()` (new); `seq_selectActivePattern()` rewritten with `keep_mask`/`effect_same`; `seq_realignTrackToMasterClock()` delegates to `seq_realignTrackPosition()` | P1 |
| `Core/Sequencer/sequencer.h` | `seq_selectActivePattern()` signature + contract | P1 |
| `Core/DSP/Instruments/InstrumentManager.c` | `instrumentManager_installedTouches()`, `instrumentManager_prepareModulationForSlotChange()` (new) | P1 |
| `Core/DSP/Instruments/InstrumentManager.h` | `prepareModulationForSlotChange()` prototype | P1 |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | `presetMorph_retargetScene()` (new) | P1 |
| `Core/Bank/Scene/Preset/presetMorphEngine.h` | `retargetScene()` prototype | P1 |
| `Core/Bank/Scene/Preset/presetManager.c` | `PM_ALL_SLOTS`, `drumset_apply_rebind_mask`, `instrument_apply_rebind_mask` (P1); `preset_clearSlotStepOverrides()`, shared `PRESET_*_FIELDS` lists + `_Static_assert`s, identity tests, `preset_sceneSwitchKeepMask()`, `preset_prepareSceneSwitch()`, `preset_startSceneSwitchApply()` (P1); `preset_startSceneModulationRebind()` takes `source_mask`; masked rebind cursor; Scene 17 copy functions use shared lists (P1); `pm_bg_kind`, `preset_requestBackgroundSceneLoad()`, phase changes (P2); F1 `carry_rebind` guard (assessment) | P1/P2 |
| `Core/Bank/Scene/Preset/presetManager.h` | `PRESET_BG_KIND_*`, P1 prototypes, `preset_requestBackgroundSceneLoad()` | P1/P2 |
| `Core/Menu/menu.c` | executor per-track decide/carry/switch/apply (P1); Scene/Pattern Load OK through background request (P2); `menu_syncQueuedSceneLed()` Scene 17 fast-blink (P2) | P1/P2 |
| `Core/Hardware/frontPanel/buttonHandler.c` | `PAR_FOLLOW`-off double-click explicit realign; F2 comment fix | P1 |
| `Core/Hardware/SD/filesystem.c` | `filesystem_patternRealignPlayingTracks()` comment updated | P2 |
| `Core/Bank/Scene/AutosaveTrace.h` | `AUTOSAVE_TRACE_STAGE_SCENE_SWITCH = 'x'` | P1 |
| `tools/diagnostic_tools/decode_devlogs.py` | `'x'` decode branch | P1 |
| `tools/dsp_test/Makefile` | `scene_identity` target + `.PHONY` | P1 |
| `tools/dsp_test/modules/scene_identity.c` | new host test | P1 |

---

## Items Left for the User

- **P1 and P2 device matrices** (`S098_P0_SCENE_SWITCH_CORRECTIONS.md` §8). No
  hardware in this session.
- **Specification and MEMORY.md updates** per §10/T of the plan (now handled by
  the session logging process).

---

## Out of Scope

- B2 (AutoSave speed); Q7 records how this work touches it.
- Legato across non-identical changes (Q8).
- Kit or Instrument Loads through Scene 17 (Q9).
- The copy/clear reload job through Scene 17 (§5.4 of the plan).
