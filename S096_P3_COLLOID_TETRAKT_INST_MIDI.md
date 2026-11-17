# S096 P3 — Colloid and Tetrakt: envelopes, new sources, MIDI (plan and assessment, 2026-11-17)

**Status:** plan, **every decision settled** (user, 2026-11-17; §9) and
**all memory signed off** (§8). Nothing is implemented. Every name, line
and figure below was checked in the tree at `fd3d58be` (branch
`dev-ph7-alkaline`), with a fresh `make all`. Cost figures marked **est.**
are estimates; the device gates in §7 decide the oscillator counts.

| # | Item | Verdict |
|---|---|---|
| C1 | Colloid envelopes follow a real gate (step length, length special, sustain) | Feasible. Control rate only (≈ +0.3 instr/sample). No new rows. |
| C2a | Pitch ADSR with a bipolar ±2-octave amount (0.38 semitone per step) | Feasible. ≈ +1.7 instr/sample. 5 rows. |
| C2b | One noise source (level, tone, width, pan), mixed with the oscillators | Feasible. ≈ +25 instr/sample (est.). 4 rows. All modes stay inside the Basic share by estimate; 12 plain oscillators very probably need not drop to 10 (§3.2). |
| C2c | Wavetable scan type `wts`: interpolated frames that move while the note plays | Feasible. **6 oscillators fit with a wide margin; 8 fit by estimate (18,900–19,600 of 19,620); 10 do not.** No interpolation slot. ≈ 670 B ITCM. |
| M | A separate MIDI note-mode module (`MidiNoteModes.c/h`) that owns every special note mode: normal, Tetrakt layers, Colloid paraphonic, and later polyphonic | Feasible. Foreground only. **≈ 2,380 B SRAM1, fixed, approved (user, 2026-11-17).** Every key of every group is tracked with full detail: 6 instances of any mode, and the later polyphonic mode (§4). |
| C3 | Paraphonic MIDI on a Colloid track with its own channel | Feasible, as a mode of M. No RAM beyond M. |
| C4 | Channel pressure (and note/velocity) as MIDI Learn sources, delta or absolute; recorded like a menu edit while live record is on | **Not possible today** (verified, §5.1). Feasible: 0 B for the table, ≈ 72 B SRAM1 for the overlays. |
| T1 | Tetrakt takes 4 MIDI notes (one per layer), striped, then pitched | Feasible, as a mode of M. +32 B SRAM1 (trigger ring). |

---

## 1. What exists today (verified)

**The Colloid's envelopes** (`ColloidVoice.c`):
- `col_egStep()` (l. 336) runs per block. Attack aims at 1.2 and stops at
  1.0, then decay to sustain. The sustain stage releases when `susOn` is 0.
- `susOn = sus && seq_isRunning()` (l. 944). This is C-D19: a stopped
  transport releases.
- The trigger (`Colloid_triggerVoice()`, l. 888–889) sets both envelopes to
  ATTACK from their current level.
- **There is no gate.** With `sus` off a note runs attack → decay →
  release whatever the step length. With `sus` on and the transport
  running it holds until a step lock or automation turns `sus` off.
- `instrumentManager_triggerTrack()` passes no step length to the Colloid
  (InstrumentManager.c l. 2603–2612, "C-D8: the sustain switch ends notes").
  The Alkaline already gets `seq_trackStepSamples(trigger_track)` (l. 2599).

**The length special** is the step's `fill` (`pat_step_specials_t.fill`,
PatternData.h l. 256; STEP page `PAR_STEP_FILL`, menuPages.h l. 156).
- It only acts on roll steps: `seq_rollBegin()` returns at once when the
  roll rate is `off` (sequencer.c l. 2939).
- On a roll step, the window is step interval × fill / 100. At the window's
  end `seq_processRollTicks()` calls `instrumentManager_requestGateDuck()`
  when fill < 100 (l. 6135): a ≈ 5 ms output fade in the mixer, not a note
  release.

**Note-off in the engines.** `voiceControl_noteOff()` only drops queued
triggers (MidiVoiceControl.c l. 357). Nothing reaches an engine. It is
called at a whole-Scene change (sequencer.c l. 2321, l. 4584), at Stop
(l. 6783) and at a mute (l. 6902). A per-track Scene change
(`seq_setTrackPlayedScene()`, l. 1931) calls nothing.

**The trigger path.** `voiceControl_processPending()` pops each trigger with
its own reset flag (`resetFlags[i]`), restores, drains the step's automation,
then fires (l. 431–460). `voiceControl_fireTrigger()` does not pass the
reset flag on. Ring entries are 3 B × 32 = 96 B (`voiceTriggerRing`, nm).

**MIDI notes** (`MidiNotes.c`). One resetting trigger per note-on through
`seq_triggerVoice()`, reported to LiveRecord. Note-off only ends an
overlay roll. The note map (`MidiNoteMap.c`) is stateless and knows
nothing of instrument types.

**Channel pressure.** `midiIn_frameDinByte()` frames 0xD0 with one data
byte (MidiIn.c l. 285), but `midiIn_dispatch()` has no case for it
(l. 179–211): it is forwarded unconsumed. Learn captures CCs only
(`midiLearn_captureCc()`). The Learn row is 4 B: `ccKind` (CC + kind bit),
`space` (0..7, 3 bits used), `type`, `index` (MidiLearn.c l. 39–44).

**Tetrakt.** `Tetrakt_triggerVoice()` restarts the layers set in `layerOn`
(the `lon` row, `tkt_layer_on`, 4 bits; TetraktVoice.c l. 470–481).
`layerOn` is read only at the trigger, after the step's automation.

**Sizes** (measured with the firmware flags):
- `sizeof(ColloidVoice)` 1,148 B, `sizeof(TetraktVoice)` 1,064 B.
- Slot reserve 1,264 B (2 × `DrumVoice`), so the Colloid has **116 B**
  spare.
- Colloid rows: 50 of 64 (`COLLOID_PARAM_DESCRIPTOR_COUNT`).
- ITCM 15,368 / 16,384 B (**1,016 B** free). Flash headroom 23,796 B.

**Colloid cost today** (`colloid_cpu.py` on this build, instructions per
sample, stereo route):

