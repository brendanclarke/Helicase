# S106 P1 — Generators (plan, revision 5)

**Date:** 2026-11-28. **Branch:** `dev-ph7-alkaline` (clean at `1d2e42ca`).
**Status:** plan, all questions answered. No source was changed.

**Revision 5** folds in the remaining follow-ups (2026-11-28):
- **FQ3:** a commit clears every special except those the generator writes, and keeps or
  blanks automation by `ato`. A future generator may declare pass-through specials; the arp
  has none.
- **FQ5:** `ato` is the automation switch whenever the type is not `off`, with no exception for
  pass-through. Revision 3's carve-out (A15) is removed.
- **FQ2, FQ4, FQ7, FQ9–FQ12:** as recommended.

§14 is now a table of the decisions.

**Revision 4** folded in your answers to FQ1, FQ6 and FQ8 (2026-11-28):
- **FQ1:** a commit sets the track's generator to `off` (§5.5).
- **FQ6:** `leg` applies as written: there is never an attack-decay restart on that step. The
  recording rule guarantees `leg` is written only while the Colloid voice is in attack, decay
  or sustain, never in release (G40, §7.3).
- **FQ8:** no recording writes `sus`, of any kind. The paraphonic `col_sustain` automation and
  the release mechanism stay as they are. The `sus` special exists, settable from the menu,
  with one rule: a note may not enter release during a step that has it (G36). It therefore
  only matters while Colloid's `col_sustain` row is `off`.

The other follow-ups were answered in revision 5 (§14).

**Revision 3** folded in your answers to Q1–Q18 (2026-11-28). The changes:
- the arp wraps at the track length (Q1);
- the Scene target namespace is corrected (Q2, §1.8);
- `ato` off means none of the track's automation is read (Q3);
- your lane packing (Q4);
- field-wise Morph and random rolled at build (Q5);
- defaults (Q6);
- view, record and the new **commit** cell (Q7, §5.5);
- MIDI: no `mid` parameter, and the arp takes every note on the track (Q8);
- fan-out and copy/clear (Q9, Q10);
- velocity patterns (Q11);
- the switch-time build (Q12);
- RAM approved (Q13);
- legato and sustain defined (Q14, §7);
- the underline rule (Q15);
- held-step editing (Q16), MIDI Learn (Q17) and the layout (Q18).

Revision 3 put the follow-up questions in §14; they are all answered now (revisions 4–5).

**On Q2:** revision 2's option (B), "own-track" targets, was wrong and is withdrawn. The
Scene targets are one Scene-wide namespace of 64 IDs. An automation entry on any track's step
may address any of them, and Pattern storage has no per-track namespace. 22 IDs are free
(64 − 42 rows, counting the retired `srt` row 6 as used; 23 if `srt` is reused). Removing the
7 shuffle rows gives 29, and the generators take 28 (4 lanes × 7 tracks) — 1 spare (63 rows),
or 2 if `srt` is reused. Revision 2's option (A) already had this layout, but its wording
implied an ownership that does not exist; §1.8 and §4.6 now say it plainly.

**On the S099 legato questions:** they cannot be recovered. They were asked in chat during
S099 P2 §11 and never written into the tree. The deleted `S099_P2_ADV_FIXES.md` (`a0ebcf08`)
and the S099 handoff only refer to them. None of the eight local transcripts of this project
contains them; the S099 session's transcript is not on this machine. Your Q14 answer now
defines legato and sustain (§7), with FQ6–FQ8 (§14).

**Your requests (2026-11-26/28):**

| § | Item |
|---|---|
| 3–5 | The Generator page (`knowledge_files/drafts/GENERATOR_PAGE_SPEC_DRAFT.md`) as the SHIFT+MODE STEP menu, re-based on your generator rules |
| 6 | The first type: the Arpeggiator |
| 7 | Roll special values `leg`, `sus`, `l+s`, and what they do on Colloid |

**Labels:**
- **Code:** read from the tree, with a file and line (clean tree at `1d2e42ca`).
- **Measured:** a number taken from the tree (the method is given).
- **Est.:** an estimate with its basis. The gate named beside it measures it.

**Binding rules that apply** (`MEMORY.md`):
- RAM: approved (Q13, §8); any growth beyond §8 comes back for sign-off;
- no backward compatibility (the AutoSave layout and the Scene target IDs move);
- TIM3 order and the step-reset rule (S081 §5.8) do not change; timing is read only from
  `seq_effectiveTrack*[]`; the Pattern region is never written from Morph or automation
  (S078);
- automation priority: step auto → menu edit; a MIDI write is a menu edit;
- Scene switch = per-track identity (S098 P1);
- MIDI: never cache a Scene index; consume-then-route; the note mode is resolved per message;
- copy/clear holds the raw block API exclusively;
- no 1 ms blocking in the main loop;
- the CPU ceiling (S089 P2): no per-sample DSP cost changes. The legato path on Colloid
  skips work; it adds none (§7.2);
- no step edits `MEMORY.md` or `knowledge_files/`. §11 lists the statements made out of date.

---

## 0. Summary

- **Where:** SHIFT+MODE STEP opens the **Generator page** (select mode `0x06`, page id
  `SOM_PAGE` renamed `GENERATOR_PAGE`). The press does nothing today.
- **What it edits:** one generator per track, a Scene parameter.
  - Row 0 is `gen`, `off` by default, with no other rows.
  - Clicking into `gen` opens a browse view showing the long and short names; it commits
    on click-out.
  - A type other than `off` adds its field cells, `ato`, and a last **commit** cell
    (`cmt`, `OK`).
- **How it plays:** a non-`off` track reads its trigger bits and four specials from one
  device-wide **generator store** (3,696 B), not from its Pattern. The store is rebuilt in
  the foreground when:
  - the track's played Scene changes;
  - the track's Pattern changes;
  - an effective generator lane changes;
  - for the arp, the effective track length changes.

  A track not rebuilt since a switch does not fire. A track rebuilt after a parameter change
  may play stale until the rebuild lands.
