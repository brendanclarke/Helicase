# S104 P1 — Tune-ups (plan, revision 2)

**Date:** 2026-11-25. **Branch:** `dev-ph7-alkaline` (clean at `9c2462bd`).
**Status:** plan. No source was changed.

**Revision 2 (same day)** folds in your answers:
- Q1–Q3: keep `1:-` as it is; the new condition is `!1-`, inserted right after it,
  with no migration;
- Q16: tap tempo moves to COPY;
- Q19: one bar digit;
- Q23/Q24: no new names, and the load seeding rule;
- Q25: A with an interpolation slot, B as the fallback;
- the Tetrakt track-7 rules: T7 automation routes to the longer decays; T6 wins a
  shared layer; simultaneous T6/T7 triggers keep their own decays.

Questions still open are marked **open** in §11.

**Your requests (2026-11-25), one section each:**

| § | Item |
|---|---|
| 1 | Step conditions: `1:-` (first pass only, exists) and the new `!1-` (every pass except the first) |
| 2 | Track-button preview changes the selected menu parameter |
| 3 | Load/Save: pot 1 limited to the page's own types, as the main encoder is (open item M1) |
| 4 | MIDI Omni channel |
| 5 | DJ mode (SHIFT+PLAY+PERF) |
| 6 | Tetrakt on tracks 6 and 7: four separate track-7 decays, automatable by track |
| 7 | Tetrakt looped samples (listed after the one-shots, as on the Drum family) |
| 8 | One-shot LFOs (`knowledge_files/drafts/ENHANCED_FEATURES.md` §LFOs) |

**Labels:**
- **Code:** read from the tree, with a file and line.
- **Measured:** a number taken from the current tree (the method is given).
- **Est.:** an estimate with its basis; the gate named beside it measures it.

**Binding rules that apply** (`MEMORY.md`):
- the CPU ceiling (S089 P2) and per-block accounting (S093): no per-sample cost may
  rise without an equal measured saving; per-block work counts at /32; VDIVs are
  counted separately;
- the two interpolation slots are pre-charged 45 instructions a sample each
  (`CPU_USE_DSP_AUDIT.md` item 75; `INSTRUMENTS_DSP_REFERENCE.md` "The interpolation
  slots"). Slot claims are made in the async, never in the sync render (S093 P8
  item 79);
- Tetrakt's gate: `tools/dsp_test/tetrakt_cpu.py` `GATE = 390`;
- RAM sign-off for any new or enlarged allocation (§9);
- Load/Save: pots keep the full storage lock (S090 P1);
- a MIDI write is a menu edit (Q1, S083); never cache a Scene index in MIDI code;
- automation ramps end at every position break (S081 P2); step reset rules (S081 §5.8);
- no backward compatibility: no migration, no aliasing;
- every code change carries an adjacent comment block. Schedule entry IDs are not
  cited in code.

No step here edits `MEMORY.md` or `knowledge_files/`. §12 lists the statements these
changes make out of date.

---

## 0. Summary

| § | Change | Main files | RAM (SRAM1) | CPU | Open |
|---|---|---|---:|---|---|
| 1 | `!1-` inserted at 21 (21..40 → 22..41, no migration); the 256-pass counter wrap fixed | `sequencer.c`, `MenuText.h`, `menu.c` | 0 | none (TIM3, per step) | — |
| 2 | Re-pressing the selected track in VOICE mode no longer runs `menu_switchPage()` | `buttonHandler.c` | 0 | none | — |
| 3 | Pot 1 goes through the encoder's own type step; ten pot-only helpers are deleted | `menu.c` | 0 (flash shrinks) | none | Q6, Q7 |
| 4 | Global `omn` channel: everything received on it plays the selected track | `MidiIn.c/h`, `MidiNotes.c`, `MidiControl.c`, `MidiProgram.c`, menu, settings | +18 B | none (foreground) | Q8–Q12 |
| 5 | DJ screen in PERF: BPM to 1/100, master position, tap tempo on COPY, ±5 % nudge, bar/step jumps | `buttonHandler.c`, `menu.c`, `sequencer.c/h`, settings | +28 B | none in DSP | Q13–Q15, Q17, Q18, Q20, Q21 |
| 6 | Four Kit track-7 decays (A–D). T7 automation of `dyA..dyD` routes to them by track; T6 wins a shared layer | `SceneData`, `presetManager.c`, `Autosave`, storage, `sequencer.c`, `InstrumentManager.c`, `menu.c`, `LiveRecord.c`, copy/clear, `TetraktVoice` | +106 B | per block only, ≈ +0.1 a sample (est.) | Q24b |
| 7 | Tetrakt rows list `s00..` then `l00..`. A seam block reads exactly with an interpolation slot, else falls back to B | `TetraktVoice.c`, `InstrumentManager.c`, `menu.c` | 0 | no-seam blocks +0; seam with slot ≤ +6 (pool-charged); fallback ≤ +1 (est.) | Q26, Q27 |
| 8 | Ten LFO waves: `xtr` and nine one-shots; offset = start delay on one-shots | `lfo.c/h`, `MenuText.h`, `InstrumentManager.c` | 0 | per block only, ≈ +0.4 a sample system-wide (est.) | Q28–Q31 |

---

## 1. Step conditions `1:-` and `!1-`

### 1.1 What exists (code)

- **The table has 41 conditions, 0..40** (`Core/Menu/MenuText.h:525`):
  - `-`, r10..r95 (1..10), `1:2`..`4:4` (11..19), **`1:-` (20)**;
  - `fil !fl` (21, 22), `lop !lp` (23, 24), `lst !ls` (25, 26);
  - `1mu..7mu` (27..33), `!1m..!7m` (34..40).
- **`1:-`** passes when `repeatCount == 0` (`seq_conditionRepeatPasses()`,
  `Core/Sequencer/sequencer.c:3009`).
- **The repeat counter is per track** (`seq_trackRepeatCount[7]`, `:296`):
  - +1 at each wrap (fwd `:3485`, rev `:3442`, pip `:3450`/`:3457`, rnd `:3468`;
    once-modes never wrap);
  - reset to 0 by a per-track Scene change (`:2165`), the Scene-change reset
    (`:2472`) and transport start (`:7560`).
  - The new condition uses the same counter (Q3).