| Mode | Host count | Device |
|---|---|---|
| plain 12 (`wav`, `wtb`) | 390.6 (upper bound: counts the pwm branch) | wtb 12 measured 17,433 at 383.5 → 1.42 cycles per instruction |
| pwm Saw/Rec 6 | 376.5 | ≈ 17,950 (S096 P2, 1.49 cycles per instruction) |
| pwm Sin/Tri 6 | 379.6 | ≈ 17,920 (1.475) |

Basic share: **19,620** cycles per block. Margin today: about 1,700 cycles
in the dearest modes, about 36 instructions per sample.

---

## 2. C1 — The Colloid's gate and envelopes

### 2.1 Rules (the user's brief, restated)

1. **A gate per note.** A trigger opens the gate. Its length is:
   - **sequencer step:** the step length, or step × fill / 100 when the
     step's length special (`fill`) is below 100;
   - **roll step:** the roll's window. A carried roll keeps the gate open
     across the carry;
   - **live pad, MIDI on G, preview:** one step of the track, as if a step
     was set (§9 Q3). The pad's button-up is not used.
2. **The gate is never shorter than the amp attack.** If the gate ends
   during the amp attack, the attack completes. Then every envelope goes to
   release. The amp skips decay.
3. **Sustain holds the gate.** While the effective sustain is on, the gate
   stays open whatever the length. This is the case until `sus` goes off
   (lock, automation, menu), the transport stops, the track or Scene
   changes (rule 6), or the track is muted.
4. **Decay and sustain only while the gate is open.** This applies to all
   three envelopes: amp, filter and pitch (C2a). When the gate closes:
   - every envelope releases from its current level, whatever its stage;
   - a filter or pitch envelope still in attack releases too;
   - only the amp attack extends the gate (rule 2). If it finishes while
     the filter attack is still running, both release together.
5. **Retrigger.** Every envelope restarts its attack from its current level.
   It never restarts from 0 unless it had fully released. This is today's
   rule, kept for the pitch envelope too.
6. **Scene or track change.** A whole-Scene change or a per-track Scene
   change releases a sustained note at the track's first step event in the
   new Scene (with or without a trigger), unless that step triggers or
   carries `sus`-on automation. In that case the note continues (§9 Q4).
   Held paraphonic keys keep their note across the change while the slot
   is still a paraphonic Colloid (§4.4).

### 2.2 Design

**Engine (`ColloidVoice.c/.h`).**
- New state:
  - `gateLeft` (int32, samples);
  - `susBreak` (uint8: set by a forced release, cleared by the next
    trigger);
  - `midiHold` (uint8: set by the note-mode module, §4, while paraphonic
    keys are held).
- `Colloid_triggerVoice()` gains `stepSamples` and `first`:
  - `first` (the reset flag) sets `gateLeft = stepSamples`;
  - a roll retrigger (`first` 0) leaves `gateLeft` alone, so the roll's
    hits end where its window ends;
  - every trigger clears `susBreak`, and the envelopes go to ATTACK from
    their current level (unchanged).
- New `Colloid_gateRelease()` sets `gateLeft = 0`. New `Colloid_gateRenew()`
  sets `gateLeft = stepSamples` (for a roll carry).
- Per block, in `Colloid_calcVoiceAsync()`:
  - `susEff = sus && !susBreak && (seq_isRunning() || midiHold)`;
  - `held = susEff || gateLeft > 0`; then `gateLeft −= 32`, floored at 0;
  - `gateEff = held || ampEg.stage == ATTACK`.
- `col_egStep()` takes `gateEff` in place of `susOn`:
  - the amp: in attack it always runs to 1.0, then goes to DECAY if held,
    otherwise to RELEASE; in decay or sustain it goes to RELEASE when not
    held;
  - the filter and pitch envelopes (any stage but idle) go to RELEASE when
    `gateEff` is 0.
- Cost: a few compares and one subtract per block.

**Trigger funnel.**
- `voiceControl_fireTrigger()` passes `resetFlags[i]` on.
- `instrumentManager_triggerTrack()` gets a `reset` argument. Its only
  caller is MidiVoiceControl.c l. 260. The COL case passes
  `seq_trackStepSamples(trigger_track)` and `reset`, as the Alkaline does.

**Release requests.**
- New `instrumentManager_requestGateRelease(track)`: a per-slot byte,
  consumed in the slot's async, the same pattern as `gateDuck`. A COL slot
  turns it into `Colloid_gateRelease()`; other types ignore it.
- `instrumentManager_requestGateDuck()` on a COL slot becomes a release.
  The 5 ms fade would cut the release tail.

