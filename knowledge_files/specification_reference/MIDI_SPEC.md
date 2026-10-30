# MIDI Specification and Code Reference

> **Current as of Session 084 close (2026-10-26), branch
> `dev-ph7-voice_expansion`.** The S083 MIDI implementation is
> hardware-checked; the S084 changes (overlay rolls and flams on the master
> tick, 15 overlay types, external clock interpolation) are built and
> host-checked, hardware pending. Where this document and the code
> disagree, the code wins; fix this document in the same change.

The LXR-02 MIDI interface:
- global MIDI settings and the note-to-track map;
- control changes: CC1/CC2, NRPN and MIDI Learn;
- program and Bank change; realtime and transport;
- routing and transmit.

**Part I** describes what the instrument does, for users and for anyone
changing behaviour. **Part II** is the code reference for the developer who
maintains or extends `Core/MIDI/`: module structure, data flow, every
public API, integration points, invariants, how to extend it, and how to
test it. **Part III** records the decisions and their history.

- **History:**
  - Decided in Session 082 planning (2026-10-16/17).
  - Built in Session 083 (2026-10-21/22).
  - The full decision and implementation record is
    `knowledge_files/log_archive/083_SESSION_HANDOFF_LOG.md`.
  - Session 084 (2026-10-26) moved overlay roll/flam timing onto the
    master tick, unified the overlay roll list with the STEP list, and
    interpolated external clock ticks (`084_SESSION_HANDOFF_LOG.md`).
  - The background Bank load and Scene 17 that MIDI uses were built in
    Session 082 (`082_SESSION_HANDOFF_LOG.md`).
- **Related documents:**
  - `PATTERN_DYNAMIC_STACK.md` §6.2a (write priority), §6.5 (master-clock
    anchors), §6.7 (live record), §4 (9-bit target IDs);
  - `BANK_PRESET_ARCHITECTURE.md` (Scene settings, Morph, edit masks);
  - `FILESYSTEM_SPEC.md` (`settings.cfg`, `midilrn.cfg`, `sceneset.scg`);
  - `MODULE_INTERCHANGE_SPEC.md` (module APIs, PERF surface, SHIFT+REC);
  - `STORAGE_SRAM_MANIFEST.md` (RAM);
  - `DEV_MODES.md` (trace stage `m`, stall observers).

## Contents

**Part I — Behaviour**
1. Terms
2. Settings
3. Receive pipeline: filtering, consumption, routing
4. Notes
5. Control changes
6. MIDI Learn
7. Program change and Bank change
8. Realtime and transport
9. Transmit
10. Scene 17 and the background Bank load
11. Storage

**Part II — Code reference**
12. Architecture overview
13. Module reference (`Core/MIDI/`)
14. Integration points outside `Core/MIDI/`
15. Execution contexts and concurrency
16. How to extend
17. Testing and diagnostics
18. RAM
19. Binding rules

**Part III — Decisions**
20. Decision record
21. Maintaining this document

---

# Part I — Behaviour

## 1. Terms

| Term | Meaning |
|------|---------|
| Track | Sequencer track T1..T7 (code index 0..6). |
| Voice / slot | Instrument slot V1..V6 (code index 0..5). T6 and T7 both play V6 (closed/open HiHat) and move together. |
| G | The global MIDI channel (MIDI page `ch`), 1..16, or `off`. |
| Track channel | A track's MIDI channel (STEP track settings `ch`, per Scene): `off`, `glb` (follow G), 1..16. |
| Track note | A track's MIDI root (STEP track settings `nte`, per Scene): `off`, `on` (no root), 1..127. |
| Effective channel `ec(t)` | The track channel if set, else G. None when the track channel is `off`, or when it is `glb` and G is `off`. |
| Voice channel | A channel that is some track's effective channel and is not G. |
| Root | The lowest note of a track's note range. It plays the voice's default pitch. |
| Internal note | The note value the voice receives: 63 = default pitch; pitch offset = note − 63 semitones (`Oscillator.c`). |
| Target ID | The canonical 9-bit parameter ID shared with step automation, LiveRecord, NRPN and Learn (§5.4). |
| Consumed | The LXR acted on a received message (§3.2). Consumed messages are not forwarded. |
| Active Scene | The Scene being viewed and written to (MEMORY.md definition). Scene 17 while it is active. |
| Played Scene | The Scene a track is playing; differs from the active Scene with per-track playback. |
| Scene 17 | The background Scene that keeps playback going through a Bank load (§10). |
| BAR overlay | The PERF trigger overlay `<`/`>` (one state, set by the BAR buttons or CC2). |

## 2. Settings

### 2.1 Global (MENU_MIDI_PAGE; `settings.cfg`)

| Cell (shown) | Key | Values | Meaning |
|--------------|-----|--------|---------|
| `snc` | `ext_sync` | `off usb din pls aut` | Sync source. MIDI realtime is used for `usb`, `din` and `aut` (AUTO priority, low to high: internal, USB, DIN, jack pulse). |
| `ch` | `midi_chan_global` | `off`, 1..16 | G. With `off`, nothing is received or sent on a global channel. |
| `txf` | `midi_filt_tx` | 16 combinations of N R C P (`off N R RN C CN CR CRN P PN PR PRN PC PCN PCR all`) | What the LXR **sends** from itself (§9). It never affects pass-through. |
| `rxf` | `midi_filt_rx` | same | What the LXR **consumes** (§3). |
| `mrt` | `midi_routing` | `off UtD DtD DtU bth DtA AtD all` | Pass-through routing (§3.3). |

Filter letters (stored bits: N = 1, R = 2, C = 4, P = 8):
- **N:** notes.
- **R:** realtime (clock and transport, plus SPP/MTC).
- **C:** control (NRPN, Learn CCs, CC1, CC2; on TX, the learned CC if one
  exists, else NRPN).
- **P:** program change and Bank change (CC0).

`RXF P` governs both PC and CC0; there is no separate Bank-change enable.

**Defaults:** a card without these keys loads RXF = TXF = `off` and
MRT = `off`, so the LXR consumes, sends and forwards nothing until they
are set. Set RXF and TXF to `all` for normal use and for testing.

### 2.2 Per track, per Scene (STEP track settings page 2; `sceneset.scg`; AutoSave Scene cells)

The page reads `ch nte lop rmp`.

| Cell | Full view | Menu values (in order) | Default |
|------|-----------|------------------------|---------|
| `ch` | Voice Channel | `off`, `glb` (follows G), 1..16 | `glb` |
| `nte` | Track Note | `off`, `on` (no root), note names 1..127 | `on` |

**Stored values** (`sceneset.scg`, AutoSave):
- `ch`: 0 = `glb`, 1..16, 17 = `off` (`MIDI_TRACK_CHANNEL_OFF`);
- `nte`: 0 = `on`, 1..127, 128 = `off` (`MIDI_TRACK_NOTE_OFF`);
- the menu converts between the two orders (§14.1).

**Effects:**
- **`ch off`:** the track has no MIDI channel at all: no notes in or out,
  no voice-channel CC1/NRPN/Learn/PC, no per-track PC out.
- **`nte on`** (the default): no root; the track shares its channel's
  notes (stripe on G, split elsewhere, §4.1).
- **`nte 1..127`:** the root of the track's receive range. It is a root,
  not a pitch: the root plays the default pitch.
- **`nte off`:** the track takes no part in the note map. It receives no
  notes, sends none, and takes no stripe/split position, but keeps its
  channel for CC1/NRPN/Learn/PC.

**Other rules:**
- MIDI note 0 cannot be a track's root.
- Channel and note are Scene settings, so the note map can change on a
  Scene change.
- Changing a track's `ch` or `nte` ends the track's sounding MIDI note.

### 2.3 PERF overlay settings used by MIDI (SHIFT+REC page; `settings.cfg`)

| Cell | Key | Meaning |
|------|-----|---------|
| `pb<` / `pb>` | `perf_btn_lt/rt` | Trigger type of overlay `<` / `>` (15 types: 4 single velocities, 2 flams, and the nine STEP roll rates `1/8 8tr 16 16t 32 32t 64 64t 128` = step index type − 5, S084 P3). Stored values 6..11 changed meaning; no migration. The first hit fires in the foreground, repeats and flam mains on the 96-PPQ master tick (so they follow the internal tempo or the external clock; S084 P2). |
| `pn<` / `pn>` | `perf_note_lt/rt` | Panel note of the overlay (`def` = default pitch). MIDI notes always use their own resolved pitch. |
| `pv<` / `pv>` | `perf_vel_lt/rt` | Roll velocity rhythm (the step `rtm` list: `off`, `1-2`..`4-5` halve, `1+2`..`4+5` double); roll types only. |

SHIFT+REC page 1 is `pb< pn< pb> pn>`; page 2 is `pv< pv> qnt rtk`.

---

## 3. Receive pipeline

### 3.1 Order

1. Frame the message from its port (DIN byte stream, or USB event packet)
   and stamp its arrival time.
2. SysEx is pass-through only: forward per MRT.
3. While a background Bank load runs, drop (do not consume) everything
   except realtime, note on/off and CC other than CC0/CC32 (§10).
4. System common (MTC quarter frame, SPP, Song Select) goes to transport
   (§8) and is forwarded whether or not it acted.
5. If the type is blocked by RXF, it is not consumed.
6. Otherwise offer it to its consumer: notes §4, CC0 §7.2, other CCs §5–§6,
   PC §7.1.
7. If no consumer acted, forward it according to MRT (§3.3).

Realtime messages (Clock, Start, Continue, Stop) take a separate,
timestamped path into TIM3 (§8). They are **always** forwarded according to
MRT, whether or not they were consumed.

### 3.2 Consumed

A message is consumed when it is:
- a note that resolved to a track (a muted track's note is consumed and
  silent);
- an NRPN protocol CC on G or a voice channel while RXF C is on:
  - CC98/99/100/101/38 always;
  - CC6/96/97 only when the selected number is a valid target there;
- CC0 that requested a Bank load; CC1 or CC2 that acted;
- a CC that matched a Learn entry, or was captured by Learn;
- a PC 0..15 that acted.

Everything else is forwarded: unmapped notes, CCs on unused channels, PC
16..127, PC to an unavailable Scene, SysEx, pitch bend and pressure.

### 3.3 Routing (MRT)

| Value | Routes |
|-------|--------|
| `off` | none |
| `UtD` | USB in → DIN out |
| `DtD` | DIN in → DIN out |
| `DtU` | DIN in → USB out |
| `bth` | USB → DIN and DIN → USB |
| `DtA` | DIN → DIN and DIN → USB |
| `AtD` | USB → DIN and DIN → DIN |
| `all` | USB → DIN, DIN → DIN, DIN → USB |

