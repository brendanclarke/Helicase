# S102 P1 — Chromatic keyboard overlay: implementation notes

**Date:** 2026-11-22; Fix 1 2026-11-24. **Branch:** `dev-ph7-alkaline`,
base `8ca780d1` ("clean up documents and test scratch"). **Status:** the
first build is committed in `ef3d3802`; Fix 1 (§11) is uncommitted and built.
**Device:** the first build was tested and did not hold notes (§11); Fix 1 is
not yet tested on the device (§8, §11).

**Your instruction (2026-11-22):** chromatic overlay on SHIFT+BAR `<`/`>`
from any mode; SEQ row = keyboard on the selected track; each key uses the
trigger type set for the entry BAR in the SHIFT+REC menu; held while held;
recordable; middle C on SEQ1; black keys lit; ±3 octaves with BAR `<`/`>`;
BAR LEDs show the octave; no chase, one brief pulse per note played by the
active track; nothing else interrupted; SHIFT+BAR again leaves; octave
remembered for the boot session only. "Implement that directly."

**The image:** `build/LXRV2_lxr02.img`, 674,520 B payload, with Fix 1
(before S102: 673,576 B; first build: 673,812 B).

---

## 0. Summary

| § | Change | Status |
|---|---|---|
| 2 | `buttonHandler.c`: keyboard state, press/release routers, pulse service, overflow reset | built |
| 11 | Fix 1, `buttonHandler.c`: single types played as MIDI notes (`midiNoteMode_noteOn/Off`), so a Colloid holds | built, device test open |
| 3 | `buttonHandler.c`: three touch points in the existing BAR-overlay engine (ownership, rhythm) | built |
| 4 | `ledHandler.c/.h`, `dout.c/.h`, `din.c`: the owned-LED composite for SEQ1..16 and both BAR LEDs | built |
| 5 | `MidiVoiceControl.c/.h`: the note watch at the trigger enqueue | built |
| 6 | Linked-image checks: mixer and render-drain hot loops vs HEAD | mixer 41 equal; render drain: 9 control-rate loops longer after Fix 1 (open, §11) |

**The figures** (before S102 = `8ca780d1`):

| | Before S102 | First build | With Fix 1 | Δ total |
|---|---:|---:|---:|---:|
| Flash (image payload) | 673,576 B | 673,812 B | 674,520 B | +944 B |
| `.text` | 673,136 B | 673,360 B | 674,072 B | +936 B |
| `.data` | 440 B | 452 B | 448 B | +8 B (section alignment) |
| `.bss` | 422,480 B | 422,496 B | 422,512 B | +32 B (section alignment) |
| ITCM | 15,424 B | 15,424 B | 15,424 B | 0 |
| `mixer_calcNextSampleBlock` hot loops | 41 | 41 equal | 41 equal | 0 |
| `audio_check_and_render` hot loops | 28 | 28 equal | 9 grew by 1–4 (control rate) | open, §11 |

**RAM:** 41 B of new SRAM1 (§7), **approved by you 2026-11-24**.

---

## 1. Behaviour as built

### 1.1 Entry and exit

- **SHIFT+BAR `<` or SHIFT+BAR `>`** in any mode enters. The BAR used is
  remembered for the session's trigger type (`which` 1 = `<`, 2 = `>`).
- **SHIFT+either BAR** while it is up leaves it.
- On exit the SEQ row and both BAR LEDs show whatever the current mode has
  painted underneath, as it is at that moment (§4). No repaint is needed.
- The press is taken before every other routing, so SHIFT+BAR is now always
  the overlay toggle. Nothing used SHIFT+BAR before: the Effect BAR hooks
  are NULL for every type, and the Load/Save name editor ignores SHIFT.

### 1.2 Keyboard

- SEQ1 = C, chromatic upwards; SEQ16 = the D# an octave and a third above.
- **Middle C = internal note 60.** The step `nte` cell shows 60 as `C5`.
  The voice's own pitch (the default note) is internal 63, shown `D#5`, so
  a step with no note special pulses SEQ4 at the middle octave (§9 Q1).
- Black keys (SEQ 2, 4, 7, 9, 11, 14, 16) are always lit; white keys dark.
- **BAR `<` / `>`** (no SHIFT) step the octave down/up, −3..+3. At a limit
  the press does nothing. BAR LEDs: middle octave both lit; above it only
  `>`; below it only `<`.
