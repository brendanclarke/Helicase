# S079 P1 — Step Condition / Roll / Micro-Timing Special Values

Session 079, Part 1 — General plan for expanding the step special system with
trigger conditions, per-step roll (rate, rhythm, fill), and micro-timing delay.

Decisions from Q&A round 1 are folded in; resolved questions are marked
(DECIDED). Follow-up questions at the end.

---

## 1. Current state

The dynamic pool block stores three special values per step, gated by bits 0..2
of the special-flags byte (`PatternData.h:46-51`):

| Bit | Special      | Range   | Default |
|-----|--------------|---------|---------|
| 0   | Note         | 0..127  | 63      |
| 1   | Velocity     | 0..127  | 100     |
| 2   | Probability  | 0..127  | 127     |
| 3-7 | *reserved*   | —       | —       |

The `probability` field is currently a pure random gate: values below 127 scale
a hardware RNG comparison (`seq_evaluateStepCondition()`, `sequencer.c:1264`).
This function is the single gate called before every step fires; it returns 0
to suppress both trigger and automation for the step.

**Bits 3..7 of the special-flags byte are available.** Five unused bits can
encode up to five new special value slots.

**Pool capacity:** 8,192 bytes per Scene (256 × 32 B chunks).

**No per-track repetition counter exists.** A new per-track wrap counter is
needed for the X:Y conditions.

**Existing roll is a live performance feature only.** `seq_rollRate` /
`seq_rollState` (`sequencer.c:170-171`) provide a held-button retrigger.
There is no per-step roll in the Pattern data.

**No per-step microtiming exists.** The only timing offset is per-track shuffle.

---

## 2. Special-flags byte layout

Re-purpose bit 2 (probability → trigger condition) and allocate bits 3..6:

| Bit | Special                | Stored byte   | Range      | Default       |
|-----|------------------------|---------------|------------|---------------|
| 0   | Note                   | note          | 0..127     | 63            |
| 1   | Velocity               | velocity      | 0..127     | 100           |
| 2   | Trigger condition      | condition     | 0..36      | 0 (`-`)       |
| 3   | Roll rate              | roll          | 0..N       | 0 (off)       |
| 4   | Micro-timing delay     | timing        | 0..99      | 0 (none)      |
| 5   | Roll rhythm            | rhythm        | packed     | 0 (`1-1`)     |
| 6   | Roll fill              | fill          | 0..100     | 100 (full)    |
| 7   | *reserved (last bit)*  | —             | —          | —             |

**No backward compatibility.** (DECIDED: Q1.) Old Patterns with probability
bytes are not supported by the new firmware. The offline converter
`tools/convert_pattern_specials.py` (new) translates old probability values to
the new condition encoding before flashing. No runtime migration, no version
detection in the reader. Bump the PAT4 format to version 5; the reader rejects
version 4 with old-format probability.

**Condition value 0 is the default** (DECIDED: Q4): "always fire", displayed
as `-`. Values 37..255 also always fire (forward compatibility). The condition
byte is only stored when bit 2 is set, so a step with no condition has no
condition byte in the pool and defaults to `-` (always fire).

---

## 3. Trigger conditions

### 3.1 Probability conditions (values 1..10)

| Value | Label | Behaviour                                    |
|-------|-------|----------------------------------------------|
| 0     | `-`   | Always fire (default)                        |
| 1     | `r10` | 10% chance of firing (random)                |
| 2     | `r20` | 20% chance                                   |
| 3     | `r30` | 30% chance                                   |
| 4     | `r40` | 40% chance                                   |
| 5     | `r50` | 50% chance                                   |
| 6     | `r60` | 60% chance                                   |
| 7     | `r70` | 70% chance                                   |
| 8     | `r80` | 80% chance                                   |
| 9     | `r90` | 90% chance                                   |
| 10    | `r95` | 95% chance                                   |

### 3.2 Repeat-count conditions (values 11..20)

`X:Y` — the step fires only on the Xth iteration of a track repeating Y times.
Requires the new per-track repetition counter (`seq_trackRepeatCount[7]`).