- **USB → USB is never routed.**
- **USB framing:** every forwarded message is re-framed for USB with cable
  0 and the correct code index. Channel messages use status >> 4; MTC and
  Song Select 0x2; SPP 0x3; Tune Request 0x5; realtime and SysEx bytes 0xF
  (single-byte packets).

---

## 4. Notes

### 4.1 Note → track map

The map is computed per message from the active Scene's track settings and
G. For an incoming note (channel c, note n):

- **S** = the tracks whose effective channel is c, in track order,
  excluding tracks whose note is `off`. A track set explicitly to G's
  channel number counts as a G track.
- **A** = the tracks in S with an assigned note; **U** = the rest.

**Rule 1 — assigned ranges (any channel).**
- Sort A by note. Each assigned track owns the notes from its own note up
  to one below the next assigned note, or up to 127 for the last one.
- Internal note = 63 + (n − its note).
- If two tracks have the same note, the lower-numbered track owns the
  range and the other owns nothing.

**Rule 2 — the region below the lowest assigned note** (all of 0..127 if A
is empty):

- **c = G (stripe):** note n triggers `U[n mod |U|]` at internal note 63.
  Every note in the stripe plays the default pitch.
- **c ≠ G (split):** the region is divided into |U| contiguous blocks, in
  track order.
  - Block sizes differ by at most one, and the lower tracks get the extra
    notes.
  - Each block starts at its track's root.
  - Internal note = 63 + (n − block start).
- **U empty:** the region is unmapped (not consumed, so forwarded).

**Rule 3 — one voice per note.** A note triggers at most one track.

**Rule 4 — pitch clamp.** The internal note is clamped to 127, so a range
rises at most +64 semitones above its root. Higher notes in the range play
at +64.

**Channel cases:**
- Tracks with their own channel never take part in G's stripe.
- With G `off`, the G rules do not apply and `glb` tracks are unmapped.
- A track with `ch off` is on no channel.

### 4.2 Worked examples

| Setup | Result |
|-------|--------|
| G = 10; no track settings | Ch 10: note 0 → T1, 6 → T7, 7 → T1, … 36 → T2, 60 → T5. All at default pitch. |
| G = 10; T3 `nte` 48, T5 `nte` 60 | Ch 10: notes 0..47 stripe over T1 T2 T4 T6 T7 (n mod 5). 48..59 → T3, +0..+11 semitones. 60..127 → T5, +0..+64 (notes 124..127 all +64). |
| T1, T2, T3 `ch` 2; no notes | Ch 2: T1 0..42 (43 notes), T2 43..85 (43), T3 86..127 (42). Note 50 → T2, +7. |
| Ch 2: T1 (no note), T2 `nte` 36, T3 (no note), T4 `nte` 60 | Below 36: T1 0..17, T3 18..35 (each chromatic from its root). T2 36..59. T4 60..127. |
| G = 10; T3 `nte off` | Ch 10 stripes over T1 T2 T4 T5 T6 T7 (n mod 6); T3 receives no notes but still answers NRPN/CC1 on its channel. |

### 4.3 What a note does

**Note-on:**
- One resetting trigger of the resolved track at the note's velocity, with
  the internal note as pitch, through the sequencer's trigger path. The
  synth, the trigger jack and TX N all fire, as for any LXR trigger.
- A muted track does not sound (the note is still consumed).
- Each note-on resets the voice's held automation and ends its R12 override
  (S081 step reset rule).

**Overlay action:**
- When the BAR overlay is `<` or `>` (one state, set by the PERF BAR
  buttons or by CC2 on G, §5.3, in any mode), the note performs that
  overlay's action instead: one of 15 trigger types (S084 P3).
- It plays at the note's resolved pitch, not the overlay's `pn` setting.
- Each track has its own repeat state, shared with the panel's VOICE
  overlay: rolls on several tracks run at once, and a new hit on a track
  replaces only that track's roll.
- A roll repeats until that note's note-off. Its velocity starts at 100
  and follows the overlay's `pv` rhythm. Since S084 the repeats fire on the
  sequencer's 96-PPQ master tick at exactly the STEP roll rates, spaced from
  the note's arrival, so they follow the internal tempo or an external
  clock and play the same as a recorded or STEP roll.
- A flam ignores its note-off (the main hit always plays, 12 master ticks
  = 1/32 after the grace hit).
- MIDI-held rolls are not VOICE holds: they neither hold nor join the
  looper.

**Note-off:** stops an overlay roll. A single trigger ignores it.

**Recording:**
- Each trigger is reported to LiveRecord (source MIDI), with the overlay
  kind where one applies.
- Note pitch is recorded like an overlay trigger's; a default-pitch hit
  records no note special.
- The report carries the note's **arrival age**, so placement uses the time
  the note arrived, not the time it was processed. The shift is limited to
  one step interval.
- The sound itself plays when the note is processed.

---

## 5. Control changes

### 5.1 Reserved CCs (never learnable)

| CC | Use |
|----|-----|
| 0 | Bank change (§7.2) |
| 1 | Morph (§5.2) |
| 2 | BAR overlay (§5.3) |
| 6, 38 | Data entry MSB/LSB (NRPN) |
| 32 | Bank select LSB (reserved, ignored) |
| 96, 97 | Data increment/decrement (NRPN) |
| 98, 99 | NRPN number LSB/MSB |
| 100, 101 | RPN number LSB/MSB |
| 120..127 | Channel mode messages |

Every other CC (3..5, 7..31, 33..95, 102..119) may be learned (§6).

### 5.2 CC1 — Morph

- **On G:** the global Morph (`mrp`), which sets **all** Morph:
  - the six voice Morph amounts;
  - the Effect Morph (`fxm`), applied like an `fxm` menu edit: it clears a
    Pattern `fxm` override and holds over an FX Morph lock until the next
    Morph automation.

  It fans out through the edit mask. It is **never recorded** (G-1,
  binding), including the voice and Effect amounts it sets. The panel `mrp`
  edit does the same.
- **On a voice channel:** the Morph amount of every voice whose track has
  that effective channel. It is recorded as an individual voice Morph edit
  (`1vm`..`6vm`).
- **Value:** 0..126 → value × 2; 127 → 255 (the 7-bit Morph translation).

### 5.3 CC2 — BAR overlay

On G only, in any UI mode. On other channels CC2 is not consumed and is
forwarded. Values:
- 0 = overlay off (single trigger, the default);
- 1 = BAR `<` overlay;
- 2 = BAR `>` overlay;
- other values are ignored and forwarded.

CC2 **sets the PERF BAR overlay itself**, exactly like pressing BAR `<` or
`>`. There is one overlay state:
- in PERF, the BAR LEDs follow (unless the looper owns them; they are
  restored when it disengages or on PERF entry);
- the panel VOICE buttons in PERF fire the overlay;
- received notes perform it (§4.3), in any mode;
- held rolls re-configure (roll → roll keeps its phase; single/flam → roll
  starts repeating; overlay off stops repeats).

The last BAR press or CC2 wins. The overlay persists until changed and is
off at boot. The trigger types are `pb<`/`pb>`, and a roll's velocity rhythm
is `pv<`/`pv>` (§2.3).

### 5.4 NRPN — parameter address space

**NRPN number = the canonical 9-bit target ID.** CC99 = ID >> 7, CC98 = ID &
0x7F.

| IDs | Targets |
|-----|---------|
| 0..383 | Voice parameters: `slot × 64 + descriptor index` (slot 0..5) |
| 384..447 | Scene targets: `1vm..6vm`, `7dc`, `1ou..6ou`, `1fx..6fx`, `fxm` 404, track length 405..411, scale 412..418, shuffle 419..425; 426..447 unused |
| 448..510 | Effect parameters: Effect-local index 0..62 |
| 511 | Off (ignored) |

- **Instrument-agnostic.** A voice ID addresses whatever descriptor sits at
  that index in the slot's current instrument type, and an Effect ID the
  current Effect type's parameter. The same NRPN means different parameters
  on different types, with no special casing. A chart per instrument type
  and per Effect type is the user's reference (not yet published).
- **Unused IDs** are ignored and the message is forwarded.
- **Channels:**
  - Every ID works on G.
  - On a voice channel only that voice's IDs work: its voice block and its
    per-voice Scene targets (`Nvm`, `Nfx`, `Nou`, `7dc` for slot 6, and
    the track length/scale/shuffle of its tracks).
  - Numbering is absolute everywhere.
  - Other Scene IDs (`fxm`) and Effect IDs are ignored on voice channels.
- **Protocol** (state per port and channel, up to 8 contexts per port):
  - CC99/98 select the number;
  - CC6 carries the value;
  - CC38 is ignored (consumed);
  - CC96/97 step the current value ±1;
  - any RPN selection, including RPN null (CC101/100 = 127/127), deselects
    the NRPN until a new number arrives.

### 5.5 Values

All MIDI parameter values are **absolute 7-bit** and pass to the parameter
unchanged (identity, as in step automation).
- They are clamped to the parameter's own domain (descriptor maximum,
  enumerated lists, target tokens), the same rule step automation applies.
- Drum `osc2_mod_type` (`mod`): 0 `fm`, 1 `mix`, 2 `snc`, 3–127 `rng`
  (S085 P1; before S085, 1–127 gave `mix`).
- `oscN_wave` (`wav`): 0–7 the built-in waves (`Sin Tri Saw Rec Noi Cym Re3
  Sw3`), 8 on the installed user samples (since S086; before, samples
  started at 6); values above the last sample clamp to it.
- `osc1_position` (`pos`, S086): 0–127, identity. It was appended as the
  last descriptor of each instrument (local index 39; Snare 38), so its NRPN
  number is slot × 64 + that index; every earlier NRPN number and MIDI Learn
  row is unchanged. A move glides the oscillator phase within one block
  (`INSTRUMENTS_DSP_REFERENCE.md` §6.1.1).
- `osc1_run` (`run`, S088): 0 `rst`, 1 `fre`, 2 `itp`; values above 2 clamp
  to `itp`. Appended after `osc1_type`, so its NRPN number (slot × 64 +
  index) and MIDI Learn row follow automatically with no renumbering. It is
  a selector: one value per step when automated, never morphed.
- The only translation is the Morph amounts (§5.2).
- WIDE8 Effect rows expand through the Effect type's 7-bit expansion, as
  automation does.

### 5.6 Write rules

**A MIDI write is a menu edit.** It has the same effect, the same priority
and the same actions against automation and recording as a menu edit of
that parameter, through one shared commit used by the menu cells and MIDI
(`menu_commitTargetEdit()`, §14.1):
- **Instrument parameters:** R12. The move sounds at once and suppresses
  that parameter's automation until the voice's next trigger, a transport
  change or a Scene change.
