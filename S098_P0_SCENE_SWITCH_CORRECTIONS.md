# S098 P0 — Scene Switch Corrections: Assessment and Plan

Date: 2026-11-18. Branch `dev-ph7-alkaline` at `04c5c004`, with the S097 P3
work uncommitted in the tree (fact only). Line numbers are as of this tree.

**Status: decisions taken (user, 2026-11-18).** The recommendations in §6 are
accepted, Q2 is answered differently from the recommendation, and the Scene 17
mode-LED rule (T3) was added. The line-level schedule is
`S098_P0_IMPLEMENTATION.md`. No code has been changed yet.

Source: `DRAFT_ONGOING_BUGS.md` B1, and the future-work note in
`S097_P3_FS_OPTIMIZE_INVESTIGATION.md` §10.

**Targets (user):**

- **T1.** An instant switch from one Scene to an identical Scene is inaudible,
  whenever it happens.
- **T2.** A Scene Load during playback that loads onto the playing Scene, or onto
  the Scene any track plays (per-track Scene playback), first copies what is
  playing to Scene 17 and moves playback there. The user then switches manually
  after the Load completes, as a Bank Load does today.
- **T3 (added 2026-11-18).** While playback runs from Scene 17, the Load/Save
  mode button LED flashes, always. That alone is enough to alert the user.

---

## 1. Summary

- **T1: the switch destroys state on purpose.** A Scene change runs about
  fifteen resets and clears (§2.2). Each one can be heard by itself, even
  between two identical Scenes: queued triggers are dropped, rolls and
  step-length windows are cut, conditions restart, the random play mode jumps,
  every voice slot is wiped (forced at its next trigger), Morph, Effect and
  velocity modulation is detached until every slot has committed, the FX
  sequence step and Effect overlays are reset, and the Colloid is released.
- **The inaudible switch already exists.** The Scene 17 switch
  (`seq_switchToBackgroundScene()` + `effects_rebindSourceScene()`) changes
  only which Scene playback reads from. It can do that because Scene 17 is an
  exact copy.
- **One switch procedure, decided per track (Q2, user).** A Scene change runs
  the same way whether all, some or none of the tracks are identical. Per
  track, the procedure compares what the track plays now with what the new
  Scene holds for it:
  - an identical track (and its voice slot) only changes which Scene it reads
    from, like the Scene 17 switch;
  - a different track gets exactly today's per-track change.

  The Effect is decided the same way, once for the Scene. A whole identical
  Scene is simply the case where nothing differs (§4).
- **A handoff struct is not needed (§3).** Every piece of voicing state is
  stored per slot or per track, never per Scene. That includes the Colloid
  gate, sustain, glide and paraphonic intervals, the Tetrakt layers and S1
  line, the sequencer cursors and counters, and the automation holds and
  overrides. Nothing is lost by changing the Scene index. It is lost because
  the switch clears it. Only two derived values are stored per Scene and must
  move with an identical track: the `morph_interpolation[]` image and the Morph
  worker's Scene index.
- **T2: the Bank Load state machine is mostly generic.** In
  `preset_tickBackgroundBankLoad()`, phases 1–3 (decide, quiesce, build and
  switch, or rebuild and move) and phase 5 (wait) work for any kind of Load.
  Only phase 4 posts a Bank request. Scene and Pattern Loads run through the
  same machine with a one-byte Load kind (§5). The S097 P3 realign stays as a
  backstop (Q3).
- **T3:** the existing fast-blink slot that already flashes `LED_MODE4` during
  an unlocked Bank Load also flashes whenever Scene 17 exists (§5.6).

**Order:**

1. **P1:** T1, the per-track switch.
2. **P2:** T2 and T3.
3. **P3 (optional):** a `pattrace.bin` decoder.

Each phase ends with a device check before the next one starts.

**Cost:**

- **RAM:** +3 B SRAM1, which needs sign-off (§9).
- **CPU:** no DSP or render-path change. The new foreground work is one
  identity test per executed Scene change (P1), and the existing Scene 17 build
  per Load start (P2).
- **Flash:** grows by a small amount, measured in each phase.
- **Loop gate:** applies, because `sequencer.c`, `presetManager.c`,
  `InstrumentManager.c` and `menu.c` are in the LTO unit.

---

## 2. T1: what a Scene change does today

### 2.1 Call path

