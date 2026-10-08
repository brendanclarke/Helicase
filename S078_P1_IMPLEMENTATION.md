# S078 P1 — Track Settings Implementation Schedule

Implements `S078_P1_TRACK_SETTINGS.md` (accepted).
Seven implementation steps; each independently testable on hardware.

---

## Step 1 — 128-position Q8.8 step-scale LUT and per-track accumulators

### Step 1A — Stage 1: integer-only per-track accumulators

The global `SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` divisor is replaced by
per-track DDA accumulators. Stage 1 uses integer-only tick intervals (Q8.8
with .8 = 0) to exercise accumulator plumbing without fractional behaviour.

---

#### 1A-1 `Core/Sequencer/StepScale.h` — new API, retire old constants

**Lines 18–19 — modify**

Replace `STEP_SCALE_COUNT` (14) and `STEP_SCALE_DEFAULT` (4) with the new
128-position constants.

```
Old:
  #define STEP_SCALE_COUNT   14u
  #define STEP_SCALE_DEFAULT 4u

New:
  #define STEP_SCALE_COUNT   128u
  #define STEP_SCALE_DEFAULT 76u
```

/*
 * 128-position continuous step-scale parameter (S078).
 *
 * What: the full CC 0..127 range replaces the former 14-entry discrete table.
 * STEP_SCALE_DEFAULT is CC 76, the 1/16th note stop on the log curve. Why:
 * the per-track step scale is now a continuous log parameter; the old 14-entry
 * index is retired. Inputs: every caller that clamps or defaults a scale byte.
 * Outputs: storage, menu, and playback all operate on the 0..127 domain.
 * Affiliates: StepScale.c (LUT and labels), sequencer.c (DDA accumulators),
 * PatternData.c/h (default), EffectTypes.h, menu.c, copyClearService.c.
 */

**Line 22 — modify**

Change the `stepScale_ticks()` return doc and signature to return Q8.8:

```
Old:
  uint16_t stepScale_ticks(uint8_t index);

New:
  uint16_t stepScale_ticksQ8(uint8_t cc);
```

/*
 * Return the Q8.8 fixed-point 96-PPQ tick interval for one CC position.
 *
 * What: looks up the 128-entry Q8.8 LUT. Input: CC 0..127 from stored
 * track_scale or FX seq_step_scale. Output: a uint16_t in Q8.8 format
 * (integer part in high byte, fractional part in low byte), representing
 * the number of 96-PPQ ticks per step at this scale position. Out-of-range
 * inputs return the default (CC 76, 1/16 = 24.0 ticks = 0x1800). Affiliates:
 * sequencer.c DDA accumulator, seq_fxClockTick(), seq_realign paths.
 */

**Lines 25–28 — modify**

Rename `stepScale_shortName` → keep but redefine; add a musical-stop query:

```
Old:
  const char *stepScale_shortName(uint8_t index);
  const char *stepScale_longName(uint8_t index);

New:
  const char *stepScale_shortName(uint8_t cc);
  const char *stepScale_longName(uint8_t cc);
  uint8_t stepScale_isMusicalStop(uint8_t cc);
```

/*
 * stepScale_shortName — 3-char display label for one CC position.
 *
 * What: for the 14 musical stops, returns the symbolic name (e.g. "/16",
 * "32t"). For all other CC values, returns NULL; the caller formats the raw
 * CC integer. Input: CC 0..127. Output: pointer to a static 3-char string
 * or NULL. Affiliates: menu.c display handler MENU_TRACK_SCALE.
 *
 * stepScale_longName — full-width label for the 14 musical stops, NULL
 * otherwise. Same pattern.
 *
 * stepScale_isMusicalStop — nonzero when the CC lands on one of the 14
 * nudged musical positions. Input: CC 0..127. Output: 0 or 1. Affiliates:
 * menu.c display path.
 */

---

#### 1A-2 `Core/Sequencer/StepScale.c` — new LUT, new label tables

**Lines 15–28 — remove (old tables)**

Remove `stepScale_tickTable[14]`, `stepScale_shortTable[14][4]`, and
`stepScale_longTable[14]`.

**Add — Q8.8 tick LUT (128 entries, 256 B flash)**

```c
static const uint16_t stepScale_q8Table[128] = {
    /* CC 0..127: round(multiplier * 24 * 256).
     * Stage 1: all entries integer-rounded (low byte = 0x00).
     * Stage 2: true Q8.8 fractional values. */
    ...
};
```

/*
 * 128-entry Q8.8 step-scale tick LUT (S078 §2.4).
 *
 * What: each entry is round(multiplier(cc) * 24 * 256) in Q8.8 format, where
 * multiplier(cc) = 0.25 * 2^(cc / (127/5)), nudged at 14 musical stops.
 * Why: the DDA accumulator in sequencer.c adds 256 (1.0 Q8.8) per PPQ tick
 * and compares against this interval to decide when each track advances.
 * Inputs: the step_scale_0_127.csv nudged multiplier column. Outputs: read
 * by stepScale_ticksQ8(cc). Range: min 1536 (CC 0, 0.25x), max 49152
 * (CC 127, 8.0x); all fit uint16_t. Affiliates: sequencer.c per-track
 * accumulator, seq_fxClockTick(), seq_realignActivePatternToMasterClock().
 */

**Add — musical stop lookup table (14 entries)**

```c
typedef struct {
    uint8_t  cc;
    char     short_name[4];  /* 3 chars + NUL */
    char     long_name[8];   /* up to 7 chars + NUL */
} stepScale_musicalStop_t;

static const stepScale_musicalStop_t stepScale_stops[14] = {
    {  0, "/64", "1/64"   },
    { 16, "32t", "1/32T"  },
    { 38, "/32", "1/32"   },
    { 54, "16t", "1/16T"  },
    { 60, "d32", "d 1/32" },
    { 76, "/16", "1/16"   },
    { 83, "8Tr", "1/8T"   },
    { 86, "d16", "d 1/16" },
    { 93, "/8 ", "1/8"    },
    {100, "4Tr", "1/4T"   },
    {103, "d/8", "d 1/8"  },
    {110, "/4 ", "1/4"    },
    {120, "d/4", "d 1/4"  },
    {127, "/2 ", "1/2"    },
};
```

/*
 * Musical stop label table (S078 §2.2, §2.3).
 *
 * What: the 14 CC positions whose log multipliers are nudged to exact musical
 * subdivisions. Each entry carries the CC value, 3-char short name, and full
 * label. Why: stepScale_shortName() and stepScale_longName() scan this table
 * to return symbolic labels when the parameter is exactly on a musical stop;
 * non-stop positions return NULL so the caller shows the raw CC integer.
 * Inputs: step_scale_0_127.csv musical value column. Outputs: read by the
 * stepScale_shortName/longName/isMusicalStop APIs. Affiliates: menu.c
 * MENU_TRACK_SCALE display.
 */

**Lines 30–49 — remove/rewrite (old accessor functions)**

Replace the three old functions with:

```c
uint16_t stepScale_ticksQ8(uint8_t cc)
{
    return (cc < STEP_SCALE_COUNT)
        ? stepScale_q8Table[cc]
        : stepScale_q8Table[STEP_SCALE_DEFAULT];
}
```

/*
 * Q8.8 tick interval for one CC position (S078 §2.4.1).
 *
 * Input: CC 0..127. Output: Q8.8 uint16_t tick interval; out-of-range CCs
 * return the 1/16 default. Caller: sequencer.c DDA accumulator per PPQ tick.
 * This is an ISR-safe flash-only read with no allocation. Affiliates:
 * stepScale_q8Table[], seq_processSchedulerTick(), seq_fxClockTick().
 */

```c
const char *stepScale_shortName(uint8_t cc)
{
    for (uint8_t i = 0; i < 14u; i++) {
        if (stepScale_stops[i].cc == cc)
            return stepScale_stops[i].short_name;
    }
    return NULL;
}

const char *stepScale_longName(uint8_t cc)
{
    for (uint8_t i = 0; i < 14u; i++) {
        if (stepScale_stops[i].cc == cc)
            return stepScale_stops[i].long_name;
    }
    return NULL;
}

uint8_t stepScale_isMusicalStop(uint8_t cc)
{
    for (uint8_t i = 0; i < 14u; i++) {
        if (stepScale_stops[i].cc == cc)
            return 1u;
    }
    return 0u;
}
```

/*
 * Short/long name and musical-stop query (S078 §2.3).
 *
 * What: linear scan of the 14-entry musical stop table. Returns the symbolic
 * label when the CC is an exact musical stop, NULL otherwise. The caller
 * (menu.c display) formats the raw CC integer when NULL is returned.
 * Why: only 14 of 128 positions have meaningful names; all others show their
 * numeric CC value. Input: CC 0..127. Output: static string pointer or NULL.
 * The 14-entry scan is negligible (foreground only). Affiliates: menu.c
 * MENU_TRACK_SCALE display handler.
 */

---

#### 1A-3 `Core/Sequencer/sequencer.c` — per-track DDA accumulators

**Line 148 (after `seq_stepIndex[NUM_TRACKS]`) — add**

```c
static uint16_t seq_trackAccumulator[NUM_TRACKS];
```

/*
 * Per-track Q8.8 DDA tick accumulator (S078 §2.4.1).
 *
 * What: one uint16_t per track holding the fractional tick remainder. Every
 * PPQ tick adds 256 (1.0 in Q8.8); when the accumulator meets or exceeds the
 * track's Q8.8 interval, a step advance fires and the interval is subtracted.
 * Why: replaces the global SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP modulo test,
 * allowing each track to advance at its own rate from the 128-position log
 * curve. Inputs: incremented by seq_processSchedulerTick(). Reset by
 * seq_setStepIndexToStart(), seq_realignActivePatternToMasterClock(),
 * seq_realignTrackToMasterClock(). Outputs: gates calls to
 * seq_advanceTrackStep(). RAM: 14 bytes ISR-static SRAM1 (7 × uint16_t).
 * Affiliates: stepScale_ticksQ8(), pat_scene_region_t::track_scale.
 */

**Line 530 (seq_init, after memset calls) — add**

```c
memset(seq_trackAccumulator, 0, sizeof(seq_trackAccumulator));
```

/*
 * Zero all DDA accumulators at init (S078).
 *
 * What: clears per-track tick accumulators to 0. Why: boot and full init
 * must start with no fractional remainder. Affiliates: seq_setStepIndexToStart()
 * also clears accumulators on transport reset.
 */

