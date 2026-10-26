# S084 P0 — Roll Fix-up: Implementation Schedule

**Session:** 084, phase P0 (implementation complete; hardware checks pending;
see §10 log)
**Branch:** `dev-ph6-midi`, base `59406a4` (clean)
**Date:** 2026-10-24
**Assessment and plan:** `S084_P0_ROLL_FIXUP.md` (findings F1–F8, phases P1–P4)

---

## 0. How to use this schedule

- **Line numbers are at `59406a4`.** Within one file, apply the changes from
  the highest line down, or find each change by its **anchor** (the
  function or symbol named in the entry). Earlier edits shift later lines.
- **Actions:** **ADD** (new code), **REMOVE** (delete), **MODIFY** (replace
  in place). A MODIFY of a function replaces its body and its header
  comment.
- **Comment blocks.** Each entry carries the comment block that goes with
  the code, in the house format (What / Why / Inputs / Outputs / Accessors
  / Affiliates, plus Context and RAM where they apply). The block replaces
  whatever comment the change supersedes. The same text is the in-place
  documentation in both the `.c` and the `.h` file. A header prototype takes
  the short contract form shown in its own entry.
- **Code sketches** fix the names, the order of operations and the
  invariants. Formatting and local variable names are free.
- **Tags.** New comments are tagged `S084 P1`…`S084 P4` after the phase that
  introduces them.

### Binding rules this work must keep

- **Step reset rule (S081 §5.8):** a roll's first hit and each carrying
  step's renewal reset; retriggers 2+ never reset.
- **F-R:** only the next step's trigger cuts a roll. A plain hit ends it, a
  carrying roll continues it, and a different roll replaces it. An empty or
  automation-only step does not cut it.
- **LiveRecord never runs in TIM3.** TIM3 publishes; the foreground drains.
- **TIM3 order** in `seq_processSchedulerTick()`: master boundary → chain →
  deferred triggers → looper → DDA → rolls → FX clock. S084 adds **live
  overlay ticks first**, before the running gate (P2-4).
- **RAM Allocation Approval Policy:** §2 must be acknowledged before any
  phase is implemented.

---

## 1. Decisions applied

| # | Decision | Source | Applied in |
|---|---|---|---|
| Q2 | No migration of `perf_btn_*`; old saved settings and Patterns need not survive. | User, 2026-10-24 | P3 (no migration code) |
| Q4 | A step carries the running roll when rate and rhythm match, **unless the previous roll step's fill was below 100 %**. The new step's fill only sets its own window. | User, 2026-10-24 | P1-1, P1-8, P1-10 |
| Q7 | Every **held** roll (panel VOICE or MIDI note) is recorded as a span, at any speed; the slow/fast split is retired for rolls. Flams keep F-L. | User, 2026-10-24 | P4-5, P4-13, P4-14 |
| Q1 | PERF roll labels reuse the STEP labels. | Recommendation | P3-1 |
| Q3 | Live roll hits are spaced from the press, not grid-locked. | Recommendation | P2-3 |
| Q5 | Recorded rolls on all 7 tracks at once. | Recommendation | P4-1 |
| Q6 | The flam main hit moves onto the master tick (12 ticks). | Recommendation | P2-3, P2-13, P2-14 |
| Q8 | The `pv<`/`pv>` rhythm is recorded into the step `rtm`. | Recommendation | P4-20..P4-23 (separable: without them the rhythm is passed along but not stored) |
| Q9 | Live rolls run while stopped; under external sync they follow `seq_tempo`. | Recommendation | P2-4, P2-5 |

The five recommendations are applied unless the user objects. Q8 sits in
its own entries so it can be dropped without touching the rest.

---

## 2. RAM (user acknowledgement required before implementation)

All entries are **SRAM1 `.bss`, static, runtime-only, never persisted**,
and live for the life of the firmware.

| Entry | Owner | Change | Bytes |
|---|---|---|---:|
| P1-1 | `sequencer.c` step roll | `seq_rollRemaining[7]` u8 removed | −7 |
| P1-1 | `sequencer.c` step roll | `seq_rollWindow[7]` u16 added | +14 |
| P1-2 | `sequencer.c` step roll | `seq_rollDuckDelta/Delay/Active[7]` removed | −21 |
| P2-1 | `sequencer.c` live engine | `seq_liveMode/Rate/Accum/Pos/Note/Vel[7]` (6 × 7 u8) | +42 |
| P2-1 | `sequencer.c` live engine | `seq_liveRhythm` u8 | +1 |
| P2-1 | `sequencer.c` live mailbox | `seq_liveHitCount[7]` u8, `seq_liveFlamMask` u8, `seq_liveHitUs[7]` u32 | +36 |
| P2-9 | `buttonHandler.c` | `bh_overlay[7]` 8 B → 4 B per track (`pos`, `interval`, `next` out; `rate` in) | −28 |
| P4-1 | `LiveRecord.c` | `liveRec_roll[7]` × 8 B added | +56 |
| P4-1/2/3 | `LiveRecord.c` | 11 scalar roll bytes removed | −11 |
| P4-20 | `PatternData.h` → `PatternStackService.c` | `pat_record_spec_t` + `rhythm` (4 staging slots) | +4 |
| | | **Net (before alignment padding)** | **+86** |

Stack: `liveRec_trigger_t` +1 B per record (P4-17), and
`pat_record_spec_t` +1 B per local copy. Alignment may add up to 3 B per
array. The link map after each phase gives the exact `.bss` delta, to be
reported against the S083 close (`text=608,744`, `data=424`,
`bss=430,112`). Freed bytes are not reused for anything else.

---

## 3. Order of work and checkpoints

1. **P1** (sequencer.c only). Build `make all && make img`. Hardware
   check V1 (§9).
2. **P2 + P3** together, since both touch the overlay type tables. Build.
   Hardware checks V2, V5, V6.
3. **P4** (LiveRecord, PatternData, MidiNotes). Build. Hardware checks
   V3, V4, V5.
4. **§8** specification and memory updates at close-out.

Each phase builds on its own: P2 compiles against P1's sequencer, and P4
against P2's `seq_live*` API. P2-12 and P2-17 already pass the new
`rhythm` argument, so P4-14 (`liveRec_rollRateChange()` definition), P4-17
(the `liveRec_trigger_t.rhythm` field) and P4-18 (its prototype) **must
land in the same build as P2**. They are listed under P4 because they
belong to LiveRecord.

---

## 4. P1 — Step roll engine: time window (fixes F2, F3)

All entries are in `Core/Sequencer/sequencer.c`.

### P1-1 MODIFY roll state block — lines 609–633 (anchor: `Per-track roll playback state (S079 §4)`)

- REMOVE `static uint8_t seq_rollRemaining[NUM_TRACKS];` (line 627).
- ADD `static uint16_t seq_rollWindow[NUM_TRACKS];`.
- ADD the flag defines for `seq_rollActive`:

```c
#define SEQ_ROLL_RUN    0x01u   /* window open: retriggers fire */
#define SEQ_ROLL_CARRY  0x02u   /* fill 100: the next step may carry it */
```

- `seq_rollActive[]` keeps its type; its value becomes these flags.
- Replace the block comment:

```c
/*
 * Per-track step-roll playback state (S079 §4; time window S084 P1).
 *
 * What: seq_rollActive holds two flags. SEQ_ROLL_RUN: the roll's window is
 * open and retriggers fire. SEQ_ROLL_CARRY: the roll's step had fill 100,
 * so the track's next step may carry it (user Q4, 2026-10-24: a carry needs
 * the same rate and rhythm, and is refused when the previous roll step's
 * fill was below 100). CARRY set with RUN clear is "parked": the window has
 * closed and no hit fires, but seq_rollTickAccum keeps counting down to 0,
 * so a carry resumes on phase. Parking lasts until the track's next step
 * event (P1-12). seq_rollTickAccum counts the 96-PPQ ticks to the next
 * retrigger. seq_rollWindow counts the master ticks left in which
 * retriggers may fire: the step interval x fill / 100 from the step's
 * (possibly deferred) fire, replaced by each carrying step and extended over
 * a carrying step's deferral (P1-13). seq_rollVelPos is the position in the
 * rhythm velocity cycle. seq_rollLastRate/Rhythm/Fill are the running
 * roll's parameters (carry test, window-end duck). seq_rollStepVel/Note are
 * the payload each retrigger fires with.
 * Why: S079-S083 counted hits per step (seq_rollRemaining). A carry added
 * slots - 1 hits and resumed from an accumulator frozen at ppq - 1, so every
 * carried step lost its boundary hit and started almost one roll period
 * late. Recorded rolls (stored as carried steps) fell to about one hit per
 * step, and the loss depended on the track's step scale (S084 P0 F2). A
 * time window keeps the rate on the master clock at any step scale and needs
 * no per-step count; a rate that does not divide the step (16t in a 1/16
 * step) is exact.
 * Inputs: seq_rollBegin() at a step's fire or deferred fire; the
 * carry-eligibility check and the deferred-carry extension in
 * seq_advanceTrackStep(); the F-R cut; seq_rollClearTrack().
 * Outputs: retriggers and the window-end duck from seq_processRollTicks().
 * Context: written and read in TIM3 only. RAM: 8 x 7 u8 + 7 u16 = 70 B
 * SRAM1 static (S084: +14 B window, -7 B remaining, -21 B duck arrays).
 * Affiliates: seq_rollClearTrack(), seq_rollCarries(), seq_rollBegin(),
 * seq_processRollTicks(), seq_rollComputeVelocity(),
 * instrumentManager_requestGateDuck().
 */
```

### P1-2 REMOVE duck countdown state — lines 635–648 (anchor: `Per-track roll fill duck countdown (S079 P2 fix 4)`)

Delete the comment and `seq_rollDuckDelta[]`, `seq_rollDuckDelay[]`,
`seq_rollDuckActive[]`. The window end is the duck point: P1-14 requests the
duck when a fill < 100 window closes.

### P1-3 ADD `seq_rollClearTrack()` — after P1-2 (before `Per-track play state (S078 §4.4)`, line 650)

```c
static void seq_rollClearTrack(uint8_t track)
{
    seq_rollActive[track] = 0u;
    seq_rollTickAccum[track] = 0u;
    seq_rollVelPos[track] = 0u;
    seq_rollWindow[track] = 0u;
    seq_rollLastRate[track] = 0u;
    seq_rollLastRhythm[track] = 0u;
    seq_rollLastFill[track] = 0u;
}
```

```c
/*
 * End one track's step roll and clear its runtime state (S084 P1).
 *
 * What: zeroes the track's roll flags, accumulator, rhythm position,
 * window and last-roll parameters, so no retrigger, duck or carry follows.
 * seq_rollStepVel/Note are left as they are (only read while a roll runs).
 * Why: the same ten-line clear was repeated at four sites (boot, per-track
 * Scene change and its choke partner, whole-Scene change, transport
 * reset), and each site had to change when the state changed. Inputs:
 * track 0..6 (callers guarantee the range). Outputs: the per-track roll
 * state. Context: TIM3, or the foreground with TIM3 masked (the existing
 * call sites). Accessors: seq_init(), seq_setTrackPlayedScene(),
 * seq_selectActivePattern(), seq_setStepIndexToStart(). Affiliates: the
 * step roll state block above. Live overlay rolls (seq_live*) are a
 * separate state and are never cleared here.
 */
```

### P1-4 MODIFY `seq_init()` roll clear — lines 1685–1694

Replace the ten `memset(seq_roll…)` lines with:

```c
	/* S079/S084 P1: no step roll runs at boot. */
	for (i = 0u; i < NUM_TRACKS; i++)
		seq_rollClearTrack(i);
```

Declare `uint8_t i;` at the top of `seq_init()` if it has no counter. No
other comment is needed; the helper carries the description.

### P1-5 MODIFY `seq_setTrackPlayedScene()` roll clear — lines 1814–1837

Replace the block (from `/* S079 P2 fix 4: per-track Scene change cancels…`
through the closing `}` of the `track >= 5u` pair block) with:

```c
    /* S079 P2 fix 4 / S084 P1: a per-track Scene change ends the track's
     * step roll, and its choke partner's (tracks 5 and 6). */
    seq_rollClearTrack(track);
    if (track >= 5u)
        seq_rollClearTrack((uint8_t)((track == 5u) ? 6u : 5u));
```

### P1-6 MODIFY `seq_selectActivePattern()` roll clear — lines 2123–2133

Replace the comment line and the ten `memset` lines with:

```c
	/* S079 P2 fix 4 / S084 P1: a Scene change ends every running step roll
	 * (its window, duck and carry with it). Live overlay rolls continue. */
	for (track = 0u; track < NUM_TRACKS; track++)
		seq_rollClearTrack(track);
```

Use the function's existing loop variable, or declare one.

### P1-7 MODIFY `seq_setStepIndexToStart()` roll clear — lines 6575–6584

Replace the nine `seq_roll…[i] = 0u;` lines and the three
`seq_rollDuck…[i] = 0u;` lines with `seq_rollClearTrack(i);`. Keep the
existing `/* S079: reset per-track condition and roll runtime state. */`
line above them.

### P1-8 MODIFY `seq_rollCarries()` — lines 2742–2754

```c
static uint8_t seq_rollCarries(uint8_t track, const pat_step_specials_t *sp)
{
    return (uint8_t)((seq_rollActive[track] & SEQ_ROLL_CARRY) != 0u &&
                     sp->roll != 0u &&
                     seq_rollLastRate[track] == sp->roll &&
                     seq_rollLastRhythm[track] == sp->rhythm);
}
```

