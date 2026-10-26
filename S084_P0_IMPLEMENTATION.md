# S084 P0 — Roll Fix-up: Implementation Schedule

**Session:** 084, phase P0
**Status:** **NOT YET IMPLEMENTED** on the current repository. The schedule
is complete and ready to follow from §4. The §10 log is empty.
**Branch:** `prime` at `debbe72f`. Its tree is identical to `59406a4`, the
base this schedule was written against (checked 2026-10-26: `git diff
59406a4 HEAD` is empty).
**Date:** 2026-10-24; revised for re-implementation 2026-10-26
**Assessment and plan:** `S084_P0_ROLL_FIXUP.md` (findings F1–F8, phases P1–P4)
**Reference build:** `Helicase-dev-ph7-spike/` at `26fb7c3` holds an earlier,
abandoned implementation of this schedule (see §11 and
`Helicase-dev-ph7-spike/S084_POST-MORTEM.md`). Every entry here was checked
against that code, and its approved assessment fixes A1–A4 are folded into
the entries.

---

## 0. How to use this schedule

- **Line numbers are at `59406a4`**, which equals the current `prime` tree
  (every anchor was re-checked on 2026-10-26). Within one file, apply the
  changes from the highest line down, or find each change by its **anchor**
  (the function or symbol named in the entry). Earlier edits shift later
  lines.
- **The spike is reference only.** To see an entry as it was built, run
  `git -C Helicase-dev-ph7-spike diff 59406a4 26fb7c3 -- <file>`.
  `sequencer.c/.h`, `LiveRecord.c/.h`, `buttonHandler.c`, `MidiNotes.c`,
  `PatternData.c/.h` and `PatternStackService.c` carry only P0 hunks there.
  `MenuText.h`, `menu.c`, `ParameterArray.h`, the specifications and
  `MEMORY.md` also carry S084 voice work (P1–P3), which must **not** be
  copied. Take only the hunks this schedule names (§8 lists the
  documentation hunks). Where an entry and the spike differ, the entry
  wins: §11.2 lists the differences.
- **Comments must not cite entry IDs** (`P1-12`, `P4-13`, …). This document
  is deleted at close-out, so in-code text names functions instead. Phase
  tags (`S084 P1`) are fine.
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
  introduces them, and `S084 P0 A1`…`A4` for the assessment fixes.

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
- **External clock (assessment A1, §5.5):** after this work an external clock
  edge runs only the first tick of its period, and `seq_tick()` interpolates
  the rest. Never go back to running a period's ticks back to back.
- **RAM Allocation Approval Policy:** the §2 allocations are approved by the
  user (2026-10-26). Any allocation beyond §2 needs a new acknowledgement.

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
| A1 | Interpolate external clock ticks (assessment option 1), so step and live rolls, shuffle and micro-timing are even under MIDI clock and pulse sync. | User approved the assessment actions, 2026-10-24 | P2-5, P2-23..P2-28 |
| A2 | A held roll released on a later step before its second hit takes the normal end write, not the plain-trigger rewrite. | User approved, 2026-10-24 | P4-12 |
| A3 | Drain held entries before `bh_overlayActive` changes, so hits keep the old overlay's rhythm. | User approved, 2026-10-24 | P2-21, P2-22 |
| A4 | Comment and specification nits. | User approved, 2026-10-24 | P4-3, §8 |

The five recommendations are applied unless the user objects. Q8 sits in
its own entries so it can be dropped without touching the rest. A1–A4 come
from the assessment of the spike build (§11.3). They were approved there,
and here they are ordinary entries.

---

## 2. RAM (approved by the user, 2026-10-26)

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
| P2-23 | `sequencer.c` external clock | `seq_extTicksOwed` u8 (A1) | +1 |
| | | **Net (before alignment padding)** | **+87** |

Stack: `liveRec_trigger_t` +1 B per record (P4-17), and
`pat_record_spec_t` +1 B per local copy. Alignment may add up to 3 B per
array. The link map after each phase gives the exact `.bss` delta, to be
reported against the S083 close (`text=608,744`, `data=424`,
`bss=430,112`). Freed bytes are not reused for anything else.

**Approved:** the user approved these allocations (+87 B before alignment)
for this re-implementation on 2026-10-26. They were first acknowledged for
the spike build on 2026-10-24, where `bss` measured +88 B and
`seq_extTicksOwed` fitted existing padding. The approval covers only the
rows above. Report the measured delta in §10; an allocation not listed here
still needs its own acknowledgement.

---

## 3. Order of work and checkpoints

1. **P1** (sequencer.c only). Build `make all && make img`. Host check H1
   (§9). Hardware check V1 under internal sync. Run the external-clock
   half of V1 after P2, which carries the A1 interpolation.