- **Every hard-coded condition number** (all of them are in `sequencer.c` or the menu;
  the LXR1/LXR-02 converters emit only 0..10 and are unaffected):

  | Where | What |
  |---|---|
  | `seq_conditionEval()` `:3058`–`:3110` | bounds 40; branches ≤ 20, 21/22, 23/24, 25/26, mute `cond − 27`, ≤ 33 |
  | Muted-track self-probe `:3527`–`:3530` | 27..33 / 34..40 |
  | `seq_rampConditionPredict()` `:4476`–`:4483` | bound 40; 11..20; 25; 26 |
  | `menu.c:6962` | clamp 40 |
  | `menu.c:13227` | label bound 40 |
  | Comment blocks | the table's "[1..41] = condition indices 0..40" (`MenuText.h:523`); "condition indices 23/24" (`sequencer.c:313`); the `seq_conditionEval()` header |

- **Existing defect:** the counter is `uint8_t` and wraps 255 → 0.
  - After 256 passes, `1:-` plays again.
  - `1:3` plays twice in a row at the wrap.

### 1.2 Plan

1. **New table, 42 entries** (`MenuText.h`):

   | Values | Conditions |
   |---|---|
   | 0..20 | unchanged |
   | **21** | **`!1-`** |
   | 22, 23 | `fil`, `!fl` |
   | 24, 25 | `lop`, `!lp` |
   | 26, 27 | `lst`, `!ls` |
   | 28..34 | `1mu..7mu` |
   | 35..41 | `!1m..!7m` |

   - **No migration.** A stored 21..40 now reads as the condition one place earlier in
     the new list: a stored `fil` plays as `!1-`, and so on.
2. **Code:**
   - every bound 40 → 41;
   - `seq_conditionRepeatPasses()` handles 11..21: 21 passes when
     `repeatCount != 0`;
   - the repeat branch becomes `cond <= 21u`;
   - fill 22/23, loop 24/25, last 26/27;
   - mute `(cond − 28) % 7`, muted when ≤ 34;
   - the self-probe 28..34 / 35..41;
   - the predictor: 11..21 predictable, last 26/27;
   - every comment block above rewritten to the new numbers.
3. **Wrap fix** (Q4, kept as recommended: no answer): `seq_repeatCountNext(c)`
   returns `c + 1`, except that 255 goes to 4.
   - 256 ≡ 4 (mod 12), so `% 2`, `% 3` and `% 4` keep their sequence.
   - 0 is never reached again.
   - Used at the five increment sites and in the predictor's `count + repeatInc`
     (`:4480`).
4. **Host test** (`tools/dsp_test/modules/`):
   - `1:-` / `!1-` over 300 passes;
   - every renumbered condition at its new value;
   - `1:3` across the wrap;
   - break proofs: the old wrap, and one un-shifted mute bound.

---

## 2. Track-button preview changes the selected parameter

### 2.1 Cause (code)

1. `handleVoiceButton()` previews only on a re-press of the selected track with the
   transport stopped (`shouldPreviewVoice`,
   `Core/Hardware/frontPanel/buttonHandler.c:3162`).
2. In VOICE mode that press still runs `menu_switchPage(voiceNr)` (`:3323`).
3. `menu_switchPage()` ends with `menu_resetActiveParameter()` (`menu.c:20185`).
4. On a voice page, that moves the cursor to the screen's first selectable column
   (`menu.c:19724`).

### 2.2 Plan

- In the VOICE-mode branch, when `wasSelectedVoice` is set and
  `menu_activePage == voiceNr`, skip `menu_switchPage()` and
  `led_setActiveSelectButton()`. The press only previews.
- Any other surface showing (VOICE step edit, morph view) leaves `menu_activePage`
  different from `voiceNr`, so the press returns to the voice page as today.
- **STEP mode is not changed** (Q5, not answered; recommended). Its re-press toggles
  the track-settings half by design.

---

## 3. Load/Save pot 1 (open item M1)

### 3.1 What exists (code)

- **The encoder's type row** steps only the current page's list, with wraparound,
  through `menu_nextRestoredLoadSaveType()` (`menu.c:7314`):
  - Load: Kit, KitMrp, Scene, Bank, Pattern, Samples (`:7295`);
  - Save: the first five (`:7306`);
  - nested Instrument Load: `menu_instrumentLoadStepType()`;
  - nested Instrument Save: a Normal/Morph toggle (`menu_handleLoadSaveMenu()`,
    `:16625`).
- **Pot 1** (`menu_handleLoadSaveKnobDelta()` `case 0`, `:12127`) walks a combined
  list (`menu_loadSaveLogicalCount()`, `:11614`):
  - Load types, every voice's Instrument options, Save types, then 14 nested-Save
    entries;
  - so it crosses Load ↔ Save and enters nested modes.

### 3.2 Plan

1. **Pot 1 = an encoder type turn.**
   - `case 0` sets `SAVE_STATE_EDIT_TYPE` and `editModeActive = 1`, then calls
     `menu_handleLoadSaveMenu(delta, 0u)`, as pot 2 already does for the number row;
   - all the encoder's boundaries come with it: index-read cancel, name-session
     flush, Scene-mask reset, busy paint;
   - in nested mode it steps what the encoder steps there (Q6).
2. **The full storage lock stays** (Q7, binding S090 P1): a pot-1 turn during a list
   load is dropped.
3. **Delete the pot-only helpers** that become unused. `-Wall` would warn, and each
   has no other caller (checked):
   - `menu_loadSaveLogicalPosition()`, `menu_loadSaveLogicalCount()`,
     `menu_loadSaveApplyLogicalPosition()`;
   - `menu_instrumentLoadOptionTotal()`, `menu_instrumentLoadCurrentOptionForVoice()`,
     `menu_instrumentLoadOptionCountForVoice()`, `menu_instrumentLoadOptionAt()`;
   - `menu_loadSaveEnterInstrumentLoad()`, `menu_loadSaveEnterInstrumentSave()`,
     `menu_loadSaveSetInstrumentVoiceLed()` (`menu.c:11495–12040`).
   - `menu_loadSaveEnterTop()` (`:10943`) and `menu_instrumentEntryScene()`
     (`:11392`) keep their other callers.
4. **Repaint:** pot 1 keeps its full repaint.

Nested Instrument Load/Save is still entered with the VOICE buttons and left with the
Load/Save MODE button.

---

## 4. MIDI Omni channel

### 4.1 What exists (code)

- **G** is `midiIn_config.global` (`MidiIn.c`, `midiIn_setGlobalChannel()` `:65`). It
  comes from `PAR_MIDI_CHAN_GLOBAL` (`DTYPE_1B16`, 0 = off) and `settings.cfg`
  `midi_chan_global`.