```c
/*
 * Does an incoming roll step continue the running roll? (S079 §4.2;
 * carry rule S084 P1, user Q4.)
 *
 * What: true when the running roll may be carried (SEQ_ROLL_CARRY: its
 * step had fill 100 and the track has not had a non-carrying step since)
 * and the incoming step has a roll with the same rate and rhythm. The
 * incoming step's fill does NOT have to match; it only sets that step's
 * window. Why: a recorded roll ends on a step whose fill is the release
 * point (< 100). S079 refused that carry, so the last step restarted the
 * roll at the step boundary, which jumped the phase and reset the velocity
 * rhythm (S084 P0 F3). User decision Q4 (2026-10-24): carry unless the
 * previous step's fill was below 100. Inputs: track, the incoming step's
 * resolved specials. Output: nonzero for a carry. Context: TIM3. Accessors:
 * seq_rollBegin(), the carry-eligibility check and the deferred-carry
 * extension in seq_advanceTrackStep(). Affiliates: SEQ_ROLL_CARRY (set by
 * seq_rollBegin(), cleared at the next non-carrying step event).
 */
```

### P1-9 REMOVE `seq_rollArmDuck()` — lines 2756–2770

Delete the function and its comment. Its job moves into P1-14 (the duck is
requested when a fill < 100 window closes).

### P1-10 MODIFY `seq_rollBegin()` — lines 2772–2856

```c
static void seq_rollBegin(uint8_t track, const pat_step_specials_t *sp)
{
    uint8_t ppq = seq_rollPpq(sp->roll);
    uint8_t carry = seq_rollCarries(track, sp);
    uint16_t window;

    if (ppq == 0u) {
        seq_rollActive[track] = 0u;
        seq_rollWindow[track] = 0u;
        return;
    }
    /* Step interval x fill / 100 in whole ticks, rounded from Q8.8. */
    window = (uint16_t)(((uint32_t)stepScale_ticksQ8(
                 seq_effectiveTrackScale[track]) * sp->fill + 12800u)
                 / 25600u);
    if (!carry) {
        seq_rollTickAccum[track] = ppq;
        seq_rollVelPos[track] = 0u;
        seq_triggerVoice(track, sp->velocity, sp->note, 1);
        seq_rollVelPos[track] = 1u;
    } else {
        voiceControl_resetOnly(track);   /* S081 §5.8 renewal, no hit */
    }
    seq_rollWindow[track] = window;
    seq_rollActive[track] = (uint8_t)(SEQ_ROLL_RUN |
        ((sp->fill >= 100u) ? SEQ_ROLL_CARRY : 0u));
    seq_rollLastRate[track] = sp->roll;
    seq_rollLastRhythm[track] = sp->rhythm;
    seq_rollLastFill[track] = sp->fill;
    seq_rollStepVel[track] = sp->velocity;
    seq_rollStepNote[track] = sp->note;
}
```

Keep the existing `Roll renewal requests a reset-only (S081 §5.8)` comment
inside the carry branch unchanged. Replace the function header comment:

```c
/*
 * Begin or carry a per-step roll at the step's fire time (S079 §4, §6;
 * time window S084 P1).
 *
 * What: computes the step's roll window, the step interval x fill / 100 in
 * whole 96-PPQ ticks, from the track's effective step scale. Without a
 * carry it fires hit #1 (reset = 1) and starts the retrigger accumulator at
 * the rate's tick interval. With a carry it fires nothing, requests the
 * reset-only renewal and leaves the accumulator and the rhythm position
 * running, so the hits continue on the same phase across the step
 * boundary. Either way the window is replaced by this step's window,
 * measured from now: the new step owns the roll from here (F-R). The step's
 * fill sets the carry flag for the next step (fill 100 only, user Q4).
 * Fill 0 gives a zero window: a single hit, ducked on the same tick.
 * Why: the S079 engine counted hits per step, and its carry lost the
 * boundary hit of every carried step (S084 P0 F2). The rate is the master
 * clock (seq_rollPpq()); the step scale only sets how long the roll lasts.
 * Inputs: track, the step's resolved specials (roll, rhythm, fill,
 * velocity, note), seq_effectiveTrackScale[track]. Outputs: hit #1 or a
 * reset-only renewal; seq_rollActive/TickAccum/VelPos/Window and the last
 * roll parameters. Context: TIM3 (seq_advanceTrackStep() on time,
 * seq_fireDeferred() when deferred). Affiliates: seq_rollCarries(),
 * seq_processRollTicks(), voiceControl_resetOnly(), stepScale_ticksQ8().
 */
```

### P1-11 MODIFY F-R cut — lines 3115–3136 (anchor: `A running roll ends only when the next step has a trigger that fires`)

In the `if (seq_rollActive[track] && sp.roll == 0u && …)` body, replace
`seq_rollDuckActive[track] = 0u;` with `seq_rollWindow[track] = 0u;`. The
condition is unchanged (`seq_rollActive[track]` is now nonzero for RUN or a
parked CARRY). Append to the existing comment, before `Affiliates:`:

```c
		 * S084 P1: the cut clears the window and both flags, so neither a
		 * retrigger nor a duck nor a carry follows the plain hit.
```

### P1-12 ADD carry-eligibility check — between line 3166 (`}` that closes the echo block) and line 3168 (`if (pat_isStepActive(track, …)) {`)

```c
		if ((seq_rollActive[track] & SEQ_ROLL_CARRY) != 0u &&
		    !(rec_fired && !trigger_suppressed &&
		      ((seq_eraseFullMask | seq_eraseSpecialsMask) &
		       (uint8_t)(1u << track)) == 0u &&
		      seq_rollCarries(track, &sp)))
			seq_rollActive[track] &= (uint8_t)~SEQ_ROLL_CARRY;
```

```c
		/*
		 * Carry eligibility lasts exactly one step (S084 P1).
		 *
		 * What: at every step event of the track, a roll that could be
		 * carried keeps that right only if THIS step will carry it: its
		 * trigger fires (bit 15 and the condition, rec_fired), it is not
		 * suppressed by an echo mark or a live hold, it is not being erased,
		 * and its roll has the same rate and rhythm (seq_rollCarries()). If
		 * not, SEQ_ROLL_CARRY is cleared. A roll whose window is still open
		 * keeps running to its window end (F-R: an empty step does not cut
		 * it); a parked roll (window already closed) ends here. Why: a
		 * fill-100 roll parks at its window end so that the ±1 tick jitter of
		 * a fractional step interval, or a shuffled or micro-timed carrying
		 * step, cannot break the carry. Without this check a parked roll would
		 * stay carryable across empty steps and resume on a stale phase.
		 * Inputs: rec_fired, trigger_suppressed, seq_eraseFullMask,
		 * seq_eraseSpecialsMask, sp. Output: SEQ_ROLL_CARRY cleared or kept.
		 * Context: TIM3. Affiliates: seq_rollCarries(), seq_rollBegin(),
		 * seq_processRollTicks() (parking), the deferred-carry extension
		 * below.
		 */
```

### P1-13 ADD deferred-carry window extension — after lines 3268–3269 (`seq_trackShuffleRoll[track] = (uint8_t)(sp.roll > 0u);`)

```c
						if (sp.roll > 0u &&
						    seq_rollCarries(track, &sp)) {
							if (seq_rollWindow[track] < total_delay)
								seq_rollWindow[track] = total_delay;
							seq_rollActive[track] |= SEQ_ROLL_RUN;
						}
```

```c
						/*
						 * Keep a carried roll sounding through its deferral
						 * (S084 P1).
						 *
						 * What: when a step that will carry the running roll
						 * is deferred by shuffle and/or micro-timing, the
						 * roll's window is stretched (never shortened) to
						 * reach the deferred fire, and a parked roll is
						 * reopened. At the fire, seq_rollBegin() carries it
						 * and sets the step's own window. Why: the previous
						 * step's window ends at this step's boundary; without
						 * the stretch the hits that fall between the boundary
						 * and the deferred fire would be lost, which is the
						 * F2 gap again on every shuffled step. Inputs:
						 * total_delay (ticks to the fire), sp. Outputs:
						 * seq_rollWindow, SEQ_ROLL_RUN. A muted fire
						 * (seq_cancelPendingTrigger()) carries nothing, so the
						 * roll parks at the stretched window end and the next
						 * step event ends it. Context: TIM3. Affiliates:
						 * seq_fireDeferred(), seq_rollBegin(),
						 * seq_rollCarries().
						 */
```

### P1-14 MODIFY `seq_processRollTicks()` — lines 5882–5939

```c
static void seq_processRollTicks(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t st = seq_rollActive[track];
		uint8_t ppq;
		uint8_t vel;

		if (st == 0u)
			continue;
		if ((st & SEQ_ROLL_RUN) != 0u && seq_rollWindow[track] == 0u) {
			st = (uint8_t)(st & ~SEQ_ROLL_RUN);
			seq_rollActive[track] = st;
			if (seq_rollLastFill[track] < 100u)
				instrumentManager_requestGateDuck(track);
			if (st == 0u)
				continue;
		}
		if ((st & SEQ_ROLL_RUN) == 0u) {
			if (seq_rollTickAccum[track] > 0u)
				seq_rollTickAccum[track]--;       /* parked: phase only */
			continue;
		}
		seq_rollWindow[track]--;
		if (seq_rollTickAccum[track] > 0u) {
			seq_rollTickAccum[track]--;
			continue;
		}
		ppq = seq_rollPpq(seq_rollLastRate[track]);
		if (ppq == 0u) {
			seq_rollActive[track] = 0u;
			continue;
		}
		vel = seq_rollComputeVelocity(track);
		seq_triggerVoice(track, vel, seq_rollStepNote[track], 0);
		seq_rollTickAccum[track] = (uint8_t)(ppq - 1u);
		seq_rollVelPos[track]++;
	}
}
```

```c
/*
 * Tick-driven per-step roll retrigger engine (S079 §4; time window
 * S084 P1).
 *
 * What: once per 96-PPQ master tick, for each track with a step roll:
 *  - window end: when an open window (SEQ_ROLL_RUN) has no ticks left it
 *    closes. A fill < 100 roll requests the voice duck here (the fill
 *    boundary, S079 P2 fix 4) and ends; a fill-100 roll parks
 *    (SEQ_ROLL_CARRY only) until the track's next step decides on a carry;
 *  - parked: the accumulator keeps counting down to 0 and holds there, and
 *    no hit fires, so a carry resumes exactly on phase (a hit that fell due
 *    while parked fires on the carry's tick);
 *  - running: one window tick is used, and when the accumulator reaches 0
 *    a retrigger fires (reset = 0) at the rhythm-shaped velocity, the
 *    accumulator restarts at the rate's interval and the rhythm position
 *    advances.
 * Hits therefore sit at first-hit + k x interval for as long as windows
 * stay open, independent of the track's step scale.
 * Why: the S079 engine counted hits per step and froze its accumulator
 * between steps, so carried steps lost their boundary hit (S084 P0 F2).
 * Inputs: seq_rollActive, seq_rollWindow, seq_rollTickAccum,
 * seq_rollVelPos, seq_rollLastRate/Fill, seq_rollStepNote. Outputs:
 * seq_triggerVoice(), instrumentManager_requestGateDuck(). Context: TIM3,
 * from seq_processSchedulerTick() after the DDA, so a step that carries the
 * roll renews its window before the window-end test on the same tick.
 * Affiliates: seq_rollBegin(), seq_rollCarries(), seq_rollComputeVelocity(),
 * seq_rollPpq().
 */
```

### P1-15 MODIFY scheduler comment — lines 6053–6056 (in `seq_processSchedulerTick()`)

```c
    /*
     * S079 §4 / S084 P1: step roll retriggers run after the DDA advance, so
     * a step that carries a roll renews its window before the window-end
     * test on the same tick.
     */
```

### P1-16 MODIFY `seq_rollPpq()` comment — lines 2663–2677

Keep the text. Replace the `Affiliates:` sentence with:

```c
 * Affiliates: seq_processRollTicks, seq_rollBegin, seq_processLiveTicks
 * and seq_liveRollStart/SetRate (S084 P2: PERF/MIDI overlay rolls use the
 * same table), stepRollRateNames and perfBtnTypeNames (MenuText.h; the
 * overlay roll types are step index + 5, S084 P3), menu_getRollRateName,
 * STEP roll clamp. LiveRecord's copy (liveRec_rollPpqTicks) is removed
 * (S084 P4).
```

---

## 5. P2 — PERF/MIDI overlay rolls and flams on the master tick (fixes F4)

### 5.1 `Core/Sequencer/sequencer.c`

#### P2-1 ADD live engine state — after P1-3 (before `Per-track play state (S078 §4.4)`, line 650)

```c
#define SEQ_LIVE_IDLE  0u
#define SEQ_LIVE_ROLL  1u
#define SEQ_LIVE_FLAM  2u
static volatile uint8_t  seq_liveMode[NUM_TRACKS];
static uint8_t           seq_liveRate[NUM_TRACKS];
static uint8_t           seq_liveAccum[NUM_TRACKS];
static uint8_t           seq_livePos[NUM_TRACKS];
static uint8_t           seq_liveNote[NUM_TRACKS];
static uint8_t           seq_liveVel[NUM_TRACKS];
static volatile uint8_t  seq_liveRhythm;
static volatile uint8_t  seq_liveHitCount[NUM_TRACKS];
static volatile uint8_t  seq_liveFlamMask;
static volatile uint32_t seq_liveHitUs[NUM_TRACKS];
```

