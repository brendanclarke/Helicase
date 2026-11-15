# S095 P2 — Colloid, the Sixth Instrument (draft)

- **Status:** **ready to hand off** for evaluation and implementation
  (2026-11-12, after S095 P1 landed). **Start at §H.** It holds the tree
  state, what is decided, what is still open, the order of work, the gates
  and the deliverables.
  - History: written after S093 P8; updated for S095 in §U; revised
    2026-11-12 for the user's P2 brief (Basic class, a stereo position per
    oscillator with `spr`/`mot`, lightweight amp and filter ADSRs, the
    sustain switch, mixer-level pan, the 1,024-point sine as step 0);
    re-checked against the tree after S095 P1 (§H.2).
  - **Session-start check (2026-11-12, S095 P2):** §H.2 re-verified on
    `b7f7eec4`. Three questions for the user, **C-D19 to C-D21 (§H.7)**,
    must be answered before the schedule (§H.4 step 3). Two corrections
    are made in the body: the future note-off hook (§4) and the wave-row
    shape (§2.3, §8).
  - Where sections disagree, **§H wins, then §U, then the body.**
  - **No firmware code is changed by this document.** Figures marked *measured* come
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
- **Brief additions (user, 2026-11-12):**
  - **Basic class** for this implementation. A later expansion (insert FX
    or more oscillators) would be a separate Advanced type, as planned for
    the Alkaline;
  - **each oscillator has a stereo position.** With the filters in parallel
    (stereo), every oscillator is summed into L and R at its position;
  - **`spread` and `motion`** act on the oscillator positions only.
    `motion` sets both the amount of deviation and its rate. Lower-pitched
    oscillators sit closer to the centre and move less;
  - **an ADSR amp envelope, kept lightweight;**
  - **an ADSR filter envelope, kept lightweight** (same message).
- **Decisions (user, 2026-11-12, second round):**
  - **C-D8:** no gate-length parameter; instead an automatable **sustain
    on/off** (§4);
  - **C-D11:** the voice's pan is the mixer's own per-slot pan, applied to
    the stereo pair (§5);
  - **C-D10:** asked what a 1,024-point sine across the board would cost
    (answered in §12). **Decided (user, 2026-11-12): adopt it as the
    first P2 step, re-tested on its own gates;**
  - every other recommendation accepted.
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

## H. Handoff (2026-11-12, after S095 P1): start here

### H.1 What P2 is

- **The goal:** a sixth instrument type, **Colloid** (`col`). It is a
  **Basic**-class stack of free-running band-limited oscillators. Its
  pieces:
  - waves: Saw, Rec, Tri, Sin or user wavetable frames, or PWM;
  - pitch: intervals, detune and portamento;
  - a stereo position per oscillator, set by `spr` and `mot`;
  - two smooth TPT state-variable filters (series or parallel) with drive;
  - lightweight amp and filter ADSRs with a sustain switch;
  - a true stereo output into the mixer.
- **Step 0 (decided):** the 1,024-point sine across the board, as its own
  gated step (§12, C-D10).
- **The design is complete in the body** (§0–§9). Every user decision is
  recorded in §12. Implementation choices that are not user decisions
  are the implementer's (H.6).

### H.2 The tree as P2 finds it (measured 2026-11-12, after S095 P1)

| Item | Value | Source |
|---|---|---|
| Flash | **711,036 / 753,664 B**, headroom **42,628 B** (the link warning is at 16,384) | `make all` link budget |
| ITCM | **14,032 / 16,384 B** (2,352 free) | same |
| DTCM statics | 4,396 B | same |
| FX arena | 126,656 B (the S095 P1 prefix is inside it) | same |
| Instrument slot reserve | 2 × `sizeof(DrumVoice)` per slot (`INSTRUMENT_RUNTIME_SLOT_BYTES`, `InstrumentManager.c:202-232`) | source |
| Types today | Drum, Snare, Cymbal, HiHat, Alkaline: all **Basic** (no type is Advanced) | `InstrumentManager.h:89-90`, flags `:290-291` |
| Effect types | Off, StereoFilter, CrumpBit, **Delay, Reverb, Echo** (S095 P1); all three Space types are stereo in/out | `EffectsManager.h` |

**What S095 P1 changed that P2 touches** (see
`S095_P1_IMPLEMENTATION.md` §S18):

- **CPU budgets.** S095 P1 withdrew D19's Advanced re-budget. The shares
  are unchanged from S094 P2 §21:

  | Share | Cycles per block |
  |---|---:|
  | Basic, each | 19,620 |
  | Advanced, each | 21,803 |
  | Effect | 16,634 |
  | Shared mixer | 12,784 |

  §U.2's 21,635 and 16,970 are superseded.
- **The mixer's Effect return gains** now multiply by
  `effects_returnGain()` (`mixer.c:1244-1256`). This is the stopped-
  transport idle: an Effect whose output stays below −90 dBFS while the
  transport is stopped ramps out over 1 s and stops processing.
  - `effects_process()` is now **noinline** (`EffectsManager.c`), so the
    mixer's code keeps its shape.
  - The Colloid's stereo send path (§5) must leave both untouched. The
    `elf_p4` mixer gate (59 hot loops equal) is the check.
- **The interpolation slot lock** (P1 R5) is a phantom grant in
  `modNode_resetTargets()`. The Colloid uses no interpolation slots, so it
  is unaffected.
- **FxBuffer voice units:** none are claimed by any code. The Colloid
  claims none (no delay line).
- **Code anchors re-verified on this tree:**

  | Anchor | Location |
  |---|---|
  | `instrumentManager_typeSelectableForSceneSlot()` | `InstrumentManager.c:443` |
  | its Advanced test | `:461-463` |
  | `instrumentManager_ampEnvelopeQuiet()` | `:1572` |
  | the Alkaline trigger's `seq_trackStepSamples()` | **`:2274`** (was 2268) |
  | the `IM_SPECIAL_OSC_TYPE` writer | **`:3931`** (was 3925) |
  | `runtime_slots` | `:213` |
  | `osc_pwmReadS()` | `Oscillator.c:3785` |
  | `osc_pwmReadD()` | `:3809` |
  | `OSC_PWM_STORE` | `:3827` |
  | `sampleData` | `mixer.c:1074` |
  | `fx_active` | `:1099` |
  | the stereo return law | **`:1247-1256`** (with `fx_idle`) |
  | `mixer_addDataToOutput()` | `:375` |
  | `menu_instrumentLoadIndex[]` | `menu.c:1729` |
  | `TABLES` | `check_special_tags.py:21` |
  | `filesystem_trimInstrumentTreeBlocking()` | `filesystem.c:24251` |
  | the last named ITCM subsection, `.itcm.s094p1` | `STM32F765VIHx_FLASH.ld:182` |
  | `voiceControl_noteOff()` | `MidiVoiceControl.c:357` |
  | `seq_setRunningEx()` | `sequencer.c:6733` |
  | the 4,097-point `sine_table` | `wavetable.h:43`, `:54` (21 uses in `Oscillator.c`) |
  | the truncated LFO read | `lfo.c:94` |
  | `INTERPOLATE_OSC` 1 | `config.h:953` |
  | ON_OFF tags 1–3 used (4–15 free) | `MenuText.h:183`, `:213`, `:240` |

### H.3 Decided, and still open

- **Decided (user):**
  - C-D1 Basic;
  - C-D8 the sustain switch, with no length parameter;
  - C-D10 the 1,024-point sine as step 0;
  - C-D11 the mixer-level pan;
  - every other recommendation in §12: C-D4…C-D7, C-D9, C-D12…C-D18.
- **Decided (user, 2026-11-12, after the session-start check, §H.7):
  the recommendations accepted.**
  - **C-D19:** sustain holds only while the transport runs
    (`sus && seq_isRunning()`); stopped hits play A-D-R;
  - **C-D20:** the platform's `typ` (wav/wtb/pwm) / `wav` / `pos` shape,
    with one spread row `psp`;
  - **C-D21:** PWM is the Rec pulse only.
- **The schedule is written:** `S095_P2_IMPLEMENTATION.md`, verified in a
  scratch export of `b7f7eec4` (its §C). Where it departs from this draft,
  its §B says why, and it wins.
- **Open, decided by measurement (not by asking):** **C-D2, the oscillator
  count.** Spike and measure 12 plain / 6 PWM against Basic 19,620, falling
  back to 10 plain (§6.1).
- **Open, set by ear in the listening step** (draft laws in the body; the
  implementer sets and records them):
  - the `dtn` curve;
  - the `por` range;
  - the `mot` depth and rate law (C-D15);
  - the `w(f)` corners, 80 / 640 Hz (C-D14);
  - the `fam` range;
  - the `drv` range;
  - the level normalisation (C-D12);
  - the envelope time curve.