- **Each consumer resolves channel → tracks itself:**

  | Consumer | Where |
  |---|---|
  | Notes | `midiNotes_handle()` → `midiNoteMap_resolve()` (`MidiNotes.c:53`) |
  | CC1 / NRPN | `midiControl_voiceSlotsOnChannel()` (`MidiControl.c:79`), `midiControl_targetOnChannel()` (`:98`) |
  | Program change | `midiProgram_programChange()` (`MidiProgram.c:28`) |
  | Paraphonic Colloid | `midiNoteMode_trackMode()` (`MidiNoteModes.c:149`): a non-G channel → `MNM_PARA` |

- **A lone unassigned track on its own channel** receives every note at internal
  = 63 + n (clamped). A lone Tetrakt gets sub n mod 4, pitch n / 4
  (`MidiNoteMap.c` rule 2).
- **The Global page row 0 is full** (`menuPages.h:55`). Row 2 has five free cells.

### 4.2 Meaning (proposed; Q8–Q10)

A channel message on the Omni channel **O** acts on the **selected track**
(`menu_getActiveVoice()`) as if that track were alone on its own channel:

| Message | On O |
|---|---|
| Note on/off | One-track map: `channel[t] = O` for the selected track, `off` for the others, `note[t]` and `wide` as stored. Then the overlay and note-mode paths as today. `nte off` receives nothing. |
| CC1 | That track's voice Morph |
| NRPN / data entry | Targets of that track's slot; track-scoped Scene targets only for that track |
| Program change | `seq_assignTracksToScene(1 << track, program)` |
| CC2, CC0 | Not consumed (G only) |
| MIDI Learn | O is a literal channel |

- O overrides G or a track's channel when they are equal (Q10).
- Omni ignores the track's own `ch` setting.

### 4.3 Plan

1. **Setting:**
   - `PAR_MIDI_CHAN_OMNI`, appended after `PAR_PERF_VEL_RT` (`NUM_PARAMS` stays 384);
   - `DTYPE_1B16` with the `off` name (`menu.c:6642`) and the clamp (`menu.c:6902`);
   - label `omn` / CAT_MIDI / long `Omni Ch`, with a `TEXT_MIDI_CHAN_OMNI` before
     `NUM_NAMES` (`menu.h:187`);
   - default 0 (`filesystem.c:2880`, `:3017`, `menu.c:21339`);
   - sanitize > 16 → 0;
   - `settings.cfg` key `midi_chan_omni`;
   - `menu_parseGlobalParam()` → `midiIn_setOmniChannel()`, which ends sounding notes
     on change.
2. **Menu** (Q12): row 0 becomes `bpm syn qnt ch omn txf rxf mrt`, and `scr` moves to
   row 2 column 3. Re-check the row-2 traversal (the Session 025 warning in
   `menuPages.h`).
3. **`midiIn_omniTrack(channel)`** returns the selected track for O, else 0xFF. It
   reads per message, with no cache.
4. **Consumers** test it first:
   - `midiNotes_handle()` uses the one-track map;
   - `midiControl_voiceSlotsOnChannel()` returns the slot bit;
   - `midiControl_targetOnChannel()`: track kinds compare with the track;
   - `midiProgram_programChange()` uses that track's bit;
   - `midiNoteMode_trackMode()` needs no change while O ≠ G.
5. **Held-note latch** (Q11; 17 B):
   - a 128-bit Omni note bitmap and a latched track;
   - the first Omni note-on with none held latches the selected track;
   - later notes and every note-off go to it until no Omni note is held;
   - a change of O clears it.
6. **Host test** (`tools/midi_test/`): one-track map pitches for an unassigned track,
   a rooted track and a Tetrakt.

---

## 5. DJ mode (SHIFT+PLAY+PERF)

### 5.1 What exists (code)

- **Gesture slot:**
  - SHIFT+PLAY arms `bh_chainPlayArmed` in every mode (`buttonHandler.c:3592`);
  - SHIFT+PERF is the Effect mode (`handleModeButtons()`, `(mode + 4) & 7` with SHIFT,
    from `:2596`);
  - so the new chord needs a branch ahead of that arithmetic.
- **Latched-surface precedent:** the chain editor (`menu_chainMenuOpen()`,
  `menu.c:10226`), closed for every MODE press in
  `buttonHandler_closeLatchedSurfaces()` (`buttonHandler.c:2522`).
- **COPY:** pressed in PERF it arms a copy/clear session, or with SHIFT a clear or live
  erase (`:3650`). The held-COPY router `copyClear_buttonPressed()` runs early in
  `processPress()` (`:3455`).
- **SHIFT release in PERF** re-enters `PERFORMANCE_PAGE` (`:4161` onward), which would
  close a DJ screen.
- **BAR1/BAR2 in PERF:** the trigger overlay, or the looper's triplet/dot while
  looping (`:3680`, `:3724`). SHIFT+BAR is the chromatic keyboard (S102 P1).
- **Tempo:**
  - `seq_tempo` is an integer `uint16_t` (`sequencer.c:892`), set by `seq_setBpm()`
    (`:2063`);
  - the timer period `seq_calcDeltaT()` (`:2051`) does an integer `60000 / bpm`, so
    133 BPM runs at 133.04;
  - `seq_getBpm()` feeds LFO sync, Space/CrumpBit delay sync, clockSync and
    `seq_trackStepSamples()` (`:2085`).
- **Master position:**
  - `seq_elapsedPpqTicks` and `seq_masterStepClock` (`:85–87`);
  - the repositioning model is `seq_continueRunning()`: set the tick, then
    `seq_realignActivePatternToMasterClock()`. That realigns tracks and the FX
    sequence, ends ramps and clears R12.
- **Repaint and services:** `menu_repaint()` (`menu.c:13882`),
  `menu_serviceRuntimeWidgets()` (`:18560`).

### 5.2 Behaviour

**Mode and exit:** DJ mode is **PERF mode** with a DJ screen. VOICE, SEQ and SELECT
work as in PERF (Q21). Any MODE press closes it, PERF included.

**Screen** (Q19 answered: one bar digit):
- top `BPM 128.50 +1.2%` (the nudge field is blank at 0);
- bottom `POS 001.1.01`:
  - `RRR` = repetition from 1, wrapping at 999;
  - `B` = bar inside the repetition, 1..8;
  - `SS` = step 01..16;
  - stopped: `001.1.01`.
