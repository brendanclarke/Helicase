# S077 Per-Track Scene Playback

## Feature Summary

Split the concept of "active scene" from "played scene" so that individual
tracks can play back their sequence, instrument, and voice parameters from a
scene other than the globally active one.  The active scene remains the master
for effect/bus parameters and the default playback source; per-track overrides
let the user mix sequences and instruments from different scenes in a single
performance.

---

## 1  Semantic Definitions

### 1.1  Active Scene

The scene selected in PERF mode via a SEQ button press.  It is:

- the scene whose **effect, bus compressor, and non-track scene parameters**
  are always used for playback (these cannot be overridden per-track);
- the scene that owns the **master step clock** (bar counter, beat pulse,
  transport boundaries);
- the target of `scene_selectActive()`, `seq_selectActivePattern()`, and
  `preset_startDrumsetApply()`;
- blinked on its SEQ LED during playback (tempo-pulse on the active scene LED,
  inverted play LED style).

### 1.2  Viewed Scene

The scene whose track parameters and steps are presented in VOICE and STEP
modes, and whose pattern is the target of edits.

- If **PAR_FOLLOW is on**: viewed = active always; a single SEQ press in PERF
  changes both active and viewed, and write access follows immediately (once
  pending writes on the prior scene conclude).
- If **PAR_FOLLOW is off**: a single SEQ press in PERF changes the **viewed**
  scene only (navigates the UI without changing playback).  A double-click
  changes viewed, active, and playback together.

### 1.3  Played Scene (per track)

The scene from which a specific track draws its **sequence data** (step
pattern), **instrument/voice parameters** (including dynamic-deferred voice
switching), and **per-voice scene settings** (fader, fx send, output routing,
morph amount, MIDI channel/note).

- Default: the active scene (i.e. `seq_perTrackPattern[track] ==
  seq_activePattern`).
- Override: set by holding a VOICE button in PERF mode and pressing a SEQ
  button representing the desired scene.
- Tracks 6 and 7 always switch together (HiHat choke pairing).

### 1.4  Compound State

At any moment the system has:

| State         | Count | Scope                     |
|---------------|-------|---------------------------|
| Active scene  | 1     | Global                    |
| Viewed scene  | 1     | Global (UI)               |
| Played scene  | 7     | Per-track (6+7 linked)    |

When no per-track override is set, all seven played-scene entries equal the
active scene and the system behaves exactly as today.

---

## 2  Current Architecture (baseline)

### 2.1  Scene/Pattern index chain

```
scene_active_index          (SceneData.c — identity only)
  └─ seq_activePattern      (sequencer.c — playback read index)
       └─ seq_perTrackPattern[7]   (sequencer.c — stub, all == activePattern)
```

`seq_advanceTrackStep()` currently reads from `seq_activePattern` directly for
`pat_isStepActive()`, `pat_sceneRegion()` (track length), and
`pat_readStepSpecials()`.  It does NOT use `seq_perTrackPattern[]`.

`seq_triggerVoice()` reads MIDI channel/note from `seq_activePattern`.

`seq_realignActivePatternToMasterClock()` reads region from `seq_activePattern`.

### 2.2  Voice/instrument ownership

Voices are DSP-runtime slots.  The instrument playing at slot N is whichever
`kit_instrument_slot_t` image was last applied by
`preset_resetAndApplyKitVoiceImage()`.  There is one set of six live instrument
slots — not per-scene.  Scene switching runs the deferred
`preset_startDrumsetApply()` → `preset_tickDrumsetApply()` worker, which waits
for each voice's envelope to go quiet before hot-swapping its instrument image
and rebinding modulation.

### 2.3  Scene settings used during playback