| Value | Label | Fires on iteration...                         |
|-------|-------|-----------------------------------------------|
| 11    | `1:2` | 1st, 3rd, 5th, ... (odd plays)                |
| 12    | `2:2` | 2nd, 4th, 6th, ... (even plays)               |
| 13    | `1:3` | 1st, 4th, 7th, ...                            |
| 14    | `2:3` | 2nd, 5th, 8th, ...                            |
| 15    | `3:3` | 3rd, 6th, 9th, ...                            |
| 16    | `1:4` | 1st, 5th, 9th, ...                            |
| 17    | `2:4` | 2nd, 6th, 10th, ...                           |
| 18    | `3:4` | 3rd, 7th, 11th, ...                           |
| 19    | `4:4` | 4th, 8th, 12th, ...                           |
| 20    | `1:-` | First play only (never again until reset)     |

Semantics: `seq_trackRepeatCount[track]` increments when the track's step index
wraps (any play mode). For forward: when `seq_stepIndex` wraps from `len-1` to
`0`. For reverse: `0` to `len-1`. For pingpong: at each boundary reversal.
For random: each `len` steps traversed (a free counter modulo `len`). For once
modes: the counter never increments (the track stops before wrapping).

The evaluation checks `(repeatCount % Y) == (X - 1)`.
`1:-` is special: fire only when `repeatCount == 0`.

**Reset conditions:** the counter resets to 0 on:
- Scene change (global `seq_setActivePattern()`)
- Individual track Scene change (`seq_setTrackPlayedScene()`)
- Transport start (`seq_startPlaying()`)

If a repeat-count condition is set on a track in once mode (`onc`/`1fr`), the
step may never fire. This is accepted.

### 3.3 Fill / Not Fill (values 21..22)

| Value | Label | Behaviour                                     |
|-------|-------|-----------------------------------------------|
| 21    | `fil` | Step fires only during fill mode               |
| 22    | `!fl` | Step fires normally but is suppressed during fill |

(DECIDED: Q2.) Fill mode is active **while the PERF button is held**. The PERF
mode overlay is entered immediately on press (normal behaviour); fill activates
after the double-click timeout (`DOUBLE_CLICK_TIMEOUT`, 300 ms in `config.h`)
of continuous hold, preventing inadvertent fill triggers during a normal
mode-switch press. On release: fill deactivates, PERF mode stays.

Implementation: `buttonHandler_tick()` tracks PERF press duration. When
`held_ms >= DOUBLE_CLICK_TIMEOUT`, set `seq_fillActive = 1`. On PERF release,
set `seq_fillActive = 0`. The flag is read by `seq_evaluateStepCondition()`.

(DECIDED: Q8.) The condition always gates both trigger AND automation together.
A `fil` step whose condition fails (fill not active) suppresses everything,
including automation.

### 3.4 Last / Not Last (values 23..24)

| Value | Label | Behaviour                                        |
|-------|-------|--------------------------------------------------|
| 23    | `lst` | Fires only if the last conditional step also fired |
| 24    | `!ls` | Fires only if the last conditional step did NOT fire |

"Last conditional step" means the most recent step on THIS track that had any
trigger condition set (value 1..36). Default/absent steps (value 0) are
invisible to the chain. A `lst` step looks back only to the previous
conditional step in play order.

A per-track flag `seq_trackLastConditionFired[7]` records whether the most
recently evaluated conditional step fired or was suppressed. At track start
(or counter reset), this flag defaults to 0 (not fired), so an opening `lst`
will not fire and an opening `!ls` will fire.

Chaining: `r50` → `lst` → `lst` — the r50 coin-flip gates the entire chain.

Non-linear play modes: the flag tracks play order (reverse, pingpong, random),
not step-number order. No special handling needed.

### 3.5 Mute / Not Mute (values 25..36)

