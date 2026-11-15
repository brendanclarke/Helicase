# S095 P3 — Colloid follow-ups: plan

- **Status:** implemented directly, 2026-11-13, at your instruction
  (no separate schedule). §7 is answered in §9. §10 has the notes kept
  while the code was written. Host-gated. The device steps are §10.9.
  One sign-off is open: flash ends 840 B past the P2 line (§10.8, §10.10).
- **Base:** branch `dev-ph7-alkaline`. The tree is S095 P2 steps 0–2 plus the
  RNG fix (`S095_P2_IMPLEMENTATION.md` §9). The diagnostic and bisection
  images have been deleted, and the tree never held any diagnostic code.
- **Source:** your device notes of 2026-11-12, after P2 was heard on the unit.
- **Audience:** you, to answer §7, and the session that writes and implements
  the schedule. That session reads `MEMORY.md`, `S095_P2_DRAFT_COLLOID.md`
  §H, `S095_P2_IMPLEMENTATION.md` and this document.

---

## 0. The items

| # | Item | Kind | Where |
|---|---|---|---|
| F1 | A missing Instrument type folder freezes the unit | firmware defect (not Colloid code) | `filesystem.c` |
| F2 | The PWM forms other than Rec are missing | P2 decision C-D21 reversed | engine, ITCM |
| F3 | Pitch slew (`slw`) replaces glide (`por`, `pmd`); legato (`lgt`) removed | redesign | engine, rows |
| F4 | Three literal pitch intervals replace the interval sets (`int`) | redesign | engine, rows, tables |
| F5 | Detune ×5 | range | engine |
| F6 | Spread and motion ×5, with oscillators spending less time at the centre | range and law | engine |
| F7 | Position spread (`psp`) ×5 | range | engine |
| F8 | AEG page: ADSR only; `sus` moves to the MOD page | layout | rows, pages |
| F9 | One filter with right-channel offsets, plus a route selector `rte` | redesign | engine, rows |
| F10 | Filter drive on the click page; Alkaline's post-filter drive and tone on the MIX page | layout, new DSP | engine, rows |
| F11 | The page layout as a whole | layout | `colloid_menu_pages[]` |

---

## 1. F1: the freeze with a missing type folder

**What happened.** Your card had no `Instrument/Colloid/` folder. Stepping
the Load type to Colloid froze the unit. Adding the folder fixed it, so the
Colloid engine itself was never the cause.

**Root cause (found by reading; the device test confirmed the trigger):**

1. `filesystem_loadInstrumentIndex_tick()` cannot open the type folder
   (case 7). Phase 24 then hands over to `filesystem_beginInstrumentIndexRecovery()`.
2. The one-type scan (`filesystem_scanInstruments_tick()`) also finds no
   folder and finishes. It returns `op_close_status`, a global it never set
   on this path, so it reports whatever the last operation left there.
3. `filesystem_createBootIndex_tick()` then tries to create the folder. This
   function runs once per tick, and its local `entry` is set only in cases 4,
   5 and 11. **Case 6 runs on a later tick and reads `entry` uninitialised:**

   ```c
   if (!afatfs_mkdir_lfn(entry->storage_directory, ...))
   ```

   It dereferences a junk register value. The processor faults, which stops
   every lower-priority interrupt, the display's TIM7 included. That is why
   the screen stopped mid-word ("Colloine" is "Collo" written over
   "Alkaline"). The audio DMA kept replaying its last buffer, which was the
   buzz.
4. No other type ever reached this path, because every other folder already
   existed on your card. The bug predates P2. It is the same for any type
   whose folder is missing.

**The fix:**

- **F1.1 `filesystem_createBootIndex_tick()` case 6:** fetch
  `entry = instrumentManager_registryEntryAt(op_instrument_scan_registry_index)`
  before using it, and finish with `FS_STATUS_ERROR` if it is NULL. A
  comment block records why a local cannot carry across ticks.
- **F1.2 `filesystem_scanInstruments_tick()` case 0:** set
  `op_close_status = FS_STATUS_DONE` explicitly, so a missing folder counts
  as an empty list rather than as the previous operation's result.
- **F1.3 Audit:** a scan of every `filesystem_*_tick()` for locals read in a
  phase that does not set them found only this one. The other hits were
  `line_ready` and `eof`, which are set through pointer arguments, and a
  `name` that appears only in a comment. The schedule repeats the scan
  against its own tree.
- **Behaviour after the fix:**
  - A missing type folder is created along with an empty `.hcindex`, and
    the Load row shows an empty list.
  - Nothing freezes.
  - A folder whose index is empty is rescanned on each browse. That is
    harmless, and it stays as it is.

**Device check (one flash):** rename `Instrument/Colloid` on the card, then
step the Load type to Colloid. The unit must not freeze, and the card must
then hold `Instrument/Colloid/.hcindex`. Rename the folder back afterwards.

F1 is independent of the Colloid changes, and it ships first (§8 step 0).

---

## 2. Assessment of each note

### F2. The PWM forms: why only Rec, and the plan

**Why.** This was P2 decision **C-D21**, which you accepted with the other
recommendations:

- the Colloid's PWM oscillators run in their own pair loops in ITCM;
- each extra PWM form was estimated at about 500 B of ITCM;
- the approved ITCM line (+1,024 B) held the plain loops and one PWM form.

So under `typ pwm` the `wav` list has a single entry, Rec. The Alkaline and
the drum engines offer **Sin, Tri, Saw and Rec** under `pwm`
(`OSC_PWM_WAVE_COUNT` 4).

