# Session 083 — Handoff Log

```
DATE: 2026-10-21 / 2026-10-22
SESSION GOAL: Implement the MIDI plan written in Session 082
              (S083_P1_MIDI_IMPLEMENTATION_PLAN.md; target spec
              MIDI_SPEC.md): SCOPING_TARGETS.md Phase 6 §6.1 (MIDI and
              External Control Cleanup), deferred item A20 (MIDI mapping),
              and the MIDI items deferred from S081.
COMPLETED:
  P1  Plan revision 5 (fourth decision round Q1-Q12, then Q13), a full
      code schedule (S083_P1_IMPLEMENTATION.md: every change by file, line
      and action, with its comment block), the implementation in phases
      A-H, an assessment (findings F-1..F-7, D-1, D-2) and the fixes.
  P2  Three rounds of hardware follow-ups:
      A  track settings page 2: `ch` gains `off`, `nte` gains `off`
         (`def` -> `on`, category Track), `asm` -> `rmp` (Automtn, Ramp);
      B  MIDI Learn persistence: root-cause fix plus the new root file
         `midilrn.cfg`, safely rewritten after every Learn;
      C  global Morph also sets FX Morph (not recorded);
      D  SHIFT+REC `pv<`/`pv>` (overlay roll velocity rhythm); page 2 =
         `pv< pv> qnt rtk`;
      E  CC2 on G sets the PERF BAR overlay itself (state + LEDs);
      F  comment-block audit of every landed change.
  Session close: logs, specification reference rewrite (MIDI_SPEC.md is
  now the complete use and code reference), MEMORY.md, volatile notes.
VERIFIED ON HARDWARE:
  - The P1 build (phases A-G plus the assessment fixes): the user tested it
    ("seems ok") and reported the track-page corrections (P2 A), Learn rows
    not surviving a reboot (P2 B), FX Morph not following global Morph,
    the missing `pv<`/`pv>`, and CC2 not acting on the PERF overlay
    (P2 C-E).
  - The final build: "hardware checks out" (user, 2026-10-22), covering the
    P2 check lists (§9.7).
  - The per-phase matrices (§7) were not reported row by row; §11.2 keeps
    them as the regression list.

CHANGES THIS SESSION (detail in §12):
- Core/MIDI/: rebuilt. New MidiDefs.h, MidiBuffer, MidiDin, MidiIn,
  MidiTransport, MidiNoteMap, MidiNotes, MidiControl, MidiLearn,
  MidiProgram, MidiOut, MidiPitch.h; MidiVoiceControl kept. Deleted
  MidiParser, FIFO, Uart, MidiRealtime, MidiMessages.h, MidiNoteNumbers.h.
  valueShaper.h moved to Core/DSPAudio/.
- tools/midi_test/ (new): host test of MidiNoteMap.
- Core/Hardware/USB/App/usb_midi_core.c/h, usb_manager.c/h: ring size from
  config.h, arrival stamp, drop-newest, usb_sendMidiPacket() (CIN).
- Core/Sequencer/sequencer.c/h, clockSync.c/h, sequencerTimer.c,
  LiveRecord.c/h: TX through MidiOut, Continue/SPP/resume tick, PC origin,
  seq_assignTracksToScene(), seq_rollRhythmVelocity(), arrival age.
- Core/Hardware/frontPanel/buttonHandler.c/h: per-track overlay executor
  (panel + MIDI), one overlay state settable by CC2, `pv<`/`pv>` roll
  velocity, Learn gesture and LEDs, legacy remnant removal.
- Core/Menu/menu.c/h, menuPages.h, MenuText.h: one commit path for panel
  and MIDI, MIDI settings cells, track `ch`/`nte` lists, Learn capture and
  display, Scene change with origin, A1 fix, panel TX hook, `rmp`, `pv<`/
  `pv>`, SHIFT+REC page 2.
- Core/Bank/Scene/SceneData.c/h, Preset/presetManager.c/h,
  Preset/ParameterArray.h, Pattern/PatternData.c/h: track channel/note
  domains, global Morph -> FX Morph, legacy MIDI setters removed, PAR_PERF_
  VEL_LT/RT, comments.
- Core/DSP/Effects/EffectsManager.c/h: effects_setMorphAmountFromGlobal().
- Core/Hardware/SD/filesystem.c/h, storageTypes.c/h: settings.cfg (G off,
  routing, `perf_vel_*`, lines=26), midilrn.cfg ops, sceneset domains.
- Core/Bank/Scene/AutosaveTrace.h, tools/decode_devlogs.py: trace stage
  `m`; decoder op list fixed.
- main.c, config.h, Makefile.

KNOWN ISSUES INTRODUCED: none open. Accepted behaviours: see §13.
KNOWN ISSUES RESOLVED: the pre-S083 MIDI gaps (§3); S082 P3 A1 (queued
  action on an unavailable target); stale track `ch`/`nte` display; the
  muted-track note-off bug; USB CIN framing; Learn persistence; the
  decode_devlogs.py op-name drift.

NEXT SESSION RECOMMENDED GOAL: continue SCOPING_TARGETS.md Phase 6 (MIDI
  §6.1 and A20 are done; see §15).
BLOCKERS: none.

CRITICAL REMINDERS FOR NEXT SESSION:
- A MIDI write is a menu edit: always go through menu_commitTargetEdit();
  never add a second MIDI write path.
- Channel convention in code: 1..16, 0 = off/unassigned. Track `ch` stores
  17 for `off`; track `nte` stores 128 for `off`. The menu shows off first
  and converts.
- midiTransport_processRealtime() runs in TIM3 only.
- The note map is stateless and read from the active Scene per message;
  never cache a Scene index in MIDI code (S082 R14).
- Learn rows live in midilrn.cfg, not settings.cfg.
- There is one BAR overlay state (bh_overlayActive); CC2 sets it.
- RXF/TXF default to off on a card without the keys: set them to `all`
  before testing MIDI.
```

---

## 1. Session overview and timeline

| When | Step | Record |
|------|------|--------|
| 2026-10-21 | Read the plan; fourth question round Q1-Q12; Q13 (timestamp RAM) | §4.3 |
| 2026-10-21 | Plan revision 5 | §2-§5 |
| 2026-10-21 | Code deep dive; implementation schedule written (no code) | §6 |
| 2026-10-21 | Implementation by the user, phases A-H (commit `d007027`) | §7 |
| 2026-10-21 | Assessment (§8) and the targeted fixes (§8.3) | §8 |
| 2026-10-22 | P2 A: track `ch`/`nte`/`rmp` | §9.1 |
| 2026-10-22 | P2 B: `midilrn.cfg` | §9.2 |
| 2026-10-22 | P2 C/D/E: FX Morph, `pv<`/`pv>`, CC2 | §9.3-§9.5 |
| 2026-10-22 | P2 F: comment-block audit; P2 handoff written | §9.6 |
| 2026-10-22 | Hardware: "checks out"; session close | §11 |

**Commits:**
- `f9aaeec`: S083 pre-implementation (planning documents);
- `d007027`: "s083 midi implemented, untested" (phases A-H);
- `bbea23f`: "session 083 midi, implemented and tested, needs closeout"
  (assessment fixes, all P2 work, the three root documents and the first
  close-out drafts);
- the final close-out document edits (this log's last revision, the
  specification rewrite, MEMORY.md, volatile notes) followed `bbea23f`.

**Session documents:** these root files are superseded by this log and the
specifications, and can be deleted:
- `S083_P1_MIDI_IMPLEMENTATION_PLAN.md` (revision 5 + Q13);
- `S083_P1_IMPLEMENTATION.md` (schedule, implementation notes, assessment,
  fixes, follow-ups §15-§17);
- `S083_P2_MIDI_TRACK_FIXUPS.md` (the P2 handoff).

**Authority from now on:**
- `MIDI_SPEC.md` is the complete use and code reference.
- The code wins where the two disagree.