| Value | Label | Behaviour                                   |
|-------|-------|---------------------------------------------|
| 25    | `1mu` | Fires if voice 1 is muted                   |
| 26    | `2mu` | Fires if voice 2 is muted                   |
| 27    | `3mu` | Fires if voice 3 is muted                   |
| 28    | `4mu` | Fires if voice 4 is muted                   |
| 29    | `5mu` | Fires if voice 5 is muted                   |
| 30    | `6mu` | Fires if voice 6 is muted                   |
| 31    | `!1m` | Fires if voice 1 is NOT muted               |
| 32    | `!2m` | Fires if voice 2 is NOT muted               |
| 33    | `!3m` | Fires if voice 3 is NOT muted               |
| 34    | `!4m` | Fires if voice 4 is NOT muted               |
| 35    | `!5m` | Fires if voice 5 is NOT muted               |
| 36    | `!6m` | Fires if voice 6 is NOT muted               |

(DECIDED: Q3.) **Self-mute bypass.** When a step on a muted track has a mute
condition that tests its own track (e.g., `3mu` on track 3), the mute gate is
bypassed for that step and the condition is evaluated. If the condition passes
(track 3 is indeed muted), the step fires. If the condition fails, the step is
suppressed.

Sequencer change: the current hard mute gate at `sequencer.c:1419`
(`if (!(seq_mutedTracks & (1u << track)))`) must be restructured. For muted
tracks, the sequencer must:
1. Read the step's address entry (check bit 14 for specials).
2. If specials exist, read the condition byte from the pool block.
3. If the condition is a self-mute condition (the tested voice == this track),
   evaluate the condition and proceed if it passes.
4. If no specials or no self-mute condition, skip (existing behaviour).

This adds a 16-bit address read per step on muted tracks, and a pool block read
only for muted-track steps that have specials. The cost is small because muted
tracks process far fewer steps than active ones and the address read is a
single aligned SRAM1 halfword.

### 3.6 Suggested additional conditions (not committed)

- **Velocity range** (`>64`, `<64`): fires based on last velocity on this track.
- **Step count modulo** (`%3`, `%5`, `%7`): polyrhythmic against binary lengths.
- **Clock division gate** (`clk`): fire on global bar downbeats.
- **Combined AND**: two conditions both true. Needs a second byte or combined
  encoding.

---

## 4. Roll rate (per-step)

A step marked with a roll repeats its trigger at a fixed sub-division rate,
producing a ratchet/flam/buzz effect.

(DECIDED: Q5.) Roll rates are **absolute note values**, independent of the
track's step scale. A `32` roll always produces 32nd-note retriggers whether
the track is at 1/16 or 1/8 scale. On a 1/8-scale track, a 32nd roll produces
4 retriggers per step; on a 1/16-scale track, it produces 2.

(DECIDED: Q6.) Roll retrigger timing runs in ISR context, same mechanism as
shuffle (`seq_processSchedulerTick()`).

### 4.1 Rate values

| Value | Label | Absolute rate | PPQ ticks between triggers (96 PPQ) |
|-------|-------|---------------|-------------------------------------|
| 0     | `off` | No roll       | —                                   |
| 1     | `1/8` | 8th note      | 12                                  |
| 2     | `8tr` | 8th triplet   | 8                                   |
| 3     | `16`  | 16th note     | 6                                   |
| 4     | `16t` | 16th triplet  | 4                                   |
| 5     | `32`  | 32nd note     | 3                                   |
| 6     | `32t` | 32nd triplet  | 2                                   |
| 7     | `48`  | 48th note     | 2                                   |
| 8     | `64`  | 64th note     | 1.5 → round to 1 or 2              |

At 96 PPQ, a 64th note is 1.5 PPQ ticks, which cannot be exactly represented.
Accept quantization to PPQ ticks (1 tick at 96 PPQ is ~5.2ms at 120 BPM;
imperceptible for roll retriggers).

### 4.2 Roll carry-over across steps

If two or more adjacent steps on the same track (in play order) have:
- The same roll rate, AND
- The same roll rhythm, AND
- The same roll fill

