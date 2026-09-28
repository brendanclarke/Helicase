# S072 Step 8 — Implementation Schedule: FX Sequencer

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §11.1–§11.3, §13.4, the FX-sequencer
layer of §9 (`seq(i)` and the held Morph lane), and §17.1 Step 8:

> FX sequencer: TIM3 latch, modes, shared scale table (including the
> track-scale UI switch), transport rules, lock editing, LEDs.

**Gate:** every mode × length × scale; the Scene-switch alignment example;
`sel` while stopped (§11).

**What becomes usable:**

- The Effect page's `run`/`len`/`scl` now drive a 16-step FX sequencer that
  steps with the master clock (`fwd`/`rev`/`pip`/`rnd`), or follows a
  user-selected step (`sel`).
- **Lock editing:** hold one or more SEQ buttons on the Effect page and turn
  a sequenceable cell (the StereoFilter `frq`/`res`/`drv`/`typ`/`vol`/`pan`,
  and `mrp` for the Morph lane). The value is written into every held step and
  those lanes are locked there.
- Locked values replace the Morph-interpolated value while their step plays.
  A Morph-lane lock holds until the next Morph lock or the Scene-rule reset.
- **SEQ LEDs:** steps with locks are lit, the FX chase runs, the `sel` step
  blinks, held steps flash, and steps beyond `len` are dark.
- The STEP page's track `scale` switches to the same 14-entry table
  (display/storage only; track playback still ignores scale).

**Not in this step:**

- Pattern automation of Effect parameters (the `fx` category) and LFO `fx`
  targets (Step 9);
- fan-out (Step 10);
- lock removal (A15);
- track-scale playback (the joint scale pass).

**Prerequisites:** apply `S072_ST7_IMPLEMENTATION.md` §13.3 Finding 1 (the
stale `typ` candidate) and `S072_ST6_IMPLEMENTATION.md` §12.3 Findings 1–2. The
walk-through exercises SELECT, Scene Save and Kit Save paths that those fixes
touch.

**Status:** implementation complete in the working tree; clean production build
passes. Hardware acceptance gates remain pending.

**Line numbers** refer to the Step 7 working tree reviewed in
`S072_ST7_IMPLEMENTATION.md` §13.

---

## 0. Decisions and notes

### D1 — Held-step display of unlocked lanes (confirm)

§13.4 says: "The display shows the first held step's value, with the Tier-1
underline when that lane is locked."

- An unlocked lane still retains a stored value: it is 0 by default, or the
  last value if a lock was ever set.
- Showing that stale byte would suggest a value the step does not play.

**Proposal:**

- **Locked:** show the lane value, underlined.
- **Unlocked:** show the value the step actually plays (the ordinary cell
  value), not underlined.

The first edit on an unlocked lane seeds from that displayed value, so the
turn starts where the sound is.

### D2 — Edits on non-sequenceable cells while SEQ is held (confirm)

- **Non-sequenceable cells** are `typ`, `out` (no flt lane), `run`, `len` and
  `scl`.
- **Proposal:** while a lock-editing hold is active, turning these does
  nothing, so a hold gesture can never change retained settings by accident.
- VOICE's held overlay is likewise edit-only for automatable cells.

### D3 — Scene switch before the next FX boundary (confirm, plan-consistent)

- A17: a switch lands where the new Scene "would be", and the worked example
  plays the new position at the next boundary.
- **Proposal:** between the switch and the new Scene's next FX boundary, no
  lane locks apply (menu values only). The held Morph lane is also cleared,
  under the Scene rule (F1).
- This avoids applying the new Scene's locks at the old Scene's step index.
- In `sel` the selected step applies immediately.

### D4 — Track-scale reinterpretation (acknowledge)

- Track scale moves to the shared table (§11.3). The index meaning changes:
  the old `off` = 10 now reads as `1/4`. The new default is `1/16` (4).
- Track playback still ignores scale, so this is display and storage only.
- Stored values above 13 are shown clamped to 1/16 and rewritten only when
  edited.
- PAT4 files keep their raw byte (A38: nukable).

### D5 — RAM and flash (acknowledge)

| Object | Region | Bytes | Status |
|---|---|---|---|
| `effects_state_t` + `seq_step`, `seq_step_valid`, `seq_sel_step`, `held_morph`, `held_morph_valid`, `seq_serial` | SRAM1 | 76 → 84 (+8 with alignment) | plan §16.1 item 4 ("sequencer runtime", "held Morph-lane value") |
| `seq_fxEvent` latch | SRAM1 | +1 | item 7 ("step latch") |
| menuEffects hold and LED signature (`hold_active`, `hold_mask`, `led_serial`, `led_len`, `led_mode`, `led_running`) | SRAM1 | 14 → 21 (+7) | item 8 ("FX page state about 16"), 5 B over the estimate |

- Net SRAM1 is about +16 B. DTCM is unchanged.
- Estimated **flash: +2.8 to +3.5 KB** (the clock tick, resolution layer,
  lock-edit API, page hold/LED code, and step-scale module), partly offset by
  removing the menuEffects scale tables and `trackScaleNames`.
- Headroom after Step 8 would be about 12 KB (it is 15,512 B now; see
  `S072_ST7_IMPLEMENTATION.md` §13.3 Finding 2).

### Notes (no decision needed)

1. **Timing ownership** (plan §4 rules).
   - TIM3 only *publishes* a step index into a one-byte latch at each FX
     boundary.
   - The foreground `effects_service()` (every render block) consumes it,
     latches Morph-lane locks, and resolves lane overrides.
   - Nothing in TIM3 touches EffectsManager, DSP state or LEDs.
2. **Position is a pure function of the master clock** (A17).
   - `n = seq_elapsedPpqTicks / ticks(scale)`, and the active Scene's
     `run`/`len`/`scl` are read at each boundary.
   - A PERF Scene switch does not reset `seq_elapsedPpqTicks`, so the new
     Scene lands on its own grid. The worked example (`fwd` step 4 → `rev` at
     `n = 4` = index 11) follows from the `rev` formula.
3. **Resets.** Transport start/stop, Pattern-boundary change and external
   reset all run `seq_setStepIndexToStart()`, which now also publishes an FX
   RESET: the step is invalidated and the held Morph lane cleared. Scene
   switches run `effects_activateScene()`, which does the same (D3).
4. **`rnd` hold and shrinking `len`.** `rnd` draws `GetRngValue() % L` in TIM3
   only at boundaries; between boundaries the foreground keeps the drawn
   index. The foreground always applies `step % len`, so a shrunk `len` never
   indexes past the sequence.
5. **Chase ownership.** The Pattern chase drain (`led_updateCurrentStep`)
   currently *clears* the chase layer on any page that is not
   voice/SEQ/Euklid. On `EFFECT_PAGE` that would erase the FX chase every
   sixteenth note, so it now returns early there and the Effect page owns the
   layer (§8).

---