**Plan: the four platform forms, in two loop families rather than four.**

- **Rec and Saw: the existing loop, with no new code path.**
  - The platform's two-read form is `row[a] + sign × row[b]`
    (`osc_pwmReadD()`): Rec is sign −1, Saw (summed) is sign +1.
  - The Colloid's `col_pulse()` hard-codes the subtraction. It becomes
    `sign × row[b]` with the sign passed per pair. That is an MLA instead
    of a SUB, the same cost on the M7.
  - The Saw form's level compensation (`oscPwmLevelSawQ14`) varies with
    each oscillator's width. Applying it per oscillator per sample would
    add a multiply in the inner loop. Instead, the mean of the oscillators'
    gains is folded into the per-block `inGain`, which costs nothing per
    sample. With `psp` 0 every width is the same, so the mean is exact.
  - `oscPwmDSaw[]` replaces `oscPwmDRec[]` for the width under Saw.
- **Tri and Sin: one new pair of loops (store and add), shared by both.**
  - This is the platform's phase-warp form: the WT and WS loops,
    `osc_pwmReadT()` and `osc_pwmReadS()`, with per-oscillator
    `oscPwmTriW/Ra/Rb` and `oscPwmSinW/Ra/Rb`.
  - Tri reads the octave row of `triTable`; Sin reads `sine_table`. The
    only difference is the row pointer and the warp constants, both set
    per block, so one loop pair serves both.
  - The per-oscillator warp values (W, Ra, Rb) are computed per block in
    `col_oscControl()`.
- **ITCM cost:** one new loop pair, about 500–600 B (*estimate*, from the
  208 B measured for the P2 PWM pair, plus the warp). This crosses the P2
  ITCM line (§5); the hardware limit of 16,384 B still leaves room.
  Alternatively, the warp pair can go in flash at a CPU cost measured on
  the device. That is question Q12.
- **List order under `pwm`:** the platform's Sin, Tri, Saw, Rec, so a Scene
  reads the same across engines and `menu_formatWaveCell()`'s PWM path is
  shared. This is question Q10. The `wav` list keeps Saw, Rec, Tri, Sin.

### F3. Pitch slew replaces glide; legato is removed

**Assessment.** You're right that P2 modelled a traditional mono synth:

- `por` was a glide time;
- `pmd` chose between "all notes" and "legato only";
- `lgt` (tie) made a held retrigger skip the attack and the redraw.

None of that fits free-running oscillators. The new model:

- **Every oscillator has its own current pitch**, in log2 Hz, held in the
  voice (`pitchCur[o]`). Each block it moves toward its own target at the
  slew rate:

  ```
  target[o] = note + coa + fin + interval[group(o)] + detune c_o
  ```

- **Anything that changes the target is slewed:** a new note, `coa`, `fin`,
  an interval, `dtn`. LFO and velocity pitch modulation are the one open
  case (Q2).
- **`slw` 0 is instant.** At voice init the pitch jumps to its target, so
  the first note after a load does not sweep in.
- **Retrigger:**
  - both envelopes always restart their attack from the level they are at
    (P2 already does this; `Colloid_triggerVoice()` keeps the level);
  - the per-oscillator draws (`rnd[]`, used by `psp`) are redrawn on every
    trigger, now that there is no legato case.
- **Removed:**
  - the rows `por` (`col_glide`), `pmd` (`col_glide_mode`) and `lgt`
    (`col_tie`);
  - the voice fields `glide`, `glideMode`, `tie`, `gliding` and `pitchJump`
    (the jump becomes an init-only flag);
  - `ONOFF_TAG_COL_GLIDE_MODE` and `colGlideModeNames`;
  - `COL_GLIDE_ROWS`;
  - the `legato` host break.
- **Added:** the row `slw` (`col_slew`) on the MOD page, after `dst amt sus`.
- **The slew law is question Q1.** "Slew rate" usually means a constant
  rate (semitones per second): a bigger jump takes longer, so interval
  changes arrive staggered. "Lazily" could also mean an exponential
  approach, where every oscillator arrives at the same time. I recommend
  the constant rate.
- **Cost:** per oscillator per block, one compare-and-step in log2 plus one
  `alk_exp2f()`. That is control rate (about 12 × 30 cycles per block,
  *estimate*) and adds nothing per sample. The exp2 replaces P2's
  `colRatio[]` lookup and its first-order detune approximation, which
  would be 18 cents out at ×5 detune (F5). So the change pays for F5's
  accuracy too.

### F4. Three literal pitch intervals replace the interval sets

**Assessment.** P2's `int` chose one of sixteen preset chords
(`colIntervals[]`, `colMember[]`). You want three independent bipolar
numeric intervals.

- **Rows:** `in1`, `in2` and `in3` (`col_interval_1` to `_3`), bipolar
  numeric (`DTYPE_PM63`, stored 63 = 0). They go on the FM page's first
  screen with `dtn`: `in1 in2 in3 dtn`.
- **Unit:** one step is one semitone (Q5). The display shows −63 to +63, and
  oscillators above 0.45 fs are clamped as in P2. If you'd rather have a
  smaller range such as ±24, that needs a new display type.
- **Groups (Q6):** oscillator `o` belongs to group `o mod 4`, where group 0
  is the root and groups 1–3 are `in1` to `in3`.
  - With 12 oscillators that is three per group.
  - Under `pwm` (6 oscillators) the root and `in1` get two each, and `in2`
    and `in3` one each.
  - With all intervals at 0, every oscillator is on the root, which is
    today's unison at `int` 0.