...then the roll continues seamlessly across the step boundary. The retrigger
timing and velocity pattern from the first step carry into the second without
resetting. This allows a continuous roll to extend over several steps.

If any of the three roll parameters differ on the next step, the roll resets.

Implementation: per-track state tracks the current roll sub-tick accumulator
and the velocity pattern position. On step advance, compare the incoming step's
roll parameters to the stored ones; if all match, continue; otherwise reset.

### 4.3 Roll interaction with trigger condition

The trigger condition gates the entire step. If a step has both a condition and
a roll, and the condition suppresses the step, no roll triggers fire.

### 4.4 Roll interaction with existing live roll

The existing live performance roll (`seq_rollRate`/`seq_rollState`) and the
per-step roll are independent. If both are active simultaneously, per-step roll
takes priority: the live roll is suppressed for any track that has a per-step
roll active. When the per-step roll ends (next step has no roll), the live roll
resumes.

---

## 5. Roll rhythm (`rrt`)

A separate special value (bit 5) controlling the velocity pattern of each roll
retrigger.

### 5.1 Encoding

The display format is `{full_count}{direction}{cycle_length}`:
- `full_count`: how many retriggers at step velocity before changing
- `direction`: `-` (halve) or `+` (double)
- `cycle_length`: total retriggers before the pattern repeats

Example: `2-5` on a velocity-100 step → sequence 100, 100, 50, 50, 50, repeat.
The first 2 retriggers use step velocity; the remaining 3 use half velocity.

Byte encoding:

| Bits  | Field        | Range | Meaning                             |
|-------|--------------|-------|-------------------------------------|
| 7..4  | cycle_length | 1..15 | Total retriggers in one cycle        |
| 3     | direction    | 0..1  | 0 = halve (`-`), 1 = double (`+`)  |
| 2..0  | full_count   | 1..7  | Step-velocity retriggers first       |

Constraint: `full_count <= cycle_length`. Violation → treat as `1-1`.
Byte value 0x00 → `1-1` default (every retrigger at step velocity).

Halved velocity floors at 1 (never 0). Doubled velocity clamps at 127.

### 5.2 Roll rhythm carry-over

The velocity pattern position carries over across step boundaries under the
same rules as the roll rate carry-over (§4.2): all three roll parameters (rate,
rhythm, fill) must match for the pattern to continue.

---

## 6. Roll fill (`rfl`)

A separate special value (bit 6) controlling what percentage of the step
duration the roll occupies.

### 6.1 Value range

0..100 (percent). Default: 100 (the roll fills the entire step duration).

A value of 50 means retriggers occur only in the first half of the step
duration; the second half is silent (no retriggers). The retrigger rate is
unchanged — the triggers are spaced at the roll rate but stop after 50% of the
step has elapsed.

A value of 0 means no retriggers at all (the roll is effectively off,
equivalent to not having a roll). The UI should probably prevent storing 0
and instead clear the roll entirely.

### 6.2 Interaction with roll carry-over

Roll fill must match for carry-over (§4.2). If fill differs on the next step,
the roll resets even if rate and rhythm match.

---

## 7. Micro-timing delay

A per-step timing nudge that delays the step's trigger between its natural
fire time and the next step's fire time.

### 7.1 Value range

0 (default) to 99, representing percent delay. A value of 0 means the step
fires at its natural time. A value of 50 means the step fires halfway between
its natural time and the next step. A value of 99 fires almost at the next
step's position.

### 7.2 Interaction with per-track timing

The delay is relative to the track's own step interval:
```
delay_ticks = (step_interval_ticks * microtiming_value) / 100
```
where `step_interval_ticks` is the DDA interval for the current track scale
(`stepScale_ticksQ8()`, integer part).

### 7.3 Interaction with shuffle

Shuffle and micro-timing are additive. Shuffle delays odd steps by
`(shuffle × 24) / 256` ticks; micro-timing adds on top.

(DECIDED: Q7.) The combined delay is allowed to extend into the subsequent
step's territory. However, if the next step fires before the deferred trigger
completes, the deferred trigger is **dropped** and the new step fires instead.
This prevents a delayed trigger from firing on top of an already-playing next
step.