**Lines 1567–1615 (`seq_processSchedulerTick`) — rewrite core loop**

Replace the global `SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` modulo test with
per-track DDA accumulator logic.

Old (lines 1600–1605):
```c
if (seq_initialSchedulerTick == 0u &&
    (seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) == 0u) {
    for (track = 0u; track < NUM_TRACKS; track++)
        seq_advanceTrackStep(track);
    anyAdvanced = 1u;
}
```

New:
```c
if (seq_initialSchedulerTick == 0u) {
    for (track = 0u; track < NUM_TRACKS; track++) {
        const pat_scene_region_t *region =
            pat_sceneRegion(seq_perTrackPattern[track]);
        uint8_t cc = (region) ? region->track_scale[track]
                              : STEP_SCALE_DEFAULT;
        uint16_t interval = stepScale_ticksQ8(cc);

        seq_trackAccumulator[track] += 256u;
        while (seq_trackAccumulator[track] >= interval) {
            seq_trackAccumulator[track] -= interval;
            seq_advanceTrackStep(track);
            anyAdvanced = 1u;
        }
    }
}
```

/*
 * Per-track DDA step advance (S078 §2.4.1, replacing global divisor).
 *
 * What: each PPQ tick adds 1.0 (256 in Q8.8) to every track's accumulator.
 * When accumulator >= interval, a step advance fires and the interval is
 * subtracted. The fractional remainder carries forward for drift-free
 * fractional timing. Why: allows each track to run at its own rate from the
 * 128-position log curve. The old global modulo
 * (seq_elapsedPpqTicks % SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP) is removed.
 * Inputs: seq_trackAccumulator[track], track_scale from the track's played
 * Scene region, stepScale_ticksQ8(). Outputs: seq_advanceTrackStep() called
 * when the threshold is met; anyAdvanced set for LED chase update. The while
 * loop handles the minimum-interval case (CC 0 = 1536, > 256, so at most one
 * iteration per tick). ISR cost: 7 additions + 7 comparisons per PPQ tick.
 * Affiliates: seq_advanceTrackStep(), seq_trackAccumulator[], pat_sceneRegion().
 */

The old `seq_masterStepClock` / `seq_masterStepCnt` global 16th-note counter
continues to be maintained at line 1587–1590 using the existing
`SEQ_INTERNAL_TICKS_PER_DEFAULT_STEP` modulo — this counter is still needed
for bar display, bar boundaries, trigger clock, and MIDI beat. It is no
longer used for track step advance.

**Lines 2175–2208 (`seq_setStepIndexToStart`) — add accumulator reset**

After line 2202 (`seq_clearAutomationDirty()`), inside the `for` loop at
line 2203:

```c
    seq_trackAccumulator[i] = 0u;
```

/*
 * Zero DDA accumulators on transport reset (S078).
 *
 * What: each track's fractional tick remainder is cleared alongside its step
 * cursor. Why: transport start/stop and Pattern-boundary resets must begin
 * with clean accumulator state so step 0 fires on the first boundary.
 * Affiliates: seq_processSchedulerTick() DDA loop, seq_resetStepScheduler().
 */

**Lines 1482–1531 (`seq_realignActivePatternToMasterClock`) — add accumulator phase**

Inside the `for` loop at line 1520, after setting `seq_stepIndex[track]` and
`seq_lastMasterStep[track]`:

```c
        uint16_t interval = stepScale_ticksQ8(region ? region->track_scale[track]
                                                     : STEP_SCALE_DEFAULT);
        uint32_t master_q8 = (uint32_t)seq_elapsedPpqTicks << 8u;
        seq_trackAccumulator[track] = (uint16_t)(master_q8 % interval);
```

Also update the step index computation to use the interval:

```c
        seq_stepIndex[track] = (int16_t)((master_q8 / interval) % len);
```

/*
 * DDA accumulator phase realignment (S078 §2.4.5).
 *
 * What: when realigning a track to the master clock, the accumulator is set
 * to the correct fractional phase (master_ticks_elapsed_Q8 % interval) so
 * the track picks up exactly where it would have been if running from the
 * start. The step index is also computed from the Q8.8 timeline rather than
 * the old master step clock. Why: a realigned track must not lose its phase
 * relationship to the master clock. Inputs: seq_elapsedPpqTicks (global),
 * track's Q8.8 interval. Outputs: seq_trackAccumulator[track] set to the
 * correct remainder, seq_stepIndex[track] set to the correct position.
 * Affiliates: seq_realignTrackToMasterClock() (single-track variant).
 */

**Lines 1548–1565 (`seq_realignTrackToMasterClock`) — add accumulator phase**

Same accumulator phase logic as above, applied to the single-track variant.
After line 1559 (`seq_stepIndex[track] = ...`):

```c
    uint16_t interval = stepScale_ticksQ8(region ? region->track_scale[track]
                                                 : STEP_SCALE_DEFAULT);
    uint32_t master_q8 = (uint32_t)seq_elapsedPpqTicks << 8u;
    seq_trackAccumulator[track] = (uint16_t)(master_q8 % interval);
    seq_stepIndex[track] = (int16_t)((master_q8 / interval) % len);
```

/*
 * Single-track DDA phase realignment (S078 §2.4.5).
 *
 * What: the single-track variant of the all-track realignment above. Used
 * by per-track Scene assignment and double-click realign. Same phase
 * computation and rationale. Affiliates: seq_setTrackPlayedScene().
 */

---

#### 1A-4 `Core/Bank/Scene/Pattern/PatternData.h` — update default

**Line 366 — modify**

```
Old:
  #define TRACK_SCALE_DEFAULT STEP_SCALE_DEFAULT

(No change needed — TRACK_SCALE_DEFAULT already aliases STEP_SCALE_DEFAULT,
which is now 76.)
```

The alias `TRACK_SCALE_DEFAULT` continues to resolve through `STEP_SCALE_DEFAULT`
which was changed in 1A-1 from 4 to 76.

---

#### 1A-5 `Core/Bank/Scene/Pattern/PatternData.c` — update init comment

**Line 836 — modify comment**

```
Old:
  /* track_scale and track_shuffle: stored and persisted but not yet consumed
   * by playback. */

New:
  /* track_scale: consumed by per-track DDA accumulators (S078). */
```

The init code at lines 841–843 already uses `TRACK_SCALE_DEFAULT` (now 76)
and `0u` for shuffle, so no functional change.

---

#### 1A-6 `Core/Menu/menu.c` — display handler for new scale format

**Line 8384 — modify**

```
Old:
  case MENU_TRACK_SCALE:    return (uint8_t)STEP_SCALE_COUNT;

New:
  case MENU_TRACK_SCALE:    return (uint8_t)(STEP_SCALE_COUNT - 1u);
```

/*
 * Track scale max value update (S078 §2.3).
 *
 * What: the maximum encoder value changes from 13 (14-entry table) to 127
 * (128-position curve). STEP_SCALE_COUNT is now 128, so max = 127.
 * Affiliates: encoder clamping, MENU_TRACK_SCALE display.
 */

**Line 8430 — modify**

```
Old:
  case MENU_TRACK_SCALE:    p = stepScale_shortName((uint8_t)curParmVal); break;

New:
  case MENU_TRACK_SCALE: {
      const char *name = stepScale_shortName((uint8_t)curParmVal);
      if (name) {
          p = name;
      } else {
          buf[0] = (curParmVal >= 100u) ? (char)('0' + curParmVal / 100u) : ' ';
          buf[1] = (curParmVal >= 10u)  ? (char)('0' + (curParmVal / 10u) % 10u) : ' ';
          buf[2] = (char)('0' + curParmVal % 10u);
          return;
      }
      break;
  }
```

/*
 * Track scale display formatter (S078 §2.3).
 *
 * What: musical stops show their symbolic name (e.g. "/16"); all other CC
 * positions show the raw 7-bit value right-justified in 3 characters. CC 0
 * is always the musical stop "/64", so bare "0" never appears. Why: decimal
 * multipliers are not informative on a 3-character display. Inputs:
 * curParmVal 0..127. Outputs: 3-char buf written directly for numeric
 * values, or pointer to static label for musical stops. Affiliates:
 * stepScale_shortName(), stepScale_isMusicalStop().
 */

**Line 14601 — modify**

```
Old:
  parameter_values[PAR_TRACK_SCALE]   = TRACK_SCALE_DEFAULT;

(No code change needed — TRACK_SCALE_DEFAULT is already used and now resolves
to 76.)
```

**Line 1059 — modify**

```
Old:
  [PAR_TRACK_SCALE] = DTYPE_MENU|(MENU_TRACK_SCALE<<4),

New:
  [PAR_TRACK_SCALE] = DTYPE_MENU|(MENU_TRACK_SCALE<<4),

(No change — the DTYPE_MENU encoding is retained; the max-value function
and display function handle the new 0..127 range.)
```

---

#### 1A-7 `Core/DSP/Effects/EffectTypes.h` — update FX scale constants

**Lines 99–100 — modify**

```
Old:
  #define EFFECT_SEQ_SCALE_COUNT           STEP_SCALE_COUNT
  #define EFFECT_SEQ_SCALE_DEFAULT         STEP_SCALE_DEFAULT

(No code change needed — these aliases resolve through StepScale.h, which
now defines STEP_SCALE_COUNT = 128 and STEP_SCALE_DEFAULT = 76.)
```

Verify the `_Static_assert` in `EffectsManager.c:161` still passes.

---

#### 1A-8 `Core/Menu/CopyClear/copyClearService.c` — update clear default

**Line 1021 — modify**

```
Old:
  pat_setTrackScale(job->scene, job->track, TRACK_SCALE_DEFAULT);

(No code change needed — TRACK_SCALE_DEFAULT now resolves to 76.)
```

---

#### 1A-9 `Core/Hardware/SD/filesystem.c` — Pattern reader: add `track_play_mode`

No change for Step 1A. The existing reader at lines 3036–3042, 13162–13167,
15192–15197, and 28410–28417 reads `track_scale` as a raw byte. The old
14-index values (0..13) will now map to the bottom of the 128-position curve
(very fast scales near 1/64). This is the intended no-migration behaviour
(S078 §2.5).

---

### Step 1B — Stage 2: fractional Q8.8 accumulators

#### 1B-1 `Core/Sequencer/StepScale.c` — switch LUT to true Q8.8

**`stepScale_q8Table[128]` — modify values**

Replace integer-rounded entries with true Q8.8 values computed from
`round(nudged_multiplier * 24 * 256)` using `step_scale_0_127.csv`.