```c
/*
 * Live (PERF BAR overlay / MIDI note) roll and flam engine (S084 P2).
 *
 * What: one entry per track for a held overlay gesture, driven by the
 * 96-PPQ master tick. seq_liveMode: SEQ_LIVE_IDLE, SEQ_LIVE_ROLL (repeats
 * until stopped) or SEQ_LIVE_FLAM (one pending main hit). seq_liveRate: the
 * step-roll index 1..9 (seq_rollPpq()). seq_liveAccum: ticks to the next
 * hit. seq_livePos: the roll's velocity-rhythm position. seq_liveNote/Vel:
 * the internal pitch and base velocity of the hits. seq_liveRhythm: the
 * packed rhythm byte of the active overlay (`pv<`/`pv>`), one for all
 * tracks, because every held entry follows the one overlay state.
 * Mailbox to the foreground: seq_liveHitCount counts roll hits fired since
 * the last take (saturating at 255), seq_liveFlamMask has bit N set when
 * track N's flam main fired, and seq_liveHitUs is the TIM2 µs stamp of the
 * latest hit.
 * Why: S080-S083 timed overlay repeats with a 1 ms foreground timer from
 * the menu tempo PAR_BPM, with integer division. Rolls ran 2-7 % fast,
 * ignored external sync, and jittered with render load, while recorded rolls
 * play from this master tick, so live and recorded rolls never matched
 * (S084 P0 F4). Running both on one clock makes them identical. LiveRecord
 * never runs in TIM3, so hits are published here and reported by the
 * foreground (buttonHandler_overlayDrain()) with their arrival age.
 * Inputs: the seq_live* API below (foreground, PRIMASK sections) and
 * seq_processLiveTicks() (TIM3). Outputs: seq_triggerVoice() hits and the
 * mailbox. Not cleared by Scene changes or transport resets: a held gesture
 * belongs to the panel or MIDI, not to the Pattern. RAM: 6 x 7 u8 + 1 +
 * 7 u8 + 1 + 7 u32 = 79 B SRAM1 static. Affiliates: buttonHandler overlay
 * (start, re-configure, drain, end), seq_rollPpq(),
 * seq_rollRhythmVelocity(), LiveRecord (through buttonHandler).
 */
```

#### P2-2 ADD live engine clear in `seq_init()` — after the P1-4 loop (line 1694 area)

```c
	/* S084 P2: no live overlay roll or flam is armed at boot. */
	memset((void *)seq_liveMode, 0, sizeof(seq_liveMode));
	memset((void *)seq_liveHitCount, 0, sizeof(seq_liveHitCount));
	seq_liveFlamMask = 0u;
	seq_liveRhythm = 0u;
```

#### P2-3 ADD `seq_processLiveTicks()` and the live API — after `seq_processRollTicks()` (after line 5939)

```c
static void seq_processLiveTicks(void)
{
	uint8_t track;

	for (track = 0u; track < NUM_TRACKS; track++) {
		uint8_t mode = seq_liveMode[track];
		uint8_t ppq;
		uint8_t vel;

		if (mode == SEQ_LIVE_IDLE)
			continue;
		if (seq_liveAccum[track] > 0u) {
			seq_liveAccum[track]--;
			continue;
		}
		if (mode == SEQ_LIVE_FLAM) {
			seq_triggerVoice(track, seq_liveVel[track],
			                 seq_liveNote[track], 1);
			seq_liveMode[track] = SEQ_LIVE_IDLE;
			seq_liveFlamMask |= (uint8_t)(1u << track);
		} else {
			ppq = seq_rollPpq(seq_liveRate[track]);
			if (ppq == 0u) {
				seq_liveMode[track] = SEQ_LIVE_IDLE;
				continue;
			}
			vel = seq_rollRhythmVelocity(seq_liveRhythm,
			                             seq_liveVel[track],
			                             seq_livePos[track]);
			if (seq_livePos[track] < 0xFFu)
				seq_livePos[track]++;
			else
				seq_livePos[track] = 16u;   /* 256 mod 60: same phase */
			seq_triggerVoice(track, vel, seq_liveNote[track], 0);
			seq_liveAccum[track] = (uint8_t)(ppq - 1u);
			if (seq_liveHitCount[track] < 0xFFu)
				seq_liveHitCount[track]++;
		}
		seq_liveHitUs[track] = timebase_tim2Now();
	}
}
```

```c
/*
 * Live overlay roll/flam tick (S084 P2).
 *
 * What: once per 96-PPQ master tick, for every armed live entry: count the
 * accumulator down; at 0, a flam fires its main hit (reset = 1, it is the
 * gesture's main trigger) and goes idle, and a roll fires a retrigger
 * (reset = 0) at the overlay rhythm's velocity for its position, then
 * restarts the accumulator at the rate's interval. Each hit updates the
 * mailbox (count or flam bit, µs stamp). Why: overlay hits on the master
 * clock, exactly the step-roll rate table and spacing, so a live roll and
 * its recording sound the same at any tempo and under external sync (S084
 * P0 F4). Inputs: seq_live* state, seq_liveRhythm. Outputs:
 * seq_triggerVoice(), the mailbox. Context: TIM3 only. Called from
 * seq_processSchedulerTick() before the running gate (internal sync, or
 * external sync while running) and from seq_tick() under external sync
 * while stopped, never both on one tick. Affiliates: seq_liveRollStart(),
 * seq_liveTakeHits(), buttonHandler_overlayDrain().
 */
```

Public API, defined right after `seq_processLiveTicks()`. Each one masks
TIM3 with the file's existing pattern
(`uint32_t primask = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(primask);`):

```c
void seq_liveRollStart(uint8_t track, uint8_t rate, uint8_t note,
                       uint8_t vel, uint8_t pos)
{   /* track >= NUM_TRACKS: return. ppq = seq_rollPpq(rate); ppq == 0: stop.
     * Masked: mode = ROLL, rate, accum = ppq - 1, pos, note, vel,
     * hitCount = 0, flam bit cleared. */ }

void seq_liveRollSetRate(uint8_t track, uint8_t rate)
{   /* Masked, mode == ROLL only: n = accum + seq_rollPpq(rate)
     * - seq_rollPpq(old rate), as int16, clamped to 0..ppq - 1;
     * accum = n; rate = rate. Any other mode: no change. */ }

void seq_liveFlamArm(uint8_t track, uint8_t note, uint8_t vel,
                     uint8_t ticks)
{   /* Masked: mode = FLAM, accum = ticks ? ticks - 1 : 0, note, vel,
     * hitCount = 0, flam bit cleared. */ }

void seq_liveStop(uint8_t track)
{   /* mode = IDLE (single byte store; the mailbox is kept for a final
     * take). */ }

void seq_liveSetRhythm(uint8_t rhythm)
{   /* seq_liveRhythm = rhythm (single byte store). */ }

uint8_t seq_liveTakeHits(uint8_t track, uint32_t *stampUs,
                         uint8_t *flamMain)
{   /* Masked: n = hitCount; hitCount = 0; *flamMain = flam bit; bit
     * cleared; *stampUs = seq_liveHitUs[track]; return n.
     * track >= NUM_TRACKS: return 0, *flamMain = 0. */ }
```

The `.c` definitions carry this block (one block above the group):

```c
/*
 * Live overlay engine API (S084 P2). Contracts in sequencer.h.
 *
 * What: the foreground arms, re-rates, stops and drains the per-track live
 * entries. Each call that writes more than one byte masks TIM3 (PRIMASK),
 * so seq_processLiveTicks() never sees a half-written entry. Start values:
 * a roll's first repeat is ppq - 1 ticks after the next tick boundary, so
 * the first gap from the press is between ppq - 1 and ppq ticks and every
 * later gap is exactly ppq (Q3: hits are spaced from the press). A rate
 * change keeps the time since the last hit (the S081 F-B rule "new interval
 * from the last hit time"). Why: buttonHandler owns the gesture, the
 * Sequencer owns the clock. Inputs/outputs: see each contract. Context:
 * foreground. Accessors: buttonHandler_overlayStart(),
 * buttonHandler_overlayReconfigure(), buttonHandler_overlayEndTrack(),
 * buttonHandler_overlayDrain(), buttonHandler_overlayTick(). Affiliates:
 * seq_processLiveTicks(), the live engine state block.
 */
```

#### P2-4 ADD live tick call in `seq_processSchedulerTick()` — immediately before line 5960 (`if (!seq_running)`)

```c
	/*
	 * Live overlay rolls and flams first, running or stopped (S084 P2).
	 *
	 * What: services the PERF/MIDI overlay entries on this master tick,
	 * before the running gate, so a held roll repeats at the tempo while the
	 * transport is stopped (internal sync) and follows every external clock
	 * tick while running. Why: the overlay must be playable stopped, as
	 * before S084, but on the master clock. Order: it touches no Pattern,
	 * DDA or step-roll state; a stored step on the same track this tick
	 * triggers after it. Context: TIM3. Affiliates: seq_processLiveTicks(),
	 * seq_tick() (external sync, stopped).
	 */
	seq_processLiveTicks();
```

#### P2-5 MODIFY `seq_tick()` — lines 6193–6194

```c
		if (!seq_getExtSync())
			seq_processSchedulerTick();
		else if (!seq_running)
			seq_processLiveTicks();
```

Add above it:

```c
		/*
		 * External sync, transport stopped (S084 P2, Q9): no clock reaches
		 * the scheduler (clockSync and the pulse input tick it only while
		 * running), so live overlay rolls run from this tempo timer.
		 * seq_tempo follows the incoming clock (sync_calcBpm(),
		 * trigger_applyPulseTempo()), so the rate matches the external
		 * tempo. With no clock at all they keep the last measured tempo.
		 */
```

#### P2-6 MODIFY `seq_triggerVoice()` comment — lines 1946–1960

Replace the `Affiliates:` sentence with:

```c
 * Affiliates: voiceControl_noteOn/noteOnNoReset, seq_advanceTrackStep
 * callers, seq_processRollTicks, seq_processLiveTicks (S084 P2: overlay
 * roll repeats reset = 0, flam main reset = 1, from TIM3), the
 * buttonHandler overlay start (first hits, foreground), seq_previewVoice.
```

### 5.2 `Core/Sequencer/sequencer.h`

#### P2-7 MODIFY `seq_rollRhythmVelocity()` contract — lines 222–232

Replace the `Callers:` sentence with:

```c
 * (`pv<`/`pv>`) must use one rule. Pure, no state. Callers:
 * seq_rollComputeVelocity() (step rolls) and seq_processLiveTicks()
 * (overlay rolls, S084 P2), both TIM3.
```

#### P2-8 ADD live API prototypes — after line 233 (`uint8_t seq_rollRhythmVelocity(…);`)

```c
/*
 * Live overlay roll/flam engine (S084 P2). Contracts; the definitions in
 * sequencer.c carry the full description.
 *
 * seq_liveRollStart(track, rate, note, vel, pos): arm a held roll on the
 * master tick. The caller has already fired hit 0. rate is the step-roll
 * index 1..9 (0 or invalid: stops). The first repeat follows one rate
 * interval after the press (ppq - 1 whole ticks from the next tick); pos is
 * the rhythm position of that repeat (1 after a fired hit 0). Clears the
 * track's mailbox.
 * seq_liveRollSetRate(track, rate): change a running roll's rate and keep
 * the time since its last hit (S081 F-B). No effect unless a roll runs.
 * seq_liveFlamArm(track, note, vel, ticks): arm a flam main hit `ticks`
 * master ticks after the press (12 = 1/32), fired with reset = 1. Clears
 * the mailbox.
 * seq_liveStop(track): disarm the track; the mailbox keeps unreported hits
 * for a final seq_liveTakeHits().
 * seq_liveSetRhythm(rhythm): the packed rhythm byte applied to every live
 * roll from its next hit (the active overlay's `pv<`/`pv>`).
 * seq_liveTakeHits(track, &stampUs, &flamMain): returns the roll hits
 * fired since the last take (0..255), sets flamMain when the flam main
 * fired, and stampUs to the TIM2 µs time of the latest hit; clears them.
 * Context: foreground; TIM3-safe (PRIMASK). Accessor: buttonHandler
 * overlay. Affiliates: seq_processLiveTicks().
 */
void    seq_liveRollStart(uint8_t track, uint8_t rate, uint8_t note,
                          uint8_t vel, uint8_t pos);
void    seq_liveRollSetRate(uint8_t track, uint8_t rate);
void    seq_liveFlamArm(uint8_t track, uint8_t note, uint8_t vel,
                        uint8_t ticks);
void    seq_liveStop(uint8_t track);
void    seq_liveSetRhythm(uint8_t rhythm);
uint8_t seq_liveTakeHits(uint8_t track, uint32_t *stampUs,
                         uint8_t *flamMain);
```

### 5.3 `Core/Hardware/frontPanel/buttonHandler.c`

#### P2-9 MODIFY overlay entry table — lines 368–401 (anchor: `Per-track overlay repeat state (S083 Q5…)`)

```c
typedef struct {
	uint8_t heldMidiNote;
	uint8_t note;
	uint8_t count;
	uint8_t rate;
} bh_overlay_track_t;
```

ADD next to `BH_OVERLAY_VEL`:

```c
#define BH_FLAM_TICKS            12u  /* flam main: 1/32 at 96 PPQ */
```

Comment:

```c
/*
 * Per-track overlay gesture state (S083 Q5; S084 P2 master-tick timing).
 *
 * What: one entry per track. heldMidiNote: the MIDI note whose note-off
 * ends this track's roll, stored as note + 1 (0 = panel or none, so the
 * zero-initialised table needs no init code). note: internal note, bit 7
 * set when it came from `def` (records no note special, S081 Q12). count:
 * 0 idle, 1 = flam main pending, 0xFF = held roll. rate: the held roll's
 * step-roll index 1..9 (reported with each roll hit so LiveRecord can start
 * a span at any hit, S084 P4). The hit timing, rhythm position and
 * repeats live in the Sequencer's live engine (seq_live*, TIM3); this
 * table only owns the gesture. Why: S080-S083 timed repeats here with a
 * 1 ms timer from PAR_BPM, off the master clock (S084 P0 F4). MIDI notes on
 * several tracks (and several held VOICE buttons) roll at once (S083 Q5).
 * RAM: 7 x 4 B = 28 B SRAM1 static (S084: -28 B; pos, interval and next
 * removed, rate added). Writers: overlay start, drain, end and
 * re-configure below (all foreground). Affiliates: buttonHandler_tick(),
 * MidiNotes, seq_liveRollStart(), seq_liveTakeHits().
 */
```

#### P2-10 REMOVE ms timing helpers; MODIFY section comment — lines 865–904

Delete `buttonHandler_overlaySixteenthMs()` and
`buttonHandler_overlayDivisionMs()` (lines 881–904). Replace the section
comment (lines 865–880) with:

```c
/*
 * BAR overlay trigger helpers (S080 E12; master-tick timing S084 P2).
 *
 * The overlay turns the VOICE row (and MIDI notes, S083) into a trigger row
 * while BAR `<` or `>` is active. Types 0..3 fire one hit at a fixed
 * velocity; 4..5 fire a soft grace hit now and a main hit BH_FLAM_TICKS
 * master ticks later; 6..14 are held rolls at the step-roll rates (index =
 * type - 5, S084 P3). First hits fire here in the foreground; flam mains
 * and roll repeats fire from the Sequencer's live engine on the 96-PPQ
 * master tick (seq_live*, TIM3) and are reported to LiveRecord by
 * buttonHandler_overlayDrain(). The ms timer from PAR_BPM is gone: it ran
 * off the master clock and ignored external sync (S084 P0 F4).
 * Affiliates: handleVoiceButton() (E12), processRelease(),
 * buttonHandler_tick(), MidiNotes, LiveRecord.
 */
```

ADD below the `#define BH_OVERLAY_*` group (P2-9) the type layout defines:

```c
#define BH_OVERLAY_TYPE_FLAM_FIRST  4u   /* flm, fls */
#define BH_OVERLAY_TYPE_ROLL_FIRST  6u   /* 1/8 .. 128 = step index 1..9 */
#define BH_OVERLAY_TYPE_MAX         14u
```

#### P2-11 ADD `buttonHandler_overlayRhythm()` — before `buttonHandler_overlayReport()` (line 906)

```c
static uint8_t buttonHandler_overlayRhythm(uint8_t which)
{
    return parameter_values[(which == 1u) ? PAR_PERF_VEL_LT
                                          : PAR_PERF_VEL_RT];
}
```

```c
/*
 * Velocity rhythm of overlay `which` (S083 follow-up; S084 helper).
 *
 * What: the packed roll-rhythm byte of `pv<` (which 1) or `pv>` (which 2;
 * also returned for 0, where no roll runs). Why: the same lookup was
 * repeated in the tick, start and re-configure paths; S084 also records it
 * (P4). Inputs: which. Output: rhythm byte (0 = off). Accessors:
 * buttonHandler_overlayStart(), _Reconfigure(), _Drain(), _Tick().
 * Affiliates: PAR_PERF_VEL_LT/RT, seq_liveSetRhythm().
 */
```

#### P2-12 MODIFY `buttonHandler_overlayReport()` — lines 906–933

ADD the parameter `uint8_t rhythm` after `roll`, and `t.rhythm = rhythm;`.
In the comment, replace `(track, note, velocity, kind, roll, OVERLAY source)`
with `(track, note, velocity, kind, roll, rhythm, source, age)` and append:

```c
 * S084: roll is the step-roll index on ROLL_START and on every ROLL_HIT
 * (the held rate), rhythm the active overlay's `pv` byte on roll kinds (0
 * otherwise), so LiveRecord can start or re-rate a span at any hit.
```

#### P2-13 MODIFY `buttonHandler_overlayStart()` — lines 935–999

```c
static void buttonHandler_overlayStart(uint8_t track, uint8_t which,
                                       uint8_t note, uint8_t heldMidiNote,
                                       uint8_t source, uint16_t ageUs)
{
    bh_overlay_track_t *e = &bh_overlay[track];
    uint8_t type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                                 : parameter_values[PAR_PERF_BTN_RT];
    uint8_t pitch = (uint8_t)(note & 0x7Fu);
    uint8_t isDef = (uint8_t)((note & BH_OVERLAY_NOTE_DEFAULT) != 0u);

    if (type > BH_OVERLAY_TYPE_MAX)
        type = 0u;
    if (e->count != 0u) {                 /* a new gesture replaces the old */
        uint8_t wasRoll = (uint8_t)(e->count == 0xFFu);

        seq_liveStop(track);
        buttonHandler_overlayDrain(track);
        e->count = 0u;
        if (wasRoll)
            liveRec_rollRelease(track);
    }
    e->note = note;
    if (type < BH_OVERLAY_TYPE_FLAM_FIRST) {
        static const uint8_t singleVel[4] = {127u, 100u, 64u, 32u};

        seq_triggerVoice(track, singleVel[type], pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, singleVel[type],
                                    LIVEREC_HIT_SINGLE, 0u, 0u, source, ageUs);
        e->heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
        return;
    }
    if (type < BH_OVERLAY_TYPE_ROLL_FIRST) {
        seq_triggerVoice(track, 64u, pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, 64u,
                                    LIVEREC_HIT_FLAM_GRACE, 0u, 0u,
                                    source, ageUs);
        seq_liveFlamArm(track, pitch, BH_OVERLAY_VEL, BH_FLAM_TICKS);
        e->count = 1u;
    } else {
        uint8_t idx = (uint8_t)(type - (BH_OVERLAY_TYPE_ROLL_FIRST - 1u));
        uint8_t rhythm = buttonHandler_overlayRhythm(which);

        seq_triggerVoice(track, BH_OVERLAY_VEL, pitch, 1);
        buttonHandler_overlayReport(track, pitch, isDef, BH_OVERLAY_VEL,
                                    LIVEREC_HIT_ROLL_START, idx, rhythm,
                                    source, ageUs);
        seq_liveSetRhythm(rhythm);
        seq_liveRollStart(track, idx, pitch, BH_OVERLAY_VEL, 1u);
        e->rate = idx;
        e->count = 0xFFu;
    }
    e->heldMidiNote = heldMidiNote;
}
```

```c
/*
 * Start an overlay gesture on one track (S080 E12; S083 Q5 per-track;
 * S084 P2 master-tick timing, P3 15 types).
 *
 * What: a new gesture first ends the track's old one: its live entry is
 * stopped, its unreported hits are drained to LiveRecord, and a held roll's
 * recording is ended at this point. Then it reads the trigger type of
 * overlay `which` (1 = BAR <, 2 = BAR >) and fires the first hit:
 *  - 0..3 single: one hit at 127/100/64/32;
 *  - 4..5 flam: grace hit at 64 now, and the main hit (BH_OVERLAY_VEL,
 *    reset) armed BH_FLAM_TICKS master ticks later (seq_liveFlamArm());
 *  - 6..14 roll: hit 0 at BH_OVERLAY_VEL now, then repeats at step-roll
 *    index type - 5 on the master tick, shaped by the overlay's `pv`
 *    rhythm from position 1 (seq_liveRollStart()), until release.
 * The internal note is stored for every type, so a later switch of a held
 * single to a roll plays the right pitch (the S083 entry kept a stale note
 * after a single). Why: S084 P0 F4 (timing) and F1 (types).
 * Inputs: track, which, internal note (bit 7 = `def`), heldMidiNote (MIDI
 * note + 1 that ends a roll, or BH_OVERLAY_NO_MIDI_NOTE), source and age
 * for LiveRecord. Outputs: seq_triggerVoice(), the LiveRecord report, the
 * live entry, bh_overlay[track]. Context: foreground. Callers:
 * buttonHandler_overlayTrigger() (panel), buttonHandler_overlayMidiNoteOn()
 * (MidiNotes). Affiliates: buttonHandler_overlayDrain(), _EndTrack(),
 * seq_liveRollStart(), seq_liveFlamArm(), liveRec_triggerIn().
 */
```

#### P2-14 ADD `buttonHandler_overlayDrain()` — before `buttonHandler_overlayStart()` (line 935; it is called from Start, EndTrack, Reconfigure and Tick)

```c
static void buttonHandler_overlayDrain(uint8_t track)
{
    bh_overlay_track_t *e = &bh_overlay[track];
    uint32_t stampUs;
    uint32_t age;
    uint8_t flamMain;
    uint8_t hits;
    uint8_t pitch = (uint8_t)(e->note & 0x7Fu);
    uint8_t isDef = (uint8_t)((e->note & BH_OVERLAY_NOTE_DEFAULT) != 0u);
    uint8_t source = (e->heldMidiNote != BH_OVERLAY_NO_MIDI_NOTE)
                     ? LIVEREC_SRC_MIDI : LIVEREC_SRC_OVERLAY;

    if (e->count == 0u)
        return;
    hits = seq_liveTakeHits(track, &stampUs, &flamMain);
    if (hits == 0u && !flamMain)
        return;
    age = timebase_tim2Delta(timebase_tim2Now(), stampUs);
    if (age > 0xFFFFu)
        age = 0xFFFFu;
    if (flamMain) {
        buttonHandler_overlayReport(track, pitch, isDef, BH_OVERLAY_VEL,
                                    LIVEREC_HIT_FLAM_MAIN, 0u, 0u, source,
                                    (uint16_t)age);
        e->count = 0u;
        e->heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
        return;
    }
    if (e->count == 0xFFu)
        buttonHandler_overlayReport(track, pitch, isDef, BH_OVERLAY_VEL,
                                    LIVEREC_HIT_ROLL_HIT, e->rate,
                                    buttonHandler_overlayRhythm(
                                        bh_overlayActive),
                                    source, (uint16_t)age);
}
```

```c
/*
 * Report a track's live hits to LiveRecord (S084 P2).
 *
 * What: takes the hits the Sequencer's live engine fired since the last
 * take. A fired flam main is reported as FLAM_MAIN and ends the entry (a
 * MIDI-held flam's note-off already let it finish, B-12). One or more roll
 * hits are reported as ONE ROLL_HIT carrying the held rate and rhythm:
 * LiveRecord only counts roll hits and starts a span at the first hit it
 * sees for a track with no open span. The age is the time since the latest
 * hit (TIM2 µs, capped at 65535), so its placement uses the hit time, not
 * the 1 ms drain time (the S083 Q6 arrival-age path). Why: LiveRecord never
 * runs in TIM3; TIM3 publishes, this foreground step reports. Inputs:
 * track, bh_overlay[track], the live mailbox. Outputs:
 * buttonHandler_overlayReport() calls; the entry ends after a flam main.
 * Context: foreground. Accessors: buttonHandler_overlayTick() (every
 * pass), _EndTrack() and _Start() (final take before a gesture ends),
 * _Reconfigure() (before a rate or type change). Affiliates:
 * seq_liveTakeHits(), liveRec_triggerIn().
 */
```

#### P2-15 MODIFY `buttonHandler_overlayTrigger()` — line 1022

`bh_overlayHeldType = (type > 11u) ? 0u : type;` →
`bh_overlayHeldType = (type > BH_OVERLAY_TYPE_MAX) ? 0u : type;`.

#### P2-16 MODIFY `buttonHandler_overlayEndTrack()` — lines 1028–1045

```c
static void buttonHandler_overlayEndTrack(uint8_t track)
{
    seq_liveStop(track);
    buttonHandler_overlayDrain(track);
    bh_overlay[track].count = 0u;
    bh_overlay[track].heldMidiNote = BH_OVERLAY_NO_MIDI_NOTE;
    voiceControl_noteOff(track);
    midiOut_trackNoteOff(track);
    liveRec_rollRelease(track);
}
```

Replace the first sentence of the comment with:

```c
 * What: stops the track's live entry (no further repeat or flam main can
 * fire), reports the hits that already sounded (so LiveRecord's "released
 * before the second hit" test sees them), sends the voice and MIDI
 * note-off, and ends the track's recorded roll (S084: per track).
```

and add `seq_liveStop(), buttonHandler_overlayDrain(),` to its
`Affiliates:`.

#### P2-17 MODIFY `buttonHandler_overlayReconfigure()` — lines 1082–1150

```c
static void buttonHandler_overlayReconfigure(uint8_t which)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++) {
        bh_overlay_track_t *e = &bh_overlay[track];
        uint8_t midiHeld = (uint8_t)(e->heldMidiNote != BH_OVERLAY_NO_MIDI_NOTE);
        uint8_t panelHeld = (uint8_t)((bh_overlayVoiceMask >> track) & 1u);
        uint8_t oldRoll = (uint8_t)(e->count == 0xFFu);
        uint8_t type;
        uint8_t idx;
        uint8_t rhythm;

        if (!(midiHeld || panelHeld))
            continue;
        buttonHandler_overlayDrain(track);
        if (which == 0u) {
            seq_liveStop(track);
            e->count = 0u;
            liveRec_rollRelease(track);
            continue;
        }
        type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                             : parameter_values[PAR_PERF_BTN_RT];
        if (type > BH_OVERLAY_TYPE_MAX)
            type = 0u;
        if (type < BH_OVERLAY_TYPE_ROLL_FIRST) {
            seq_liveStop(track);
            e->count = 0u;
            liveRec_rollRateChange(track, 0u, 0u);
            continue;
        }
        idx = (uint8_t)(type - (BH_OVERLAY_TYPE_ROLL_FIRST - 1u));
        rhythm = buttonHandler_overlayRhythm(which);
        seq_liveSetRhythm(rhythm);
        if (oldRoll) {
            seq_liveRollSetRate(track, idx);
        } else {
            /* single/flam -> roll: the hit already played was hit 0. */
            seq_liveRollStart(track, idx, (uint8_t)(e->note & 0x7Fu),
                              BH_OVERLAY_VEL, 1u);
            e->count = 0xFFu;
        }
        e->rate = idx;
        liveRec_rollRateChange(track, idx, rhythm);
    }
}
```