| Setting category         | Source                          | Per-track? |
|--------------------------|---------------------------------|------------|
| Step pattern / automation| PatternData via scene region    | Will be    |
| Instrument image         | scene_t.kit.instruments[slot]   | Will be    |
| Track length / scale     | pat_scene_region_t              | Will be    |
| Fader, FX send, output   | scene_settings_t                | Will be    |
| Morph amount (per-voice) | scene_settings_t                | Will be    |
| MIDI channel / note      | scene_settings_t                | Will be    |
| Effect / FX sequence     | scene_t.effect                  | No (active)|
| Bus compressor           | scene_settings_t.bus_comp       | No (active)|
| Global morph amount      | scene_settings_t.morph_amount   | No (active)|
| Effect morph amount      | scene_settings_t.effect_morph   | No (active)|

### 2.4  PERF button handling

`buttonHandler_seqButtonPressed()` → `menu_perfModeSceneButtonPressed()` →
`scene_selectActive()` + `seq_selectActivePattern()` +
`preset_startDrumsetApply()`.

No hold-track-then-press-scene gesture exists today.  PERF VOICE buttons
currently do mute-clear (unmute voices 0..N).

---

## 3  Design

### 3.1  New per-track played-scene state

```c
// sequencer.c — existing stub, repurposed:
uint8_t seq_perTrackPattern[NUM_TRACKS];   // played scene per track

// new: whether any track differs from active
uint8_t seq_perTrackActive;                // nonzero if any override is set
```

`seq_perTrackActive` is a derived convenience flag, recomputed whenever
`seq_perTrackPattern[]` changes.  It gates the fast path: when zero, all
existing code paths that reference `seq_activePattern` are correct as-is.

### 3.2  Sequencer changes

**`seq_advanceTrackStep()`**: Replace `seq_activePattern` reads with
`seq_perTrackPattern[track]` for:

- `pat_sceneRegion()` → track length, step activity, step specials
- `pat_isStepActive()` → step trigger check
- `pat_readStepSpecials()` → velocity, note, probability
- `seq_queueStepAutomations()` → per-track automation blocks
- `seq_queueEffectStepMarker()` → apply if effect type matches active scene's
  effect type; drop if types differ (see §3.6)

**`seq_triggerVoice()`**: Use `seq_perTrackPattern[voiceNr]` for MIDI
channel/note lookup instead of `seq_activePattern`.

**`seq_realignActivePatternToMasterClock()`**: Each track reads its own played
scene's region for track length: `pat_sceneRegion(seq_perTrackPattern[track])`.

**New: `seq_setTrackPlayedScene(track, scene)`**: Sets one track's played scene
and recalculates `seq_perTrackActive`.  Tracks 5 and 6 are linked (set
together).

**New: `seq_realignTrackToMasterClock(track)`**: Realigns a single track's step
position against the master clock using its own played scene's track length.

**New: `seq_clearPerTrackOverrides()`**: Resets all entries to `seq_activePattern`
and clears `seq_perTrackActive`.

### 3.3  Realignment formula

The realignment formula for a track with length L and the master at absolute
step M:

```
realigned_position = M % L
```

This is exactly what `seq_realignActivePatternToMasterClock()` already does,
using each track's own played scene's `track_length`.  No loop counters are
needed — the master step clock is the sole time reference.

### 3.4  Instrument / voice switching for per-track override

When a track is assigned to play from a different scene, its voice slot must be
loaded with that scene's instrument image and settings.  This is a **single-slot
variant** of `preset_startDrumsetApply()`.

**New: `preset_startSingleVoiceApply(uint8_t slot, uint8_t source_scene)`**:

1. If the drumset worker is active and this slot is pending, clear this slot's
   bit from `drumset_apply_pending_mask` — the drumset worker will skip it.
2. Capture LFO phase for the slot (snapshot before reset).
3. Clear runtime modulation targets for that slot only.
4. Apply per-voice scene settings from the source scene (fader, FX send,
   output, morph amount).
5. Arm a single-slot pending mask for the deferred apply.
6. The deferred quiet-wait and image swap reuse the same envelope-quiet
   mechanism as the drumset worker.