- The octave is a plain static: 0 at boot, kept across exits, never saved.

### 1.3 What a key plays

- The track is `menu_getActiveVoice()` at the moment of the press, and only
  that track.
- Every key is played as a received MIDI note on the selected track. The
  entry BAR's `pb<` / `pb>` decides how (Fix 1, §11):
  - **Single types** (`vmx` `v10` `v64` `v32`, velocity 127/100/64/32): the
    key press is `midiNoteMode_noteOn()` on the track's effective MIDI
    channel and the release is `midiNoteMode_noteOff()`, the same calls a
    received note-on/off makes when no BAR overlay takes it. A Colloid on
    its own channel plays paraphonically and **holds while the key is
    down**; more held keys become intervals, as from a MIDI keyboard. Other
    tracks get the MIDI single trigger (Alkaline: its 7/12-step gate; drums
    and Tetrakt: one-shot), exactly as over MIDI.
  - **Flam and roll types** (`flm`, `fls`, the nine roll rates): the overlay
    engine (`buttonHandler_overlayStart()`), as a MIDI note under a BAR
    overlay, with the MIDI note-off rule on release: a roll stops, a flam
    keeps its main hit (B-12). A roll uses the entry BAR's `pv<` / `pv>`.
    One sounding flam/roll key: a second key replaces the first.
  - `pn<` / `pn>` are not used: the key is the pitch.
- As over MIDI: the PERF BAR overlay, when on, turns the Colloid's
  paraphonic mode off; a Colloid on `glb` is not paraphonic.
- Each key remembers its note, track and channel, so its release is the
  right note-off after an octave or selection change. A key on a newly
  selected track first sends note-offs for the old track's held keys.
- A muted track stays silent, as for a received MIDI note.
- **Recording:** the note modes and the overlay engine report to
  `liveRec_triggerIn()` exactly as for MIDI (source `LIVEREC_SRC_MIDI`), so
  everything records whenever live record is on (paraphonic `sus` and
  intervals as automation, rolls as spans with their `pv` rhythm).

### 1.4 What is not touched

- The PERF BAR overlay (`bh_overlayActive`) is kept as it was. VOICE buttons
  in PERF and received MIDI notes still use it while the keyboard is up.
- BAR presses cannot change it while the keyboard is up (they transpose).
  CC2 on G still can (it is MIDI, not the BAR buttons); its BAR LED writes
  land underneath the keyboard's BAR display.
- Screen, knobs, SELECT, VOICE, MODE, SHIFT, PLAY, REC, COPY: unchanged.
  SHIFT still does its own mode thing on press/release (e.g. the VOICE Morph
  view while held), since SHIFT is part of the chord.
- SEQ keys pressed on the keyboard are removed from
  `buttonHandler_seqHeldMask()`, so turning a knob while playing never
  becomes a held-step automation edit, and the Effect page hold does not see
  them.

### 1.5 Pulse

- No chase light on the keyboard (the chase keeps running underneath and is
  visible again on exit).
- Every trigger enqueued for the selected track (sequencer step, roll hit,
  MIDI, preview, the keyboard itself) inverts its key once for 50 ms (= the
  VOICE LED trigger pulse): a black key goes dark, a white key lights. Notes
  outside the 16 keys show nothing. One pulse at a time: a new note moves
  the pulse at once (one mono track rarely plays two notes within 50 ms; a
  Tetrakt layer chord is one pitch).
- A paraphonic Colloid's second and later held keys set intervals and fire
  no trigger (as over MIDI), so they do not pulse; only the first key's
  trigger does.

### 1.6 Button routing details

- Presses taken: SHIFT+BAR (always), BAR and SEQ (while up). Their releases
  are taken by masks (`barMask`, `keyMask`), so a key pressed on the keyboard
  and released after exit still ends its note, and a SEQ step held before
  entry still gets its normal release.
- A taken BAR press still marks a PERF VOICE hold as "action occurred" and
  ends a SEQ double-click window, as any non-SEQ press does.
- The keyboard intercept runs before live erase, held COPY, the Load Scene
  and VOICE Scene-mask SEQ overlays and the Load/Save BAR text helpers: all
  of those are SEQ/BAR uses, and the keyboard owns SEQ and BAR while up.