- **Stereo home positions:** `colHome[]` is re-derived so that each group's
  oscillators spread across the field rather than one group landing on one
  side. The same goes for `colDetC[]`, so each group is detuned
  symmetrically.
- **Removed:**
  - the `int` row (`col_interval`);
  - `colIntervals`, `colIntervalCount`, `colMember` and `colRatio`;
  - `COL_INTERVAL_COUNT`;
  - `ONOFF_TAG_COL_INTERVAL` and `colIntervalNames`;
  - the generator's interval code in `tools/colloid/gen_col_tables.py`.

### F5. Detune ×5

- `COL_DETUNE_MAX_LN` goes from ±50 cents to **±250 cents** at `dtn` 127.
  The `dtn²` curve stays, so the low end keeps its fine resolution.
- The detune offset is applied in log2 as part of the slewed pitch (F3), so
  it is exact at any amount. P2's `1 + c δ` form would have been about
  18 cents out at 250 cents.
- The `init.col` default byte (40) then gives about 25 cents instead of 5.
  The listening step sets the default.

### F6. Spread and motion ×5, with less time at the centre

**Assessment.** Today an oscillator's position is:

```
p = clamp(spr × home[o] + depth × m(t), −1, +1) × w(f)
```

with `depth = 0.6 √mot`.

- **Multiplying `spr` and the motion depth by 5** pushes the sum well past
  ±1 for most oscillators. The clamp then holds them at the edges for part
  of every motion cycle, so they spend less time near the centre, which is
  what you described.
- **Edge law (Q4):** a hard clamp makes the oscillators sit still at the
  edges. I recommend the division-free soft clip already in the voice (the
  cubic `x − 4/27 x³` on a ±1.5 clamp, rescaled to ±1). The oscillators
  then crowd toward the edges as `spr` and `mot` rise, but keep moving.
- **What `mot` ×5 scales (Q3):** `mot` sets both a depth (0.6 √mot) and a
  rate (0.05 to 2 Hz). I recommend depth ×5 with the rate unchanged.
  Rate ×5 would reach 10 Hz, which is a tremolo-like autopan and a
  different effect.
- **Unchanged:**
  - the low-frequency weighting w(f) (C-D14) still pulls low notes toward
    the centre;
  - `spr` and `mot` still show `---` in any mono route (F9).

### F7. Position spread (`psp`) ×5

- **Wavetable:** the frame offset becomes `rnd × psp × 5 × F/2`, wrapped in
  the table.
- **PWM:** the width offset becomes `rnd × psp × 5 × 63.5`, folded at 0 and
  127. The single fold becomes a loop of at most three folds, or a modulo
  on 254.
- **The catch (Q9).** P2's maximum already reaches half a table either side
  (wtb) or the whole width range on one side (pwm). Taken literally, ×5
  makes everything above `psp` ≈ 25 sound the same: uniformly random over
  the whole table or range. If that's what you want, take it as written.
  If you meant that the knob felt weak over its useful range, a curve that
  reaches today's maximum by about `psp` 40 and keeps widening the PWM
  range above that may serve better.
- **PWM at `pos` 0 (50 %):** today the fold means spread only narrows the
  pulse. With ×5 the folds reach the whole width range, so a wide `psp`
  gives every oscillator an independent width. That is probably the
  "not nearly enough" you heard.

### F8. AEG: ADSR only; `sus` moves to MOD

- **AEG:** `atk dec slv rel`, one screen.
- **MOD:** `dst amt sus slw`, one screen.
- `sus` keeps C-D8 and C-D19: it holds at the sustain level while it is on
  and the transport runs.

### F9. One filter with right-channel offsets, and `rte`

**Assessment.** P2 shows two independent filters (`cta rsa tya`,
`ctb rsb tyb`) and a mode (`fmd` par/ser). You want one filter, with the
right channel or the second stage described as offsets from it.

**Page 1: `frq res typ rte`**

- `frq` is the cutoff and `res` the resonance.
- `typ` is the left filter's type: LP BP HP Nch Pek.
- `rte` (the route) offers `str` (stereo), then LP BP HP Nch Pek.
  - **`str` (default):**
    - the left filter takes `frq res typ`, `fam`, `drv`;
    - the right filter always uses the left's type, with `frq + fro`,
      `res + reo`, `fam + eno` and `drv + dro`;
    - at zero offsets the two filters are identical;
    - this is P2's parallel loop (`col_renderPar()`) with no per-sample
      change; the right drive gain is a second per-block constant.
  - **A filter type:**
    - the voice is mono, so `spr` and `mot` show `---`;
    - the signal is drive(`drv`) → the left filter (`typ`, `frq`, `res`,
      `fam`) → drive(`drv + dro`) → the second filter (`rte`'s type,
      `frq + fro`, `res + reo`, `fam + eno`);
    - this is P2's series loop (`col_renderSer()`);
    - the second shaper's gain is folded into the first filter's output
      mix coefficients (mH, mB, mL × gain), so it costs nothing per
      sample (Q7).

**Page 2: `fro reo eno dro`**

- These are the cutoff, resonance, envelope-amount and drive offsets.
- All are bipolar (`DTYPE_PM63`, 63 = 0) and in the units of the cell they
  offset: one `fro` step is one `frq` step, and so on. The sums are clamped
  to their cell's range.

**Superseded:**

- C-D16 (series is mono, `spr`/`mot` show `---`) now applies to every
  non-`str` route.
- C-D17 (one `fam` on both cutoffs) becomes `fam` plus `eno` on the second
  filter.