**Resolving "plan …" citations in code comments.** The S083 code comments
cite the deleted plan by section (`plan 3.5`, `plan 3.9 C`) and by decision
id (`F27`, `Q1`, `C-1`, `R-B`, `F-2`). Decision ids are in §4 and §8 of this
log and in `MIDI_SPEC.md` §20. Section numbers map as follows (other "plan
§x" citations in the tree belong to earlier sessions' plans):

| Plan section | Topic | Now described in |
|--------------|-------|------------------|
| 1.1–1.5 (M, N, T, P, C ids) | Requirements | this log §2; `MIDI_SPEC.md` Part I |
| 2 | Pre-S083 code baseline | this log §3 |
| 3.1 | Module layout | `MIDI_SPEC.md` §12.1, §13 |
| 3.2 | Message flow, consume-then-route | `MIDI_SPEC.md` §3, §12.2 |
| 3.3 | Note map and TX inverse (Q4) | `MIDI_SPEC.md` §4.1, §9, §13.6 |
| 3.4 | Triggers, overlays, recording | `MIDI_SPEC.md` §4.3, §14.3 |
| 3.5 | Parameter writes (menu edit, NRPN) | `MIDI_SPEC.md` §5.4–§5.6, §12.3, §14.1 |
| 3.6 | MIDI Learn | `MIDI_SPEC.md` §6, §13.9 |
| 3.7 | Program and Bank change | `MIDI_SPEC.md` §7, §13.10 |
| 3.8 | Realtime and transport | `MIDI_SPEC.md` §8, §13.5, §14.2 |
| 3.9 | Transmit (N, R, C, P) | `MIDI_SPEC.md` §9, §12.4, §13.11 |
| 3.10 | Menu (G off, `chn`/`nte`, MRT, retired ids) | `MIDI_SPEC.md` §2, §14.1 |
| 3.11 | Arrival time | `MIDI_SPEC.md` §4.3, §13.7, §14.2 |
| 3.12 | Storage | `MIDI_SPEC.md` §11; `FILESYSTEM_SPEC.md` |
| 4, 13 | What MIDI needs from S082; S082 outcome notes | `MIDI_SPEC.md` §7.2, §10 |
| 6, 12 (Q13) | Resources, RAM options | this log §4.3, §10; `MIDI_SPEC.md` §18 |
| 7, 13.3, 14.4 (R ids) | Risks | this log §5 |
| 9, 11 (F, Q ids) | Decisions | this log §4; `MIDI_SPEC.md` §20 |
| 14 | S081 outcome notes (recording hooks, reset, per-track PC realign R20) | `MIDI_SPEC.md` §4.3, §5.6, §14.2; this log §5 |

---

## 2. Requirements (plan §1, as decided)

### 2.1 Global settings (MENU_MIDI_PAGE)

| Id | Cell | Requirement |
|----|------|-------------|
| M1 | `snc` | MIDI realtime (clock/transport) is a sync source for `usb`, `din` and `aut`. |
| M2 | `ch` | Global MIDI channel G, 1..16 plus **0 = off**. |
| M3 | `TXF` | What the LXR **transmits** from itself: N note, R realtime, C control (learned CC if set, else NRPN), P program + Bank change. Never governs pass-through. |
| M4 | `RXF` | What the LXR **consumes** (same letters). A blocked message is not consumed, so it is forwarded. |
| M5 | `MRT` | Routing of messages that are not consumed: `off UtD DtD DtU bth DtA AtD all`. Clock and transport are forwarded per MRT even when consumed. |

### 2.2 Note → track map (per Scene)

| Id | Situation | Rule |
|----|-----------|------|
| N1 | Only G; no track notes | Tracks striped across G: note 0 → T1 … note 6 → T7, repeating to 127. Every note plays the default pitch. |
| N2 | Only G; some tracks have a note | A track with a note leaves the stripe and owns its note (default pitch) and every higher note, a semitone each, up to the next assignment or 127. |
| N3 | Some tracks have a channel; no notes | On each channel the tracks share 0..127 in contiguous near-equal ranges in track order, each chromatic from its root; lower tracks get the remainder. |
| N4 | Channels and notes | As N3, but each assigned track starts at its note up to the next assignment; notes below the lowest assignment are shared by the channel's tracks with no note. |
| N5 | Priority | A note never triggers more than one voice; the lowest-numbered track wins. |
| N6 | Action | One trigger at the note's velocity; with the BAR overlay on, the overlay's action instead. Rolls on different tracks run at the same time (Q5). |

### 2.3 Realtime, PC/Bank, control

| Id | Requirement |
|----|-------------|
| T1 | Clock, Start, Continue (resume), Stop, SPP while stopped, MTC (existing). Start/Stop/SPP ignored with `snc = off` and accepted only from the `snc` source (Q11). MMC later. |
| P1 | A PC is a Scene change; only PC 0..15 is data. |
| P2 | PC on G = whole-Scene change exactly like a PERF SEQ press (`scd`, `pcr`), in any state. |
| P3 | PC on a track's channel moves only those tracks, like hold VOICE + SEQ; no queue. |
| P4 | CC0 on G loads Bank 000..127 through the S082 background load; the UI goes to Load/Save (S082 P3: may be left once unlocked); PC/CC0 during a load are dropped. |
| C1 | NRPN number = canonical 9-bit target ID: voices 0..383 (`slot × 64 + descriptor`), Scene 384..447, Effect 448..510. Unused IDs ignored and forwarded. |
| C2 | NRPN works on G; voice IDs (including per-voice Scene targets) also on that voice's channel, same absolute numbers. Other Scene and FX IDs on G only. |
| C3 | Values instrument-agnostic and absolute; only the Morph amounts translate (×2, 127 → 255). |
| C4 | Learn global, 128 entries, type-tagged; one CC → many parameters, one parameter → at most one CC. (Stored in `settings.cfg` as planned; moved to `midilrn.cfg` in P2 B.) |
| C5 | Learn gesture SHIFT+PLAY+REC, touch, `LRN`, three LEDs flash, any of the three cancels; no UI remove; re-learn replaces. |
| C6 | Reserved, never learnable: 0, 1, 2, 6, 32, 38, 96..101, 120..127. |
| C7 | CC1: global Morph on G (never recorded, G-1); on a voice channel that voice's Morph for every voice on the channel (recorded as an individual edit). |
| C8 | CC2 on G only, any mode: 0 single, 1 BAR `<`, 2 BAR `>`. (P2 E: CC2 sets the PERF BAR overlay itself.) |
| C9 | **A MIDI write is a menu edit (Q1).** |

---

## 3. Baseline before S083 (plan §2, checked 2026-10-16/21)

| Area | What existed | Gap closed |
|------|--------------|------------|
| Framing | `MidiParser.c` DIN framing; USB framed in `usb_midi_core.c`; realtime to the `MidiRealtime` ring (TIM3) | SPP/Song Select not framed; `usb_sendMidi()` used `status >> 4` as CIN (system common and SysEx malformed on USB); USB RX ring overwrote when full |
| Context | `midi_service()`: `uart_processMidi()` + 8 USB messages per pass | Channel messages carried no timestamp |
| Routing | Every message forwarded **before** parsing | Consume-then-route, new MRT list |
| Filters | `midiParser_txRxFilter` (RX low nibble, TX high, N=1 R=2 C=4 P=8) | Kept as the RXF/TXF layout |
| Channels | `midi_MidiChannels[8]`: [7] = G (no off), [0..6] a legacy mirror; the Scene getter fell back to track+1 | G off; track unassigned; one source of truth (Scene settings) |
| Note input | G played the menu-active voice; elsewhere every exact (channel, note) match fired | N1-N5 |
| Live record | Notes via `liveRec_triggerIn()`; CC to the stub `liveRec_paramIn(0xFFFF, MIDI)` | Real target IDs; arrival age |
| CC | CC1 existed; other CCs used the legacy LXR CC/CC2 map into the active Scene's Normal endpoint (S075 F3) with no fan-out | NRPN + Learn, fan-out, menu-edit semantics |
| NRPN | 14-bit number truncated to 8 bits into the legacy CC2 page | 9-bit target IDs |
| PC | RX `seq_setNextPattern()` was a no-op; TX on Scene change | P1-P3 |
| Realtime | Continue = Start; no SPP | T1 |
| TX | Note-off cache one note per channel (`midi_chan_notes[16]`) | Per-track record |
| Overlay | S080 single repeat slot (`bh_overlayTrack/Note/Vel/Count/Interval/Next`) | Per-track table (Q5) |
| Dead code | `midiParser_playVoiceMidiNote()`, `midiParser_triggerVoice()`, `preset_applySoundParameter()`, the STEP armed-automation reset-lock remnant, `midi_NoteOverride[7]`, about 1,000 lines of `#if 0` | Removed |

---

## 4. Decisions

### 4.1 Decision log F1-F51 (2026-10-16/17, Session 082 planning)

| F | Outcome |
|---|---------|
| F1 | Canonical 9-bit IDs. |
| F2 | Scene target block only; per-voice Scene targets allowed on the voice channel. |
| F3 | Absolute numbering on voice channels. |
| F4 | Absolute values, instrument-agnostic, no special casing; Morph uses its 7-bit translation. |
| F5 | Fan out; active Scene. (Per-track playback: writes and Learn type matching use the active Scene, accepted, R10.) |
| F6 | Learn global, 128 entries, type-tagged. |
| F7 | One CC → several targets; each is one of the 128 entries. First match on TX. |
| F8 | SHIFT+PLAY+REC + parameter, `LRN`, flashing, cancel with any of the three, no UI remove, re-learn replaces. |
| F9-F10 | Not needed (no fan-out; applies device-wide by voice and type). |
| F11, F13-F15, F17, F21, F23-F25 | Yes (as recommended). |
| F12 | Channel and note stay per Scene. |
| F16 | Keep the stripe from note 0. |
| F18 | Banks 000..127 only; the UI stays on Load/Save; no separate Bank enable. |
| F19 | Background load design (became S082). |
| F20 | Discard (fastest); AutoSaves are stale after a Bank load. |
| F22 | MRT `off UtD DtD DtU bth DtA AtD all`. |
| F26 | CC6 value, CC38 ignored, CC96/97 ±1, clamp to max. |
| F27 | One CC → many parameters (one row each); one parameter → at most one CC; re-learn replaces CC and channel. |
| F28-F36 | Accepted (F36: 4 B entries, about 519 B). |
| F37 | `RXF P` governs both PC and CC0; no `bnk` setting. |
| F38-F48 | Moved to S082 as D1-D16 (see `082_SESSION_HANDOFF_LOG.md`). |
| F49 | Re-learning on another channel kind takes the new kind. |
| F50 | A learned CC applies only to its entry's slot, even on a shared voice channel; CC1 is the only CC reaching every voice on a shared channel. |
| F51 | While Scene 17 is active, Learn type matching uses Scene 17's slot types. |

### 4.2 Architectural decisions (plan §9)

| Topic | Decision |
|-------|----------|
| Address space | One 9-bit ID space for automation, LiveRecord, NRPN and Learn. |
| Values | Absolute 7-bit, instrument-agnostic; Morph translation only. |
| Learn | Global, 128 type-tagged entries; no fan-out, no Scene storage, no UI remove. |
| Note map | Per Scene, resolved on the fly (no table). |
| MIDI writes | A menu edit: one shared commit entry, active Scene, edit-mask fan-out, R12/R13 and recording as for the menu, Normal image only, no echo. |
| Module layout | `Core/MIDI/` rebuilt as single-role modules; `MidiLearn` routes CCs into the other modules. |
| Overlay executor | Per-track repeat state, one table for panel and MIDI. |
| Bank change | Background load through Scene 17. |
| Routing | Consume first, then forward; realtime always forwarded per MRT. |
| Out of scope | External MIDI sequencing tracks (§6.8), one-shot LFOs, MMC, SysEx dump/restore, MIDI 2.0, multi-cable USB. |

### 4.3 Fourth round Q1-Q13 (2026-10-21)

| Q | Outcome |
|---|---------|
| Q1 | **MIDI has the same effect as a menu edit:** same priority, same actions against automation and recording; a parameter shown in the menu updates. One shared Menu commit (`menu_commitTargetEdit()`). Replaces the S075 F3 "MIDI lowest" rule. |
| Q2 | **User rule (binding):** "Scenes or tracks from Scenes are not automatable unless they are changed via a voice edit mask from an active Scene or track." Applied as the S082 P3 A1 fix: a queued action whose target Scene is unavailable is skipped whole (mute and Morph parts included). |
| Q3 | S081 and S082 passed their hardware checks (user). |
| Q4 | No TX when the inverse map cannot express the pitch on the track's own range. |
| Q5 | (b) per-track overlay repeat state; RAM approved. One executor table for panel and MIDI (R23). |
| Q6 | Arrival timestamps; the only use is the right step position when recording. |
| Q7 | Rebuild `Core/MIDI/` with new, mostly renamed files: `MidiBuffer` replaces `FIFO`; a new `MidiLearn` coordinates CCs into the other modules; `tools/midi_test/` approved. |
| Q8 | With G off, voice targets transmit as NRPN on the voice channel. |
| Q9 | No PC/CC0 echo for MIDI-started Scene changes and Bank loads; a Menu Bank load sends CC0 only on DONE. |
| Q10 | The Learn target touch does not change the value; Learn targets the parameter ID (Normal) even in a Morph view. |
| Q11 | SPP gated by `snc` like Start/Stop. |
| Q12 | The USB RX ring drops the newest packet when full. |
| Q13 | (b): DIN stamps 128 B; USB RX ring 128 × 6 B; ring size `MIDI_USB_RX_RING_SIZE` in `config.h` (power of two, a larger value needs a new RAM acknowledgement); drain `MIDI_USB_DRAIN_PER_PASS` 16; the queued-PC origin bit accepted. |

Q13 options as weighed (for the record):

| Option | DIN | USB | Net |
|--------|-----|-----|-----|
| (a) | +128 B | 256 × 6 B | +640 B |
| (b) chosen | +128 B | 128 × 6 B | −1,152 B measured (the old ring was 2,048 B) |
| (c) | — | — | 0 (placement at processing time) |

Reasoning for (b): one USB full-speed frame carries at most 16 events per
64 B packet; 128 entries hold 8 such packets; with drop-newest and a
drain of 16 per pass a dense DAW stream is not lost; only a long SysEx
forward could overflow, and SysEx is pass-through only.

### 4.4 Schedule items confirmed before Phase A

**RAM deltas R-A..R-D (+20 B over the approved list, accepted):**

| Id | Item | Bytes | Why |
|----|------|------:|-----|
| R-A | Per-track overlay table 7 × 8 B (was planned as 6 B per track) | 56 | Held MIDI note, internal note + `def` flag in bit 7, velocity, count, interval 2, next 2. |
| R-B | TX note-off per track 7 × 2 B | 14 | A note-off must go to the channel its note-on used; the channel can change in between. Still 4 B less than the 18 B it replaced. |
| R-C | `seq_resumeTick` (uint32) | 4 | Stop resets the master clock; Continue and SPP need a resume position. A 16-bit 1/16 count would wrap after 1,024 bars. |
| R-D | DIN framer in-progress `MidiMsg` 4 → 6 B | +2 | Measured −2 B: the old message was 8 B. |

**Consequences C-1..C-11 (applied; none vetoed):**

| Id | Consequence |
|----|-------------|
| C-1 | A track's `nte` is a MIDI root only. The overlay `def` note and the stopped-transport VOICE preview play the default pitch (internal 63), not the track's MIDI note (before S083, `nte` 36 played 27 semitones down). |
| C-2 | The sequencer's outgoing note is the inverse map, not "`nte` if set, else the step note". |
| C-3 | MIDI-in note triggers are transmitted (TX N) on the inverse-mapped note. |
| C-4 | While Learn waits for a target, touching a cell without a MIDI address edits it normally; only an addressed cell is captured without an edit. |
| C-5 | A Scene change (MIDI PC, chain, queue) repaints the PERF Scene LEDs only in PERF mode, so the STEP row is not overwritten. |
| C-6 | TX uses each track's played Scene's channel/note settings; RX uses the active Scene. |
| C-7 | NRPN data CCs (6, 96, 97) are consumed only when the selected number is a valid target there; CC98/99/100/101/38 are always consumed on G or a voice channel. |
| C-8 | A per-track PC is not transmitted on a channel equal to G. |
| C-9 | A held BAR beat CC2 when both selected an overlay. **Retired in P2 E** (one state, last wins). |
| C-10 | SysEx forwarded to USB goes as single-byte packets (CIN 0xF). |
| C-11 | Received Start/Stop/Continue are forwarded per MRT but not re-sent by the LXR's own transport TX. |

---

## 5. Risks (plan §7, §13.3, §14.4) and their status

| Id | Risk | Status at close |
|----|------|-----------------|
| R1 | Scene 17 in 16-Scene code paths | MIDI resolves the active Scene per message; no cached index. |
| R2 | AutoSave off while Scene 17 exists | Accepted (S082 D11). |
| R3 | A stray CC0 from a DAW replaces all 16 Scenes | Accepted; `RXF P` off blocks PC and CC0. |
| R4 | The note map is per Scene, so a Scene change remaps a DAW drum track | Accepted (F12). |
| R5 | DIN bandwidth with NRPN TX during sweeps | Coalescing, number cache, 10 ms throttle, 9 B room check. |
| R6 | Echo feedback loops | No MIDI-originated change is transmitted. |
| R7 | NRPN numbers mean different parameters per type | By design (C3); Learn avoids it with its type tag. A per-type NRPN chart is still to be published. |
| R8 | Foreground latency delays notes and placement | Placement uses arrival stamps (Q6); sound plays at processing time. |
| R9 | Note-on forwarded, note-off consumed after a map change (or reverse) leaves a hanging note downstream | Accepted. |
| R10 | Per-track playback vs active-Scene writes | Accepted (F5). |
| R11 | Settings line width/count with Learn lines | Superseded: Learn moved to `midilrn.cfg` (≤131 lines). |
| R12 | Hidden internal callers of the legacy map | Audited and removed (A-19). |
| R13 | P4 unverifiable before S082 hardware | Closed (Q3). |
| R14 | A MIDI module caching a Scene index | No cache: `midiNoteMap_load(scene_getActiveIndex())` per message. |
| R15 | Writes into Scenes a load is replacing | Fan-out through `bank_editTargetMask()` (availability-aware). |
| R16 | S082 P3 A1 queued action on an unavailable target | Fixed (Q2, D-1). |
| R17 | S082 hardware untested | Closed (Q3). |
| R18 | DEV trace flush suppressed during a Load/Save command | Unchanged; copy the card after the command ends. |
| R19 | Write-path priority undecided | Closed (Q1). |
| R20 | A MIDI per-track PC without a realign starts no new take | Fixed: `seq_assignTracksToScene()` realigns (epoch bump). |
| R21 | S081 hardware lists unrun | Closed (Q3). |
| R22 | MIDI placement used processing time | Fixed (Q6). |
| R23 | Per-track executor changes the panel (several held VOICE buttons roll together) | Applied; the user did not veto. |
| R24 | The rebuild touches about 20 files and the TIM3 realtime path | Phase A kept behaviour parity; the user's hardware testing passed. |
| R25 | 16-bit µs stamps wrap every 65.5 ms | Ages above `MIDI_ARRIVAL_MAX_AGE_US` (20 ms) fall back to processing time. |

---

## 6. The implementation schedule (method)

- **Source:** written from a code deep dive at `f10e797`.
- **Every change** is listed by file, line, anchor and action (ADD,
  REMOVE, MODIFY, NEW FILE, DELETE FILE, MOVE FILE).
- **Comment blocks** are written to paste as they stand: What, Why,
  Inputs, Outputs, Accessors, Affiliates.
- **Two-file pattern:** the `.h` carries the full contract and the `.c`
  says "Contract in X.h" plus implementation notes (the
  `presetManager`/`EffectsManager` pattern).
- **Licence blocks:**
  - Derived files keep the original LXR licence block above the Brendan
    Clarke modifications block: `MidiDin` from `Uart.c`, `MidiBuffer` from
    `FIFO.c`, `MidiIn`/`MidiTransport` from `MidiParser.c`, `MidiDefs.h`
    from `MidiMessages.h`, `MidiPitch.h` from `MidiNoteNumbers.h`.
  - New files carry the Brendan Clarke block only.
- **Channel convention:** 1..16 with 0 = off/unassigned inside the new
  code; the wire nibble is `channel − 1`. This removed the zero-/one-based
  mixing of `midi_MidiChannels[]` and the old `scene_getTrackMidiChannel()
  − 1` calls.
- **Order:** phases A → H; Phases B, D, E and F add their modules then.
  Phase A creates no stubs, so every phase builds on its own.

---

## 7. Implementation by phase (as built)

### 7.1 Phase A — rebuild, cleanup, consume-then-route

**Modules and files:**
- A-1: `config.h` MIDI block: `MIDI_USB_RX_RING_SIZE` 128,
  `MIDI_USB_DRAIN_PER_PASS` 16, `MIDI_DIN_DRAIN_PER_PASS` 16,
  `MIDI_ARRIVAL_MAX_AGE_US` 20,000. Later phases added
  `MIDI_NRPN_CONTEXTS` 8, `MIDI_LEARN_MAX_ENTRIES` 128,
  `MIDI_LEARN_FULL_SHOW_MS` 1,000 and `MIDI_OUT_PARAM_INTERVAL_MS` 10.
- A-2..A-8: new `MidiDefs.h`, `MidiBuffer`, `MidiDin`, `MidiPitch.h`,
  `MidiTransport`, `MidiOut`, `MidiIn`.
- A-9: `MidiVoiceControl.c` includes `MidiDefs.h` (no other change).
- A-10: the old files were deleted; `valueShaper.h` moved to
  `Core/DSPAudio/`. No include edits were needed: `-ICore/DSPAudio` was
  already on the path.
- A-11: the Makefile source list was updated.

**Callers:**
- A-12: `main.c` calls `midiDin_init()`; the body of `midi_service()` is
  `midiIn_service()`.
- A-13: `sequencerTimer.c` calls `midiTransport_processRealtime()` in TIM3.
- A-14: `sequencer.c` sends all MIDI through `MidiOut`:
  - removed `midi_chan_notes[16]`/`midi_notes_on`, `seq_setNextPattern()`
    (a no-op), `seq_sendMidi()`, `seq_midiNoteOff()` and
    `seq_sendMidiNoteOn()`;
  - added `midiOut_trackNoteOff(0xFF)` at whole-Scene changes and at Stop;
  - `seq_sendRealtime()` → `midiOut_realtime()`;
  - `seq_sendProgChg()` uses `MidiOut` and refuses index ≥ 16.
- **Bug fixed by A-14d:** `seq_setMute()` had passed a zero-based MIDI
  channel as a voice index, stopping the wrong voice or none. It now ends
  the muted track's voice and MIDI note.
- A-16, USB:
  - the ring size comes from `config.h`, with a compile-time
    power-of-two ≤ 256 check;
  - entries carry a 16-bit stamp from `packetTimestampUs`;
  - the ring drops the newest message when full (Q12);
  - `usb_sendMidi()` became `usb_sendMidiPacket(cin, b0, b1, b2)`;
    `MidiOut` computes the CIN.
- A-17: `Oscillator.c` uses `midiPitch_detune()` (was
  `midiParser_calcDetune()`).
- A-18: include renames in `SomGenerator.c`, `InstrumentManager.c`,
  `lfo.c` and `StereoFilterEffect.c`.
- A-19, legacy path audit (plan R12):
  - removed `preset_applySoundParameter()`;
  - `menu_sendSoundParameter()` now only calls `menu_parseGlobalParam()`,
    because sound parameters left the flat namespace and
    `END_OF_SOUND_PARAMETERS` is 1;
  - removed the STEP armed-automation reset-lock remnant in
    `buttonHandler.c` (`buttonHandler_resetLock` was never set, and the
    path fed the retired legacy map).

**Settings and data:**
- A-20, menu MIDI settings:
  - `ch` 0..16 (0 shows `off`), using `DTYPE_1B16` plus a static-param
    special case because the dtype space is full;
  - track `chn` 0..16 (0 `glb`), with a note-off on change;
  - `nte` 0 = `def` (became `on`/`off` in P2 A);
  - MRT and RXF/TXF through `midiIn_setRouting()`/`midiIn_setFilter()`;
  - `PAR_MIDI_CHAN_1..7` and `PAR_MIDI_MODE` retired as no-op slots, so
    nothing was renumbered.
- A-21: `MenuText.h` MRT names, `glb` (and the then `def`).
- A-22: `SceneData`: channel 0 = unassigned, note 0 = `def`, defaults 0;
  the getter's track+1 fallback was removed.
- A-23: `storageTypes.c` accepts `midi_channel` 0.
- A-24: `filesystem.c` defaults and G-off sanitising (0..16 valid; above
  16 falls back to 1).

**Deviations and findings:**
- The framer's `IGNORE` initial state is kept, so a stray data byte before
  any status is discarded.
- **G+ size fix:** the planned `struct MidiBits` with an `enum MidiSource`
  bitfield made `MidiMsg` 12 B (GCC gives the enum an `int` container).
  `MidiBits` is now one byte of `uint8_t` bitfields, so `MidiMsg` is 6 B.
  `MidiIn.c` checks both sizes with `_Static_assert`.

### 7.2 Phase B — note map, note actions, per-track overlay, arrival age

- B-1: `MidiNoteMap.c/h` (N1-N5 and the Q4 inverse), stateless and
  table-free. `MIDI_NOTEMAP_HOST_TEST` builds it alone.
- B-2/B-3: `tools/midi_test/notemap_test.c` and its Makefile. Run with
  `make -C tools/midi_test run`. It covers:
  - the §4.2 examples;
  - N5;
  - G off;
  - 200,000 random settings, with the invariant that every sent note
    resolves back to the same track and internal note;
  - from P2 A, the `ch off`/`nte off` cases.
- B-4: `MidiNotes.c/h`:
  - the single-trigger path calls `seq_triggerVoice()` and then reports
    the same hit to LiveRecord, so the trigger jack, TX and the record
    agree;
  - roll repeats pass `reset = 0`.
- B-5: `MidiIn` gets the note and CC2 consumers.
- B-7/B-8: `liveRec_trigger_t.age_us`. `liveRec_triggerIn()` moves the
  captured position back by `age_us × 256 / periodUs` Q8 ticks, limited to
  one step interval.
- B-9..B-11: the per-track overlay executor `bh_overlay[7]` replaced the
  single slot, with public MIDI entry points.
- **B-12 behaviour notes:**
  - MIDI-held rolls are not VOICE holds: they neither hold nor join the
    looper, and a looper engage does not end them;
  - a MIDI flam ignores its note-off (the main hit always plays);
  - a MIDI roll ends at its note-off.

### 7.3 Phase C — transport

- C-1/C-2: `seq_setRunningEx(isRunning, sendRealtime)`,
  `seq_continueRunning(sendRealtime)` and `seq_setResumeTick(tick)`.
  - Stop stores `seq_resumeTick`; Start sets it to 0.
  - Continue places the master clock one tick before `resume` and
    realigns every track and the FX sequence through the master-clock
    anchors.
- C-3: `clockSync.c`: a received Start/Stop calls
  `seq_setRunningEx(.., 0)` (C-11: not re-sent); new
  `sync_midiContinue()`.
- C-4: `MidiTransport`:
  - Continue → `sync_midiContinue()`;
  - SPP while stopped, from the `snc` source, → `seq_setResumeTick(pos ×
    24)`.
  - The SPP source test is the side-effect-free twin of the realtime gate:
    it does not touch the AUTO activity flags.

### 7.4 Phase D — PC, CC0, the A1 fix

- D-1: the A1 fix in `menu_serviceDeferredSceneChange()`: queued actions
  run only for `scene < 16 && bank_sceneAvailable(scene)`.
- D-2: the PERF LED guard (C-5).
- D-3: `menu_perfModeSceneChangeFrom(scene, origin)`; the old
  `menu_perfModeSceneButtonPressed()` is its panel wrapper.
- D-4: `seq_sceneChangeFromMidi` (1 B) via `seq_setSceneChangeOrigin()`.
  `seq_sendProgChg()` consumes it. A chain fire clears it, so a
  chain-raised change does send its PC.
- D-5: `seq_assignTracksToScene(mask, scene)`, shared by the panel (hold
  VOICE + SEQ) and MIDI. For each track it:
  - sets the played Scene;
  - realigns it (epoch bump, so LiveRecord starts a new take; R20);
  - ends its MIDI note;
  - applies each slot once.
- D-6/D-7: `MidiProgram` (PC with availability checks; CC0 calls
  `preset_requestBackgroundBankLoad(v, 0, 0, PRESET_BANK_ORIGIN_MIDI)`).

### 7.5 Phase E — NRPN, CC1, MIDI writes as menu edits

- E-2: the cell commit became
  `menu_cellCommitValueEx(cell, value, panel)`:
  - `menu_cellCommitValue()` is the panel wrapper;
  - with `panel = 0` the panel's view state is ignored (Normal image
    always);
  - `menu_commitTargetEdit(id, value7, origin)` resolves a 9-bit ID in the
    active Scene into the same cell commit, records it and repaints it;
  - `menu_commitGlobalMorphFromMidi()` handles G-1;
  - also added `menu_cellMidiTarget()`, `menu_targetVisible()`,
    `menu_notifyTargetChanged()`, and track timing by target ID
    (`menu_commitTrackTimingTarget()`).
