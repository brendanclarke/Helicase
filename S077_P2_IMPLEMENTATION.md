# S077 P2 — Per-Track Scene Playback: Implementation Schedule

Companion to `S077_P2_PER_TRACK_PLAYBACK.md`.  Every code change required to
implement the per-track scene playback feature, cited by file, line, and
operation (ADD / MODIFY / REMOVE).  Each entry carries a comment-block
description suitable for placement in both `.c` and `.h` files.

---

## Phase 1: Sequencer per-track read path

### 1.1  New state: `seq_perTrackActive` flag

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 183 (after `seq_perTrackPattern[NUM_TRACKS]`)  
**Operation:** ADD

```
/*
 * Per-track playback override flag.
 *
 * What:       nonzero when any entry in seq_perTrackPattern[] differs from
 *             seq_activePattern. Why: gates the fast path — when zero, all
 *             existing code paths that reference seq_activePattern are
 *             correct as-is and no per-track branching is needed. Recomputed
 *             by seq_recomputePerTrackActive() whenever seq_perTrackPattern[]
 *             changes. Inputs: seq_perTrackPattern[], seq_activePattern.
 *             Output: 0 or 1.
 * RAM:        +1 byte SRAM1. Owner: Sequencer. Lifetime: static.
 * Affiliates: seq_setTrackPlayedScene(), seq_clearPerTrackOverrides(),
 *             seq_selectActivePattern(), seq_alignActivePatternToScene(),
 *             seq_handleMasterBoundary().
 */
uint8_t seq_perTrackActive = 0;
```

**File:** `Core/Sequencer/sequencer.h`  
**Line:** 51 (after `extern uint8_t seq_perTrackPattern[NUM_TRACKS]`)  
**Operation:** ADD

```
/*
 * Nonzero when any track plays from a Scene other than seq_activePattern.
 *
 * Inputs: derived from seq_perTrackPattern[] vs seq_activePattern.
 * Output: foreground code reads this to gate per-track UI/record behaviour.
 * Affiliates: seq_setTrackPlayedScene(), seq_clearPerTrackOverrides().
 */
extern uint8_t seq_perTrackActive;
```

---

### 1.2  Helper: `seq_recomputePerTrackActive()`

**File:** `Core/Sequencer/sequencer.c`  
**Line:** ~185 (after the new `seq_perTrackActive` declaration)  
**Operation:** ADD (static helper)

```
/*
 * Recompute the seq_perTrackActive convenience flag.
 *
 * What:       walks seq_perTrackPattern[] and sets seq_perTrackActive to 1 if
 *             any entry differs from seq_activePattern, 0 otherwise.
 * Why:        centralises the derivation so every mutation site calls one
 *             function rather than duplicating the scan.
 * Inputs:     seq_perTrackPattern[NUM_TRACKS], seq_activePattern.
 * Output:     seq_perTrackActive (0 or 1).
 * Caller:     seq_setTrackPlayedScene(), seq_clearPerTrackOverrides(),
 *             seq_selectActivePattern(), seq_alignActivePatternToScene(),
 *             seq_handleMasterBoundary().
 */
static void seq_recomputePerTrackActive(void)
```

---

### 1.3  New API: `seq_setTrackPlayedScene()`

**File:** `Core/Sequencer/sequencer.c`  
**Line:** ~540 (before `seq_triggerVoice`, in the public API region)  
**Operation:** ADD

```
/*
 * Assign one track to play from a specific Scene.
 *
 * What:       sets seq_perTrackPattern[track] to scene_index and recalculates
 *             seq_perTrackActive. Tracks 5 and 6 (HiHat choke pair, indices 5
 *             and 6 in the seven-track array) always switch together.
 * Why:        the PERF hold-VOICE+press-SEQ gesture calls this to override one
 *             track's playback Scene without changing the global active Scene.
 * Inputs:     track 0..6, validated scene_index (caller must validate
 *             presence). Output: seq_perTrackPattern[track] updated.
 * ISR safety: foreground-only write to a single byte read by TIM3; ARM
 *             Cortex-M7 byte stores are atomic.
 * Affiliates: seq_clearPerTrackOverrides(), seq_recomputePerTrackActive(),
 *             preset_startSingleVoiceApply() (Phase 2, called after this).
 */
void seq_setTrackPlayedScene(uint8_t track, uint8_t scene_index)
```

**File:** `Core/Sequencer/sequencer.h`  
**Line:** after `seq_alignActivePatternToScene` declaration  
**Operation:** ADD

```
/*
 * Set one track's played Scene for per-track playback.
 *
 * Inputs: track 0..6, valid Scene index. Output: seq_perTrackPattern[track]
 * updated, seq_perTrackActive recomputed. Tracks 5+6 switch together.
 * Client: buttonHandler PERF hold-VOICE+press-SEQ gesture.
 */
void seq_setTrackPlayedScene(uint8_t track, uint8_t scene_index);
```

---

### 1.4  New API: `seq_realignTrackToMasterClock()`

**File:** `Core/Sequencer/sequencer.c`  
**Line:** ~1265 (after `seq_realignActivePatternToMasterClock`)  
**Operation:** ADD

```
/*
 * Realign a single track's step position against the master clock.
 *
 * What:       derives one track's seq_stepIndex from seq_masterStepClock using
 *             that track's own played Scene's track_length from PatternData's
 *             pat_sceneRegion(seq_perTrackPattern[track]).
 * Why:        when a per-track Scene assignment changes a track's loop length,
 *             the track must be repositioned against the master clock to avoid
 *             phase discontinuities. Also used for the double-click single-
 *             track realign gesture.
 * Inputs:     track 0..6, seq_masterStepClock (global).
 * Output:     seq_stepIndex[track] and seq_lastMasterStep[track] updated.
 *             Chase LED dirty bit set if track is the active voice.
 * Affiliates: pat_sceneRegion(), seq_realignActivePatternToMasterClock()
 *             (the all-track variant that already uses the same formula).
 */
void seq_realignTrackToMasterClock(uint8_t track)
```

**File:** `Core/Sequencer/sequencer.h`  
**Line:** after `seq_realignActivePatternToMasterClock` declaration  
**Operation:** ADD

```
/*
 * Realign one track's step cursor from the master clock using its own played
 * Scene's track length.
 *
 * Input: track 0..6. Output: seq_stepIndex[track] repositioned.
 * Client: per-track assignment gesture, double-click single-track realign.
 */
void seq_realignTrackToMasterClock(uint8_t track);
```

---

### 1.5  New API: `seq_clearPerTrackOverrides()`

**File:** `Core/Sequencer/sequencer.c`  
**Line:** after `seq_setTrackPlayedScene`  
**Operation:** ADD

```
/*
 * Reset all per-track played-Scene entries to the active Scene.
 *
 * What:       sets every seq_perTrackPattern[] entry to seq_activePattern and
 *             clears seq_perTrackActive to 0.
 * Why:        any scene-level playback change (PERF SEQ press without VOICE
 *             hold) coalesces all tracks back to one Scene (plan §4.3, Q2).
 * Inputs:     seq_activePattern. Output: all entries equal activePattern.
 * Affiliates: seq_selectActivePattern() calls this implicitly through its
 *             existing loop; this function is the explicit public API for the
 *             button handler coalesce gesture.
 */
void seq_clearPerTrackOverrides(void)
```

**File:** `Core/Sequencer/sequencer.h`  
**Line:** after `seq_setTrackPlayedScene` declaration  
**Operation:** ADD

```
/*
 * Coalesce all tracks back to the active Scene.
 *
 * Output: all seq_perTrackPattern[] == seq_activePattern; seq_perTrackActive = 0.
 * Client: scene-level SEQ press coalesce (PAR_FOLLOW on single-click,
 * PAR_FOLLOW off double-click).
 */
void seq_clearPerTrackOverrides(void);
```

---

### 1.6  MODIFY `seq_advanceTrackStep()` — per-track pattern reads

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 907  
**Operation:** MODIFY

Change:
```c
const pat_scene_region_t *region = pat_sceneRegion(seq_activePattern);
```
To:
```c
const pat_scene_region_t *region = pat_sceneRegion(seq_perTrackPattern[track]);
```

```
/*
 * Per-track played-Scene read (S077 P2 §3.2).
 *
 * What:       reads the track's own played Scene instead of the global active
 *             Scene for pattern region, step activity, step specials, and
 *             automation. Why: when a track plays from a Scene other than the
 *             active one, its sequence data (loop length, step triggers,
 *             automation) must come from that track's assigned Scene. Inputs:
 *             seq_perTrackPattern[track] — a single-byte SRAM1 read, atomic on
 *             Cortex-M7. Output: region, step checks, and automation all
 *             resolve against the track's played Scene. When no per-track
 *             override is set, seq_perTrackPattern[track] == seq_activePattern
 *             and behaviour is identical to the original code.
 * Affiliates: seq_perTrackPattern[], seq_setTrackPlayedScene().
 */
```

**Line:** 928–929  
**Operation:** MODIFY

Change:
```c
pat_step_specials_t sp = pat_readStepSpecials(
    seq_activePattern, track, (uint8_t)seq_stepIndex[track]);
```
To:
```c
pat_step_specials_t sp = pat_readStepSpecials(
    seq_perTrackPattern[track], track, (uint8_t)seq_stepIndex[track]);
```

**Line:** 932  
**Operation:** MODIFY

Change:
```c
if (pat_isStepActive(track, (uint8_t)seq_stepIndex[track], seq_activePattern)) {
```
To:
```c
if (pat_isStepActive(track, (uint8_t)seq_stepIndex[track], seq_perTrackPattern[track])) {
```

**Line:** 939–940  
**Operation:** MODIFY (erase path — still writes to the playing scene)