Comment: keep the S081/S083 rule list. Replace the opening `What:` lines and
add the S084 sentence:

```c
/*
 * Re-configure active overlay entries after an overlay change (S081 5.7
 * R19/F-B/F-O; S083 per-track; S084 P2 master tick, P3 15 types).
 *
 * What: for every held entry (panel-held or MIDI-held; after a BAR press,
 * a CC2 change or an event-ring overflow), first reports its unreported
 * hits, then applies the S081 rules per track, judging "was a roll" by the
 * entry's own count (0xFF):
 *  - overlay off: the live entry stops, no extra hit, the recorded roll
 *    ends;
 *  - roll -> roll: seq_liveRollSetRate() keeps the time since the last hit
 *    (new interval from the last hit time);
 *  - single/flam -> roll: seq_liveRollStart() starts repeats one interval
 *    from now at rhythm position 1 (the hit already played was hit 0); a
 *    pending flam main is dropped, as before;
 *  - roll -> single/flam: the live entry stops, the recorded roll ends.
 * The new overlay's `pv` rhythm is published to the live engine at once.
 * Why: S084 P0 F4: rates on the master clock. Inputs: which (0 off, 1 <,
 * 2 >), the entries. Outputs: live engine calls, entries, LiveRecord rate
 * changes (rate and rhythm, S084 P4). Context: foreground. Callers:
 * buttonHandler_overlaySwitchHeld() (BAR press, CC2), the event-ring
 * overflow recovery in buttonHandler_processEvents(). Affiliates:
 * buttonHandler_overlayDrain(), seq_liveRollSetRate(), seq_liveRollStart(),
 * seq_liveStop(), liveRec_rollRateChange(), liveRec_rollRelease().
 */
```

#### P2-18 MODIFY `buttonHandler_overlaySwitchHeld()` — line 1172

`((type > 11u) ? 0u : type)` → `((type > BH_OVERLAY_TYPE_MAX) ? 0u : type)`.

#### P2-19 MODIFY `buttonHandler_overlayTick()` — lines 1197–1260

```c
static void buttonHandler_overlayTick(void)
{
    uint8_t track;

    seq_liveSetRhythm(buttonHandler_overlayRhythm(bh_overlayActive));
    for (track = 0u; track < NUM_TRACKS; track++)
        buttonHandler_overlayDrain(track);
}
```

```c
/*
 * Foreground overlay service, every buttonHandler_tick() (S080 E12; S083
 * per-track; S084 P2: timing moved to the master tick).
 *
 * What: publishes the active overlay's velocity rhythm (`pv<` = PAR_PERF_
 * VEL_LT, `pv>` = PAR_PERF_VEL_RT) to the live engine, so a `pv` edit or
 * a BAR/CC2 overlay change shapes every held roll from its next hit, and
 * reports each track's new live hits to LiveRecord
 * (buttonHandler_overlayDrain()). It no longer fires hits: flam mains and
 * roll repeats fire in TIM3 (seq_processLiveTicks()). Why: S084 P0 F4.
 * Inputs: bh_overlayActive, the PERF velocity parameters, the entries.
 * Outputs: seq_liveSetRhythm(), LiveRecord reports. Context: foreground,
 * 1 ms. Accessor: buttonHandler_tick(). Affiliates: seq_liveTakeHits(),
 * seq_rollRhythmVelocity() (applied in TIM3).
 */
```

#### P2-20 MODIFY `buttonHandler_tick()` comment — line 1331

`/* S080 E12: service the overlay flam/roll repeat timer. */` →
`/* S080 E12 / S084 P2: publish the overlay rhythm and report live hits. */`

### 5.4 `Core/Hardware/frontPanel/buttonHandler.h`

No API change. In the overlay comment (lines 124–141), no edit is
required. The MIDI note-off text ("a flam's main hit still plays") stays
true.

---

## 6. P3 — One roll list (fixes F1)

### P3-1 MODIFY `perfBtnTypeNames` — `Core/Menu/MenuText.h` lines 302–314

```c
static const char perfBtnTypeNames[][4] = {
    {15},
    {"vmx"},{"v10"},{"v64"},{"v32"},{"flm"},{"fls"},
    {"1/8"},{"8tr"},{" 16"},{"16t"},{" 32"},{"32t"},{" 64"},{"64t"},{"128"},
};
```

```c
/*
 * PERF BAR overlay trigger-type names (S080 E4; S084 P3 one roll list).
 *
 * [0] = count 15; [1..15] = stored value 0..14. 0..3 single velocities
 * (vmx v10 v64 v32), 4..5 flams (flm fls), 6..14 held rolls with exactly
 * the STEP roll labels and rates: value v is step-roll index v - 5 (1/8,
 * 8tr, 16, 16t, 32, 32t, 64, 64t, 128). Why: PERF offered only six of the
 * nine step rates under different names (S084 P0 F1); one list makes every
 * step roll playable live and keeps one vocabulary (Q1). Stored values
 * 6..11 changed meaning; no migration (user Q2, 2026-10-24). Rendered by
 * the static-param special case in menu.c (all DTYPE_MENU ids are taken),
 * which reads the count from [0][0]. Affiliates: PAR_PERF_BTN_LT/RT,
 * stepRollRateNames, seq_rollPpq(), buttonHandler_overlayStart()
 * (BH_OVERLAY_TYPE_* layout), menu_clampCellValue().
 */
```

### P3-2 MODIFY `stepRollRateNames` comment — `MenuText.h` lines 288–296

Append to `Affiliates:`: `perfBtnTypeNames (overlay roll types 6..14 =
these indices 1..9, S084 P3).`

### P3-3 MODIFY overlay type clamp — `Core/Menu/menu.c` lines 6484–6487 and comment lines 6450–6458

`if (*value > 11u) *value = 11u;` → `if (*value > 14u) *value = 14u;`. In
the comment, `clamp the overlay trigger type to 0..11` → `clamp the overlay
trigger type to 0..14 (S084 P3: 6 non-roll types + 9 step-roll rates)`.

### P3-4 MODIFY overlay type renderer — `Core/Menu/menu.c` line 11652 (in `menu_getS080StaticName()`)

```c
        /* S084 P3: count from the table's own [0][0], so it cannot drift. */
        menu_nameTable3(perfBtnTypeNames, (uint8_t)perfBtnTypeNames[0][0],
                        value, buf);
```

### P3-5 MODIFY parameter comment — `Core/Bank/Scene/Preset/ParameterArray.h` lines 214–224

Replace `The *_BTN ids select a trigger type (vmx, v10, v64, v32, flm, fls,
r/8, r16, r32, r64, r6t, 128);` with:

```c
	 * The *_BTN ids select a trigger type 0..14 (vmx, v10, v64, v32, flm,
	 * fls, then the nine STEP roll rates 1/8 8tr 16 16t 32 32t 64 64t 128 =
	 * step-roll index type - 5; S084 P3, no migration of older values);
```

### P3-6 MODIFY held-type comment — `buttonHandler.c` lines 407–414

`the trigger type (0..11)` → `the trigger type (0..14, S084 P3)`.

### P3-7 Settings file

`Core/Hardware/SD/filesystem.c`: **no code change**. `perf_btn_lt/rt` are
read into `parameter_values[]` unclamped (lines 3002–3004) and written as
numbers (lines 18599/18605). An out-of-range value is treated as type 0 by
`buttonHandler_overlayStart()` and clamped by the menu on the first edit.
No migration (Q2).

---

## 7. P4 — LiveRecord: per-track spans for every held roll (fixes F5, F6, F7, F8)

### 7.1 `Core/Sequencer/LiveRecord.c`

#### P4-1 MODIFY roll capture state — lines 100–122 (anchor: `Open fast-roll capture (S081 §8.8…)`)

REMOVE `liveRec_rollHeld`, `liveRec_rollStep`, `liveRec_rollIndex`,
`liveRec_rollStartDelay`, `liveRec_rollSpan` (lines 118–122). ADD:

```c
#define LIVEREC_ROLL_HELD      0x01u
#define LIVEREC_ROLL_SPAN      0x02u
#define LIVEREC_ROLL_NOTE_DEF  0x04u
typedef struct {
    uint8_t flags;
    uint8_t step;
    uint8_t index;
    uint8_t startDelay;
    uint8_t note;
    uint8_t vel;
    uint8_t hits;
    uint8_t rhythm;
} liveRec_roll_t;
static liveRec_roll_t liveRec_roll[NUM_TRACKS];
```

```c
/*
 * Open roll capture, one per track (S081 §8.8 E-4, P1C items 4/15; S084 P4
 * per track, every held roll).
 *
 * What: while a track's roll gesture is recorded, its entry has
 * LIVEREC_ROLL_HELD and:
 *  - step/startDelay: the start step and its micro-timing as written;
 *  - index/rhythm: the current step-roll rate and velocity rhythm (the
 *    overlay's `pv`, Q8), written into every span and the end step;
 *  - note/vel and LIVEREC_ROLL_NOTE_DEF: the payload (a `def` note records
 *    no note special, S081 Q12);
 *  - hits: roll hits reported since the start (the "released before the
 *    second hit" test);
 *  - LIVEREC_ROLL_SPAN: the playhead has reached the start step (at once
 *    when the start is the current step or behind it; at the start step's
 *    ring entry when quantised forward, LC-8).
 * The span writer writes only steps after the start, and only with SPAN
 * set; the start and the end drain the ring first (liveRec_syncRing()).
 * Why: S081 kept one slot for all tracks, but S083 Q5 lets overlay rolls
 * run on several tracks at once: another track's hits were counted against
 * the open roll, a release on any track ended it, and a second start
 * overwrote it and left the first track's live hold stuck (S084 P0 F5).
 * Every held roll is now a span at any rate (user Q7, 2026-10-24), so there
 * is no slow/fast state. Inputs: liveRec_triggerIn() (start, hits),
 * liveRec_rollRateChange(). Outputs: span, start and end step writes;
 * seq_setLiveHold(). RAM: 7 x 8 B = 56 B SRAM1 static (replaces 11 B of
 * scalars). Affiliates: liveRec_drainRing(), liveRec_endRoll(),
 * liveRec_endAllRolls().
 */
```

#### P4-2 REMOVE roll payload scalars — lines 139–141

Delete `liveRec_rollNote`, `liveRec_rollNoteDefault`, `liveRec_rollVel`.

#### P4-3 MODIFY P1B/P1C state — lines 166–183

REMOVE `liveRec_rollTrack`, `liveRec_rollHits`, `liveRec_rollPendingIdx`
(lines 181–183). In the comment, delete the three lines naming them and the
`rollStartDelay/rollSpan` line, and append:

```c
 * (S084 P4: the roll fields moved into the per-track liveRec_roll[].)
```

Reduce the `RAM about 14 B` figure by the 3 B removed here.

#### P4-4 MODIFY forward declaration — lines 198–199

```c
/* End one track's open roll, or every track's (S081 LR-13; S084 per
 * track); defined below with the roll helpers. */
static void liveRec_endRoll(uint8_t track, uint8_t writeEnd);
static void liveRec_endAllRolls(uint8_t writeEnd);
```

#### P4-5 REMOVE `liveRec_rollPpqTicks()` — lines 284–290

Delete the function and its one-line comment. Its only use was the
slow/fast classification that Q7 retires.

#### P4-6 MODIFY `liveRec_writeStep()` — lines 300–331

ADD parameter `uint8_t rhythm` after `fill`, and `spec.rhythm = rhythm;`.
Comment: `(note or "default note", velocity, timing, roll, fill, fil)` →
`(note or "default note", velocity, timing, roll, fill, rhythm, fil)`, and
append:

```c
 * S084 P4: rhythm is the roll's velocity rhythm (the overlay `pv` byte,
 * Q8); it is stored only with a roll, so the recorded roll plays with the
 * velocity shape that was heard (S084 P0 F7). Pass 0 for a non-roll write.
```

Callers (all updated in their own entries): 416 (P4-10), 1112 (P4-11),
1221/1230/1238 (P4-12).

#### P4-7 MODIFY `liveRec_newTake()` — lines 333–347

ADD as the first statement: `liveRec_endAllRolls(0u);`. Append to its
comment:

```c
 * S084 P4: a take boundary also ends every open roll WITHOUT an end write
 * (PATTERN_DYNAMIC_STACK.md §6.7.4: Scene change, REC off, rtk change),
 * and lifts its live hold. Before S084 only a playhead break did this, so a
 * release after REC off still wrote the end step (S084 P0 F8). A roll still
 * held starts a new span at its next hit if the new take records.
```

#### P4-8 MODIFY `liveRec_init()` — lines 349–361

ADD `memset(liveRec_roll, 0, sizeof(liveRec_roll));` after the
`liveRec_erase` memset.

#### P4-9 MODIFY `liveRec_syncPosition()` — line 374

`liveRec_endRoll(0u);` → `liveRec_endAllRolls(0u);`.

#### P4-10 MODIFY span writer in `liveRec_drainRing()` — lines 394–419, and the LC-8 mark — lines 455–459