`menu_perfModeSceneChangeFrom()` is called for a PERF press and for a MIDI PC.
If `scd` is not instant and the transport runs, the change waits for
`menu_serviceDeferredSceneChange()` (`menu.c:10474`). Otherwise
`menu_perfSceneChangeExecute()` (`menu.c:10333`) runs at once. It calls, in
order:

1. `scene_selectActive()`
2. `bank_selectActiveSceneForEditMask()`
3. `menu_setShownPattern()`
4. **`seq_selectActivePattern()`** (`sequencer.c:2367`; its only caller)
5. `seq_cancelPendingDirectChange()`
6. `seq_resetToPatternStart()`, only if `pcr` is on
7. **`preset_startDrumsetApply()`** (`presetManager.c:1973`)
8. LED refresh and repaint

A chain-fired change to the Scene already playing goes to
`seq_chainRepeatSelf()` instead, with no DSP apply.

### 2.2 What each step resets, and what that does between identical Scenes

**In `seq_selectActivePattern()`:**

| # | Action | Effect between identical Scenes |
|---|--------|---------------------------------|
| S1 | `seq_trackPlayState[]` cleared | a finished `onc`/`1fr` track restarts |
| S2 | `seq_trackRepeatCount[]`, `seq_trackRndStepCount[]`, `seq_trackLastConditionFired[]` cleared | `X:Y`, `fil`/`!fl` and `lst` conditions evaluate as on a first pass, so conditional steps appear or vanish (B1 row 1) |
| S3 | `seq_rollClearTrack()` for every track (`l. 712`), which also clears `seq_lenWindow[]` | a step roll is cut; a plain step with fill < 100 % loses its length release, so a Colloid note is not ended by its window (B1 rows 2 and 4) |
| S4 | `seq_menuOverride[]` cleared | an R12 menu edit stops holding |
| S5 | `seq_rampBreakAll()` | running automation ramps end |
| S6 | `seq_playbackEpoch++`, `seq_sceneEpoch++`, `seq_liveRecResetMarks()` | a new LiveRecord take starts; echo marks expire; `midiNoteMode_service()` (`MidiNoteModes.c:571`) re-asserts the paraphonic state |
| S7 | `seq_realignActivePatternToMasterClock()` (`l. 4941`) | cursors and DDA phase are re-derived from the master clock. The random mode draws a new step (`seq_stepIndexForMode()`, `l. 4931`). A track that was off the grid jumps. The FX sequence is realigned and the Effect track overlays are reset (`seq_requestEffectOverlayReset()`) (B1 row 5) |
| S8 | `seq_markTrackChange(0x7F)` | every Colloid releases at its first step in the new Scene unless that step triggers or drains `sus` = on (S096 P3 C1; B1 row 3) |
| S9 | `voiceControl_noteOff(0xFF)` (`MidiVoiceControl.c:402`) | drops every trigger that TIM3 has queued but the render pass has not yet drained. A step that fired just before the switch is lost; this is why the result depends on when the switch happens |
| S10 | `midiOut_trackNoteOff(0xFF)` | external MIDI notes are cut |
| S11 | `seq_clearSceneAutomationDirty()` | a later transport stop no longer restores the Scene targets that the old Scene's automation wrote |
| S12 | `seq_sendProgChg()` | MIDI out only (not heard internally) |

**In `preset_startDrumsetApply()`:**

