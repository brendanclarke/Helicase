# S091 P4 — `grn` Rework: Target Model, Feasibility, and the Engine as Built

- **Date:** 2026-11-04. **Branch:** `dev-ph7-voice_expansion`.
- **Status:** plan, all decisions taken (§A5: Q1–Q12, user 2026-11-04).
  The code implementation schedule is `S091_P4_IMPLEMENTATION.md`
  (pre-verified in a scratch build, 2026-11-05); §A7 records what its code
  deep dive decided and measured. Part A records the user's target model
  for the granular sub-type, checks it against the engine accepted in
  S091 P3, and the new page organisation for every CrumpBit sub-type (§A6;
  it adds a shared `bal` row). Part B is the reference for `grn` as built
  (`S091_P3_Implementation.md`, accepted on hardware 2026-11-04).
- **Code today:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`:
  - `crumpBit_runGrain()`: per block;
  - `crumpBit_grainSpan()`: per frame;
  - `crumpBit_grainPick()`: one new grain's settings.

  Page side: `CrumpBitParameters.c` (`crumpBit_layoutGrain`,
  `crumpBit_subtypeRowNames`, `crumpBit_uiFormatValue3()`).

---

# Part A — The target model

## A0. Verdict

**The model is possible on the engine we have.**
- It keeps everything structural:
  - the record line and delay line splitting the share;
  - two grain voices reading the record line with drop-sample pitched
    16.16 positions;
  - one hardware RNG word per block;
  - the frame loop split at the block's one trigger;
  - the delay line's return chain;
  - the shared mix stage and output filter.
- What changes is the **control model** (§A2):
  - when a grain starts is decoupled from how long it plays;
  - new laws for position, length, density, width and pitch;
  - a flat-topped window instead of a triangle;
  - a delay time of its own;
  - level jitter (`amp`) retired, freeze kept.

**It is a better model than the current one.** Today one value, R (`div`),
is the delay time, the position window and the size base all at once. A
grain's length is also its restart period, so overlap is always exactly two.
The target model gives the standard granular control set (position, length,
density, scatter, pitch set, width). It also has a clear identity point: the
"copy" setting reproduces the input (§A1.7).

**Estimated cost** (the plan's estimate; §A7 has the measured values:
RAM +12 B, `grn` ≈ 201 per frame):
- RAM: **+8 B** (runtime 88 → 96 B). With P3's +12 B this is +20 B, inside
  the user's +32 B approval.
- Per frame: about **+2 to +6** over today's 131 (a per-voice age and a
  trapezoid window).
- Per block: **cheaper** than today. The controls index rhythmic positions
  directly, so the 113-instruction division search goes.
- The new shared `bal` (§A6.2) adds ≈ 0–2 per frame to every sub-type.
- Whole Effect: ≈ 190–200 per frame for `grn`, against today's ≈ 192 and
  the accepted ≈ 195. To be measured when the schedule is pre-verified.

**Two places where the spec meets physics** are decided (§A3, user
2026-11-04):
1. More than two grains overlapping: the oldest voice is stolen with a
   3 ms fade.
2. Grains that would read audio the record head is overwriting are
   clamped. Full-buffer reverse while recording therefore plays about half
   the buffer; it plays the whole buffer with freeze on.

---

## A1. The model (user, 2026-11-04)

N is the record buffer: one line, half the CrumpBit share. That is 63,328
samples = 1.436 s with the whole arena, less when voice units are claimed.
Every control below that says "maximum" or "whole buffer" means the live N.

### A1.1 Position (bipolar)

Where a grain starts, measured back from the record head.

| Setting | Start |
|---|---|
| 0 | immediately behind the incoming sound (a lag of ≈ 2 samples) |
| positive | a fixed lag in **rhythmic divisions**, growing with the knob, up to N. With the copy settings (§A1.7) this is a plain single-line delay of that division |
| negative | a **random** lag, uniform in [2, 2 + m·(N − 2)] with m = \|position\|/max. It runs from "near the head" to "anywhere in the buffer, equally likely" at full negative |

### A1.2 Length (bipolar)

How long each grain plays, and in which direction.
- The magnitude is the length; the sign is the direction.
- Full positive = the whole buffer forward.
- Full negative = the whole buffer reversed (subject to §A3.2).
- **Q1 (decided):** the centre is the shortest grain, ≈ 5 ms (220
  samples). From there the length grows **exponentially** (equal ratios per
  step) to N at each end: about 8 octaves over 63 steps per side. Labels
  show ms/s.

### A1.3 Density (bipolar)

How often grains are triggered, independently of their length.

| Setting | Triggers |
|---|---|
| 0 | none: no new grain starts (playing grains finish) |
| +1 | one per N (once per maximum buffer length) |
| +2 … max | progressively shorter **rhythmic** periods |
| negative | **random** intervals; the further negative, the more grains on average |

Because density and length are independent, grains overlap when the length
exceeds the period, and leave gaps when it is shorter.

**Q6 (decided):**
- On the negative side, each interval is uniform in [0, 2 × mean], at
  least one block (32 frames).
- The mean runs from N at −1 down to ≈ 20 ms at full negative,
  exponentially.

**Positive-side list (Q7):** see §A1.8.

### A1.4 Width

Each grain's pan is drawn at its start and held for its life.

| Width | Pan |
|---|---|
| 0 | mono (centre) |
| 64 | anywhere from fully left to fully right, uniform |
| 127 | 50–100 % left or 50–100 % right (\|pan\| uniform in [0.5, 1], random side) |

In between, the distribution blends from one setting to the next.

### A1.5 Pitch: P1–P3 and Spread

- **P1** is the pitch of every grain while spread is 0. P1 = 0 plays at the
  natural pitch.
- **Spread** draws P2 and P3 more and more often as it rises:
  - at 80, all three are equally likely;
  - above 80, each grain also gets a random offset around its chosen pitch,
    up to **±1 semitone** at 127 (today ±35 cents).
- **Q4 (decided):** P1, P2 and P3 are **absolute** pitches, each ±24
  semitones. Today P2/P3 are relative to the root.

### A1.6 Delay line

- A standard digital delay, fed by **the grain signal only** (Q9, as
  today), with the current feedback (`fbk`, return chain unchanged).
- **Its own delay time (Q8, decided by the user).** The time is not
  rhythmic-only:
  - it is a **continuous spread of delay times** across the control;
  - **the rhythmic divisions are marked** on it, as on CrumpBit's other
    time controls. The shared StepScale vocabulary
    (`Core/Sequencer/StepScale.h`) is a 128-position continuous log curve
    in tempo-relative time, with 14 marked stops (`/64 32t /32 16t d32 /16
    8Tr d16 /8 4Tr d/8 /4 d/4 /2`), shown by `stepScale_formatShort()`;
    other positions show their number;
  - **fixed and clamped**, like the others: the knob always names the same
    position, and stops at the longest time that fits N at the current
    tempo.
- **Q10 (decided, user): the extended curve.** StepScale ends at a half
  note (1.0 s at 120 BPM, 0.67 s at 180 BPM), below the 1.436 s buffer
  above ≈ 84 BPM.
  - `grn`'s delay time gets its own 128-position curve, with StepScale's log
    law and stop labels, extended to one bar: `/64` … `/2`, `d/2`, `1br`,
    six octaves.
  - The clamp keeps it inside N at slow tempos.
  - The curve is a generated flash table (≈ 512 B + the stop list); no RAM.
    Any RAM it might need is approved (user).
- The delay line reads the sample exactly D behind its write head. A
  change of D jumps (no glide, user, S091 P3).

### A1.7 Freeze (`frz`): kept (Q5, "very important")

- While on, the record line is not written; grains keep reading the held
  buffer, and the delay line keeps running (as today).
- **Anchored (§A3.3):** positions are measured from the record head at the
  moment of freezing. Position 0 stays "the last thing recorded", and a
  positive Position stays that far back.
- **Freeze lifts the §A3.2 limits:** nothing is overwritten, so every
  length, direction and pitch reads the held audio. A full-buffer reverse
  needs freeze.

### A1.8 Rhythmic lists for Position and Density (Q7)

- **Fixed and clamped:** the knob always names the same division and stops
  at the longest one that fits N at the current tempo. Stored values are
  never rewritten (A30).
- **Position positive:** the division list from the shortest to the longest
  that fits, over 63 steps (several steps per division). The lag is the
  division's length.
- **Density positive:** +1 is the buffer length N, then the divisions from
  the longest that fits down to the shortest.
- **The list:** the StepScale stops plus the extended curve's longer stops
  (`d/2`, `1br`, Q10).

### A1.9 The identity point: the "copy" setting

**Density +1, Length full positive, Position 0, P1 0, Spread 0** makes the
grain output a copy of the input:
- A grain starts ≈ 2 samples behind the head and plays forward at speed 1.
  It and the head advance together, so it reads the input ≈ 2 samples late
  for the whole buffer length.
- The next trigger comes exactly one N later and starts at the same lag, so
  it reads the same samples the old grain is reading.
- The two voices crossfade between identical signals, so the joins are
  seamless. This needs the flat-topped window with complementary fades
  (§A2).
- "Copy" means a copy of what is recorded: the mono, 8-bit, bit-crushed
  dry signal (the record line is mono today). At `mix` 127 the copy
  replaces the stereo input with that mono version, plus the delay's
  echoes. A stereo record line would double the record memory or halve its
  length.
- From there, every control moves away from the copy in one direction:
  - Position positive turns it into a delay;
  - Position negative turns it into a scatter;
  - Length makes it a chop or a reverse;
  - Density makes it repeats or a cloud;
  - pitch and width colour it.

---

## A2. Target against the engine as built

| Element | As built (Part B) | Target | Change |
|---|---|---|---|
| Record line, delay line | 50/50 split, written together | same | none |
| Voices | 2, restarting every G and G/2 apart | 2, triggered by density, each with its own start time and length | per-voice age and length (+8 B) |
| Trigger timing | tied to grain size G | density: rhythmic period or random interval, independent of length | a per-block trigger countdown. A minimum period of one block (32 frames) keeps "≤ 1 trigger per block", so the two-span frame loop stays |
| Overlap | always exactly 2 | length/period; can exceed 2 | voice allocation (§A3.1) |
| Window | triangle over the whole grain (or its active part) | flat top with short complementary fades (≈ 3 ms, at most half the grain) | same per-frame cost class: `min(fade_in, fade_out, 1)` clamped at 0 |
| Grain length | R/32..R from `sze`, centre smallest | bipolar Length, up to N, centre smallest | new law; latched per grain at its start |
| Position | 63 follow, right spray within R, left slices of R | 0 follow, positive fixed rhythmic lag up to N, negative random up to the whole buffer | new law; no R involved |
| Density | duty 25–100 % of G, same restart rate | bipolar: off at 0, rhythmic positive, random negative | new law; the row becomes bipolar |
| Pitch | root + relative `st2`/`st3`, ±35 cents detune | P1–P3 absolute, ±1 semitone detune | small change in the candidate pick |
| Width | fixed ±25 % lean blended to uniform random; linear pan | mono → uniform → wide-only, as §A1.4 | new law (per grain, per block: no frame cost). Recommend constant-power pan |
| Delay time | = R | its own continuous time with the rhythmic stops marked, fixed and clamped to N (Q8) | its own row (`rte`) on the extended StepScale-style curve (Q10); the tap read is unchanged |
| Output-filter balance | — | new shared row `bal` (§A6.2) | per-channel filter coefficients in stage 4 (every sub-type) |
| Page order | `sze fbk div mrp` overlay, … (§B6) | the §A6 order | layouts and the manager's SELECT 1 screen 1 |
| Rhythmic values | free Rate snapped by the 113-instruction division search | knob → division index directly | cheaper per block |
| Startup gate | whole engine silent until R + 4G is recorded (up to 1.44 s) | per grain: each grain's reads must lie in recorded audio (§A3.2 with `valid` for N) | no global silence |
| Freeze (`frz`) | record line held; starts follow the moving head | **kept** (Q5), anchored at the freeze moment (§A3.3) | one anchor (2 B) |
| Level jitter (`amp`) | per grain | **retired** (Q5) | row 20 removed; the per-voice level bytes go (every grain at full level) |

**Rows.** The grain model needs no new rows and frees one (`amp`). The
page organisation adds one shared row (`bal`). The mapping (page
names to be decided in the presentation task):

| Control | Row (key) | Dtype / default |
|---|---|---|
| Position `pos` | 10 `crump_dly_pan` | bipolar, 63 = 0 |
| Length `lng` | **20** `crump_grain_length` (the retired `amp` index; §A7: not `ton`, whose default must stay flat for `dly`) | bipolar, default 127 = full positive (copy) |
| Density `den` | 19 `crump_grain_density` | **becomes bipolar**; default +1 (copy) |
| Delay time `tim` | 7 `crump_rate` | continuous, divisions marked, clamped to N (Q8, Q10) |
| Feedback `fbk` | 6 `crump_feedback` | as today |
| P1 / P2 / P3 | 15 `crump_aux`, 16, 17 | bipolar semitones (absolute) |
| Spread `spr` | 18 | as today, detune to ±1 st |
| Width `wid` | 21 (unchanged) | new law |
| Freeze `frz` | 9 `crump_sync` | as today, anchored (Q5) |
| ~~Level jitter `amp`~~ | ~~20~~ | **removed** (Q5); `CRUMPBIT_ROWS_GRN` loses it. No backward compatibility: an old `crump_grain_amp` key in a file is ignored. No lane map named it |
| Filter balance `bal` | **22** (new, shared) | bipolar, 63 = 0 (§A6.2); `CRUMPBIT_PARAM_COUNT` 22 → 23 (§A7) |

With those defaults, a Scene that switches to `grn` plays the copy (plus the
delay's echoes) until something is turned.

---

## A3. Where the spec meets physics (decisions)

### A3.1 More than two grains at once

Density and length are independent, so a long grain triggered often wants
more than two voices. Example: Length N with Density +2 overlaps 2–4
grains. The engine has two voices; a third costs ≈ +17 per frame (S091 P3
plan, S5) plus state.

| Option | Behaviour | Cost |
|---|---|---|
| **A. Steal the oldest** (**decided**, Q2) | a trigger with both voices busy restarts the older voice after a ≈ 3 ms fade-out, so the new grain starts at most one block late | per-voice release flag (in the age/length bytes) |
| B. Skip triggers | while both voices are busy, a trigger is ignored | none; dense settings thin out |
| C. Shorten | effective length ≤ 2 × period | none; Length stops at a ceiling set by Density |

### A3.2 Reading audio that is being overwritten

While recording, the record head moves forward one sample per sample. A
grain's read point must stay between the head and the oldest kept sample,
i.e. its lag in [2, N]. With start lag `lag0`, ratio `r` and length `L`:
- **Forward, r > 1** (pitch up): the grain catches up with the head:
  L ≤ (lag0 − 2)/(r − 1).
- **Forward, r < 1** (pitch down): the grain falls behind:
  L ≤ (N − lag0)/(1 − r).
- **Reverse, ratio r:** the read walks back while the head walks forward:
  L ≤ (N − lag0)/(1 + r).

**Consequences:**
- **Full-buffer reverse while recording** (Length full negative, Position 0,
  pitch 0) can play only **≈ N/2** before reaching freshly written audio.
  It plays the whole buffer backwards only with **freeze** on.
- **Pitch up at Position 0** has no room at all. Today's engine moves the
  start back by the needed headroom.
- **The copy case** (r = 1 forward) has no limit: the lag stays constant.

| Option | Behaviour |
|---|---|
| **A. Clamp** (**decided**, Q3) | per grain, at its start: pitch-up grains start further back if needed (headroom, as today); the length is cut to what stays inside recorded audio, and the grain ends early with its fade-out. Clean |
| B. Allow crossing | the grain reads across the head or the oldest sample: a tape-like discontinuity click once per crossing. Glitchy, could be a deliberate texture |

With freeze on, nothing is overwritten. Reads wrap freely around the
frozen buffer, and every length is allowed.

### A3.3 Freeze anchor

Today a frozen grain's position is measured from the still-moving record
head, so in follow mode it sweeps through the frozen loop. **Decided**
(Q5, with freeze kept):
measure positions from the head position at the moment of freezing, so
Position keeps its meaning (0 = the last thing recorded).

### A3.4 Rhythmic lists against tempo and buffer

Position, Density and the delay time use divisions at the current BPM, so
which divisions fit in N changes with the tempo and the share.
- **Fixed list, clamped** (**decided**, Q7, for every rhythmic control
  including the delay time): the knob always names the same
  division and stops at the largest that fits. This matches today's `rte`:
  stored values are never rewritten (A30).
- **Fitted list:** the knob spans only the divisions that fit. More travel,
  but a value's meaning changes with tempo and share.

---

## A4. Implementation sketch (for the P4 schedule)

- **State** (`CrumpBitGrainState`, 32 → 40 B):
  - shared: `write_pos`, `length`, `valid` (6 B), trigger countdown (2 B),
    freeze anchor (2 B), delay DC memories (8 B);
  - per voice: 16.16 position (4), Q3.12 step (2), age (2), length/end (2),
    pan (1), i.e. 11 B × 2. The level bytes go with `amp`.

  Total 40 B; the union 40 B, runtime 96 B (+8 B). In the shared part the
  `bal` byte takes the slot of the removed `amp` byte (0 B).
- **Per frame**, against today:
  - +1 age increment per voice;
  - the trapezoid window in place of the triangle (same operation count);
  - the delay tap reads its own time (unchanged cost).
  - Estimate 133–137.
- **Per block:**
  - the trigger scheduler: countdown; next period from the density list,
    or a random interval;
  - the frame of the block's trigger and its voice (oldest or free);
  - the candidate pick: position by its law, pitch slot, detune, width pan,
    the length clamp of §A3.2 (one divide);
  - division lookups, with no search;
  - the delay time: a curve lookup and the clamp to N.
- **One trigger per block:** a minimum trigger interval of 32 frames
  (0.73 ms, above 1,300 grains/s), far above any musical density.
- **Host laws to add:**
  - the copy identity: at the copy settings, the grain output equals the
    recorded signal (the 8-bit mono dry signal, which is what "a copy of
    the input" can mean here) delayed by the fixed lag, sample for sample,
    across retriggers;
  - Position positive = a delay of exactly the division;
  - no read of unrecorded or overwritten audio under every setting;
  - the width distributions;
  - the density statistics;
  - freeze: the record line bit-exact while frozen, and Position measured
    from the anchor;
  - the delay time equals its curve value at every position, clamped to N;
    the marked positions are the StepScale stops plus `d/2` and `1br`;
  - `bal`: at `bal` 0 both channels are bit-exact with today's filter;
    otherwise each channel equals the single-channel filter at its own
    `frq ± bal`, for every type.

---

## A5. Decisions (user, 2026-11-04)

| # | Question | Decision |
|---|---|---|
| Q1 | Length at the centre, and the law to N | shortest ≈ 5 ms; exponential (equal ratios) to N, labelled in ms/s (accepted recommendation) |
| Q2 | More than two overlapping grains (§A3.1) | A: steal the oldest with a 3 ms fade (accepted) |
| Q3 | Reads into overwritten audio (§A3.2) | A: clamp; the whole buffer reversed needs freeze (accepted) |
| Q4 | P2/P3 absolute or relative to P1 | absolute, each ±24 st (accepted) |
| Q5 | `frz` and `amp` | **keep freeze ("very important")**, anchored (§A3.3); **remove `amp`** (user) |
| Q6 | Negative Density | uniform intervals in [0, 2 × mean], mean from N at −1 to ≈ 20 ms at full negative (accepted) |
| Q7 | Rhythmic lists: fixed or fitted | fixed and clamped, like the other controls (accepted; user confirmed for the delay time) |
| Q8 | Delay time | **not rhythmic-only (user):** a continuous spread of times across the control, with the rhythmic divisions marked as on the other delay controls (StepScale stops); fixed and clamped |
| Q9 | Delay line input | the grains only (accepted) |
| Q10 | Delay-time curve ceiling | the extended curve to one bar, accepted; any RAM it needs approved (user) |

| Q11 | `grn` FX-lane pool | 4 `den`, 5 `tim`, 6 `frz`, 9 `pos`, 10 `lng`, 14 `p1`, 15 `spr`; P2/P3, `wid` and `fbk` lane-less; `bal` lane-less in every sub-type (accepted) |
| Q12 | Order of the other sub-types' controls after `sub` | as proposed in §A6.3 (accepted) |

No question is open.

---

## A6. Page organisation (user, 2026-11-04)

These are CrumpBit's Effect pages in encoder order (the encoder walks every
cell across screens and SELECT buttons). Rules:
- The shared controls come first, the output filter's next, then `mix` and
  `sub`.
- **Only sub-effect parameters may follow `sub`.**
- This applies to every sub-type. §A6.3 gives `grn`'s order (user) and the
  proposed order for the others (Q12).

### A6.1 The pages

| # | SELECT / screen | Row 0 | Cells | Owner |
|---|---|---|---|---|
| 1 | SELECT 1 s0 | names | `typ out vol pan` | manager (unchanged) |
| 2 | SELECT 1 s1 | names | **`mrp run len scl`** (was `run len scl mrp`) | manager: **applies to every Effect type** (`flt` too) |
| 3 | SELECT 2 s0, the overlay (home) | bit display, no labels, values only | **`mrp frq res bal`** (was `ton fbk rte mrp`) | CrumpBit |
| 4 | SELECT 2 s1 | names | **`typ frq res bal`**: the output filter with its type | CrumpBit |
| 5 | SELECT 2 s2 | names | **`mix sub`** + the first two sub-effect controls | CrumpBit |
| 6+ | SELECT 2 s3, then SELECT 3 s0.. | names | the rest of the sub-effect controls, in order | CrumpBit, per sub-type layout |

- **`mrp`** stays on two pages, as today: the manager's (page 2) and the
  overlay (page 3).
- **`frq` and `res`** appear on pages 3 and 4. Both cells edit the same row.
- **The SELECT 2 button** returns to the overlay (the home screen).
- **SELECT buttons on CrumpBit pages are bit toggles**, as today: the later
  screens are reached with the encoder.
- **Implementation:**
  - page 2: the manager's `second[]` cell order in
    `menuEffects_cellAt()`, plus the layout comment at the top of
    `menuEffects.c`;
  - pages 3–6+: the `crumpBit_layout*` tables (one per sub-type, or a
    shared head plus a per-sub-type tail) and `custom_row0`;
  - the screen-1 `mrp` keeps its manager handling (lane 0, Morph amount).

### A6.2 `bal`: output-filter balance (new shared row)

- **Bipolar, 63 = 0** (shown −63..64), default 0.
- **Positive** raises the right channel's cutoff by +1 `frq` step per
  value and lowers the left's by −1. **Negative** does the opposite:
  - `frq_R` = clamp(`frq` + `bal`, 0, 127);
  - `frq_L` = clamp(`frq` − `bal`, 0, 127).
- **Shared** by every sub-type: it belongs to the output filter (stage 4),
  so it joins `CRUMPBIT_ROWS_SHARED`. Morph, LFO and Pattern, like `frq`.
- **Implementation:**
  - stage 4 computes `g`, `a1..a3` per channel (one more table read and one
    more divide per block);
  - the frame loop keeps two coefficient sets (≈ +0–2 per frame, register
    pressure);
  - the type and resonance crossfade stays shared;
  - at `bal` 0 the result is bit-identical to today's.
- **RAM:** one row byte in the shared part, in the slot freed by `amp` (0 B
  net).
- **Lane:** none (the nine shared lanes are full; Q11).

### A6.3 The sub-effect controls after `sub`

**`grn` (user):** freeze, position, length, density, then the pitch
controls, then width, then the delay time and feedback.

| Page | SELECT / screen | Cells |
|---|---|---|
| 5 | SELECT 2 s2 | `mix` `sub` `frz` `pos` |
| 6 | SELECT 2 s3 | `len` `den` `p1` `p2` |
| 7 | SELECT 3 s0 | `p3` `spr` `wid` `tim` |
| 8 | SELECT 3 s1 | `fbk` – – – |

(The page names are working names; their final spelling belongs to the
presentation task.)

**The other sub-types (proposed, Q12):** the same rule, the most-used
control first. Each fits pages 5–6.

| Sub-type | Page 5 (after `mix sub`) | Page 6 |
|---|---|---|
| `dly` | `rte` `fbk` | `snc` `dpn` `ton` – |
| `cmb` | `not` (`rte`) `fbk` | `pol` (`snc`) `dpn` `ton` – |
| `pit` | `int` (`aux`) `win` (`rte`) | `fbk` `snp` (`snc`) `dpn` `ton` |
| `rvb` | `siz` (`rte`) `dcy` (`fbk`) | `frz` (`snc`) `wid` (`dpn`) `dmp` (`ton`) – |

- **`ton` leaves the overlay** in every sub-type: the overlay is now the
  filter (`mrp frq res bal`).

### A6.4 Cost

- **RAM:** 0 B for the pages; `bal`'s byte reuses `amp`'s slot.
- **Flash:**
  - the layouts: one 146 B table per distinct layout; `dly`/`cmb` and `rvb`
    can share one;
  - the Q10 curve, ≈ 0.5 KB.
- **CPU:** pages are foreground. `bal` costs ≈ 0–2 per frame and one
  divide per block, in every sub-type.

---

## A7. Implementation decisions and measurements (code deep dive, 2026-11-05)

`S091_P4_IMPLEMENTATION.md` implements Part A. Its code was built and
tested in a scratch copy of HEAD `b0ed5701`. These points were decided
there, where the plan left room or the code showed a need.

### A7.1 Rows

- **Length gets its own row (20, `crump_grain_length`, page `lng`),** not
  `ton` (row 11) as §A2 first proposed.
  - A row has one default for every sub-type. `ton` must default to 63
    (flat tone for `dly`), which would make a new `grn` Length the
    shortest grain instead of the copy setting.
  - Row 20 was the retired `amp`, so no other index moves: Width stays
    21, `bal` is appended as 22, and the count is **23**.
- **Page names** (working names; the presentation task decides):
  - `p1` is `aux`'s name in `grn`; `p2` and `p3` are rows 16/17's short
    names;
  - `tim` (delay time), `frz`, `pos`;
  - `lng`, not `len`, because SELECT 1's FX-sequence length is `len`.
- **`dpn` becomes BUFFER_DEPENDENT.** In `grn` Position's rhythmic half is
  clamped by `effective_max` to the stops that fit the line (fixed and
  clamped, Q7); in every other sub-type `dpn` is never limited.
- **Defaults give the copy setting** (Density 64 = +1, Length 127), with
  `bal` 63.

### A7.2 State and RAM

- **`CrumpBitGrainState` is 44 B,** not 40 B: a 32-bit trigger countdown
  (random intervals reach 2N = 126,656 frames) and a deferred-trigger byte
  for steals.
- **Runtime 100 B: +24 B over 76 B**, inside the +32 B approval (P3 +12 B,
  P4 +12 B).
- **The shared part stays 56 B:** `bal`'s byte took the alignment byte, and
  `lng` took `amp`'s.
- **The arena:** in the measured build the extra 12 B crossed the 32 B
  boundary. FXBUF is now 126,656 B at `0x20001140`, exactly what the N1
  constants assumed.

### A7.3 Engine

- **Fade:** every grain fades in over 128 frames and out over the 128
  frames after its Length. The fixed power-of-two fade makes the window
  steps exact, so the copy setting's crossfades sum to exactly 1.
- **Pan law:** the balance law (unity at the centre, the S075 delay-pan
  law), not constant power as §A2 recommended. With constant power the
  copy setting and Width 0 would play at −3 dB.
- **Retry instead of silence:** a trigger whose grain has nothing recorded
  behind its lag yet fires again on the next block. A rhythmic Position
  waits until that lag is recorded. Entering `grn` therefore plays
  within a block or two, not after a whole Density period.
- **Steals:** the stolen voice fades out from the trigger frame, and the
  trigger is deferred by the fade (128 frames), when that voice is free.
  One steal at a time; a further trigger with no free voice is dropped.
- **Density's rhythmic periods are at most N.** A division longer than the
  line acts as +1.
- **Freeze:**
  - `valid` does not grow while frozen;
  - unfreezing a line that was never full restarts `valid` at 0;
  - a share change stops both voices and drops the anchor.

### A7.4 Measured (scratch build; worst case per frame, P3's method)

| Sub-type | Frame loop | Whole Effect | P3 |
|---|---:|---:|---:|
| `grn` | **136** (133 on the other span) | **≈ 201** | 192 |
| `dly` / `cmb` / `pit` / `rvb` | unchanged | `bal` adds ≈ 0.7 | — |

- **`grn`:** the frame loop is +5, from the flat-top windows. The per-block
  work is +4 per frame: the scheduler, the per-grain clamp and the
  Position limit (76 instructions per block in the manager).
- **Against your ≈ 195:** `grn` is ≈ 6 above it, ≈ 0.25 % of the CPU over
  P3. Whether it stays within the board's previous worst case is hardware
  check H1 of the schedule.

### A7.5 Tools

- **`elf_looppath.py` gains `--in-range`:** the new `runGrain`'s cold
  blocks close conditional branches back into the function, and searching
  those as loops never finishes.
- **The tape-seat delay-clamp break** (break 4) is now caught by a scripted
  check. CB11 met it only by chance, and the new row count changed its
  random sequence.

---

# Part B — `grn` as built (S091 P3, accepted 2026-11-04)

## B1. The signal path

```
input L/R ─► bit crush (bof/biv) ─► dry h ──────────────────────────┐
                                     │                               │
                                     ▼ mono ½(hL+hR), 8-bit          │
                         ┌──── RECORD LINE (½ share) ◄── frz stops writing
                         │           │
                         │   grain A ─┤ two grains read it, pitched,
                         │   grain B ─┘ windowed, levelled, panned
                         │           │
                         │           ▼ stereo grains ────────────┐
                         │           │ mono sum                   │
                         │           ▼                            │
                         │   DELAY LINE (½ share) ◄─ feedback ─┐  │
                         │           │  tap exactly R behind    │  │
                         │           └─► DC block, knee, fbk ───┘  │
                         │           │ tap (mono, both sides)      │
                         │           ▼                             ▼
                         │      wet = grains + tap ──► MIX ◄─ dry h
                         │                              │
                         └──────────────────────────────▼
                                         output filter (typ/frq/res) ─► out