7. After commit: if the drumset worker is still running, its final modulation
   rebind pass will include this slot.  If the drumset worker is idle, the
   single-voice apply runs its own modulation setup for the committed slot.

**Coexistence rule (decided)**: the single-voice apply supersedes the drumset
worker for the affected slot.  The drumset worker's per-slot independence
(round-robin scan, per-slot pending bits) makes this safe — clearing a bit
from `drumset_apply_pending_mask` is the only coordination needed.

For tracks 5+6 (HiHat pair), both slots switch together in one call.

### 3.5  Per-voice scene settings application

When a track plays from a non-active scene, its voice-level scene settings must
come from the source scene rather than the active scene:

- `mixer_audioRouting[slot]` ← source scene's `audio_out[slot]`
- `mixer_fxSend[slot]` ← source scene's `fx_send_amount[slot]`
- `mixer_fxSendMorph[slot]` ← source scene's `fx_send_morph[slot]`
- `mixer_faderSetting[slot]` ← source scene's `fader_setting[slot]`
- per-voice morph amount ← source scene's `voice_morph_amount[slot]`

These are applied once at switch time by the single-voice apply function, and
re-applied if the source scene's settings are edited (if PAR_FOLLOW routes
editing to the played scene).

**Not per-track** (always from the active scene): the Effect record, bus
compressor, global morph amount, effect morph amount.

### 3.6  Automation handling (decided)

Step automation entries from a track's played scene are queued and drained
normally.  Resolution rules:

**Voice-scoped targets** (voice morph, FX send, output, instrument parameters):
applied if the target parameter exists on the currently loaded instrument in
that slot.  Dropped only if there is no matching parameter (e.g. the instrument
type differs between the automation's authored scene and what's actually loaded,
though in practice the instrument IS from the track's played scene, so matches
are expected).  It is left to the user to evaluate whether cross-scene voice
automation sounds musical; it should not break anything.

**Effect/FX-sequence targets**: applied if the active scene's effect type
matches the track's played scene's effect type.  Dropped if the types differ.
The FX sequencer itself always runs from the active scene; per-track FX step
markers apply only when the type check passes.

**Bus compressor and other scene-global targets**: applied to the active scene's
globals.  The automation value is authored for the track's source scene but
acts on the audible active scene.  Dropped only if the parameter doesn't exist
(which for bus compressor it always does — four fixed fields).

`seq_applySceneAutomation()` already resolves targets through
`sceneModTarget_descriptor()`.  The per-track effect-type check is a new gate
added to the drain path: compare `scene_effectConst(track_played_scene)->type`
against `scene_effectConst(active_scene)->type` before applying effect-scoped
entries.

### 3.7  Live record disable

`seq_recordActive` is refused when `seq_perTrackActive` is set.  The record-arm
UI should grey out or not toggle when per-track overrides exist.  Rationale:
recording into a track whose played scene differs from the viewed scene would
write steps into the wrong scene's pattern data.

---

## 4  User Interaction

### 4.1  Setting per-track playback (PERF mode)

**Gesture: hold VOICE + press SEQ**

1. User holds a VOICE button (track 1–7) in PERF mode.
2. While held, user presses a SEQ button (scene 0–15).
3. That track switches to play from the selected scene.
4. Tracks 6 and 7 always switch together.

**VOICE mute behavior in PERF (changed)**: the old cumulative unmute gesture
(VOICE press unmutes tracks 0..N) is removed.  PERF VOICE buttons now behave
as **single-track mute toggles on release**:

- VOICE press: arms hold state (no immediate action).
- If a SEQ button is pressed during the hold: per-track assignment occurs;
  mute is cancelled — no toggle on release.
- If another VOICE button is pressed during the hold: it does NOT cancel the
  mute gesture (multi-track holds are permitted — see §10 Q11).
