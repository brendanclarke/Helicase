# S104 P1 — Tune-ups: implementation schedule

**Date:** 2026-11-25. **Branch:** `dev-ph7-alkaline`, clean at `9c2462bd`.
**Status:** all eight steps applied and host-tested (see §11); device checks in
§9.3 are outstanding. The design is `S104_P1_TUNE-UPS.md` (revision 2).

**Decisions in force** (user, 2026-11-25: "Ram approved, everything sounds ok there"):
- every open recommendation in the plan's §11 is accepted: Q6–Q15, Q17, Q18, Q20, Q21,
  Q24b, Q26–Q31;
- §6 rule 3 is read as **T6 pre-empts T7** on a shared layer;
- the RAM table is approved (§0.3 here). One item needs your confirmation (§0.3, row
  K): 32 B inside the existing Tetrakt slot reserve, which is not a new allocation.

---

## 0. How to use this schedule

### 0.1 Conventions

- **Line numbers** are for the clean tree at `9c2462bd`. Every entry also quotes an
  anchor (a line of code to search for), because earlier edits in the same file shift
  the numbers. **Within one file, apply entries bottom-up** (highest line first), or
  find each anchor by text.
- **Action:** **Add** (new code at the place named), **Modify** (replace the quoted
  lines), **Remove** (delete the quoted range).
- **Comment blocks:** every change carries one. The block in each entry is
  paste-ready documentation for the code it sits beside. Following the S084 rule, a
  comment never cites a schedule entry ID (C-1, D-4 …); it may cite the session
  (`S104 P1`).
- **Code** in an entry is complete where it is new. For a Modify, the entry shows the
  new form of the lines it replaces.
- **No entry edits `MEMORY.md` or `knowledge_files/`.** §10 lists the statements made
  out of date.

### 0.2 Order of work (each step: build, host tests, gates; then the next)

| Step | Entries | Why this order |
|---|---|---|
| 1 | P-1 | One branch, no dependencies |
| 2 | L-1 .. L-4 | Menu-local |
| 3 | C-1 .. C-12 | Sequencer + menu; host test `seq_conditions` |
| 4 | O-1 .. O-15 | Adds `PAR_MIDI_CHAN_OMNI` and the `settings.cfg` line 27 |
| 5 | D-1 .. D-27 | Adds `PAR_BPM_FINE` (line 26) and the terminator line 28 |
| 6 | F-1 .. F-10 | LFO (per-block DSP) |
| 7 | T-1 .. T-33 | Track-7 decays (adds `TktLayer.pflags`, used by step 8) |
| 8 | K-1 .. K-12 | Tetrakt loops |

**After each step:**
- `make all && make img`;
- `make -C tools/dsp_test -k all`: only the two known failures may remain
  (`voice_control` 14, `alkaline` AL-OD0);
- the step's own host laws and break proofs;
- `python3 tools/dsp_test/elf_hotloops.py` and the inlined-set diff against the
  baseline taken before step 1 into `tools/dsp_test/baselines/s104p1/` (§9.1);
- for steps 6–8, `tetrakt_cpu.py` and `elf_blockpath.py` as named in each section.

### 0.3 RAM (approved, 2026-11-25)

| Row | Owner | Region | Bytes | Object |
|---|---|---|---:|---|
| O | `MidiIn.c` | SRAM1 | +1 | `midiIn_config.omni` |
| O | `MidiNotes.c` | SRAM1 | +17 | `midiNotes_omniHeld[4]` (16) + `midiNotes_omniLatch` (1) |
| D | `sequencer.c` | SRAM1 | +8 | `seq_tempo` (2) → `seq_tempoCenti` (2); `seq_tempoNudge` (4); `seq_tempoEff` (4) |
| D | `buttonHandler.c` | SRAM1 | +17 | `bh_djCopyConsume` 1, `bh_djBar` 1, `bh_djDownStep` 4, `bh_djRampTick` 2, `bh_djTapFirstUs` 4, `bh_djTapLastUs` 4, `bh_djTapCount` 1 |
| D | `menu.c` | SRAM1 | +3 | `menu_djActive` 1, `menu_djPaintTick` 2 |
| T | `SceneData.c` | SRAM1 | +102 | `kit_settings_t` 2 → 8 B in 17 `scene_t` (measured) |
| T | `InstrumentManager.c` | SRAM1 | +3 | `slot6_track7_decay_step_active` (bit mask) + `slot6_track7_decay_step_value[4]` (was 1 + 1) |
| T | `filesystem.c` | SRAM1 | +1 | `op_kitset.seen_t7_mask` (`storage_kitset_t`) |
| T | `TktLayer` | slot reserve | 0 | `decayAlt`, `pendDecayAlt`, `pflags` in the 3 measured padding bytes (offsets 85–87) |
| **K** | `TktLayer` | **slot reserve** | **0 new** | **`loopStartPh`, `loopEndPh` (2 × uint32 × 4 layers = 32 B). `TetraktVoice` 1,080 → 1,112 B inside the existing 1,264 B `INSTRUMENT_RUNTIME_SLOT_BYTES` reserve (asserted at `InstrumentManager.c:360`). No new allocation (the S101 P2 A2q precedent). Please confirm.** |
| F | `Lfo` | voice structs | 0 | `osState` in the 2 padding bytes after `retrigger` (offsets 10–11, measured) |
| | | **Total** | **152 B SRAM1** | |

The DJ split differs from the plan's single "24 B in `buttonHandler.c`" row; the total
is unchanged (+28 B):
- the nudge value lives in `sequencer.c`, so the timer can read it;
- the paint tick lives in `menu.c`.

### 0.4 Corrections to the plan found in the code dive

1. **Tetrakt "simultaneous" window (plan §6.1):** a START or FADE runs the layer's
   control in the very next `Tetrakt_calcVoiceAsync()`, outside the A2q schedule
   (`TetraktVoice.c:561–563`). The pending window is therefore one block (0.73 ms),
   or two after a FADE, not 2.9 ms. The rule (T6 wins while pending) is unchanged.
2. **Existing defect: Kit file track-7 keys are never read.**
   - `storage_kitsetParseLine()` splits into `char key[24]`
     (`storageTypes.c:2047`), and `storage_splitKeyValue()` truncates at 23
     characters;
   - `slot6_track7_amp_envelope_decay` (31) and
     `slot6_track7_morph_amp_envelope_decay` (37) never match, so the values are
     written but silently dropped on load;
   - the new keys are longer than 23 too. T-10 widens the buffer (stack only).
3. **Existing gap: VOICE7 held-step edits skip Kit-setting cells.**
   `va_writeAutomationFromKnob()` (`menu.c:4110–4119`) returns for a
   `MENU_CELL_KIT_SETTING`. T-25 gives the Tetrakt track-7 cells a target.
4. **Existing behaviour: a T7 hit restores T6's decay locks.**
   `seq_restoreAutomatedParameters()` maps track 7 to slot 5 and restores every held
   lock of that slot (`sequencer.c:4335–4386`). T-21 fixes this for the four Tetrakt
   decays, as the plan's rule 5 requires.
5. **Seam-prediction cost (plan §7.3 step 3)** is ≈ 10–12 instructions per looped
   layer per block, not ≈ 6, with the loop points cached (K-1). The worst case is four
   looped layers: ≈ +1.5 a sample, inside Tetrakt's headroom (§8.6).
6. **One-shot LFO cost (plan §8.3):** the dearest one-shot path (state step + shape
   dispatch) is est. +5..+8 per control, not +3..+4. That is ≈ +0.6 a sample
   system-wide. Measured at the gate (F-9); Q31 accepted it as measured.

---

## 1. Step conditions (`!1-`)

### C-1 — `Core/Menu/MenuText.h:509–533` — **Modify** (the comment and `conditionNames[]`)

Anchor: `static const char conditionNames[][4] = {`

```c
/*
 * Trigger condition display names (S079 §3; fix 3 extends to track 7;
 * S080 B1 adds the loop-active pair; S104 P1 adds `!1-`).
 *
 * What: the 3-character label of every step trigger condition, indexed
 * directly by the stored condition value (menu_getConditionName() reads
 * conditionNames[value + 1]), so the table must stay in numeric order.
 * S104 P1 inserts `!1-` at value 21, right after `1:-` (20): `1:-` plays a
 * step only on the track's first pass since its Scene (or the transport)
 * started it, and `!1-` plays it on every pass except that first one. Both
 * read the per-track repeat counter (seq_trackRepeatCount[]).
 * Every later condition moves up by one: fil/!fl 22/23, lop/!lp 24/25,
 * lst/!ls 26/27, 1mu..7mu 28..34, !1m..!7m 35..41. The mute group stays
 * in the dispatch's order (seq_conditionEval(): voice (cond - 28) % 7,
 * muted when cond <= 34).
 * No migration (user, S104 P1): a Pattern stored before S104 that used
 * values 21..40 now plays the condition one place earlier in this list.
 *
 * [0] = count 42; [1..42] = condition values 0..41.
 */
static const char conditionNames[][4] = {
    {42},
    {"  -"},{"r10"},{"r20"},{"r30"},{"r40"},{"r50"},{"r60"},{"r70"},
    {"r80"},{"r90"},{"r95"},
    {"1:2"},{"2:2"},{"1:3"},{"2:3"},{"3:3"},{"1:4"},{"2:4"},{"3:4"},
    {"4:4"},{"1:-"},{"!1-"},
    {"fil"},{"!fl"},{"lop"},{"!lp"},{"lst"},{"!ls"},
    {"1mu"},{"2mu"},{"3mu"},{"4mu"},{"5mu"},{"6mu"},{"7mu"},
    {"!1m"},{"!2m"},{"!3m"},{"!4m"},{"!5m"},{"!6m"},{"!7m"},
};
```

### C-2 — `Core/Sequencer/sequencer.c:283–296` — **Modify** (the comment) + **Add** after `:296`

Anchor: `static uint8_t seq_trackRepeatCount[NUM_TRACKS];`

Replace the comment above the declaration. Keep the declaration. Add the helper
immediately after it:

```c
/*
 * Per-track repeat count for X:Y, 1:- and !1- conditions (S079 §3.2;
 * S104 P1 wrap rule).
 *
 * What: advances by one each time a track's step index wraps (forward:
 * len-1->0, reverse: 0->len-1, pip: at each boundary reversal, random: once
 * per len steps drawn), always through seq_repeatCountNext(). Why: the X:Y
 * conditions need a modulo counter, and 1:- / !1- need "is this the first
 * pass" (count 0). Reset to 0 on transport/pattern reset
 * (seq_setStepIndexToStart), Scene change (seq_resetTrackForSceneChange) and
 * per-track Scene assignment. Once modes never wrap, so the counter stays at
 * 0. Inputs: play-mode wrap logic in seq_advanceTrackStep(). Outputs:
 * seq_conditionEval() and seq_rampConditionPredict(). RAM: 7 B SRAM1
 * static. Affiliates: seq_setStepIndexToStart(), seq_selectActivePattern(),
 * seq_repeatCountNext().
 */
static uint8_t seq_trackRepeatCount[NUM_TRACKS];

/*
 * Next value of a repeat counter (S104 P1).
 *
 * What: c + 1, except that 255 is followed by 4, never by 0.
 * Why: the counter is one byte. A plain 255 -> 0 wrap made `1:-` play again
 * after 256 passes, `!1-` skip one, and `1:3` play two passes in a row
 * (255 % 3 == 0 and 0 % 3 == 0). 256 is 4 modulo 12 (the least common
 * multiple of the X:Y divisors 2, 3 and 4), so continuing at 4 keeps every
 * X:Y sequence unbroken, and 0 ("first pass") is reached only by a reset.
 * Inputs: the current count. Output: the next count. Context: TIM3 (the
 * wrap sites) and the ramp predictor.
 * Accessors: seq_advanceTrackStep(), seq_rampConditionPredict().
 */
static inline uint8_t seq_repeatCountNext(uint8_t c)
{
    return (uint8_t)((c == 255u) ? 4u : (c + 1u));
}
```

### C-3 — `Core/Sequencer/sequencer.c:308–316` — **Modify** (comment only)

Anchor: `* Per-track last-condition-fired flag for lst/!ls chaining (S079 §3.4).`

```c
/*
 * Per-track last-condition-fired flag for lst/!ls chaining (S079 §3.4).
 *
 * What: set to 1 when the last evaluated conditional step (condition 1..41)
 * fired, 0 when it was suppressed. Default 0 (not fired). Updated by
 * seq_evaluateStepCondition() for any step with condition != 0. Why: lst/!ls
 * conditions chain off the most recent conditional step on this track.
 * Inputs: seq_evaluateStepCondition(). Outputs: conditions 26 (lst) and 27
 * (!ls; S104 P1 numbering) read this. RAM: 7 B SRAM1 static. Affiliates:
 * seq_setStepIndexToStart().
 */
```

### C-4 — `Core/Sequencer/sequencer.c:3000–3022` — **Modify** (`seq_conditionRepeatPasses()`)

Anchor: `static uint8_t seq_conditionRepeatPasses(uint8_t cond, uint8_t repeatCount)`

```c
/*
 * Repeat-count condition test (S079 §3; factored for S081 P2; S104 P1 !1-).
 *
 * What: conditions 11..19 ("X:Y") pass when repeatCount % Y == X - 1;
 * 20 ("1:-") passes only on the first pass (repeatCount 0); 21 ("!1-")
 * passes on every pass except the first (repeatCount != 0). Why: playback
 * and the ramp predictor must apply the identical rule; the predictor passes
 * the repeat count the next step will see. The counter never returns to 0
 * except by a reset (seq_repeatCountNext()), so 1:- and !1- stay exact
 * however long the Scene plays. Inputs: condition 11..21, repeat count.
 * Output: pass flag. Context: TIM3. Accessors: seq_conditionEval(),
 * seq_rampConditionPredict().
 */
static uint8_t seq_conditionRepeatPasses(uint8_t cond, uint8_t repeatCount)
{
    static const uint8_t xy_table[9][2] = {
        {1u,2u},{2u,2u},{1u,3u},{2u,3u},{3u,3u},
        {1u,4u},{2u,4u},{3u,4u},{4u,4u}
    };

    if (cond == 20u)
        return (uint8_t)(repeatCount == 0u);
    if (cond == 21u)
        return (uint8_t)(repeatCount != 0u);
    return (uint8_t)((repeatCount % xy_table[cond - 11u][1]) ==
                     (uint8_t)(xy_table[cond - 11u][0] - 1u));
}
```

### C-5 — `Core/Sequencer/sequencer.c:3024–3047` — **Modify** (the `seq_evaluateStepCondition` header comment)

Anchor: `* Evaluate the per-step trigger condition (S079 §3).`

```c
/*
 * Evaluate the per-step trigger condition (S079 §3; S104 P1 numbering).
 *
 * What: the single gate before a step fires. Returns 1 (fire) or 0
 * (suppress). Updates seq_trackLastConditionFired for conditional steps.
 * Why: all condition logic lives at one entry point. Condition 0 and
 * undefined values (42..255) always pass.
 *
 * Probability conditions 1..10 map to 10%..95% and are compared against the
 * hardware RNG. Repeat-count conditions 11..21: X:Y (11..19) fire when
 * (seq_trackRepeatCount % Y) == (X - 1); 20 ("1:-") fires only on the first
 * pass and 21 ("!1-", S104 P1) on every pass except the first. Fill
 * conditions 22/23 read seq_fillActive; loop conditions 24 ("lop") and 25
 * ("!lp") read seq_loopActive; last conditions 26/27 read
 * seq_trackLastConditionFired. Mute conditions 28..41 test one voice's mute
 * bit in seq_mutedTracks (28..34 muted, 35..41 not muted). Total table size
 * is 42 (values 0..41; MenuText.h conditionNames[]).
 *
 * Inputs: specials struct (condition index), track index (for mute/repeat/
 * last state). Outputs: gate result, side effect on
 * seq_trackLastConditionFired. Context: TIM3 ISR. Affiliates:
 * seq_advanceTrackStep(), GetRngValue(), seq_mutedTracks,
 * seq_trackRepeatCount, seq_fillActive, seq_trackLastConditionFired.
 */
```

### C-6 — `Core/Sequencer/sequencer.c:3058–3116` — **Modify** (`seq_conditionEval()` body)

Anchor: `if (cond == 0u || cond > 40u)` (the first of two; the second is C-9)

Replace from the bound test to the end of the mute `else` block with:

```c
    if (cond == 0u || cond > 41u)
        return 1u;

    if (cond <= 10u) {
        /* Probability: 10%..95%, values 1..10. */
        static const uint8_t prob_thresholds[10] = {
            13u, 25u, 38u, 51u, 64u, 76u, 89u, 102u, 114u, 121u
        };
        uint8_t threshold = prob_thresholds[cond - 1u];
        uint8_t rnd = (uint8_t)(((uint16_t)(GetRngValue() & 0x7FFFu) *
                                 127u) / 32767u);

        result = (uint8_t)(rnd < threshold);
    } else if (cond <= 21u) {
        /* Repeat count: X:Y, 1:- and !1-, values 11..21 (shared with the
         * S081 P2 ramp predictor through seq_conditionRepeatPasses()). */
        result = seq_conditionRepeatPasses(cond, seq_trackRepeatCount[track]);
    } else if (cond <= 23u) {
        /* Fill / not-fill, values 22..23 (S104 P1 shift). */
        if (cond == 22u)
            result = seq_fillActive;
        else
            result = (uint8_t)(!seq_fillActive);
    } else if (cond <= 25u) {
        /*
         * Loop active / inactive, values 24..25 (S080 B4; S104 P1 shift).
         *
         * What: 24 ("lop") fires only while the PERF looper is engaged; 25
         * ("!lp") only while it is not. Why: lets pattern steps react to the
         * looper. Input: seq_loopActive (single-byte, foreground set, ISR
         * read). No boundary timing - evaluated on every step. Affiliates:
         * conditionNames[] (MenuText.h), seq_loopEngage()/Disengage().
         */
        if (cond == 24u)
            result = seq_loopActive;
        else
            result = (uint8_t)(!seq_loopActive);
    } else if (cond <= 27u) {
        /* Last / not-last conditional step, values 26..27 (S104 P1 shift). */
        if (cond == 26u)
            result = seq_trackLastConditionFired[track];
        else
            result = (uint8_t)(!seq_trackLastConditionFired[track]);
    } else {
        /*
         * Mute / not-mute, values 28..41 (S104 P1 shift). 28..34 = "is voice N
         * muted" for voices 1..7, 35..41 = "is voice N not muted". The
         * zero-based voice index is (cond - 28) mod 7 over both halves.
         */
        uint8_t voice_idx = (uint8_t)((cond - 28u) % 7u);
        uint8_t is_muted = (uint8_t)((seq_mutedTracks >> voice_idx) & 1u);

        if (cond <= 34u)
            result = is_muted;
        else
            result = (uint8_t)(!is_muted);
    }
```

### C-7 — `Core/Sequencer/sequencer.c:3442, 3450, 3457, 3468, 3485` — **Modify** (5 lines)

Anchor: `seq_trackRepeatCount[track]++;` (five occurrences in `seq_advanceTrackStep()`)

Each becomes:

```c
			seq_trackRepeatCount[track] =
			    seq_repeatCountNext(seq_trackRepeatCount[track]);
```

Add this comment once, above the `switch (play_mode)` at `:3436`:

```c
	/*
	 * Every pass wrap below advances the repeat counter through
	 * seq_repeatCountNext() (S104 P1), never with ++: the counter must not
	 * return to 0 by overflow, because 0 means "first pass" for 1:- / !1-.
	 */
```

### C-8 — `Core/Sequencer/sequencer.c:3526–3531` — **Modify** (the muted-track self-probe)

Anchor: `/* S080 B4: mute condition ranges shifted +2 by lop/!lp. */`

```c
				/*
				 * Self-mute conditions (S080 B4; S104 P1 shifts them +1
				 * behind !1-): 28..34 "this voice muted", 35..41 "this
				 * voice not muted", voice = cond - 28 or cond - 35.
				 */
				if (cond >= 28u && cond <= 34u &&
				    (uint8_t)(cond - 28u) == track)
					self_mute = 1u;
				else if (cond >= 35u && cond <= 41u &&
				    (uint8_t)(cond - 35u) == track)
					self_mute = 1u;
```

### C-9 — `Core/Sequencer/sequencer.c:4462–4486` — **Modify** (`seq_rampConditionPredict()`)

Anchor: `static uint8_t seq_rampConditionPredict(const pat_step_specials_t *sp,`

Rewrite the comment's condition list ("the repeat counters (with the count B will
see), 1:- / !1- …"), then the body:

```c
    uint8_t cond = sp->condition;

    if (cond == 0u || cond > 41u)
        return 1u;
    /* X:Y, 1:- and !1- (11..21) read the count B will see; the wrap
     * follows seq_repeatCountNext() exactly as playback does (S104 P1). */
    if (cond >= 11u && cond <= 21u)
        return seq_conditionRepeatPasses(
            cond, repeatInc ? seq_repeatCountNext(seq_trackRepeatCount[track])
                            : seq_trackRepeatCount[track]);
    if (cond == 26u)
        return seq_trackLastConditionFired[track];
    if (cond == 27u)
        return (uint8_t)(!seq_trackLastConditionFired[track]);
    return 0u;              /* rNN, fil/!fl, lop/!lp, mute: not predictable */
```

### C-10 — `Core/Menu/menu.c:6954–6964` — **Modify** (step-condition clamp)

Anchor: `* Step-special domains (S079; S080 widens condition): condition 0..40,`

```c
    /*
     * Step-special domains (S079; S080 widens condition; S104 P1 adds !1-):
     * condition 0..41, roll rate 0..8,
     * micro-timing 0..99, roll fill 0..100, rhythm snapped to its ordered list
     * (S079 P2). Applied here because the DTYPE_* nibble space is full.
     */
    if (cell->kind == MENU_CELL_STATIC &&
        cell->static_param == PAR_STEP_CONDITION) {
        if (*value > 41u)
            *value = 41u;
        return;
    }
```

### C-11 — `Core/Menu/menu.c:13212–13232` — **Modify** (`menu_getConditionName()`)

Anchor: `static void menu_getConditionName(uint16_t value, char *buf)`

```c
/*
 * Format one step trigger condition into a three-character field (S079 §3;
 * S104 P1: 42 conditions).
 *
 * What: writes the 3-char condition label for values 0..41 (S080 B1 adds the
 * lop/!lp pair; fix 3 added track 7; S104 P1 adds !1- at 21). Values
 * 42..255 (undefined) display as the always-fire default. Inputs: raw stored
 * condition and a three-character output. Outputs: exactly three
 * characters, no terminator. Affiliates: conditionNames[] (MenuText.h),
 * va_formatValue3(), menu_formatCellValue3(), the edit-view painter.
 */
static void menu_getConditionName(uint16_t value, char *buf)
{
    const char *p;

    if (value > 41u)
        value = 0u;
    p = conditionNames[value + 1u];
    buf[0] = p[0];
    buf[1] = p[1] ? p[1] : ' ';
    buf[2] = p[2] ? p[2] : ' ';
}
```

### C-12 — host test `seq_conditions` — **Add** (`tools/dsp_test/modules/seq_conditions.c`, Makefile rule)

**Makefile** (`tools/dsp_test/Makefile`):
- add `seq_conditions` to the `.PHONY` list (`:57`) and to `all` (`:66`);
- add the rule after `seq_steplen`:

```make
# S104 P1: step conditions 1:- / !1- and the renumbered list; the repeat
# counter's wrap. Break: the old 255 -> 0 wrap must fail law SC-WRAP.
seq_conditions: | $(GEN)
	$(EX) --src $(ROOT)/Core/Sequencer/sequencer.c \
	  --func seq_repeatCountNext --func seq_conditionRepeatPasses \
	  --func seq_conditionEval --func seq_rampConditionPredict \
	  --out $(GEN)/seq_conditions.c
	$(HOSTCC) $(HOSTFLAGS) -I$(GEN) modules/seq_conditions.c -o build/seq_conditions -lm
	./build/seq_conditions
	mkdir -p build/break_seqcond
	sed 's/return (uint8_t)((c == 255u) ? 4u : (c + 1u));/return (uint8_t)(c + 1u);/' \
	  $(GEN)/seq_conditions.c > build/break_seqcond/seq_conditions.c
	$(HOSTCC) -Ibuild/break_seqcond $(HOSTFLAGS) modules/seq_conditions.c \
	  -o build/seq_conditions_break -lm
	@if ./build/seq_conditions_break >/dev/null 2>&1; then \
	  echo "seq_conditions_break: FAILED (the 255 -> 0 wrap was not caught)"; \
	  exit 1; else echo "seq_conditions_break: caught (SC-WRAP fails as expected)"; fi
```

**`modules/seq_conditions.c`:**
- stubs: `pat_step_specials_t { uint8_t condition; }`, `seq_trackRepeatCount[7]`,
  `seq_trackLastConditionFired[7]`, `seq_fillActive`, `seq_loopActive`,
  `seq_mutedTracks`, a deterministic `GetRngValue()`; then
  `#include "seq_conditions.c"`.
- **Laws:**
  - **SC-1ST:** over 300 passes from a reset (`c = 0`, then `c =
    seq_repeatCountNext(c)`), `1:-` (20) passes on pass 0 only and `!1-` (21) on
    every other pass.
  - **SC-XY:** every X:Y (11..19) passes on the same passes as `c % Y == X − 1`
    computed on an unbounded counter, for 1,000 passes. This proves the wrap to 4
    keeps them.
  - **SC-WRAP:** at the 255 → next step, `1:-` does not pass and `1:3` does not pass
    twice in a row.
  - **SC-MAP:** one value per renumbered group. 22 follows `seq_fillActive`; 24
    follows `seq_loopActive`; 26 follows `seq_trackLastConditionFired`; 28 + v is
    "voice v muted" and 35 + v "not muted", for v = 0..6; 42 always passes.
  - **SC-PRED:** for 11..21, `seq_rampConditionPredict(sp, t, 1)` equals
    `seq_conditionEval()` after one more pass; 26/27 match.
- **Comment block** at the top of the file, in the `sequencer.c` host-test form
  (What / Why / Inputs / Outputs / Accessors / Affiliates), naming the laws above
  and the break.

---

## 2. Track-button preview (selected parameter jumps)

### P-1 — `Core/Hardware/frontPanel/buttonHandler.c:3320–3324` — **Modify**

Anchor: `if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {` followed by
`menu_switchPage(voiceNr);`

```c
        menu_setActiveVoice(voiceNr);
        led_setActiveVoice(voiceNr);
        if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {
            /*
             * A stopped-transport audition keeps the menu cursor (S104 P1).
             *
             * What: when this press re-presses the selected track while its
             * own voice page is already showing and the transport is stopped
             * (shouldPreviewVoice), the page is not switched again; the press
             * only previews (below). Any other press still runs
             * menu_switchPage(), which rebuilds the page and puts the cursor on
             * the screen's first selectable column (menu_resetActiveParameter()).
             * Why: the re-switch moved the selected parameter back to column 0
             * at every audition (user report, S104 P1). A different surface on
             * the same voice (the VOICE step edit, or a Morph view whose page
             * differs) leaves menu_activePage != voiceNr, so that press returns
             * to the voice page as before.
             * Inputs: shouldPreviewVoice, menu_activePage, voiceNr.
             * Outputs: no page or cursor change on an audition re-press.
             * Affiliates: menu_switchPage(), menu_resetActiveParameter(),
             * seq_previewVoice() at the end of this function.
             */
            if (!(shouldPreviewVoice && menu_activePage == voiceNr)) {
                menu_switchPage(voiceNr);
                led_setActiveSelectButton(menu_getSubPage());
            }
```

The rest of the branch (the PAR_FOLLOW view-follows-track block) is unchanged and
stays inside `if (bh_state.selectButtonMode == SELECT_MODE_VOICE) {`.

**Check (device):** on every VOICE screen, with the cursor on column 2 or 3,
re-pressing the track auditions and leaves the cursor where it was. Pressing another
track still opens its page at column 0.

---

## 3. Load/Save pot 1 (open item M1)

### L-1 — `Core/Menu/menu.c:12127–12138` — **Modify** (`menu_handleLoadSaveKnobDelta()` `case 0`)

Anchor: `uint16_t count = menu_loadSaveLogicalCount();`

```c
    case 0:
        /*
         * Pot 1 is the type row's encoder turn (S104 P1, open item M1).
         *
         * What: puts the cursor on the type row in edit mode and hands the
         * delta to menu_handleLoadSaveMenu(), exactly as an encoder turn on
         * that row is handled: top level steps only this page's explicit type
         * list with wraparound (menu_nextRestoredLoadSaveType(): Load = Kit,
         * KitMrp, Scene, Bank, Pattern, Samples; Save = the first five);
         * nested Instrument Load steps the selected track's Instrument types
         * (menu_instrumentLoadStepType()); nested Instrument Save toggles
         * Normal/Morph. Every boundary the encoder applies comes with it
         * (index-read cancel, name-session flush, Scene-mask reset, busy
         * paint).
         * Why: pot 1 walked one combined ring (all Load types, every track's
         * Instrument options, all Save types, 14 nested-Save rows), so a turn
         * crossed Load <-> Save and entered or left nested Instrument modes
         * (user, S104 P1: "clamped to only the global load or save types in
         * the same way as the main encoder"). Nested modes are still entered
         * with the VOICE buttons and left with the Load/Save MODE button.
         * The storage lock above still drops a pot-1 turn while storage is
         * busy (S090 P1: pots keep the full lock).
         * Inputs: delta. Outputs: menu_saveOptions.state, editModeActive, the
         * encoder's type step. Affiliates: menu_handleLoadSaveMenu(),
         * menu_nextRestoredLoadSaveType(), menu_instrumentLoadStepType().
         */
        menu_saveOptions.state = SAVE_STATE_EDIT_TYPE;
        editModeActive = 1u;
        menu_handleLoadSaveMenu(delta, 0u);
        break;
```

