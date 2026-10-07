# S077 Hardware Retest Checklist

All items from Session 076 (P1–P4), Session 077 P1 (background region /
copy snapshot migration), plus carry-over items from previous sessions that
involve the same subsystems. Mark each item with the test result: PASS,
FAIL (with notes), or SKIP (with reason).

**Observation key:**
- **D** = observe on device (LEDs, button response, audio behavior)
- **I** = observe in the interface (LCD display)
- **C** = observe in on-card output file (trace log, AutoSave)
- **S** = observe in saved result (Scene/Bank save, then inspect card)

---

## P1 — Scene Parameter Automation Override Clears

These verify Rule A (non-automation writes clear overrides) and Rule B
(Scene activation clears all overrides).

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 1.1 | Play a pattern with FX send automation on a voice. While playing, turn the FX send knob on VOICE page. Verify the knob edit takes effect immediately and automation no longer holds. | D/I | PASS |
| 1.2 | Same test with audio-out automation: play a pattern with audio-out step automation, change the routing on VOICE page. Verify new route is heard. | D | PASS |
| 1.3 | Play a pattern with voice Morph automation. Change the voice Morph on PERF page. Verify the Morph edit replaces the automation value. | D/I | PASS |
| 1.4 | Play a pattern with slot-6 track-7 decay automation. Edit the decay on VOICE page. Verify the edit takes. | D | PASS |
| 1.5 | Play a pattern with Effect Morph (`fxm`) automation. Edit `fxm` on PERF or Effect page. Verify the edit replaces the override. | D/I | PASS |
| 1.6 | **Rule B — Scene switch clears all.** Set up automation overrides on several parameters (FX send, Morph, audio out). Switch to another Scene and back. Verify all overrides are cleared and parameters return to their stored values (not the automation values). | D/I | PASS |
|   | - *Edge case:* switch Scenes while playback is running with active automation on multiple override families. Verify no stale overrides persist on the return Scene. | D | PASS |
| 1.7 | **Regression:** normal step automation still works — values are applied during playback and cleared on transport stop/restart. | D | PASS |

---