- **Repetition** = the longest effective track length on the master 1/16 grid
  (1..128 steps, so at most 8 bars and always one digit). A track slowed by its
  scale spans several displayed repetitions.

**Pots:**

| Pot | Action |
|---|---|
| 1 | ±1 BPM a detent (1..255) |
| 2 | ±0.01 BPM, carrying into the integer |
| 3 | ±1 bar (384 ticks), running only (Q18) |
| 4 | ±1 step (24 ticks), running only (Q18) |

- Pots 3 and 4 clamp at 0, realign every track and the FX sequence, and move looper
  anchors by the same amount. No SPP is sent while running.

**COPY = tap tempo** (Q16 answered):
- every COPY press is a tap; its release is consumed;
- a tap more than 2 s after the last starts a new sequence;
- from the second tap: tempo = 60 / (mean interval since the first tap), to 0.01 BPM,
  clamped 1..255;
- the COPY LED flashes on each tap;
- copy/clear, SHIFT+COPY clear and live erase are unavailable while DJ mode is open.

**SHIFT** is the normal PERF modifier (SHIFT+SEQ queue, SHIFT+VOICE, the SHIFT+PLAY
chords). Its release does not leave the DJ screen.

**BAR1 / BAR2 = nudge down / up** (Q14):
- held: 2.5 %/s toward ±5 %, from the current value;
- released: linear to 0 over 0.5 s from the release value;
- a re-press resumes from the current value. The other button ramps through 0. With
  both held, the last pressed wins;
- no overlay toggle, looper modifier or chromatic keyboard while open (Q20). The
  overlay state is kept.

**The nudge drives the clock only** (Q15): the step timer, MIDI clock out and the
step-length gates. Synced LFOs and delays follow the base tempo.

**External sync** (Q17): pots, tap and nudge do nothing; the screen shows the measured
tempo.

### 5.3 Plan

1. **Tempo in 1/100 BPM** (`sequencer.c/h`):
   - `seq_tempo` → `seq_tempoCenti` (`uint16_t`, 100..25,500);
   - `seq_tempoEff` (`float`, BPM × (1 + nudge); 4 B; written in the foreground, read
     in TIM3);
   - `seq_calcDeltaT()` in float (`6,000,000 / (centi × 96) × 4`), which removes the
     integer truncation;
   - `seq_setBpm(bpm)` → `seq_setTempoCenti(bpm × 100)`; new `seq_getTempoCenti()` and
     `seq_setTempoNudge(float)`;
   - `seq_getBpm()` returns the rounded **base** tempo, so its callers are unchanged;
   - `lfo_recalcSync()` runs on a change of the rounded base;
   - `seq_trackStepSamples()` uses the effective tempo (`den = 24,576 × centiEff / 100`,
     64-bit).
2. **Master position:**
   - `seq_getMasterPosition(&rep, &bar, &step)` (foreground) uses a new
     `seq_repetitionSteps()`, the longest effective length;
   - `seq_jumpMasterTicks(int32_t delta)`, under PRIMASK:
     - clamp at 0;
     - set `seq_elapsedPpqTicks`, `seq_masterStepClock` and `seq_masterStepCnt`;
     - shift every `seq_loopAnchor[]`;
     - call `seq_realignActivePatternToMasterClock()`.
   - The section is bounded: 7 tracks, a 64-bit memset and the FX realign, well
     under 1 ms. The tick write and the realign must not interleave with TIM3. (The
     existing double-click realign, `buttonHandler.c:2040`, runs without PRIMASK.)
3. **Persistence** (Q13):
   - `PAR_BPM_FINE` (0..99), appended;
   - `settings.cfg` key `bpm_fine`;
   - commit `centi = PAR_BPM × 100 + PAR_BPM_FINE`; a Global-page `bpm` edit keeps the
     fraction.
4. **ButtonHandler:**
   - `handleModeButtons()`: before the SHIFT arithmetic,
     `buttonHandler_getShift() && bh_chainPlayArmed && mode == SELECT_MODE_PERF` enters
     PERF (without arming fill) and opens DJ;
   - `buttonHandler_closeLatchedSurfaces()` closes DJ;
   - `processPress()`: before `copyClear_buttonPressed()` (`:3455`), a COPY press while
     DJ is open taps and sets `bh_djCopyConsume`. The COPY release is consumed by that
     flag;
   - BAR1/BAR2 press/release ahead of the PERF branches;
   - the SHIFT release skips its PERF page re-entry while DJ is open;
   - nudge ramp in `buttonHandler_tick()` every 10 ms (`time_sysTick`) →
     `seq_setTempoNudge()`; tap stamps use TIM2 µs with unsigned deltas.
5. **Menu:**
   - `menu_djOpen()`, `menu_djClose()` and `menu_djIsOpen()`;
   - a DJ painter selected in `menu_repaint()` ahead of `menu_repaintGeneric()`;
   - a pot intercept at the top of the page routing in `menu_parseKnobDelta()`
     (`:18222`);
   - a repaint from `menu_serviceRuntimeWidgets()` when the text changes (the diff
     paint sends changed characters only).
6. **Writes:** pot 1/2 and tap write `parameter_values[PAR_BPM]` / `[PAR_BPM_FINE]`,
   apply through `menu_parseGlobalParam()`, and call `filesystem_markSettingsDirty()`.
   The Global commit only does that for `MENU_MIDI_PAGE`/`RECORDING_PAGE` cells
   (`menu.c:5842`).
7. **Host test:** the tempo period at 120.00 / 133.00 / 128.37 BPM, and the nudge law.
   Break proof: the integer division.
8. **`settings.cfg` lines** (shared with §4):
   - `bpm_fine` case 26 and `midi_chan_omni` case 27;
   - the terminator at case 28, `lines=28` (`filesystem.c` from `:19292`), with its
     comment block.

---

## 6. Tetrakt: four track-7 decays, automatable by track

### 6.1 What exists (code, measured)

- **The generated track-7 decay exists only for a non-Choke type with an
  `amp_envelope_decay` row:**
  - the VOICE7 cell is a `MENU_CELL_KIT_SETTING` (`menu.c:4880`);
  - the value is applied to the shared amp EG at the T7 trigger
    (`instrumentManager_applySlot6AlternateDecay()`, `InstrumentManager.c:3233`);
  - stored in `kit_settings_t` (2 B, `SceneData.h:89`);
  - its step automation is Scene target `7dc` (id 7), held as one override until the
    Scene-automation restore (`sequencer.c:1743`).