Change:
```c
pat_setStepActive(seq_activePattern, track,
                  (uint8_t)seq_stepIndex[track], 0u);
patSvc_enqueueErase(seq_activePattern, track,
                    (uint8_t)seq_stepIndex[track]);
```
To:
```c
pat_setStepActive(seq_perTrackPattern[track], track,
                  (uint8_t)seq_stepIndex[track], 0u);
patSvc_enqueueErase(seq_perTrackPattern[track], track,
                    (uint8_t)seq_stepIndex[track]);
```

```
/*
 * Live erase writes to the track's played Scene (S077 P2 §3.2).
 *
 * What:       erase targets the pattern data the track is actually playing,
 *             not necessarily the global active Scene. Why: erasing from the
 *             active Scene while the track plays from a different Scene would
 *             clear steps that are not audible and leave audible steps intact.
 * Inputs:     seq_perTrackPattern[track]. Output: step cleared in the correct
 *             Scene's pattern region.
 */
```

---

### 1.7  MODIFY `seq_queueStepAutomations()` — per-track pattern read

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 766  
**Operation:** MODIFY

Change:
```c
region = pat_sceneRegion(seq_activePattern);
```
To:
```c
region = pat_sceneRegion(seq_perTrackPattern[track]);
```

```
/*
 * Per-track automation region (S077 P2 §3.2).
 *
 * What:       reads the automation block from the track's own played Scene.
 * Why:        automation entries are authored per-Scene; a track playing from
 *             Scene B must queue Scene B's automation, not Scene A's.
 * Inputs:     seq_perTrackPattern[track]. Output: automation entries are
 *             read from the correct Scene's pattern pool.
 * Affiliates: seq_drainPendingAutomation() (the foreground consumer).
 */
```

---

### 1.8  MODIFY `seq_queueEffectStepMarker()` — effect type gate

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 826–843  
**Operation:** MODIFY (add effect-type gate)

The existing function queues an FX marker when the track's bit is set in
`seq_effectAutomationTracks`.  Add a type-match gate: compare the track's
played Scene's effect type against the active Scene's effect type.  If they
differ, suppress the marker (the foreground drain's `effects_automationStepBegin`
would apply parameters from the wrong effect type).

```
/*
 * Effect-type gate for per-track FX step markers (S077 P2 §3.6, Q9).
 *
 * What:       before queueing an FX step boundary, compare the active Scene's
 *             retained effect type against the track's played Scene's retained
 *             effect type. If the types differ, the marker is dropped.
 * Why:        FX automation entries are authored for a specific effect type.
 *             Applying them to a different type would write parameters that do
 *             not correspond to the correct effect, producing undefined DSP
 *             behaviour.
 * Inputs:     scene_effectConst(seq_activePattern)->type,
 *             scene_effectConst(seq_perTrackPattern[track])->type.
 * Output:     marker queued only when types match.
 * Fast path:  when seq_perTrackPattern[track] == seq_activePattern, the
 *             comparison is trivially equal and no scene_effectConst() call is
 *             needed. Use seq_perTrackActive as the outer gate.
 * Affiliates: effects_automationStepBegin() (the foreground consumer),
 *             EffectsManager.c effects_sceneType() (the type accessor).
 */
```

NOTE: This requires `scene_effectConst()` to be callable from TIM3 ISR context.
`scene_effectConst()` is a const pointer return from SRAM1 with no allocation
or I/O — it is safe.  However, the comparison itself adds two pointer reads
(~4 cycles) to the ISR path per track per step only when `seq_perTrackActive`
is nonzero.  Gate the entire comparison behind `seq_perTrackActive` to avoid
the cost when no per-track override is set.

---

### 1.9  MODIFY `seq_triggerVoice()` — per-track MIDI channel/note

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 587  
**Operation:** MODIFY

Change:
```c
midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_activePattern, voiceNr) - 1u);
```
To:
```c
midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_perTrackPattern[voiceNr], voiceNr) - 1u);
```

**Line:** 594  
**Operation:** MODIFY

Change:
```c
midiNote = scene_getTrackMidiNote(seq_activePattern, voiceNr);
```
To:
```c
midiNote = scene_getTrackMidiNote(seq_perTrackPattern[voiceNr], voiceNr);
```

```
/*
 * Per-track MIDI channel/note (S077 P2 §6.5).
 *
 * What:       MIDI output uses the track's played Scene's channel and note
 *             settings, not the global active Scene's.
 * Why:        each track should send MIDI on the channel/note configured in
 *             the Scene it is playing from.
 * Inputs:     seq_perTrackPattern[voiceNr].
 * Output:     MIDI note-on uses the correct Scene's channel and note.
 */
```

---

### 1.10  MODIFY `seq_previewVoice()` — per-track MIDI channel/note

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 623  
**Operation:** MODIFY

Change:
```c
note = scene_getTrackMidiNote(seq_activePattern, voiceNr);
```
To:
```c
note = scene_getTrackMidiNote(seq_perTrackPattern[voiceNr], voiceNr);
```

**Line:** 637  
**Operation:** MODIFY

Change:
```c
midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_activePattern, voiceNr) - 1u);
```
To:
```c
midiChan = (uint8_t)(scene_getTrackMidiChannel(seq_perTrackPattern[voiceNr], voiceNr) - 1u);
```

```
/*
 * Preview uses the track's played Scene for MIDI identity (S077 P2 §6.5).
 *
 * What:       stopped-transport voice preview should audition through the
 *             MIDI channel/note of whichever Scene the track is playing from.
 */
```

---

### 1.11  MODIFY `seq_realignActivePatternToMasterClock()` — per-track region

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 1254  
**Operation:** MODIFY

Change:
```c
const pat_scene_region_t *region = pat_sceneRegion(seq_activePattern);
```
To:
```c
/* Per-track realignment reads each track's own played Scene's region. */
```

**Line:** 1256–1261 (the `for` loop body)  
**Operation:** MODIFY

Change the loop to read each track's own region:
```c
for (track = 0u; track < NUM_TRACKS; track++) {
    const pat_scene_region_t *region = pat_sceneRegion(seq_perTrackPattern[track]);
    uint8_t len = (region && region->track_length[track] > 0u)
                  ? region->track_length[track]
                  : NUM_STEPS_PER_BAR;
    seq_stepIndex[track] = (int16_t)(seq_masterStepClock % len);
    seq_lastMasterStep[track] = (uint8_t)seq_stepIndex[track];
}
```

```
/*
 * Per-track realignment uses each track's played Scene (S077 P2 §3.2).
 *
 * What:       each track derives its step position from the master clock using
 *             its own played Scene's track_length, not the global active
 *             Scene's. Why: tracks playing from different Scenes may have
 *             different track lengths. A global region read would position
 *             tracks at step offsets calculated for the wrong loop length.
 * Inputs:     seq_perTrackPattern[track], seq_masterStepClock.
 * Output:     seq_stepIndex[track] = masterStepClock % track's length.
 * Note:       the region pointer moves inside the loop. pat_sceneRegion()
 *             returns a pointer to SRAM1 with no allocation; the per-track
 *             call adds ~7 pointer dereferences to this foreground function.
 */
```

---

### 1.12  MODIFY `seq_selectActivePattern()` — coalesce + recompute flag

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 676–677  
**Operation:** MODIFY

After the existing loop that sets all `seq_perTrackPattern[]` entries, add:
```c
seq_perTrackActive = 0;
```

```
/*
 * Scene-level switch always coalesces (S077 P2 §4.3, Q2).
 *
 * What:       the existing loop already sets every seq_perTrackPattern[] entry
 *             to the new pattern. Explicitly clear seq_perTrackActive here
 *             because the loop makes all entries equal to seq_activePattern.
 */
```

---

### 1.13  MODIFY `seq_alignActivePatternToScene()` — coalesce + recompute flag

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 730–731  
**Operation:** MODIFY

After the existing loop, add:
```c
seq_perTrackActive = 0;
```

```
/*
 * Bank Load alignment also clears per-track overrides (S077 P2 §6.4).
 *
 * What:       Bank Load coalesces all tracks to the loaded active Scene.
 *             Per-track overrides from the prior Bank are stale and must be
 *             cleared because Scene indices now point at newly loaded content.
 */
```

---

### 1.14  MODIFY `seq_handleMasterBoundary()` — coalesce + recompute flag

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 1205–1206  
**Operation:** MODIFY

After the existing per-track loop in the boundary switch, add:
```c
seq_perTrackActive = 0;
```

```
/*
 * Master-boundary pattern commit clears per-track overrides (S077 P2).
 *
 * What:       when seq_pendingPattern differs from seq_activePattern and the
 *             bar boundary commits the switch, all per-track entries are
 *             already set to seq_activePattern by the existing loop.
 *             Explicitly clear seq_perTrackActive.
 */
```

---

### 1.15  MODIFY `seq_init()` — clear flag

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 487 (after the existing per-track init loop)  
**Operation:** ADD

```c
seq_perTrackActive = 0;
```

---

### 1.16  MODIFY `seq_drainPendingAutomation()` — per-track instrument slot read

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 1084–1089  
**Operation:** MODIFY

The voice automation drain currently resolves the instrument slot from
`seq_activePattern`:
```c
if (instrumentManager_targetValid(seq_activePattern, target, ...)) {
    ...
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(seq_activePattern, slot);
```

This must change to read from the track's played Scene for voice-scoped
targets.  The track identity is embedded in the automation record's
`identity` field as `step_id / NUM_STEPS`:

```
/*
 * Per-track instrument slot for voice automation (S077 P2 §3.6, Q1).
 *
 * What:       when draining a voice-scoped automation entry, resolve the
 *             instrument slot from the track's played Scene instead of the
 *             global active Scene. The track index is recovered from the
 *             queued step identity: track = (identity & STEP_ID_MASK) /
 *             NUM_STEPS. Why: the track's instrument is loaded from its
 *             played Scene (by the Phase 2 single-voice apply), so the
 *             descriptor image used for target validation and runtime write
 *             must match. Inputs: the track's seq_perTrackPattern[track].
 * Output:     target validation and runtime write use the correct Scene's
 *             instrument slot. When seq_perTrackActive is 0, the played
 *             Scene equals the active Scene and this changes nothing.
 * Affiliates: instrumentManager_targetValid(), scene_instrumentSlotConst(),
 *             instrumentManager_writeRuntime().
 */
```