### L-2 — `Core/Menu/menu.c:11456–11623` — **Remove**

Anchor (first line): `* Hardware Load/Save Pot-1 linearization helpers.` (comment opening at `:11456`)
Last line: the closing `}` of `menu_loadSaveLogicalCount()` (`:11623`).

**Removed functions** (each has no caller after L-1; checked with `grep`):
- `menu_loadSaveRestoredTypeIndex()` (`:11477`; callers only `:11593`, `:11606`);
- `menu_instrumentLoadOptionCountForVoice()` (`:11495`);
- `menu_instrumentLoadOptionAt()` (`:11517`);
- `menu_instrumentLoadCurrentOptionForVoice()` (`:11555`);
- `menu_instrumentLoadOptionTotal()` (`:11571`);
- `menu_loadSaveLogicalPosition()` (`:11582`);
- `menu_loadSaveLogicalCount()` (`:11614`).

`menu_loadSaveClearInstrumentVoiceBlinks()` (`:11625`) stays.

### L-3 — `Core/Menu/menu.c:11861–12040` — **Remove**

From `static void menu_loadSaveSetInstrumentVoiceLed(uint8_t voice)` (`:11861`) to the
closing `}` of `menu_loadSaveApplyLogicalPosition()` (`:12040`).

**Removed functions** (only callers were each other and L-1's old body):
- `menu_loadSaveSetInstrumentVoiceLed()`;
- `menu_loadSaveEnterInstrumentLoad()`;
- `menu_loadSaveEnterInstrumentSave()`;
- `menu_loadSaveApplyLogicalPosition()`.

`menu_backgroundBankLoadUiPoll()` (ends `:11859`) and
`menu_loadSaveActiveNameBuffer()` (`:12042`) stay.

**Build check:** `-Wall` reports no unused static function in `menu.c`.

### L-4 — comments that describe the old pot-1 ring — **Modify**

| Line | Anchor | New text |
|---|---|---|
| `:7288` | `* Pot-1 and encoder type cycling use explicit lists instead of enum order.` | `* Encoder and pot-1 type cycling (pot 1 is an encoder turn on the type row since S104 P1) use explicit lists instead of enum order.` |
| `:11404` | `* matching top-level Load/Save entry and Pot-1 navigation.` | `* matching top-level Load/Save entry.` |
| `:11643–11645` | `/* Pot-1 can leave nested Instrument Load without passing the dedicated` | `/* A MIDI-origin Bank load can leave nested Instrument Load without passing the dedicated exit button (S104 P1: pot 1 no longer can). Finalize the one reversible preview before replacing its type/index browser context or clearing the nested-active flag. */` |
| `:11658` | `* Pot-1 can leave the combined Kit/Instrument family without leaving the` | `* A top-level type entry (encoder or pot 1 on the type row, or a MIDI-origin Bank load) can leave the Kit family without leaving the` (the rest of the block unchanged) |
| `:12083–12084` | `* Why: the pots bypassed this gate, so pot 1 could change page, type or` | `* Why: the pots bypassed this gate, so pot 1 could change the type (and, before S104 P1, page or nested Instrument mode) and clear the shared name cache while an index` |
| `:12177` | `* Pot 1 still repaints in full because it changes the page/type layout.` | `* Pot 1 still repaints in full because it changes the type row's layout.` |
| `:17455–17456` | `* Pots (menu_handleLoadSaveKnobDelta()) keep their lock: Pot 1 also` | `* Pots (menu_handleLoadSaveKnobDelta()) keep their lock (S090 P1, kept in S104 P1 although pot 1 now steps types only).` |

**Device check:** pot 1 on Load, Save, nested Instrument Load and nested Instrument
Save steps exactly what the encoder steps on each.

---

## 4. MIDI Omni channel

Meaning (plan §4.2, accepted):
- a channel message on **O** acts on the **selected track** as if that track were
  alone on its own channel;
- O overrides G or a track channel when they are equal;
- Learn treats O as a literal channel;
- CC0 and CC2 are never consumed on O;
- notes held on O stay on the track they started on until all are released.

### O-1 — `Core/Bank/Scene/Preset/ParameterArray.h:325` — **Add** after `PAR_PERF_VEL_RT,`

Anchor: `PAR_PERF_VEL_RT,` (followed by a blank line and `NUM_PARAMS = 384,`)

```c
	/*
	 * S104 P1: the MIDI Omni channel `omn` (0 off, 1..16).
	 *
	 * What: a persisted Global beside G. A channel message received on it
	 * acts on the selected track (menu_getActiveVoice()) as if that track
	 * were alone on its own MIDI channel (MidiIn.c midiIn_omniTrack()).
	 * Why: play and control whichever track is selected from one keyboard
	 * channel without re-assigning track channels (user, S104 P1).
	 * Appended after PAR_PERF_VEL_RT so no established id is renumbered;
	 * NUM_PARAMS stays 384. Inputs: Global page row 0 column 4, settings.cfg
	 * `midi_chan_omni`. Outputs: menu_parseGlobalParam() ->
	 * midiIn_setOmniChannel(). Affiliates: filesystem settings writer/parser,
	 * filesystem_sanitizeLoadedGlobals().
	 */
	PAR_MIDI_CHAN_OMNI,
```

(D-1 appends `PAR_BPM_FINE` after this one.) Add after the existing
`_Static_assert(PAR_PERF_VEL_RT < NUM_PARAMS, …)` at `:345`:

```c
/* S104 P1: the appended Globals still fit the fixed flat array. */
_Static_assert(PAR_BPM_FINE < NUM_PARAMS,
               "S104 P1 Globals must stay inside NUM_PARAMS");
```

### O-2 — `Core/Menu/menu.c:1353` — **Add** after `[PAR_PERF_VEL_RT] = DTYPE_0B255,`

```c
    /* S104 P1: omn is G's twin (0 off, 1..16; the off name and the clamp are
     * static-param special cases in menu_getMidiStaticName() and the static
     * clamp, as for PAR_MIDI_CHAN_GLOBAL). */
    [PAR_MIDI_CHAN_OMNI] = DTYPE_1B16,
```

### O-3 — labels — **Add** (`menu.h` `:187`, `:291`, `:396`; `MenuLabels.c` `:91`, `:199`; `menu.c` `:1499`)

**1. `Core/Menu/menu.h:186`** — after `TEXT_PERF_VEL_LT, TEXT_PERF_VEL_RT,`:

```c
    /* S104 P1: the Global MIDI Omni channel `omn` (category MIDI). */
    TEXT_MIDI_CHAN_OMNI,
```

**2. `Core/Menu/menu.h:291`** — after `LONG_DRIFT, LONG_TRACK, LONG_LOWCUT, LONG_OUTFILT,`:

```c
    /* S104 P1: "Omni Ch" (MenuLabels.c, same position). */
    LONG_OMNI_CH,
```

**3. `Core/Menu/menu.h:396`** — after `SHORT_DP2, SHORT_CHS, SHORT_SRCE, SHORT_DTK, SHORT_LCT,`:

```c
    /* S104 P1: "omn" (MenuLabels.c, same position). */
    SHORT_OMN,
```

**4. `Core/Menu/MenuLabels.c:91`** — after `{"src"},{"dtk"},{"lct"},`:

```c
    /* S104 P1: the Global MIDI Omni channel (menu.h SHORT_OMN). */
    {"omn"},
```

**5. `Core/Menu/MenuLabels.c:199`** — after `{"Low Cut"},{"OutFilt"},`:

```c
    /* S104 P1: the Global MIDI Omni channel (menu.h LONG_OMNI_CH). */
    {"Omni Ch"},
```

**6. `Core/Menu/menu.c:1499`** — after `{SHORT_PERF_VEL_RT,CAT_PERFORMANCE,LONG_PERF_VEL_RT},` (the last
`valueNames[]` row):

```c
    /* S104 P1: TEXT_MIDI_CHAN_OMNI. */
    {SHORT_OMN,CAT_MIDI,LONG_OMNI_CH},
```

The existing `_Static_assert`s on the three label tables (`MenuLabels.c:202–210`)
check the new lengths (223 → 224, 195 → 196; `NUM_NAMES` 122 → 123; all below 256,
measured).

### O-4 — `Core/Menu/menuPages.h:55–56` and `:74–77` — **Modify** (Global rows 0 and 2)

Row 0 (`:55–56`):

```c
  /*
   * S104 P1: `omn` (the MIDI Omni channel) sits beside G (`ch`) in column 4;
   * the screensaver cell moves to the Scene-change screen (row 2, column 3).
   */
  {TEXT_BPM,TEXT_EXT_SYNC,TEXT_QUANTISATION,TEXT_MIDI_CHAN_GLOBAL,TEXT_MIDI_CHAN_OMNI,TEXT_MIDI_FILT_TX,TEXT_MIDI_FILT_RX,TEXT_MIDI_ROUTING,
    PAR_BPM,PAR_EXT_SYNC, PAR_QUANTISATION, PAR_MIDI_CHAN_GLOBAL, PAR_MIDI_CHAN_OMNI, PAR_MIDI_FILT_TX, PAR_MIDI_FILT_RX, PAR_MIDI_ROUTING},
```

Row 2 (`:74–77`). Column 3 becomes the screensaver cell; columns 4–7 stay empty. Also
change "so no parameter follows qac on this screen" in its comment (`:66`) to "`scr`
follows qac (S104 P1); no parameter follows `scr` on this screen":

```c
  {TEXT_SCENE_CHANGE_DIV,TEXT_SCENE_QUEUE_DIV,TEXT_SCENE_QUEUE_ACTION,TEXT_SCREENSAVER_ON_OFF,
    TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY,
    PAR_SCENE_CHANGE_DIV,PAR_SCENE_QUEUE_DIV,PAR_SCENE_QUEUE_ACTION,PAR_SCREENSAVER_ON_OFF,
    PAR_NONE,PAR_NONE,PAR_NONE,PAR_NONE},
```

**Check (device):** the Session 025 warning in this table applies. Turn the encoder
through every Global screen to the compressor page and back, and confirm `scr` and
the compressor page are both reachable; press MODE MENU repeatedly to cycle the
screens.

### O-5 — `Core/Menu/menu.c:6642` and `:6902` — **Modify** (off name and clamp)

`menu_getMidiStaticName()` (`:6642`): make `case PAR_MIDI_CHAN_GLOBAL:` a two-label case:

```c
    case PAR_MIDI_CHAN_GLOBAL:
    case PAR_MIDI_CHAN_OMNI:      /* S104 P1: omn shows `off` for 0, as G */
```

Static clamp (`:6902`):

```c
        case PAR_MIDI_CHAN_GLOBAL:
        case PAR_MIDI_CHAN_OMNI:  /* S104 P1: 0 off, 1..16 */
            if (*value > 16u) *value = 16u;
            return;
```

### O-6 — `Core/Menu/menu.c:20361` — **Add** a case in `menu_parseGlobalParam()` after the `PAR_MIDI_CHAN_GLOBAL` case

```c
    case PAR_MIDI_CHAN_OMNI:
        /*
         * MIDI Omni channel O (S104 P1).
         *
         * What: 1..16, 0 = off. A channel message on O acts on the selected
         * track as if that track were alone on its own channel; O wins over
         * G and track channels with the same number. Inputs: menu/settings
         * value. Output: MidiIn configuration; MidiIn releases every note
         * still held on the old O first. Affiliates: midiIn_setOmniChannel(),
         * midiIn_omniTrack(), filesystem_sanitizeLoadedGlobals().
         */
        midiIn_setOmniChannel(value);
        break;
```

### O-7 — `Core/Hardware/SD/filesystem.c` — **Modify / Add** (settings defaults, sanitize, key)

| Line | Anchor | Change |
|---|---|---|
| `:3020` | `parameter_values[PAR_MIDI_CHAN_GLOBAL] = 1u;` in `filesystem_resetSettingsToDefaults()` | Add after it: `parameter_values[PAR_MIDI_CHAN_OMNI] = 0u;   /* S104 P1: Omni off */` |
| `:2904–2907` | `uint8_t ch = parameter_values[PAR_MIDI_CHAN_GLOBAL];` in `filesystem_sanitizeLoadedGlobals()` | Add after the G clamp: `/* S104 P1: Omni 0..16; anything else is off. */ if (parameter_values[PAR_MIDI_CHAN_OMNI] > 16u) parameter_values[PAR_MIDI_CHAN_OMNI] = 0u;` |
| `:3110` | `else if (strcmp(key, "perf_vel_rt") == 0) *param = PAR_PERF_VEL_RT;` | Add after it: `/* S104 P1: the MIDI Omni channel and the BPM hundredths. */ else if (strcmp(key, "midi_chan_omni") == 0) *param = PAR_MIDI_CHAN_OMNI;` (D-4 adds `bpm_fine` beside it) |

`filesystem_resetGlobalsToDefaults()` (`:2874`) already zeroes every id from
`PAR_BEGINNING_OF_GLOBALS` to `NUM_PARAMS`, so it needs no change.

### O-8 — `Core/Hardware/SD/filesystem.c:19287–19307` — **Modify** (settings writer: two data lines and the terminator)

Anchor: `case 26u:` (the terminator)

Replace `case 26u:` … `return filesystem_formatAssignmentU16Line(dst, cap, "lines", 26u);`
with:

```c
    case 26u:
        /* S104 P1: the BPM's hundredths (0..99; DJ mode, the tap tempo). */
        return filesystem_formatAssignmentU16Line(
            dst, cap, "bpm_fine", parameter_values[PAR_BPM_FINE]);
    case 27u:
        /* S104 P1: the MIDI Omni channel (0 off, 1..16). */
        return filesystem_formatAssignmentU16Line(
            dst, cap, "midi_chan_omni", parameter_values[PAR_MIDI_CHAN_OMNI]);
    case 28u:
        /*
         * Self-checking terminator for format expansion.
         *
         * What: emits "lines=28\n" as the final line, where 28 is the count
         * of preceding data lines (cases 0u..27u: 17 original + 4 overlay
         * (S080 E10) + 3 scene globals (S080 F5, P2 Defect 5) + 2 overlay
         * roll rhythms (S083 follow-up) + bpm_fine and midi_chan_omni
         * (S104 P1; 26 before)). Why: gives consumers a cheap completeness
         * signal that distinguishes a genuine end-of-file from a
         * clean-but-early EOF after a crash. Adding a settings key requires
         * incrementing both its own case and this declared count. The boot
         * recovery prelude validates this count against the file's own line
         * count; the normal load path ignores it as an unknown key. MIDI
         * Learn rows are not settings lines: they live in midilrn.cfg (S083
         * §16). Affiliates: §6 of S057_SETTINGS_WRITE_SAFE.md.
         */
        return filesystem_formatAssignmentU16Line(dst, cap, "lines", 28u);
```

The recovery check (`:22528–22531`) compares the file's declared `lines=` with its
own line count, so a card written before S104 (`lines=26`) still validates. No
migration is needed: missing keys keep the defaults.

### O-9 — `Core/MIDI/MidiIn.c:36–51, :65–77` — **Modify** (the config) + **Add** (setter, getter, resolver)

Config (`:36–51`). Add `omni` and extend the comment's "What" with
"`omni`: the Omni channel O (1..16, 0 = off; S104 P1)" and RAM "4 B configuration":

```c
static struct {
	uint8_t global;
	uint8_t filter;
	uint8_t route;
	uint8_t omni;		/* S104 P1: Omni channel O, 0 = off */
} midiIn_config = { 1u, 0xFFu, 0u, 0u };
```

Add `#include "menu.h"` (for `menu_getActiveVoice()`) to the includes (`:20–30`).
Add after `midiIn_globalChannel()` (`:77`):

```c
/*
 * MIDI Omni channel O (S104 P1).
 *
 * What: midiIn_setOmniChannel() stores O (1..16; 0 or > 16 = off) after
 * releasing every note still held on the old O (midiNotes_omniReleaseAll(),
 * which ends paraphonic and gated voices with their own note-offs).
 * midiIn_omniTrack() is the one resolver every consumer asks first: for a
 * message on O it returns the selected track (menu_getActiveVoice(), 0..6),
 * else 0xFF. Why: a channel on O addresses "the selected track as if alone
 * on its own channel", whatever that track's `ch` (glb, off or a number) and
 * whatever G is: O wins when they are equal. The selected track is read per
 * message (no cache), because the user changes it at any time; held notes
 * keep their track through MidiNotes' latch instead. Inputs: channel 1..16.
 * Outputs: track or 0xFF. Context: foreground (midiIn_service()).
 * Accessors: menu_parseGlobalParam() (setter); midiNotes_handle(),
 * midiControl_voiceSlotsOnChannel(), midiControl_targetOnChannel(),
 * midiControl_handleCc(), midiProgram_programChange(),
 * midiProgram_bankChange(). Affiliates: MidiNotes' Omni latch.
 */
void midiIn_setOmniChannel(uint8_t channel)
{
	if (channel > 16u)
		channel = 0u;
	if (channel != midiIn_config.omni)
		midiNotes_omniReleaseAll();
	midiIn_config.omni = channel;
}

uint8_t midiIn_omniChannel(void)
{
	return midiIn_config.omni;
}

uint8_t midiIn_omniTrack(uint8_t channel)
{
	const uint8_t o = midiIn_config.omni;
	uint8_t track;

	if (o == 0u || channel != o)
		return 0xFFu;
	track = menu_getActiveVoice();
	return (track < 7u) ? track : 0xFFu;
}
```

### O-10 — `Core/MIDI/MidiIn.h:46` — **Add** after `uint8_t midiIn_globalChannel(void);`

```c
/* S104 P1, MIDI Omni channel O (0 off, 1..16). Setter: menu_parseGlobalParam()
** (releases the notes held on the old O). midiIn_omniTrack(channel): the
** selected track 0..6 when `channel` is O, else 0xFF; every channel consumer
** asks it first (contract in MidiIn.c). */
void    midiIn_setOmniChannel(uint8_t channel);
uint8_t midiIn_omniChannel(void);
uint8_t midiIn_omniTrack(uint8_t channel);
```

### O-11 — `Core/MIDI/MidiNotes.c:53–103` — **Modify** `midiNotes_handle()` + **Add** the latch

Add before `midiNotes_handle()` (after `midiNotes_setNoteAction()`, `:50`):

```c
/*
 * Omni held-note latch (S104 P1; +17 B SRAM1, approved).
 *
 * What: midiNotes_omniHeld is a 128-bit map of the MIDI notes held on the
 * Omni channel; midiNotes_omniLatch is the track they play (0xFF = none).
 * The first Omni note-on with no Omni note held latches the selected
 * track; every later note-on and every note-off goes to the latched track
 * until the map is empty again. Why: a note-off must reach the track its
 * note-on reached. Without the latch, a track change while a paraphonic
 * Colloid (MNM_PARA) or a gated voice holds a note sends the note-off to
 * the new track and leaves the old one hanging. A bitmap, not a count, so a
 * repeated note-on or a lost note-off cannot desynchronise it.
 * Inputs: Omni note-on/off in midiNotes_handle(). Outputs: the target track.
 * Lifetime: runtime only. Affiliates: midiIn_omniTrack(),
 * midiNotes_omniReleaseAll() (O change).
 */
static uint32_t midiNotes_omniHeld[4];
static uint8_t  midiNotes_omniLatch = 0xFFu;

static uint8_t midiNotes_omniAnyHeld(void)
{
	return (uint8_t)((midiNotes_omniHeld[0] | midiNotes_omniHeld[1] |
	                  midiNotes_omniHeld[2] | midiNotes_omniHeld[3]) != 0u);
}

/*
 * The one-track map of an Omni message (S104 P1).
 *
 * What: keeps the Scene's note roots and wide (Tetrakt) bits, puts `track`
 * alone on `channel` (every other track `off`) and sets G off, so
 * midiNoteMap_resolve() applies the own-channel rules (rule 1 from the
 * track's `nte` root, else rule 2's split, which a lone track owns
 * entirely) and never G's stripe, even when O equals G.
 * Inputs: a loaded map, track 0..6, channel O. Output: *map modified.
 * Accessors: midiNotes_handle(), midiNotes_omniReleaseAll().
 */
static void midiNotes_omniMap(midiNoteMap_settings_t *map, uint8_t track,
                              uint8_t channel)
{
	uint8_t t;

	map->global = 0u;
	for (t = 0u; t < MIDI_NOTEMAP_TRACKS; t++)
		map->channel[t] = (t == track) ? channel : MIDI_NOTEMAP_CHANNEL_OFF;
}

/* Contract in MidiNotes.h (S104 P1). */
void midiNotes_omniReleaseAll(void)
{
	const uint8_t o = midiIn_omniChannel();
	midiNoteMap_settings_t map;
	uint8_t n;

	if (midiNotes_omniLatch < MIDI_NOTEMAP_TRACKS && o != 0u) {
		midiNoteMap_load(scene_getActiveIndex(), &map);
		midiNotes_omniMap(&map, midiNotes_omniLatch, o);
		for (n = 0u; n < 128u; n++) {
			uint8_t track, internal, sub;

			if ((midiNotes_omniHeld[n >> 5] & (1u << (n & 31u))) == 0u)
				continue;
			if (!midiNoteMap_resolve(&map, o, n, &track, &internal, &sub))
				continue;
			buttonHandler_overlayMidiNoteOff(track, n);
			(void)midiNoteMode_noteOff(track, o, internal, sub);
		}
	}
	memset(midiNotes_omniHeld, 0, sizeof(midiNotes_omniHeld));
	midiNotes_omniLatch = 0xFFu;
}
```

In `midiNotes_handle()`, replace from `midiNoteMap_load(scene_getActiveIndex(), &map);`
(`:72`) through the resolve test (`:74`) with:

```c
	/*
	 * Resolve on the active Scene (Scene 17 while it is active), read per
	 * message (S082 R14). An unmapped note is not consumed.
	 * S104 P1, Omni: a note on O resolves through the one-track map of the
	 * latched track (the selected track when no Omni note is held), and the
	 * latch map is updated only for a note that resolved.
	 */
	midiNoteMap_load(scene_getActiveIndex(), &map);
	omni = midiIn_omniTrack(channel);
	if (omni != 0xFFu) {
		if (midiNotes_omniLatch < MIDI_NOTEMAP_TRACKS)
			omni = midiNotes_omniLatch;
		midiNotes_omniMap(&map, omni, channel);
	}
	if (!midiNoteMap_resolve(&map, channel, msg->data1, &track, &internal, &sub)) {
		if (omni != 0xFFu && !on)
			midiNotes_omniHeld[msg->data1 >> 5] &= ~(1u << (msg->data1 & 31u));
		return 0u;
	}
	if (omni != 0xFFu) {
		if (on) {
			midiNotes_omniHeld[msg->data1 >> 5] |= (1u << (msg->data1 & 31u));
			midiNotes_omniLatch = omni;
		} else {
			midiNotes_omniHeld[msg->data1 >> 5] &= ~(1u << (msg->data1 & 31u));
			if (!midiNotes_omniAnyHeld())
				midiNotes_omniLatch = 0xFFu;
		}
	}
```

- Add `uint8_t omni;` to the locals.
- Add `#include <string.h>` (for `memset`) if `MidiNotes.c` lacks it.
- The rest of the function is unchanged: the overlay, the mute test, and
  `midiNoteMode_noteOn(track, channel, …)`. Channel O is passed on, so
  `midiNoteMode_trackMode()` treats a Colloid on O as paraphonic whenever O ≠ G.
- **Known limit:** when O equals G, a Colloid on O stays single-trigger, because the
  paraphonic test compares with G (`MidiNoteModes.c:159`).

### O-12 — `Core/MIDI/MidiNotes.h:48` — **Add** after `uint8_t midiNotes_handle(const MidiMsg *msg);`

```c
/*
 * S104 P1: release every note held on the Omni channel (the latched track's
 * note-offs through the overlay and the note modes), then clear the latch.
 * Accessor: midiIn_setOmniChannel() (before O changes).
 */
void midiNotes_omniReleaseAll(void);
```

### O-13 — `Core/MIDI/MidiControl.c:79–130, :276` — **Modify** (slots, targets, onG)

**`midiControl_voiceSlotsOnChannel()`** (`:79`). Insert as the first statement:

```c
	/*
	 * S104 P1, Omni: a message on O addresses only the selected track's
	 * slot (track 7 shares slot 6), whatever G and the track channels are.
	 */
	{
		const uint8_t omni = midiIn_omniTrack(channel);

		if (omni != 0xFFu)
			return (uint8_t)(1u << ((omni >= 6u) ? 5u : omni));
	}
```

**`midiControl_targetOnChannel()`** (`:98`):
- replace `if (global != 0u && channel == global) return 1u;` with:

```c
	/* S104 P1: O is never G (O wins when equal). */
	const uint8_t omni = midiIn_omniTrack(channel);

	if (omni == 0xFFu && global != 0u && channel == global)
		return 1u;
```

  (the declaration moves to the top of the function with the others);
- in the `TRACK_LENGTH/SCALE/SHUFFLE` case (`:122–128`), first:

```c
			/* S104 P1: on O, only the selected track's own timing. */
			if (omni != 0xFFu)
				return (uint8_t)(d->voice_slot == omni);
```

**`midiControl_handleCc()`** (`:276–277`):

```c
	/* S104 P1: a message on the Omni channel is never a G message. */
	const uint8_t onG = (uint8_t)(global != 0u && channel == global &&
	                              midiIn_omniTrack(channel) == 0xFFu);
```

The rest is unchanged:
- CC1 and NRPN use `voiceSlots`, which is now Omni-aware;
- CC2 needs `onG`;
- Learn rows (`midiLearn_captureCc()`/`applyCc()`) see the literal channel.

### O-14 — `Core/MIDI/MidiProgram.c:28–62` — **Modify** (program change, bank change)

`midiProgram_programChange()`: after the `program > 15` test, add:

```c
	/*
	 * S104 P1, Omni: a program change on O assigns only the selected track
	 * to the Scene, as a PC on that track's own channel would.
	 */
	{
		const uint8_t omni = midiIn_omniTrack(channel);

		if (omni != 0xFFu) {
			seq_assignTracksToScene((uint8_t)(1u << omni), program);
			return 1u;
		}
	}
```

`midiProgram_bankChange()`: first statement:

```c
	/* S104 P1: a Bank change belongs to G only; on O it is not consumed. */
	if (midiIn_omniTrack(channel) != 0xFFu)
		return 0u;
```

### O-15 — host test `notemap_test` — **Modify** (`tools/midi_test/notemap_test.c`)

Add law **NM-OMNI**. Extract the real `midiNotes_omniMap()` from `MidiNotes.c`
with `tools/dsp_test/extract.py --func midiNotes_omniMap` (the test already links
`MidiNoteMap.c` alone), apply it to a settings snapshot, and resolve:
- an unassigned track alone gets notes 0..127 at internal 63 + n (clamped 127);
- a track with root 48 gets internal 63 at note 48;
- a Tetrakt (wide) track gets sub n mod 4, pitch 63 + n / 4;
- the same with G equal to O (no stripe).

Break: `sed 's/\tmap->global = 0u;//'` on the extracted fragment. With O = G the G
stripe then gives every note internal 63, and NM-OMNI must fail.

**Device check:**
- Omni into a paraphonic Colloid and a Tetrakt;
- change track while notes are held (no hung notes);
- CC1 on O moves the selected track's Morph;
- PC on O re-assigns only that track;
- set O = G: notes play the selected track and CC1 is no longer global Morph.

---

## 5. DJ mode (SHIFT+PLAY+PERF)

Behaviour (plan §5.2, accepted):
- DJ mode is PERF mode with a DJ screen; any MODE press closes it;
- pots: BPM, BPM hundredths, bar jump, step jump;
- COPY is tap tempo; BAR1/BAR2 nudge −/+ up to 5 %;
- the nudge drives the clock only;
- under external sync, pots, tap and nudge do nothing.

### 5.1 Sequencer: tempo to 1/100 BPM and the effective tempo

#### D-1 — `Core/Bank/Scene/Preset/ParameterArray.h` — **Add** after `PAR_MIDI_CHAN_OMNI,` (O-1)

```c
	/*
	 * S104 P1: the BPM's hundredths (0..99).
	 *
	 * What: the internal tempo is PAR_BPM + PAR_BPM_FINE / 100 BPM
	 * (seq_setTempoCenti(PAR_BPM x 100 + PAR_BPM_FINE)). Why: DJ mode sets
	 * the tempo to 1/100 BPM with its second pot and with tap tempo, and
	 * the setting must survive a power cycle (settings.cfg `bpm_fine`).
	 * No menu cell shows it: the Global page's `bpm` cell edits the whole
	 * BPM and keeps the fraction. Appended so no id is renumbered.
	 * Inputs: DJ pots and tap (menu_djSetTempoCenti()), settings load.
	 * Outputs: menu_parseGlobalParam() PAR_BPM/PAR_BPM_FINE case.
	 * Affiliates: filesystem settings writer/parser, sanitize.
	 */
	PAR_BPM_FINE,
```

#### D-2 — `Core/Menu/menu.c:1353` — **Add** after the O-2 line

```c
    /* S104 P1: the BPM's hundredths; no cell shows it (DJ mode writes it,
     * clamped to 0..99 there and on settings load). */
    [PAR_BPM_FINE] = DTYPE_0B127,
```

#### D-3 — `Core/Menu/menu.c:20517–20527` — **Modify** (`menu_parseGlobalParam()` `case PAR_BPM:`)

Anchor: `* BPM is Sequencer transport timing. Menu clamps zero to one before`

```c
    case PAR_BPM:
    case PAR_BPM_FINE:
        /*
         * Tempo, to 1/100 BPM since S104 P1.
         *
         * What: PAR_BPM (the whole BPM, 1..255) and PAR_BPM_FINE (its
         * hundredths, 0..99) together set the sequencer's base tempo
         * through seq_setTempoCenti(). Either id applies both, so a bulk
         * Global apply (PAR_BPM first, PAR_BPM_FINE later in the id order)
         * and a single edit give the same result. Menu clamps a zero BPM to
         * one, because the sequencer cannot run at 0 BPM, and an
         * out-of-range fraction to 0.
         * Inputs: parameter_values[PAR_BPM], [PAR_BPM_FINE] (already stored
         * by the caller). Outputs: Sequencer base tempo; synced LFOs follow
         * the rounded whole BPM (seq_setTempoCenti()).
         * Affiliates: menu_djSetTempoCenti(), filesystem settings,
         * seq_setTempoCenti().
         */
        (void)value;
        if (parameter_values[PAR_BPM] == 0u)
            parameter_values[PAR_BPM] = 1u;
        if (parameter_values[PAR_BPM_FINE] > 99u)
            parameter_values[PAR_BPM_FINE] = 0u;
        seq_setTempoCenti((uint16_t)((uint16_t)parameter_values[PAR_BPM] * 100u +
                                     parameter_values[PAR_BPM_FINE]));
        break;
```

#### D-4 — `Core/Hardware/SD/filesystem.c` — **Add** (defaults, sanitize, key)

| Line | Anchor | Add |
|---|---|---|
| `:3017` | `parameter_values[PAR_BPM] = 120u;` in `filesystem_resetSettingsToDefaults()` | `parameter_values[PAR_BPM_FINE] = 0u;   /* S104 P1: 120.00 BPM */` |
| sanitize (beside O-7) | `filesystem_sanitizeLoadedGlobals()` | `/* S104 P1: hundredths 0..99. */ if (parameter_values[PAR_BPM_FINE] > 99u) parameter_values[PAR_BPM_FINE] = 0u;` |
| key map (beside O-7) | `else if (strcmp(key, "midi_chan_omni") == 0)` | `else if (strcmp(key, "bpm_fine") == 0) *param = PAR_BPM_FINE;` |

The writer line is O-8 (`case 26u`).

#### D-5 — `Core/Sequencer/sequencer.c:892` — **Modify** (`static uint16_t seq_tempo = 120;`)

```c
/*
 * Tempo state (S104 P1: hundredths of a BPM and the DJ nudge).
 *
 * What: seq_tempoCenti is the base tempo in 1/100 BPM (100..65,500; the
 * menu's PAR_BPM x 100 + PAR_BPM_FINE, or the measured external tempo x 100
 * from clockSync/the trigger jack through seq_setBpm()). seq_tempoNudge is
 * the DJ-mode nudge as a fraction (-0.05..+0.05; 0 outside a nudge).
 * seq_tempoEff = base x (1 + nudge) in BPM; under external sync the nudge
 * is ignored. Why: the TIM3 tempo timer (seq_calcDeltaT()) and the
 * step-length gate (seq_trackStepSamples()) run at the effective tempo, so
 * a nudge moves the clock and MIDI clock out. Synced LFOs and delays read
 * seq_getBpm(), the rounded base, so a nudge does not re-tune them
 * (user, S104 P1). Writers: seq_setTempoCenti(), seq_setTempoNudge()
 * (foreground; clockSync also from TIM3), each a single aligned store; the
 * readers take one aligned load. RAM: 2 + 4 + 4 B SRAM1 (S104 P1, +8 B,
 * approved). Affiliates: seq_updateTempoEff(), menu DJ mode, buttonHandler
 * nudge ramp.
 */
static volatile uint16_t seq_tempoCenti = 12000u;
static volatile float    seq_tempoNudge = 0.0f;
static volatile float    seq_tempoEff   = 120.0f;
```

The comments at `:905` and `:2081` that name `seq_tempo` become `seq_tempoEff`.

#### D-6 — `Core/Sequencer/sequencer.c:2051–2075` — **Modify** (`seq_calcDeltaT()`, `seq_setBpm()`, `seq_getBpm()`) + **Add** the new tempo API

Anchor: `static void seq_calcDeltaT(uint16_t bpm)`

```c
/*
 * Tempo-timer period of one 96-PPQ tick (S104 P1: float, effective tempo).
 *
 * What: seq_deltaT = 60,000 ms / bpm / 96 x SYSTICK_TICKS_PER_MS, computed in
 * float from seq_tempoEff (floored at 0.01 BPM). Why: the period was an
 * integer 60000 / bpm (133 BPM ran at 133.04), and DJ mode needs 1/100 BPM
 * and the nudge. seq_tick() carries the fractional remainder ("rest"), so
 * the average tick rate is exact. Inputs: seq_tempoEff. Output: seq_deltaT.
 * Context: TIM3 (seq_tick(), seq_extClockTicks()).
 */
static void seq_calcDeltaT(void)
{
	const float bpm = (seq_tempoEff > 0.01f) ? seq_tempoEff : 0.01f;

	seq_deltaT = (60000.0f / bpm) / (float)SEQ_INTERNAL_PPQ *
	             (float)SYSTICK_TICKS_PER_MS;
	/* The transport tick stays uniform: fixed-grid patterns have no shuffle. */
}

/*
 * Recompute the effective tempo (S104 P1).
 * What: seq_tempoEff = centi / 100 x (1 + nudge); the nudge is ignored under
 * external sync (the external clock owns the tempo). Inputs: seq_tempoCenti,
 * seq_tempoNudge, seq_getExtSync(). Output: seq_tempoEff. Context: any;
 * one aligned float store. Accessors: seq_setTempoCenti(),
 * seq_setTempoNudge(), seq_setExtSyncSource().
 */
static void seq_updateTempoEff(void)
{
	const float base = (float)seq_tempoCenti * 0.01f;

	seq_tempoEff = seq_getExtSync() ? base : base * (1.0f + seq_tempoNudge);
}

/* Contract in sequencer.h (S104 P1). */
void seq_setTempoCenti(uint16_t centi)
{
	const uint16_t oldBpm = seq_getBpm();

	if (centi < 100u)
		centi = 100u;
	if (centi > 65500u)
		centi = 65500u;
	seq_tempoCenti = centi;
	seq_updateTempoEff();
	/* Synced LFOs follow the rounded whole BPM; recompute them only when it
	 * changes (a hundredths edit or an external tempo jitter of < 0.5 BPM
	 * leaves their rate alone). */
	if (seq_getBpm() != oldBpm)
		lfo_recalcSync();
}

uint16_t seq_getTempoCenti(void)
{
	return seq_tempoCenti;
}

/* Contract in sequencer.h (S104 P1). */
void seq_setTempoNudge(float nudge)
{
	if (nudge > 0.05f)
		nudge = 0.05f;
	if (nudge < -0.05f)
		nudge = -0.05f;
	seq_tempoNudge = nudge;
	seq_updateTempoEff();
}

float seq_getTempoNudge(void)
{
	return seq_tempoNudge;
}

//------------------------------------------------------------------------------
/*
 * Whole-BPM tempo write (menu before S104, clockSync, trigger jack).
 *
 * What: bpm 1..655 -> seq_setTempoCenti(bpm x 100). Why: the external tempo
 * sources measure whole BPM; 655 is the largest whole BPM whose hundredths
 * fit the 16-bit seq_tempoCenti. Inputs: bpm (0 is treated as 1).
 * Accessors: clockSync.c sync_tick(), triggerJacks.c.
 */
void seq_setBpm(uint16_t bpm)
{
	if (bpm == 0u)
		bpm = 1u;
	if (bpm > 655u)
		bpm = 655u;
	seq_setTempoCenti((uint16_t)(bpm * 100u));
}
//------------------------------------------------------------------------------
/* The rounded base tempo in whole BPM (S104 P1: from 1/100 BPM). Readers:
 * LFO sync, Space/CrumpBit delay sync, clockSync's first-window seed. */
uint16_t seq_getBpm()
{
	return (uint16_t)((seq_tempoCenti + 50u) / 100u);
}
```

Also in `seq_setExtSyncSource()` (`:7021–7028`), add `seq_updateTempoEff();` after
`seq_isSyncExternal = source;`, with the comment `/* S104 P1: the nudge applies only
to the internal clock. */`.

#### D-7 — `Core/Sequencer/sequencer.c:2085–2105` — **Modify** (`seq_trackStepSamples()`)

Anchor: `const uint64_t bpm = (seq_tempo != 0u) ? (uint64_t)seq_tempo : 1u;`

Replace the `bpm`, `den` and `samples` lines with:

```c
	/*
	 * The effective tempo in hundredths (S104 P1: 1/100 BPM, DJ nudge),
	 * never below 1 BPM. samples = ticksQ8 x fs x 60 x 100 / (256 x 96 x
	 * centi): the numerator and the denominator are both x 100 against the
	 * whole-BPM form, and 24,576 x bpm is even, so every whole tempo gives
	 * the earlier result bit for bit (law AL-T is unchanged).
	 *   numerator ticksQ8 x 264,648,000 (x 100 of 2,646,480): at most
	 *   49,152 x 264,648,000 = 1.3e13, in 64 bits;
	 *   denominator 24,576 x centi; + den / 2 rounds to the nearest sample.
	 */
	const float eff = seq_tempoEff;
	const uint64_t centi = (eff >= 1.0f) ? (uint64_t)(eff * 100.0f + 0.5f) : 100u;
	const uint64_t den = 24576u * centi;
	const uint64_t samples = (ticksQ8 * 264648000u + den / 2u) / den;
```

Update the contract comment in `sequencer.h:299–321` to read "the effective tempo
(seq_tempoEff, S104 P1: 1/100 BPM, nudged in DJ mode)" in place of "seq_tempo".

#### D-8 — `Core/Sequencer/sequencer.c:7097, :7150` — **Modify**

`seq_calcDeltaT(seq_tempo);` (both) → `seq_calcDeltaT();`

#### D-9 — `Core/Sequencer/sequencer.c` — **Add** after `seq_setResumeTick()` (ends `:7393`)

```c
/*
 * Master position for the DJ screen (S104 P1).
 *
 * What: *rep = the repetition 1..999 (wrapping), *bar = the bar inside it
 * 1..8, *step = the step inside the bar 1..16, from the master clock
 * (seq_elapsedPpqTicks / 24 = master 1/16 steps). A repetition is the
 * longest effective track length (seq_effectiveTrackLength[], 1..128 steps,
 * 0 read as 16) on the master 1/16 grid, so it is at most 8 bars (user,
 * S104 P1: one bar digit). A track slowed by its scale spans several shown
 * repetitions. Stopped: 1.1.1. Why: the DJ screen's RRR.B.SS. Inputs: the
 * running flag, the master tick, the effective lengths (single aligned
 * reads; TIM3 writes them). Outputs: the three values. Context: foreground.
 * Accessor: menu_repaintDjPage().
 */
void seq_getMasterPosition(uint16_t *rep, uint8_t *bar, uint8_t *step)
{
	uint32_t steps;
	uint32_t inRep;
	uint8_t longest = 0u;
	uint8_t t;

	if (!rep || !bar || !step)
		return;
	if (!seq_running) {
		*rep = 1u; *bar = 1u; *step = 1u;
		return;
	}
	for (t = 0u; t < NUM_TRACKS; t++) {
		uint8_t len = seq_effectiveTrackLength[t];

		if (len < 1u)
			len = NUM_STEPS_PER_BAR;
		if (len > longest)
			longest = len;
	}
	steps = seq_elapsedPpqTicks / SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP;
	inRep = steps % longest;
	*rep = (uint16_t)((steps / longest) % 999u + 1u);
	*bar = (uint8_t)(inRep / NUM_STEPS_PER_BAR + 1u);
	*step = (uint8_t)(inRep % NUM_STEPS_PER_BAR + 1u);
}

/*
 * Move the running master clock by whole ticks (S104 P1, DJ bar/step pots).
 *
 * What: under PRIMASK, moves seq_elapsedPpqTicks by delta (clamped at 0),
 * re-derives seq_masterStepClock / seq_masterStepCnt, moves every engaged
 * loop anchor by the same amount (clamped at 0), then
 * seq_realignActivePatternToMasterClock(): every track's cursor and DDA
 * phase from the new master tick (play-mode aware), the FX sequence on the
 * master grid, the R12 menu overrides cleared, ramps ended (a position
 * break, S081 P2), echo marks expired. Returns 1 when it moved.
 * Why: DJ pots 3/4 shift the phase of everything by a bar or a step while
 * playing (beat matching). Moving the anchors keeps a running loop's
 * window on its own phase. MIDI slaves are not moved: no SPP is sent while
 * running. Refused while stopped, under external sync (the external clock
 * owns the position), and before the first tick (seq_initialSchedulerTick).
 * Inputs: delta in 96-PPQ ticks (384 = a bar, 24 = a step).
 * Outputs: the master clock and every track position. Context: foreground;
 * the PRIMASK section is bounded (seven tracks, the 64-bit override memset,
 * the FX realign; the nested PRIMASK sections inside save and restore).
 * Accessor: menu_djKnob().
 */
uint8_t seq_jumpMasterTicks(int32_t delta)
{
	uint32_t primask;
	int64_t target;
	uint8_t t;

	if (!seq_running || seq_getExtSync() || seq_initialSchedulerTick ||
	    delta == 0)
		return 0u;
	primask = __get_PRIMASK();
	__disable_irq();
	target = (int64_t)seq_elapsedPpqTicks + (int64_t)delta;
	if (target < 0)
		target = 0;
	delta = (int32_t)(target - (int64_t)seq_elapsedPpqTicks);
	seq_elapsedPpqTicks = (uint32_t)target;
	seq_masterStepClock = (uint16_t)(seq_elapsedPpqTicks /
	                                 SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP);
	seq_masterStepCnt = (uint8_t)seq_masterStepClock;
	if (seq_loopActive) {
		for (t = 0u; t < NUM_TRACKS; t++) {
			int64_t a;

			if ((seq_loopTrackMask & (uint8_t)(1u << t)) == 0u)
				continue;
			a = (int64_t)seq_loopAnchor[t] + (int64_t)delta;
			seq_loopAnchor[t] = (a < 0) ? 0u : (uint32_t)a;
		}
	}
	seq_realignActivePatternToMasterClock();
	__set_PRIMASK(primask);
	return 1u;
}
```

#### D-10 — `Core/Sequencer/sequencer.h:296–297` — **Modify / Add**

```c
/*
 * Tempo (S104 P1: 1/100 BPM and the DJ nudge; contract in sequencer.c).
 * seq_setBpm(): whole BPM 1..655 (external sources). seq_setTempoCenti():
 * the base tempo in 1/100 BPM (menu PAR_BPM x 100 + PAR_BPM_FINE).
 * seq_getBpm(): the rounded base (LFO and delay sync). seq_setTempoNudge():
 * -0.05..+0.05 of the base, internal clock only (DJ BAR buttons).
 */
void seq_setBpm(uint16_t bpm);
uint16_t seq_getBpm(void);
void seq_setTempoCenti(uint16_t centi);
uint16_t seq_getTempoCenti(void);
void seq_setTempoNudge(float nudge);
float seq_getTempoNudge(void);
/* S104 P1, DJ mode (contracts in sequencer.c). */
void seq_getMasterPosition(uint16_t *rep, uint8_t *bar, uint8_t *step);
uint8_t seq_jumpMasterTicks(int32_t delta);
```

#### D-11 — `tools/dsp_test/modules/seq_steplen.c:29, :63, :70, :74, :78` — **Modify**

- `static uint16_t seq_tempo = 120u;` → `static volatile float seq_tempoEff = 120.0f;`
  with the comment `/* S104 P1: the function reads the effective tempo */`;
- `seq_tempo = bpms[b];` → `seq_tempoEff = (float)bpms[b];`;
- `seq_tempo = 120u;` → `seq_tempoEff = 120.0f;`;
- `seq_tempo = 1u;` → `seq_tempoEff = 1.0f;`;
- `seq_tempo = 0u;` → `seq_tempoEff = 0.0f;` (still read as 1 BPM).

AL-T must pass unchanged. That proves the ×100 form is bit-identical for whole
tempos. The existing 32-bit break still applies: its `sed` pattern on the `ticksQ8`
line is untouched by D-7.

#### D-12 — host test `seq_tempo` — **Add** (`tools/dsp_test/modules/seq_tempo.c`, Makefile rule)

- **Build:** extract `seq_calcDeltaT`, `seq_updateTempoEff`, `seq_setTempoCenti`,
  `seq_setTempoNudge`, `seq_getBpm`, `seq_getTempoCenti` from `sequencer.c`. Stub
  `seq_getExtSync()` (a host flag), `lfo_recalcSync()` (a counter),
  `SEQ_INTERNAL_PPQ` 96, `SYSTICK_TICKS_PER_MS` 4.
- **Laws:**
  - **TP-PERIOD:** for 120.00, 133.00, 128.37 and 255.99 BPM,
    `seq_deltaT × 96 / 4 = 60,000 / bpm` within 1e-5 relative.
  - **TP-NUDGE:** nudge +0.05 gives `seq_tempoEff = 1.05 × base`; with the
    external-sync flag set, `seq_tempoEff = base`; values beyond ±0.05 clamp.
  - **TP-ROUND:** `seq_getBpm()` rounds (12049 → 120, 12050 → 121).
    `lfo_recalcSync()` runs only when the rounded value changes.
- **Break:** `sed` the period line to `(float)(60000u / (uint32_t)bpm)`. TP-PERIOD
  must fail at 133.00.

### 5.2 Menu: the DJ screen, pots and tempo writes

#### D-13 — `Core/Menu/menu.h:639` — **Add** after `uint8_t menu_chainMenuIsOpen(void);`

```c
/*
 * DJ mode (S104 P1; contract in menu.c). menu_djOpen()/Close(): the DJ
 * screen over PERFORMANCE_PAGE (ButtonHandler opens it on SHIFT+PLAY+PERF and
 * closes it on any MODE press). menu_djIsOpen(): 1 while shown.
 * menu_djSetTempoCenti(): set the base tempo in 1/100 BPM (pots, tap).
 */
void menu_djOpen(void);
void menu_djClose(void);
uint8_t menu_djIsOpen(void);
void menu_djSetTempoCenti(uint16_t centi);
```

#### D-14 — `Core/Menu/menu.c` — **Add** the DJ state and functions, after `menu_chainMenuIsOpen()` (`:10266–10269`)

```c
/*
 * DJ mode (S104 P1).
 *
 * What: menu_djActive is set while the DJ screen replaces the PERF page's
 * cells. The mode is still SELECT_MODE_PERF and menu_activePage stays
 * PERFORMANCE_PAGE, so VOICE (mutes, overlay triggers), SEQ (Scenes) and
 * SELECT (looper) act exactly as in PERF. Shown: top row "BPM 128.50 +1.2%"
 * (EXT under external sync; the nudge field is blank at 0), bottom row
 * "POS RRR.B.SS" (seq_getMasterPosition()). Pots: 1 = whole BPM, 2 = BPM
 * hundredths, 3 = bar jump, 4 = step jump (running only). The encoder is
 * inert. menu_djPaintTick paces the position refresh.
 * Why: a tempo/position surface for playing live (user, S104 P1).
 * Inputs: ButtonHandler (open/close), pots, the runtime service.
 * Outputs: LCD rows, tempo writes, master-clock jumps. Closed by
 * menu_djClose() (every MODE press through
 * buttonHandler_closeLatchedSurfaces()) and by any switch to another page
 * (menu_switchPage()). RAM: 1 + 2 B SRAM1 (approved).
 * Affiliates: menu_repaintDjPage(), menu_djKnob(), buttonHandler DJ
 * gestures, seq_setTempoCenti(), seq_jumpMasterTicks().
 */
#define MENU_DJ_PAINT_MS 25u
static uint8_t  menu_djActive = 0u;
static uint16_t menu_djPaintTick = 0u;

void menu_djOpen(void)
{
    menu_djActive = 1u;
    editModeActive = 0u;
    menu_djPaintTick = time_sysTick;
    menu_repaintAll();
}

void menu_djClose(void)
{
    if (!menu_djActive)
        return;
    menu_djActive = 0u;
    menu_repaintAll();
}

uint8_t menu_djIsOpen(void)
{
    return menu_djActive;
}

/*
 * Set the base tempo in 1/100 BPM (S104 P1, DJ pots and tap).
 * What: clamps to 1.00..255.99 BPM, splits into PAR_BPM / PAR_BPM_FINE,
 * applies through menu_parseGlobalParam() (seq_setTempoCenti()) and
 * restarts the settings.cfg debounce. Why: the Global commit marks settings
 * dirty only for MENU_MIDI_PAGE/RECORDING_PAGE cells (menu_cellCommitValue()),
 * and the DJ writes are not cells. Inputs: centi. Outputs: the two
 * parameter bytes, the sequencer tempo, a settings write.
 * Accessors: menu_djKnob(), buttonHandler_djTap().
 */
void menu_djSetTempoCenti(uint16_t centi)
{
    if (centi < 100u)
        centi = 100u;
    if (centi > 25599u)
        centi = 25599u;
    parameter_values[PAR_BPM] = (uint8_t)(centi / 100u);
    parameter_values[PAR_BPM_FINE] = (uint8_t)(centi % 100u);
    menu_parseGlobalParam(PAR_BPM, parameter_values[PAR_BPM]);
    filesystem_markSettingsDirty();
    menu_knobs_dirty = 1u;
}

/*
 * One DJ pot turn (S104 P1).
 * What: pot 1 moves the whole BPM by delta (the hundredths kept), pot 2 the
 * hundredths by delta (carrying into the whole BPM), pot 3 the running
 * master clock by delta bars (384 ticks), pot 4 by delta steps (24 ticks).
 * Nothing under external sync (the external clock owns tempo and position).
 * Inputs: knob 0..3, signed delta. Outputs: menu_djSetTempoCenti() or
 * seq_jumpMasterTicks(); a repaint request.
 * Accessor: menu_parseKnobDelta().
 */
static void menu_djKnob(uint8_t knobNr, int8_t delta)
{
    int32_t centi = (int32_t)parameter_values[PAR_BPM] * 100 +
                    (int32_t)parameter_values[PAR_BPM_FINE];

    if (delta == 0 || seq_getExtSync())
        return;
    switch (knobNr) {
    case 0:
        centi += (int32_t)delta * 100;
        break;
    case 1:
        centi += (int32_t)delta;
        break;
    case 2:
        (void)seq_jumpMasterTicks((int32_t)delta * 384);
        menu_knobs_dirty = 1u;
        return;
    case 3:
        (void)seq_jumpMasterTicks((int32_t)delta * 24);
        menu_knobs_dirty = 1u;
        return;
    default:
        return;
    }
    if (centi < 100)
        centi = 100;
    if (centi > 25599)
        centi = 25599;
    menu_djSetTempoCenti((uint16_t)centi);
}
```

#### D-15 — `Core/Menu/menu.c:15632` — **Add** the painter before `static void menu_repaintGeneric(void)`, and the hook as its first statement

```c
/*
 * Write v as `digits` decimal digits, zero-padded (S104 P1, DJ screen).
 */
static void menu_djPutDec(char *dst, uint32_t v, uint8_t digits)
{
    while (digits-- != 0u) {
        dst[digits] = (char)('0' + (v % 10u));
        v /= 10u;
    }
}

/*
 * Paint the DJ screen into editDisplayBuffer (S104 P1).
 *
 * What: row 0 "BPM ddd.dd +n.n%" ("EXT" under external sync; whole BPM
 * right-aligned in 3 cells; the nudge in tenths of a percent, blank at 0),
 * row 1 "POS RRR.B.SS" (seq_getMasterPosition()). Why: the DJ screen
 * replaces the PERF cells while menu_djActive is set; painting through
 * menu_repaintGeneric() means every caller (menu_repaint(), the knob
 * service, the marker retry) shows it and sendDisplayBuffer() sends only the
 * changed characters. Inputs: seq_getTempoCenti(), seq_getTempoNudge(),
 * seq_getExtSync(), the master position. Outputs: both shadow rows; the
 * cursor off. Accessor: menu_repaintGeneric().
 */
static void menu_repaintDjPage(void)
{
    const uint16_t centi = seq_getTempoCenti();
    const uint16_t whole = (uint16_t)(centi / 100u);
    const float n = seq_getTempoNudge();
    const int32_t tenths = (int32_t)(n * 1000.0f + ((n >= 0.0f) ? 0.5f : -0.5f));
    uint16_t rep;
    uint8_t bar;
    uint8_t step;

    memset(editDisplayBuffer[0], ' ', 16u);
    memset(editDisplayBuffer[1], ' ', 16u);
    editDisplayBuffer[0][16] = '\0';
    editDisplayBuffer[1][16] = '\0';
    memcpy(&editDisplayBuffer[0][0], seq_getExtSync() ? "EXT" : "BPM", 3u);
    if (whole >= 100u)
        editDisplayBuffer[0][4] = (char)('0' + whole / 100u);
    if (whole >= 10u)
        editDisplayBuffer[0][5] = (char)('0' + (whole / 10u) % 10u);
    editDisplayBuffer[0][6] = (char)('0' + whole % 10u);
    editDisplayBuffer[0][7] = '.';
    menu_djPutDec(&editDisplayBuffer[0][8], centi % 100u, 2u);
    if (tenths != 0) {
        const uint32_t a = (uint32_t)((tenths < 0) ? -tenths : tenths);

        editDisplayBuffer[0][11] = (tenths < 0) ? '-' : '+';
        editDisplayBuffer[0][12] = (char)('0' + (a / 10u) % 10u);
        editDisplayBuffer[0][13] = '.';
        editDisplayBuffer[0][14] = (char)('0' + a % 10u);
        editDisplayBuffer[0][15] = '%';
    }
    seq_getMasterPosition(&rep, &bar, &step);
    memcpy(&editDisplayBuffer[1][0], "POS", 3u);
    menu_djPutDec(&editDisplayBuffer[1][4], rep, 3u);
    editDisplayBuffer[1][7] = '.';
    editDisplayBuffer[1][8] = (char)('0' + bar % 10u);
    editDisplayBuffer[1][9] = '.';
    menu_djPutDec(&editDisplayBuffer[1][10], step, 2u);
    cur_want_on = 0u;
}
```

In `menu_repaintGeneric()`, before `if (menu_stepAutomationPageActive()) {`:

```c
    /* S104 P1: the DJ screen owns the PERF page's LCD while it is open. */
    if (menu_djActive && menu_activePage == PERFORMANCE_PAGE) {
        menu_repaintDjPage();
        return;
    }
```

`menu_serviceKnobRepaint()` (`:18489`) needs no change: it calls
`menu_repaintGeneric()` and then `sendDisplayBuffer()`, so a DJ pot's repaint shows
the DJ screen.

#### D-16 — `Core/Menu/menu.c` `menu_switchPage()` — **Add** before its first `menu_stepAutomationReset();` (≈ `:19876`)

Anchor (in `menu_switchPage()`): `menu_stepAutomationReset();` after the comment
`* Leaving or re-entering a Menu context invalidates the variable-length`

```c
    /*
     * S104 P1: DJ mode lives on PERFORMANCE_PAGE only. A real switch to any
     * other page (a MIDI-origin Bank load, the chain editor, a mode press)
     * ends it; PERF re-entries (the PERF SHIFT release) keep it. Placed
     * after the busy/queued-exit returns, so only a page change that happens
     * closes it.
     */
    if (pageNr != PERFORMANCE_PAGE)
        menu_djActive = 0u;
```

#### D-17 — `Core/Menu/menu.c:18225` — **Add** in `menu_parseKnobDelta()` after `if (menu_storageBusy && !menu_backgroundLoadReleasesPanel()) return;`

```c
    /*
     * S104 P1: the DJ screen owns the four pots (tempo, hundredths, bar
     * jump, step jump) before Learn target selection, copy/clear and the
     * cell editor: the PERF cells are hidden, so no pot may edit them.
     */
    if (menu_djActive && menu_activePage == PERFORMANCE_PAGE) {
        if (knobNr < ENDLESS_POT_COUNT)
            menu_djKnob(knobNr, delta);
        return;
    }
```

#### D-18 — `Core/Menu/menu.c:17402–17410` — **Add** in `menu_parseEncoder()` after the local declarations

```c
    /*
     * S104 P1: the encoder is inert on the DJ screen (its PERF cells are
     * hidden). The button state is still tracked so no stale click is seen
     * after the screen closes.
     */
    if (menu_djActive && menu_activePage == PERFORMANCE_PAGE) {
        lastEncoderButton = button;
        return;
    }
```

#### D-19 — `Core/Menu/menu.c:18570` — **Add** in `menu_serviceRuntimeWidgets()` after its busy `return;`

```c
    /*
     * S104 P1: the DJ screen's position follows the master clock. A repaint
     * every MENU_DJ_PAINT_MS (25 ms) through menu_repaint(); the diff paint
     * sends only the characters that changed (usually the step digits).
     * Paused under the screensaver, as the live Scene refresh is.
     */
    if (menu_djActive && menu_activePage == PERFORMANCE_PAGE &&
        !screensaver_isActive() &&
        (uint16_t)(time_sysTick - menu_djPaintTick) >= MENU_DJ_PAINT_MS) {
        menu_djPaintTick = time_sysTick;
        menu_repaint();
    }
```

### 5.3 ButtonHandler: entry, exit, COPY tap, BAR nudge

#### D-20 — `Core/Hardware/frontPanel/buttonHandler.c:358` — **Add** after `static uint8_t bh_chainPlayArmed = 0u;`

```c
/*
 * DJ-mode gesture state (S104 P1; +17 B SRAM1, approved).
 *
 * What: bh_djCopyConsume swallows the release of a COPY press taken as a
 * tap. bh_djBar: bit 0 BAR1 held, bit 1 BAR2 held, bit 2 the last pressed is
 * BAR2 (with both held the last pressed wins). bh_djDownStep is the ramp-down
 * step per 10 ms after the last BAR release (the release value / 50, so the
 * nudge reaches 0 in 0.5 s). bh_djRampTick paces the nudge service on the
 * 1 ms time_sysTick. bh_djTapFirstUs / bh_djTapLastUs / bh_djTapCount are
 * the tap sequence on the TIM2 microsecond counter (unsigned deltas through
 * timebase_tim2Delta()). Why: DJ mode's COPY tap tempo and BAR nudge (user,
 * S104 P1). The nudge value itself is Sequencer's (seq_getTempoNudge()).
 * Inputs: COPY/BAR presses while menu_djIsOpen(), buttonHandler_tick().
 * Outputs: seq_setTempoNudge(), menu_djSetTempoCenti(). Affiliates:
 * buttonHandler_djTap(), buttonHandler_djBarPress()/Release(),
 * buttonHandler_djNudgeService().
 */
#define BH_DJ_RAMP_MS          10u
#define BH_DJ_NUDGE_MAX        0.05f
#define BH_DJ_NUDGE_UP_STEP    (BH_DJ_NUDGE_MAX * (float)BH_DJ_RAMP_MS / 2000.0f)
#define BH_DJ_RELEASE_STEPS    50u          /* 0.5 s of 10 ms steps */
#define BH_DJ_TAP_GAP_US       2000000u     /* a longer gap starts a new tap run */
static uint8_t  bh_djCopyConsume = 0u;
static uint8_t  bh_djBar = 0u;
static float    bh_djDownStep = 0.0f;
static uint16_t bh_djRampTick = 0u;
static uint32_t bh_djTapFirstUs = 0u;
static uint32_t bh_djTapLastUs = 0u;
static uint8_t  bh_djTapCount = 0u;
```

#### D-21 — `Core/Hardware/frontPanel/buttonHandler.c` — **Add** the DJ functions after `buttonHandler_chromaPulseService()` (ends `:1771`), before `buttonHandler_tick()`

```c
/*
 * COPY tap in DJ mode (S104 P1).
 * What: a tap more than 2 s after the last one starts a new run (the first
 * tap only stamps). From the second tap: tempo = 60 s / (the mean interval
 * since the run's first tap), to 1/100 BPM, through menu_djSetTempoCenti()
 * (clamped 1.00..255.99). After 255 taps the run restarts from the latest
 * tap (the count is one byte). The COPY LED flashes on every tap. Nothing
 * changes under external sync. Inputs: TIM2 now. Outputs: the tap state, a
 * tempo write. Accessor: processPress() DJ branch.
 */
static void buttonHandler_djTap(void)
{
    const uint32_t now = timebase_tim2Now();

    led_flashLed(LED_COPY);
    if (seq_getExtSync())
        return;
    if (bh_djTapCount == 0u || bh_djTapCount == 255u ||
        timebase_tim2Delta(now, bh_djTapLastUs) > BH_DJ_TAP_GAP_US) {
        bh_djTapFirstUs = (bh_djTapCount == 255u) ? bh_djTapLastUs : now;
        bh_djTapLastUs = now;
        bh_djTapCount = (bh_djTapCount == 255u) ? 2u : 1u;
        if (bh_djTapCount == 1u)
            return;
    } else {
        bh_djTapLastUs = now;
        bh_djTapCount++;
    }
    {
        const uint32_t span = timebase_tim2Delta(now, bh_djTapFirstUs);
        const uint32_t mean = span / (uint32_t)(bh_djTapCount - 1u);
        uint64_t centi;

        if (mean == 0u)
            return;
        centi = (6000000000ULL + (uint64_t)(mean / 2u)) / (uint64_t)mean;
        if (centi > 25599u)
            centi = 25599u;
        menu_djSetTempoCenti((uint16_t)centi);
    }
}

/*
 * BAR press/release in DJ mode (S104 P1). bar: 1 = BAR1 (nudge down),
 * 2 = BAR2 (nudge up). A press marks the button held and the last pressed;
 * the release clears it, hands "last pressed" to the button still held, and
 * when none is held fixes the ramp-down step from the current nudge (so it
 * reaches 0 in 0.5 s). The BAR LED is lit while its button is held.
 * Accessors: processPress()/processRelease() DJ branches.
 */
static void buttonHandler_djBarPress(uint8_t bar)
{
    bh_djBar = (uint8_t)((bh_djBar | bar) & 0x03u);
    if (bar == 2u)
        bh_djBar |= 0x04u;
    led_setValue(1u, (bar == 1u) ? LED_BAR1 : LED_BAR2);
}

static void buttonHandler_djBarRelease(uint8_t bar)
{
    bh_djBar = (uint8_t)(bh_djBar & (uint8_t)~bar);
    if ((bh_djBar & 0x03u) == 0x01u)
        bh_djBar &= (uint8_t)~0x04u;
    else if ((bh_djBar & 0x03u) == 0x02u)
        bh_djBar |= 0x04u;
    if ((bh_djBar & 0x03u) == 0u)
        bh_djDownStep = fabsf(seq_getTempoNudge()) / (float)BH_DJ_RELEASE_STEPS;
    led_setValue(0u, (bar == 1u) ? LED_BAR1 : LED_BAR2);
}

/*
 * The DJ nudge ramp (S104 P1), every pass from buttonHandler_tick().
 * What: per 10 ms step: while a BAR is held, the nudge moves 0.025 % toward
 * the held direction (2.5 %/s, so 0 to +-5 % in 2 s), clamped at +-5 %;
 * with none held it moves toward 0 by bh_djDownStep. A re-press continues
 * from the current value. At most 20 steps are caught up after a stall;
 * then the pacing re-anchors. Under external sync the nudge is set to 0.
 * The ramp runs whether or not the DJ screen is open, so a nudge left when
 * the screen closes still returns to 0. Writes only on a change.
 * Inputs: bh_djBar, bh_djDownStep, time_sysTick. Outputs:
 * seq_setTempoNudge(). Accessor: buttonHandler_tick().
 */
static void buttonHandler_djNudgeService(void)
{
    uint8_t steps = 0u;
    float n;
    float old;

    while ((uint16_t)(time_sysTick - bh_djRampTick) >= BH_DJ_RAMP_MS &&
           steps < 20u) {
        bh_djRampTick = (uint16_t)(bh_djRampTick + BH_DJ_RAMP_MS);
        steps++;
    }
    if ((uint16_t)(time_sysTick - bh_djRampTick) >= BH_DJ_RAMP_MS)
        bh_djRampTick = time_sysTick;
    if (steps == 0u)
        return;
    old = seq_getTempoNudge();
    n = old;
    if (seq_getExtSync()) {
        n = 0.0f;
    } else {
        while (steps-- != 0u) {
            if ((bh_djBar & 0x03u) != 0u) {
                n += (bh_djBar & 0x04u) ? BH_DJ_NUDGE_UP_STEP
                                        : -BH_DJ_NUDGE_UP_STEP;
                if (n > BH_DJ_NUDGE_MAX)
                    n = BH_DJ_NUDGE_MAX;
                if (n < -BH_DJ_NUDGE_MAX)
                    n = -BH_DJ_NUDGE_MAX;
            } else if (n > 0.0f) {
                n = (n > bh_djDownStep) ? (n - bh_djDownStep) : 0.0f;
            } else if (n < 0.0f) {
                n = (-n > bh_djDownStep) ? (n + bh_djDownStep) : 0.0f;
            }
        }
    }
    if (n != old)
        seq_setTempoNudge(n);
}
```

`#include <math.h>` for `fabsf`, if `buttonHandler.c` lacks it.

#### D-22 — `Core/Hardware/frontPanel/buttonHandler.c:1773–1776` — **Add** at the start of `buttonHandler_tick()`

```c
    /* S104 P1: the DJ nudge ramp (runs also after the DJ screen closes). */
    buttonHandler_djNudgeService();
```

#### D-23 — `Core/Hardware/frontPanel/buttonHandler.c:2596` — **Add** in `handleModeButtons()` before `if (buttonHandler_getShift() && mode == SELECT_MODE_STEP)`

```c
    /*
     * SHIFT+PLAY+PERF opens DJ mode (S104 P1).
     *
     * What: with SHIFT held and the SHIFT+PLAY chord armed (PLAY held), the
     * PERF MODE button enters PERF mode (as a plain PERF press, without
     * arming the fill hold) and opens the DJ screen on PERFORMANCE_PAGE.
     * Every latched surface closes first (an open DJ screen too, so a
     * repeat of the chord reopens it), the chromatic keyboard is left (DJ
     * takes BAR1/BAR2), and the BAR LEDs go dark until a nudge.
     * Why: SHIFT+PERF alone is the Effect mode ((mode + 4) & 7 below), so
     * the chord needs its own branch ahead of that arithmetic (user,
     * S104 P1). Inputs: SHIFT, bh_chainPlayArmed, mode. Outputs: PERF mode,
     * the DJ screen. Affiliates: menu_djOpen(),
     * buttonHandler_closeLatchedSurfaces(), the SHIFT+PLAY chord (BUT_START_STOP).
     */
    if (buttonHandler_getShift() && bh_chainPlayArmed &&
        mode == SELECT_MODE_PERF) {
        buttonHandler_closeLatchedSurfaces(1u, 0u);
        if (bh_chroma.which != 0u)
            buttonHandler_chromaExit();
        buttonHandler_setMorphVoiceMode(0u);
        bh_state.selectButtonMode = SELECT_MODE_PERF;
        led_clearAllBlinkLeds();
        led_setMode2(SELECT_MODE_PERF);
        led_clearSequencerLeds();
        led_clearSelectLeds();
        menu_switchPage(PERFORMANCE_PAGE);
        menu_switchSubPage(0);
        menu_resetActiveParameter();
        menu_djOpen();
        led_setValue(0u, LED_BAR1);
        led_setValue(0u, LED_BAR2);
        return;
    }
```

#### D-24 — `Core/Hardware/frontPanel/buttonHandler.c:2553` — **Add** in `buttonHandler_closeLatchedSurfaces()` before `if (leaving_perf)`

```c
    /* S104 P1: every MODE press (and the MIDI-origin Bank load) ends DJ mode. */
    if (menu_djIsOpen())
        menu_djClose();
```

#### D-25 — `Core/Hardware/frontPanel/buttonHandler.c:3419–3425` — **Add** in `processPress()` before `if (buttonHandler_chromaPress(buttonNr))`

```c
    /*
     * S104 P1: DJ mode takes COPY (tap tempo) and BAR1/BAR2 (nudge down/up)
     * before every other routing (the chromatic keyboard, held COPY and live
     * erase, the PERF overlay and looper BAR meanings). Copy/clear and live
     * erase are unavailable while the DJ screen is open.
     */
    if (menu_djIsOpen()) {
        if (buttonNr == BUT_COPY) {
            bh_djCopyConsume = 1u;
            buttonHandler_djTap();
            return;
        }
        if (buttonNr == BUT_BAR1 || buttonNr == BUT_BAR2) {
            buttonHandler_djBarPress((buttonNr == BUT_BAR1) ? 1u : 2u);
            return;
        }
    }
```

#### D-26 — `Core/Hardware/frontPanel/buttonHandler.c:3895–3898` — **Add** at the start of `processRelease()`, before `if (buttonHandler_chromaRelease(buttonNr))`

```c
    /*
     * S104 P1: releases of presses DJ mode took, whether or not the screen
     * is still open (a MODE press may close it while a BAR is held; that BAR's
     * release still ends its nudge hold).
     */
    if (buttonNr == BUT_COPY && bh_djCopyConsume) {
        bh_djCopyConsume = 0u;
        return;
    }
    if ((buttonNr == BUT_BAR1 && (bh_djBar & 0x01u)) ||
        (buttonNr == BUT_BAR2 && (bh_djBar & 0x02u))) {
        buttonHandler_djBarRelease((buttonNr == BUT_BAR1) ? 1u : 2u);
        return;
    }
```

#### D-27 — `Core/Hardware/frontPanel/buttonHandler.c:1456` and `:2143` — **Modify** (BAR LED writers)

In `buttonHandler_setOverlay()` (`:1456`) and `buttonHandler_loopCheckDisengage()`
(`:2143`), add `&& !menu_djIsOpen()` to the `SELECT_MODE_PERF` test, with the
comment `/* S104 P1: in DJ mode the BAR LEDs show the nudge buttons. */`.

**Device checks (§5):**
- SHIFT+PLAY+PERF opens the screen from every mode; every MODE button closes it,
  PERF included;
- pots 1/2 change the tempo and `settings.cfg` keeps it over a power cycle;
- pots 3/4 jump while running, including with a loop engaged; nothing happens
  stopped;
- COPY taps at 120 BPM read 120.00 ± 0.2;
- a BAR held 2 s reads ±5.0 % and returns to 0 within 0.5 s of release;
- external sync shows `EXT` and ignores the controls;
- the `cpu` widget is unchanged.

---

## 6. One-shot LFOs

Waves (plan §8.2, accepted): `xtr` = 8; one-shots 9..17 =
`si1 tr1 su1 sd1 sq1 rn1 xu1 xd1 xt1`.
- A one-shot plays one cycle of its base wave, then holds its end value.
- `sq1` is inverted (1 then 0), and its start delay outputs 0.
- `rn1` draws once when it fires.
- On a one-shot, the offset is a start delay of offset / 127 of a cycle.
- Retrigger `off`: runs once, then holds.

### F-1 — `Core/DSPAudio/lfo.h:58` — **Add** after `#define LFO_EXP_DOWN	0x07`

```c
/*
 * S104 P1: the exponential triangle and the one-shot waves.
 *
 * What: LFO_EXP_TRI (8, "xtr") is t^3 with t the triangle 1 - |2p - 1|
 * (exponential up, then down). Values 9..17 are the one-shot forms of the
 * nine looping waves in their order (base = value - LFO_OS_FIRST): each plays
 * one cycle and holds its end value until the next retrigger; LFO_OS_REC is
 * inverted (1 for the first half, then 0) and LFO_OS_NOISE holds one random
 * value drawn when it fires. LFO_OS_RUN/PRE/DONE are Lfo.osState values:
 * PRE is the start delay (the phase runs up to its wrap from -phaseOffset),
 * DONE holds the end. LFO_WRAP_MASK has a bit for every wave that acts on
 * a phase wrap (noise, every one-shot), the one test lfo_stepBlock() makes.
 * Why: one-shot LFOs with a start delay (knowledge_files/drafts/
 * ENHANCED_FEATURES.md, user S104 P1). Stored wave values 0..7 keep their
 * meaning; LFO_WAVE_COUNT is the menu list length (lfoWaveNames[]).
 */
#define LFO_EXP_TRI		0x08
#define LFO_OS_FIRST	0x09
#define LFO_OS_SINE		0x09
#define LFO_OS_TRI		0x0A
#define LFO_OS_SAW_UP	0x0B
#define LFO_OS_SAW_DOWN	0x0C
#define LFO_OS_REC		0x0D
#define LFO_OS_NOISE	0x0E
#define LFO_OS_EXP_UP	0x0F
#define LFO_OS_EXP_DOWN	0x10
#define LFO_OS_EXP_TRI	0x11
#define LFO_WAVE_COUNT	18u
#define LFO_OS_RUN		0u
#define LFO_OS_PRE		1u
#define LFO_OS_DONE		2u
#define LFO_WRAP_MASK	((1u << LFO_NOISE) | (0x1FFu << LFO_OS_FIRST))
```

### F-2 — `Core/DSPAudio/lfo.h:108` — **Add** after `uint8_t 	retrigger;`

```c
	/*
	 * One-shot state (S104 P1): LFO_OS_RUN (0, also every looping wave),
	 * LFO_OS_PRE (the start delay), LFO_OS_DONE (holding the end value).
	 * Set by lfo_retriggerPhase(), advanced on a phase wrap by
	 * lfo_osWrap() from lfo_calc() and lfo_stepBlock(). It sits in the two
	 * padding bytes before phaseOffset (offsets 10..11, measured), so
	 * sizeof(Lfo) stays 124 B and no voice struct moves.
	 */
	uint8_t		osState;
```

### F-3 — `Core/DSPAudio/lfo.h:213` — **Add** after `void lfo_retrigger(uint8_t voiceNr);`

```c
/*
 * Retrigger one LFO's phase (S104 P1).
 *
 * Looping waves: phase = phaseOffset (as before S104). One-shot waves: with
 * a nonzero offset, phase = 0 - phaseOffset and LFO_OS_PRE (the wave holds
 * its start value until the phase wraps, offset / 127 of one cycle later,
 * then runs one cycle); with offset 0, phase 0 and LFO_OS_RUN (`rn1` draws
 * at once). Accessors: instrumentManager_retriggerRuntimeLfos(),
 * instrumentManager_restoreLfoPhaseIfNeeded() (Scene retrigger).
 */
void lfo_retriggerPhase(Lfo *lfo);
```

### F-4 — `Core/DSPAudio/lfo.c:50–64` — **Add** in `lfo_init()` after `lfo->retrigger = 0;`

```c
	lfo->osState		= LFO_OS_RUN;	/* S104 P1: a one-shot runs once from here */
```

### F-5 — `Core/DSPAudio/lfo.c:65` — **Add** the helpers before `float lfo_calc(Lfo *lfo)`

```c
/*
 * One-shot LFO helpers (S104 P1).
 *
 * lfo_shapeAt(): the 0..1 value of looping wave `base` (LFO_SINE ..
 * LFO_EXP_TRI, not noise or rect) at phase `ph`, the same formulas as
 * lfo_calc()'s cases. The one-shots use it so lfo_calc()'s looping cases
 * keep their code and cost.
 * lfo_osWrap(): the one-shot step on a phase wrap: PRE -> RUN (a one-shot
 * noise draws its value), RUN -> DONE with the phase pinned at the end
 * (0xFFFFFFFF), DONE re-pins the phase after each later wrap, so the value
 * holds without a per-block test on the looping waves.
 * lfo_calcOneShot(): the value of a one-shot (values 9..17): the wrap step
 * first, then PRE reads the start (phase 0; `sq1` reads 0), RUN the running
 * phase, DONE the end; `sq1` is 1 for the first half of RUN, else 0; `rn1`
 * returns its held value.
 * Inputs: the Lfo, the block's wrap flag. Outputs: the value; osState and
 * phase on a wrap. Context: the per-block LFO control (S101 P2 A1: every
 * other block; lfo_stepBlock() takes the block between).
 * Affiliates: lfo_calc(), lfo_stepBlock(), lfo_retriggerPhase().
 */
static float lfo_shapeAt(uint8_t base, uint32_t ph)
{
	switch (base) {
	case LFO_SINE:
		return ((sine_table[ph >> 22] / 32767.f) + 1) / 2.f;
	case LFO_TRI:
		return (1.f - fabsf((ph / (float)0xffffffff) * 2 - 1));
	case LFO_SAW_UP:
		return ph / (float)0xffffffff;
	case LFO_SAW_DOWN:
		return (0xffffffff - ph) / (float)0xffffffff;
	case LFO_EXP_UP: {
		const float x = ph / (float)0xffffffff;
		return x * x * x;
	}
	case LFO_EXP_DOWN: {
		const float x = (0xffffffff - ph) / (float)0xffffffff;
		return x * x * x;
	}
	case LFO_EXP_TRI: {
		const float t = 1.f - fabsf((ph / (float)0xffffffff) * 2 - 1);
		return t * t * t;
	}
	default:
		return 0.f;
	}
}

static void lfo_osWrap(Lfo *lfo)
{
	switch (lfo->osState) {
	case LFO_OS_PRE:
		lfo->osState = LFO_OS_RUN;
		if (lfo->waveform == LFO_OS_NOISE)
			lfo->rnd = (float)(GetRngValue()) / 32767.0f;
		break;
	case LFO_OS_RUN:
		lfo->osState = LFO_OS_DONE;
		lfo->phase = 0xFFFFFFFFu;
		break;
	default:
		lfo->phase = 0xFFFFFFFFu;   /* DONE: re-pin the end */
		break;
	}
}

static float lfo_calcOneShot(Lfo *lfo, uint8_t overflow)
{
	uint32_t ph;

	if (overflow)
		lfo_osWrap(lfo);
	if (lfo->waveform == LFO_OS_NOISE)
		return lfo->rnd;
	if (lfo->waveform == LFO_OS_REC)
		return (lfo->osState == LFO_OS_RUN && lfo->phase <= 0x7fffffffu)
		       ? 1.f : 0.f;
	ph = (lfo->osState == LFO_OS_RUN) ? lfo->phase
	     : ((lfo->osState == LFO_OS_PRE) ? 0u : 0xFFFFFFFFu);
	return lfo_shapeAt((uint8_t)(lfo->waveform - LFO_OS_FIRST), ph);
}
```

### F-6 — `Core/DSPAudio/lfo.c:143` — **Add** before `default:` in `lfo_calc()`'s switch

```c
	//---
		/* S104 P1: the exponential triangle (looping). */
		case LFO_EXP_TRI:
			return lfo_shapeAt(LFO_EXP_TRI, lfo->phase);
	//---
		/* S104 P1: the one-shot waves 9..17 (lfo_calcOneShot()). */
		case LFO_OS_SINE ... LFO_OS_EXP_TRI:
			return lfo_calcOneShot(lfo, overflow);
```

(`case a ... b:` is a GNU range; the build is `-std=gnu11`.)

### F-7 — `Core/DSPAudio/lfo.c:187–188` — **Modify** (`lfo_stepBlock()`'s wrap test)

Anchor: `if (lfo->waveform == LFO_NOISE && oldPhase > lfo->phase)`

```c
	/*
	 * A wrap in the stepped block (S101 P2 A1; S104 P1 one-shots): noise
	 * draws its next value as lfo_calc() would; a one-shot takes its state
	 * step (lfo_osWrap()), so a wrap on the control-free block is never
	 * lost. One table test covers both (LFO_WRAP_MASK); the bound keeps
	 * the shift defined for a corrupt wave byte.
	 */
	if (oldPhase > lfo->phase && lfo->waveform < LFO_WAVE_COUNT &&
	    ((LFO_WRAP_MASK >> lfo->waveform) & 1u)) {
		if (lfo->waveform == LFO_NOISE)
			lfo->rnd = (float)(GetRngValue()) / 32767.0f;
		else
			lfo_osWrap(lfo);
	}
```

### F-8 — `Core/DSPAudio/lfo.c` (end) + `InstrumentManager.c:2665, :4671–4672` — **Add / Modify** (retrigger)

`lfo.c`, after `lfo_retrigger()`:

```c
/* Contract in lfo.h (S104 P1). */
void lfo_retriggerPhase(Lfo *lfo)
{
	if (lfo->waveform >= LFO_OS_FIRST && lfo->waveform < LFO_WAVE_COUNT) {
		if (lfo->phaseOffset != 0u) {
			lfo->phase = 0u - lfo->phaseOffset;
			lfo->osState = LFO_OS_PRE;
		} else {
			lfo->phase = 0u;
			lfo->osState = LFO_OS_RUN;
			if (lfo->waveform == LFO_OS_NOISE)
				lfo->rnd = (float)(GetRngValue()) / 32767.0f;
		}
		return;
	}
	lfo->phase = lfo->phaseOffset;
}
```

`InstrumentManager.c:2665` (`instrumentManager_retriggerRuntimeLfos()`):
`lfo->phase = lfo->phaseOffset;` → `lfo_retriggerPhase(lfo);   /* S104 P1: one-shot delay */`

`InstrumentManager.c:4671–4672` (`instrumentManager_restoreLfoPhaseIfNeeded()`):
`lfo->phase = lfo->phaseOffset;` (the `LFO_RETRIGGER_SCENE` branch) →
`lfo_retriggerPhase(lfo);   /* S104 P1: a Scene retrigger restarts a one-shot too */`.
The hand-off branch (`lfo->phase = lfo_scene_handoff.phase[slot];`) is unchanged;
the incoming LFO keeps its `osState`.

### F-9 — `Core/Menu/MenuText.h:109–111` — **Modify** (`lfoWaveNames[]`)

```c
/*
 * LFO wave names (S104 P1: 18; the value is the stored lfo_wave byte).
 * 0..7 the looping waves, 8 `xtr` (exponential triangle), 9..17 their
 * one-shot forms `si1 .. xt1` (lfo.h LFO_OS_*). The count is the row's menu
 * clamp (menu.c MENU_LFO_WAVES).
 */
static const char lfoWaveNames[][4] = {
    {18}, {"sin"},{"tri"},{"sup"},{"sdn"},{"sqr"},{"rnd"},{"xup"},{"xdn"},
    {"xtr"},
    {"si1"},{"tr1"},{"su1"},{"sd1"},{"sq1"},{"rn1"},{"xu1"},{"xd1"},{"xt1"},
};
```

### F-10 — host test `lfo` — **Modify** (`tools/dsp_test/Makefile:286–303`, `modules/lfo.c`)

**Makefile:**
- the `--macro` list gains `LFO_EXP_TRI LFO_OS_FIRST LFO_OS_SINE LFO_OS_TRI
  LFO_OS_SAW_UP LFO_OS_SAW_DOWN LFO_OS_REC LFO_OS_NOISE LFO_OS_EXP_UP LFO_OS_EXP_DOWN
  LFO_OS_EXP_TRI LFO_WAVE_COUNT LFO_OS_RUN LFO_OS_PRE LFO_OS_DONE LFO_WRAP_MASK`;
- the `--func` list becomes `--func lfo_shapeAt --func lfo_osWrap --func
  lfo_calcOneShot --func lfo_calc --func lfo_calcPhaseInc --func lfo_setFreq --func
  lfo_setSync --func lfo_stepBlock --func lfo_retriggerPhase` (helpers first);
- add a third break build, `build/break_lfo3`, in the pattern of the two above:
  `sed 's|lfo->phase = 0xFFFFFFFFu;   /\* DONE: re-pin the end \*/|;|'` on
  `$(GEN)/lfo_funcs.c` (the comment makes the DONE line unique). The DONE state then
  stops re-pinning, the held phase drifts on, and **LF-OS2 must fail**.

**`modules/lfo.c`:**
- the `Lfo` mirror (`:59–68`) gains `uint8_t osState;` after `retrigger`;
- laws, added to the header comment block:
  - **LF-OS1:** for each one-shot 9..17 at two rates (frq 40 and 90), after
    `lfo_retriggerPhase()` with offset 0, the values over one cycle equal the
    looping wave's, and every later block returns the end value (sin 0.5, tri 0, sup
    1, sdn 0, xup 1, xdn 0, xtr 0);
  - **LF-OS2:** after the end, 1,000 more blocks (`lfo_stepBlock()` +
    `lfo_calc()` pairs) keep the end value;
  - **LF-OS3:** with offset 64, the start value holds for 64/127 of a cycle (±1
    block), then the cycle runs;
  - **LF-OS4:** `sq1` is 0 during the delay, 1 for the first half, then 0; `rn1`
    calls `GetRngValue()` exactly once per retrigger (the stub counts);
  - **LF-OS5:** waves 0..7 give bit-identical values to the pre-S104 extract (copy
    the old `lfo_calc()` into the test as a reference).