2. **P2 + P3** together, since both touch the overlay type tables. P2
   includes the A3 drain (P2-21, P2-22) and the A1 external-clock
   interpolation (§5.5). Build. Hardware checks V1 (external clock), V2,
   V5, V6, V7.
3. **P4** (LiveRecord, PatternData, MidiNotes). Build. Hardware checks
   V3, V4, V5.
4. **§8** specification and memory updates at close-out.

Each phase builds on its own: P2 compiles against P1's sequencer, and P4
against P2's `seq_live*` API. P2-12 sets `t.rhythm`, and P2-17 calls
`liveRec_rollRateChange()` with three arguments. So the **P2 build must
also take**:
- P4-17 (the `liveRec_trigger_t.rhythm` field);
- P4-18 (the three-argument prototype);
- P4-19 (`lr.rhythm = 0u` in `MidiNotes.c`, so the new field is never left
  uninitialised);
- an interim three-argument signature on the *existing*
  `liveRec_rollRateChange()` definition in `LiveRecord.c`: add `uint8_t
  rhythm` and `(void)rhythm;`, and keep the S081 body.

Do not pull P4-14's new body into P2, because it needs P4-1 and P4-12.
P4-14 replaces the interim body in the P4 build. Until P4 lands,
LiveRecord still records with the S081 single-slot logic, so run the
recording checks (V3, V4, the recording half of V5) only after P4.

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
 * event (the carry-eligibility check in seq_advanceTrackStep()).
 * seq_rollTickAccum counts the 96-PPQ ticks to the next retrigger.
 * seq_rollWindow counts the master ticks left in which retriggers may fire:
 * the step interval x fill / 100 from the step's (possibly deferred) fire,
 * replaced by each carrying step and extended over a carrying step's
 * deferral (seq_advanceTrackStep()). seq_rollVelPos is the position in the
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
    if (track >= 5u) {
        seq_rollClearTrack((uint8_t)((track == 5u) ? 6u : 5u));
    }
```

Keep the braces. Without them GCC warns about misleading indentation
against the tab-indented menu-override block that follows (seen in the
spike).

### P1-6 MODIFY `seq_selectActivePattern()` roll clear — lines 2123–2133

Replace the comment line and the ten `memset` lines with:

```c
	/* S079 P2 fix 4 / S084 P1: a Scene change ends every running step roll
	 * (its window, duck and carry with it). Live overlay rolls continue. */
	for (uint8_t track = 0u; track < NUM_TRACKS; track++)
		seq_rollClearTrack(track);
```

The function has no function-scope loop variable, and it already uses
loop-scoped `uint8_t track` declarations (`-std=gnu11`), so declare one
the same way.

### P1-7 MODIFY `seq_setStepIndexToStart()` roll clear — lines 6575–6584

Replace the ten roll lines, the seven `seq_roll…[i] = 0u;` lines and the
three `seq_rollDuck…[i] = 0u;` lines, with `seq_rollClearTrack(i);`. Keep the
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

This is the final form, including the A1 interpolation branch (§5.5).
`seq_extTicksOwed` is added by P2-23.

```c
		if (!seq_getExtSync()) {
			seq_extTicksOwed = 0u;
			seq_processSchedulerTick();
		} else if (!seq_running) {
			seq_processLiveTicks();
		} else if (seq_extTicksOwed != 0u) {
			seq_extTicksOwed--;
			seq_processSchedulerTick();
		}
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
		 *
		 * External sync, transport running (S084 P0 A1): the clock edge ran
		 * the period's first tick (seq_extClockTicks()); each tempo-timer
		 * period here runs one of the ticks still owed, so the period's
		 * ticks are spread evenly instead of bunched on the edge. Internal
		 * sync never owes ticks; a switch to internal drops any left over.
		 */
```

The internal-MIDI-clock send block that follows (`if(!seq_getExtSync())
//only send internal MIDI clock…`) is unchanged.

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
    bh_overlay_track_t *e;
    uint8_t type = (which == 1u) ? parameter_values[PAR_PERF_BTN_LT]
                                 : parameter_values[PAR_PERF_BTN_RT];
    uint8_t pitch = (uint8_t)(note & 0x7Fu);
    uint8_t isDef = (uint8_t)((note & BH_OVERLAY_NOTE_DEFAULT) != 0u);

    if (track >= NUM_TRACKS)
        return;
    e = &bh_overlay[track];
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

The `track >= NUM_TRACKS` guard is in the spike build. Without it, GCC's
bounded-array analysis warns about the smaller 4-byte entry. Callers already
pass a valid track.

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

#### P2-21 ADD `buttonHandler_overlayDrainAll()` — right after `buttonHandler_overlayDrain()` (P2-14), before `buttonHandler_overlayStart()` (assessment A3)

```c
static void buttonHandler_overlayDrainAll(void)
{
    uint8_t track;

    for (track = 0u; track < NUM_TRACKS; track++)
        buttonHandler_overlayDrain(track);
}
```