- **Approved (user, 2026-11-12), binding from the next session's start:**
  - **P2 flash line: ≤ +8 KB net** (8,192 B) against the 711,036 B build
    of §H.2, step 0's sine saving included. That leaves ≥ 34,436 B
    headroom. Each implementation step reports its running net total
    against this line.
    - Basis (*estimate*): the Colloid ≈ 10–13 KB (§7.2), less the sine's
      −6,144 B (measured table size, §12 C-D10), plus a margin.
    - The S095 P1 lesson: hold each phase to a line and measure it.
      S095 P1 met its flash only after it was split into phase lines.
  - **ITCM: ≤ +1 KB** (1,024 B) for the oscillator loops, against
    14,032 B, so ≤ 15,056 / 16,384 B. Any more needs a new sign-off.
  - **RAM:** 0 B new. The voice fits the slot reserve (§7.1; asserted).
  - **CPU:** the Colloid measured on the device as Basic (cold, its
    dearest: parallel, the chosen count, a stereo send) within 19,620
    cycles per block. On the host, its instruction totals are recorded as
    the regression baseline.

### H.4 Order of work, gates and deliverables

| Step | What | Gate (all must pass before the next step) | Deliverable |
|---|---|---|---|
| **0** | 1,024-point sine across the board (§12 C-D10): regenerate the table, shift 20 → 22 at the readers, LFO read | host goldens regenerated and passing (`oscillator`, `pwm`, `lfo`, `alkaline`, and the rest of `make -C tools/dsp_test all`); `elf_p4_check` against a **fresh snapshot** (the oscillator dumps change by the shift constant only, the loop counts do not); flash −6,144 B ± LTO noise; one listening check | the change and its records in `tools/dsp_test/baselines/s095p2/` |
| 1 | **Spike** (a scratch copy outside the tree, §U.6): the oscillator pair loops with the S term (§2.1) and the shipped PWM read, both filter loops, the stereo mixer functions against the `QADD` loops, a stub voice with the per-block positions and both envelopes | linked loop counts (`elf_looppath.py --cond-heads --in-range`); per-block path (`elf_blockpath.py`); the mixer gate (`elf_p4_check`, 0 grew); ITCM and flash | measured counts per mode; the **C-D2** proposal |
| 2 | **Device run** of the stub voice (one flash): cold, dearest, stereo send, with the probe | ≤ 19,620 cycles per block at the proposed count | C-D2 settled; the cycles per instruction recorded |
| 3 | **Schedule**: `S095_P2_IMPLEMENTATION.md`, citing every change by file and line, as `S095_P1_IMPLEMENTATION.md` | C-D19 to C-D21 answered (§H.7); the user approves it | the schedule |
| 4 | Registry and type (append-only), an empty voice | builds; `check_special_tags.py`; the Instrument Load lists `Colloid`; Scene save and load round-trip | |
| 5 | Oscillators and filters | host laws (§11 step 3); linked loop counts equal to the spike's | |
| 6 | Mixer stereo path | `elf_p4_check` mixer 0 grew; the stereo dry and send laws | |
| 7 | Parameters, pages, `init.col` | DEV self-check; host law: keys, labels, defaults, lanes | |
| 8 | PWM mode | host PWM law; the PWM count's cost measured | |
| 9 | Device: the Colloid as Basic (cold, dearest, stereo send); then a Scene of Colloids and Alkalines (the `cpu` widget, underruns); then listening (the by-ear laws, H.3) | ≤ 19,620; no underruns | the measured record; the by-ear values recorded |

### H.5 Tools and records

- **CPU, host:**
  - `tools/dsp_test/elf_looppath.py`, `elf_blockpath.py`,
    `control_cost.py --elf`;
  - S095 P1's `space_cpu.py` is a worked example of a type gate that
    counts frame loops at their iterations, per-block paths and loops
    inside per-block code. Use that method; S095 P1 found per-block loops
    worth up to 29 per frame that a loop-only count misses.
- **Mixer and oscillator regression:** `make -C tools/dsp_test
  elf_p4_check P4BASE=baselines/<dir>`. Snapshot with `elf_p4_snapshot`
  at P2's start.
- **Flash:** the link budget printed by `make all`, plus `nm -S` diffs
  against a saved ELF.
- **Records:** `tools/dsp_test/baselines/s095p2/`, not `build/`.

### H.6 Rules for the implementing session (user, standing)

- **Budgets are the implementer's responsibility.** A feature or law the
  implementer chose that costs too much is fitted or cut down by the
  implementer, then reported with measured figures. Ask the user only to
  change **their** decisions (H.3).
- **Report only measured numbers,** or estimates labelled with their
  basis. Give resource figures as one net total against the build P2
  starts from.
- **Code and memory:**
  - RAM and ITCM growth need sign-off. ITCM up to +1 KB is already
    approved for P2 (H.3); RAM stays at 0 B new;
  - every change carries an accurate comment block in the house style.
- **Documents and commits:**
  - `MEMORY.md` and `knowledge_files/` are never edited, and never in the
    same turn as other files;
  - commits are the user's;
  - nothing needed goes in `build/`;
  - no `.claude` auto-memory.
- **Device runs:** one flash per question.

### H.7 Session-start check (2026-11-12, S095 P2, tree `b7f7eec4`)

**Re-verified, unchanged:**

- **Link budget** (`tools/diagnostic_tools/link_budget.py` on
  `build/lxr02.elf`): flash 711,036 / 753,664 B; ITCM 14,032 / 16,384 B;
  DTCM statics 4,396 B; FX arena 126,656 B. These are §H.2's figures.
- **Slot reserve** (`nm -S`): `runtime_slots` is 7,584 B, which is
  6 × 1,264 B. So 2 × `sizeof(DrumVoice)` is still 1,264 B after S094.
- **`sine_table`** (`nm -S`): 8,194 B.
- **Every anchor in §H.2's table** is at the line given there.
- `seq_isRunning()` (`sequencer.h:522`) is the running-flag accessor that
  the S095 P1 Effect idle already reads from DSP code
  (`EffectsManager.c:2451`). The Colloid's stop edge (§4) reads it too.
- **A step with automation but no trigger still queues its entries**
  (`PATTERN_DYNAMIC_STACK.md` §6, "When a step's entries are queued").
  The step reset at the next trigger restores the base value. So a `sus`
  off lock on a step with no hit ends the note there, and the next note
  finds `sus` back at its base. C-D8 works as designed.

**Three questions for the user. C-D21 is needed before the step 1 spike;
all three are needed before the schedule (step 3):**

- **C-D19: a held note started while the transport is stopped.**
  - **The fact:** pads (`buttonHandler.c:1062-1079`), MIDI notes
    (`MidiNotes.c:90`) and the preview (`seq_previewVoice()`) all trigger
    through `seq_triggerVoice()`, running or stopped. MIDI note-off goes
    only to `buttonHandler_overlayMidiNoteOff()` (`MidiNotes.c:69-70`).
    Nothing reaches an engine.
  - **The consequence:** with `sus` on, a stopped-transport hit holds
    until one of three things happens: the next hit on that track, a menu
    `sus` off, or a later start then stop. §4's stop edge does not help,
    because the transport was never running. The Colloid is the first type
    that can hold without end. The Alkaline gate always ends, by step
    length.
  - **(a) Recommended:** sustain holds only while the transport runs. The
    effective switch is `sus && seq_isRunning()`, read once per block (the
    stop edge reads the flag already). 0 B and about 2 instructions per
    block. Stopped hits play A-D-R.
  - **(b)** A release path now. MIDI note-off and pad release would queue a
    release to the engine, drained in the main loop like the trigger ring.
    This touches `MidiNotes.c`, `buttonHandler.c` and `MidiVoiceControl.c`,
    and needs a few bytes of RAM (sign-off).
  - **(c)** As drafted: the note holds until one of the three events above.
- **C-D20: the shape of the `typ`, `wav` and `pos` rows.**
  - **The draft:** `typ` is `wav`/`pwm`. `wav` is one list (`Saw Rec Tri
    Sin w01..wNN`). `pos`/`wsp` are the frame centre and spread, and
    `pwd`/`pws` the width centre and spread: 8 OSC cells.
  - **The platform pattern (Alkaline, S093 P8 and S094 P4):**
    - `typ` is `wav`/`wtb`/`pwm`, named by `alkTypeNames` (tag
      `ONOFF_TAG_ALK_TYPE`, `MenuText.h:240-253`);
    - the `wav` list is keyed by `typ`. `instrumentManager_waveDomainMax()`
      (`InstrumentManager.c:852`) and `menu_formatWaveCell()`
      (`menu.c:12185`) each have a type branch. The LFO and velocity
      ranges, the menu clamp, the automation display and the `typ` switch
      clamp all follow from those two;
    - `pos` is the pulse width under `pwm` (Drum and Alkaline).
  - **Recommended: follow the platform pattern.**
    - `typ`: `wav`/`wtb`/`pwm`, reusing tag 3's names (no new tag).
    - `wav`: under `wav` it lists Saw Rec Tri Sin; under `wtb`, wNN; under
      `pwm`, the C-D21 forms.
    - `pos`: the frame centre under `wtb` (latched at the trigger, C-D4)
      and the width under `pwm` (read every block). It shows `---` under
      `wav`, where the oscillators are free-running.
    - One spread row, `psp` (`col_pos_spread`), spreads frames under `wtb`
      and widths under `pwm`.
    - The OSC page becomes `coa fin wav typ` / `pos psp`, with 2 cells
      free. Two fewer rows than the draft.
    - The helpers gain a Colloid branch beside the Alkaline's. This is the
      same work as the draft's, but the result keeps the existing meaning
      of `wav` and `pos` for LFO, Morph and automation. With the combined
      list, an LFO or Morph sweep of `wav` would cross from `Sin` into
      `w01`.