**Gate (CPU):**
- `python3 tools/dsp_test/elf_blockpath.py` on `lfo_calc` and `lfo_stepBlock`,
  before and after;
- report the dearest one-shot path and the `lfo_stepBlock` path. Est. +5..+8 and
  +1 per control, ≈ +0.6 a sample system-wide; accepted as measured (Q31);
- `elf_hotloops.py` shows no change in any per-sample loop.

---

## 7. Tetrakt: four track-7 decays, routed by track

Rules (plan §6.2, accepted):
1. on VOICE7 with a Tetrakt on slot 6, `dyA..dyD` show and edit the track-7 values;
2. T7 decay automation (step locks, live recording, held-step edits, locks copied
   from T6) sets the track-7 values; T6's are unaffected;
3. **T6 pre-empts T7** on a layer both restart while it is pending;
4. simultaneous triggers keep their own decays;
5. a T7 hit no longer restores T6's decay locks.

Layer 0 is also today's generic track-7 decay for non-Choke types with
`amp_envelope_decay`, so the generic path keeps its behaviour: Scene target `7dc`,
LFO and velocity targets, all on layer 0.

### 7.1 Data: Kit settings, AutoSave, files

#### T-1 — `Core/Bank/Scene/SceneData.h:68–91` — **Modify** (`kit_settings_t`)