- If any other button (non-VOICE, non-SEQ) is pressed during the hold: mute
  is cancelled.
- VOICE release with no cancelling action: toggle mute for that single track.

**Implementation**: `buttonHandler.c` tracks a `perf_heldVoiceMask` bitmask.
While any VOICE bit is set in PERF mode, SEQ presses are intercepted for
per-track assignment.  A `perf_voiceActionOccurred` flag cancels the mute
toggle.

### 4.2  Realignment gestures

**Double-click scene (no voice held)**:

- All tracks realign to the master step clock.
- Each track's step position is set to `masterStepClock % track_length` using
  its own played scene's track length.

**Hold VOICE + double-click scene**:

- Switch that track to play from the scene if not already.
- Realign just that track vs. the master step clock.

**Double-click detection (general-purpose)**: Uses the existing button timer
infrastructure, extended for double-click.  Only one button can be in a
double-click window at a time; any action other than the same button being
pressed again cancels the window.  The first click fires immediately (no
latency on single-click actions); the second click within the timeout fires the
double-click action additively.  Timeout is defined as `DOUBLE_CLICK_TIMEOUT`
in `config.h`.  The detector is designed to apply to any button, not just PERF
SEQ.

### 4.3  Scene change clears all per-track overrides (decided)

**Any scene-level playback change coalesces all tracks to the new scene.**
This means:

- **PAR_FOLLOW on, single click on any SEQ button** (same or different scene):
  all tracks switch to that scene.  Per-track overrides are cleared.  Realign
  is always performed on change to account for different track settings.  If
  PAR_FOLLOW is on, the new scene is also active, viewed, and editable.

- **PAR_FOLLOW off, single click**: changes the **viewed scene** only.  No
  playback change, no coalescing, no realign.  Per-track overrides remain.

- **PAR_FOLLOW off, double-click**: changes viewed, active, and playback.
  All tracks coalesce to the double-clicked scene.  Per-track overrides are
  cleared.  Realign is performed.

The VOICE hold + SEQ press gesture is a **per-track** assignment, not a
scene-level change.  It does NOT trigger coalescing — only that one track (or
tracks 6+7 for HiHat) is affected.

**Returning a single track to the active scene**: hold that track's VOICE
button and press the active scene's SEQ button.  This sets the track's played
scene to the active scene (equivalent to clearing that track's override).

### 4.4  PAR_FOLLOW off interaction

With PAR_FOLLOW off, the VOICE hold + SEQ press gesture works exactly the same
as with PAR_FOLLOW on — it's a per-track playback change, not a view change.
The VOICE hold takes priority over the normal single-click-to-view behavior:
while a VOICE button is held, a SEQ press is always a per-track assignment
regardless of PAR_FOLLOW.

When PAR_FOLLOW is off and viewing a non-playback scene, the per-track
instrument/sequence differences are audible but the viewed scene is what's
shown in VOICE/STEP modes.  The viewed scene is always editable.

Future note: when live record is implemented, if PAR_FOLLOW is off and the
viewed scene differs from the playback scene, arming record should set the
view to the playback scene so that recording targets the correct pattern data.

---

## 5  LED Feedback

### 5.1  Active scene LED

- **During playback**: the active scene's SEQ LED pulses with the tempo (like
  the play LED but inverted — on at beat boundaries, off between).
- **When stopped**: the active scene's SEQ LED continues its tempo pulse
  (blinking at the last-known tempo).

### 5.2  Viewed scene LED

- If viewed scene **differs** from the active scene (PAR_FOLLOW off): the
  viewed scene's SEQ LED flashes rapidly (faster than tempo pulse).
- If viewed == active: the tempo pulse takes precedence (no extra flash).

### 5.3  Per-track playback indication (decided: none)

Per-track scene assignments have **no persistent LED indication** anywhere on
the interface.  No VOICE LED blink, no SEQ LED state, no status indicator.
The audible difference is the only indicator.  This applies regardless of
PAR_FOLLOW setting.