---

### 1.17  MODIFY `seq_drainPendingAutomation()` — effect automation type gate

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 1103–1108  
**Operation:** MODIFY

The effect automation drain currently applies all block-7 entries
unconditionally.  Add a type-match gate for entries whose owning track
plays from a different Scene:

```
/*
 * Effect automation type gate (S077 P2 §3.6, Q9).
 *
 * What:       before applying a block-7 (Effect) automation entry, compare
 *             the active Scene's effect type against the owning track's
 *             played Scene's effect type. If the types differ, drop the
 *             entry. Why: effect parameters are type-specific. Applying
 *             parameters authored for one effect type (e.g. CrumpBit) to a
 *             different active effect type (e.g. StereoFilter) would write
 *             values into undefined parameter slots.
 * Inputs:     scene_effectConst(seq_activePattern)->type,
 *             scene_effectConst(seq_perTrackPattern[track])->type.
 *             Track derived from identity as (step_id / NUM_STEPS).
 * Output:     entry applied only when types match.
 * Fast path:  when seq_perTrackActive is 0, skip the comparison.
 */
```

---

### 1.18  MODIFY `seq_restoreAutomatedParameters()` — per-track instrument slot

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 1159  
**Operation:** MODIFY

Change:
```c
instrument = scene_instrumentSlotConst(seq_activePattern, slot);
```
To:
```c
instrument = scene_instrumentSlotConst(seq_perTrackPattern[trigger_track], slot);
```

```
/*
 * Retrigger restore reads the track's played Scene (S077 P2 §3.2).
 *
 * What:       the morph_interpolation[] values used to restore automation
 *             overlays must come from the Scene whose instrument is actually
 *             loaded in this slot — the track's played Scene, not the global
 *             active Scene. Why: if a track plays from Scene B, its runtime
 *             instrument image is Scene B's; restoring from Scene A's image
 *             would apply wrong values.
 * Inputs:     seq_perTrackPattern[trigger_track].
 */
```

---

### 1.19  MODIFY `seq_restoreAllAutomation()` — per-track instrument slot

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 334  
**Operation:** MODIFY

Change:
```c
instrument = scene_instrumentSlotConst(seq_activePattern, slot);
```
To:
```c
instrument = scene_instrumentSlotConst(seq_perTrackPattern[slot], slot);
```

Note: `seq_restoreAllAutomation()` iterates by slot (0..5), not track.  For
the restore, the slot index IS the track index (track 7 maps to slot 5 via
the existing guard).  `seq_perTrackPattern[slot]` is valid because slots 0..5
correspond to tracks 0..5.

```
/*
 * All-slot automation restore uses per-track played Scene (S077 P2 §3.2).
 *
 * What:       the morph_interpolation[] restore base must come from the Scene
 *             whose instrument is actually loaded in each slot.
 * Inputs:     seq_perTrackPattern[slot] for slot 0..5.
 */
```

---

### 1.20  MODIFY `seq_setRecordingMode()` — gate on per-track overrides

**File:** `Core/Sequencer/sequencer.c`  
**Line:** 1749  
**Operation:** MODIFY

Change:
```c
void seq_setRecordingMode(uint8_t active)
{
    seq_recordActive = active;
}
```
To:
```c
void seq_setRecordingMode(uint8_t active)
{
    if (active && seq_perTrackActive)
        return;
    seq_recordActive = active;
}
```

```
/*
 * Record-arm gate on per-track overrides (S077 P2 §3.7).
 *
 * What:       refuses to arm live recording when any track plays from a Scene
 *             other than the active Scene.
 * Why:        recording into a track whose played Scene differs from the
 *             viewed Scene would write steps into the wrong Scene's pattern
 *             data. The record-arm UI should grey out or not toggle.
 * Inputs:     seq_perTrackActive. Output: seq_recordActive unchanged when
 *             per-track overrides exist.
 */
```

---

## Phase 2: Single-voice instrument apply

### 2.1  New API: `preset_startSingleVoiceApply()`

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Line:** ~1787 (after `preset_startDrumsetApply`)  
**Operation:** ADD

```
/*
 * Apply one voice slot from a specific Scene for per-track playback.
 *
 * What:       a single-slot variant of preset_startDrumsetApply() for per-track
 *             Scene assignment. Applies one instrument slot's image, scene
 *             settings (fader, FX send, output, morph amount), and audio routing
 *             from source_scene, using the same deferred envelope-quiet mechanism
 *             as the drumset worker.
 * Why:        when a track is assigned to play from a different Scene, its voice
 *             slot must be loaded with that Scene's instrument image and settings.
 *             The full drumset worker replaces ALL slots; this replaces ONE.
 * Inputs:     slot 0..5, source Scene index.
 * Outputs:    (1) If the drumset worker is active and this slot is pending,
 *                 clear this slot's bit from drumset_apply_pending_mask.
 *             (2) Capture LFO phase for this slot.
 *             (3) Clear runtime modulation targets for this slot only.
 *             (4) Apply per-voice Scene settings from source_scene:
 *                 - mixer_audioRouting[slot] ← source scene's audio_out[slot]
 *                 - mixer_fxSend[slot] ← source scene's fx_send_amount[slot]
 *                 - mixer_fxSendMorph[slot] ← source scene's fx_send_morph[slot]
 *                 - mixer_faderSetting[slot] ← source scene's fader_setting[slot]
 *                 - per-voice morph amount ← source scene's voice_morph_amount[slot]
 *             (5) Arm a single-slot pending mask for deferred image swap.
 *             (6) After commit: run modulation setup for the committed slot if
 *                 the drumset worker is idle; otherwise let the drumset worker's
 *                 final modulation rebind include this slot.
 * Coexistence: supersedes the drumset worker for the affected slot by clearing
 *             the corresponding bit in drumset_apply_pending_mask. The drumset
 *             worker's per-slot independence (round-robin scan, per-slot pending
 *             bits) makes this safe. (Plan §3.4, Q6-b).
 * For tracks 5+6 (HiHat pair): called twice, once for each slot.
 * Affiliates: preset_tickDrumsetApply(), drumset_apply_pending_mask,
 *             preset_resetAndApplyKitVoiceImage(), preset_applyKitAudioRouting(),
 *             instrumentManager_captureLfoPhases(),
 *             instrumentManager_clearRuntimeModulationTargetsForSlot().
 */
void preset_startSingleVoiceApply(uint8_t slot, uint8_t source_scene)
```

**File:** `Core/Bank/Scene/Preset/presetManager.h`  
**Line:** after `preset_startDrumsetApply` declaration (~line 500)  
**Operation:** ADD

```
/*
 * Per-track single-voice deferred apply (S077 P2 §3.4).
 *
 * Inputs: slot 0..5, source Scene. Output: that slot is applied from the
 * source Scene using the deferred quiet-wait mechanism. Supersedes the
 * drumset worker for the affected slot.
 */
void preset_startSingleVoiceApply(uint8_t slot, uint8_t source_scene);
```

---

### 2.2  New helper: per-slot modulation target clear

**File:** `Core/DSP/InstrumentManager/InstrumentManager.c`  
**Operation:** ADD

```
/*
 * Clear runtime modulation targets for one slot only.
 *
 * What:       detaches LFO and velocity modulation nodes for a single slot
 *             without disturbing other slots' bindings.
 * Why:        the single-voice apply must clear only the affected slot's
 *             outgoing graph, unlike the full drumset worker which clears all
 *             six slots at once. Clearing all would break live modulation on
 *             slots that are not being reassigned.
 * Inputs:     slot 0..5.
 * Output:     LFO adapter targets and velocity sources for this slot are
 *             detached.
 * Affiliates: instrumentManager_clearAllRuntimeModulationTargets() (the
 *             all-slot version used by the drumset worker).
 */
void instrumentManager_clearRuntimeModulationTargetsForSlot(uint8_t slot)
```

**File:** `Core/DSP/InstrumentManager/InstrumentManager.h`  
**Operation:** ADD declaration

---

### 2.3  New helper: per-slot LFO phase capture

**File:** `Core/DSP/InstrumentManager/InstrumentManager.c`  
**Operation:** ADD

```
/*
 * Capture LFO phase for one slot before a single-voice apply.
 *
 * What:       snapshots the running LFO phase for one slot into the static
 *             handoff struct.
 * Why:        preset_resetAndApplyKitVoiceImage() will zero the phase via
 *             instrumentManager_resetRuntimeSlot(). The snapshot preserves it
 *             for restoration when the incoming LFO does not have scene
 *             retrigger set.
 * Inputs:     slot 0..5.
 * Output:     lfo_scene_handoff for this slot populated and marked valid.
 * Affiliates: instrumentManager_captureLfoPhases() (all-slot version),
 *             instrumentManager_restoreLfoPhaseIfNeeded() (consumer).
 */
void instrumentManager_captureLfoPhaseForSlot(uint8_t slot)
```

**File:** `Core/DSP/InstrumentManager/InstrumentManager.h`  
**Operation:** ADD declaration

---

### 2.4  MODIFY `preset_tickDrumsetApply()` — respect single-voice supersede

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Line:** 1855–1856  
**Operation:** No change required

The existing code already checks `(drumset_apply_pending_mask & bit) == 0u`
and skips cleared slots.  The single-voice apply clears the relevant bit, so
the drumset worker will naturally skip slots that have been superseded.  No
code change is needed; the existing independence is sufficient.

```
/* DOCUMENTATION ONLY — no code change.
 *
 * The drumset worker's round-robin scan already skips slots whose pending bit
 * has been cleared. preset_startSingleVoiceApply() clears the bit for its
 * target slot, so the drumset worker will not overwrite the single-voice
 * apply's work. This is the design described in plan §3.4, Q6-b.
 */
```

