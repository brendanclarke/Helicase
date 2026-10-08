# S078 P1 — Track Settings Expansion

Session 078, Part 1 — proposal document.  
Status: **accepted**.

---

## 1. Background

Track-level parameters `track_length[7]`, `track_scale[7]`, and
`track_shuffle[7]` are stored, Menu-editable, dirty-marked, and PAT4-persisted
(PATTERN_DYNAMIC_STACK.md §1, §6.4). Of the three, only `track_length` currently
affects playback (Session 068). `track_scale` and `track_shuffle` are retained
bytes with no sequencer consumption — all seven tracks advance on a single
global 24-PPQ-tick divisor regardless of their stored values. This session
expands these settings and adds new per-track parameters: play mode and (later)
morph targets.

The existing StepScale table (`Core/Sequencer/StepScale.c`) has 14 discrete
entries ranging from 1/64 to 2 bars, stored as 96-PPQ tick counts. The FX
sequencer already consumes this table for its own clock.

---

## 2. Step scale — continuous log curve with musical stops

### 2.1 The new model

Replace the current 14-entry discrete StepScale index with a **128-position
(0..127) continuous log-scale parameter**, transmitted and stored as a single
byte. The parameter follows a general logarithmic curve from approximately
0.25× (1/64 note) to 8× (1/2 note) relative to the default 1/16 step.

The raw log curve is defined by:

    multiplier(cc) = 0.25 × 2^(cc / (127 / 5))

This produces a smooth, continuous sweep from 0.25 (cc=0) to 8.0 (cc=127).

### 2.2 Musical stops — nudged positions

Specific CC positions on the curve are **nudged** to sit exactly on musically
relevant subdivisions, as defined by `step_scale_0_127.csv`. These nudged
positions and their musical values are:

| CC | Nudged multiplier | Musical value      |
|---:|-------------------:|--------------------|
|  0 | 0.25               | 1/64               |
| 16 | 0.3333...          | 1/32 triplet       |
| 38 | 0.5                | 1/32               |
| 54 | 0.6667...          | 1/16 triplet       |
| 60 | 0.75               | dotted 1/32        |
| 76 | 1.0                | 1/16 (default)     |
| 83 | 1.3333...          | 1/8 triplet        |
| 86 | 1.5                | dotted 1/16        |
| 93 | 2.0                | 1/8                |
|100 | 2.6667...          | 1/4 triplet        |
|103 | 3.0                | dotted 1/8         |
|110 | 4.0                | 1/4                |
|120 | 6.0                | dotted 1/4         |
|127 | 8.0                | 1/2                |

All intermediate positions use the un-nudged log multiplier for a smooth,
continuous feel when sweeping with a knob or MIDI CC.

### 2.3 Display

- **Musical positions (14 stops):** display the symbolic name (`/16`, `8Tr`,
  `d32`, etc.) when the parameter is exactly on one of the 14 nudged
  musical stops.
- **All other positions:** display the raw 7-bit value as a 3-character
  base-10 number (`  1` through `127`). CC 0 is the musical stop 1/64, so
  bare `0` never appears. The decimal multiplier is not shown — it is not
  informative on a 3-character display and the musical stops cover every
  position a user would seek out by name.

### 2.4 Tick computation (ISR) — fractional DDA accumulator

The continuous curve produces non-integer tick counts at most CC positions
(e.g., CC 50 → multiplier 0.622 → 14.93 ticks). Rather than rounding to
integer ticks (which would collapse many adjacent CC positions to the same
playback rate), we use **fractional tick accumulators** to preserve the
truly continuous character of the curve.

#### 2.4.1 The DDA approach

Store each CC position's tick interval as a **Q8.8 fixed-point uint16_t**
in a 128-entry flash LUT (256 B). The value is `round(multiplier × 24 × 256)`.

Each track has a Q8.8 accumulator (`uint16_t`). Every PPQ tick:

    for each track:
        accumulator[track] += 256          // add 1.0 in Q8.8
        while accumulator[track] >= interval[track_scale]:
            accumulator[track] -= interval[track_scale]
            advance_track_step(track)

The fractional remainder carries forward, so over time the average step rate
is exactly `multiplier × 24` ticks — no drift, no quantisation collapse.

**Example:** CC 50, multiplier 0.622, interval = round(0.622 × 24 × 256) =
3822 (Q8.8 = 14.93 real ticks). The accumulator fires every 14 or 15 ticks,
averaging 14.93 — audibly smooth and distinct from the neighbouring CC
positions at 14 or 15 integer ticks.

