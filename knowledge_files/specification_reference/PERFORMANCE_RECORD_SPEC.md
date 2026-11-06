# Performance and Recording Controls Specification

## Authority and status

This is the authoritative reference for the PERF performance surface, live
record/erase controls, the BAR overlay trigger engine, and the gesture-to-engine
control flow, **current as of Session 091** (2026-11-05,
`dev-ph7-voice_expansion`).

This document covers the **control layer**: how physical and MIDI gestures reach
the sequencer engines, and what the user sees and hears.  The internal engine
maths (looper wrap/anchor, live record ring/placement, roll timing, scene change
boundary detection, chain evaluation) live in
`PATTERN_DYNAMIC_STACK.md` §6.5–§6.7.  The module boundary contracts live in
`MODULE_INTERCHANGE_SPEC.md`.  The Effect-page FX-sequence lock editing, R13
resolution and the `typ` transaction live in `EFFECTS_BUS_REFERENCE.md`.

Source files: `Core/Hardware/frontPanel/buttonHandler.c/.h` (gestures, overlay
engine, erase router, REC button), `Core/Sequencer/LiveRecord.c/.h` (record/erase
logic), `Core/Sequencer/sequencer.c/.h` (TIM3, looper, live overlay engine, roll
engine), `Core/Menu/menu.c/.h` (pages, scene change executor, parameter commit,
recording hooks), `Core/Menu/menuEffects.c/.h` (FX-page erase, lock editing).

---

## 1. PERF mode overview

PERF is the live performance surface.  It exposes Morph controls, the track
looper, the BAR overlay trigger engine, scene selection and chaining, live record
and live erase.  Entry: MODE PERF (`selectButtonMode`).  Exit: any MODE press
(`handleModeButtons()` force-releases the looper, closes SHIFT+REC and the chain
editor).

PERF occupies two menu pages:

| Page | Sub-pages | Content |
|---|---|---|
| `PERFORMANCE_PAGE` | 0: `mrp fxm 1VM 2VM`; 1: `3VM 4VM 5VM 6VM` | Scene Morph, Effect Morph, per-voice Morph amounts |
| `RECORDING_PAGE` (SHIFT+REC) | 0: `pb< pn< pb> pn>` (Prfrmnc); 1: `pv< pv> qnt rtk` | BAR overlay settings, roll velocity rhythms, quantisation, recording track |

The encoder crosses sub-pages through the static cross-sub-page step
(`menu_moveToMenuItem()`), so a sub-page with fewer than eight cells is never a
dead end.

### 1.1 Button map

| Gesture | Action |
|---|---|
| SEQ | Scene change at `scd` division (instant when stopped); with VOICE held and no loop: per-track Scene assignment; while looping: always a full change; while chain editor open: view that Scene's chain |
| SHIFT+SEQ | Queue a change at `qcd` (replaces a pending queue); with the SHIFT+PLAY chord armed: open chain editor |
| SHIFT+PLAY | Arm the chain chord (`bh_chainPlayArmed`); never toggles transport in any mode |
| SHIFT+PLAY+REC | Arm MIDI Learn (SHIFT, PLAY, REC flash; next touched addressable parameter shows `LRN`) |
| SELECT1..8 (hold) | Looper division: 1 bar, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64, 1/128; last pressed wins |
| VOICE then SELECT | Loop only the held VOICE tracks |
| SELECT held, VOICE press | Move the loop to the currently held VOICE set (kept tracks keep their loop start) |
| VOICE press (looping, no SELECT) | Add that track to the loop with a fresh loop start |
| VOICE (looping) | Looper only: never fires overlay, never toggles mute, never starts erase gesture |
| VOICE release (looped track) | Drop that track; it resumes where it would have been |
| BAR1/BAR2 (looping) | Momentary triplet ×2/3 / dot ×3/2 |
| BAR1/BAR2 (not looping) | Toggle VOICE-trigger overlay `<` / `>` (one at a time, LED lit) |
| VOICE (overlay on, no SHIFT) | Fire overlay trigger; release = note-off. Not while loop is engaged |
| SHIFT+VOICE (overlay on) | Normal mute toggle |
| VOICE release (no loop, no overlay, no action during hold) | Mute toggle |
| REC tap | Toggle live record LATCH on release (before 300 ms) |
| REC hold (> 300 ms) | OVERDUB while held |
| SHIFT+CLEAR (either order, while armed) | Live erase; SEQ/VOICE presses become erase gestures per mode |
| SHIFT+REC | Latched settings menu (`RECORDING_PAGE`) — any mode |
| MODE press | Close SHIFT+REC and chain editor; leave PERF force-releases looper |