```c
            if (b.track < NUM_TRACKS) {
                liveRec_roll_t *r = &liveRec_roll[b.track];

                if ((r->flags & (LIVEREC_ROLL_HELD | LIVEREC_ROLL_SPAN)) ==
                        (LIVEREC_ROLL_HELD | LIVEREC_ROLL_SPAN) &&
                    prev != r->step)
                    liveRec_writeStep(b.track, prev, r->note,
                        (uint8_t)((r->flags & LIVEREC_ROLL_NOTE_DEF) != 0u),
                        r->vel, 0u, r->index, 100u, r->rhythm,
                        seq_fillActive);
            }
```

and

```c
            /* LC-8: a forward-quantised roll start is reached here. */
            if ((liveRec_roll[b.track].flags & LIVEREC_ROLL_HELD) != 0u &&
                b.step == liveRec_roll[b.track].step)
                liveRec_roll[b.track].flags |= LIVEREC_ROLL_SPAN;
```

In the span comment, replace `when the rolled track's next ring entry
arrives` with `when a rolled track's next ring entry arrives (each track
with an open roll, S084 P4)`, `liveRec_rollSpan` with `its
LIVEREC_ROLL_SPAN flag`, and append before `Affiliates:`:

```c
             * The span keeps fill 100, so the next step carries it in
             * playback (S084 P1 carry rule), and the roll's rhythm (Q8).
```

#### P4-11 MODIFY `liveRec_commitHit()` — lines 1092–1121, and its callers 1171, 1174, 1428

ADD parameter `uint8_t rhythm` after `roll`. Pass it to `liveRec_writeStep()`
as the new `rhythm` argument (`roll ? rhythm : 0u`). Callers: the flam grace
(1171) and flam main (1174) and the single hit (1428) pass `0u`; the roll
start (P4-12) passes the roll's rhythm. Comment: add `rhythm` to `Inputs:`.

#### P4-12 MODIFY `liveRec_endRoll()` and ADD `liveRec_endAllRolls()` — lines 1178–1250

```c
static void liveRec_endRoll(uint8_t track, uint8_t writeEnd)
{
    liveRec_roll_t *r;

    if (track >= NUM_TRACKS)
        return;
    r = &liveRec_roll[track];
    if ((r->flags & LIVEREC_ROLL_HELD) == 0u)
        return;
    if (writeEnd && seq_isRunning()) {
        seq_track_time_t snap;
        seq_place_t pl;
        uint8_t def = (uint8_t)((r->flags & LIVEREC_ROLL_NOTE_DEF) != 0u);

        seq_captureTrackTime(track, &snap);
        liveRec_syncRing();
        if ((r->flags & LIVEREC_ROLL_HELD) == 0u)
            return;                 /* a position break ended it (C-1) */
        if (seq_placeFromSnapshot(track, &snap, liveRec_frac8(&snap), 0u,
                                  &pl)) {
            if (r->hits == 0u || (r->flags & LIVEREC_ROLL_SPAN) == 0u) {
                liveRec_writeStep(track, r->step, r->note, def, r->vel,
                                  r->startDelay, 0u, 0u, 0u, seq_fillActive);
            } else if (pl.step == r->step) {
                int16_t f = (int16_t)pl.delay - (int16_t)r->startDelay;

                liveRec_writeStep(track, r->step, r->note, def, r->vel,
                                  r->startDelay, r->index,
                                  (uint8_t)((f < 1) ? 1 : ((f > 100) ? 100 : f)),
                                  r->rhythm, seq_fillActive);
            } else {
                liveRec_writeStep(track, pl.step, r->note, def, r->vel, 0u,
                                  r->index,
                                  (uint8_t)((pl.delay < 1u) ? 1u : pl.delay),
                                  r->rhythm, seq_fillActive);
            }
        }
    }
    seq_setLiveHold(track, 0u);
    r->flags = 0u;
}

static void liveRec_endAllRolls(uint8_t writeEnd)
{
    uint8_t t;

    for (t = 0u; t < NUM_TRACKS; t++)
        liveRec_endRoll(t, writeEnd);
}
```

Comment for `liveRec_endRoll()`: keep the three release rules verbatim and
replace the surrounding text:

```c
/*
 * End one track's recorded roll (S081 §8.8, P1B item 16, P1C items 4/7;
 * S084 P4 per track, every held roll).
 *
 * What: places the release on the rolled track and writes one step:
 *  - released before the second hit, or before a forward-quantised start
 *    step was reached: the START step is rewritten as a plain trigger at
 *    its start delay (roll, fill and rhythm removed);
 *  - released on the start step: the start step keeps its delay and gets
 *    fill = release delay - start delay (1..100);
 *  - released on a later step: that step gets timing 0 (a spanned step
 *    starts at its boundary) and fill = release delay (1..99).
 * The end step keeps the roll's rate and rhythm, and the span before it
 * has fill 100, so playback carries the roll into the end step and stops
 * it at the release point (S084 P1 carry rule, user Q4). The track's live
 * hold is lifted. With writeEnd = 0 (playhead break, take boundary)
 * nothing is written. Only this track's entry is touched (S084 P0 F5).
 * Why: the Sequencer runs a roll's window from its delayed first hit
 * (seq_rollBegin()), so on the start step the fill is measured from the
 * start delay. Inputs: track, writeEnd, liveRec_roll[track], the live
 * capture. Outputs: one write, live hold off, entry cleared. Accessors:
 * liveRec_rollRelease(), liveRec_rollRateChange(), liveRec_triggerIn() (a
 * new start on a held track), liveRec_endAllRolls(). Affiliates:
 * seq_placeFromSnapshot(), seq_setLiveHold(), pat_recordStep() (fill 100
 * stored as absent, PD-1).
 */
```

Comment for `liveRec_endAllRolls()`:

```c
/*
 * End every track's open roll (S084 P4).
 *
 * What: liveRec_endRoll() for tracks 0..6. Why: a playhead break or a take
 * boundary ends all open rolls at once. Inputs: writeEnd (0 at both
 * callers: no end write). Outputs: as liveRec_endRoll(). Accessors:
 * liveRec_syncPosition(), liveRec_newTake().
 */
```

#### P4-13 MODIFY roll branch of `liveRec_triggerIn()` — lines 1365–1416

Replace both roll blocks (the `ROLL_HIT && liveRec_rollHeld` early return
and the `ROLL_START || ROLL_HIT` block) with:

```c
    if (t->kind == LIVEREC_HIT_ROLL_START || t->kind == LIVEREC_HIT_ROLL_HIT) {
        liveRec_roll_t *r = &liveRec_roll[track];

        if (t->kind == LIVEREC_HIT_ROLL_HIT &&
            (r->flags & LIVEREC_ROLL_HELD) != 0u) {
            if (r->hits < 0xFFu)
                r->hits++;            /* spanned: no per-hit write */
            return;
        }
        if (t->roll == 0u) {
            (void)liveRec_commitHit(track, &pl, t->note, t->note_is_default,
                                    t->velocity, 0u, 0u, setFil);
            return;
        }
        if ((r->flags & LIVEREC_ROLL_HELD) != 0u)
            liveRec_endRoll(track, 1u);   /* a new start ends the old roll */
        liveRec_syncRing();
        seq_setLiveHold(track, 1u);
        r->flags = (uint8_t)(LIVEREC_ROLL_HELD |
                   ((pl.rel <= 0) ? LIVEREC_ROLL_SPAN : 0u) |
                   (t->note_is_default ? LIVEREC_ROLL_NOTE_DEF : 0u));
        r->step = pl.step;
        r->startDelay = pl.delay;
        r->index = t->roll;
        r->rhythm = t->rhythm;
        r->note = t->note;
        r->vel = t->velocity;
        r->hits = 0u;
        (void)liveRec_commitHit(track, &pl, t->note, t->note_is_default,
                                t->velocity, t->roll, t->rhythm, setFil);
        return;
    }
```

```c
    /*
     * Held rolls: every one is a span, per track (S081 §8.8; S084 P4,
     * user Q7).
     *
     * What: a ROLL_HIT on a track with an open roll only counts (the span
     * writer covers its steps). A ROLL_START, or a ROLL_HIT on a track with
     * no open roll (REC armed mid-roll, a new take, or a single/flam ->
     * roll switch), opens the track's roll at this hit: the step is placed
     * and quantised like any hit and written with the rate, fill 100 and
     * the rhythm; the track is live-held so its stored triggers stay silent
     * while held. A start on a track that already holds a roll first ends
     * that roll here. A hit without a rate (roll 0) is a plain single.
     * Why: S081 recorded a roll slower than the track's step as single
     * hits, each quantised on its own, so the split depended on the step
     * scale and a triplet roll under `qnt` lost its rate (S084 P0 F6); the
     * user decided every held roll is a span (Q7, 2026-10-24: "yes, if the
     * button or MIDI input note is held"). The pending-index state of the
     * S081 rate switch is gone: each ROLL_HIT carries the held rate. Flams
     * keep F-L (recorded hit by hit).
     * Inputs: t (kind, roll, rhythm, note, velocity, note_is_default), the
     * placement pl, setFil. Outputs: liveRec_roll[track], the start-step
     * write, live hold. Affiliates: liveRec_endRoll(), liveRec_drainRing()
     * (spans), liveRec_commitHit().
     */
```

#### P4-14 MODIFY `liveRec_rollRateChange()` — lines 1432–1472

```c
void liveRec_rollRateChange(uint8_t track, uint8_t rollIndex, uint8_t rhythm)
{
    liveRec_roll_t *r;

    if (track >= NUM_TRACKS)
        return;
    r = &liveRec_roll[track];
    if (rollIndex == 0u) {
        liveRec_endRoll(track, 1u);
        return;
    }
    if ((r->flags & LIVEREC_ROLL_HELD) != 0u) {
        r->index = rollIndex;
        r->rhythm = rhythm;
    }
}
```

```c
/*
 * Overlay change while a roll gesture is held (S081 §8.8 F-C, R19; S084 P4
 * per track, every roll a span).
 *
 * What: rollIndex 0 (the overlay became single/flam or off) ends this
 * track's recorded roll at this point. Any other rate: an open roll takes
 * the new rate and rhythm from the step containing the switch (the span
 * writer and the end write use them; F-C). With no open roll nothing is
 * stored: the next ROLL_HIT carries the rate and opens the span (P4-13).
 * Why: S081 re-classified slow and fast rates here and kept a pending index
 * in a single shared slot; with spans for every rate (Q7) and per-track
 * slots (F5) only the rate and rhythm are left to update. In playback the
 * rate change is a different roll, so the switch step restarts the roll at
 * its boundary (F-R). Inputs: track, step-roll index 0..9, rhythm byte.
 * Outputs: liveRec_roll[track]. Accessors:
 * buttonHandler_overlayReconfigure(). Affiliates: liveRec_endRoll(),
 * liveRec_drainRing().
 */
```

#### P4-15 MODIFY `liveRec_rollRelease()` — lines 1474–1478

```c
void liveRec_rollRelease(uint8_t track)
{
    liveRec_endRoll(track, 1u);
}
```

```c
/*
 * A held roll gesture was released (S081; S084 P4 per track).
 *
 * What: ends THIS track's recorded roll with its end write. S081 ignored
 * the track and ended whichever roll was open (S084 P0 F5). Inputs: track.
 * Accessors: buttonHandler_overlayEndTrack(), _Reconfigure() (overlay
 * off), _Start() (a new gesture replaces a held roll). Affiliates:
 * liveRec_endRoll().
 */
```

### 7.2 `Core/Sequencer/LiveRecord.h`

#### P4-16 MODIFY hit kinds comment — lines 39–48

```c
 * ROLL_START  the first hit of a held roll (opens a span, S084 P4).
 * ROLL_HIT    later hits of a held roll, reported by the foreground drain
 *             (one report may cover several TIM3 hits). It carries the held
 *             rate; it opens a span on a track with none open (S084 P4).
```

#### P4-17 MODIFY `liveRec_trigger_t` — lines 61–82

ADD `uint8_t rhythm;` after `roll`. Comment changes: `roll (the step roll
index for a ROLL_START; 0 otherwise)` → `roll (the step-roll index on
ROLL_START and ROLL_HIT; 0 otherwise), rhythm (S084 P4: the roll's packed
velocity rhythm, the overlay pv byte, on roll kinds; 0 otherwise)`, and
`Stack only (+2 B per record)` → `Stack only (+3 B per record since S083)`.

#### P4-18 MODIFY `liveRec_rollRateChange()` prototype — line 110

```c
/* S084 P4: rate and rhythm of a held roll after an overlay change (0 ends
** it); release ends one track's roll. */
void    liveRec_rollRateChange(uint8_t track, uint8_t rollIndex,
                               uint8_t rhythm);
void    liveRec_rollRelease(uint8_t track);
```

### 7.3 `Core/MIDI/MidiNotes.c`

#### P4-19 MODIFY direct-note record — line 98

ADD `lr.rhythm = 0u;` after `lr.roll = 0u;` (a plain MIDI note is a single;
the struct is built field by field, so the new field must be set).

### 7.4 `Core/Bank/Scene/Pattern/PatternData.h` and `.c` (Q8; separable)

#### P4-20 MODIFY `pat_record_spec_t` — `PatternData.h` lines 288–304

ADD `uint8_t rhythm;` after `fill`. Comment:

```c
/*
 * Recorded step spec (S081 D-2b, §8.4; S084 P4 rhythm).
 *
 * What: the fields a live-recorded trigger writes to one step: note,
 * velocity, micro-timing, roll rate, fill and (S084) roll rhythm, plus
 * setFil (R10) and noteIsDefault (a `def` note records no note special).
 * rhythm is stored only with a roll (roll != 0) and only when nonzero;
 * otherwise the step's rhythm is cleared. The condition is kept unless
 * setFil is set. Why: an overlay roll played with a `pv` velocity rhythm
 * must replay with it (S084 P0 F7, Q8). RAM: 8 B per copy (4 staging slots
 * in PatternStackService, +4 B).
 */
```

