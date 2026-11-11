# S095 P2 — Colloid, the Sixth Instrument (draft)

- **Status:** draft plan and assessment for review (written after S093 P8;
  **updated for S095 in §U, which wins over anything below**). **No
  firmware code is changed by this document.** Figures marked *measured* come
  from standalone scratch spikes compiled with the Makefile's DSP flags and
  counted with `elf_looppath.py --cond-heads --in-range` (§13). Figures
  marked *estimate* are not measured. Both kinds are re-measured on a linked
  image before a schedule is written.
- **Brief (user, 2026-11-10):**
  - mix as many oscillators as possible; it may be an Advanced instrument;
  - two state-variable resonant filters, series or parallel (stereo); basic
    but **smooth**; drive into them; no post-drive, no SRR;
  - a stereo spread of the oscillators when the filters are parallel;
  - PWM as an oscillator option;
  - intervals, a detune spread and portamento; free-running oscillators;
  - an amp envelope with sustain: slow and basic is fine, but smooth;
  - user wavetables without modulation or interpolation, with a wavetable
    selection plus a spread that picks a random index further from the
    centre as the spread rises.
- **Binding rules that apply:**
  - S089: no per-sample cost may rise without a measured saving. A new
    instrument is held to the ceiling of its class: the worst-case Scene
    must not rise.
  - S093: per-block work counts.
  - Constant CPU: budget the dearest mode.
  - `INSTRUMENTS_DSP_REFERENCE.md` §9.2 (the add-a-type checklist) and §12
    (oscillator code rules).
  - RAM and ITCM need sign-off.
  - Append-only type registry; no backward compatibility.
  - `MEMORY.md` document rule: no step here edits `MEMORY.md` or
    `knowledge_files/`; §12 lists what would go out of date.
- **Companion draft:** `DRAFT_PWM_IMPLEMENTATION.md` *(superseded, §U.4: PWM
  shipped in S094 P0/P1/P4; reuse `osc_pwmReadD()` and `oscPwmTables`)*.

---

## U. S095 update (2026-11-11): what changed since this draft, and what the next session does

This draft was written on 2026-11-10, after S093 P8. S094 and the S095 P1
plan have changed the ground under it. **Where this section conflicts with
a figure or reference below, this section wins.** The superseded lines are
marked *(superseded, §U)*.

### U.1 Order of work and dependencies

- S095 runs P1 first: the Delay, Reverb and Echo Effects, then the user's
  compressor adjustments.
- P2 (this document) comes after. It depends on the Advanced budget that P1
  sets (U.2).
- **Next session:**
  1. Read U and `S095_P1_FX_DELAY_RVB_ECHO.md` §H, which has the standing
     rules, the tree state and the tools.
  2. Do the §11 spike, with U.2's measure.
  3. Bring the counts back to the user for C-D1 and C-D2.
  4. Then write the schedule.

### U.2 The CPU gate is now measured cycles per block, not instruction ceilings

**The rule** (S094 P2 §§18–21, AS-7) replaces S089's Basic 261.3 and the
Advanced 285.9/292.9 instruction ceilings:

- a type is measured on the device with the probe;
- as the first instance in a Scene (cold), at its dearest settings, with a
  stereo send;
- counting block-rate work, render, SRR (none here) and its output/send
  add;
- instruction counts remain the host regression tool, not the gate.

**Budget in force** (cycles per block, 97 % cap of 156,705):

| Item | Budget |
|---|---:|
| Basic ×4 | 19,620 |
| **Advanced ×2** | 21,803 → **21,635** after S095 P1 D19 |
| Effect | 16,970 (after P1) |
| Shared mixer | 12,784 |
| Slack | 500 |

**Translation of this draft's ≈ 278 (estimate, to be measured):**

- **The reference.** The Alkaline at its worst (cold, `wtb` blend, about
  276.9 whole-voice instructions per sample) measured 16,572 cycles of
  render plus 3,037 of send add, 19,609 in all (S094 P2 §18). Its render is
  about 1.87 cycles per instruction, including the cold I-cache penalty
  (about 2,200 for the Alkaline).
- **The Colloid's voice.** About 274 per sample without the mixer delta,
  which is about 16,400 cycles of render.
- **The send add.** It was 3,037 before `QADD`. `QADD` is now in the mixer
  adds, so it is about 2,100–2,200 (estimate), plus the stereo delta.
- **Total:** about **18,600–19,600 against 21,635** as Advanced.
- **The Colloid has no VDIV per sample** (the Alkaline's ladder has 3, at
  about 14 cycles each). Its cycles per instruction may therefore be lower
  than the Alkaline's.

**What follows (estimates):**

- **As Advanced,** there are about 2,000–3,000 cycles of room. That is
  roughly 2–3 more oscillator pairs, **about 16–18 plain or 10 PWM**.
- **As Basic** (19,620), 14 plain may already fit.
- **C-D1 (class) and C-D2 (count) are therefore reopened.** The spike plus
  one device run decides them.

### U.3 The Advanced list today

- S094 P2 §22 moved Cymbal and HiHat from Advanced to Basic. No type is
  Advanced today.
- The Colloid would be the first. The other planned Advanced type is an
  Alkaline variant with wavefolders and tone, estimated at 20,700–21,200.
- The two-per-Scene limit is `instrumentManager_typeSelectableForSceneSlot()`
  (`InstrumentManager.c:443`, the flag test at `:461-463`).
- Text below that says "shared with Cymbal/HiHat" is superseded.

### U.4 PWM is shipped, so reuse it

- **The companion draft no longer exists under its old name.** The form
  shipped as `S094_P0_PWM_IMPLEMENTATION.md` and S094 P1 S3. The width law
  was redesigned in `S094_P4_PWM_OSC_INVESTIGATION.md` and
  `S094_P4_IMPLEMENTATION.md`.
- **The two-read pulse** is `osc_pwmReadD()` (`Oscillator.c:3809`).
  - It uses a 15-bit fraction (`(ph >> 7) & 0x7FFF`), not this draft's
    16-bit one.
  - It stores through `OSC_PWM_STORE` (SSAT16, `:3827`).