---

### 2.5  Per-voice Scene settings application helper

**File:** `Core/Bank/Scene/Preset/presetManager.c`  
**Operation:** ADD (static helper, called by `preset_startSingleVoiceApply`)

```
/*
 * Apply per-voice Scene settings from a specific source Scene.
 *
 * What:       writes one slot's Scene settings (audio out, FX send normal,
 *             FX send morph, fader setting, voice morph amount) from the
 *             source Scene into the live mixer/runtime state.
 * Why:        when a track plays from a non-active Scene, its voice-level
 *             Scene settings must come from the source Scene rather than the
 *             active Scene (plan §3.5). These are applied once at switch time.
 * Inputs:     slot 0..5, source Scene index.
 * Output:     mixer routing, FX send, fader, and morph amounts updated for
 *             this slot. No AutoSave or retained SceneData mutation — this is
 *             a runtime-only overlay.
 * Not per-track (always from active Scene): Effect record, bus compressor,
 *             global morph amount, effect morph amount.
 * Affiliates: preset_applyKitAudioRouting(), preset_applySceneSettings(),
 *             scene_getVoiceAudioOut(), scene_getVoiceFxSendAmount(),
 *             scene_getVoiceFxSendMorph(), scene_getVoiceFaderSetting(),
 *             scene_getVoiceMorphAmount().
 */
static void preset_applyPerVoiceSceneSettings(uint8_t slot,
                                               uint8_t source_scene)
```

---

## Phase 3: Button handler / PERF gestures

### 3.1  New state: PERF VOICE hold mask and action flag

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** ~41 (after `btn_held` declaration)  
**Operation:** ADD

```
/*
 * PERF mode held-VOICE bitmask for per-track Scene assignment.
 *
 * What:       one bit per VOICE button (bits 0..6) set while that VOICE button
 *             is physically held in PERF mode. While any bit is set, SEQ
 *             presses are intercepted for per-track assignment instead of
 *             scene-level switching.
 * Why:        the hold-VOICE+press-SEQ gesture enables per-track Scene
 *             playback (plan §4.1). Multiple VOICE buttons may be held
 *             simultaneously for multi-track assignment (Q11).
 * RAM:        +1 byte SRAM1. Owner: buttonHandler.c.
 * Affiliates: perf_voiceActionOccurred (mute cancel flag),
 *             seq_setTrackPlayedScene() (the sequencer API called on SEQ press),
 *             preset_startSingleVoiceApply() (the DSP apply called after).
 */
static uint8_t perf_heldVoiceMask = 0;

/*
 * PERF VOICE action-occurred flag for mute toggle cancellation.
 *
 * What:       set to 1 when a held VOICE button's hold produced a per-track
 *             assignment (SEQ press during hold). When set, the VOICE release
 *             does not toggle mute.
 * Why:        the VOICE release is a single-track mute toggle only when no
 *             per-track assignment or other non-VOICE/non-SEQ action occurred
 *             during the hold (plan §4.1, Q8).
 * RAM:        +1 byte SRAM1. Owner: buttonHandler.c.
 * Affiliates: perf_heldVoiceMask.
 */
static uint8_t perf_voiceActionOccurred = 0;
```

---

### 3.2  New state: double-click detector

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** after the new PERF state  
**Operation:** ADD

```
/*
 * General-purpose double-click detector.
 *
 * What:       detects a second press of the same button within
 *             DOUBLE_CLICK_TIMEOUT of the first press. Only one button can be
 *             in a double-click window at a time. The first click fires
 *             immediately (no single-click latency); the second click within
 *             the timeout fires the double-click action additively.
 * Why:        PERF mode needs double-click on SEQ buttons for realignment
 *             (plan §4.2, Q7). The detector is general-purpose so it can be
 *             reused for other buttons in the future.
 * Protocol:   (1) On any button press, call dblclick_onPress(buttonNr).
 *             (2) It returns DBLCLICK_FIRST on the first registered press,
 *                 DBLCLICK_DOUBLE on the second press of the same button
 *                 within the timeout, or DBLCLICK_FIRST if a different button
 *                 or any non-matching action intervened.
 *             (3) Call dblclick_cancel() when any action other than the same
 *                 button press occurs (cancels the pending window).
 * RAM:        ~4 bytes SRAM1 (button ID, timestamp, active flag).
 * Affiliates: DOUBLE_CLICK_TIMEOUT (config.h), time_sysTick.
 */
static uint8_t dblclick_button = 0;
static uint16_t dblclick_deadline = 0;
static uint8_t dblclick_armed = 0;

#define DBLCLICK_FIRST  0u
#define DBLCLICK_DOUBLE 1u

static uint8_t dblclick_onPress(uint8_t buttonNr) { ... }
static void dblclick_cancel(void) { ... }
```

**File:** `config.h`  
**Line:** 436 (after `BUTTON_HOLD_DELAY_MS`)  
**Operation:** ADD

```
/*
 * Double-click detection window (S077 P2 §4.2, Q7).
 *
 * What:       the maximum milliseconds between the first and second press of
 *             the same button for the second press to count as a double-click.
 * Why:        PERF SEQ double-click triggers realignment. The timeout must be
 *             short enough to avoid false positives during rapid scene
 *             switching but long enough for a deliberate double-tap.
 * Inputs:     time_sysTick. Output: double-click detector comparison.
 * Domain:     must stay below 32768 for unsigned 16-bit wrap-safe comparison.
 * Affiliates: buttonHandler.c dblclick_onPress().
 */
#define DOUBLE_CLICK_TIMEOUT 300u
```

---

### 3.3  MODIFY `handleVoiceButton()` — PERF mode per-track gesture

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** 1238–1256  
**Operation:** REMOVE (the cumulative unmute block) + ADD (new PERF VOICE behaviour)

REMOVE the existing PERF voice block:
```c
if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
    uint8_t i;
    for (i = 0; i <= voiceNr; i++) {
        if (buttonHandler_mutedVoices & (1u << i)) {
            seq_setMute(i, 0);
            buttonHandler_mutedVoices &= (uint8_t)~(1u << i);
        }
    }
    buttonHandler_showMuteLEDs();
    if (shouldPreviewVoice)
        seq_previewVoice(voiceNr);
    return;
}
```

ADD replacement:
```c
if (bh_state.selectButtonMode == SELECT_MODE_PERF) {
    /*
     * PERF VOICE press: arm hold for per-track assignment (S077 P2 §4.1).
     *
     * What:       sets this VOICE's bit in perf_heldVoiceMask. While any VOICE
     *             bit is set, SEQ presses become per-track assignments. The
     *             mute toggle fires on release unless cancelled.
     * Replaces:   the old cumulative unmute gesture (Q8).
     */
    perf_heldVoiceMask |= (uint8_t)(1u << voiceNr);
    return;
}
```

```
/*
 * PERF VOICE button press: per-track hold arm (S077 P2 §4.1, Q8).
 *
 * What:       replaces the old cumulative unmute (tracks 0..N) with a hold
 *             arm for per-track Scene assignment. The VOICE button sets its
 *             bit in perf_heldVoiceMask; SEQ presses while held become
 *             per-track assignments. Mute toggle fires on release.
 * Why:        the cumulative unmute conflicted with the per-track gesture.
 *             Single-track mute toggle on release is more intuitive and
 *             supports multi-track holds (Q11, Q12).
 * Inputs:     voiceNr 0..6. Output: perf_heldVoiceMask updated.
 * Affiliates: processRelease() (the release handler that toggles mute),
 *             buttonHandler_seqButtonPressed() (the SEQ intercept).
 */
```

---

### 3.4  MODIFY `buttonHandler_seqButtonPressed()` — PERF SEQ intercept

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** 779–781  
**Operation:** MODIFY

Change:
```c
case SELECT_MODE_PERF:
    menu_perfModeSceneButtonPressed(seqButtonPressed);
    break;
```
To:
```c
case SELECT_MODE_PERF:
    if (perf_heldVoiceMask != 0u) {
        /* Per-track assignment: hold VOICE + press SEQ (S077 P2 §4.1). */
        buttonHandler_perfVoiceHeldSeqPressed(seqButtonPressed);
    } else {
        /* Scene-level switch, with double-click detection. */
        buttonHandler_perfScenePressed(seqButtonPressed);
    }
    break;
```

```
/*
 * PERF SEQ press routing (S077 P2 §4.1, §4.2).
 *
 * What:       when a VOICE is held, SEQ presses route to per-track assignment.
 *             When no VOICE is held, SEQ presses route to scene-level switch
 *             with double-click detection for realignment.
 * Affiliates: buttonHandler_perfVoiceHeldSeqPressed() (per-track assign),
 *             buttonHandler_perfScenePressed() (scene switch + double-click).
 */
```

---

### 3.5  New handler: `buttonHandler_perfVoiceHeldSeqPressed()`

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Operation:** ADD (static function)

```
/*
 * Per-track Scene assignment: VOICE held + SEQ pressed (S077 P2 §4.1).
 *
 * What:       for each VOICE bit set in perf_heldVoiceMask, calls
 *             seq_setTrackPlayedScene(track, scene_index) and
 *             seq_realignTrackToMasterClock(track), then starts
 *             preset_startSingleVoiceApply() for each affected slot.
 * Why:        the user holds one or more VOICE buttons and presses a SEQ
 *             button to assign those tracks to play from the pressed Scene.
 * Inputs:     seqButtonPressed 0..15 (the Scene index).
 *             perf_heldVoiceMask (which tracks to assign).
 * Output:     per-track playback state updated, instrument apply started,
 *             perf_voiceActionOccurred set to 1 (cancels mute toggle).
 * Validation: scene_index must be valid and present (bank_scenePresent).
 * HiHat link: tracks 5 and 6 always switch together; if either bit is set
 *             in the mask, both are assigned.
 * Affiliates: seq_setTrackPlayedScene(), seq_realignTrackToMasterClock(),
 *             preset_startSingleVoiceApply().
 */
static void buttonHandler_perfVoiceHeldSeqPressed(uint8_t seqButtonPressed)
```