- E-3: removed `preset_setInstrumentParameterFromMidi()` and
  `menu_clampInstrumentValue()`.
- E-4: `MidiControl`:
  - NRPN contexts 2 ports × 8 × 3 B;
  - CC1, CC2 and the channel rule (C2, C-7);
  - CC96/97 step the current value read by `midiControl_targetValue7()`.
- **Finding:** `menu_cellCommitValueEx()` keeps the live Morph-image
  decision only for the flat sound ids admitted by
  `menu_paramUsesMorphView()`. MIDI targets are static
  Scene/instrument/Effect cells, so they always commit Normal, even with a
  panel Morph view open: the intended Q1 result.

### 7.6 Phase F — MIDI Learn

- F-2/F-3: `MidiLearn`:
  - 128 rows of 4 B: ccKind (bits 0..6 CC, bit 7 = kind `v`), space
    (0..5 slot, 6 Scene, 7 Effect), type, index;
  - capture, replace, full and cancel;
  - type matching;
  - the `learn=` parse/format.
  - The firmware links no printf: `midiLearn_append()` builds each line
    by hand. An unresolvable row is written as a `#` comment so the
    `lines=` count stays exact.
- F-4: `midiControl_handleCc()` ends with Learn. For non-reserved CCs only,
  a waiting capture takes the CC first; otherwise every matching row is
  applied.