## 1. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/Sequencer/StepScale.h` | new | add | Shared 14-entry step-scale table API |
| 2 | `Core/Sequencer/StepScale.c` | new | add | Ticks, compact and full labels |
| 3 | `Makefile` | after the `Core/Sequencer/sequencer.c` entry | add | `Core/Sequencer/StepScale.c` |
| 4 | `Core/Menu/MenuText.h` | 123–127 | remove | `trackScaleNames` (replaced) |
| 5 | `Core/Menu/menu.c` | 7472, 7518 | modify | `MENU_TRACK_SCALE` uses StepScale |
| 6 | `Core/Bank/Scene/Pattern/PatternData.h` | 236 | modify | `TRACK_SCALE_DEFAULT` (1/16) replaces `TRACK_SCALE_OFF` |
| 7 | `Core/Bank/Scene/Pattern/PatternData.c` | 783, 1127 | modify | New default; clamp stale bytes for display |
| 8 | `Core/Menu/menu.c` | 12987 | modify | Menu default for `PAR_TRACK_SCALE` |
| 9 | `Core/Sequencer/sequencer.h` | before 191 | add | FX event latch API |
| 10 | `Core/Sequencer/sequencer.c` | 65, after 191, after 1088, 1662 | add | StepScale include, latch, FX clock tick, RESET publish |
| 11 | `Core/DSP/Effects/EffectsManager.h` | after the edit API (~258) | add | FX-sequencer runtime and lock-edit API |
| 12 | `Core/DSP/Effects/EffectsManager.c` | 12–19, 101–111, 573–662 | modify/add | State, consume/resolve, select, lock edit, Scene/type resets |
| 13 | `Core/Menu/menuEffects.h` | after `menuEffects_renderLeds` | add | Hold, SEQ press, LED API |
| 14 | `Core/Menu/menuEffects.c` | 24–29, 60–67, 209–215, 440–451, 520–531, 614–663 | modify/add | Shared scale labels; hold, lock edit, markers source, SEQ LEDs |
| 15 | `Core/Menu/menu.c` | after 2347 block, 9300–9304, 9520, 9585, 10756, 10885–10897, 12195 | add/modify | Effect lock markers; held edits; LED render on entry |
| 16 | `Core/Hardware/frontPanel/buttonHandler.c` | 461–500, 716–790 | modify | FX SEQ press/release, hold-expiry routing |
| 17 | `Core/Hardware/frontPanel/ledHandler.c` | 1187 | add | Pattern chase yields the chase layer on the Effect page |
| 18 | Docs | — | modify | SRAM_MANIFEST, MODULE_INTERCHANGE, BANK_PRESET_ARCHITECTURE, FILESYSTEM_SPEC, plan §11/§13.4/§17.1, MEMORY |

There is no change to SceneData, Autosave, filesystem, storageTypes, mixer or
the Instrument engines.

---

## 2. Shared step-scale table

### 2.1 `Core/Sequencer/StepScale.h` (new)

```c
/*
 * Core/Sequencer/StepScale.h
 *
 *  Created on: 28.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */
#ifndef STEP_SCALE_H_
#define STEP_SCALE_H_

#include <stdint.h>

/*
 * Shared step-scale table (Session 072 step 8; plan §11.3, A10).
 *
 * What: one sequencer-owned table of step durations at 96 PPQ, ordered by
 * duration, used by the FX sequencer (timing) and by the track step-scale
 * setting (storage and display; track playback still ignores scale until the
 * joint scale pass). It replaces trackScaleNames and its old index meaning.
 * Index order is also the `.fx` step_scale token order (storageTypes.c,
 * S072_ST6 D3) and EFFECT_SEQ_SCALE_* (EffectTypes.h); static asserts keep
 * them equal.
 *
 *   0 1/64 (6)   1 1/32T (8)  2 1/32 (12)  3 1/16T (16)  4 1/16 (24)
 *   5 1/8T (32)  6 1/16. (36) 7 1/8 (48)   8 1/4T (64)   9 1/8. (72)
 *  10 1/4 (96)  11 1/2 (192) 12 1 bar (384) 13 2 bars (768)
 *
 * Accessors are pure and ISR-safe (read-only flash tables); out-of-range
 * indices return the 1/16 entry so a stale byte can never divide by zero or
 * index past a table. Clients: sequencer.c (TIM3 FX clock), menu.c
 * (MENU_TRACK_SCALE), menuEffects.c (`scl` cell), PatternData (default).
 */
#define STEP_SCALE_COUNT    14u
#define STEP_SCALE_DEFAULT   4u   /* 1/16 */

uint16_t stepScale_ticks(uint8_t index);
const char *stepScale_shortName(uint8_t index);   /* exactly 3 chars */
const char *stepScale_longName(uint8_t index);    /* up to 8 chars   */

#endif /* STEP_SCALE_H_ */
```

### 2.2 `Core/Sequencer/StepScale.c` (new)

```c
/*
 * Core/Sequencer/StepScale.c — shared step-scale table (see StepScale.h).
 */
#include "StepScale.h"

/* Ticks per step at the sequencer's 96 PPQ internal resolution. */
static const uint16_t stepScale_tickTable[STEP_SCALE_COUNT] = {
    6u, 8u, 12u, 16u, 24u, 32u, 36u, 48u, 64u, 72u, 96u, 192u, 384u, 768u
};

/* Three-character compact labels (menu cells); padded with spaces. */
static const char stepScale_short[STEP_SCALE_COUNT][4] = {
    "/64", "32t", "/32", "16t", "/16", "/8t", "16.",
    "/8 ", "/4t", "/8.", "/4 ", "/2 ", "1br", "2br"
};

/* Full-view labels (clicked-in single-parameter view). */
static const char *const stepScale_long[STEP_SCALE_COUNT] = {
    "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/16.",
    "1/8", "1/4T", "1/8.", "1/4", "1/2", "1 bar", "2 bars"
};

uint16_t stepScale_ticks(uint8_t index)
{
    return stepScale_tickTable[(index < STEP_SCALE_COUNT)
                               ? index : STEP_SCALE_DEFAULT];
}

const char *stepScale_shortName(uint8_t index)
{
    return stepScale_short[(index < STEP_SCALE_COUNT)
                           ? index : STEP_SCALE_DEFAULT];
}

const char *stepScale_longName(uint8_t index)
{
    return stepScale_long[(index < STEP_SCALE_COUNT)
                          ? index : STEP_SCALE_DEFAULT];
}
```

### 2.3 `Makefile` — after `Core/Sequencer/sequencer.c \`

```make
  Core/Sequencer/StepScale.c \
```

### 2.4 Track-scale UI switch (plan §11.3; D4)

- **`MenuText.h` lines 123–127:** remove `trackScaleNames`.
- **`menu.c`:** add `#include "StepScale.h"` beside `#include "menuEffects.h"`.
- **`menu.c` line 7472** (`getMaxEntriesForMenu`):

```c
    /* Shared sequencer step-scale table (Session 072 step 8; plan §11.3). */
    case MENU_TRACK_SCALE:    return (uint8_t)STEP_SCALE_COUNT;
```

- **`menu.c` line 7518** (`getMenuItemNameForValue`):

```c
    case MENU_TRACK_SCALE:    p = stepScale_shortName(curParmVal);  break;
```

`stepScale_shortName()` bounds the index itself, so a stale stored byte can
never read past the table.

- **`PatternData.h` line 236:** replace `#define TRACK_SCALE_OFF 10u` with:

```c
#include "StepScale.h"
/*
 * Default track step scale (Session 072 step 8): 1/16 in the shared
 * sequencer table (StepScale.h). The old `off` index (10) no longer exists;
 * stored bytes are reinterpreted in the new index order (A38, S072_ST8 D4).
 * Track playback still ignores scale until the joint scale pass.
 */
#define TRACK_SCALE_DEFAULT STEP_SCALE_DEFAULT
```

  Move the `#include` into the header's include block if the header keeps
  includes at the top.

- **`PatternData.c` line 783:** `TRACK_SCALE_OFF` → `TRACK_SCALE_DEFAULT`.
  Update the adjacent comment ("Defaults are TRACK_SCALE_DEFAULT (1/16) …").
- **`PatternData.c` line 1127** (`pat_applyTrackSettingsToMenu`):

```c
    /* Stale pre-step-8 scale bytes (> 13) display as the 1/16 default until
     * edited (S072_ST8 D4); the stored byte is not rewritten here. */
    parameter_values[PAR_TRACK_SCALE] =
        (region->track_scale[track] < STEP_SCALE_COUNT)
            ? region->track_scale[track] : TRACK_SCALE_DEFAULT;
```

- **`menu.c` line 12987:** `parameter_values[PAR_TRACK_SCALE] = TRACK_SCALE_DEFAULT;`

---

## 3. Sequencer — FX clock and latch

### 3.1 `sequencer.h` — before `#endif` (line 191), add

