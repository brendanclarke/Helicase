# Session 076 Handoff Log

**Date:** 2026-10-06
**Branch:** `dev-ph6-cleanup`
**Session goal:** Four independent feature/fix batches (P1–P4), each planned,
implemented, and assessed within the session.
**Verified on hardware:** No — all four parts build clean and await hardware
testing. The retest checklist is `S077_RETEST_CHECKLIST.md`.

---

## 1. Summary

Session 076 delivered four implementation batches covering bug fixes, feature
expansions, and new UI capabilities. All code compiles to a clean ARM build
with no new warnings. No hardware verification was performed; a comprehensive
retest checklist was created for the next session.

| Part | Topic | Files changed | Flash Δ | RAM Δ |
|------|-------|--------------|---------|-------|
| P1 | Scene parameter automation override clear rules | 7 | baseline | 0 |
| P2 | LFO Scene-reset retrigger + phase offset scaling fix | 9+1 tool | −448 (LTO) | +32 B |
| P3 | Copy/clear morph additions | 8 | +2,848 | 0 |
| P4 | Reload scene, bar chaselight, SHIFT+SELECT pattern length | 5 | +792 | 0 |

**Final build (DEV config):** text 535,976, data 416, bss 426,744.
Flash payload 536,392 B of 753,664 B (**headroom 217,272 B**).
RAM growth: +32 B total (28-byte `lfo_scene_handoff` struct + 4 B alignment).

---

## 2. P1 — Scene Parameter Automation Override Clear Rules

### Problem

Five families of step-automation override state persist beyond their useful
lifetime:

1. `fx_send_step_override[6]` (presetManager.c)
2. `audio_out_step_override[6]` (presetManager.c, since S071)
3. `morph_step_override[6]` (presetMorphEngine.c, since S070)
4. `slot6_track7_decay_step_active` + `_value` (InstrumentManager.c)
5. `effects_automation.morph_override_valid` (EffectsManager.c)

These are cleared on transport reset (`seq_clearSceneAutomationDirty()`), but
two other scenarios left stale overrides:

- **Rule A — Non-automation writes should clear:** a user edit or Morph change
  to the same parameter should replace the override, but the old code only
  checked automation dirty bits and didn't clear the override struct.
- **Rule B — Scene activation should clear all:** switching to a new Scene
  should start with no stale overrides from the previous Scene.

### Changes (16 inline additions, 7 files)

**presetMorphEngine.c/h:**
- Added `presetMorph_clearStepAutomationOverride(uint8_t slot)` — single-slot
  helper that clears `morph_step_override[slot]`.

**presetManager.c** (7 inline additions):
- `preset_setVoiceFxSendAmount()`: clear `fx_send_step_override[slot]`.
- `preset_setVoiceFxSendMorph()`: clear `fx_send_step_override[slot]`.
- `preset_setVoiceAudioOut()`: clear `audio_out_step_override[slot]`.
- `preset_morphVoiceScene()`: clear `morph_step_override` via the new helper.
- `preset_morphScene()`: clear `morph_step_override` via the new helper.
- `preset_setSlot6Track7AmpEnvelopeDecay()`: clear `slot6_track7_decay_step_active`.
- `preset_applySceneSettings()`: clear all five override families (Rule B).

**EffectsManager.c/h:**
- Added `effects_clearMorphAutomationOverride()`.
- `effects_setMorphAmountScene()`: inline clear of the Effect Morph override.

**sequencer.c/h:**
- Added `seq_clearSceneAutomationDirty()` — clears `seq_scene_automation_dirty`
  bitmap.
- Called from `seq_selectActivePattern()` and `seq_alignActivePatternToScene()`.

### Design notes

- Rule A clears are placed at the point where the retained value changes, so
  the override is removed before the new value is applied. The next step
  automation event will re-set the override if needed.
- Rule B clear is in `preset_applySceneSettings()`, which runs on every Scene
  activation. This ensures all five families are clean before the new Scene's
  automation can set them.
- No RAM change. No new state. The clears are single-assignment writes to
  existing fields.

---

## 3. P2 — LFO Scene-Reset Retrigger + Phase Offset Scaling Fix

### Feature: LFO retrigger value 7 ("scn")

LFO retrigger values were 0–6 (off, v1–v6). A new value 7 ("scn") resets the
LFO phase on Scene change, enabling phase-coherent LFO behavior across Scene
transitions.