```c
/*
 * Report every track's live hits under the current overlay (S084 P0 A3).
 *
 * What: buttonHandler_overlayDrain() for tracks 0..6. Called right before
 * bh_overlayActive changes (BAR press, CC2, event-ring overflow). Why: the
 * drain reports a roll hit with the active overlay's `pv` rhythm; draining
 * after the change gave hits played under the old overlay the new overlay's
 * rhythm, which a span opened by such a hit would then store. Inputs: the
 * entries, bh_overlayActive (still the old value). Outputs: LiveRecord
 * reports. Context: foreground. Accessors: buttonHandler_setOverlay(), the
 * BAR1/BAR2 presses in processPress(), buttonHandler_processEvents()
 * overflow recovery. Affiliates: buttonHandler_overlayReconfigure() (which
 * drains again, now with nothing left from the old overlay).
 */
```

#### P2-22 ADD drain-all calls before each `bh_overlayActive` change (assessment A3)

Insert the line immediately **before** each assignment:

| Anchor (line at `59406a4`) | Line to insert |
|---|---|
| `buttonHandler_setOverlay()`: `bh_overlayActive = which;` (1283) | `buttonHandler_overlayDrainAll();     /* S084 P0 A3: old rhythm */` |
| `processPress()` BAR1: `bh_overlayActive = (bh_overlayActive == 1u) ? 0u : 1u;` (3116) | `buttonHandler_overlayDrainAll();  /* S084 P0 A3: old rhythm */` |
| `processPress()` BAR2: `bh_overlayActive = (bh_overlayActive == 2u) ? 0u : 2u;` (3154) | `buttonHandler_overlayDrainAll();  /* S084 P0 A3: old rhythm */` |
| `buttonHandler_processEvents()` overflow recovery: `bh_overlayActive = 0u;` (3753, after `buttonHandler_loopForceRelease();`) | `buttonHandler_overlayDrainAll();     /* S084 P0 A3: old rhythm */` |

Only these four sites assign `bh_overlayActive`, besides its definition
(`grep -n 'bh_overlayActive =' buttonHandler.c`). `buttonHandler_overlayReconfigure()` still drains each
held entry itself (P2-17). After the drain-all it finds no hits left from
the old overlay.

### 5.4 `Core/Hardware/frontPanel/buttonHandler.h`

No API change. In the overlay comment (lines 124–141), no edit is
required. The MIDI note-off text ("a flam's main hit still plays") stays
true.

### 5.5 External clock tick interpolation (assessment A1) — `Core/Sequencer/sequencer.c`

**Why this is part of P2.** Under external sync the scheduler ran all the
ticks of one clock period back to back: 4 per MIDI clock, and `stepSize ×
3` per jack pulse, which is 24 at the default `PRE_4_PPQ`. P2 moves live
rolls onto those ticks, so without this section live `64` rolls jitter
under MIDI clock, `128` rolls lose hits, and every roll finer than the
pulse collapses under pulse sync. Step rolls, shuffle and micro-timing
already had this problem since S081. This section fixes all of them
together (§11.3 A1). Each period still runs exactly as many ticks as
before; only their timing within the period changes. Internal sync is not
affected.

#### P2-23 ADD `seq_extTicksOwed` — after the `seq_deltaT` declaration (lines 691–693), before `seq_delayedSyncStepFlag` (line 694)

```c
static volatile uint8_t seq_extTicksOwed = 0u;
```

```c
/*
 * External-clock ticks still to interpolate (S084 P0 A1).
 *
 * What: the 96-PPQ scheduler ticks of the current external clock period
 * (MIDI clock: 4 per clock; pulse input: stepSize x 3 per pulse) that have
 * not been processed yet. A clock or pulse processes its first tick at once
 * (seq_extClockTicks()); seq_tick()'s tempo timer, whose period follows the
 * measured external tempo (seq_tempo), processes the rest one by one; the
 * next clock first catches up any tick still owed, so no clock period ever
 * runs more or fewer ticks than before. Why: the scheduler used to run all
 * ticks of a clock back to back, so every hit inside one clock period fired
 * at the same instant: live rolls of 64/128 bunched or lost hits under MIDI
 * clock, and under pulse sync (24 ticks per pulse at 4 PPQ) every roll,
 * shuffle and micro-timing finer than the pulse collapsed. Inputs: written
 * in TIM3 (clock/pulse handlers, seq_tick()); cleared by transport
 * start/stop/continue and the external reset (foreground or TIM3, a single
 * byte store). Output: interpolated seq_processSchedulerTick() calls. RAM:
 * 1 B SRAM1 static. Affiliates: seq_resetDeltaAndTick(),
 * seq_triggerNextMasterStep(), seq_tick(), seq_calcDeltaT().
 */
```