#### 2.4.2 Range check

Maximum interval: CC 127, multiplier 8.0, interval = 8 × 24 × 256 = 49,152.
Fits in uint16_t (max 65,535). The accumulator value stays below interval +
256 = 49,408, also safe. Minimum interval: CC 0, multiplier 0.25, interval =
0.25 × 24 × 256 = 1,536. The `while` loop fires at most once per tick at
this interval (1536 > 256), so there is no runaway.

#### 2.4.3 ISR cost

Per PPQ tick: one addition, one comparison, and (at step boundaries only) one
subtraction and a function call per track. At 96 PPQ × 200 BPM = 320 ticks/s,
this is 7 × 320 = 2,240 add/compare pairs per second on a 216 MHz core —
negligible.

#### 2.4.4 Risk mitigation — implementation plan

Fractional accumulators in an ISR carry real risk: drift, phase errors on
realignment, and interaction with every existing sequencer path that assumes
uniform step timing. The implementation should proceed in stages:

**Stage 1 — integer-only, discrete positions.** Replace the current global
divisor with per-track accumulators, but keep integer tick intervals (round
each LUT entry to nearest int, Q8.8 with .8 = 0). This exercises the
per-track accumulator plumbing, realignment, and play-mode interaction
without introducing fractional behaviour. Validate on hardware:
- All 14 old musical positions produce identical timing to the old StepScale
  table.
- Two tracks at different scales stay phase-correct across pattern
  boundaries.
- `seq_realignActivePatternToMasterClock()` correctly positions each track
  based on its own scale.
- Stop/start, scene change, and per-track scene assignment all reset
  accumulators correctly.

**Stage 2 — fractional accumulators.** Switch the LUT to true Q8.8 values.
Validate:
- A track at a non-musical CC position plays at an audibly distinct rate
  from its integer-rounded neighbours.
- Sweep the scale parameter smoothly with a knob while playing; no glitches,
  pops, or lost steps.
- Long runs (>10 minutes) show no cumulative drift between a fractional
  track and the master clock bar boundary.

**Stage 3 — FX sequencer migration.** Move `seq_fxClockTick()` to the same
Q8.8 LUT and DDA accumulator model. The FX accumulator (one additional
`uint16_t`, 2 bytes ISR-static) is reset by the same paths that clear the
Pattern track accumulators: transport stop/start and scene change. Validate
the FX sequencer at non-musical positions.

#### 2.4.5 Realignment

`seq_realignActivePatternToMasterClock()` currently computes each track's
step index from the master clock. With per-track accumulators, realignment
must also reset the accumulator to the correct fractional phase:

    master_ticks_elapsed = seq_elapsedPpqTicks  (in Q8.8: value << 8)
    track_step = master_ticks_elapsed / interval[track_scale]
    accumulator[track] = master_ticks_elapsed % interval[track_scale]

This preserves phase coherence: a realigned track picks up exactly where
it would have been if it had been running from the start.

### 2.5 Storage and defaults

The retained byte remains `region->track_scale[track]` (one byte per track in
the PAT4 track-parameters region). The init default changes from
`STEP_SCALE_DEFAULT` (4, the old 14-entry index for 1/16) to **76** (the CC
position for 1/16 on the new 128-position curve).

**No on-device migration.** Existing PAT4 files and `.fx` files are not
migrated on load. The firmware simply applies the new defaults; old files
with non-default scale values will play at whatever the new curve maps their
stored byte to (likely a very fast scale near the bottom of the curve). A
separate **Python conversion utility** (`tools/convert_scene_scale.py`) is
provided for users who need to migrate existing card content. It reads PAT4
and `.fx` files, remaps old 14-index scale bytes to the corresponding new
CC positions, and writes the updated files back.

### 2.6 Relationship to the FX sequencer

The FX sequencer shares the same 128-position curve. Its retained
`seq_step_scale` byte uses the same CC encoding. `EFFECT_SEQ_SCALE_COUNT`
becomes 128; `EFFECT_SEQ_SCALE_DEFAULT` becomes 76. Default is 1/16; no
special concern for what old `.fx` files end up at — the conversion utility
handles cards that need it.

---

## 3. Per-track shuffle

### 3.1 Current state

`track_shuffle[track]` is stored (0..127), persisted, and Menu-editable, but
the sequencer fires every step at a uniform tick boundary — no shuffle offset
is applied (PATTERN_DYNAMIC_STACK.md §6.4).