```

1. **Shared stage 1** (as every sub-type): the 8-bit conversion with the
   data-line masks (`bof`, `biv`) and AC coupling give the dry signal `h`.
2. **The share is split 50/50** into two lines of equal length: a
   **record line** and a **delay line**, one byte (8-bit) per sample. With
   the whole arena, each line is 63,328 samples = **1.436 s**.
3. **The record line** is written every frame with the mono dry signal
   ½(hL + hR), so it holds the recent input (after the bit crush).
4. **Two grains**, A and B, read the record line:
   - each at its own pitch;
   - in its own direction;
   - with a triangular window, a level and a pan.

   Together they form a stereo **grain signal**.
5. **The delay line** is fed with the grains' mono sum plus its own
   feedback, and is read exactly **R** samples behind the write head.
   - It has no glide and no interpolation: a change of R jumps.
   - The feedback passes the same return chain as the tape delay (DC
     blocker, gain `fbk`, cubic soft knee), with the tone stage off.
6. **Wet** = stereo grains + the delay tap. The tap is mono, the same on
   both sides.
7. **Shared stage 3, the MIX stage:** out = h + `mix`·(wet − h).
8. **Shared stage 4:** the output filter (`typ`, `frq`, `res`).

So the delay line **echoes the grains, not the input**. With `fbk` 0 you
hear the grains plus one echo of them R later. With `fbk` up, the grains
repeat and pile up every R.

---

## B2. Time: R (the `div` control)

- **R is the one time constant of the engine.** It is three things at
  once:
  - the delay line's delay;
  - how far back grains may start (the selection window);
  - the base of the grain size.
- **R is always tempo-synced** (user G1, so `snc` is free for freeze):
  - the free Rate curve (`rte` 0..127: 20 ms .. 2.87 s, exponential);
  - snapped to the nearest step division at the current BPM, the same
    rule as the delay's Sync;
  - limited to what fits one line (≤ 1.436 s with the whole arena);
  - clamped to at least 128 samples.
- **Page:** `rte` is shown as `div`, with the division label (`/16`,
  `16t`, `1br` …).
- **Effective range:** the stored `rte` is clamped (per block) to the
  largest value that still fits a half line (`crumpBit_subtypeRateLimit()`).
  At slow tempos the long divisions do not fit; the knob runs into the
  clamp.

---

## B3. Grains

### B3.1 Why two grains

- **Each restart jumps** to a new place in the record line. A hard jump
  clicks, so each grain fades in and out with its triangle window, at 0 at
  the jump.
- **With one grain**, those fades would chop the sound into a pulse.
- **With two grains half a grain apart**, one is at full level while the
  other jumps. The two triangles sum to exactly 1, so the stream is
  continuous: the classic two-head rotating-tape trick, also used by `pit`.
- **Two is the fewest grains that gives a gap-free overlap.** They are also
  two independent voices: each has its own start, pitch, level and pan.
- **The limit:** never more than two at once. A third grain was estimated
  at ≈ +17 per frame (decision S5).

### B3.2 Clock, size and overlap

- **Two grains, half a grain apart.**
  - Grain A restarts every **G** samples.
  - Grain B restarts G/2 later.
  - So a new grain starts every G/2.
- **Grain size G** = R × 2^(−5·(1 − d/64)), where d = |`sze` − 63| (0..64).
  - It runs from **R/32** (`sze` at the centre) to **R** (`sze` fully left
    or right), in equal ratio steps: 64 steps per side over 5 octaves of
    size.
  - The minimum is 64 samples (1.45 ms).
  - Example at 120 BPM with R = ¼ note (0.5 s): G runs from 15.6 ms to
    0.5 s.
- **G changes immediately** (each block). A grain's position, pitch,
  level and pan change only when it restarts.

### B3.3 Window and density (`den`)

- **Window:** each grain has a triangular window that rises from 0 to 1
  and back to 0 over its **active length**:
  - active length = G × (0.25 + 0.75·`den`/127);
  - after the active length, the grain is silent until it restarts.
- **At `den` 127** (default) the active length is the whole grain. The two
  triangles, half a grain apart, sum to exactly 1, giving a continuous
  overlap-add stream.
- **Lower `den`** shortens each grain's sounding part:
  - down to 25 % of G at `den` 0;
  - the two grains then cover only half of the time, which opens gaps;
  - the result is sparser and more "pointillist".
- **The restart rate does not change with `den`**: it is still every G/2.
  Only the duty changes.

### B3.4 Where a grain starts: `pos` (the `dpn` row)

Each grain picks a start point behind the record head when it restarts.
The start lies within the window R, and only where samples have been
written (`valid`). `pos` is bipolar around 63:

| `pos` | Mode | Rule |
|---|---|---|
| 63 (centre) | **Follow** | start just behind the record head; grains trail the input closely (a smeared, stuttering delay) |
| 64..127 (right) | **Spray** | start uniformly at random within a share of the window that grows from 0 to the whole R at 127: the classic cloud |
| 0..62 (left) | **Slice** | start on a random grain-sized slice boundary among the most recent slices. The number of candidate slices grows from 0 to R/G at 0: rhythmic re-ordering of recent material |

**Headroom rule:** a forward grain pitched up (ratio r > 1) walks faster
than the record head. It therefore starts at least G·(r − 1) + 2 samples
back, so it never overtakes the head within its life.
- In follow mode this means pitched-up grains start further back.
- At large G and high r, the needed lag can exceed R (the grain reads older
  material) or the line (the grain then starts at the oldest sample).

A reversed grain walks backwards, so its start is pulled at least G·r
inside the window.

### B3.5 Direction and pitch

- **Direction:** the side of `sze`. Left of 63 = reversed, 63 and right =
  forward.
- **Reads are drop-sample** (no interpolation, user G6): 16.16 positions,
  stepped by the ratio. This is deliberately gritty.
- **Pitch set:**
  - `st1` (the `aux` row, 15) is the **root**;
  - `st2` and `st3` are intervals **relative to the root**;
  - all are in whole semitones, 63 = 0;
  - only ±24 semitones are used. Values beyond are clamped (the page shows
    the clamped value).
  - A grain's pitch is root, root + st2, or root + st3, chosen by `spr`.
    The sum is clamped to ±24 semitones (ratio ¼ .. 4).

### B3.6 Spread (`spr`): which pitch, plus detune

At each restart a grain draws its pitch slot with weights **1 : a : b**:

| `spr` | root | `st2` | `st3` | Detune |
|---:|---:|---:|---:|---|
| 0 | 100 % | 0 | 0 | none |
| 20 | 67 % | 33 % | 0 | none |
| 40 | 50 % | 50 % | 0 | none |
| 60 | 40 % | 40 % | 20 % | none |
| 80 | ⅓ | ⅓ | ⅓ | none |
| 81..127 | ⅓ | ⅓ | ⅓ | random ± up to 35 cents at 127, growing linearly from 80 (user G8) |

- a = min(`spr`, 40)/40.
- b = clamp((`spr` − 40)/40, 0, 1).

### B3.7 Level jitter (`amp`) and random pan (`wid`)

- **`amp`:** each new grain's level is 1 − (`amp`/127)·u, with u random in
  0..1.
  - 0 = every grain at full level (default).
  - 127 = levels uniformly 0..1.
- **`wid`:** each grain's pan blends from a **fixed lean** towards a fully
  random pan.
  - The lean is A at 25 % (left of centre) and B at 75 % (right).
  - 0 = the fixed lean (default), so grains alternate slightly left and
    right even at 0.
  - 127 = a uniform random pan per grain.
- **Pan law:** linear (L = level·(1 − pan), R = level·pan), not constant
  power.

### B3.8 Randomness

- **One hardware RNG word per block**, expanded by an integer hash into the
  five random numbers a new grain needs:
  - position;
  - pitch slot;
  - detune;
  - level;
  - pan.
- **One candidate per block** is drawn for whichever grain restarts next.
  It is used only if that restart falls inside the block; otherwise the
  next block draws again.

---

## B4. Freeze (`frz`, the `snc` row)

- **While `frz` is on, the record line is not written.** The frame's code
  is redirected to the delay line's slot, which the delay write then
  overwrites.
- **The grains keep reading the frozen material:**
  - positions wrap around the whole record line (the full half share, not
    only R);
  - the delay line keeps running, so echoes and `fbk` still evolve over a
    frozen source.
- **The record head keeps advancing while frozen.** Start points are still
  chosen relative to it, so:
  - in **follow** mode, frozen grains sweep forward through the frozen
    loop at the head's speed;
  - in **spray** and **slice** modes they wander within the window behind
    the moving head.
- **Turning freeze off** resumes recording at the head's current position.

---

## B5. Silence after entering `grn` (the gate)

- **The wet grains and the delay tap stay muted** until enough of the line
  is written for every possible read to land on recorded samples:
  - recorded ≥ R + 4·G + one block;
  - or the whole line, if that is less.
- **With long grains** (`sze` near an extreme, G ≈ R) this can mean the
  whole line first: up to **1.44 s of silence** after switching to `grn`
  or after a share change.
- **On a sub-type switch the buffer is reinterpreted, not cleared.** The
  old bytes play in the new format, but the gate still waits for fresh
  writes before it opens.

---

## B6. The controls on the page (`sub` = `grn`)

Names are the page names in `grn` (the `name3` hook); the row underneath
keeps its key (and, on other screens, its delay name).

### SELECT 2 (the base screens, shared layout)

| Screen | Cells |
|---|---|
| 0 (overlay, home) | `sze` `fbk` `div` `mrp` |
| 1 | `sze` `fbk` `div` `sub` |
| 2 | `frz` `pos` `mix` `typ` |
| 3 | `frq` `res` – – |

### SELECT 3 (`grn` only)

| Screen | Cells |
|---|---|
| 0 | `st1` `st2` `st3` `spr` |
| 1 | `den` `amp` `wid` – |

The SELECT 3 button itself still toggles data line 2 (CrumpBit owns every
SELECT button). The screens are reached by the encoder after SELECT 2's
last screen.

### Every control in `grn`

| Page | Row (key) | Range / default | Value shown | What it does in `grn` |
|---|---|---|---|---|
| `sze` | 11 `crump_tone` | 0..127, 63 | −63..64 | grain size and direction. Centre = smallest grains (R/32); outwards = longer, up to R at the ends; left = reversed. (The delay's tone stage is off in `grn`.) |
| `fbk` | 6 `crump_feedback` | 0..127, 64 | number | the delay line's feedback: linear to 1.0 at 100, up to 1.2 at 127, soft-kneed (as the tape delay) |
| `div` | 7 `crump_rate` | 0..127, 64; clamped to fit a half line | division | R: the delay time, the grain window and the grain-size base; always tempo-synced |
| `frz` | 9 `crump_sync` | off/on, off | on/off | freeze the record line (§B4) |
| `pos` | 10 `crump_dly_pan` | 0..127, 63 | −63..64 | grain start mode: left slice, centre follow, right spray (§B3.4) |
| `st1` | 15 `crump_aux` | 0..127, 63 | semitones (±24) | root pitch of every grain |
| `st2` | 16 `crump_grain_st2` | 0..127, 63 | semitones (±24) | second pitch, relative to the root |
| `st3` | 17 `crump_grain_st3` | 0..127, 63 | semitones (±24) | third pitch, relative to the root |
| `spr` | 18 `crump_grain_spread` | 0..127, 0 | number | how many grains use `st2`/`st3`; detune above 80 (§B3.6) |
| `den` | 19 `crump_grain_density` | 0..127, 127 | number | the sounding share of each grain, 25..100 % (§B3.3) |
| `amp` | 20 `crump_grain_amp` | 0..127, 0 | number | random level per grain (§B3.7) |
| `wid` | 21 `crump_grain_width` | 0..127, 0 | number | random pan per grain (§B3.7) |
| `mix` | 5 `crump_mix` | 0..127, 0 | number | dry ↔ wet (the shared MIX stage; the same in every sub-type) |
| `bof` / `biv` | 3 / 4 | bit masks | overlay | bit crush before recording (shared) |
| `typ` `frq` `res` | 12–14 | as P2 | as P2 | output filter after the mix (shared) |
| `sub` | 8 `crump_subtype` | 0..4, 0 | `dly cmb pit grn rvb` | the sub-type; Pattern-automatable, never on a lane |

(Descriptor rows, `CrumpBitParameters.h`: 3 `bof`, 4 `biv`, 5 `mix`, 6
`fbk`, 7 `rte`, 8 `sub`, 9 `snc`, 10 `dpn`, 11 `ton`, 12 `typ`, 13 `frq`,
14 `res`, 15 `aux`, 16–21 the grain rows.)

### Automation

- **FX sequencer lanes in `grn`:**
  - shared: 0 `mrp`, 1 `bof`, 2 `biv`, 3 `mix`, 7 vol, 8 pan, 11 `typ`,
    12 `frq`, 13 `res`;
  - pool: 4 `st2`, 5 `st3`, 6 `frz`, 9 `pos`, 10 `sze`, 14 `st1`, 15
    `spr`.
  - `fbk`, `div`, `den`, `amp` and `wid` have **no lane** in `grn` (user
    L2).
  - With the target model, the pool assignment should be revisited:
    Density, Length and Position are the main performance controls.
- **Morph, LFO and Pattern** reach rows 15–21 like the delay rows. `sub`
  is Pattern-only.
- **Lanes follow the Scene's stored `sub`.** A Pattern switch to `grn`
  changes the engine, but the lanes keep the Scene's map.

---

## B7. Rough edges of the as-built engine

The right-hand column says what the target model (Part A) does with each.

| # | Rough edge | Under the target model |
|---|---|---|
| 1 | `sze` centre = smallest grains, and the direction flips there: a small move around 63 switches between the tiniest forward and reversed grains | still true for Length's centre; Q1 decides the shortest grain. A small dead zone at the centre is an option |
| 2 | `sze`, `pos` and the pitch rows show −63..64, but pitch uses only ±24 | presentation task: scale ±24 over the full travel |
| 3 | `spr`, `den`, `amp`, `wid` show raw 0..127 | presentation task: divisions, percent, `1`/`1+2`/`123`/`±1st` text |
| 4 | `div` jumps (no glide), and the delay tap is mono | the delay gets its own time; a stereo tap stays optional (a second read) |
| 5 | the delay always adds one echo of the grains | unchanged (Q9); `fbk` 0 still gives one echo at the delay time. A tap level would need a row |
| 6 | up to 1.44 s of startup silence | resolved: the per-grain gate (§A2) |
| 7 | freeze positions follow the moving head | resolved by the freeze anchor (§A3.3) |
| 8 | slice and spray scale with R/G | resolved: Position no longer depends on R or G |
| 9 | linear pan; the ±25 % lean means `wid` 0 is not mono | resolved: Width 0 = mono; constant power recommended |
| 10 | `den` changes the duty, not the grain rate | resolved: Density is the trigger rate |
| 11 | one candidate per block | kept: one trigger per block (minimum interval 32 frames) |
| 12 | pitch changes are heard only at a grain's restart | inherent; with the copy-style long grains a pitch change waits up to one grain. Short grains or higher density respond faster |

**Costs today, for planning:**
- `grn`'s frame loop is 131 instructions; the whole Effect is ≈ 192 per
  frame worst case.
- The `grn` state is 32 B, the size of the union.
- Per-block work (the candidate, the window step, the gate) is cheap.
  Per-frame additions cost directly.