```c
typedef struct {
    /*
     * Kit-level settings that are not instrument parameter images.
     *
     * slot6_track7_decay[] and slot6_track7_morph_decay[] are the generated
     * track-7 decay endpoints (Normal, Morph) of the shared slot-6/track-7
     * voice pair, one per layer (S104 P1). Layer 0 is the generic value used
     * when slot 6 hosts a non-Choke instrument with a base
     * amp_envelope_decay row; a Tetrakt uses all four, one per layer A..D
     * (its tkt_decay_a..d rows are the track-6 values). Choke instruments use
     * real `_choke` descriptors inside their instrument file instead.
     * These are Kit-owned because they depend on the Kit's slot-6 voice
     * layout; they are never written into an instrument file.
     *
     * Per-voice audio routing used to live here but is now Scene-owned in
     * scene_settings_t. Routing is a performance/mix setting that must survive
     * root Kit swaps.
     *
     * Autosave extension rule: every new serialized Kit setting must append a
     * named Kit parameter index/getter and write through SceneData's change-
     * aware Kit store boundary. Direct assignment is reserved for boot
     * initialization or a validated whole-object commit followed by the named
     * Kit region marker. RAM: 8 B per Kit (S104 P1: was 2; +6 B x 17 resident
     * Scenes = +102 B SRAM1, approved). Affiliates: Autosave.h
     * AUTOSAVE_KIT_PARAM_*, scene_setSlot6Track7Decay(),
     * scene_seedTrack7Decays().
     */
    uint8_t slot6_track7_decay[SCENE_TRACK7_DECAY_LAYERS];
    uint8_t slot6_track7_morph_decay[SCENE_TRACK7_DECAY_LAYERS];
} kit_settings_t;
```