---

### 3.6  New handler: `buttonHandler_perfScenePressed()`

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Operation:** ADD (static function)

```
/*
 * PERF Scene press with double-click detection (S077 P2 §4.2, §4.3).
 *
 * What:       handles a SEQ press in PERF mode when no VOICE is held.
 *             Integrates double-click detection to distinguish single-click
 *             (view or switch depending on PAR_FOLLOW) from double-click
 *             (full change + realign).
 *
 *             PAR_FOLLOW on, single click: scene-level switch via
 *               menu_perfModeSceneButtonPressed(). Coalesces all tracks.
 *             PAR_FOLLOW on, double click: same + explicit realign.
 *             PAR_FOLLOW off, single click: view change only via
 *               menu_setShownPattern() + menu_refreshPerfSceneLeds(). No
 *               playback change. Per-track overrides remain.
 *             PAR_FOLLOW off, double click: full scene change + coalesce +
 *               realign via menu_perfModeSceneButtonPressed().
 *
 * Inputs:     seqButtonPressed 0..15, PAR_FOLLOW state.
 * Output:     scene state updated per the above rules.
 * Affiliates: menu_perfModeSceneButtonPressed() (the existing Scene switch),
 *             seq_clearPerTrackOverrides() (coalesce),
 *             seq_realignActivePatternToMasterClock() (realign),
 *             dblclick_onPress() (double-click detection).
 */
static void buttonHandler_perfScenePressed(uint8_t seqButtonPressed)
```

---

### 3.7  MODIFY `processRelease()` — PERF VOICE release mute toggle

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** in `processRelease` (the release event handler)  
**Operation:** ADD (new case for VOICE release in PERF mode)

```
/*
 * PERF VOICE release: single-track mute toggle (S077 P2 §4.1, Q8, Q12).
 *
 * What:       on VOICE button release in PERF mode, clear this button's bit
 *             from perf_heldVoiceMask. If perf_voiceActionOccurred is 0 and no
 *             non-VOICE/non-SEQ button was pressed during the hold, toggle
 *             mute for this single track. Each VOICE button's mute fires on
 *             its own release independently (Q12).
 * Why:        replaces the old cumulative unmute. The mute toggle is cancelled
 *             when a per-track assignment occurred during the hold, or when
 *             any button other than another VOICE or SEQ button was pressed.
 * Inputs:     perf_heldVoiceMask, perf_voiceActionOccurred.
 * Output:     mute toggled or not; mask bit cleared.
 * Affiliates: seq_setMute(), buttonHandler_muteVoice(),
 *             buttonHandler_showMuteLEDs().
 */
```

---

### 3.8  MODIFY `processPress()` — cancel double-click on non-SEQ actions

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** in `processPress`, after the SEQ/SELECT/VOICE routing  
**Operation:** ADD

When any button other than the double-click candidate is pressed, call
`dblclick_cancel()`.

```
/*
 * Cancel double-click window on non-SEQ actions (S077 P2 §4.2, Q7).
 *
 * What:       any button press other than the same button cancels the
 *             double-click detection window.
 */
```

---

### 3.9  MODIFY `processPress()` — cancel PERF VOICE mute on non-VOICE/non-SEQ

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** in `processPress`, when a non-VOICE, non-SEQ button is pressed while PERF VOICE is held  
**Operation:** ADD

```
/*
 * Cancel PERF VOICE mute gesture on unrelated button (S077 P2 §4.1, Q8).
 *
 * What:       if perf_heldVoiceMask is nonzero and a button that is neither a
 *             VOICE nor a SEQ button is pressed, set perf_voiceActionOccurred
 *             to 1 so the subsequent VOICE releases do not toggle mute.
 */
```

---

## Phase 4: LED feedback

### 4.1  MODIFY `menu_refreshPerfSceneLeds()` — active scene tempo pulse

**File:** `Core/Menu/menu.c`  
**Line:** 6869–6878  
**Operation:** MODIFY

The active Scene LED currently blinks.  Change it to use a tempo-pulse
(inverted play LED style: on at beat boundaries, off between beats).  This
requires reading the sequencer beat state.

```
/*
 * Active Scene LED tempo pulse (S077 P2 §5.1).
 *
 * What:       the active Scene's SEQ LED pulses with the tempo rather than
 *             using the standard blink rate. During playback, the LED is on at
 *             beat boundaries and off between beats (inverted relative to the
 *             play LED). When stopped, the LED continues blinking at the
 *             standard blink rate.
 * Why:        distinguishes the active Scene from simply selected/viewed
 *             Scenes and provides tempo-synchronised visual feedback.
 * Inputs:     seq_ledState.beatPulse, seq_isRunning(), active Scene index.
 * Output:     LED_SEQ(active_scene) tempo-pulsed during playback, blink
 *             when stopped.
 * Affiliates: led_processSeqLedState() (updates beat pulse from sequencer),
 *             led_notifyPatternChanged().
 */
```

---

### 4.2  ADD viewed-scene rapid flash (PAR_FOLLOW off)

**File:** `Core/Menu/menu.c`  
**Line:** in `menu_refreshPerfSceneLeds()`  
**Operation:** ADD

```
/*
 * Viewed Scene rapid flash (S077 P2 §5.2).
 *
 * What:       when the viewed Scene differs from the active Scene (PAR_FOLLOW
 *             off), the viewed Scene's SEQ LED flashes rapidly (faster than
 *             tempo pulse).
 * Why:        provides visual feedback about which Scene the user is viewing/
 *             editing when it differs from the playing Scene.
 * Inputs:     menu_getViewedPattern(), scene_getActiveIndex().
 * Output:     LED_SEQ(viewed_scene) rapid flash when viewed != active.
 * Affiliates: led_setBlinkLed() (rapid blink rate is an existing mechanism).
 */
```

---

### 4.3  ADD transient per-track indication during VOICE hold

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** in the new PERF VOICE press handler (§3.3)  
**Operation:** ADD

```
/*
 * Transient per-track Scene indication during VOICE hold (S077 P2 §5.4).
 *
 * What:       when holding a VOICE button in PERF for per-track assignment,
 *             blink the SEQ LED of the Scene that the held track is currently
 *             playing from. This is visible only during the hold gesture and
 *             disappears when the VOICE button is released.
 * Why:        allows the user to see which Scene a track is assigned to
 *             before changing it, without persistent LED clutter.
 * Inputs:     seq_perTrackPattern[voiceNr], perf_heldVoiceMask.
 * Output:     LED_SEQ(played_scene) blink during hold; restored on release.
 * Affiliates: processRelease() (restores PERF LED state).
 */
```

---

## Phase 5: PAR_FOLLOW view-follows-track

### 5.1  MODIFY `handleVoiceButton()` — VOICE mode view follows track

**File:** `Core/Hardware/frontPanel/buttonHandler.c`  
**Line:** 1276–1279 (the VOICE mode track selection path)  
**Operation:** ADD (conditional view switch)

```
/*
 * View-follows-track when PAR_FOLLOW is on (S077 P2 §6.1).
 *
 * What:       when PAR_FOLLOW is on and the user selects a track in VOICE
 *             mode, if that track's played Scene differs from the current
 *             viewed Scene, switch the viewed Scene and write target to the
 *             track's played Scene.
 * Why:        VOICE and STEP modes should show the settings of the actually
 *             playing track, not just the active Scene. This lets the user
 *             edit parameters of the instrument that is actually sounding.
 * Inputs:     seq_perTrackPattern[voiceNr], parameter_values[PAR_FOLLOW],
 *             menu_getViewedPattern().
 * Output:     menu_setShownPattern(seq_perTrackPattern[voiceNr]) when the
 *             played Scene differs and PAR_FOLLOW is on.
 * Guard:      only when seq_perTrackActive is nonzero (no overhead when no
 *             per-track overrides exist).
 * Affiliates: menu_setShownPattern(), led_updatePatternTrack().
 */
```

---

## RAM Budget Summary

| Item                              | Size   | Region | Phase |
|-----------------------------------|--------|--------|-------|
| seq_perTrackActive (flag)         | 1 B    | SRAM1  | 1     |
| perf_heldVoiceMask (button)       | 1 B    | SRAM1  | 3     |
| perf_voiceActionOccurred (flag)   | 1 B    | SRAM1  | 3     |
| dblclick_button                   | 1 B    | SRAM1  | 3     |
| dblclick_deadline                 | 2 B    | SRAM1  | 3     |
| dblclick_armed                    | 1 B    | SRAM1  | 3     |
| **Total new RAM**                 | **7 B**| SRAM1  |       |

`seq_perTrackPattern[7]` already exists (7 B, allocated in S067).

---

## Change Matrix