### 5.4  During VOICE hold in PERF

When holding a VOICE button in PERF for per-track assignment, the scene that
the held track is currently playing from blinks on its SEQ LED.  This is a
transient indicator visible only during the hold gesture — it disappears when
the VOICE button is released.  This allows the user to see which scene a track
is assigned to before changing it.

---

## 6  Impact on Other Subsystems

### 6.1  PAR_FOLLOW and VOICE/STEP mode

When PAR_FOLLOW is on and per-track playback is active:

- VOICE and STEP modes should show the settings of the **actually playing**
  track, not just the active scene.
- The writable scene should follow the active track's played scene so the user
  can edit parameters of the instrument that's actually sounding.
- When the user selects a track (VOICE press in VOICE mode), if that track's
  played scene differs from the current viewed scene, the viewed scene and
  write target switch to that track's played scene.

When PAR_FOLLOW is off:

- The viewed scene is always represented in the UI, regardless of per-track
  overrides.
- Per-track instrument/sequence differences are audible but not shown in the
  interface.

### 6.2  Copy/clear

Copy/clear operates on the viewed scene (the write target).  No interaction
with per-track playback beyond the write-target rule.

### 6.3  AutoSave

Per-track playback state is **transient performance state**, not saved.
AutoSave does not capture or restore per-track scene assignments.  A power
cycle or preset load returns all tracks to the active scene.

### 6.4  Bank/Scene Load (decided: preserve overrides)

Load operations **preserve** per-track overrides.  A track playing from scene 5
continues to play from scene 5 after a load — now with scene 5's newly loaded
content.  It is the user's responsibility to account for per-track playback
when loading scenes.  The rationale is simplicity and user trust: clearing
overrides on every load would be surprising in a live performance context.

### 6.5  MIDI

Per-track MIDI channel/note comes from the track's played scene, not the active
scene.  MIDI program change sends the active scene only (as today).

### 6.6  Effect automation (decided: type-gated)

The FX sequencer always advances from the active scene's FX sequence.  However,
tracks playing from other scenes **do apply their FX step automation** if the
active scene's effect type matches the track's played scene's effect type.  If
the types differ, per-track FX automation entries are dropped silently.

This means: if scenes A and B both have a StereoFilter effect, track 3 playing
from scene B will apply its FX automation to the active filter.  If scene B has
CrumpBit while scene A has StereoFilter, track 3's FX automation is dropped.

### 6.7  Morph

Voice morph operates on whatever instrument image is loaded in each slot.  If
slot N is loaded from scene B, morph interpolates between scene B's normal and
morph endpoints for that slot.  The global morph amount comes from the active
scene; per-voice morph amounts come from each track's played scene.

---

## 7  RAM/Flash Budget

### 7.1  New state

| Item                              | Size      | Region  |
|-----------------------------------|-----------|---------|
| seq_perTrackActive (flag)         | 1 B       | SRAM1   |
| perf_heldVoiceMask (button)       | 1 B       | SRAM1   |
| perf_voiceActionOccurred (flag)   | 1 B       | SRAM1   |
| double-click detector state       | ~4 B      | SRAM1   |
| **Total new RAM**                 | **~7 B**  | SRAM1   |

`seq_perTrackPattern[7]` already exists (7 B, allocated in S067).

### 7.2  Flash estimate

Per-track playback logic, button handling, LED code, preset single-voice apply:
estimated 2–4 KB flash.  Current headroom: ~217 KB.  Well within budget.

---

## 8  Implementation Phases

### Phase 1: Sequencer per-track read path

- Make `seq_advanceTrackStep()` use `seq_perTrackPattern[track]` instead of
  `seq_activePattern` for pattern reads, step checks, and specials.