#### P2-24 ADD `seq_extClockTicks()` — after `seq_setDeltaT()` (line 6131), before `seq_triggerNextMasterStep()` (line 6133)

```c
static void seq_extClockTicks(uint16_t ticks)
{
	while (seq_extTicksOwed != 0u) {
		seq_extTicksOwed--;
		seq_processSchedulerTick();
	}
	if (ticks != 0u) {
		seq_processSchedulerTick();
		seq_extTicksOwed = (uint8_t)((ticks > 256u) ? 255u : (ticks - 1u));
	}
	seq_lastTick = systick_ticks;
	seq_calcDeltaT(seq_tempo);
}
```

```c
/*
 * Run one external clock period's scheduler ticks, interpolated (S084 P0 A1).
 *
 * What: first processes every tick still owed from the previous period
 * (catch-up when the clock ran faster than the measured tempo), then the
 * tick that belongs on this clock edge, and leaves the remaining ticks - 1
 * owed for seq_tick() to process one per tempo-timer period. The timer is
 * re-anchored on this edge (seq_lastTick, seq_calcDeltaT()), so the
 * interpolated ticks are spaced by the measured external tempo from here.
 * Each period therefore runs exactly `ticks` scheduler ticks, as before;
 * only their timing within the period changes. Why: back-to-back ticks put
 * every hit, roll retrigger, shuffle and micro-timing inside one clock
 * period on the same instant (S084 P0 assessment A1). Inputs: ticks (4 per
 * MIDI clock; stepSize x 3 per pulse, at most 96). Outputs:
 * seq_processSchedulerTick() calls, seq_extTicksOwed, the tempo timer
 * anchor. Context: TIM3 (clockSync via midiTransport_processRealtime(),
 * triggerJacks_tick()); runs only while the transport runs. Accessors:
 * seq_resetDeltaAndTick(), seq_triggerNextMasterStep(). Affiliates:
 * seq_tick() (interpolation), seq_setRunningEx()/seq_continueRunning()/
 * seq_resetToPatternStart() (clear the owed ticks).
 */
```

#### P2-25 MODIFY `seq_triggerNextMasterStep()` and `seq_resetDeltaAndTick()` — lines 6133–6167

- `seq_triggerNextMasterStep()`: replace the body after its comment (the
  `ticks` loop, `seq_lastTick = …` and `seq_calcDeltaT(…)`, lines 6145–6150)
  with:

  ```c
  	seq_extClockTicks((uint16_t)((uint16_t)stepSize * 3u));
  ```

  and append to its comment, after `MIDI, and pulse sync.`:

  ```c
  	 * S084 P0 A1: the ticks are spread over the pulse
  	 * period (seq_extClockTicks()), not run back to back.
  ```

- `seq_resetDeltaAndTick()`: delete `uint8_t i;`, replace the `for` loop
  and the two timer lines with:

  ```c
  	seq_extClockTicks(SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK);
  ```

  and append to its comment, after `previous 32-steps-per-beat grid.`:

  ```c
  	 * S084 P0 A1: the first tick runs on the
  	 * clock, the other three are interpolated by seq_tick().
  ```

#### P2-26 MODIFY `seq_resetToPatternStart()` — after `seq_delayedSyncStepFlag = 0;` (line 6175)

```c
	seq_extTicksOwed = 0u;          /* S084 P0 A1: no tick of the old period */
```

#### P2-27 MODIFY `seq_setRunningEx()` — first statements of the body (line 6267, before `/* S081 §19.1 item 6 …`)

```c
	/* S084 P0 A1: a transport change drops external ticks still owed, so a
	 * restart never replays the old clock period's remainder. */
	seq_extTicksOwed = 0u;
```

#### P2-28 MODIFY `seq_continueRunning()` — after `if (seq_running) return;` (line 6376), before `seq_playbackEpoch++;`

```c
	seq_extTicksOwed = 0u;          /* S084 P0 A1: see seq_setRunningEx() */
```

**Do not** clear `seq_extTicksOwed` in `seq_resetStepScheduler()`. That
function also runs at the master-boundary Pattern switch inside a tick, and
the period's remaining ticks must still run there, as the old back-to-back
loop ran them.

**Behaviour to expect:**
- **MIDI clock:** each clock runs its first tick on the edge, and the other
  three follow at the measured 96-PPQ period.
- **Pulse input (4 PPQ):** the pulse runs its first tick, and the other 23
  follow the same way.
- **Tempo estimate off:** if the clock speeds up, the next edge catches up
  the remainder (at most `ticks − 1`). If it slows down, the interpolation
  stops at the period's tick count and waits for the edge.
- LiveRecord placement gains real sub-clock tick periods (`seq_ppqPeriodUs`
  was near 0 for the burst ticks).