| # | Action | Effect between identical Scenes |
|---|--------|---------------------------------|
| P1 | `instrumentManager_captureLfoPhases()`, then `instrumentManager_clearAllRuntimeModulationTargets()` (`InstrumentManager.c:1918`) | for all six sources: Morph-LFO and Effect-LFO contributions (`presetMorph_clearLfoSource()`, `effects_clearLfoSource()`), slot-decimation and Scene-target LFO bindings, and every velocity binding are detached, and each target falls back to its baseline. They are rebound only after all six slots have committed (`preset_startSceneModulationRebind()`). That takes until each ringing voice triggers, or up to `DRUMSET_APPLY_FORCE_TICKS` (1,000 passes, about 1 s). **Correction (S098 schedule):** the descriptor LFO adapters (LFO → instrument parameter, `lfo_descriptor_targets[][]`) are *not* cleared here. They keep modulating until the rebind reinstalls them |
| P2 | `preset_applySceneSettings()` (`l. 1535`) clears the FX-send, audio-out and Morph step overrides and the slot 6/track 7 decay override, then calls `presetMorph_rebuildScene()` | the current step's Scene-target automation stops acting until the next automated step |
| P3 | `effects_activateScene()` (`EffectsManager.c:2085`) | a same-type Effect keeps its tails, but the FX sequence step is invalidated (`seq_step_valid = 0`), the Effect automation overlays are reset and every parameter is rewritten (`force_all`). The comment on `effects_rebindSourceScene()` records this as audible |
| P4 | per slot: `preset_resetAndApplyKitVoiceImage()` (`l. 1849`) → `instrumentManager_resetRuntimeSlotFromScene()` (`InstrumentManager.c:1997`): a `memset` of the whole runtime slot, then the type's init. This runs when the slot is quiet (`instrumentManager_ampEnvelopeQuiet()`, `l. 1869`), is forced at the voice's next trigger (`preset_applyDeferredSceneSlotForTrigger()` from `voiceControl_prepareTrigger()`), or is forced after 1,000 passes | a voice still ringing is wiped just before its next trigger: the envelope restarts from 0, filter states are cleared, `fre`/`itp` carrier phases are reset, and an `itp` crossfade is lost. **Colloid:** the glide start (`pitchCur`), the gate, `sus`, and intervals held by MIDI are lost. **Tetrakt:** `Tetrakt_isQuiet()` (`TetraktVoice.c:812`) ignores the S1 tail (SD10), and the init clears the voice unit, so the space tail is always cut, even when the layers are quiet. **Alkaline:** the slide latch is lost |
| P5 | `instrumentManager_restoreLfoPhaseIfNeeded()` after the reset | an LFO set to `scn` retrigger restarts |

### 2.3 The B1 report explained

| Symptom | Causes |
|---------|--------|
| Steps out of place | S2, S7 |
| Notes truncated | S3, S8, S9, P4 |
| Notes re-appearing | S1 (once-mode restart), S2 (conditions) |
| Pumping or tone shifts (expected, not reported) | P1, P2, P3 |

### 2.4 The inaudible switch already in the tree

**`seq_switchToBackgroundScene()` (`sequencer.c:2621`):**

- Under PRIMASK, points the active, pending and every per-track Scene at 16,
  and re-snapshots the looper.
- Keeps step positions, DDA accumulators, rolls, conditions, epochs and looper
  anchors.
- Cancels pending Scene changes and refreshes the effective-parameter cache.
- Sends no program change, no note-off and no LED notify, and runs no drumset
  apply.

**`effects_rebindSourceScene()` (`EffectsManager.c:2149`)** retargets only
the Scene whose Effect record the running Effect reads.

The switch is inaudible because Scene 17 is built as an exact copy
(`preset_buildBackgroundScene()`, `preset_copyBackgroundTrack()`,
`preset_copyBackgroundSlot()`; S082 D4). T1 widens "exact copy" to "identical
content", and decides it per track.

---

## 3. Is a per-voice handoff struct needed? (Colloid, Tetrakt)

Where the state that makes up a voice's sound lives:

| State | Storage | Indexed by | Kept by a switch that only changes which Scene is read? |
|-------|---------|------------|------------------------------------------------|
| Voice runtime: envelopes, filters, oscillator phases. Colloid: `gateLeft`, `susBreak`, `midiHold`, `changeArm`/`susSeen`, `pitchCur`, `note`, `interval[]`, the noise (`ColloidVoice.h:335`). Tetrakt: four `TktLayer`, S1 `line`/`wi`/feedback, `bindGen` (`TetraktVoice.h:140`). Alkaline: slide and gate | `runtime_slots[slot]` (`InstrumentManager.c`) | slot | yes |
| LFO nodes, descriptor adapters, the velocity modulation graph | runtime slot, `lfo_descriptor_targets[][]`, `velocityModulators[]` | slot | yes |
| Automation holds; `audio_out_step_override[]`, `fx_send_step_override[]` (`presetManager.c`), `morph_step_override[]` (`presetMorphEngine.c`), the slot 6/track 7 decay override | Preset statics | slot | yes |
| Sequencer: cursor, DDA, play state, counters, rolls, length windows, menu override, ramps, looper | `sequencer.c` statics | track | yes |
| MIDI note modes: held keys, paraphonic voice | `MidiNoteModes.c` pools | group / slot / channel | yes |
| Effect runtime, FX sequence clock, Effect overlays | `effects_state` | (one) | yes, with `effects_rebindSourceScene()` |
| **`morph_interpolation[]`** | `scenes[n].kit.instruments[s].parameter_images` | **Scene** | **no.** It is derived, not saved, and holds the *active* Scene's resolved amount, including a step override and the LFO Morph layer (`presetMorph_getResolvedVoiceAmount()`, `presetMorphEngine.c:839`). `seq_restoreAutomatedParameters()` reads the played Scene's image at every resetting trigger |
| **Morph worker target** | `morph_worker.scene_index` (`presetMorphEngine.c:348`) | **Scene** | **no.** After the switch, `presetMorph_writeRuntimeBaseEx()` (`l. 493`) refuses writes for a Scene that is not active, so a pass already in progress (for example the last pass of a Morph sweep) would never reach the runtime |