- Make `seq_triggerVoice()` use `seq_perTrackPattern[voiceNr]` for MIDI.
- Make `seq_realignActivePatternToMasterClock()` use per-track played scene.
- Add `seq_setTrackPlayedScene()`, `seq_realignTrackToMasterClock()`,
  `seq_clearPerTrackOverrides()`.
- Add `seq_perTrackActive` flag and update it in all relevant paths.
- Existing scene-switch paths (`seq_selectActivePattern`, etc.) continue to
  set all entries, so behavior is unchanged when no override is set.
- Gate `seq_recordActive` on `!seq_perTrackActive`.

### Phase 2: Single-voice instrument apply

- Factor `preset_startSingleVoiceApply()` from the existing drumset worker.
- Per-voice scene-settings application (fader, FX send, output, morph).
- Track 5+6 linked switching.
- Test: calling the new function for one track while playback runs should
  swap that track's instrument using the deferred quiet-wait path.

### Phase 3: Button handler / PERF gestures

- General-purpose double-click detector: `DOUBLE_CLICK_TIMEOUT` in `config.h`,
  uses existing button timer infrastructure, applicable to any button.
- Implement hold-VOICE-press-SEQ per-track assignment gesture.
- Replace PERF VOICE cumulative-unmute with single-track mute toggle on
  release (cancelled by non-VOICE/non-SEQ actions during hold).
- Scene-level SEQ press coalesces all tracks + realigns (PAR_FOLLOW on: single
  click; PAR_FOLLOW off: double-click).
- PAR_FOLLOW off: single-click = view only, double-click = full change.
- Double-click realignment (active scene and per-track hold variants).
- Live record gate.

### Phase 4: LED feedback

- Active scene tempo pulse (inverted play style).
- Viewed ≠ active flash.
- Held-voice transient scene indication (blink track's current played scene
  during VOICE hold in PERF).
- No persistent per-track override indication (decided Q5).

### Phase 5: PAR_FOLLOW view-follows-track

- When PAR_FOLLOW is on and the user selects a track, switch the viewed scene
  and write target to that track's played scene.
- VOICE and STEP modes display the played track's scene parameters.

---

## 9  Risks

### 9.1  ISR safety

`seq_advanceTrackStep()` runs in TIM3 ISR (priority 2).  The per-track pattern
array is SRAM1 and already accessed there.  No new allocation or I/O is
introduced in the ISR path — only changing which index is read.  The foreground
assignment writes and the ISR reads are both single-byte, so atomicity is
guaranteed on ARM Cortex-M7.

### 9.2  Instrument apply during playback

The deferred voice-swap worker waits for envelope quiet before applying.  A
per-track switch uses the same mechanism.  Risk: if the user switches multiple
tracks rapidly, multiple pending single-voice applies may overlap.

**Decided mitigation (Q6-b)**: the single-voice apply supersedes the drumset
worker for its slot by clearing the corresponding bit in
`drumset_apply_pending_mask`.  The drumset worker already handles slots
independently, so skipping a pre-committed slot is safe.  If the drumset worker
is idle, the single-voice apply manages its own modulation setup.

### 9.3  Edit-mask fan-out

When a track plays from scene B while the active scene is A, edits in VOICE
mode should not fan out from scene A's edit mask to scene B.  The edit-mask
gate in `bank_sceneMaskVoiceEdit()` must not inadvertently write instruments or
parameters into a scene that a track is merely reading for playback.

### 9.4  Automation target aliasing

If a track from scene B fires an automation entry targeting a scene-global
parameter, that parameter belongs to the active scene A, not B.  Voice-scoped
automation from scene B works because the track's instrument IS from scene B
(parameter indices match by construction).  Effect automation is type-gated:
applied if the effect types match, dropped if they differ.  Bus compressor
automation applies to the active scene's compressor regardless of source (four
fixed fields, always present).  The user may hear unexpected results from
cross-scene automation; this is accepted as a creative tool, not a bug.

### 9.5  DSP CPU