- **Tetrakt decays** are `tkt_decay_a..d` (`TetraktParameters.c:174–177`), read per
  control from `layer[i].decay`. Track 7 plays slot 6 as an ordinary hit.
- **The `_choke` sibling route is blocked:** Tetrakt has 64 of
  `INSTRUMENT_PARAM_COUNT` 64 descriptors (`InstrumentManager.h:19`), and the
  per-slot automation and override masks are `uint64_t`.
- **Step automation already carries its track to the drain:**
  - TIM3 queues `step_id = track × NUM_STEPS + step` (`sequencer.c:2941`);
  - `seq_drainPendingAutomationTo()` (`:4061`) decodes `owner_track` (`:4117`) before
    the instrument write (`:4150`).
- **Per pass, the trigger funnel runs restore → drain → fire**
  (`voiceControl_processPending()`, `MidiVoiceControl.c`):
  - restore for every popped trigger (`seq_restoreAutomatedParameters()`,
    `sequencer.c:4323`);
  - then one drain;
  - then the triggers in pop order.
- **Restore is per slot:** a T7 trigger restores (`slot = 5`) every held lock of
  slot 5, T6's included. Today a T7 hit therefore snaps a T6 decay lock back to the
  base mid-note.
- **The Tetrakt START latch runs at the layer's next control** (`TetraktVoice.c:247`).
  With S101 P2 A2q, one layer's control runs each block, so a pending START waits up
  to 4 blocks (2.9 ms; 8 blocks after a FADE).
- **Measured** (probe compiled with the firmware flags): `TktLayer` has `pendVel` at
  offset 84 and `env` at 88, so 3 padding bytes are free. `sizeof(TktLayer)` = 204,
  `TetraktVoice` = 1,080 B.

### 6.2 Behaviour (your rules)

1. **The track decides; no new names** (Q23). On VOICE7 with a Tetrakt on slot 6, the
   cells `dyA..dyD` show and edit the track-7 values.
2. **T7 decay automation goes to the longer decays; T6's are unaffected.**
   - A step lock of `tkt_decay_x` on track 7 sets track-7 layer x. That includes locks
     recorded, held-step edited, or copied from track 6.
   - A lock on track 6 sets the T6 row as today.
   - The T7 lock is held until T7's next trigger, like a descriptor lock.
3. **T6 wins a shared layer.** When a T6 and a T7 trigger both restart the same layer
   (both masks contain A..D) before that layer's START latch, the layer plays the T6
   trigger: short decay, T6's velocity and sample.
   - **Interpretation flagged:** your text says "a T6 trigger always pre-empts a T6
     trigger". The plan reads it as T6 pre-empting T7, as a closed hat chokes an open
     one.
   - Outside that window, the later trigger restarts the layer as today (R1
     fade-then-start), with its own track's decay.
4. **Simultaneous triggers keep their own decays.** Layers only T6 restarts use the T6
   decays; layers only T7 restarts use the T7 decays. This holds in either pop order
   and with step locks on both tracks in the same pass.
5. **A T7 hit no longer disturbs T6 decay locks.** Restore for trigger track 7 skips
   the four decay locals, which only T6 owns.

### 6.3 Plan

1. **Kit settings** (measured):
   - `kit_settings_t` gets `slot6_track7_decay[4]` and `slot6_track7_morph_decay[4]`;
     index 0 is today's value (the generic amp decay, and layer A on a Tetrakt);
   - `kit_settings_t` 2 → 8 B, `kit_t` 1,160 → 1,166 B, `scene_t` 1,656 → 1,662 B,
     so **+6 B × 17** (`scenes[16]` plus `scene_background`) = **+102 B**;
   - update the S098 field lists and the `== 2u` assert (`presetManager.c:3545`,
     `:3551`);
   - the 2,048 B stage union's asserts check the fit.
2. **Setters and getters take a `layer` (0..3)** for both images. Callers to update:
   - `SceneData.c:605`, `:643`, `:1230`;
   - `Autosave.c:1724`;
   - `menu.c:5501`;
   - `copyOps.c:719`, `:1108`, `:1194`;
   - `clearOps.c:518`, `:593`;
   - `InstrumentManager.c:4393`;
   - `presetManager.c:1598`, `:2617`.
3. **AutoSave:** `AUTOSAVE_KIT_PARAM_COUNT` 2 → 8 inside the reserved 118-byte Kit
   allocation (`Autosave.h:204`; live bytes 2 → 8). The record layout and version are
   unchanged.
4. **Kit file keys:**
   - index 0 keeps `slot6_track7_amp_envelope_decay` and
     `slot6_track7_morph_amp_envelope_decay`;
   - new keys `slot6_track7_decay_b/c/d` and their `_morph_` twins (parser
     `storageTypes.c:2082`, writer `filesystem.c:19365`).