| # | File | Line(s) | Op | Phase | Description |
|---|------|---------|----|-------|-------------|
| 1 | sequencer.c | 183 | ADD | 1 | `seq_perTrackActive` flag |
| 2 | sequencer.h | 51 | ADD | 1 | `extern seq_perTrackActive` |
| 3 | sequencer.c | ~185 | ADD | 1 | `seq_recomputePerTrackActive()` static |
| 4 | sequencer.c | ~540 | ADD | 1 | `seq_setTrackPlayedScene()` |
| 5 | sequencer.h | ~134 | ADD | 1 | declaration of above |
| 6 | sequencer.c | ~1265 | ADD | 1 | `seq_realignTrackToMasterClock()` |
| 7 | sequencer.h | ~68 | ADD | 1 | declaration of above |
| 8 | sequencer.c | ~540 | ADD | 1 | `seq_clearPerTrackOverrides()` |
| 9 | sequencer.h | ~134 | ADD | 1 | declaration of above |
| 10 | sequencer.c | 907 | MOD | 1 | `seq_advanceTrackStep` region read |
| 11 | sequencer.c | 928-929 | MOD | 1 | `pat_readStepSpecials` scene source |
| 12 | sequencer.c | 932 | MOD | 1 | `pat_isStepActive` scene source |
| 13 | sequencer.c | 939-942 | MOD | 1 | Live erase scene source |
| 14 | sequencer.c | 766 | MOD | 1 | `seq_queueStepAutomations` region read |
| 15 | sequencer.c | 826-843 | MOD | 1 | `seq_queueEffectStepMarker` type gate |
| 16 | sequencer.c | 587 | MOD | 1 | `seq_triggerVoice` MIDI channel |
| 17 | sequencer.c | 594 | MOD | 1 | `seq_triggerVoice` MIDI note |
| 18 | sequencer.c | 623 | MOD | 1 | `seq_previewVoice` MIDI note |
| 19 | sequencer.c | 637 | MOD | 1 | `seq_previewVoice` MIDI channel |
| 20 | sequencer.c | 1254-1261 | MOD | 1 | `seq_realignActivePatternToMasterClock` per-track region |
| 21 | sequencer.c | 676-677 | MOD | 1 | `seq_selectActivePattern` clear flag |
| 22 | sequencer.c | 730-731 | MOD | 1 | `seq_alignActivePatternToScene` clear flag |
| 23 | sequencer.c | 1205-1206 | MOD | 1 | `seq_handleMasterBoundary` clear flag |
| 24 | sequencer.c | 487 | ADD | 1 | `seq_init` clear flag |
| 25 | sequencer.c | 1084-1089 | MOD | 1 | `seq_drainPendingAutomation` per-track instrument |
| 26 | sequencer.c | 1103-1108 | MOD | 1 | `seq_drainPendingAutomation` effect type gate |
| 27 | sequencer.c | 1159 | MOD | 1 | `seq_restoreAutomatedParameters` per-track |
| 28 | sequencer.c | 334 | MOD | 1 | `seq_restoreAllAutomation` per-track |
| 29 | sequencer.c | 1749-1752 | MOD | 1 | `seq_setRecordingMode` per-track gate |
| 30 | presetManager.c | ~1787 | ADD | 2 | `preset_startSingleVoiceApply()` |
| 31 | presetManager.h | ~500 | ADD | 2 | declaration of above |
| 32 | presetManager.c | ~1787 | ADD | 2 | `preset_applyPerVoiceSceneSettings()` static |
| 33 | InstrumentManager.c | — | ADD | 2 | `instrumentManager_clearRuntimeModulationTargetsForSlot()` |
| 34 | InstrumentManager.h | — | ADD | 2 | declaration of above |
| 35 | InstrumentManager.c | — | ADD | 2 | `instrumentManager_captureLfoPhaseForSlot()` |
| 36 | InstrumentManager.h | — | ADD | 2 | declaration of above |
| 37 | buttonHandler.c | ~41 | ADD | 3 | `perf_heldVoiceMask`, `perf_voiceActionOccurred` |
| 38 | buttonHandler.c | ~41 | ADD | 3 | Double-click detector state + helpers |
| 39 | config.h | 436 | ADD | 3 | `DOUBLE_CLICK_TIMEOUT` |
| 40 | buttonHandler.c | 1238-1256 | REM+ADD | 3 | Replace PERF VOICE cumulative unmute |
| 41 | buttonHandler.c | 779-781 | MOD | 3 | PERF SEQ routing (VOICE held / double-click) |
| 42 | buttonHandler.c | — | ADD | 3 | `buttonHandler_perfVoiceHeldSeqPressed()` |
| 43 | buttonHandler.c | — | ADD | 3 | `buttonHandler_perfScenePressed()` |
| 44 | buttonHandler.c | processRelease | ADD | 3 | PERF VOICE release mute toggle |
| 45 | buttonHandler.c | processPress | ADD | 3 | Double-click cancel on non-SEQ |
| 46 | buttonHandler.c | processPress | ADD | 3 | PERF VOICE mute cancel on non-VOICE/non-SEQ |
| 47 | menu.c | 6869-6878 | MOD | 4 | Active Scene tempo pulse |
| 48 | menu.c | 6869-6878 | ADD | 4 | Viewed != active rapid flash |
| 49 | buttonHandler.c | PERF VOICE | ADD | 4 | Transient SEQ LED during VOICE hold |
| 50 | buttonHandler.c | 1276-1279 | ADD | 5 | View-follows-track (PAR_FOLLOW on) |

---

## Dependencies and ordering

```
Phase 1 ──┐
           ├── Phase 2 ──┐
           │              ├── Phase 3 ──── Phase 4
           │              │                  │
           │              │                  └── Phase 5
           │              │
           └──────────────┘
```

- **Phase 1** is standalone: all sequencer read-path changes are safe to test
  with manual `seq_perTrackPattern[]` overrides before the UI exists.
- **Phase 2** depends on Phase 1 because the single-voice apply must set
  `seq_perTrackPattern[]` before applying the instrument.
- **Phase 3** depends on Phase 2 because the button gesture must call both
  `seq_setTrackPlayedScene()` and `preset_startSingleVoiceApply()`.
- **Phase 4** can proceed in parallel with Phase 3's button work but depends
  on the state variables from Phases 1 and 3.
- **Phase 5** depends on all prior phases.

---

## Testing checkpoints

| Phase | Test |
|-------|------|
| 1 | Manually set `seq_perTrackPattern[0] = 1` while playing Scene 0. Track 1 should play Scene 1's sequence with Scene 1's track length and step data. Other tracks unaffected. |
| 1 | Verify `seq_perTrackActive` is recomputed correctly: set one override → flag = 1; clear all → flag = 0; scene switch → flag = 0. |
| 1 | Verify record-arm is refused when `seq_perTrackActive` is nonzero. |
| 2 | Call `preset_startSingleVoiceApply(0, 3)` during playback. Track 1 should swap to Scene 3's instrument using the deferred quiet-wait. Other voices unaffected. |
| 2 | Verify that a single-voice apply supersedes a running drumset worker for the same slot: start a Scene switch, then immediately do a per-track assign on one slot. The per-track assign's image should win. |
| 3 | Hold VOICE 1 + press SEQ 5: track 1 should switch to Scene 5. Release VOICE 1: no mute toggle. |
| 3 | Hold VOICE 1, press MODE button, release VOICE 1: no mute toggle (cancelled). |
| 3 | Press VOICE 1 and release without pressing SEQ or other buttons: track 1 mute toggles. |
| 3 | Double-click SEQ in PERF: all tracks realign. |
| 3 | PAR_FOLLOW off, single-click SEQ: view changes, playback unchanged. |
| 3 | PAR_FOLLOW off, double-click SEQ: full scene change + coalesce + realign. |
| 4 | Active Scene LED pulses with tempo during playback. |
| 4 | Hold VOICE 1 in PERF: the SEQ LED of track 1's played Scene blinks. Release: LED restored. |
| 5 | With PAR_FOLLOW on and track 1 playing from Scene 3: press VOICE 1 in VOICE mode → view switches to Scene 3. |

---

## Implementation Progress Log

Maintained by the implementing session. Each entry records what was applied,
any deviation from the schedule above with its reason, and the build result.

### Phase 1 — Sequencer per-track read path (DONE, build PASS)

Applied to `Core/Sequencer/sequencer.c` and `sequencer.h`:

- Added `seq_perTrackActive` (1 B SRAM1) and the static
  `seq_recomputePerTrackActive()` helper; updated the stub comment on
  `seq_perTrackPattern[]` to describe the live per-track semantics.
- Added public APIs `seq_setTrackPlayedScene()` (HiHat pair 5+6 linked),
  `seq_clearPerTrackOverrides()`, `seq_realignTrackToMasterClock()`, and the
  read accessor `seq_getTrackPlayedScene()`.
- `seq_init()`, `seq_selectActivePattern()`, `seq_alignActivePatternToScene()`,
  `seq_handleMasterBoundary()` now clear `seq_perTrackActive` after their
  existing all-track coalescing loops.
- `seq_advanceTrackStep()` reads region/specials/step-active and live-erase from
  `seq_perTrackPattern[track]`.
- `seq_queueStepAutomations()` reads the automation region from
  `seq_perTrackPattern[track]`.
- `seq_queueEffectStepMarker()` gained an effect-type gate (new static helper
  `seq_trackEffectTypeMatchesActive()`), applied only when
  `seq_perTrackActive` is set.
- `seq_triggerVoice()` / `seq_previewVoice()` read MIDI channel/note from the
  track's played Scene.
- `seq_realignActivePatternToMasterClock()` reads each track's own region.
- `seq_drainPendingAutomation()` derives `owner_track`/`owner_scene` from the
  packed identity, resolves voice-scoped instrument validation/image from the
  played Scene, and type-gates block-7 Effect entries.
- `seq_restoreAutomatedParameters()` and `seq_restoreAllAutomation()` read the
  instrument image from the played Scene.
- `seq_setRecordingMode()` refuses to arm when `seq_perTrackActive` is set.

Deviation: none material. The effect-type helper treats a missing effect record
for either Scene as a no-match (drop), which is the conservative choice.

Build (DEV config, `make all`): text=536,592, data=416, bss=427,008. RAM delta
attributable to this phase is +1 B (`seq_perTrackActive`); the larger bss
figure includes the already-committed S077 P1 work.

### Phase 2 — Single-voice instrument apply

Pending: see architecture note below before the code entries.

### Architecture deviation found in Phase 2 (must read)

The schedule's §2.1/§3.5 assume flat runtime mixer mirrors
(`mixer_fxSend[]`, `mixer_fxSendMorph[]`, `mixer_faderSetting[]`, a per-voice
Morph-amount mirror). Those arrays do not exist in this tree. The live mixer
reads FX send, fader mode, and Morph amount every block from retained SceneData
through `scene_getActiveIndex()` (see `mixer_faderGains()` and
`preset_getEffectiveFxSendAmount()`). The faithful adaptation is therefore:

- keep `preset_applyPerVoiceSceneSettings()` as the routing write
  (`mixer_audioRouting[slot]` from the source Scene), and
- resolve FX send / fader mode / Morph amount live from the track's played
  Scene via a new `preset_getSlotPlayedScene(slot)` bridge that reads
  `seq_perTrackPattern[slot]`.

This is a superset of the schedule's intent (settings edits in the played Scene
are reflected without a re-apply) and adds no RAM.

### Phase 2 — Single-voice instrument apply (DONE, build PASS)

Applied to `Core/DSP/Instruments/InstrumentManager.{c,h}`,
`Core/Bank/Scene/Preset/presetManager.{c,h}`,
`Core/Bank/Scene/Preset/presetMorphEngine.{c,h}`, and `Core/DSPAudio/mixer.c`:

- InstrumentManager: `instrumentManager_clearRuntimeModulationTargetsForSlot()`
  (single-source teardown of the two LFO pairs plus velocity),
  `instrumentManager_captureLfoPhaseForSlot()`, and
  `instrumentManager_resetRuntimeSlotFromScene(slot, scene)` (the existing
  `instrumentManager_resetRuntimeSlot()` now delegates to it with the active
  Scene, so active-Scene behaviour is unchanged).
- presetMorphEngine: `presetMorph_writeRuntimeBaseEx()` adds a force flag;
  `presetMorph_applyVoiceNow()` keeps force = 0; new
  `presetMorph_applyVoiceNowFromScene()` uses force = 1 and the retained base
  amount (no active-Scene LFO layer) for a per-track played Scene.
- presetManager: `preset_applyInstrumentRuntimeValueForced()` (bypasses the
  active-Scene write guard); `preset_setSupplementalParameterInternal()` and
  `preset_applyKitVoiceSupplementalInternal()` gained the same force flag;
  `preset_resetAndApplyKitVoiceImage()` now resets from the applied Scene and
  restores the LFO phase for both active and per-track paths;
  `preset_getSlotPlayedScene()`; `preset_applyPerVoiceSceneSettings()`;
  `preset_commitSingleVoiceSlot()`; `preset_tickSingleVoiceApply()`;
  `preset_startSingleVoiceApply()`. `preset_tickDrumsetApply()` services the
  single-voice worker first, `preset_applyWorkersIdle()` also waits for it, and
  `preset_startDrumsetApply()` drops stale single-voice bits on a coalesce.
  `preset_applyDeferredSceneSlotForTrigger()` force-commits a pending
  single-voice slot before its note fires.
- mixer.c: `mixer_faderGains()` is called with `preset_getSlotPlayedScene(slot)`
  so per-voice FX send, fader mode, and Morph amount follow the played Scene.
  The bus compressor stays on `scene_getActiveIndex()`.

Deviation from the schedule text: `preset_resetAndApplyKitVoiceImage()` no
longer skips the runtime reset for a non-active Scene (that guard was the
active-Scene-only assumption the per-track apply must break). The LFO phase
restore is likewise unconditional. These are behaviour-neutral for the existing
active-Scene callers.

Known limitation (documented in the code comments): InstrumentManager expands
Scene-namespace LFO/velocity target tokens through `scene_getActiveIndex()` in
`instrumentManager_writeRuntimeInternal()`. A per-track played Scene whose LFO
target uses the Scene namespace therefore resolves against the active Scene's
target table. Voice-namespace targets (the common case) resolve slot-relative
and are correct. See plan Q13.

### Phase 3 — Button handler / PERF gestures (DONE, build PASS)

Applied to `config.h` and `Core/Hardware/frontPanel/buttonHandler.c`:

- `config.h`: `DOUBLE_CLICK_TIMEOUT` (300 ms, below 32,768 for wrap-safe 16-bit
  comparison).
- buttonHandler: `perf_heldVoiceMask`, `perf_voiceActionOccurred`, and the
  general-purpose `dblclick_onPress()` / `dblclick_cancel()` detector. The
  PERF VOICE press now arms the hold (no immediate mute); the plain-PERF
  press-time mute and the SHIFT+PERF cumulative unmute are removed. PERF SEQ
  presses route to `buttonHandler_perfVoiceHeldSeqPressed()` when a VOICE is
  held and to `buttonHandler_perfScenePressed()` otherwise. VOICE release runs
  the single-track mute toggle unless the hold was cancelled. `processPress()`
  feeds the detector and marks unrelated presses; `processRelease()` handles the
  PERF VOICE release; the event-overflow reconciliation clears all three.

Deviation: the schedule's §3.3 only shows removing the cumulative-unmute block,
but in this tree the plain PERF VOICE mute is a separate earlier branch
(`muteModeActive`). The implemented gesture moves both behind the new hold/release
model to deliver the §4.1 behaviour. The double-click result is derived at the
call site (`!dblclick_armed && dblclick_button == buttonNr`) rather than stored,
saving a byte.

### Phase 4 — LED feedback (DONE, build PASS)

Applied to `Core/Menu/menu.c` and `Core/Hardware/frontPanel/ledHandler.c`:

- `menu_refreshPerfSceneLeds()`: the active Scene LED uses `seq_ledState.beatPulse`
  while running (tempo pulse) and the standard blink when stopped; a viewed
  Scene that differs from the active Scene while PAR_FOLLOW is off gets the
  blink indication.
- `led_processSeqLedState()`: the BEAT drain refreshes the PERF Scene row while
  in PERF mode so the pulse animates.
- buttonHandler: `buttonHandler_perfRefreshHeldSceneLeds()` blinks the SEQ LED of
  each held track's played Scene during a PERF VOICE hold and restores the row
  on the last release.

Deviation: the LED engine has a single persistent blink cadence
(`LED_BLINK_TIME_MS`, 250 ms) and no separate rapid rate; §5.2 is delivered as
blink-versus-pulse rather than a distinct fast cadence. No new LED-engine state
was added. While the transport runs, the per-beat PERF refresh also clears the
transient held-Scene blink; when stopped (the normal assignment context) the
held indication persists.

### Phase 5 — PAR_FOLLOW view-follows-track (DONE, build PASS)

- `handleVoiceButton()` VOICE-mode selection: when PAR_FOLLOW is on,
  `seq_perTrackActive` is set, and the selected track plays from a Scene other
  than the viewed one, `menu_setShownPattern()` moves the view to the played
  Scene. The retained Scene write target still follows the active Scene's edit
  mask; this phase moves the view only (plan §6.1).

### Final build

DEV config, `make all`: text=538,544, data=420, bss=427,008.
Flash 538,964 / 753,664 B (headroom 214,700 B). `link_budget.py` reports no
warning. RAM delta versus the committed S077 P1 baseline (bss=427,008) is 0 B
at the reported total: the 8 B of new static storage
(`seq_perTrackActive` 1 B, `single_voice_pending_mask` 1 B,
`perf_heldVoiceMask` 1 B, `perf_voiceActionOccurred` 1 B, `dblclick_button` 1 B,
`dblclick_deadline` 2 B, `dblclick_armed` 1 B) fit inside the existing 8-byte
.bss alignment slack recorded in S077 P1 (+264 B measured for a +256 B table).
This is 1 B above the schedule's 7-byte table because of
`single_voice_pending_mask`, the Phase 2 deferred mask the schedule describes in
prose but omitted from its RAM table.

---

## Code Review Assessment

Review performed against the full diff of 16 files, 1,483 insertions, 110
deletions.  Each phase assessed against the schedule above and the plan in
`S077_P2_PER_TRACK_PLAYBACK.md`.

### Summary verdict

The implementation faithfully covers all 50 change-matrix entries.  One
confirmed bug (double-click timing) and one documentation inconsistency were
found; remaining deviations from the schedule are sound adaptations documented
in the progress log.

### Phase 1 — Sequencer per-track read path

**All 20 schedule entries present.**

- `seq_perTrackActive`, `seq_recomputePerTrackActive()`, public APIs
  (`seq_setTrackPlayedScene`, `seq_clearPerTrackOverrides`,
  `seq_realignTrackToMasterClock`) are correctly placed and documented.
- Bonus accessor `seq_getTrackPlayedScene()` added (not in schedule); used by
  Phase 2's `preset_getSlotPlayedScene()` bridge and Phase 3's held-Scene LED
  repaint.  Clean addition.
- `seq_advanceTrackStep()` ISR changes (lines 907, 928–932): all four
  `seq_activePattern` references switched to `seq_perTrackPattern[track]`.
  Single-byte SRAM1 read, atomic on Cortex-M7.  ISR-safe.
- Erase path (lines 939–942): changed to `seq_perTrackPattern[track]`.  The
  original schedule said erase should write to the active Scene; the
  implementation (and progress log) instead writes to the playing Scene.  This
  matches the plan's later decision that erase targets the audible data —
  correct.
- Effect type gate: the schedule's §1.8 is implemented as a static helper
  `seq_trackEffectTypeMatchesActive()` plus a gate in both
  `seq_queueEffectStepMarker()` (ISR) and `seq_drainPendingAutomation()`
  (foreground).  The ISR cost is two const-pointer reads gated behind
  `seq_perTrackActive`; acceptable.  `NULL` records treated as no-match (drop
  marker) is the conservative choice and cannot produce a false positive.
- `seq_drainPendingAutomation()`: the owner-track derivation
  `(identity & STEP_ID_MASK) / NUM_STEPS` is correct and matches the existing
  `effects_automationStepBegin()` derivation it replaced inline.
- Coalesce sites (`seq_selectActivePattern`, `seq_alignActivePatternToScene`,
  `seq_handleMasterBoundary`, `seq_init`): all set `seq_perTrackActive = 0`
  after their existing all-track loops.  Correct.
- `seq_setRecordingMode()`: gate added.  Disarm (`active == 0`) is always
  honoured; arming is refused when overrides exist.  Correct per plan §3.7.