No DSP CPU change.  Per-track playback does not add or remove any DSP work; it
only changes which instrument image and parameters a slot uses.  The constant-
CPU policy is preserved.

---

## 10  Decisions and Remaining Questions

### Decided (Q1–Q10)

**Q1 — Automation scope**: (b) applied.  Voice-scoped automation applies if the
target parameter exists on the loaded instrument (matches by construction since
the instrument is from the track's played scene).  Effect automation applies if
the active scene's effect type matches the track's played scene's effect type;
dropped if types differ.  Bus compressor and other scene-global automation
applies to the active scene's globals.  User evaluates whether cross-scene
automation sounds musical; it should not break anything.

**Q2 — Clearing per-track overrides**: (a) any scene-level playback change
(PERF SEQ press without a VOICE hold) coalesces all tracks to the new scene.
Realign is always performed on change.  Per-track overrides are transient and
do not survive a scene change.

**Q3 — Scene change semantics**: folded into Q2.  Any scene-level playback
change moves all tracks to the new scene.  PAR_FOLLOW on: the new scene is
active, viewed, and editable.  PAR_FOLLOW off: the viewed scene may differ
(user navigated elsewhere); the viewed scene is editable, not the playback
scene.  Future: when live record is implemented, arming record with
PAR_FOLLOW off and a non-playback viewed scene should set the view to the
playback scene.

**Q4 — Load behavior**: (b) preserve per-track overrides.  A load replaces
scene content; the track continues to play from its assigned scene index with
the newly loaded content.  User's responsibility to manage.

**Q5 — Per-track status visibility**: (a) no indication.  Per-track overrides
are purely audible.  No persistent LED state, no VOICE LED blink, no SHIFT
overlay.  Applies regardless of PAR_FOLLOW.

**Q6 — Single-voice/drumset coexistence**: (b) single-voice apply supersedes
the drumset worker for the affected slot.  Clears the slot's bit from
`drumset_apply_pending_mask`; drumset worker skips pre-committed slots.

**Q7 — Double-click detection**: uses the existing button timer infrastructure,
extended.  General-purpose detector applicable to any button (not just PERF
SEQ).  First click fires immediately (no single-click latency); second click
within timeout fires the double-click action additively.  Timeout defined as
`DOUBLE_CLICK_TIMEOUT` in `config.h`.

**Q8 — PERF VOICE behavior**: the old cumulative unmute (VOICE press unmutes
tracks 0..N) is removed.  PERF VOICE buttons are now single-track mute toggles
on release.  Mute is cancelled if any button other than another VOICE button
was pressed during the hold.

**Q9 — FX automation from per-track scenes**: applied if the active scene's
effect type matches the track's played scene's effect type.  Dropped if types
differ.  The FX sequencer itself always advances from the active scene.

**Q10 — Saveable per-track state**: no.  Per-track assignment is transient.
However: when the background Bank load feature is developed, Scene 17 will
snapshot the current playback state before a full Bank replacement.  That
playback snapshot must include the per-track scene assignments.  This is not
disk-saveable scene state but persistent runtime state that the Bank load
snapshot system will need to capture.

**Q11 — Multi-track per-scene assignment**: (a) yes.  All held VOICE buttons
switch together when a SEQ is pressed.  The `perf_heldVoiceMask` bitmask
naturally supports this with no additional state.

**Q12 — Mute toggle scope**: (c) independent per-release.  Each VOICE button's
mute toggle fires on its own release, regardless of other VOICE buttons held.
Another VOICE button being held does not cancel the mute gesture.

**Q13 — Cross-voice modulation rebind**: bind against the live runtime state.
If the target parameter exists on whatever instrument is currently loaded in
the source slot, map it.  If it doesn't exist, drop it.  Cross-scene
modulation that produces unexpected results is left to the user — same
principle as automation (Q1).  The rebind reads the live tagged type vector,
matching the existing `preset_tickInstrumentApply()` behavior.