### 3.2 Implementation

Shuffle delays every **even-numbered step** (0-indexed steps 1, 3, 5, ...) by
a fraction of the step interval. A shuffle value of 0 means no delay (straight
time); 127 means the even step is delayed almost to the next odd step (extreme
swing).

### 3.3 Shuffle is always based on the 1/16th grid

The shuffle delay is always calculated relative to the **1/16th note step
interval** (24 PPQ ticks at 96 PPQ), regardless of the track's current step
scale. A track playing quarter notes (scale ×4) does not get quarter-note
shuffle offsets — it still gets 1/16th-note shuffle offsets, which are
proportionally tiny relative to the quarter-note step duration and effectively
vanish.

This is correct musically: shuffle is a groove/feel concept tied to the
underlying tempo grid. "This track plays quarter notes" means the steps are
quarter notes; the shuffle swing between consecutive beats should still be
at the 1/16th resolution, not stretched to match the step duration. Extreme
shuffle on a fast track (1/64 or 1/32) delays by a large fraction of the
step; extreme shuffle on a slow track (1/4 or 1/2) delays by a negligible
fraction. This matches musical expectation.

In the ISR:

    shuffle_delay_ticks = (shuffle_value * 24) / 256

This is a constant tied to BPM, not to the track's scale. Per-track state:
`seq_trackShuffleDelay[NUM_TRACKS]` (7 bytes ISR-static, `uint8_t`). When a
track advances to an odd-indexed step and its shuffle value is nonzero, the
trigger is deferred by `shuffle_delay_ticks` PPQ ticks. The ISR decrements
the counter each PPQ tick and fires the step when it reaches zero.

Shuffle is orthogonal to both length and scale.

---

## 4. Play modes

### 4.1 New per-track parameter

Add `track_play_mode[NUM_TRACKS]` to the Pattern region's track parameters
(7 bytes; fits within the 13 reserved bytes per track in the PAT4 wire
format — see PATTERN_DYNAMIC_STACK.md §7, offset 112, "7 × 16 B" track
parameter block).

### 4.2 Modes

| Value | Token | Description |
|------:|:------|:------------|
| 0 | `fwd` | Forward (default). Steps advance 0→1→2→...→length−1→0→... |
| 1 | `rev` | Reverse. Steps advance length−1→...→2→1→0→length−1→... |
| 2 | `pip` | Ping-pong. Boundaries play twice: 0→...→L−1→L−1→...→0→0→...→L−1→... Cycle length = 2 × length; default length 16 syncs to 2 bars (32 steps). Matches the FX sequencer implementation (`sequencer.c:513-517`). |
| 3 | `rnd` | Random. Each step boundary picks a uniformly random step index within the track's length. |
| 4 | `onc` | Once (synced). Track enters aligned to the master step clock, plays through once forward, then stops. Does not repeat. |
| 5 | `1fr` | Once free. Track always starts at step 0 regardless of the master clock position, plays through once forward, then stops. |