- **Effect parameters:** R13 (menuHold beats FX lock and track overlay
  until the next automation).
- **Scene targets:** their per-target automation clears.

MIDI always writes the **Normal** endpoint; a panel Morph view does not
redirect it.

- **Scope:** the active Scene, fanned out through the VOICE edit mask
  exactly like a knob edit (`bank_editTargetMask()`).
  - While Scene 17 is active, only 17.
  - During an unlocked Bank load, Scenes still loading are skipped.
  - A track playing another Scene (per-track playback) is not written.
- **Recording:** in LATCH/OVERDUB each successful store is reported with
  `liveRec_paramIn(targetId, MIDI)`. Track length is not recorded, and
  global Morph is never recorded.
- **Display:** a parameter shown in the menu repaints promptly
  (rate-limited by the knob-repaint coalescing).
- **No echo:** a change that came in over MIDI is never transmitted (§9).

---

## 6. MIDI Learn

### 6.1 Model

Learn is **global** (device-wide), held in RAM and stored in `midilrn.cfg`
(§6.4), with up to **128 entries**. Each entry maps one CC on one channel
kind to one parameter:

| Field | Values |
|-------|--------|
| CC | 3..119, excluding the reserved CCs (§5.1) |
| Channel kind | `g`: on G. `v`: on the target voice's effective channel (voice targets and per-voice Scene targets only). |
| Target | Voice slot 1..6 + descriptor; Effect + parameter; or Scene target |
| Type | Instrument type (voice targets) or Effect type (Effect targets); none for Scene targets |

The channel *kind* is stored, not the number, so entries follow later
channel edits.

### 6.2 Rules

- **One CC → many parameters:** each is its own entry (macros).
- **One parameter → at most one CC.** An entry is unique on (target, type).
  Learning a parameter that already has an entry replaces that entry's CC
  and channel kind.
- **Type match:** an entry applies only while its type is loaded in the
  target slot (or the active Scene's Effect has its type). This is checked
  against the active Scene (Scene 17 while active). A slot can therefore
  hold different entries for different instrument types, with the same CC.
- **Slot-specific:** a learned CC on a voice channel shared by several
  voices affects only its entry's slot. CC1 is the only CC that reaches
  every voice on a shared channel.
- **Several matches:** all matching entries are applied.
- **Values and writes:** §5.5, §5.6.
- **TX** uses the first entry matching the current type (§9).

### 6.3 Learn gesture

1. **Arm:** press SHIFT, then PLAY, then REC, holding the earlier buttons.
   - These presses perform none of their own functions: no transport
     toggle, no record toggle, no SHIFT+REC menu, no chain arm.
   - **SHIFT+PLAY no longer toggles the transport in any mode**;
     SHIFT+PLAY+SEQ in PERF still opens the chain editor.
   - Learn latches, and SHIFT, PLAY and REC flash.
2. **Pick a target:** touch a parameter by turning an RV1–RV4 knob or
   editing a cell with the encoder.
   - It must have a MIDI address (voice, Scene target, Effect).
   - That parameter becomes the target and shows `LRN` as its value. The
     touch does not change the value.
   - In a SHIFT Morph view the target is still the parameter itself: Learn
     has no Morph endpoint.
   - Touching a cell with no address (BPM, settings, step data) edits it
     normally, and Learn keeps waiting.
3. **Send a CC.** A valid CC is not reserved and arrives on G, or (voice
   targets only) on the target voice's effective channel.
   - The entry is stored or replaced.
   - `midilrn.cfg` is rewritten at once (§6.4).
   - The display returns to normal.
4. **Cancel:**
   - pressing SHIFT, PLAY or REC cancels; the press is consumed;
   - a MODE or page change cancels;
   - a Bank load cancels, also a MIDI CC0 load started while already on
     Load/Save.

   While Learn waits, any other CC is processed normally and notes keep
   playing.
5. **Full table:** a Learn that would need a new entry when 128 are used is
   refused (`FUL` shown for 1 s) and ends.

There is no remove in the UI. To remove an entry, delete its line from
`midilrn.cfg`.

### 6.4 `midilrn.cfg`

The table is its own root file, `midilrn.cfg`. It has a header, one line
per entry, then a `lines=N` terminator. The keys are readable, so the file
can be edited by hand:

```
format=helicase.midilearn
version=1
learn=020,g,v1,drm,filter_freq
learn=021,v,v3,snr,noise_freq
learn=022,g,fx,flt,filter_freq
learn=023,g,scn,1vm
lines=6
```

**Format:** `learn=<CCC>,<g|v>,<v1..v6|fx|scn>,<type token>,<descriptor
key>`. Scene targets have no type token; their key is the Scene target
token.

**Loading:**
- On load, keys resolve to indices once (instrument descriptors, Effect
  rows, Scene target table).
- A malformed or unresolvable `learn=` line is **skipped**; the rest of the
  file still loads. An unresolvable row is written back as a `#` comment.
- `lines=N` counts every line before it (2 + entries). Only the
  `midilrn.tmp` boot recovery checks it, so deleting a line by hand is
  safe.
- A wrong `format` or `version` line empties the table.
- A missing file is an empty table: a card has no `midilrn.cfg` until its
  first Learn.

**Writing:**
- **Every Learn that stores or replaces an entry rewrites the whole file at
  once** (no debounce), with the safe method: `midilrn.tmp`, sync, remove
  the old file, rename. A `FUL` refusal writes nothing.
- If the card is busy (AutoSave, a Load/Save command, a background Bank
  load), the write waits until it is idle.
- A Learn made during a write causes one more write.
- Learns made during a background Bank load are kept and written.
- A failed write retries once; after that it waits for the next Learn.
- `midilrn.cfg` keeps being written while Scene 17 is active (it is not
  AutoSave).

**Boot:**
- The file is read right after `settings.cfg`, before audio.
- A leftover `midilrn.tmp` is promoted when complete and discarded
  otherwise.
- A settings load does not change the table.

Details: `FILESYSTEM_SPEC.md` "`midilrn.cfg`".

---

## 7. Program change and Bank change

### 7.1 Program change (RXF P)

Only PC 0..15 is data, selecting Scenes 1..16. A PC to an unavailable
Scene (not resident, or still loading) and PC 16..127 are not consumed.

- **On G:** a whole-Scene change, exactly like a PERF SEQ press, in any
  mode and transport state:
  - `scd` division timing and the `pcr` reset apply;
  - with the transport stopped it is instant;
  - it releases Scene 17 if that is active;
  - outside PERF mode the PERF Scene LEDs are not repainted.
- **On a voice channel:** every track whose effective channel is that
  channel switches to play that Scene, exactly like hold VOICE + SEQ:
  - T6 and T7 switch together;
  - each moved track is realigned to the master clock, so LiveRecord
    starts a new take;
  - no queue; Scene 17 is not released.
- If a channel is both G and a voice channel, G wins.
- **During a Bank load:** dropped (§10).

### 7.2 Bank change (CC0 on G, RXF P)

- **Request:** CC0 value v loads Bank slot v (000..127) with the background
  Bank load: `preset_requestBackgroundBankLoad(v, 0, 0,
  PRESET_BANK_ORIGIN_MIDI)`. MIDI origin is always Replace with all 16
  Scenes selected. CC32 is ignored.
- **When the request starts:**
  - It is held as the one pending request (latest wins).
  - It starts only when Preset, the filesystem facade and the Menu are
    idle.
  - It then closes every latched surface and session, cancels a waiting
    Learn, and switches the UI to Load/Save → Bank at slot v, exactly as if
    it had been selected in the Menu.
  - After the slot's preview it acts as OK. A missing slot ends FAILED.
- **During the load:** as for a Menu load.
  - From phase 5 of a load that runs on Scene 17 (or of a running direct
    load), the panel is unlocked and the user may leave Load/Save.
  - A load started with the transport stopped keeps the UI on Load:[Bank]
    until it ends.
- **Playback** continues through Scene 17 (§10) when the transport is
  running.
- **Dropped:** CC0 arriving during a load; CC0 for an empty or invalid
  slot.
- A running copy/clear operation is dropped or completed, then the load
  runs.

---

## 8. Realtime and transport (RXF R, `snc`)

| Message | Behaviour |
|---------|-----------|
| Clock | Drives the sequencer when `snc` selects its source (24 PPQ → 96 PPQ internal). |
| Start | Restart from the Pattern start (arms on the next clock). |
| Continue | Resume from the stored position: where the transport stopped, or where an SPP located it. |
| Stop | Stop; the first unplayed tick is stored as the resume position. |
| Song Position Pointer | Honoured while stopped only: sets the resume position (SPP × 24 internal ticks). Tracks re-project through the master-clock anchors (`PATTERN_DYNAMIC_STACK.md` §6.5) at Continue. |
| MTC | Starts the sequencer when 8 zero quarter frames (0:0:0:0) arrive; stops after a 100 ms timeout. Informational while running or externally clocked. |
| Start/Stop/Continue/SPP with `snc = off`, or from a source `snc` does not select | Ignored. |
| MMC | Not supported. |

**Rules:**
- Realtime bytes are timestamped at arrival and handled in TIM3.
- **AUTO source selection:** a jack clock suppresses MIDI clocks, and DIN
  suppresses USB while DIN sync is fresh (500 ms hold).
- While slaved to external clock, the LXR does not generate its own clock.
- **Clock ticks are interpolated (S084):** each incoming clock owns 4 of the
  96-PPQ scheduler ticks (a jack pulse owns `stepSize × 3`). The clock edge
  runs the first; the sequencer's tempo timer, whose period is the measured
  external tempo, runs the other three between clocks; a tick still owed
  when the next clock arrives is caught up first. Rolls (STEP and overlay),
  shuffle and micro-timing are therefore even within a clock period. Start,
  Stop, Continue and an external reset drop any owed ticks.
- While stopped under external sync, PERF/MIDI overlay rolls keep playing at
  the measured tempo (the last measured one if no clock arrives).
- Received Start/Stop/Continue are forwarded per MRT but not re-sent by
  the LXR, so there are no duplicates downstream.

---

## 9. Transmit (TXF)

LXR-generated messages go to **both** USB and DIN. A message whose channel
is off (G off with the track `glb`, or the track `ch off`) is not sent.
Changes that arrived over MIDI are never echoed.

**N (notes):**
- Every trigger the LXR plays is sent: sequencer, overlay, preview and
  MIDI-in triggers.
- **Channel:** the track's effective channel in the Scene the track plays.
- **Note:** the inverse of §4.1 (root + internal − 63), so recorded LXR
  output plays back identically.
- Nothing is sent when the pitch cannot be expressed in the track's own
  range:
  - a stripe track sends only its default pitch (as its root);
  - a ranged track sends only root..range end;
  - a `nte off` track sends nothing.
- Note-off is tracked per track (note and channel), so tracks sharing a
  channel cannot leave hanging notes. Stop, mute, Scene changes and
  `ch`/`nte` edits end notes.

**R (realtime):** clock while the LXR is clock master; Start, Continue,
Stop on transport changes.

**C (parameters):**
- **What is sent:** locally made parameter changes (knob, encoder), not
  step-automation playback.
- **How:**
  - as the parameter's Learn CC (on G, or the voice channel for kind `v`)
    if it has one for the current type;
  - otherwise as NRPN on G;
  - when G is off, voice targets go as NRPN on the voice channel, and
    Scene and Effect targets send nothing.
- **Coalescing:** to the latest value per target. A change to another
  target first sends the pending one.
- **NRPN number CCs** are resent only when the number or channel changes,
  per port.
- **Throttle:** at most one parameter message every 10 ms, and only with 9
  bytes free in the DIN FIFO, so clock and notes are not delayed.

**P (program/Bank):**
- PC on G on a whole-Scene change.
- PC on each moved track's channel on a panel per-track change (not on G,
  one per channel).