No code structure change; only the numeric table contents change.

/*
 * Stage 2 Q8.8 LUT (S078 §2.4.4).
 *
 * What: the low byte of each entry now carries the fractional tick part.
 * Why: Stage 1 validated per-track accumulator plumbing with integer-only
 * intervals. Stage 2 activates the truly continuous character of the curve —
 * adjacent CC positions produce audibly distinct step rates instead of
 * collapsing to the same integer tick count. Inputs: step_scale_0_127.csv
 * nudged multiplier column. Outputs: DDA accumulator fires at fractionally
 * spaced intervals, averaging the exact mathematical rate over time.
 */

---

## Step 2 — FX sequencer migration (Stage 3)

#### 2-1 `Core/Sequencer/sequencer.c` — FX DDA accumulator

**Line 98 (after `seq_fxEvent`) — add**

```c
static uint16_t seq_fxAccumulator = 0u;
```

/*
 * FX sequencer Q8.8 DDA accumulator (S078 §2.4.4 Stage 3).
 *
 * What: one uint16_t holding the FX sequencer's fractional tick remainder,
 * replacing the integer modulo in seq_fxClockTick(). Why: the FX sequencer
 * shares the same 128-position curve as Pattern tracks and must support
 * fractional tick intervals for non-musical CC positions. Inputs:
 * incremented 256 per PPQ tick; reset by seq_setStepIndexToStart() and
 * seq_fxPublishReset(). Outputs: gates seq_fxPublishStep() calls. RAM: 2
 * bytes ISR-static SRAM1. Affiliates: stepScale_ticksQ8(),
 * scene_effectConst()->seq_step_scale.
 */

**Lines 491–527 (`seq_fxClockTick`) — rewrite to use DDA accumulator**

```
Old:
  ticks = stepScale_ticks(record->seq_step_scale);
  if ((seq_elapsedPpqTicks % ticks) != 0u)
      return;
  ...
  n = seq_elapsedPpqTicks / ticks;

New:
  uint16_t interval = stepScale_ticksQ8(record->seq_step_scale);
  seq_fxAccumulator += 256u;
  if (seq_fxAccumulator < interval)
      return;
  seq_fxAccumulator -= interval;
  ...
  /* Step position must be tracked statelessly or via a separate counter.
   * For pip/rev/rnd modes, maintain a seq_fxStepCounter that increments
   * each time the accumulator fires, and derive the display index from it
   * using the same formulas as before. */
```

/*
 * FX sequencer DDA clock (S078 §2.4.4 Stage 3).
 *
 * What: replaces the integer modulo (seq_elapsedPpqTicks % ticks) with the
 * same Q8.8 DDA accumulator model used by Pattern tracks. Why: the FX
 * sequencer shares the 128-position curve and must handle fractional tick
 * intervals. The step counter replaces the stateless n = ticks/elapsed
 * computation because fractional intervals make that division ambiguous.
 * Inputs: seq_fxAccumulator, interval from stepScale_ticksQ8(). Outputs:
 * seq_fxPublishStep() on each boundary. Reset: seq_setStepIndexToStart()
 * and seq_fxPublishReset(). Affiliates: effects_service() foreground consumer.
 */

**Add to `seq_fxClockTick` area — FX step counter**

```c
static uint32_t seq_fxStepCounter = 0u;
```

/*
 * FX sequencer step counter (S078 Stage 3).
 *
 * What: monotonically incremented each time the FX DDA accumulator fires.
 * Replaces the former stateless n = seq_elapsedPpqTicks / ticks for
 * computing fwd/rev/pip/rnd step indices. Why: with fractional tick
 * intervals, integer division of the master clock no longer produces the
 * correct step count. Input: incremented in seq_fxClockTick(). Reset:
 * seq_setStepIndexToStart(). Output: used to compute the FX step index.
 */

**Lines 2175–2208 (`seq_setStepIndexToStart`) — add FX accumulator reset**

After the track accumulator reset:

```c
seq_fxAccumulator = 0u;
seq_fxStepCounter = 0u;
```

---

## Step 3 — Per-track shuffle consumption

#### 3-1 `Core/Sequencer/sequencer.c` — shuffle delay state

**After `seq_trackAccumulator[NUM_TRACKS]` — add**

```c
static uint8_t seq_trackShuffleDelay[NUM_TRACKS];
static uint8_t seq_trackShufflePending[NUM_TRACKS];
```

/*
 * Per-track shuffle delay counters (S078 §3.2).
 *
 * What: seq_trackShuffleDelay[track] counts down the remaining PPQ ticks
 * before a shuffle-deferred step fires. seq_trackShufflePending[track] is
 * nonzero when a step trigger has been deferred and is waiting for its
 * delay to expire. Why: shuffle delays even-numbered steps (0-indexed
 * 1, 3, 5, ...) by a fraction of the 1/16th note interval. The delay is
 * always based on 24 PPQ ticks (the 1/16th grid at 96 PPQ), not the track's
 * step scale. Inputs: set when seq_advanceTrackStep() reaches an odd step
 * with nonzero shuffle. Decremented each PPQ tick. Outputs: the deferred
 * trigger fires when the counter reaches zero. RAM: 14 bytes ISR-static
 * SRAM1 (7 × uint8_t × 2). Affiliates: pat_scene_region_t::track_shuffle,
 * seq_triggerVoice().
 */

**In `seq_init()` — add**

```c
memset(seq_trackShuffleDelay, 0, sizeof(seq_trackShuffleDelay));
memset(seq_trackShufflePending, 0, sizeof(seq_trackShufflePending));
```

**In `seq_setStepIndexToStart()` — add**

```c
    seq_trackShuffleDelay[i] = 0u;
    seq_trackShufflePending[i] = 0u;
```

#### 3-2 `Core/Sequencer/sequencer.c` — shuffle in `seq_processSchedulerTick`

**In the per-PPQ-tick loop (before the DDA accumulator check) — add**

```c
for (track = 0u; track < NUM_TRACKS; track++) {
    if (seq_trackShufflePending[track]) {
        if (seq_trackShuffleDelay[track] == 0u) {
            seq_trackShufflePending[track] = 0u;
            /* Fire the deferred trigger — specials already resolved. */
            seq_triggerVoice(track, seq_trackShuffleVel[track],
                            seq_trackShuffleNote[track]);
        } else {
            seq_trackShuffleDelay[track]--;
        }
    }
}
```

/*
 * Shuffle delay tick-down (S078 §3.2).
 *
 * What: each PPQ tick decrements the shuffle delay counter for any track
 * with a pending deferred trigger. When the counter reaches zero, the
 * deferred trigger fires. Why: shuffle must operate at PPQ tick resolution
 * for smooth swing feel. The delay is computed from
 * (shuffle_value * 24) / 256, which produces 0..11 ticks of delay at 96 PPQ.
 * Inputs: seq_trackShufflePending[], seq_trackShuffleDelay[]. Outputs:
 * seq_triggerVoice() when delay expires. Affiliates: seq_advanceTrackStep()
 * sets up the deferred trigger.
 */

#### 3-3 `Core/Sequencer/sequencer.c` — shuffle in `seq_advanceTrackStep`

**Lines 1095–1173 (`seq_advanceTrackStep`) — modify trigger path**

After step_allowed and pat_isStepActive checks, where `seq_triggerVoice` is
currently called (line 1157):

```
Old:
  seq_triggerVoice(track, sp.velocity, sp.note);

New:
  uint8_t shuffle_val = region ? region->track_shuffle[track] : 0u;
  uint8_t is_odd_step = (uint8_t)(seq_stepIndex[track] & 1u);
  if (shuffle_val > 0u && is_odd_step) {
      uint8_t delay = (uint8_t)((uint16_t)shuffle_val * 24u / 256u);
      if (delay > 0u) {
          seq_trackShuffleDelay[track] =
              (uint8_t)(delay - 1u);
          seq_trackShufflePending[track] = 1u;
          seq_trackShuffleVel[track] = sp.velocity;
          seq_trackShuffleNote[track] = sp.note;
      } else {
          seq_triggerVoice(track, sp.velocity, sp.note);
      }
  } else {
      seq_triggerVoice(track, sp.velocity, sp.note);
  }
```

/*
 * Shuffle trigger deferral (S078 §3.2).
 *
 * What: odd-indexed steps (1, 3, 5, ...) with nonzero shuffle are deferred
 * by (shuffle_value * 24) / 256 PPQ ticks. Even steps fire immediately.
 * The delay counter is set to (delay - 1u) because the countdown loop in
 * seq_processShuffleDelays() decrements first and fires when delay reaches
 * 0 on the subsequent tick — setting delay = N would give N+1 ticks of
 * actual delay; the subtraction is safe because delay > 0u is guaranteed.
 * Why: shuffle is a groove/feel concept that delays every other beat. The
 * delay is always based on the 1/16th grid (24 PPQ ticks), not the track's
 * step scale, so it vanishes for large scales (musically correct). Inputs:
 * region->track_shuffle[track], seq_stepIndex[track] parity. Outputs:
 * immediate trigger or deferred trigger setup. Affiliates:
 * seq_processSchedulerTick() shuffle tick-down loop.
 */

Additional ISR-static arrays for deferred trigger data:

```c
static uint8_t seq_trackShuffleVel[NUM_TRACKS];
static uint8_t seq_trackShuffleNote[NUM_TRACKS];
```

---

## Step 4 — Play modes

#### 4-1 `Core/Bank/Scene/Pattern/PatternData.h` — add `track_play_mode`

**Line 85 (after `track_shuffle[NUM_TRACKS]`) — add**

```c
    uint8_t track_play_mode[NUM_TRACKS];
```

/*
 * Per-track play mode (S078 §4.1).
 *
 * What: one byte per track selecting the playback direction: 0 fwd, 1 rev,
 * 2 pip, 3 rnd, 4 onc (once synced), 5 1fr (once free). Values >= 6 are
 * treated as fwd. Why: allows each track to have an independent playback
 * direction, matching the FX sequencer's existing run modes. Inputs: Menu
 * edits, Scene/Pattern loads. Outputs: seq_advanceTrackStep() direction logic.
 * Storage: one byte per track in PAT4 track header reserved bytes (offset 3).
 * RAM: 7 bytes in pat_scene_region_t per Scene. Affiliates: sequencer.c
 * play mode ISR logic, filesystem.c PAT4 reader/writer.
 */

**Line 90 (`_Static_assert`) — update**