Implementation: the deferred trigger mechanism stores a pending trigger per
track. When a new step advances and wants to fire, any pending deferred trigger
for that track is cancelled first, then the new step's trigger (with its own
deferral, if any) takes over.

### 7.4 Implementation approach

Merge with the existing shuffle deferral system. The per-track
`seq_trackShufflePending`/`seq_trackShuffleDelay` mechanism is extended to
carry a combined delay. When a step fires:

1. Compute `shuffle_delay` (for odd steps with nonzero shuffle).
2. Compute `microtiming_delay` from the step's timing value.
3. `total_delay = shuffle_delay + microtiming_delay`.
4. If `total_delay > 0`, defer the trigger; otherwise fire immediately.

The deferred trigger is serviced by `seq_processSchedulerTick()` each PPQ tick,
same as shuffle today.

### 7.5 Interaction with automation

Automation fires at the same delayed time as the trigger. The step's automation
entries are queued when the deferred trigger fires, not when the step advances.

### 7.6 Interaction with rolls

A step with both roll and micro-timing: the entire roll starts at the delayed
position. The first retrigger fires at the delayed time; subsequent retriggers
follow from there at the roll rate.

---

## 8. `pat_step_specials_t` struct change

```c
typedef struct {
    uint8_t note;       /* bit 0 */
    uint8_t velocity;   /* bit 1 */
    uint8_t condition;  /* bit 2: trigger condition index */
    uint8_t roll;       /* bit 3: roll rate index */
    uint8_t timing;     /* bit 4: micro-timing delay 0..99 */
    uint8_t rhythm;     /* bit 5: roll rhythm encoding */
    uint8_t fill;       /* bit 6: roll fill percent 0..100 */
    uint8_t flags;      /* which bits were present */
} pat_step_specials_t;
```

Default values for absent fields: condition = 0 (always fire), roll = 0 (off),
timing = 0 (none), rhythm = 0 (`1-1`), fill = 100 (full).

All callers of `pat_readStepSpecials()` and `pat_blockRead()` must be updated
for the wider struct and new flag bits. Primary consumers: sequencer playback,
STEP menu display/edit, copy/clear, multi-step broadcast.

---

## 9. STEP page layout

(DECIDED: Q9.) Each special gets its own parameter cell, always shown. The
STEP page's step-parameter subpages are extended:

| Subpage | Cell 0      | Cell 1    | Cell 2      | Cell 3      |
|---------|-------------|-----------|-------------|-------------|
| 1       | `vel` 0-127 | `not` 0-127 | `con` label list | `dly` 0-99 |
| 2       | `rol` rate list | `rrt` rhythm | `rfl` 0-100 | *(empty or reserved)* |

Subpage 0 (track settings: length, scale, shuffle, run mode) is unchanged.

The `con` cell replaces the old `prb` cell. It cycles through the condition
list (37 entries) with pot rotation and displays 3-char labels. The `DTYPE`
changes from `DTYPE_0B127` to a new `DTYPE_CONDITION` that formats the value
as a label lookup.

`rol`, `rrt`, and `rfl` are similarly displayed: `rol` uses a label list
(the rate table), `rrt` displays the `N±M` format, `rfl` is a numeric 0-100.

---

## 10. RAM and storage budget

### 10.1 Pool block growth

Current max block: 2 header + 1 flags + 3 specials + 63×2 automation = 132 B
(`PAT_RAW_BLOCK_MAX`).

New max: 2 header + 1 flags + 7 specials + 63×2 automation = 136 B (4-byte
aligned). `PAT_RAW_BLOCK_MAX` changes from 132 to 136.

All stack-allocated buffers sized to `PAT_RAW_BLOCK_MAX` (`copyClear_pasteStep`
and related) grow by 4 bytes. Verify against the stack budget.

### 10.2 Per-track ISR state (new)