- **Automation:** 28 Scene-wide targets `1g1`..`7g4` (4 lanes per track's generator) replace
  the 7 shuffle targets. Each lane is a 7-bit byte of packed fields. Morph is field-wise.
  - `ato` off on a non-`off` generator: none of that track's Pattern automation is read.
  - The automation underline shows in every mode, whatever `ato` is.
- **View and record:**
  - Generator mode's SEQ row shows the generator output; every other mode shows the
    Pattern.
  - Live record writes the Pattern only. Recorded automation, including the generator lanes,
    goes to the normal destination track (`rtk`).
- **Commit (`cmt` → `OK`):** rewrites the track's Pattern from the generator's current build:
  - the last random roll is written as fixed data, unless the type says otherwise;
  - every special the generator does not write is cleared (a later type may pass some
    through);
  - the track's automation is kept when `ato` is on and blanked when it is off;
  - the track's generator is then set to `off`.

  It runs as a copy/clear job and is refused whole if it cannot complete.
- **Arpeggiator (§6):**
  - **Input:** each active Pattern step's note, velocity, condition and roll.
  - **Output:** note, velocity, length (`fil`) and condition for `stp` steps, wrapping at the
    track length. The first note keeps the input's condition, and later notes get `lst`.
  - **Packing:** `in1`+`oct`+1 bit of `in3` / `in2`+3 bits of `in3` / `nln`+`dir`+1 bit of
    `vel` / `stp`+3 bits of `vel`. An interval change is at most two automation events.
  - **MIDI:** the arp takes every MIDI note on its track (the track's own MIDI settings switch
    it) and plays one note at a time on the voice, Colloid included.
- **Legato and sustain (§7):**
  - `leg`, `sus` and `l+s` are roll values 10..12, settable from the menu on any track.
  - Only Colloid acts on them. `sus`: no release on that step (it only matters while
    `col_sustain` is `off`). `leg`: no restart of attack-decay on that step.
  - Live record writes `leg` only, on Colloid tracks only, and only while the voice is not in
    release. No recording ever writes `sus`.
  - The arp reads them as input flags (window restart, sequence kept).
- **Defect found (F1, §10):** `seq_scene_automation_dirty` is 32-bit, but indices already
  reach 41.
- **RAM:** ≈ 5,483 B SRAM1, approved (§8). Revision 3 adds nothing.

---

## 1. What exists (code)

### 1.1 The SHIFT+MODE STEP press and select mode 0x06

- `buttonHandler.h:18–25`: modes VOICE 0, PERF 1, STEP 2, LOAD_SAVE 3, FX 5, MENU 7.
  `:24` says "0x06 (SHIFT+MODE STEP) was the SOM generator; unused since S097".
- `buttonHandler.c:2799–2808`: "SHIFT+MODE STEP has no action (S097)". The press returns
  early. Without that return, the shifted arithmetic `(mode + 4) & 7` (`:2815`, `:2867`)
  would give 6.
- The mode switch at `buttonHandler.c:2874` has no `case 0x06`.
- `ledHandler.c:1243`: `case 6: break;`. Mode 5 blinks MODE2 (`:1242`).
- `menu.h:87`, `:94`: `EUKLID_PAGE` and `SOM_PAGE` remain. `SOM_PAGE` is kept only so
  `EFFECT_PAGE` keeps its value.

### 1.2 Generators today, and how the draft has aged

| Draft statement | Tree today |
|---|---|
| SOM on select mode 0x06 | Archived in S097 (`archive/som/`). Mode 0x06 is free. |
| Euklid on select mode 5 | Mode 5 is the Effect page. Euklid is compiled out (`config.h:285–294`) and still bakes into the Pattern (`EuklidGenerator.c:256`). Not touched by this plan. |
| No free select-mode slot | Codes 4 and 6 are free. |
| Six const sub-tables, one per type | The registry-driven Effect page owner (`menuEffects.c`) is the better model (§5). |
| Off / Overdub / Replace | Superseded: a type other than `off` replaces the Pattern for its track. |
| Arpeggiator needs a scale table | Not needed: intervals are semitones above the step's own note. |

### 1.3 Pattern storage and the specials

- `PatternData.h:16–20`: 7 tracks × 128 steps. `PATTERN_TRACK_BYTES` = 16.
- `PatternData.h:56–63`: seven specials, bits 0..6: note, velocity, condition, roll, timing,
  rhythm, fill. Bit 7 is reserved.
- `PatternData.c:1239–1246`: `pat_readStepSpecials()` defaults are note 63
  (`PAT_DEFAULT_NOTE`, `config.h:426`), velocity 100 (`:427`), condition 0, roll 0,
  timing 0, rhythm 0, fill 100. Playback never tests the presence flags (only
  `copyOps.c:581–583` and the automation byte count at `sequencer.c:3044–3058` do). So a
  generator stores four plain bytes per step, and a special it does not store takes the
  default.
- "Length" is `fil`. A plain trigger is `seq_triggerVoice(track, sp.velocity, sp.note, 1)`
  then `seq_lenStart(track, sp.fill)` (`sequencer.c:3966–3967`). Fill < 100 opens a release
  window (`:3397`, S096 P3); 100 = the engine's one step.
- `PatternData.c:99–103`: `pat_markSceneDirty()` is the single Pattern mutation funnel. Every
  Pattern writer reaches it, including the raw pool paths (list in revision 2 §1.3). It is
  also reached from TIM3 (live erase).

### 1.4 The sequencer's reads

`seq_advanceTrackStep()` (`sequencer.c:3504`) reads through PatternData on
`seq_perTrackPattern[track]`:

| Line | Read | Purpose |
|---|---|---|
| 3666–3672 | address entry, `pat_readStepSpecials()` | muted self-mute condition |
| 3715–3717 | specials, `pat_isStepActive()` | muted recording track's fired bit |
| 3729 | `pat_readStepSpecials()` | the step's specials |
| 3744, 3765, 3830 | `pat_isStepActive()` | fired bit, roll cut, trigger |
| 3845, 3851 | `pat_setStepActive(..., 0)` | live erase (TIM3 write) |
| 3963–3967 | `sp.roll > 0u` → `seq_rollBegin()`, else plain trigger | trigger |
| 3004–3060, 4012 | `seq_queueStepAutomations()`, `pat_stepAutomationCount()` | automation |
| 4734–4749 | count / specials / active of step b | ramp pairing |
| 6748, 6754 | `pat_readStepSpecials()` | deferred fire |

### 1.5 Conditions: "previous" is `lst`

- `MenuText.h:521–551`: value 26 is `lst`, 27 is `!ls`.
- `sequencer.c:3235–3240`: `lst` returns `seq_trackLastConditionFired[track]`, and the
  evaluator writes its result back (`:3256–3257`). A chain of `lst` steps after a conditional
  base step therefore fires exactly when the base did.
- Reset on a Scene change (`:2608`), a per-track assignment (`:2301–2308`) and a transport
  restart (`:7872`).

### 1.6 Roll specials

- `MenuText.h:562–565`: `stepRollRateNames`, 10 entries. `sequencer.c:3293–3301`:
  `seq_rollPpq()`, table[10], 0 for rate > 9. `menu.c:7083`: STEP roll clamp 0..9.
- **`leg` does not exist.** S099 P2 §11 proposed it; its questions are lost (see the header).
- **Hazard (code):** a stored roll ≥ 10 goes through `sp.roll > 0u` (`sequencer.c:3963`) into
  `seq_rollBegin()`, which returns at `ppq == 0` without firing (`:3442–3446`). The step is
  silent.
- Roll rhythm (the velocity-pattern model): `stepRollRhythmBytes[]` (`menu.c:13280–13286`),
  decoded by `seq_rollRhythmVelocity()` (`sequencer.h:230`).

### 1.7 MIDI notes to a track

- `midiNotes_handle()` (`MidiNotes.c:125`) resolves on the active Scene through
  `midiNoteMap_resolve()` to a track and an internal note.
  - Note-offs go to `midiNoteMode_noteOff()`.
  - A muted track consumes the note.
  - The BAR overlay takes note-ons first; then `midiNoteMode_noteOn()`.
- `MidiNoteModes` (`.h:1–24`; `midiNoteMode_trackMode()` at `.c:149–163`): `MNM_LAYER4`
  (Tetrakt), `MNM_PARA` (Colloid on its own channel, overlay off: press-order held keys,
  earliest = fundamental, later keys = `in1..in3`), else `MNM_NORMAL`. The group pool holds
  held keys in press order (≈ 2,380 B, sized for six instances).
- Track MIDI on/off is the track's own `ch`/`nte` (Scene `midi_channel`/`midi_note`; `nte`
  off/on/notes, `MEMORY.md` §MIDI). Revision 2's `mid` is therefore not needed (Q8).

### 1.8 Scene targets: one Scene-wide namespace

- **One namespace.** Instrument descriptors are IDs 0..383. The Scene targets are
  `SCENE_MOD_TARGET_BASE` (= `INSTRUMENT_VOICE_ID_COUNT` 384) + table index
  (`SceneModTargets.c:5–9`). Effect targets are block 7, from 448. 0x1FF is the Pattern off
  sentinel.
  - A step-automation entry is `{target, value}` with a 9-bit target and a 7-bit value
    (`PatternData.h:259–271`).
  - Any entry on any track's step may name any of these IDs. Track targets such as `3ln`
    name *which* track they act on through the descriptor's `voice_slot` field, not through
    the track that stores them.