5. **Load seeding** (Q24 answered: "a load sets T7 decays the same as T6 if they
   aren't present"):
   - `storage_kitset_t` gains `seen_t7_mask` (+1 B in the static `op_kitset`), set by
     each parsed key;
   - one helper, `scene_seedTrack7Decays(kit_t *kit, uint8_t seen)`, fills every
     unseen index from slot 6's own images:
     - Normal and Morph;
     - a Tetrakt: index x ← `tkt_decay_x`;
     - any other type with `amp_envelope_decay`: index 0 ← it;
   - called where a staged Kit becomes resident after its six Instrument files: the
     Kit Load commit before `target_scene->kit = op_staged_kit` (`filesystem.c:13056`,
     which also covers KitMrp's `filesystem_loadedKit()`), the Bank/boot reader
     (`:32360`), and `filesystem_commitSceneStage()` (`:18479`);
   - an **Instrument Load into slot 6** never carries these values. It seeds all four
     at its commit (`presetManager.c:2833`, destination slot 6).
   - A Scene reset keeps 0 (`SceneData.c:1230`; not a load).
   - **Q24b open:** confirm that Instrument Load counts as a load here.
6. **Routing by track in the drain** (`sequencer.c`, the instrument branch at `:4150`):
   - when `owner_track == 6`, the slot-5 type is Tetrakt and the target's local is one
     of the four decay rows, call
     `instrumentManager_setTrack7LayerDecayOverride(layer, value)`;
   - then skip `seq_writeInstrumentAutomation()` and `seq_rampStartIfPaired()`. The
     T7 decay is taken at the trigger, so it has no ramp;
   - the R12 menu-override test is unchanged.
7. **Restore by track** (`seq_restoreAutomatedParameters()`, `:4323`):
   - trigger track 6 on a Tetrakt clears the four T7 overrides and masks the four T6
     decay locals out of the restore (they stay held for T6);
   - trigger track 5 restores as today and never touches the T7 overrides;
   - restore precedes the drain in the same pass, so a lock on the firing step is
     written after the clear.
8. **Overrides:** `slot6_track7_decay_step_active/value` (`InstrumentManager.c:273`)
   become a 4-bit active mask plus `value[4]` (+3 B).
   - The generic 7dc path keeps its Scene target and its restore at the
     Scene-automation boundary (index 0).
   - Rule A clears (`presetManager.c:1455`, `:1639`, `:1677`; `menu.c:5521`) clear
     per layer.
9. **Menu and recording:**
   - on VOICE7 with a Tetrakt on slot 6, the `tkt_decay_a..d` cells are
     `MENU_CELL_KIT_SETTING` with layer 0..3 (beside the `amp_envelope_decay` test at
     `menu.c:4880`);
   - their automation, Learn and recording target is the slot-6 descriptor
     `tkt_decay_x` (`menu_cellMidiTargetAt()` and the kit-setting target case at
     `menu.c:5987`, `:15036`);
   - so a lock recorded or held-step edited on track 7 is stored on track 7's step and
     routed by step 6 above. `LiveRecord.c:647` keeps 7dc for the generic case only.
10. **Tetrakt runtime** (`TetraktVoice.h/.c`): `TktLayer` gains three bytes in the
    measured padding: `decayAlt`, `pendDecayAlt` and `pflags`
    (bit 0 pending, bit 1 pending-is-T7, bit 2 playing-is-T7). Size unchanged.
    - **`Tetrakt_triggerVoice()`** gains `alternate` (from `alternate_slot6`,
      `InstrumentManager.c:3305`) and the four T7 decays. For each restarted layer:
      - if a trigger is already pending (bit 0) and is T6's while this one is T7, the
        layer is left untouched (rule 3);
      - otherwise it latches `pendSample`, `pendVel`, bit 1 = `alternate` and
        `pendDecayAlt`, and sets bit 0;
      - so a T6 trigger always overwrites a pending T7 one.
    - **The START latch** (`tkt_layerControl()`, `:247`) copies bit 1 → bit 2 and
      `pendDecayAlt` → `decayAlt`, then clears bit 0.
    - **The decay read** (`:258`) is `(pflags & 4) ? decayAlt : decay`, before the
      velocity offset, so `TKT_KIND_DECAY` applies to both.
    - The T7 value is fixed per note at the T7 trigger, as the generic track-7 decay is
      today. T6 layers keep following the T6 row live.
    - `instrumentManager_applySlot6AlternateDecay()` passes the four values (override,
      else Kit) for a Tetrakt instead of writing an amp EG.
11. **Known limit:** MIDI NRPN, velocity and LFO targets of `tkt_decay_x` still act on
    the T6 row. Tracks 6 and 7 share one slot, so a channel cannot name the track.
    Step automation, VOICE7 edits and live recording are track-routed.
12. **Cost (est.):**
    - a select in the per-block control, ≈ +0.1 a sample;
    - the trigger branch and the drain test are foreground;
    - **gates:** `tetrakt_cpu.py` (both lines) and `elf_hotloops.py`.
13. **Host tests:**
    - `voice_control`/sequencer: T6+T7 in one pass in both pop orders with shared and
      disjoint masks; a T7 lock routing; a T7 hit leaving a T6 lock held;
    - break proofs: last-writer-wins on a shared layer; restore without the track mask.

---

## 7. Tetrakt looped samples

### 7.1 What exists (code, measured)

- **The Drum family's list** is stock, `s`, `l`, `w`, capped at 128
  (`osc_userListCount()`, `Oscillator.c:1322`). Tetrakt rows list only `s`
  (`instrumentManager_waveDomainMax()`, `InstrumentManager.c:1280`; Tetrakt branch of
  `menu_formatWaveCell()`, `menu.c:12553`).
- **Tetrakt's read is fused into the post loop:**
  - one-shot clamp `ph = (ph > endPh) ? endPh : ph` (`TetraktVoice.c:673`);
  - `live` requires `OSC_BIND_SMP`; `tkt_bindLayer()` sets the gain for SMP only.
- **The exact Drum loop** (`osc_slpPlainBlock()`, `Oscillator.c:1840`) adds a seam
  select and a wrap per sample. Catalogue loop points: `loop_start` / `loop_end`
  (whole sample without a `smpl` chunk, `SampleInstall.c:325`).
- **Interpolation slots:**
  - two system-wide (`OSC_WAVE_INTERP_MAX_ACTIVE`, `config.h:1039`);
  - claimed per block with `modNode_interpClaim(osc)` in the async; sticky for last
    block's holder;
  - shared with wavetable blending and `itp` fades;
  - the global `osc` interpolation setting off grants none;
  - **each slot is pre-charged 45 a sample** (the pool 90). Precedent: the Alkaline
    wavetable uses a slot for +15.
- **Measured now** (`python3 tools/dsp_test/tetrakt_cpu.py --elf build/lxr02.elf`):
  - worst block **379.1** against **390** (headroom 10.9);
  - steady 329.9.

### 7.2 Design (Q25 answered: A with an interpolation slot, B as fall-back)

Only a block in which a layer's read reaches the seam needs seam handling. That is at
most a few blocks per loop pass. Every block is one of three kinds:

| Block, per looped layer | Loop form | Per-sample change (est.) | Who pays |
|---|---|---|---|
| **No seam in this block:** predicted in the async (end-of-block phase < `(le − 1) << 12`) | the **existing one-shot form**, unchanged; its clamp never triggers | **0** | — |
| **Seam, slot granted** | **A**, exact: seam select `idx + 1 == le ? ls`, wrap `ph −= lenPh` | +4..+6 | the slot's pre-charged 45 |
| **Seam, no slot** (both slots held, or interpolation off) | **B**, early wrap: the pass plays `[ls, le − 1]` and jumps to `ls` | 0..+1 | Tetrakt's own headroom: four layers ≤ +4, so ≤ 383.1 against 390 |

- **Is it enough for A?** Yes.
  - A's premium is about a tenth of a slot's 45, and it is spent only in seam blocks.
  - The worst case without a slot is B, inside Tetrakt's gate.
  - So no per-sample cost rises beyond what the pool already charges.
- **B is the fallback, heard only when both slots are busy at a seam:** one frame
  shorter on that pass, with the seam step `data[ls] − data[le − 2]`.
  - A slot is held for one block per pass, so contention is with two wavetable voices
    or `itp` fades holding both slots at that moment.
  - A wavetable denied for that one block steps its frame (inaudible, R5).

### 7.3 Plan

1. **List:**
   - Tetrakt rows' domain = `min(2 × samples, 128) − 1`;
   - values `samples..2·samples − 1` are `l` entries of the same samples (Q26: the
     Drum-family order and cap);
   - `instrumentManager_waveDomainMax()`, and the Tetrakt branch of
     `menu_formatWaveCell()` with the `s` / `l` prefix.
2. **Bind:**
   - at the START latch (`:247`) the row value selects `oscType` SMP or SLP and
     `waveform` = value mod samples;
   - `tkt_bindLayer()`: gain for SLP;
   - `live` admits SLP;
   - per block for a looped layer: `wrapPh`, `lenPh`, and `phaseInc` clamped to below
     the loop length (Q27; no division).
3. **Seam prediction and claim** (`Tetrakt_calcVoiceAsync()`, after the controls):
   - per looped live layer: `ph + 32 × inc ≥ (le − 1) << 12` ⇒ seam;
   - on a seam, `modNode_interpClaim(&L->osc)` sets the layer's form A, else B;
   - otherwise the form is plain;
   - one byte per layer holds the form (inside `pflags`, bits 3–4; no RAM);
   - **est.** ≈ 6 instructions per looped layer per block ≈ +0.75 a sample with four
     looped layers.
4. **Loop instances:**
   - `tkt_layerPostStoreSeamA()` / `AddSeamA()` and `StoreSeamB()` / `AddSeamB()`
     beside today's store/add pair. Their bodies equal today's except for the
     phase/seam lines;
   - the sync block picks one of the forms per layer (≈ 8 branch instructions per
     block, /32);
   - flash ≈ +2.5–3 KB (est.; headroom 22,728 B at S099).
5. **The envelope still ends a looped layer** (the decay runs as for a one-shot).
6. **Gates:**
   - `tetrakt_cpu.py`: charge each layer at the dearer of plain and B. Add a third
     line for A with the premium shown ≤ 45 per slot held. `REQUIRED` gains the four
     twins;
   - `elf_loopdump.py` for every form;
   - host law TK-READ: A equals `osc_slpPlainBlock()` within its rounding across a
     seam, at several pitches; B's documented one-frame rule; plain blocks
     bit-identical to today;
   - break proofs: an off-by-one seam, and a seam block run in the plain form.

---

## 8. One-shot LFOs

### 8.1 What exists (code, measured)

- **Waves:** eight, 0..7 `sin tri sup sdn sqr rnd xup xdn` (`lfo.h`, `MenuText.h:110`).
  The count is the clamp (`menu.c:12480`, `:14532`).
- **Phase:**
  - `lfo_calc()` (`lfo.c:66`) and `lfo_stepBlock()` (`:179`, the other block of the
    S101 P2 A1 schedule) both advance it;
  - `overflow` is the wrap;
  - retrigger sets `phase = phaseOffset` (`InstrumentManager.c:2631`, `:4662`);
  - `phaseOffset` = byte × 2³² / 127 (`:5350`).
- **Measured:** `Lfo` has 2 free padding bytes after `retrigger` (offset 9, next field
  at 12). `sizeof(Lfo)` = 124 B.

### 8.2 Waves (Q28)

**Value 8 is `xtr`**, `t³` with `t = 1 − |2p − 1|`.

**One-shots, values 9..17:** each plays one cycle of its base wave
(`base = value − 9`) and holds the end value.

| Value | Name | Behaviour |
|---:|---|---|
| 9 | `si1` | one cycle of `sin` |
| 10 | `tr1` | one cycle of `tri` |
| 11 | `su1` | one cycle of `sup` |
| 12 | `sd1` | one cycle of `sdn` |
| 13 | `sq1` | **inverted:** 1 for the first half, then 0. Its start delay outputs 0 |
| 14 | `rn1` | draws one value when it fires, and holds it |
| 15 | `xu1` | one cycle of `xup` |
| 16 | `xd1` | one cycle of `xdn` |
| 17 | `xt1` | one cycle of `xtr` |

**Offset = start delay on one-shots** (Q29): retrigger sets
`phase = 0 − phaseOffset`, PRE. The start value holds until the wrap, then the wave
runs. The next wrap ends it. The delay is offset / 127 of one cycle.

Retrigger `off`: runs once, then holds (Q30).

### 8.3 Plan

1. **Enum and table:** `LFO_EXP_TRI 8`, `LFO_OS_FIRST 9` .. 17, `LFO_WAVE_COUNT 18`;
   `lfoWaveNames` count 8 → 18. Stored 0..7 are unchanged.
2. **State:** `Lfo.osState` (RUN / PRE / DONE) in the padding. Size unchanged.
3. **`lfo_calc()`:** new cases 8..17 only. Waves 0..7 keep their paths. One inline
   `lfo_osWrap()` on `overflow`: PRE → RUN (draws for `rn1`), RUN → DONE, DONE
   re-clamps.
4. **`lfo_stepBlock()`:** its noise test becomes `(LFO_WRAP_MASK >> waveform) & 1` on
   `overflow`, covering noise and the one-shots.
5. **Retrigger:** `lfo_retriggerPhase(lfo)` at `instrumentManager_retriggerRuntimeLfos()`
   and the Scene-retrigger branch of `instrumentManager_restoreLfoPhaseIfNeeded()`. The
   Scene hand-off keeps the incoming state.
6. **Cost (Q31):**
   - per block only: ≈ +3–4 on the dearest one-shot path, ≈ +1 in the step;
   - six slot LFOs on a two-block schedule → **≈ +0.4 a sample** system-wide (est.);
   - gate: `elf_blockpath.py` on `lfo_calc` / `lfo_stepBlock`.
   - No saving has been found, so this is your decision.
7. **Host laws** (`tools/dsp_test/modules/lfo.c`):
   - each one-shot ends and holds;
   - the delay is offset / 127 of a cycle at two rates;
   - `sq1` is inverted with a 0 delay;
   - `rn1` draws once;
   - waves 0..7 are bit-identical;
   - break proof: no DONE re-clamp.

---

## 9. RAM (sign-off needed)

| § | Owner | Region | Bytes | What |
|---|---|---|---:|---|
| 4 | `MidiIn.c` | SRAM1 | 1 | `midiIn_config.omni` |
| 4 | `MidiIn.c` | SRAM1 | 17 | Omni held-note bitmap (16) + latched track (1) |
| 5 | `sequencer.c` | SRAM1 | 4 | `seq_tempoEff` (`seq_tempo` → `seq_tempoCenti`: 0) |
| 5 | `buttonHandler.c` | SRAM1 | 24 | DJ open flag, COPY consume, nudge, release value and stamp, direction, tap stamps and count |
| 6 | `SceneData.c` | SRAM1 | 102 | `kit_settings_t` 2 → 8 B in 17 `scene_t` (measured) |
| 6 | `InstrumentManager.c` | SRAM1 | 3 | T7 overrides: active mask + `value[4]` (was 2 B) |
| 6 | `filesystem.c` | SRAM1 | 1 | `op_kitset.seen_t7_mask` |
| 6, 7 | `TktLayer` | slot reserve | 0 | 3 B in padding (measured): `decayAlt`, `pendDecayAlt`, `pflags` |
| 8 | `Lfo` | voice structs | 0 | 1 B in padding (measured) |
| | | **Total** | **≈ 152 B** | |

The new parameters use existing `parameter_values[]` slots. No DTCM and no FXBUF.

## 10. Order and checks

1. **Order:**
   - §2, §3 and §1 first;
   - then §4, then §5 (shared `settings.cfg` lines);
   - then §8, §6 and §7. §7 last, because it uses §6's `pflags` byte. Each DSP item
     passes its gate before the next.
2. **Each item:**
   - `make all && make img`;
   - `make -C tools/dsp_test all` (only `voice_control` 14 and `alkaline` AL-OD0 are
     expected to fail);
   - the item's host laws with break proofs;
   - `elf_hotloops.py` and the inlined-set diff against
     `tools/dsp_test/baselines/s104p1/`.
3. **Device checks:**

   | § | Check |
   |---|---|
   | 1 | `!1-` across a Scene change and a restart; one renumbered condition from each group |
   | 2 | Audition on every VOICE screen |
   | 3 | Pot 1 on the Load, Save and nested pages |
   | 4 | Omni into a paraphonic Colloid and a Tetrakt, with a track change while notes are held |
   | 5 | Nudge feel; COPY tap accuracy; jumps while a loop runs; the `cpu` widget |
   | 6 | T6/T7 on the same step with shared and disjoint `lon`; T7 decay locks; a T7 hit during a T6 lock; Kit Load of an old file (seeded); Instrument Load into slot 6 |
   | 7 | Loop seams at low pitch with interpolation on, and off (B) |
   | 8 | One-shot shapes and delays |

## 11. Decisions

**Answered:**
- Q1–Q3: `1:-` kept; `!1-` at 21, with no migration; the existing per-track counter.
- Q16: tap on COPY.
- Q19: one digit, repetition ≤ 8 bars.
- Q23: no new names; the track decides.
- Q24: seed from T6 when absent.
- Q25: A with a slot, B fallback.

**Kept as recommended (no answer):** Q4 (wrap fix) and Q5 (STEP-mode toggle kept).

**Open (recommendation in bold):**

| Q | Question | Recommendation |
|---|---|---|
| Q6 | Pot 1 in nested Instrument Load/Save | **Mirror the encoder there** |
| Q7 | Pot 1 under the storage lock | **Keep the full lock** (binding) |
| Q8 | Omni scope | **Notes, CC1, NRPN, PC on the selected track; Learn literal; no CC0/CC2** |
| Q9 | Note map | **The selected track alone on its own channel** |
| Q10 | O = G or a track channel | **O wins** |
| Q11 | Held-note latch (17 B) | **Yes** |
| Q12 | Menu | **Row 0 `… ch omn …`; `scr` to row 2** |
| Q13 | Persist `bpm_fine` | **Yes** |
| Q14 | Nudge ramp | **As in §5.2** |
| Q15 | What the nudge drives | **The clock only** |
| Q17 | External sync | **Pots, tap and nudge inactive** |
| Q18 | Bar/step pots | **Running only; realign; move loop anchors; no SPP** |
| Q20 | BAR buttons | **Nudge only while open** |
| Q21 | VOICE / SEQ / SELECT | **As in PERF** |
| Q24b | Instrument Load into slot 6 seeds the T7 decays | **Yes** |
| Q26 | Loop list | **Drum-family order, 128 cap** |
| Q27 | Very short loops at high pitch | **Clamp `phaseInc` below the loop length** |
| Q28 | Wave order and names | **As in §8.2** |
| Q29 | Offset = start delay | **Yes** |
| Q30 | Retrigger `off` | **Run once, then hold** |
| Q31 | ≈ +0.4 a sample, per block, no saving found | **Accept, measured at the gate**, or name a saving |

**§6 rule 3 interpretation** (T6 pre-empts T7): please confirm.

## 12. Statements made out of date (for you to act on)

- **`MEMORY.md`:**
  - Open Items **M1** is resolved by §3;
  - MIDI / Clock reminders gain O;
  - the Tetrakt binding paragraph changes (sample rows with loops; track 7 decays).
    Its "1,064 B voice struct" is already stale: the probe measures 1,080 B;
  - the SRAM1 total moves by ≈ 152 B.
- **`PATTERN_DYNAMIC_STACK.md`:** :71 (the 41-entry condition table and its numbers)
  and :944 (the predictor's list).
- **`FILESYSTEM_SPEC.md`:** :868 (settings keys) and :880 (`lines=26`); Kit keys for
  the T7 decays and the load seeding rule.
- **`MIDI_SPEC.md`:** the channel rules (Omni).
- **`MENU_UI_SPEC.md`:** §2.1 Global page layout; PERF gestures (DJ mode, COPY tap);
  Load/Save pot 1; VOICE7 Tetrakt decays.
- **`TETRAKT_MODULE_REFERENCE.md`:** loops (seam forms, slot use) and track 7.
- **`INSTRUMENTS_DSP_REFERENCE.md`:** LFO waves; Tetrakt as an interpolation-slot
  user.
- **`CPU_USE_DSP_AUDIT.md`** item 75: Tetrakt seam blocks join the slot users.
- **Manual:** `manual/chapters/09_midi.md:10` (Global channel row: add `omn`);
  `06_operation_modes.md` (PERF: DJ mode).