| Variable                          | Size  | Region | Lifetime | Owner     |
|-----------------------------------|-------|--------|----------|-----------|
| `seq_trackRepeatCount[7]`         | 7 B   | SRAM1  | Static   | Sequencer |
| `seq_trackLastConditionFired[7]`  | 7 B   | SRAM1  | Static   | Sequencer |
| `seq_fillActive`                  | 1 B   | SRAM1  | Static   | BtnHandler|
| `seq_fillHoldTicks`               | 2 B   | SRAM1  | Static   | BtnHandler|
| Per-track roll state:             |       |        |          |           |
|   `seq_rollAccum[7]` (sub-tick)   | 7 B   | SRAM1  | Static   | Sequencer |
|   `seq_rollVelPos[7]` (vel cycle) | 7 B   | SRAM1  | Static   | Sequencer |
|   `seq_rollLastRate[7]`           | 7 B   | SRAM1  | Static   | Sequencer |
|   `seq_rollLastRhythm[7]`         | 7 B   | SRAM1  | Static   | Sequencer |
|   `seq_rollLastFill[7]`           | 7 B   | SRAM1  | Static   | Sequencer |
|   `seq_rollStepVel[7]`            | 7 B   | SRAM1  | Static   | Sequencer |
|   `seq_rollActive[7]`             | 7 B   | SRAM1  | Static   | Sequencer |
| **Total new ISR state**           |**~67 B**| SRAM1 | Static  | Seq/Btn   |

The existing `seq_trackShuffleDelay[7]` / `seq_trackShufflePending[7]` /
`seq_trackShuffleVel[7]` / `seq_trackShuffleNote[7]` (28 B) are extended to
carry the combined shuffle+microtiming delay. No net growth for micro-timing
if the mechanisms are merged; the only addition is the delay calculation at
step-advance time.

### 10.3 Flash estimate

New code: condition evaluation table, roll tick logic, micro-timing deferral,
mute bypass, display label tables, STEP page subpage, menu edit support, new
dtype. Estimate: 3–5 KB text growth. Current headroom: 213,392 B free.

### 10.4 Pattern file format

PAT4 version bumps to 5. The reader rejects version 4 (old probability
encoding). The converter tool `tools/convert_pattern_specials.py` converts
PAT4 v4 → v5 offline. The pool byte stream is unchanged in structure; only the
meaning of the condition byte (formerly probability) changes.

---

## 11. Converter tool

New: `tools/convert_pattern_specials.py`. Reads PAT4 v4 files from an SD card
directory, converts probability bytes in pool blocks to the new condition
encoding, and writes PAT4 v5 files.

Conversion rule for the old probability byte (0..127):
- 127 → remove the condition byte entirely (default = always fire)
- 114..126 → condition 10 (`r95`)
- 102..113 → condition 9 (`r90`)
- 89..101 → condition 8 (`r80`)
- 76..88 → condition 7 (`r70`)
- 64..75 → condition 6 (`r60`)
- 51..63 → condition 5 (`r50`)
- 38..50 → condition 4 (`r40`)
- 25..37 → condition 3 (`r30`)
- 13..24 → condition 2 (`r20`)
- 0..12 → condition 1 (`r10`)

The mapping rounds to the nearest 10% bucket. Values that were fine-grained
(e.g., 73% probability) lose that precision — accepted; the old continuous
range is not carried forward.

---

## 12. Remaining ambiguities and risks

### A1. Flags byte nearly full

Bits 0..6 are now assigned; only bit 7 remains reserved. Any future step
special would require either: a second flags byte (structural change to the
pool block format), or repurposing a value within an existing field (overloaded
encoding). Accepted for now.

### A2. Pool capacity under heavy use

A step with all 7 specials and no automation costs 10 bytes (2 header + 1 flags
+ 7 values). Seven tracks × 128 steps × 10 bytes = 8,960 bytes > 8,192-byte
pool. The extreme case cannot fit. The allocator must refuse gracefully and the
UI must report "pool full" to the user.