- F-5: `midiLearn_service()` (the `FUL` timeout) runs in `midiIn_service()`.
- F-6, the gesture in `buttonHandler.c`:
  - **F-6a: SHIFT+PLAY no longer toggles the transport in any mode.** It
    arms the chord; SHIFT+PLAY+SEQ in PERF still opens the chain editor;
  - SHIFT+PLAY+REC arms Learn;
  - a press of SHIFT, PLAY or REC while Learn is active cancels it and is
    consumed;
  - `buttonHandler_setLearnLeds()`.
  - The arm test is the first statement of `case BUT_REC:`. This was
    accepted: while live erase is on, the erase router consumes PLAY
    first.
- F-7: `menu.c`:
  - pot and encoder target selection without a value change (Q10);
  - `LRN`/`FUL` display (`menu_learnName()`);
  - cancel on a page or mode change.
- F-8: `learn=` lines in `settings.cfg`, with `lines=24+N`. **Superseded by
  P2 B.**

### 7.7 Phase G — transmit

- G-2: `midiControl_targetChannel()` (Q8).
- G-3/G-4: `MidiOut`:
  - `midiOut_trackTrigger()` (the inverse map with the Q4 rule);
  - parameter TX: learned CC else NRPN, number cache per port,
    `MIDI_OUT_PARAM_INTERVAL_MS` 10 throttle, a 9 B DIN room check;
  - `midiOut_service()` is the sole consumer of the S082 one-shot Bank
    result byte: a Menu-origin DONE sends CC0, and a MIDI-origin or failed
    load sends nothing;
  - `midiOut_trackProgramChange()` (C-6, C-8).