- **Rows:** `SceneModTargets.c:19–165` has 42 rows:
  - 0..5 voice Morph;
  - 6 retired `srt` (its ID is kept so later IDs don't move);
  - 7 `7dc`;
  - 8..13 audio out;
  - 14..19 FX send;
  - 20 `fxm`;
  - 21..27 length;
  - 28..34 scale;
  - 35..41 shuffle (`:150–162`).
- **Capacity:** `:167` asserts `SCENE_MOD_TARGET_COUNT <= 64`, so **22 are free** (23 if the
  `srt` row were reused). Shuffle's 7 make 29.
- Track targets carry `SCENE_MOD_TARGET_USE_AUTOMATION` only and route through
  `seq_applySceneAutomation()` (`sequencer.c:4090`, `:4134–4176`).
- Per-track Morph endpoints are `scene_settings_t.track_morph_*[7]`, interpolated at the
  slot's resolved Morph amount (track 7 on slot 6) by `presetMorph_getTrackEffective*()`
  (`presetMorphEngine.h:206–228`).
- Users of the track kinds:
  - `menu.c:2564`, `:4368`, `:6101`;
  - `MidiControl.c:134–136`, `:256–264`;
  - `LiveRecord.c:667`.
- Users of the target index:
  - `copyOps.c:399`;
  - `menu.c:4398`, `:5605`, `:5823`, `:15068`;
  - `MidiLearn.c:115`, `:639`;
  - `InstrumentManager.c:1740`;
  - `sequencer.c:4100`.

### 1.9 Scene switch paths

| Path | Code |
|---|---|
| Whole-Scene change (direct, deferred, queued, chain) | foreground `menu_perfSceneChangeExecute()` (`menu.c:10635`) → `seq_selectActivePattern()` (`sequencer.c:2616`); deferred/queued run from `menu_serviceDeferredSceneChange()` (`menu.c:10805`); keep mask `preset_sceneSwitchKeepMask()` (`presetManager.c:3776`) |
| Per-track assignment | `seq_setTrackPlayedScene()` (`sequencer.c:2269`; tracks 5/6 together) |
| Align / clear overrides | `seq_alignActivePatternToScene()` (`:2811`), `seq_clearPerTrackOverrides()` (`:2354`) |
| Scene 17 | `seq_switchToBackgroundScene()` (`:2924`), return at `:2986` |
| Loads, copy/clear, Bank Load | reach `pat_markSceneDirty()` |

### 1.10 Persistence

- `scene_settings_t` is 75 B (`presetManager.c:3564`), × 17 records.
- AutoSave Scene cells: 118 allocated, 76 live (`Autosave.h:190–191`), 42 free.
- Effect region 512 B at 128, 419 live (source at 430..431); Scene section 1,920 B.
  `AUTOSAVE_HEADER_FORMAT_VERSION 3u` (`:48`).
- `sceneset.scg` keys are text and additive. Keep-mask fields:
  `PRESET_TRACK_SETTINGS_FIELDS(X)` (`presetManager.c:3554–3556`).

### 1.11 The Effect page as the model

- `menuEffects.c` owns its cells behind `MENU_CELL_EFFECT` (`menu.c:2197–2207`).
- `typ`: click-in candidate (`menuEffects_editModeChanged()`, `:719`), browse without wrap
  (`:749`), view row 0 "Effect"/"Type", row 1 the 8-character name (`:644`).
- The click-to-confirm idiom is Load/Save's `OK` with the arrow (`menu.c:14352–14364`).

### 1.12 Colloid's gate and envelopes (the only engine that acts on `leg`/`sus`)

- `Colloid_triggerVoice()` (`ColloidVoice.c:1258`):
  - every trigger restarts the amp, filter and pitch attacks from their current levels
    (`:1282–1283`, `:1303`);
  - a first hit sets `gateLeft` to the track's step length (`:1299–1300`);
  - it clears `susBreak` and `changeArm` (`:1301–1302`).
- Gate state (`ColloidVoice.h:439–481`):
  - `gateLeft` counts down per block;
  - `susBreak` blocks the `sus` row after a forced release;
  - `midiHold` lets `sus` hold while stopped;
  - `changeArm`/`susSeen` decide release on a Scene/track change.
- The `sus` row (`col_sustain`, `ColloidParameters.c:289–292`) holds at the sustain level
  while on and the transport runs. A step lock or automation turns it off to end a note.
- Release requests are per slot: `instrumentManager_gateRelease[]` (`InstrumentManager.c:399–415`).
  TIM3 writes 1 (window end) or 2 (forced). Only a Colloid slot acts on it; other types clear
  it unread. `Colloid_gateRenew()` keeps the gate for one step (a roll carry).
- Trigger funnel: `instrumentManager_triggerTrack()` → `Colloid_triggerVoice(..., reset)`
  (`InstrumentManager.c:3405–3419`).
- The pending trigger is `{voice, note, vel, layers}` (`voiceControl_enqueueTriggerLocked()`,
  `MidiVoiceControl.c:331`). `layers` is meaningful only for Tetrakt
  (`voiceControl_noteOnLayers()`, `:394`).

### 1.13 Live record hooks

- Hits commit through `liveRec_commitHit()` (`LiveRecord.c:1208`) with a `roll` argument into
  `liveRec_writeStep()`.
- A hit's own automation is written by `liveRec_writeHitAutomation()` (`:1171`). Paraphonic
  notes record `sus`, intervals and `coa` there (`:1159–1169`).
- Recorded automation goes to the recording track `rtk` (`PAR_AUTOM_TRACK`, `menu.c:7028–7031`).

### 1.14 MIDI Learn rows

- `midiLearn_entry_t` (`MidiLearn.c:43–48`) is `{ccKind, space, type, index}`. A Scene target
  row has space 6, index = the Scene-local index, and type `0xFF` (`MIDI_LEARN_TYPE_NONE`,
  `:53`), which is unused for Scene rows.

### 1.15 Copy/clear jobs

- `cc_job_t` (`copyClearService.h:40–47`, 6 B); classes `CC_JOB_PASTE 0x10`,
  `CC_JOB_CLEAR 0x20`.
- Executors return DONE/WAIT/DROP per tick (`:50–52`) with a `cc_run_t` cursor (`:60–65`).
- The service holds the raw block API exclusively (`MEMORY.md` §Storage).

---

## 2. Behaviour (your rules)

**Generators**

- **G1** One generator type per track; the default is `off`, which does nothing.
- **G2** A non-`off` track reads trigger bits and specials from the generator store.
- **G3** `ato` on/off on every type. Whenever the type is not `off`, `ato` on reads the track's
  Pattern automation and `ato` off does not (Q3, FQ5). This holds while the arp passes
  through as well.
- **G4** Type and parameters are Scene parameters. The type is editable only from the menu.
- **G5** Up to four 7-bit automatable parameters (lanes) per track, bit-packed; step-
  automatable and morphable; not LFO.
- **G6** Any number of non-automatable parameters may follow.
- **G7** Shuffle becomes non-automatable. Its 7 targets plus the free targets give the 28
  generator lanes, Scene-wide.
- **G8** Store: 112 B trigger bits + 3,584 B specials, once for the device, rebuilt per
  switch.
- **G9** Rebuild on a Scene or track switch, a Pattern edit or record, or a parameter change.
- **G10** After a switch, a non-`off` track does not fire until rebuilt.
- **G11** After a parameter change it may play stale until rebuilt.
- **G12** SHIFT+MODE STEP; row 0 `gen`, `off`, nothing else.
- **G13** Clicking into `gen` shows the long and short names, as the Effect type does.
- **G29** Generator mode's SEQ row shows the output. Every other mode shows the Pattern (Q7).
- **G30** Live record writes only the Pattern. Recorded generator-lane automation goes to the
  normal destination track (Q7).
- **G31** The last cell is a commit (`cmt` → `OK`). It rewrites the track from the generator,
  writing the last roll of anything random as fixed data unless the type says otherwise. It
  keeps or blanks automation by `ato` (Q7).
- **G32** The automation underline shows in Generator mode and every other mode, whatever
  `ato` is. The held-step method with the underline is how generator automation is seen
  (Q15).
- **G33** Generator fields are MIDI-Learnable (Q17).

**Arpeggiator**

- **G15** `arp` / `Arpegtr`.
- **G16** Reads active steps, notes and velocities from its own track. It generates steps and
  notes for a set number of steps after an active step.
- **G17** Generates note, velocity, length (`fil`) and condition.
- **G18** The first output step has the input's condition. If the input has one, later notes
  have `lst`.
- **G19** Three intervals from the step's root plus an octave range, among other parameters.
- **G20** MIDI:
  - it takes every MIDI note on its track and plays one note at a time on the voice,
    Colloid included;
  - the lowest held note is the root, and it is active while it or another note is held;
  - other held notes replace the intervals and may widen the octave, within the parameter
    limits (Q8);
  - a polyphonic chord arp is a later idea, not now.
- **G21** Notes play as entered except in `rnd`. Direction is octave direction: `up`
  (default), `dn`, `u-d`, `rnd`.
- **G22** Interval `0` plays the root, and any set interval (including `0`) plays the
  generator's triggers. All three `off` = the arp passes through: the track's normal steps
  play with all their specials. Automation still follows `ato` (G3).
- **G23–G28** Fields: intervals 4 bits each (`off`, `0`..`12`), octave 2 bits, direction
  2 bits, velocity pattern 4 bits, step count 4 bits
  (`1 2 3 4 5 6 7 8 9 12 16 24 32 48 64 128`), note length 4 bits
  (`1 2 3 5 8 12 17 23 29 37 45 54 64 75 87 100`). 28 bits in total. A legato or sustained
  input step restarts the window but not the sequence.
- **G34** Packing (Q4):
  - lane 1 = `in1` + `oct` + 1 bit of `in3`;
  - lane 2 = `in2` + 3 bits of `in3`;
  - lane 3 = `nln` + `dir` + 1 bit of `vel`;
  - lane 4 = `stp` + 3 bits of `vel`.

**Legato and sustain (Q14)**

- **G35** Only Colloid reads `leg`/`sus`/`l+s`. On every other instrument they are not read.
  Live MIDI does not write them on a track whose instrument is not Colloid. The menu can set
  them on any track.
- **G36** `sus` on a step: a note may not enter its release phase during that step. This is
  the only thing `sus` does, so it has an effect only while Colloid's `col_sustain` row is
  `off` (FQ8).
- **G37** `leg` on a step: there can be no restart of the attack-decay phase on that step, as
  written, whatever state the voice is in (FQ6).
- **G38** Neither: the normal rules. An active step starts at attack, and the `col_sustain`
  row's automation applies.
- **G39** No recording of any kind writes `sus` (FQ8). The existing `col_sustain` automation
  (including the paraphonic recording) and the release mechanism are unchanged. `sus` is set
  only from the menu, and exists for later use.
- **G40** `leg` is written only by MIDI or chromatic-mode live record, or explicitly by the
  user. Live record writes it (with the step active) when all of these hold:
  - one or more note-ons arrive on a step while other notes are held;
  - one or more held notes are released on that same step;
  - the Colloid voice's amp envelope is in attack, decay or sustain, never release or idle.

  The last condition guarantees a recorded `leg` never lands on a note in release (FQ6).
- **G42** A commit sets the track's generator to `off` (FQ1).
- **G43** A commit clears every special except the ones the generator writes. Automation is
  kept when `ato` is on and blanked when it is off. A future generator may declare specials it
  passes through from the Pattern; the arp passes none (FQ3).
- **G41** Not in the PERF overlays. Portamento may be integrated later; not now.

---

## 3. Data model

### 3.1 Scene settings (per track, in `scene_settings_t`)

| Field | Bytes | Meaning |
|---|---:|---|
| `gen_type[7]` | 7 | bits 0..6 type id (0 `off`, 1 `arp`); **bit 7 = `ato`** |
| `gen_lane[7][4]` | 28 | the four 7-bit lanes, Normal |
| `gen_lane_morph[7][4]` | 28 | their Morph endpoints |
| `gen_fixed[7][4]` | 28 | non-automatable parameters (the arp uses none; kept for later types, G6) |
| **per Scene** | **91** | `scene_settings_t` 75 → 166 B |

- Setters: `scene_setGenType()`, `scene_setGenAto()`, `scene_setGenLane()`,
  `scene_setGenLaneMorph()`, `scene_setGenFixed()`.
- Each follows the AutoSave extension rule (`SceneData.h:285–298`) and requests a rebuild
  when the Scene is a played Scene of the track.
- G6 allows "more" fixed parameters than four. Four are allocated; a type that needs more
  comes back for sign-off.

### 3.2 The generator store (device-wide, `GeneratorEngine.c`)

```c
static uint8_t gen_trig[NUM_TRACKS][PATTERN_TRACK_BYTES];          /* 112 B   */
static uint8_t gen_spec[NUM_TRACKS][NUM_STEPS][GEN_SPECIAL_SLOTS];  /* 3,584 B */
```

The type's slot map `special_bit[4]` is note, velocity, fill, condition for the arp. The TIM3
reader starts from the `pat_readStepSpecials()` defaults and overwrites only the mapped
fields, so roll, timing and rhythm are 0 on generated steps.

### 3.3 Runtime state (`GeneratorEngine.c`)

| Object | Bytes | Use |
|---|---:|---|
| `gen_readyMask` | 1 | bit t: store valid for the played Scene (TIM3) |
| `gen_passMask` | 1 | bit t: the build falls through to the Pattern (G22; TIM3) |
| `gen_requestMask` | 1 | bit t: rebuild owed |
| `gen_builtScene[7]`, `gen_builtType[7]` | 14 | what the store was built from |
| `gen_builtLane[7][4]` | 28 | the effective lanes it was built from |
| `gen_builtLen[7]` | 7 | the effective track length it was built for |
| `gen_stepOverride[7][4]`, `gen_overrideMask` | 32 | step-automation lane overlays |
| `gen_patSceneDirty` | 4 | bit s: Scene s's Pattern changed |
| `gen_live[7]` | 140 | MIDI live arp per track: sequence[16], count, position, velocity, flags |
| **total** | **228** | |

---

## 4. Architecture

### 4.1 Module and registry

`Core/Bank/Scene/Pattern/GeneratorEngine.c/.h` and `GeneratorArp.c`. They read Patterns only
through `pat_*`. The commit writes through the copy/clear service (§5.5).

```c
typedef struct {
    const char *short3, *full8;      /* "arp", "Arpegtr " */
    uint8_t field_count;             /* arp: 8 */
    const gen_field_desc_t *fields;  /* name3, long8, count, default, names,
                                        up to 2 parts {lane, shift, bits} */
    uint8_t fixed_count;             /* arp: 0 */
    const gen_param_desc_t *fixed;
    uint8_t special_bit[4];          /* slot map */
    /* Specials a commit keeps from the Pattern step (PAT_SPECIAL_*_BIT mask)
     * instead of clearing them (G43). arp: 0. */
    uint8_t passthrough_bits;
    void (*morph)(const uint8_t n[4], const uint8_t m[4], uint8_t amt, uint8_t out[4]);
    uint8_t (*passes)(const uint8_t lane[4]);
    void (*build)(uint8_t track, uint8_t scene, const uint8_t lane[4],
                  const uint8_t fixed[4], uint8_t len,
                  uint8_t trig[16], uint8_t spec[NUM_STEPS][4]);
    /* Commit hook (G31): may rewrite a built step's specials before the
     * Pattern write (e.g. write a condition instead of a diced outcome).
     * NULL = write the build as it stands (arp). */
    void (*commitStep)(uint8_t step, uint8_t spec[4]);
} gen_type_desc_t;
```

The registry order is the storage schema: 0 `off`, 1 `arp`. Append only.

### 4.2 Rebuild requests (G9)

| Trigger | Where |
|---|---|
| Whole-Scene change | `seq_selectActivePattern()`: clear the changed tracks' ready bits in the PRIMASK section and request them; kept tracks keep the store |
| Per-track assignment, align, override clear, Scene 17 | `seq_setTrackPlayedScene()`, `seq_alignActivePatternToScene()`, `seq_clearPerTrackOverrides()`, `seq_switchToBackgroundScene()`: clear ready, request |
| Pattern edit/record | `pat_markSceneDirty()` sets a bit of `gen_patSceneDirty` (32-bit OR under PRIMASK; TIM3 reaches it too); the service requests the tracks that play that Scene; ready is kept (G11) |
| Setting edit | the SceneData setters |
| Effective lane change | the service compares `gen_builtLane` with the effective lanes |
| Effective length change | the service compares `gen_builtLen` with `seq_effectiveTrackLength[t]` (arp wraps, Q1) |
| Type change | the click-out commit of `gen` |

### 4.3 Effective lanes and field-wise Morph (Q5: as recommended)

- Effective lane = the step overlay if active, else `morph()` of Normal against the Morph
  endpoint at the slot's resolved Morph amount (track 7 on slot 6).
- `morph()` unpacks both sides, interpolates each field, and repacks:
  - ordered fields (`oct`, `stp`, `nln`) step by index with rounding;
  - an interval with `off` on one side switches at 50 %, and two set intervals step by
    semitone;
  - `dir` and `vel` switch at 50 %.
- It runs in the foreground only. A rebuild is requested only on a changed effective lane.
- `passes()` runs on the effective lanes and sets `gen_passMask`.

### 4.4 Service and switch-time build (Q12: as recommended)

- `gen_service()` rebuilds at most one track per foreground pass. Arp build Est. < 60 µs,
  measured by the host test cycle count and a DEV trace stage.
- `gen_buildNow(changed_mask)` runs right after `seq_selectActivePattern()` returns, and after
  a per-track assignment. Arp × 7 Est. < 420 µs.
- The store has one copy. The build writes each step's specials before its trigger bit, so at
  most one step tears (G11).

### 4.5 Sequencer read routing (TIM3)

- `seq_stepFires(track, step)` and `seq_stepSpecials(track, step)` replace the direct reads
  of §1.4 on the playback paths.
- A pass track (`gen_passMask`) reads its Pattern as `off` does.
- A non-pass track with ready clear does not fire (G10).
- While `gen_live[t]` is held, the live arp supplies trigger, note, velocity and fill; the
  condition is 0 (§6.6).
- The muted self-mute probe (`:3666`) uses `seq_stepSpecials()`.
- **Automation (G3):** for any non-`off` track with `ato` off, pass-through included, no
  automation of that track's steps is queued (`seq_queueStepAutomations()` and the count at
  `:4012`; ramps pair nothing). That applies to every target, generator lanes included. With
  `ato` on, automation is queued as on an `off` track. Lanes can still be automated from other
  tracks' steps, since the targets are Scene-wide.