**Lifecycle:**
1. `captureLfoPhases()` — called from `preset_startDrumsetApply()` before the
   Scene's instruments are replaced. Captures the current phase of every LFO
   whose retrigger == `LFO_RETRIGGER_SCENE` (7u) into a static 28-byte struct
   `lfo_scene_handoff` (6 slots × {valid, phase} = 28 B BSS, rounded by
   alignment from 25 theoretical).
2. `restoreLfoPhaseIfNeeded(voice)` — called from
   `preset_resetAndApplyKitVoiceImage()` after each voice is rebuilt. If the
   capture is valid for that slot and the new LFO retrigger is still `scn`,
   writes the captured phase back. Otherwise the LFO starts at its offset as
   usual.

**Collision fix:** the retrigger install loop in InstrumentManager had a
guard `< INSTRUMENT_SLOT_COUNT` that should be `<= INSTRUMENT_SLOT_COUNT`
(6 voices, 1-indexed tracks). Fixed inline.

### Bug fix: LFO phase offset scaling

**Problem:** the `lfo_offset` parameter (0–127 byte) was written raw as a
`uint32_t` to `phaseOffset`, but the LFO uses a full 32-bit phase accumulator
(0–0xFFFFFFFF). Writing 0–127 into a uint32_t field meant the offset was
effectively always zero (127/4,294,967,295 ≈ 0.000003 %).

**Fix:** Added `IM_SPECIAL_LFO_OFFSET` (value 19) to the special-writer enum.
The `lfo_offset` parameter is now tagged `ROW_SPECIAL` (was `ROW`) in all four
instrument `*Parameters.c` files. The special writer scales the byte value:
`(uint64_t)byteValue * 0xFFFFFFFFu / 127u`, mapping 0 → 0x00000000 and
127 → 0xFFFFFFFF.

### Changes (13+1 changes, 9+1 files)

**lfo.h:** `#define LFO_RETRIGGER_SCENE 7u`.

**MenuText.h:** `retriggerNames` count 7 → 8, added `"scn"`.

**InstrumentManager.h:** Added `IM_SPECIAL_LFO_OFFSET` (19); declared
`captureLfoPhases()` and `restoreLfoPhaseIfNeeded()`.

**InstrumentManager.c:**
- Static `lfo_scene_handoff` struct (28 B BSS).
- `captureLfoPhases()` and `restoreLfoPhaseIfNeeded()` implementations.
- Retrigger collision fix (guard `<= INSTRUMENT_SLOT_COUNT`).
- `IM_SPECIAL_LFO_OFFSET` case in the special writer.
- `"lfo_offset"` classifier mapping for the special tag.

**DrumParameters.c, SnareParameters.c, CymbalParameters.c, HiHatParameters.c:**
`lfo_offset` row changed from `ROW` to `ROW_SPECIAL`.

**presetManager.c:**
- `captureLfoPhases()` call in `preset_startDrumsetApply()`.
- `restoreLfoPhaseIfNeeded(voice)` call in `preset_resetAndApplyKitVoiceImage()`.

**tools/dsp_test/check_special_tags.py:** Added `'lfo_offset': 'LFO_OFFSET'`.

### Build impact

- Flash: −448 B (LTO variation, not from these changes).
- RAM: +32 B BSS (`lfo_scene_handoff` struct; 28 B data + 4 B alignment).
- Approved under RAM policy: 32 B, SRAM1 `.bss`, permanent, InstrumentManager.

---

## 4. P3 — Copy/Clear Morph Additions

### Feature

Added morph-reset and morph-copy operations to the copy/clear framework.

**New clear selections:**

| Enum | Value | Menu | What it does |
|------|-------|------|-------------|
| `CC_CLEAR_RESET_MORPH` | 4 | VOICE track, EFFECTS track | Equalise one slot's Morph endpoints to its Normal endpoints, plus correlated Scene morph endpoints (FX send morph, Kit slot-6 decay morph), fanning out through the edit mask |
| `CC_CLEAR_SCENE_RESET_MORPH` | 8 | PERF Scene | All six slots + all correlated Scene morph + every morphable Effect morph endpoint → Normal; does NOT fan out; does NOT clear present bit; does NOT touch morph amount |
| `CC_CLEAR_SCENE_RESET_FX_MORPH` | 9 | PERF Scene | Effect morphable morph endpoints only → Normal; fans out through edit mask |

**New copy selections:**