- G-5: `seq_triggerVoice()` → `midiOut_trackTrigger(track,
  seq_perTrackPattern[track], note, vol)`.
- G-6: the preview plays internal 63 (C-1).
- G-7: the panel TX hook `midiOut_parameterChanged(id)` after the
  encoder/pot commit. It is skipped by the early-return paths (LFO and
  velocity target cells, roll rhythm, held-step overlays), which was
  accepted.
- G-8: the per-track PC TX after the panel `seq_assignTracksToScene()`.

### 7.8 Phase H — documents

Specifications and `MEMORY.md` were updated at the time. They were rewritten
again at this session close (§14).

### 7.9 Build trail (DEV link, `make all`)

| Point | text / data / bss | Note |
|-------|-------------------|------|
| S082 close | 598,120 / 420 / 430,928 | baseline |
| A | 594,400 / 428 / 430,268 | legacy parser state released |
| B | 595,352 / 424 / 430,312 | overlay table +47 B |
| C | 595,736 / 424 / 430,312 | `seq_resumeTick` inside alignment |
| D | 595,864 / 428 / 430,328 | origin flag |
| E | 599,608 / 428 / 430,376 | NRPN 48 B |
| F | 602,688 / 424 / 430,904 | Learn 528 B with alignment |
| G | 603,984 / 424 / 430,896 | TX state |
| G+ (size fix) | 603,896 / 424 / 430,120 | `MidiMsg` 6 B, USB ring 768 B |
| assessment fixes | 604,200 / 424 / 430,112 | |
| P2 A | 605,784 / 424 / 430,112 | |
| P2 B | 608,424 / 424 / 430,112 | |
| P2 C-E | 608,744 / 424 / 430,112 | **final**; flash 608,848 / 753,664 B, headroom 144,816 B |

---

## 8. Assessment and fixes (2026-10-21)

### 8.1 Gates at the assessment

- Build and image PASS; host test PASS.
- `sizeof(MidiMsg) == 6` and `sizeof(struct MidiBits) == 1` are asserted.
- **Warnings:** the 7 `-Wunused-function` warnings in `filesystem.c`
  (`filesystem_applyLegacy22Globals`, `_blockRename`,
  `_reportBootSubstep`, `_bootLoggingSetKitDetail`,
  `_applyStaleGlobalsFallback`, `_staleMetaPrefixLen`,
  `_metaHasStoredGlobalsLen`) **predate S083**.
  - The first assessment missed them because `filesystem.o` had not been
    rebuilt; this was corrected.
  - They are left as they are.

### 8.2 RAM as linked, and the old ring

Net bss versus the S082 close was **−808 B**, where the plan expected about
+220 B.

**Cause:** the pre-S083 `MidiMsg` was **8 B**, not 4 B. Its enum bitfield
took an `int` container on this ABI, so:
- the old USB ring was 256 × 8 B = **2,048 B**, and Q13 (b) released
  1,280 B;
- R-D was −2 B.

This was D-1; the documents were corrected.

### 8.3 Findings and fixes (all fixed, no new RAM)

| Id | Finding | Fix |
|----|---------|-----|
| F-1 (medium) | Continue replayed the last tick before Stop: a double hit when a step boundary fell there | Stop stores `seq_resumeTick = seq_initialSchedulerTick ? 0 : seq_elapsedPpqTicks + 1` (the first unplayed tick). |
| F-2 | A TIM3 trigger and a foreground trigger of one track could overwrite the note record, leaving a hanging note | `midiOut_trackNoteOffLocked()`; one PRIMASK section across the previous off, the note-on and the record update; velocity masked to 7 bits. |
| F-3 | BAR overlay off stopped MIDI rolls although CC2 still selected one | Selector `BH_OVERLAY_SEL_ALL/MIDI/PANEL`. **Superseded in P2 E** (one state). |
| F-4 | Parameter TX could carry a byte > 127 (target-selection tokens up to 0xFF; 0xFF = System Reset) | `midiControl_targetValue7()` clamps to 127; `midiOut_cc()` masks the channel to 4 bits and the data to 7. |
| F-5 | Learn not cancelled by a MIDI Bank load started while already on Load/Save | `menu_backgroundBankLoadUiBegin()` calls `midiLearn_cancel()` first. |
| F-6 | DIN stamp read after the read index was published | `midiBuffer_rxOut()` reads byte and stamp first. |
| F-7 | A per-track change of T6 or T7 alone moved the HiHat pair but realigned one | `seq_assignTracksToScene()` and `midiOut_trackProgramChange()` treat bits 5/6 as a pair. |
| D-1 | Documents said the old ring was 1,024 B | Corrected to 2,048 B / −1,280 B. |
| D-2 | `midiOut_trackProgramChange()` comment named MIDI as a caller | Panel only (a received per-track PC is not echoed, Q9). |

### 8.4 Observations for testing (still valid)

- **Filters default to off.** `filesystem_resetSettingsToDefaults()` sets
  `midi_filt_rx`/`midi_filt_tx` to 0. A card without these keys consumes
  and sends nothing, and MRT also defaults to `off`. Set RXF and TXF to
  `all` for MIDI tests. This predates S083; changing the default is a
  product decision.
- **SHIFT+PLAY** no longer toggles the transport (F-6a).
- **MIDI flams and rolls:** a MIDI flam ignores its note-off; a roll ends
  at it; MIDI-held rolls are not looper holds.

---

## 9. P2 — hardware follow-ups (2026-10-22)

### 9.1 A: track settings page 2 (`ch`, `nte`, `rmp`)

**Request:**
- The STEP track settings page 2 is `ch nte lop asm`.
- `ch`: add `off` below `glb`.
- `nte`: category Track (it showed Step); add `off` below the default; the
  default `def` is renamed `on`.