```c
/*
 * FX-sequencer step latch (Session 072 step 8; plan §11.1).
 *
 * What: TIM3 publishes one byte per FX boundary: SEQ_FX_EVENT_STEP plus the
 * 0..15 step index (newest wins). The common reset path
 * (seq_setStepIndexToStart: transport start/stop, Pattern-boundary change,
 * external reset) publishes SEQ_FX_EVENT_RESET, which is sticky until taken
 * and survives a later STEP publish. seq_fxTakeEvent() atomically reads and
 * clears the latch; it returns 0 when nothing was published.
 * Why: timing stays in TIM3 while every Effect-state change (lane resolution,
 * held Morph lane, DSP writes) stays in foreground (plan §4 rules).
 * Client: effects_service() once per render block.
 */
#define SEQ_FX_EVENT_STEP   0x80u
#define SEQ_FX_EVENT_RESET  0x40u
#define SEQ_FX_EVENT_INDEX  0x0Fu
uint8_t seq_fxTakeEvent(void);
```

### 3.2 `sequencer.c` (add)

**Include, after line 65** (`#include "presetMorphEngine.h"`):

```c
#include "StepScale.h"     /* shared FX/track step-scale table (step 8) */
```

**State, after line 191** (`static uint32_t seq_scene_automation_dirty;`):

```c
/*
 * FX-sequencer event latch (+1 B SRAM1; plan §16.1 item 7).
 *
 * Bits: SEQ_FX_EVENT_STEP | index, and sticky SEQ_FX_EVENT_RESET. Written by
 * TIM3 (step publish, Pattern-boundary reset) and by foreground transport
 * paths (reset publish); read-and-cleared by seq_fxTakeEvent() in
 * foreground. Every read-modify-write runs with interrupts masked so the two
 * producers cannot lose each other's bits.
 */
static volatile uint8_t seq_fxEvent = 0u;

/* Publish one FX step; a pending RESET stays set so it is not lost. */
static void seq_fxPublishStep(uint8_t index)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	seq_fxEvent = (uint8_t)((seq_fxEvent & SEQ_FX_EVENT_RESET) |
	                        SEQ_FX_EVENT_STEP |
	                        (index & SEQ_FX_EVENT_INDEX));
	__set_PRIMASK(primask);
}

/* Publish a reset; it supersedes any unconsumed step. */
static void seq_fxPublishReset(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	seq_fxEvent = SEQ_FX_EVENT_RESET;
	__set_PRIMASK(primask);
}

uint8_t seq_fxTakeEvent(void)
{
	uint32_t primask = __get_PRIMASK();
	uint8_t event;

	/* Atomic read-and-clear for the foreground consumer (effects_service). */
	__disable_irq();
	event = seq_fxEvent;
	seq_fxEvent = 0u;
	__set_PRIMASK(primask);
	return event;
}

/*
 * Publish the FX-sequencer position at an FX boundary (TIM3; plan §11.1).
 *
 * Inputs: seq_elapsedPpqTicks and the active Scene's retained run/len/scl.
 * Output: at most one latch publish per call. A boundary is every
 * stepScale_ticks(scl) PPQ ticks from the scheduler's reset, so n is a pure
 * function of the master clock (A17):
 *   fwd n mod L; rev L-1-(n mod L);
 *   pip p = n mod 2L, p < L ? p : 2L-1-p (endpoints twice, A14);
 *   rnd uniform 0..L-1 from GetRngValue() at each boundary (repeats, A13);
 *   sel publishes nothing (the foreground applies the selected step).
 * At n = 0 (transport start) this yields fwd/pip 0, rev L-1, rnd a draw
 * (§11.2). Reads three retained bytes only; no Effect state is touched.
 */
static void seq_fxClockTick(void)
{
	const effect_record_t *record =
		scene_effectConst(scene_getActiveIndex());
	uint16_t ticks;
	uint32_t n;
	uint8_t len;
	uint8_t index;

	if (!record || record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
		return;
	ticks = stepScale_ticks(record->seq_step_scale);
	if ((seq_elapsedPpqTicks % ticks) != 0u)
		return;
	len = record->seq_length;
	if (len < EFFECT_SEQ_LENGTH_MIN || len > EFFECT_SEQ_LENGTH_MAX)
		len = EFFECT_SEQ_LENGTH_DEFAULT;
	n = seq_elapsedPpqTicks / ticks;
	switch (record->seq_run_mode) {
	case EFFECT_SEQ_RUN_REV:
		index = (uint8_t)(len - 1u - (n % len));
		break;
	case EFFECT_SEQ_RUN_PIP: {
		uint32_t p = n % (2u * (uint32_t)len);
		index = (uint8_t)((p < len) ? p : (2u * len - 1u - p));
		break; }
	case EFFECT_SEQ_RUN_RND:
		/* GetRngValue() is int16_t: mask to a non-negative draw first. */
		index = (uint8_t)(((uint16_t)GetRngValue() & 0x7FFFu) % len);
		break;
	case EFFECT_SEQ_RUN_FWD:
	default:
		index = (uint8_t)(n % len);
		break;
	}
	seq_fxPublishStep(index);
}
```

**`seq_processSchedulerTick()` — after the track-advance block, line 1088**
(the closing `}` of `if (anyAdvanced) { … }`), before `midiParser_checkMtc();`:

```c
	/*
	 * FX-sequencer boundary check (Session 072 step 8). Runs on every
	 * processed PPQ tick, including the immediate tick at transport start,
	 * so it sees each elapsed value exactly once; the master-boundary early
	 * return above (Pattern change) re-enters through the initial tick.
	 */
	seq_fxClockTick();
```

**`seq_setStepIndexToStart()`, line 1662** — before
`seq_restoreAllSceneAutomation();`:

```c
	/*
	 * FX sequencer follows the same common reset path (Session 072 step 8):
	 * the foreground invalidates the current FX step and clears the held
	 * Morph lane (Scene rule, F1) when it consumes this event.
	 */
	seq_fxPublishReset();
```

---

## 4. EffectsManager — FX-sequencer resolution and lock editing

### 4.1 `EffectsManager.h` — after the Step 7 edit API, add

```c
/*
 * FX-sequencer runtime and lock editing (Session 072 step 8; plan §9, §11).
 *
 * effects_seqActiveStep(): the step whose lane locks apply right now, or
 * EFFECT_SEQ_STEP_NONE. `sel` always applies its selected step; other modes
 * apply the latest TIM3 step only while the transport runs and after the
 * active Scene's first boundary (A11, S072_ST8 D3). Always < length.
 * effects_seqSelectedStep()/effects_seqSelect(): the `sel` cursor (runtime,
 * not retained). Selecting a step beyond the length is ignored. Selecting in
 * `sel` also latches that step's Morph-lane lock (held value, F1).
 * effects_seqSerial(): increments on every step/reset/select/lock edit/type
 * or Scene change so UI can repaint LEDs without polling the record.
 * effects_laneOfParam(): descriptor index -> sequencer lane (1..15).
 * effects_getLaneLock(): lane value of one step; returns nonzero if locked.
 * effects_setSeqLaneLock(): write value into every step of step_mask for one
 * lane and set its lock bit (no removal, A15). Values clamp to 0..255 for
 * the Morph lane (0) or to the lane descriptor's maximum. Retained writes go
 * through SceneData setters (AutoSave-marked); Step 10 adds fan-out here.
 * Returns nonzero when any retained byte changed.
 */
#define EFFECT_SEQ_STEP_NONE 0xFFu

uint8_t effects_seqActiveStep(void);
uint8_t effects_seqSelectedStep(void);
void effects_seqSelect(uint8_t step);
uint8_t effects_seqSerial(void);
uint8_t effects_laneOfParam(effect_type_id_t type, uint8_t index,
                            uint8_t *lane_out);
uint8_t effects_getLaneLock(uint8_t scene_index, uint8_t step, uint8_t lane,
                            uint8_t *value_out);
uint8_t effects_setSeqLaneLock(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value);
```

### 4.2 `EffectsManager.c` (modify/add)

**Includes, after line 18:**

```c
#include "sequencer.h"   /* seq_fxTakeEvent(), seq_isRunning() (step 8) */
#include "StepScale.h"   /* shared scale table size (step 8)            */
```

**`effects_state_t`, lines 101–111** — add before the `common` member, and
update the size assert:

```c
    /*
     * FX-sequencer runtime (Session 072 step 8; plan §9, §11).
     * seq_step/seq_step_valid: latest TIM3 step for the active Scene; valid
     * only after its first boundary following a reset or Scene switch.
     * seq_sel_step: `sel` cursor. held_morph/held_morph_valid: Morph-lane
     * lock value holding until the next Morph lock or the Scene-rule reset.
     * seq_serial: UI change counter (wraps).
     */
    uint8_t seq_step;
    uint8_t seq_step_valid;
    uint8_t seq_sel_step;
    uint8_t held_morph;
    uint8_t held_morph_valid;
    uint8_t seq_serial;
```

```c
_Static_assert(sizeof(effects_state_t) == 84u,
               "effects_state_t size is recorded in SRAM_MANIFEST.md");
_Static_assert(STEP_SCALE_COUNT == EFFECT_SEQ_SCALE_COUNT &&
               STEP_SCALE_DEFAULT == EFFECT_SEQ_SCALE_DEFAULT,
               "retained Effect scale indices use the shared sequencer table");
```

Adjust the 84 if the compiler places the new bytes differently: 76 + 6,
padded to 4 before the float-bearing `common`.

**New static helpers, before `effects_service()` (line 615):**

```c
/* Clamp a retained length into the live 1..16 domain. */
static uint8_t effects_seqLength(const effect_record_t *record)
{
    uint8_t len = record->seq_length;

    return (len < EFFECT_SEQ_LENGTH_MIN || len > EFFECT_SEQ_LENGTH_MAX)
        ? EFFECT_SEQ_LENGTH_DEFAULT : len;
}

/* Latch a Morph-lane lock of one step as the held Effect Morph (F1). */
static void effects_seqLatchMorph(const effect_record_t *record, uint8_t step)
{
    const effect_seq_step_t *entry = &record->steps[step];

    if ((entry->lock_mask & (1u << EFFECT_SEQ_LANE_MORPH)) != 0u) {
        effects_state.held_morph = entry->value[EFFECT_SEQ_LANE_MORPH];
        effects_state.held_morph_valid = 1u;
    }
}

/*
 * Consume the TIM3 latch (plan §11.1). RESET invalidates the step and clears
 * the held Morph lane (Scene rule); STEP records the newest index and latches
 * its Morph lock. Both may arrive together (reset, then first step).
 */
static void effects_seqConsume(const effect_record_t *record)
{
    uint8_t event = seq_fxTakeEvent();

    if (event == 0u)
        return;
    if (event & SEQ_FX_EVENT_RESET) {
        effects_state.seq_step_valid = 0u;
        effects_state.held_morph_valid = 0u;
    }
    if (event & SEQ_FX_EVENT_STEP) {
        effects_state.seq_step = (uint8_t)(event & SEQ_FX_EVENT_INDEX);
        effects_state.seq_step_valid = 1u;
        if (record->seq_run_mode != EFFECT_SEQ_RUN_SEL)
            effects_seqLatchMorph(record, (uint8_t)(effects_state.seq_step %
                                          effects_seqLength(record)));
    }
    effects_state.seq_serial++;
}

/* Active step for a record, or EFFECT_SEQ_STEP_NONE (contract in header). */
static uint8_t effects_seqStepFor(const effect_record_t *record)
{
    uint8_t len = effects_seqLength(record);

    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        return (uint8_t)(effects_state.seq_sel_step % len);
    if (!seq_isRunning() || !effects_state.seq_step_valid)
        return EFFECT_SEQ_STEP_NONE;
    return (uint8_t)(effects_state.seq_step % len);
}

/*
 * Lane override for one descriptor index on the active step (A16): a locked
 * lane replaces the Morph-interpolated value. Lane 0 (Morph) is not a
 * descriptor lane and is handled through the held Morph base instead.
 */
static uint8_t effects_seqOverride(const effect_registry_entry_t *entry,
                                   const effect_seq_step_t *step,
                                   uint8_t index, uint8_t *value)
{
    uint8_t lane;

    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (entry->lanes[lane] == index &&
            (step->lock_mask & (uint16_t)(1u << lane)) != 0u) {
            *value = step->value[lane];
            return 1u;
        }
    }
    return 0u;
}
```

**`effects_service()`, lines 615–662 (modify):**

- After `if (!record || !entry) return;` insert:

```c
    /*
     * FX-sequencer layer (Session 072 step 8; plan §9 seq(i)). Consume the
     * TIM3 latch, then resolve the Morph base (held Morph-lane lock beats
     * the retained amount; the Pattern fxm overlay joins ahead of both in
     * Step 9) and the active step whose lane locks replace menu(i).
     */
    effects_seqConsume(record);
    active_step = effects_seqStepFor(record);
    step = (active_step != EFFECT_SEQ_STEP_NONE)
        ? &record->steps[active_step] : NULL;
```

- Replace `morph = scene_getEffectMorphAmount(effects_state.scene_index);` with:

```c
    morph = effects_state.held_morph_valid
        ? effects_state.held_morph
        : scene_getEffectMorphAmount(effects_state.scene_index);
```

- After the `value` initializer inside the descriptor loop, add:

```c
        /* A locked lane on the active FX step replaces menu(i) (A16). */
        if (step && (step->lock_mask & 0xFFFEu) != 0u)
            (void)effects_seqOverride(entry, step, index, &value);
```

- Declare `uint8_t active_step; const effect_seq_step_t *step;` with the other
  locals.
- The existing clamp to `max_value`, the buffer clamp, the `last_applied`
  compare and `write_param` then apply unchanged.

**`effects_activateScene()`, line 573** — before `effects_state.force_all = 1u;`:

```c
    /*
     * Scene rule (F1, S072_ST8 D3): the new Scene's lane locks apply from its
     * next FX boundary; the held Morph lane does not carry across Scenes.
     */
    effects_state.seq_step_valid = 0u;
    effects_state.held_morph_valid = 0u;
    effects_state.seq_serial++;
```

**`effects_changeType()`, line 588** — after `scene_finishEffectWholeCommit()`:

```c
    /* The whole sequence was cleared (F3), including the Morph lane. */
    if (scene_index == effects_state.scene_index) {
        effects_state.held_morph_valid = 0u;
        effects_state.seq_serial++;
    }
```

**New public functions, after `effects_setMorphAmount()`:**

```c
uint8_t effects_seqActiveStep(void)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);

    return record ? effects_seqStepFor(record) : EFFECT_SEQ_STEP_NONE;
}

uint8_t effects_seqSelectedStep(void)
{
    return effects_state.seq_sel_step;
}

/*
 * `sel` jump (plan §11.2, A12): tap or hold selects the step immediately,
 * whether running or stopped; unlocked steps are valid and apply only menu
 * values. A Morph-lane lock on the selected step becomes the held Morph.
 */
void effects_seqSelect(uint8_t step)
{
    const effect_record_t *record =
        scene_effectConst(effects_state.scene_index);

    if (!record || step >= effects_seqLength(record))
        return;
    effects_state.seq_sel_step = step;
    if (record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        effects_seqLatchMorph(record, step);
    effects_state.seq_serial++;
}

uint8_t effects_seqSerial(void)
{
    return effects_state.seq_serial;
}

/* Descriptor index -> lane 1..15 of one type (lane 0 is Morph). */
uint8_t effects_laneOfParam(effect_type_id_t type, uint8_t index,
                            uint8_t *lane_out)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t lane;

    if (!entry)
        return 0u;
    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (entry->lanes[lane] == index) {
            if (lane_out)
                *lane_out = lane;
            return 1u;
        }
    }
    return 0u;
}

uint8_t effects_getLaneLock(uint8_t scene_index, uint8_t step, uint8_t lane,
                            uint8_t *value_out)
{
    const effect_record_t *record = scene_effectConst(scene_index);

    if (!record || step >= EFFECT_SEQ_STEP_COUNT ||
        lane >= EFFECT_SEQ_LANE_COUNT)
        return 0u;
    if (value_out)
        *value_out = record->steps[step].value[lane];
    return (uint8_t)((record->steps[step].lock_mask &
                      (uint16_t)(1u << lane)) != 0u);
}

/* Write-and-lock one lane across held steps (contract in header). */
uint8_t effects_setSeqLaneLock(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value)
{
    const effect_record_t *record = scene_effectConst(scene_index);
    uint8_t step;
    uint8_t changed = 0u;

    if (!record || lane >= EFFECT_SEQ_LANE_COUNT ||
        !effects_laneFileKey(record->type, lane))
        return 0u;
    if (lane != EFFECT_SEQ_LANE_MORPH) {
        /* effects_laneFileKey() succeeded, so the entry and lane row exist. */
        const effect_param_descriptor_t *descriptor = effects_descriptor(
            record->type, effects_registryEntry(record->type)->lanes[lane]);
        if (descriptor && value > descriptor->max_value)
            value = descriptor->max_value;
    }
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        const effect_seq_step_t *entry = &record->steps[step];
        uint16_t bit = (uint16_t)(1u << lane);

        if ((step_mask & (uint16_t)(1u << step)) == 0u)
            continue;
        if (entry->value[lane] != value || (entry->lock_mask & bit) == 0u)
            changed = 1u;
        scene_setEffectSeqLaneValue(scene_index, step, lane, value);
        scene_setEffectSeqLaneLocked(scene_index, step, lane, 1u);
    }
    if (changed)
        effects_state.seq_serial++;
    return changed;
}
```