**Length windows for plain steps (TIM3, `sequencer.c`).**
- A plain (non-roll) step that fires with fill < 100 starts a per-track
  length window, `stepScale_ticksQ8(scale) × fill / 25600` ticks (the roll
  window's formula, l. 2942).
- `seq_processRollTicks()` counts it down. At 0 it calls
  `instrumentManager_requestGateRelease(track)`.
- The track's next trigger replaces the window.
- RAM: 7 × 2 B = **14 B SRAM1**.
- Drums and the Alkaline are unaffected: only a COL slot acts on the
  request (§9 Q2).

**Roll carry.** A carry calls `voiceControl_resetOnly()` (l. 2964). In
`voiceControl_processPending()` the leftover-reset loop calls a new
`instrumentManager_renewGate(voice)`. For a COL slot that is
`Colloid_gateRenew(seq_trackStepSamples())`, so a carried roll does not
dip at the step boundary.

**Scene, track and mute (rule 3, rule 6).**
- **Change marks.** A per-track "change" bit (1 B) is set at:
  - `seq_selectActivePattern()` (l. 2203);
  - the master-boundary switch (l. 4584);
  - `seq_setTrackPlayedScene()`;
  - `seq_clearPerTrackOverrides()`.
- **First step.** TIM3 marks the track's first step event after the change
  (`seq_advanceTrackStep()`, or `seq_fireDeferred()` for a deferred step).
- **Decision.** The foreground decides after that pass's drain. If the step
  fired a trigger, or the drain wrote `sus` = on for the slot, the note
  continues. Otherwise `Colloid_gateRelease()` runs and `susBreak` is set.
- **Mute.** A mute (l. 6902) releases at once in the same way.
- **Stop.** Stop needs nothing new: `susEff` already drops with the
  transport.

### 2.3 Tests (`tools/dsp_test/modules/colloid.c`, new laws)

- **CO-GATE:**
  - a step shorter than the amp attack: the attack completes, then release,
    with no decay;
  - a step longer than the attack: decay and sustain hold to the step's
    end, then release;
  - `sus` on: held across steps;
  - `fill` 50: release at half a step;
  - the filter or pitch attack is cut by the gate, but never before the amp
    attack ends.
- **CO-RETRIG:** a retrigger in release restarts the attack from the
  current level, for all three envelopes.
- **CO-ROLLGATE:**
  - roll hits inside a window keep the gate;
  - the window's end releases;
  - a carry renews the gate.
- **Sequencer host test** (`modules/sequencer.c`): the plain-step length
  window fires a request at step × fill / 100, and a following trigger
  cancels it.
- **Break builds:** (a) the amp attack not extending the gate; (b) a roll
  retrigger resetting `gateLeft`. Both must fail.

---

## 3. C2 — Pitch envelope, noise, wavetable scan

### 3.1 Rows and pages (append-only; 50 → 59 of 64)

| Key | Short | Page / cell | Type | Default |
|---|---|---|---|---|
| `col_penv_attack` | `pat` | AEG 4 (second screen) | 0..127 (colEgK) | 0 |
| `col_penv_decay` | `pde` | AEG 5 | 0..127 | 40 |
| `col_penv_sustain_level` | `psl` | AEG 6 | 0..127 | 0 |
| `col_penv_release` | `pre` | AEG 7 | 0..127 | 40 |
| `col_penv_amount` | `pam` | OSC 6 (second screen, after `wav typ`) | PM63: 24 / 63 = 0.38 semitone per step, ±63 = ±24 semitones (§9 Q7) | 63 (none) |
| `col_noise_level` | `nlv` | MOD 4 (second screen) | 0..127 | 0 |
| `col_noise_tone` | `nto` | MOD 5 | PM63 | 63 (0) |
| `col_noise_width` | `nwd` | MOD 6 | 0..127 | 127 |
| `col_noise_pan` | `npn` | MOD 7 | PM63 (one pan rule) | 63 (centre) |

- `init.col` (`SD_CARD/Instrument/Colloid/init.col`) gets the new keys.
- `Colloid_initVoice()` mirrors them.
- Every new row needs its `IM_SPECIAL_*` tag check (S073: plain rows are
  `IM_SPECIAL_NONE`).

### 3.2 CPU assessment (instructions per sample, stereo route)

| Addition | Est. | How |
|---|---|---|
| C1 gate | +0.3 | per block |
| C2a pitch envelope | +1.7 | one `col_egStep()` (block path 28) and one `alk_exp2f()` per block, folded into `col_oscControl()`'s `modMul` (after the slew) |
| C2b noise | +25 | per sample in the filter loop (§3.4): RNG read, conversion, one-pole HP, one SVF, two FMAs into L/R, plus about 4 of spills. Per block about 150 (two prewarps): +4.7 |
| **Total on every mode** | **≈ +27** | |

| Mode | Host now | Est. after | Est. device (cycles per instruction) | Margin to 19,620 |
|---|---|---|---|---|
| plain 12 | 390.6 | ≈ 418 | ≈ 18,980 (1.42) | ≈ 640 |
| pwm Saw/Rec 6 | 376.5 | ≈ 404 | ≈ 19,240 (1.49) | ≈ 380 |
| pwm Sin/Tri 6 | 379.6 | ≈ 407 | ≈ 19,190 (1.475) | ≈ 430 |

**Answer on the oscillator count.**
- By estimate, 12 plain oscillators can stay, and so can the pwm 6.
- The margins are within the estimates' error: about ±1.5 % from the
  cycles-per-instruction spread.
- The gate is the device measurement (S094 P2 cycle probe, cold, dearest,
  stereo send).
- **Fallback if plain 12 measures over** (decided, §9): `COL_OSC_COUNT` 10. That saves
  one pair, 30 + 2 × 186 / 32 + 24 / 32 ≈ 42 instructions per sample, about
  1,900 cycles. The code already supports it (`COL_INV_SQRT_COUNT` has the
  10 case).
- **If a pwm mode measures over:** it has no cheap count fallback (6 → 4 is
  a large change in sound). The fallback is a cheaper noise: drop the SVF
  and keep the one-pole HP and LP, with no narrow band, about −8.

### 3.3 C2a — Pitch envelope

- A third `ColEg` (`pitchEg`) with the gate rules of §2.1.
- Its offset in octaves is `level × (pam − 63) / 63 × 2`: 0.38 semitone
  per step of `pam` at full level (decided, §9 Q7). Folded in as
  `modMul × alk_exp2f(offset)` once per block, so it:
  - acts after the slew, like the LFO and velocity multiplier;
  - moves every oscillator, the root and the intervals alike;
  - still picks band-limited octave rows (`col_octave(f)` sees `f`).
- **Steps.** The pitch steps once per block (0.73 ms). A full-depth 1 ms
  attack therefore moves in large per-block steps. That is the same
  per-block pitch rule as `slw` and the LFO, and it is accepted here.
- RAM: about 20 B (`ColEg` 8, three bytes, sustain and amount floats).

### 3.4 C2b — Noise

**Source.** "Same as existing": the hardware RNG, `GetRngValue()`, full
signed value, read once per sample (MEMORY.md DSP/RNG rule; a software PRNG
was rejected in S073).

- **Observation:** the RNG makes a new value every ≈ 833 ns, about 180 CPU
  cycles (ColloidVoice.c l. 186). A short loop reads the same value several
  times: `calcNoiseBlock()` does this at its fastest rate.
- **The read goes in the filter/output loop.** `col_renderPar()` is ≈ 106
  instructions, ≈ 150 cycles per sample, so the noise gets a fresh value at
  nearly every sample. That is the "full band" the brief asks for.
- **Rejected:** a separate noise loop (~22 instructions) would repeat each
  value 5–6 times and sound darker.

**Mix.**
- The noise is mono, panned by `npn` into L/R, and added before the drive:
  `xl = drive((m + 2s) × gin + n × gNL)`, likewise `xr`.
- It therefore goes through the drive, both filters and the post stage,
  like the oscillators. In the mono route (`col_renderSer()`) it is one
  FMA into `x`.
- Level 127 is about one oscillator stack's level into the drive
  (normalised by `inGain`; set by ear).

**Tone and width law** (per block, slewed like `ColSvf`; no per-sample
coefficient ramp, to save registers):
- Corners in semitones above 20 Hz (0..115.7, the cutoff law).
- At width 1:
  - tone 0: HP at 0 and LP at 115.7, the full band;
  - tone > 0 raises the HP corner to `t × 115.7` (cuts lows);
  - tone < 0 lowers the LP corner to `(1 + t) × 115.7` (cuts highs).
- As width goes to 0, both corners close on their centre
  `c = (h₁ + l₁) / 2`:
  - tone 0 narrows around ≈ 565 Hz;
  - tone ±1 narrows around the top or bottom.
- Implementation: a one-pole TPT HP at `h`, then one TPT SVF at `l`.
  - Its output mix goes from LP (k = √2) at width 1 to UBP with k → 0.05
    (Q ≈ 20, the Colloid's floor) at width 0.
  - A make-up gain for the narrow band (≈ 1 / √bandwidth, capped) is set
    by ear.
- Constant cost: level 0 still runs the loop.

**RAM:** about 60 B: 4 row bytes, the one-pole and SVF states,
coefficients, mix, slewed corners and k, and gL/gR.

### 3.5 C2c — Wavetable scan type `wts`

**Type.**
- `typ` becomes wav / wtb / pwm / **wts**. The Colloid no longer shares
  the Alkaline's `ONOFF_TAG_ALK_TYPE` list (the Alkaline keeps 3 entries).
- New tag `ONOFF_TAG_COL_TYPE` (8, after `ONOFF_TAG_TKT_LAYERS` 7) and a
  `colTypeNames` list.
- `Colloid_setOscType()` maps byte 3 to `OSC_TYPE_WTB` plus a Colloid
  `scan` flag. Platform types are not added.

**Frame law.**
- `pos` is live (not latched at the trigger, unlike `wtb`, C-D4), so the
  knob, LFO, velocity, automation or Morph scan the table. No new scan
  source (decided, §9 Q8).
- Per oscillator, the target position is `pos × (F − 1) / 127 + r × k × F / 2`
  (the `psp` curve).
- It is **folded** at 0 and F − 1, not wrapped, so a scan never jumps from
  the last frame to the first. The second read is then always the next
  frame in memory.

**Blending, every block.**
- Each oscillator's position glides linearly from where it ended to its
  target.
- It stops at the next frame boundary within the block, so the block
  always blends two adjacent frames, A and A + 1.
- The top scan speed is one frame per block (1,378 frames/s): a 64-frame
  table in ≥ 46 ms.

**Loop.**
- New `col_scanPairStore/Add` in a new ITCM subsection `.itcm.s096p3`,
  linked after the last Colloid subsection (MEMORY.md ITCM order rule).
- Per oscillator:
  - one phase and one point/fraction pair for both reads;
  - row B read at `+2,048 B` from row A (a LDRSH immediate, so no second
    pointer register);
  - two interpolations, a blend with a Q31 `g` stepped by `dg` and read
    through its top half (SMULWT-style).
- **Always interpolated, no interpolation slot:** the blend is the loop
  itself (the S093 P8 slot rule does not apply). Constant cost.
- State reuses the pwm-only arrays (`frame` = A, `pwD`/`pwD0` = the
  fraction at the block's end and start), so the scan adds **0 B**.

**Count that fits** (est. ≈ 48–56 instructions per pair per sample; plain
pair 27/30, pwm pair 66):

| Count | Est. per sample (incl. C1/C2a/C2b) | Est. device (1.42) | Fits 19,620 |
|---|---|---|---|
| 6 | ≈ 353 | ≈ 16,040 | yes, wide margin |
| 8 | ≈ 415–431 | ≈ 18,900–19,600 | only near the low estimate |
| 10 | ≈ 470 | ≈ 21,400 | no |

**Decided (§9):**
- Build the loop and count it with `elf_looppath.py`.
- Ship 8 if a pair costs ≤ 52 and the device run is inside the share,
  otherwise 6.
- Either count is even, as the position packing needs (`posPair[o / 2]`).
- The level uses `1 / √N` (C-D12), with an assert tied to the count, as
  `COL_NORM_PWM`.

**ITCM:** about 2 × 0x150 B ≈ 670 B of the 1,016 B free (≈ 350 B will
remain). Signed off (§8).

### 3.6 Tests

- **CO-PENV:** offset law; gate; the amount sign; ±24 semitones at the
  extremes.
- **CO-NOISE:**
  - level 0 is the S096 output bit for bit (the constant-cost loop adds
    `0 × n`);
  - spectral checks at the tone and width corners (full band, HP, LP,
    narrow);
  - pan law;
  - mono route.
- **CO-SCAN:**
  - the loop against a restated law (two frames, gliding `g`);
  - no step at a frame boundary (the CO-PWG style third-difference bound);
  - fold at both ends;
  - F = 1.
- **`colloid_cpu.py`:** add the `wts` modes, a new baseline in
  `tools/dsp_test/baselines/s096p3/`, and the inlined-set diff
  (MEMORY.md CPU rules).

---

## 4. The MIDI note-mode module, and C3 — paraphonic mode

### 4.1 Why a separate module

The special note modes are not a Colloid or a Tetrakt matter. They decide
what a received note does to a voice, and the polyphonic mode will span
several voices. So one foreground module owns them all:
`Core/MIDI/MidiNoteModes.c/h`, prefix `midiNoteMode_`.

**What stays where.**
- `MidiNoteMap` stays stateless: channel and note → track. It learns only
  how many notes a track takes per pitch (§6.1).
- `MidiNotes` keeps the overlay branch and then hands every resolved note
  to the module.
- The engines know nothing of MIDI. They get triggers with a layer mask,
  `midiHold` and transient automation, through `InstrumentManager` and the
  sequencer's queue.

**Modes** (`midiNoteMode_t`). The mode is resolved per message from the
active Scene; MEMORY.md says never cache a Scene index in MIDI code.

| Mode | When | What a note does |
|---|---|---|
| `MNM_NORMAL` | any other track | today's single trigger (moved here unchanged from `midiNotes_handle()`) |
| `MNM_LAYER4` | the slot holds a Tetrakt (any channel, G included) | one trigger of one layer (§6) |
| `MNM_PARA` | the slot holds a Colloid, the track's effective channel is not G, and the BAR overlay is off | paraphonic (§4.3) |
| `MNM_POLY` | reserved: a later `pNN` channel group | polyphonic over several voices; it will override `MNM_PARA` (§4.5) |

**Instances.** Any mode can be active on all six slots at once: six
paraphonic Colloids on six channels, six Tetrakts, or any mix.
- `MNM_LAYER4` and `MNM_NORMAL` keep no state.
- `MNM_PARA` and `MNM_POLY` use the fixed pools below. Nothing is
  allocated per mode or per type.
- Tracks 6 and 7 both play slot 6, so they share slot 6's state.

### 4.2 State and sizing (fixed: every key, full detail; approved)

Two pools: **groups** (a set of held keys) and **voices** (one per slot).
- A paraphonic Colloid is a group with one member voice.
- A later polyphonic group has 2..6 member voices on one channel.
- At most 6 groups can exist, since each needs at least one slot.
- **Every group can hold all 128 keys** with full detail (user,
  2026-11-17: "every key, full detail"; the SRAM is approved). No key is
  ever dropped or forgotten, so "all keys released" is always exact.

```c
typedef struct {            /* one held key, 3 B */
    uint8_t note;           /* internal note 0..127 */
    uint8_t vel;            /* its note-on velocity (poly: re-voicing a stolen key) */
    uint8_t owner;          /* PARA: 0 fundamental, 1..3 in1..in3, 0xFF none;
                             * POLY: the voice slot 0..5, 0xFF stolen */
} midiNoteMode_key_t;

typedef struct {            /* one group, 388 B */
    uint8_t mode;           /* MNM_PARA / MNM_POLY / 0 free */
    uint8_t channel;        /* 1..16, for revalidation */
    uint8_t members;        /* slot mask */
    uint8_t count;          /* keys held, 0..128, in press order */
    midiNoteMode_key_t key[MIDI_NOTEMODE_KEYS];   /* 128 x 3 B */
} midiNoteMode_group_t;

typedef struct {            /* one voice slot, 8 B */
    uint8_t group;          /* 0..5, or 0xFF */
    uint8_t flags;          /* midiHold mirror, reassert pending */
    uint8_t note0;          /* PARA: the trigger note F0; POLY: the note playing */
    uint8_t fund;           /* PARA: the current fundamental F */
    uint8_t age;            /* POLY: steal order */
    uint8_t ivAge[3];       /* PARA: assignment order of in1..in3 (round robin) */
} midiNoteMode_voice_t;
```

| Pool | Bytes |
|---|---|
| groups: 6 × (4 + 128 × 3) | 2,328 |
| voices: 6 × 8 | 48 |
| age counter, padding | 4 |
| **Total, SRAM1, static** | **≈ 2,380** (approved) |

**Why these sizes.**
- **128 keys per group:** a note number can be held at most once per
  group, so 128 entries can never overflow. The polyphonic mode can park any
  number of stolen keys and re-voice them in press order, at their own
  velocity.
- **Lists, not tables:** the keys sit in press order (a release compacts
  the list), which gives the earliest-held key (the paraphonic
  fundamental) and the newest stolen key directly. A note-on or note-off
  searches at most 128 entries. This is foreground work, once per MIDI
  message.
- **A repeated note-on** for a key already held (no note-off between) is
  not stored twice; it updates the key's velocity.
- **The pools are the same for both modes.** The polyphonic mode adds code,
  not RAM. A voice that is still releasing is found through the engines'
  existing quiet test (`instrumentManager_ampEnvelopeQuiet()`), not a
  stored flag.
- `MIDI_NOTEMODE_KEYS` (128) goes in `config.h` beside the other MIDI sizes,
  with a static assert on the pool size.

### 4.3 C3 — paraphonic rules (the brief)

- **First note** (the group holds no keys):
  - one resetting trigger at its velocity and pitch (the fundamental F);
  - `sus` = on is automated;
  - every group plays the patch's own intervals;
  - in recording: one active step with the note, plus `sus` = on.
- **Later notes** while any are held: no trigger. Each takes the first
  interval group not yet assigned (`in1`, `in2`, `in3`), set to
  `note − F` (clamped ±63). With all three assigned, the next note takes
  the group assigned longest ago (round robin, `ivAge`). Recording writes
  only that interval's automation.
- **Fundamental released:** the earliest-pressed held key becomes F′.
  `coa` is automated by `F′ − F0`, and every assigned interval is
  re-expressed against F′, so the held pitches keep sounding. (The step's
  note cannot change without a trigger. `coa` is the recordable carrier:
  `n = coa + note − 63`, ColloidVoice.c l. 962.)
- **A non-fundamental key released:** its group returns to the patch's
  interval (decided, §9 Q9).
- **All keys released:** `sus` = off is automated, so the gate closes (C1)
  and the voice releases.
- **Pitch moves:** interval and fundamental changes glide at `slw`, like
  any Colloid pitch change.

### 4.4 Paraphonic design (inside the module)

**Entry points (foreground).**
- `midiNoteMode_noteOn(track, internal, sub, vel, ageUs)` and
  `midiNoteMode_noteOff(track, internal, sub)`, called from
  `midiNotes_handle()` after the mute and overlay checks. `sub` is the
  note's place within a multi-note track (§6.1); it is 0 for one-note
  tracks.
- `midiNoteMode_prepareTrigger(slot)`, called from
  `voiceControl_prepareTrigger()` for a resetting trigger. A slot with held
  paraphonic keys re-queues its current `sus`, intervals and `coa` before
  the drain: the live keys win over the Pattern while held (decided, §9
  Q6).
- `midiNoteMode_service()`, in the main loop. It revalidates every active
  group against the active Scene: slot type, mode and channel. A group
  that no longer resolves drops its keys, clears `midiHold` and frees
  itself. This also covers a Scene change, a type change and a channel
  edit, with no hook in TIM3. A group that still resolves after a Scene
  change keeps its keys and re-asserts `sus` = on (§9 Q4).

**Writes are transient automation, not menu edits.**
- The values (`sus`, `in1..in3`, `coa`) are queued into the same pending
  automation queue TIM3 uses (`seq_pending_automation`, sequencer.c l. 899)
  through a new foreground producer, `seq_queueForegroundAutomation()`,
  under PRIMASK.
- The drain then applies them after the restore of the same pass's
  resetting trigger (the S087 order). They are held until the slot's next
  resetting trigger, exactly like step automation.
- This is a deliberate exception to "a MIDI write is a menu edit" (Q1
  rule), approved (§9 Q5). A menu edit would overwrite the patch's `sus`
  and intervals in the Scene with every chord.

**Recording.**
- New `liveRec_paramValueIn(target9, value7)`: a value-carrying sibling of
  `liveRec_paramIn()`. It reads no menu value and keeps the same
  first-change-per-step rule.
- The first note also goes through `liveRec_triggerIn()` as today.

**Stopped transport.**
- `midiHold` (C1) keeps `susEff` on while keys are held, so paraphonic
  notes sustain with the transport stopped.
- The module sets it through a new `instrumentManager_setMidiHold(slot,
  on)` and clears it at the last note-off or when it drops a group.

### 4.5 The polyphonic mode later (not in this plan)

The pools already hold what it needs:
- **A group:** `members` = the slots of a `pNN` group, `channel` = NN.
- **Allocation:** each key takes a quiet member voice, else the oldest
  (`age`).
- **Stealing:** a stolen key keeps `owner` 0xFF. When a sounding key is
  released, the newest stolen key is re-voiced at its stored `vel`.
- **Note-off:** the voice's gate ends through the C1 release request.

What stays open for that session: the storage of a `pNN` group (track
`ch` stays 0..17 now, MEMORY.md MIDI), its menu, and the note map rule for
a group. `MNM_POLY` takes precedence over `MNM_PARA` for its member tracks.

### 4.6 Tests

- **New host test** `tools/midi_test/notemode_test.c`, `MidiNoteModes.c`
  linked alone, with the Scene lookup, the queue and the trigger stubbed:
  - mode resolution: Colloid on G is normal; Colloid off G is paraphonic;
    Tetrakt is layer4 on any channel; the overlay on falls back;
  - **six instances:** six paraphonic Colloids on six channels with chords
    held at once, plus a mix of Tetrakt and Colloid slots; no pool
    exhaustion, no cross-talk;
  - tracks 6 and 7 feeding slot 6's group;
  - paraphonic:
    - first-note trigger and `sus` on;
    - interval assignment;
    - round robin at the 5th key;
    - fundamental hand-over (`coa` and the intervals keep every sounding
      pitch);
    - non-fundamental release;
    - all-off → `sus` off;
    - clamp at ±63;
    - a chord arriving within one pass;
    - all 128 keys held on one group, then released in a random order
      (`sus` off only after the last one);
    - a repeated note-on for a held key;
  - revalidation: a type change or channel edit drops the group and clears
    `midiHold`;
  - a static assert on the pool size (≈ 2,380 B).
- **Device:** a held chord, a stopped transport, recording, and playback
  of the recorded steps; two paraphonic Colloids on two channels at once.

---

## 5. C4 — Channel pressure and other Learn sources

### 5.1 Today

- Pressure is not assignable (§1): no dispatch case, no capture, no row
  field.
- Notes never capture a Learn (they go to `midiNotes_handle()`), so the
  "ignore note and velocity during Learn" part already holds.

### 5.2 Design

**Receive.**
- `midiIn_dispatch()` gains a `CHANNEL_PRESSURE` case under RXF C
  (`MIDI_FILTER_CONTROL`), calling a new `midiControl_handlePressure()`.
- During a background Bank load, pressure is accepted like a CC
  (`midiIn_acceptedDuringBankLoad()`).

**Capture.**
- `midiLearn_capturePressure(channel)`: while a target waits, the first
  pressure message (any value) stores a pressure row with mode `+`.
- A CC still captures as today. Whichever of the two comes first wins.

**Row encoding (0 B extra).**
- `space` bits 0..2 keep 0..7.
- Bits 3..4 hold the source: 0 CC, 1 pressure, 2 note number, 3 velocity.
- Bits 5..6 hold the mode: 0 the source's default, 1 absolute,
  2 positive delta, 3 negative delta.
- For non-CC sources `ccKind` keeps only the kind bit.

**`midilrn.cfg`.**
- The first field of `learn=` becomes a source token:
  - `CCC`: a CC, absolute, as today;
  - `prs+`, `prs-`, `prs=`;
  - `nte+`, `nte-`, `nte=`;
  - `vel+`, `vel-`, `vel=`.
- The writer emits these canonical forms. Existing CC lines are unchanged,
  so no migration question arises.
- Note and velocity rows exist only through the file (the gesture never
  captures them, per the brief).

**Apply.**
- **Absolute** (`=`): exactly a CC. `midiControl_writeTarget()`, a menu
  edit, recorded, R12.
- **Delta** (`+` / `−`): a non-destructive overlay on voice targets.
  - **Table:** `midiLearn_overlay[6][4]` of {local, delta, base}, 3 B each,
    **72 B SRAM1**.
  - **Writer:** `instrumentManager_writeRuntime()` adds the slot/local's
    active delta (clamped to the row's range) to every value it writes, and
    stores that value as the overlay's base. Menu edits, Morph, automation
    and restores therefore all carry the pressure on top, and "automation
    wins" still holds for the base.
  - **Changes:** a pressure change rewrites the runtime from the stored
    base. Pressure 0 frees the entry and writes the base back.
  - **Sign:** `+` adds up to the parameter's maximum; `−` subtracts down to
    its minimum.
- **Recording a delta (user, 2026-11-17, §9 Q11).** While live record is
  writing (LATCH or OVERDUB, the target owned by the recording track's
  Scene), a delta change records like a menu edit, as other MIDI
  automation does:
  - the value recorded is the **effective** value the delta sets,
    `clamp(base ± delta)`. It goes through `liveRec_paramValueIn(target9,
    value7)` (§4.4), with the menu's first-change-per-step rule; so a
    pressure swell is recorded step by step, and its return to 0 records
    the base again;
  - the change also sets the parameter's R12 override
    (`seq_menuOverrideSet(slot, local)`), as a menu edit does, so the
    lane being overdubbed does not overwrite the base under the player's
    hand until the voice's next trigger;
  - the sound is unchanged by recording: the overlay still rides on the
    base, and the Scene's stored value is not written (only an absolute row
    commits, §5.2 Absolute);
  - on playback the recorded steps are ordinary step automation; pressure
    played over them adds on top of the recorded value.
  The rule is the same for every delta source (pressure, note number,
  velocity). With live record off, a delta is not recorded and sets no R12
  override.
- **Note and velocity deltas** hold from a note-on until the next note-on
  on that channel (the platform's velocity rule, decided, §9 Q12).
- **Scope:** delta modes on Effect and Scene targets are refused at parse
  time in this step (decided, §9 Q11).

**Uniqueness.** Today "one parameter → one CC" (MIDI_SPEC §6.2). Decided:
one row per (parameter, source), so a cutoff can have a CC (base) and
pressure (on top). TX still uses the first CC row (decided, §9 Q13).

### 5.3 Tests

- **`tools/midi_test`:**
  - the `learn=` parse/format round trip for every token;
  - the space-byte packing;
  - capture order (pressure first / CC first).
- **Overlay law** (host, `instrument_manager.c`):
  - base + delta clamps;
  - a menu edit under pressure keeps the delta;
  - pressure 0 restores the base exactly.
- **Recording law** (host, LiveRecord with the pattern service stubbed):
  - with live record writing, a pressure change records `clamp(base ±
    delta)` on the current step, once per step (first change), and sets
    the R12 override;
  - pressure back to 0 records the base;
  - with live record off, nothing is recorded and no override is set;
  - the same for a note and a velocity delta.

---

## 6. T1 — Tetrakt: four notes per track

### 6.1 Note map (`MidiNoteMap.c/h`) and the module

**Who decides the width.** The note map stays stateless and type-blind.
It is told how many notes each track takes per pitch:
- `midiNoteMap_settings_t` gains `wide`, a track mask (stack only, +1 B):
  a set bit means the track takes 4 notes per pitch.
- `midiNoteMap_load()` fills it from `midiNoteMode_wideTracks(scene)`. The
  module sets a bit for each track whose mode is `MNM_LAYER4`, so the map
  never reads instrument types.
- Track 7 plays slot 6, so T6 and T7 are both wide when slot 6 is a
  Tetrakt.
- A later mode that needs another width changes only the module and this
  mask.

**Resolve** also returns `sub`, the note's place within its pitch: 0..3 on
a wide track, 0 otherwise. The module turns `sub` into the layer (A..D).
- **Rule 1 (assigned range):** `d = n − root`, `sub = d mod 4`,
  internal = `63 + d / 4`.
- **Rule 2, G stripe:** a wide track takes 4 consecutive stripe places
  (`sub` 0..3), so the place is `n mod |U′|` with `|U′| = |U| + 3 × wide`.
  Default pitch, as every stripe.
- **Rule 2, split** (own or shared channel): inside its block,
  `sub = (n − start) mod 4`, internal = `63 + (n − start) / 4`. A wide
  track's block is weighted 4 so its semitone span matches the others'
  (decided, §9 Q14).
- **Rule 4:** the clamp still caps internal at 127.

**Inverse (TX)** gains `sub`. A Tetrakt step sends one note per layer set
in its effective `lon` (decided, §9 Q15); the module gives the layer →
`sub` order.

### 6.2 Trigger with a layer mask

**Why not automation.** The brief's "temporarily sets the trigger mask
automation" is met for recording (§6.3), but the sound must not rely on it.
Two notes of one voice in one render pass share one drain, so the last
`lon` write would serve both triggers (MidiVoiceControl.c l. 423–425,
"last writer wins"). A two-note chord would lose a layer.

**The mask rides on the trigger instead.** The module's `MNM_LAYER4`
branch turns `sub` into `1 << sub` and fires one trigger with that mask.
It keeps no state, so six Tetrakts cost nothing in the module's pools.
- `VoiceTriggerEvent` gains `layers` (0 = use `lon`): 3 → 4 B, so the ring
  is 96 → **128 B (+32 B SRAM1)**, and the pass's stack batch also grows
  by 32 B.
- `seq_triggerVoice()` gets a sibling with the mask.
- `instrumentManager_triggerTrack()` passes it on. `Tetrakt_triggerVoice()`
  uses `mask ? mask : layerOn`.
- Each layer latches its own velocity (`pendVel`, already per layer).

**Overlay rolls** (BAR `<`/`>`): the overlay branch runs before the
module, so it asks the module for the note's mask
(`midiNoteMode_layerMask(track, sub)`). `bh_overlay[track]` keeps it
(+7 B), so a held MIDI roll repeats the same layer (decided, §9 Q16).

### 6.3 Recording

- `liveRec_trigger_t` gains `layers` (stack only).
- LiveRecord writes the hit as today, plus a `lon` step automation entry
  through `patSvc_writeStepAutomation()`. Its value is the OR of every
  MIDI layer recorded on that step in this pass of the step: the first hit
  replaces an existing `lon` lock, later hits on the same step OR into it.
- Playback is then ordinary step automation: `lon` applies before the
  trigger.

### 6.4 Tests

- **`tools/midi_test/notemap_test.c`:**
  - wide stripe on G (mixed with ordinary tracks);
  - wide assigned range (`sub` and pitch);
  - weighted split;
  - the clamp;
  - inverse round trip for every `sub`;
  - six wide tracks on one channel;
  - the `nte off` invariant still holds.
- **`tools/midi_test/notemode_test.c`:** `sub` → mask, and the overlay
  query.
- **`modules/voice_control.c`:** two layer notes in one pass keep both
  masks.
- **`modules/tetrakt.c`:** the mask override and `lon` fallback.

---

## 7. Order of work (each phase builds and passes host tests alone)

1. **P3a — C1 gate.** Engine, funnel `reset`, release/renew requests, the
   length window, the change and mute marks. Gate: host laws; the device
   behaviour by ear.
2. **P3b — C2a + C2b.** Pitch envelope and noise. Gate: `colloid_cpu.py`
   counts, then the device run in all three Colloid modes. **Decide 12 vs
   10 here.**
3. **P3c — C2c `wts`.** Build at 8, count, device run, settle on 8 or 6.
4. **P3d — module M, then T1.** `MidiNoteModes.c/h` with its full pools
   (sized for polyphony from the start), mode resolution, the `NORMAL`
   branch moved from `MidiNotes`, revalidation; then the `wide` note map,
   the `LAYER4` mode, the trigger mask and recording.
5. **P3e — C4.** Pressure, Learn sources, overlays, and the delta
   recording rule. `liveRec_paramValueIn()` lands here (P3f reuses it).
6. **P3f — C3.** The `PARA` mode in the module. It uses P3a's `midiHold`,
   the foreground automation producer and P3e's `liveRec_paramValueIn()`.

---

## 8. Resources (MEMORY.md RAM rule): all signed off (user, 2026-11-17)

| What | Bytes | Region | Owner |
|---|---|---|---|
| Colloid gate, pitch envelope, noise state | ≈ 88 (1,148 → ≈ 1,236 of 1,264) | slot reserve (no growth) | `ColloidVoice` |
| Plain-step length windows | 14 | SRAM1 | `sequencer.c` |
| Change/first-step marks, release requests | ≈ 8 | SRAM1 | `sequencer.c`, `InstrumentManager.c` |
| Trigger ring `layers` | 32 (+32 stack) | SRAM1 | `MidiVoiceControl.c` |
| Overlay rolls' masks | 7 | SRAM1 | `buttonHandler.c` |
| Note-mode pools: 6 groups × 128 keys, 6 voices (§4.2) | ≈ 2,380 | SRAM1 | `MidiNoteModes.c` |
| Pressure/note/velocity overlays | 72 | SRAM1 | `MidiLearn.c` / `InstrumentManager.c` |
| `wts` loops | ≈ 670 (signed off) | ITCM (≈ 350 B left) | `ColloidOsc.c` |
| **SRAM1 total** | **≈ 2,513** (signed off) | | |

DTCM is untouched (earmarked for delay lines). Flash: a few KB of
23,796 B headroom.

---

## 9. Decision record (user, 2026-11-17)

Q7 and Q11 were decided by the user as stated below. Every other question
was approved as recommended, and all memory in §8 is signed off.

| # | Question | Decision |
|---|---|---|
| Q1 | Noise source | Hardware RNG per sample, read inside the filter/output loop (≈ a fresh value per sample). |
| Q2 | Length special on other types | A plain step's `fill` < 100 ends only Colloid notes; drums and the Alkaline are unchanged. |
| Q3 | Gate of a live pad, G-channel MIDI note or preview | One step of the track, as if a step was set; the pad's button-up is not used. |
| Q4 | "First played step" after a Scene or track change | The track's first step event in the new Scene, with or without a trigger (a trigger simply retriggers). Held paraphonic keys keep their note while the slot is still a paraphonic Colloid; the module re-asserts `sus`. |
| Q5 | Paraphonic writes | Transient automation, not menu edits: an approved exception to the "MIDI write is a menu edit" rule. |
| Q6 | Pattern triggers while paraphonic keys are held | The live keys re-assert and win while held. |
| Q7 | Pitch envelope amount | **User:** PM63, **0.38 semitone per step**, ±63 = ±24 semitones. |
| Q8 | Scan source for `wts` | None new: `pos` is moved by the LFO, velocity, automation and Morph. The optional `sce` row is dropped. |
| Q9 | A non-fundamental key released | Its interval group returns to the patch's interval. |
| Q10 | Paraphonic with the BAR overlay on | The overlay's roll/flam action wins. |
| Q11 | Recording pressure deltas | **User:** with live record on, a delta change records the effective value it sets (`clamp(base ± delta)`), and behaves like a menu edit, as other MIDI automation does (§5.2: first change per step, R12 override). With live record off it is not recorded. Delta modes stay on voice targets only (approved). |
| Q12 | Note and velocity deltas | Held until the next note-on on the channel. They follow the Q11 recording rule. |
| Q13 | Learn uniqueness | One row per (parameter, source): a CC and pressure can share a parameter. |
| Q14 | Split weighting | A Tetrakt block weighs 4 on a shared channel. |
| Q15 | Tetrakt TX | One note per layer set in the step's effective `lon`. |
| Q16 | Overlay rolls on a Tetrakt | Repeat the note's own layer. |
| Q17 | Module sizing | Every key, full detail: 6 groups × 128 keys + 6 voices, ≈ 2,380 B SRAM1. |
| — | Oscillator counts | Plain 12 stays unless the device gate fails (then 10); `wts` ships 8 if a pair costs ≤ 52 and the device run is inside the share, else 6. |
| — | Memory | All of §8 signed off: ≈ 2,513 B SRAM1, ≈ 670 B ITCM, ≈ 88 B inside the Colloid's slot reserve. |

---

## 10. Statements this work makes out of date (for the user to act on)

- **MIDI_SPEC.md:**
  - §4.1 (the note map: wide tracks, `sub`, weighted split, stripe
    count);
  - §4.3 (Colloid paraphonic note-on/note-off; Tetrakt layer notes;
    "note-off: a single trigger ignores it" no longer holds for Colloid
    tracks);
  - §6.1/§6.2 (Learn: sources, modes, uniqueness, and the delta
    recording rule);
  - §6.4 (`learn=` tokens);
  - §12/§13 (the new `MidiNoteModes` module and its place in the
    pipeline);
  - §3 dispatch (channel pressure);
  - §18 (RAM);
  - §19 binding rule Q1 (the paraphonic exception).
- **PATTERN_DYNAMIC_STACK.md §6:** `fill` now also acts on plain steps
  (Colloid length window); a roll's window end releases a Colloid instead
  of ducking it.
- **PERFORMANCE_RECORD_SPEC.md §3:** recording MIDI writes value-carrying
  automation (paraphonic `sus` / intervals / `coa`; Tetrakt `lon` OR-merge;
  Learn deltas at their effective value, with the R12 override, §5.2).
- **MEMORY.md:**
  - "MIDI Learn: 128 rows" (row semantics extended);
  - "A MIDI write is a menu edit (Q1)" (paraphonic exception);
  - the ITCM figure (15,368 → ≈ 16,040 B);
  - "Note map: … Lowest track wins" (wide tracks);
  - the MIDI reminders gain the note-mode module.
- **MODULE_INTERCHANGE_SPEC.md** "Core/MIDI" and **REPOSITORY_LAYOUT.md:**
  the new `MidiNoteModes.c/h`.
- **Code comments:**
  - `ColloidVoice.h`'s C-D8/C-D19 notes;
  - InstrumentManager.c l. 2603 ("no step length");
  - ColloidParameters.c's page comment;
  - `MidiLearn.h`'s "one parameter → at most one CC";
  - `MidiVoiceControl.c`'s "last writer wins" note (it still holds for
    automation; the mask now bypasses it).