- Event-ring overflow and exit: every held note ends (note-offs for MIDI
  keys, the overlay gesture ended) and the masks clear on overflow; the
  overlay stays up on overflow (it is a latch).

---

## 2. `buttonHandler.c` — the keyboard

- **State `bh_chroma`** (10 B, `.data`): `which`, `octave`, `track` (the
  entry the keyboard still owns, 0xFF none), `key` (sounding key), `keyMask`,
  `barMask`, `pulseKey`, `pulseEnd`.
- `buttonHandler_chromaPress()` — first line of `processPress()`.
- `buttonHandler_chromaRelease()` — first line of `processRelease()`.
- **State `bh_chromaNotes`** (18 B, `.bss`, Fix 1): `noteTrack`,
  `noteChannel`, `keyNote[16]` (note | 0x80 while the key holds a MIDI note).
  Kept out of `bh_chroma` (see §11 figures).
- `buttonHandler_chromaKeyOn()` (single → `midiNoteMode_noteOn()`, flam/roll
  → `buttonHandler_overlayStart()`), `_NotesOff()`, `_NotesHeld()`,
  `_EndNote()`, `_Exit()`, `_Paint()`, `_BaseNote()`.
- `buttonHandler_chromaPulseService()` — every `buttonHandler_tick()`:
  re-arms the note watch on the selected track, takes the latest note, runs
  the 50 ms pulse.
- `buttonHandler_chromaForceRelease()` — event-ring overflow block.
- `buttonHandler_seqHeldMask()` masks out `keyMask`.

## 3. `buttonHandler.c` — overlay engine touch points

The keyboard uses the overlay entries with `heldMidiNote` = none and no
`bh_overlayVoiceMask` bit, so `buttonHandler_overlayReconfigure()` (BAR/CC2
change) and `buttonHandler_overlayMidiNoteOff()` never touch a keyboard
note. Ownership is the one byte `bh_chroma.track`:

1. `buttonHandler_overlayStart()`: after the old gesture's final drain, a new
   gesture on the keyboard's track clears the ownership (the keyboard sets it
   again after its own call). A PERF VOICE or MIDI gesture on that track thus
   takes it over cleanly.
2. `buttonHandler_overlayEndTrack()`: clears ownership after the final drain.
3. Rhythm: `buttonHandler_overlayDrain()` reports a keyboard roll's hits with
   the keyboard BAR's `pv`; `buttonHandler_overlayTick()` publishes the
   keyboard BAR's `pv` to the live engine while a keyboard roll is held,
   otherwise the PERF overlay's as before. The live engine has one rhythm,
   so a MIDI/VOICE roll held at the same time as a keyboard roll plays the
   keyboard's rhythm (§9 Q3).

## 4. LEDs — the owned composite

- `led_ownedShow(seqBits, bar1, bar2)` / `led_ownedRelease()` in
  `ledHandler.c`: compute the physical bits of STEP1..16 and BAR2 (chain)
  into `dout_ownedMask[]` / `dout_ownedData[]`, and take the SW43 (BAR1) GPIO
  LED with `dout_setSw43Owned()`.
- `din_dout_exchange()` sends `(outputData & ~ownedMask) | ownedData`.
  `dout_setSw43Led()` is ignored while SW43 is owned; release re-sends
  ledHandler's current SW43 state.
- Every ledHandler layer (base, chase, blink, flash, pulse, fast blink) and
  every mode's painter keep running on the state underneath. That is what
  makes exit show "the appropriate display for whatever mode the menu is in"
  with no repaint, and why no other LED code needed changing.
- All writers and the SPI exchange are the main loop (S101), so no locking.

## 5. `MidiVoiceControl.c` — note watch

- `voiceControl_watchTrack` (0xFF none) / `voiceControl_watchNote`
  (note + 1): written in `voiceControl_enqueueTriggerLocked()` (already under
  PRIMASK, TIM3 or foreground); taken and re-armed by
  `voiceControl_noteWatch(track)`.
- **Why the enqueue, not the fire.** The first build called the hook from
  `voiceControl_fireTrigger()`. LTO inlines the whole fire path into
  `audio_check_and_render()`; that moved its register allocation and made
  nine automation-drain / special-writer loops 1–4 instructions longer per
  entry (`elf_hotloops`: 13 equal, 6 shrank, 9 grew). `noinline` on the hook
  did not help (3 still grew). At the enqueue the render function is
  byte-identical in size and every hot loop is equal (§6).