- CC0 on G after a successful Menu Bank load.
- Scene 17 has no PC number: nothing is sent for it.
- **No echo:**
  - a Scene change started by a MIDI PC sends no PC, also when its queued
    change fires later;
  - a chain-fired change does send its PC;
  - a MIDI-started Bank load sends no CC0, and a failed load sends none.

---

## 10. Scene 17 and the background Bank load

Authority: Session 082 (`082_SESSION_HANDOFF_LOG.md`,
`MODULE_INTERCHANGE_SPEC.md` "Background Bank load"). For MIDI:

- **Scene 17 as the active Scene:** while it is active, the note map, Learn
  type matching and NRPN/CC writes act on it, and writes fan out to 17
  alone.
- **PC:** a PC on G leaves 17; a PC on a voice channel moves only those
  tracks.
- **Drop filter:** while a load runs, only these are acted on:
  - realtime (0xF8–0xFF);
  - note on/off;
  - CC, except CC0 and CC32.

  Program change, pitch bend, pressure, MTC quarter frame and song
  position/select are dropped. Dropped messages count as **not consumed**,
  so they are still forwarded per MRT.
- **After the user leaves 17 mid-load:** the active Scene is a loaded or
  kept resident Scene. Fan-out excludes Scenes the load has not finished.
- **Chains:** chain conditions do not fire while a Bank load runs. A PC
  arriving after the load is a normal whole-Scene change.
- Scene 17's track MIDI channel/note settings are copied from each track's
  played Scene at the copy.

---

## 11. Storage

| Data | Where | Notes |
|------|-------|-------|
| `snc ch txf rxf mrt` | `settings.cfg` | `midi_chan_global` 0 = off; `midi_routing` 0..7 as §3.3; filters 4-bit N R C P. |
| `pb pn pv` (overlay) | `settings.cfg` | `perf_btn_*`, `perf_note_*`, `perf_vel_*`. |
| Learn table | `midilrn.cfg` (root) | §6.4; written after every Learn. |
| Track `ch` / `nte` | Scene settings: `sceneset.scg` `midi_channel` (0..17) / `midi_note` (0..128), AutoSave Scene cells | §2.2 stored values. |
| BAR overlay state, NRPN contexts, parameter TX cache, note-off records | RAM only | Reset at boot. |

No migration of older files (standing project rule). A `learn=` line in
`settings.cfg` from the first S083 build is ignored.

---

# Part II — Code reference

## 12. Architecture overview

### 12.1 Module map

`Core/MIDI/` holds single-role modules; each has one owner role and one
prefix.

| Module | Prefix | Owns | Context |
|--------|--------|------|---------|
| `MidiDefs.h` | `midiDefs_` | `MidiMsg`, status and CC numbers, the reserved-CC rule, filter bits, the MRT table, channel/origin/target conventions, the track `off` values | — |
| `MidiBuffer.c/h` | `midiBuffer_` | SPSC byte buffers (DIN TX normal and realtime lanes; DIN RX with per-byte 16-bit stamps) | ISR / foreground |
| `MidiDin.c/h` | `midiDin_` | USART3 driver (PB10 TX / PB11 RX, 31,250 baud), RX stamping, realtime diversion, whole-message TX | USART3 ISR (priority 5); senders under PRIMASK |
| `MidiIn.c/h` | `midiIn_` | Shared settings (G, RXF/TXF, MRT); DIN framing; USB intake; the Bank-load drop filter; consume-then-route dispatch; the main-loop service | Foreground |
| `MidiTransport.c/h` | `midiTransport_` | Timestamped realtime ring; TIM3 realtime handler with the `snc` gate; SPP; MTC | TIM3 (realtime); foreground (SPP/MTC) |
| `MidiNoteMap.c/h` | `midiNoteMap_` | §4.1 resolver and its TX inverse; stateless; host-testable | Foreground and TIM3 |
| `MidiNotes.c/h` | `midiNotes_` | What a received note does; CC2 → BAR overlay | Foreground |
| `MidiControl.c/h` | `midiControl_` | CC1, CC2 dispatch, NRPN state machine, channel rules, value read, the hand-off to Learn | Foreground |
| `MidiLearn.c/h` | `midiLearn_` | Learn table, capture state, lookup and dispatch, `learn=` line format/parse | Foreground |
| `MidiProgram.c/h` | `midiProgram_` | PC and CC0 receive | Foreground |
| `MidiOut.c/h` | `midiOut_` | Every port write: framing, forwarding, realtime, notes with per-track note-off, PC, parameter TX, CC0 after a load | Foreground and TIM3 |
| `MidiPitch.h` | `midiPitch_` | Default note 63, note-frequency table, detune helper | — |
| `MidiVoiceControl.c/h` | `voiceControl_` | Trigger funnel into the audio render (S081 pending-reset mask) | Producer any; consumer audio render |

`valueShaper.h` is DSP code in `Core/DSPAudio/`.

**Dependency direction:**
- `MidiIn` calls the consumers (`MidiNotes`, `MidiControl`, `MidiProgram`,
  `MidiTransport`).
- The consumers call into the product (Menu, Sequencer, buttonHandler,
  Preset).
- Only `MidiOut` writes ports.
- `MidiNoteMap` depends on nothing but a Scene-settings read in
  `midiNoteMap_load()`.

### 12.2 Receive flow

```
USART3 RX ISR ─ stamp (TIM2 µs) ─┬─ 0xF8..0xFF ──► midiTransport_push() ─► realtime ring
                                 └─ other bytes ─► midiBuffer_rxIn() (byte + 16-bit stamp)
OTG ISR (usb_midi_core.c) ───────┬─ realtime ────► midiTransport_push()
                                 └─ other ───────► USB ring (MidiMsg + stamp; drop newest when full)

main loop: midiIn_service()
  ├─ up to 16 DIN bytes: midiDin_rxPop() ─► midiIn_frameDinByte() ─► midiIn_dispatch()
  ├─ up to 16 USB messages: usb_getMidi() ─► midiIn_dispatch()
  ├─ midiLearn_service()      (FUL timeout)
  ├─ midiOut_service()        (parameter TX, CC0 after a load)
  └─ usb_tick()

midiIn_dispatch(msg):
  SysEx ─────────────────────────────────────────────► forward
  Bank load running && !acceptedDuringBankLoad ───────► forward (not consumed)
  0xF1/0xF2/0xF3 ─► midiTransport_handleSystemCommon() ► forward (always)
  NOTE_ON/OFF  (RXF N) ─► midiNotes_handle()
  CC0          (RXF P) ─► midiProgram_bankChange()
  CC ≠ 0, 32   (RXF C) ─► midiControl_handleCc() ─► CC2 | CC1 | NRPN | Learn
  PROG_CHANGE  (RXF P) ─► midiProgram_programChange()
  not consumed ───────────────────────────────────────► midiOut_forward() per MRT

TIM3: midiTransport_processRealtime()  (32 events per call)
  each event: midiOut_forwardRealtime() (always) ─► RXF R? ─► snc source? ─►
  Clock → sync_tickTimestamp(); Start → sync_midiStartStop(1);
  Continue → sync_midiContinue(); Stop → sync_midiStartStop(0)
```

### 12.3 Write flow (a received parameter change)

```
midiControl_handleCc()                 NRPN data / CC1 voice / Learn match
  └─ midiControl_writeTarget(id, v7)
       └─ menu_commitTargetEdit(id, v7, MIDI_ORIGIN_MIDI)       (menu.c)
            ├─ build the menu cell for id in the ACTIVE Scene
            ├─ clamp to the cell's domain; Morph ×2; Effect WIDE8 expand
            ├─ menu_cellCommitValueEx(cell, v, panel = 0)
            │     the same commit as a knob: R12 / R13 / Scene clears,
            │     edit-mask fan-out, Normal image (panel view ignored)
            ├─ LATCH/OVERDUB: liveRec_paramIn(id, LIVEREC_SRC_MIDI)
            └─ menu_notifyTargetChanged(id) → coalesced repaint if visible
```

Global Morph (CC1 on G) uses `menu_commitGlobalMorphFromMidi()`: the `mrp`
static cell, then `preset_morphScene()` per edit-mask Scene, which sets the
voice and Effect Morph amounts. It is never recorded.

### 12.4 Transmit flow

```
seq_triggerVoice(track, vel, internal, reset)   (TIM3 or foreground)
  └─ midiOut_trackTrigger(track, playedScene, internal, vel)
       ├─ midiNoteMap_inverse() → (channel, note), or none (Q4) → note-off only
       └─ midiOut_trackNoteOn(): PRIMASK { previous note-off; note-on; record }

panel edit commit (menu.c, encoder or pot)
  └─ midiOut_parameterChanged(id)  → pending
       midiOut_service() (≥10 ms, ≥9 B DIN room) → midiOut_sendParameter(id)
          learned CC (midiLearn_ccForTarget) else NRPN (number cache per port)

seq_sendProgChg() (whole Scene; skipped if MIDI-originated) → midiOut_programChange()
buttonHandler per-track change → midiOut_trackProgramChange(mask, scene)
preset_takeBackgroundBankLoadResult() (in midiOut_service) → CC0 after a Menu DONE
seq_sendRealtime() → midiOut_realtime()   (TXF R)
```

---

## 13. Module reference (`Core/MIDI/`)