---

## 5. menuEffects — hold, lock edit, SEQ LEDs

### 5.1 `menuEffects.h` — after `menuEffects_renderLeds()`, add

```c
/*
 * FX-sequencer page gestures and LEDs (Session 072 step 8; plan §13.4).
 *
 * menuEffects_seqButtonPressed(): SEQ tap/hold edge; in `sel` it jumps to the
 * step immediately (A12). menuEffects_seqHoldExpired(): called by
 * ButtonHandler when a SEQ press crosses the common hold threshold (the VOICE
 * View-B threshold); opens lock editing for the physically held steps.
 * menuEffects_seqHoldActive(): nonzero while that hold owns edits.
 * menuEffects_holdEdit(): write one pot/encoder delta into every held step's
 * lane for a sequenceable cell and set the lock bits; non-sequenceable cells
 * are ignored while holding (S072_ST8 D2). Returns nonzero on change.
 * menuEffects_holdDisplay(): value to show for a cell while holding, from the
 * first (lowest) held step; *locked selects the Tier-1 underline (D1).
 * menuEffects_renderSeqLeds(): paint the SEQ row (locks, chase, `sel` blink,
 * length) and then the type's render hook.
 */
void menuEffects_seqButtonPressed(uint8_t step);
void menuEffects_seqHoldExpired(void);
uint8_t menuEffects_seqHoldActive(void);
uint8_t menuEffects_holdEdit(const menuEffects_cell_t *cell, int16_t delta);
uint8_t menuEffects_holdDisplay(const menuEffects_cell_t *cell,
                                uint8_t *value, uint8_t *locked);
void menuEffects_renderSeqLeds(void);
```

### 5.2 `menuEffects.c` (modify/add)

**Includes, lines 24–29:** add `#include "ledHandler.h"` and
`#include "StepScale.h"`.

**Scale tables:**

- Remove `menuEffects_scaleShort` and `menuEffects_scaleLong` (lines 60–67).
- At line 448, replace `memcpy(dst, menuEffects_scaleShort[value], 3u);` with
  `memcpy(dst, stepScale_shortName((uint8_t)value), 3u);`.
- At line 529, replace `menuEffects_scaleLong[value]` with
  `stepScale_longName((uint8_t)value)`.
- Update the comment: "Labels come from the sequencer-owned shared table
  (StepScale.h, step 8)."

**State, after `menuEffects_lastType`:**

```c
/*
 * FX-sequencer page state (Session 072 step 8; +7 B, S072_ST8 D5).
 * hold_active/hold_mask: lock-edit hold owning edits and the physically held
 * SEQ steps it last saw. led_*: last painted SEQ-row signature so the LED
 * row repaints only when the step, length, mode, transport, or record
 * changed.
 */
static uint8_t menuEffects_holdActive;
static uint16_t menuEffects_holdMask;
static uint8_t menuEffects_ledSerial;
static uint8_t menuEffects_ledLen;
static uint8_t menuEffects_ledMode;
static uint8_t menuEffects_ledRunning;
```

**`menuEffects_enter()`:** also clear `menuEffects_holdActive` and
`menuEffects_holdMask`, and set `menuEffects_ledSerial` to
`(uint8_t)(effects_seqSerial() - 1u)` so the first service repaints the row.

**`menuEffects_leave()`, lines 209–215:** add

```c
    uint8_t step;

    /* The Effect page owns the SEQ row and chase layer only while visible. */
    menuEffects_holdActive = 0u;
    menuEffects_holdMask = 0u;
    led_clearActive_step();
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++)
        led_setBlinkLed((uint8_t)(LED_STEP1 + step), 0u);
```

**New helpers and API, after `menuEffects_renderLeds()`:**

```c
/* Sequencer lane of one page cell: mrp is lane 0; PARAM rows map through
 * the registry; every other cell is not sequenceable. */
static uint8_t menuEffects_cellLane(const menuEffects_cell_t *cell,
                                    uint8_t *lane)
{
    const effect_record_t *record = menuEffects_record();

    if (!cell || !record)
        return 0u;
    if (cell->kind == MENU_FX_CELL_MORPH_AMOUNT) {
        *lane = EFFECT_SEQ_LANE_MORPH;
        return 1u;
    }
    if (cell->kind != MENU_FX_CELL_PARAM)
        return 0u;
    return effects_laneOfParam(record->type, cell->index, lane);
}

/* Lowest physically held step, or 0xFF. */
static uint8_t menuEffects_firstHeldStep(void)
{
    uint8_t step;

    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        if (menuEffects_holdMask & (uint16_t)(1u << step))
            return step;
    }
    return 0xFFu;
}

void menuEffects_seqButtonPressed(uint8_t step)
{
    const effect_record_t *record = menuEffects_record();

    /* `sel`: tap and hold both jump immediately (plan §11.2, A12). */
    if (record && record->seq_run_mode == EFFECT_SEQ_RUN_SEL)
        effects_seqSelect(step);
}

void menuEffects_seqHoldExpired(void)
{
    /* Mask is re-read by the service so its first pass repaints and flashes. */
    menuEffects_holdActive = 1u;
    menuEffects_holdMask = 0u;
}

uint8_t menuEffects_seqHoldActive(void)
{
    return menuEffects_holdActive;
}

/*
 * Held lock edit (plan §13.4). Seed from the first held step: its lane value
 * when locked, otherwise the value the step plays now (D1). Apply the delta,
 * clamp in the cell's domain, then write-and-lock every held step.
 */
uint8_t menuEffects_holdEdit(const menuEffects_cell_t *cell, int16_t delta)
{
    uint8_t lane;
    uint8_t first = menuEffects_firstHeldStep();
    uint8_t stored;
    int32_t next;
    uint16_t value;

    if (!menuEffects_holdActive || first == 0xFFu ||
        !menuEffects_cellLane(cell, &lane))
        return 0u;                               /* D2: ignored while held */
    next = effects_getLaneLock(scene_getActiveIndex(), first, lane, &stored)
        ? (int32_t)stored : (int32_t)menuEffects_cellValue(cell);
    next += delta;
    value = (next < 0) ? 0u : (uint16_t)((next > 255) ? 255 : next);
    menuEffects_clampValue(cell, &value);
    return effects_setSeqLaneLock(scene_getActiveIndex(), menuEffects_holdMask,
                                  lane, (uint8_t)value);
}

uint8_t menuEffects_holdDisplay(const menuEffects_cell_t *cell,
                                uint8_t *value, uint8_t *locked)
{
    uint8_t lane;
    uint8_t first = menuEffects_firstHeldStep();
    uint8_t stored;

    if (!menuEffects_holdActive || first == 0xFFu ||
        !menuEffects_cellLane(cell, &lane))
        return 0u;
    *locked = effects_getLaneLock(scene_getActiveIndex(), first, lane,
                                  &stored);
    *value = *locked ? stored : (uint8_t)menuEffects_cellValue(cell);
    return 1u;
}

/*
 * Paint the SEQ row for the Effect page (plan §13.4):
 *   steps with any lock inside the length are lit; steps beyond the length
 *   are dark; the `sel` step blinks; while running in fwd/rev/pip/rnd the
 *   active step carries the chase layer. The type's render hook runs last.
 */
void menuEffects_renderSeqLeds(void)
{
    const effect_record_t *record = menuEffects_record();
    uint8_t len;
    uint8_t active;
    uint8_t step;

    if (!record)
        return;
    len = record->seq_length;
    for (step = 0u; step < EFFECT_SEQ_STEP_COUNT; step++) {
        uint8_t led = (uint8_t)(LED_STEP1 + step);
        uint8_t lit = (uint8_t)(step < len &&
                                record->steps[step].lock_mask != 0u);
        led_setValue(lit, led);
        led_setBlinkLed(led, (uint8_t)(
            record->seq_run_mode == EFFECT_SEQ_RUN_SEL &&
            step == effects_seqSelectedStep()));
    }
    active = effects_seqActiveStep();
    if (record->seq_run_mode != EFFECT_SEQ_RUN_SEL &&
        active != EFFECT_SEQ_STEP_NONE)
        led_setActive_step(active);
    else
        led_clearActive_step();
    menuEffects_renderLeds();
}
```