## 6. Checks run

**First build** (Fix 1 results are in §11):

- `make all && make img`: clean (only the usual newlib stub warnings).
- Baseline rebuilt from HEAD in a scratch worktree (removed afterwards).
- `elf_loopdump.py` + `elf_hotloops.py --compare`:
  - `mixer_calcNextSampleBlock`: 41 hot loops equal, 0 grew, 0 shrank; size
    0x31b8 both.
  - `audio_check_and_render.part.0`: 28 hot loops equal, 0 grew, 0 shrank;
    size 0x1058 both.
- No DSP file changed. The added control work: one byte compare per
  trigger enqueue; one PRIMASK-guarded byte exchange per `buttonHandler_tick()`
  pass; the 17-LED composite only on enter/transpose/pulse; 5 AND/OR per SPI
  exchange.
- No host test: the feature is panel and LED routing with no host harness.

## 7. RAM (approved by you 2026-11-24)

| Symbol | Bytes | Region | Owner |
|---|---:|---|---|
| `bh_chroma` | 10 | SRAM1 `.data` | `buttonHandler.c` |
| `dout_ownedMask` | 5 | SRAM1 `.bss` | `dout.c` (written by `ledHandler.c`) |
| `dout_ownedData` | 5 | SRAM1 `.bss` | `dout.c` (written by `ledHandler.c`) |
| `dout_sw43Owned` | 1 | SRAM1 `.bss` | `dout.c` |
| `voiceControl_watchTrack` | 1 | SRAM1 `.data` | `MidiVoiceControl.c` |
| `voiceControl_watchNote` | 1 | SRAM1 `.bss` | `MidiVoiceControl.c` |
| `bh_chromaNotes` | 18 | SRAM1 `.bss` | `buttonHandler.c` (Fix 1) |
| **Total** | **41** | SRAM1 | |

Not from the DTCM or SRAM1 earmarks (delay lines, Pattern data): these are
new statics. Flash +944 B.

## 8. Device tests (open)

1. SHIFT+BAR `<` in each mode (VOICE, STEP, PERF, FX, Load/Save, SHIFT+REC
   menu open): black keys lit, both BARs lit; screen unchanged.
2. Keys play the selected track at the `pb<` type; repeat entering with
   SHIFT+BAR `>` for `pb>`. Single, flam and roll each: the roll stops at
   release, uses `pv<` / `pv>`; a flam released early still plays its main.
2a. (Fix 1) Colloid on its own channel, PERF overlay off, single type: a held
   key sustains and releases on key-up, like the MIDI keyboard; two held keys
   play the interval; a key held across an octave change still releases.
3. BAR `>` ×3: only `>` lit, pitch up 3 octaves; a 4th press does nothing.
   BAR `<` back past the middle: only `<` lit. Exit and re-enter: octave kept.
   Power cycle: middle octave.
4. Running Pattern on the selected track: its notes blink once on the keys
   (default-pitch steps blink SEQ4 at the middle octave); no chase.
5. Live record on: keyboard notes and rolls record onto the selected track.
6. PERF with the PERF overlay `<` on: enter with SHIFT+BAR `>`; VOICE buttons
   still fire the `<` overlay; MIDI notes still use `<`; BAR presses only
   transpose. Exit: BAR `<` LED shows the PERF overlay again.
7. Exit in each mode: the SEQ row (Scenes in PERF, steps in VOICE/STEP, chase
   if running) and BAR LEDs are as the mode shows them.
8. Hold a key, turn a knob in VOICE mode: the knob edits the voice, not a
   held step.
9. Hold a roll key (or a held Colloid key) and exit with SHIFT+BAR: the note
   stops; releasing the key afterwards does nothing.
10. Muted selected track: keys silent.

## 9. Decisions taken in the build (tell me to change any)

- **Q1 Middle C.** Internal 60 (shown `C5`), not 63 (the voice's own pitch,
  shown `D#5`). Range: SEQ1 = 24 at −3, SEQ16 = 111 at +3.