Every public function has a full contract block in its header. The `.c`
definition carries "Contract in X.h" plus implementation notes. This section
summarises each module; read the header for the exact contract.

### 13.1 `MidiDefs.h`

**`MidiMsg`** (6 B: `status`, `data1`, `data2`, `struct MidiBits bits`,
`uint16_t stamp`):
- `bits` is one byte of `uint8_t` bitfields: `source:1` (`midiSourceMIDI`
  = DIN 0, `midiSourceUSB` 1), `sysxbyte:1`, `length:2` (data bytes 0..2).
  - **Do not change the bitfield types:** an `enum` or `unsigned`
    bitfield takes a 4 B container on this ABI. That is what made the
    pre-S083 message 8 B, and an early S083 draft 12 B.
  - `MidiIn.c` checks `sizeof(MidiMsg) == 6` and `sizeof(struct MidiBits)
    == 1` with `_Static_assert`.
- `stamp` is the low 16 bits of TIM2 µs at arrival. LXR-generated messages
  leave it 0.

**Constants:**
- Status bytes (`NOTE_ON`, `MIDI_CC`, `MIDI_SONG_POSITION`, `MIDI_CLOCK`,
  …).
- CC numbers: `CC_BANK_CHANGE` 0, `CC_MOD_WHEEL` 1, `CC_NOTE_ACTION` 2,
  data entry, NRPN/RPN.
- `midiDefs_ccReserved(cc)` (§5.1).
- `MIDI_FILTER_NOTE/REALTIME/CONTROL/PROGRAM` (1/2/4/8).
- `MIDI_ROUTE_*` and `midiDefs_routeMask(mrt)` (the §3.3 table).
- `MIDI_CHANNEL_NONE` 0, `MIDI_TRACK_CHANNEL_OFF` 17, `MIDI_TRACK_NOTE_OFF`
  128.
- `MIDI_ORIGIN_PANEL`/`MIDI_ORIGIN_MIDI`, `MIDI_TARGET_OFF` 511.
- Port masks `MIDI_PORT_DIN/USB/BOTH`.

**Conventions:** channels inside the code are **1..16 with 0 =
off/unassigned**; the wire nibble is `channel − 1`. Target IDs are the
canonical 9-bit IDs.

### 13.2 `MidiBuffer.c/h`

- **Types:** `midiBuffer_t` (64 data bytes, read/write indices; holds 63
  bytes, one slot free) and `midiBuffer_rx_t` (adds `stamp[64]`).
- **API:** `midiBuffer_init/in/out/free()`, `midiBuffer_rxInit/rxIn/rxOut()`.
- **Contract:** one producer and one consumer per buffer.
  - The producer writes payload and stamp before publishing `write`.
  - The consumer reads byte **and stamp** before publishing `read`
    (assessment F-6).
- Instances live in `MidiDin.c`.

### 13.3 `MidiDin.c/h`

**API:**
- `midiDin_init()`.
- `midiDin_sendBytes(bytes, count)`: queues 1..3 bytes as one unit or none.
  A single byte ≥ 0xF8 goes to the realtime lane, which drains first.
- `midiDin_txFree()`, `midiDin_rxPop(&byte, &stamp)`.
- Drop counters: `midiDin_txDropCount()`, `midiDin_realtimeTxDropCount()`.

**RX ISR:**
- every byte is stamped with `timebase_tim2Now()`;
- 0xF8..0xFF go to `midiTransport_push()` with the full stamp;
- other bytes go to the RX buffer with the low 16 bits.

Only `MidiOut` calls the send functions.

### 13.4 `MidiIn.c/h`

**Settings API** (called by `menu_parseGlobalParam()`):
- `midiIn_setGlobalChannel(0..16)` ends every sounding track note when G
  changes, because `glb` tracks change channel.
- `midiIn_globalChannel()`.
- `midiIn_setFilter(isTx, mask)` and `midiIn_rxAllows/txAllows(bit)`: one
  byte, RX in the low nibble, TX in the high.
- `midiIn_setRouting(mrt)` and `midiIn_routeMask()`.

**Service:** `midiIn_service()` from the main loop (§12.2).

**Framing** (`midiIn_frameDinByte()`):
- running status for channel messages only;
- SysEx bytes dispatched one by one with `sysxbyte = 1`;
- MTC/Song Select 1 data byte, SPP 2, Tune Request 0;
- 0xF4/0xF5 ignored with their data;
- the initial `IGNORE` state discards data bytes before any status;
- a message takes the stamp of its first byte: the status byte, or the
  first data byte under running status;
- system common messages do not establish running status.

**Dispatch** (`midiIn_dispatch()`, static): §12.2. Also the static
`midiIn_acceptedDuringBankLoad()`, the S082 drop classes.

**RAM:** configuration 3 B, framer state 1 B, in-progress message 6 B.

### 13.5 `MidiTransport.c/h`

**Realtime ring** (32 × `MidiRealtimeEvent {status, source, timestampUs}`):
- `midiTransport_push()` (ISRs) drops the **oldest** event when full: a
  stale clock is worse than a missing one;
- `midiTransport_dropCount()`.

**`midiTransport_processRealtime()`:** TIM3 only (binding); up to 32
events. Each event:
1. is forwarded per MRT;
2. then passes the RXF R gate;
3. then the source gate (`midiTransport_sourceAllowed()`: AUTO priority
   internal < USB < DIN < jack pulse, a 500 ms DIN hold over USB, activity
   recorded);
4. then: Clock → `sync_tickTimestamp()`; Start →
   `sync_midiStartStop(1)`; Continue → `sync_midiContinue()`; Stop →
   `sync_midiStartStop(0)`.

**`midiTransport_handleSystemCommon(msg)`** (foreground):
- **MTC quarter frame:** the existing start rule (8 zero frames →
  `seq_setRunning(1)`; informational while running or externally clocked).
- **SPP:** while stopped and from the `snc` source
  (`midiTransport_sourceSelected()`, the side-effect-free twin of the
  realtime gate) → `seq_setResumeTick(pos × 24)`.

**`midiTransport_checkMtc()`** (TIM3 and foreground): stops the sequencer
100 ms after MTC that started it disappears.

### 13.6 `MidiNoteMap.c/h`

**Input type:** `midiNoteMap_settings_t` (15 B, stack only): `global`, and
`channel[7]` / `note[7]` in stored form. Host-local constants
`MIDI_NOTEMAP_ROOT_INTERNAL` 63, `MIDI_NOTEMAP_CHANNEL_OFF` 17 and
`MIDI_NOTEMAP_NOTE_OFF` 128 are tied to `MidiDefs.h` by `_Static_assert`.

**API:**
- `midiNoteMap_load(scene, &s)`: firmware only. It reads the Scene's track
  settings and G. **Always call it per message with the active (RX) or
  played (TX) Scene; never cache the result** across messages (Scene
  changes, Scene 17).
- `midiNoteMap_effectiveChannel(&s, t)`: 0 for `ch off`; `glb` → G.
- `midiNoteMap_tracksOnChannel(&s, ch)`: bit mask including `nte off`
  tracks. Use it for CC/NRPN/PC.
- `midiNoteMap_resolve(&s, ch, note, &track, &internal)`: RX, §4.1.
- `midiNoteMap_inverse(&s, track, internal, &ch, &note)`: TX. Returns 0
  when no note resolves back to the same track and internal note.
- The static `midiNoteMap_noteTracksOnChannel()` excludes `nte off` tracks
  for resolve and inverse.

**Properties:** stateless and table-free (at most 7 tracks per lookup, no
RAM). With `MIDI_NOTEMAP_HOST_TEST` defined it compiles alone on the host
(§17.1).

### 13.7 `MidiNotes.c/h`

**`midiNotes_handle(msg)`:**
1. Resolve through `midiNoteMap_load(scene_getActiveIndex())` +
   `resolve()`. Unmapped → return 0 (forward).
2. Note-off → `buttonHandler_overlayMidiNoteOff(track, note)`.
3. A muted track → consumed, silent.
4. Overlay on (`buttonHandler_overlayActive()`) →
   `buttonHandler_overlayMidiNoteOn(track, which, internal, note, age)`.
5. Otherwise a single trigger: `seq_triggerVoice(track, vel, internal, 1)`,
   then `liveRec_triggerIn()` with source MIDI, `note_is_default`, and
   `age_us`.

**Age:** `(uint16_t)(now16 − stamp)`; above `MIDI_ARRIVAL_MAX_AGE_US`
(20 ms) it is 0, which protects against the 65.5 ms wrap.

**`midiNotes_setNoteAction(v)`:** CC2; 0..2 →
`buttonHandler_setOverlay(v)`. It keeps no state.

### 13.8 `MidiControl.c/h`

**`midiControl_handleCc(msg)`** (not CC0/CC32):
- **CC2:** on G → `midiNotes_setNoteAction()`; elsewhere 0.
- **CC1:** on G → `menu_commitGlobalMorphFromMidi()`; on a voice channel,
  `midiControl_writeTarget(voiceMorphId(slot))` for each slot on the
  channel.
- **CC99/98/101/100/38:** only on G or a voice channel. They allocate or
  update the NRPN context; RPN deselects; CC38 is consumed and ignored.
- **CC6/96/97:**
  - need a complete NRPN number in the context;
  - id < 511 and valid on this channel (`midiControl_targetOnChannel()`),
    else not consumed;
  - 96/97 step `midiControl_targetValue7()` ±1;
  - then `midiControl_writeTarget()`.
- **Other non-reserved CCs:** `midiLearn_captureCc()` first, then
  `midiLearn_applyCc()`.

**Helpers:**
- `midiControl_targetOnChannel(id, ch)`: §5.4 channel rule.
- `midiControl_writeTarget(id, v7)` → `menu_commitTargetEdit()`.
- `midiControl_targetValue7(scene, id)`: the Normal value in 7-bit form.
  Morph 255 → 127, else /2. Instrument bytes are clamped to 127, because
  target tokens reach 0xFF. WIDE8 Effect rows go through
  `effects_storeValue7()`.
- `midiControl_voiceSlotsOnChannel(ch)`: 0 for G or an unused channel.
- `midiControl_targetChannel(id)`: the effective channel of a target's
  voice; 0 for Effect and `fxm`.

**NRPN contexts:** `midiControl_nrpn[2 ports][MIDI_NRPN_CONTEXTS = 8]` of
3 B. `key` holds bit 7 used, 6 MSB seen, 5 LSB seen, 4 RPN, bits 0..3
channel − 1. When all 8 are used, slot `(ch − 1) % 8` is reused.

### 13.9 `MidiLearn.c/h`