Add before the struct:
`#define SCENE_TRACK7_DECAY_LAYERS 4u   /* S104 P1: layer 0 generic/A, 1..3 Tetrakt B..D */`

#### T-2 — `Core/Bank/Scene/SceneData.h:522–538` — **Modify** (accessor prototypes)

```c
/*
 * Kit-owned generated track-7 decay accessors (S104 P1: one per layer).
 *
 * Inputs: resident Scene index, layer 0..3 (0 = the generic non-Choke
 * value and Tetrakt layer A; 1..3 = Tetrakt B..D) and a 0..127 endpoint for
 * setters. Outputs: retained Normal/Morph values, or 0 for an invalid Scene
 * or layer. Changed final values mark their named Kit AutoSave cells after
 * storage; equal values do not schedule work. scene_seedTrack7Decays()
 * fills the endpoints a loaded Kit did not carry from slot 6's own track-6
 * decays (a staged Kit); scene_seedResidentTrack7Decays() does the same for
 * a resident Scene through the setters (Instrument Load into slot 6).
 */
void scene_setSlot6Track7Decay(uint8_t scene_index, uint8_t layer, uint8_t value);
uint8_t scene_getSlot6Track7Decay(uint8_t scene_index, uint8_t layer);
void scene_setSlot6Track7MorphDecay(uint8_t scene_index, uint8_t layer,
                                    uint8_t value);
uint8_t scene_getSlot6Track7MorphDecay(uint8_t scene_index, uint8_t layer);
void scene_seedTrack7Decays(kit_t *kit, uint8_t seen_mask);
void scene_seedResidentTrack7Decays(uint8_t scene_index);
```

The four old names are removed (no aliasing). §7.2 lists every caller.

#### T-3 — `Core/Bank/Scene/SceneData.c:605–681` — **Modify** (the four accessors → layered)

```c
void scene_setSlot6Track7Decay(uint8_t scene_index, uint8_t layer, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one generated track-7 Normal decay endpoint (S104 P1: per layer).
     *
     * Inputs: resident Scene index, layer 0..3 and a 0..127 menu-domain
     * value. Output: Kit settings retain the value the track-7 trigger uses
     * for that layer (InstrumentManager: the amp EG of a generic slot-6
     * instrument for layer 0; a Tetrakt's layer decays). This cannot be
     * folded into the descriptor-image setters because the value is not part
     * of any instrument file and has no descriptor index. A changed
     * normalized byte is stored before its named Kit parameter
     * (AUTOSAVE_KIT_PARAM_T7(layer, 0)) is marked; equal values do nothing.
     */
    if (!scene || layer >= SCENE_TRACK7_DECAY_LAYERS)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeKitParameterByte(
        scene_index, &scene->kit.settings.slot6_track7_decay[layer],
        AUTOSAVE_KIT_PARAM_T7(layer, 0u), value);
}

uint8_t scene_getSlot6Track7Decay(uint8_t scene_index, uint8_t layer)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Retained 0..127 Normal endpoint; 0 for an invalid Scene or layer.
     * Clients: Menu display, Preset apply, storage, the track-7 trigger. */
    return (scene && layer < SCENE_TRACK7_DECAY_LAYERS)
        ? scene->kit.settings.slot6_track7_decay[layer] : 0u;
}

void scene_setSlot6Track7MorphDecay(uint8_t scene_index, uint8_t layer,
                                    uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one generated track-7 Morph decay endpoint (S104 P1: per layer).
     * Same contract as the Normal setter; the AutoSave cell is
     * AUTOSAVE_KIT_PARAM_T7(layer, 1). Kept separate from Scene
     * voice_morph_amount[], which is the interpolation amount.
     */
    if (!scene || layer >= SCENE_TRACK7_DECAY_LAYERS)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeKitParameterByte(
        scene_index, &scene->kit.settings.slot6_track7_morph_decay[layer],
        AUTOSAVE_KIT_PARAM_T7(layer, 1u), value);
}

uint8_t scene_getSlot6Track7MorphDecay(uint8_t scene_index, uint8_t layer)
{
    const scene_t *scene = scene_getConst(scene_index);

    return (scene && layer < SCENE_TRACK7_DECAY_LAYERS)
        ? scene->kit.settings.slot6_track7_morph_decay[layer] : 0u;
}
```

#### T-4 — `Core/Bank/Scene/SceneData.c:1230–1231` — **Modify** (Scene reset)

```c
    /* S104 P1: every track-7 decay layer, both endpoints, to 0 (a reset is
     * not a load: the load seeding rule does not apply here). */
    for (slot = 0u; slot < SCENE_TRACK7_DECAY_LAYERS; slot++) {
        scene_setSlot6Track7Decay(scene_index, slot, 0u);
        scene_setSlot6Track7MorphDecay(scene_index, slot, 0u);
    }
```

#### T-5 — `Core/Bank/Scene/SceneData.c` — **Add** the seeding helpers after `scene_getSlot6Track7MorphDecay()`

```c
/*
 * Seed the track-7 decays a load did not carry (S104 P1; user Q24: "a load
 * sets T7 decays the same as T6 if they aren't present in the scene or kit").
 *
 * What: for each layer that slot 6's type has a track-7 decay for
 * (instrumentManager_track7DecayIndex(): a Tetrakt has A..D at its
 * tkt_decay_a..d rows; any other type with amp_envelope_decay has layer 0),
 * an endpoint whose bit is clear in seen_mask (bit 2 x layer = Normal,
 * bit 2 x layer + 1 = Morph; storage_kitset_t.seen_t7_mask) takes slot 6's
 * own value of that row from the same image (Normal from
 * instrument_parameters[], Morph from morph_instrument_parameters[]).
 * scene_seedTrack7Decays() writes a staged kit_t directly (the commit that
 * follows copies the whole Kit and marks it); scene_seedResidentTrack7Decays()
 * writes a resident Scene through the setters (AutoSave-marked), for an
 * Instrument Load into slot 6, which never carries these values.
 * Why: an older or hand-made Kit file, or a newly loaded slot-6 instrument,
 * would otherwise leave track 7 at 0 (an instant decay) or at another
 * type's value. Inputs: the Kit or Scene, the seen mask. Outputs: unseen
 * endpoints. Accessors: filesystem.c Kit/Scene/boot commits,
 * presetManager.c Instrument commit. Affiliates: scene_setSlot6Track7Decay().
 */
void scene_seedTrack7Decays(kit_t *kit, uint8_t seen_mask)
{
    const kit_instrument_slot_t *slot6;
    uint8_t layer;

    if (!kit)
        return;
    slot6 = &kit->instruments[INSTRUMENT_SLOT_COUNT - 1u];
    for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
        uint8_t index;

        if (!instrumentManager_track7DecayIndex(slot6->type, layer, &index))
            continue;
        if ((seen_mask & (uint8_t)(1u << (2u * layer))) == 0u)
            kit->settings.slot6_track7_decay[layer] =
                slot6->parameter_images.instrument_parameters[index];
        if ((seen_mask & (uint8_t)(1u << (2u * layer + 1u))) == 0u)
            kit->settings.slot6_track7_morph_decay[layer] =
                slot6->parameter_images.morph_instrument_parameters[index];
    }
}

void scene_seedResidentTrack7Decays(uint8_t scene_index)
{
    const kit_instrument_slot_t *slot6 =
        scene_instrumentSlotConst(scene_index, INSTRUMENT_SLOT_COUNT - 1u);
    uint8_t layer;

    if (!slot6)
        return;
    for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
        uint8_t index;

        if (!instrumentManager_track7DecayIndex(slot6->type, layer, &index))
            continue;
        scene_setSlot6Track7Decay(scene_index, layer,
            slot6->parameter_images.instrument_parameters[index]);
        scene_setSlot6Track7MorphDecay(scene_index, layer,
            slot6->parameter_images.morph_instrument_parameters[index]);
    }
}
```

#### T-6 — `Core/Bank/Scene/Autosave.h:205, :280–284` — **Modify**

`#define AUTOSAVE_KIT_PARAMETER_LIVE_BYTES       2u` → `8u   /* S104 P1: 4 layers x 2 endpoints */`

```c
/*
 * Live Kit-settings cells (S104 P1: four track-7 decay layers).
 *
 * Index = 2 x layer + image (0 Normal, 1 Morph), so indices 0 and 1 keep
 * their pre-S104 meaning (the generic / layer A endpoints) and an older
 * record restores them unchanged; 2..7 were reserved cells of the 118-byte
 * Kit allocation (no layout or version change). A pre-S104 record holds no
 * live B..D bytes; whatever its reserved cells hold is clamped to 0..127 by
 * the setters on the first restore (no migration).
 */
typedef enum {
    AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_DECAY = 0,
    AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY,
    AUTOSAVE_KIT_PARAM_T7_DECAY_B,
    AUTOSAVE_KIT_PARAM_T7_MORPH_DECAY_B,
    AUTOSAVE_KIT_PARAM_T7_DECAY_C,
    AUTOSAVE_KIT_PARAM_T7_MORPH_DECAY_C,
    AUTOSAVE_KIT_PARAM_T7_DECAY_D,
    AUTOSAVE_KIT_PARAM_T7_MORPH_DECAY_D,
    AUTOSAVE_KIT_PARAM_COUNT
} autosave_kit_parameter_t;
#define AUTOSAVE_KIT_PARAM_T7(layer, morph) \
    ((uint8_t)(2u * (uint8_t)(layer) + ((morph) ? 1u : 0u)))
```

The asserts at `:399–402` hold with 8 ≤ 118.

#### T-7 — `Core/Bank/Scene/Autosave.c:1326–1343` and `:1720–1731` — **Modify**

Getter (`autosave_getLivePayloadByte()` Kit branch):

```c
        /*
         * The eight track-7 decay cells (S104 P1: index = 2 x layer +
         * image). Inputs: Kit parameter indices 0..117. Output: indices 0..7
         * sample the retained endpoints; later reserved indices report absent.
         */
        if (kit_parameter < AUTOSAVE_KIT_PARAM_COUNT) {
            const uint8_t layer = (uint8_t)(kit_parameter / 2u);

            *value = (kit_parameter & 1u)
                ? scene->kit.settings.slot6_track7_morph_decay[layer]
                : scene->kit.settings.slot6_track7_decay[layer];
            return 1u;
        }
        return 0u;
```

Apply (`autosave_applyKitPayload()`); rewrite its comment's "the 2 live Kit-parameter
bytes" to "the 8":

```c
    for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
        scene_setSlot6Track7Decay(scene_index, layer,
            kit_section[AUTOSAVE_KIT_PARAMETERS_OFFSET +
                        AUTOSAVE_KIT_PARAM_T7(layer, 0u)]);
        scene_setSlot6Track7MorphDecay(scene_index, layer,
            kit_section[AUTOSAVE_KIT_PARAMETERS_OFFSET +
                        AUTOSAVE_KIT_PARAM_T7(layer, 1u)]);
    }
```

(with `uint8_t layer;` declared).

#### T-8 — `Core/Bank/Scene/Preset/presetManager.c:3544–3556, :3691, :4088` — **Modify** (S098 field lists)

```c
#define PRESET_SLOT6_KIT_FIELDS(X) \
    X(slot6_track7_decay) \
    X(slot6_track7_morph_decay)
```

```c
_Static_assert(sizeof(kit_settings_t) == 8u,
               "kit_settings_t changed: classify the new field in the S098 "
               "field lists (presetManager.c), then update 8");
```

Match (`:3689–3693`; the fields are arrays since S104 P1):

```c
#define PM_MATCH_SLOT6_KIT(f) \
        if (memcmp(sa->kit.settings.f, sb->kit.settings.f, \
                   sizeof(sa->kit.settings.f)) != 0) return 0u;
```

Copy (`:4087–4089`):

```c
#define PM_COPY_SLOT6_KIT(f) \
        memcpy(dst->kit.settings.f, src->kit.settings.f, sizeof(dst->kit.settings.f));
```

Add to the list's comment (`:3528`): "the track-7 decay fields are per-layer arrays
since S104 P1, compared and copied whole".

#### T-9 — `Core/Bank/Scene/Preset/presetManager.c:1598–1640` and `presetManager.h:552–555` — **Modify** (the menu-edit setter)

Rename `preset_setSlot6Track7AmpEnvelopeDecay()` to `preset_setSlot6Track7Decay()` and
add `layer`:

```c
uint8_t preset_setSlot6Track7Decay(uint8_t scene_index, uint8_t layer,
                                   instrument_image_select_t image,
                                   uint8_t value, uint8_t record_automation);
```

In the body:
- `if (!scene_get(scene_index) || layer >= SCENE_TRACK7_DECAY_LAYERS) return 0u;`;
- the stores become `scene_setSlot6Track7MorphDecay(scene_index, layer, value)` /
  `scene_setSlot6Track7Decay(scene_index, layer, value)`;
- the Rule-A clear (`:1638–1639`) becomes
  `instrumentManager_clearSlot6Track7StepDecayLayer(layer);`.

Add to the comment: "S104 P1: one layer (0 = generic / Tetrakt A, 1..3 = Tetrakt
B..D); the Rule-A clear ends only that layer's step override."

#### T-10 — `Core/Bank/Scene/Preset/presetManager.c:2613–2620` — **Modify** (KitMrp)

```c
            /* S104 P1: every layer's Normal value becomes its Morph endpoint. */
            for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++)
                scene_setSlot6Track7MorphDecay(
                    scene_index, layer,
                    source->settings.slot6_track7_decay[layer]);
```

Declare `uint8_t layer;` in the enclosing block. Update the comment's setter name.

#### T-11 — `Core/Hardware/SD/storageTypes.c:2047, :2082–2120` and `storageTypes.h:176` — **Modify** (Kit file parser)

**Key buffer (`:2047`):**

```c
    /*
     * Key buffer (S104 P1: 24 -> 40). storage_splitKeyValue() truncates a
     * key to key_max - 1 characters, so the 31- and 37-character track-7
     * decay keys never matched in a 24-byte buffer and were silently
     * dropped on load (found in S104 P1). 40 covers every kitset key; it is
     * stack only.
     */
    char key[40];
```

**Struct (`storageTypes.h:176`):** add before `} storage_kitset_t;`:

```c
    /*
     * S104 P1: the track-7 decay endpoints this file carried, bit 2 x layer
     * (Normal) and 2 x layer + 1 (Morph); scene_seedTrack7Decays() fills the
     * rest from slot 6's track-6 decays at the commit. Cleared by
     * storage_kitsetInit().
     */
    uint8_t seen_t7_mask;
```

**Parser (`:2082–2120`):** replace the two key branches with one table branch:

```c
        } else if (storage_kitsetTrack7Key(key, &t7_bit)) {
            /*
             * Optional generated track-7 decay endpoints (S104 P1: per layer).
             *
             * Keys: slot6_track7_amp_envelope_decay /
             * slot6_track7_morph_amp_envelope_decay (layer 0, the generic
             * value and Tetrakt A: names kept), slot6_track7_decay_b/c/d and
             * slot6_track7_morph_decay_b/c/d (Tetrakt B..D). Inputs: a
             * top-level kitset key in the 0..127 parameter domain. Output:
             * target_kit retains the endpoint and seen_t7_mask its bit. This
             * is Kit-owned metadata, not an instrument-file descriptor, so it
             * is parsed from kitset.kcg. Missing keys are seeded at the commit
             * (scene_seedTrack7Decays()).
             */
            if (!target_kit)
                return STORAGE_STATUS_BAD_VALUE;
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed > 127u)
                parsed = 127u;
            if (t7_bit & 1u)
                target_kit->settings.slot6_track7_morph_decay[t7_bit / 2u] = parsed;
            else
                target_kit->settings.slot6_track7_decay[t7_bit / 2u] = parsed;
            kit->seen_t7_mask |= (uint8_t)(1u << t7_bit);
        }
```

with `uint8_t t7_bit;` among the locals, and a helper before
`storage_kitsetParseLine()`:

```c
/*
 * Kitset track-7 decay keys (S104 P1). Index = 2 x layer + image (the
 * AutoSave order); the writer (filesystem_nextKitsetLine()) uses the same
 * table. Output: 1 with *bit for a known key.
 */
static const char *const storage_kitsetTrack7Keys[8] = {
    "slot6_track7_amp_envelope_decay", "slot6_track7_morph_amp_envelope_decay",
    "slot6_track7_decay_b", "slot6_track7_morph_decay_b",
    "slot6_track7_decay_c", "slot6_track7_morph_decay_c",
    "slot6_track7_decay_d", "slot6_track7_morph_decay_d",
};

static uint8_t storage_kitsetTrack7Key(const char *key, uint8_t *bit)
{
    uint8_t i;

    for (i = 0u; i < 8u; i++) {
        if (storage_streq(key, storage_kitsetTrack7Keys[i])) {
            *bit = i;
            return 1u;
        }
    }
    return 0u;
}

/* The key text of bit 0..7 for the writer (contract in storageTypes.h). */
const char *storage_kitsetTrack7KeyName(uint8_t bit)
{
    return (bit < 8u) ? storage_kitsetTrack7Keys[bit] : "";
}
```

Declare `const char *storage_kitsetTrack7KeyName(uint8_t bit);` in `storageTypes.h`,
with a one-line contract.

#### T-12 — `Core/Hardware/SD/filesystem.c:19362–19388` — **Modify** (Kit file writer)

Replace the `index == 2u`, `index == 3u` and `index == 4u` lines and the
`index - 5u` slot mapping with:

```c
    /*
     * The eight track-7 decay lines (S104 P1: lines 2..9, bit = index - 2 =
     * 2 x layer + image, the storageTypes.c key table). A Morph save
     * (KitMrp) writes each layer's endpoints interpolated at slot 6's voice
     * Morph amount, as the single pair was before.
     */
    if (index >= 2u && index < 10u) {
        const uint8_t bit = (uint8_t)(index - 2u);
        const uint8_t layer = (uint8_t)(bit / 2u);
        const uint8_t base = kit->settings.slot6_track7_decay[layer];
        const uint8_t morph = kit->settings.slot6_track7_morph_decay[layer];
        uint8_t v = (bit & 1u) ? morph : base;

        if (op_kit_save_mode == STORAGE_INSTRUMENT_SAVE_MORPH &&
            op_kit_save_source_scene < SCENE_COUNT)
            v = filesystem_interpolateMorphEndpoint(
                base, morph,
                source_scene ? source_scene->settings.voice_morph_amount[5u]
                             : 0u);
        return filesystem_formatAssignmentU16Line(
            dst, cap, storage_kitsetTrack7KeyName(bit), v);
    }
    if (index == 10u)
        return filesystem_formatLiteralLine(dst, cap, "\n");

    /*
     * Each slot serializes four logical lines: header, type, file, and a
     * blank separator, from line 11 (S104 P1; was 5). ...
     */
    index = (uint16_t)(index - 11u);
```

The rest of the slot mapping is unchanged.

#### T-13 — `Core/Hardware/SD/filesystem.c:13050, :32360, :18491` — **Add** (seeding at the three Kit commits)

| Site | Anchor | Add before it |
|---|---|---|
| Kit Load commit (`:13050`) | `for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u; scene_index++) {` (the loop whose body has `target_scene->kit = op_staged_kit;`) | `/* S104 P1: track-7 decays the kitset did not carry take slot 6's track-6 values (scene_seedTrack7Decays()); this also serves KitMrp, which reads op_staged_kit through filesystem_loadedKit(). */ scene_seedTrack7Decays(&op_staged_kit, op_kitset.seen_t7_mask);` — **place it before** `if (current_op == FS_INTERNAL_OP_LOAD_KIT) {` (`:13002`), so KitMrp sees the seeded stage too |
| Boot/Bank reader (`:32360`) | `scene->kit = op_staged_kit;` | `scene_seedTrack7Decays(&op_staged_kit, op_kitset.seen_t7_mask);   /* S104 P1 */` |
| Scene stage (`:18491`) | the `for (scene_index …` loop in `filesystem_commitSceneStage()` | `/* S104 P1: as for a Kit Load. */ scene_seedTrack7Decays(&fs_stage_workspace.scene_stage.kit, op_kitset.seen_t7_mask);` |

`op_kitset` is `storage_kitsetInit()`-cleared at each Kit and Scene parse start
(`:12911`, `:13749`) and is not reused before the commit.

#### T-14 — `Core/Bank/Scene/Preset/presetManager.c:2833` — **Add** after `scene->kit.instruments[slot] = *staged;`

```c
        /*
         * S104 P1 (user Q24b): an Instrument Load into slot 6 carries no
         * track-7 decays, so they take the new instrument's own track-6
         * decays (scene_seedResidentTrack7Decays(): Tetrakt A..D, or the
         * generic amp_envelope_decay), AutoSave-marked through the setters.
         */
        if (slot == INSTRUMENT_SLOT_COUNT - 1u)
            scene_seedResidentTrack7Decays(target_scene_index);
```

### 7.2 Every other caller of the old accessors

#### T-15 — `Core/Menu/CopyClear/copyOps.c:717–723, :1106–1109, :1194–1195` — **Modify**

Instrument copy slot 6 → slot 6 (`:717–723`):

```c
            /* S104 P1: every track-7 layer, both endpoints. */
            for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
                scene_setSlot6Track7Decay(
                    m, layer, scene_getSlot6Track7Decay(src->scene, layer));
                scene_setSlot6Track7MorphDecay(
                    m, layer, scene_getSlot6Track7MorphDecay(src->scene, layer));
            }
```

Normal → Morph copies (`:1106–1109`, `:1194–1195`):

```c
            for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++)
                scene_setSlot6Track7MorphDecay(
                    m, layer, scene_getSlot6Track7Decay(src->scene, layer));
```

(`uint8_t layer;` in each function; update the comment blocks at `:1055`, `:1159`
that name the setter).

#### T-16 — `Core/Menu/CopyClear/clearOps.c:517–519, :592–594` — **Modify**

```c
            for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++)
                scene_setSlot6Track7MorphDecay(
                    m, layer, scene_getSlot6Track7Decay(m, layer));
```

(same for `:593`; update the comments at `:478`, `:566`).

#### T-17 — layer-0 readers — **Modify**

| Site | Old | New |
|---|---|---|
| `MidiControl.c:234` | `scene_getSlot6Track7AmpEnvelopeDecay(scene_index)` | `scene_getSlot6Track7Decay(scene_index, 0u)` |
| `LiveRecord.c:648` | `scene_getSlot6Track7AmpEnvelopeDecay(a)` | `scene_getSlot6Track7Decay(a, 0u)` |
| `menu.c:15037` | `scene_getSlot6Track7AmpEnvelopeDecay(scene)` | `scene_getSlot6Track7Decay(scene, 0u)` |
| `InstrumentManager.c:4393` | `preset_setSlot6Track7AmpEnvelopeDecay(scene_getActiveIndex(), 0u, (uint8_t)value, 0u)` | `preset_setSlot6Track7Decay(scene_getActiveIndex(), 0u, 0u, (uint8_t)value, 0u)` (layer 0, image Main) |
| `InstrumentManager.c:4507` | `scene->kit.settings.slot6_track7_amp_envelope_decay` | `scene->kit.settings.slot6_track7_decay[0]` |
| `InstrumentManager.c:3275` | `scene->kit.settings.slot6_track7_amp_envelope_decay` | `scene->kit.settings.slot6_track7_decay[0]` |

Each keeps its comment, with "(layer 0: `7dc`, S104 P1)" added. The Scene target `7dc`
(id 7) is layer 0 for every type.

### 7.3 Runtime: overrides, routing, restore, trigger

#### T-18 — `Core/DSP/Instruments/InstrumentManager.c:263–275` — **Modify** (the step override)

```c
/*
 * Step-automation runtime overrides of the generated track-7 decays
 * (S104 P1: one per layer).
 *
 * What: bit L of slot6_track7_decay_step_active set = layer L's track-7
 * decay is slot6_track7_decay_step_value[L] at the next track-7 trigger,
 * ahead of the LFO override (layer 0 only) and the retained Kit value.
 * Writers: the Scene target `7dc` (layer 0; seq_applySceneAutomation(),
 * cleared at the Scene-automation restore), and, for a Tetrakt on slot 6,
 * a track-7 step lock of tkt_decay_a..d routed by its track
 * (seq_drainPendingAutomationTo(); cleared at track 7's next trigger
 * restore, seq_restoreAutomatedParameters()). Rule A: a menu edit of one
 * layer clears that layer (instrumentManager_clearSlot6Track7StepDecayLayer()).
 * Lifetime: runtime only; no Kit/Scene storage or AutoSave marker is
 * touched. RAM: 1 + 4 B (S104 P1: was 1 + 1; +3 B, approved).
 * Affiliates: instrumentManager_track7LayerDecays(),
 * instrumentManager_applySlot6AlternateDecay().
 */
static uint8_t slot6_track7_decay_step_active;
static uint8_t slot6_track7_decay_step_value[SCENE_TRACK7_DECAY_LAYERS];
```

In `instrumentManager_applySlot6AlternateDecay()` (`:3271–3272`):
`slot6_track7_decay_step_active ? slot6_track7_decay_step_value : …` →
`(slot6_track7_decay_step_active & 1u) ? slot6_track7_decay_step_value[0] : …`.

#### T-19 — `Core/DSP/Instruments/InstrumentManager.c:4529–4555` and `InstrumentManager.h:764–765` — **Modify** (override API)

```c
/*
 * Track-7 decay step overrides (S104 P1: per layer; contract above the
 * statics). Set: layer 0..3, 7-bit value. ClearLayer: one layer (Rule A).
 * Clear: every layer (Scene-automation restore, Scene activation, slot
 * step-override clear). ClearTetraktTrack7: every layer, at track 7's
 * trigger restore on a Tetrakt (the locks belong to the step that fired).
 */
void instrumentManager_setSlot6Track7StepDecayOverride(uint8_t layer, uint8_t value)
{
    if (layer >= SCENE_TRACK7_DECAY_LAYERS)
        return;
    if (value > 127u)
        value = 127u;
    slot6_track7_decay_step_value[layer] = value;
    slot6_track7_decay_step_active |= (uint8_t)(1u << layer);
}

void instrumentManager_clearSlot6Track7StepDecayLayer(uint8_t layer)
{
    if (layer < SCENE_TRACK7_DECAY_LAYERS)
        slot6_track7_decay_step_active &= (uint8_t)~(1u << layer);
}

void instrumentManager_clearSlot6Track7StepDecayOverride(void)
{
    slot6_track7_decay_step_active = 0u;
}
```

`InstrumentManager.h:764–765`:

```c
void instrumentManager_setSlot6Track7StepDecayOverride(uint8_t layer, uint8_t value);
void instrumentManager_clearSlot6Track7StepDecayLayer(uint8_t layer);
void instrumentManager_clearSlot6Track7StepDecayOverride(void);
```

`sequencer.c:3964`: `instrumentManager_setSlot6Track7StepDecayOverride(value);` →
`instrumentManager_setSlot6Track7StepDecayOverride(0u, value);   /* 7dc = layer 0 */`.

#### T-20 — `Core/DSP/Instruments/InstrumentManager.c` — **Add** the track-7 helpers after `instrumentManager_clearSlot6Track7StepDecayOverride()`; prototypes in `InstrumentManager.h`

```c
/*
 * Track-7 decay mapping (S104 P1).
 *
 * instrumentManager_track7DecayIndex(): the descriptor index of `type`'s
 * track-6 decay for track-7 layer `layer`: a Tetrakt's tkt_decay_a..d
 * (TETRAKT_PARAM_DECAY_A_INDEX + layer); for another type, layer 0 is its
 * amp_envelope_decay row, if any. 1 with *index_out, else 0.
 * instrumentManager_track7DecayLayerOf(): the inverse for a Tetrakt (the
 * layer 0..3 of descriptor `local`, else 0xFF); other types give 0xFF (their
 * one generic value is the Scene target `7dc`, not a descriptor).
 * instrumentManager_track7DecayLayer(): the same for a voice target id in
 * `scene_index` (slot 6 only). instrumentManager_track7DecayLocalMask(): the
 * four Tetrakt decay locals as a 64-bit slot mask (0 for other types).
 * Why: track 7 on a Tetrakt owns these four rows for automation and
 * display, track 6 the rows themselves; one place knows the mapping.
 * Accessors: Menu (VOICE7 cells), Sequencer (drain routing, restore),
 * SceneData (load seeding), LiveRecord (recorded value).
 */
uint8_t instrumentManager_track7DecayIndex(instrument_type_t type, uint8_t layer,
                                           uint8_t *index_out)
{
    if (layer >= SCENE_TRACK7_DECAY_LAYERS || !index_out)
        return 0u;
    if (type == INSTRUMENT_TYPE_TKT) {
        *index_out = (uint8_t)(TETRAKT_PARAM_DECAY_A_INDEX + layer);
        return 1u;
    }
    if (layer != 0u)
        return 0u;
    return instrumentManager_descriptorIndexByKey(type, "amp_envelope_decay",
                                                  index_out) ? 1u : 0u;
}

uint8_t instrumentManager_track7DecayLayerOf(instrument_type_t type, uint8_t local)
{
    if (type != INSTRUMENT_TYPE_TKT || local < TETRAKT_PARAM_DECAY_A_INDEX ||
        local >= (uint8_t)(TETRAKT_PARAM_DECAY_A_INDEX + SCENE_TRACK7_DECAY_LAYERS))
        return 0xFFu;
    return (uint8_t)(local - TETRAKT_PARAM_DECAY_A_INDEX);
}

uint8_t instrumentManager_track7DecayLayer(uint8_t scene_index,
                                           instrument_param_id_t id)
{
    const kit_instrument_slot_t *slot;

    if (!instrumentParam_isVoiceParameter(id) ||
        instrumentParam_slot(id) != INSTRUMENT_SLOT_COUNT - 1u)
        return 0xFFu;
    slot = scene_instrumentSlotConst(scene_index, INSTRUMENT_SLOT_COUNT - 1u);
    return slot ? instrumentManager_track7DecayLayerOf(slot->type,
                                                       instrumentParam_local(id))
                : 0xFFu;
}

uint64_t instrumentManager_track7DecayLocalMask(uint8_t scene_index)
{
    const kit_instrument_slot_t *slot =
        scene_instrumentSlotConst(scene_index, INSTRUMENT_SLOT_COUNT - 1u);

    return (slot && slot->type == INSTRUMENT_TYPE_TKT)
        ? (0xFULL << TETRAKT_PARAM_DECAY_A_INDEX) : 0u;
}

/*
 * The four track-7 decays a track-7 trigger plays (S104 P1).
 * What: per layer, the step override if set, else (layer 0) the 7dc LFO
 * override if active, else the active Scene's retained Normal value
 * (the generic path's precedence, layer by layer). Inputs: the overrides,
 * the active Scene's Kit. Output: out[4]. Accessor:
 * instrumentManager_triggerTrack() (Tetrakt on slot 6, track 7).
 */
static void instrumentManager_track7LayerDecays(uint8_t out[SCENE_TRACK7_DECAY_LAYERS])
{
    const uint8_t scene_index = scene_getActiveIndex();
    uint8_t layer;

    for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
        if (slot6_track7_decay_step_active & (uint8_t)(1u << layer))
            out[layer] = slot6_track7_decay_step_value[layer];
        else if (layer == 0u && slot6_track7_decay_lfo_active)
            out[layer] = slot6_track7_decay_lfo_value;
        else
            out[layer] = scene_getSlot6Track7Decay(scene_index, layer);
    }
}
```