- The rows `cta rsa tya fmd ctb rsb tyb` become `frq res typ rte fro reo eno dro`.
- `ONOFF_TAG_COL_FILT_MODE` and `colFiltModeNames` are replaced by a route
  tag with seven names (`str LP BP HP Nch Pek`, plus the count).

### F10. Drive placement, and Alkaline's post-filter drive and tone

- **Filter drive:** `drv` (`col_drive`, ×1 to ×16 into the filters) moves to
  the click page's second screen: `fam drv`.
- **Post-filter drive and tone, as the Alkaline (S094 P1 S4):**
  - the shared `instrument_drive` row (`Overdriv`, `drv`) through
    `IM_SPECIAL_INSTRUMENT_DRIVE`, which gets a Colloid branch;
  - a `col_tone` row (`Tone`, `ton`), byte 127 = open;
  - they take the MIX page's third and fourth cells, `vol pan ton drv`, as
    the Alkaline's do.
- **Per sample, after the filter:**

  ```
  o  = makeup × sat(y × gain) × amp
  tz = o + c (tz − o)
  ```

  - `gain` and `makeup` come from the Alkaline's `alkOvdGain[]` and
    `alkOvdMakeup[]`, and `c` from `alkToneC[]`;
  - the `c` form is exact when open: with `ton` 127 and drive 0 the output
    is today's to the bit, which a host law checks.
- **The saturator (Q11):**
  - the Alkaline's is the OTA's tanh, a [3/2] Padé with one VDIV per sample
    (`DL_P32N`/`DL_P32D`); matching its feel means using the same form;
  - in stereo that is two VDIVs per sample, about +30–40 cycles
    (*estimate*); in a mono route the sample is computed once;
  - the Colloid's own division-free cubic is the cheaper fallback, if the
    device CPU run (§8 step 7) says so.

### F11. The new page layout

Each page is 16 cells, shown as four-cell screens. Empty cells are not
listed.

| Button | Page | Screen 1 | Screen 2 |
|---|---|---|---|
| OSC | 0 | `coa fin pos psp` | `wav typ` |
| AEG | 1 | `atk dec slv rel` | — |
| MOD | 2 | `dst amt sus slw` | — |
| FM | 3 | `in1 in2 in3 dtn` | `spr mot` |
| Click | 4 | `fat fde fsl fre` | `fam drv` (filter drive) |
| FILTER | 5 | `frq res typ rte` | `fro reo eno dro` |
| LFO | 6 | as now (the Drum layout) | |
| MIX | 7 | `vol pan ton drv` (post drive) | — |

Your note said the intervals "can be on the 'mod' page after 'slw'", and
also that the first FM page is the three intervals and detune. I've read
that as the table above shows: MOD is one screen, and the intervals are on
FM. Q8 confirms it.

---

## 3. The parameter rows

Fifty rows (P2 has 47; the limit is 64). The Colloid has no stored content
other than `init.col`, and the project's no-migration rule applies, so the
enum is **renumbered in page order**, not appended. `init.col` is
regenerated by `tools/colloid/make_init_col.py`.

| Row | Key | Long / short | Type | Default (byte) |
|---|---|---|---|---|
| coa | `osc1_pitch_coarse` | Coarse / coa | as P2 | 48 |
| fin | `osc1_pitch_fine` | Fine / fin | as P2 | 63 |
| pos | `osc1_position` | Position / pos | as P2 | 0 |
| psp | `col_pos_spread` | PosSprd / psp | 0..127, ×5 law (F7) | 0 |
| wav | `osc1_wave` | Waveform / wav | as P2; 4 entries under pwm | 0 |
| typ | `osc1_type` | Type / typ | as P2 | 0 |
| atk dec slv rel | `col_attack` … | as P2 | as P2 | 40 70 90 70 |
| sus | `col_sustain` | Sustain / sus | on/off | 1 |
| slw | `col_slew` | Slew / slw | 0..127, 0 = instant (Q1) | 0 |
| in1 in2 in3 | `col_interval_1` … `_3` | Intrvl1..3 / in1..in3 | PM63, semitones | 63 (0) |
| dtn | `col_detune` | Detune / dtn | 0..127, ±250 cents | 40 (by ear) |
| spr | `col_spread` | Spread / spr | ×5 law (F6) | 80 (by ear) |
| mot | `col_motion` | Motion / mot | ×5 law (F6, Q3) | 30 (by ear) |
| fat fde fsl fre | `col_fenv_*` | as P2 | as P2 | 0 70 64 70 |
| fam | `col_fenv_amount` | Amount / fam | PM63 | 63 |
| drv | `col_drive` | Drive / drv | as P2 (filter drive) | 0 |
| frq | `col_cutoff` | Cutoff / frq | 0..127 | 90 |
| res | `col_reso` | Reso / res | 0..127 | 20 |
| typ | `col_filt_type` | FiltType / typ | LP BP HP Nch Pek | LP |
| rte | `col_route` | Route / rte | str LP BP HP Nch Pek | str |
| fro reo eno dro | `col_cut_ofs` … `col_drive_ofs` | CutOfs ResOfs EnvOfs DrvOfs / fro reo eno dro | PM63 | 63 (0) |
| vol pan | as P2 | as P2 | as P2 | 100 63 |
| ton | `col_tone` | Tone / ton | 0..127, 127 open | 127 |
| drv | `instrument_drive` | Overdriv / drv | 0..127 (Alkaline law) | 0 |
| LFO (12), velocity (2) | as P2 | as P2 | as P2 | as P2 |