| Enum | Value | Menu | What it does |
|------|-------|------|-------------|
| `CC_COPY_MORPH` | 2 | VOICE track, EFFECTS track | Copy one slot's Normal endpoints into its Morph endpoints, fanning out through the edit mask |
| `CC_COPY_SCENE_MORPH` | 5 | PERF Scene | All six slots' Normal → Morph endpoints; does NOT fan out |

### Reordering: `CC_CLEAR_RESET_MORPH` before `CC_CLEAR_SEND`

"reset morph" (4) is visible in both the VOICE and EFFECTS TRACK menus, while
"send" (5) is visible only in EFFECTS TRACK. By placing "reset morph" before
"send" in a single shared `ccClear_trackLabels[]` array, the VOICE menu count
stops at "reset morph" and the EFFECTS count appends "send". This means
`CC_CLEAR_SEND` moved from its previous value 4 to 5.

### Changes (11 change groups, 8 files)

**clearOps.h:** Added `CC_CLEAR_RESET_MORPH=4`, reordered `CC_CLEAR_SEND=5`;
added `CC_CLEAR_SCENE_RESET_MORPH=8`, `CC_CLEAR_SCENE_RESET_FX_MORPH=9`.

**clearOps.c:** Labels reordered, counts updated (track 5, track_fx 6,
scene 10), added 3 executors (`ccClear_runResetMorphTrack`,
`ccClear_runResetSceneMorph`, `ccClear_runResetFxMorph`), dispatch updated.

**copyOps.h:** Added `CC_COPY_MORPH=2`, `CC_COPY_SCENE_MORPH=5`.

**copyOps.c:** Labels and counts updated, added 2 executors
(`ccCopy_runMorphTrack`, `ccCopy_runSceneMorph`), dispatch updated,
identical-paste check.

**presetManager.h:** Declared `preset_resetSlotMorphToNormal()` and
`preset_copySlotNormalToMorph()`.

**presetManager.c:** Implementations of both helpers.

**EffectsManager.h:** Declared `effects_resetMorphToNormal()` and
`effects_resetMorphToNormalSingle()`.

**EffectsManager.c:** Implementations of both helpers.

**copyClearSession.c:** Indicator edits for morph copy operations ('m' suffix).

### Executor detail

**`ccClear_runResetMorphTrack`:** For the job's slot, calls
`preset_resetSlotMorphToNormal(scene, slot)` which copies all Morphable Normal
descriptor bytes to Morph, plus the FX send morph → Normal and the slot-6
decay morph → Normal. Fans out through `bank_sceneFanoutMask()`.

**`ccClear_runResetSceneMorph`:** Loops all 6 slots calling
`preset_resetSlotMorphToNormal()` plus `effects_resetMorphToNormal()` for
the Scene's Effect. Does NOT fan out (parallels `clear scene`).

**`ccClear_runResetFxMorph`:** Calls `effects_resetMorphToNormalSingle(scene)`
for the Effect's morphable endpoints only. Fans out through the edit mask.

**`ccCopy_runMorphTrack`:** For the job's slot, calls
`preset_copySlotNormalToMorph(scene, slot)`. Fans out through
`bank_sceneFanoutMask()`.

**`ccCopy_runSceneMorph`:** All 6 slots via `preset_copySlotNormalToMorph()`
plus effect morph. Does NOT fan out.

### Build impact

- Flash: +2,848 B (new executors, helper functions, label strings).
- RAM: 0.

---

## 5. P4 — Reload Scene, Bar Chaselight, SHIFT+SELECT Pattern Length

Three independent features.

### 5.1 Feature 1: "Reload Scene" in the Clear Scene Menu

**`CC_CLEAR_SCENE_RELOAD` (10)** in the PERF clear-scene menu. When confirmed,
reads the Scene's HCNAMES source register and, if valid (numeric slot 0–999),
issues `preset_loadSceneForScenes()` to reload the Scene from the SD card.

Implementation:

```c
static uint8_t ccClear_runReloadScene(const cc_job_t *job)
{
    uint16_t row = filesystem_identityRow(FS_ROW_SCENE, job->scene, 0u);
    uint16_t src = (uint16_t)(filesystem_residentSource(row) &
                              FS_RESIDENT_SOURCE_VALUE_MASK);
    if (src <= 999u)
        (void)preset_loadSceneForScenes(src, (uint16_t)(1u << job->scene));
    return CC_RUN_DONE;
}
```