- **The width law (S094 P4):**
  - pos 0 = 50 %, pos 127 = the narrowest: Rec 0.39 %, Saw/Tri 2.54 %;
  - tables in `Core/DSPAudio/oscPwmTables.h`: `oscPwmDRec`/`oscPwmDSaw`
    (pos → d), `oscPwmLevelRecQ14`/`oscPwmLevelSawQ14`, and the Tri warp
    tables.
- **For the Colloid:**
  - `pwd` and `pws` map through `oscPwmDRec` (the saw-row pulse is the Rec
    orientation), so every voice shares one law. This replaces the draft's
    own "folded at 1 and 127" law.
  - The width offsets fold or clamp in pos space, 0..127.
  - The PWM pair loop is re-spiked with the shipped read.
- **The level tables** offer optional loudness matching across widths.
  §2.4's "no per-oscillator level" stays the default; the tables are an
  option for C-D12.

### U.5 Other changes since the draft

| Item | Was | Now |
|---|---|---|
| Crash | "without crash: ceiling 286.9"; crash removal as an ITCM reserve | **Crash is removed** (S094 P1 S2). Those lines are superseded. |
| Flash | headroom 42,828 B | **691,100 B used, 62,564 B headroom.** S095 P1 estimates about +16 KB, which leaves about 46.5 KB for the Colloid's 10–13 KB. |
| ITCM | 12,128 / 16,384 | **13,984 / 16,384 B** (2,400 free). The Colloid's oscillator loops (about 900 B) fit, leaving about 1,500. |
| Mixer | dry loop 42 / stereo 44 (measured before `QADD`) | `QADD` is in the adds (S094 P2), so **re-measure** the mono and stereo dry and send loops. |
| Mixer anchors | | `sampleData` `mixer.c:1074` (`:860` is `mixer_calcNextSampleBlockTest()`'s); the stereo balance law `mixer.c:1236-1247` (was l. 1114–1117); `fx_active` `:1099`; the FX path `:1201-1255` |
| D-cache | off | **On** (MPU region 0, write-through, non-shareable). The voice state in SRAM is cached. All earlier cycle reasoning predates this. |
| `IM_SPECIAL_OSC_TYPE` Alkaline branch | l. 3890 | **`InstrumentManager.c:3925`** |
| Verified unchanged | | type enum `InstrumentManager.h:89-90`; `runtime_slots` `InstrumentManager.c:213`; `ampEnvelopeQuiet` `:1572`; `seq_trackStepSamples(trigger_track)` `:2268`; ON_OFF tags 1–3 used (`MenuText.h:183`, `:213`, `:240`), 4–15 free |
| S095 P1 interplay | | P1 locks one of the two interpolation slots while an Effect feed runs. The Colloid uses no slots (no interpolation), so it is unaffected. Its stereo voice feeds stereo-input Effects; all three P1 types are stereo in. |

### U.6 Revised next steps (replaces §11, steps 1–2)

1. **Spike (linked, a scratch copy outside the tree):**
   - the oscillator loops, with the shipped `osc_pwmReadD()` form for PWM;
   - both filter loops;
   - the stereo mixer functions against the `QADD` loops;
   - a stub voice.
2. **Host gates:**
   - linked loop counts, the per-block path (`elf_blockpath.py`,
     `control_cost.py --elf`);
   - the mixer gate (0 grew);
   - ITCM and flash (`link_budget.py`).
3. **One device run with the probe:**
   - the stub voice at its dearest mode, cold, with a stereo send;
   - measured against Basic 19,620 and Advanced 21,635;
   - **output:** the oscillator counts per class and mode, for C-D1 and
     C-D2.
4. Then §11 steps 3–5.

### U.7 Standing rules

These are the same as `S095_P1_FX_DELAY_RVB_ECHO.md` §H.3:

**Documents and commits**

- No step edits `MEMORY.md` or `knowledge_files/`, and they are never
  edited in the same turn as any other file.
- Commits are the user's.
- Nothing needed goes in `build/`. Records go in
  `tools/dsp_test/baselines/`.
- No `.claude` auto-memory. Project context is the root `MEMORY.md`; the
  agent working notes are `knowledge_files/volatile/memory/VOLATILE_MEMORY.md`.

**Resources**

- RAM and ITCM growth need sign-off.
- Logging allocations exist only under `DEV_MODE_LOGGING`.

**Device runs and reporting**

- One flash per question.
- Report resource figures as one net total against the current build.

**Code**

- Every change carries an accurate comment block.

### U.8 S095 review against the tree (2026-11-11)

**Corrected references:**

- `sampleData` is at `mixer.c:1074` (`:860` is the test function's);
- the balance law is at `mixer.c:1236-1247`;
- the noinline Alkaline renders are at `AlkalineVoice.h:243-248`;
- `menu_instrumentLoadIndex[]` is at `menu.c:1729`;
- `check_special_tags.py` `TABLES` is at `:21-32`;
- `filesystem_trimInstrumentTreeBlocking()` is at `filesystem.c:24251`;
- new ITCM code goes after `.itcm.s094p1`, now the last named subsection
  (`STM32F765VIHx_FLASH.ld:182`), not after `.itcm.s089p3`.

**Rechecked, unchanged:**

- `osc_pwmReadD()` `Oscillator.c:3809` and `OSC_PWM_STORE` `:3827`;
- `InstrumentManager.c`: `:213`, `:443`, `:461-463`, `:1572`, `:2268`,
  `:3925`;
- the slot reserve is 2 × `sizeof(DrumVoice)` = 1,264 B
  (`InstrumentManager.c:195-232`);
- ON_OFF tags 1–3 are used, and 4–15 are free;
- `voiceControl_noteOff()` is at `MidiVoiceControl.c:357`.

**From the P1 review (`S095_P1_FX_DELAY_RVB_ECHO.md` §13):**

- None of A1–A3 changes this draft.
- **A3:** no code claims FxBuffer voice units today. The Colloid needs none
  (§9 item 7), so that stays true.
- **The Advanced budget (21,635)** is final only after P1's device run.
  §5.7 there takes any shortfall in what a locked slot frees from Advanced.


---

## 0. Summary: what we can do

| Feature | Proposal | Cost basis |
|---|---|---|
| **Oscillators** | **14** band-limited oscillators (interpolated), or **8** with PWM. Option: ≈ 28 with truncated reads ("dense"). Free-running, own phase each | Pairs measured at 26 per sample (13 each); a PWM pair 44 with its own width per oscillator |
| **Waves** | Saw / Rec / Tri (the stock band-limited octave rows), or any installed user wavetable (`wNN`), one 1,024-point frame per oscillator, no interpolation | Same loop; only the row pointer differs |
| **Wavetable spread** | `pos` = centre frame; `wsp` = spread. Each oscillator takes a random frame offset that grows with `wsp`, drawn at the note (main loop) | 0 per sample |
| **Intervals / detune** | `int` picks an interval set (uni, oct, 5th, maj, min, sus, 7th …); `dtn` spreads the detune around each member | Per block only |
| **Portamento** | `por` glide time (log-pitch one-pole per block); `pmd` always / legato only | Per block only |
| **Filters** | Two linear TPT state-variable filters (Simper/Zavalishin form), LP/BP/HP each. Coefficients ramped per sample (smooth). A division-free cubic drive before each. Series (mono) or parallel (stereo: filter A left, B right) | **72** per sample parallel (the dearest), 64 series, 0 VDIV |
| **Stereo spread** | `ste`: in parallel, the even and odd oscillators cross-fade between left and right | Folded into the filter loop (included in 72) |
| **Amp envelope** | ADSR, per-block exponential segments with a per-sample ramp. **Sustain ends by a gate**: `gat` (length in steps of the triggering track) and `lgt` (tie/legato), the Alkaline gate mechanism (§4) | Ramp folded into the filter loop |
| **Output** | True stereo voice: a second int16 block, a stereo path in the mixer | Mixer dry path +2 per sample (measured), send ≈ +2 (*estimate*) |
| **Class** | **Advanced** (≈ 278 against the 292.9 Advanced ceiling). Basic would allow 12 / 7 oscillators (§6.3) *(superseded, §U.2: the gate is now cycles per block; Advanced 21,635, Basic 19,620; class and count are reopened)* | |
| **RAM** | **0 B new.** The voice (≈ 990 B, *estimate*), its oscillator buses and right-channel block all fit the existing 1,264 B slot reserve | |
| **ITCM** | ≈ 0.9 KB (the oscillator loops) | Sign-off |
| **Flash** | ≈ 10–13 KB (*estimate*; Alkaline's engine is ≈ 14.5 KB) | Headroom 42,828 B *(superseded, §U.5: 62,564 B now, ≈ 46.5 KB after S095 P1)* |

**The two findings that make 14 oscillators possible:**
1. **Integer oscillators.** An interpolated table read accumulated into an
   int32 bus costs 15 alone and **13 each in pairs**. The shipped float
   loops cost 23–24.
2. **A division-free filter pair.** Two TPT SVFs with drive, the stereo
   spread, the amp ramp and both output stores cost **72** with 0 VDIV per
   sample. One ZDF filter costs 85–104 with 2 VDIV.

---

## 1. Signal path

```
14 oscillators (free-running; own pitch, row, phase)
   even ones -> bus E (int32[32])      odd ones -> bus O (int32[32])
                       |
          +------------+---------------------------+
          | parallel (stereo)                       | series (mono)
          | L = a E + b O,  R = b E + a O           | x = E + O
          |   a = (1 + ste)/2, b = (1 - ste)/2      |
          | drive -> SVF A -> L                      | drive -> SVF A -> drive -> SVF B
          | drive -> SVF B -> R                      | L = R = out
          +------------+---------------------------+
                       |
          amp ADSR (per-sample ramp) -> int16 L block (mixer's buffer)
                                        int16 R block (inside the voice)
                       |
          mixer: stereo dry path (balance) + stereo/mono FX send
```

Per block, before the render:
- pitch: note + coarse/fine + glide, then each oscillator's interval and
  detune ratio, its octave row and its frame row;
- the two filters' coefficients and their per-sample steps;
- the ADSR step and gate count;
- the drive gain and the spread gains.

---

## 2. The oscillator bank

### 2.1 The loop

- **State per oscillator:** `{ uint32_t phase; uint32_t inc; const int16_t
  *row; }`, 12 B.
- **The read:** the 1,024-point row read at `phase >> 22`, linearly
  interpolated in integer:
  `a + ((b − a) · ((phase >> 6) & 0xFFFF) >> 16)`.
- **The accumulation:** two oscillators per pass, added into one int32 bus.
  The first pair of each bus **stores**, so no clear pass is needed.
- **Copies:** separate copies for "pair store", "pair add" and "single"
  (for an odd count), plus the PWM forms. Each is in a new `.itcm.sNNN`
  subsection, `noinline, noclone` (§12 of the reference).
- **Even/odd buses:** even oscillators go to bus E, odd ones to bus O.
  Interval members are assigned by pair (`member = (i >> 1) mod M`), so each
  member sounds on both sides (§2.2).

| Loop (*measured*) | Per sample | Per oscillator |
|---|---:|---:|
| Single, add | 15 | 15 |
| **Pair, add** | **26** | **13** |
| Pair, store (first pair of a bus) | 24 | 12 |
| Four per pass, add | 61 | 15.25 (register pressure: worse) |
| Single, truncated read | 8 | 8 |
| Pair, truncated | 12 | 6 |
| PWM single (two-read pulse) | 23 | 23 |
| PWM pair, one shared width | 43 | 21.5 |
| **PWM pair, own width per oscillator** | **44** | **22** |

### 2.2 Pitch: note, intervals, detune, portamento

- **Base pitch.** The Colloid keeps one `OscInfo osc` as its pitch anchor,
  as Alkaline does. It holds `midiFreq` (coarse/fine), the note,
  `modNodeValue` (the LFO/velocity target on `coa`) and the `wav` / `typ` /
  `pos` bytes. So the existing `osc1_*` writers and the `osc1_` accessor
  work unchanged.
  - Per block: `log2 f` → the glide → one `exp2` for the base increment.
- **Intervals** (`int`, a selector): a constant table of semitone sets, up
  to four members each. Ratios come from a constant table of 37 floats
  (−12..+24 semitones), so there is no `exp2` per oscillator. Draft set:

  | # | Name | Semitones | | # | Name | Semitones |
  |---:|---|---|---|---:|---|---|
  | 0 | `uni` | 0 | | 8 | `su2` | 0 2 7 |
  | 1 | `oct` | 0 12 | | 9 | `su4` | 0 5 7 |
  | 2 | `sub` | −12 0 | | 10 | `7th` | 0 4 7 10 |
  | 3 | `5th` | 0 7 | | 11 | `m7` | 0 3 7 10 |
  | 4 | `4th` | 0 5 | | 12 | `mj7` | 0 4 7 11 |
  | 5 | `o+5` | 0 7 12 | | 13 | `6th` | 0 4 7 9 |
  | 6 | `maj` | 0 4 7 | | 14 | `m9` | 0 3 7 14 |
  | 7 | `min` | 0 3 7 | | 15 | `pwr` | −12 0 7 12 |

- **Detune** (`dtn`): each oscillator has a fixed coefficient `c_i` in
  −1..+1 (a constant table, spread like a supersaw: dense near the centre,
  wider at the edges).
  - `dtn` maps 0..127 to 0..±50 cents (curve set by ear).
  - Ratio: `1 + c_i · δ`, with δ the spread in natural-log units. The
    linear form is within 0.7 cent of the exponential at 50 cents.
  - Per oscillator per block: two multiplies and a conversion.
- **Octave row:** each oscillator picks its own stock octave row from its
  own frequency (the exponent form `osc_setTableFreq()` uses). An interval
  set spanning two octaves therefore stays band-limited on every member.
- **Portamento** (`por`, `pmd`):
  - a one-pole on the base `log2 f` per block, as the Alkaline slide lag;
  - `por` 0 = off, up to ≈ 2 s (curve set by ear);
  - `pmd`: `all` (every note glides) or `leg` (only legato notes, §4).
- **Free-running:** no phase is ever reset. `Colloid_initVoice()` seeds the
  phases from `GetRngValue()` (main loop, not audio), so the stack does not
  start phase-aligned.

### 2.3 Waves and the wavetable spread

- **`wav`** lists `Saw Rec Tri`, then `w01..wNN` (the installed
  wavetables). The stock rows are the band-limited `[11][1024]` tables.
  - A user wavetable frame is 1,024 points, 16-bit (S093 P8), not
    band-limited per octave, so high notes alias as on every other
    instrument's `wtb`.
  - No sine: there is no 1,024-point sine row set. A sine wavetable can be
    installed, or a sine copy of the loop added (C-D10).
- **Binding:** the Colloid binds its own table, as `alk_wtbBind()` does:
  table index and catalogue generation are checked once per block, and
  `osc_userBind()` is never called.
- **Spread** (`pos`, `wsp`):
  - at each non-legato trigger, every oscillator draws `r_i` in −1..+1
    from `GetRngValue()` (the trigger runs in the main loop);
  - its frame is `wrap(centre + round(r_i · wsp/127 · frames/2))`, with
    the centre from `pos`;
  - `wsp` 0 puts every oscillator on the centre frame.
- **No interpolation, no mid-note jumps (recommended):** the frames are
  latched at the trigger, so a held note never steps between frames.
  Changes to `pos` / `wsp` apply from the next note (C-D4).
- **Interpretation to confirm (C-D5):** "wavetable selection and spread" is
  read as frames **within** the selected table. Spreading across
  neighbouring **tables** in the catalogue (w03, w04, w02 …) costs the same
  (only the row pointer differs) and could be a second mode.

### 2.4 PWM

- `typ` = `wav` / `pwm`.
- Under `pwm` every oscillator plays the band-limited two-read pulse (the
  saw-row difference of `DRAFT_PWM_IMPLEMENTATION.md` §1.1, form D), with:
  - `pwd`: the centre width, read every block (an LFO on it moves every
    oscillator together); *mapped through the shipped S094 P4 law (§U.4)*;
  - `pws`: the width spread. Each oscillator draws its own offset `q_i` in
    −1..+1 at each non-legato trigger (main loop, like `r_i`); its width is
    `pwd + q_i · pws`, folded back at the 1 and 127 ends, recomputed every
    block.
- **Every oscillator has its own width.** Nothing is shared but the centre
  and the spread amount. Cost (*measured*): the pair loop with one width per
  oscillator is 44 against 43 with a shared width, +0.5 per oscillator (one
  more register, 2 stack references against 1). The block's offsets are
  derived per block and passed in; the draws are in the 28 B of §7.1.
- **No per-oscillator level is needed.** In the summed bus each pulse keeps
  the saw's full peak-to-peak whatever its width; only its loudness changes
  with width, as on an analogue PWM. The bus normalisation (§2.5) covers
  headroom.
- **The count follows the mode, the cost does not:** 8 PWM oscillators
  (4 pairs × 44) or 14 plain ones (24 + 6 × 26) are both budgeted. This is
  an algorithm choice like a filter type, not a skip.
- The same two-read loop applied to a wavetable frame is a comb on that
  frame. It is free to allow, but it is not PWM in the usual sense (C-D6).

### 2.5 Quality notes

- **Integer interpolation:**
  - the fraction is 16 bits;
  - the rounding is ±1 LSB against the float form;
  - the int32 bus has headroom for 2^16 full-scale oscillators;
  - the level into the drive is scaled by a per-block constant (1/√N or
    1/N; set by ear).
- **The dense option (truncated reads):**
  - on the ramps, truncation adds noise about −60 dB under a saw;
  - at each band-limited edge the read lands on a table point (1/1,024 of
    a cycle), which is aliasing-like grit on high notes (*estimate*);
  - many detuned oscillators decorrelate it. A host law measures it before
    the option is offered.

---

## 3. The filters

- **Form:** the linear TPT state-variable filter. Per sample:
  ```
  v3 = x − ic2;  v1 = a1·ic1 + a2·v3;  v2 = ic2 + a2·ic1 + a3·v3
  ic1 = 2·v1 − ic1;  ic2 = 2·v2 − ic2;  y = mH·x + mB·v1 + mL·v2
  ```
  - LP (0, 0, 1), BP (0, 1, 0), HP (1, −k, −1). Notch and peak are other
    mixes at the same cost.
  - Per block: `g = tan(π fc/fs)` (`fastTan()`, one VDIV), `k = 2 − 2·res`
    (floored at ≈ 0.05), `a1 = 1/(1 + g(g + k))` (one VDIV), `a2 = g·a1`,
    `a3 = g·a2`. That is 4 VDIV per block for both filters (≈ 0.13 per
    sample).
- **"Smooth":**
  - the TPT form stays stable under any modulation of `g` and `k`;
  - `a1..a3` are ramped linearly per sample from last block's values, so a
    cutoff sweep has no block steps;
  - the cutoff target is slewed per block, like `SVF_recalcFreqSlew()`;
  - this is the main reason to prefer it over the shipped nonlinear ZDF
    (85–104, 2 VDIV) and over a Chamberlin SVF (unstable near Nyquist).
- **Drive:**
  - `x' = clamp(g_d·x, ±1.5)`, then `y = x' − (4/27)·x'^3`: a cubic soft
    clip with no division;
  - before filter A, and in series also before filter B;
  - `drv` sets `g_d` (×1..×16, per block);
  - there is no drive after the filters (brief).
- **Resonance level:** the drive is on the inputs only, so at high
  resonance the peak can exceed full scale. The output store saturates (in
  the measured loop); `k`'s floor bounds the Q.
- **Modes** (`fmd`):
  - `par`: A on the left bus mix, B on the right, the spread `ste`
    applies;
  - `ser`: A → B on the mono sum, output on both sides, `ste` has no
    effect.
  - Both modes run both filters every block (constant cost); `par` is the
    dearest and is budgeted.

| Filter loop (*measured*; includes drive, spread, the amp ramp and both int16 stores) | Per sample | Stack refs |
|---|---:|---:|
| Parallel, independent HP/BP/LP mix per filter | **72** | 7 |
| Series, independent mixes | 64 | 4 |
| (Parallel with one shared mix: the first spike) | (65) | |

The parallel loop spills (7 stack references). A tuned layout may bring it
down; ramping only `a1` and deriving `a2`/`a3` is one candidate (*not
measured*).

---

## 4. Amp envelope and the gate: how sustain works

**Nothing today tells an instrument that a note has ended.**
- Steps have no length.
- `voiceControl_noteOff()` (`MidiVoiceControl.c` l. 357) only drops queued
  triggers and clears `active_voices`; nothing reaches an engine.
- Alkaline solves this with a **gate**: 7/12 of the triggering track's step
  (`seq_trackStepSamples()`), counted down 32 samples per block.
- Colloid uses the same mechanism, made adjustable.

| Row | Meaning |
|---|---|
| `atk dec sus rel` | ADSR: per-block exponential segments (one-pole; attack aims above 1 and stops at 1, the analogue curve). Times ≈ 1 ms .. 10 s, sustain 0..1. The block value is ramped per sample in the output loop (measured, inside the 72) |
| `gat` | Gate length: a multiple of the triggering track's step (draft law: 1/16 step … 16 steps, log). The gate opens at the trigger; when it reaches 0, release begins |
| `lgt` | Tie (an on/off row, step-automatable like Alkaline's `sld`). A trigger that arrives while the gate is open, on a step with `lgt` on, is **legato**: no envelope restart, the gate is re-armed, and the pitch glides (with `pmd` `leg`, only these notes glide) |

- **Retrigger** starts the attack from the current level (no click).
- **Quiet test:** `Colloid_isQuiet()` = envelope idle and level < 1e-4,
  the `instrumentManager_ampEnvelopeQuiet()` threshold. It needs a branch
  there (l. 1572), as Alkaline has.
- **Track 7's alternate hit:** `instrumentManager_applySlot6AlternateDecay()`
  needs a `SlopeEg2` and returns early without one (as for Alkaline). A
  Colloid in slot 6 plays the alternate hit as an ordinary hit. No change is
  needed.
- **Later options (not proposed now):**
  - a MIDI note-off release path (`voiceControl_noteOff()` →
    `instrumentManager` → `Colloid_release()`), for keyboard play, with
    `gat` gaining a `MIDI` setting;
  - a `hold` gate (open until the next trigger), which needs a release on
    transport stop.

---

## 5. Stereo output and the mixer

The mixer renders each slot into one mono stack block (`sampleData`,
`mixer.c:1074`). Colloid adds a right channel without new RAM:

| Item | Proposal |
|---|---|
| Right block | `int16_t right[32]` **inside `ColloidVoice`** (in the slot reserve) |
| Accessor | `instrumentManager_slotRight(slot)`: the Colloid's block, or NULL for every other type |
| Mixer slot loop | after `instrumentManager_calcSlotSyncBlock()`: `right ? stereo path : existing path`, one pointer test per slot per block. The existing calls keep their arguments |
| Dry path | new noinline `mixer_addVoiceStereoToOutput()`: L → left bus, R → right bus, with the **balance law** of the stereo FX return (`mixer.c:1236-1247`, centre 63 = unity both sides). Single-side routes (`DAC1_L` …) take (L+R)/2 |
| FX send | new noinline `mixer_addVoiceStereoToOutputAndFx()`: a stereo-input Effect gets L/R; a mono-input Effect gets (L+R)/2 |
| SRR | none (`instrumentManager_slotUsesSrr()` default 0; brief) |
| `pan` row | `DTYPE_PM63`, stored 63 = centre; balance law for this stereo source (the one-pan rule allows a different stereo law) |

- **Cost (*measured*, before `QADD`; re-measure, §U.5):** the shipped mono dry loop (DAC1 stereo case) is
  42; the stereo form is **44** (+2). The send is about the same again
  (*estimate* +2).
- **Risk:** the engine renders are inlined into
  `mixer_calcNextSampleBlock()`. Any code added there can move unrelated
  inlined loops (S086 F1; the Alkaline render moved the mixer twice in P8).
  Mitigations:
  - `Colloid_calcVoiceAsync()` and `Colloid_calcVoiceSyncBlock()` are
    noinline (the Alkaline precedent, `AlkalineVoice.h:243-248`);
  - the stereo functions are noinline;
  - the mixer gate (`elf_p4_check` / `elf86_mixer_check`, 0 grew) is a hard
    pass condition.

---

## 6. Cost

### 6.1 Composition (Advanced, dearest mode = parallel)

| Part | 14 plain | 8 PWM | Basis |
|---|---:|---:|---|
| Oscillators | 24 + 6 × 26 = **180** | ≈ 42 + 3 × 44 = **174** | measured loops (PWM store pair *estimated* from the add pair; own width per oscillator) |
| Filters, drive, spread, amp ramp, stores | 72 | 72 | measured |
| Mixer stereo delta (dry + send) | ≈ 4 | ≈ 4 | dry +2 measured, send *estimate* |
| Per block / 32 | ≈ 22 | ≈ 19 | *estimate*, below |
| **Total** | **≈ 278** | **≈ 269** | |
| Advanced ceiling (Cymbal/HiHat dearest, measured not frozen) *(superseded, §U.2)* | 292.9 | 292.9 | `DRAFT_SAMPLE_VOICE_REDUCTIONS.md` §4.7.1 |
| Margin (without crash: ceiling 286.9) *(superseded, §U.2, U.5)* | 14.9 (8.9) | 23.9 (17.9) | |

**Per block (*estimate*):**

| Part | Instructions |
|---|---:|
| Async: base pitch, glide, one `exp2` | ≈ 40 |
| 14 × oscillator control (ratio, increment, octave row, frame row) | ≈ 250 |
| Two filters' coefficients and ramp steps (4 VDIV) | ≈ 90 |
| ADSR, gate, drive gain, spread gains | ≈ 65 |
| Render entry/exit (7 oscillator calls, the filter loop) | ≈ 220 |
| Dispatch; mixer per-block extras | ≈ 30 |
| **Total** | **≈ 700 → ≈ 22 per sample** |

**The ninth PWM or fifteenth plain oscillator:**
- a 9th PWM oscillator (+23 + per block) reaches ≈ 292.5: it fits 292.9
  by only 0.4, and not 286.9;
- a 15th plain one (+15) reaches ≈ 293.6: it does not fit.
- 14 / 8 is recommended until the linked figures are in.

### 6.2 The cycle proxy

- The Colloid has **no VDIV per sample** and about 4 per block. The
  Cymbal/HiHat ceiling has 3 VDIV per sample.
- On the S093 P0 cycle proxy (VDIV ≈ 14 cycles), the ceiling is 331.9 and
  the Colloid ≈ 280, a margin of ≈ 52 against ≈ 15 on instructions.
- If the proxy gated a division-free voice, ≈ 20 plain oscillators would
  fit.
- The instruction count is the established gate, and the tighter one here,
  so it is used (C-D3).

### 6.3 Advanced or Basic

| Class | Plain | PWM | Dense (truncated, *estimate*) | Placement |
|---|---:|---:|---:|---|
| **Advanced** (≤ 292.9) *(superseded, §U.2/U.3: 21,635 cycles; no other Advanced type today)* | **14** | **8** | ≈ 28 | at most two per Scene, shared with Cymbal/HiHat (`instrumentManager_typeSelectableForSceneSlot()`, `InstrumentManager.c` l. 443) |
| Basic (≤ 261.3, the Drum ceiling) *(superseded, §U.2: 19,620 cycles)* | 12 (≈ 250) | 7 (≈ 244) | ≈ 22 | any slot, any number |

- **The gap is small** (+2 / +1 oscillators). The filter pair, the mixer
  delta and the per-block work cost the same in either class.
- **Recommended: Advanced**, per the brief ("as many oscillators as
  possible").
- Basic is a real alternative if two Colloids plus Cymbal/HiHat in one
  Scene matters more (C-D1).
- A type cannot be both (`:460-462` of the same file).

### 6.4 Constant-cost check

| Choice | Cost |
|---|---|
| `fmd` series / parallel | both filters run; parallel (72) budgeted |
| Filter type LP/BP/HP/notch | mix coefficients; same cost |
| `typ` `wav` / `pwm` | 14 × 13 or 8 × 21.5: both budgeted |
| Stock row / wavetable frame | same loop; the row pointer differs |
| Spread, intervals, detune, glide | per block |
| Silence, gate closed | the voice renders in full every block |

---

## 7. Memory

### 7.1 RAM: inside the existing slot reserve

`runtime_slots[6]` reserves 1,264 B per slot (2 × `sizeof(DrumVoice)`;
`InstrumentManager.c:195-232`). A `ColloidVoice` adds a union member and
an assert. It does not grow the union.

| Member | Bytes (*estimate*) |
|---|---:|
| `OscInfo osc` (the pitch anchor, the `osc1_*` rows) | 72 |
| 14 × `{phase, inc, row}` | 168 |
| Per-oscillator spread draws (frame, width) | 28 |
| Buses E and O, `int32_t[2][32]` | 256 |
| Right output block, `int16_t[32]` | 64 |
| Two filter states and ramps (`ic1 ic2 a1..a3 da1..da3 mH mB mL k`) | ≈ 112 |
| ADSR, gate, glide, amp ramp | ≈ 48 |
| Parameter members (≈ 30 floats/bytes) | ≈ 100 |
| `Lfo lfo` (the platform pair) | 124 |
| Wavetable bind (pointer, frames, index, generation) | 16 |
| **Total** | **≈ 990** (≤ 1,264; ≈ 270 to spare. The dense option adds ≈ 200) |

- **No new static, DTCM or SRAM allocation.**
- The buses could instead be one static shared by both possible Colloids
  (they render one after another). That would save nothing here, would
  need a sign-off, and is not proposed.

### 7.2 ITCM and flash

| | Bytes (*estimate*, from measured loop sizes) | Where |
|---|---:|---|
| Oscillator loops: pair store, pair add, single; PWM pair store, PWM pair add, PWM single | ≈ 900 | ITCM, new `.itcm.sNNN` after `.itcm.s094p1`, the last one today (`STM32F765VIHx_FLASH.ld:182`) |
| Dense pair (if adopted) | ≈ 90 | ITCM |
| Filter/output loops (parallel 528 B, series 476 B measured) | ≈ 1,000 | flash, as the other filters and post-chains (ITCM measured worse for filters in S023); the spike measures both |
| Mixer stereo dry and dry + send functions (six routing cases each) | ≈ 2,500 | flash |
| Voice, control, parameters, pages, interval and detune tables | ≈ 7,000–9,000 | flash |

ITCM total with PWM and Colloid: 12,128 + ≈ 2,000 + ≈ 900 ≈ 15.0 / 16.4 KB.
Crash removal (≈ 540 B) and the reductions draft's S-C (≈ 2.2 KB) are the
ITCM reserves if needed. *(Superseded, §U.5: ITCM is 13,984 / 16,384 B with
PWM shipped and crash removed; ≈ 14.9 KB with the Colloid.)*

---

## 8. Parameters and pages (draft)

- **Values:** bytes 0..127 as on every instrument.
- **Selectors:** all 16 `DTYPE_MENU` ids are taken (S078), so new selectors
  use the `DTYPE_ON_OFF` high-nibble tag, as `ONOFF_TAG_ALK_TYPE` (3) does.
  Free tags are 4–15. The Colloid needs four: `int`, `fmd`, filter type,
  `pmd`.
- **Keys:** `osc1_*`, `instrument_*`, `lfo_*` and `velo_*` reuse the
  platform rows and writers. New keys take the `col_` prefix.

| Page (SELECT index) | Cells | Rows |
|---|---|---|
| 0 OSC | `coa fin wav typ` / `pos wsp pwd pws` | `osc1_pitch_coarse`, `osc1_pitch_fine`, `osc1_wave`, `osc1_type`, `osc1_position`, `col_wt_spread`, `col_pw_width`, `col_pw_spread` |
| 1 AEG | `atk dec sus rel` / `gat lgt` | `col_attack`, `col_decay`, `col_sustain`, `col_release`, `col_gate`, `col_tie` |
| 2 mod | `dst amt` / `por pmd` | velocity rows; `col_glide`, `col_glide_mode` |
| 3 FM (unison) | `int dtn ste` | `col_interval`, `col_detune`, `col_stereo` |
| 4 click | — | |
| 5 FILTER | `cta rsa tya fmd` / `ctb rsb tyb drv` | `col_cut_a`, `col_res_a`, `col_type_a`, `col_filt_mode`, `col_cut_b`, `col_res_b`, `col_type_b`, `col_drive` |
| 6 LFO | the Drum layout (12 cells) | platform LFO rows |
| 7 MIX | `vol pan` | `instrument_vol`, `instrument_pan` |

- About 43 rows.
- Continuous rows are `MORPHABLE | MODULATABLE | AUTOMATABLE`, with the
  conversion done at control rate in the async, as Alkaline's float rows.
  So **no new special writers** are needed.
- The selectors (`typ`, `int`, `fmd`, filter types, `pmd`, `lgt`) are
  `SELECTOR`: automatable per step, never morphed.
- `typ` reuses `IM_SPECIAL_OSC_TYPE` with a Colloid branch, as the Alkaline
  branch at `InstrumentManager.c:3925`.
- `pos` and `wsp` are read at the trigger (§2.3). `pwd`, cutoffs,
  resonance, drive, detune and spread are read every block (smooth through
  the ramps and slews).

---

## 9. Platform integration (`INSTRUMENTS_DSP_REFERENCE.md` §9.2, item by item)

| §9.2 item | Colloid |
|---|---|
| 1 Enum and registry | `INSTRUMENT_TYPE_COL` after `INSTRUMENT_TYPE_ALK`, before `UNKNOWN` (`InstrumentManager.h` l. 89–90); a designated row `"col"`, `".col"`, label `"Colloid"` (7 cells; Load Morph shows `ColloMrp`), directory `"Colloid"` (7 characters), `INSTRUMENT_FLAG_ADVANCED`. Type-sized arrays grow by one (e.g. `menu_instrumentLoadIndex[]`, `menu.c:1729`: +2 B, `.bss`) |
| 2 Files | `Core/DSP/Instruments/Colloid/ColloidVoice.c/.h`, `ColloidParameters.c/.h`, and a control/table file if needed |
| 3 Runtime | union member + assert (`:195-232`); `instrumentManager_colloidRuntime()`; a case in every §3.2 dispatcher; `ampEnvelopeQuiet()` branch (l. 1572); `seq_trackStepSamples(trigger_track)` passed to the trigger as for Alkaline (l. 2268); `instrumentManager_slotRight()` (new) |
| 4 Build | include path; parameters in `SRCS`; voice in `DSP_SRCS` with an explicit `-Ofast` rule (the pattern rule covers only `Core/DSPAudio/`) |
| 5 Host proof | `check_special_tags.py` `TABLES['COL']` (`:21-32` hold the five today); the tag gate; `init.col` generator/check |
| 6 Storage | append-only token (the `.hcnames` header accepts an older header as a leading part); AutoSave stores `col`; `SD_CARD/Instrument/Colloid/init.col`. The boot directory trim enumerates every `/Instrument` subdirectory (`filesystem_trimInstrumentTreeBlocking()`, `filesystem.c:24251`), so the new folder is covered |
| 7 Class | Advanced (§6.3); no FxBuffer voice units needed (no delay line) |

---

## 10. Risks and unknowns

| Risk | Handling |
|---|---|
| The mixer's inlined loops move when the stereo branch is added | noinline voice and stereo functions; the mixer gate is a pass condition; spike first |
| The per-block estimate (≈ 700) is off | `elf_blockpath.py` / `control_cost.py`-style count in the spike; the oscillator count is set from the measured total |
| The parallel filter loop's spills grow when linked | measured linked; layout candidates (§3) |
| Summing 14 sources: level and headroom into the drive | per-block normalisation; host level law |
| The Advanced ceiling is not frozen *(superseded, §U.2: the gate is the measured 21,635-cycle budget)* | measure the stub voice with the probe (§U.6) |
| User wavetables alias at high notes | as every `wtb` today; noted, not fixed |
| The sound of the integer interpolation and the dense option | host laws (alias/noise floor against an oversampled reference), then the user's ear |

---

## 11. Proposed next steps

1. **Spike (linked, outside the tree):**
   - the oscillator loops, both filter loops, the stereo mixer functions
     and a stub voice in a scratch copy of the tree;
   - gates: linked loop counts, per-block path, `elf_p4_check` (mixer
     0 grew), ITCM/flash.
   - **Output:** the oscillator counts per class and mode.
2. **Freeze the Advanced ceiling** as a baseline file. *(Superseded: see §U.6.)*
3. **Host model and laws:**
   - filter stability under modulation and coefficient ramps;
   - ADSR smoothness (no step larger than the ramp allows);
   - gate timing against `seq_trackStepSamples()`;
   - interval and detune ratios;
   - spread gains (mono sum at `ste` 0);
   - level normalisation;
   - the dense option's noise floor.
4. **Schedule**, then implementation in this order:
   - registry/type (append) and an empty voice;
   - oscillators and filters;
   - mixer stereo;
   - parameters/pages and `init.col`;
   - PWM.
5. **Device:** the worst-case Scene with two Colloids in the Advanced
   places and four dearest Drums (`cpu` widget, underruns); then listening.

---

## 12. Decisions

| # | Question | Recommendation |
|---|---|---|
| C-D1 | Class | Advanced (14 / 8). Basic gives 12 / 7 in any slot. **Reopened (§U.2):** under the cycle budgets ≈ 16–18 / 10 as Advanced and possibly 14 as Basic (estimates); decide from the spike and device run |
| C-D2 | Oscillator count | 14 plain / 8 PWM, set finally from the linked spike **and the device run (§U.6)** |
| C-D3 | Gate measure for a division-free voice | Instructions (established); the cycle proxy would allow ≈ 20. **Settled by S094 P2 (§U.2):** measured cycles per block with the probe |
| C-D4 | Wavetable frames | latched at each non-legato trigger (no mid-note steps) |
| C-D5 | "Spread" means frames within one table, or neighbouring tables | Frames within the selected table; neighbouring tables as a later mode (same cost) |
| C-D6 | PWM on a wavetable frame (a comb) | Not offered at first: `pwm` plays the saw-row pulse |
| C-D7 | The dense (truncated) option | Not at first; only if its host noise floor and the user's ear accept it |
| C-D8 | Gate source | `gat` + `lgt` (sequencer-native, as Alkaline). MIDI note-off release later |
| C-D9 | Filter coefficient smoothing | per-sample ramps (as measured) plus per-block cutoff slew |
| C-D10 | A sine wave | Not built in (no 1,024-point sine rows); a sine wavetable can be installed |
| C-D11 | Stereo pan law | balance (the FX return's stereo law), centre 63 |
| C-D12 | Level normalisation into the drive | 1/√N or 1/N, by ear |

---

## 13. Measurement record (2026-11-10)

- **Where:** scratch spikes outside the tree, not kept.
- **Flags:** `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
  -Ofast -std=gnu11 -ffunction-sections` (no LTO).
- **Counter:** `elf_looppath.py --cond-heads --in-range`, longest path.
- **Calibration:** the shipped table read measured 24 standalone (24
  linked); a copy of the mixer's mono dry loop measured 42.

| Spike | What | Loop |
|---|---|---:|
| `c1_accInterp` | one oscillator, integer interpolation, int32 accumulate | 15 |
| `c2_accTrunc` | one oscillator, truncated | 8 |
| `c3_accPairInterp` | two per pass | 26 |
| `c6_quadInterp` | four per pass | 61 |
| `c7_pairStore` | two per pass, store | 24 |
| `c8_pairTrunc` | two per pass, truncated | 12 |
| `c4_accPwm` / `c5_pairPwm` | PWM (two-read) single / pair | 23 / 43 |
| `c10_pairPwmShared` / `c9_pairPwmOwn` | PWM pair, B read at `φ − w`: one shared width / own width per oscillator | 43 / 44 |
| `f1_par` / `f4_parSpread` | two TPT SVFs, drive, one shared mix (spread outside / folded in) | 65 / 65 |
| `f5_par` | parallel, independent HP/BP/LP mixes, spread, amp ramp, two stores | 72 (7 stack refs) |
| `f2_one` | one SVF with drive, float in/out | 26 |
| `f3_ser` / `f6_ser` | series, shared / independent mixes | 57 / 64 |
| `s1_spread` | spread as a separate pass | 16 (folding it in is free) |
| `mx_mono` / `mx_stereo` | mixer dry path, DAC1 stereo routing: shipped mono form / stereo | 42 / 44 |
| sizes (`nm -S`) | pair 144 B, pair store 140 B, PWM pair 208 B, `f5_par` 528 B, `f6_ser` 476 B | |
| P8 image | `link_budget.py`: flash 710,836 / 753,664; ITCM 12,128 / 16,384. Alkaline engine symbols ≈ 14,460 B (`nm -S`) | |

**Oscillator pair sketch (the form measured as `c3_accPairInterp`):**

```c
for (i = 0; i < 32; i++) {
    uint32_t ia = p0 >> 22, na = (ia + 1u) & 0x3FFu;
    int32_t a = r0[ia], b = r0[na];
    int32_t s = a + (((b - a) * (int32_t)((p0 >> 6) & 0xFFFFu)) >> 16);
    ia = p1 >> 22; na = (ia + 1u) & 0x3FFu; a = r1[ia]; b = r1[na];
    s += a + (((b - a) * (int32_t)((p1 >> 6) & 0xFFFFu)) >> 16);
    acc[i] += s;  p0 += i0;  p1 += i1;
}
```

---

## 14. Statements this draft would make out of date (for the user to log, if adopted)

- `INSTRUMENTS_DSP_REFERENCE.md`:
  - the opening "six instrument voices";
  - §3.1 (the engine table, the union, the type limits);
  - §3.2 (the dispatcher list);
  - §5 (a new §5.6 engine; renumbering);
  - §8.2 (per-voice estimates);
  - §9.2 (the stereo slot accessor as a new checklist item).
- `MIXER_SYSTEM_DSP_REFERENCE.md`: one mono block per slot becomes mono or
  stereo; the stereo dry and send paths.
- `BANK_PRESET_ARCHITECTURE.md`, `FILESYSTEM_SPEC.md`: the sixth type, its
  token, extension and folder.
- `STORAGE_SRAM_MANIFEST.md`: the union member, ITCM growth, flash.
- `CPU_USE_DSP_AUDIT.md`: the Colloid's cost items; the frozen Advanced
  ceiling.
- `MEMORY.md`: the type list, the Alkaline-precedent notes on gates, and
  the mixer's mono-only assumption.