- **C-D21: the PWM forms.**
  - The Alkaline's `pwm` offers three: Saw (summed), Sqr (differenced) and
    Tri (warp).
  - The draft gives the Colloid one: the saw-row difference, a Rec pulse
    on the `oscPwmDRec` law.
  - Each extra form is its own loop set (pair store, pair add, single),
    about 500 B of ITCM. This is an *estimate*: §7.2 gives ≈ 900 B for
    all six loops, and the three plain loops are ≈ 360 B (from the
    measured 144 B and 140 B pairs). The PWM pair alone measured 208 B.
  - The +1 KB ITCM line holds the plain loops and **one** PWM form.
  - **Recommended:** Rec only. Under `pwm`, `wav` shows `Rec` as its one
    entry. A second form would need an ITCM sign-off above +1 KB.

**Corrections made in the body:**

- **§4, §4.2: the later MIDI note-off release cannot hook
  `voiceControl_noteOff()`.** `seq_triggerVoice()` calls it before every
  note-on (`sequencer.c:2126`; the preview at `:2180`), in TIM3 as well as
  the foreground. A release there would fire on every hit, and from an
  ISR. The release needs its own queued path (C-D19 (b)).
- **§2.3, §8:** the combined `wav` list and the `pwd`/`pws` rows are
  marked as pending C-D20.
- **§5: the pan law's consequence is recorded (C-D11 is unchanged).**

**For step 0 (the sine), checked in the tree:**

- **Readers:**
  - `Oscillator.c` has 21 `sine_table` references in `calcSineBlock()`,
    `calcSine()`, `calcFmSineBlock()`, `calcFmSine()`, the sync sine
    (`:746`), the ring sine (`:875`) and `osc_pwmReadS()` (`:3785-3791`);
  - the float readers change `>> 20`, `& 0xfffff` and `x 2^-20` to
    `>> 22`, `& 0x3fffff` and `x 2^-22`;
  - `osc_pwmReadS()` changes `ps >> 20` and `(ps >> 5) & 0x7FFF` to
    `>> 22` and `(ps >> 7) & 0x7FFF`. Its range comment "|b − a| <= 51"
    becomes **202** (measured on the decimated table); the product still
    fits int32;
  - `lfo.c:94` changes `>> 20` to `>> 22`.
- **Tables and their notes:** `wavetable.h:43` (`TABLESIZE`) and its
  notes, and `wavetable.c`'s data and its "top 12 bits" note.
- **Host:** `osc_host.h:37-38`. `tools/dsp_test/Makefile:1054` extracts
  the table from `wavetable.c`, so it needs no edit.
  `tools/dsp_test/spike/` is the frozen retry kit and is not edited.
- **The data:** the shipped table is not an exact formula. 4,005 of its
  4,097 entries differ from `round(−32767 cos)`, and entry 1,024 is 12.
  **Decimating it by 4** keeps every shared point. Any phase on a
  multiple of 2^22 then reads bit-identically to today. (A choice for the
  implementer, recorded here.)
- The linker script names `sine_table` in a comment only
  (`STM32F765VIHx_FLASH.ld:60`), with no placement.

**Integration sites (every Alkaline special case outside its engine; each
needs a Colloid case or a deliberate default):** `InstrumentManager.c`:

| Line | Function |
|---:|---|
| `:75` | registry |
| `:210`, `:226` | union, assert |
| `:295` | the runtime accessor |
| `:852` | `waveDomainMax` |
| `:1472` | `runtimeInstance` |
| `:1492` | `runtimeLfo` |
| `:1563` | `runtimeInit` |
| `:1592` | `ampEnvelopeQuiet` |
| `:1707` | `resetRuntimeSlotFromScene` |
| `:1825` | `runtimePan` |
| `:1857` | `runtimeVolume` |
| `:2088` | async |
| `:2125` | sync |
| `:2263` | `triggerTrack` |
| `:2395` | `osc` |
| `:2446` | `oscBySelector` |
| `:3868` | `IM_SPECIAL_INSTRUMENT_DRIVE` |
| `:3951` | `IM_SPECIAL_OSC_TYPE` |

Outside it: `menu.c:12185` (`menu_formatWaveCell()`) and
`storageTypes.h:142` (`STORAGE_INSTRUMENT_ALK`).

## U. S095 update (2026-11-11): what changed since this draft, and what the next session does

This draft was written on 2026-11-10, after S093 P8. S094 and the S095 P1
plan have changed the ground under it. **Where this section conflicts with
a figure or reference below, this section wins** (and §H wins over this
section: S095 P1 has since landed, §H.2). The superseded lines are marked
*(superseded, §U)*.

### U.1 Order of work and dependencies

- S095 runs P1 first: the Delay, Reverb and Echo Effects, then the user's
  compressor adjustments.
- P2 (this document) comes after. **As a Basic type (2026-11-12) it no
  longer depends on P1's Advanced budget.** The Basic share (19,620) is not
  touched by P1's D19.