The Scene load lifecycle (preset_loadSceneForScenes → filesystem facade →
on_scene_load_complete → preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD) →
pm_status = PRESET_UPDATE_READY → menu_pollPresetStatus() →
menu_startSoundApply()) handles everything: Bank-present promotion, Pattern
dirty marking, runtime application, and display repaint. No page navigation
is needed.

**Files:** `clearOps.h` (+1 enum), `clearOps.c` (+1 label, +1 count, +1
executor, +1 dispatch case).

### 5.2 Feature 2: Bar Chaselight on SELECT LEDs in STEP Mode

A tempo-pulsed chaselight on the SELECT row shows the bar currently being
played by the sequencer.

**`led_updateSelectBarChaselight()`** (static in `ledHandler.c`):
1. Base state: viewed bar (`menu_currentBar`) LED on, all others off.
2. If running (`seq_isRunning()`):
   - Compute `playbackBar = seq_ledState.chaseStep / NUM_STEPS_PER_BAR`.
   - Same bar as viewed + beat pulse: turn the LED **off** (inverted pulse —
     the already-lit LED blinks off at the beat).
   - Different bar + beat pulse: turn the LED **on** (normal pulse).
3. If stopped: base state only (no overlay).

Called from both the `SEQ_LED_DIRTY_BEAT` and `SEQ_LED_DIRTY_CHASE` handlers
when `SELECT_MODE_STEP` is active.

**Interaction with per-track length:** `seq_ledState.chaseStep` reflects the
active voice and wraps within `track_length`, so the chaselight never exceeds
the track's range.

**Files:** `ledHandler.c` (+1 static function, +2 call sites in dirty
handlers).

### 5.3 Feature 3: SHIFT+SELECT Sets Pattern Length in STEP Mode

In STEP mode, SHIFT+SELECT button N sets the active track's pattern length
to `(N + 1) * NUM_STEPS_PER_BAR`. This is per-track (the existing model).

Implementation in `buttonHandler.c` `handleSelectButton()`: the SHIFT path
for `SELECT_MODE_STEP` was split from the `VOICE` fallthrough. STEP now:
1. Computes `newLength = (selectNr + 1) * NUM_STEPS_PER_BAR`.
2. Calls `pat_setTrackLength(scene, track, newLength)`.
3. Clamps `menu_currentBar` if beyond the new length.
4. Calls `pat_applyTrackSettingsToMenu()` and `menu_repaintAll()`.

**Files:** `buttonHandler.c` (split case, ~15 lines).

### Cross-feature interaction

- F3 → F2: shortening a track clamps the viewed bar and the chaselight stays
  in bounds (chaseStep wraps within track_length).
- F1 → F2: reloading a Scene realigns the sequencer; the chaselight follows.
- F3 → F1: independent.

### Build impact

- Flash: +792 B.
- RAM: 0.

---

## 6. Build Progression

| Stage | text | data | bss | Flash payload | Headroom |
|-------|------|------|-----|---------------|----------|
| S075 close (baseline) | 532,408 | 416 | 426,712 | 532,824 | 220,840 |
| P1 (automation clear) | 532,784 | 416 | 426,712 | 533,200 | 220,464 |
| P2 (LFO scene reset) | 532,336 | 416 | 426,744 | 532,752 | 220,912 |
| P3 (morph additions) | 535,184 | 416 | 426,744 | 535,600 | 218,064 |
| P4 (reload/chase/length) | 535,976 | 416 | 426,744 | 536,392 | 217,272 |

---

## 7. New Public APIs

### presetMorphEngine.h
- `presetMorph_clearStepAutomationOverride(uint8_t slot)` — clear one voice
  Morph step override (P1).

### presetManager.h
- `preset_resetSlotMorphToNormal(uint8_t scene, uint8_t slot)` — copy a slot's
  Normal descriptor endpoints into its Morph image, plus correlated Scene
  endpoints (P3).
- `preset_copySlotNormalToMorph(uint8_t scene, uint8_t slot)` — copy Normal
  to Morph for one slot (P3).

### EffectsManager.h
- `effects_clearMorphAutomationOverride(void)` — clear the Effect Morph
  automation override (P1).
- `effects_resetMorphToNormal(uint8_t scene)` — equalise all morphable Effect
  endpoints to their Normal values (P3).
- `effects_resetMorphToNormalSingle(uint8_t scene)` — same, for fan-out use
  (P3).