- `asm`: label `rmp`, category `Automtn`, long name `Ramp`.

**Meaning chosen** (unstated by the user, confirmed on hardware):

| Setting | Effect |
|---------|--------|
| `ch glb` (default) | Follow G (unchanged). |
| `ch off` | No MIDI channel at all: no notes in or out, no voice-channel CC1/NRPN/Learn/PC, no per-track PC out. |
| `nte on` (default) | No root: stripe on G, split elsewhere (unchanged). |
| `nte 1..127` | Root of the receive range (unchanged). |
| `nte off` | Left out of the note map: no notes in or out, and no stripe/split position. The channel still carries CC1/NRPN/Learn/PC. |

**Storage versus menu order.** The defaults stay stored as 0, so there is
no format change and no migration:

| Setting | Stored | Menu order |
|---------|--------|------------|
| `ch` | 0 `glb`, 1..16, 17 `off` (`MIDI_TRACK_CHANNEL_OFF`) | 0 `off`, 1 `glb`, 2..17 |
| `nte` | 0 `on`, 1..127, 128 `off` (`MIDI_TRACK_NOTE_OFF`) | 0 `off`, 1 `on`, 2..128 |

`menu.c` converts at the edge: menu = stored + 1, with `off` at 0
(`menu_trackMidiChanToMenu/FromMenu()`, `…NoteToMenu/FromMenu()`).

**Changes:**
- **`MidiDefs.h`:** the two constants.
- **`SceneData`:** the setters clamp to 0..17 / 0..128 (above becomes
  `off`); the getters return the `off` values.
- **`storageTypes.c`:** the `sceneset.scg` parse accepts 0..17 and 0..128.
- **`MidiNoteMap`:**
  - `effectiveChannel()` returns 0 for `ch off` (not G);
  - static `midiNoteMap_noteTracksOnChannel()` drops `nte off` tracks for
    `resolve()` and `inverse()`;
  - `tracksOnChannel()` keeps them (CC/NRPN/PC);
  - host-local copies of the constants, tied by `_Static_assert`.
- **Host test:** new cases plus a "`nte off` never resolves" invariant.
- **`menu.c`:**
  - `menu_getParameterDisplayValue()` reads track `ch`/`nte` from the
    **viewed Scene** in menu order. This fixed a pre-existing stale
    display: the mirror was never refreshed from the Scene.
  - Clamps: G 0..16, `ch` 0..17, `nte` 0..128.
  - `menu_getMidiStaticName()` names the lists.
  - `menu_parseGlobalParam()` converts and ends the track's MIDI note on a
    `ch` **or** `nte` change.
- **Menu text:**
  - new `TEXT_TRACK_NOTE` (category Track); the step-special note keeps
    `TEXT_NOTE` (category Step);
  - new `CAT_AUTOMATION`; `asm` → `rmp`, long name `Ramp`;
  - `menuText_def` removed.
- **Comments:** `asm` → `rmp` everywhere. The code and storage names
  (`auto_smooth`, `PAR_TRACK_AUTO_SMOOTH`, PAT4 byte 5) are unchanged.

### 9.2 B: MIDI Learn persistence — `midilrn.cfg`

**Root cause:** `midiLearn_formatSettingsLine()` returned `1` instead of the
line length in both returns.
- `filesystem_writeTextLine()` writes exactly that many bytes, so each row
  reached `settings.cfg` as `l`.
- On load the `l` line (no `=`) was a parse error. The fixed lines had
  loaded, but reading stopped and no row was restored.
- The defect came from the schedule's F-3 text.

**User decision:** Learn rows move to their own root file, `midilrn.cfg`,
**safely rewritten every time a Learn assignment is made**. The seven
follow-up decisions were all accepted:

1. **Format:** `format=helicase.midilearn`, `version=1`, the `learn=` rows,
   then `lines=N` with N = 2 + rows. Editable by hand. An unresolvable row
   is written as a `#` comment.
2. **`settings.cfg`:** back to fixed lines. A `learn=` line from the first
   S083 build is an ignored unknown key; no migration.
3. **Safe write:** `midilrn.tmp`, sync, remove the old file, rename. At
   boot, a leftover tmp is validated by its terminator, then promoted or
   discarded.
4. **Timing:**
   - immediate, with no debounce, when a row is stored or replaced; `FUL`
     writes nothing;
   - waits for an idle facade (AutoSave, a Load/Save command, a background
     Bank load);
   - a Learn made during a write queues exactly one more write;
   - Learns during an unlocked load are kept.
5. **Boot:**
   - read right after `settings.cfg`, before audio;
   - a missing file means an empty table; malformed rows are skipped;
   - a settings load no longer clears the table.
6. **RAM:** 1 B request flag (SRAM1, static, owner filesystem).
7. **Trace:** stage `m`, in logging builds only.

**Changes:**
- **`MidiLearn`:**
  - formatter returns `(uint8_t)pos`;
  - renamed `midiLearn_formatFileLine()` / `midiLearn_parseFileValue()`;
  - `midiLearn_captureCc()` calls `filesystem_requestMidiLearnWrite()`.
- **`storageTypes.h`:** `STORAGE_MIDILEARN_FILENAME` and
  `STORAGE_MIDILEARN_TEMP_FILENAME`.
- **`filesystem.h`:** `filesystem_requestMidiLearnWrite()` and
  `filesystem_requestLoadMidiLearn(cb)`.
- **`filesystem.c`, settings revert:** removed `FS_SETTINGS_FIXED_LINES`,
  the learn lines and the `learn` key, and `midiLearn_clearAll()` from the
  settings defaults.
- **`filesystem.c`, new ops:** `FS_INTERNAL_OP_LOAD_MIDILEARN` and
  `FS_INTERNAL_OP_SAVE_MIDILEARN`, **appended at the end of the enum** so
  trace op numbers stay stable. Boot code `MLRNLOAD`; error names
  `MlrL`/`MlrS`.
- **`filesystem.c`, other:**
  - state `fs_midilearn_write` (bit 0 PENDING, bit 1 RETRIED);
  - `filesystem_parseMidiLearnLine()`: ERROR only for a wrong
    format/version;
  - `filesystem_nextMidiLearnLine()`;
  - `filesystem_loadMidiLearn_tick()`: Pass A 0-8 recovery, Pass B 10-14
    load, always DONE;
  - `filesystem_saveMidiLearn_tick()`: the safe-write phases 0-10, sharing
    the settings stall observer (code `MlWr`, site 8).
- **Scheduler:**
  - `filesystem_midiLearnWriterSchedule_tick()` runs before the settings
    writer, with the same idle and copy/clear guard, plus
    `!menu_isLoadSaveCommandActive()`, the boot gate
    `fs_settings_runtime_ready` and a READY volume;
  - PENDING is cleared at start;
  - **retry policy:** one retry on error (PENDING|RETRIED), then wait for
    the next Learn.
- **`main.c`:** blocking boot load after the settings load, under the
  boot-logging timeout.
- **Trace stage `m`:**

  | Event | value |
  |-------|-------|
  | 0x01 WRITE | rows, failed, flags after |
  | 0x02 RECOVERY | outcome 1 promoted / 2 discarded / 3 remove failed / 4 rename failed; lines, N, seen |
  | 0x03 LOAD | rows, present, result 0 ok / 1 bad format / 2 read error; lines |

- **`tools/decode_devlogs.py`:** stage `m`, `MLRNLOAD` and the op names.
  **Also fixed** a pre-existing drift: `FS_INTERNAL_OP_PATTERN_TRACE_FLUSH`
  was missing from the op list, so every op from `AUTOSAVE_PATTERN_DRAIN`
  onward decoded as its neighbour. The list now matches the enum (54
  entries, checked by script).

### 9.3 C: global Morph sets FX Morph

**Cause:** `preset_morphScene()` only wrote the stored byte.
- A Pattern `fxm` override (S076 Rule A) or an FX Morph lock still won.
- The `PAR_EFFECT_MORPH` mirror and the `fxm` cell stayed stale.

**Fix:**
- `effects_setMorphAmountFromGlobal(scene, amount)` sets one Scene with no
  fan-out (the caller fans out). It stores the amount, clears the `fxm`
  override and sets `menuHoldMorph` on the active Scene (the `fxm`
  menu-edit rule).
- `preset_morphScene()` calls it and, for the active Scene, also calls
  `preset_syncEffectMorphMirror()`.
- `menu_commitGlobalMorphFromMidi()` also repaints `fxm`.
- **Never recorded**, like the voice Morphs the gesture sets (G-1).

**Safety:** bulk Global apply starts at `PAR_BEGINNING_OF_GLOBALS` (135)
and `PAR_MORPH` is 78, so a settings load never sets the menu hold.

### 9.4 D: SHIFT+REC `pv<` / `pv>`

**Request:**
- New `pv<`/`pv>` set the velocity rhythm of the BAR overlay roll, using
  the same values as the step special (`1-2`, `1-3`, …).
- SHIFT+REC page 2 = `pv< pv> qnt rtk` on one page.

**Design:**
- **Value:** the packed rhythm byte of the step `rtm`: bits 7..4 cycle,
  bit 3 0 halve / 1 double, bits 2..0 full count.