In practice: most steps will not have all specials. A typical step with
condition + roll costs 6 bytes (2 header + 1 flags + 2 values + 1 condition).
Seven tracks × 128 steps × 6 = 5,376 bytes — well within budget.

### A3. Roll timing quantization at 64th notes

At 96 PPQ, a 64th note is 1.5 ticks — not exactly representable. Accept
quantization (alternate between 1 and 2 ticks). At 120 BPM this is a ~2.6ms
jitter, below perception for rapid retriggers.

### A4. Mute bypass overhead

For muted tracks, the sequencer now reads a 16-bit address entry per step.
Steps with specials (bit 14 set) additionally require a pool block read to
check for a self-mute condition. This is more work than the current zero-cost
mute skip, but the overhead is bounded: at most 7 halfword reads per step
advance (one per muted track), and pool reads only for steps that actually have
specials. The pool read is a short SRAM1 access, not SD I/O.

### A5. Roll fill at small values

Roll fill of 1% on a short step may produce zero retriggers (the fill window
is shorter than one retrigger interval). The roll would be silent. This is
correct by definition — if the user sets fill to 1%, they get 1% of the step.

### A6. Micro-timing + shuffle overflow into next step

At maximum shuffle + maximum micro-timing, the combined delay can approach 2×
the step interval. The decided behaviour (Q7) allows this but drops the
deferred trigger if the next step fires first. Risk: the user programs a large
delay and the trigger never fires because the next step always pre-empts it.
This is by design — the user controls both values.

### A7. STEP page subpage navigation

Adding a second step-parameter subpage means the user must scroll to see
roll/rhythm/fill values. The existing subpage mechanism uses BAR or a similar
control. Verify that the subpage navigation gesture still works with 3
subpages (track settings + 2 step-specials pages).

---

## 13. Follow-up questions

### F1. Roll fill value 0

Should roll fill 0 be treated as "no roll" (equivalent to clearing the roll)?
Or should it be a valid value meaning "zero-length roll" (silent)? If the
former, the UI should prevent storing 0 and instead clear the roll special
entirely when the user dials fill down to 0. If the latter, 0 is a stored
value that produces no retriggers.

### F2. Absolute roll rates on very slow tracks

A track at 1/2 scale (one step = a half note = 48 PPQ ticks). A 64th roll on
that step produces 48 / 1.5 ≈ 32 retriggers per step. A 1/1 scale (whole note
= 96 ticks) with a 64th roll → ~64 retriggers per step. These are musically
extreme but computationally fine (one trigger per PPQ tick at most). Is this
the intended range, or should there be a maximum retrigger count per step?

### F3. Condition + roll + micro-timing deferred-trigger interaction

A step with condition + roll + micro-timing delay: the condition is evaluated
at step-advance time (not at the delayed time). If the condition passes, the
entire roll (starting from the delayed position) fires. Is this correct? Or
should the condition be re-evaluated at the delayed trigger time (allowing
fill mode to activate between step-advance and the delayed trigger)?

**Recommendation:** evaluate at step-advance time. Evaluating at delayed time
would require storing the condition index in the deferred trigger state and
re-running the RNG, which changes the probabilistic outcome.

### F4. `lst`/`!ls` interaction with `fil`/`!fl`

If a step has `fil` and doesn't fire (fill not active), does it update the
`seq_trackLastConditionFired` flag? If yes, a subsequent `lst` step would not
fire (because the `fil` step didn't fire). If no, the `lst` step would look
further back to the previous conditional step that wasn't fill-gated.

**Recommendation:** yes, update the flag. The `fil` step was evaluated and
suppressed — that is a "did not fire" result, and `lst`/`!ls` should reflect
it. This keeps the model simple: every conditional step updates the flag,
regardless of which condition type suppressed it.

### F5. Python converter scope

Should `tools/convert_pattern_specials.py` also handle the S078 scale
conversion (`tools/convert_scene_scale.py`), or remain a separate tool? A
combined converter that handles both PAT4 v4 → v5 (probability) and the scene
scale adjustment would simplify card preparation to one step.