The only stored state indexed by Scene is `scenes[]` and `pat_regions[]`. This
was checked: the only other `[SCENE_COUNT]` array is `fs_pattern_generation`,
which is AutoSave bookkeeping.

**Conclusion:**

- **No handoff struct is needed.** For each identical slot, the switch:
  - copies `morph_interpolation[]` from the Scene the track plays to the
    incoming Scene (64 B per slot, foreground);
  - retargets the Morph worker to the incoming Scene, keeping its pass for the
    identical slots.
- **Colloid and Tetrakt need nothing of their own.** Their voicing stays in
  the slot when the slot is not reset. The Colloid change decision (S8) is not
  armed for an identical track. When no track changes, the MIDI note-mode
  epoch does not move, so nothing is re-asserted.
- **When a handoff would be needed:** a *non-identical* change that keeps a
  same-type voice playing, for example keeping a held Colloid note or a
  Tetrakt tail while the new parameters take over (a legato Scene change). That
  is Q8, not in S098.

---

## 4. T1 design: one switch procedure, decided per track (Q2)

### 4.1 What is compared

`preset_sceneSwitchKeepMask(n, &effect_same)` (new, Preset, foreground) runs
before any state moves. Bit `t` of the result is set when track `t` and its
voice slot are identical in Scene `n` and in the Scene the track plays now
(`seq_getTrackPlayedScene(t)`). Tracks 5 and 6 share slot 5, so they are kept
or changed together.

**Per track** (the fields `preset_copyBackgroundTrack()` copies, `l. 3408`):

- every step's block bytes and trigger bit, read with `pat_rawReadBlock()`
- `track_length`, `track_scale`, `track_shuffle`, `track_play_mode`
- the `track_loop_exclude` and `track_auto_smooth` bits
- `midi_channel[t]`, `midi_note[t]`
- `track_morph_length[t]`, `track_morph_scale[t]`, `track_morph_shuffle[t]`

**Per slot** (the fields `preset_copyBackgroundSlot()` copies, `l. 3482`):

- `kit.instruments[s].type`, `instrument_parameters[]`,
  `morph_instrument_parameters[]` (but **not** `morph_interpolation[]`, §3)
- `voice_morph_amount[s]`, `audio_out[s]`, `fx_send_amount[s]`,
  `fx_send_morph[s]`, `fader_setting[s]`
- for slot 5 only: `kit.settings.slot6_track7_*`

**Effect** (`effect_same`, compared against the active Scene, or Scene 17):

- the Effect record, including the FX sequence steps
- `effect_morph_amount`

**Not compared:**

- `morph_amount`: the PERF bulk-set mirror; runtime Morph uses the per-voice
  amounts.
- `bus_comp[]`: read live from the active Scene by `BusCompressor.c`, with no
  state to keep.
- `chain_condition[]`, `chain_next[]`: they decide what plays next, not what
  sounds now. Scene 17 drops them too (D9b).
- `pattern_change_bar`, `pattern_next`.
- names (HCNAMES).
- `morph_interpolation[]` (derived, §3).

**Design rules:**

- **One field list.** The compared fields and the Scene 17 copy fields come
  from shared X-macro lists in `presetManager.c`. `sizeof` guards fire when a
  field is added to `scene_settings_t`, `kit_settings_t` or
  `pat_scene_region_t`.
- **Compare content, not pool layout.** Two identical Patterns can store their
  blocks at different pool offsets, so the test compares the bytes read through
  `pat_rawReadBlock()`.