**Table:** `midiLearn_table[MIDI_LEARN_MAX_ENTRIES = 128]` of 4 B:
- `ccKind`: bits 0..6 CC, bit 7 = kind `v`;
- `space`: 0..5 slot, 6 Scene, 7 Effect;
- `type`: `instrument_type_t` / `effect_type_id_t` / 0xFF;
- `index`: descriptor 0..63, Scene-local, or Effect-local 0..62.

Also `midiLearn_used`, `_state` (IDLE, WAIT_TARGET, WAIT_CC, FULL),
`_target`, `_fullUntil`.

**Gesture API:**
- `midiLearn_arm()` (buttonHandler) and `midiLearn_cancel()`;
- `midiLearn_active()`, `midiLearn_selectingTarget()`,
  `midiLearn_selectTarget(id)` (menu, on the touched cell);
- `midiLearn_displayTarget()` / `midiLearn_showingFull()` (`LRN`/`FUL`);
- `midiLearn_service()` (the `FUL` timeout).

**Receive:**
- `midiLearn_captureCc(ch, cc)`:
  - stores or replaces the row, or shows `FUL`;
  - calls `filesystem_requestMidiLearnWrite()`;
  - ends the gesture.
- `midiLearn_applyCc(ch, cc, v7)`: every row with matching CC, kind and
  current type (`midiLearn_typeMatches()` against the active Scene). A `v`
  row also needs `midiControl_targetOnChannel()`, so it moves only its own
  slot.

**Transmit:** `midiLearn_ccForTarget(id, &cc, &kind)` returns the first row
for the current type.

**File API:**
- `midiLearn_formatFileLine(i, dst, cap)` returns the **line length**
  (`filesystem_writeTextLine()` writes exactly that many bytes). It writes
  a `#` comment for an unresolvable row, and no printf is used.
- `midiLearn_parseFileValue(value)`: 1 stored, 0 skipped.
- `midiLearn_clearAll()`, `midiLearn_count()`.

### 13.10 `MidiProgram.c/h`

- **`midiProgram_programChange(ch, prog)`:**
  - prog ≤ 15 and `bank_sceneAvailable()` required;
  - on G → `menu_perfModeSceneChangeFrom(prog, MIDI_ORIGIN_MIDI)`;
  - else, for the channel's tracks (`tracksOnChannel`) →
    `seq_assignTracksToScene(mask, prog)`.
- **`midiProgram_bankChange(ch, v)`:** G only, v ≤ 127 →
  `preset_requestBackgroundBankLoad(v, 0, 0, PRESET_BANK_ORIGIN_MIDI)`.

### 13.11 `MidiOut.c/h`

**Framing and forwarding:**
- `midiOut_sendMsg(msg, ports)`: DIN through `midiDin_sendBytes()`, USB
  through `usb_sendMidiPacket(cin, b0, b1, b2)`; `midiOut_usbCin()` picks
  the code index. TXF is not applied here.
- `midiOut_forward(msg)`: MRT, by source port.
- `midiOut_forwardRealtime(status, source)` (TIM3).
- `midiOut_realtime(status)`: TXF R.

**Notes:**
- `midiOut_trackNoteOn(track, ch, note, vel)` and
  `midiOut_trackNoteOff(track | 0xFF)` keep the per-track record:
  `midiOut_trackNote[7]` (bit 7 sounding, note) and
  `midiOut_trackChannel[7]`.
- The note-on holds one PRIMASK section across the previous note-off, the
  note-on and the record update, through the static
  `midiOut_trackNoteOffLocked()`.
- Note-off is sent regardless of TXF.
- `midiOut_trackTrigger(track, scene, internal, vel)`: the inverse map;
  none → note-off only.

**Program/Bank:**
- `midiOut_programChange(ch, prog)`: TXF P.
- `midiOut_trackProgramChange(mask, scene)`: the panel per-track change.
  One PC per distinct effective channel in the target Scene, never on G;
  the HiHat pair counts as one.

**Parameters:**
- `midiOut_parameterChanged(id)`: panel only.
- State: `midiOut_pending` (id + 1), `midiOut_lastSendMs`,
  `midiOut_nrpnKey[2]` (((ch − 1) << 9 | id) + 1).
- `midiOut_service()` sends the pending parameter (learned CC, else NRPN)
  and CC0 after a Menu-origin DONE. It is the **sole consumer** of
  `preset_takeBackgroundBankLoadResult()`.
- The static `midiOut_cc()` masks the channel to 4 bits and the data to 7
  bits.

### 13.12 `MidiPitch.h` and `MidiVoiceControl`

- **`MidiPitch.h`:** `MIDI_DEFAULT_TRIGGER_NOTE` 63, the note-frequency
  table and `midiPitch_detune()` (used by `Oscillator.c`).
- **`MidiVoiceControl`:** the trigger funnel used by the sequencer,
  buttonHandler and the audio render. `voiceControl_noteOn()` /
  `noteOnNoReset()` / `resetOnly()` / `noteOff()` /
  `processPending()`; the S081 per-voice pending-reset mask. It is
  unchanged by S083.

---

## 14. Integration points outside `Core/MIDI/`

### 14.1 Menu (`Core/Menu/menu.c/h`)

- **`menu_commitTargetEdit(id, value7, origin)`:** the one MIDI write entry
  (§12.3).
  - **Instrument:** an Instrument cell, clamped by `menu_clampCellValue()`;
    R12 through the cell commit.
  - **Scene targets:** Nvm/fxm static cells with Morph ×2; Nou/Nfx
    Scene-setting cells; 7dc the Kit-setting cell; track len/scl/shf
    through `menu_commitTrackTimingTarget()`.
  - **Effect:** the Effect row of the active type, with menuHold.
  - Records via `liveRec_paramIn()` (automatable targets; never track
    length) and repaints via `menu_notifyTargetChanged()`.
- **`menu_cellCommitValueEx(cell, value, panel)`** (static): the cell
  commit. With `panel = 0` it ignores the panel's Morph-view and
  held-step state, so MIDI writes Normal. `menu_cellCommitValue()` is the
  panel wrapper.
- **`menu_commitGlobalMorphFromMidi(v7)`:** `mrp`, never recorded. It
  repaints `mrp`, `1vm..6vm` and `fxm`.
- **Repaint and target lookup:**
  - `menu_notifyTargetChanged(id)` / static `menu_targetVisible(id)`:
    repaint by target ID through `menu_knobs_dirty` (the LCD-rate-limited
    path);
  - `menu_cellMidiTarget(cell, &id)` (static): a cell's MIDI address (used
    by Learn capture and the TX hook).
- **Learn UI:**
  - capture in the encoder path and in `menu_parseKnobDelta()` while
    `midiLearn_selectingTarget()`, with no value change;
  - `menu_learnName()` shows `LRN`/`FUL`; `menu_learnDisplayChanged()`;
  - cancel in `menu_switchPage()` and in
    `menu_backgroundBankLoadUiBegin()`.
- **Panel TX hook:** `midiOut_parameterChanged(id)` after the encoder and
  pot commits. Early-return paths (target-selection cells, roll rhythm,
  held-step overlays) send nothing.
- **Scene change with origin:**
  - `menu_perfModeSceneChangeFrom(scene, origin)`; the old
    `menu_perfModeSceneButtonPressed()` is the panel wrapper;
  - `menu_perfSceneChangeExecute()` repaints PERF LEDs only in PERF;
  - `menu_serviceDeferredSceneChange()` skips a queued action whose target
    is unavailable, whole (user rule Q2).
- **MIDI settings in `menu_parseGlobalParam()`:**
  - `PAR_MIDI_CHAN_GLOBAL` → `midiIn_setGlobalChannel()`;
  - `PAR_MIDI_ROUTING` → `midiIn_setRouting()`;
  - `PAR_MIDI_FILT_TX/RX` → `midiIn_setFilter()`;
  - `PAR_TRACK_MIDI_CHAN/NOTE` → convert from menu order, store in the
    viewed Scene, `midiOut_trackNoteOff()` on change;
  - the retired `PAR_MIDI_CHAN_1..7` and `PAR_MIDI_MODE` are no-op slots
    (ids kept).
- **Track `ch`/`nte` display:**
  - `menu_getParameterDisplayValue()` reads the viewed Scene
    (`menu_trackMidiChanToMenu()` / `NoteToMenu()`: menu = stored + 1, with
    `off` at 0);
  - `menu_getMidiStaticName()` names `off`/`glb`/channel and
    `off`/`on`/note;
  - the clamps are in `menu_clampCellValue()`.

### 14.2 Sequencer (`Core/Sequencer/`)

- **`seq_triggerVoice(track, vel, internal, reset)`:** every LXR trigger.
  It sends MIDI through `midiOut_trackTrigger(track,
  seq_perTrackPattern[track], …)`. `seq_previewVoice()` plays internal 63.
- **Transport:**
  - `seq_setRunningEx(run, sendRealtime)`: `seq_setRunning()` is the
    sending wrapper; received transport passes 0.
  - Stop stores `seq_resumeTick = seq_initialSchedulerTick ? 0 :
    seq_elapsedPpqTicks + 1` (the first unplayed tick); Start clears it.
  - `seq_continueRunning(sendRealtime)` places the master clock one tick
    before the resume tick and realigns tracks and the FX sequence through
    the master-clock anchors.
  - `seq_setResumeTick()` is for SPP.
- **Scene changes and output:**
  - `seq_assignTracksToScene(mask, scene)`: per-track Scene change (panel
    and MIDI). It sets the played Scene, realigns (epoch bump → new
    LiveRecord take), ends the MIDI note, applies each slot once, and moves
    the HiHat pair together.
  - `seq_setSceneChangeOrigin(origin)` and `seq_sceneChangeFromMidi`:
    `seq_sendProgChg()` sends nothing for a MIDI-originated change; a chain
    fire clears the flag.
  - `midiOut_trackNoteOff(0xFF)` at whole-Scene changes and Stop;
    `seq_setMute()` ends the muted track's voice and MIDI note.
- **`seq_rollRhythmVelocity(rhythm, vel, pos)`:** the shared roll-rhythm
  rule (step rolls and overlay rolls, both TIM3).
- **Live overlay engine (S084):** `seq_liveRollStart()`,
  `seq_liveRollSetRate()`, `seq_liveFlamArm()`, `seq_liveStop()`,
  `seq_liveSetRhythm()`, `seq_liveTakeHits()`; `seq_processLiveTicks()`
  fires overlay roll repeats (reset 0) and flam mains (reset 1) in TIM3,
  first in `seq_processSchedulerTick()` (before the running gate) and from
  `seq_tick()` under external sync while stopped
  (`PATTERN_DYNAMIC_STACK.md` §6).