- **List and labels:** the same ordered list `stepRollRhythmBytes[]`
  (`off`, `1-2`..`4-5`, `1+2`..`4+5`), with the same labels and stepping.
- **One rule for both rolls:** `seq_rollRhythmVelocity(rhythm, vel, pos)`
  is the former body of `seq_rollComputeVelocity()` (now a wrapper).
- **Velocity:** overlay roll hit 0 is 100 (`BH_OVERLAY_VEL`); each repeat
  plays the rhythm velocity at its position. `1-2` gives 100, 50, 100…;
  `1+3` gives 100, 127, 127, 100…
- **Scope:**
  - roll types only; single and flam ignore it, and the flam main stays at
    100;
  - panel and MIDI rolls;
  - the rhythm is read live from the active overlay.
- **Recording:** LiveRecord gets the played velocities. The rhythm itself
  is not recorded into steps (the S081 rule "rhythm is never recorded").
- **Persistence:** `settings.cfg` `perf_vel_lt`/`perf_vel_rt` (default 0),
  cases 24/25, `lines=26`. Old 24-line files load with off.

**Code:**
- `PAR_PERF_VEL_LT/RT` are appended after `PAR_TRACK_AUTO_SMOOTH` (no id is
  renumbered).
- Labels `pv<`/`pv>`, Prfrmnc, `Rhythm<`/`Rhythm>`; dtype `DTYPE_0B255`.
- `menu_paramIsRollRhythm()` replaces six `PAR_STEP_RHYTHM` tests:
  two value names, the clamp, the full view, the encoder and the pot.
- `menuPages.h`: RECORDING_PAGE sub-page 1. The old `rtk` sub-page 2 is
  now reserved.
- **Overlay entry:** the field `vel` (always 100) became `pos`, so the
  entry size is unchanged.
  - `pos` is set to 1 after the first roll hit and on single/flam → roll;
    roll → roll keeps it.
  - It wraps 255 → 16 (256 mod 60), keeping the phase for cycles 2..5.

### 9.5 E: CC2 sets the PERF BAR overlay

**Finding:** CC2 was the agreed assignment (§2.3 C8). As built, it kept its
own state in `MidiNotes` (`midiNotes_action`) that only shaped received
notes:
- it never changed `bh_overlayActive`, so the panel VOICE overlay and the
  BAR LEDs did not follow;
- a BAR overlay in PERF overrode it (C-9).

The user confirmed: **CC2 on G only, for both the trigger and the overlay
change.**

**Design: one overlay state.**
- `bh_overlayActive` (0 off, 1 `<`, 2 `>`) is set by the BAR presses in
  PERF and by CC2 on G in any mode. The last action wins.
- Received notes use it in any mode; the panel VOICE buttons in PERF.
- **LEDs:** CC2 updates the BAR LEDs in PERF unless the looper owns them;
  PERF entry and looper disengage restore them from the state.
- **Retired:** C-9, and the F-3 selector.
- **Consequence (accepted):** an overlay turned on with BAR in PERF also
  shapes received MIDI notes after leaving PERF.

**Code:**
- **`buttonHandler`:**
  - added `buttonHandler_overlayActive()` and
    `buttonHandler_setOverlay(which)` (the same change as a BAR press;
    repeats ignored);
  - removed `buttonHandler_overlayHeldBar()`,
    `buttonHandler_overlayMidiActionChanged()` and the selector;
  - `buttonHandler_overlayReconfigure(which)` re-configures every held
    entry.
- **`MidiNotes`:**
  - removed `midiNotes_action` (−1 B) and `midiNotes_noteAction()`;
  - `midiNotes_setNoteAction()` calls `buttonHandler_setOverlay()`.
- **`MidiControl`:** the CC2 case keeps the G-only check, with a comment
  block.

### 9.6 F: comment-block audit

Every change from P2 was checked for a block covering What, Why,
Inputs/Outputs, callers and affiliates. These were added or refreshed:

| Location | Change |
|----------|--------|
| `menu.c`, two call-site comments | No longer say `def`. |
| `midiNoteMap_noteTracksOnChannel()` | Full block. |
| `seq_rollComputeVelocity()` | Notes the delegation to `seq_rollRhythmVelocity()`. |
| `buttonHandler.c` overlay start/reconfigure/tick | Roll position, `pv`, flam velocity. |
| `preset_morph()` / `preset_morphScene()` | Include the Effect Morph. |
| `effects_setMorphAmountScene()` | Caller list. |
| `MidiControl.c` CC2 case | New block. |

Sizes were identical after the audit.

### 9.7 P2 hardware checks (passed: "hardware checks out")

- **A:**
  - page 2 reads `ch nte lop rmp`, with full names "Voice Channel", "Track
    Note" and "Automtn Ramp";
  - `ch`/`nte` follow the viewed track and Scene;
  - `ch off`/`nte off` behave as in §9.1;
  - the values survive reboot (`sceneset.scg` 17/128).
- **B:**
  - the first Learn creates `midilrn.cfg` with no tmp left;
  - Learns survive reboot; re-learn and macros rewrite the file;
  - a busy card writes afterwards; `FUL` writes nothing;
  - `settings.cfg` has no learn rows;
  - tmp promote/discard works, and hand edits load.
- **C:** `mrp` and CC1 on G move FX Morph, also over `fxm` automation until
  its next step. No `fxm`/`Nvm` lane is recorded.
- **D:**
  - page 2 is `pv< pv> qnt rtk`;
  - roll velocities follow the rhythm for panel and MIDI rolls;
  - the values survive reboot.
- **E:**
  - CC2 0/1/2 on G sets the overlay and the LEDs; the last action wins;
  - CC2 on other channels is ignored and forwarded.

---

## 10. Resources (RAM policy record)

All SRAM1, static, runtime lifetime, approved under the RAM Allocation
Approval Policy (2026-10-17, 2026-10-21, 2026-10-22).

| Item | Owner | Bytes |
|------|-------|------:|
| Learn table 128 × 4 B + count/state/target/deadline | MidiLearn | 518 (528 with alignment) |
| Per-track overlay table 7 × 8 B (R-A) | buttonHandler | 56 |
| NRPN RX contexts 2 × 8 × 3 B | MidiControl | 48 |
| Parameter TX: NRPN key cache 2 × 2 B, pending 2 B, last-send 2 B | MidiOut | 8 |
| TX note-off record 7 × 2 B (R-B) | MidiOut | 14 |
| DIN RX buffer: 66 B + 64 × 2 B stamps | MidiDin/MidiBuffer | 194 |
| USB RX ring 128 × 6 B | usb_midi_core | 768 (was 2,048) |
| DIN framer message (R-D) | MidiIn | 6 |
| Resume tick (R-C) | Sequencer | 4 |
| PC origin flag | Sequencer | 1 |
| `midilrn.cfg` request flag | filesystem | 1 |
| CC2 state | MidiNotes | retired (P2 E) |
| Retired | `midiParser_originalCcValues[255]`, `midi_NoteOverride[7]`, `midi_MidiChannels[8]`, `midi_chan_notes[16]` + count, the 9 B overlay slot, parser statics | released; a release does not authorise reuse |

**Totals:**
- Final DEV bss is 430,112 B, against 430,928 B at the S082 close (−816 B).
- The P2 bytes (+1 −1) fit in alignment padding.
- `pv<`/`pv>` use spare `parameter_values[]` ids.
- The overlay `pos` reuses the former `vel` byte.

**Flash:** text 598,120 → 608,744 B. Headroom 144,816 B.

---

## 11. Build results and hardware verification

### 11.1 Final gates

- `make all`, `make img` PASS.
- **Warnings:** only older ones. The 7 `filesystem.c` unused-function
  warnings, plus `PatternData.c:201` packed-member and
  `EuklidGenerator.c:54` sign-compare when those units recompile.
- `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`:
  - ITCM 4,168 / 16,384 B;
  - DTCM statics 4,472 B;
  - FXBUF 126,592 B at 0x20001180, margin 3,712 B.
- `make -C tools/midi_test run` → `PASS (0 failures)`.

### 11.2 Regression list (per-phase matrices; not reported row by row)

Keep these for future MIDI work. The user's testing passed overall.

- **A (receive and routing):**
  - clock slaved to DIN and USB (`snc` din/usb/aut), no drift;
  - MTC start at 0:0:0:0 and timeout stop;
  - the MRT matrix for all 8 values (DIN loopback and a DAW): only the
    routed paths carry traffic, SysEx and SPP arrive intact on USB with the
    right CIN, nothing loops USB → USB;
  - Stop and Scene changes send note-offs;
  - a PC during a Bank load is forwarded, not acted on.
- **B (notes and overlays):**
  - the host test;
  - each note-map example on DIN and USB;
  - a muted track's note is consumed and silent; an unmapped note is
    forwarded;
  - CC2 with each overlay type, with rolls on two or three tracks from
    MIDI and from the panel;
  - LATCH placement of MIDI notes on step boundaries under CPU load (age
    on vs off).
- **C (transport):**
  - Start from the Pattern start; Continue resumes (compare 7- and
    16-step tracks);
  - a DAW locate (SPP) then Continue lands on the bar/beat, with the FX
    sequence aligned;
  - SPP from the wrong source, or with `snc` off, is ignored;
  - one forwarded copy of Start/Continue/Stop;
  - LXR as master sends Start/Stop.