- `#include "TetraktParameters.h"` in `InstrumentManager.c`, if not already
  present (check the include list).
- Prototypes for the four public helpers go in `InstrumentManager.h` beside
  T-19's.
- `instrumentManager_track7LayerDecays()` is static and used earlier in the file
  (T-21), so add its forward declaration to the static prototypes at
  `InstrumentManager.c:488–489`:
  `static void instrumentManager_track7LayerDecays(uint8_t out[SCENE_TRACK7_DECAY_LAYERS]);`.

#### T-21 — `Core/DSP/Instruments/InstrumentManager.c:3364–3382` — **Modify** (the Tetrakt trigger case)

```c
    case INSTRUMENT_TYPE_TKT: {
        /*
         * S096 P1: no step length (one-shot layers that decay). The
         * platform part (LFO retrigger, velocity nodes) runs inside
         * Tetrakt_triggerVoice() for `slot`.
         * S104 P1: track 7 on slot 6 passes its four track-7 decays
         * (instrumentManager_track7LayerDecays()); each restarted layer
         * plays the decay of the track that restarted it, and a T6 trigger
         * keeps a layer a pending T7 trigger also asked for (TetraktVoice.c
         * Tetrakt_triggerVoice()). Track 6 passes NULL.
         */
        /*
         * S096 P3 T1: `layers` (from a MIDI layer note or an overlay repeat)
         * overrides lon for this trigger only; 0 plays lon (after the step's
         * automation). The track's MIDI TX happens here, with the mask that
         * played (one note per layer, decided Q15; finding D3: TIM3's TX ran
         * before the step's lon was drained).
         */
        uint8_t altDecay[SCENE_TRACK7_DECAY_LAYERS];

        if (alternate_slot6)
            instrumentManager_track7LayerDecays(altDecay);
        Tetrakt_triggerVoice(instrumentManager_tetraktRuntime(slot), slot,
                             velocity, note, layers,
                             alternate_slot6 ? altDecay : NULL);
        midiOut_trackLayers(trigger_track, seq_getTrackPlayedScene(trigger_track),
                            note, velocity,
                            layers ? layers : instrumentManager_tetraktRuntime(slot)->layerOn);
        break; }
```

The old "finds no amp_envelope_decay row and does nothing, so track 7 plays slot 5 as
an ordinary hit" sentence is removed.

#### T-22 — `Core/Sequencer/sequencer.c:4150–4157` — **Add** in `seq_drainPendingAutomationTo()`'s voice branch, before the R12 test

Anchor: `uint8_t slot = instrumentParam_slot(target);` followed by
`* Skip overridden parameters in the drain (S081 §5.1, R12).`

```c
                    /*
                     * Track-7 decay locks on a Tetrakt (S104 P1).
                     *
                     * What: an entry owned by track 7 whose target is one of
                     * slot 6's four Tetrakt decay rows (tkt_decay_a..d) sets
                     * that layer's track-7 step override
                     * (instrumentManager_setSlot6Track7StepDecayOverride())
                     * and is consumed: the row (track 6's decay) is not
                     * written, not marked held, and starts no ramp. Why: on a
                     * Tetrakt, track 7 plays the longer decays and its decay
                     * automation must move them only (user, S104 P1); the
                     * value is taken at the track-7 trigger (fixed per note),
                     * so a ramp has nothing to move. Placed before the R12
                     * test: a VOICE6 edit of a track-6 decay must not suppress
                     * track 7's lock. Inputs: owner_track, owner_scene,
                     * target, value. Outputs: the override; cleared at track
                     * 7's next trigger restore. Affiliates:
                     * instrumentManager_track7DecayLayer(),
                     * seq_restoreAutomatedParameters().
                     */
                    if (owner_track == INSTRUMENT_SLOT_COUNT) {
                        const uint8_t t7Layer =
                            instrumentManager_track7DecayLayer(owner_scene, target);

                        if (t7Layer < SCENE_TRACK7_DECAY_LAYERS) {
                            instrumentManager_setSlot6Track7StepDecayOverride(
                                t7Layer, value);
                            i++;
                            continue;
                        }
                    }
```

#### T-23 — `Core/Sequencer/sequencer.c:4323–4390` — **Modify** (`seq_restoreAutomatedParameters()`)

After `seq_menuOverride[slot] = 0u;` (`:4345`) and before `mask = seq_automation_dirty[slot];`:

```c
    /*
     * Track 7 on a Tetrakt owns only its own decays (S104 P1).
     *
     * What: a track-7 trigger (trigger_track 6, slot 5) of a Tetrakt clears
     * its four track-7 decay step overrides (set by this pass's drain, which
     * runs after this restore, for the step that fires now) and leaves the
     * four tkt_decay_a..d locals out of the restore: those rows are track
     * 6's decays and their locks belong to track 6's step. Without the mask,
     * a track-7 hit snapped a track-6 decay lock back to its base mid-note.
     * Inputs: trigger_track, the played Scene's slot-5 type. Outputs: the
     * overrides; t6Keep, held across the restore. Affiliates:
     * instrumentManager_track7DecayLocalMask(), the drain's track-7 routing.
     */
    if (trigger_track == INSTRUMENT_SLOT_COUNT) {
        t6Keep = instrumentManager_track7DecayLocalMask(
            seq_perTrackPattern[trigger_track]);
        if (t6Keep != 0u)
            instrumentManager_clearSlot6Track7StepDecayOverride();
    }
    mask = seq_automation_dirty[slot] & ~t6Keep;
```

- Declare `uint64_t t6Keep = 0u;` with the other locals.
- The final store becomes
  `seq_automation_dirty[slot] = (rampHeld | t6Keep) & seq_automation_dirty[slot];`,
  so the kept locals stay held for track 6.
- Update the function's header comment: "S104 P1: on a Tetrakt, track 7's trigger
  restores everything except the four decay rows, and clears its track-7 decay
  overrides."

#### T-24 — `Core/DSP/Instruments/Tetrakt/TetraktParameters.h:31` and `TetraktParameters.c:149` — **Add**

`.h`, after `#define TETRAKT_PARAM_LAYER_ON_INDEX   20u`:

```c
/* S104 P1: the layer-A decay row; tkt_decay_a..d are this + 0..3 (track 7's
 * decays are routed by these indices, InstrumentManager.c). */
#define TETRAKT_PARAM_DECAY_A_INDEX    12u
```

`.c`, after the `TETRAKT_PARAM_LAYER_ON_INDEX` assert:

```c
_Static_assert(TETRAKT_PARAM_DECAY_A_INDEX == TKT_PARAM_DECAY_A,
               "TETRAKT_PARAM_DECAY_A_INDEX names the layer-A decay row");
```

#### T-25 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.h:110–132, :189–190` — **Modify** (`TktLayer`, the trigger prototype)

After `uint8_t  pendVel;`:

```c
	/*
	 * Track-7 decay state (S104 P1), in the three padding bytes after
	 * pendVel (offsets 85..87, measured; sizeof(TktLayer) unchanged).
	 * decayAlt: the decay a note started by track 7 plays; pendDecayAlt:
	 * the pending track-7 trigger's decay; pflags: TKT_PF_PEND (a trigger is
	 * waiting for this layer's START latch), TKT_PF_PEND_ALT (that trigger
	 * is track 7's), TKT_PF_ALT (the playing note is track 7's), and the
	 * loop read form (TKT_PF_FORM_*, S104 P1 loops). Written by
	 * Tetrakt_triggerVoice() and tkt_layerControl()'s START latch; read by the
	 * decay control and the post-loop dispatch.
	 */
	uint8_t  decayAlt;
	uint8_t  pendDecayAlt;
	uint8_t  pflags;
```

Flags (before the struct):

```c
#define TKT_PF_PEND       0x01u
#define TKT_PF_PEND_ALT   0x02u
#define TKT_PF_ALT        0x04u
```

After the struct:

```c
_Static_assert(sizeof(TktLayer) == 204u,
               "S104 P1: the track-7 bytes must stay in TktLayer's padding");
```

K-1 later changes this to 212.

Prototype (`:189–190`):

```c
void Tetrakt_triggerVoice(TetraktVoice *voice, uint8_t source_slot,
                          uint8_t velocity, uint8_t note, uint8_t layers,
                          const uint8_t *altDecay);
```

Update the entry-point comment (`:175`): "altDecay: track 7's four decays, NULL for
track 6 (S104 P1)".

#### T-26 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:462–504` — **Modify** (`Tetrakt_triggerVoice()`)

Signature as T-25. Replace the layer loop's body (`:492–502`) with:

```c
		const uint8_t on = layers ? layers : voice->layerOn;

		if (!(on & (1u << i)))
			continue;
		/*
		 * T6 pre-empts T7 (S104 P1, user rule 3): a track-7 trigger leaves
		 * alone a layer a track-6 trigger has already restarted and whose
		 * START is still pending (TKT_PF_PEND without TKT_PF_PEND_ALT); a
		 * track-6 trigger always takes the layer. So when both tracks
		 * restart a layer in the same pass (or within the one-block START
		 * window, two after a FADE), it plays track 6's velocity, sample
		 * and decay whatever the pop order, and layers only one track
		 * restarts keep that track's decay (rule 4).
		 */
		if (altDecay && (L->pflags & TKT_PF_PEND) &&
		    !(L->pflags & TKT_PF_PEND_ALT))
			continue;
		L->pendSample = L->sample;
		L->pendVel = velocity;
		L->pendDecayAlt = altDecay ? altDecay[i] : 0u;
		L->pflags = (uint8_t)((L->pflags & (uint8_t)~TKT_PF_PEND_ALT) |
		                      TKT_PF_PEND |
		                      (altDecay ? TKT_PF_PEND_ALT : 0u));
		if (L->stage == TKT_STAGE_PLAY && L->env >= TKT_QUIET)
			L->stage = TKT_STAGE_FADE;
		else if (L->stage != TKT_STAGE_FADE)
			L->stage = TKT_STAGE_START;
```

The two `L->stage = …` lines are textually unchanged, so the `tetrakt_break_fade`
`sed` still applies.

#### T-27 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:246–249, :258, :270` — **Modify** (the START latch and the decay read)

START latch (`:246–249`), inside `if (!init && L->stage == TKT_STAGE_START) {`, after
`L->velo = …;`:

```c
		/* S104 P1: the note takes its trigger's track: track 7's decay and
		 * flag move from pending to playing; the pending marks clear. */
		L->decayAlt = L->pendDecayAlt;
		L->pflags = (uint8_t)((L->pflags & (uint8_t)~(TKT_PF_PEND |
		                                              TKT_PF_PEND_ALT |
		                                              TKT_PF_ALT)) |
		                      ((L->pflags & TKT_PF_PEND_ALT) ? TKT_PF_ALT : 0u));
```

Decay read (`:258`, `:270`):

```c
	/* S104 P1: a note track 7 started plays its track-7 decay (decayAlt);
	 * track 6's row is L->decay. The velocity group's offset applies to
	 * whichever the note plays. */
	const uint8_t dsrc = (L->pflags & TKT_PF_ALT) ? L->decayAlt : L->decay;
	...
	decay = (float)dsrc;
	...
		case TKT_KIND_DECAY:    decay = tkt_eff(dsrc, off);   break;
```

(Declare `dsrc` at the top of the block with the other locals.)

`Tetrakt_initVoice()` (`:424–455` layer loop): add `L->decayAlt = 70u;`,
`L->pendDecayAlt = 70u;`, `L->pflags = 0u;`.

**Cost:** one load, test and select in the per-block control. Est. +3 instructions a
control, /32 ≈ +0.1 a sample. Gate: `tetrakt_cpu.py`, both lines (§8.6).

### 7.4 Menu, recording, display

#### T-28 — `Core/Menu/menu.c:2200–2203` — **Modify** (`menu_kit_setting_kind_t`)

```c
typedef enum {
    MENU_KIT_SETTING_NONE = 0,
    MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY,
    /*
     * S104 P1: a Tetrakt on slot 6 shows its four track-7 decays on VOICE7
     * in the tkt_decay_a..d cells (layer = kind - ..._A). Their value is
     * Kit-owned (scene_get/setSlot6Track7Decay()); their automation target is
     * the slot-6 descriptor itself, recorded on track 7's steps and routed by
     * track (seq_drainPendingAutomationTo()).
     */
    MENU_KIT_SETTING_T7_LAYER_A,
    MENU_KIT_SETTING_T7_LAYER_B,
    MENU_KIT_SETTING_T7_LAYER_C,
    MENU_KIT_SETTING_T7_LAYER_D
} menu_kit_setting_kind_t;

/* S104 P1: the layer 0..3 of a Tetrakt track-7 cell, else 0xFF. */
static uint8_t menu_kitSettingT7Layer(const menu_cell_t *cell)
{
    if (!cell || cell->kind != MENU_CELL_KIT_SETTING ||
        cell->kit_setting < MENU_KIT_SETTING_T7_LAYER_A ||
        cell->kit_setting > MENU_KIT_SETTING_T7_LAYER_D)
        return 0xFFu;
    return (uint8_t)(cell->kit_setting - MENU_KIT_SETTING_T7_LAYER_A);
}
```

The helper goes after the `menu_cell_t` typedef (`:2224` area), where `menu_cell_t` is
complete.

#### T-29 — `Core/Menu/menu.c:4877–4897` — **Add** before the generated generic cell (`if (menu_activePage == VOICE7_PAGE && slot_index == 5u && !(… INSTRUMENT_FLAG_CHOKE) …`)

```c
        if (menu_activePage == VOICE7_PAGE && slot_index == 5u) {
            const uint8_t t7Layer = instrumentManager_track7DecayLayerOf(
                slot->type, cell.descriptor_index);

            /*
             * Tetrakt track-7 decay cells (S104 P1).
             *
             * What: on VOICE7, a Tetrakt's tkt_decay_a..d cells show and edit
             * track 7's decay of that layer (Kit-owned), borrowing the row's
             * descriptor for name, dtype and the automation target. Why: on
             * tracks 6 and 7 the Tetrakt's four decays are per track (user,
             * S104 P1: "the track selection is the decider", no new names).
             * Inputs: the slot-6 type, the resolved descriptor index.
             * Output: a MENU_CELL_KIT_SETTING cell of kind T7_LAYER_A..D.
             * Affiliates: menu_kitSettingT7Layer(), the Kit cell display,
             * commit, clear-target and held-step paths.
             */
            if (t7Layer < SCENE_TRACK7_DECAY_LAYERS) {
                cell.kind = MENU_CELL_KIT_SETTING;
                cell.kit_setting =
                    (uint8_t)(MENU_KIT_SETTING_T7_LAYER_A + t7Layer);
                cell.slot = slot_index;
                return cell;
            }
        }
```

#### T-30 — `Core/Menu/menu.c:5350–5358` (display) and `:5480–5530` (commit) — **Modify**

Display, after the `SLOT6_TRACK7_AMP_DECAY` arm:

```c
        /* S104 P1: a Tetrakt track-7 layer (Normal, or Morph in a Morph view). */
        if (menu_kitSettingT7Layer(cell) < SCENE_TRACK7_DECAY_LAYERS) {
            const uint8_t layer = menu_kitSettingT7Layer(cell);

            return voiceModeShowMorph
                ? scene_getSlot6Track7MorphDecay(scene_getActiveIndex(), layer)
                : scene_getSlot6Track7Decay(scene_getActiveIndex(), layer);
        }
```

The generic arm becomes `scene_getSlot6Track7(Morph)Decay(scene_getActiveIndex(), 0u)`.

Commit:
- the generic arm calls `preset_setSlot6Track7Decay(scene_index, 0u, …)`;
- add a Tetrakt arm after it:

```c
        if (menu_kitSettingT7Layer(cell) < SCENE_TRACK7_DECAY_LAYERS) {
            /*
             * Commit a Tetrakt track-7 decay layer (S104 P1).
             *
             * What: every masked resident Scene receives the layer's Kit
             * endpoint (Normal, or Morph in a Morph view) through
             * preset_setSlot6Track7Decay(), which also ends that layer's step
             * override (Rule A: the menu value takes effect at the next
             * track-7 hit). No Scene-automation bit: the layer's automation
             * is the slot-6 descriptor on track 7's steps, not `7dc`.
             */
            const uint8_t layer = menu_kitSettingT7Layer(cell);

            for (scene_index = 0u; scene_index < SCENE_SLOT_TOTAL; scene_index++) {
                if ((edit_mask & (1ul << scene_index)) == 0u)
                    continue;
                changed |= preset_setSlot6Track7Decay(
                    scene_index, layer,
                    morphView ? INSTRUMENT_IMAGE_MORPH : INSTRUMENT_IMAGE_MAIN,
                    (uint8_t)value,
                    (uint8_t)(!morphView && scene_index == scene_getActiveIndex()));
            }
            return changed;
        }
```

#### T-31 — `Core/Menu/menu.c:17794–17796, :18071–18075, :4110–4119, :3596–3597, :3633` — **Modify** (targets and markers)

| Site | Change |
|---|---|
| `menu_cellMidiTarget()` `:17794` | Leave Tetrakt layer cells with no target (the `if` already only accepts `SLOT6_TRACK7_AMP_DECAY`). Add the comment: `/* S104 P1: a Tetrakt track-7 decay cell has no MIDI address: tracks 6 and 7 share slot 6, so an NRPN or a Learn row of tkt_decay_x reaches track 6's row; the track-7 values are reached by step automation, VOICE7 edits and live recording. */` |
| `menu_cellClearTarget()` `:18071` | Before `if (cell.kit_setting != MENU_KIT_SETTING_SLOT6_TRACK7_AMP_DECAY) return 0u;` add: `{ uint8_t idx; const uint8_t layer = menu_kitSettingT7Layer(&cell); if (layer < SCENE_TRACK7_DECAY_LAYERS) { const kit_instrument_slot_t *s6 = scene_instrumentSlotConst(scene_getActiveIndex(), 5u); if (!s6 || !instrumentManager_track7DecayIndex(s6->type, layer, &idx)) return 0u; out->pattern_target = instrumentParam_make(5u, idx); return 1u; } }`, with the comment `/* S104 P1: the Tetrakt track-7 decay cells' target is the slot-6 decay row itself; a lock recorded or cleared from VOICE7 lives on track 7's steps and is routed by track. */` |
| `va_writeAutomationFromKnob()` `:4110–4119` | Before the final `} else { return; }` add an arm: `} else if (menu_kitSettingT7Layer(&cell) < SCENE_TRACK7_DECAY_LAYERS) { target = instrumentParam_make(menu_voicePageToSlot(menu_activePage), cell.descriptor_index);`, with the comment `/* S104 P1: held-step edits of a Tetrakt track-7 decay cell write the slot-6 decay row on the held (track-7) steps; the drain routes them to track 7. */` |
| `va_applyVoiceMarkers()` `:3596–3597` | `cell.kind == MENU_CELL_INSTRUMENT \|\|` → `cell.kind == MENU_CELL_INSTRUMENT \|\| menu_kitSettingT7Layer(&cell) < SCENE_TRACK7_DECAY_LAYERS \|\|`; the target expression treats the Tetrakt cell as an instrument cell (`instrumentParam_make(slot, cell.descriptor_index)`) |
| `va_applyVoiceMarkers()` `:3633` | `} else if (cell.kind == MENU_CELL_INSTRUMENT &&` → `} else if ((cell.kind == MENU_CELL_INSTRUMENT \|\| menu_kitSettingT7Layer(&cell) < SCENE_TRACK7_DECAY_LAYERS) &&`, with the comment `/* S104 P1: track 7's decay cells underline from track 7's lanes. */` |

The cell keeps `descriptor_index` (the borrowed row), so the marker and held-value
paths need no other change.

#### T-32 — `Core/Sequencer/LiveRecord.c:616–628` — **Modify** (`liveRec_targetValue7()`)

In the voice branch, before reading `instrument_parameters[local]`:

```c
        /*
         * S104 P1: recording on track 7 (rtk 6) of a Tetrakt decay row takes
         * track 7's value of that layer (the cell the user turns on VOICE7),
         * not track 6's row.
         */
        if (liveRec_rtk == INSTRUMENT_SLOT_COUNT) {
            const uint8_t t7Layer = instrumentManager_track7DecayLayer(a, target);

            if (t7Layer < SCENE_TRACK7_DECAY_LAYERS)
                return scene_getSlot6Track7Decay(a, t7Layer);
        }
```

### 7.5 Tests

#### T-33 — host tests — **Modify / Add**

- **`tools/dsp_test/modules/tetrakt.c`:**
  - every `Tetrakt_triggerVoice(&V, s, vel, note, layers)` call gains `, NULL`
    (lines 253, 267, 275, 315, 318, 325, 330, 350, 387, 415, and any later ones;
    `grep` them).
  - **New law TK-T7:**
    - a T7 trigger (`altDecay` = {10, 20, 30, 40}) of `lon` 15 plays each layer at
      its alt decay (the per-block factor equals `colEgK[alt]`), while
      `L->decay` = 90 is untouched;
    - a T6 trigger then a T7 trigger in one pass on layers {A, B} and {B, C}: A and
      B play the T6 decay and velocity, C the T7 decay;
    - the same in the reverse pop order gives the same result;
    - a T7 trigger alone after the START latch restarts normally with its decay.
  - **Break:** `sed` out the `continue;` of the T6-wins test. TK-T7 must fail.
- **`tools/dsp_test/modules/sequencer.c`** (drain extract): **new law SQ-T7**.
  - Stub `instrumentManager_track7DecayLayer()` (slot 5, local 12..15 → 0..3) and
    `instrumentManager_setSlot6Track7StepDecayOverride()` (record).
  - A track-7 entry on local 13 records layer 1 and writes nothing.
  - A track-6 entry on local 13 writes the row.
- **`tools/dsp_test/modules/voice_control.c`** (`voiceControl_processPending` extract):
  a T6 lock on local 12 stays held across a T7 trigger restore. Stub
  `instrumentManager_track7DecayLocalMask()` = `0xF << 12`. The break (no `t6Keep`)
  must fail it.
- **Kit file round trip:** extend the storage host test, if one exists in
  `tools/dsp_test` (check `make -C tools/dsp_test` targets); otherwise device check.
  - write a Kit with eight distinct track-7 values and reload: all eight return;
  - a file with only the two old keys seeds B..D from slot 6 (Tetrakt);
  - before T-11, the two old keys did not load (device-check the fix).

**Device checks (§7):**
- Tetrakt on slot 6: VOICE7 `dyA..dyD` differ from VOICE6's and are heard on T7
  hits only;
- T7 step locks of `dyB` change only T7's B layer;
- T6 and T7 on one step with `lon` A+B on T6 and B+C on T7: B is short;
- a T7 hit while a T6 decay lock plays leaves that lock;
- Kit Load of a pre-S104 kit seeds B..D;
- Instrument Load of a Tetrakt into slot 6 seeds all four from its own decays;
- generic types (Drum on slot 6): `7dc` behaves as before.

---

## 8. Tetrakt looped samples

Design (plan §7.2, Q25 answered):
- Tetrakt sample rows list `s00..` (one-shot) then `l00..` (the same samples,
  looped), capped at 128 values;
- a block in which a looped layer's read cannot reach the seam runs today's
  one-shot loop unchanged;
- a seam block reads exactly (A) with an interpolation slot, or falls back to the
  early wrap (B).

### K-1 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.h:131` — **Add** after `float    bindGain;`; flags and the size assert

```c
	/*
	 * Loop points of a looped (SLP) bind (S104 P1), in Q19.12 phase:
	 * loopStartPh = loop_start << 12 and loopEndPh = loop_end << 12
	 * (loop_end exclusive; the whole sample without a `smpl` chunk,
	 * SampleInstall.c), written at every bind (tkt_bindLayer()); 0 for a
	 * one-shot bind. Why: the async's seam prediction and the sync render's
	 * seam forms read them every block of a looped layer; caching them here
	 * keeps catalogue lookups out of both (and out of the sync render's
	 * register allocation). RAM: 8 B a layer, 32 B a voice, inside the
	 * 1,264 B slot reserve (TetraktVoice 1,080 -> 1,112 B; asserted at
	 * InstrumentManager.c:360). No new allocation (S101 P2 A2q precedent).
	 */
	uint32_t loopStartPh, loopEndPh;
```

Add to the flag defines (T-25):

```c
/* S104 P1: the post-loop form of this block (Tetrakt_calcVoiceAsync()). */
#define TKT_PF_FORM_SHIFT 3u
#define TKT_PF_FORM_MASK  0x18u
#define TKT_FORM_PLAIN    0u   /* no seam this block: the one-shot loop */
#define TKT_FORM_A        1u   /* seam, interpolation slot held: exact */
#define TKT_FORM_B        2u   /* seam, no slot: early wrap */
```

Change T-25's assert to `sizeof(TktLayer) == 212u` and its message to "S104 P1:
204 B + the two loop points".

### K-2 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:179–188` — **Modify** (`tkt_bindLayer()`)

```c
static __attribute__((noinline)) void tkt_bindLayer(TktLayer *L, const uint32_t restart)
{
	const sample_catalogue_entry_t *entry;

	osc_userBind(&L->osc);
	if (restart)
		L->osc.phase = 0u;
	/*
	 * S104 P1: a looped (SLP) bind is a catalogue sample too: it takes the
	 * sample's gain and caches its loop points (K-1's fields); a one-shot
	 * clears them, which is also the "not looped" test of the async and the
	 * sync dispatch. A loop of under one frame plays as a one-shot.
	 */
	entry = (L->osc.sampleCacheLooped == OSC_BIND_SMP ||
	         L->osc.sampleCacheLooped == OSC_BIND_SLP)
		? sampleMemory_sampleEntry(L->osc.sampleCacheIndex) : NULL;
	L->bindGain = entry ? sampleMemory_gainFromCode(entry->gain_code) : 1.0f;
	if (entry && L->osc.sampleCacheLooped == OSC_BIND_SLP &&
	    entry->loop_end > entry->loop_start) {
		L->loopStartPh = entry->loop_start << OSC_SAMPLE_PHASE_SHIFT;
		L->loopEndPh = entry->loop_end << OSC_SAMPLE_PHASE_SHIFT;
	} else {
		L->loopStartPh = 0u;
		L->loopEndPh = 0u;
	}
}
```

### K-3 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:246–247` — **Modify** (the START latch's sample)

Replace `L->osc.waveform = L->pendSample;` with:

```c
		/*
		 * S104 P1: the row's value names a one-shot (0 .. samples - 1) or
		 * the same sample looped (samples .. 2 x samples - 1; the Drum
		 * family's s/l order). osc_userBind() binds SMP or SLP from oscType
		 * and the index from waveform at this restart's bind below.
		 */
		{
			const uint8_t n = sampleMemory_getNumSamples();

			if (n != 0u && L->pendSample >= n) {
				L->osc.oscType = OSC_TYPE_SLP;
				L->osc.waveform = (uint8_t)(L->pendSample - n);
			} else {
				L->osc.oscType = OSC_TYPE_SMP;
				L->osc.waveform = L->pendSample;
			}
		}
```

### K-4 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c` `Tetrakt_initVoice()` layer loop (`:424–455`) — **Add**

`L->loopStartPh = 0u; L->loopEndPh = 0u;   /* S104 P1: a one-shot until a looped bind */`

### K-5 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:569–574` — **Add** in `Tetrakt_calcVoiceAsync()` after the `ctlPhase` update, before `tkt_spaceControl(voice);`