- **Cost.** At most 7 × 128 step reads on each side, most of them empty, once
  per executed change, in the foreground. The DEV `'x'` trace record measures
  it; it must stay well under 1 ms.
- **No race.** Scene data is written only by the foreground; TIM3 only reads
  it.

### 4.2 The switch

**Sequencer side: `seq_selectActivePattern(n, keep_mask, effect_same)`.**

Every track:

- moves to `n`;
- refreshes the looper snapshot.

The Scene as a whole:

- `seq_previousScene` is set;
- the chain counters restart;
- the program change and the LED notify are sent;
- Scene 17 is released when it is left.

Each **changed** track gets today's per-track change:

- play state, condition counters, roll and length window, menu override, ramp
  and echo mark are reset;
- the cursor and DDA are realigned to the master clock;
- the Colloid change mark is set;
- its queued triggers and MIDI note are dropped;
- its Scene-target dirty bits are cleared.

When any track changes, the epochs move (new LiveRecord take, MIDI note-mode
re-assert) and the Effect overlays reset.

When the Effect changes, the FX sequence is realigned.

A **kept** track keeps all of S1–S11.

**Preset side: `preset_startSceneSwitchApply(keep_mask, effect_same)`.**

For each changed slot:

- the LFO phase is captured;
- its step overrides are cleared;
- its Morph pass is requested;
- the drumset worker commits it at its quiet point or trigger, as today.

The modulation graph is cleared and rebound only where it touches a changed
slot (`instrumentManager_prepareModulationForSlotChange()`):

- the changed sources themselves;
- kept sources whose binding targets a changed slot;
- with a changed Effect, the Effect and `fxm` bindings.

Kept slots are untouched.

The Effect is rebound when identical; otherwise it is activated as today. The
PERF mirrors are always synced.

**Before the switch: `preset_prepareSceneSwitch(n, keep_mask)`.** For each kept
slot, it copies `morph_interpolation[]` into `n`. It also retargets the Morph
worker, removing the changed slots from its current pass.

### 4.3 What it covers

| Change | Covered |
|--------|---------|
| PERF press, MIDI PC, deferred (`scd`) | yes: they share the executor |
| Queued (`qac`) | yes: the mute and Morph parts run first, as today, and the test runs after them |
| Chain-fired to another Scene | yes |
| Chain to the Scene already playing | unchanged (`seq_chainRepeatSelf()`) |
| From Scene 17 to a resident Scene | yes: this is how T1 and T2 fit together (§5.3) |
| Per-track Scene assignment (`seq_assignTracksToScene()`, `l. 2198`) | unchanged: it already changes only the assigned tracks |
| `pcr` on (Q1) | the restart runs as today, after the switch |

### 4.4 Consequences

On a switch, each identical track:

- keeps its LFO phase: `scn` retrigger does not apply (Q11);
- keeps a finished once-mode state;
- keeps its random-mode position;
- keeps its condition counts.

For the switch as a whole:

- the program change is still sent, and identical tracks' external notes are
  not cut (Q10);
- the LiveRecord take carries on into the new Scene when no track changes, as
  for the Scene 17 switch (D35).

**Re-pressing the playing Scene** (identical by definition) is silent unless
`pcr` is on.

**The PERF double-click realign** keeps working:

- with `PAR_FOLLOW` on it already calls `seq_realignActivePatternToMasterClock()`;
- with `PAR_FOLLOW` off its documented "+ realign" now gets the same explicit
  call, because the switch no longer realigns identical tracks.

---

## 5. T2: Scene and Pattern Loads through Scene 17

### 5.1 Today

**The Load writes in place.** A Scene Load (`preset_loadSceneForScenes()`,
`presetManager.c:3218`; Menu OK at `menu.c:16774`) and a Pattern Load
(`preset_loadPatternForScenes()`, `l. 4488`; Menu OK at `menu.c:16814`) write
their destination Scenes in place, including a Scene that is playing. The
Load calls `pat_initScene()` and then reads the file (S097 P3 §9.2), so while
it runs:

- the playing Pattern holds the defaults until the header extension has been
  read;
- the Pattern content plays partly empty during the read.

**The S097 P3 stopgap.** `filesystem_patternRealignPlayingTracks()`
(`filesystem.c:13332`) puts the tracks back on the master clock afterwards. It
is called at:

- Scene Load phase 52 (`l. 14443`);
- Pattern Load phase 14, on success (`l. 16596`) and on error (`l. 16625`);
- Bank Load phase 17 (`l. 15652`).

**Two other gaps:**

- **No availability session.** `bank_beginLoadAvailability()`
  (`BankData.c:294`) is called only for progressive Bank Loads. A MIDI PC, a
  queued change or a chain change during a Scene Load can therefore enter a
  half-written destination.
- **The transport is not held.** Neither panel PLAY nor MIDI Start is held
  during a Load.

### 5.2 The Bank path today

`preset_tickBackgroundBankLoad()` (`presetManager.c:3874`):

| Phase | Work | Bank-only? |
|-------|------|------------|
| 1 | `preset_backgroundScene17Needed(act \| empty)` (`l. 3674`) | no |
| 2 | drops the copy/clear queue (D12), AutoSave yield, waits for an idle card and pattern snapshot; a Replace also discards the dirty mask | only the drop and the discard |
| 3 | the Scene 17 build and switch, or a rebuild and move | no |
| 4 | posts `preset_loadBank()` | **yes** |
| 5 | waits for `pm_status` idle | no |
| 6 | the MIDI-origin preview | yes |

### 5.3 Design

**The Load kind.** `pm_bg_kind` is new (1 B): Bank, Scene or Pattern.
`pm_request_type` cannot carry the kind, because `preset_loadName()` rewrites
it while the user browses.

**The request.** `preset_requestBackgroundSceneLoad(slot, mask, kind)` is new.
The Menu Scene and Pattern Load OK call it instead of the direct requests. It
refuses a missing slot (`filesystem_sceneSlotExists()` /
`filesystem_patternSlotExists()`), so Scene 17 is never built for nothing.

**The phases for a Scene or Pattern Load:**

| Phase | What it does |
|-------|--------------|
| 1 | decide whether Scene 17 is needed; if not (transport stopped, or no played Scene in the mask), go straight to phase 4 with no quiesce |
| 2 | AutoSave yield and the waits; copy/clear is **waited for, not dropped** (Q4); the dirty mask is never discarded |
| 3 | unchanged |
| 4 | posts `preset_loadSceneForScenes()` or `preset_loadPatternForScenes()`; while running, starts an availability session for the destinations (Q5) |
| 5 | as for a Bank Load |

**What stays Bank-only.** The predicates `preset_backgroundBankLoadRunning()`,
`…Locked()` and `…Unlocked()` stay Bank-only, so the panel lock (R7), the MIDI
filter and the chain gate do not change for Scene Loads (Q6). The MIDI CC0
result byte is written only for a Bank Load.

**Completion.** `on_scene_load_complete()` and `on_pattern_load_complete()`
end the availability session. The Menu completion is unchanged:
`preset_startLoadApply()` finds the active Scene is 16 and applies nothing.

**After the Load,** playback stays on Scene 17 until the user changes Scene.
With P1, the switch back changes only the tracks whose content changed.

### 5.4 Consequences

- **No AutoSave while on Scene 17.** AutoSave drains are suspended while
  Scene 17 exists (`FILESYSTEM_SPEC.md` l. 1288; `filesystem.c` about
  l. 29165): kept by Q7, recorded against B2. The T3 LED makes the state
  visible.
- **A track that does not fit** in `pat_background_region` is left empty on
  Scene 17 (D3).
- **Edits made on Scene 17** go to Scene 17. Saves use its home Scene (D20).
- **The copy/clear "reload Scene" job** (`clearOps.c:707`) keeps the direct
  `preset_loadSceneForScenes()` call; the realign backstop covers it (Q3).

### 5.5 The stopgap realign (Q3: kept as a backstop)

It does nothing while Scene 17 carries the tracks. It still acts in two cases:

- a Load posted while stopped, after which the transport is started before the
  Pattern has been read;
- the copy/clear reload job.

Only its comment changes.

### 5.6 T3: the Scene 17 LED

`menu_syncQueuedSceneLed()` (`menu.c:10531`) owns fast-blink slot
`LED_FAST_SLOT_BANK_LOAD` on `LED_MODE4` and re-derives it every
`buttonHandler_tick()`.

- **The new condition:** `scene_backgroundExists()`, in every mode, transport
  running or stopped. Before, the LED flashed only during an unlocked Bank
  Load outside Load/Save mode; that case is kept.