The `triggerJacks.c` comment ("derives intermediate substeps from BPM until
the next master pulse arrives") becomes accurate. Leave it unchanged.

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

Change the first comment line's `RAM about 14 B (P1B 10 B + P1C 4 B)` to
`RAM about 11 B (14 B before S084 P4 moved 3 B of roll state)` (A4).

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

Also replace `Affiliates: seq_takeRecBoundary(), liveRec_writeStep(), LC-7
state.` with `Affiliates: seq_takeRecBoundary(), liveRec_writeStep(),
liveRec_roll[].`, and `Extend a held fast roll` with `Extend a held roll`
(title line, adding `; S084 P4 per-track slots` to its source list).

#### P4-24 MODIFY `liveRec_syncRing()` comment — lines 464–471 (numbered last; it belongs here in file order)

The comment names the removed `liveRec_rollSpan`. Replace `also run at a
fast-roll start and a roll end` with `also run at a roll start and a roll
end`, and `afterwards liveRec_prevStep[] and liveRec_rollSpan match the
capture.` with:

```c
 * afterwards liveRec_prevStep[] and the held track's LIVEREC_ROLL_SPAN flag
 * match the capture (S084 P4).
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
            /*
             * Plain-trigger rewrite only while no span can exist (S084 P0
             * A2).
             *
             * What: the start step becomes a plain trigger when the start
             * step was not yet reached (forward-quantised), or when the
             * release is still on the start step with no second hit. A
             * release on a later step before the second hit takes the normal
             * end write below. Why: every held roll is a span (Q7), so a
             * slow roll (interval longer than the step) can have span steps
             * written before its second hit. Rewriting only the start step
             * left those spans to replay as a roll that was never played.
             * With the normal end write, the P1 time window carries hit 0
             * through the spans and closes at the release, before the next
             * hit was due: playback is the one hit that was heard (its tail
             * ducked at the release, as at every recorded roll end).
             */
            if ((r->flags & LIVEREC_ROLL_SPAN) == 0u ||
                (r->hits == 0u && pl.step == r->step)) {
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

The old per-branch one-line comments inside the function (`Released
before the second hit…`, `Released on the start step…`, `Released on a
later step…`, `Span every step that ended before the release…`) are
dropped. The header comment and the A2 block carry the rules.

Comment for `liveRec_endRoll()`: the first release rule changes for A2; keep
the other two verbatim, and replace the surrounding text:

```c
/*
 * End one track's recorded roll (S081 §8.8, P1B item 16, P1C items 4/7;
 * S084 P4 per track, every held roll).
 *
 * What: places the release on the rolled track and writes one step:
 *  - released before a forward-quantised start step was reached, or on
 *    the start step before the second hit: the START step is rewritten as
 *    a plain trigger at its start delay (roll, fill and rhythm removed). A
 *    release on a later step before the second hit takes the later-step
 *    rule below, so span steps already written stay covered (S084 P0 A2);
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
 * stored: the next ROLL_HIT carries the rate and opens the span
 * (liveRec_triggerIn()).
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

Replace the `FLAM_MAIN` line, which still names the retired foreground
timer (the spike left it stale):

```c
 * FLAM_MAIN   the flam's delayed main hit (fires in TIM3, reported by
 *             buttonHandler_overlayDrain(), S084 P2).
```

and the `ROLL_START`/`ROLL_HIT` lines with:

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

The spike holds the as-built text for every row below. Read it with
`git -C Helicase-dev-ph7-spike diff 59406a4 26fb7c3 -- <file>`. The
**base lines** are at `59406a4` (= `prime`); they are the old-side line
numbers of the spike's hunks. Copy the hunks named **P0**. **Skip** every
other hunk in the same file: those are S084 voice work (oscillators,
samples/wavetables, swap, transient, Mix FX) and go to S085/S086. Re-derive
every build number (`text`/`data`/`bss`, free flash, measured `.bss` delta)
from this implementation's own link map. Do not copy the spike's numbers.

Paths are under `knowledge_files/specification_reference/` unless shown.

| File | Base lines | P0 change | Skip (not P0) |
|---|---|---|---|
| `PATTERN_DYNAMIC_STACK.md` | 78, 81–82 | Roll summary: 9 rates; the length is a time window since S084 P1; duck and carry follow the window. | — (every hunk in this file is P0) |
| same | 408–415 | Roll engine paragraph: rate in master ticks; window = interval × fill from the (deferred) fire; a carry renews the window; parking; the deferred-carry stretch; duck at the window end; the live engine. | |
| same | 422–433 | §5.4 rate table: add the "PERF type" column (values 6..14 = index 1..9). | |
| same | 441–446 | F-R / carry rule per Q4: same rate and rhythm, refused after a fill < 100 step; the fill no longer has to match. | |
| same | 577–578, 586–587 | Step reset rule: flam grace in the foreground, flam main in TIM3 (reset 1), live roll repeats reset 0 (`seq_processLiveTicks()`). | |
| same | 1405–1406, 1441–1442 | Overlay recording sources (`buttonHandler_overlayStart()`, the TIM3 hits drained by `buttonHandler_overlayDrain()`); the roll's rhythm is recorded. | |
| same | 1458–1476 | §6.7.4 Rolls: the overlay list = the 9 step rates; every held roll is a span (Q7); per-track open rolls; rate switch per `liveRec_rollRateChange()`; A2 end rule; a take boundary ends rolls with no end write (F8); rhythm recorded (Q8). The spike text already drops the self-citation (A4). | |
| same | 2029 | `patSvc` record row: note/velocity/timing/roll/fill and the roll's rhythm. | |
| `MIDI_SPEC.md` | 148, 278 | `pb<`/`pb>`: 15 types (4 single velocities, 2 flams, the 9 STEP roll rates = index type − 5); values 6..11 not migrated. | 1556 (`Session 084 P1 - appended instrument targets`) |
| `FILESYSTEM_SPEC.md` | 77–78, 716 | `perf_btn_*` range 0..14 since S084 P3; older values are not migrated. | 135, 375, 846, 2273–2309, 2428–2431, 2837–2849 (samples/wavetables/instrument keys) |
| `MODULE_INTERCHANGE_SPEC.md` | 629 | `pat_recordStep()` row: the roll's `rhythm`. | 2055 (`Session 084 P1 — voice foundations`) |
| same | 830, 836 | PERF overlay row and Overlay recording row: first hits in `buttonHandler_overlayStart()`, TIM3 hits reported by `buttonHandler_overlayDrain()`, `liveRec_rollRateChange(track, idx, rhythm)`. | |
| same | 1034 | `seq_triggerVoice()` row: flam main reset 1 from TIM3; live roll repeats reset 0. | |
| same | 1038, 1040 | `seq_resetDeltaAndTick()` / `seq_triggerNextMasterStep()` rows: external tick interpolation (A1). | |
| same | 1049 (+ new row after it) | `seq_rollRhythmVelocity()` callers; new row for the live engine API (`seq_liveRollStart/SetRate`, `seq_liveFlamArm`, `seq_liveStop`, `seq_liveSetRhythm`, `seq_liveTakeHits`). | |
| same | 1822–1823, 1830–1832 | `liveRec_trigger_t` gains `rhythm`; the roll classification is removed; per-track `liveRec_roll[7]` state. | |
| `STORAGE_SRAM_MANIFEST.md` | 7–9, new bullet after 12 | "Current as of" and an "S084 P0 changes" bullet, with **this build's** numbers. Itemise them as §2 does (+87 B before alignment). Do not copy the spike bullet's "−7 B … helper bookkeeping" sentence, which is muddled. | 358–365, 584, 807 (sample flash, sample caches, `Session 084 P1 - measured storage`) |
| same | after 97 | Step roll arrays: `seq_rollWindow` replaces `seq_rollRemaining` and the duck arrays. | |
| same | new row after 590; 592–593; 612 | Sequencer live engine row (79 B); LiveRecord row (per-track `liveRec_roll[7]`, ≈ 291 B); PatternStackService staging row (40 B); `bh_overlay[7]` row (28 B). Add `seq_extTicksOwed` (1 B, A1) to the sequencer entry; the spike omitted it. | |
| `BANK_PRESET_ARCHITECTURE.md` | 773 | `PAR_PERF_BTN_LT/RT` trigger type 0..14 since S084 P3. | 874 (`Session 084 P1 — deferred instrument swap rules`) |
| `MEMORY.md` (root) | 23–24 | Current working source: S084 P0 implemented on `prime`, hardware pending. | the `Session 084 P1 — voice foundations` bullet inserted after line 36 |
| same | after 36 | Build at S084 P0 (this build's numbers). | |
| same | after 61 | S084 P0 summary (P1–P4) and the binding rules: carry rule Q4, spans Q7, live engine in TIM3, LiveRecord never in TIM3. | |
| same | 241–242 | Open product decisions: remove "whether recorded overlay rolls should carry the `pv` rhythm" (decided, Q8). | |
| same | 1863–1866 | TIM3 order bullet: live overlay ticks first, before the running gate; `seq_tick()` services them under external sync while stopped. | |
| same | after 1873 | New "MIDI / Clock Reminders" binding bullet: external clock ticks are interpolated (A1); never return to back-to-back ticks. | |

The root `S084_*.md` documents become superseded by the session handoff log
at close-out, as in earlier sessions.

---

## 9. Verification

Build after each phase: `make all && make img`; report `text/data/bss`
against the S083 close and the §2 estimate. There must be no new warning
from a changed line. `PatternData.c:201` (`-Waddress-of-packed-member`) is
pre-existing. For comparison only, the spike measured `text=608,736` /
`bss=430,096` after P1 alone and `text=609,288` / `data=424` /
`bss=430,200` with every entry applied.

**Host check H1 (required by the S084 gate, `S084_POST-MORTEM.md` §3).**
Use the scratchpad only; install no new utility. Copy `seq_rollPpq()`,
`seq_rollCarries()`, `seq_rollBegin()` and `seq_processRollTicks()` into a
host C file, with stubs for `seq_triggerVoice()` (print the tick and
`reset`), `voiceControl_resetOnly()`, `instrumentManager_requestGateDuck()`
(print "duck"), `seq_rollComputeVelocity()` and `stepScale_ticksQ8()` (a
fixed Q8 step interval). The driver calls `seq_rollBegin()` at each step
boundary, before `seq_processRollTicks()` on the same tick (the TIM3
order). Between steps it applies the carry-eligibility rule (P1-12). Build
it twice: from the `59406a4` code, and from the P1 code. The check counts
only if the `59406a4` build reproduces the F2 defect:

| Case (24-tick 1/16 step) | `59406a4` must print | P1 must print |
|---|---|---|
| Two adjacent steps, roll `32` (12), fill 100 | hits 0, 12, 35 (boundary hit lost, late) | hits 0, 12, 24, 36; nothing at 48 |
| Two adjacent steps, roll `64` (6), fill 100 | 0 6 12 18, then 29 35 41 | every 6 ticks from 0 to 42 |
| Step 1 fill 50, step 2 fill 100, roll `32` | — | hit 0, duck at 12, fresh hit 24 (reset 1), 36 (Q4) |
| Roll step, empty step, roll step, roll `32` | — | 0, 12; ends at 24; fresh 48 (reset 1), 60 |

Hardware (user), each at 120 BPM internal and on an external MIDI clock at
a tempo different from `bpm`:

| # | After | Check | Expect |
|---|---|---|---|
| V1 | P1 (external clock half after P2) | Two adjacent STEP steps, roll `32`, fill 100, `scl` 1/16, 1/8, 1/32 and a triplet scale; then the same with shuffle on and with micro-timing on the second step. | An even roll with no gap or late hit at any boundary. Velocity rhythm continues across steps. Under MIDI clock, equally even (A1). |
| V1b | P1 | Same, the first step at fill 50. | The first step's roll stops (ducks) at half the step; the second step starts a fresh roll (Q4). |
| V1c | P1 | Roll step, then an empty step, then the same roll step. | The roll ends at its window; the third step starts fresh (no resumed phase). |
| V2 | P2+P3 | Track 1 STEP roll `16t` over 4 steps; track 2 the same rate held from PERF. All 9 PERF rates and both flams. | No phasing between the tracks. Every PERF type plays at its STEP rate. Labels match the STEP list. Under MIDI clock, `64` and `128` are even (A1). |
| V3 | P4 | Hold each of the 9 PERF rates for 2+ steps on tracks at `scl` 1/16 and 1/8, `qnt off` and `qnt 16`, with `pv` set. Also (A2): on a 1/16 track with REC on, tap overlay `1/8` for about 150 ms across one step boundary. | Playback has the played rate and velocity shape, no hiccup on the release step, and the roll ends at the release. The `1/8` tap replays as exactly one hit. |
| V4 | P4 | Hold rolls on two tracks while recording; release in either order; release after REC off. | Both recorded; no stuck live hold; nothing written after REC off (F8). |
| V5 | P2–P4 | Repeat V2 and V3 with MIDI notes under a CC2 overlay. Switch the overlay (BAR or CC2) while a roll is held with different `pv<`/`pv>`. | As V2/V3. Hits before the switch keep the old rhythm (A3). |
| V6 | P2 | Transport stopped: hold PERF rolls with internal sync, and with external sync while the clock keeps running. Then under 4-PPQ pulse sync, running: every roll rate, shuffle and micro-timing. | They repeat at the internal tempo, and at the external tempo respectively. Under pulse sync everything plays evenly within each pulse (A1). |
| V7 | P2 | External sync: start, stop and Continue repeatedly, with a step roll and a held PERF roll. | No extra or missing tick at the first clock after a start or Continue (`seq_extTicksOwed` is cleared). |

---

## 10. Implementation log

**Not started.** Record each phase here as it lands on `prime`: the date,
the entries applied, any deviation from an entry (and why), the build
numbers against the S083 close and §2, the warnings check, and the H1/V
results as the user reports them.

| Phase | Date | Entries | Build (`text`/`data`/`bss`) | Deviations | Checks |
|---|---|---|---|---|---|
| P1 | | P1-1..P1-16 | | | H1, V1 |
| P2 + P3 | | P2-1..P2-28, P3-1..P3-7, P4-17..P4-19 + interim `liveRec_rollRateChange()` signature (§3) | | | V1 (ext), V2, V5, V6, V7 |
| P4 | | P4-1..P4-16, P4-20..P4-24 | | | V3, V4, V5 |
| §8 docs | | — | — | | — |

---

## 11. Spike reference (`26fb7c3`, superseded)

This schedule was first implemented on `dev-ph6-midi` (2026-10-24). That
tree went on to the S084 voice work, and it was rolled back as a whole
(`Helicase-dev-ph7-spike/S084_POST-MORTEM.md`). None of it is on `prime`. Its
P0 code built clean and passed static review. It was **never hardware
tested**. This section keeps what the re-implementation needs from it.

### 11.1 Spike build figures (for comparison only)

| Point | `text` | `data` | `bss` | Note |
|---|---:|---:|---:|---|
| S083 close (`59406a4`) | 608,744 | 424 | 430,112 | baseline |
| After P1 | 608,736 | 424 | 430,096 | `bss` −16 (estimate −14, the rest alignment) |
| After P1–P4 | 609,128 | 424 | 430,200 | `bss` +88 (estimate +86 + 2 alignment) |
| After the A1–A4 fixes | 609,288 | 424 | 430,200 | `seq_extTicksOwed` fitted existing padding; image 609,712 B |

### 11.2 Where the spike differed from the original schedule (all folded in)

| Entry | Spike code | Now in this schedule |
|---|---|---|
| P1-5 | braced the `track >= 5u` clause (warning fix) | P1-5 code |
| P1-6 | `for (uint8_t track = …)` | P1-6 code |
| P2-13 | `track >= NUM_TRACKS` guard (GCC bounded-array warning) | P2-13 code |
| P1-1, P4-14 | comments cited entry IDs (`P1-12`, `P1-13`, `P4-13`) | replaced by function names (§0 rule) |
| P4-10 / `liveRec_syncRing()` | rewrote the syncRing comment (it named the removed `liveRec_rollSpan`); no entry existed | P4-24 |
| P4-16 | left the `FLAM_MAIN … (fires in overlayTick)` line stale | P4-16 now replaces it |
| §8 | also changed `BANK_PRESET_ARCHITECTURE.md` 773, `MIDI_SPEC.md` 278, several `PATTERN_DYNAMIC_STACK.md` and `MODULE_INTERCHANGE_SPEC.md` rows not in the old table | §8 hunk map |

### 11.3 Spike assessment findings A1–A4 (approved by the user, 2026-10-24)

| # | Finding | Severity | Folded into |
|---|---|---|---|
| A1 | External clock ticks ran back to back, so live rolls finer than the clock bunched or collapsed (and step rolls, shuffle and micro-timing already did) | High under external sync | P2-5, §5.5 (P2-23..P2-28), V1/V2/V6/V7 |
| A2 | Every roll is a span, so a slow roll released on a later step before its second hit left spans that replayed as a roll never played | High for recording | P4-12, V3 |
| A3 | Hits drained during an overlay switch were reported with the new overlay's rhythm | Low | P2-21, P2-22, V5 |
| A4 | `RAM about 14 B` comment not reduced; a self-citation in `PATTERN_DYNAMIC_STACK.md` §6.7.4 | Nit | P4-3, §8 |

**A1 detail.**
- `seq_resetDeltaAndTick()` (MIDI clock) ran `SEQ_INTERNAL_TICKS_PER_MIDI_CLOCK` = 4
  scheduler ticks back to back on each clock.
- `seq_triggerNextMasterStep()` (pulse) ran `stepSize × 3`, which is 24 at
  `PRE_4_PPQ`.

The effect under MIDI clock:
- rates that are a multiple of 4 ticks stay even;
- `64` (6 ticks) alternates 1- and 2-clock gaps;
- `128` (3 ticks) fires two hits in one burst every third clock, and the
  second retrigger replaces the first in the same audio block.

Under 4-PPQ pulse sync, every roll faster than 1/16 collapses into one
burst per pulse.

The option chosen was option 1: interpolate the owed ticks from
`seq_tick()`'s tempo timer, and catch up at the next edge. The rejected
alternative was to always service the live engine from `seq_tick()` under
external sync. That fixes only live rolls, and it loses phase lock with
step rolls and recordings.

**A2 detail.** On a 1/16 track (24 ticks) with overlay `1/8` (48 ticks), hit
0 lands on step n, and the release comes 40 ticks later. Step n+1 has
already been written as a span, and the old rule rewrote only step n as a
plain hit. Playback then started a new roll at n+1. With the A2 condition,
the release takes the later-step end write: P1's window carries hit 0 to
the release, and playback is the one hit that was heard. The rejected
alternative was to backfill the spans at the first `ROLL_HIT`. It is exact,
but it needs a per-track pending-step list (more RAM and code).