### 1.2 Looper and overlay priority

While the loop is engaged, SELECT, BAR and VOICE always keep their looper
functions.  A VOICE held as an overlay trigger when the loop engages ends that
gesture and becomes an ordinary TRACK hold.  Live record and erase are not turned
off: the looper and the record modes run together (S081 P3 L-2).

Event-ring overflow force-releases the looper and closes latched menus.

---

## 2. BAR overlay trigger engine

The overlay fires performance triggers (single hits, flams, rolls) on any track
from PERF VOICE presses and from MIDI notes on mapped tracks.

### 2.1 Activation

- BAR1/BAR2 in PERF (not looping) toggle `bh_overlayActive` (0 = off, 1 = `<`,
  2 = `>`).
- MIDI CC2 on G sets it the same way (0 off, 1 `<`, 2 `>`; last action wins).
- Received MIDI notes perform the overlay in any mode (S083).

### 2.2 Trigger types

Per-overlay settings: `pb<`/`pb>` (`PAR_PERF_BTN_LT/RT`), `pn<`/`pn>`
(`PAR_PERF_NOTE_LT/RT`), stored in `settings.cfg`.

| Value | Label | Action |
|---:|---|---|
| 0..3 | `vmx v10 v64 v32` | One hit at velocity 127 / 100 / 64 / 32 |
| 4..5 | `flm fls` | Grace hit at 64 now; main at 100, 12 master ticks (1/32) later, reset 1 |
| 6..14 | `1/8 8tr 16 16t 32 32t 64 64t 128` | Held roll at step-roll index `value − 5`; hit 0 at 100, repeats every interval until release; velocity shaped by `pv` rhythm from position 1 |

A stored value above 14 is treated as type 0.

### 2.3 Velocity rhythm

`pv<`/`pv>` (`PAR_PERF_VEL_LT/RT`): the velocity rhythm for overlay rolls.
Values from the `rtm` list (`off`, `1-2`..`4-5` halve, `1+2`..`4+5` double).
Single and flam types ignore the rhythm.

### 2.4 Architecture

| Layer | Owns | Context |
|---|---|---|
| `buttonHandler.c` | `bh_overlay[7]` gesture table (4 B per track: `heldMidiNote`, `note` with bit 7 = `def`, `count` 0 idle / 1 flam main pending / 0xFF held roll, `rate` 1..9); the first hit; reports to LiveRecord | Foreground |
| Sequencer live engine (`seq_live*`) | Timing: roll repeats and flam main on the 96-PPQ master tick; per-track mailbox (hit count, flam bit, TIM2 µs stamp) | TIM3 (`seq_processLiveTicks()`) |
| LiveRecord | Recording (`liveRec_triggerIn()`, roll spans) | Foreground only |

### 2.5 Lifecycle

1. **Start** — `buttonHandler_overlayStart()`: ends any active gesture on the
   track, fires the first hit with `seq_triggerVoice(…, reset 1)`, reports
   SINGLE / FLAM_GRACE / ROLL_START to LiveRecord, arms the Sequencer's live
   engine (`seq_liveFlamArm()` or `seq_liveRollStart()`).

2. **Repeats** — fire in TIM3; nothing in the foreground times them.  The first
   repeat follows `ppq − 1` whole ticks after the next tick boundary; then every
   `ppq` exactly.  Hits are spaced from the press, not grid-locked.

3. **Drain** — `buttonHandler_overlayDrain()` takes the TIM3 mailbox
   (`seq_liveTakeHits()`): a fired flam main is reported as FLAM_MAIN; roll hits
   as one ROLL_HIT carrying rate, rhythm and the latest hit's age.