**`menuEffects_service()`, lines 614–650** — add before `return actions;`:

```c
    /*
     * Lock-edit hold (Session 072 step 8): follow the physical SEQ mask,
     * flash newly held steps, end when every SEQ button is released.
     */
    if (menuEffects_holdActive) {
        uint16_t mask = buttonHandler_seqHeldMask();

        if (mask == 0u) {
            menuEffects_holdActive = 0u;
            menuEffects_holdMask = 0u;
            actions |= MENU_FX_ACT_REPAINT;
        } else if (mask != menuEffects_holdMask) {
            led_flashGroup(LED_FLASH_GROUP_SEQ,
                           (uint16_t)(mask & (uint16_t)~menuEffects_holdMask));
            menuEffects_holdMask = mask;
            actions |= MENU_FX_ACT_REPAINT;
        }
    }
    /* SEQ row: repaint only when the step, record, or transport changed. */
    if (record &&
        (effects_seqSerial() != menuEffects_ledSerial ||
         record->seq_length != menuEffects_ledLen ||
         record->seq_run_mode != menuEffects_ledMode ||
         seq_isRunning() != menuEffects_ledRunning ||
         (actions & MENU_FX_ACT_REPAIR))) {
        menuEffects_ledSerial = effects_seqSerial();
        menuEffects_ledLen = record->seq_length;
        menuEffects_ledMode = record->seq_run_mode;
        menuEffects_ledRunning = seq_isRunning();
        menuEffects_renderSeqLeds();
    }
```

`menuEffects.c` also needs `#include "sequencer.h"` for `seq_isRunning()`.

The FX chase refreshes through this service on every published step. Because
`seq_serial` changes per step, the service repaints the row once per FX step,
at most once per foreground pass.

---

## 6. `menu.c` (add/modify)

### 6.1 Effect lock markers — after `va_applyVoiceMarkers()` (ends ~line 2520), add

```c
/*
 * Effect-page held-step markers (Session 072 step 8; plan §13.4, D1).
 *
 * While a lock-edit hold is active, every visible sequenceable Effect cell
 * shows the first held step's value (menuEffects_holdDisplay); a locked lane
 * gets the Tier-1 underline on its rightmost value character. Reuses the
 * VOICE CGRAM marker transaction, so slot ownership and LCD queue safety are
 * identical; with no markers the transaction restores any stale glyph refs.
 */
static void menu_applyEffectMarkers(void)
{
    uint8_t glyph_probe[8];
    uint8_t desired_base[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_row[4] = { 0u, 0u, 0u, 0u };
    uint8_t marker_col[4] = { 0u, 0u, 0u, 0u };
    uint8_t desired_valid = 0u;
    uint8_t activePage;
    uint8_t activeParameter;
    uint8_t first;
    uint8_t count;
    uint8_t i;

    if (menu_activePage != EFFECT_PAGE)
        return;
    activePage = (uint8_t)((menuIndex & MASK_PAGE) >> PAGE_SHIFT);
    activeParameter = (uint8_t)(menuIndex & MASK_PARAMETER);
    first = editModeActive ? activeParameter : 0u;
    count = editModeActive ? 1u : 4u;
    for (i = 0u; i < count; i++) {
        uint8_t column = (uint8_t)(first + i);
        menu_cell_t cell = menu_resolveCell(activePage, column);
        uint8_t value;
        uint8_t locked;
        char *field;
        int8_t right;
        uint8_t slot = editModeActive ? 0u : i;

        if (cell.kind != MENU_CELL_EFFECT ||
            !menuEffects_holdDisplay(&cell.fx, &value, &locked))
            continue;
        field = editModeActive ? &editDisplayBuffer[1][13]
                               : &editDisplayBuffer[1][4u * i];
        va_formatValue3(&cell, value, field);
        if (!locked)
            continue;
        for (right = 2; right >= 0 && field[right] == ' '; right--)
            ;
        if (right >= 0 && lcd_underlineGlyph((uint8_t)field[right],
                                             glyph_probe)) {
            desired_base[slot] = (uint8_t)field[right];
            marker_row[slot] = 1u;
            marker_col[slot] = (uint8_t)((editModeActive ? 13u : 4u * i) +
                                         (uint8_t)right);
            desired_valid |= (uint8_t)(1u << slot);
        }
    }
    va_queueMarkerTransaction(desired_base, desired_valid,
                              marker_row, marker_col);
}
```

Add its prototype beside the `va_*` prototypes (~line 1760).

### 6.2 `menu_repaintGeneric()` (modify)

- **Manager-cell early return** (~9300):
  `if (cell.kind == MENU_CELL_EFFECT && menuEffects_paintEditView(&cell.fx)) { menu_applyEffectMarkers(); return; }`.
  The `mrp` full view is a sequenceable manager cell, so it needs the held
  value and marker too.
- **End of the function** (line 9520): after `va_applyVoiceMarkers();` add
  `menu_applyEffectMarkers();`.

### 6.3 Held edits

- **`menu_encoderChangeParameter()`**, after the `typ` browse block (line 9585
  `return;`):

```c
    /* Held SEQ steps: the encoder writes lane locks, never retained values
     * (plan §13.4, S072_ST8 D2). */
    if (cell.kind == MENU_CELL_EFFECT && menuEffects_seqHoldActive()) {
        (void)menuEffects_holdEdit(&cell.fx, inc);
        return;
    }
```

- **`menu_parseKnobDelta()`**, after the `typ` return (line 10756):

```c
    /* Held SEQ steps: pots write lane locks (plan §13.4, S072_ST8 D2). */
    if (cell.kind == MENU_CELL_EFFECT && menuEffects_seqHoldActive()) {
        if (menuEffects_holdEdit(&cell.fx, delta))
            menu_knobs_dirty = 1u;
        return;
    }
```

### 6.4 LEDs on entry — line 12195

Replace `menuEffects_renderLeds();` with `menuEffects_renderSeqLeds();`. It
paints the FX row, then calls the type hook.

### 6.5 Service — lines 10885–10897

No change is needed: `menuEffects_service()` now returns `MENU_FX_ACT_REPAINT`
for hold transitions and paints LEDs itself.

---

## 7. `buttonHandler.c` (modify)

### 7.1 `buttonHandler_armTimerActionStep()`, line 461 — before the VOICE branch

```c
    if (bh_state.selectButtonMode == SELECT_MODE_FX) {
        /*
         * Effect page: the SEQ hold threshold opens FX lock editing
         * (Session 072 step 8; plan §13.4, same threshold as VOICE View-B).
         * The timer sentinel then consumes the matching release.
         */
        menuEffects_seqHoldExpired();
        return;
    }
```

### 7.2 `buttonHandler_seqButtonPressed()`, non-SHIFT switch (after `case SELECT_MODE_PERF:` ~747)