The `_Static_assert` for `pat_scene_region_t` size must be updated to account
for the additional 7 bytes from `track_play_mode[NUM_TRACKS]`. New expected
size = old size + 7.

#### 4-2 `Core/Bank/Scene/Pattern/PatternData.c` — init play mode, add setter

**Line 843 (in `pat_initScene` init loop) — add**

```c
        region->track_play_mode[track] = 0u;  /* fwd */
```

**After `pat_setTrackShuffle` (~line 1173) — add**

```c
void pat_setTrackPlayMode(uint8_t scene_index, uint8_t track, uint8_t value)
{
    pat_scene_region_t *region = pat_sceneRegionMut(scene_index);
    if (!region || track >= NUM_TRACKS)
        return;
    region->track_play_mode[track] = value;
    pat_markSceneDirty(scene_index);
}
```

/*
 * Per-track play mode setter (S078 §4.1).
 *
 * What: writes the play mode byte for one track in the Pattern region.
 * Input: scene_index, track 0..6, value 0..5. Output: region updated,
 * Pattern AutoSave marked dirty. Affiliates: menu.c PAR_TRACK_PLAY_MODE
 * edit handler, pat_applyTrackSettingsToMenu().
 */

**Line 1141 (`pat_applyTrackSettingsToMenu`) — add**

```c
    parameter_values[PAR_TRACK_PLAY_MODE] = region->track_play_mode[track];
```

#### 4-3 `Core/Bank/Scene/Pattern/PatternData.c` — region copy body

**Line 1847 (after `track_shuffle` memcpy) — add**

```c
    memcpy(dst->track_play_mode, src->track_play_mode,
           sizeof(dst->track_play_mode));
```

#### 4-4 `Core/Bank/Scene/Preset/ParameterArray.h` — add PAR_TRACK_PLAY_MODE

**After line 110 (`PAR_TRACK_MIDI_NOTE`) — add**

```c
    PAR_TRACK_PLAY_MODE,
```

/*
 * Per-track play mode flat parameter (S078 §4.1).
 *
 * What: menu display/edit mirror for the track's play mode byte. Input: Menu
 * track settings page. Output: parameter_values[PAR_TRACK_PLAY_MODE] holds
 * 0..5. Affiliates: pat_applyTrackSettingsToMenu(), pat_setTrackPlayMode(),
 * menuPages.h SEQ_PAGE layout.
 */

#### 4-5 `Core/Sequencer/sequencer.c` — play mode ISR state

**After `seq_trackShuffleNote[NUM_TRACKS]` — add**

```c
static uint8_t seq_trackPlayState[NUM_TRACKS];
#define SEQ_PLAY_STATE_STOPPED  (1u << 0)
#define SEQ_PLAY_STATE_PIP_REV  (1u << 1)
```

/*
 * Per-track play state (S078 §4.4).
 *
 * What: one byte per track packing runtime play mode state. Bit 0 is the
 * "stopped" flag for once modes. Bit 1 is the pip direction (0 = forward,
 * 1 = reverse). Remaining bits reserved. Why: the DDA owns WHEN a step
 * fires; this state owns WHERE the step index goes. Inputs: set by
 * seq_advanceTrackStep() direction logic, cleared by retrigger events.
 * Outputs: gates step advance for stopped once-mode tracks; controls pip
 * direction toggle. RAM: 7 bytes ISR-static SRAM1. Affiliates:
 * seq_setStepIndexToStart(), seq_setRunning(), seq_selectActivePattern(),
 * seq_setTrackPlayedScene().
 */

**In `seq_init()` and `seq_setStepIndexToStart()` — add**

```c
memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
```

#### 4-6 `Core/Sequencer/sequencer.c` — rewrite `seq_advanceTrackStep` direction

**Lines 1120–1122 (step increment and wrap) — replace with play mode switch**

```
Old:
  seq_stepIndex[track]++;
  if (seq_stepIndex[track] >= (int16_t)len)
      seq_stepIndex[track] = 0;

New:
  uint8_t play_mode = (region) ? region->track_play_mode[track] : 0u;
  if (play_mode >= 6u) play_mode = 0u;

  if ((play_mode == 4u || play_mode == 5u) &&
      (seq_trackPlayState[track] & SEQ_PLAY_STATE_STOPPED))
      return;

  switch (play_mode) {
  case 0u: /* fwd */
      seq_stepIndex[track]++;
      if (seq_stepIndex[track] >= (int16_t)len)
          seq_stepIndex[track] = 0;
      break;
  case 1u: /* rev */
      seq_stepIndex[track]--;
      if (seq_stepIndex[track] < 0)
          seq_stepIndex[track] = (int16_t)(len - 1u);
      break;
  case 2u: { /* pip */
      uint16_t cycle = (uint16_t)(len * 2u);
      /* pip_pos tracks position 0..cycle-1 in the 6-bit counter field. */
      uint8_t pip_pos = /* advance within cycle */ ...;
      seq_stepIndex[track] = (pip_pos < len)
          ? (int16_t)pip_pos
          : (int16_t)((uint16_t)(len * 2u - 1u) - pip_pos);
      break;
  }
  case 3u: /* rnd */
      seq_stepIndex[track] = (int16_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
      break;
  case 4u: /* onc — synced */
  case 5u: /* 1fr — once free */
      seq_stepIndex[track]++;
      if (seq_stepIndex[track] >= (int16_t)len) {
          seq_stepIndex[track] = (int16_t)(len - 1u);
          seq_trackPlayState[track] |= SEQ_PLAY_STATE_STOPPED;
          return;
      }
      break;
  }
```

/*
 * Play mode direction logic (S078 §4.4).
 *
 * What: replaces the unconditional increment-and-wrap with a per-track
 * direction switch. FWD: existing behaviour. REV: decrement, wrap at 0 to
 * length-1. PIP: cycle of 2×length, boundaries play twice. RND: uniform
 * random within length. ONC/1FR: forward one-pass, set stopped flag at end.
 * Why: allows each track to have an independent playback direction and mode.
 * Inputs: region->track_play_mode[track], seq_trackPlayState[track]. Outputs:
 * seq_stepIndex[track] updated, stopped flag set for once modes. Affiliates:
 * seq_trackPlayState[] retrigger logic, seq_setTrackPlayedScene().
 */

#### 4-7 `Core/Sequencer/sequencer.c` — retrigger on Scene change

**In `seq_selectActivePattern()` (~line 789) — add**

Clear stopped flags for all tracks:

```c
memset(seq_trackPlayState, 0, sizeof(seq_trackPlayState));
```

**In `seq_setTrackPlayedScene()` (~line 620) — add**

Clear stopped flag for the reassigned track(s):

```c
    seq_trackPlayState[track] &= (uint8_t)~SEQ_PLAY_STATE_STOPPED;
    if (track >= 5u) {
        seq_trackPlayState[5] &= (uint8_t)~SEQ_PLAY_STATE_STOPPED;
        seq_trackPlayState[6] &= (uint8_t)~SEQ_PLAY_STATE_STOPPED;
    }
```

/*
 * Once-mode retrigger on Scene change/per-track assignment (S078 §4.3).
 *
 * What: clears the stopped flag so a once-mode track restarts its one-pass
 * playback. For per-track assignment, only the reassigned track(s) are
 * retriggered. Voice 6 assigns both tracks 5 and 6 together. Why: Scene
 * change and per-track reassignment are defined retrigger events; double-
 * click realign is not. Affiliates: seq_setRunning() (transport retrigger).
 */

**In `seq_setRunning()` (line 1802) — add**

Clear stopped flags on transport start (both start and stop paths go through
`seq_setStepIndexToStart()` which already clears via memset).

#### 4-8 `Core/Hardware/SD/filesystem.c` — PAT4 reader/writer for `track_play_mode`

**Line 3041 (Pattern writer, in track header loop) — add**

```c
        header[base + 3u] = region->track_play_mode[track];
```

**Lines 13167, 15197, 28417 (Pattern readers, in track header loops) — add**

```c
                region->track_play_mode[track] = staging_buf[base + 3u];
```

/*
 * PAT4 track_play_mode serialization (S078 §4.5).
 *
 * What: byte offset 3 in each track's 16-byte header block stores the play
 * mode. Old PAT4 files have 0x00 in this byte (reserved/zero-filled), which
 * maps to fwd (correct default). Why: play mode must survive save/load
 * cycles. Inputs: pat_scene_region_t::track_play_mode[track]. Outputs: one
 * byte per track in the PAT4 track header. Affiliates: the three reader
 * paths (root Pattern Load, Scene Load Pattern, AutoSave Pattern boot).
 */

#### 4-9 `Core/Menu/CopyClear/copyClearService.c` — copy/clear track play mode

**Line 852 (track copy, after shuffle) — add**

```c
                pat_setTrackPlayMode(job->scene, job->track,
                                     s->track_play_mode[src->track]);
```

**Line 1022 (track clear, after shuffle) — add**

```c
            pat_setTrackPlayMode(job->scene, job->track, 0u);
```

#### 4-10 `Core/Menu/menuPages.h` — update SEQ_PAGE layout

**Line 89 — modify**

```
Old:
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_MIDI_CHANNEL,TEXT_NOTE,TEXT_SHUFFLE,TEXT_EMPTY,TEXT_EMPTY,TEXT_EMPTY, PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,PAR_SHUFFLE,PAR_NONE,PAR_NONE,PAR_NONE},

New:
  {TEXT_PAT_LENGTH,TEXT_TRACK_SCALE,TEXT_SHUFFLE,TEXT_TRACK_PLAY_MODE,TEXT_MIDI_CHANNEL,TEXT_NOTE,TEXT_EMPTY,TEXT_EMPTY, PAR_TRACK_LENGTH,PAR_TRACK_SCALE,PAR_SHUFFLE,PAR_TRACK_PLAY_MODE,PAR_TRACK_MIDI_CHAN,PAR_TRACK_MIDI_NOTE,PAR_NONE,PAR_NONE},
```

/*
 * SEQ_PAGE track settings layout update (S078 §7).
 *
 * What: groups the three morphable parameters (len, scl, shf) together on
 * the left, followed by play mode (mod), then MIDI channel and note. Two
 * empty slots remain. Why: keeps the most performance-relevant parameters
 * together and adds the new play mode cell. Inputs: TEXT_TRACK_PLAY_MODE and
 * PAR_TRACK_PLAY_MODE (new constants). Affiliates: menu.c edit handler,
 * ParameterArray.h.
 */