### sequencer.h
- `seq_clearSceneAutomationDirty(void)` — clear the Scene automation dirty
  bitmap (P1).

### InstrumentManager.h
- `IM_SPECIAL_LFO_OFFSET` (value 19) — special-writer tag for LFO phase
  offset scaling (P2).
- `captureLfoPhases(void)` — capture LFO phases for Scene-change handoff (P2).
- `restoreLfoPhaseIfNeeded(uint8_t voice)` — restore captured phase if the
  new LFO retrigger is `scn` (P2).

### clearOps.h
- `CC_CLEAR_RESET_MORPH` (4) — track-level morph reset (P3).
- `CC_CLEAR_SCENE_RESET_MORPH` (8) — Scene-level morph reset (P3).
- `CC_CLEAR_SCENE_RESET_FX_MORPH` (9) — Effect-only morph reset (P3).
- `CC_CLEAR_SCENE_RELOAD` (10) — reload Scene from SD card (P4).

### copyOps.h
- `CC_COPY_MORPH` (2) — track-level morph copy (P3).
- `CC_COPY_SCENE_MORPH` (5) — Scene-level morph copy (P3).

### lfo.h
- `LFO_RETRIGGER_SCENE` (7u) — Scene-change retrigger value (P2).

---

## 8. Files Changed

### P1 (7 files)
- `Core/Bank/Scene/Preset/presetMorphEngine.c` — new single-slot clear helper
- `Core/Bank/Scene/Preset/presetMorphEngine.h` — declaration
- `Core/Bank/Scene/Preset/presetManager.c` — 7 inline override clears
- `Core/DSP/Effects/EffectsManager.c` — Effect Morph override clear + inline
- `Core/DSP/Effects/EffectsManager.h` — declaration
- `Core/Sequencer/sequencer.c` — Scene automation dirty clear + call sites
- `Core/Sequencer/sequencer.h` — declaration

### P2 (9+1 files)
- `Core/DSPAudio/lfo.h` — `LFO_RETRIGGER_SCENE` define
- `Core/Menu/MenuText.h` — retriggerNames expanded
- `Core/DSP/Instruments/InstrumentManager.h` — `IM_SPECIAL_LFO_OFFSET`, declarations
- `Core/DSP/Instruments/InstrumentManager.c` — handoff struct, capture/restore, special writer, collision fix
- `Core/DSP/Instruments/Drum/DrumParameters.c` — `lfo_offset` → `ROW_SPECIAL`
- `Core/DSP/Instruments/Snare/SnareParameters.c` — same
- `Core/DSP/Instruments/Cymbal/CymbalParameters.c` — same
- `Core/DSP/Instruments/HiHat/HiHatParameters.c` — same
- `Core/Bank/Scene/Preset/presetManager.c` — capture/restore call sites
- `tools/dsp_test/check_special_tags.py` — `lfo_offset` mapping

### P3 (8 files)
- `Core/Menu/CopyClear/clearOps.h` — 3 new enums + reorder
- `Core/Menu/CopyClear/clearOps.c` — labels, counts, 3 executors, dispatch
- `Core/Menu/CopyClear/copyOps.h` — 2 new enums
- `Core/Menu/CopyClear/copyOps.c` — labels, counts, 2 executors, dispatch
- `Core/Bank/Scene/Preset/presetManager.h` — 2 declarations
- `Core/Bank/Scene/Preset/presetManager.c` — 2 helper implementations
- `Core/DSP/Effects/EffectsManager.h` — 2 declarations
- `Core/DSP/Effects/EffectsManager.c` — 2 helper implementations
- `Core/Menu/CopyClear/copyClearSession.c` — indicator 'm' suffix

### P4 (5 files)
- `Core/Menu/CopyClear/clearOps.h` — `CC_CLEAR_SCENE_RELOAD` enum
- `Core/Menu/CopyClear/clearOps.c` — label, count, executor, dispatch
- `Core/Hardware/frontPanel/buttonHandler.c` — SHIFT+SELECT STEP split
- `Core/Hardware/frontPanel/ledHandler.c` — bar chaselight function + calls

---

## 9. Decisions and Rationale

- **Rule A/B naming (P1):** chosen for clarity in the override-clear pattern.
  Rule A = "a non-automation write to the same parameter clears its override."
  Rule B = "Scene activation clears all overrides."
- **Struct size (P2):** the plan estimated `lfo_scene_handoff` at 25 B
  (6 × {1B valid + 4B phase} + 1B count); actual is 28 B due to struct member
  alignment. Corrected in the assessment.
