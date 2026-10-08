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
          seq_trackShuffleDelay[track] = delay;
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

## Step 5 — Morphability

#### 5-1 `Core/Bank/Scene/SceneData.h` — morph endpoint storage in Scene settings

**After line 233 (`bus_comp[SCENE_BUS_COMP_FIELD_COUNT]`) — add**

```c
    uint8_t track_morph_length[NUM_TRACKS];
    uint8_t track_morph_scale[NUM_TRACKS];
    uint8_t track_morph_shuffle[NUM_TRACKS];
```

/*
 * Per-track Morph endpoints for length, scale, and shuffle (S078 §5.4).
 *
 * What: the Morph endpoint values for three track parameters, stored per
 * Scene. The Normal values live in pat_scene_region_t (Pattern data); these
 * are the Morph endpoints interpolated by the voice morph amount. Why: the
 * Morph system is Scene-level — Patterns store step/track data, Scenes own
 * Morph endpoints. Tracks 6 and 7 share voice 6's morph amount
 * (voice_morph_amount[5]) but may have different endpoints here.
 * Inputs: Menu Morph-endpoint edits, Scene Load. Outputs: the morph worker
 * interpolates between Pattern Normal values and these endpoints. RAM: 21
 * bytes per Scene (3 × 7), 336 bytes total across 16 Scenes. No AutoSave
 * version bump — user deletes AutoSave files after implementation.
 * Affiliates: presetMorphEngine.c track morph worker, Autosave Scene params,
 * sceneset.scg writer/parser.
 */

#### 5-2 `Core/Bank/Scene/SceneData.c` — defaults and setters

**In `scene_settingsDefaults()` — add**

```c
    memset(out->track_morph_length, 0, sizeof(out->track_morph_length));
    for (uint8_t t = 0u; t < NUM_TRACKS; t++) {
        out->track_morph_length[t] = NUM_STEPS_PER_BAR;
        out->track_morph_scale[t] = STEP_SCALE_DEFAULT;
        out->track_morph_shuffle[t] = 0u;
    }
```

/*
 * Track Morph endpoint defaults (S078 §5.4).
 *
 * What: fresh Scenes start with Morph endpoints matching the Normal defaults
 * (length 16, scale 76/1/16, shuffle 0), so Morph amount 0..255 produces no
 * change until the user edits the Morph endpoints.
 */

**Add setter/getter pairs:**

```c
void scene_setTrackMorphLength(uint8_t scene_index, uint8_t track, uint8_t value);
uint8_t scene_getTrackMorphLength(uint8_t scene_index, uint8_t track);
void scene_setTrackMorphScale(uint8_t scene_index, uint8_t track, uint8_t value);
uint8_t scene_getTrackMorphScale(uint8_t scene_index, uint8_t track);
void scene_setTrackMorphShuffle(uint8_t scene_index, uint8_t track, uint8_t value);
uint8_t scene_getTrackMorphShuffle(uint8_t scene_index, uint8_t track);
```

/*
 * Track Morph endpoint setters (S078 §5.4).
 *
 * What: stores one track's Morph endpoint for length/scale/shuffle in the
 * Scene settings. Clamps and marks the matching AutoSave Scene parameter cell.
 * Inputs: Scene index, track 0..6, value. Outputs: retained Scene data
 * updated. Affiliates: Autosave Scene parameter cells (new entries),
 * presetMorphEngine.c track morph path.
 */

#### 5-3 `Core/Bank/Scene/Autosave.h` — new Scene parameter cells

**After line 258 (`AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE = 45`) — add**

```c
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_LENGTH_BASE = 51,   /* 51..57 */
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SCALE_BASE = 58,    /* 58..64 */
    AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE = 65,  /* 65..71 */
    AUTOSAVE_SCENE_PARAM_COUNT = 72
```

**Update `AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES`:**

```
Old:
  #define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    51u

New:
  #define AUTOSAVE_SCENE_PARAMETER_LIVE_BYTES    72u
```

/*
 * Track Morph endpoint AutoSave cells (S078 §5.4).
 *
 * What: 21 new AutoSave Scene parameter cells for the three track Morph
 * endpoint arrays (7 tracks × 3 parameters). These occupy previously
 * reserved cells in the 118-byte Scene parameter allocation. Why: the Morph
 * endpoints must survive power cycles via AutoSave. No format version bump
 * needed — user deletes AutoSave files. Inputs: Scene setters mark these
 * cells. Outputs: Autosave drain serializes them. 72 <= 118 capacity, so
 * the allocation remains valid. Affiliates: scene_setTrackMorph*() setters.
 */

#### 5-4 `Core/Bank/Scene/Preset/presetMorphEngine.c` — track morph worker

**After the existing voice morph tick loop (~line 493) — add**

A new track morph pass that runs after the per-descriptor pass completes:

```c
static void presetMorph_trackTick(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene) return;

    for (uint8_t track = 0u; track < NUM_TRACKS; track++) {
        uint8_t slot = (track <= 5u) ? track : 5u;
        uint8_t amount = scene->settings.voice_morph_amount[slot];
        const pat_scene_region_t *region = pat_sceneRegion(scene_index);
        if (!region) continue;

        /* Length: interpolate, round to nearest integer, clamp 1..128 */
        uint8_t norm_len = region->track_length[track];
        uint8_t morph_len = scene->settings.track_morph_length[track];
        uint8_t new_len = presetMorph_interpolate(norm_len, morph_len, amount);
        if (new_len < 1u) new_len = 1u;
        /* Apply only if changed — avoid unnecessary dirty marks */
        ...

        /* Scale: interpolate CC values */
        uint8_t norm_scale = region->track_scale[track];
        uint8_t morph_scale = scene->settings.track_morph_scale[track];
        uint8_t new_scale = presetMorph_interpolate(norm_scale, morph_scale, amount);
        ...

        /* Shuffle: interpolate 0..127 */
        uint8_t norm_shuffle = region->track_shuffle[track];
        uint8_t morph_shuffle = scene->settings.track_morph_shuffle[track];
        uint8_t new_shuffle = presetMorph_interpolate(norm_shuffle, morph_shuffle, amount);
        ...
    }
}
```

/*
 * Track morph interpolation worker (S078 §5.1, §5.3).
 *
 * What: interpolates length, scale, and shuffle between Pattern Normal
 * values and Scene Morph endpoints using the associated voice's morph
 * amount. Track N uses voice_morph_amount[N] for tracks 0..5; tracks 6
 * and 7 use voice_morph_amount[5] (HiHat/Choke pair). The interpolated
 * values are applied as live overrides — they affect DDA tick intervals
 * and shuffle delays in real time but do not modify the stored Pattern
 * region. Why: continuous morph sweep of length/scale/shuffle produces
 * musically useful real-time variation. Inputs: pat_scene_region_t Normal
 * values, scene_settings_t Morph endpoints, voice_morph_amount[slot].
 * Outputs: live track playback parameters updated. Called from the
 * bounded morph tick after descriptor passes complete. Affiliates:
 * presetMorph_interpolate() (existing arithmetic), sequencer.c DDA
 * accumulator (reads track_scale live), seq_advanceTrackStep() (reads
 * track_length and track_shuffle live).
 */

Note: the morph worker writes to the Pattern region's track_length,
track_scale, and track_shuffle fields as live overrides. The Normal values
must be preserved separately (either cached or restored from the Scene's
base values on Morph amount = 0).

#### 5-5 `Core/Hardware/SD/storageTypes.c` — sceneset parser for morph endpoints

**After `fx_send_morph` parser block (~line 701) — add**

```c
    } else if (storage_streq(key, "track_morph_length")) {
        return storage_parseCsvU8(value,
                                  target_settings->track_morph_length,
                                  NUM_TRACKS);
    } else if (storage_streq(key, "track_morph_scale")) {
        return storage_parseCsvU8(value,
                                  target_settings->track_morph_scale,
                                  NUM_TRACKS);
    } else if (storage_streq(key, "track_morph_shuffle")) {
        return storage_parseCsvU8(value,
                                  target_settings->track_morph_shuffle,
                                  NUM_TRACKS);
```

/*
 * sceneset.scg parser for track Morph endpoints (S078 §5.4).
 *
 * What: three new CSV keys, each carrying 7 comma-separated uint8_t values.
 * Optional for old sceneset files — missing keys leave the defaults from
 * scene_settingsDefaults(). Why: track morph endpoints are Scene settings
 * that must survive Scene Save/Load. Inputs: sceneset.scg line. Outputs:
 * target_settings track_morph_* arrays. Affiliates: filesystem.c sceneset
 * writer, scene_commitSettings().
 */

#### 5-6 `Core/Hardware/SD/filesystem.c` — sceneset writer for morph endpoints

**After line 17291 (case 14, fx_send_morph write) — add**

```c
    case 15u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "track_morph_length",
            scene->settings.track_morph_length, NUM_TRACKS);
    case 16u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "track_morph_scale",
            scene->settings.track_morph_scale, NUM_TRACKS);
    case 17u:
        return filesystem_formatAssignmentCsvU8Line(
            dst, cap, "track_morph_shuffle",
            scene->settings.track_morph_shuffle, NUM_TRACKS);
```

/*
 * sceneset.scg writer for track Morph endpoints (S078 §5.4).
 *
 * What: three new lines appended after fx_send_morph, keeping all earlier
 * line numbers unchanged. Why: Scene Save must persist the Morph endpoints.
 * Inputs: scene->settings.track_morph_* arrays. Outputs: three CSV lines
 * in sceneset.scg. Affiliates: storageTypes.c parser counterpart.
 */

#### 5-7 `Core/Bank/Scene/SceneModTargets.h` — new target kinds