- **Roll values 10..12** take the plain-trigger branch (`:3963`), never `seq_rollBegin()`
  (§7).
- **Live erase** (`:3845`, `:3851`) acts on the Pattern, gated by the Pattern's own bit.
- **Cost (Est.):** < 40 cycles per track per step in TIM3. No render cost.

### 4.6 Scene targets (Q2: Scene-wide, as laid out)

- **Remove** the 7 shuffle rows (`SceneModTargets.c:150–162`). **Append** 28 rows of kind
  `SCENE_MOD_TARGET_KIND_GEN_LANE`, `USE_AUTOMATION` only:
  - `voice_slot` = the track whose generator the lane drives, and a new `param` field = the
    lane 0..3, exactly as `3ln` names track 3;
  - order 1g1 1g2 1g3 1g4 2g1 … 7g4, giving 63 rows, IDs 384..446;
  - names `"Track"`, `"1 Gen 1"`, `"1g1"` (Q15: fine).
- Any track's steps may carry them, like every other Scene target. Live record puts recorded
  lane automation on `rtk` (G30).
- **ID reuse:** shuffle IDs 419..425 become 1g1..2g3 (§12 R3).
- `seq_applySceneAutomation()` gains `GEN_LANE`: `gen_setStepOverride(track, lane, value)`
  and its dirty bit (needs F1). `seq_restoreAllSceneAutomation()` clears the overlays and
  requests rebuilds.