```c
	/*
	 * Loop seam forms for this block (S104 P1, user Q25).
	 *
	 * What: for every looped layer (loopEndPh != 0) that is sounding, the
	 * block's last read is predicted from this block's phase and step
	 * (phase + 32 x phaseInc; the controls above already ran). If it can
	 * reach the last loop frame ((loop_end - 1) << 12), the block holds the
	 * seam: an interpolation slot is claimed (modNode_interpClaim(); here in
	 * the async, after modNode_resetTargets(), never in the sync render:
	 * S093 P8 item 79) and the layer reads exactly (form A); without a slot
	 * (both held by wavetables or itp fades, or interpolation off) it takes
	 * the early wrap (form B; a loop of under two frames, which B cannot
	 * shorten, plays the plain form and stops at the sample end). Otherwise
	 * the plain one-shot loop runs unchanged (its end clamp is never reached
	 * in such a block). The form is stored in pflags for the sync dispatch.
	 * Why: the exact seam costs a seam select and a wrap per sample (A,
	 * +4..+6 a sample, est.); it is paid only in seam blocks, from the slot
	 * pool's pre-charged 45 a sample (CPU_USE_DSP_AUDIT.md item 75), and the
	 * no-slot worst case B (0..+1 a sample) fits Tetrakt's own gate.
	 * Inputs: each layer's phase, phaseInc, loop points, stage. Outputs:
	 * the form bits; a slot claim (sticky for one more block, as every
	 * claim is). Cost: est. 10..12 instructions a looped layer a block.
	 * Affiliates: Tetrakt_calcVoiceSyncBlock(), tkt_layerPost().
	 */
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];
		uint32_t form = TKT_FORM_PLAIN;

		if (L->loopEndPh != 0u && L->stage != TKT_STAGE_IDLE &&
		    L->osc.phase + (uint32_t)OUTPUT_DMA_SIZE * L->osc.phaseInc >=
		        L->loopEndPh - (1u << OSC_SAMPLE_PHASE_SHIFT)) {
			if (modNode_interpClaim(&L->osc))
				form = TKT_FORM_A;
			else if (L->loopEndPh - L->loopStartPh >=
			         (2u << OSC_SAMPLE_PHASE_SHIFT))
				form = TKT_FORM_B;
		}
		L->pflags = (uint8_t)((L->pflags & (uint8_t)~TKT_PF_FORM_MASK) |
		                      (uint8_t)(form << TKT_PF_FORM_SHIFT));
	}
```

The phase sum cannot overflow: phases are below `SAMPLE_MAX_FRAMES << 12`
(asserted < 2³¹, `SampleMemory.h:203`), and 32 × the largest step is about 10.5 M.

### K-6 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:619–712` — **Modify** (`tkt_layerPost()`)

Signature:

```c
static inline __attribute__((always_inline)) void tkt_layerPost(TktLayer *L, const float vg,
                                                                  const uint32_t first,
                                                                  const uint32_t form)
```

`form` is a compile-time constant in every instance (K-7), so each instance holds
one form's loop only.

Changes inside:

1. `live` (`:626–627`) admits looped binds:

```c
	/* osc_renderTyped()'s rule: only a catalogue bind (SMP or, S104 P1,
	 * SLP) with data reads; anything else reads a zero pair at phase 0,
	 * so the loop is the same and the layer is silent */
	const uint32_t live = ((L->osc.sampleCacheLooped == OSC_BIND_SMP ||
	                        L->osc.sampleCacheLooped == OSC_BIND_SLP) &&
	                       L->osc.sampleCacheData && L->osc.sampleCacheSize != 0u);
```

2. `const uint32_t inc = L->osc.phaseInc;` (`:630`) becomes the block below. For
   the plain form it folds to the same constant:

```c
	/*
	 * S104 P1 seam forms. A (exact): the read after loop_end - 1 is
	 * loop_start (the Drum osc_slpPlainBlock()'s seam), the phase wraps at
	 * loop_end by the loop length. B (early wrap): the pass reads
	 * [loop_start, loop_end - 1] and wraps at loop_end - 1, so no seam
	 * select is needed (one frame shorter that pass; inaudible on drum
	 * loops, 1 % sharp on a 100-frame cycle). The step is clamped below the
	 * wrap length (Q27: no division; a loop shorter than one step at a high
	 * pitch aliases anyway). Plain: the one-shot clamp at the sample end.
	 */
	const uint32_t le = L->loopEndPh >> OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t ls = L->loopStartPh >> OSC_SAMPLE_PHASE_SHIFT;
	const uint32_t wrapPh = (form == TKT_FORM_A) ? L->loopEndPh
	                      : (L->loopEndPh - (1u << OSC_SAMPLE_PHASE_SHIFT));
	const uint32_t lenPh = wrapPh - L->loopStartPh;
	const uint32_t inc = (form != TKT_FORM_PLAIN && L->osc.phaseInc >= lenPh)
	                     ? (lenPh - 1u) : L->osc.phaseInc;
```

3. The read and the step (`:666–673`):

```c
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const uint32_t nx = (form == TKT_FORM_A && idx + 1u == le) ? ls : idx + 1u;
		const int32_t a = data[idx];
		float x = (float)((a << OSC_SAMPLE_PHASE_SHIFT) +
		                  (int32_t)(ph & 0xFFFu) * ((int32_t)data[nx] - a)) * g;

		ph += inc;
		if (form == TKT_FORM_PLAIN)
			ph = (ph > endPh) ? endPh : ph;
		else
			ph = (ph >= wrapPh) ? (ph - lenPh) : ph;
```

- For the plain form, `nx` is `idx + 1u`, exactly as before. The text
  `(int32_t)(ph & 0xFFFu) * ` is kept, so the `tetrakt_break_frac` `sed` still
  applies.
- `const float rs = L->bindGain * (1.0f / 4096.0f);` is untouched (the
  `tetrakt_break_gain` `sed`).

Update the function's header comment:
- "ph = min(ph + phaseInc, end)" becomes "the plain form: ph = min(ph + phaseInc,
  end); the seam forms (S104 P1): see the block above";
- add `form` to "Inputs".

### K-7 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:714–722` — **Modify** (the instances)

```c
/*
 * The post-loop instances (the Colloid's store / add pattern; S104 P1 adds
 * the two seam forms): layer A stores, B..D add; one loop each, the form a
 * compile-time constant. Plain is the pre-S104 loop.
 */
static __attribute__((noinline)) void tkt_layerPostStore(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 1u, TKT_FORM_PLAIN);
}
static __attribute__((noinline)) void tkt_layerPostAdd(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 0u, TKT_FORM_PLAIN);
}
static __attribute__((noinline)) void tkt_layerPostStoreSeamA(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 1u, TKT_FORM_A);
}
static __attribute__((noinline)) void tkt_layerPostAddSeamA(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 0u, TKT_FORM_A);
}
static __attribute__((noinline)) void tkt_layerPostStoreSeamB(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 1u, TKT_FORM_B);
}
static __attribute__((noinline)) void tkt_layerPostAddSeamB(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 0u, TKT_FORM_B);
}
```

### K-8 — `Core/DSP/Instruments/Tetrakt/TetraktVoice.c:885–893` — **Modify** (`Tetrakt_calcVoiceSyncBlock()`'s layer loop)

```c
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];
		const float vg = voice->veloVol ? L->velo : 1.0f;
		/* S104 P1: the form the async chose for this block (K-5). */
		const uint32_t form = (uint32_t)(L->pflags & TKT_PF_FORM_MASK) >>
		                      TKT_PF_FORM_SHIFT;

		if (i == 0u) {
			if (form == TKT_FORM_A)
				tkt_layerPostStoreSeamA(L, vg);
			else if (form == TKT_FORM_B)
				tkt_layerPostStoreSeamB(L, vg);
			else
				tkt_layerPostStore(L, vg);
		} else {
			if (form == TKT_FORM_A)
				tkt_layerPostAddSeamA(L, vg);
			else if (form == TKT_FORM_B)
				tkt_layerPostAddSeamB(L, vg);
			else
				tkt_layerPostAdd(L, vg);
		}
	}
```

Update its comment: "The order of the four layers never depends on a setting; the
loop form per layer is the async's seam decision (S104 P1); the dearer form is the
counted one (tetrakt_cpu.py)."

### K-9 — `Core/DSP/Instruments/InstrumentManager.c:1283–1294` — **Modify** (`instrumentManager_waveDomainMax()` Tetrakt branch)

```c
    /*
     * S096 P1: a Tetrakt sample row (tkt_sample_a..d) lists the installed
     * samples: 0 .. count - 1 (0 with none installed: the layer is silent).
     * S104 P1: then the same samples looped, count .. 2 x count - 1 (the
     * Drum family's s, l order), capped at 128 values as osc_userListCount()
     * caps the modulator list (with more than 64 samples the top loops are
     * unreachable). The rows are MENU_WAVEFORM selectors (TetraktParameters.c
     * ROW_MENU_SELECT), so this is their menu clamp and automation range
     * (instrumentManager_waveCellCount()).
     */
    if (descriptor && descriptor->file_key &&
        strncmp(descriptor->file_key, "tkt_sample_", 11u) == 0) {
        const uint16_t list = (uint16_t)((uint16_t)sampleMemory_getNumSamples() * 2u);
        const uint8_t count = (uint8_t)((list > 128u) ? 128u : list);

        return count ? (uint8_t)(count - 1u) : 0u;
    }
```

### K-10 — `Core/Menu/menu.c:12597–12611` — **Modify** (`menu_formatWaveCell()` Tetrakt branch)

```c
    /*
     * S096 P1: a Tetrakt sample row (tkt_sample_a..d) names the installed
     * sample at its value: the catalogue's 8-character name and its sNN code.
     * S104 P1: values samples .. 2 x samples - 1 (below 128) name the same
     * sample looped, with the lNN code, as the Drum family's modulator list.
     * A value past the list names nothing (blank, "---" in the compact cell).
     * The count is the row's clamp (instrumentManager_waveDomainMax()).
     */
    if (isInstrument && strncmp(cell->descriptor->file_key, "tkt_sample_", 11u) == 0) {
        if (value < samples) {
            sampleMemory_getDisplayName(value, name);
            memcpy(nameDst, name, SAMPLE_DISPLAY_NAME_LEN);
            menu_formatUserShortName('s', value, codeDst);
        } else if (value < (uint16_t)(2u * samples) && value < 128u) {
            const uint8_t looped = (uint8_t)(value - samples);

            sampleMemory_getDisplayName(looped, name);
            memcpy(nameDst, name, SAMPLE_DISPLAY_NAME_LEN);
            menu_formatUserShortName('l', looped, codeDst);
        }
        return;
    }
```

### K-11 — `tools/dsp_test/tetrakt_cpu.py` — **Modify**

- **`REQUIRED`** gains `tkt_layerPostStoreSeamA`, `tkt_layerPostAddSeamA`,
  `tkt_layerPostStoreSeamB`, `tkt_layerPostAddSeamB`.
- **The counted per-sample loops** use the dearer of plain and B per instance:
  `store = max(post_per_sample(elf,'tkt_layerPostStore'), post_per_sample(elf,'tkt_layerPostStoreSeamB'))`,
  the same for add. The block paths of the post instances take the same max.
  The async's prediction loop is inside `Tetrakt_calcVoiceAsync`'s block path, so
  it is already counted.