- **D (program and Bank change):**
  - PC on G in every mode, running and stopped, `scd` and `pcr` variants,
    with no echo;
  - PC 16..127 and non-resident Scenes are forwarded;
  - a voice-channel PC moves only those tracks (new take; T6/T7
    together);
  - Scene 17 rules;
  - CC0 bursts (latest wins), dropped during a load, RXF P off;
  - the A1 queue case.
- **E (NRPN and writes):**
  - NRPN sweep 0..510 per type: clamp, Morph translation, unused IDs
    forwarded, the cell follows;
  - R12/R13/Scene-target behaviour against automation;
  - the voice-channel ID rules;
  - fan-out over the edit mask, during an unlocked load, and on 17;
  - recording (track length not recorded);
  - CC1 G-1;
  - CC96/97; RPN null.
- **F (Learn):**
  - the gesture in all modes;
  - re-learn on kind `v`; macros; type matching on Scene 17;
  - shared-channel `v` rows; reserved CCs never capture;
  - each cancel path; `FUL` at 128;
  - hand edits;
  - SHIFT+PLAY never toggles the transport.
- **G (transmit):**
  - a DAW round trip of LXR output plays back identically;
  - no hanging notes with two tracks on one channel;
  - NRPN coalescing and throttle with no clock jitter
    (`midiDin_txDropCount()` 0);
  - no echo, and CC0 only after a successful Menu load;
  - per-track PC on each channel but not G;
  - preview and `def` at the default pitch.
- **Fixes F-1..F-7:**
  - no double step at the Continue point;
  - a DAW and the sequencer on one track leave no hanging notes;
  - CC0 on Load/Save ends Learn;
  - the HiHat pair moves together.

---

## 12. Affected files and API changes

### 12.1 Files

| File | Change |
|------|--------|
| `Core/MIDI/*` | Rebuilt (§7.1); P2: `MidiDefs.h` off constants, `MidiNoteMap` off handling, `MidiLearn` file API, `MidiNotes` no own state, `MidiControl` CC2 comment. |
| `tools/midi_test/notemap_test.c`, `Makefile` | New host test. |
| `Core/Hardware/USB/App/usb_midi_core.c/h`, `usb_manager.c/h` | Ring, stamps, drop-newest, `usb_sendMidiPacket()`. |
| `Core/Sequencer/sequencer.c/h` | TX via MidiOut; `seq_setRunningEx()`, `seq_continueRunning()`, `seq_setResumeTick()`, `seq_setSceneChangeOrigin()`, `seq_assignTracksToScene()`, `seq_rollRhythmVelocity()`; mute fix; `rmp` comments. |
| `Core/Sequencer/clockSync.c/h`, `sequencerTimer.c`, `LiveRecord.c/h` | `sync_midiContinue()`; TIM3 realtime call; `age_us`. |
| `Core/Hardware/frontPanel/buttonHandler.c/h` | Overlay executor and state, `pv` velocity, Learn gesture/LEDs, per-track PC TX, remnant removal. |
| `Core/Menu/menu.c/h`, `menuPages.h`, `MenuText.h` | Commit path, MIDI cells, Learn UI, origin Scene change, A1, C-5, TX hook, track lists, `rmp`, `pv`, SHIFT+REC page. |
| `Core/Bank/Scene/SceneData.c/h` | Track channel/note domains. |
| `Core/Bank/Scene/Preset/presetManager.c/h` | Legacy MIDI setters removed; global Morph → FX Morph. |
| `Core/Bank/Scene/Preset/ParameterArray.h` | `PAR_PERF_VEL_LT/RT`; retired MIDI ids as no-ops. |
| `Core/Bank/Scene/Pattern/PatternData.c/h` | `rmp` comments. |
| `Core/DSP/Effects/EffectsManager.c/h` | `effects_setMorphAmountFromGlobal()`. |
| `Core/Hardware/SD/filesystem.c/h`, `storageTypes.c/h` | Settings keys and `lines=26`; `midilrn.cfg`; sceneset domains. |
| `Core/Bank/Scene/AutosaveTrace.h`, `tools/decode_devlogs.py` | Stage `m`; op list. |
| `Core/DSPAudio/Oscillator.c`, `valueShaper.h` (moved), `SomGenerator.c`, `InstrumentManager.c`, `lfo.c`, `StereoFilterEffect.c` | Includes, detune helper. |
| `main.c`, `config.h`, `Makefile` | Init/service, MIDI config block, sources, Learn boot load. |

### 12.2 API added, renamed or removed (outside `Core/MIDI/`)

- **Menu:**
  - `menu_commitTargetEdit()`, `menu_commitGlobalMorphFromMidi()`,
    `menu_notifyTargetChanged()`, `menu_perfModeSceneChangeFrom()`,
    `menu_learnDisplayChanged()`;
  - static: `menu_cellCommitValueEx()`, `menu_cellMidiTarget()`,
    `menu_targetVisible()`, `menu_paramIsRollRhythm()`, the track
    conversions.
- **Sequencer:** `seq_setRunningEx()`, `seq_continueRunning()`,
  `seq_setResumeTick()`, `seq_assignTracksToScene()`,
  `seq_setSceneChangeOrigin()`, `seq_rollRhythmVelocity()`;
  `sync_midiContinue()`.
- **buttonHandler:** `buttonHandler_overlayActive()`,
  `buttonHandler_setOverlay()`, `buttonHandler_overlayMidiNoteOn/Off()`,
  `buttonHandler_setLearnLeds()`.
- **Effects:** `effects_setMorphAmountFromGlobal()`.
- **Filesystem:** `filesystem_requestMidiLearnWrite()`,
  `filesystem_requestLoadMidiLearn()`.
- **USB:** `usb_sendMidiPacket()`.
- **Removed:** `preset_applySoundParameter()`,
  `preset_setInstrumentParameterFromMidi()`, `seq_setNextPattern()`,
  `seq_midiNoteOff()`, `seq_sendMidiNoteOn()`, `usb_sendMidi()`, every
  `midiParser_*`, `uart_*` and FIFO function, `menu_clampInstrumentValue()`,
  `buttonHandler_getArmedAutomationStep()`,
  `buttonHandler_overlayHeldBar()`,
  `buttonHandler_overlayMidiActionChanged()`, `midiNotes_noteAction()`.

---

## 13. Accepted behaviours and known limits (not bugs)

- RXF/TXF/MRT default to off on a card without the keys (§8.4).
- SHIFT+PLAY no longer toggles the transport in any mode.
- A MIDI flam ignores its note-off. MIDI-held rolls are not looper holds.
- R9: a hanging note downstream can follow a mid-note map change.
- **Shared overlay state:** a BAR overlay set in PERF also applies to MIDI
  notes in other modes (one state).
- `pv` rhythms are not recorded into steps.
- **Learn write retries:** one automatic retry, then the next Learn re-arms
  it. A failing card leaves `midilrn.cfg` stale (trace `m` WRITE and `E`
  records show it).
- **NRPN numbers** are instrument-agnostic: one NRPN chart per type is
  still to be published.
- **Panel TX:** early-return edit paths (LFO/velocity target cells, roll
  rhythm, held-step overlays) send no parameter TX.

---

## 14. Specification reference updates (S083 close)

| Document | Update |
|----------|--------|
| `MIDI_SPEC.md` | Rewritten as the complete use and code reference: behaviour (§1-§11), architecture and message flow, every module's API and internals, integration points, how to extend, RAM, binding rules, decisions and their history, test tools. The planning-document references are removed. |
| `MODULE_INTERCHANGE_SPEC.md` | Core/MIDI section, overlay entry points, SEQ/RECORDING page rows, settings keys. |
| `FILESYSTEM_SPEC.md` | `settings.cfg` 26 lines, `midilrn.cfg` section, `sceneset.scg` rows 4/5. |
| `PATTERN_DYNAMIC_STACK.md` | §6.2a MIDI = menu edit; Continue/SPP resume; MIDI arrival age; `rmp`. |
| `BANK_PRESET_ARCHITECTURE.md` | MIDI writes through the edit mask; global Morph sets FX Morph. |
| `DEV_MODES.md` | Stage `m`, `MlWr`. |
| `STORAGE_SRAM_MANIFEST.md` | S083 measured rows. |

---

## 15. Carry-forward

1. **State:** all S083 code is in `bbea23f` on `dev-ph6-midi`; the final
   close-out document edits came after it.
2. **Delete** the root documents `S083_P1_MIDI_IMPLEMENTATION_PLAN.md`,
   `S083_P1_IMPLEMENTATION.md` and `S083_P2_MIDI_TRACK_FIXUPS.md` (user's
   plan); this log and `MIDI_SPEC.md` carry them.
3. **`SCOPING_TARGETS.md`:** Phase 6 §6.1 and A20 are done. Pick the next
   Phase 6 item.
4. **Optional product decisions:**
   - RXF/TXF defaults (off today);
   - whether a BAR overlay set in PERF should apply to MIDI notes outside
     PERF;
   - whether recorded overlay rolls should carry the `pv` rhythm.
5. **Publish** a per-instrument-type and per-Effect-type NRPN chart (R7).
6. **Open since S082:** F5 (Scene 2 Case-3 invalidation, instrumented); A1
   is closed by this session; A2 (frozen-screen blink loss) is
   low-severity.