**No issues found in Phase 1.**

### Phase 2 — Single-voice instrument apply

**All 7 schedule entries present, with documented architectural deviation.**

The deviation (no flat mixer mirrors — FX send, fader mode, and Morph amount
read live from SceneData through `preset_getSlotPlayedScene()`) is a sound
adaptation.  It is a superset of the schedule's intent: live edits in the played
Scene are reflected without a re-apply, and it adds zero RAM.

- `instrumentManager_clearRuntimeModulationTargetsForSlot()`: correctly tears
  down the two LFO pair destinations and velocity source for one slot using the
  existing `instrumentManager_restoreLfoSupplementalTarget()` and
  `modNode_clearDestination()` primitives.  Other slots untouched.
- `instrumentManager_captureLfoPhaseForSlot()`: snapshots one slot's running
  LFO phase into the shared handoff struct.  Marks `valid = 1` so
  `restoreLfoPhaseIfNeeded` can restore it.
- `instrumentManager_resetRuntimeSlotFromScene()`: extracted from
  `resetRuntimeSlot()`; the old function now delegates to it with
  `scene_getActiveIndex()`.  Behaviour-neutral for existing callers.
- `preset_startSingleVoiceApply()`: clears the drumset worker's bit (supersede
  coordination), captures LFO phase, clears modulation targets, applies routing,
  and arms `single_voice_pending_mask`.  Correct.
- `preset_tickSingleVoiceApply()`: round-robin scan with quiet-wait, one slot
  per pass.  Serviced before the drumset worker in `preset_tickDrumsetApply()`.
  Correct.
- `preset_startDrumsetApply()`: clears `single_voice_pending_mask = 0` on
  coalesce.  Correct — a scene-level switch supersedes any pending per-track
  apply.
- `preset_applyDeferredSceneSlotForTrigger()`: the single-voice force path runs
  before the drumset force path, with the correct track-to-slot mapping
  (`trigger_track >= INSTRUMENT_SLOT_COUNT` → slot 5 for the HiHat pair).  The
  guard `trigger_track <= INSTRUMENT_SLOT_COUNT` (i.e. ≤ 6) passes all 7
  valid tracks.
- Morph engine: `presetMorph_applyVoiceNowInternal()` added with a
  `force_runtime` flag; the LFO Morph layer is correctly gated to the active
  Scene only.  `presetMorph_writeRuntimeBaseEx()` routes through
  `preset_applyInstrumentRuntimeValueForced()` which bypasses the active-Scene
  write guard.  The non-force path is behaviour-neutral: the guard simply
  moved from the internal function to the `Ex` wrapper.
- Mixer: `mixer_faderGains()` now receives `preset_getSlotPlayedScene(slot)`.
  The bus compressor remains on `scene_getActiveIndex()`.  Correct.
- `preset_resetAndApplyKitVoiceImage()`: the active-Scene guard on
  `instrumentManager_resetRuntimeSlot()` is removed; it now unconditionally
  calls `resetRuntimeSlotFromScene(voice, scene_index)`.  This is correct:
  the per-track path must reset the slot from the played Scene.  Existing
  active-Scene callers pass the active Scene, so behaviour is unchanged.
  Similarly, the LFO phase restore is now unconditional — correct, because the
  slot reset zeroes the phase in both paths.
- Known limitation documented: Scene-namespace LFO/velocity target tokens
  resolve through `scene_getActiveIndex()` in InstrumentManager's token
  expansion.  Voice-namespace targets (the common case) are correct.  Matches
  plan Q13.

**No issues found in Phase 2.**

### Phase 3 — Button handler / PERF gestures

**All 10 schedule entries present.**

- `perf_heldVoiceMask`, `perf_voiceActionOccurred`: correctly declared as
  static module-scope variables.
- `dblclick_onPress()` / `dblclick_cancel()`: general-purpose detector.
  `processPress()` feeds every button press into the detector and cancels on
  non-SEQ presses.  VOICE presses also cancel the window (via
  `dblclick_cancel()` in the PERF VOICE arm path).  Non-VOICE/non-SEQ presses
  while a VOICE is held set `perf_voiceActionOccurred = 1`.  Correct.
- PERF VOICE press: arms the hold, shows transient LED indication.  The old
  cumulative unmute block is removed.  The old single-track press-time mute
  (the `muteModeActive` branch for PERF) is now bypassed by the early return.
  Both old behaviours replaced by the hold/release model.  Correct deviation
  from the schedule, which only removed the cumulative-unmute block.
- PERF VOICE release (`buttonHandler_perfVoiceReleased`): clears the mask bit,
  toggles mute if `perf_voiceActionOccurred == 0`, resets the flag when the
  last VOICE is released.  Each VOICE fires independently (Q12).  Correct.
- PERF SEQ routing: `perf_heldVoiceMask != 0` routes to per-track assignment,
  else routes to scene-with-double-click.  Correct.
- `buttonHandler_perfVoiceHeldSeqPressed()`: iterates all 7 tracks in the mask,
  validates the Scene with `bank_scenePresent()`, calls
  `seq_setTrackPlayedScene` + `seq_realignTrackToMasterClock` +
  `preset_startSingleVoiceApply` per track.  Track-to-slot mapping correct
  (track ≥ 6 → slot 5).  Sets `perf_voiceActionOccurred = 1`.  Correct.
- `buttonHandler_perfScenePressed()`: PAR_FOLLOW on/off × single/double-click
  matrix implemented.  Correct.
- Event overflow reconciliation: `perf_heldVoiceMask`,
  `perf_voiceActionOccurred`, and `dblclick_armed` all cleared.  Prevents
  stranded state.  Correct.
- View-follows-track (Phase 5, §5.1): implemented in `handleVoiceButton()`
  VOICE-mode selection.  Guarded by `parameter_values[PAR_FOLLOW]` and
  `seq_perTrackActive`.  Correct.

**One confirmed bug found:**

**`dblclick_onPress()` timing comparison is inverted (line 432).**

The condition `(uint16_t)(time_sysTick - dblclick_deadline) < 32768u` is the
wrap-safe idiom for "the deadline has passed" — confirmed by the identical
pattern on line 731 (`buttonHandler_tick()` hold timer), where it correctly
fires AFTER `buttonHandler_buttonTimer` has elapsed.

In the double-click detector, `dblclick_deadline` is set to
`time_sysTick + DOUBLE_CLICK_TIMEOUT` (a future point).  The condition on
line 432 is TRUE when the deadline has passed (window expired) and FALSE when
still within the window.  This is backwards: the double-click should be
detected when the second press arrives BEFORE the deadline, not after.

Effect: fast double-clicks (< 300 ms) are treated as two separate first clicks;
slow second presses (> 300 ms, up to ~32 s) are treated as double-clicks.  The
realignment gesture is unreachable with fast taps and falsely triggered with
slow presses.

Fix: change the comparison to check that the deadline has NOT passed:

```c
/* line 432: */
(uint16_t)(dblclick_deadline - time_sysTick) < 32768u
```

This inverts the half-ring test so the condition is TRUE when `time_sysTick` is
still before `dblclick_deadline` (within the window).

### Phase 4 — LED feedback

**All 3 schedule entries present.**

- Active Scene tempo pulse: `menu_refreshPerfSceneLeds()` uses
  `seq_ledState.beatPulse` while running, standard blink while stopped.  The
  `led_processSeqLedState()` BEAT drain calls `menu_refreshPerfSceneLeds()` in
  PERF mode to drive the animation.  Correct.
- Viewed-scene indication: blink when `!PAR_FOLLOW` and `viewed != active`.
  The schedule called for "rapid flash"; the implementation uses the LED
  engine's single persistent blink cadence (`LED_BLINK_TIME_MS`).  The
  progress log documents this: no separate rapid rate exists.  The
  blink-versus-pulse difference is still visually distinct.  Acceptable.
- Transient held-Scene indication: `buttonHandler_perfRefreshHeldSceneLeds()`
  blinks each held track's played Scene's SEQ LED, restores normal PERF LEDs
  on last release.  Correct.

**No issues found in Phase 4.**

### Phase 5 — PAR_FOLLOW view-follows-track

**Schedule entry present.**

- `handleVoiceButton()` VOICE-mode selection: when PAR_FOLLOW is on and the
  selected track's played Scene differs from the viewed Scene,
  `menu_setShownPattern()` moves the view.  Guarded by `seq_perTrackActive`
  to avoid overhead when no overrides exist.  Correct.

**No issues found in Phase 5.**

### Cross-cutting observations

- **RAM budget**: 8 B new static storage, 1 B above the schedule's 7 B table
  due to `single_voice_pending_mask`.  Fits within the existing .bss alignment
  slack.  No concern.
- **ISR safety**: all `seq_perTrackPattern[]` reads in `seq_advanceTrackStep()`
  are single-byte SRAM1 loads, atomic on Cortex-M7.  The effect-type gate adds
  two const-pointer reads gated behind `seq_perTrackActive`.  No new locks or
  barriers required.
- **DSP CPU**: `preset_getSlotPlayedScene()` adds one function call (likely
  inlined) per slot per audio block in the mixer loop.  Constant cost.  Meets
  the DSP CPU policy.
- **Code documentation**: every change carries a comment block matching the
  schedule's specification.  Internal deviations are documented in the
  progress log.
- **Build**: DEV config, text=538,544, data=420, bss=427,008.  Flash
  headroom 214,700 B.  No linker warnings.

### Action taken

1. **Fixed** the inverted double-click timing comparison on `buttonHandler.c`
   line 432.  Changed `(uint16_t)(time_sysTick - dblclick_deadline) < 32768u`
   to `(uint16_t)(dblclick_deadline - time_sysTick) < 32768u`.  The corrected
   half-ring test is TRUE when `time_sysTick` is still before
   `dblclick_deadline` (within the window), matching the detector's intent.
   Retest checklist items 6.23–6.27 are now unblocked.