- Remove the shuffle cases:
  - `menu.c:2564`, `:4368`, `:6101` (the STEP `shu` cell no longer offers held-step
    automation);
  - `MidiControl.c:136`, `:256–264`;
  - `LiveRecord.c:667`.

  The shuffle Morph endpoint and menu edit remain.

### 4.7 Persistence

- **`sceneset.scg`:** keys `gen_type`, `gen_lane`, `gen_lane_morph`, `gen_fixed`. A missing
  key loads `off`.
- **AutoSave:** move the Scene/Effect boundary inside the fixed 1,920 B section:

  | Constant (`Autosave.h`) | Now | New |
  |---|---:|---:|
  | `AUTOSAVE_SCENE_PARAMETER_ALLOC_BYTES` (`:190`) | 118 | 182 |
  | `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` (`:191`) | 76 | 167 |
  | `AUTOSAVE_EFFECT_OFFSET` (`:192`) | 128 | 192 |
  | `AUTOSAVE_EFFECT_SECTION_BYTES` (`:64`) | 512 | 448 |
  | `AUTOSAVE_EFFECT_PARAMETER_ALLOC_BYTES` | 501 | 437 |
  | `AUTOSAVE_HEADER_FORMAT_VERSION` (`:48`) | 3 | 4 |

  Bases: `GEN_TYPE 76`, `GEN_LANE 83`, `GEN_LANE_MORPH 111`, `GEN_FIXED 139`, `COUNT 167`.
  The record and payload sizes do not change.
- **Pattern (PAT4):** no format change. The commit writes ordinary steps.

### 4.8 Identity, edit mask, copy/clear (Q9, Q10a: as recommended)

- Keep mask: the four `gen_*` fields join `PRESET_TRACK_SETTINGS_FIELDS(X)`
  (`presetManager.c:3554`); the size assert at `:3564` becomes 166.
- Generator edits fan out to the Scenes in the VOICE edit mask, and each fan-out to a played
  Scene requests that track's rebuild.
- Track copy carries the track's generator settings; Scene copy carries all of them.

---

## 5. Menu: the Generator page

### 5.1 Entry and LEDs

- `buttonHandler.c:2799–2808`: the early return becomes entry to `SELECT_MODE_GEN` (0x06).
  `case SELECT_MODE_GEN:` at `:2874` calls `menu_switchPage(GENERATOR_PAGE)`.
- `ledHandler.c:1243`: `case 6:` blinks MODE3.
- `menu.h:94`: `SOM_PAGE` is renamed `GENERATOR_PAGE`.
- The page edits the active track (TRACK buttons select it). The first SHIFT release after
  entry does not toggle the Morph view.

### 5.2 Cells (Q18)

`MENU_CELL_GENERATOR` delegates to `menuGenerator.c`, as `MENU_CELL_EFFECT` does. Type-driven,
4 cells per screen:

| Screen | `off` | `arp` |
|---|---|---|
| SELECT 1, screen 0 | `gen` | `gen` `in1` `in2` `in3` |
| SELECT 1, screen 1 | — | `oct` `dir` `vel` `stp` |
| SELECT 1, screen 2 | — | `nln` `ato` `cmt` |

- **`gen`:** a click-in browse with row 0 "Generatr"/"Type" and row 1 the `full8` name with
  `short3` at column 13. It commits on click-out only if changed, and resets the lanes and
  fixed bytes to the type's defaults (Q6). No automation, Morph or MIDI.