- **New third line:**
  `seam A premium per held slot: ps(StoreSeamA) - ps(Store) = X (pool charge 45)`.
  Exit 1 if X > 45 (the premium must stay inside one slot's charge).
- **Docstring:** add "S104 P1: the counted form per layer is the dearer of plain and
  the early-wrap seam form B; form A runs only with an interpolation slot, whose
  45-a-sample charge (CPU_USE_DSP_AUDIT.md item 75) covers its premium, printed and
  checked separately".
- The `GATE` stays 390.

### K-12 — host test `tetrakt` — **Modify** (`tools/dsp_test/modules/tetrakt.c`, Makefile)

- **New law TK-LOOP**, using the host catalogue (`CAT_HOST_SRC`) with a 1,000-frame
  test sample whose loop is [200, 900):
  - **form A** (`g_claim = 1`) at rates 1.0, 0.73 and 2.6 equals a reference
    `osc_slpPlainBlock()`-rule read (computed in the test in double precision) to
    within its integer rounding, across at least five wraps;
  - **form B** (`g_claim = 0`) wraps at frame 899 with length 699 and never reads
    frame 899's successor;
  - a block whose prediction says "no seam" is bit-identical to the one-shot loop
    on the same phase;
  - row values `samples .. 2·samples − 1` bind SLP with index value − samples.
- **Breaks** (Makefile, in the pattern of `tetrakt_break_*`):
  - *seam:* `sed 's/(form == TKT_FORM_A \&\& idx + 1u == le) ? ls : idx + 1u/idx + 1u/'`
    must fail TK-LOOP's form-A check;
  - *predict:* `sed 's/L->loopEndPh - (1u << OSC_SAMPLE_PHASE_SHIFT)) {/L->loopEndPh + 0x40000000u) {/'`
    (never predicts a seam) must fail TK-LOOP, because the plain form then clamps at
    the sample end.
- **Existing laws** TK-TRIG … TK-A2Q must pass unchanged, and every existing break
  must still be caught.

### 8.6 Tetrakt gate (steps 7 and 8)

- **Step 7 (T)** must pass `make -C tools/dsp_test tetrakt_cpu_check
  TKT_BASE=baselines/s104p1/tetrakt.txt` (growth ≤ 0.5), est. +0.1.
- **Step 8 (K)** raises the worst case by design:
  - B's 0..+1 per layer, plus the prediction ≈ +1.5 a sample;
  - run it **without** `TKT_BASE` against `GATE = 390`, and record the new number
    as `baselines/s104p1/tetrakt_k.txt`.
  - Measured now: 379.1. Expected after K: ≤ 385 (est.).
  - The seam-A line must report a premium ≤ 45.

---

## 9. Gates, baselines and checks

### 9.1 Before step 1

1. Copy `build/lxr02.elf` (built from `9c2462bd`, `make all`) to
   `tools/dsp_test/baselines/s104p1/lxr02_base.elf`. Record:
   - `python3 tools/dsp_test/tetrakt_cpu.py --elf build/lxr02.elf > tools/dsp_test/baselines/s104p1/tetrakt.txt`
     (measured now: `loops 292.5 + per block 2771/32 = 379.1`);
   - `python3 tools/dsp_test/elf_hotloops.py` (its baseline form) into the same
     folder;
   - `python3 tools/dsp_test/elf_blockpath.py` for `lfo_calc` and `lfo_stepBlock`.
2. Note the flash and SRAM1 totals from `link_budget.py`.

### 9.2 After every step

| Gate | Expected |
|---|---|
| `make all && make img` | builds without warnings (`-Wall -Wextra`): no unused statics after L-2/L-3 |
| `make -C tools/dsp_test -k all` | only `voice_control` 14 and `alkaline` AL-OD0 fail (known) |
| New laws and breaks | SC-*, TP-*, NM-OMNI, LF-OS*, TK-T7, SQ-T7, TK-LOOP pass; their breaks are caught |
| `elf_hotloops.py` + the inlined-set diff | steps 1–5: no per-sample loop changes; 6: none; 7: the Tetrakt control only; 8: the new seam instances and the async |
| `tetrakt_cpu.py` | §8.6 |
| `link_budget.py` | SRAM1 +152 B in total (§0.3); flash est. +6..+8 KB (DJ, Omni, the loop instances, the LFO) inside 22,728 B headroom |

### 9.3 Device checks (one list)

| § | Check |
|---|---|
| 1 | `!1-` across a Scene change and a restart; one condition from each renumbered group; `1:-` / `!1-` after > 256 passes |
| 2 | Audition on every VOICE screen keeps the cursor |
| 3 | Pot 1 on Load, Save, nested Instrument Load and Save |
| 4 | §4 O-15 list |
| 5 | §5 D-27 list |
| 6 | One-shot shapes, delays and retrigger `off` by ear |
| 7 | §7 T-33 list |
| 8 | Loop seams at low pitch with interpolation on (A) and off (B); a long drum loop; a single-cycle loop |

---

## 10. Statements made out of date (for you to act on)

No entry edits these. Each needs updating after the schedule lands.

- **`MEMORY.md`:**
  - Open Items **M1** (Load/Save pot 1) is resolved by L-1;
  - MIDI / Clock reminders gain the Omni channel O (O wins over G; selected track;
    latch);
  - the Tetrakt binding paragraph gains: rows list loops (`l`); seam forms A
    (slot) / B; four track-7 decays routed by track; T6 pre-empts T7. Its "1,064 B
    voice struct" is stale: measured 1,080 B, 1,112 B after K-1;
  - Sequencer: tempo in 1/100 BPM; the DJ nudge; `seq_jumpMasterTicks()`;
  - the SRAM1 total +152 B.
- **`PATTERN_DYNAMIC_STACK.md`:** `:71` (the condition table and its numbers: 42
  entries) and `:944` (the predictor's list: 11..21, 26/27).
- **`FILESYSTEM_SPEC.md`:**
  - `:868` (settings keys `bpm_fine`, `midi_chan_omni`) and `:880` (`lines=28`);
  - the kitset track-7 keys (eight; the 23-character truncation fix);
  - the load seeding rule.
- **`MIDI_SPEC.md`:** the channel rules (Omni).
- **`MENU_UI_SPEC.md`:**
  - §2.1 Global page (row 0 `… ch omn …`; `scr` on row 2);
  - PERF (DJ mode: entry, screen, pots, COPY tap, BAR nudge);
  - Load/Save pot 1;
  - VOICE7 Tetrakt decays.
- **`TETRAKT_MODULE_REFERENCE.md` / `TETRAKT_MODELLING_REFERENCE.md`:** loops, seam
  forms, track 7.
- **`INSTRUMENTS_DSP_REFERENCE.md`:** LFO waves 8..17; Tetrakt as an
  interpolation-slot user.
- **`CPU_USE_DSP_AUDIT.md`:** item 75 (Tetrakt seam blocks join the slot users);
  the new Tetrakt figure.
- **Manual:**
  - `manual/chapters/09_midi.md:10` (Global table: `omn`);
  - `06_operation_modes.md` (PERF: DJ mode);
  - `05_sequencer.md` (conditions, if listed there).

---

## 11. Implementation log

Notes taken while applying the schedule, one block per step. Line numbers below
are post-change.

### 11.0 Baselines (before step 1)

- `build/lxr02.elf` from the clean tree at `d5948f59`; flash 697,176 B / headroom
  56,488 B, ITCM 15,424 B, SRAM1 `.bss` 422,540 B (`link_budget.py`).
- Copied to `tools/dsp_test/baselines/s104p1/`: `lxr02_base.elf`, `tetrakt.txt`
  (`loops 292.5 + 2771/32 = 379.1`; steady 329.9), `lfo_dispatch_blockpath.txt`,
  `tetrakt_async_blockpath.txt`, `link_budget.txt`, and
  `p4base/{osc,mixer}.txt` (`make elf_p4_snapshot`).
- **Note:** `lfo_calc` and `lfo_stepBlock` are inlined into
  `lfo_dispatchNextValue` in this image, so their block-path gate is taken on that
  symbol (56 instructions, 0 frame loops) rather than the named symbols the
  schedule cites.

### 11.1 Step 1 — P-1 (track-button preview)

- `buttonHandler.c`: the VOICE branch runs `menu_switchPage(voiceNr)` only when
  `!(shouldPreviewVoice && menu_activePage == voiceNr)`; the comment block is as
  specified. STEP mode and FX mode untouched.
- `make all`: no warnings; flash unchanged (697,176 B).

### 11.2 Step 2 — L-1 .. L-4 (Load/Save pot 1)

- `menu.c` `case 0`: sets `SAVE_STATE_EDIT_TYPE`, `editModeActive = 1`, then
  `menu_handleLoadSaveMenu(delta, 0u)`; the busy-lock block above still drops a
  pot-1 turn while storage is busy.
- Removed the pot-only ring: `menu_loadSaveRestoredTypeIndex()`,
  `menu_instrumentLoadOptionCountForVoice()`, `menu_instrumentLoadOptionAt()`,
  `menu_instrumentLoadCurrentOptionForVoice()`,
  `menu_instrumentLoadOptionTotal()`, `menu_loadSaveLogicalPosition()`,
  `menu_loadSaveLogicalCount()`, `menu_loadSaveSetInstrumentVoiceLed()`,
  `menu_loadSaveEnterInstrumentLoad()`, `menu_loadSaveEnterInstrumentSave()`,
  `menu_loadSaveApplyLogicalPosition()`.
- **Deviation from the schedule's code-dive:** `menu_loadSaveEnterTop()` had no
  caller left once `menu_loadSaveApplyLogicalPosition()` was gone (the schedule
  expected other callers). `-Wall` reported it; removed, and the comment that named
  it now points at `menu_loadSaveEnterTopAt()`.
- L-4 comments updated; the pot-1 comment no longer claims it crosses voices.
- `make all`: no warnings; flash **695,404 B** (−1,772 B); headroom 58,260 B.

### 11.3 Step 3 — C-1 .. C-12 (conditions and the wrap)

- `MenuText.h`: 42-entry table with `!1-` at 21; all later values shifted by one.
- `sequencer.c`: `seq_repeatCountNext()` added; five wrap sites use it; the
  dispatch, self-probe and predictor renumbered (bounds 41, repeat 11..21, fill
  22/23, loop 24/25, last 26/27, mute 28..41); comments rewritten. The predictor's
  wild-card case for `c == 255` cannot occur as a stored count but is handled by
  the helper for the same reason playback is.
- `menu.c`: clamp and `menu_getConditionName()` bound 41.
- New host test `tools/dsp_test/modules/seq_conditions.c` (laws SC-1ST, SC-XY,
  SC-WRAP, SC-MAP, SC-PRED) and its Makefile rule + break. All laws pass; the
  break (plain `c + 1`) is caught. `make seq_conditions` green.
- `make all`: flash **695,484 B** (+80 B over step 2), headroom 58,180 B.

### 11.4 Step 4 — O-1 .. O-15 (MIDI Omni)

- `Par: PAR_MIDI_CHAN_OMNI` appended, dtype `DTYPE_1B16`, labels (omn / Omni
  Ch / CAT_MIDI), Global row 0 now `bpm syn qnt ch omn txf rxf mrt`, `scr` moved
  to row 2 column 3, `off` name and 0..16 clamp shared with G, parse case calls
  `midiIn_setOmniChannel()`.
- `filesystem.c`: defaults 0 (both default paths), sanitize > 16 -> 0, key
  `midi_chan_omni`, writer cases 26 `bpm_fine` / 27 `midi_chan_omni` / 28 the
  terminator `lines=28` (the recovery check compares declared against the file's
  own line count, so a pre-S104 `lines=26` still validates).
- `MidiIn.c/h`: `omni` in the config, `midiIn_setOmniChannel()`/`omniChannel()`/
  `omniTrack()`; `menu.h` included for the selected track.
- `MidiNotes.c/h`: the 128-bit held map + latch, `midiNotes_omniMap()`,
  `midiNotes_omniReleaseAll()`; the resolve path uses the one-track map and
  updates the map only on a resolved note.
- `MidiControl.c`: slots/targets ask `midiIn_omniTrack()` first; track timing
  targets compare with the selected track; `onG` is false on O. `MidiProgram.c`:
  PC on O assigns only that track; CC0 on O not consumed.
- **Ordering deviation:** `O-8` writes `bpm_fine`, so `D-1`/`D-2` (the parameter
  and its dtype) were applied here; the rest of step 5 follows. The O-1
  assert now names `PAR_BPM_FINE`.
- Host test `tools/midi_test`: `omni_map.c` generated with the extractor, law
  NM-OMNI added (unassigned / rooted / wide, G == O), break removes
  `map->global = 0u;` and is caught. `make run` green (both existing tests and
  both breaks).
- `make all`: flash **696,512 B**, headroom 57,152 B. The `#if 0` dead block in
  `filesystem.c` (retired Bank-tree quarantine, ~24,941) leaves seven pre-existing
  `-Wunused-function` warnings for functions that block references; unchanged by
  this step (the block is under `#if 0` in the base tree).

### 11.5 Step 5 — D-1 .. D-27 (DJ mode, centi BPM)

- Sequencer: `seq_tempoCenti`/`seq_tempoNudge`/`seq_tempoEff` replace `seq_tempo`;
  `seq_calcDeltaT()` is float on the effective tempo; `seq_setTempoCenti()`,
  `seq_setTempoNudge()`, `seq_getBpm()` (rounded base), `seq_setBpm()` (x100,
  clamped 655), `seq_setExtSyncSource()` recomputes the effective tempo;
  `seq_trackStepSamples()` uses the x100 form (AL-T unchanged).
- `seq_getMasterPosition()` and `seq_jumpMasterTicks()` added under PRIMASK
  (7 tracks, anchors, realign).
- Menu: BPM/FINE parse case, `menu_djOpen/Close/IsOpen/SetTempoCenti/Knob`, the
  DJ painter with the diff-paint hook, page-switch close, pot intercept, encoder
  inert, 25 ms position repaint. `settings.cfg` gains `bpm_fine` (case 26) and
  `lines=28`.
- ButtonHandler: DJ state, COPY tap, BAR nudge/ramp, SHIFT+PLAY+PERF entry, any
  MODE closes, press/release consumption, BAR LED suppression in DJ. `math.h`
  added for `fabsf`.
- Host: new `seq_tempo` (TP-PERIOD/NUDGE/ROUND; the integer-period break is
  caught) and `seq_steplen` updated to `seq_tempoEff` (AL-T unchanged).
- `make all`: flash **700,056 B**, headroom 53,608 B.

### 11.6 Step 6 — F-1 .. F-10 (LFO waves)

- `lfo.h`: `xtr` 8 and one-shots 9..17, `LFO_WAVE_COUNT` 18, `osState` in the
  padding, `lfo_retriggerPhase()`; `lfo.c`: `lfo_shapeAt`/`lfo_osWrap`/
  `lfo_calcOneShot`, new switch cases, the `LFO_WRAP_MASK` test in
  `lfo_stepBlock()`, retrigger phase; `MenuText.h` 18 names.
- `InstrumentManager`: voice and Scene retriggers call `lfo_retriggerPhase()`.
- Host `lfo`: laws LF-OS1..LF-OS5 added; all pass; the new break (DONE re-pin
  removed) is caught. Existing LF1..LF6 and their three breaks unchanged.
- **Cost (measured):** `elf_blockpath.py` on `lfo_dispatchNextValue` (the symbol
  `lfo_calc` and `lfo_stepBlock` are inlined into it in this image): **56 -> 79**
  per control (+23; the est. was +5..+8). Six slot LFOs on the two-block schedule
  is ~3 controls/block, so ~+69 instructions/block, ~+2.2 a sample-equivalent
  (est. +0.6). No per-sample loop changes (`loops 0`). Accepted as measured per
  Q31; recorded for the CPU audit.
- `make all`: flash **700,880 B**, headroom 52,784 B.

### 11.7 Step 7 — T-1 .. T-33 (four track-7 decays)

- Data: `kit_settings_t` 2 -> 8 B (two 4-byte arrays),
  `SCENE_TRACK7_DECAY_LAYERS`, layered `scene_set/getSlot6Track7Decay()` and
  `...MorphDecay()`, `scene_seedTrack7Decays()` / `scene_seedResidentTrack7Decays()`;
  reset zeroes all four layers; AutoSave live bytes 2 -> 8 (`index = 2 x layer +
  image`), apply loops all layers; S098 field lists compare/copy the arrays whole;
  KitMrp seeds every layer's Morph endpoint; the preset setter is
  `preset_setSlot6Track7Decay(scene, layer, image, value, record)`.
- **Existing-file bug fixed (T-11):** the kitset key buffer was `char key[24]` and
  the 31/37-character track-7 keys were silently truncated, so they never loaded.
  It is now 40 B, with one key table (eight keys) shared by the parser and writer;
  `storage_kitset_t.seen_t7_mask` records what the file carried, and the three Kit
  commits seed the rest. The Kit writer's eight decay lines are 2..9 and the slot
  block starts at line 11.
- Runtime: the override is a 4-bit mask + `value[4]`; `track7DecayIndex/LayerOf/
  Layer/LocalMask` helpers; the T6-pre-empts-T7 pending window; the START latch and
  decay read (`decayAlt`); the drain routes track-7 locks of `tkt_decay_a..d` to
  the layer override before the R12 test; the restore masks the four rows out for a
  track-7 Tetrakt trigger and clears the overrides. Menu: `T7_LAYER_A..D` kit cells,
  display/commit, clear target, held-step target, marker test. Live recording reads
  the layer value on track 7.
- **Host layout note:** the firmware `TktLayer == 204` assert is guarded by
  `#if !defined(TKT_HOST)`; the host `OscInfo` layout differs, so the host build
  defines `TKT_HOST` in `modules/tetrakt.c`.
- Tests: TK-T7 + break (T6-wins `continue;` removed); SQ-T7 (drain routing) and
  SQ-T7R (restore mask) with its break added to `modules/sequencer.c`. All pass.
  The Kit file round trip needs the device (no storage host target).
- **Cost (measured):** `tetrakt_cpu.py` **379.1 -> 380.7** (+1.6 a sample; est.
  +0.1). The worst block is the 4-START block, so the new latch/decay select is
  paid four times there. The absolute gate (390) passes. The step's "growth <= 0.5"
  check cannot be evaluated: `tetrakt_cpu.py --baseline` matches only an integer
  `loops` field, while the recorded baseline prints `292.5`, so the regex is a
  no-op (pre-existing). Recorded for the CPU audit.
- `make all`: flash **703,756 B**, headroom 49,908 B.

### 11.8 Step 8 — K-1 .. K-12 (Tetrakt looped samples)

- `TktLayer` gains `loopStartPh`/`loopEndPh` (212 B; firmware assert, host
  guarded); `tkt_bindLayer()` caches the loop of an SLP bind and clears it for a
  one-shot; the START latch maps a row value `samples..2s-1` to SLP with
  `waveform = value - samples`; the async predicts the seam and claims an
  interpolation slot (A), else B, else plain in `pflags` bits 3-4; the post loop
  is parameterised by `form` with six instances; the sync block dispatches on the
  form. Rows list `s00..` then `l00..` (domain `min(2n,128)-1`) and the menu shows
  `sNN`/`lNN`.
- Tests: TK-LOOP (form A exact across >= 5 wraps at 1.0/0.73/2.6 frames a sample,
  form B early wrap, no-seam bit-identical, row->bind mapping, async form choice)
  plus two breaks (seam select removed; never predict) - all caught.
- **Gates:** `tetrakt_cpu.py` **389.8 / 390** with the dearer of plain and B per
  layer; seam A premium **3.2** per held slot (charge 45). `elf_p4_check`: 83/83
  oscillator/filter loops exact, 0 missing; mixer hot loops 0 grew (1 shrank).
  `elf_blockpath`: plain 91, seam A 103, seam B 99 per post instance (per block;
  /32 in the gate).
- `make all && make img`: flash **707,068 B** (headroom 46,596 B), SRAM1 `.bss`
  **422,704 B** (+164 vs the baseline; the §0.3 table totals +152, so **+12 B over
  the quoted figure** - alignment padding in `kit_t`/`scene_t` is the likely
  source; flagged for the user). ITCM unchanged 15,424 B.
- Full host suite (`make -C tools/dsp_test -k all`): the two known failures remain
  (`voice_control` 14, `alkaline` AL-OD0). **Observed extra:** `colloid` CO-PWG
  failed once inside the full run but passes on every standalone run
  (`make colloid` x3, 0 failures, breaks caught); colloid shares no changed code
  path, so this is recorded as a pre-existing full-run ordering artifact, not
  attributed to S104.
- Device checks from §§1-8 remain outstanding (no hardware in this session).

---

## 12. Assessment (2026-11-25)

Independent check of the tree as implemented: the working copy on `d5948f59`, all
eight steps in. Everything below was re-run or re-read in this assessment, not taken
from §11.

### 12.1 What was checked

| Check | Result |
|---|---|
| `make all` after touching every changed `.c` | Builds. Warnings: the seven `filesystem.c` `-Wunused-function` (the base file gives the same seven, checked) and the toolchain's newlib/LTO notes. None from S104 code. |
| `link_budget.py` | Flash **707,068 B** (headroom 46,596 B; +9,892 B over the base 697,176, est. +6..+8 KB). ITCM 15,424 B (unchanged). DTCM 4,392 B (unchanged). |
| RAM, object by object (`nm -S`, base ELF vs new) | **+152 B exactly**, every object as in §0.3. The 12 B more in `.bss` (§11.8) is link padding between objects, not an allocation. |
| `make -C tools/dsp_test -k all` | Only the known failures: `voice_control` 14, `alkaline` AL-OD0. CO-PWG passed this run (the S101 P2 non-reproducible flake). |
| New laws and breaks | SC-*, TP-*, LF-OS1..5, TK-T7, TK-LOOP, SQ-T7, SQ-T7R pass; `seq_conditions_break`, `seq_tempo_break`, `lfo_break4`, `tetrakt_break_t7/seam/predict` and `sequencer_break_t7` are caught |
| `make -C tools/midi_test run` | notemap and notemode PASS; `notemap_break_omni` and `notemode_break_hold` caught |
| `tetrakt_cpu.py` | 389.8 / 390 as printed; **395.4 with callee costs** (A-1) |
| `elf_blockpath.py` `lfo_dispatchNextValue` | 56 → 79 (A-2) |
| Code against the schedule | Read diff by diff for all 35 changed source files (§12.3) |

### 12.2 Findings

| ID | Severity | Finding |
|---|---|---|
| A-1 | **High (CPU rule)** | Tetrakt's true worst case is **≈ 395.4**, over the 390 gate; the gate prints 389.8 |
| A-2 | **Medium (decision)** | One-shot LFOs cost +23 a control (≈ +2.2 a sample system-wide), against the +0.4 accepted under Q31 |
| A-3 | Medium (tool) | `tetrakt_cpu.py --baseline` never compares (pre-existing); the step-7 growth gate could not run |
| A-4 | Low | `menu_djIsOpen()` is 1 while a DJ entry waits behind a busy Load/Save exit, so COPY/BAR on the Load/Save page go to DJ mode |
| A-5 | Low | A BAR held through a DJ exit keeps ramping the nudge until it is released |
| A-6 | Low | The seam-A premium is checked on the Store instance only |
| A-7 | Info | Kit writer: the Morph-save condition is now the same for both endpoint lines (a behaviour change for a KitMrp save sourced from Scene 17) |
| A-8 | Info | Schedule errors the implementer corrected (§12.4) |

#### A-1 — Tetrakt worst case over the gate (high)

**What.** `elf_blockpath.py` counts each `bl` as one instruction and lists the
callee; "a caller adds the callee's own count" (its docstring). `tetrakt_cpu.py`
adds `tkt_layerControl`, `tkt_bindLayer` and `osc_userBind`, but not:

| Callee | Path | Calls in the worst block | Uncounted |
|---|---:|---:|---:|
| `modNode_interpClaim()` (new, the async's seam claim; the loop is unrolled: 4 `bl` in `Tetrakt_calcVoiceAsync`) | 36 | 4 | 140 |
| `sampleMemory_getNumSamples()` (new, the START latch's s/l mapping) | 3 | 4 | 8 |
| `sampleMemory_sampleEntry()` (pre-existing, in the bind) | 8 | 4 | 28 |
| `sampleMemory_getGeneration()` (pre-existing, async) | 3 | 1 | 2 |

- **Corrected** (same method on both ELFs): base **380.0**, now **395.4** (+15.4).
- The printed numbers are 379.1 and 389.8; the pre-existing part of the gap is
  0.9.
- The worst block is reachable: a hit restarts all four layers (four controls and
  binds), and a loop shorter than one block's read (under about 32 × phaseInc
  frames: up to ≈ 2,500 frames at the top pitch) makes the same block a seam block
  for every layer.
- A refused claim runs nearly all of `modNode_interpClaim()` (three tests and the
  counter arithmetic), so the B fallback does not avoid the cost. Charging granted
  claims to the slot pool does not bring the total under 390 either.

**Where the +309 per-block instructions went** (block paths, base → new):

| Item | Change |
|---|---|
| async | 105 → 212 (the seam prediction; est. 40–48) |
| `tkt_layerControl` | 347 → 371, × 4 (the s/l mapping, the decay select; est. +3) |
| `tkt_bindLayer` | 30 → 43, × 4 |
| post-instance block paths (the B form's max) | +44 |
| sync dispatch | +8 |
| spaceOut | +2 |

Per-sample loops grew 292.5 → 293.5 (B +0.2 a layer).

**Fix proposal** (measure each; apply all three; est. ≈ 388 after):

1. **A-1a (gate truth).** `tetrakt_cpu.py` adds callee costs for the calls it does
   not already count:

   ```python
   # S104 P1 assessment: elf_blockpath counts a `bl` as 1; add the callee's
   # own path for every call the composition does not already add.
   CALLEES = (('Tetrakt_calcVoiceAsync', 'sampleMemory_getGeneration', 1),
              ('Tetrakt_calcVoiceAsync', 'modNode_interpClaim', 4),
              ('tkt_layerControl', 'sampleMemory_getNumSamples', 4),
              ('tkt_bindLayer', 'sampleMemory_sampleEntry', BIND_LAYERS))
   blk += sum(n * (B.block_path(a.elf, callee)[0] - 1)
              for (_, callee, n) in CALLEES if present(a.elf, callee))
   ```

   With 1b, the claim term moves to the steady/seam line.

2. **A-1b (no slot claim in a restart block).**
   - Move the four claims out of the async's straight path into one
     `noinline tkt_seamClaims(voice)`;
   - the async calls it only when no layer restarted this block (a voice-local
     flag set by the control loop when any layer ran a START latch);
   - in a restart block every predicted seam takes form B (plain if the loop is
     under 2 frames).

   Why it is enough: claims and STARTs then never share a block, so the gate's
   worst is max(the START block without claims, a steady seam block with four
   claims). The latter is ≈ 336.7 + 4.5 ≈ 341. Sound: one block of B, a loop one
   frame shorter on that pass, only when a hit lands on a seam.
   `tetrakt_cpu.py` composes the two lines and gates the larger.

3. **A-1c (cheaper per-block work).**
   - Map the row value to one-shot/looped at the trigger
     (`Tetrakt_triggerVoice()`, foreground): `pendSample` is stored already
     reduced, and a `pflags` bit 5 says SLP. The START latch then only copies:
     −`sampleMemory_getNumSamples()`, a compare, a subtract and a branch in the
     control, est. −1.0 a sample.
   - Compute each layer's seam threshold (`loopEndPh − 4096 − 32 × phaseInc`) in
     the control, where `phaseInc` changes, into a new `uint32_t seamPh` per layer
     (+16 B in the slot reserve: 1,112 → 1,128 of 1,264). The prediction becomes
     one load and compare per layer: est. −1.9 a sample (async 212 → ≈ 136,
     control +4).

   **Est. after 1a + 1b + 1c:** START block ≈ 395.4 − 4.4 − 1.0 − 1.9 + 0.3 ≈
   **388.4**; seam block ≈ 341. Margin ≈ 1.6. This is an estimate; the gate
   measures it.

Until A-1 is fixed, step 8 does not meet the CPU ceiling (`MEMORY.md` S089 P2, S093
per-block rule). Steps 1–7 are unaffected: step 7 alone is 380.7 printed, ≈ 381.9
corrected.

#### A-2 — One-shot LFO cost (medium, decision)

- **Measured:** `lfo_dispatchNextValue` (with `lfo_calc` and `lfo_stepBlock`
  inlined) is **56 → 79** a control.
- Six slot LFOs on the S101 P2 A1 two-block schedule run ≈ 3 controls a block:
  ≈ +69 a block, **≈ +2.2 a sample system-wide**. Q31 accepted ≈ +0.4 "measured
  at the gate"; the measured figure is 5×.
- The dearest path is a one-shot: `lfo_calc()`'s switch, then `lfo_calcOneShot()`,
  `lfo_osWrap()`, and `lfo_shapeAt()`'s second switch.
- **Cheaper form (est. ≈ +1.1 a sample):** before `lfo_calc()`'s switch,
  `if (w >= LFO_OS_FIRST) { ph = one-shot effective phase; w = base; }`, then fall
  into the existing cases on `ph` (`sq1`/`rn1` handled in the pre-step). One
  dispatch instead of two. Every LFO pays ≈ 2 for the test.
- **Decision for you:** accept +2.2 as measured, or take the cheaper form and
  re-measure.

#### A-3 — `tetrakt_cpu.py --baseline` is a no-op (medium, pre-existing)

- `re.match(r'tetrakt: loops \d+ \+ per block …')` (`tetrakt_cpu.py:195`) needs an
  integer `loops`, but every output since S101 P2 prints a decimal (`292.5`). The
  match fails silently and growth is never checked.
- **Fix:** `loops [0-9.]+`.
- The step-7 "growth ≤ 0.5" gate (§8.6) could not run. Step 7's measured growth is
  +1.6 printed (+1.9 corrected), above the 0.5 the schedule set. The schedule's
  est. +0.1 was wrong: the START latch and decay select run in all four controls of
  the worst block.

#### A-4 — DJ mode opened behind a busy Load/Save exit (low)

- On a Load/Save page with storage busy, `menu_switchPage(PERFORMANCE_PAGE)`
  queues the exit and returns.
- `menu_djOpen()` then sets `menu_djActive` while `menu_activePage` is still
  Load/Save.
- The paint and pot hooks test the page, but `menu_djIsOpen()` does not, so
  `processPress()` takes COPY and BAR as tap/nudge on the Load/Save page (where
  they are Bank COPY and name editing) until the queued exit runs.
- **Fix:** `return (uint8_t)(menu_djActive && menu_activePage == PERFORMANCE_PAGE);`.
  Keep the BAR/COPY release paths on their own flags (they already are).

#### A-5 — Nudge ramp continues after DJ exit while a BAR is held (low)

- `buttonHandler_djNudgeService()` ramps up whenever `bh_djBar` has a held bit.
  After a MODE press closes DJ mode, a BAR still held keeps pushing toward ±5 %
  until released.
- **Fix:** in the service, treat "held" as `(bh_djBar & 0x03u) && menu_djIsOpen()`.
  With DJ closed, a held BAR ramps down. The release path stays as is.

#### A-6 — The seam-A premium is checked on Store only (low)

- `tetrakt_cpu.py` checks `StoreSeamA − Store` against 45. Add's premium is also
  3.2 (measured: 61.1 − 57.9), so nothing is over.
- **Fix:** check the larger of the two premiums.

#### A-7 — Kit writer Morph condition (info)

- Before S104, the Normal line interpolated only when
  `op_kit_save_source_scene < SCENE_COUNT`, but the Morph line interpolated in
  every Morph save.
- The new writer uses the first condition for both lines.
- For a KitMrp save sourced from Scene 17 (index 16), the Morph line is now
  written raw, consistent with the Normal line. Flagged; no change proposed.

### 12.3 Conformance, section by section

| § | Entries | Result |
|---|---|---|
| 1 | C-1..C-12 | As scheduled. Every renumbered constant (eval, self-probe, predictor, menu clamp, label bound) and all five wrap sites are correct. |
| 2 | P-1 | As scheduled |
| 3 | L-1..L-4 | As scheduled, plus `menu_loadSaveEnterTop()` removed (§12.4) |
| 4 | O-1..O-15 | As scheduled. The unresolved note-off clears its bit and returns 0; O wins over G in notes, CC1, NRPN, PC and CC0. |
| 5 | D-1..D-27 | As scheduled; A-4 and A-5 are design gaps of the schedule, not deviations |
| 6 | F-1..F-10 | As scheduled; no loader or menu path clamps the wave byte to the old 0..7 |
| 7 | T-1..T-33 | As scheduled. Seeding is at all three Kit commits and the Instrument commit, and `op_kitset` is re-initialised on every parse path that reaches them. The implementer also admitted the track-7 cells at a third `va_applyVoiceMarkers()` site. The T6-keep restore law is in `modules/sequencer.c` (SQ-T7R), not `voice_control.c`; equivalent. |
| 8 | K-1..K-12 | As scheduled; A-1 and A-6 are gate issues |

The seam forms, the pre-emption window and the restore mask were read for
correctness:
- **A:** exact seam, single wrap with `inc < lenPh`.
- **B:** reads stop at `le − 1`.
- **Plain:** unchanged, bit-identical by TK-LOOP.
- **Rebind past a new loop:** handled by `osc_userBind()`'s generation fold.
- **T6 over a pending T7:** in both orders.
- **T7 restore:** keeps T6's four decay bits held.

No defect was found in these paths.

### 12.4 Schedule errors the implementation corrected

- **L-3:** the schedule said `menu_loadSaveEnterTop()` kept a caller at `:10943`;
  it had none once `menu_loadSaveApplyLogicalPosition()` went. Removing it was
  right.
- **§8.6:** step 7's est. +0.1 was wrong (A-3).
- **§0.4 item 5:** the async prediction est. (10–12 a layer) was wrong; measured
  ≈ 27, plus the uncounted claim (A-1).
- **§0.4 item 6:** the LFO est. +5..+8 a control was wrong; measured +23 (A-2).
- **O-4 / D-1 ordering:** `bpm_fine` (O-8) needs `PAR_BPM_FINE`, so D-1/D-2 landed
  in step 4; the schedule should have said so.

### 12.5 Needed before the session can close

1. **A-1** fixed and measured (`tetrakt_cpu.py` with callee costs ≤ 390), or your
   decision.
2. **A-2:** your decision (accept +2.2, or the single-dispatch form).
3. A-3..A-6 fixed: all small; A-3 and A-6 are tool-only, and A-4 and A-5 add no
   DSP cost.
4. Device checks §9.3, all outstanding, including the Kit file round trip (T-11's
   key-truncation fix and the B–D seeding), which has no host target.

---

## 13. Assessment fixes (2026-11-25)

All the §12 fixes are applied. A-2's proposed form was applied and measured; it saves
nothing (13.2). Line numbers are post-change. Per the S084 rule, code comments name
the session ("S104 P1 assessment") but no finding ID.

### 13.1 A-1 — Tetrakt worst case back under the gate

**Result:** `tetrakt_cpu.py` with callee costs: **389.9 / 390** (START block). The
seam block is 341.2, steady 335.4. It was 395.4 corrected (389.8 printed). The
margin is **0.1**: under the gate, but thin (13.6).

| Fix | Where | What |
|---|---|---|
| A-1a | `tools/dsp_test/tetrakt_cpu.py` | `CALLEES` table: every callee of the composed functions is either composed explicitly or added as n × (callee path − 1); a callee missing from the table stops the tool. `callee_extra()` applies it. The START line adds the async's `sampleMemory_getGeneration()` and the bind's `sampleMemory_sampleEntry()` (both pre-existing gaps). The new seam line adds `tkt_seamClaims()` + 4 × `modNode_interpClaim()`. The gate is the larger of the two lines; a new last line prints it. |
| A-1b | `TetraktVoice.c` `Tetrakt_calcVoiceAsync()`, new `tkt_seamClaims()` | The prediction loop only marks seam layers (`seamMask`) and gives them form B. The four claims moved to the out-of-line `tkt_seamClaims()` (`#pragma GCC unroll 4`, so the path tool sees every call), called only when `seamMask != 0 && !anyStart`. `anyStart` is set when a layer enters its control in START. In a restart block a seam takes B for that one block. |
| A-1c (1) | `Tetrakt_triggerVoice()`, the START latch, `TetraktVoice.h` `TKT_PF_PEND_SLP` (`pflags` bit 5) | The row value is split at the trigger (foreground, once per trigger): `pendSample` is the catalogue index and bit 5 says looped. The latch copies `oscType`/`waveform` with no `sampleMemory_getNumSamples()` call. |
| A-1c (2) | `TktLayer.seamPh` (new), `tkt_layerControl()`, `Tetrakt_initVoice()` | The seam threshold `loopEndPh − 4,096 − 32 × phaseInc` (0 when the loop is shorter than one block's reach; 0xFFFFFFFF for a one-shot) is computed in the control after the bind and the step, the only places either changes. The prediction is one compare per layer. |
| A-1c (3) | `tkt_bindLayer()` | A loop under two frames binds as a one-shot (`loop_end >= loop_start + 2`), so the prediction needs no form-B length test (the last 0.6 a sample). |

**Block paths** (base → assessment → now):

| Function | Base | Assessment | Now |
|---|---:|---:|---:|
| async | 105 | 212 | **163** |
| `tkt_layerControl` | 347 | 371 | 376 |
| `tkt_bindLayer` | 30 | 43 | 44 |
| `tkt_seamClaims` | — | — | 46 (seam line only) |

Per-sample loops are unchanged (293.5).

**RAM:** `seamPh` is 4 B a layer: `TktLayer` 212 → 216 B, `TetraktVoice` 1,112 →
1,128 B, inside the existing 1,264 B slot reserve (`InstrumentManager.c:360`
assert; the `TktLayer == 216` assert, firmware only). Per your "make those fixes"
this takes up the 16 B proposed in §12 A-1c. RAM objects are still **+152 B** over
the base (checked with `nm`); `.bss` is 16 B smaller than before (padding).

**Host tests** (TK-LOOP, header updated):
- the trigger splits a looped and a one-shot row;
- `seamPh` gives the same decision as the multiply-add test for every phase
  (step 997) at tunes 0, 63, 127;
- a seam in a block where another layer restarts takes B with a slot free, and A
  the next block.

**Breaks:**
- `tetrakt_break_predict` now `sed`s the `seamPh` compare to never match;
- new `tetrakt_break_restart` drops `!anyStart`. Both are caught.

### 13.2 A-2 — One-shot LFO: single dispatch, no saving

- **Applied:** `lfo_calc()` takes the one-shot pre-step (state step, effective
  phase, `rn1`/`sq1`) before its switch and reads the base wave through the
  existing cases at that phase. `lfo_shapeAt()` and `lfo_calcOneShot()` are gone,
  and the looping cases read a local `ph`. `xtr` is an inline case. The test
  Makefile's `--func` list drops the two helpers.
- **Measured:** `lfo_dispatchNextValue` stays at **79**, the same as the
  two-dispatch form. The disassembly shows why: the longest path is a one-shot's
  wrap block (the state step and its stores) followed by the dearest shape (`xtr`),
  and both forms compile to the same branch chain. The +23 a control (≈ +2.2 a
  sample system-wide) is the cost of the one-shot state step itself.
- The single-dispatch form is kept: it is the same cost with two fewer
  functions.
- LF-OS1..5 pass, so waves 0..7 are bit-identical to the pre-S104 reference.
  `lfo_break4` is caught.
- **Still your decision (A-2):** accept ≈ +2.2 a sample as measured. Further
  savings would need a different design (for example, dropping the `xtr` shapes or
  the start delay), not a restructure.

### 13.3 A-3 — `tetrakt_cpu.py --baseline`

- The regex reads `loops [0-9.]+`. A baseline file without a `tetrakt:` line now
  stops the tool instead of passing silently.
- **Proof:** against the pre-S104 `baselines/s104p1/tetrakt.txt` (379.1) the tool
  now prints `GREW (baseline 379.1)`.
- The post-fix figure is saved as `baselines/s104p1/tetrakt_assessment.txt` (the
  START line, 389.9) for later growth checks.

### 13.4 A-4 — DJ open only on PERFORMANCE_PAGE

`menu_djIsOpen()` (`menu.c`) returns
`menu_djActive && menu_activePage == PERFORMANCE_PAGE`. A DJ entry queued behind a
busy Load/Save exit no longer takes COPY/BAR on the Load/Save page. The COPY/BAR
release paths still run on their own flags.

### 13.5 A-5 — No nudge ramp after DJ exit

`buttonHandler_djNudgeService()`:
- a BAR counts as held only while `menu_djIsOpen()`;
- `bh_djBar` bit 3 records that the last step was a hold, and the first step after
  any end of the hold (a release, or the screen closing with a BAR down) fixes the
  ramp-down step from the value reached;
- `buttonHandler_djBarRelease()` no longer computes the step;
- the state comment documents bit 3. No RAM.

### 13.6 A-6 — Seam-A premium on both instances

`tetrakt_cpu.py` checks the larger of `StoreSeamA − Store` and `AddSeamA − Add`
against 45. Both are 3.2.

### 13.7 Code-comment IDs

Comments citing S104 schedule or assessment IDs were rewritten to name the session
only: the async's "(K-5)" from the schedule, and the assessment-fix comments. The
pre-S104 code keeps its own references, as before.

### 13.8 Checks after the fixes

| Check | Result |
|---|---|
| `make all` | No new warnings (the seven pre-existing `filesystem.c` ones remain). Flash **706,564 B** (headroom 47,100 B; −504 B against §12). ITCM 15,424 B. |
| RAM objects (`nm`, base vs now) | +152 B |
| `make -C tools/dsp_test -k all` | Only `voice_control` 14 and `alkaline` AL-OD0 fail (known); 111 breaks caught, including the new `tetrakt_break_restart` |
| `make -C tools/midi_test run` | PASS, both breaks caught |
| `tetrakt_cpu.py` | 389.9 / 390 (START), 341.2 (seam), premium 3.2 |
| `elf_blockpath.py lfo_dispatchNextValue` | 79 (base 56) |

### 13.9 Open

1. **A-2:** your decision (13.2).
2. **Tetrakt margin 0.1** (13.1). Any later Tetrakt change must pay for itself.
   Candidates, if headroom is wanted:
   - fold the IDLE test into `seamPh` (set 0xFFFFFFFF when a layer goes idle):
     est. −0.3;
   - cache the effective decay byte at the START latch instead of selecting it in
     every control: est. −0.3.
3. **Device checks §9.3**, all outstanding, including the Kit file round trip and
   the loop seams (form A with interpolation on, B with it off, and a hit landing
   on a seam: one B block).
4. **Statements made out of date (§10), additions:**
   - `TETRAKT_MODULE_REFERENCE.md`: the seam threshold, no claims in a restart
     block, loops under two frames bind as one-shots, the trigger-side s/l split;
   - `CPU_USE_DSP_AUDIT.md`: Tetrakt 389.9 with callee costs (the earlier
     379.1/380.0 figures did not count `sampleMemory_*`); the LFO +23 a control;
   - `tools/dsp_test/DSP_TEST.md` (if it describes `tetrakt_cpu.py`): the
     `CALLEES` table and the two-line gate.

## 14. One-shot LFO cost: `xtr` dropped, below base (2026-11-26)

Your decision on 13.2: "unacceptable increase. Drop xtr." 13.2's diagnosis was wrong.
The 79 path was not `xtr` but a path that cannot happen: a one-shot in DONE whose
state step ran into the looping-noise random draw, both under the one shared
switch. The compiler cannot prove the two exclusive, so the static count stacks
them. Dropping `xtr` alone left the count at 79.

### 14.1 Applied

- **`xtr`/`xt1` removed** (`lfo.h`, `lfo.c`, `MenuText.h` `lfoWaveNames` 16
  entries). One-shots are 8..15 and `LFO_WAVE_COUNT` is 16. The test Makefile
  macros and `modules/lfo.c` `endv[8]` follow. No migration: wave bytes saved by
  the earlier S104 build read as the renumbered list.
- **`lfo_calc()`: one switch on wave, state and wrap.** The key is
  `2 × waveform + osState + wrap`, with `osState` stored pre-scaled (RUN 0, PRE 32,
  DONE 64) so the key costs `ldrb`, `ldrb`, `adc`. The wrap is the add's carry
  (`__builtin_add_overflow`).
  - Every looping wave takes the same case in all six (state, wrap) keys; a stale
    `osState` is ignored.
  - Each one-shot has its own cases:
    - PRE returns its start value. On the wrap it enters RUN and still returns
      the start value.
    - RUN reads its looping twin's shape. On the wrap it enters DONE and returns
      its end value.
    - DONE returns the end value.
  - No case tests the wrap flag, and no case does a state step plus a shape or a
    draw.
  - The phase is no longer re-pinned in DONE, because the held value is a
    constant.
  - Two macros (`LFO_ANY_STATE`, `LFO_ONE_SHOT`) write the cases, and are
    `#undef`'d after the switch.
- **Synced step taken straight from `phaseInc`** (`lfo_calc()`,
  `lfo_stepBlock()`): `sync ? phaseInc : (uint32_t)(phaseInc × modNodeValue)`.
  The former `× 1.0f` paid the free rate's float round trip plus a branch, and
  rounded a large `phaseInc`. Law L-D2 (sync wins) is unchanged. The S094
  comment block is rewritten to match.
- **`lfo_stepBlock()` wrap:** `noise → draw`, else `wave ≥ 8 → lfo_osWrap()`. The
  `LFO_WRAP_MASK` table test is removed, along with its macro.
  `lfo_osWrap()` is now PRE → RUN and RUN → DONE only: no draw, no re-pin.
- **`rn1`** draws on its first RUN control and goes straight to DONE.
  `lfo_retriggerPhase()` no longer draws.

### 14.2 Behaviour changes

| Change | Size |
|---|---|
| A one-shot with a start delay starts its shape one control after the wrap; the firing control returns the start value | ≤ 1 control (2 blocks, ≈ 1.45 ms) |
| `rn1` draws on its first running control, not at the retrigger (offset 0), nor on the wrap that ends the delay | 1 control later |
| `si1` holds exactly 0.5 (it was the table value at phase 0xFFFFFFFF) | none audible |
| A synced LFO steps by exactly `phaseInc` (it was float-rounded) | rate error was ≤ 6 × 10⁻⁸ relative |

### 14.3 Measured

| Path | Base (HEAD, pre-S104) | 13.8 | Now |
|---|---|---|---|
| `elf_blockpath.py lfo_dispatchNextValue` (control) | 56 | 79 | **55** |
| Stepped block, inlined in `mixer_calcNextSampleBlock` (hand count, longest = noise draw) | 34 | not counted | **30** |

- **Base:** built from a scratch worktree at HEAD, then removed.
- **Stepped block:** the tool weights the mixer's slot loop 0, so this path was
  counted by hand from the disassembly, from `instrumentManager_runtimeLfo()`'s
  return to the `bl lfo_dispatchNextValue`.
- **Net:** −1 on the control and −4 on the stepped block, for each slot LFO, every
  two blocks. That's about −0.25 a sample system-wide, so no saving is needed from
  elsewhere.
- **Control path now:**
  - the longest leaf is a tie between `rn1`'s draw-and-DONE store and the looping
    sine;
  - the key matches the base's wrap-flag cost;
  - the dispatch (`cmp`/`bhi`/`tbb`) matches the base's.
- **Size:** flash 706,428 B, −136 B against 13.8. RAM unchanged.

### 14.4 Tests

- **LF-OS2:** now checks that DONE holds over later wraps (it used to check that
  the phase was pinned).
- **LF-OS4:** `rn1` draws once, on the first control. There's a new delay case: no
  draw during the delay, then exactly one.
- **`lfo_break2`:** `sed` retargeted to the new select, with an applied-check.
- **`lfo_break4`:** replaced. A one-shot whose RUN wrap does not enter DONE must
  fail LF-OS1, and it does.
- **Laws:** LF1–6 and LF-OS1–5 pass. Waves 0..7 are bit-identical to the pre-S104
  reference (unsynced).
- **Full suite** (`make -C tools/dsp_test -k all`): only `voice_control` 14 and
  `alkaline` AL-OD0 fail (known).
- **Build:** `make all` gives no new warnings, and `make img` was written.

### 14.5 Open

1. 13.9 item 1 (A-2) is closed by this section.
2. §10 / 13.9 item 4: in `CPU_USE_DSP_AUDIT.md`, the LFO note now reads "control
   55 (base 56), stepped block 30 (base 34)", not "+23 a control".
3. Device check to add to §9.3: a delayed one-shot starts on time, and `rn1`
   changes once per retrigger, after its delay.