```c
        case SELECT_MODE_FX:
            /*
             * FX page SEQ (plan §13.4): `sel` jumps on the press edge; every
             * press arms the common hold timer unless a lock-edit hold
             * already owns the row, in which case the press joins the hold
             * and its release is consumed.
             */
            menuEffects_seqButtonPressed(seqButtonPressed);
            if (menuEffects_seqHoldActive())
                buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            else
                buttonHandler_setTimeraction(seqButtonPressed);
            break;
```

The FX timer stores the 0..15 button index rather than an absolute Pattern
step. `buttonHandler_tick()` maps it back with `% NUM_STEPS_PER_BAR`, so this
is correct.

### 7.3 `buttonHandler_seqButtonReleased()` (after `case SELECT_MODE_PERF: break;` ~780)

```c
    case SELECT_MODE_FX:
        /* Hold-owned releases are consumed; a plain tap only clears its
         * timer (no Pattern step toggles on the Effect page). */
        if (menuEffects_seqHoldActive()) {
            buttonHandler_buttonTimerStepNr = TIMER_ACTION_OCCURED;
            return;
        }
        (void)buttonHandler_TimerActionOccured();
        break;
```

SHIFT+SEQ on the Effect page stays inert: the SHIFT branch has no FX case.

---

## 8. `ledHandler.c` — `led_updateCurrentStep()`, before line 1187

```c
    /*
     * The Effect page owns the chase layer for the FX-sequencer position
     * (Session 072 step 8); the Pattern chase must neither draw nor clear it
     * there, otherwise every sixteenth-note drain would erase the FX chase.
     */
    if (menu_activePage == EFFECT_PAGE)
        return;
```

The stop path in `led_processSeqLedState()` still clears the chase when
transport stops, which is correct for the FX chase too. The service repaints
on the transport change.

---

## 9. Documentation (same change set)

- **`SRAM_MANIFEST.md`:** `effects_state` 84 B; `seq_fxEvent` 1 B;
  menuEffects state 21 B.
- **`MODULE_INTERCHANGE_SPEC.md`:**
  - `StepScale` (sequencer-owned shared table; clients);
  - the TIM3 → EffectsManager FX latch contract (`seq_fxTakeEvent`);
  - the EffectsManager FX-sequencer API;
  - the menuEffects hold and LED contract;
  - ButtonHandler FX SEQ routing.
- **`BANK_PRESET_ARCHITECTURE.md`:** track scale uses the shared table (D4);
  FX lock edits are active-Scene-only until Step 10.
- **`FILESYSTEM_SPEC.md`:** PAT4 `track_scale` byte is reinterpreted in the
  shared index order (A38, D4).
- **`EFFECTS_BUS_FEATURE_PLAN.md`:**
  - §11: record D3 and the clock placement;
  - §13.4: D1 and D2;
  - §13.2: remove "provisional" (labels now shared);
  - §17.1: mark Step 8 implemented once built.
- **`MEMORY.md` volatile note:** "S072 Step 8: FX sequencer live (TIM3 latch →
  effects_service), lock editing via SEQ hold on the Effect page, shared
  StepScale table (track scale switched, playback unchanged)."

---

## 10. Build and verification gates

### Build

1. `make clean && make all` succeeds with no new warnings.
2. `link_budget.py`:
   - Record the flash delta (expected +2.8 to +3.5 KB; headroom about 12 KB).
   - `.bss` about +16 B.
   - DTCM and FXBUF are unchanged.

### Hardware (production image, a Scene with `flt`, a voice send raised, SHIFT+PERF)

3. **Lock editing.**
   - Hold SEQ 1: after the hold threshold, step 1 flashes and the page shows
     step 1's values (unlocked = current values, not underlined).
   - Turn `frq`: the value is underlined and SEQ 1 lights.
   - Hold SEQ 5+9+13 together and turn `frq` again: all three light.
   - Tap SEQ (short): nothing is written and no lock is set.
4. **`typ`/`run`/`len`/`scl` while holding** do nothing (D2). Normal edits
   resume on release.
5. **`fwd`, `len 16`, `1/16`.** Start transport: the filter jumps on steps
   1/5/9/13, and the chase runs on the SEQ row. Stop: locks stop applying and
   the chase clears.
6. **Every mode × a length and scale subset:**
   - `rev` (start at L−1);
   - `pip` (endpoints twice; with L = 4: 0 1 2 3 3 2 1 0 0 …);
   - `rnd` (repeats allowed; the boundaries hold);
   - lengths 1, 5, 16;
   - scales `/64`, `/16`, `/8.`, `1br`, `2br`: chase speed matches the table.
   - Steps beyond `len` are dark and never play.
7. **`sel`:**
   - Stopped: tap SEQ 5, and step 5's locks apply at once (audible). The
     step blinks.
   - Hold SEQ 5: it jumps and opens editing.
   - Running: `sel` stays on the selected step; no chase.
8. **Morph lane.**
   - Lock `mrp` = 255 on step 1 and 0 on step 9 (with `vol`/`frq` endpoints
     differing).
   - While running, Morph holds 255 from step 1 through step 8 and 0 from
     step 9.
   - Stop/start and Scene switch return it to the retained `mrp`.
9. **Scene-switch alignment (A17):**
   - Scene A `fwd` 1/16 len 16, Scene B `rev` 1/16 len 16, with distinct
     locks.
   - Switch from PERF just after A plays step 4. B's first applied step is
     index 11 (step 12), then 11, 10, …
   - Between the switch and that boundary, only menu values apply (D3).
10. **Type change** clears locks and LEDs, and the held Morph drops.
11. **Track scale:**
    - The STEP page `scl` shows the 14 shared labels; the default for a new
      Scene is `/16`.
    - Old cards show a stale value clamped to `/16`.
    - Track playback is unchanged.
12. **Persistence:** locks survive reboot (AutoSave) and Scene Save/Load
    (`.fx` `lane.*` lines).
13. **CPU:** the `cpu` widget with `flt` and 6 locked lanes running at `/64`
    matches Step 7 within noise.

### Not testable yet

- Pattern automation and LFO on `fx` (Step 9);
- fan-out (Step 10);
- lock removal (A15).

### Rollback

Revert the change set. There is no file-format change. The track-scale index
reinterpretation only affects display.

---

## 11. Implementation notes

### 11.1 Source changes completed

- Added `Core/Sequencer/StepScale.c/.h` as the single fourteen-entry, 96-PPQ
  scale table. Pattern track defaults and Menu formatting now use the shared
  index order; stale PAT4 values display as `/16` without being rewritten.
- Added the one-byte TIM3 FX RESET/STEP latch in `sequencer.c`. The ISR-side
  path computes `fwd`, `rev`, `pip`, and `rnd` from `seq_elapsedPpqTicks` and
  publishes only the compact event. `effects_service()` consumes it in
  foreground. Pattern-boundary early returns still publish the independent FX
  boundary.
- Expanded EffectsManager's retained resolution state to 84 bytes and added
  selected/active-step, lane lookup, lock inspection, and held-step lock-write
  APIs. Scene activation invalidates the prior step and held Morph value;
  `sel` becomes valid immediately on a SEQ selection, including while stopped.
- Added FX SEQ hold/tap routing to ButtonHandler/Menu. Sequenceable StereoFilter
  lanes and Morph locks write through EffectsManager; `typ`, `run`, `len`,
  `scl`, and other non-sequenceable cells remain inert during a hold. The
  Effect page owns lock/chase/selected-step LEDs and suppresses the Pattern
  chase layer while visible.
- Added the missing PRIMASK compatibility intrinsics to the project CMSIS shim
  so the latch's short atomic take/publish transaction uses the existing target
  compatibility boundary.

### 11.2 SRAM and link measurement

The clean ST8 production link on 2026-09-28 is:

```text
text 478720   data 416   bss 426160   dec 905296
Flash 479,136 / 491,520 B; headroom 12,384 B
ITCM 3,768 / 16,384 B
DTCM statics 4,448 B
FXBUF 126,624 B at 0x20001160; margin 3,744 B
```