- **`clockSync.c`:** `sync_tickTimestamp()` measures the tempo
  (`seq_setBpm(sync_calcBpm(avg))`) and, while running, calls
  `seq_resetDeltaAndTick()`; `sync_midiStartStop()` (arms on the next
  clock) and `sync_midiContinue()`.
- **Clock tick interpolation (S084):** `seq_resetDeltaAndTick()` and
  `seq_triggerNextMasterStep()` call `seq_extClockTicks(ticks)` (catch up
  owed ticks, run the edge tick, owe `ticks − 1`, re-anchor the tempo
  timer); `seq_tick()` runs one owed tick per tempo period
  (`seq_extTicksOwed`).
- **`sequencerTimer.c`:** calls `midiTransport_processRealtime()` in TIM3.
- **LiveRecord:**
  - `liveRec_trigger_t.age_us` (0 for panel and repeat hits);
    `liveRec_triggerIn()` moves the captured position back by `age_us ×
    256 / periodUs` Q8 ticks, at most one step interval;
  - `liveRec_paramIn(id, MIDI)` reads the stored value, so call it after
    the store.

### 14.3 buttonHandler (`Core/Hardware/frontPanel/buttonHandler.c/h`)

**Overlay state:** `bh_overlayActive` (0/1/2) is the one overlay state.
- BAR1/BAR2 in PERF toggle it, unless the looper is engaged (BARs are then
  its modifiers).
- `buttonHandler_setOverlay(which)` (CC2) sets it like a BAR press: LEDs in
  PERF while `!seq_loopActive`, then `buttonHandler_overlaySwitchHeld()`.
- `buttonHandler_overlayActive()` reads it in any mode.

**Per-track executor** (S083 Q5; timing moved to the Sequencer in S084;
full walkthrough `MODULE_INTERCHANGE_SPEC.md` "BAR overlay trigger
engine"): `bh_overlay[7]` of 4 B: `heldMidiNote` (note + 1, 0 =
panel/none), `note` (bit 7 = `def`), `count` (0 idle, 1 flam main pending,
0xFF roll), `rate` (step-roll index 1..9).
- `buttonHandler_overlayStart()` fires the first hit (foreground) and arms
  the Sequencer's live engine (`seq_liveFlamArm()` /
  `seq_liveRollStart()`); a gesture already on the track ends first.
- Repeats and flam mains fire in TIM3 (`seq_processLiveTicks()`), each
  repeat at `seq_rollRhythmVelocity(pv of the active overlay, 100, pos)`.
- `buttonHandler_overlayDrain(track)` reports the TIM3 hits to LiveRecord
  (one `ROLL_HIT` per drain, or `FLAM_MAIN`) with the latest hit's age;
  `buttonHandler_overlayTick()` drains every track each foreground pass and
  publishes the active overlay's `pv` rhythm (`seq_liveSetRhythm()`).
- `buttonHandler_overlayDrainAll()` runs before every overlay state change
  (CC2, BAR presses, overflow recovery) so hits keep the old rhythm.
- `buttonHandler_overlayReconfigure(which)` re-configures every held entry
  on an overlay change; it drains each entry **before** judging it held, so
  a MIDI flam whose main already fired is not turned into a roll its
  note-off cannot stop.
- **MIDI entry points:** `buttonHandler_overlayMidiNoteOn()` and
  `buttonHandler_overlayMidiNoteOff()` (ends a roll; a flam's main hit
  still plays).

**Learn gesture:**
- SHIFT+PLAY arms the chord (`bh_chainPlayArmed`): SHIFT+PLAY no longer
  toggles the transport.
- `case BUT_REC:` with the chord → `midiLearn_arm()`.
- While Learn is active, SHIFT/PLAY/REC presses cancel it and are
  consumed.
- `buttonHandler_setLearnLeds(on)` flashes SHIFT/PLAY/REC and restores
  them after.

**Per-track PC:** `buttonHandler_perfVoiceHeldSeqPressed()` →
`seq_assignTracksToScene()` → `midiOut_trackProgramChange()`.

### 14.4 Scene settings, Preset, Effects

- **`SceneData`:** `scene_set/getTrackMidiChannel()` (0..17),
  `scene_set/getTrackMidiNote()` (0..128). Out-of-range values become
  `off` in the setters. A stale channel byte > 17 reads as 0.
- **`storageTypes.c`:** `sceneset.scg` `midi_channel` 0..17, `midi_note`
  0..128.
- **`presetManager.c`:**
  - `preset_morphScene()` sets the Scene Morph, the six voice amounts and
    the Effect Morph (`effects_setMorphAmountFromGlobal()`). It clears step
    Morph overrides on the active Scene and syncs the `mrp`/`Nvm`/`fxm`
    mirrors.
  - `preset_requestBackgroundBankLoad()` and
    `preset_takeBackgroundBankLoadResult()` (S082).
- **`EffectsManager`:** `effects_setMorphAmountFromGlobal(scene, amount)`
  sets one Scene with no fan-out. It clears the Pattern `fxm` override and
  sets `menuHoldMorph` on the active Scene.

### 14.5 Filesystem and boot

- **`settings.cfg`:** `midi_chan_global`, `midi_filt_tx/rx`,
  `midi_routing`, `ext_sync`, `perf_btn_*`, `perf_note_*`, `perf_vel_*`.
  The bulk Global apply replays them into `MidiIn` at boot and after a
  settings load.
- **`midilrn.cfg`:**
  - `filesystem_requestMidiLearnWrite()` sets a 1 B request, written
    immediately when the facade is idle;
  - `filesystem_requestLoadMidiLearn(cb)` is the boot load, called by
    `main.c` after the settings load, before audio;
  - the ops are `FS_INTERNAL_OP_LOAD/SAVE_MIDILEARN`.
  - Details: `FILESYSTEM_SPEC.md`.
- **`main.c`:** `midiDin_init()`; `midi_service()` → `midiIn_service()` in
  the main loop.

### 14.6 USB (`Core/Hardware/USB/App/`)

- **`usb_midi_core.c` (OTG ISR):**
  - realtime events go to `midiTransport_push()` with the packet time;
  - other events go into `usb_MidiMessages[MIDI_USB_RX_RING_SIZE]` (6 B
    each, with `stamp`);
  - the newest is dropped when the ring is full;
  - SysEx packets are passed through with `sysxbyte`.
- **`usb_manager.c`:** `usb_getMidi(&msg)` (foreground consumer),
  `usb_sendMidiPacket(cin, b0, b1, b2)` (cable 0), `usb_tick()`.
- **`config.h`:** `MIDI_USB_RX_RING_SIZE` must be a power of two ≤ 256
  (compile-time check). Raising it needs a RAM acknowledgement.

---

## 15. Execution contexts and concurrency

| Context | MIDI work |
|---------|-----------|
| USART3 ISR (prio 5) | DIN RX stamp and push; DIN TX drain (realtime lane first). |
| OTG ISR | USB RX: realtime push, ring insert with stamp. |
| TIM3 | `midiTransport_processRealtime()` (clock/transport, realtime forwarding; the clock edge runs its first scheduler tick, `seq_tick()` the interpolated rest); sequencer triggers → `midiOut_trackTrigger()`, including overlay roll repeats and flam mains from the live engine (S084); LXR clock/transport TX; `midiTransport_checkMtc()`. |
| Foreground (main loop) | `midiIn_service()`: framing, dispatch, every consumer, Learn, parameter TX, CC0 TX, USB flush; menu, Learn capture; panel overlays. |

**Rules:**
- **Port writes** (`midiDin_sendBytes()`, `usb_sendMidiPacket()`) run
  under short PRIMASK sections, because TIM3 and the foreground both send.
- **The per-track note record** is updated under one PRIMASK section with
  its sends (§13.11).
- **Realtime never enters the channel framer**, so running status is never
  disturbed.
- **Never call `midiTransport_processRealtime()` outside TIM3.**
- **No MIDI code caches a Scene index.** The active (RX) or played (TX)
  Scene is read per message, so Scene changes, Scene 17 and loads are
  followed.

---

## 16. How to extend

**Add a consumer for a new message type:**
1. Add its status constant to `MidiDefs.h`.
2. Frame it in `midiIn_frameDinByte()` with its data length (USB arrives
   framed).
3. Add a case to `midiIn_dispatch()` behind the right RXF bit. Return
   "consumed" only when the LXR acted, so the rest is forwarded.
4. If it must act during a Bank load, extend
   `midiIn_acceptedDuringBankLoad()` (an S082 decision; get user
   approval).
5. Give it the right USB code index in `midiOut_usbCin()`.

**Add a fixed-meaning CC:**
1. Define it in `MidiDefs.h`.
2. Add it to `midiDefs_ccReserved()` so Learn cannot take it.
3. Handle it in `midiControl_handleCc()` before the Learn fall-through.
4. Update §5.1.

**Add a new automatable target:**
- Add it to the 9-bit ID space (`SceneModTargets` / descriptors) and to
  `menu_commitTargetEdit()` (the cell it maps to).
- Then NRPN, Learn (if `MidiLearn`'s space/type/index encoding and the
  `learn=` format can name it), LiveRecord and TX work without further
  MIDI code. Also:
  - `midiControl_targetOnChannel()` decides voice-channel access;
  - `midiControl_targetValue7()` gives its TX/step value;
  - `midiControl_targetChannel()` gives its TX channel.

**Add a MIDI setting:**
1. Add the ParameterArray id (append; never renumber).
2. Add the menu cell and the clamp/name special cases.
3. Apply it in `menu_parseGlobalParam()` → a `midiIn_` setter.
4. Add a `settings.cfg` key and writer case, raising the `lines=` count.
5. Update §2 and `FILESYSTEM_SPEC.md`.

**Change the note map:**
1. Edit `MidiNoteMap.c` only.
2. Keep `resolve()` and `inverse()` consistent: every sent note must
   resolve back.
3. Extend `tools/midi_test/notemap_test.c` and run it.

**Change transmit:** all port output goes through `MidiOut`. Respect TXF,
the no-echo rule (MIDI-originated changes never call
`midiOut_parameterChanged()`, and `seq_sceneChangeFromMidi` suppresses the
PC), and the DIN room check.

**Change the Learn file format:**
1. Bump `version` in `filesystem_nextMidiLearnLine()`.
2. Teach `filesystem_parseMidiLearnLine()`. There is no migration by
   project rule, so decide what an old file means.

**RAM:** any new static buffer needs the RAM Allocation Approval Policy
(MEMORY.md) and a `STORAGE_SRAM_MANIFEST.md` row.

---

## 17. Testing and diagnostics

### 17.1 Host test (`tools/midi_test/`)

```
make -C tools/midi_test run      # prints PASS (0 failures)
```