#### P4-21 MODIFY `pat_recordStep()` prototype comment — `PatternData.h` lines 307–318

`applies the record spec (note, velocity, timing, roll, fill; clears
rhythm; …)` → `applies the record spec (note, velocity, timing, roll, fill,
rhythm with a roll; clears rhythm without one; …)`.

#### P4-22 MODIFY `pat_recordStep()` — `PatternData.c` lines 813–826 (comment), 852–853, 886–898

- REMOVE lines 852–853 (`/* Rhythm is never recorded. */` and the
  unconditional clear).
- MODIFY the roll branch (886–898):

```c
    /* Roll rate + fill + rhythm (S084 P4: rhythm only with a roll). */
    if (spec->roll == 0u) {
        flags &= (uint8_t)~(PAT_SPECIAL_ROLL_BIT | PAT_SPECIAL_FILL_BIT |
                            PAT_SPECIAL_RHYTHM_BIT);
    } else {
        sp.roll = spec->roll;
        flags |= PAT_SPECIAL_ROLL_BIT;
        if (spec->fill >= 100u) {
            flags &= (uint8_t)~PAT_SPECIAL_FILL_BIT;
        } else {
            sp.fill = spec->fill;
            flags |= PAT_SPECIAL_FILL_BIT;
        }
        if (spec->rhythm == 0u) {
            flags &= (uint8_t)~PAT_SPECIAL_RHYTHM_BIT;
        } else {
            sp.rhythm = spec->rhythm;
            flags |= PAT_SPECIAL_RHYTHM_BIT;
        }
    }
```

- In the function comment (813–826), replace `(note, velocity, timing,
  roll, fill; clears rhythm; …)` with `(note, velocity, timing, roll, fill,
  and the roll's rhythm (S084 P4; cleared on a step without a roll); …)`.

#### P4-23 MODIFY staging RAM note — `PatternStackService.c` lines 98–101

`RAM: +34 B.` → `RAM: +34 B (S084 P4: +4 B, the spec's rhythm byte in each
of the 4 slots).` No code change: the slot copies the whole spec.

---

## 8. Specification and memory updates (close-out)

| File | Location | Change |
|---|---|---|
| `knowledge_files/specification_reference/PATTERN_DYNAMIC_STACK.md` | ~405–416 (roll engine paragraph) | Time-window engine: rate in master ticks, window = interval × fill from the (deferred) fire, carry renews the window, parking, deferred-carry stretch, duck at window end. |
| same | 417–436 (§5.4 table) | Add a "PERF type" column: values 6..14 = index 1..9. |
| same | 438–444 (F-R) | Carry rule per Q4: same rate and rhythm, refused after a fill < 100 step; fill no longer has to match. |
| same | 1458–1478 (§6.7.4 Rolls) | Overlay list = the 9 step rates; every held roll is a span (Q7); per-track open rolls; rate switch per P4-14; take boundary ends rolls with no end write (F8); rhythm recorded (Q8). |
| same | §6.5 / TIM3 order notes | Live overlay ticks first, before the running gate; `seq_tick()` services them under external sync while stopped. |
| `MIDI_SPEC.md` | line 148 | `pb<`/`pb>`: 15 types: 4 single velocities, 2 flams, the 9 STEP roll rates. Note the master-tick timing and external-clock tracking. |
| `FILESYSTEM_SPEC.md` | lines 76–78 and the settings key list (~715) | `perf_btn_*` range 0..14; values from older cards are not migrated. |
| `MODULE_INTERCHANGE_SPEC.md` | line 831, 836 | Overlay recording: hits fire in TIM3, reported by `buttonHandler_overlayDrain()`; `liveRec_rollRateChange(track, idx, rhythm)`. |
| same | line 1049 | `seq_rollRhythmVelocity()` callers: `seq_rollComputeVelocity()`, `seq_processLiveTicks()`. |
| same | after line 1060 | New row: the live engine API (`seq_liveRollStart/SetRate`, `seq_liveFlamArm`, `seq_liveStop`, `seq_liveSetRhythm`, `seq_liveTakeHits`). |
| same | lines 1822–1823 | `liveRec_trigger_t` gains `rhythm`; roll classification removed; per-track rolls. |
| `STORAGE_SRAM_MANIFEST.md` | lines 94–97 | Step roll arrays: window replaces remaining and the duck arrays. |
| same | line 592 | LiveRecord roll capture: per-track `liveRec_roll[7]` (56 B). |
| same | line 612 | `bh_overlay[7]`: 28 B (held MIDI note, note, count, rate). |
| same | new row, history line | Sequencer live engine 79 B; S084 net +86 B (and the measured `.bss`). |
| `MEMORY.md` | TIM3 order bullet (~line 1865) | Live overlay ticks first (before the running gate). |
| same | open product decisions (~line 241) | Remove "whether recorded overlay rolls should carry the `pv` rhythm" (decided, Q8). |
| same | session record | S084 summary and the binding rules: carry rule Q4, spans Q7, live engine in TIM3. |

The root `S084_*.md` documents become superseded by the session handoff log
at close-out, as in earlier sessions.

---

## 9. Verification

Build after each phase: `make all && make img`; report `text/data/bss`
against the S083 close and the §2 estimate. Hardware (user), each at 120 BPM
internal and on an external MIDI clock at a tempo different from `bpm`:

| # | After | Check | Expect |
|---|---|---|---|
| V1 | P1 | Two adjacent STEP steps, roll `32`, fill 100, `scl` 1/16, 1/8, 1/32 and a triplet scale; then the same with shuffle on and with micro-timing on the second step. | An even roll with no gap or late hit at any boundary. Velocity rhythm continues across steps. |
| V1b | P1 | Same, the first step at fill 50. | The first step's roll stops (ducks) at half the step; the second step starts a fresh roll (Q4). |
| V1c | P1 | Roll step, then an empty step, then the same roll step. | The roll ends at its window; the third step starts fresh (no resumed phase). |
| V2 | P2+P3 | Track 1 STEP roll `16t` over 4 steps; track 2 the same rate held from PERF. All 9 PERF rates and both flams. | No phasing between the tracks. Every PERF type plays at its STEP rate. Labels match the STEP list. |
| V3 | P4 | Hold each of the 9 PERF rates for 2+ steps on tracks at `scl` 1/16 and 1/8, `qnt off` and `qnt 16`, with `pv` set. | Playback has the played rate and velocity shape, no hiccup on the release step, and the roll ends at the release. |
| V4 | P4 | Hold rolls on two tracks while recording; release in either order; release after REC off. | Both recorded; no stuck live hold; nothing written after REC off (F8). |
| V5 | P2–P4 | Repeat V2 and V3 with MIDI notes under a CC2 overlay. | As V2/V3. |
| V6 | P2 | Transport stopped: hold PERF rolls with internal sync, and with external sync while the clock keeps running. | They repeat at the internal tempo, and at the external tempo respectively. |

Host (optional, scratchpad only, no new installed utility): compile
`seq_rollBegin()`/`seq_processRollTicks()` with stubs and print hit ticks
for the F2 trace before and after P1.

---

## 10. Implementation log

### 2026-10-24 — P1 landed

- P1-1..P1-16 applied in `Core/Sequencer/sequencer.c` only, with the
  schedule's comment blocks in place (roll state block, `seq_rollClearTrack`,
  carry rule, `seq_rollBegin`, `seq_processRollTicks`, the F-R cut, the
  one-step carry eligibility, the deferred-carry stretch, and the updated
  `seq_rollPpq`/scheduler comments).
- `make all && make img` passes. `text=608,736`, `data=424`, `bss=430,096`
  (`bss` −16 vs the S083 close; the §2 P1 estimate is −14, the rest is
  alignment). Flash payload 609,160 B, 144,504 B free.
- One indentation warning in `seq_setTrackPlayedScene()` was introduced by
  the two-line helper call next to the tab-indented menu-override block; it
  is fixed by bracing the `track >= 5u` clause.
- No `seq_rollRemaining`, `seq_rollDuck*` or `seq_rollArmDuck` reference
  remains in code (the old names appear only in the explanatory comment).

### 2026-10-24 — P2 + P3 landed

- P2-1..P2-8 in `sequencer.c`/`sequencer.h`: live engine state (79 B),
  `seq_processLiveTicks()`, the six `seq_live*` calls with PRIMASK sections,
  the live tick call before the running gate, the `seq_tick()` external-sync
  stopped path, and the updated trigger/rhythm comments.
- P2-9..P2-20 in `buttonHandler.c`: `bh_overlay[7]` is 4 B per track
  (heldMidiNote, note, count, rate), `BH_FLAM_TICKS`/`BH_OVERLAY_TYPE_*`,
  the ms helpers and both rate tables are gone, `overlayRhythm()`/
  `overlayDrain()` added, and start/report/reconfigure/end/tick now use the
  live engine. A track-range guard was added at the top of
  `buttonHandler_overlayStart()` to keep GCC's bounded-array analysis quiet
  on the smaller entry.
- P3-1..P3-6: `perfBtnTypeNames` is the 15-type list (`vmx v10 v64 v32 flm
  fls` + the nine STEP labels), the menu clamp is 0..14, the renderer reads
  the count from `[0][0]`, and the ParameterArray/buttonHandler/step-name
  comments are updated. P3-7 needed no settings code (Q2: no migration).

### 2026-10-24 — P4 landed

- P4-1..P4-15 in `LiveRecord.c`/`LiveRecord.h`: `liveRec_roll[7]` (8 B per
  track, flags/step/index/startDelay/note/vel/hits/rhythm), the payload
  scalars and the single-slot state removed, `liveRec_endRoll(track,
  writeEnd)` + `liveRec_endAllRolls()`, per-track span/start/end writes,
  `liveRec_newTake()` ends every roll with no end write, and
  `liveRec_rollRateChange(track, index, rhythm)` / `liveRec_rollRelease(
  track)` are per track. `liveRec_rollPpqTicks()` is deleted.
- P4-16..P4-19: `liveRec_trigger_t.rhythm`, the three-argument rate-change
  prototype, and `lr.rhythm = 0` for a plain MIDI note.
- P4-20..P4-23: `pat_record_spec_t.rhythm`, `pat_recordStep()` stores the
  rhythm only with a roll and clears it without one, and the staging RAM
  note is updated. The four `patSvc_recordStage` copies grow by 1 B each.

### 2026-10-24 — build and documentation

- `make all && make img` passes with the final code: `text=609,128`,
  `data=424`, `bss=430,200` (net +88 B measured against the S083 close;
  §2 estimated +86 B before alignment). Flash payload 609,552 B of
  753,664 B, 144,112 B free. No new compiler warning comes from the
  changed files (the remaining warnings are pre-existing).
- §8 applied: `PATTERN_DYNAMIC_STACK.md` (§6 roll window/carry/live engine,
  rate table PERF column, reset rule, §6.7.4 rolls),
  `MIDI_SPEC.md` (`pb<`/`pb>` 15 types and master-tick timing),
  `FILESYSTEM_SPEC.md` (`perf_btn_*` 0..14, no migration),
  `MODULE_INTERCHANGE_SPEC.md` (overlay table, overlay recording, live API
  row, trigger-reset row, LiveRecord rows and state),
  `STORAGE_SRAM_MANIFEST.md` (S084 build row and per-owner entries),
  `BANK_PRESET_ARCHITECTURE.md` (S080 Global range), and `MEMORY.md`
  (current source/build, S084 summary and binding rules, TIM3 order, open
  decisions).
- Not done: hardware checks V1..V6 (§9) and the optional host scratch
  harness for the F2 trace. The user runs the hardware matrix.

---

## 11. Post-implementation assessment (2026-10-24)

**Scope:** the working tree on `dev-ph6-midi` against base `59406a4`, read
entry by entry against §4–§8, plus a rebuild and a read of the runtime
paths around the changed code. No hardware was run.

### 11.1 Conformance

| Area | Entries | Result |
|---|---|---|
| P1 step roll engine (`sequencer.c`) | P1-1..P1-16 | All present, as specified. No remaining reference to `seq_rollRemaining`, `seq_rollDuck*` or `seq_rollArmDuck()` (one historical mention in a comment). |
| P2 live engine (`sequencer.c/.h`, `buttonHandler.c`) | P2-1..P2-20 | All present, as specified. One addition: a `track >= NUM_TRACKS` guard in `buttonHandler_overlayStart()` (harmless). `buttonHandler_overlayDivisionMs()`/`SixteenthMs()` are gone; no caller remains. |
| P3 one roll list | P3-1..P3-7 | All present. The menu renderer reads the count from `perfBtnTypeNames[0][0]`; the clamp is 14; `filesystem.c` is unchanged, as planned. |
| P4 LiveRecord / PatternData / MidiNotes | P4-1..P4-23 | All present, as specified. Every `liveRec_writeStep()`/`liveRec_commitHit()` caller passes the new `rhythm` argument; `pat_record_spec_t` has no other user that builds it field by field. |
| §8 documents | — | Applied. Spot-checked `PATTERN_DYNAMIC_STACK.md` §6 and §6.7.4, `MEMORY.md` (TIM3 order, S084 summary); both match the code. |