- **Next session:** *(superseded by §H.4's order of work.)*
  1. Read U and `S095_P1_FX_DELAY_RVB_ECHO.md` §H, which has the standing
     rules, the tree state and the tools.
  2. Do the §11 spike, with U.2's measure.
  3. Bring the counts back to the user for C-D2 (the class is settled:
     Basic; every other decision is settled, and C-D10, the sine, is P2
     step 0).
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
| **Advanced ×2** | 21,803 *(S095 P1 withdrew D19's 21,635, §H.2)* |
| Effect | 16,634 *(D19's 16,970 withdrawn, §H.2)* |
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
- **Total:** about **18,600–19,600 against 21,803** as Advanced (the
  2026-11-10 14-oscillator form; the Colloid is Basic, §6.1).
- **The Colloid has no VDIV per sample** (the Alkaline's ladder has 3, at
  about 14 cycles each). Its cycles per instruction may therefore be lower
  than the Alkaline's.

**What follows (estimates):**

- **As Advanced,** there are about 2,000–3,000 cycles of room. That is
  roughly 2–3 more oscillator pairs, **about 16–18 plain or 10 PWM**.
- **As Basic** (19,620), 14 plain may already fit.
- **C-D1 is settled: Basic (user, 2026-11-12).** C-D2 (the count) is
  decided by the spike plus one device run against **Basic 19,620**.
- **With a stereo position per oscillator** (§2.6, about +2 per oscillator
  per sample), the Basic estimate is **≈ 10–12 plain or 6 PWM** (§6.3).

### U.3 The Advanced list today

- S094 P2 §22 moved Cymbal and HiHat from Advanced to Basic. No type is
  Advanced today.
- The Colloid is **not** Advanced (2026-11-12). The planned Advanced types
  are an Alkaline variant (wavefolders and tone, estimated 20,700–21,200)
  and, later, a Colloid expansion (insert FX or more oscillators) as its
  own registry type.
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
| Flash | headroom 42,828 B | *(2026-11-11: 691,100 used.)* **After S095 P1 (measured 2026-11-12): 711,036 used, 42,628 B headroom** (§H.2). The Colloid's 10–13 KB (*estimate*) less the sine's −6,144 B fits. |
| ITCM | 12,128 / 16,384 | **14,032 / 16,384 B** after S095 P1 (2,352 free). The Colloid's oscillator loops (≈ 1.0 KB) fit, leaving ≈ 1,350. |
| Mixer | dry loop 42 / stereo 44 (measured before `QADD`) | `QADD` is in the adds (S094 P2), so **re-measure** the mono and stereo dry and send loops. |
| Mixer anchors | | `sampleData` `mixer.c:1074` (`:860` is `mixer_calcNextSampleBlockTest()`'s); the stereo return law `mixer.c:1247-1256` after S095 P1 (it now multiplies by `fx_idle`, §H.2); `fx_active` `:1099` |
| D-cache | off | **On** (MPU region 0, write-through, non-shareable). The voice state in SRAM is cached. All earlier cycle reasoning predates this. |
| `IM_SPECIAL_OSC_TYPE` Alkaline branch | l. 3890 | **`InstrumentManager.c:3931`** (re-verified after S095 P1) |
| Verified unchanged | | type enum `InstrumentManager.h:89-90`; `runtime_slots` `InstrumentManager.c:213`; `ampEnvelopeQuiet` `:1572`; `seq_trackStepSamples(trigger_track)` `:2274` (moved 6 lines; the Colloid does not use it); ON_OFF tags 1–3 used (`MenuText.h:183`, `:213`, `:240`), 4–15 free |
| S095 P1 interplay | | P1 locks one of the two interpolation slots while an Effect feed runs (a phantom grant in `modNode_resetTargets()`). The Colloid uses no slots, so it is unaffected. Its stereo voice feeds stereo-input Effects; all three P1 types are stereo in. The Effect return carries P1's stopped-transport idle gain (§H.2). |

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
   - measured against Basic 19,620 (the Colloid's class; Advanced is
     21,803, §H.2);
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
- the balance law is at `mixer.c:1236-1247` *(after S095 P1: `:1247-1256`, §H.2)*;
- the noinline Alkaline renders are at `AlkalineVoice.h:243-248`;
- `menu_instrumentLoadIndex[]` is at `menu.c:1729`;
- `check_special_tags.py` `TABLES` is at `:21-32`;
- `filesystem_trimInstrumentTreeBlocking()` is at `filesystem.c:24251`;
- new ITCM code goes after `.itcm.s094p1`, now the last named subsection
  (`STM32F765VIHx_FLASH.ld:182`), not after `.itcm.s089p3`.

**Rechecked, unchanged:**

- `osc_pwmReadD()` `Oscillator.c:3809` and `OSC_PWM_STORE` `:3827`;
- `InstrumentManager.c`: `:213`, `:443`, `:461-463`, `:1572`, `:2268`,
  `:3925` *(after S095 P1: `:2274` and `:3931`, §H.2)*;
- the slot reserve is 2 × `sizeof(DrumVoice)` = 1,264 B
  (`InstrumentManager.c:195-232`);
- ON_OFF tags 1–3 are used, and 4–15 are free;
- `voiceControl_noteOff()` is at `MidiVoiceControl.c:357`.

**From the P1 review (`S095_P1_FX_DELAY_RVB_ECHO.md` §13, settled in P1
revision 5 as D24–D26):**

- None of them changes this draft.
- **D26:** no code claims FxBuffer voice units today, and the elastic
  seating keeps that path supported for later. The Colloid needs no units
  (§9 item 7).
- **D25:** the slot lock and the drain flag share
  `effects_state.reserved`. The Colloid uses no interpolation slots, so
  neither affects it.
- *(Superseded, §H.2: S095 P1 withdrew D19; Advanced stays 21,803 and
  nothing is taken from it for the feed.)*


---

## 0. Summary: what we can do

| Feature | Proposal | Cost basis |
|---|---|---|
| **Oscillators** | **≈ 10–12** band-limited oscillators (interpolated), or **6** with PWM, as **Basic** (2026-11-12; set by the spike, C-D2). Free-running, own phase each | Pairs measured at 26 per sample (13 each); a PWM pair 44 with its own width per oscillator; + ≈ 2 per oscillator for its stereo position (§2.6, *estimate*) |
| **Waves** | Saw / Rec / Tri (the stock band-limited octave rows), **Sin** (one 1,024-point row for every octave; C-D10, P2 step 0), or any installed user wavetable (`wNN`), one 1,024-point frame per oscillator, no interpolation | Same loop; only the row pointer differs |
| **Wavetable spread** | `pos` = centre frame; `wsp` = spread. Each oscillator takes a random frame offset that grows with `wsp`, drawn at the note (main loop) | 0 per sample |
| **Intervals / detune** | `int` picks an interval set (uni, oct, 5th, maj, min, sus, 7th …); `dtn` spreads the detune around each member | Per block only |
| **Portamento** | `por` glide time (log-pitch one-pole per block); `pmd` always / legato only | Per block only |
| **Filters** | Two linear TPT state-variable filters (Simper/Zavalishin form), LP/BP/HP each. Coefficients ramped per sample (smooth). A division-free cubic drive before each. Series (mono) or parallel (stereo: filter A left, B right) | **72** per sample parallel (the dearest), 64 series, 0 VDIV |
| **Stereo position** (2026-11-12) | Every oscillator has its own position. `spr` sets how far the positions spread, `mot` how far and how fast they move. Lower-pitched oscillators sit nearer the centre and move less. In parallel the oscillators sum into L and R at their positions; in series the voice is mono (§2.6) | A mid/side pair of buses: + ≈ 2 per oscillator per sample (one `SMLAW` each); positions per block |
| **Amp envelope** | ADSR, lightweight: per-block one-pole segments with a per-sample linear ramp. **No length parameter:** an automatable sustain switch `sus` holds the note at the sustain level until it is switched off (a step lock, Pattern automation, the menu, MIDI), the next trigger, or the transport stops; with `sus` off the envelope runs attack-decay-release. `lgt` ties legato notes (§4) | Ramp folded into the filter loop |
| **Filter envelope** (2026-11-12) | ADSR, the same lightweight form and the same sustain switch, with a bipolar amount `fam` on both cutoffs (§4.2) | Per block only: it moves the cutoff targets, which the filters already ramp per sample |
| **Output** | True stereo voice: a second int16 block, a stereo path in the mixer | Mixer dry path +2 per sample (measured), send ≈ +2 (*estimate*) |
| **Class** | **Basic** (user, 2026-11-12): any slot, any number per Scene, held to the Basic share of 19,620 cycles per block (§6.3). An Advanced expansion would be a separate type later | |
| **RAM** | **0 B new.** The voice (≈ 990 B, *estimate*), its oscillator buses and right-channel block all fit the existing 1,264 B slot reserve | |
| **ITCM** | ≈ 1.0 KB (the oscillator loops with the position term) | **Approved: ≤ +1 KB** (user, 2026-11-12; §H.3) |
| **Flash** | ≈ 10–13 KB (*estimate*; Alkaline's engine is ≈ 14.5 KB), less the sine's −6,144 B (step 0); **approved P2 line ≤ +8 KB net** (user, 2026-11-12; §H.3) | Headroom **42,628 B** after S095 P1 (measured, §H.2) |

**The two findings that make a large oscillator count possible:**
1. **Integer oscillators.** An interpolated table read accumulated into an
   int32 bus costs 15 alone and **13 each in pairs**. The shipped float
   loops cost 23–24.
2. **A division-free filter pair.** Two TPT SVFs with drive, the stereo
   spread, the amp ramp and both output stores cost **72** with 0 VDIV per
   sample. One ZDF filter costs 85–104 with 2 VDIV.

---

## 1. Signal path

```
N oscillators (free-running; own pitch, row, phase, stereo position p_i)
   every one -> bus M += s_i                (int32[32], mid)
             -> bus S += p_i s_i            (int32[32], side; p_i in -1..+1)
                       |
          +------------+---------------------------+
          | parallel (stereo)                       | series (mono)
          | L = M + S,  R = M - S                   | x = M  (S unused)
          | drive -> SVF A -> L                      | drive -> SVF A -> drive -> SVF B
          | drive -> SVF B -> R                      | L = R = out
          +------------+---------------------------+
             cutoffs A, B: knob + filter ADSR x fam (per block, ramped per sample)
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
- the two ADSRs' steps (amp, filter) and the sustain switch;
- every oscillator's stereo position (spread, motion, its pitch scaling);
- the drive gain.

---

## 2. The oscillator bank

### 2.1 The loop

- **State per oscillator:** `{ uint32_t phase; uint32_t inc; const int16_t
  *row; }`, 12 B.
- **The read:** the 1,024-point row read at `phase >> 22`, linearly
  interpolated in integer:
  `a + ((b − a) · ((phase >> 6) & 0xFFFF) >> 16)`.
- **The accumulation:** two oscillators per pass, added into the int32 mid
  bus M, and each sample x its position into the side bus S (2026-11-12,
  §2.6). The first pair **stores** both buses, so no clear pass is needed.
- **Copies:** separate copies for "pair store", "pair add" and "single"
  (for an odd count), plus the PWM forms. Each is in a new `.itcm.sNNN`
  subsection, `noinline, noclone` (§12 of the reference).
- **Mid/side buses (2026-11-12; replaces the even/odd buses E/O):** every
  oscillator adds to M; its sample times its position adds to S. Interval
  members are assigned by pair (`member = (i >> 1) mod M`), so each member
  has two oscillators to place.

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
| Pair add + position into S (*estimate*, 2026-11-12) | ≈ 30 | ≈ 15 |
| PWM pair + position into S (*estimate*) | ≈ 48 | ≈ 24 |

The position term is one `SMLAWB` / `SMLAWT` per oscillator: `S += (s x
p) >> 16`, with the pair's two Q15 positions packed into one register.
Add one `LDR`/`STR` of `S[i]` per pass (none in the storing pair). This
is about +2 per oscillator and costs one more live register, not two. The
spike measures it (§U.6).

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

- *(Pending C-D20, §H.7: the recommended shape is the platform's, with
  `typ` `wav`/`wtb`/`pwm` and the `wav` list keyed by `typ`. The
  combined list below is the draft's form.)*
- **`wav`** lists `Saw Rec Tri`, then `w01..wNN` (the installed
  wavetables). The stock rows are the band-limited `[11][1024]` tables.
  - A user wavetable frame is 1,024 points, 16-bit (S093 P8), not
    band-limited per octave, so high notes alias as on every other
    instrument's `wtb`.
  - **Sine (C-D10, decided: P2 step 0):** today the stock sine is a
    4,097-point table (`wavetable.h:54`, read at `phase >> 20`). Step 0
    makes it 1,024 points (+1 guard) for every instrument. `wav` then lists
    **`Saw Rec Tri Sin`, then `w01..`**; the Colloid plays `Sin` with the
    same loop, one row for every octave (a sine needs no band-limiting).
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

- `typ` = `wav` / `pwm` *(pending C-D20: `wav`/`wtb`/`pwm`, with `pos`
  and one spread row in place of `pwd`/`pws`; the forms are C-D21, §H.7)*.
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
  (4 pairs × 44) or 14 plain ones (24 + 6 × 26) are both budgeted *(the
  Advanced figures; as Basic with positions, 6 PWM or 10–12 plain, §6.1)*. This is
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

### 2.6 Stereo position, spread and motion (user, 2026-11-12)

**What it does.** Every oscillator has a stereo position `p_i` in −1..+1.
With the filters in parallel, the oscillators sum into L and R at their
positions, and filter A runs on L, filter B on R. In series the voice is
mono and the positions have no effect (C-D16).

**The law** (per block, per oscillator; no per-sample cost beyond the S
bus):

```
p_i = w(f_i) x clamp( spr' x h_i + mot_depth x m_i(t), -1, +1 )
```

- **`h_i`, the home position:** a constant table in −1..+1. Neighbours in
  detune order alternate sides and spread outward, so adjacent detuned
  copies never sit together. The two oscillators of an interval member get
  opposite sides.
- **`spr`, spread** (`col_spread`, 0..127): `spr' = spr / 127`. At 0 every
  oscillator is centred (a mono stack); at 127 the homes reach the edges.
- **`mot`, motion** (`col_motion`, 0..127; one knob for amount *and* rate,
  per the brief):
  - the depth rises with the knob: `mot_depth = 0.6 x (mot/127)^0.5`
    (draft law, set by ear);
  - so does the rate: from 0.05 Hz at 1 to 2 Hz at 127, log (draft law);
  - 0 is still.
- **`m_i(t)`, each oscillator's own movement:** a per-block LFO, its phase
  advanced by `rate x k_i`, where `k_i` comes from a constant table
  (0.71..1.37, irrational ratios). The oscillators therefore never move in
  step. The waveform is a parabolic sine (no table, no libm; C-D15).
- **`w(f_i)`, the pitch scaling (the brief: "lower-pitched oscillators
  closer to the centre and less motion"):**
  - computed from each oscillator's own frequency, the one its increment
    already uses;
  - 0 at or below 80 Hz, 1 at or above 640 Hz, log-linear between (three
    octaves; draft corners, C-D14);
  - it scales the home position *and* the motion, so a low note collapses
    the whole stack toward the centre, and a `sub` member sits nearer the
    centre than its octave partner.
- **The pan law:** mid/side, linear: `L = M + S`, `R = M − S`. Each
  oscillator lands at `(1 + p)/2` left and `(1 − p)/2` right. The two
  together are one multiply-add per oscillator, against two for a
  constant-power law (C-D13).
- **Smoothness:** `p_i` steps once per block (1,378 times a second). At
  `mot`'s top rate, 2 Hz and depth 0.6, one step is at most about 0.005 of
  the field, which is inaudible. No per-sample ramp is needed.
- **Cost:**
  - per sample, ≈ 2 per oscillator (§2.1);
  - per block, ≈ 12 per oscillator (phase step, parabola, `w`, clamp,
    pack), about 0.4 per sample each.

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
  - `par`: A on L = M + S, B on R = M − S, at the oscillators' positions
    (§2.6);
  - `ser`: A → B on M, output on both sides; `spr` and `mot` have no
    effect (the page shows `---`).
  - Both modes run both filters every block (constant cost); `par` is the
    dearest and is budgeted.

| Filter loop (*measured*; includes drive, the even/odd spread, the amp ramp and both int16 stores; with M/S the spread becomes L = M + S, R = M − S, the same two adds) | Per sample | Stack refs |
|---|---:|---:|
| Parallel, independent HP/BP/LP mix per filter | **72** | 7 |
| Series, independent mixes | 64 | 4 |
| (Parallel with one shared mix: the first spike) | (65) | |

The parallel loop spills (7 stack references). A tuned layout may bring it
down; ramping only `a1` and deriving `a2`/`a3` is one candidate (*not
measured*).

---

## 4. The envelopes and the gate: how sustain works

**Nothing today tells an instrument that a note has ended.**
- Steps have no length.
- `voiceControl_noteOff()` (`MidiVoiceControl.c` l. 357) only drops queued
  triggers and clears `active_voices`; nothing reaches an engine.
- Alkaline solves this with a **gate**: 7/12 of the triggering track's step
  (`seq_trackStepSamples()`), counted down 32 samples per block.
- **The Colloid has no length parameter (user, C-D8, 2026-11-12).** Its
  envelopes hold or not by an automatable **sustain switch**, `sus`. A
  step lock or a Pattern automation write turns it off on the step where
  the note should end.

| Row | Meaning |
|---|---|
| `atk dec slv rel` | Amp ADSR (lightweight, §4.1). Times ≈ 1 ms .. 10 s; `slv` is the sustain level, 0..1. The block value is ramped per sample in the output loop (measured, inside the 72) |
| `fat fde fsl fre` / `fam` | Filter ADSR (user, 2026-11-12; §4.2), the same form and the same switch; `fsl` its sustain level; `fam` a bipolar amount on both cutoffs |
| `sus` | **Sustain on/off** (`DTYPE_ON_OFF`, automatable per step and by Pattern automation; not morphed). It acts on both envelopes. **On:** after the decay each envelope holds at its sustain level until `sus` turns off, the next non-legato trigger, or the transport stops. **Off:** attack → decay → release, with no hold (the release starts when the decay has come within 1 % of its sustain level). Turning `sus` off while a note holds starts both releases on that block |
| `lgt` | Tie (an on/off row, step-automatable like Alkaline's `sld`). A trigger that arrives while the note is still held (not yet in release), on a step with `lgt` on, is **legato**: neither envelope restarts, and the pitch glides (with `pmd` `leg`, only these notes glide) |

- **Retrigger** starts both attacks from their current levels (no click).
- **Transport stop releases** a holding note. A stop runs in the
  sequencer's context (`seq_setRunningEx()`, `sequencer.c:6733`; MIDI Stop
  arrives through TIM3), and today it reaches no engine
  (`voiceControl_noteOff(0xFF)` only drops queued triggers). So the
  Colloid reads the running flag once per block and starts both releases
  on the running → stopped edge. The previous flag is 1 B inside the voice
  (the slot reserve): no new RAM, no ISR-to-engine call. Without this a
  sustained note would drone until the next trigger.
- **And the Effect after it (S095 P1, §H.2):** after a stop the Effect
  keeps processing until its output stays below −90 dBFS, so the
  Colloid's release tails into a reverb or delay are heard in full before
  the Effect idles.
- **A hit while the transport is stopped** (a pad, a MIDI note, the
  preview) has no stop edge to release it. With `sus` on it would hold
  until the next hit. **C-D19 (§H.7)**; recommended: `sus` holds only
  while the transport runs.
- **MIDI note-off, later:** the same release, through its **own queued
  path** when a keyboard path is added. *(Corrected 2026-11-12, §H.7: not
  `voiceControl_noteOff()`, which `seq_triggerVoice()` calls before every
  note-on, `sequencer.c:2126`, from TIM3 as well.)*

### 4.1 Lightweight ADSR (both envelopes; user, 2026-11-12)

- **One routine, two instances:** `col_egStep(eg, rows)`, per block only.
- **State per envelope:** `{ float level; uint8_t stage; }` (8 B with
  padding).
- **Per block:** `level += k[stage] x (target[stage] − level)`. There is
  one per-block coefficient per time row, from a 128-entry generated table
  (`col_egK[r] = 1 − exp(−32 / (T(r) fs))`), so there is no `exp`,
  division or per-sample work in the envelope itself.
- **The stages:**
  - **attack:** aims at 1.2 and stops at 1.0 (the analogue curve, a finite
    attack time);
  - **decay:** aims at the sustain level;
  - **sustain:** holds while `sus` is on;
  - **release:** aims at 0, until idle under 1e-4.
- **Release starts:** when `sus` turns off, at a transport stop, or (with
  `sus` off) when the decay settles; both envelopes together.
- **Cost:** about 10 instructions per block each, under 1 per sample for
  both.
- **The amp envelope's smoothness:** the block value ramps linearly per
  sample in the output loop (already in the measured 72).
- **The filter envelope needs no per-sample ramp:** it moves the cutoff
  targets, and the filters already ramp their coefficients per sample
  (§3, C-D9).

### 4.2 Filter envelope (user, 2026-11-12)

- **Rows:**
  - `fat fde fsl fre`: attack, decay, sustain level, release, with the amp
    envelope's laws;
  - `fam`: the amount, bipolar, 63 = none, up to ±6 octaves at the ends
    (draft range, by ear).
- **Target:** both cutoffs, in the log (index) domain of the cutoff law:
  `cut_x' = cut_x + fam' x env x 72 semitones`, clamped to the law's range.
  Filter A and B keep their own knob offsets, and the envelope moves them
  together (C-D17).
- **Sustain switch, legato and retrigger:** as the amp envelope (§4).
- **Cost:** per block only. It changes the cutoff indices before the
  per-block coefficient calculation, which runs every block anyway (4 VDIV
  for both filters, §3). 0 per sample.
- **Quiet test:** `Colloid_isQuiet()` = envelope idle and level < 1e-4,
  the `instrumentManager_ampEnvelopeQuiet()` threshold. It needs a branch
  there (l. 1572), as Alkaline has.
- **Track 7's alternate hit:** `instrumentManager_applySlot6AlternateDecay()`
  needs a `SlopeEg2` and returns early without one (as for Alkaline). A
  Colloid in slot 6 plays the alternate hit as an ordinary hit. No change is
  needed.
- **Later option (not proposed now):** a MIDI note-off release path
  (a queued release, drained in the main loop → `instrumentManager` →
  `Colloid_release()`; not `voiceControl_noteOff()`, §H.7) for keyboard
  play. It drives the same release as `sus` off. *(The
  former `hold` gate option is what `sus` now is, C-D8.)*

---

## 5. Stereo output and the mixer

The mixer renders each slot into one mono stack block (`sampleData`,
`mixer.c:1074`). Colloid adds a right channel without new RAM:

| Item | Proposal |
|---|---|
| Right block | `int16_t right[32]` **inside `ColloidVoice`** (in the slot reserve) |
| Accessor | `instrumentManager_slotRight(slot)`: the Colloid's block, or NULL for every other type |
| Mixer slot loop | after `instrumentManager_calcSlotSyncBlock()`: `right ? stereo path : existing path`, one pointer test per slot per block. The existing calls keep their arguments |
| Dry path | new noinline `mixer_addVoiceStereoToOutput()`: L × `panL` → left bus, R × `panR` → right bus, where `panL`/`panR` are **the mixer's own per-slot pan gains** (`squareRootLut`, the gains `mixer_addDataToOutput()` takes at `mixer.c:375`), exactly as for every mono voice (C-D11, user 2026-11-12). Single-side routes (`DAC1_L` …) take (L+R)/2 × the side's gain |
| FX send | new noinline `mixer_addVoiceStereoToOutputAndFx()`: a stereo-input Effect gets L/R; a mono-input Effect gets (L+R)/2 |
| SRR | none (`instrumentManager_slotUsesSrr()` default 0; brief) |
| `pan` row | **The mixer-level pan, unchanged** (user, C-D11): the platform `instrument_pan` row (`DTYPE_PM63`, 63 = centre) and the mixer's existing law. The Colloid adds no pan row and no stereo law. Its own stereo picture is set by `spr`/`mot` (§2.6); the mixer pan places that picture |

- **What C-D11 means in level (2026-11-12, from `mixer.c:1185-1196`):**
  - the mixer's gains are constant-power, `squareRootLut[127 − pan]` /
    `squareRootLut[pan]`;
  - at the centre, L and R each take the same gain a mono voice gets. A
    Colloid at `spr` 0 (L = R) is therefore exactly as loud as a mono
    voice;
  - towards an edge it acts as a balance: at a hard pan the far channel,
    and the oscillators placed on that side, are dropped.
  - The Effect return uses a different law for its stereo output (unity at
    the centre, `mixer.c:1246-1250`). It is not used for the voice, so
    that a Colloid matches the mono voices at the centre.
- **Cost (*measured*, before `QADD`; re-measure, §U.5):** the shipped mono dry loop (DAC1 stereo case) is
  42; the stereo form is **44** (+2). The send is about the same again
  (*estimate* +2).
- **S095 P1 (§H.2):** the Effect return's gains now multiply by
  `effects_returnGain()`, and `effects_process()` is noinline. The stereo
  send leaves both as they are: it only fills the FX bus, and the Effect,
  its idle and its return follow unchanged. A stereo send into a
  mono-input Effect (StereoFilter in mono, CrumpBit's mono sum) takes
  (L+R)/2, as the row above says.
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

### 6.1 Composition (Basic, dearest mode = parallel; revised 2026-11-12)

**As Basic, with a position per oscillator and both envelopes (*estimates*;
the spike measures them):**

| Part | 12 plain | 10 plain | 6 PWM | Basis |
|---|---:|---:|---:|---|
| Oscillators with the S term | 27 + 5 × 30 = **177** | 27 + 4 × 30 = **147** | 46 + 2 × 48 = **142** | §2.1 table |
| Filters, drive, M/S to L/R, amp ramp, stores | 72 | 72 | 72 | measured (§3) |
| Mixer stereo delta (dry + send) | ≈ 4 | ≈ 4 | ≈ 4 | §5 |
| Per block / 32 (oscillator control, positions, both envelopes, filter coefficients) | ≈ 26 | ≈ 24 | ≈ 20 | below |
| **Total** | **≈ 279** | **≈ 247** | **≈ 238** | |
| Reference: the Alkaline's worst, 276.9 instructions = 19,609 cycles with its send, against Basic 19,620 | | | | S094 P2 §18 |

- **10 plain / 6 PWM fit Basic with room** on the instruction proxy.
- **12 plain sits at the line.** The Colloid has no per-sample VDIV (the
  Alkaline has 3), so its cycles per instruction may be lower and 12 may
  fit. The device run decides (C-D2).
- **Per block:** the draft's ≈ 700 for 14 oscillators (below), less ≈ 18
  per oscillator not built, plus ≈ 12 per oscillator for its position,
  plus ≈ 20 for the two envelopes. That is ≈ 830 for 12 (≈ 26 per sample)
  and ≈ 640 for 6 PWM.

**The superseded Advanced composition (2026-11-10), for reference:**

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

### 6.3 Advanced or Basic *(settled 2026-11-12: Basic; the table below is the 2026-11-10 assessment, superseded by §6.1)*

| Class | Plain | PWM | Dense (truncated, *estimate*) | Placement |
|---|---:|---:|---:|---|
| **Advanced** (≤ 292.9) *(superseded, §U.2/U.3, §H.2: 21,803 cycles; no Advanced type today)* | **14** | **8** | ≈ 28 | at most two per Scene, shared with Cymbal/HiHat (`instrumentManager_typeSelectableForSceneSlot()`, `InstrumentManager.c` l. 443) |
| Basic (≤ 261.3, the Drum ceiling) *(superseded, §U.2: 19,620 cycles)* | 12 (≈ 250) | 7 (≈ 244) | ≈ 22 | any slot, any number |

- **The gap is small** (+2 / +1 oscillators). The filter pair, the mixer
  delta and the per-block work cost the same in either class.
- ~~Recommended: Advanced~~ **Decided: Basic** (user, 2026-11-12). The
  Advanced expansion (insert FX or more oscillators) becomes a separate
  type later, as for the Alkaline.
- Basic is a real alternative if two Colloids plus Cymbal/HiHat in one
  Scene matters more (C-D1).
- A type cannot be both (`:460-462` of the same file).

### 6.4 Constant-cost check

| Choice | Cost |
|---|---|
| `fmd` series / parallel | both filters run; parallel (72) budgeted |
| Filter type LP/BP/HP/notch | mix coefficients; same cost |
| `typ` `wav` / `pwm` | the plain count (12 or 10) × ≈ 15 or 6 PWM × ≈ 24 with positions (§6.1): both budgeted |
| Stock row / wavetable frame | same loop; the row pointer differs |
| Spread, intervals, detune, glide | per block |
| `spr`, `mot` (positions) | per block; the S term runs every sample in both filter modes (in series its bus is ignored) |
| Filter envelope, `fam` | per block (cutoff targets) |
| Silence, `sus` off or released | the voice renders in full every block |

---

## 7. Memory

### 7.1 RAM: inside the existing slot reserve

`runtime_slots[6]` reserves 1,264 B per slot (2 × `sizeof(DrumVoice)`;
`InstrumentManager.c:195-232`). A `ColloidVoice` adds a union member and
an assert. It does not grow the union.

| Member | Bytes (*estimate*) |
|---|---:|
| `OscInfo osc` (the pitch anchor, the `osc1_*` rows) | 72 |
| 12 × `{phase, inc, row}` (2026-11-12: Basic, at most 12) | 144 |
| Per-oscillator spread draws (frame, width) | 24 |
| Per-oscillator position: `p_i` (int16, packed in pairs) + motion phase (uint32) | 72 |
| Buses M and S, `int32_t[2][32]` (were E and O) | 256 |
| Right output block, `int16_t[32]` | 64 |
| Two filter states and ramps (`ic1 ic2 a1..a3 da1..da3 mH mB mL k`) | ≈ 112 |
| Amp and filter ADSR, the sustain switch's state and the running-flag edge, glide, amp ramp | ≈ 56 |
| Parameter members (≈ 30 floats/bytes) | ≈ 100 |
| `Lfo lfo` (the platform pair) | 124 |
| Wavetable bind (pointer, frames, index, generation) | 16 |
| **Total** | **≈ 1,060** (≤ 1,264; ≈ 200 to spare; 2026-11-12 with positions and the filter envelope) |

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
ITCM reserves if needed. *(Superseded, §U.5, §H.2: ITCM is 14,032 /
16,384 B after S095 P1; ≈ 15.0 KB with the Colloid's ≈ 1.0 KB.)*

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
| 0 OSC | `coa fin wav typ` / `pos wsp pwd pws` *(C-D20 recommended: `coa fin wav typ` / `pos psp`)* | `osc1_pitch_coarse`, `osc1_pitch_fine`, `osc1_wave`, `osc1_type`, `osc1_position`, `col_wt_spread`, `col_pw_width`, `col_pw_spread` *(C-D20 recommended: the first five, then `col_pos_spread`)* |
| 1 AEG | `atk dec slv rel` / `sus lgt` | `col_attack`, `col_decay`, `col_sustain_level`, `col_release`, `col_sustain` (on/off, C-D8), `col_tie` |
| 2 mod | `dst amt` / `por pmd` | velocity rows; `col_glide`, `col_glide_mode` |
| 3 FM (unison) | `int dtn spr mot` | `col_interval`, `col_detune`, `col_spread`, `col_motion` (2026-11-12; `ste` removed) |
| 4 click → FENV | `fat fde fsl fre` / `fam` | `col_fenv_attack`, `col_fenv_decay`, `col_fenv_sustain_level`, `col_fenv_release`, `col_fenv_amount` (2026-11-12) |
| 5 FILTER | `cta rsa tya fmd` / `ctb rsb tyb drv` | `col_cut_a`, `col_res_a`, `col_type_a`, `col_filt_mode`, `col_cut_b`, `col_res_b`, `col_type_b`, `col_drive` |
| 6 LFO | the Drum layout (12 cells) | platform LFO rows |
| 7 MIX | `vol pan` | `instrument_vol`, `instrument_pan` |

- About 48 rows (2026-11-12: + `mot` and the five filter-envelope rows,
  − `ste`).
- Continuous rows are `MORPHABLE | MODULATABLE | AUTOMATABLE`, with the
  conversion done at control rate in the async, as Alkaline's float rows.
  So **no new special writers** are needed.
- The selectors (`typ`, `int`, `fmd`, filter types, `pmd`, `lgt`) are
  `SELECTOR`: automatable per step, never morphed.
- `typ` reuses `IM_SPECIAL_OSC_TYPE` with a Colloid branch, as the Alkaline
  branch at `InstrumentManager.c:3931`.
- `pos` and `wsp` are read at the trigger (§2.3). `pwd`, cutoffs,
  resonance, drive, detune, `spr`, `mot`, both envelopes' rows and `fam`
  are read every block (smooth through the ramps and slews).

---

## 9. Platform integration (`INSTRUMENTS_DSP_REFERENCE.md` §9.2, item by item)

| §9.2 item | Colloid |
|---|---|
| 1 Enum and registry | `INSTRUMENT_TYPE_COL` after `INSTRUMENT_TYPE_ALK`, before `UNKNOWN` (`InstrumentManager.h` l. 89–90); a designated row `"col"`, `".col"`, label `"Colloid"` (7 cells; Load Morph shows `ColloMrp`), directory `"Colloid"` (7 characters), **`INSTRUMENT_FLAG_BASIC`** (2026-11-12; a type with no class flag is not selectable, S094 P2 §22). Type-sized arrays grow by one (e.g. `menu_instrumentLoadIndex[]`, `menu.c:1729`: +2 B, `.bss`) |
| 2 Files | `Core/DSP/Instruments/Colloid/ColloidVoice.c/.h`, `ColloidParameters.c/.h`, and a control/table file if needed. **Templates:** the Alkaline (`Core/DSP/Instruments/Alkaline/`: `AlkalineVoice.c/.h` for the noinline renders and the wavetable bind, `AlkalineParameters.c/.h` for the rows and pages, `AlkalineControl.c/.h` for the control-rate conversions, `AlkalineTables.c/.h` for generated tables) |
| 3 Runtime | union member + assert (`:195-232`); `instrumentManager_colloidRuntime()`; a case in every §3.2 dispatcher (the full site list, from the Alkaline's cases, is in §H.7); `ampEnvelopeQuiet()` branch (l. 1572); **no** `seq_trackStepSamples()` argument (no length parameter, C-D8; the Alkaline's call at `:2274` is untouched); `instrumentManager_slotRight()` (new) |
| 4 Build | include path; parameters in `SRCS`; voice in `DSP_SRCS` with an explicit `-Ofast` rule (the pattern rule covers only `Core/DSPAudio/`) |
| 5 Host proof | `check_special_tags.py` `TABLES['COL']` (`:21-32` hold the five today); the tag gate; an `init.col` generator and check, after `tools/alkaline/make_init_alk.py` and its `tools/dsp_test/Makefile` target; a host module `tools/dsp_test/modules/colloid.c` after `modules/alkaline.c` (the extracted-source pattern of `make alkaline`) |
| 6 Storage | append-only token (the `.hcnames` header accepts an older header as a leading part); AutoSave stores `col`; `SD_CARD/Instrument/Colloid/init.col` (as `SD_CARD/Instrument/Alkaline/init.alk`). The boot directory trim enumerates every `/Instrument` subdirectory (`filesystem_trimInstrumentTreeBlocking()`, `filesystem.c:24251`), so the new folder is covered |
| 7 Class | **Basic** (user, 2026-11-12); no FxBuffer voice units needed (no delay line) |

---

## 10. Risks and unknowns

| Risk | Handling |
|---|---|
| The mixer's inlined loops move when the stereo branch is added | noinline voice and stereo functions; the mixer gate is a pass condition; spike first |
| The per-block estimate (≈ 700) is off | `elf_blockpath.py` / `control_cost.py`-style count in the spike; the oscillator count is set from the measured total |
| The parallel filter loop's spills grow when linked | measured linked; layout candidates (§3) |
| The S term (position) spills in the oscillator pair loops (2026-11-12) | the pair's two positions packed in one register (`SMLAWB`/`SMLAWT`); measured in the spike; if it spills, fewer oscillators (C-D2) |
| A Basic Colloid at 12 oscillators sits at the Basic line | the device run decides 12 or 10 (C-D2); Basic is held to 19,620 like the Alkaline |
| Summing up to 12 sources: level and headroom into the drive | per-block normalisation (1/√N, C-D12); host level law |
| The Advanced ceiling is not frozen *(superseded, §U.2, §H.2: the gate is the measured cycle budget; the Colloid is Basic, 19,620)* | measure the stub voice with the probe (§U.6) |
| User wavetables alias at high notes | as every `wtb` today; noted, not fixed |
| The sound of the integer interpolation and the dense option | host laws (alias/noise floor against an oversampled reference), then the user's ear |

---

## 11. Proposed next steps

*§H.4 is the order of work with its gates; this section keeps the detail
of each step.*

1. **Spike (linked, outside the tree):**
   - the oscillator loops (with the S term, §2.1), both filter loops, the
     stereo mixer functions and a stub voice with the per-block positions
     and both envelopes, in a scratch copy of the tree;
   - gates: linked loop counts, per-block path, `elf_p4_check` (mixer
     0 grew), ITCM/flash.
   - **Output:** the oscillator counts per class and mode.
2. **Freeze the Advanced ceiling** as a baseline file. *(Superseded: see §U.6.)*
3. **Host model and laws:**
   - filter stability under modulation and coefficient ramps;
   - ADSR smoothness (no step larger than the ramp allows);
   - the sustain switch: `sus` on holds, `sus` off on a step starts both
     releases on that block, `sus` off runs A-D-R, a transport stop
     releases, legato (`lgt`) never restarts;
   - interval and detune ratios;
   - positions: mono at `spr` 0 and `mot` 0; `w(f)` at 80 / 640 Hz; motion
     steps per block below the audible bound;
   - both envelopes: stage timing against the tables; no step above the
     ramp; legato does not restart;
   - level normalisation;
   - the dense option's noise floor.
4. **Schedule**, then implementation in this order:
   - **step 0 (C-D10, decided):** the 1,024-point sine across the board.
     It has its own gates: the host goldens regenerated and passing,
     `elf_p4_check` against a fresh snapshot (the shift constant only), the
     flash saving measured, and one listening check. It is done and gated
     before the Colloid's first step;
   - registry/type (append) and an empty voice;
   - oscillators and filters;
   - mixer stereo;
   - parameters/pages and `init.col`;
   - PWM.
5. **Device:** the Colloid measured as a Basic type (cold, dearest:
   parallel, 12 or 10 oscillators or 6 PWM, a stereo send) against 19,620;
   then a Scene of Colloids and Alkalines (`cpu` widget, underruns); then
   listening.

---

## 12. Decisions (revised 2026-11-12)

**Settled:**

| # | Question | Decision |
|---|---|---|
| C-D1 | Class | **Basic** (user, 2026-11-12). An Advanced expansion is a separate type later |
| C-D3 | Gate measure | Measured cycles per block with the probe (S094 P2, §U.2) |

**The rest (user, 2026-11-12): recommendations accepted,** except C-D8 and
C-D11 (settled differently, shown). C-D10 is settled as step 0. C-D2 is
still set by measurement.

| # | Question | Recommendation |
|---|---|---|
| C-D2 | Oscillator count as Basic | Spike and measure **12 plain / 6 PWM**, falling back to **10 plain** if 12 does not fit 19,620 (§6.1). Set from the device run; not a taste question |
| C-D4 | Wavetable frames | Latched at each non-legato trigger: no mid-note steps (the brief: no modulation) |
| C-D5 | "Spread" (`wsp`): frames within the selected table, or neighbouring tables | Frames within the selected table; neighbouring tables as a later mode (same cost) |
| C-D6 | PWM on a wavetable frame (a comb) | Not offered: `pwm` plays the saw-row pulse |
| C-D7 | The dense (truncated-read) option | Drop it. It was a way to double the count as Advanced; as Basic the count is set by C-D2 |
| C-D8 | Note end | **Settled (user):** no length parameter; an automatable sustain switch `sus` (on: hold until off, the next trigger or transport stop; off: A-D-R). `lgt` ties legato notes. MIDI note-off later drives the same release |
| C-D9 | Filter coefficient smoothing | Per-sample ramps (as measured) plus a per-block cutoff slew; the filter envelope rides on it |
| C-D10 | A sine wave | **Settled (user, 2026-11-12):** a 1,024-point sine across the board, as **step 0 of the P2 schedule**, re-tested on its own gates before any Colloid work (C-D10 below) |
| C-D11 | The voice's pan | **Settled (user):** the mixer-level pan, unchanged: the mixer's per-slot `panL`/`panR` applied to L and R (§5) |
| C-D12 | Level normalisation into the drive | 1/√N (incoherent detuned oscillators sum by power), by ear |
| **C-D13** | The per-oscillator pan law (2026-11-12) | **Linear mid/side** (`L = M + S`, `R = M − S`): one multiply-add per oscillator. Constant-power needs two (+ ≈ 1 per oscillator) for about 3 dB more width at the centre, which a stack of many oscillators does not need |
| **C-D14** | `w(f)`, the pitch scaling of position and motion | By each oscillator's **absolute frequency**: 0 at or below 80 Hz, 1 at or above 640 Hz, log-linear between. A low note centres the whole stack and a `sub` member sits inside its partner. The corners are draft values, set by ear |
| **C-D15** | `mot`: the one-knob law and the movement shape | Depth 0.6 × √(mot/127) and rate 0.05 → 2 Hz (log) rising together; each oscillator at its own rate ratio (0.71..1.37); a parabolic-sine shape; per-block steps (no ramp). The alternative is a smoothed random walk per oscillator: more organic, same cost |
| **C-D16** | `spr` / `mot` in series (mono) filter mode | No effect, shown `---` (the filter chain is mono). The alternative, positions after the filters, would need two series chains: Advanced-class cost |
| **C-D17** | Filter envelope routing | One bipolar `fam` on **both** cutoffs, A and B keeping their knob offsets. The alternative, separate amounts per filter, costs one row and nothing per sample |
| **C-D18** | Envelope rows shared with velocity | Velocity reaches the envelopes only through the existing velocity `dst` targets (the amp level, `fam`); no new velocity rows |

**Decided (user, 2026-11-12: the recommendations accepted; the detail is
in §H.7):**

| # | Question | Recommendation |
|---|---|---|
| **C-D19** | What releases a held note started while the transport is stopped (a pad, a MIDI note, the preview) | `sus` holds only while the transport runs (`sus && seq_isRunning()`, per block; 0 B). Stopped hits play A-D-R. A queued note-off release path comes later |
| **C-D20** | The `typ` / `wav` / `pos` row shape | The platform pattern: `typ` `wav`/`wtb`/`pwm` (tag 3's names); `wav` keyed by `typ`; `pos` = the frame centre (`wtb`) or the width (`pwm`); one spread row `psp`. 6 OSC cells, not 8 |
| **C-D21** | The PWM forms | Rec (the saw-row difference on `oscPwmDRec`) only. Each further form is ≈ 500 B of ITCM (*estimate*, §H.7), above the +1 KB line |

### C-D10: a 1,024-point sine across the board (the user's question)

**Today:**

- `sine_table` is 4,097 points (`TABLESIZE` 4096 + 1 guard,
  `wavetable.h:43`, `:54`), 8,194 B of flash;
- it is read at `phase >> 20` in about 21 places in `Oscillator.c`
  (`calcSineBlock()` and the FM, ring and sync forms; the PWM sine warp's
  `osc_pwmReadS()`, `:3785-3791`) and once in `lfo.c:94`;
- `INTERPOLATE_OSC` is 1 (`config.h:953`), so every oscillator read is
  linearly interpolated; the LFO's read is truncated.

**What changes with 1,025 points (1,024 + guard):**

- the readers shift by 22 instead of 20, and the fraction takes the next
  two bits. That is the same instruction count, so it is cost-neutral;
- the PWM sine warp's split points (`oscPwmSinW[]`) are 32-bit phase
  values, not table indices, so they do not change.

| | Effect |
|---|---|
| **Accuracy, interpolated (every oscillator)** | Linear interpolation of a sine on N points errs by at most (2π/N)²/8: **−106.5 dB at 1,024** against −130.6 dB at 4,096. Both are below the int16 output's step (1/32,768, −90 dB). **Inaudible:** outputs move by about 1 LSB at most |
| **Accuracy, truncated (the LFO)** | The step error is 2π/N: −44 dB at 1,024 against −56 dB at 4,096. The LFO is evaluated per block and drives control values, so this means 1,024 steps per cycle instead of 4,096: about 0.4 cent per step on a ±1 semitone vibrato at its steepest. Inaudible; it could be interpolated for 2 instructions per block if wanted |
| **Bit-exactness** | Every sine-based voice (Drum, Snare, Cymbal, HiHat oscillators and FM carriers, the PWM sine warp, the LFO) changes by up to about 1 LSB. **The downside is in the tests, not the sound:** the host oscillator/PWM/LFO/Alkaline goldens that compare against stored values or against `sine_table` copies (`modules/oscillator.c`, `alkaline.c`, `lfo.c`, `pwm_dump.c`) must be regenerated, and the oscillator ITCM loops get one new shift constant (the `elf_p4` oscillator dumps change by that constant, not in count) |
| **Flash** | **−6,144 B** (8,194 → 2,050) |
| **D-cache** | Sine-heavy Scenes keep 2 KB of sine hot instead of up to 8 KB of a 16 KB D-cache. This is a small CPU gain, unmeasured |
| **For the Colloid** | `Sin` in `wav`, the same 1,024-point loop and the same cost as Saw/Rec/Tri, one row for every octave |
| **Scope** | It touches every instrument's sine path, so it is a separate prerequisite step with its own gates (the host goldens regenerated, `elf_p4_check` with a re-snapshot, one listening check), not part of the Colloid engine |

**Decision (user, 2026-11-12): adopted, as step 0 of the P2 schedule, re-tested on its own gates.** (Recommendation was:) adopt it, as step 0 of the P2 schedule. The sonic cost
is below the 16-bit floor; flash and cache improve; the work is a table
regeneration, a shift change at about 22 sites, and regenerated goldens.
The alternative, a separate 1,024-point sine row for the Colloid only, is
+2 KB of flash and changes nothing else.

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
- 2026-11-12: the Basic list in `S094_P2_ALKALINE_SAVINGS.md` §21 (a
  sixth Basic type and its measured worst), and the per-oscillator stereo
  position and two-envelope design in the new engine section.
- Found at the session-start check (2026-11-12), already out of date
  whether or not P2 is adopted: `MEMORY.md` says "`OSC_SAMPLE_START`
  stays **6**" (the Alkaline rule) and "`OSC_SAMPLE_START` is 6 (S093
  P3)". The tree has **5** (`Oscillator.h:69`) since S094 P1 S2 removed
  CRASH (`50c7436d`).
- Step 0 (the sine): `INSTRUMENTS_DSP_REFERENCE.md`'s table sizes and the
  sine read (`phase >> 22`), `STORAGE_SRAM_MANIFEST.md`'s flash table list,
  and any note that the sine is 4,096 points.