4. **Overlay switch** — every assignment of `bh_overlayActive` is preceded by
   `buttonHandler_overlayDrainAll()`.  Then
   `buttonHandler_overlayReconfigure()` walks the tracks: drain first, then judge
   whether the entry is panel-held or MIDI-held.  Held entries: overlay off →
   stop; non-roll → stop; roll → roll → rate update; single/flam → roll → start.

5. **End** — `buttonHandler_overlayEndTrack()`: `seq_liveStop()`, drain, voice
   and MIDI note-off, `liveRec_rollRelease(track)`.

### 2.6 MIDI entry points

- `buttonHandler_overlayMidiNoteOn(track, which, internal, midiNote, ageUs)` —
  starts the gesture on the track.
- `buttonHandler_overlayMidiNoteOff(track, midiNote)` — ends a MIDI-held roll;
  a flam ignores its note-off and keeps its main.

One executor and one state table for panel and MIDI.

### 2.7 Timing facts

Live ticks run before the transport gate, so held rolls play while stopped
(internal tempo, or measured external tempo while stopped under external sync).
Live overlay state is never cleared by scene changes or transport resets.

---

## 3. Live record

Live record writes what the user plays and turns onto one recording track of one
target Scene.  The engine is `Core/Sequencer/LiveRecord.c/.h` (prefix
`liveRec_`).  Internal maths: `PATTERN_DYNAMIC_STACK.md` §6.7.

### 3.1 Modes

| Mode | Gesture | Behaviour |
|---|---|---|
| OFF | — | Idle |
| LATCH | REC tap (toggles on release, before 300 ms) | First adjustment of a parameter latches it for the take; latched parameters own their automation on `rtk` |
| OVERDUB | REC held past 300 ms | Only adjusted parameters are written; nothing latches or clears |
| ERASE | SHIFT+CLEAR (either order) while LATCH or OVERDUB | Live erase; ends overdub; REC does nothing while erase is on |

Arming while stopped is allowed; recording starts with playback.  Every overdub
entry/exit, REC off, track or `rtk` change, and any scene change starts a new
take (latches and dirty bits cleared).  The looper does not lock record out.

### 3.2 Recording track (`rtk`)

`PAR_AUTOM_TRACK` (runtime only, RECORDING_PAGE sub-page 1 cell 3, shown 1..7).
Follows every active-track change (`menu_setActiveVoice()` →
`liveRec_onActiveTrackChanged()`).  SHIFT+REC can override it until the next
track selection.

### 3.3 Target Scene

Target T = P(rtk), the played Scene of `rtk`.  While writing and T ≠ A (the
active Scene), LiveRecord holds a Pattern Stack Service target override.  While
the target is not open, writes are dropped and counted.

Values come from A (the Scene the pages display), never from T.

### 3.4 Ownership

A trigger on track t records only if P(t) == T and the slot type in T matches A.
An Instrument parameter of slot s records only if the track playing s plays T and
the types match.  Effect or Scene parameters record only if T == A.  Erase on
track t records only if P(t) == T.

### 3.5 Trigger funnel

Sources that feed `liveRec_triggerIn()`:

| Source | Path | Hit kinds |
|---|---|---|
| PERF BAR overlay | `buttonHandler_overlayReport()` → `liveRec_triggerIn()` | SINGLE, FLAM_GRACE, ROLL_START (from `overlayStart`); FLAM_MAIN, ROLL_HIT (from `overlayDrain`) |
| MIDI notes | `midiNotes_handle()` (kind SINGLE), or through overlay executor when BAR overlay is on | SINGLE (with arrival `age_us`) |
| Roll release | `liveRec_rollRelease(track)` | — (ends the span) |
| Roll rate change | `liveRec_rollRateChange(track, idx, rhythm)` | — (updates the span) |

Triggers carry `liveRec_trigger_t`: track, note, velocity, kind, roll index,
rhythm, source, note_is_default flag, and `age_us`.

**Arrival time (S083 Q6):** a MIDI hit's captured position is moved back by
`age_us × 256 / snap.periodUs` Q8 ticks (at most one step interval) before
quantising, so a note that waited for the foreground records on the step it
arrived on.

### 3.6 Placement

Placement uses `seq_captureTrackTime()` + `seq_placeFromSnapshot()`:

