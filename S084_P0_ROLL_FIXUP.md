# S084 P0 — Roll Fix-up: Assessment and Plan

**Session:** 084, phase P0 (assessment and plan; no code changed)
**Branch:** `dev-ph7-voice_expansion` (code tree identical to `59406a4`, the
base the findings were read from)
**Date:** 2026-10-24; status revised 2026-10-26
**Status:** **IMPLEMENTED (2026-10-26); hardware checks pending.** User
decisions Q2, Q4 and Q7 are recorded (2026-10-24, §5). The other questions
proceed on the recommendation. The change schedule is
`S084_P0_IMPLEMENTATION.md`; its §10 log carries the per-phase build and
host-check results. It also carries the user-approved assessment fixes
A1–A4 from an earlier build on the abandoned spike
(`../Helicase-dev-ph7-spike/`, reference only; schedule §11). The §8 P0
specification and `MEMORY.md` updates are applied; hardware checks and the
session handoff log remain with the user (schedule §9).
**Scope:** rolls triggered from the PERF BAR overlay (panel VOICE and MIDI
notes), step rolls (STEP special `rol`), and how LiveRecord writes a played
roll into the Pattern.

**User statement of the problem:**

1. The step roll list and the PERF roll list do not match. PERF should offer
   every step roll rate, plus its extra types (single velocities, flams).
2. A recorded roll does not play back at the rate it was played.
3. Roll rate should follow the master clock and must not depend on the
   track's step scale.

All findings below come from reading the code. None has been measured on
hardware yet. The worked traces for F2 follow the code line by line.

---

## 1. Summary

| # | Finding | Severity | Explains |
|---|---|---|---|
| F1 | PERF offers 6 roll rates; STEP offers 9. `8tr`, `16t`, `32t` cannot be played live. | Product gap | Problem 1 |
| F2 | A roll carried into the next step drops its boundary hit and starts one roll period late. Each carried step plays `slots − 1` hits. Every recorded roll longer than one step is stored as carried steps. | **Defect, high** | Problems 2 and 3 |
| F3 | A recorded roll's last step has a different fill, so it does not carry. It restarts the roll at the step boundary, which moves the phase and resets the velocity rhythm. | Defect, medium | Problem 2 |
| F4 | PERF and MIDI overlay rolls use a 1 ms foreground timer derived from the menu `PAR_BPM` value, with integer truncation. The rate is not the master clock. It runs about 2–5 % fast and ignores external sync entirely. | **Defect, high** | Problems 2 and 3 |
| F5 | LiveRecord keeps a single open roll. Concurrent overlay rolls (allowed since S083 Q5) corrupt it: hits are counted against the wrong track, a release ends another track's roll, and a live hold can stick. | Defect, medium | Problem 2 (multi-track) |
| F6 | The slow/fast split records slow rolls hit by hit. With `qnt` on, each hit is quantised separately, so a roll rate that is not on the quantise grid (e.g. `8tr` once added) is not preserved. | Latent, becomes real with F1 | Problem 2 |
| F7 | The `pv<`/`pv>` velocity rhythm plays live but is not recorded (rhythm is cleared); the recorded roll plays at a flat velocity of 100. | Already an open product decision | "Sounds different" |
| F8 | A take boundary (REC off, `rtk` change, …) does not end an open recorded roll; its release still writes the end step. Found while scheduling. | Defect, low | — |

The roll **rate** of the step engine is already absolute: `seq_rollPpq()`
returns 96-PPQ master ticks, and `seq_processRollTicks()` runs on the master
TIM3 tick. Step scale enters through the per-step **hit count** (`slots`),
and F2 is where that count breaks the rate. The fix is to make a roll's
length a **time window** on the master clock, not a count of hits, and to
run PERF/MIDI rolls on the same engine.

---

## 2. How rolls work today

### 2.1 Step rolls (TIM3)

- Storage: the step special `roll` (index 0..9) plus `rhythm` and `fill`
  (`PatternData.h`).