**Build (re-run for this review):** `make all` after touching the changed
`.c` files gives `text=609,128`, `data=424`, `bss=430,200`, the same as the
implementation log. Against the S083 close that is `bss` +88 B: the §2
estimate of +86 B plus 2 B of alignment. Under the RAM policy the user
should acknowledge the measured +88 B. No warning comes from a changed line;
`PatternData.c:201` (`-Waddress-of-packed-member`) is on an unchanged line
and pre-existing.

**Binding rules:** LiveRecord is still never called from TIM3 (the live
engine only writes the mailbox; `buttonHandler_overlayDrain()` reports). The
reset rule holds: hit #1 and the flam main reset, roll repeats and carried
steps do not, and a carry calls `voiceControl_resetOnly()`. F-R holds: the
cut block is unchanged apart from clearing the window. The TIM3 order is as
documented, with live ticks first.

### 11.2 Findings

| # | Finding | Severity | Status |
|---|---|---|---|
| A1 | Under external sync, master ticks arrive in bursts, so live rolls faster than the clock grid bunch or collapse. | **High** for externally synced performance (new for live rolls; pre-existing for step rolls) | Open; needs a decision |
| A2 | Every roll is now a span, so a slow roll released before its second hit leaves span steps that replay as a roll that was never played. | **High** for recording | Open; small fix proposed |
| A3 | Hits drained during an overlay switch are reported with the *new* overlay's rhythm. | Low | Optional |
| A4 | The comment `RAM about 14 B` in the LiveRecord P1B/P1C block was not reduced by the 3 B removed (P4-3). Also, `PATTERN_DYNAMIC_STACK.md` §6.7.4 cites itself ("PATTERN_DYNAMIC_STACK §6.7.4"). | Nit | Optional |

#### A1 — External clock ticks arrive in bursts

- **Where:** `seq_resetDeltaAndTick()` (MIDI clock, called from
  `clockSync.c` while running) runs `SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK` = 4
  scheduler ticks back to back on each incoming clock.
  `seq_triggerNextMasterStep()` (pulse input) runs `stepSize × 3` ticks
  back to back: **24 ticks per pulse** at the default `PRE_4_PPQ`. The
  live engine (P2-4) is serviced inside those ticks, so all hits that fall
  inside one burst fire at the same instant.
- **Effect under MIDI clock:**
  - rates that are a multiple of 4 ticks (1/8, 8tr, 16, 16t, 32, 32t, 64t)
    stay even;
  - `64` (6 ticks) alternates 1- and 2-clock gaps (±25 % of the interval);
  - `128` (3 ticks) fires two hits in the same burst every third clock, and
    the second retrigger replaces the first in the same audio block, so hits
    are lost.
- **Effect under pulse sync (4 PPQ):** every roll faster than 1/16
  collapses into one burst per pulse. The same applies to shuffle and
  micro-timing deferrals.
- **History:** this already applied to step rolls (and to every sub-clock
  timing) since S081 made the scheduler 96 PPQ. It is new for overlay rolls,
  which S080–S083 timed with a millisecond timer: evenly spaced, though at
  the wrong tempo. It does not affect internal sync, which ticks one at a
  time from `seq_tick()`.
- **Options:**
  1. **Interpolate external ticks (recommended, separate plan).** On each
     MIDI clock (or pulse), process only the ticks owed. Let `seq_tick()`'s
     tempo timer, which already follows `seq_tempo` from the measured clock,
     run the intermediate ticks between clocks, and catch up at the next
     clock if behind. This fixes step rolls, live rolls, shuffle and
     micro-timing under external sync together. It changes the external-sync
     timing core, so it needs its own plan and test matrix.
  2. **Narrow:** under external sync, always service the live engine from
     `seq_tick()`'s tempo timer, not from the scheduler bursts. Live rolls
     become even, but they lose phase lock to the clock and no longer match
     step rolls or recordings exactly under external sync.
- **Verification hook:** §9 V6 and the external-clock half of V2 will show
  this on `64`/`128` (MIDI) and on any roll (pulse).

#### A2 — Slow roll released before its second hit replays as a roll

- **Where:** the span writer in `liveRec_drainRing()` (P4-10) writes each
  ended step after the start as a span (roll, fill 100) with no regard to
  `r->hits`. `liveRec_endRoll()` (P4-12) then applies the S081 rule
  "released before the second hit → the start step becomes a plain
  trigger", which rewrites **only the start step**.
- **Why it is new:** before S084 only fast rolls (interval shorter than the
  step) were spanned, and for those the second hit always lands before the
  first span step ends. Q7 spans slow rolls too, where a span step can end
  before the second hit.
- **Scenario:** track at 1/16 (24 ticks), overlay `1/8` (48 ticks), REC on.
  Press, so hit 0 lands on step n. Release 40 ticks later, after step n+1
  has ended and before the second hit is due at 48. Steps n+1.. were
  written as spans, and the start step n is rewritten as a plain hit. In
  playback step n plays one hit, then step n+1 is a roll that does not carry
  (the previous step has no roll). It fires a new hit #1 at its boundary and
  rolls on through the remaining span steps. That is a roll that was never
  played. At 120 BPM any tap of about 125–250 ms that crosses a step
  boundary does this; with `8tr` it happens whenever the press lands late in
  a step.
- **Proposed fix (one condition):** in `liveRec_endRoll()`, use the
  plain-trigger rewrite only when no span can exist yet:

  ```c
  if ((r->flags & LIVEREC_ROLL_SPAN) == 0u ||
      (r->hits == 0u && pl.step == r->step)) {
      /* plain trigger at the start (unchanged) */
  ```

  A release on a later step then takes the normal end write: that step
  gets timing 0 and fill = the release offset. With the P1 time window,
  playback carries hit 0's roll through the spans and closes the window at
  the release, before the next hit was due, so it plays exactly the one hit
  that was heard. Side effect: the duck at the window end cuts that hit's
  tail at the release point. Every recorded roll end already does this
  (fill < 100). A quantised start can still shift the window enough to add
  or drop one hit at the end, as for any recorded roll.
- **Alternative:** write no span before the first `ROLL_HIT` and backfill
  the passed steps then. This is exact, but it needs a per-track list of
  pending steps (up to 8 steps before the second hit of a `1/8` roll on the
  finest scale), so more RAM and more code. Not recommended.
- **Verification hook:** add to V3: a short `1/8` tap across one step
  boundary on a 1/16 track must replay as one hit.

#### A3 — Rhythm on reports drained during an overlay switch

`buttonHandler_setOverlay()` and the BAR press set `bh_overlayActive` before
`buttonHandler_overlayReconfigure()` drains, so hits fired under the old
overlay are reported with the new overlay's `pv` byte. This matters only
when such a `ROLL_HIT` opens a span (no span open), and then only for the
stored `rtm` of that span's start. Optional fix: pass the old overlay's
rhythm into the drain from `buttonHandler_overlaySwitchHeld()`, or drain all
held entries before `bh_overlayActive` changes.

#### A4 — Nits

- `LiveRecord.c`, the S081 P1B/P1C state comment: `RAM about 14 B` should
  read about 11 B (P4-3 removed 3 B).
- `PATTERN_DYNAMIC_STACK.md` §6.7.4, the last Rolls bullet: drop the
  self-citation "PATTERN_DYNAMIC_STACK §6.7.4".

### 11.3 Verdict

The implementation follows the schedule and builds clean. **A2 should be
fixed before the hardware recording checks (V3/V4).** It is a one-condition
change in `liveRec_endRoll()`, plus a comment and a V3 case. **A1 needs a
user decision:** the recommended interpolation of external ticks is a
separate piece of work. Until then, V2/V6 under external sync should be
expected to show uneven `64`/`128` live rolls (MIDI clock) and collapsed
fast rolls (pulse). Under internal sync, V1, V2, V6 (stopped) and the
playback half of V5 can run now; run V3, V4 and the recording half of V5
after the A2 fix. A3 and A4 are optional.

---

## 12. Assessment fixes applied (2026-10-24)

The user approved the §11 recommended actions. Four targeted fixes were
made; line numbers are in the working tree after the fixes.

### 12.1 A1 — External clock ticks interpolated (option 1)

`Core/Sequencer/sequencer.c`:

| Action | Where | Change |
|---|---|---|
| ADD | after `seq_deltaT` (line 792) | `static volatile uint8_t seq_extTicksOwed`: the ticks of the current external clock period not yet run, with a full comment block (what, why, inputs, outputs, RAM, affiliates). |
| ADD | `seq_extClockTicks()` (line 6482), before `seq_triggerNextMasterStep()` | Catches up any ticks still owed, runs the tick that belongs on this edge, leaves `ticks − 1` owed, and re-anchors the tempo timer (`seq_lastTick`, `seq_calcDeltaT(seq_tempo)`). Each period still runs exactly the same number of ticks; only their timing changes. |
| MODIFY | `seq_triggerNextMasterStep()` | The back-to-back loop is replaced by `seq_extClockTicks(stepSize × 3)`. |
| MODIFY | `seq_resetDeltaAndTick()` | The 4-tick loop is replaced by `seq_extClockTicks(SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK)`. |
| MODIFY | `seq_tick()` (lines 6557–6572) | Under external sync while running, each tempo-timer period runs one owed tick. Internal sync drops any owed ticks. External sync while stopped still runs only the live engine (P2-5). |
| MODIFY | `seq_setRunningEx()`, `seq_continueRunning()`, `seq_resetToPatternStart()` | Clear `seq_extTicksOwed`, so a start, stop, Continue or external reset never replays the old period's remainder. |

**Behaviour.**
- **MIDI clock:** each clock runs its first tick on the edge, and the other
  three follow at the measured 96-PPQ period.
- **Pulse input (default 4 PPQ):** the pulse runs its first tick, and the
  other 23 follow the same way.
- **Tempo estimate off:** if the clock speeds up beyond the estimate, the
  next edge catches up the remainder (at most `ticks − 1`). If it slows
  down, the interpolation stops at the period's tick count and waits for the
  edge.
- **Master boundary:** `seq_resetStepScheduler()` deliberately does *not*
  clear the owed ticks. It also runs at the master-boundary Pattern switch
  inside a tick, and the old loop kept running the rest of the period there
  too.

**Effect.** Live and step rolls, shuffle and micro-timing are spread evenly
within each external clock period. Under MIDI clock, `64` and `128` rolls
are even. Under pulse sync, rolls finer than the pulse no longer collapse.
LiveRecord placement also gains real sub-clock tick periods
(`seq_ppqPeriodUs` was near 0 for the 3 burst ticks).

**RAM:** +1 B SRAM1 (`seq_extTicksOwed`). This is a new allocation under
the RAM policy, part of the approved option 1. The measured `.bss` did not
change: the byte fits existing padding.

**Docs:**
- `MODULE_INTERCHANGE_SPEC.md`: the `seq_resetDeltaAndTick()` and
  `seq_triggerNextMasterStep()` rows describe the interpolation.
- `MEMORY.md` "MIDI / Clock Reminders": a new binding bullet; do not return
  to back-to-back ticks.
- The `triggerJacks.c` comment ("derives intermediate substeps from BPM
  until the next master pulse arrives") is now accurate and was left
  unchanged.

### 12.2 A2 — Slow roll released before its second hit

`Core/Sequencer/LiveRecord.c`, `liveRec_endRoll()`:

- **MODIFY:** the plain-trigger rewrite condition is now
  `(flags & LIVEREC_ROLL_SPAN) == 0 || (hits == 0 && pl.step == r->step)`.
  It was `hits == 0 || (flags & LIVEREC_ROLL_SPAN) == 0`.
- **ADD:** an in-place comment block explaining the rule.
- **MODIFY:** the first release rule in the function's header comment.

A release on a later step before the second hit now takes the normal
later-step end write. The P1 window carries hit 0 through the span steps
and closes at the release, so playback is the one hit that was heard (its
tail ducked at the release, as at any recorded roll end). Before the fix,
the spans replayed as a roll that was never played.

### 12.3 A3 — Hits drained under the old overlay

`Core/Hardware/frontPanel/buttonHandler.c`:

- **ADD** `buttonHandler_overlayDrainAll()` (line 1003, with a comment
  block).
- **ADD** a call to it immediately before each change of `bh_overlayActive`:
  - `buttonHandler_setOverlay()` (CC2), line 1343;
  - the BAR1 press, line 3177;
  - the BAR2 press, line 3216;
  - the event-ring overflow recovery, line 3816.

Hits played under the old overlay are now reported with the old overlay's
`pv` rhythm. The re-configure that follows drains again and finds nothing
from the old overlay.

### 12.4 A4 — Nits

- `LiveRecord.c`, the S081 P1B/P1C state comment: `RAM about 14 B` is now
  `about 11 B (14 B before S084 P4 moved 3 B of roll state)`.
- `PATTERN_DYNAMIC_STACK.md` line 1538: the self-citation is removed.

### 12.5 Build and verification

- **Build:** `make all && make img` pass. `text=609,288` (+160 B over
  §10/§11), `data=424`, `bss=430,200` (unchanged). Image
  `build/LXRV2_lxr02.img` is 609,712 B. No warning or error comes from the
  three changed source files.
- **Verification additions to §9:**
  - **V3 (A2):** on a 1/16 track, tap overlay `1/8` for about 150 ms across
    one step boundary with REC on. It must replay as exactly one hit.
  - **V2/V6 under external sync (A1):** `64` and `128` live rolls must be
    even under MIDI clock. Under 4-PPQ pulse sync, every roll rate, shuffle
    and micro-timing must play evenly within each pulse. Repeat V1 under
    MIDI clock to confirm step rolls carry evenly there too.
  - **External sync start/stop/Continue:** no extra or missing tick at the
    first clock after a start or Continue (`seq_extTicksOwed` is cleared).
- **Not run:** hardware (the user's matrix §9 plus the additions above).