The approved ST8 SRAM additions are `effects_state_t` 76→84 bytes (+8 after
alignment), `seq_fxEvent` +1 byte, and `menuEffects` +7 bytes, for a
linker-visible +16 bytes in SRAM1. DTCM and the FX arena are unchanged.

### 11.3 Verification status

- `make clean` followed by `make -j2 all`: passed.
- `git diff --check`: passed after the implementation edits.
- Existing warnings remain in unrelated filesystem, packed USB, splash, and
  nano-libc/linker code; no new ST8 warning was introduced by the final build.
- Hardware gates in §10 remain unexecuted: mode × length × scale, Scene A17
  alignment, stopped `sel`, Morph hold, persistence, and CPU measurement.

---

## 12. Assessment (2026-09-28)

### 12.1 Result

**Accepted, with one functional finding (F1) to fix before or with Step 9.**
The user confirmed on hardware (with `flt`) that the firmware runs and the
five test points pass.

### 12.2 Build

Clean rebuild (`make clean && make all`), exit 0:

```text
text 478720   data 416   bss 426160   dec 905296
Flash 479,136 / 491,520 B   headroom 12,384 B   (Step 7 → Step 8: about +3.1 KB)
DTCM statics 4,448 B        FXBUF 126,624 B at 0x20001160, margin 3,744 B
```

- **Warnings.** There are 20 warning lines, all pre-existing:
  - filesystem unused statics;
  - USB `packed`;
  - splash constants;
  - EuklidGenerator sign-compare;
  - asyncfatfs unused parameter;
  - `PatternData.c:160` packed-member address (`pat_addrPtr`, not in the ST8
    diff);
  - nano-libc stubs;
  - the LTO serial note.

  No warning comes from StepScale, sequencer, EffectsManager, menuEffects,
  menu, buttonHandler, ledHandler or the CMSIS shim.
- **Flash.** Headroom is now 12,384 B, below `link_budget.py`'s 16 KiB soft
  warning. Every later step must estimate its flash cost against this figure.
  Step 9's estimate is in `S072_ST9_IMPLEMENTATION.md` §0.

### 12.3 Code against schedule

| Area | Status | Notes |
|---|---|---|
| `StepScale.c/.h` | ✔ | 14 entries, default 4 (`/16`). The `_shortName`/`_longName` fallback to the default covers stale PAT4 values. |
| `sequencer.c` latch | ✔ | `seq_fxPublishStep` keeps RESET and ORs in STEP + index; `seq_fxPublishReset` overwrites. Take/publish sit under PRIMASK. |
| `seq_fxClockTick` | ✔ + guard | Adds a `run_mode >= COUNT` guard (good; retained data is untrusted). `rnd` uses the masked `GetRngValue()`. |
| FX tick on Pattern-boundary returns | ✔ deviation, accepted | Both `seq_handleMasterBoundary()` early returns now call `seq_fxClockTick()` + `midiParser_checkMtc()` before returning. Each path still publishes exactly once per PPQ tick (the returns skip the tail call), so there is no double publish. This fixes a real hole: the Pattern switch bar would otherwise have dropped the FX boundary. |
| RESET in `seq_setStepIndexToStart` | ✔ | Published before the Scene/voice restores. |
| `cmsis_intrinsics.h` | ✔ | `__get_PRIMASK`/`__set_PRIMASK`/`__disable_irq` added to the compat shim. This is required for the host/compat build and is in scope. |
| `EffectTypes.h` | ✔ | `EFFECT_SEQ_SCALE_*` aliases to `STEP_SCALE_*`. A `_Static_assert` in EffectsManager pins them equal. |
| PatternData / MenuText / menu.c track scale | ✔ | `TRACK_SCALE_DEFAULT`; the `trackScaleNames` table is gone. The `MENU_TRACK_SCALE` count (14) bounds encoder edits. The display falls back to `/16` for stale values. `pat_applyTrackSettingsToMenu` passes the raw byte through: no clamp was added, and none is needed (display- and encoder-bounded; storage is not rewritten, which matches the rollback note). |
| EffectsManager state/API | ✔ | 84 B `_Static_assert`; consume, active step, override, held Morph, `activateScene`/`changeType` resets as scheduled. |
| menuEffects hold / lock edit / LEDs | ✔ | As scheduled. See F2 for Morph display. |
| buttonHandler / ledHandler | ✔ | SEQ press/release and hold expiry. `led_updateCurrentStep` returns early on `EFFECT_PAGE`. |

### 12.4 Findings

**F1 (medium, functional): the `sel` selection is lost on every RESET.**
`seq_step_valid` serves two purposes:

- it validates the clock step (set by `effects_seqConsume` STEP);
- it validates the `sel` cursor (set by `effects_seqSelect`).

RESET clears it in `effects_seqConsume`. `seq_setStepIndexToStart()`
publishes RESET on:

- transport start;
- transport stop;
- every Pattern-boundary switch (`sequencer.c` ~1086);
- external reset.

In `sel` mode, `seq_fxClockTick` publishes no STEP, so nothing sets the flag
again. The result:

- Select step 5 while stopped (it applies, which passes gate 7), then press
  play. `sel` now applies **no** locks until SEQ is tapped again.
- A Pattern change drops the selection in the same way.
- After a Scene switch, `effects_seqSelectedStep()` returns NONE, so the
  selected-step LED also disappears.

This contradicts D3/A-plan (`sel` holds the selected step through transport)
and gate 7 "Running: sel stays on the selected step".

*Fix (no RAM cost; restores the scheduled §4 behavior):* `sel` needs no
validity flag, because A12 says "`sel` always applies".

1. The `effects_seqStepFor()` `sel` branch returns
   `seq_sel_step % len` unconditionally.
2. `effects_seqSelectedStep()` returns `seq_sel_step` unconditionally.
3. `effects_seqSelect()` stops setting `seq_step_valid`, which again belongs
   only to the clock step.
4. In `sel`, `effects_service()` re-latches the selected step's Morph lock
   whenever no held value is valid (after a RESET or a Scene switch). The
   Morph lane then keeps applying, as "`sel` always applies" requires.

The cursor carries across a Scene switch and is bounded by the new Scene's
`len`. That was the scheduled D3 behavior ("in `sel` the selected step
applies immediately").

Step 9 §3.1 schedules this fix, because Step 9 edits the same functions.

**F2 (low, display): a held `mrp` lock does not show its lock value.**
`menu_applyEffectMarkers()` (`menu.c` ~2559–2562) formats non-PARAM manager
cells through `menuEffects_formatValue3()`. That function writes nothing for
`MENU_FX_CELL_MORPH_AMOUNT`: it returns 0 and only handles TYPE/RUN/SCALE. The
field therefore keeps the normally rendered retained `mrp` value, while the
underline correctly marks it as locked.

*Fix:* in `menu_applyEffectMarkers()`, format `MENU_FX_CELL_MORPH_AMOUNT` with
`numtostrpu(field, value, ' ')`, the same 0..255 numeric form the normal cell
uses. Step 9 §7.8 schedules this, because Step 9 touches the same marker
function.

**F3 (note): the track-scale clamp was not added in
`pat_applyTrackSettingsToMenu`.** This is acceptable, as the table row above
explains. No action is needed.

### 12.5 Prerequisite fixes from earlier steps: still not applied

These are still absent from the tree and still need applying:

- **ST6 F1.** Blank-name Scene Save must write `"        "` rather than
  `fx_stem` (`filesystem.c` ~19657–19661 and ~19886–19890).
- **ST6 F2.** Remove the Effect-row lines from
  `filesystem_saveKitDirectory_tick` (Kit Save must not write Effect rows).
- **ST7 F1.** Add `menuEffects_typeEdit = 0u;` at the top of
  `menuEffects_selectPressed()` (`menuEffects.c` ~299).

The Step 9 schedule relists them in its change index (§1, rows P1–P3) so they
land with the Step 9 change set.

### 12.6 Hardware

The user reports hardware OK, and the five test points confirmed with `flt`.
Gates 7 (after transport start) and 8 (the `mrp` display) will show F1/F2
until those fixes are applied. The remaining §10 gates (6 mode × scale sweep,
9 A17 alignment, 12 persistence, 13 CPU) roll into Step 9's regression list.