- **Field cells:** an edit rewrites only the field's bits, in both lanes for a split field.
  - A menu edit is R12, as track timing cells are (`menu.c:4390–4399`).
  - A held-step edit writes the field's lane(s) on the held steps of the active track: each
    lane is the held step's stored value (or the effective lane) with the field replaced
    (Q16).
- **Underline (G32):** a field is underlined when any of its lanes has stored automation
  anywhere in the viewed Scene, in every mode and whatever `ato` is. The existing search
  takes the field's lane targets.
- **MIDI Learn (G33):** touching a field while Learn waits records a Scene row for its first
  lane with `type` = the field index (0..7) instead of `0xFF` (`MidiLearn.c:53`). A CC then
  sets that field, scaled to its range, as a menu edit. A row with `type` `0xFF` on a lane
  target is not created (follow-up FQ11).

### 5.3 SHIFT Morph view

Holding SHIFT shows and edits the Morph endpoint lanes, field by field.

### 5.4 SEQ row (G29)

In Generator mode the SEQ row shows the active track's generated triggers for the viewed bar,
with the chase. While SEQ buttons are held it shows the held steps (Q16). Every other mode
shows the Pattern, unchanged.

### 5.5 Commit (`cmt`, G31)

- **Cell:** `cmt`, value `OK` with the Load/Save arrow (`menu.c:14358–14364`). A click starts
  the commit; the cell shows `...` while it runs.
- **Job:** a new copy/clear class `CC_JOB_GEN_COMMIT 0x30` (`copyClearService.h:27–30`), run
  by the service, so it holds the raw block API exclusively and is bounded per tick. Per step
  0..127 of the track on its played Scene:
  1. trigger = the built trigger bit;
  2. specials: the type's four slots (after `commitStep()`, if any) are written. The specials
     in the type's `passthrough_bits` keep the Pattern step's value. Every other special is
     cleared (G43). For the arp, roll, timing and rhythm are cleared, including `leg`/`sus`
     on input steps;
  3. automation: kept as it is when `ato` is on; removed from every step of the track when
     `ato` is off.
- **Fixed random:** the store already holds the last roll (`rnd` direction, `rnd` velocity
  pattern), so the commit writes exactly what is playing. A type that wants a random
  *parameter* written instead provides `commitStep()`. The arp provides none.
- **Preconditions:** the track's ready bit set, not pass, and the viewed Scene equal to the
  track's played Scene. The Pattern pool must hold the rewritten steps, checked up front
  with `pat_rawFreeChunks()`. Otherwise the cell refuses (`--`), and nothing is written
  (FQ2, FQ4).
- **After (G42, FQ1):** the job's last phase sets the track's generator type to `off` through
  `scene_setGenType()` on the same Scene, so the arp does not re-arp its own output. `ato` and
  the lanes are left as they are (they are reset only by a later type change). The Pattern
  rewrite and the type change are AutoSaved like any edit.

---

## 6. The Arpeggiator (`arp`, `Arpegtr`)

### 6.1 Fields

| Cell | Long | Bits | Values | Default |
|---|---|---:|---|---|
| `in1` | Interval1 | 4 | 0 `off`, 1..13 = `0`..`12` (14, 15 clamped) | `off` |
| `in2` | Interval2 | 4 | as `in1` | `off` |
| `in3` | Interval3 | 4 | as `in1` | `off` |
| `oct` | OctRange | 2 | `+0`..`+3` | `+0` |
| `dir` | Directn | 2 | `up` `dn` `u-d` `rnd` | `up` |
| `vel` | VelPattn | 4 | §6.2 | `---` |
| `stp` | ArpSteps | 4 | `1 2 3 4 5 6 7 8 9 12 16 24 32 48 64 128` | `8` |
| `nln` | NoteLen | 4 | `fil` `1 2 3 5 8 12 17 23 29 37 45 54 64 75 87 100` | `100` |
| `ato` | ReadAuto | — | `off`/`on` | `off` |
| `cmt` | Commit | — | `OK` | — |

The defaults are all intervals `off`, so choosing `arp` is inaudible until set (Q6).

### 6.2 Velocity patterns (Q11: as proposed)

Every pattern scales the input velocity v and never exceeds it. k = position in the window.

| Idx | Name | Velocity at k |
|---:|---|---|
| 0 | `---` | v |
| 1..10 | `1-2 1-3 2-3 1-4 2-4 3-4 1-5 2-5 3-5 4-5` | the roll halve patterns (`stepRollRhythmBytes[1..10]`) decoded on k by `seq_rollRhythmVelocity()` |
| 11 | `rup` | 25 % → 100 % across the window |
| 12 | `rdn` | 100 % → 25 % |
| 13 | `pk` | 25 % → 100 % → 25 % |
| 14 | `rnd` | 50..100 %, rolled at build (Q5) |
| 15 | `1st` | v at k = 0, v/2 after |

### 6.3 Packing (Q4)

| Lane | bit 6 | bits 5..4 | bits 3..0 |
|---|---|---|---|
| g1 | `in3` bit 3 | `oct` | `in1` |
| g2 | `in3` bits 2..0 (bits 6..4) | | `in2` |
| g3 | `vel` bit 3 | `dir` | `nln` |
| g4 | `vel` bits 2..0 (bits 6..4) | | `stp` |

- Your layout names *one* bit of `in3` for lane 1 and the other three for lane 2. The plan
  puts `in3`'s top bit in g1 (A14) and `vel`'s top bit in g3, by the same rule.
- All three intervals and the octave are in g1+g2, so a chord change is two automation events.
- Split fields: `in3` spans g1/g2 and `vel` spans g3/g4. A held-step edit writes both lanes;
  automation of one lane alone moves the split field within its half.

### 6.4 Build (store, from the Pattern)

For track t on Scene s, effective lanes, Lt = `seq_effectiveTrackLength[t]`, W = `stp`:

1. **Pass:** if `in1`–`in3` are all `off`, set the pass bit and stop.
2. **Sequence:** I = the set intervals in entered order; R = `oct`. `seq[]` has
   n = |I|×(R+1) entries (at most 12):
   - `up`: octaves 0→R;
   - `dn`: octaves R→0;
   - `u-d`: 0→R then back without repeating the turns (A12);
   - `rnd`: random entries of `up`, rolled at build (Q5).
3. **Input steps** 0..Lt−1: active steps with root r (note special, else 63), velocity v,
   condition c and roll.
4. **Windows:** each input step b opens W steps, (b+k) mod Lt (Q1: wrap), and the window ends
   at the next input step.
5. **Position:** a plain input resets pos to 0. `leg`/`sus`/`l+s` on the input keeps pos
   (G27; the arp reads these on every track, whatever the instrument, FQ9).
6. **Output at k:**
   - trigger;
   - note = clamp(r + seq[pos] + 12·octave, 0..127);
   - velocity = pattern(v, k), at least 1;
   - fill = `nln`;
   - condition = c at k = 0; `lst` for k > 0 when c ≠ 0; else 0.

   Then pos = (pos+1) mod n.
7. **Empty steps:** steps not covered are off; Lt..127 are off.

Input roll rates, micro-timing and rhythm are not carried (Q10b, as recommended).

### 6.5 Conditions on the output

k = 0 carries c, and every later note carries `lst`, which follows the last conditional step's
outcome and writes it back (§1.5). On a generator track nothing conditional lies between the
base and its notes, so the chain is exact.

### 6.6 MIDI (Q8)

- **Mode:** `MNM_ARP` in `midiNoteMode_trackMode()` (`MidiNoteModes.c:149`) whenever the active
  Scene's generator for the track is `arp` and not pass. It takes precedence over `MNM_PARA`
  and `MNM_LAYER4`.
  - The track's own `ch`/`nte` switch MIDI on or off; there is no generator parameter.
  - The BAR overlay still takes note-ons first, and a muted track consumes them.
  - Held notes do not trigger the voice directly. The arp plays one note at a time, on
    Colloid too (no paraphonic chord).