1. Capture under PRIMASK: elapsed tick, PPQ µs stamp and period, the track's
   live cursor, accumulator, effective interval/length/shuffle, play
   mode/state, played Scene and loop state.
2. Compute shift: un-quantised = sub-tick fraction; `qnt` 1/8..1/64 = snap to
   nearest master-grid boundary; `qnt trk` = snap to nearest step start on the
   track's own grid.
3. Shift the cursor with `seq_shiftPositionQ8()` (signed Q8, run-mode walk).
   A looped track whose shift crosses a wrap is mapped onto the replayed pass.
4. Round to the nearer step start.
5. `delay = round(off × 100 / interval)` from the step's nominal start (shuffle
   is never used: playback adds it).
6. Determine `rel` (behind / current / ahead) for the echo rule.

### 3.7 Echo marks

A step landed **ahead** of the playhead gets an echo mark: TIM3 skips that
stored trigger once (automation still plays).  The mark expires at
`capture + ahead + one interval + 1 ticks`, so it can never silence a later hit.
A write onto the **current** step mutes that step's pending deferral (no second
hit).  Steps behind get nothing.

### 3.8 Automation recording

**Recordable:** VOICE-page Instrument cells, `Nvm`, `Nou`, `Nfx`, `7dc`;
Effect AUTOMATABLE rows and the Effect page Morph cell; SEQ `scl`/`shf` (only
while the viewed Pattern is the active Scene); PERF Morph cells `1VM`..`6VM` and
`fxm`.

**Not recordable:** the master morph `mrp` (no target; never recorded), RECORDING
/ global / MIDI / Load/Save pages, step editors, held-step overlays, FX lane
locks, LFO/velocity targets, `typ`, Morph-view edits, `len`.

The hook `menu_liveRecRecordCell()` runs after pot and encoder commits.
MIDI writes record through `menu_commitTargetEdit()` with the real target ID.

### 3.9 LATCH vs OVERDUB automation table

At the end of step N of `rtk`, for every latched target (LATCH mode):

| Condition | Own Instrument param | Foreign |
|---|---|---|
| dirty | written (switch 0) / bit cleared (switch 1) | same |
| not dirty, N fired | write the menu value (re-stamp) | clear an existing entry |
| not dirty, N not fired, entry exists | clear | clear |
| not dirty, N begun by a looper jump | nothing | nothing |

OVERDUB: dirty targets are written; nothing latches, clears or re-stamps.

### 3.10 First-change switch

`LIVEREC_AUTOMATION_FIRST_CHANGE` (`config.h`, default 1): the first change of a
target in a step is written at once into the live step of `rtk`.  Later changes
in that step are not written.  Setting 0: the S081 P1 rule (value at step end is
written).

### 3.11 Roll recording

Every held roll is recorded as a **span** at any rate relative to the track's
step.  There is no slow/fast split and no per-hit quantisation.

- Start step placed and quantised like any hit; written with roll rate, fill 100,
  and the active `pv` rhythm; the track is live-held (stored triggers skipped
  while held, automation plays).
- Each later ring entry writes the previous step as a span (trigger + rate +
  fill 100 + rhythm + `fil`).
- Release: start step not yet reached → becomes a plain trigger; on start step
  after second hit → fill = release delay − start delay; on later step → fill =
  release delay.  End step keeps rate and rhythm.
- Open rolls are per track (`liveRec_roll[7]`): concurrent overlay/MIDI rolls
  on several tracks record independently.
- Rate switches while held update from the step containing the switch.
- A take boundary ends every open roll without an end write.

### 3.12 Global Morph never recorded (G-1)

`mrp` (`PAR_MORPH`) has no recording target.  Each individual voice or FX Morph
edit uses its own path (`liveRec_morphValue[7]` / `liveRec_recordValue7()`), so
a global Morph change cannot leak into a pending write.

---

## 4. Live erase

### 4.1 Entry and exit

SHIFT+CLEAR (either order) while LATCH or OVERDUB enters erase.  Both buttons
released exits.  SHIFT and CLEAR LEDs stay lit while either is held.

### 4.2 Erase gestures