New MenuText constants needed: `TEXT_TRACK_PLAY_MODE`. New display dtype
for play mode: `DTYPE_MENU|(MENU_TRACK_PLAY_MODE<<4)` with short labels
`fwd`/`rev`/`pip`/`rnd`/`onc`/`1fr`.

#### 4-11 `Core/Menu/menu.c` — play mode edit handler and display

**In the dtype table (~line 1059) — add**

```c
    [PAR_TRACK_PLAY_MODE] = DTYPE_MENU|(MENU_TRACK_PLAY_MODE<<4),
```

**In `getMenuItemMaxVal` (~line 8384) — add**

```c
    case MENU_TRACK_PLAY_MODE:  return 5u;
```

**In `getMenuItemNameForValue` (~line 8430) — add**

```c
    case MENU_TRACK_PLAY_MODE: {
        static const char playModeNames[][4] = {
            "fwd", "rev", "pip", "rnd", "onc", "1fr"
        };
        uint8_t m = (curParmVal <= 5u) ? curParmVal : 0u;
        p = playModeNames[m]; break;
    }
```

**In the edit switch (~line 13839) — add**

```c
    case PAR_TRACK_PLAY_MODE:
        pat_setTrackPlayMode(menu_getViewedPattern(), menu_getActiveVoice(), value);
        break;
```

---

## Step 5 — Morphability (effective-getter architecture)

The morph worker does **not** write interpolated values into the Pattern
region — doing so would overwrite retained Normal values and mark the
Pattern dirty. Instead, it follows the S075 FX-send Morph pattern:
Pattern region keeps Normal values; Scene settings stores Morph endpoints;
the morph worker computes effective values cached in ISR-static arrays;
the sequencer reads the effective arrays.

#### 5-1 `Core/Bank/Scene/SceneData.h` — morph endpoint storage in Scene settings

**After line 233 (`bus_comp[SCENE_BUS_COMP_FIELD_COUNT]`) — add**

```c
    uint8_t track_morph_length[NUM_TRACKS];
    uint8_t track_morph_scale[NUM_TRACKS];
    uint8_t track_morph_shuffle[NUM_TRACKS];
```

21 bytes per Scene (3 × 7), 336 bytes total across 16 Scenes. No AutoSave
version bump — user deletes AutoSave files after implementation.

#### 5-2 `Core/Bank/Scene/SceneData.c` — defaults and setters

**In `scene_settingsDefaults()` — add**

```c
    for (uint8_t t = 0u; t < NUM_TRACKS; t++) {
        out->track_morph_length[t] = NUM_STEPS_PER_BAR;
        out->track_morph_scale[t] = STEP_SCALE_DEFAULT;
        out->track_morph_shuffle[t] = 0u;
    }
```

Fresh Scenes start with Morph endpoints matching the Normal defaults
(length 16, scale 76, shuffle 0), so Morph amount 0..255 produces no
change until the user edits the Morph endpoints.

**Add setter/getter pairs** for `scene_setTrackMorphLength/Scale/Shuffle()`
and matching getters. Each setter clamps and marks the corresponding
AutoSave Scene parameter cell.

#### 5-3 `Core/Bank/Scene/Autosave.h` — new Scene parameter cells

**After line 258 (`AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE = 45`) — add**

```c
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_LENGTH_BASE = 51,   /* 51..57 */
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SCALE_BASE = 58,    /* 58..64 */
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE = 65,  /* 65..71 */
    AUTOSAVE_SCENE_PARAM_COUNT = 72
```

Update `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES` from 51u to 72u. 72 <= 118
capacity, so the allocation remains valid.

#### 5-4 `Core/Bank/Scene/Preset/presetMorphEngine.c` — track morph worker with effective-value arrays

**Add ISR-static effective-value arrays in `sequencer.c`:**

```c
static uint8_t seq_effectiveTrackLength[NUM_TRACKS];
static uint8_t seq_effectiveTrackScale[NUM_TRACKS];
static uint8_t seq_effectiveTrackShuffle[NUM_TRACKS];
```

**Add a new track morph pass in `presetMorphEngine.c` after the
per-descriptor pass completes:**

```c
static void presetMorph_trackTick(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene) return;
    const pat_scene_region_t *region = pat_sceneRegion(scene_index);
    if (!region) return;

    for (uint8_t track = 0u; track < NUM_TRACKS; track++) {
        uint8_t slot = (track <= 5u) ? track : 5u;
        uint8_t amount = scene->settings.voice_morph_amount[slot];

        uint8_t len = presetMorph_interpolate(
            region->track_length[track],
            scene->settings.track_morph_length[track], amount);
        if (len < 1u) len = 1u;
        seq_effectiveTrackLength[track] = len;

        seq_effectiveTrackScale[track] = presetMorph_interpolate(
            region->track_scale[track],
            scene->settings.track_morph_scale[track], amount);

        seq_effectiveTrackShuffle[track] = presetMorph_interpolate(
            region->track_shuffle[track],
            scene->settings.track_morph_shuffle[track], amount);
    }
}
```

The effective arrays are initialised from Normal values at pattern load /
scene change (morph amount 0 path). The sequencer reads
`seq_effectiveTrackLength[track]` etc. instead of
`region->track_length[track]` in `seq_advanceTrackStep()`,
`seq_processSchedulerTick()`, and the DDA interval lookup.

#### 5-5 `Core/Hardware/SD/storageTypes.c` — sceneset parser for morph endpoints

**After `fx_send_morph` parser block (~line 701) — add three CSV keys:**
`track_morph_length`, `track_morph_scale`, `track_morph_shuffle`. Each
parses 7 comma-separated uint8_t values via `storage_parseCsvU8()`.
Missing keys in old sceneset files leave the defaults from
`scene_settingsDefaults()`.

#### 5-6 `Core/Hardware/SD/filesystem.c` — sceneset writer for morph endpoints

**After case 14 (fx_send_morph write) — add cases 15, 16, 17** using
`filesystem_formatAssignmentCsvU8Line()` for the three morph endpoint
arrays.

#### 5-7 `Core/Menu/menu.c` — STEP morph UI (SHIFT overlay)

**SHIFT held on the STEP front page** shows and edits the Scene's track
morph endpoints instead of the Pattern's Normal values. This follows the
existing `voiceModeShowMorph` pattern used by VOICE pages:

- Add a `stepModeShowMorph` flag (or extend `voiceModeShowMorph` scope).
- When the flag is set, the three morphable cells (len, scl, shf) on the
  STEP page resolve against the Scene morph endpoint fields instead of the
  Pattern region. The play mode cell (`mod`) is hidden or greyed in morph
  view (discrete, non-morphable).
- SHIFT hold sets the flag (`buttonHandler.c`, extend the SHIFT-hold path
  at lines 1793-1820 to cover MODE_STEP). SHIFT release clears it (extend
  lines 1990-2022). Latching via SHIFT+MODE_STEP toggles it (extend
  lines 1129-1149).
- Editing while the flag is set writes to the Scene morph endpoints via
  `scene_setTrackMorphLength/Scale/Shuffle()` and calls
  `preset_rebuildMorph()` to refresh interpolation.
- Display reads from the Scene's morph endpoint fields when the flag is
  set, otherwise from the Pattern region's Normal values.

#### 5-8 `Core/Menu/CopyClear/copyClearService.c` — morph copy/clear for track endpoints

Extend the existing "scene → morph" copy and "reset morph" clear
operations to include track morph endpoints:

- **CC_COPY_MORPH / CC_COPY_SCENE_MORPH:** copy Normal
  `track_length/scale/shuffle` from the Pattern region into the Scene
  morph endpoint fields.
- **CC_CLEAR_RESET_MORPH / CC_CLEAR_SCENE_RESET_MORPH:** equalise the
  morph endpoints to the current Normal values (setting morph amount to 0
  effectively).

#### 5-9 `Core/Bank/Scene/SceneModTargets.h/.c` — new automation target kinds

Add `SCENE_MOD_TARGET_KIND_TRACK_LENGTH/SCALE/SHUFFLE` and 21 new target
table entries (7 tracks × 3 parameters), IDs 21..41. Automation writes
to the effective arrays; the restore path recomputes effective values from
Normal + Morph endpoint + morph amount.

#### 5-10 `Core/Sequencer/sequencer.c` — scene automation apply for track targets

In `seq_applySceneAutomation()`, add cases for the three new target
kinds. Automation values write to the `seq_effectiveTrack*` arrays
directly (runtime overlays). `seq_restoreAllSceneAutomation()` restores
by recomputing the effective value from `presetMorph_interpolate(normal,
morph_endpoint, amount)` — no separate base-value store needed since the
Normal and Morph endpoint are always available.

---

## Step 6 — Conversion utility

#### 6-1 `tools/convert_scene_scale.py` — add (new file)

A Python script that reads PAT4 and `.fx` files from an SD card image,
remaps old 14-index scale bytes to the corresponding new CC positions using
a 14-entry remap table, and writes updated files back.

```python
REMAP_TABLE = {
    0:  0,    # 1/64
    1: 16,    # 1/32 triplet
    2: 38,    # 1/32
    3: 54,    # 1/16 triplet
    4: 76,    # 1/16 (default)
    5: 83,    # 1/8 triplet
    6: 86,    # dotted 1/16
    7: 93,    # 1/8
    8: 100,   # 1/4 triplet
    9: 103,   # dotted 1/8
    10: 110,  # 1/4
    11: 127,  # 1/2 note (192 ticks)
    12: 127,  # 1 bar → clamp to 1/2 (exceeds new 8.0x max)
    13: 127,  # 2 bars → clamp to 1/2 (exceeds new 8.0x max)
}
```

/*
 * Offline scale migration utility (S078 §2.5).
 *
 * What: reads PAT4 files and .fx files, remaps old 14-index track_scale and
 * seq_step_scale bytes to the new 128-position CC values, recalculates CRC,
 * writes updated files. Why: no on-device migration is performed; users who
 * need to migrate existing card content run this utility. Inputs: PAT4 file
 * paths. Outputs: updated PAT4 and .fx files with correct scale CC values.
 *
 * Note: old indices 11 (192 ticks = 1/2) and 12/13 (384/768 ticks = 1 bar/
 * 2 bars) exceed the new curve's maximum of 8.0× (1/2 note). These are
 * mapped to CC 127 (1/2) with a warning. The user must manually adjust
 * patterns that used 1-bar or 2-bar step scales.
 */

---

## Step 7 — Bar display fix

#### 7-1 `Core/Menu/menu.c` — `menu_currentBar` follows viewed track

**Find and modify every site where `menu_currentBar` is updated during
playback chase:**