- `seq_rollPpq()` — [sequencer.c:2678](Core/Sequencer/sequencer.c#L2678):
  `{0, 48, 32, 24, 16, 12, 8, 6, 4, 3}` = off, 1/8, 8T, 16, 16T, 32, 32T,
  64, 64T, 128. Names: `stepRollRateNames`,
  [MenuText.h:297](Core/Menu/MenuText.h#L297).
- `seq_rollBegin()` — [sequencer.c:2787](Core/Sequencer/sequencer.c#L2787),
  called at the step's (possibly deferred) fire:
  - window = `stepScale_ticksQ8(effective scale) >> 8` × fill / 100;
  - `slots = ceil(window / ppq)` (capped at 255);
  - no carry: fire hit #1, `accum = ppq`, `remaining = slots − 1`;
  - carry (`seq_rollCarries()`: active and the same rate, rhythm **and**
    fill): `remaining += slots − 1`, with no hit and a reset-only call.
- `seq_processRollTicks()` — [sequencer.c:5902](Core/Sequencer/sequencer.c#L5902),
  every master tick after the DDA. It skips a track whose `remaining == 0`
  **before** decrementing `accum`.

### 2.2 PERF / MIDI overlay rolls (foreground)

- Types: `perfBtnTypeNames`
  ([MenuText.h:310](Core/Menu/MenuText.h#L310)): `vmx v10 v64 v32 flm fls`
  `r/8 r16 r32 r64 r6t 128` (0..11; clamp at
  [menu.c:6484](Core/Menu/menu.c#L6484); `settings.cfg perf_btn_lt/rt`).
- `buttonHandler_overlayStart()` —
  [buttonHandler.c:949](Core/Hardware/frontPanel/buttonHandler.c#L949):
  rolls use `rollDiv = {3,4,5,6,6,7}` with `rollIdx = {1,3,5,7,8,9}`.
  The interval in ms is `buttonHandler_overlayDivisionMs()` from
  `15000 / PAR_BPM`, times 2/3 for `r6t`.
- `buttonHandler_overlayTick()` —
  [buttonHandler.c:1211](Core/Hardware/frontPanel/buttonHandler.c#L1211),
  from `buttonHandler_tick()`. It fires due repeats on `time_sysTick`
  (TIM6, 1 ms) and reports each hit to LiveRecord.
- MIDI notes under an overlay go through the same code
  (`buttonHandler_overlayMidiNoteOn()`), so every finding here applies to
  panel and MIDI alike.
- The same `rollDiv`/`rollIdx` tables are copied in
  `buttonHandler_overlayReconfigure()`
  ([buttonHandler.c:1126](Core/Hardware/frontPanel/buttonHandler.c#L1126)).

### 2.3 Recording (LiveRecord, foreground)

- `liveRec_triggerIn()` —
  [LiveRecord.c:1370](Core/Sequencer/LiveRecord.c#L1370):
  - a roll is classed **slow** when `ppq ≥ step interval` and recorded hit
    by hit as single steps;
  - otherwise it is **fast**: the start step gets the roll (fill 100) and
    the track is live-held. Each later ring entry writes the step just
    ended as a span (timing 0, same rate, fill 100). The release writes the
    end step with fill = release offset (`liveRec_endRoll()`,
    [LiveRecord.c:1200](Core/Sequencer/LiveRecord.c#L1200)).
- `liveRec_rollPpqTicks()` duplicates `seq_rollPpq()`
  ([LiveRecord.c:285](Core/Sequencer/LiveRecord.c#L285)).

So a recorded fast roll held for N steps is stored as N adjacent roll steps,
which play back through the **carry** path.

---

## 3. Findings

### F1 — PERF roll list is a subset of the step list

| Step index | STEP label | ticks | PERF type today |
|---:|---|---:|---|
| 1 | `1/8` | 48 | `r/8` (6) |
| 2 | `8tr` | 32 | — |
| 3 | `16` | 24 | `r16` (7) |
| 4 | `16t` | 16 | — |
| 5 | `32` | 12 | `r32` (8) |
| 6 | `32t` | 8 | — |
| 7 | `64` | 6 | `r64` (9) |
| 8 | `64t` | 4 | `r6t` (10) |
| 9 | `128` | 3 | `128` (11) |

The two lists also use different labels for the same rate (`r6t` vs `64t`,
`r/8` vs `1/8`). The mapping is hand-maintained in three places: two copies
in buttonHandler and the spec table in `PATTERN_DYNAMIC_STACK.md` §6.7.4.

### F2 — Carry loses the boundary hit (root cause of the rate change)

When a chain's `remaining` reaches 0, `accum` is left at `ppq − 1` and then
**frozen**, because the `remaining == 0` test comes before the decrement. A
carry at the next boundary adds `slots − 1` hits (it counts the step's
own first hit, which a carry never fires). The roll resumes from the frozen
`accum`.

Trace: track at 1/16 (24 ticks), roll `32` (12 ticks), fill 100, two
adjacent roll steps (the shape LiveRecord writes):

| Tick | Expected hit | Actual |
|---:|---|---|
| 0 | hit | hit (step A, slots 2, remaining 1) |
| 12 | hit | hit (remaining 0, accum 11 frozen) |
| 24 | hit | — (carry: remaining 1; accum 11 → 10) |
| 35 | — | **hit** (late) |
| 36 | hit | — |
| 48 | hit | — (carry: remaining 1) |
| 59 | — | **hit** |

After the first step the roll plays **one hit per step**, offset by 11
ticks: it has turned into a step-rate pulse. Roll `64` (6 ticks) at 1/16
plays `0 6 12 18 | 29 35 41 | 53 59 65 | …`: 3 hits per step instead of 4,
with a gap at every boundary. The slower the step scale, the smaller the
relative loss (1/8 step: 7 of 8 hits). That is why the recorded rate
appears to "vary with the track's step scale".

The same defect affects step rolls typed into adjacent STEP steps with
identical rate, rhythm and fill. A single-step roll (no carry) plays
correctly, which is why the step engine looks right in isolation.

Secondary issues of the hit-count model:

- `slots` uses the truncated integer step interval, and a step whose
  interval is not a multiple of `ppq` cannot be represented (`16t` in a
  24-tick step = 1.5 hits per step). The count model is wrong for any rate
  that does not divide the step.
- (Corrected in the implementation pass: the step-scale LUT tops out below
  256 ticks, so `slots` never reaches its 255 cap. It is not a defect.)
- A delayed start step's window runs past the boundary, and the carry then
  adds the next step's count on top, so the counts overlap.

### F3 — The end step of a recorded roll restarts the roll

`liveRec_endRoll()` writes the release step with fill < 100. Because
`seq_rollCarries()` requires an equal fill, that step is a "different roll"
and `seq_rollBegin()` fires a fresh hit #1 at the boundary. It resets
`accum` and the velocity rhythm position. For an unquantised roll start
(start delay d ≠ 0), the phase jumps from `d + k·ppq` to the boundary on the
last step. That gives an audible hiccup or double hit, and with F2 a
sudden return to the full rate on the last step.

### F4 — PERF/MIDI roll rate is not the master clock

`buttonHandler_overlayDivisionMs()` uses `15000 / PAR_BPM` (integer ms),
then integer division:

| BPM | `r/8`..`128` exact (ms) | used (ms) | error |
|---:|---|---|---|
| 120 | 128th = 15.625 | 15 | +4.2 % fast |
| 120 | 64T = 20.83 | 20 | +4.2 % fast |
| 137 | 16th = 109.49 | 109 | +0.4 % |
| 137 | 128th = 13.69 | 13 | +5.3 % fast |
| 175 | 128th = 10.71 | 10 | +7.1 % fast |

On top of the truncation:

- **External sync:** `PAR_BPM` is the internal tempo setting. Under `usb`,
  `din`, `pls` or `aut` sync, the overlay roll runs at the menu tempo, not
  the incoming clock. The recorded roll then plays at the real clock, so
  the two rates can differ by any amount.
- **Jitter:** the repeat is serviced from the foreground
  `buttonHandler_tick()` on a 1 ms grid, so render load delays hits.
- **Phase:** hits are spaced from the press time in ms and drift against
  the master grid. Step rolls are spaced in master ticks.

A live roll and its recording are therefore produced by two different
clocks. Even with F2 fixed they will not match unless the overlay rolls move
onto the master tick.

The flam main hit (`fls`/`flm`, 1/32 later) has the same ms/`PAR_BPM`
dependency.

### F5 — Concurrent overlay rolls break LiveRecord's single roll slot

S083 Q5 made overlay repeats per track (`bh_overlay[7]`), but LiveRecord
still has one `liveRec_roll*` slot:

- `liveRec_triggerIn()`: a `ROLL_HIT` from **any** track while a roll is
  held only increments `liveRec_rollHits`, without checking the track. A
  second track's hits are never recorded, and they inflate the first
  track's hit count.
- A `ROLL_START` on a second track overwrites the slot without ending the
  first. The first track's `seq_setLiveHold(…, 1)` is never cleared, so its
  stored triggers stay suppressed.
- `liveRec_rollRelease(track)` ignores `track`
  ([LiveRecord.c:1474](Core/Sequencer/LiveRecord.c#L1474)): releasing any
  track's roll ends the held roll.

### F6 — Slow rolls recorded hit by hit lose their rate under `qnt`

The slow path quantises each hit independently (`seq_quantiseTick()` on the
master grid). Today every PERF slow rate (1/8, 16) lies on the 1/8–1/64
grids, so the loss is limited. Once F1 adds `8tr`/`16t`, a triplet roll
recorded with `qnt 16` would be snapped to straight sixteenths. The
threshold (`ppq ≥ step interval`) also depends on the step scale: the same
gesture is recorded as hits or as a span depending on the track's `scl`.

### F7 — `pv` rhythm not recorded

This is already listed as an open product decision in `MEMORY.md`. It is
included here because the user will hear it as "the recorded roll is not
the same". The encoding is identical (the step `rtm` byte), so recording it
is a small change once F3 is fixed.

### F8 — A take boundary leaves the recorded roll open

`PATTERN_DYNAMIC_STACK.md` §6.7.4 says a Scene change, stop, REC off or
`rtk` change ends an open roll without an end write. Only the playhead-break
path does this (`liveRec_syncPosition()` → `liveRec_endRoll(0)`).
`liveRec_newTake()`, which REC off, overdub, erase and `rtk` changes all
call, leaves the roll held. The later release then runs
`liveRec_endRoll(1)`, which checks only that the transport is running, so it
writes the end step after recording has stopped.

---

## 4. Plan

The design principle: **one roll engine on the master 96-PPQ tick, for
step rolls and for live (PERF/MIDI) rolls.** A roll's rate is a fixed tick
interval. Its length is a time window in ticks. Step scale decides only how
long a step's window is, never the spacing of hits.

### P1 — Step roll engine: time window instead of hit count (fixes F2, F3)

1. Replace `seq_rollRemaining[]` (hits) with `seq_rollWindow[]` (uint16
   ticks left in the window).
2. `seq_rollBegin()`:
   - window = the step interval × fill / 100, rounded from the Q8 interval;
   - no carry: fire hit #1, `accum = ppq`, window as computed;
   - carry: **no hit, `accum` untouched**, window = this step's window from
     now (replace, not add: the new step's trigger owns the roll from here,
     per F-R).
3. `seq_processRollTicks()`: while the window is open, decrement `accum` and
   the window every tick, and fire when `accum` reaches 0. When the window
   closes with fill < 100, request the duck. The duck countdown arrays
   (`seq_rollDuckDelay/Delta/Active`) fold into the window; ducking at the
   window end is the same rule.
4. **Carry rule (user, Q4, 2026-10-24):** a step carries the running roll
   when its rate and rhythm match, **unless the previous roll step's fill
   was below 100 %**. The new step's own fill does not have to match; it
   only sets that step's window. This fixes the end step of a recorded roll
   (F3).
5. A fill-100 roll whose window closes stays **parked** until the track's
   next step event. In that state `accum` keeps counting, down to 0, and
   no hit fires. This absorbs the ±1 tick jitter of fractional step
   intervals. A carrying step whose trigger is deferred (shuffle or
   micro-timing) extends the open window to its deferred fire, so no hit is
   lost in the gap. If the next step does not carry, the parked roll ends.
6. `seq_rollPpq()` stays private. LiveRecord's copy is deleted, because Q7
   removes the slow/fast classification that needed it.

Result: a roll's hits sit at `start + k·ppq` across any number of steps, at
any step scale, until a window closes or a trigger cuts it.

### P2 — PERF/MIDI overlay rolls on the master tick (fixes F4)

1. Add a per-track **live roll** state in the Sequencer: rate index, `accum`
   and rhythm position. It is serviced from `seq_processSchedulerTick()`
   **before** the `!seq_running` early return. That covers internal sync
   (running or stopped) and external sync while running. Under external sync
   while stopped, `seq_tick()` services it from its tempo timer, which
   follows `seq_tempo`; the incoming clock keeps `seq_tempo` updated. It
   fires through `seq_triggerVoice(…, reset 0)` from TIM3, as step rolls
   do.
2. API used by buttonHandler, foreground → TIM3 (single-byte writes, or a
   short critical section):
   - `seq_liveRollStart(track, rateIdx, rhythm)`: the first hit stays in the
     foreground (reset 1, reported as `ROLL_START` as today);
   - `seq_liveRollSetRate(track, rateIdx)` for a BAR or CC2 switch;
   - `seq_liveRollStop(track)`.
3. Hit reporting to LiveRecord: LiveRecord never runs in TIM3 (binding). TIM3
   posts each live hit as (track, velocity, TIM2 µs stamp) into a small
   per-track mailbox or ring. `buttonHandler_tick()` drains it and calls
   `liveRec_triggerIn()` with `ROLL_HIT` and `age_us` from the stamp (the S083
   Q6 arrival-age path), so placement is exact.
4. `bh_overlay[]` keeps note, the held MIDI note and the flam state. The
   `interval`/`next` roll fields and both `rollDiv`/`rollIdx` tables go.
   Recommended: move the flam main hit onto the tick (12 ticks) too, so
   `buttonHandler_overlayDivisionMs()` can be retired (decision Q6).
5. Phase: retriggers are spaced in master ticks from the press. See
   decision Q3 for grid-locked hits instead.

### P3 — Unify the roll lists (fixes F1)

1. PERF types become `vmx v10 v64 v32 flm fls` + the 9 step rates =
   **15 entries**, type `6 + (stepIdx − 1)`; the step index is
   `type − 5`, with no lookup table.
2. `perfBtnTypeNames` reuses the step labels (decision Q1). Count 15, clamp
   14 in `menu.c`, renderer count in `menu_nameTable3(perfBtnTypeNames, …)`.
3. `settings.cfg perf_btn_lt/rt` range becomes 0..14. **No migration
   (user, Q2, 2026-10-24):** old saved settings and Patterns are not
   preserved.
4. Specs: `MIDI_SPEC.md` (the `pb<`/`pb>` row says "12 types … 6 rolls"),
   `PATTERN_DYNAMIC_STACK.md` §6.7.4 rolls line, and the
   `FILESYSTEM_SPEC.md` range.

### P4 — LiveRecord roll recording (fixes F5, F6; optionally F7)

1. Per-track roll slots: `liveRec_roll*` becomes an array over
   `NUM_TRACKS`, or over a small number of concurrent slots if RAM is
   tight (decision Q5). Each hit and release is matched by track. A
   `ROLL_START` on a track that already holds a roll first ends that roll.
2. `liveRec_service()` span writes and `liveRec_endRoll()` are applied per
   held slot.
3. **Every held roll is recorded as a span (user, Q7, 2026-10-24: "yes, if
   the button or MIDI input note is held"),** whatever its speed relative
   to the step. A panel VOICE or a MIDI note under a roll overlay is held by
   definition, and the span ends at its release. That removes the
   step-scale-dependent slow/fast split and the per-hit quantisation (F6).
   The start hit is still placed and quantised like any hit. Flams keep
   F-L.
4. Optional (decision Q8): write the active `pv<`/`pv>` rhythm into the
   start and span steps' `rtm` (F7), and record the roll velocity as
   `BH_OVERLAY_VEL` (as today).
5. A take boundary (REC off, overdub in/out, erase in, `rtk` change, Scene
   change) ends every open roll without an end write, as §6.7.4 already
   specifies. Today `liveRec_newTake()` does not end the roll, so a release
   after REC off still writes the end step.

### Order and risk

P1 first: it is self-contained in `sequencer.c`, fixes the audible defect
for step rolls and recordings, and can be checked on hardware alone. P2 and
P3 together come next (they touch the same tables). P4 comes last, since it
depends on P1's carry rule and P2's TIM3 hit reports.

Binding rules to keep: the step reset rule (roll hit #1 and each carried
step's renewal reset; retriggers 2+ never do); F-R (only the next step's
trigger cuts a roll); LiveRecord never in TIM3; the TIM3 order in
`seq_processSchedulerTick()` (rolls after the DDA).

### RAM (approved by the user, 2026-10-26)

| Change | Region | Bytes |
|---|---|---:|
| P1: `seq_rollRemaining[7]` u8 → `seq_rollWindow[7]` u16 | SRAM1 static | +7 |
| P1: duck arrays folded into the window (3 × 7 u8) | SRAM1 static | −21 |
| P2: live engine state, 6 × 7 u8 + rhythm u8 | SRAM1 static | +43 |
| P2: TIM3 → foreground hit mailbox, 7 × (count u8 + stamp u32) + flam mask u8 | SRAM1 static | +36 |
| P2: `bh_overlay[]` loses `pos`, `interval`, `next`, gains `rate` (7 × 4) | SRAM1 static | −28 |
| P4: per-track roll slots, 7 × 8 B, minus the 11 B of scalars today | SRAM1 static | +45 |
| P4: `pat_record_spec_t` + `rhythm` (4 staging slots) | SRAM1 static | +4 |
| A1: `seq_extTicksOwed` (external clock interpolation) | SRAM1 static | +1 |
| **Net** | | **+87** |

Itemised per change in `S084_P0_IMPLEMENTATION.md` §2. The releases listed
do not authorise other reuse.

### Assessment fixes (approved 2026-10-24)

A review of the first (spike) build added four fixes. They are scheduled
as ordinary entries in `S084_P0_IMPLEMENTATION.md` (§1, §11.3):

- **A1:** interpolate external clock ticks, so rolls, shuffle and
  micro-timing are even under MIDI clock and pulse sync (P2, §5.5).
- **A2:** a slow roll released on a later step before its second hit
  keeps the normal end write (P4-12).
- **A3:** hits drained during an overlay switch keep the old overlay's
  rhythm (P2-21/22).
- **A4:** comment nits.

---

## 5. Decisions for the user

| # | Question | Recommendation | Status |
|---|---|---|---|
| Q1 | PERF roll labels: reuse the step labels (`1/8 8tr  16 16t  32 32t  64 64t 128`) or keep an `r` prefix (`r/8 r8t r16 …`)? | Reuse the step labels: one vocabulary, and the `v`/`fl` prefixes already mark the non-roll types. | Proceeding on the recommendation |
| Q2 | Stored `perf_btn_*` values 6..11 change meaning. Migrate or not? | No migration (F-H). | **User: no migration; old settings and Patterns need not survive.** |
| Q3 | Live roll phase: from the press (hits at press + k·ppq), or locked to the master grid of the rate? | From the press: it matches what an un-quantised recording plays back. | Proceeding on the recommendation |
| Q4 | Carry when rate and rhythm match but fill differs? | Yes: fill only sets the window. | **User: yes, unless the first (previous) step's roll length is below 100 %.** |
| Q5 | Concurrent recorded rolls: all 7 tracks, or limit to N slots? | All 7 tracks, to match S083 Q5. | Proceeding on the recommendation |
| Q6 | Move the flam main hit onto the master tick as well? | Yes; it retires the last `PAR_BPM` ms timing. | Proceeding on the recommendation |
| Q7 | Record every roll as a span (retire F-L for rolls)? | Yes. Flams keep F-L. | **User: yes, while the button or MIDI input note is held.** |
| Q8 | Record the `pv` rhythm into `rtm` (the open S083 decision)? | Yes: same encoding, and live then equals the recording. | Proceeding on the recommendation (separable: implementation P4-20..P4-23) |
| Q9 | External sync with no clock arriving while stopped? | Live rolls follow `seq_tempo` from `seq_tick()` while stopped; the clock keeps it updated. With no clock at all they repeat at the last measured tempo. | Proceeding on the recommendation |

---

## 6. Verification plan

Hardware (user), each at 120 BPM internal and again on external MIDI clock
at a different tempo than `PAR_BPM`:

1. **Carry:** two adjacent STEP steps with roll `32` at track `scl` 1/16,
   1/8, 1/32 and a triplet scale. Expect an even roll with no gap at the
   boundary.
2. **Live vs step:** track 1 with a step roll `16t` over 4 steps, track 2
   with the same rate held from PERF. Expect no phasing.
3. **Record:** hold each of the 9 PERF rates for 2+ steps on tracks at 1/16
   and 1/8 `scl`, with `qnt off` and `qnt 16`. Expect playback at the played
   rate, no hiccup on the release step, and the fill ending at the release.
4. **Concurrent:** hold rolls on two tracks while recording, release them
   in either order. Expect both recorded, and no stuck live hold (the
   stored triggers of the other steps play after release).
5. **MIDI:** repeat 2 and 3 with MIDI notes under a CC2 overlay.
6. **Stopped transport:** PERF rolls still repeat at the tempo rate.

Host: a scratch-built harness of `seq_rollBegin()`/`seq_processRollTicks()`
(not a new installed utility) prints hit ticks for the F2 trace before and
after P1. It is required by the S084 gate and specified as H1 in
`S084_P0_IMPLEMENTATION.md` §9, which also holds the full hardware matrix
(V1–V7, including the A1–A3 cases).

Docs to update at close-out: `PATTERN_DYNAMIC_STACK.md` §6 (roll engine,
F-R carry rule, §6.7.4 rolls), `MIDI_SPEC.md` (`pb<`/`pb>` list),
`FILESYSTEM_SPEC.md` (`perf_btn_*` range), `MODULE_INTERCHANGE_SPEC.md`
(new `seq_liveRoll*` API, public `seq_rollPpq()`), `STORAGE_SRAM_MANIFEST.md`
(RAM deltas), and `MEMORY.md`.