| Gesture | Duration | Action |
|---|---|---|
| Knob over automatable target | Until erase ends | Target joins `erase`; drain skips it on `rtk`; each passed step loses its entry; in FX mode also erases FX lane locks |
| SEQ hold (VOICE/STEP mode) | While held | Active track's triggers and specials removed, not played; condition kept on steps with automation; automation still plays |
| SEQ hold (FX mode) | While held | Every lock of each FX step the FX playhead reaches is removed |
| TRACK hold (VOICE/STEP/PERF/FX) | While held | That track: trigger, specials, condition and automation removed, not played |

SEQ in PERF (Scenes), Load/Save, global settings, SOM_GEN: presses are consumed,
nothing erased.  All erase presses are consumed (no step toggle, voice select,
mute, overlay trigger, scene change).

### 4.3 FX-sequence erase

LiveRecord publishes a 16-bit lane mask (`effects_setLiveEraseLanes()`).  At each
FX step start, EffectsManager excludes those lanes from resolution and records
them per step.  `liveRec_service()` drains into
`effects_clearSeqLanes(origin, step, lanes)`, which clears the locks in the
origin Scene and every linked same-type Scene.

### 4.4 Erase behaviour during other modes

While erase is enabled: PLAY is a plain transport toggle (never the chain chord);
SEQ never opens the chain menu.  A MODE change keeps erase but drops held
SEQ/TRACK gestures.  Knob targets stay until erase ends.

---

## 5. Latched menus

SHIFT+REC (`bh_shiftRecMenuActive`) and the chain editor
(`menu_chainMenuIsOpen()`): opening keeps them open after every button is
released; every SHIFT release is ignored while one is open (SHIFT LED stays lit).
A SHIFT press closes the open menu and arms `bh_shiftReleaseConsume`, so its
release only turns the SHIFT LED off.  REC also closes SHIFT+REC.
`handleModeButtons()` closes both first.

---

## 6. Scene change control flow

### 6.1 Direct changes

PERF SEQ press → `menu_perfModeSceneButtonPressed(scene)`:
- Instant (`scd` = 0) or stopped transport → execute immediately with `pcr`.
- Otherwise → `seq_requestSceneChange(scene)` (deferred to the next `scd`
  boundary in TIM3).

### 6.2 Queued changes

SHIFT+SEQ → `seq_queueSceneChange(scene, div)`:
- Instant or stopped → raise fire flag at once.
- Otherwise → wait for the `qcd` boundary in TIM3.

### 6.3 Execution

`menu_serviceDeferredSceneChange()` runs on every `buttonHandler_tick()`: when
the fire flag is up, it reads target/queue/chain, clears the pending state, and
executes.  Queue actions (`qac`): mute reset, morph, reload (deferred); then
`menu_perfSceneChangeExecute(target, restart)`.

A chain fire to the playing Scene → `seq_chainRepeatSelf(pcr)`.

### 6.4 PC reset

A resetting change runs `seq_resetToPatternStart()`: master clock, bar counter,
every cursor restarts, and the looper rebases.  Direct and chain changes reset
when `pcr` is on; queued changes reset only for `qac` reset modes (`res`, `u+r`,
`o+r`, `mmr`, `r+r`).

Engine maths: `PATTERN_DYNAMIC_STACK.md` §6.6.

---

## 7. LEDs in PERF

| Row | Rule |
|---|---|
| SEQ (Scenes) | Steady = present with steps; blink = active Scene while stopped or chain editor open, viewed Scene (follow off, differs), chain-viewed Scene; running active Scene = beat pulse; a running blink is never restarted |
| SEQ (queued Scene) | Double-rate flash on `LED_LAYER_FAST`, owned by `menu_syncQueuedSceneLed()` |
| SELECT | Dark unless looper is held/engaged, then the division |
| BAR | Overlay toggle state; both off while looper runs except held modifier; restored on loop release |
| VOICE | Mutes; a queued mute reset clears the mirror |
| SHIFT/REC | Lit while SHIFT+REC is open; SHIFT lit while chain editor is open; REC on in LATCH, blinks on overdub entry/exit, flashes while SHIFT+REC is open and armed; SHIFT and CLEAR lit while live erase is on |
| BAR (outside PERF) | Overlay toggle LEDs shown only in PERF; leaving PERF restores the target mode's BAR LEDs |

---

## 8. Settings owned by the performance surface