The bar display currently follows `seq_masterStepClock`, which is the global
1/16th-note counter. It should follow `seq_stepIndex[menu_getActiveVoice()]`
from the viewed track's actual position.

Specific investigation needed: `menu_currentBar` is assigned in `menu.c` and
read by `ledHandler.c`. The fix requires computing the bar from the viewed
track's step index and length, not from the master clock. This is a UI-only
change with no ISR impact.

```c
/* Compute the bar from the viewed track's position: */
uint8_t viewed_track = menu_getActiveVoice();
uint8_t step = (uint8_t)seq_stepIndex[viewed_track];
menu_currentBar = step / NUM_STEPS_PER_BAR;
```

/*
 * Bar display follows viewed track (S078 §10.7).
 *
 * What: menu_currentBar is derived from the viewed track's step index, not
 * from the master step clock. Why: with per-track step scales, each track
 * may be at a different position in its pattern; the bar display should show
 * where the viewed track is, not where the master clock is. Inputs:
 * seq_stepIndex[menu_getActiveVoice()]. Outputs: menu_currentBar, which
 * ledHandler.c reads for STEP LED page rendering and SELECT bar indicator.
 * Affiliates: ledHandler.c (reads menu_currentBar at lines 1206, 1261,
 * 1338, 1370, 1446), buttonHandler.c (reads at lines 472, 484).
 */

---

## Summary of new ISR-static SRAM1 allocations

| Variable | Type | Bytes | Owner |
|:---------|:-----|------:|:------|
| `seq_trackAccumulator[7]` | `uint16_t[7]` | 14 | sequencer.c |
| `seq_fxAccumulator` | `uint16_t` | 2 | sequencer.c |
| `seq_fxStepCounter` | `uint32_t` | 4 | sequencer.c |
| `seq_trackShuffleDelay[7]` | `uint8_t[7]` | 7 | sequencer.c |
| `seq_trackShufflePending[7]` | `uint8_t[7]` | 7 | sequencer.c |
| `seq_trackShuffleVel[7]` | `uint8_t[7]` | 7 | sequencer.c |
| `seq_trackShuffleNote[7]` | `uint8_t[7]` | 7 | sequencer.c |
| `seq_trackPlayState[7]` | `uint8_t[7]` | 7 | sequencer.c |
| `seq_effectiveTrackLength[7]` | `uint8_t[7]` | 7 | sequencer.c (Step 5) |
| `seq_effectiveTrackScale[7]` | `uint8_t[7]` | 7 | sequencer.c (Step 5) |
| `seq_effectiveTrackShuffle[7]` | `uint8_t[7]` | 7 | sequencer.c (Step 5) |
| **Total new ISR-static** | | **76** | |

Note: the proposal estimated 72 bytes (30 original + 21 effective arrays +
21 shuffle buffers). The measured 55 bytes from Steps 1-4 plus the 21 bytes
from effective arrays in Step 5 yields 76. This is still negligible relative
to SRAM1 capacity.

## Summary of new flash allocations

| Item | Bytes |
|:-----|------:|
| `stepScale_q8Table[128]` | 256 |
| `stepScale_stops[14]` | ~168 |
| Play mode label table | ~28 |
| SceneModTargets 21 new rows | ~1008 |
| **Total new flash** | **~1460** |

## Summary of new per-Scene RAM (scene_settings_t)

| Field | Bytes per Scene | Total (×16) |
|:------|----------------:|------------:|
| `track_morph_length[7]` | 7 | 112 |
| `track_morph_scale[7]` | 7 | 112 |
| `track_morph_shuffle[7]` | 7 | 112 |
| **Total** | **21** | **336** |

## Summary of new per-Scene Region RAM (pat_scene_region_t)

| Field | Bytes per Scene | Total (×16) |
|:------|----------------:|------------:|
| `track_play_mode[7]` | 7 | 112 |
| **Total** | **7** | **112** |

---

## File change index

| File | Changes |
|:-----|:--------|
| `Core/Sequencer/StepScale.h` | Modify: constants, API signatures |
| `Core/Sequencer/StepScale.c` | Rewrite: LUT, label tables, accessors |
| `Core/Sequencer/sequencer.c` | Major: DDA accumulators, shuffle, play modes, FX migration, realignment, retrigger |
| `Core/Bank/Scene/Pattern/PatternData.h` | Add: `track_play_mode[7]`, update `_Static_assert` |
| `Core/Bank/Scene/Pattern/PatternData.c` | Add: `track_play_mode` init/setter/copy, update comments |
| `Core/Bank/Scene/SceneData.h` | Add: track morph endpoint fields |
| `Core/Bank/Scene/SceneData.c` | Add: morph endpoint defaults/setters/getters |
| `Core/Bank/Scene/Autosave.h` | Add: 21 new Scene param cells |
| `Core/Bank/Scene/Preset/presetMorphEngine.c` | Add: track morph worker |
| `Core/Bank/Scene/Preset/ParameterArray.h` | Add: `PAR_TRACK_PLAY_MODE` |
| `Core/Bank/Scene/SceneModTargets.h` | Add: 3 new target kinds |
| `Core/Bank/Scene/SceneModTargets.c` | Add: 21 new target table entries |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | Modify: `stepScale_ticks()` → `stepScale_ticksQ8() / 256.0f` |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | Modify: `stepScale_shortName()` → `stepScale_formatShort()` |
| `Core/DSP/Effects/EffectTypes.h` | No code change (aliases resolve) |
| `Core/Menu/menuPages.h` | Modify: SEQ_PAGE layout |
| `Core/Menu/menu.c` | Modify: scale display, play mode display/edit, bar display |
| `Core/Menu/CopyClear/copyClearService.c` | Add: `track_play_mode` copy/clear |
| `Core/Hardware/SD/filesystem.c` | Add: `track_play_mode` reader/writer, sceneset morph lines |
| `Core/Hardware/SD/storageTypes.c` | Add: sceneset parser for morph endpoints |
| `tools/convert_scene_scale.py` | Add: new file |

---

## Implementation progress log (Codex, 2026-10-08)

Status: **Steps 1-4, 6, 7 implemented and building clean.** **Step 5
(Morphability) is deferred** pending one design decision - see below.

Clean rebuild (make clean && make all, DEV config): text 534,808 B, data 420 B,
bss 427,200 B. Flash payload 535,228 B of 753,664 (218,436 B free). No new
compiler warnings from this change set.

### Step 1 - 128-position Q8.8 LUT and per-track DDA accumulators

Implemented in its final (Stage 2) form, i.e. the true Q8.8 LUT. The plan's
Stage 1 integer-only intermediate was not left as a separate code state; the DDA
plumbing is identical either way, and Stage 1 timing can be re-validated by
temporarily zeroing the LUT low bytes (each entry then equals round(multiplier)
x 256).

- Core/Sequencer/StepScale.h - STEP_SCALE_COUNT 128, STEP_SCALE_DEFAULT 76;
  stepScale_ticksQ8(), stepScale_shortName/longName (NULL for non-stops),
  stepScale_isMusicalStop(), plus a new shared stepScale_formatShort() that owns
  the symbolic-or-decimal three-character fallback.
- Core/Sequencer/StepScale.c - 128-entry Q8.8 LUT generated from
  step_scale_0_127.csv (range 1536..49152), and the 14-entry nudge-stop label
  table.
- Core/Sequencer/sequencer.c - seq_trackAccumulator[7] (uint16_t, 14 B);
  seq_processSchedulerTick() now has the per-track DDA loop. The master 1/16th
  counter (seq_masterStepClock/seq_masterStepCnt) is still derived with the old
  24-tick modulo for bar display, boundaries, trigger clock, and MIDI beat.
- Realignment (seq_realignActivePatternToMasterClock(),
  seq_realignTrackToMasterClock()) sets the Q8.8 phase
  ((seq_elapsedPpqTicks << 8) % interval) and derives the step index from the
  same Q8.8 timeline.

### Deviations from the written plan (Step 1)

1. **Immediate step zero.** The plan seeded accumulators to 0 and dropped the
   elapsed-tick modulo guard from the advance test. With a zero seed the first
   tick adds 256 and cannot reach the minimum interval (1536), so step zero
   would be lost and the first note would arrive one step late.
   seq_setStepIndexToStart() instead seeds each accumulator to interval - 256,
   so the initial tick fires exactly once with a zero remainder. Verified by
   construction against the old behaviour (fires at elapsed 0, interval,
   2 x interval, ...).
2. **Non-forward realignment.** The plan's realign snippet used a forward
   position % len for every mode. Added seq_stepIndexForMode() so rev/pip/onc/
   1fr tracks land on the step their mode would have reached, and pip sets its
   direction bit from the mapped cycle phase.
3. **Stopped once-mode tracks.** Realignment skips a track whose once-mode
   stopped flag is set (S078 section 4.3: the double-click realign gesture must
   not restart a stopped track).
4. **Menu scale clamp.** getMaxEntriesForMenu(MENU_TRACK_SCALE) is left
   returning STEP_SCALE_COUNT; the clamp uses value >= n -> n-1, so this yields
   max 127. The plan's suggested STEP_SCALE_COUNT - 1 would have capped the
   parameter at 126.

### Step 2 - FX sequencer migration (Stage 3)

- seq_fxAccumulator (uint16_t, 2 B) and seq_fxStepCounter (uint32_t, 4 B).
- seq_fxClockTick() now uses the same Q8.8 DDA and a monotonic step counter
  instead of seq_elapsedPpqTicks % ticks and / ticks. The accumulator is seeded
  in seq_setStepIndexToStart() alongside the track accumulators.
- .fx step_scale is token-based, not a raw byte, so section 2.5's no-migration
  rule does not apply directly. storageTypes.c now keeps the original 14
  symbolic tokens but maps each to its new CC (1bar/2bar clamp to 127), parses a
  decimal CC 0..127 as a fallback, and writes the token when one exists (decimal
  otherwise). Old .fx files therefore load at the same musical division with no
  migration; only numeric step_scale values need the converter.

### Step 3 - Per-track shuffle

seq_trackShuffleDelay/Pending/Vel/Note[7] (28 B). Odd-indexed steps with nonzero
shuffle are deferred by (shuffle x 24) / 256 PPQ ticks; the tick-down runs before
the DDA advance. The delay is fixed to the 1/16th grid, not the track scale
(section 3.3). A pending deferral is flushed before a new one is set so a fast
track cannot lose an un-fired odd step; a pending deferral fires even if the
track then stops (section 10.2).