Short names repeat across pages (`typ`, `frq`, `drv`), as they already do on
the Alkaline (`alk_drive` and `instrument_drive` are both `drv`). The LFO
and velocity destination lists tell them apart by category.

**Cell-inactive rules (`colloidParameters_cellInactive()`):**

- `pos` and `psp` show `---` under `wav`, as in P2;
- `spr` and `mot` show `---` when `rte` is not `str`.

---

## 4. Files touched (overview; the schedule cites lines)

- **F1:** `Core/Hardware/SD/filesystem.c`, in `filesystem_createBootIndex_tick()`
  and `filesystem_scanInstruments_tick()`.
- **Engine:**
  - `ColloidVoice.h`: fields added and removed, the route and PWM-form
    constants, and the voice size re-checked against the 1,264 B slot;
  - `ColloidVoice.c`: the pitch slew, the intervals, detune, positions,
    `psp`, the filter route and offsets, the post stage, and the PWM form
    selection;
  - `ColloidOsc.c` and `ColloidOsc.h`: the signed two-read form and the
    warp pair;
  - `ColloidTables.c` and `ColloidTables.h`, with
    `tools/colloid/gen_col_tables.py`: the interval tables removed,
    `colHome[]` and `colDetC[]` re-derived per group.
- **Rows and pages:**
  - `ColloidParameters.c` and `ColloidParameters.h`: the descriptor count
    becomes 50;
  - `Core/Menu/MenuText.h`: the Colloid tags and name lists;
  - `Core/Menu/menu.c`: the Colloid branch of `menu_formatWaveCell()` for
    four PWM entries.
- **InstrumentManager.c:**
  - `instrumentManager_waveDomainMax()`, Colloid under pwm: 3;
  - the `IM_SPECIAL_INSTRUMENT_DRIVE` Colloid branch;
  - `Colloid_setOscType()`, whose PWM count becomes 4.
- **Storage and tools:**
  - `SD_CARD/Instrument/Colloid/init.col`, regenerated;
  - `tools/colloid/make_init_col.py`;
  - `check_special_tags.py`, `TABLES['COL']`;
  - `tools/dsp_test/modules/colloid.c` and the `colloid` target in
    `tools/dsp_test/Makefile`, as in §8.
- **Build:** `STM32F765VIHx_FLASH.ld`. If Q12 puts the warp pair in ITCM, a
  new subsection `.itcm.s095p3` is named after `.itcm.s095p2`, so no
  existing function moves.

---

## 5. Budgets

The P2 lines approved 2026-11-12 apply until you approve new ones.

| Resource | P2 line | Now | P3 estimate | Verdict |
|---|---|---|---|---|
| Flash | +8,192 B over 711,036 = 719,228 | 717,516 (1,712 B left) | +1 to +2 KB net: warp pair, post stage, routing, slew; minus the interval tables and names | may cross the line: sign-off asked if the build does |
| ITCM | +1,024 B over 14,032 = 15,056 | 14,904 (152 B left) | +500 to 600 B (the warp pair) | **crosses the line**: Q12 |
| RAM (.bss) | 0 B new | 0 | 0: the voice stays inside its union slot | — |
| Voice struct | slot reserve 1,264 B | 1,032 B | about +150 B: `pitchCur[12]`, the warp values for 6 oscillators, two tone states, two drive gains | fits; measured at build |
| CPU (Colloid only) | ≤ 19,620 cycles per block (Basic), device-measured | host count: plain12 par 309 per sample | post stage +30–40 per sample in stereo; warp forms unknown; slew at control rate | the device run in §8 step 7 decides |

The CPU ceiling rule holds:

- every per-sample change is inside the Colloid's own loops;
- F1 is control code;
- no other engine's loop may change. The loop-length gate over every
  `P4_OSC_SYMS` loop is run at each step, as in P2.

The **device CPU run** (P2 step 3, C-D2: 12 or 10 oscillators) has not been
done yet. It is folded into §8 step 7, with P3's costs included.

---

## 6. P2 decisions this supersedes

| P2 | Was | Becomes |
|---|---|---|
| C-D4 | frames latched at each non-legato trigger | latched at every trigger (no legato) |
| C-D15 | `mot` depth 0.6 √mot, rate 0.05–2 Hz | depth ×5, rate per Q3, soft edge per Q4 |
| C-D16 | series is mono; `spr`/`mot` show `---` | any non-`str` route is mono |
| C-D17 | one `fam` on both cutoffs | `fam`, plus `eno` on the second filter |
| C-D21 | PWM: Rec only | Sin, Tri, Saw, Rec (F2) |
| draft §2.2 | 16 interval sets (`int`) | three intervals `in1`–`in3` (F4) |
| draft §4 | glide time, glide mode, tie | the pitch slew `slw` (F3) |
| draft §3 | two filters and a mode | one filter, offsets and `rte` (F9) |

`S095_P2_IMPLEMENTATION.md` §7 lists statements that P2 made out of date.
The P3 schedule adds the ones above for you to log.

---

## 7. Open questions (answers needed before the schedule)