| Kind | Settings |
|---|---|
| `settings.cfg` Globals | `perf_btn_lt`, `perf_note_lt`, `perf_btn_rt`, `perf_note_rt`, `scd`, `qcd`, `qac`, `bar_reset_mode` (pcr) |
| Scene | `chain_condition[2]`, `chain_next[2]` |
| Pattern | `track_loop_exclude` (PAT4 track byte 4), `track_auto_smooth` (PAT4 track byte 5) |
| `settings.cfg` | `quantisation` (may be 5 = `trk`) |
| Runtime only | Overlay state, looper state, queue/chain pending, live record mode and `rtk` |

---

## 9. Service timing

- `liveRec_service()` runs in the 500 Hz foreground pass immediately before
  `patSvc_tick()` (`timebase.c`), so an idle-direct write executes in the same
  pass.
- `liveRec_init(menu_getActiveVoice())` runs at boot after `copyClear_init()`.
- `buttonHandler_tick()` at 500 Hz: REC hold promotion (300 ms), overlay drain,
  scene change executor.

---

## 10. Adding a PERF gesture or setting

1. Route the edge in `processPress()`/`processRelease()` before generic routing.
2. Keep hold state in event order (not `btn_held[]`, which the scan updates ahead
   of queued edges).
3. Make every press/release pair symmetric; reconcile in the overflow path and in
   `handleModeButtons()`.
4. Any timing state shared with TIM3 must be published under PRIMASK with a
   single-byte gate written last.
5. New enumerated menu cells: `DTYPE_0B127` + `menu_getS080StaticName()` /
   `menu_clampCellValue()` cases (no free DTYPE ids).
6. Mirrors appended after the Globals must be no-ops in
   `menu_parseGlobalParam()` and commit in `menu_cellCommitValue()`.
7. SEQ-row indications: go through `menu_refreshPerfSceneLeds()` or a dedicated
   LED layer; never start/stop blinks from a per-beat path.

---

## 11. Invariants

- LiveRecord never runs in TIM3, never writes a Scene other than T (Pattern) or
  the origin Scene's fan-out set (FX sequence).
- TIM3 never touches pool bytes for live record or erase; it uses the service's
  TIM3-safe enqueues.
- Every TIM3-shared live-record field is a byte or a PRIMASK-published pair, and
  `volatile`.
- Recording is best effort; playback always has priority.
- The side-effect-free condition path (`seq_conditionPassesPure()`) must stay in
  step with `seq_evaluateStepCondition()` when conditions are added.
- Every scene-change entry point must reach the FX-erase flush, or linked Scenes
  diverge.
- A live hold must never outlive its roll entry (`seq_setLiveHold(track, 0)` in
  `liveRec_endRoll()`).
- Pattern writes never fan out; FX-sequence erase does.
- `seq_recRingPush()` is the one producer of the record ring (TIM3 for step
  advance and mid-step wrap; foreground inside the looper's PRIMASK for resume).

---

## 12. Cross-references

| Topic | Document |
|---|---|
| Looper engine maths (wrap, anchor, snapshot, resume) | `PATTERN_DYNAMIC_STACK.md` §6.5 |
| Live record engine maths (ring, placement, step-end tables) | `PATTERN_DYNAMIC_STACK.md` §6.7 |
| Roll engine (window-based length, carry, types) | `PATTERN_DYNAMIC_STACK.md` §6 |
| Scene change timing (boundary detection, chaining) | `PATTERN_DYNAMIC_STACK.md` §6.6 |
| Step automation playback, priority R12 | `PATTERN_DYNAMIC_STACK.md` §6.2a |
| Effect automation priority R13, FX-sequence locks | `EFFECTS_BUS_REFERENCE.md` §6 |
| Module boundary contracts | `MODULE_INTERCHANGE_SPEC.md` |
| PERF detailed button map and engineering notes | `MODULE_INTERCHANGE_SPEC.md` "PERF performance surface" |
| BAR overlay engineering detail | `MODULE_INTERCHANGE_SPEC.md` "BAR overlay trigger engine" |
| Pattern data APIs | `PATTERN_DYNAMIC_STACK.md` §5, §12 |
| Copy/clear (erase during copy/clear) | `COPYCLEAR_UTILITIES.md` |