`notemap_test.c` builds `MidiNoteMap.c` with the host `cc` and
`MIDI_NOTEMAP_HOST_TEST`. It covers:
- the §4.2 examples;
- N5 ties;
- G off;
- `ch off` / `nte off`;
- 200,000 random settings, with the invariants: one track per note; every
  `inverse()` result resolves back to the same track and internal note; a
  `nte off` track never resolves.

Add a case for every note-map change.

### 17.2 Hardware regression list

Run with RXF/TXF = `all` and a DAW plus a DIN loopback.

- **Routing:** the MRT matrix for the 8 values (only routed paths; SysEx
  and SPP intact on USB; never USB → USB).
- **Clock:** clock slave on DIN, USB and AUT; Start/Continue/Stop; DAW
  locate (SPP) then Continue; MTC start/stop; (S084) `64`/`128` rolls even
  under MIDI clock, rolls/shuffle/micro-timing even within each 4-PPQ jack
  pulse, no extra or missing tick after Start or Continue.
- **Notes:**
  - each §4.2 example;
  - muted and unmapped notes;
  - `ch off` / `nte off`;
  - overlay rolls on several tracks from MIDI and the panel;
  - CC2 0/1/2 with the LEDs;
  - `pv` rhythms;
  - (S084) every one of the 15 overlay types at its STEP rate, with the
    transport stopped and running, internal and external clock; a CC2
    switch to a roll type while tapping notes under a flam overlay (every
    roll stops at its note-off); recorded overlay rolls replay at the played
    rate and `pv` shape.
- **Recording:** LATCH placement of MIDI notes on step boundaries under CPU
  load.
- **NRPN:** a sweep of IDs per type; R12/R13 against automation; the
  voice-channel ID rules; fan-out (edit mask, unlocked load, Scene 17);
  recording; CC1 on G never recorded; CC96/97; RPN null.
- **Learn:** the gesture in every mode; replace; macros; type matching;
  `v` rows on a shared channel; reserved CCs; cancels; `FUL`; reboot
  persistence; hand edits; tmp recovery.
- **PC/CC0:** PC on G in every mode; voice-channel PC; PC/CC0 during a load
  (forwarded, not acted on); CC0 bursts; RXF P off.
- **TX:** a DAW round trip plays back identically; no hanging notes; NRPN
  coalescing without clock jitter (`midiDin_txDropCount()` 0); no echo;
  CC0 after a Menu load only.

### 17.3 Diagnostics

- **Drop counters:**
  - `midiDin_txDropCount()` and `midiDin_realtimeTxDropCount()` (TX lane
    full);
  - `midiTransport_dropCount()` (realtime ring overrun).
- **DEV_MODE_LOGGING trace stage `m`:** `midilrn.cfg` WRITE / RECOVERY /
  LOAD records (`DEV_MODES.md`), decoded by `tools/decode_devlogs.py`.
  Boot code `MLRNLOAD`; error names `MlrL`/`MlrS`; stall code `MlWr`.

---

## 18. RAM (SRAM1, static, runtime lifetime; accepted 2026-10-17/21/22)

| Item | Bytes | Owner |
|------|------:|-------|
| Learn table 128 × 4 B + count/state/target/deadline | 518 (528 aligned) | `MidiLearn` |
| Per-track overlay table 7 × 4 B (S084; was 7 × 8 B) | 28 | `buttonHandler` |
| Live overlay engine + mailbox (S084): 6 × 7 B state, rhythm 1 B, 7 hit counts, flam mask 1 B, 7 × 4 B stamps | 79 | Sequencer |
| External clock owed ticks (S084) | 1 | Sequencer |
| NRPN RX contexts 2 × 8 × 3 B | 48 | `MidiControl` |
| Parameter TX: NRPN key cache 2 × 2 B, pending 2 B, last send 2 B | 8 | `MidiOut` |
| TX note-off record 7 × 2 B | 14 | `MidiOut` |
| DIN RX buffer 66 B + 64 × 2 B stamps; TX lanes 2 × 66 B | 194 + 132 | `MidiDin` |
| USB RX ring `MIDI_USB_RX_RING_SIZE` (128) × 6 B | 768 | `usb_midi_core` |
| Realtime ring 32 × 8 B + indices/drop counter | ~264 | `MidiTransport` |
| MIDI configuration 3 B, framer 1 B + in-progress message 6 B | 10 | `MidiIn` |
| Resume tick 4 B; PC origin flag 1 B | 5 | Sequencer |
| `midilrn.cfg` request flag | 1 | filesystem |

**Retired:** the pre-S083 USB ring (256 × 8 B = 2,048 B), the legacy
parser arrays (`midiParser_originalCcValues[255]`, `midi_NoteOverride[7]`,
`midi_MidiChannels[8]`), the sequencer's channel note cache (18 B), the
single overlay slot (9 B), the S083 P1 CC2 state (1 B), and (S084) the
overlay table's `pos`/`interval`/`next` fields (28 B). A release does
not authorise reuse.

The measured totals are in `STORAGE_SRAM_MANIFEST.md`. Anything larger than
listed needs a new acknowledgement.

---

## 19. Binding rules

- A MIDI write is a menu edit: same priority and actions (R12/R13, Scene
  target clears, recording), Normal endpoint only, through
  `menu_commitTargetEdit()`.
- Global Morph is never recorded, from any path (G-1). It sets all Morph,
  the Effect Morph included.
- One note triggers at most one voice.
- A note is transmitted only when it plays back on the same track at the
  same pitch.
- One parameter has at most one Learn CC.
- Values are absolute and instrument-agnostic, clamped to the parameter's
  domain; Morph is the only translation.
- Realtime bytes never enter the channel framer; the realtime handler runs
  in TIM3 only.
- No USB → USB routing; nothing is routed back to its source direction.
- MIDI-originated changes are never transmitted.
- Scenes, or tracks from Scenes, are not automatable unless they are
  changed through a voice edit mask from an active Scene or track (user
  rule, S083 Q2).
- MIDI code never caches a Scene index.
- Overlay hits fire in TIM3; LiveRecord is called only from the foreground
  drain. An external clock edge runs only the first tick of its period;
  never run a period's ticks back to back (S084).

---

# Part III — Decisions

## 20. Decision record

The decisions that shaped this design, with their identifiers. Full text is
in `083_SESSION_HANDOFF_LOG.md` §4.

| Id | Decision |
|----|----------|
| F1–F4 | One canonical 9-bit ID space; per-voice Scene targets on voice channels; absolute numbering; absolute values with Morph as the only translation. |
| F5, F12 | Writes fan out from the active Scene; channel and note are per Scene. |
| F6–F8, F27, F49–F51 | Learn: global, 128 type-tagged entries, one CC → many, one parameter → one CC, the SHIFT+PLAY+REC gesture, re-learn replaces CC and kind, `v` rows are slot-specific, Scene 17 types are used while it is active. |
| F16 | The stripe starts at note 0. |
| F18, F37 | Banks 000..127; RXF P governs both PC and CC0. |
| F22 | MRT list `off UtD DtD DtU bth DtA AtD all`. |
| F26 | NRPN data: CC6 value, CC38 ignored, CC96/97 ±1. |
| Q1 | A MIDI write is a menu edit (replaced the S075 F3 "MIDI lowest" rule). |
| Q2 | No automation of Scenes except through the active edit mask (the queued-action fix). |
| Q4 | No note TX outside a track's own range. |
| Q5 | Per-track overlay repeat state, one table for panel and MIDI. |
| Q6, Q13 | Arrival stamps for record placement; USB ring 128 × 6 B, size in `config.h`. |
| Q7 | `Core/MIDI/` rebuilt as single-role modules. |
| Q8 | With G off, voice targets transmit NRPN on the voice channel. |
| Q9 | No PC/CC0 echo for MIDI-originated changes; CC0 after a Menu load only on success. |
| Q10 | The Learn touch does not change the value; Learn targets Normal. |
| Q11 | SPP is gated by `snc`. |
| Q12 | The USB RX ring drops the newest when full. |
| C-1 | `nte` is a root only; the overlay `def` and the preview play the default pitch. |
| C-3 | MIDI-in triggers are transmitted. |
| C-4 | An unaddressed cell touched during Learn edits normally. |
| C-5 | PERF Scene LEDs are repainted only in PERF. |
| C-6 | TX uses the played Scene; RX the active Scene. |
| C-7 | NRPN data CCs are consumed only for valid targets. |
| C-8 | No per-track PC on G. |
| C-10 | SysEx to USB as single-byte packets. |
| C-11 | Received transport is not re-sent. |
| 2026-10-22 | Track `ch off` / `nte off`, `def` renamed `on`; Learn in `midilrn.cfg`, written after every Learn; global Morph sets FX Morph; `pv<`/`pv>`; CC2 sets the BAR overlay itself (the earlier C-9 "held BAR beats CC2" is retired). |
| S084 Q1, Q2 | Overlay roll types are the nine STEP rates with the STEP labels (`pb<`/`pb>` 0..14); stored 6..11 not migrated. |
| S084 Q3, Q6, Q9 | Overlay roll repeats and the flam main run on the master tick, spaced from the press; while stopped under external sync they follow the measured tempo. |
| S084 Q7, Q8 | Every held overlay/MIDI roll records as a span; the `pv` rhythm is recorded into the step `rtm`. |
| S084 A1 | External clock ticks are interpolated between edges. |

**Accepted limits:**
- a hanging downstream note after a mid-note map change (R9);
- per-track playback writes and Learn type matching use the active Scene
  (R10);
- NRPN numbers are type-dependent (R7);
- MIDI-held rolls are not looper holds;
- a BAR overlay set in PERF also applies to MIDI notes in other modes;
- live overlay rolls are spaced from the note's arrival, not locked to the
  rate's grid (a quantised recording starts on the grid).

(`pv` rhythms are recorded into steps since S084 Q8.)

**Out of scope:** external MIDI sequencing tracks, one-shot LFOs, MMC,
SysEx dump/restore, MIDI 2.0, multi-cable USB.

## 21. Maintaining this document

- Change behaviour and this document in the same change.
- Part I is the user-visible contract. Part II must name the real
  functions, so update it whenever a MIDI API is renamed or moved.
- Keep these in step:
  - `MODULE_INTERCHANGE_SPEC.md` (module APIs);
  - `FILESYSTEM_SPEC.md` (settings keys, `midilrn.cfg`, `sceneset.scg`);
  - `PATTERN_DYNAMIC_STACK.md` §6.2a;
  - `STORAGE_SRAM_MANIFEST.md` (RAM);
  - `DEV_MODES.md` (trace);
  - the MEMORY.md "MIDI / Clock Reminders".