## P2 — LFO Scene-Reset Retrigger + Phase Offset Scaling

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 2.1 | Set an LFO retrigger to `scn` (value 7). Verify the label shows `scn` on the VOICE page. | I | PASS |
| 2.2 | With retrigger `scn`: switch Scenes while the LFO is running. Verify the LFO phase is preserved across the Scene change (the modulation continues smoothly without a jump). | D | PASS |
| 2.3 | With retrigger `scn` on Scene A: switch to Scene B where the same LFO has a different retrigger (e.g. `v1`). Verify the captured phase is NOT restored (the LFO should retrigger on voice 1 as configured in Scene B). | D | PASS |
| 2.4 | With retrigger `scn` + a nonzero phase offset: verify the LFO starts at the offset position after a non-Scene trigger event (e.g., transport start). | D | PASS |
| 2.5 | **Phase offset scaling:** set LFO phase offset to 64 (mid-range). Verify the LFO starts at approximately 50% phase (half-cycle). Compare with offset 0 (start) and 127 (near end). | D | PASS |
|   | - *Edge case:* offset 127 should place the LFO phase near 100% of the cycle, not near 0%. | D | PASS |
| 2.6 | **Retrigger values 0–6 regression:** verify `off`, `v1`–`v6` still work correctly (LFO retriggers on the corresponding voice track's trigger). | D | PASS |
| 2.7 | Save a Scene with retrigger `scn` and a nonzero offset. Load it back. Verify both values are preserved. | S | PASS |

---

## P3 — Copy/Clear Morph Additions

### Track-level operations (VOICE and EFFECTS TRACK menus)

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 3.1 | SHIFT+COPY, press TRACK in VOICE mode. Verify "reset morph" appears as a clear selection (before "send" if in EFFECTS). | I | |
| 3.2 | Select "reset morph" and release TRACK. Verify the active track's Morph endpoints equal its Normal endpoints (check by switching to Morph view on VOICE page). | I | |
| 3.3 | Verify "reset morph" fans out through the voice-edit mask: set up a 2-Scene edit mask, clear "reset morph" on track 1, and check both Scenes' Morph endpoints. | I | |
| 3.4 | COPY, press TRACK in VOICE mode. Verify "morph" appears as a copy selection. | I | |
| 3.5 | Select "morph", press destination TRACK. Verify the destination's Morph endpoints are replaced by its Normal endpoints. | I | |
| 3.6 | Verify "morph" copy fans out through the edit mask. | I | |
| 3.7 | Verify the source indicator shows the 'm' suffix for morph copy. | I | |

### Scene-level operations (PERF Scene menu)

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 3.8 | SHIFT+COPY, press a Scene in PERF. Verify "reset morph" and "reset fx morph" appear in the scene clear menu. | I | |
| 3.9 | Select "reset morph" (Scene level). Verify all 6 slots + FX send morph + slot-6 decay morph + Effect morph endpoints are equalised to Normal. | I | |
| 3.10 | Verify Scene-level "reset morph" does NOT fan out (only the pressed Scene is affected, not edit-mask members). | I | |
| 3.11 | Select "reset fx morph". Verify only the Effect's morphable morph endpoints are equalised, not voice slots. | I | |
| 3.12 | Verify "reset fx morph" DOES fan out through the edit mask. | I | |
| 3.13 | COPY, press Scene in PERF. Verify "morph" appears in the scene copy menu. | I | |
| 3.14 | Select "morph" (Scene copy). Verify all 6 slots' Normal → Morph, applied to the destination Scene. | I | |
| 3.15 | Verify Scene-level "morph" copy does NOT fan out. | I | |

### Regression

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 3.16 | Existing clear selections still work: "track", "track auto", "track notes", "send" (EFFECTS). | I/D | |
| 3.17 | Existing Scene clear selections still work: "scene", "settings", "pattern", etc. | I/D | |
| 3.18 | Existing copy selections still work: "track", "instrument", step/bar pastes. | I/D | |

---

## P4 — Reload Scene, Bar Chaselight, SHIFT+SELECT Pattern Length

### Feature 1: Reload Scene

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 4.1 | SHIFT+COPY, press a Scene in PERF. Verify "reload scene" appears as the last selection in the scene clear menu. | I | |
| 4.2 | Load a Scene from the library. Make edits (change a parameter). Select "reload scene". Verify the Scene reverts to its saved state. | D/I | |
| 4.3 | Verify the menu page does NOT change during reload (stays on current page). | I | |
| 4.4 | Reload a Scene during playback. Verify the sequencer realigns and playback continues. | D | |
|   | - *Edge case:* reload while a step automation override is active. Verify overrides are cleared by the Scene apply. | D | |
| 4.5 | Try to reload a Scene whose source is invalid (e.g., a Scene created from scratch, not loaded from the library). Verify the operation is silently dropped (no error, no change). | I | |
| 4.6 | Try to reload while Preset is busy (e.g., immediately after another load). Verify the operation is silently dropped. | I | |

### Feature 2: Bar Chaselight

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 4.7 | Enter STEP mode and start playback. Verify a SELECT LED pulses with the beat on the bar currently being played. | D | |
| 4.8 | If the playback bar is the same as the viewed bar: verify the pulse is **inverted** (the steady-on LED blinks off at beat boundaries). | D | |
| 4.9 | If the playback bar is different from the viewed bar: verify the pulse is **normal** (LED turns on at beat boundaries, off between). | D | |
| 4.10 | Stop playback. Verify only the viewed-bar LED is on (no chaselight). | D | |
| 4.11 | Play a track with length < 128 (e.g., 48 steps = 3 bars). Verify the chaselight stays within bars 1–3 and never pulses on bars 4–8. | D | |
| 4.12 | Switch the active voice during playback. Verify the chaselight follows the new track's position (per-track length). | D | |
|   | - *Edge case:* two tracks with different lengths, switch between them. Chaselight bar should update immediately. | D | |

### Feature 3: SHIFT+SELECT Pattern Length

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 4.13 | In STEP mode, hold SHIFT and press SELECT 1 (first button). Verify the active track's length is set to 16 (1 bar). | I | |
| 4.14 | SHIFT+SELECT 4. Verify length = 64 (4 bars). | I | |
| 4.15 | SHIFT+SELECT 8. Verify length = 128 (8 bars, maximum). | I | |
| 4.16 | Set length to 2 bars while viewing bar 5. Verify the viewed bar clamps to bar 2 (the last valid bar). | I/D | |
| 4.17 | Verify the track settings display updates immediately after the length change (PAR_TRACK_LENGTH visible on LCD). | I | |
| 4.18 | Verify the track length is per-track: set track 1 to 2 bars, track 2 to 4 bars. Switch between them and confirm independent lengths. | I | |
| 4.19 | **Regression:** unshifted SELECT in STEP mode still selects bars (not setting length). | D/I | |
| 4.20 | **Regression:** SHIFT+SELECT in VOICE mode still selects bars (unchanged behavior). | D/I | |

---

## P5 — Background Region and Copy Snapshot Migration (S077 P1)

These verify the 17th background region (`pat_background_region`), the
migrated overlapping-paste scratch (background pool instead of name buffer),
the snapshot gate (`filesystem_patternSnapshotInUse()`), and the reduced
name-buffer borrow scope.

### Overlapping paste (new path)

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 5.1 | Copy a step range on the same track where source and destination overlap (e.g., copy steps 1–4 to steps 3–6 on one track). Verify the paste completes correctly: the destination steps contain the original source data, not data corrupted by the overlap. | D/I | |
| 5.2 | Same as 5.1 but with retargeted entries (copy between two tracks with different voice types). Verify the retarget applies correctly to the snapshot blocks in the background pool. | D/I | |
| 5.3 | Queue two overlapping pastes back to back (e.g., steps 1–4 → 3–6, then steps 3–6 → 5–8). Verify both complete correctly in FIFO order (sequential source capture is accepted behaviour per Q4). | D/I | |

### Snapshot gate (drain vs paste concurrency)

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 5.4 | Trigger an AutoSave Pattern drain (dirty a Pattern, wait for the drain to begin). While the drain is in flight (phases 2–6), start an overlapping paste. Verify the paste waits until the drain finishes its snapshot-reading phases, then completes correctly. | D/C | |
|   | - *Trace check (DEV build):* the trace log should contain exactly one 0x51 (`SNAPSHOT_GATE`) record per wait episode, with a nonzero tick count as the value. | C | |
| 5.5 | Verify the drain's output file is valid after the paste completes (the `.patNNa`/`.patNNb` file written during the drain is not corrupted by the paste that waited). | C/S | |

### Non-overlapping paste (regression)

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 5.6 | Copy a step range to a non-overlapping destination on the same track. Verify the paste takes the live path (no snapshot, no gate wait). | D/I | |
| 5.7 | Copy a step range to a different track. Verify the paste takes the live path. | D/I | |

### AutoSave after copy operations

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 5.8 | Perform an overlapping paste, then let the AutoSave Pattern drain run. Verify the drain writes the correct snapshot (the live region post-paste, not stale snapshot data from the paste). | C/S | |
| 5.9 | Power-cycle after an overlapping paste followed by an AutoSave drain. Verify the Pattern content survives (boot reader restores from the PAT file). | D/S | |

### Name buffer (reduced scope)

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 5.10 | Perform a paste that includes named steps (HCNAMES rows). Verify the name remap still works — destination steps receive the correct names from the source. | I | |
| 5.11 | Verify the name buffer borrow occurs only during the final name-write phase, not during the snapshot/place phases (observable via trace: no 0x40 `SCRATCH` record until the names phase). | C | |

---

## Carry-over from Previous Sessions

These items involve subsystems touched by S076 or were previously marked as
needing verification.

| # | Check | Observe | Result |
|---|-------|---------|--------|
| C1 | S075 SHIFT+TRACK overlay follow-up: while any overlay TRACK is held, every TRACK press moves the screen to that track and nothing mutes; the last release restores the Effect page. | D/I | |
| C2 | S075 F3 automation priority: LFO on Morph plus pitch automation on the same step — verify no conflict. | D | |
| C3 | S075 F3: `Nvm` automation on the same step as a voice parameter automation — verify both apply correctly. | D | |
| C4 | S075 F3: MIDI CC to an automated parameter — verify MIDI is lowest priority (automation holds until trigger). | D | |
| C5 | S075 production build re-measurement. | I | |
| C6 | S072 Steps 6–10 hardware acceptance (Phase 5 Effects bus). | D/I | |

---

## P6 — Per-Track Scene Playback (S077 P2)

### Per-track sequencer read path

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.1 | Play Scene 0.  In a debugger or test harness, set `seq_perTrackPattern[0] = 1`.  Track 1 plays Scene 1's step data and track length.  Other tracks stay on Scene 0. | D | |
| 6.2 | Same setup: Scene 1 has a shorter track length than Scene 0.  Verify track 1 wraps at Scene 1's length.  Other tracks wrap at Scene 0's length. | D | |
| 6.3 | Set one per-track override: verify `seq_perTrackActive` reads 1.  Clear all (scene switch): verify it reads 0. | C | |
| 6.4 | While a per-track override is active, try to arm live record.  Verify it refuses (seq_recordActive stays 0).  Disarm always succeeds. | D/I | |
| 6.5 | Play with a per-track override.  Live erase on the overridden track.  Verify the erased step disappears from that track's played Scene's data, not from the active Scene's. | D | |
| 6.6 | MIDI output: override track 1 to Scene 3.  Verify track 1 sends MIDI on Scene 3's channel/note.  Other tracks send on the active Scene's channel/note. | C | |
| 6.7 | Stopped-transport preview: with track 1 overridden to Scene 3, press VOICE 1 pad.  Verify the preview uses Scene 3's MIDI note/channel. | D/C | |

### Per-track automation

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.8 | Voice parameter step automation: override track 1 to Scene 3.  Scene 3 has step automation on track 1.  Verify the automation entries play from Scene 3's data. | D | |
| 6.9 | FX step automation: Scene 0 (active) has Effect type A.  Scene 3 (track 1's played Scene) has Effect type B.  Verify track 1's FX automation is suppressed (type mismatch gate). | D | |
| 6.10 | Same as 6.9 but Scene 0 and Scene 3 have the same Effect type.  Verify FX automation applies normally. | D | |
| 6.11 | Retrigger automation restore: override track 1 to Scene 3.  Scene 3 has voice parameter automation.  Verify the retrigger restore reads the instrument image from Scene 3 (no jump to Scene 0's values). | D | |

### Single-voice instrument apply

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.12 | Call `preset_startSingleVoiceApply(0, 3)` during playback.  Track 1 swaps to Scene 3's instrument using the deferred quiet-wait.  Other voices unaffected. | D | |
| 6.13 | Start a full Scene switch (drumset apply).  Immediately assign one track per-track.  Verify the per-track assign supersedes the drumset worker for that slot (the played Scene's image wins). | D | |
| 6.14 | Assign a continuously ringing voice per-track.  Verify the trigger-time force path commits the image on the next note trigger. | D | |
| 6.15 | Per-track assign: verify the assigned slot's audio routing, FX send, fader mode, and Morph amount follow the played Scene.  Edit a parameter in the played Scene; verify the edit is heard live (no re-apply needed). | D/I | |
| 6.16 | Verify the bus compressor remains on the active Scene regardless of per-track overrides. | D | |

### PERF gestures

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.17 | Hold VOICE 1 + press SEQ 5: track 1 switches to Scene 5.  Release VOICE 1: no mute toggle. | D | |
| 6.18 | Hold VOICE 1, press MODE (or any non-VOICE/non-SEQ button), release VOICE 1: no mute toggle (action cancelled). | D | |
| 6.19 | Press VOICE 1 and release without pressing SEQ or other buttons: track 1 mute toggles. | D | |
| 6.20 | Hold VOICE 1 + VOICE 2 simultaneously, press SEQ 5: both tracks switch to Scene 5. | D | |
| 6.21 | Hold VOICE 5 (HiHat closed), press SEQ 3: tracks 5 and 6 both switch to Scene 3. | D | |
| 6.22 | Press SEQ pointing to an empty/absent Scene while holding VOICE: verify no assignment (silently ignored). | D | |
|   | - *Edge case:* rapid alternating VOICE holds and releases; verify no stranded mask bits (all state clears on last release). | D | |

### Double-click (PERF SEQ)

**NOTE: items 6.23–6.27 require the dblclick timing fix on
`buttonHandler.c` line 432 before they can pass.  See the Code Review
Assessment in `S077_P2_IMPLEMENTATION.md`.**

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.23 | Double-click SEQ in PERF (PAR_FOLLOW on): all tracks realign to the master clock.  Single-click: normal scene switch. | D | |
| 6.24 | PAR_FOLLOW off, single-click SEQ: view changes, playback unchanged, per-track overrides remain. | D | |
| 6.25 | PAR_FOLLOW off, double-click SEQ: full scene change + coalesce + realign via `menu_perfModeSceneButtonPressed()`. | D | |
| 6.26 | Press SEQ, then press a VOICE (different button) within 300 ms, then press the same SEQ again: verify no false double-click (the VOICE press cancels the window). | D | |
| 6.27 | Two presses of *different* SEQ buttons within 300 ms: verify no false double-click (different button restarts the window). | D | |

### LED feedback

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.28 | PERF mode, transport running: the active Scene's SEQ LED pulses with the tempo (on at beat, off between).  Transport stopped: standard blink. | D | |
| 6.29 | PAR_FOLLOW off, viewed Scene ≠ active Scene: the viewed Scene's SEQ LED blinks (distinct from the active Scene's tempo pulse). | D | |
| 6.30 | Hold VOICE 1 in PERF: the SEQ LED of track 1's played Scene blinks.  Release: LED state restores to the normal PERF Scene row. | D | |
| 6.31 | Hold VOICE, press SEQ (assign): the held-scene blink updates to the newly assigned Scene immediately. | D | |

### PAR_FOLLOW view-follows-track

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.32 | PAR_FOLLOW on, track 1 plays from Scene 3.  Press VOICE 1 in VOICE mode: viewed Scene switches to Scene 3. | D/I | |
| 6.33 | Same, but no per-track override active.  Press VOICE 1: viewed Scene unchanged (no unnecessary switch). | D/I | |

### Coalesce and scene switch interactions

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.34 | Assign tracks 1 and 2 to different Scenes.  Switch Scene (PERF SEQ single-click, PAR_FOLLOW on): all tracks coalesce to the new Scene.  Verify `seq_perTrackActive` returns to 0. | D | |
| 6.35 | Assign a per-track override.  Load a different Bank.  Verify all overrides are cleared (per-track state does not survive a Bank Load). | D | |
| 6.36 | Assign a per-track override.  Bar boundary commits a pending pattern: verify overrides clear. | D | |

### Regression

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 6.37 | Normal Scene switching (no VOICE held) in PERF: verify unchanged behaviour (scene switch, drumset apply, realign). | D | |
| 6.38 | Voice mute in PERF without per-track feature: press and release VOICE quickly — verify mute toggles.  Hold VOICE + press SEQ to an absent Scene — verify mute still toggles on release (assignment was ignored). | D | |
| 6.39 | Existing step automation playback with no per-track overrides active: verify values are applied and cleared on stop/restart (no regression from the `seq_perTrackPattern` substitution). | D | |
| 6.40 | Morph/LFO Morph during Scene switch with no per-track overrides: verify smooth transitions, no glitches from the `presetMorph_applyVoiceNowInternal` refactor. | D | |
| 6.41 | LFO phase preservation across a normal Scene switch (no per-track): verify the S076 P2 `scn` retrigger still works (the `restoreLfoPhaseIfNeeded` guard removal must be neutral). | D | |
| 6.42 | Full drumset apply during playback (no per-track): verify the `preset_tickSingleVoiceApply()` pre-check in `preset_tickDrumsetApply()` does not add latency (mask is 0, immediate fallthrough). | D | |