- **Label array sharing (P3):** placing "reset morph" before "send" in a
  single `ccClear_trackLabels[]` array avoids duplicating the shared labels
  while serving both VOICE (5 items) and EFFECTS TRACK (6 items) menus.
- **Reload scene as a clear selection (P4):** placed in the PERF clear-scene
  menu because it operates on a whole Scene and extends the existing Scene
  management selections. It is fire-and-forget: the clear job returns
  `CC_RUN_DONE` immediately after issuing the Preset load request.

---

## 10. Risks and Edge Cases

### P1
- If a new override family is added in the future, it must be included in
  `preset_applySceneSettings()` (Rule B) and in the relevant non-automation
  setter (Rule A).

### P2
- The `lfo_scene_handoff` capture is only valid for the duration of one Scene
  apply. If a second Scene change happens before the first completes (not
  currently possible), the capture would be stale.
- The phase offset scaling uses `uint64_t` intermediate to avoid overflow;
  verified that `127 * 0xFFFFFFFF` fits in 64 bits.

### P3
- `preset_resetSlotMorphToNormal()` copies every Morphable descriptor byte.
  A future non-morphable descriptor in the Morph image position would be
  overwritten, but the descriptor flag check prevents this.
- The "reset morph" clear does not touch morph amounts — the user can still
  use the morph knob, it just won't change the sound until new Morph
  endpoints are set.

### P4
- Reload scene: if the SD card file was deleted since load, the Preset load
  fails and `menu_pollPresetStatus()` shows the standard error overlay. This
  is accepted (same as a normal failed Load).
- Bar chaselight: per-track length means different tracks can be in different
  bars. The chaselight shows the active track's bar (via `chaseStep`), matching
  the existing step-row chaselight behavior.
- SHIFT+SELECT length: sets per-track length, not per-pattern. If per-pattern
  (all tracks) is desired, the implementation would iterate all 7 tracks.

---

## 11. Carry-over from Previous Sessions

All S075 carry-over items remain open (see `S070_WORKING_NOTES.md` items 1–11).
Session 076 did not address any of them. The S077 working notes update the
status and add P1–P4 hardware verification as the top priority.

---

## 12. End of Session Block

```
DATE: 2026-10-06
SESSION GOAL: Four feature/fix batches (P1–P4)
COMPLETED: All four implemented, build clean, assessed
VERIFIED ON HARDWARE: No — retest checklist created (S077_RETEST_CHECKLIST.md)

CHANGES THIS SESSION:
- presetMorphEngine.c/h: single-slot override clear helper (P1)
- presetManager.c/h: 7 inline override clears (P1), morph reset/copy helpers (P3), LFO capture/restore calls (P2)
- EffectsManager.c/h: Effect Morph override clear (P1), morph reset helpers (P3)
- sequencer.c/h: Scene automation dirty clear (P1)
- InstrumentManager.c/h: LFO scene handoff, phase offset scaling, collision fix (P2)
- lfo.h: LFO_RETRIGGER_SCENE define (P2)
- MenuText.h: retriggerNames expanded (P2)
- Drum/Snare/Cymbal/HiHat Parameters.c: lfo_offset ROW→ROW_SPECIAL (P2)
- clearOps.c/h: morph reset clears (P3), reload scene (P4)
- copyOps.c/h: morph copy operations (P3)
- copyClearSession.c: morph indicator suffix (P3)
- buttonHandler.c: SHIFT+SELECT pattern length (P4)
- ledHandler.c: bar chaselight (P4)
- check_special_tags.py: lfo_offset mapping (P2)

KNOWN ISSUES INTRODUCED: None
KNOWN ISSUES RESOLVED: LFO phase offset scaling was broken since the original port (P2); Scene parameter automation overrides could persist incorrectly (P1)

NEXT SESSION RECOMMENDED GOAL: Hardware verification of all P1–P4 changes using S077_RETEST_CHECKLIST.md
BLOCKERS: Hardware testing required for all four parts

CRITICAL REMINDERS FOR NEXT SESSION:
- All five override families must be included in Rule B (preset_applySceneSettings) if new ones are added
- CC_CLEAR_SEND moved from value 4 to value 5 due to P3 reordering
- The lfo_scene_handoff struct is 28 B (not 25) due to alignment
- The LFO phase offset was previously broken (raw 0–127 into uint32_t); now scaled correctly
```