### Step 4 - Play modes

- pat_scene_region_t::track_play_mode[7] (offset 3 of the 16-byte PAT4 track
  header); region _Static_assert updated 23 -> 30 in both PatternData.h and
  PatternData.c.
- pat_setTrackPlayMode(), init default 0, pat_applyTrackSettingsToMenu(), and the
  whole-region copy body.
- seq_trackPlayState[7] with STOPPED (bit 0) and PIP_REV (bit 1).
  seq_advanceTrackStep() implements fwd/rev/pip/rnd/onc/1fr. Pip uses the
  direction bit plus the index, so no 6-bit cycle counter is needed (a 128-step
  track needs a 256-step cycle, which would not fit the plan's 6 bits anyway).
- Retrigger: seq_selectActivePattern(), seq_alignActivePatternToScene(), and
  seq_setTrackPlayedScene() clear the stopped state; transport start/stop and
  pattern boundaries clear it through seq_setStepIndexToStart().
- PAT4 reader/writer (1 writer + 3 readers), copy/clear, and the STEP page
  layout len | scl | shf | mod | mch | not | -- | --.
- PAR_TRACK_PLAY_MODE added to ParameterArray.h after PAR_TRACK_MIDI_NOTE.

### Deviation from the written plan (Step 4): play-mode display

The plan asked for DTYPE_MENU | (MENU_TRACK_PLAY_MODE << 4). DTYPE_MENU packs its
table id into the high nibble, so ids 0..15 are already all assigned
(MENU_TRACK_SCALE through MENU_EXT_SYNC); a seventeenth id would wrap to id 0 and
display the track-scale table. A dedicated DTYPE_* value is also impossible: the
dtype byte is unpacked with (dtype & 0x0f) in every Menu formatter, and all
sixteen nibble values (DTYPE_0B255 through DTYPE_1B128) are already used, so a
seventeenth enum value would alias DTYPE_0B255.

Resolution: store the parameter as DTYPE_0B127 and apply the 0..5 play-mode
value, name, and clamp through static-param special cases keyed on
MENU_CELL_STATIC + PAR_TRACK_PLAY_MODE in va_formatValue3(),
menu_formatCellValue3(), menu_clampCellValue(), and the menu_repaintGeneric()
edit painter, using menu_getPlayModeName() and the trackPlayModeNames table.
TEXT_TRACK_PLAY_MODE, SHORT_PLAY_MODE (mod), and LONG_PLAY_MODE (PlayMode) are
appended so no existing index moves.

### Step 6 - Conversion utility

tools/convert_scene_scale.py added. Remaps the seven track_scale bytes in PAT4
files and recomputes the header CRC32C (Castagnoli, reflected, exactly
autosave_recordCrcBegin/Finish). .fx files are token-based, so only a numeric
step_scale value in 0..13 is remapped; symbolic tokens are already correct.
Verified round-trip (CRC self-consistent, idempotent on a second pass).

### Step 7 - Bar display

**No code change required - already satisfied.** menu_currentBar is the
user-selected viewed bar (written only by buttonHandler_selectBar()), not a
playback chase position. The playback bar indication is derived from
seq_ledState.chaseStep in ledHandler.c (led_updateSelectBarChaselight(),
led_updateCurrentStep()), and seq_ledState.chaseStep is
seq_stepIndex[menu_getActiveVoice()]. With the per-track DDA the viewed track's
index already advances at its own scale, so the bar display follows the viewed
track. There is no menu_currentBar assignment in any playback path to change.

### Step 5 - Morphability: RESOLVED, ready for implementation

Both open questions from the initial deferral have been resolved:

1. **Effective-getter architecture (decided).** The morph worker does NOT write
   into the Pattern region. It follows the S075 FX-send Morph pattern: Normal
   values stay in `pat_scene_region_t`; the morph worker computes effective
   values and caches them in ISR-static arrays
   (`seq_effectiveTrackLength/Scale/Shuffle[7]`, 21 B). The sequencer reads
   these effective arrays. Automation writes to the effective arrays and
   restores by recomputing from Normal + endpoint + amount.
2. **UI and persistence specified.** SHIFT held on the STEP page shows/edits
   morph endpoints (matching VOICE/FX SHIFT overlay pattern). sceneset.scg gets
   three CSV lines. Copy/clear morph operations are extended to include track
   endpoints.

Step 5 plan text has been rewritten above (5-1 through 5-10) with the
effective-getter architecture, STEP morph UI spec, and copy/clear integration.

### Post-implementation bug fixes (review, 2026-10-08)

1. **Shuffle off-by-one (Step 3).** `seq_processShuffleDelays()` decrements
   then fires at 0 — setting `delay = N` gave N+1 actual ticks. Fixed:
   `seq_trackShuffleDelay[track] = (uint8_t)(delay - 1u)` (safe because
   `delay > 0u` is guaranteed at that point). Plan text updated.

2. **Converter REMAP_TABLE (Step 6).** Plan text had `11: 120` (dotted 1/4);
   the actual code already correctly had `11: 127` (1/2 note, 192 ticks).
   Plan text corrected to match.

### CrumpBit — downstream StepScale consumer

CrumpBit (`Core/DSP/Effects/CrumpBit/`) uses the StepScale API to compute
tempo-synced delay lengths. Two call sites updated:

- `crumpBit_divisionFor()`: `stepScale_ticks(i)` → `stepScale_ticksQ8(i) /
  256.0f` (float DSP context, Q8.8 return divided back to float ticks).
- `crumpBit_uiFormatValue3()` (via CrumpBitParameters.c):
  `stepScale_shortName()` → `stepScale_formatShort()` (handles NULL for
  non-musical stops by formatting the raw CC as a decimal 3-char string).

These changes are necessary — CrumpBit is a downstream consumer of the
renamed/retyped StepScale API.

### RAM / flash accounting (measured)

- New ISR-static SRAM1: 55 B (14 accumulator + 2 FX accumulator + 4 FX counter
  + 28 shuffle + 7 play state) - matches the plan's summary table.
- New per-Scene Region RAM: 112 B (7 B play mode x 16 Scenes).
- bss grew 192 B (55 + 112 + alignment) versus the S077 close.
- Flash: new tables (~440 B: 256 LUT + ~170 label table + ~28 play-mode names)
  plus the new sequencer/menu code. The net DEV text size fell 3,608 B versus
  S077 after a clean rebuild; this is an LTO layout/de-unrolling artefact (the
  CrumpBit division loop is now a 128-iteration loop rather than a
  possibly-unrolled 14-iteration one). Worth a second look at session close.

### Hardware test focus

1. All 14 old nudge stops produce the same timing as the old table at CC
   0/16/38/54/60/76/83/86/93/100/103/110/120/127.
2. Two tracks at different scales stay phase-correct across pattern boundaries
   and scene changes.
3. Fractional positions (e.g. CC 50) are audibly distinct from rounded
   neighbours; a smooth knob sweep has no glitches.
4. Shuffle on/off per track; extreme values.
5. Play modes fwd/rev/pip/rnd/onc/1fr including once-mode retrigger on scene
   change and per-track scene reassignment; stopped once tracks survive a
   double-click realign.
6. FX sequencer at non-musical CC positions and on old .fx tokens.
7. PAT4 round-trip of track_play_mode; copy/clear of a track carries it.
8. Old PAT4 files play at the bottom of the curve (intended); the converter
   migrates them.

---

## Step 5 implementation log (Codex, 2026-10-08)

Step 5 is now implemented against the revised effective-getter + STEP-Morph-UI
spec (5-1..5-10). Clean build (`make all`, DEV config): text 537,592 B,
data 420 B, bss 427,608 B. No new compiler warnings.

### 5-1 / 5-2 SceneData

- `scene_settings_t` gains `track_morph_length/scale/shuffle[NUM_TRACKS]`
  (21 B/Scene, 336 B across 16 Scenes).
- `scene_settingsDefaults()` and `filesystem_initSceneStage()` seed 16/76/0.
- New `scene_setTrackMorphLength/Scale/Shuffle()` (clamped, change-aware
  `scene_storeParameterByte()` funnel) and matching getters.
- `scene_commitSettings()` carries the three endpoints, so Scene
  settings/Scene copy/clear include them.

### 5-3 Autosave

- Cells 51..57 (length), 58..64 (scale), 65..71 (shuffle); COUNT and
  LIVE_BYTES 51 -> 72.
- Getter/setter branches added; the group static asserts updated.
- A zero length cell (pre-S078 record) is skipped rather than clamped to the
  unreachable minimum, so an old AutoSave record leaves the fresh default.

### 5-4 Effective-value arrays

- `seq_effectiveTrackLength/Scale/Shuffle[NUM_TRACKS]` (21 B, ISR-static,
  sequencer.c).
- `seq_refreshTrackEffectiveParams()` recomputes them per track from the
  track played Scene (Normal, Morph endpoint, retained voice Morph amount).
- `presetMorph_getTrackEffectiveLength/Scale/Shuffle()` in presetMorphEngine.c
  own the interpolation and the step-automation overlay check.
- Refresh points: `presetMorph_tick()` (every foreground worker tick, before
  its early-out), `seq_init()`, `seq_selectActivePattern()`,
  `seq_alignActivePatternToScene()`, `seq_setTrackPlayedScene()`,
  `seq_clearPerTrackOverrides()`, and `seq_setStepIndexToStart()`.
- The sequencer reads the arrays in `seq_advanceTrackStep()`,
  `seq_processSchedulerTick()`, both realign helpers, and the DDA seed.

**Deviations from the 5-4 snippet:**

1. The arrays are file-static in sequencer.c and written through
   `seq_refreshTrackEffectiveParams()`, not written directly from
   presetMorphEngine.c (the snippet file-static arrays cannot be written from
   another translation unit). The interpolation math stays in
   presetMorphEngine.c, matching the spec.
2. The refresh is per-track from each track **played Scene**
   (`seq_perTrackPattern[track]`), not one global scene, so tracks playing a
   per-track-assigned Scene (S077 P2) get the right Normal, endpoints, and
   Morph amount.
3. The amount used is the retained `voice_morph_amount[slot]` (the snippet
   choice), not the LFO/step-resolved amount, so voice-Morph step automation
   does not change track timing.

### 5-5 / 5-6 sceneset persistence

- `storageTypes.c` parses `track_morph_length/scale/shuffle` (7 CSV values).
- `filesystem.c` writer cases 15/16/17 append the three lines; all earlier
  line numbers are unchanged.

### 5-7 STEP Morph UI (SHIFT overlay)

- New dedicated `menu_patternTrackMorphEndpoint` flag +
  `menu_setPatternTrackMorphEndpoint()` (a parallel flag rather than widening
  `voiceModeShowMorph`, so the VOICE Morph latch is untouched).
- `buttonHandler.c`: SHIFT press in SELECT_MODE_STEP sets the view; SHIFT
  release clears it.
- `menu_getParameterDisplayValue()` and `menu_cellCommitValue()` redirect only
  PAR_TRACK_LENGTH/PAR_TRACK_SCALE/PAR_SHUFFLE to the viewed Scene Morph
  endpoints while the view is active; the Normal mirror and the Pattern region
  are untouched. Both the encoder and the RV1-4 pots converge on these two
  functions, so both edit the endpoints.
- The discrete play-mode cell is blanked and inert in the Morph view (5.6).

**Deviation:** the SHIFT+MODE_STEP *latch* (extend lines 1129-1149) is not
implemented. SHIFT+MODE_STEP currently maps to SELECT_MODE_SOM_GEN
((STEP + 4) & 7 == SOM_GEN) and is the only entry to the SOM generator page;
latching the Morph view there would make SOM unreachable. The hold-only view
(SHIFT held) matches the VOICE hold interaction and keeps SOM accessible. If a
latch is wanted, it needs a different gesture or a new SOM entry point.

### 5-8 Copy/clear

- `copy track morph` and `clear track reset morph` now also set the
  destination track `track_morph_*` from the source/own Pattern Normal.
- `copy scene -> morph` and `clear scene reset morph` now also copy/equalise
  all seven tracks endpoints.

### 5-9 / 5-10 Automation

- Three new Scene target kinds and 21 rows (IDs 405..425, `voice_slot` =
  track index).
- `seq_applySceneAutomation()` sets the track overlay and writes the effective
  array directly; `seq_restoreAllSceneAutomation()` clears the overlays and
  `seq_setStepIndexToStart()` recomputes the effective values.

### RAM

- New per-Scene: 336 B (21 x 16).
- New ISR-static: 55 B (steps 1-4) + 21 B (effective arrays) = 76 B; plus 24 B
  of step-automation overlay state (21 value bytes + 3 mask bytes) in the
  Morph engine. The proposal table lists 72 B; the measured total is 76 B
  ISR-static (the plan note already acknowledges this) plus the overlay bytes.
- Measured bss growth from S077 to this point: roughly +600 B (112 region play
  mode + 336 Scene settings + 76 ISR-static + overlays + alignment).

### Step 5 hardware test focus

- SHIFT held on the STEP page shows the Scene Morph endpoints; editing len/scl/
  shf writes the endpoints, not the Pattern Normal; releasing SHIFT restores
  the Normal values; the `mod` cell is blank in Morph view.
- Sweeping PERF Morph with differing endpoints changes track length/scale/
  shuffle in real time; at Morph 0 the values equal Normal.
- `copy scene -> morph` / `clear scene reset morph` and the per-track variants
  move/equalise the endpoints.
- sceneset.scg round-trips the three new lines; an old sceneset without them
  loads the 16/76/0 defaults.
- Scene step automation on a track Len/Scl/Shuf target changes playback for
  that step and restores at the transport/Pattern boundary.

---

## Code review assessment (2026-10-08)

Full diff review of all 32 changed files (Steps 1-7 plus Step 5 Morphability).
Clean build: text 537,592 B, data 420 B, bss 427,608 B. No compiler warnings.

### Architecture

The effective-getter morph architecture is correctly implemented. The data
flow is:

    Pattern region (Normal) + Scene settings (Morph endpoint)
        → presetMorph_getTrackEffective*() (interpolation + overlay check)
        → seq_refreshTrackEffectiveParams() (cache write)
        → seq_effectiveTrackLength/Scale/Shuffle[] (ISR-static cache)
        → sequencer DDA / advanceTrackStep / realign (cache read)

Normal values in `pat_scene_region_t` are never overwritten by the Morph
system. The Pattern dirty bit is never raised by Morph or step automation.
This matches the S075 FX-send Morph pattern.

### Refresh coverage

`seq_refreshTrackEffectiveParams()` is called at all required points:
- `presetMorph_tick()` — every foreground worker tick (before the
  `!active` early-out, so endpoint edits propagate even when no morph
  sweep is active)
- `seq_init()` — boot
- `seq_selectActivePattern()` — Scene change
- `seq_alignActivePatternToScene()` — committed Scene change
- `seq_setTrackPlayedScene()` — per-track Scene assignment
- `seq_clearPerTrackOverrides()` — per-track Scene coalesce
- `seq_setStepIndexToStart()` — transport reset (after automation restore)

### Step automation overlay

The overlay architecture (`track_param_override_mask` + `_value` arrays in
presetMorphEngine.c) is correct:
- `presetMorph_setTrackParamStepOverride()` sets a bit in the mask and
  stores the value. The effective getters check the mask bit first.
- `presetMorph_clearAllTrackParamStepOverrides()` zeroes all three mask
  bytes. Called from `seq_restoreAllSceneAutomation()` before the
  retained-value pass.
- `seq_applySceneAutomation()` writes the effective cache directly after
  setting the overlay, so playback changes on the next DDA tick.
- The restore path in `seq_setStepIndexToStart()` calls
  `seq_refreshTrackEffectiveParams()` after clearing overlays, so the
  cache returns to Normal + Morph base values.

### STEP Morph UI

- `menu_patternTrackMorphEndpoint` flag is correctly scoped to
  `menu_activePage == SEQ_PAGE` via `menu_patternTrackMorphViewActive()`.
- Display redirect: `menu_getParameterDisplayValue()` reads Scene morph
  endpoints for the three morphable cells. The play-mode cell is blanked
  via `menu_getPlayModeName()` returning dashes.
- Commit redirect: `menu_cellCommitValue()` writes to Scene morph
  endpoints via `scene_setTrackMorph*()` and returns early, never touching
  `parameter_values[]` or the Pattern region.
- The play-mode cell edit is correctly blocked during morph view (returns 0).

### Deviation: no SHIFT+MODE_STEP latch

Correctly documented. SHIFT+MODE_STEP maps to `SELECT_MODE_SOM_GEN` via the
`(mode + 4) & 7` rotation, so latching the Morph view there would block SOM
entry. The hold-only interaction (SHIFT down = morph view, SHIFT up = normal)
matches the VOICE hold behaviour and is the right call.

### Deviation: endpoint edit does not call preset_rebuildMorph()

The plan spec said to call `preset_rebuildMorph()` after endpoint edits. The
implementation omits this because `presetMorph_tick()` calls
`seq_refreshTrackEffectiveParams()` unconditionally on every foreground tick,
so the edit propagates within one main-loop pass (~1-2 ms). This is correct
and avoids the full morph rebuild cost. The plan spec is superseded by the
unconditional refresh.

### Copy/clear morph integration

- Track copy morph (`ccCopy_runMorphTrack`): copies source track Normal
  values to destination track morph endpoints. Correct — this is the
  "copy Normal → Morph" semantic.
- Scene copy morph (`ccCopy_runSceneMorph`): copies source Scene Normal
  for all 7 tracks. Correct.
- Track clear reset morph (`ccClear_runResetMorphTrack`): equalises
  endpoints to the track's own Normal. Correct.
- Scene clear reset morph (`ccClear_runResetSceneMorph`): equalises all 7
  tracks. Correct.

### Persistence

- sceneset.scg: three new CSV lines (cases 15/16/17 in writer, parser
  keyed on `track_morph_length/scale/shuffle`). Missing keys in old files
  fall back to staged defaults from `filesystem_initSceneStage()`.
- AutoSave: cells 51..71, `LIVE_BYTES` 51 → 72. Static asserts validate
  group coverage. Zero-value guard on length cells handles pre-S078 records.
- `scene_commitSettings()` carries the three endpoints.

### Interpolation arithmetic

`presetMorph_interpTrack()` uses unsigned `uint16_t` arithmetic:
`(normal * (255 - amount) + morph * amount + 127) / 255`. This is
algebraically identical to the existing `presetMorph_interpolate()` signed
formula. For the 0..128 track parameter domain, the unsigned version is safe
and avoids sign extension. Amount 0 returns normal, amount 255 returns morph.

### SceneModTargets

21 new rows (IDs 21..41, macro `SCENE_MOD_TARGET_ID(n)` which maps to
405..425). Length targets have min=1, max=128; scale and shuffle have
min=0, max=127. All use `SCENE_MOD_TARGET_USE_AUTOMATION` only (no
velocity/LFO), matching the plan. `voice_slot` carries the track index 0..6.

### RAM accounting

| Category | Bytes | Notes |
|:---------|------:|:------|
| ISR-static (Steps 1-4) | 55 | 14+2+4+28+7 |
| ISR-static (Step 5 effective arrays) | 21 | 3 × uint8_t[7] |
| **Total ISR-static** | **76** | |
| Step 5 overlay state | 24 | 21 value bytes + 3 mask bytes |
| per-Scene settings | 336 | 21 × 16 Scenes |
| per-Scene region | 112 | 7 × 16 Scenes |
| **Total bss growth** | ~600 | includes alignment |

### Flash

text 537,592 B — grew 2,784 B from the Steps 1-4 baseline (534,808 B).
215,652 B free of 753,664 B. The Step 5 additions are modest relative to
the free flash.

### Issues found

None. The implementation is correct against the revised plan. All
sequencer reads use the effective arrays. No retained Normal values are
overwritten by Morph or automation. AutoSave round-trips are guarded.
The STEP morph UI is correctly scoped and gated.

### Follow-ups for hardware testing

Carry forward the hardware test lists from Steps 1-4 and Step 5 above.
Priority items:

1. SHIFT held on STEP page: verify morph endpoint display/edit, play-mode
   blanking, release returns Normal.
2. PERF Morph sweep with differing endpoints: verify continuous
   length/scale/shuffle change in real time, exact Normal at amount 0.
3. sceneset.scg round-trip: save, delete AutoSave, reboot, verify endpoints.
4. Old sceneset without new keys: verify 16/76/0 defaults load cleanly.
5. Copy scene → morph / clear scene reset morph with track endpoints.
6. Step automation on track Len/Scl/Shuf targets: verify playback change,
   transport restore.
7. All 14 musical stops timing verification against the old table.
8. Two tracks at different scales: phase alignment across pattern/scene
   boundaries.
9. Play modes including once-mode retrigger and stopped-track realign.