- **Q2 Release.** Superseded by Fix 1 (§11): the release is the MIDI
  note-off (note modes for single types; a roll ends, a flam keeps its main
  under the overlay engine). The first build's panel VOICE rule was wrong.
- **Q3 One live rhythm.** While a keyboard roll is held, other live rolls
  (VOICE, MIDI) use the keyboard BAR's `pv` (the engine has one rhythm
  value).
- **Q4 SHIFT+BAR everywhere.** Including inside Load/Save name editing and
  during live erase, and SEQ is the keyboard even with COPY held or erase on.
- **Q5 Pulse length** 50 ms (the VOICE LED pulse).

## 10. Statements this makes out of date (for you to update)

- `MENU_UI_SPEC.md` (buttons, LEDs): SHIFT+BAR, the keyboard, the owned
  SEQ/BAR display.
- `PERFORMANCE_RECORD_SPEC.md` §1 (PERF surface / BAR overlay): BAR presses
  transpose instead of toggling the overlay while the keyboard is up; the
  overlay is kept.
- `MODULE_INTERCHANGE_SPEC.md` "BAR overlay trigger engine": a third user
  (the keyboard) with ownership via `bh_chroma.track`; the live rhythm rule.
- `MIDI_SPEC.md` (note modes, MidiNoteModes): the keyboard is a second caller
  of `midiNoteMode_noteOn()` / `midiNoteMode_noteOff()` (single types).
- `buttonHandler.h` enum comment still says BAR1/BAR2 have "no port
  functionality assigned" (stale since S080; code, not docs — left as is).
- Root `MEMORY.md` flash/SRAM figures (Open Items "Flash headroom") if you
  track them per session.

---

## 11. Fix 1 (2026-11-24): keys did not hold the note

**Report:** on a Colloid with a MIDI keyboard on the Colloid's channel,
holding a MIDI key sustains; holding a keyboard key did not.

**Cause:** with the PERF BAR overlay off, a received note on a Colloid on its
own channel goes to `midiNoteMode_noteOn()` → the paraphonic mode, which turns
on `sus` and the slot's MIDI hold until the note-off. The keyboard fired the
overlay engine's one-shot single instead, so nothing held. That broke the
"just as if played over MIDI" instruction; §1.3 and §9 Q2 were wrong.

**Fix (`buttonHandler.c`):**
- Single types (`vmx` `v10` `v64` `v32`): a key press is
  `midiNoteMode_noteOn(track, channel, note, 0, note, velocity, 0)`. The
  channel is the track's effective channel (`midiNoteMap_effectiveChannel()`).
  The velocity is 127/100/64/32. These are the same calls MidiNotes makes for
  a resolved note that no BAR overlay takes. The key's release is
  `midiNoteMode_noteOff()` for the same track, channel and note. So a Colloid
  on its own channel holds, and extra held keys become paraphonic intervals,
  exactly as from the MIDI keyboard. Other tracks get the MIDI single
  trigger, and LiveRecord gets what MIDI reports.
- Each key remembers its note (`bh_chromaNotes.keyNote[]`), so changing the
  octave while holding still releases the right note. A key on a newly
  selected track first sends note-offs for the keys held on the old track.
  Exit and event-ring overflow send note-offs for every held key.
- Flam and roll types keep the overlay engine, now with the MIDI note-off
  rule: a roll ends on release, and a flam keeps its main hit (B-12).
- As over MIDI: the PERF BAR overlay, when it is on, turns the Colloid's
  paraphonic mode off (`midiNoteMode_trackMode()`), and a Colloid on `glb`
  does not go paraphonic.

**RAM:** +18 B SRAM1 `.bss` (`bh_chromaNotes`: track, channel, 16 notes).
S102 total 41 B, approved by you 2026-11-24 (§7).

**Figures:** image 674,520 B (+944 B over HEAD before S102). Mixer hot loops:
41 equal. `audio_check_and_render`: 9 automation-drain / special-writer loops
are 1–4 instructions longer per automation entry (control rate). The code in
that function did not change. LTO's data layout did; moving the new fields
out of `bh_chroma` into their own `.bss` static did not help. Open.

**Device test:** Colloid on its own channel, PERF overlay off, enter with a
BAR whose `pb` is a single: a held key sustains and releases on key-up; two
held keys play the interval as from MIDI; a key held across an octave change
still releases.