| # | Question | Recommendation |
|---|---|---|
| Q1 | Slew law: constant rate (semitones per second; bigger jumps take longer) or exponential (every oscillator arrives at once)? | Constant rate, in log pitch. `slw` maps exponentially from instant to about 10 s per octave. |
| Q2 | Should LFO and velocity pitch modulation (`coa`/`fin` as targets) go through the slew? Through it, a slow slew smears vibrato. | No. The slew acts on the note, `coa`, `fin`, the intervals and detune; the modulation multiplies after it, as in P2. |
| Q3 | `mot` ×5: depth only, or depth and rate? | Depth only. Rate ×5 reaches 10 Hz, an autopan effect. |
| Q4 | Spread and motion past ±1: hard clamp (oscillators sit at the edges) or soft clip (they crowd the edges but keep moving)? | Soft clip, using the voice's existing cubic. |
| Q5 | Interval range: ±63 semitones (1 step = 1 semitone, the existing bipolar display) or a smaller range such as ±24 (needs a new display type)? | ±63, 1 semitone per step. |
| Q6 | Which oscillators follow which interval? | `o mod 4`: 3 per group with 12 oscillators; under pwm 2, 2, 1, 1. |
| Q7 | In a mono route, is `dro` the drive into the second filter, `drv + dro`? | Yes. |
| Q8 | Layout: MOD is `dst amt sus slw`; FM is `in1 in2 in3 dtn` then `spr mot`. Is that what you meant? | As written in §2 F11. |
| Q9 | `psp` ×5 is fully random above about `psp` 25. Literal ×5, or a curve? | Literal ×5 if the top of the knob should be fully random; otherwise the curve in F7. |
| Q10 | `wav` order under `pwm`: the platform's Sin, Tri, Saw, Rec, or the Colloid's own order? | The platform's order. |
| Q11 | Post-filter saturator: the Alkaline's tanh (identical feel, two VDIVs per sample in stereo) or the cheaper cubic? | The Alkaline's tanh; the cubic only if the CPU run requires it. |
| Q12 | The Tri/Sin warp loops: ITCM (+500–600 B past the P2 line, inside the 16 KB hardware limit) or flash (no ITCM, with a CPU cost)? | ITCM, signed off at +700 B (measured at build). |

---

## 8. Order of work and gates

Each step ends with:

- `make all`, with the budgets recorded in `tools/dsp_test/baselines/s095p3/`;
- `make -C tools/dsp_test all`, exiting 0 with every break caught;
- the loop-length gate (`elf_looppath.py` over every `P4_OSC_SYMS` loop),
  showing no other engine's loop changed;
- the notes kept in the implementation document as each step lands.

0. **F1, the missing-folder fix.** This can be flashed on its own. Device
   check as in §1.
1. **Rows, pages, tags and `init.col`** (F8, F11, §3), with the engine
   reading the new fields at their P2 meanings where they exist.
   - `check_special_tags.py` and `make_init_col.py` are updated.
   - Host: the existing laws still pass.
2. **The pitch model** (F3, F4, F5): per-oscillator slewed log pitch, the
   three intervals and detune ×5.
   - Laws: CO-INT is rewritten for literal intervals and groups; a new
     CO-SLEW covers the rate, `slw` 0 as instant and the init jump;
     CO-OSC is updated.
   - Breaks: `legato` is removed; `slew0` (a slew that ignores
     `slw` 0) and `group` (oscillator-to-interval mapping) are added.
3. **Positions and `psp`** (F6, F7).
   - Laws: CO-POS and CO-MOT get the ×5 and edge laws; CO-WTB and CO-PWM
     get the ×5 spread with wrap and fold.
   - Break: `fold` (a single fold at ×5).
4. **The filter route and offsets** (F9).
   - Laws: CO-SVF and CO-SER are updated, and a new CO-RTE checks that
     `str` at zero offsets gives identical left and right filters, and
     checks the mono route's chain.
   - Break: `rte` (a right filter that ignores `fro`).
5. **The post drive and tone** (F10).
   - Law: CO-POST, where drive 0 with tone 127 gives today's output
     exactly, and the tone is the Alkaline's one-pole.
   - Break: `tone` (a k-form tone that is not exact when open).
6. **The PWM forms** (F2): the signed two-read form, then the warp pair.
   - Law: CO-PWM gets the four forms, tested against the platform's
     `osc_pwmReadD`/`T`/`S` on the same widths.
   - Break: `sign` (Saw read as Rec).
7. **Device:**
   - the CPU run (P2 step 3, C-D2) with P3 in, both routes, `typ wav` 12
     and `pwm` 6;
   - then a full Scene, underruns, and listening, which sets the by-ear
     defaults (`dtn`, `spr`, `mot`, `slw`).

The user's standing rules apply:

- `MEMORY.md` and `knowledge_files/` are not edited;
- commits are yours;
- records go in `tools/dsp_test/baselines/s095p3/`, not `build/`;
- one flash per device question;
- RAM or ITCM growth needs your sign-off (Q12).

---

## 9. Answers (user, 2026-11-12)

| # | Answer | As implemented |
|---|---|---|
| Q1 | Recommendation: constant rate | `slw` 0 instant; 1..127 from 1 octave per ms to 1 octave per 10 s, exponential in `slw` |
| Q2 | Recommendation: modulation after the slew | `osc.modNodeValue` multiplies after `pitchCur` |
| Q3 | Recommendation: depth only | depth 3.0 √mot; rate 0.05–2 Hz unchanged |
| Q4 | "yes curve" | the soft edge, the cubic `col_drive()` on the position sum |
| Q5–Q8, Q10, Q11 | Recommendations | as written in §2 and §7 |
| Q9 | Read as "curve" too | `psp` k = 5 psp^1.393: P2's maximum at `psp` 40, ×5 at 127 |
| Q12 | Approved | the warp pair in ITCM, `.itcm.s095p3` |