The token names (`fwd`/`rev`/`pip`/`rnd`) match the existing FX sequencer
run mode vocabulary (`EFFECT_SEQ_RUN_FWD` etc. in `EffectTypes.h`). `sel`
(the FX sequencer's manual-select mode) is not applicable to Pattern tracks.

### 4.3 Retrigger rules for once modes

A stopped `onc` or `1fr` track is reset (retriggered) by:

- **Scene change** (global scene switch).
- **Per-track scene reassignment** (PERF hold-VOICE+press-SEQ). This counts
  as a scene change **for that specific track only**. If the newly assigned
  scene's copy of that track has a once/once-free play mode, it starts its
  one-pass playback. Other tracks are unaffected. Note: voice 6 assigns
  both tracks 6 and 7 together (`sequencer.c:628-629`), so a voice-6 scene
  reassignment retriggers both tracks if either has a once mode.
- **Transport stop/start** (play button or external sync restart).

A stopped once-mode track is **not** reset by:

- The double-click "realign" gesture — this realigns running tracks to the
  master clock but does not restart stopped ones.
- A per-track scene reassignment of a **different** track — only the
  reassigned track(s) are affected.

### 4.4 ISR changes

`seq_advanceTrackStep()` currently unconditionally increments the step index
and wraps at `track_length`. The play mode replaces this with a per-track
direction/state. The DDA (§2.4.1) owns **when** a step advance fires; the
play mode owns **where** the step index goes:

- `fwd`: current behaviour (increment, wrap).
- `rev`: decrement, wrap at 0 → length−1.
- `pip`: cycle of `2 × length`. Position `p` in the cycle maps to step
  index `p < length ? p : (2 * length - 1 - p)`. Same formula as the FX
  sequencer.
- `rnd`: `GetRngValue() % length` (the existing hardware RNG).
- `onc`: on transport start, set the track's step to `masterStepClock %
  length`. Advance forward. When step wraps past length−1, set a per-track
  "stopped" flag and suppress further advances.
- `1fr`: on transport start, set step to 0 unconditionally. Same one-pass
  stop behaviour as `onc`.

New ISR-static state: `seq_trackPlayState[NUM_TRACKS]` (7 bytes). Each byte
packs: pip direction (1 bit), stopped flag (1 bit), pip cycle counter or
unused (6 bits).

### 4.5 Storage

One byte per track in the PAT4 track-parameters reserved region. Default
value 0 (`fwd`). Values ≥6 treated as `fwd` for forward compatibility.

---

## 5. Morphability

### 5.1 Morphable parameters

The following track settings should be morphable (sweepable between Normal and
Morph endpoints via the per-voice or global Morph amount):

| Parameter | Morph behaviour |
|:----------|:----------------|
| **Length** | Interpolates between two integer lengths. Rounds to nearest integer. If the current step index is past the new morphed length, the track wraps immediately. |
| **Scale** | Interpolates between two CC positions on the 128-position log curve. The intermediate multiplier is whatever the curve says at the interpolated CC value. |
| **Shuffle** | Interpolates between two 0..127 shuffle amounts. |

These three are natural candidates because they produce continuously useful
musical variation when swept — length changes the phrase length, scale changes
the speed, shuffle changes the groove, all in real time during a morph sweep.

### 5.2 Non-morphable parameters

| Parameter | Why not morphable |
|:----------|:------------------|
| **Play mode** | Discrete enum (fwd/rev/pip/rnd/onc/1fr). No smooth sweep. |
| **MIDI channel** | Discrete routing parameter. |
| **MIDI note** | Discrete identity parameter. |

### 5.3 Morph amount source — voice morph drives its tracks

There is no separate per-track morph amount. Each track's morph interpolation
point comes from its **associated voice's morph amount**
(`scene.settings.voice_morph_amount[slot]`):

| Tracks | Morph source |
|:-------|:-------------|
| Track 1 | Voice 1 morph (`voice_morph_amount[0]`) |
| Track 2 | Voice 2 morph (`voice_morph_amount[1]`) |
| Track 3 | Voice 3 morph (`voice_morph_amount[2]`) |
| Track 4 | Voice 4 morph (`voice_morph_amount[3]`) |
| Track 5 | Voice 5 morph (`voice_morph_amount[4]`) |
| Tracks 6 and 7 | Voice 6 morph (`voice_morph_amount[5]`) |

Tracks 6 and 7 share voice 6's morph amount (the HiHat/Choke slot pair),
matching the existing `seq_setPerTrackScene()` behaviour where voice 6
assigns both tracks together (`sequencer.c:628-629`). The two tracks may
have **different morph endpoints** for length/scale/shuffle, but the
interpolation amount is always voice 6's single morph value.

### 5.4 Morph endpoint storage

Length, scale, and shuffle are **Scene parameters** for morph purposes, not
Pattern parameters. The Pattern region stores the Normal values
(`track_length`, `track_scale`, `track_shuffle`); the Morph endpoints live
in Scene data and are persisted through the existing **parameter AutoSave**
system, alongside instrument and Scene-setting morph endpoints.

This follows the existing architecture: Patterns store step/track data, but
the Morph system is a Scene-level override. The morph worker interpolates
between the Pattern's Normal value and the Scene's Morph endpoint at the
voice morph amount.

Per Scene: 3 parameters × 7 tracks = **21 bytes** of new Morph endpoint
storage in Scene data. No AutoSave format version bump needed — the user
will delete all AutoSave files after this implementation lands, so the new
layout starts clean.

### 5.5 Automation targeting

Step scale, shuffle, and length are automatable as **Scene (`scn`) targets**
through the existing Pattern automation system, using three new target IDs
in the Scene target block (block 6 of the 9-bit automation parameter ID
space, PATTERN_DYNAMIC_STACK.md §4.4).

---

## 6. Other track settings surveyed

From the session logs and SCOPING_TARGETS.md, additional track-level
parameters that were considered but are **not part of this proposal**:

| Candidate | Status | Notes |
|:----------|:-------|:------|
| Track rotation | Already implemented (`PAR_TRACK_ROTATION`). |
| Per-track scene assignment | SCOPING_TARGETS §6.5 — separate PERF mode feature, not a track setting. |
| External MIDI sequencing tracks | §6.8 — separate feature adding new tracks, not settings on existing tracks. |
| Per-track probability scaling | Not currently scoped. Could be a future track-level parameter that scales all step probabilities on the track. |
| Per-track swing style | Not currently scoped. Shuffle covers basic swing; more complex groove templates (e.g., MPC-style groove quantize) could be a future extension. |

---

## 7. Menu page layout

The current STEP front-page track settings row
([menuPages.h:89](Core/Menu/menuPages.h#L89)) is:

    len | scl | mch | not | shf | --- | --- | ---

With play mode added, the proposed layout becomes:

    len | scl | shf | mod | mch | not | --- | ---

where `mod` is play mode. This groups the three morphable parameters (len,
scl, shf) together on the left, followed by the discrete parameters. The
two empty slots remain available for future track settings.

---

## 8. Implementation order

1. **Step scale continuous curve** — new 128-position Q8.8 LUT, ISR per-track
   fractional accumulators (Stage 1 integer-only, then Stage 2 fractional),
   display formatter. This is the heaviest single item and the stated primary
   goal.
2. **FX sequencer migration (Stage 3)** — move `seq_fxClockTick()` to the
   same 128-position LUT and Q8.8 DDA accumulator. One additional `uint16_t`
   (2 bytes ISR-static). Reset by the same paths as Pattern track
   accumulators.
3. **Per-track shuffle consumption** — delay counters in the ISR, shuffle
   tick computation fixed at 1/16th grid.
4. **Play modes** — new storage byte, ISR direction/state logic, retrigger
   rules.
5. **Morphability** — morph endpoint storage in Scene data, interpolation in
   the morph worker, AutoSave marking, Scene automation target IDs.
6. **Conversion utility** — `tools/convert_scene_scale.py` for offline
   migration of old cards.
7. **Bar display fix** — `menu_currentBar` must follow the viewed track's
   position, not the master grid.

Each part is independently testable on hardware. Stage 1 (§2.4.4) is the
critical gate — it exercises the per-track accumulator plumbing before
fractional behaviour is introduced.

---

## 9. RAM and flash cost estimate

| Item | Region | Bytes | Notes |
|:-----|:-------|------:|:------|
| 128-entry Q8.8 tick LUT | Flash | 256 | `uint16_t[128]` |
| 14-entry musical-stop label table | Flash | ~112 | 14 × (CC + 3-char short + 8-char long) |
| Per-track Q8.8 accumulator | ISR static (SRAM1) | 14 | `uint16_t[7]` |
| FX seq Q8.8 accumulator | ISR static (SRAM1) | 2 | `uint16_t` |
| Per-track shuffle delay counter | ISR static (SRAM1) | 7 | `uint8_t[7]` |
| Per-track play state | ISR static (SRAM1) | 7 | `uint8_t[7]` (pip dir + stopped) |
| `track_play_mode[7]` in region | Already reserved | 7 | Uses PAT4 reserved bytes |
| Morph endpoints (per Scene) | Scene data | 21 | 3 params × 7 tracks |
| **Total new ISR-static** | | **30** | |
| **Total new flash** | | **~368** | Negligible against 214 KB free |

All new ISR-static allocations require user acknowledgement per the RAM
Allocation Approval Policy.

---

## 10. Resolved decisions (from review)

All open questions have been resolved during review. Summary of decisions:

1. **Display:** show the raw 7-bit CC value for non-musical positions, not a
   decimal multiplier. Musical stops show their symbolic name. No float
   display table needed.
2. **Shuffle on stopped once-mode tracks:** a pending shuffle delay fires its
   deferred trigger even after the track has stopped. The step was already
   reached; the shuffle just delays execution.
3. **Scale change mid-playback:** carry the DDA accumulator remainder. No
   reset on scale change — smooth behaviour during morph sweeps is more
   important than exact phase alignment on the first step after a change.
4. **Pip cycle across length changes:** let the cycle position clamp via the
   existing formula and continue. No special handling needed.
5. **No on-device migration:** new defaults only. Offline conversion via
   `tools/convert_scene_scale.py`.
6. **No AutoSave version bump:** user deletes AutoSave files after
   implementation.
7. **Bar display:** must follow the viewed track, not the master grid. Fixed
   as part of this implementation.