**After `SCENE_MOD_TARGET_KIND_EFFECT_MORPH` — add**

```c
    SCENE_MOD_TARGET_KIND_TRACK_LENGTH,
    SCENE_MOD_TARGET_KIND_TRACK_SCALE,
    SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE,
```

#### 5-8 `Core/Bank/Scene/SceneModTargets.c` — new target table entries

**After line 114 (Effect Morph row, ID 20) — add**

21 new entries (7 tracks × 3 parameters), IDs 21..41:

```c
    /* Per-track length targets (IDs 405..411) */
    { SCENE_MOD_TARGET_ID(21u), SCENE_MOD_TARGET_KIND_TRACK_LENGTH, 0u,
      1u, 128u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Track", "1 Len  ", "1ln" },
    /* ... tracks 2-7 ... */

    /* Per-track scale targets (IDs 412..418) */
    { SCENE_MOD_TARGET_ID(28u), SCENE_MOD_TARGET_KIND_TRACK_SCALE, 0u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Track", "1 Scale", "1sc" },
    /* ... tracks 2-7 ... */

    /* Per-track shuffle targets (IDs 419..425) */
    { SCENE_MOD_TARGET_ID(35u), SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE, 0u,
      0u, 127u, SCENE_MOD_TARGET_USE_AUTOMATION,
      "Track", "1 Shuf ", "1sh" },
    /* ... tracks 2-7 ... */
```

/*
 * Scene automation targets for track parameters (S078 §5.5).
 *
 * What: 21 new Scene targets for per-track length, scale, and shuffle. These
 * are automatable as Scene (`scn`) targets in Pattern automation. voice_slot
 * carries the track index (0..6) for the apply handler. Why: allows step
 * automation to modulate per-track timing parameters. IDs 21..41 occupy 21
 * of the remaining 43 available IDs in the 64-ID Scene block. Inputs:
 * Pattern 7-bit automation values. Outputs: runtime track parameter overlays
 * through seq_applySceneAutomation(). Affiliates: Menu automation picker,
 * Pattern validation.
 */

#### 5-9 `Core/Sequencer/sequencer.c` — scene automation apply for track targets

**In `seq_applySceneAutomation()` (~line 1197) — add cases**

```c
    case SCENE_MOD_TARGET_KIND_TRACK_LENGTH:
        if (descriptor->voice_slot < NUM_TRACKS)
            pat_setTrackLength(scene_getActiveIndex(),
                               descriptor->voice_slot, value);
        break;
    case SCENE_MOD_TARGET_KIND_TRACK_SCALE:
        if (descriptor->voice_slot < NUM_TRACKS)
            pat_setTrackScale(scene_getActiveIndex(),
                              descriptor->voice_slot, value);
        break;
    case SCENE_MOD_TARGET_KIND_TRACK_SHUFFLE:
        if (descriptor->voice_slot < NUM_TRACKS)
            pat_setTrackShuffle(scene_getActiveIndex(),
                                descriptor->voice_slot, value);
        break;
```

/*
 * Scene automation apply for track parameters (S078 §5.5).
 *
 * What: routes Pattern automation values to the track parameter setters.
 * voice_slot carries the track index. Why: step automation targeting track
 * length/scale/shuffle must update the live Pattern region so the DDA
 * accumulator and shuffle delay pick up the change immediately. These are
 * runtime overlays — the Pattern AutoSave dirty mark from the setter is
 * acceptable because step automation is a transient overlay that the
 * restore path will undo. Inputs: target descriptor kind and 7-bit value.
 * Outputs: pat_scene_region_t track parameters updated. Affiliates:
 * seq_restoreAllSceneAutomation() must be extended to restore these targets
 * from their retained base values.
 */

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
    11: 120,  # dotted 1/4  (was /2 old table, actually 1/2 = 127)
    12: 127,  # 1 bar → map to 1/2 as closest
    13: 127,  # 2 bars → map to 1/2 as closest (exceeds new range)
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
| **Total new ISR-static** | | **55** | |

Note: the proposal estimated 30 bytes. The additional 25 bytes come from
shuffle velocity/note deferral buffers and the FX step counter, which were
not counted in the proposal's estimate. This is still negligible relative to
SRAM1 capacity.

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
| `Core/DSP/Effects/EffectTypes.h` | No code change (aliases resolve) |
| `Core/Menu/menuPages.h` | Modify: SEQ_PAGE layout |
| `Core/Menu/menu.c` | Modify: scale display, play mode display/edit, bar display |
| `Core/Menu/CopyClear/copyClearService.c` | Add: `track_play_mode` copy/clear |
| `Core/Hardware/SD/filesystem.c` | Add: `track_play_mode` reader/writer, sceneset morph lines |
| `Core/Hardware/SD/storageTypes.c` | Add: sceneset parser for morph endpoints |
| `tools/convert_scene_scale.py` | Add: new file |