**My reading of "Q4, yes curve".** The word "curve" names the Q9 option,
while Q4's own recommendation (a soft clip) is also a curve. I applied both:
the soft edge for spread and motion, and the `psp` curve. If you meant the
literal ×5 for `psp`, it is one constant (`COL_PSP_EXP` 1.0).

**The second-instance report (2026-11-12) is closed: no bug.** Morph was on
for that track. The Morph worker re-applies every MORPHABLE row (coarse and
the envelope times among them) from the slot's two images, so those cells
follow Morph rather than the knob.

---

## 10. Implementation notes (2026-11-13)

### 10.1 F1: the missing-folder freeze

- **`filesystem_createBootIndex_tick()` case 6:** fetches `entry` from the
  registry before use and finishes ERROR if it is NULL.
- **A second defect, found while fixing this:**
  - cases 1 and 6 cleared `op_file_ready` before an `afatfs_mkdir_lfn()`
    that may refuse to start (the FAT layer busy);
  - the phase then waited forever for a callback that never comes;
  - now the wait and the retry flag are armed only once the mkdir is
    accepted; a refusal stays ready with `op_file` NULL and retries next
    tick.
- **`filesystem_scanInstruments_tick()` case 0:** `op_close_status =
  FS_STATUS_DONE`.
- **The audit (§1 F1.3):** this was the only cross-phase local.
- Each change carries its comment block in place.

### 10.2 F2: the PWM forms

- **The two-read form, Saw and Rec:**
  - `col_pulse()` takes `sign` (an MLA): Rec −1, Saw +1;
  - the pair loops take it as their last argument;
  - widths come from `oscPwmDRec` or `oscPwmDSaw`;
  - the levels `oscPwmLevel*Q14` are averaged over the stack into the
    per-block `inGain`.
- **The warp form, Sin and Tri:**
  - `col_warpStore` and `col_warpAdd`, one oscillator per loop (a pair would
    spill);
  - they use `osc_pwmWarp()` from `oscPwmWarp.h` and the Colloid's read;
  - Tri reads `triTable[octave + oscPwmTriLift[k]]`, Sin reads `sine_table`;
  - W, ra and rb are looked up per oscillator in
    `Colloid_calcVoiceSyncBlock()`.
- **List order:** under `pwm` the `wav` list is the platform's (Sin, Tri,
  Saw, Rec), and the menu's shared pwm branch names it. Note: switching `typ`
  from `wav` to `pwm` keeps the byte, so `wav` Saw (0) becomes `pwm` Sin.
  The drum engines do the same.
- **Level change:** every pwm form is levelled to a 32,768 full scale as the
  platform's store is. P2's Rec ran at half that, so a P2 Rec patch is now
  6 dB louder at `pos` 0.

### 10.3 F3–F5: the pitch model

- **The slew:**
  - each oscillator has `pitchCur[o]` (log2 Hz), slewed per block in
    `col_oscControl()` toward `base + iv[o mod 4] + colDetC[o] × dOct`,
    then `alk_exp2f()` per oscillator;
  - `pitchJump` is 1 from init, becomes 2 at the first trigger and 0 after
    that trigger's block, so a load never sweeps in.
- **Removed:**
  - the rows and fields `por`, `pmd`, `lgt`, `int` (`glide`, `glideMode`,
    `tie`, `gliding`, `pitchLog2`);
  - `colRatio`, `colIntervals`, `colIntervalCount`, `colMember`,
    `COL_GLIDE_ROWS`;
  - the tags `ONOFF_TAG_COL_INTERVAL` and `ONOFF_TAG_COL_GLIDE_MODE`, and
    their name lists.
- **`colDetC` and `colHome` are re-laid by the generator:**
  - every group (o mod 4) has a low, a near-centre and a high detune, and
    both sides of the field;
  - the six pwm oscillators sum to zero;
  - `gen_col_tables.py` asserts all of this.
- **Detune:** ±250 cents, `dtn²` curve.
- **Every trigger** restarts both attacks from their levels and redraws.

### 10.4 F6, F7: positions and `psp`

- **Positions:** p = w × edge(5 spr home + 3 √mot m), with the edge being
  `col_drive()`, moved above its new callers.
- **`psp`:**
  - `col_pspK()` (noinline) drives the frame wrap (a modulo, up to 2.5
    tables) and the width fold (a triangle of period 254);
  - the law checks psp 1 keeps every width in 0..127.

### 10.5 F9: the filter

- **Fields:** `cut res filtType route` and the offsets `cutOfs resOfs envOfs
  driveOfs` (stored /127, 63 = 0).
- **Per block:** `cutR`, `resR`, `driveR` and `famR` are built from them,
  and B's type is `typ` under `str`, else `rte − 1`.
- **`inGainR`:** the right drive under `str`; under a mono route, the
  inter-stage gain, folded into A's mix coefficients at loop entry.