- **Held set:** keys in press order in the existing `MidiNoteModes` group pool. The live arp
  uses internal notes:
  - root = the lowest held;
  - with one key, the `in1..in3` lanes apply;
  - with more, each other key's distance d from the root replaces an interval in press
    order (up to three);
  - d ≤ 12 is the interval;
  - d > 12 is folded to i = d mod 12 (12 for exact octaves) and raises the live octave range
    to ⌈d/12⌉ − 1 if larger, capped at +3 (G20, "as much as the parameter limits allow";
    FQ10).
- **Live arp:** each held-set change rebuilds `gen_live[t]` in the foreground (sequence,
  newest key's velocity, `nln`). The position is kept while keys stay held and reset on the
  first key of a new hold. While held, the window never expires.
- **Playback (TIM3):** the first note plays at the track's next step. Each step fires seq[pos]
  with pattern(v, k) and `nln`. Pattern input steps under a hold are ignored. On release, the
  track returns to the store at its next step.
- **Later, not now:** a polyphonic arp that takes a chord.

---

## 7. Legato and sustain (`leg`, `sus`, `l+s`; G35–G41)

### 7.1 Encoding (step 7, before the arp)

- Roll indices 10 `leg`, 11 `sus`, 12 `l+s`.
- `stepRollRateNames` count 10 → 13; STEP roll clamp (`menu.c:7083`) 9 → 12. Settable from
  the menu on every track.
- `seq_rollPpq()` unchanged (0 for ≥ 10).
- **Trigger branch** (`sequencer.c:3963`): `sp.roll > 0u && sp.roll < 10u` goes to the roll
  engine; 10..12 fire a plain step. On a non-Colloid track that is all they do (G35).
- `perfBtnTypeNames` unchanged (G41).

### 7.2 What Colloid does (step 9)

At the step event in TIM3, on a track whose slot is Colloid (`col` or `clx`):

| Value on the step | Effect |
|---|---|
| `sus` (11) or `l+s` (12) | **no release during this step** (G36): the step's `seq_lenStart()` window is not opened, even with `fil` < 100 (FQ7), a release request already pending for the slot is withdrawn, and the gate is renewed for one step (as `Colloid_gateRenew()` does for a roll carry). This holds on an empty step too. With `col_sustain` `on` the note holds anyway, so the special changes nothing then. Forced releases (mute, a Scene/track change with no sus-on step) are not "during the step" and still act. |
| `leg` (10) or `l+s` (12), step active | **no restart of attack-decay** (G37), as written: the trigger carries a legato flag. `Colloid_triggerVoice()` takes the new note, velocity, gate and LFO retrigger, but leaves the amp, filter and pitch envelope stages as they are, whatever they are. A `leg` set from the menu on a note in release therefore stays in release; recording never produces that case (G40). |
| neither | unchanged (G38): attack from the current level; the `col_sustain` row's automation applies |

- **Flag transport (no new RAM):** the legato flag rides in bit 7 of the pending trigger's
  `layers` byte (§1.12). Today that byte is read only on Tetrakt (mask bits 0..3), and Colloid
  ignores it. `instrumentManager_triggerTrack()` passes `legato` to `Colloid_triggerVoice()`.
- **Cost:** the legato path skips three stage writes, and the `sus` path is one TIM3 test per
  Colloid step. Per-sample DSP is unchanged.

### 7.3 Live record (step 9, Colloid tracks only, G35)

- **`sus` (G39):** never written by any recording. `liveRec_writeHitAutomation()` and the
  paraphonic `col_sustain` recording (`LiveRecord.c:1159–1169`) are unchanged.
- **`leg` (G40):** in `liveRec_commitHit()` (`LiveRecord.c:1208`), for a MIDI or chromatic-mode
  hit only, the hit is recorded (active) with roll `leg` when all of these hold:
  - other keys were held when its note-on arrived;
  - at least one of them was released within the same step;
  - the slot's Colloid amp envelope stage, read at the note-on, is `COL_EG_ATTACK`,
    `COL_EG_DECAY` or `COL_EG_SUSTAIN` (`ColloidVoice.h:261–265`), not `COL_EG_RELEASE` or
    `COL_EG_IDLE`.

  If the step already holds a menu-set `sus`, it becomes `l+s`. If any condition fails, the
  hit records as now (roll 0).
- **Stage read:** a new read-only accessor `instrumentManager_colloidAmpStage(slot)` returns
  `voice->ampEg.stage` (one byte written in the render context, so the foreground read is
  atomic). It returns `COL_EG_IDLE` for a non-Colloid slot. No RAM.
- The "held" and "released within the step" facts come from the `MidiNoteModes` held set and
  the chromatic keyboard. The schedule lists the exact readers.
- On any non-Colloid track, live record never writes 10..12.

---

## 8. RAM (approved 2026-11-28, Q13)

| Row | Owner | Region | Bytes | Object |
|---|---|---|---:|---|
| S | `GeneratorEngine.c` | SRAM1 | 112 | `gen_trig[7][16]` |
| S | `GeneratorEngine.c` | SRAM1 | 3,584 | `gen_spec[7][128][4]` |
| R | `GeneratorEngine.c` | SRAM1 | 228 | runtime state (§3.3) |
| C | `SceneData.c` | SRAM1 | 1,547 | 91 B × 17 `scene_t` |
| F1 | `sequencer.c` | SRAM1 | +4 | `seq_scene_automation_dirty` 32 → 64 bit |
| M | `menuGenerator.c` | SRAM1 | ≈ 8 | browse candidate, edit flag, screen memory |
| | | **Total** | **≈ 5,483 B** | |

- Revision 3 adds no bytes:
  - the commit reuses the copy/clear run state;
  - the legato flag uses a spare trigger bit;
  - `mid` is gone, and the fixed bytes stay for later types.
- Free SRAM1 at HEAD is measured at step 0. If padding takes the total past ≈ 5,483 B, the
  difference comes back to you.

---

## 9. Order and checks

| Step | Work | Gate |
|---|---|---|
| 0 | Baseline: `make all && make img`, `link_budget.py`, `elf_hotloops.py`, inlined set → `tools/dsp_test/baselines/s106p1/` | SRAM1/flash/ITCM recorded |
| 1 | F1: 64-bit Scene automation mask (§10) | host: bit 41 set and restored |
| 2 | Scene targets: shuffle out, 28 `GEN_LANE` rows (§4.6) | assert ≤ 64; picker lists 63 |
| 3 | Scene settings, `sceneset` keys, AutoSave relayout v4, keep mask, copy/clear, fan-out | round trips |
| 4 | `GeneratorEngine` core with `off` only; masks, `pat_markSceneDirty()` hook, service, switch-time build | host `gen_engine`: G9–G11 |
| 5 | Sequencer routing and the `ato` gate (§4.5) | `off` everywhere: TIM3 identical; loop gate 86/86 |
| 6 | Menu: mode 0x06, page, cells, `gen` browse, Morph view, SEQ output view, underline in every mode, field MIDI Learn (§5.1–§5.4) | device: entry, browse, `off` one row |
| 7 | Roll encoding 10..12 (§7.1) | host: 10..12 fire a plain step |
| 8 | Arpeggiator: fields, packing, Morph, pass, build, conditions, velocity patterns, `MNM_ARP`, live arp (§6) | host `gen_arp`: every direction × octave × interval set, window cut and wrap, `leg` position carry, `lst` chain, pass, lane split round trip, live interval folding |
| 9 | Colloid legato/sustain and the live-record `leg` write (§7.2, §7.3) | host: `sus` blocks release only within its step, and is a no-op with `col_sustain` on; `leg` keeps every stage; non-Colloid unchanged; `leg` recorded only with the G40 conditions (refused in release/idle); no recording writes `sus` |
| 10 | Commit job (§5.5) | host: commit equals the playing build; `ato` keep/blank; pool refusal; viewed ≠ played refusal; type is `off` afterwards |

After each step: `make -C tools/dsp_test -k all`. Only the known failures may remain:
`voice_control` 14, `alkaline` AL-OD0, and whatever step 0 lists.

**CPU:** no per-sample change. TIM3 adds < 40 cycles per track per step. The foreground is
bounded. **Flash (Est.):** +9..12 KB against 42,884 B headroom.

---

## 10. Defect found: the Scene automation dirty mask (F1)

- `sequencer.c:1563` `static uint32_t seq_scene_automation_dirty;`, `:4183`
  `|= (1u << index)`, `:1674`/`:1695` clears, `:1773` a `uint32_t` copy.
- Indices reach 41 (42 rows). A shift by 32..41 is undefined, and on Cortex-M7 GCC's register
  `LSL` gives 0. So `5sc`..`7sc` and `1sh`..`7sh` never set their bit, and
  `seq_restoreAllSceneAutomation()` returns at `mask == 0` (`:1777–1778`) without clearing
  their overlays.
- **Fix (step 1):** `uint64_t`, `1ull << index`, `__builtin_ctzll`. The new lanes (to index
  62) depend on it.

---

## 11. Statements made out of date (for you to act on)

- `MEMORY.md` §Sequencer/Timing "Per-track timing (S078)" and `PATTERN_DYNAMIC_STACK.md`
  §6.4: shuffle is no longer step-automatable.
- `MENU_UI_SPEC.md`:
  - SHIFT+MODE STEP opens the Generator page;
  - `SOM_PAGE` is renamed;
  - MODE3 blinks in mode 6;
  - the SEQ row in Generator mode shows the output;
  - the underline shows in every mode;
  - `rol` gains `leg`/`sus`/`l+s`;
  - the `cmt` cell.
- `MIDI_SPEC.md`: `MNM_ARP` and its precedence; field-level MIDI Learn rows (`type` = field).
- `MODULE_INTERCHANGE_SPEC.md`: `GeneratorEngine`/`GeneratorArp`; routed step reads; the
  commit job class.
- `COLLOID_MODULE_REFERENCE.md`: legato and sustain steps; the legato trigger flag; the amp
  stage accessor.
- `PERFORMANCE_RECORD_SPEC.md`: record on a generator track writes the Pattern; the `leg`
  recording rule and its stage guard; `sus` is never recorded.
- `FILESYSTEM_SPEC.md` / `STORAGE_SRAM_MANIFEST.md`: AutoSave boundary 192/448, version 4,
  `sceneset` keys, RAM rows.
- `knowledge_files/drafts/GENERATOR_PAGE_SPEC_DRAFT.md`: superseded.
- `knowledge_files/volatile/S099_WORKING_NOTES.md:25` and the S099 handoff: legato is now
  defined (this plan). The eight S099 questions are lost.

---

## 12. Risks

- **R1 RAM.** Approved at ≈ 5,483 B. Padding in `scene_t` may add bytes, measured at step 3.
- **R2 AutoSave v4.** The first boot rejects the old record, so unsaved live state is lost
  once.
- **R3 Target ID reuse.** IDs 419..425 change from shuffle to 1g1..2g3. Existing shuffle
  automation would write packed arp lanes on tracks 1–2 (no migration). Clear it before use.
- **R4 Tearing.** One store; at most one torn step (G11).
- **R5 Rebuild storms.** An LFO on voice Morph can change lanes every block. This is bounded
  per pass, but the arp may change many times per bar.
- **R6 Interaction surface.** Each of these must be routed or confirmed to read the input:
  - live record ring and fired bit;
  - erase;
  - echo marks;
  - the looper;
  - the `lst` chain (`seq_trackLastConditionFired[]`);
  - ramps;
  - deferred and shuffled fires;
  - note modes;
  - copy/clear;
  - Scene 17.

  Listed in the schedule.
- **R7 menu.c growth.** A third delegated cell kind in a 21,460-line file.
- **R8 `ato` off and `rtk`.** If `rtk` is the arp track and `ato` is off, lane automation
  recorded onto it is not read back. That follows G3 but may surprise. The underline (G32)
  still shows it.
- **R9 Commit capacity.** 128 rewritten steps with four specials can exhaust the Pattern pool
  of a busy Scene. Checked up front; a refusal is all-or-nothing.
- **R10 Legato from release (accepted, FQ6).** Recording never writes `leg` on a note in
  release (G40). A `leg` set from the menu, automated, or replayed on a later pass where the
  previous note has already released, applies as written: the note stays in release and the
  new pitch is nearly silent. This is by design.
- **R11 Stage read timing.** The G40 guard reads the amp stage at the note-on. The render
  context can move the voice into release a block later, before the step plays back. The
  guard covers what is recorded, not every later pass (R10 covers playback).
- **R12 MIDI pools.** `MNM_ARP` uses the group pool sized for six instances; seven arp tracks
  could exceed it, in which case the extra key is ignored.
- **R13 F1 changes behaviour.** `5sc`..`7sc` overlays now restore at a transport reset.

---

## 13. Readings the plan takes (not questions; correct any that are wrong)

- **A1 "8 specials".** There are 7. Read as 4 of the 7.
- **A2 "length (roll length)".** Read as `fil`; on an arp note (a plain step) it is the gate.
- **A4 "scene or track switches".** Read as the track's played Scene changing.
- **A5 "does not fire" vs "stale".** A switch clears ready; an edit keeps it.
- **A8 "as entered".** `in1`, `in2`, `in3` order; MIDI in press order.
- **A9 The root.** Played only where an interval is `0`.
- **A12 `u-d`.** Turn notes not repeated.
- **A13 One MIDI key.** The interval lanes apply.
- **A14 Which `in3` bit is in lane 1.** The plan puts the top bit in g1 (and `vel`'s top bit in
  g3). Either choice gives the same packed total.

---

## 14. Decisions on the follow-up questions (all answered)

| FQ | Decision | Where |
|---|---|---|
| FQ1 | A commit sets the track's generator to `off`. | G42, §5.5 |
| FQ2 | The commit is refused unless the viewed Scene is the track's played Scene and the track is ready and not passing through. | §5.5 |
| FQ3 | A commit clears every special except the ones the generator writes. Automation stays when `ato` is on and is blanked when it is off. A future generator may declare specials it passes through. The arp passes none (roll, timing and rhythm are cleared). | G43, §4.1, §5.5 |
| FQ4 | If the pool cannot hold the rewrite, the commit is refused whole. | §5.5 |
| FQ5 | `ato` is simply the switch: `on` reads the track's automation, `off` does not, whenever the type is not `off`, including while the arp passes through. All intervals `off` plays the track's normal steps with all their specials. Any interval at `0` (root) plays the generator's triggers. | G3, G22, §4.5 |
| FQ6 | `leg` applies as written; recording writes it only outside release. | G37, G40, §7 |
| FQ7 | `sus` wins over `fil` < 100 on the same step: no release during the step. | §7.2 |
| FQ8 | No recording writes `sus`; its only rule is no release during its step. | G36, G39, §7 |
| FQ9 | The arp reads `leg`/`sus`/`l+s` as input flags on every track. | §6.4 |
| FQ10 | Live intervals fold as §6.6 (d mod 12, 12 for exact octaves; octave range raised to ⌈d/12⌉ − 1, capped at +3); with more than three other keys, the first three in press order. | §6.6 |
| FQ11 | A Learn row names the field (`type` = field index); a CC writes the field as a menu edit, both lanes for a split field. A lane is never Learnable as a raw byte. | §5.2 |
| FQ12 | The spare Scene target ID(s) stay free. | §4.6 |

No questions remain open. The plan is ready for an implementation schedule.
