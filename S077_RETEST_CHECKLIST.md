# S077 Hardware Retest Checklist

All items from Session 076 (P1–P4) plus carry-over items from previous
sessions that involve the same subsystems. Mark each item with the test
result: PASS, FAIL (with notes), or SKIP (with reason).

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
| 1.1 | Play a pattern with FX send automation on a voice. While playing, turn the FX send knob on VOICE page. Verify the knob edit takes effect immediately and automation no longer holds. | D/I | |
| 1.2 | Same test with audio-out automation: play a pattern with audio-out step automation, change the routing on VOICE page. Verify new route is heard. | D | |
| 1.3 | Play a pattern with voice Morph automation. Change the voice Morph on PERF page. Verify the Morph edit replaces the automation value. | D/I | |
| 1.4 | Play a pattern with slot-6 track-7 decay automation. Edit the decay on VOICE page. Verify the edit takes. | D | |
| 1.5 | Play a pattern with Effect Morph (`fxm`) automation. Edit `fxm` on PERF or Effect page. Verify the edit replaces the override. | D/I | |
| 1.6 | **Rule B — Scene switch clears all.** Set up automation overrides on several parameters (FX send, Morph, audio out). Switch to another Scene and back. Verify all overrides are cleared and parameters return to their stored values (not the automation values). | D/I | |
|   | - *Edge case:* switch Scenes while playback is running with active automation on multiple override families. Verify no stale overrides persist on the return Scene. | D | |
| 1.7 | **Regression:** normal step automation still works — values are applied during playback and cleared on transport stop/restart. | D | |

---

## P2 — LFO Scene-Reset Retrigger + Phase Offset Scaling

| # | Check | Observe | Result |
|---|-------|---------|--------|
| 2.1 | Set an LFO retrigger to `scn` (value 7). Verify the label shows `scn` on the VOICE page. | I | |
| 2.2 | With retrigger `scn`: switch Scenes while the LFO is running. Verify the LFO phase is preserved across the Scene change (the modulation continues smoothly without a jump). | D | |
| 2.3 | With retrigger `scn` on Scene A: switch to Scene B where the same LFO has a different retrigger (e.g. `v1`). Verify the captured phase is NOT restored (the LFO should retrigger on voice 1 as configured in Scene B). | D | |
| 2.4 | With retrigger `scn` + a nonzero phase offset: verify the LFO starts at the offset position after a non-Scene trigger event (e.g., transport start). | D | |
| 2.5 | **Phase offset scaling:** set LFO phase offset to 64 (mid-range). Verify the LFO starts at approximately 50% phase (half-cycle). Compare with offset 0 (start) and 127 (near end). | D | |
|   | - *Edge case:* offset 127 should place the LFO phase near 100% of the cycle, not near 0%. | D | |
| 2.6 | **Retrigger values 0–6 regression:** verify `off`, `v1`–`v6` still work correctly (LFO retriggers on the corresponding voice track's trigger). | D | |
| 2.7 | Save a Scene with retrigger `scn` and a nonzero offset. Load it back. Verify both values are preserved. | S | |

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