- **`colRouteNames`:** `str LP BP HP Nch Pek`, on tag 5 (was `fmd`'s).

### 10.6 F10: the drive and tone

- **The overdrive:** `col_post()`, the OTA's [3/2] Padé with one VDIV per
  channel per sample.
  - The Alkaline's `alkOvdGain`, `alkOvdMakeup` and `alkToneC` tables, by
    byte.
  - It crossfades in over drive 0–8 (`postWet`), so drive 0 is the dry
    signal to the bit (law CO-POST).
- **The tone:** the exact c form, with a state per channel. The mono route
  runs it once and copies the state across.
- **The row:** `instrument_drive` reaches the voice through a Colloid branch
  of `IM_SPECIAL_INSTRUMENT_DRIVE`.

### 10.7 F8, F11: rows and pages

- **Rows:** 50, renumbered in page order; `COLLOID_PARAM_DESCRIPTOR_COUNT`
  is 50.
- **Pages:** as §2 F11.
- **`init.col`:** regenerated by `make_init_col.py` (50 rows).
- **Checks:** `check_special_tags.py` passes (252 rows, 0 mismatches).

### 10.8 Gates and measurements

Records are in `tools/dsp_test/baselines/s095p3/`.

**Host gates:**

- `make -C tools/dsp_test all` exits 0, with 62 breaks caught.
- Colloid:
  - 16 laws at 0 failures: new CO-SLEW, CO-RTE and CO-POST; CO-INT, CO-W,
    CO-MOT and CO-PWM rewritten for P3;
  - 15 breaks caught: `legato` removed; `slew0`, `group`, `fold`, `rte`,
    `tone` and `sign` added.
- The first `tone` break slipped: a tone state of 12,345 rounds too finely
  to show at int16. The law now poisons the state with ±3e7.

**Other engines:** loop lengths are unchanged across all 62 `P4_OSC_SYMS`
loops against the P2 image (`loop_lengths.txt`).

**Budgets:**

| Resource | Result |
|---|---|
| Flash | 720,084 B: net +2,568 B over P2 and **856 B past the P2 line (719,228 B): needs your sign-off** |
| ITCM | 15,232 B (+328 B, inside Q12's +700 B) |
| Voice | 1,112 B (slot reserve 1,264 B) |
| .bss | unchanged |

On flash: the first build was +3,784 B. `-Ofast` had inlined and unrolled
the control loop into the async, and `col_pspK()` into four callers. Both
are now out of line and the loop is rolled, which gave back 1,216 B.

**CPU** (`colloid_cpu.txt`; linked instruction counts per sample, P2 → P3).
`colloid_cpu.py` now finds the loop in `col_oscControl()` and counts the
warp mode.

| Mode | P2 | P3 |
|---|---|---|
| plain12 stereo | 309.2 | 382.1 |
| plain12 mono | 299.1 | 356.0 |
| pwm6 Saw/Rec stereo | 255.9 | 324.1 |
| pwm6 Sin/Tri stereo | — | 326.1 |

About 40 of the growth per sample is the post stage in stereo; the rest is
the per-oscillator slewed pitch at control rate. 382 × 32 is about 12.2k
instructions per block against the Basic 19,620 cycles. The device decides.

### 10.9 Device steps (yours)

1. **F1:** rename `Instrument/Colloid` on the card and step the Load type to
   Colloid. There must be no freeze, and the folder and an empty `.hcindex`
   must be created. Rename the folder back afterwards.
2. **Card:** copy the new `SD_CARD/Instrument/Colloid/init.col`. The old
   file loads with the P2 keys missing; anything missing loads 0.
3. **CPU (P2 step 3, C-D2):** one Colloid, then a full Scene with several,
   in `str` and a mono route, under `wav`, `pwm` Rec and `pwm` Sin. Listen
   for underruns.
4. **Listening:** set the by-ear defaults (`dtn`, `spr`, `mot`, `slw`) and
   check the `psp` curve.

### 10.10 Device report (2026-11-13): out-of-tune intervals; clipping; init.col

**Intervals out of tune on a new note: stale stored data, not the engine.**

- On the host, eight note changes (plain and pwm, `slw` 0, `dtn` 0) left
  every oscillator on its interval, with 0 errors over 0.5 cent.
- P3's renumbering puts P2's stored `por` / `pmd` / `int` bytes (12–14)
  under `in1`–`in3` (0, 0 and 0..15, read as about −63 semitones). Morph re-applies
  the stored images at each trigger.
- Resetting the instrument fixed it (user). The second Colloid in the
  autosave (Scene 0, slot 4, "fluxc1 5") still holds P2 bytes: `rte` 84,
  `in1` 6. Reload it.

**Clipping into the filter: fixed.**

- On the reported patch (autosave `SD_CARD_COL`, Scene 0, slot 0: Saw,
  intervals +12/−12/−12, `dtn` 62, `res` 99, BP), the 12-saw stack peaked at
  2.24 into the ±1.5 shaper at drive 0, and 1.45 % of its samples were
  clamped.
- **Change:**
  - `COL_HEADROOM` ×0.35 on the bus into the filter drive;
  - `COL_MAKEUP` ×1.43 folded into the amp's scale (net −6 dB against P2);
  - no per-sample cost.
- **Measured** (`clip_reported_patch.txt`):
  - the reported BP patch: shaper peak 0.80, nothing clamped, output peak
    27,028, no int16 clipping;
  - the same patch on LP at `res` 99 still touches int16 on 0.078 % of
    samples (the resonant peak).
- Drive (`drv` ×1..×16) still reaches saturation, now from a clean start.

**init.col.**

- The intervals and offsets were already stored as 63, which shows as 0
  (`DTYPE_PM63` displays the byte minus 63).
- The ×5 ranges made the old spread defaults extreme, so they are re-based
  in `make_init_col.py` and `Colloid_initVoice()` alike:

  | Row | Was | Now | Gives |
  |---|---|---|---|
  | `dtn` | 40 | 20 | ±6 cents at the outer pair |
  | `spr` | 80 | 20 | 0.79 of the field |
  | `mot` | 30 | 10 | depth 0.84 at 0.07 Hz |

**Gates:** host suite exit 0 (62 breaks); flash 720,068 B.