- **No repaint interferes.** The fast layer sits above the base and blink
  states that `led_setMode2()` rewrites.

---

## 6. Decisions (user, 2026-11-18)

| # | Question | Decision |
|---|----------|----------|
| Q1 | With `pcr` on, does a switch to an identical Scene still restart? | **Yes.** The restart (`seq_resetToPatternStart()`) runs after the switch; identical slots get no apply. |
| Q2 | Identical and changed tracks in one switch | **User:** "it shouldn't make a difference for how the switching runs if tracks are identical or not." One procedure, decided per track (§4). |
| Q3 | The S097 P3 realign | **Kept as a backstop**, comment updated. |
| Q4 | Copy/clear in phase 2 for Scene/Pattern Loads | **Wait** (bounded by `PM_BG_WAIT_LIMIT_MS`), do not drop. |
| Q5 | Availability session for running Scene/Pattern Loads | **Yes**, through Scene 17 or direct. |
| Q6 | Panel unlock during a Scene/Pattern Load | **No.** The Load-page hold stays; the Bank-only predicates stay Bank-only. |
| Q7 | AutoSave suspended while Scene 17 exists | **Kept**; a B2 item. |
| Q8 | Legato across non-identical changes (handoff) | **Not in S098.** |
| Q9 | Kit and Instrument Loads into the playing Scene | **Unchanged**, not through Scene 17. |
| Q10 | MIDI on a switch | **Program change sent; note-offs only for changed tracks.** |
| Q11 | `scn` LFO retrigger on an identical track | **No restart.** |

---

## 7. Phases

### P1 — T1

| File | Change |
|------|--------|
| `Core/Sequencer/sequencer.c/.h` | `seq_selectActivePattern()` takes `keep_mask`, `effect_same`; `seq_resetTrackForSceneChange()`, `seq_realignTrackPosition()`, `seq_clearSceneAutomationDirtyFor()` |
| `Core/Bank/Scene/Preset/presetManager.c/.h` | the shared field lists and the identity test; `preset_prepareSceneSwitch()`, `preset_startSceneSwitchApply()`; the masked modulation rebind; `preset_clearSlotStepOverrides()`; the `'x'` trace record |
| `Core/DSP/Instruments/InstrumentManager.c/.h` | `instrumentManager_prepareModulationForSlotChange()` |
| `Core/Bank/Scene/Preset/presetMorphEngine.c/.h` | `presetMorph_retargetScene()` |
| `Core/Menu/menu.c` | the executor |
| `Core/Hardware/frontPanel/buttonHandler.c` | the explicit realign for a `PAR_FOLLOW`-off double-click |
| `Core/Bank/Scene/AutosaveTrace.h`, `tools/diagnostic_tools/decode_devlogs.py` | trace stage `'x'` |
| `tools/dsp_test/` | host test `scene_identity` with a break proof |

### P2 — T2 and T3

| File | Change |
|------|--------|
| `Core/Bank/Scene/Preset/presetManager.c/.h` | `pm_bg_kind`, `preset_requestBackgroundSceneLoad()`, phases 0, 1, 2 and 4, `preset_bgEnd()`, the two completion callbacks |
| `Core/Menu/menu.c` | the Scene and Pattern Load OK; the LED owner |
| `Core/Hardware/SD/filesystem.c` | the comment on the realign backstop |

### P3 (optional)

A `pattrace.bin` decoder (B1 tooling note; host only).

---

## 8. Gates and device checks

**Every phase:**

- `make all && make img`.
- The loop gate against a baseline linked from the tree at the start of P1.
  The baseline is kept in `tools/dsp_test/baselines/S098/`, not in `build/`
  (MEMORY rule). Pass: 86/86 loops exact, 77 oscillator and 58 mixer hot loops
  equal, the same hot call set, all 21 ISR and render bodies identical.
- The host tests (`make -C tools/dsp_test all`).
- `link_budget.py`: report headroom.

**P1 device matrix.** Use identical copies (Scene 1 → Scene 9, then back), with
the transport running. Switch at random points: on a step, mid-step, and in
quick succession. Add one feature at a time:

1. plain drums;
2. `1:2` and `fil` conditions;
3. `rnd` and `onc` play modes;
4. a step roll;
5. a fill < 100 % step on the Colloid;
6. a Colloid with `sus` on and `slw` glide, with MIDI paraphonic keys held;
7. a Tetrakt with S1 space;
8. an LFO with `scn` retrigger, and an LFO on Morph;
9. step automation of the FX send, and a ramp;
10. the looper engaged;
11. an FX sequence running.

Then change **one track** in Scene 9 (a step, or one Instrument parameter):

- only that track and its voice change at the switch, as today;
- every other track is unaffected.

Change the Effect in Scene 9: the Effect changes as today, and the tracks
continue.

Re-press the playing Scene: silent (`pcr` off).

PERF double-click with `PAR_FOLLOW` off still realigns.

Copy the card to `SD_CARD_S098_P1`; the `'x'` records give the keep mask and
compare time of every switch.

**P2 device matrix:**

1. Scene Load into the playing Scene while running: no timing change, no
   partly empty Pattern, Scene 17 plays, and the Load/Save LED flashes in
   every mode. A manual switch back afterwards changes only the tracks the Load
   changed.
2. A Load into the Scene of a per-track-played track: only that track moves to
   Scene 17.
3. A Pattern Load into the playing Scene: same as 1.
4. A Load into Scenes that do not play: no change, no Scene 17, no flashing.
5. A stopped Load: unchanged from today.
6. A MIDI PC to a destination during a running Load: refused (Q5).
7. A Bank Load through Scene 17: the LED flashes now also in Load/Save mode
   and stops after the switch away.
8. AutoSave resumes after leaving Scene 17.

Copy the card to `SD_CARD_S098_P2`.

---

## 9. RAM, CPU, flash

**RAM: +3 B SRAM1 (.bss). Needs sign-off.**

| Bytes | Symbol | Owner | Region | Why |
|------:|--------|-------|--------|-----|
| 1 | `drumset_apply_rebind_mask` | `presetManager.c` | SRAM1 | the sources the Scene worker rebinds when its last changed slot commits (today: always all six) |
| 1 | `instrument_apply_rebind_mask` | `presetManager.c` | SRAM1 | lets the shared rebind cursor skip sources whose bindings the switch did not touch |
| 1 | `pm_bg_kind` | `presetManager.c` | SRAM1 | the Load kind of the background machine (Bank, Scene, Pattern) |

**CPU:** no render-path or DSP change. The foreground work is one identity
test per executed change, and the existing Scene 17 build per Load start. The
compare uses two `PAT_RAW_BLOCK_MAX` (136 B) stack buffers.

**Flash:** small, measured against the headroom of the P1 baseline. The last
default-build figure, S097 P3 §8.4–§9.4, is about 37.4 KB.

---

## 10. Statements this work makes out of date (for the user to act on)

The plan contains no edits to `MEMORY.md` or `knowledge_files/`.

- **`MEMORY.md` Scene/Bank:** add the switch rule, roughly "a whole-Scene
  change is decided per track: identical tracks and slots only change which
  Scene is read; changed ones get the per-track change; the Effect is rebound
  when identical".
- **`MEMORY.md` "FX sequence = master clock":** a Scene change with an
  identical Effect keeps the FX clock (rebind, no realign); a changed Effect
  realigns it as before.
- **`MEMORY.md` "pat_background_region":** the Scene 17 use now also serves
  Scene and Pattern Loads.
- **`MEMORY.md`:** the Load/Save LED flashes whenever Scene 17 exists.
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - "Scene change execution (foreground)" (l. 539);
  - "Background Bank load (S082)" (l. 2031–2072): it becomes a background Load
    of three kinds.
- **`FILESYSTEM_SPEC.md`** "Background Bank Load and Scene 17" (l. 1277–1289).
- **`PATTERN_DYNAMIC_STACK.md`:** §6.6 (l. 1361) and the reset lists at
  l. 1186, 1308 and 1388.
- **`STORAGE_SRAM_MANIFEST.md`:** the three new bytes.
- **`S097_P3_FS_OPTIMIZE_INVESTIGATION.md` §10:** done by P2.
- **`DRAFT_ONGOING_BUGS.md` B1:** status.

---

## 11. Out of scope

- B2 (AutoSave speed); Q7 records how this work touches it.
- A legato change across non-identical Scenes (Q8).
- A Kit or Instrument Load through Scene 17 (Q9).
- The copy/clear reload job through Scene 17 (§5.4).
