# S084 P1 — Implementation Schedule: Voice Foundations

**Session:** 084, phase P1 (schedule and implementation; code landed 2026-10-25,
hardware verification pending — see §16)
**Branch:** `dev-ph6-midi`
**Date:** 2026-10-25
**Plan of record:** `S084_P1_VOICE_EXPANSIONS.md` (r7). Section numbers
written "plan §x" refer to it.
**Follow-on phases:**

- `S084_P2_VOICE_EXPANSION_2.md`: sample flash, wavetables, stock waves,
  transient types, drive types.
- `S084_P3_VOICE_EXPANSION_3.md`: elastic Effects, voice buffers and tails,
  insert effects, transient buffers.

---

## 0. Read this first

### 0.1 Scope of P1

| WP | Work package | Plan |
|---|---|---|
| WP0 | DEV witness for swaps (trace stage `v`) and the CPU baseline procedure | §3.9, §8 step 0 |
| WP1 | LFO: ten one-shot / exponential-triangle waves, Scene-activation fire, `rnd` range fix | §4.5 |
| WP2 | Deferred instrument swap: real pre-fader level, real-time bound, declick, Instrument-Load preview on the shared swap | §3.9, §5.7 |
| WP3 | Oscillator data model: types `wav pwm smp slp`, dispatch, frequency per type, per-sample slew | §4.1.1 |
| WP4 | `OscillatorSample.c`: Q20.12 sample phase, `smp`/`slp`, start position | §4.1.5 |
| WP5 | `OscillatorPwm.c`: `prc psw ptr pq3 pw3 swp` | §4.1.3 |
| WP6 | `OscRenderPool.c`: the shared 4-unit pool, `itp` before waveform interpolation | §4.1.6 |
| WP7 | `OscSection.c`: `fm mix snc rng`; run modes `rst fre itp`; filter slew | §4.1.6, §4.2 |
| WP8 | Engines (Drum, Snare, Cymbal, HiHat) on `OscSection` | §5.8.4 |
| WP9 | Descriptor rows, enums, counts and menu cells (four instruments) | §4.1.1, §5.5 |
| WP10 | InstrumentManager: special writers, per-slot `wav` domain, adapter refresh, filter slew, accessors; `InstrumentCellFormat.c` | §5.1, §5.2 |
| WP11 | Menu and `MenuText.h` | §5.2, §6 r4 Q5 |
| WP12 | Build, tag proofs, decoder, host tests | §5.8.5, §9 |
| WP13 | Specification updates | §8 step 12 |

**Not in P1** (P2/P3):

- the `wtb` type and stock waves `sq3 sw3 sqa tra swa`;
- catalogue v2 / `loops/` retirement;
- transient types;
- drive types;
- Mix FX;
- voice buffers;
- elastic Effects.

In P1, `pq3`/`pw3` warp the existing `Rec`/`Saw` tables (their P2 tables
replace them later). Samples use today's v1 catalogue: `smp`/`slp` index
installed samples by number, and the v1 loop flag is ignored.

### 0.2 Line numbers

- **Baseline:** line numbers refer to the working tree on 2026-10-25:
  `dev-ph6-midi` at `59406a4` plus the uncommitted S084 P0 roll changes.
- **They drift** as earlier edits land. Every change also quotes its anchor
  text; re-find the anchor before editing.
- **Order:** apply changes within a file from the bottom up, so earlier
  line numbers stay valid.

### 0.3 Comment blocks

Every change carries a comment block, written to be pasted beside the code in
both the `.c` and the `.h` file. The blocks use this project's existing
contract layout:

```
/*
 * <Name> (S084 P1 <change id>).
 *
 * What:       ...
 * Why:        ...
 * Inputs:     ...
 * Outputs:    ...
 * Accessors:  ...
 * Affiliates: ...
 */
```

Header declarations take the same block (the `.c` copy may add an
implementation note). Removed code gets no block; the commit message and
specification record the removal.

### 0.4 Approvals needed before coding (RAM and ITCM, plan §5.6)

| Item | Region | Bytes | Owner | Lifetime |
|---|---|---:|---|---|
| `mixer_slotLastSample[6]` (int16) | SRAM1 `.bss` | 12 | `mixer.c` | static, render state |
| `mixer_slotDeclick[6]` (float) | SRAM1 `.bss` | 24 | `mixer.c` | static, render state |
| `instrumentManager_slotQuietBlocks[6]` (uint8) | SRAM1 `.bss` | 6 | `InstrumentManager.c` | static |
| `instrumentManager_slotLastPeak[6]` (uint16) | SRAM1 `.bss` | 12 | `InstrumentManager.c` | static (DEV trace and quiet test) |
| `instrumentManager_slotDeclickArm` (uint8 mask) | SRAM1 `.bss` | 1 | `InstrumentManager.c` | static |
| `preset_swapWaitStartMs[6]` (uint16) | SRAM1 `.bss` | 12 | `presetManager.c` | static |
| `instrument_swap_pending_mask` (uint8) | SRAM1 `.bss` | 1 | `presetManager.c` | static |
| `drumset_apply_waiting` (uint8) | SRAM1 `.bss` | 1 | `presetManager.c` | static |
| `drumset_apply_stall_ticks` → `drumset_apply_wait_start_ms` (uint16 → uint16) | SRAM1 `.bss` | 0 net | `presetManager.c` | static |
| `oscRenderPool_scratchSection[32]`, `oscRenderPool_scratchMod[32]` (int16) | SRAM1 `.bss` | 128 | `OscRenderPool.c` | static, render scratch |
| `oscRenderPool_unitsUsed` (uint8) | SRAM1 `.bss` | 1 | `OscRenderPool.c` | static |
| `modNode_waveInterpActiveCount` removed | DTCM `.dtcmz` | −1 (released, no reuse) | `modulationNode.c` | — |
| **Total new static** | SRAM1 | **≈ 198 B** (+ alignment) | | |

- **Inside the existing 1,176 B runtime-slot union** (no new static RAM):
  - `OscInfo` grows by about 28 B (2–3 oscillators per engine);
  - `OscSectionState` is about 3 × 100 B + 16 B per engine;
  - `Lfo` grows by 8 B.
  - The `_Static_assert`s in `InstrumentManager.c` (lines ~179–185) must
    still pass. The estimate is Drum ≈ 920 B and HiHat ≈ 860 B of 1,176 B;
    verify `sizeof` at build.
- **ITCM:** about +2.5–3.5 KB for the new oscillator renderers
  (`OscillatorPwm`, `OscillatorSample`, the sync segment helper). ITCM is
  4,168 / 16,384 B used. Report the exact figure from `link_budget.py`.
- **Flash:** about +10–14 KB text.

### 0.5 Constants introduced in P1 (single source of truth)

| Constant | Value | File |
|---|---|---|
| `OSC_TYPE_WAV / _PWM / _SMP / _SLP`, `OSC_TYPE_COUNT` | 0 / 1 / 2 / 3, 4 (P2 appends `OSC_TYPE_WTB` = 4) | `Oscillator.h` |
| `OSC_STOCK_WAVE_COUNT` | 6 (`Sin … Cym`; P2 raises it to 11) | `Oscillator.h` |
| `OSC_SAMPLE_PHASE_SHIFT` | 12 (Q20.12) | `OscillatorSample.h` |
| `OSC_PWM_PRC … OSC_PWM_SWP`, `OSC_PWM_FAMILY_COUNT` | 0…5, 6 | `OscillatorPwm.h` |
| `OSC_RUN_RST / _FRE / _ITP` | 0 / 1 / 2 | `OscSection.h` |
| `OSC_MOD_FM / _MIX / _SNC / _RNG` | 0 / 1 / 2 / 3 (0/1 keep today's stored values) | `OscSection.h` |
| `OSC_SLEW_SAMPLES`, `OSC_ITP_SAMPLES` | 132 (3 ms at 44,108 Hz) | `OscSection.h` |
| `OSC_RENDER_POOL_UNITS` | 4 (replaces `OSC_WAVE_INTERP_MAX_ACTIVE`) | `config.h` |
| `INSTRUMENT_SLOT_QUIET_PEAK` | 8 (int16 LSB ≈ −72 dBFS) | `InstrumentManager.c` |
| `INSTRUMENT_SLOT_QUIET_BLOCKS` | 8 (≈ 5.8 ms) | `InstrumentManager.c` |
| `PRESET_SWAP_FORCE_MS` | 2000 | `presetManager.c` |
| `MIXER_DECLICK_R` | 0.97758 (1 ms time constant) | `mixer.c` |
| `LFO_SINE_1 … LFO_EXP_DOWN_1`, `LFO_XTRI`, `LFO_XTRI_1`, `LFO_WAVE_COUNT` | 8…15, 16, 17, 18 | `lfo.h` |

Stored zero is today's behaviour for every new row:

- `typ` 0 = `wav`;
- `src` 0;
- `run` 0 = `rst`;
- `mixOscs` 0/1 unchanged.

Verify in step WP9-V that a missing key loads as 0 (see §15, item 1).

---

## 1. WP0 — DEV swap witness and CPU baseline

### P1-0.1 `Core/Bank/Scene/AutosaveTrace.h` — add trace stage `v`

- **Where:** line 279, before `AUTOSAVE_TRACE_STAGE_MIDI_LEARN_FILE = 'm',`
  closes the enum (insert above line 280 `} autosave_trace_stage_t;`).
- **Action:** Add.

```c
    /*
     * v: deferred instrument swaps (S084 P1 P1-0.1).
     *
     * What:       one record per committed slot swap. flags = reason:
     *             1 QUIET (pre-fader peak under the quiet threshold for the
     *             required blocks), 2 TRIGGER (forced by a note on the slot),
     *             3 BOUND (PRESET_SWAP_FORCE_MS elapsed), 4 IMMEDIATE (the slot
     *             was already quiet when the swap was requested).
     *             value32 = slot | last block peak << 8 (uint16, int16 LSB) |
     *             path << 24 (1 Scene worker, 2 per-track worker,
     *             3 Instrument load/preview).
     * Why:        S084 P1 replaces the envelope-value quiet test and the
     *             pass-count force bound (plan §3.9); hardware tests need a
     *             witness of when and why each swap happened and at what level.
     * Inputs:     preset swap commit sites.
     * Outputs:    one trace record; DEV logging only, no product state.
     * Accessors:  presetManager.c preset_traceSwap().
     * Affiliates: tools/decode_devlogs.py ('v' branch), DEV_MODES.md.
     */
    AUTOSAVE_TRACE_STAGE_VOICE_SWAP = 'v',
```

### P1-0.2 `tools/decode_devlogs.py` — decode stage `v`

- **Where:**
  - line 133, after `"m": "AUTOSAVE_TRACE_STAGE_MIDI_LEARN_FILE",` in the
    stage table;
  - `STAGE_PRODUCER` (line 136 onwards);
  - before line 1024 `else:` of the per-stage decoder (after the `'m'`
    branch ending at line 1023).
- **Action:** Add table entries and a decoder branch.
  - Table: `"v": "AUTOSAVE_TRACE_STAGE_VOICE_SWAP"`.
  - Producer: `"v": "presetManager preset_traceSwap()"`.
  - Branch: reasons `{1: "quiet", 2: "trigger", 3: "bound", 4: "immediate"}`
    and paths `{1: "scene", 2: "track", 3: "instrument"}`. Print the slot,
    reason, path, and the peak in LSB and in dBFS
    (`20·log10(peak/32767)`, `-inf` at 0).
- **Comment** (Python, above the branch):
  `# v: S084 P1 deferred instrument swap (reason, path, slot, pre-fader peak).`

### P1-0.3 CPU baseline procedure (no code)

Record in the P1 log before WP2:

- **Worst-case Scene readings:** the `cpu` widget and the underrun count on
  your worst-case Scene, with `cmp` on and off.
- **`fpseq.py --report`** counts for:
  - `transient_calcBlock` (the P3 transient cap);
  - `mixer_decimateBlock` plus the distortion share of each
    `voicePost_*` (the P3 Mix FX cap);
  - today's `calcNextOscSampleBlock` paths (the P1 oscillator reference).

These are measurements, not changes.

---

## 2. WP1 — LFO

### P1-1.1 `Core/DSPAudio/lfo.h` — wave constants

- **Where:** lines 51–58, the `#define LFO_SINE … LFO_EXP_DOWN` block.
- **Action:** Add after line 58.

```c
/*
 * One-shot and exponential-triangle LFO waves (S084 P1 P1-1.1).
 *
 * What:       values 8..15 are one-shot versions of waves 0..7 (same order:
 *             si1 tr1 su1 sd1 sq1 rn1 xu1 xd1); 16 is the exponential
 *             triangle (xtr), 17 its one-shot (xt1). Stored values 0..7 keep
 *             their meaning.
 * Why:        plan §4.5 / ENHANCED_FEATURES.md: a one-shot runs one cycle per
 *             retrigger and holds its end value; `ofs` becomes a start delay.
 * Inputs:     the lfo_wave descriptor row (MENU_LFO_WAVES).
 * Outputs:    lfo_calc() wave selection; lfoWaveNames[] count must equal
 *             LFO_WAVE_COUNT.
 * Accessors:  lfo.c, MenuText.h lfoWaveNames[], the four *Parameters.c rows.
 * Affiliates: lfo_retriggerPhase(), lfo_isOneShot(),
 *             instrumentManager_retriggerRuntimeLfos(),
 *             instrumentManager_restoreLfoPhaseIfNeeded().
 */
#define LFO_ONE_SHOT_BASE  0x08
#define LFO_SINE_1         0x08
#define LFO_TRI_1          0x09
#define LFO_SAW_UP_1       0x0A
#define LFO_SAW_DOWN_1     0x0B
#define LFO_REC_1          0x0C
#define LFO_NOISE_1        0x0D
#define LFO_EXP_UP_1       0x0E
#define LFO_EXP_DOWN_1     0x0F
#define LFO_XTRI           0x10
#define LFO_XTRI_1         0x11
#define LFO_WAVE_COUNT     18u

/* One-shot run state (S084 P1 P1-1.1): idle before the first fire, a
** start delay, the single cycle, then holding the end value. */
#define LFO_ONESHOT_IDLE   0u
#define LFO_ONESHOT_DELAY  1u
#define LFO_ONESHOT_RUN    2u
#define LFO_ONESHOT_HOLD   3u
```

### P1-1.2 `Core/DSPAudio/lfo.h` — `Lfo` struct fields

- **Where:** line 119, after `float modNodeValue;` and before
  `} Lfo;` (line 120).
- **Action:** Add.

```c
	/*
	 * One-shot state (S084 P1 P1-1.2).
	 *
	 * What:       oneShotState is LFO_ONESHOT_*; delayBlocks counts the
	 *             remaining start-delay blocks (derived from phaseOffset at
	 *             each fire).
	 * Why:        one-shot waves must stop after one cycle and may wait an
	 *             `ofs`-scaled delay before running (plan §4.5).
	 * Inputs:     lfo_retriggerPhase() arms both; lfo_calc() advances them.
	 * Outputs:    lfo_calc()'s phase handling for waves >= LFO_ONE_SHOT_BASE.
	 * Accessors:  lfo.c only.
	 * Affiliates: instrumentManager_retriggerRuntimeLfos(),
	 *             instrumentManager_restoreLfoPhaseIfNeeded(); lives inside the
	 *             engine struct, so it costs no static RAM (+8 B per LFO).
	 */
	uint8_t		oneShotState;
	uint32_t	delayBlocks;
```

### P1-1.3 `Core/DSPAudio/lfo.h` — new declarations

- **Where:** after line 141 `void lfo_retrigger(uint8_t voiceNr);`.
- **Action:** Add.

```c
/*
 * Restart an LFO's phase for a retrigger or Scene fire (S084 P1 P1-1.3).
 *
 * What:       looping waves jump to phaseOffset (today's behaviour). One-shot
 *             waves restart at phase 0 in the DELAY state with
 *             delayBlocks = phaseOffset / effective phaseInc (0 if phaseInc is
 *             0), so `ofs` becomes a start delay scaled to the LFO period.
 *             rn1 draws its single random value here.
 * Why:        one place owns the looping/one-shot difference for every caller.
 * Inputs:     lfo (non-NULL).
 * Outputs:    lfo->phase, oneShotState, delayBlocks, rnd.
 * Accessors:  instrumentManager_retriggerRuntimeLfos(),
 *             instrumentManager_restoreLfoPhaseIfNeeded().
 * Affiliates: lfo_calc(), LFO_ONE_SHOT_BASE.
 */
void lfo_retriggerPhase(Lfo *lfo);

/*
 * True for waves that run once per fire (S084 P1 P1-1.3).
 *
 * What:       waveform in 8..15 or LFO_XTRI_1.
 * Inputs:     lfo.  Outputs: 1/0.
 * Accessors:  lfo.c, InstrumentManager.c (Scene-activation fire).
 */
uint8_t lfo_isOneShot(const Lfo *lfo);
```

### P1-1.4 `Core/DSPAudio/lfo.c` — `lfo_init()`

- **Where:** lines 50–64. After line 60
  `lfo->polarity = MOD_NODE_POLARITY_NEGATIVE;`.
- **Action:** Add `lfo->oneShotState = LFO_ONESHOT_IDLE; lfo->delayBlocks = 0;`.

```c
	/*
	 * One-shot state at init (S084 P1 P1-1.4).
	 *
	 * What:       a new LFO starts IDLE with no pending delay; an idle
	 *             one-shot outputs its start value (phase 0) until it is
	 *             fired by a retrigger or a Scene activation.
	 * Why:        engine init and slot resets must leave one-shot waves in a
	 *             defined, silent-start state (plan §4.5).
	 * Inputs:     none.  Outputs: lfo->oneShotState, lfo->delayBlocks.
	 * Accessors:  each engine's *_initVoice() through lfo_init().
	 * Affiliates: lfo_retriggerPhase(), lfo_calc().
	 */
```

### P1-1.5 `Core/DSPAudio/lfo.c` — `lfo_calc()` rewrite of the phase section and noise

- **Where:** lines 66–132 (`float lfo_calc(Lfo *lfo)`).
- **Action:** Modify.
  1. **Lines 68–72 (phase advance):** a looping wave advances as today. A
     one-shot wave advances only in `LFO_ONESHOT_RUN`:
     - on overflow it enters `HOLD` with `phase = 0xFFFFFFFFu`;
     - in `DELAY`, `delayBlocks` counts down and the phase stays 0; at zero
       the state becomes `RUN`;
     - `IDLE` and `HOLD` do not advance.
  2. **Line 74 `switch(lfo->waveform)`:** switch on the *base* wave:

     ```c
     base = (w >= LFO_ONE_SHOT_BASE && w <= LFO_EXP_DOWN_1)
         ? w - LFO_ONE_SHOT_BASE
         : (w == LFO_XTRI_1 ? LFO_XTRI : w);
     ```

     Add `case LFO_XTRI:` returning `t*t*t` with
     `t = 1 − |2φ − 1|` (exponential up, then exponential down).
  3. **`LFO_REC` for one-shots:** inverted, `phase ≤ 0x7FFFFFFF → 1`, else 0.
     So `sq1` is on at once and off after half a cycle, and it holds 0. The
     looping `sqr` is unchanged.
  4. **Lines 100–107 (`LFO_NOISE`):** replace line 104 with
     `lfo->rnd = (float)(GetRngValue() & 0x7FFF) / 32767.0f;` and delete the
     commented line 103. For `rn1` the value is drawn only by
     `lfo_retriggerPhase()`.

```c
/*
 * One block of LFO output in 0..1 (S084 P1 P1-1.5).
 *
 * What:       advances the phase (looping waves always; one-shot waves only
 *             while running, with a start delay and a held end value) and
 *             returns the wave value. Noise now spans 0..1 like every wave.
 * Why:        plan §4.5 one-shots; plan §6 r4 Q17: every wave must behave the
 *             same under the negative/positive/bipolar polarity options, and
 *             noise previously spanned -1..1.
 * Inputs:     lfo (phase, phaseInc, modNodeValue, waveform, one-shot state).
 * Outputs:    value 0..1; lfo->phase, oneShotState, delayBlocks, rnd.
 * Accessors:  lfo_dispatchNextValue() once per block per slot.
 * Affiliates: lfo_retriggerPhase(), LFO_* constants, the LFO adapters in
 *             InstrumentManager (they clamp the source to 0..1).
 */
```

### P1-1.6 `Core/DSPAudio/lfo.c` — `lfo_retriggerPhase()`, `lfo_isOneShot()`

- **Where:** after line 277 (end of `lfo_retrigger()`).
- **Action:** Add both functions with the P1-1.3 blocks. Delay maths:
  `inc = (uint32_t)(lfo->phaseInc * lfo->modNodeValue)`, then
  `delayBlocks = inc ? lfo->phaseOffset / inc : 0`. One integer division
  per fire, at control rate.

### P1-1.7 `Core/DSP/Instruments/InstrumentManager.c` — retrigger uses `lfo_retriggerPhase()`

- **Where:** line 1607 `lfo->phase = lfo->phaseOffset;` in
  `instrumentManager_retriggerRuntimeLfos()`.
- **Action:** Modify to `lfo_retriggerPhase(lfo);`. Extend the existing
  comment block (lines 1590–1603) with:

```c
         * S084 P1 P1-1.7: lfo_retriggerPhase() replaces the direct phase
         * write so one-shot waves restart with their start delay; looping
         * waves still jump to phaseOffset.
```

### P1-1.8 `Core/DSP/Instruments/InstrumentManager.c` — Scene-activation fire

- **Where:** lines 2812–2825, `instrumentManager_restoreLfoPhaseIfNeeded()`.
- **Action:** Modify.
  - Fetch `lfo` first.
  - If `lfo_isOneShot(lfo)` and `lfo->retrigger` is 0 (`off`) or
    `LFO_RETRIGGER_SCENE`: call `lfo_retriggerPhase(lfo)` and return. This
    runs even when `lfo_scene_handoff.valid` is 0.
  - Otherwise keep today's body (the handoff early-return moves below the
    one-shot test).

```c
/*
 * Restore, reset or fire a slot LFO after a slot rebuild (S084 P1 P1-1.8).
 *
 * What:       one-shot LFOs with retrigger `off` or `scn` fire once when the
 *             voice's Scene activates (their start delay applies); other
 *             LFOs keep the S076 behaviour: `scn` resets to phaseOffset, the
 *             rest continue the captured phase.
 * Why:        plan §6 r4 Q17 / Q9: a one-shot with no retrigger source fires
 *             at Scene activation.
 * Inputs:     slot; the incoming LFO fields (written by the image apply just
 *             before); lfo_scene_handoff.
 * Outputs:    lfo->phase / one-shot state.
 * Accessors:  preset_resetAndApplyKitVoiceImage() (presetManager.c:1865).
 * Affiliates: lfo_retriggerPhase(), instrumentManager_captureLfoPhases().
 */
```

### P1-1.9 `Core/Menu/MenuText.h` — LFO wave names

- **Where:** lines 98–100 (`lfoWaveNames`).
- **Action:** Modify to 18 entries:

  ```
  {18}, sin tri sup sdn sqr rnd xup xdn si1 tr1 su1 sd1 sq1 rn1 xu1 xd1 xtr xt1
  ```

  Add `_Static_assert` is not possible on a static array of strings here; the
  block notes "count must equal `LFO_WAVE_COUNT` (`lfo.h`)".
- **Menu effect:** `getMaxEntriesForMenu()` (menu.c:11451) and
  `menu_automationValueMax()` (menu.c:12910) read the count, so no menu code
  changes.

---

## 3. WP2 — Deferred instrument swap (plan §3.9, §5.7)

### P1-2.1 `Core/DSP/Instruments/InstrumentManager.c` — quiet state and constants

- **Where:** line 155, `#define INSTRUMENT_AMP_EG_QUIET_THRESHOLD 0.0001f`.
- **Action:** Remove that define. Add the following below the `gateFade`
  statics (around lines 203–208).

```c
/*
 * Slot level and quiet state for deferred swaps (S084 P1 P1-2.1).
 *
 * What:       instrumentManager_slotLastPeak[] is the absolute peak of each
 *             slot's last rendered pre-volume block (int16 LSB);
 *             instrumentManager_slotQuietBlocks[] counts consecutive blocks
 *             whose peak stayed <= INSTRUMENT_SLOT_QUIET_PEAK (saturating at
 *             INSTRUMENT_SLOT_QUIET_BLOCKS); instrumentManager_slotDeclickArm
 *             is a bit mask of slots whose next rendered block must be
 *             declicked because the slot was just rebuilt.
 * Why:        plan §3.9: the old test read the amp-envelope value, which
 *             ignores the post-envelope drive (up to +48 dB) and is not the
 *             signal. The real pre-fader level is measured by the mixer.
 * Inputs:     instrumentManager_noteSlotBlockPeak() (mixer, every block);
 *             instrumentManager_resetRuntimeSlotFromScene() (arm, counters).
 * Outputs:    instrumentManager_slotQuiet(), instrumentManager_slotLastPeak(),
 *             instrumentManager_takeSlotDeclickArm().
 * Accessors:  mixer.c, presetManager.c.
 * Affiliates: P1-2.4 (mixer), P1-2.6..P1-2.10 (swap workers). 19 B SRAM1,
 *             approved in §0.4.
 */
#define INSTRUMENT_SLOT_QUIET_PEAK   8u      /* int16 LSB, about -72 dBFS */
#define INSTRUMENT_SLOT_QUIET_BLOCKS 8u      /* about 5.8 ms */
static uint16_t instrumentManager_slotLastPeak[INSTRUMENT_SLOT_COUNT];
static uint8_t  instrumentManager_slotQuietBlocks[INSTRUMENT_SLOT_COUNT];
static uint8_t  instrumentManager_slotDeclickArm;
```

### P1-2.2 `Core/DSP/Instruments/InstrumentManager.c` — replace `instrumentManager_ampEnvelopeQuiet()`

- **Where:** lines 1386–1406.
- **Action:** Modify.
  - Rename to `instrumentManager_slotQuiet()`. The body returns
    `instrumentManager_slotQuietBlocks[slot] >= INSTRUMENT_SLOT_QUIET_BLOCKS`;
    an invalid slot returns 1.
  - Add `instrumentManager_noteSlotBlockPeak()`,
    `instrumentManager_slotLastPeak()` and
    `instrumentManager_takeSlotDeclickArm()` after it.

```c
/*
 * Whether a slot is quiet enough to swap (S084 P1 P1-2.2).
 *
 * What:       1 when the slot's pre-fader block peak has stayed at or below
 *             INSTRUMENT_SLOT_QUIET_PEAK for INSTRUMENT_SLOT_QUIET_BLOCKS
 *             consecutive blocks (or the slot is invalid).
 * Why:        replaces the amp-envelope-value test (plan §3.9, §5.7).
 * Inputs:     slot.  Outputs: 1 quiet / 0 sounding.
 * Accessors:  preset_tickSingleVoiceApply(), preset_tickDrumsetApply(),
 *             preset_startInstrumentApplyImage(), preset_tickInstrumentSwap().
 * Affiliates: instrumentManager_noteSlotBlockPeak().
 */
uint8_t instrumentManager_slotQuiet(uint8_t slot);

/*
 * Record one rendered block's pre-volume peak (S084 P1 P1-2.2).
 *
 * What:       stores the peak and advances or clears the quiet counter.
 * Why:        the mixer sees every slot's rendered block once; this keeps the
 *             quiet state in its owner without the mixer knowing thresholds.
 * Inputs:     slot 0..5, peak = max |sample| of the block before volume,
 *             fader, pan and decimation.
 * Outputs:    instrumentManager_slotLastPeak[], ..._slotQuietBlocks[].
 * Accessors:  mixer_calcNextSampleBlock() (P1-2.4).
 * Affiliates: instrumentManager_slotQuiet(). Foreground render context.
 */
void instrumentManager_noteSlotBlockPeak(uint8_t slot, uint16_t peak);

/*
 * Last recorded block peak of a slot (S084 P1 P1-2.2).
 * Inputs: slot. Outputs: peak in int16 LSB (0 for invalid slots).
 * Accessors: presetManager.c preset_traceSwap() (DEV trace 'v').
 */
uint16_t instrumentManager_slotLastPeak(uint8_t slot);

/*
 * Consume a slot's declick request (S084 P1 P1-2.2).
 *
 * What:       returns 1 once after the slot was rebuilt, then clears it.
 * Why:        the mixer applies the swap declick on the first block of the
 *             new instrument (plan §5.7 item 4); the arm is set where every
 *             swap path converges (resetRuntimeSlotFromScene).
 * Inputs:     slot. Outputs: 1 if armed.
 * Accessors:  mixer_calcNextSampleBlock() (P1-2.4).
 */
uint8_t instrumentManager_takeSlotDeclickArm(uint8_t slot);
```

- **`InstrumentManager.h` line 594:** replace the declaration
  `uint8_t instrumentManager_ampEnvelopeQuiet(uint8_t slot);` with the four
  declarations and blocks above.

### P1-2.3 `Core/DSP/Instruments/InstrumentManager.c` — arm the declick on every slot rebuild

- **Where:** lines 1514–1517 in `instrumentManager_resetRuntimeSlotFromScene()`,
  after the gate-fade reset.
- **Action:** Add:
  - `instrumentManager_slotDeclickArm |= (uint8_t)(1u << slot);`
  - `instrumentManager_slotQuietBlocks[slot] = INSTRUMENT_SLOT_QUIET_BLOCKS;`
    (the new instrument has not sounded yet).

```c
    /*
     * S084 P1 P1-2.3: a rebuilt slot declicks its first block and starts
     * quiet. Every swap path (Scene worker, per-track worker, Instrument
     * load/preview, trigger force, boot) converges here, so the declick
     * covers all of them. Affiliates: instrumentManager_takeSlotDeclickArm(),
     * mixer_calcNextSampleBlock().
     */
```

### P1-2.4 `Core/DSPAudio/mixer.c` — peak tracking and swap declick per slot

- **Where:**
  - **Statics:** near lines 128–129 (`mixer_decimation_rate/cnt`).
  - **Loop:** line 965 `instrumentManager_calcSlotSyncBlock(slot, sampleData, OUTPUT_DMA_SIZE);`.
    Insert the call between lines 965 and 966
    (`mixer_decimateBlock(slot,sampleData);`).
- **Action:** Add statics (plain `static`, SRAM1, not `INCCMZ`):
  - `static int16_t mixer_slotLastSample[6];`
  - `static float mixer_slotDeclick[6];`
  - `#define MIXER_DECLICK_R 0.97758f`
  - a `static inline void mixer_slotLevelAndDeclick(uint8_t slot, int16_t *b)`.

```c
/*
 * Slot level witness and swap declick (S084 P1 P1-2.4).
 *
 * What:       for one rendered pre-volume block: (1) measures the absolute
 *             peak and reports it to InstrumentManager; (2) if the slot was
 *             just rebuilt, sets the declick offset to
 *             (last output sample of the old voice - first sample of the new
 *             one); (3) adds the decaying offset to every sample with __SSAT
 *             (offset *= MIXER_DECLICK_R per sample, about 1 ms); (4) stores
 *             the block's last sample. Steps (1), (3), (4) run every block
 *             for every slot (constant CPU); the offset is 0 when idle.
 * Why:        plan §5.7: swaps are judged on the real pre-fader level and
 *             never click, whether quiet, triggered or forced by the bound.
 * Inputs:     slot 0..5; b = the block straight from
 *             instrumentManager_calcSlotSyncBlock() (before decimation).
 * Outputs:    b (declicked in place); mixer_slotLastSample[slot],
 *             mixer_slotDeclick[slot]; instrumentManager_noteSlotBlockPeak().
 * Accessors:  mixer_calcNextSampleBlock() slot loop only.
 * Affiliates: instrumentManager_takeSlotDeclickArm(), P1-2.3. About 5
 *             instructions per sample per slot. 36 B SRAM1 (§0.4).
 */
```

The peak is taken **before** the declick, so it is the voice's own level.

### P1-2.5 `Core/Bank/Scene/Preset/presetManager.c` — constants and statics

- **Where:** lines 236–246.
- **Action:** Modify.
  - **Remove** `#define DRUMSET_APPLY_FORCE_TICKS 1000u` (line 244) and its
    comment (lines ~229–243, which wrongly called 1,000 passes "one second").
  - **Replace** line 246 `static uint16_t drumset_apply_stall_ticks = 0u;`
    with the statics below.

```c
/*
 * Real-time bound for deferred slot swaps (S084 P1 P1-2.5).
 *
 * What:       PRESET_SWAP_FORCE_MS is the longest a deferred swap waits for a
 *             quiet slot or a trigger before it is forced (with the mixer's
 *             declick). drumset_apply_wait_start_ms / drumset_apply_waiting
 *             time the Scene worker's current non-quiet episode;
 *             preset_swapWaitStartMs[] time each slot's per-track or
 *             Instrument swap; instrument_swap_pending_mask marks Instrument
 *             loads/previews waiting for their slot.
 * Why:        plan §3.9: the old bound counted unpaced main-loop passes
 *             (about tens of ms, not the documented second), which forced
 *             ringing voices early. time_sysTick is the 1 ms TIM6 counter;
 *             distances are compared as signed 16-bit (wrap-safe, as
 *             led_timeReached()).
 * Inputs:     time_sysTick (timebase.h).
 * Outputs:    the force decisions in preset_tickDrumsetApply(),
 *             preset_tickSingleVoiceApply(), preset_tickInstrumentSwap().
 * Accessors:  presetManager.c only.
 * Affiliates: instrumentManager_slotQuiet(), P1-0.1 trace 'v'.
 *             14 B SRAM1 net (§0.4).
 */
#define PRESET_SWAP_FORCE_MS 2000u
static uint16_t drumset_apply_wait_start_ms = 0u;
static uint8_t  drumset_apply_waiting = 0u;
static uint16_t preset_swapWaitStartMs[INSTRUMENT_SLOT_COUNT];
static uint8_t  instrument_swap_pending_mask = 0u;
```

- **Includes:** add `#include "timebase.h"` with the other includes, if it
  is not already reachable.
- **New static helpers**, placed before `preset_commitSingleVoiceSlot()`
  (line 2019):
  - `preset_swapWaitExpired(uint16_t start)` →
    `(int16_t)(time_sysTick - start) >= (int16_t)PRESET_SWAP_FORCE_MS`;
  - `preset_traceSwap(slot, reason, path)` → `autosaveTrace_record(AUTOSAVE_TRACE_STAGE_VOICE_SWAP, reason, slot | peak<<8 | path<<24)`.

```c
/*
 * Whether a deferred swap has waited past the bound (S084 P1 P1-2.5).
 *
 * What:       1 when PRESET_SWAP_FORCE_MS or more have passed since start,
 *             compared as a signed 16-bit distance on time_sysTick.
 * Why:        the bound must be real time and wrap-safe (time_sysTick wraps
 *             every 65.5 s); the old pass counter was neither (plan §3.9).
 * Inputs:     start (a time_sysTick stamp).  Outputs: 1 expired / 0 waiting.
 * Accessors:  preset_tickDrumsetApply(), preset_tickSingleVoiceApply(),
 *             preset_tickInstrumentSwap().
 * Affiliates: led_timeReached() (same wrap rule), PRESET_SWAP_FORCE_MS.
 */
static uint8_t preset_swapWaitExpired(uint16_t start);

/*
 * Record one committed swap in the DEV trace (S084 P1 P1-2.5).
 *
 * What:       writes stage 'v': flags = reason (1 quiet, 2 trigger, 3 bound,
 *             4 immediate), value = slot | last pre-fader block peak << 8 |
 *             path << 24 (1 Scene, 2 per-track, 3 Instrument).
 * Why:        hardware verification of plan §5.7 needs to see why and at what
 *             level each swap happened.
 * Inputs:     slot, reason, path; instrumentManager_slotLastPeak().
 * Outputs:    one trace record (DEV logging only; compiled out otherwise,
 *             like every autosaveTrace_record() call).
 * Accessors:  every swap commit site in presetManager.c.
 * Affiliates: AUTOSAVE_TRACE_STAGE_VOICE_SWAP, tools/decode_devlogs.py.
 */
static void preset_traceSwap(uint8_t slot, uint8_t reason, uint8_t path);
```

### P1-2.6 `presetManager.c` — per-track worker uses the real level and the bound

- **Where:** lines 2044–2065, `preset_tickSingleVoiceApply()`.
- **Action:** Modify the loop body.
  - The test becomes `if (!instrumentManager_slotQuiet(slot) && !preset_swapWaitExpired(preset_swapWaitStartMs[slot])) continue;`.
  - On commit, trace reason QUIET or BOUND with path 2.
- **`preset_startSingleVoiceApply()`** (line 2102): also set
  `preset_swapWaitStartMs[slot] = time_sysTick;`.
- **Block** (replaces lines 2031–2042):

```c
/*
 * Advance at most one deferred per-track single-voice apply
 * (S077 P2 §3.4; S084 P1 P1-2.6).
 *
 * What:       commits the first pending slot that is quiet (real pre-fader
 *             level) or whose wait passed PRESET_SWAP_FORCE_MS; the mixer
 *             declicks the first block of the new instrument.
 * Why:        a per-track Scene assignment hot-swaps one slot without a
 *             click and without cutting a ringing voice early (plan §5.7).
 * Inputs:     single_voice_pending_mask, instrumentManager_slotQuiet(),
 *             preset_swapWaitStartMs[].
 * Outputs:    1 when one slot committed this pass.
 * Accessors:  preset_tickDrumsetApply().
 * Affiliates: preset_commitSingleVoiceSlot(), trace 'v' (path 2),
 *             preset_applyDeferredSceneSlotForTrigger() (trigger force).
 */
```

### P1-2.7 `presetManager.c` — Scene worker uses the real level and a real-time bound

- **Where:** lines 2177–2196 inside `preset_tickDrumsetApply()`.
- **Action:** Modify.
  - `instrumentManager_ampEnvelopeQuiet(voice)` → `instrumentManager_slotQuiet(voice)`.
  - Replace the stall-count branch (lines 2179–2192): on the first non-quiet
    observation set `drumset_apply_waiting = 1` and
    `drumset_apply_wait_start_ms = time_sysTick`. When
    `preset_swapWaitExpired()`, force this voice through the existing
    `preset_applyDeferredSceneSlotForTrigger(voice)` and trace BOUND, path 1.
  - Line 2194 `drumset_apply_stall_ticks = 0u;` → `drumset_apply_waiting = 0u;`.
  - Trace QUIET, path 1, after `preset_resetAndApplyKitVoiceImage()` (line 2196).
- Replace the comment at lines 2180–2187 with:

```c
            /*
             * S084 P1 P1-2.7: no quiet result for this voice. The force is a
             * real-time bound (PRESET_SWAP_FORCE_MS from the first non-quiet
             * observation in this episode), not a pass count: main-loop
             * passes are unpaced, so the old 1,000-pass bound forced ringing
             * voices after tens of milliseconds. The forced commit reuses the
             * trigger-time path; the mixer declicks it.
             */
```

### P1-2.8 `presetManager.c` — Instrument load/preview defers instead of resetting a sounding slot

- **Where:** lines 2724–2735 at the end of `preset_startInstrumentApplyImage()`:
  `if (!active_scene_touched) return; instrumentManager_resetRuntimeSlot(slot); …`
- **Action:** Modify.
  - If `instrumentManager_slotQuiet(slot)`: keep today's immediate path
    (lines 2727–2735) and trace IMMEDIATE, path 3.
  - Otherwise call `preset_armInstrumentSwap(slot)` and return with
    `instrument_apply_active` still 0. Menu's `menu_tickInstrumentApply()`
    (menu.c:806) then sees the apply finished and **releases
    `menu_storageBusy` at once**, so scrolling never waits (Load/Save Slot
    Scrolling Policy).

```c
    /*
     * S084 P1 P1-2.8: Instrument load and Instrument-Load preview use the
     * shared deferred swap (plan §5.7 item 5, §6 r4 Q28). A quiet slot swaps
     * now; a sounding slot keeps playing its old instrument until it is
     * quiet, its next trigger, or PRESET_SWAP_FORCE_MS, and the Scene images
     * already hold the new instrument (committed above). The wait never holds
     * the Menu: instrument_apply_active stays 0, so menu_storageBusy clears
     * and scrolling continues; a newer preview simply rewrites the images and
     * re-arms the same slot. Affiliates: preset_armInstrumentSwap(),
     * preset_tickInstrumentSwap(), preset_applyDeferredSceneSlotForTrigger().
     */
```

### P1-2.9 `presetManager.c` — `preset_armInstrumentSwap()`, `preset_commitInstrumentSwapSlot()`, `preset_tickInstrumentSwap()`

- **Where:** new static functions placed after `preset_commitSingleVoiceSlot()`
  (after line 2029).
- **Action:** Add.

```c
/*
 * Arm a deferred Instrument swap for one active-Scene slot (S084 P1 P1-2.9).
 *
 * What:       captures the slot's LFO phase, sets its bit in
 *             instrument_swap_pending_mask and starts its wait timer. The
 *             slot keeps rendering its old instrument.
 * Why:        an Instrument load/preview must not cut a ringing voice and must
 *             not hold the Menu while it waits (plan §5.7).
 * Inputs:     slot 0..5 (active Scene; its images already updated).
 * Outputs:    instrument_swap_pending_mask, preset_swapWaitStartMs[slot].
 * Accessors:  preset_startInstrumentApplyImage().
 * Affiliates: instrumentManager_captureLfoPhaseForSlot(),
 *             preset_tickInstrumentSwap().
 */
static void preset_armInstrumentSwap(uint8_t slot);

/*
 * Commit a deferred Instrument swap (S084 P1 P1-2.9).
 *
 * What:       rebuilds the slot from the active Scene with the Scene-switch
 *             commit (preset_resetAndApplyKitVoiceImage(): reset, routing,
 *             immediate voice Morph image, LFO phase), applies the slot's
 *             supplemental targets (preset_applyKitVoiceSupplementalForced())
 *             and starts the existing all-source modulation rebind
 *             (preset_startSceneModulationRebind()) on the background worker
 *             (sets drumset_apply_active/drumset_apply_scene with an empty
 *             pending mask, so preset_tickDrumsetApply() drains the cursor and
 *             finishes as it does after a Scene switch).
 * Why:        reuse the proven Scene-switch commit and rebind instead of a new
 *             path (user, r4 Q28: "re-use the code").
 * Inputs:     slot; scene_getActiveIndex().
 * Outputs:    the rebuilt slot; pending bit cleared; trace 'v' path 3.
 * Accessors:  preset_tickInstrumentSwap(),
 *             preset_applyDeferredSceneSlotForTrigger().
 * Affiliates: P1-2.3 declick arm (inside the reset).
 */
static void preset_commitInstrumentSwapSlot(uint8_t slot, uint8_t reason);

/*
 * Advance at most one deferred Instrument swap (S084 P1 P1-2.9).
 *
 * What:       commits the first pending slot that is quiet or past the
 *             bound.
 * Inputs:     instrument_swap_pending_mask, instrumentManager_slotQuiet(),
 *             preset_swapWaitStartMs[].
 * Outputs:    1 when one slot committed this pass.
 * Accessors:  preset_tickDrumsetApply() (called before the per-track worker),
 *             which Menu ticks in the background at menu.c:16707 outside the
 *             storage-busy path.
 * Affiliates: preset_commitInstrumentSwapSlot().
 */
static uint8_t preset_tickInstrumentSwap(void);
```

### P1-2.10 `presetManager.c` — wire the Instrument swap into the workers and the trigger path

- **Background worker:** line 2125 (`if (preset_tickSingleVoiceApply()) return 1u;`
  in `preset_tickDrumsetApply()`). Add before it:
  `if (preset_tickInstrumentSwap()) return 1u;`
- **Trigger path:** line 2234 (top of the work in
  `preset_applyDeferredSceneSlotForTrigger()`). Add a first block:

  ```c
  if (trigger_track <= INSTRUMENT_SLOT_COUNT) {
      ivoice = (trigger_track >= INSTRUMENT_SLOT_COUNT)
          ? 5 : trigger_track;
      if (instrument_swap_pending_mask & (1u << ivoice))
          preset_commitInstrumentSwapSlot(ivoice, 2 /* TRIGGER */);
  }
  ```

  Comment block above the new lines:

```c
    /*
     * Fast swap of a pending Instrument load on trigger (S084 P1 P1-2.10).
     *
     * What:       if the triggered slot has an Instrument load/preview
     *             waiting, commit it now, before the note reaches the
     *             engine.
     * Why:        plan §5.7 item 4 / r4 Q28: a retrigger is the natural swap
     *             point; the new instrument plays the note; the mixer
     *             declicks the first block.
     * Inputs:     trigger_track (0..6; 6 maps to slot 5),
     *             instrument_swap_pending_mask.
     * Outputs:    the slot rebuilt; pending bit cleared; trace 'v' reason
     *             TRIGGER, path 3.
     * Accessors:  voiceControl_triggerNow() via MidiVoiceControl.c:211 (the
     *             single trigger funnel).
     * Affiliates: preset_commitInstrumentSwapSlot(), P1-2.3 declick arm.
     */
```

  - In the per-track block (line 2242, `preset_commitSingleVoiceSlot(svoice);`):
    add trace TRIGGER, path 2.
  - In the Scene block (line 2261): add trace TRIGGER, path 1.
- **Idle test:** line 2112 `preset_applyWorkersIdle()`. Add
  `&& instrument_swap_pending_mask == 0u`. Extend its comment with:
  "pending Instrument swaps count as work".
- **Supersession:** in `preset_startDrumsetApply()` (search
  `drumset_apply_pending_mask =` where the Scene switch arms all slots), add
  `instrument_swap_pending_mask = 0u;`. A whole-Scene switch supersedes a
  pending Instrument swap, because the new Scene's image replaces the slot
  anyway.
- **Per-track supersession:** in `preset_startSingleVoiceApply()` (line 2093
  area), clear the slot's bit in `instrument_swap_pending_mask`.

### P1-2.11 Known interaction (documented, no change)

- `preset_startInstrumentApplyImage()` already clears all runtime modulation
  targets when the active Scene is touched (line 2633).
- While an Instrument swap waits, LFOs aimed at that slot from other slots
  are detached until the rebind after the commit. That is at most
  `PRESET_SWAP_FORCE_MS`.
- Recorded in plan §5.7 and `BANK_PRESET_ARCHITECTURE.md`.

---

## 4. WP3 — Oscillator data model

### P1-3.1 `Core/DSPAudio/Oscillator.h` — type constants

- **Where:** after line 55 `#define OSC_SAMPLE_START 0x06`.
- **Action:**
  - Add the type constants and `OSC_STOCK_WAVE_COUNT`.
  - **Remove** `OSC_SAMPLE_START`. Samples leave the `wav` list; every
    reference is replaced by the `smp`/`slp` types (grep: Oscillator.c:162,
    760, 815, 611; modulationNode.c:79).

```c
/*
 * Oscillator types (S084 P1 P1-3.1).
 *
 * What:       OSC_TYPE_WAV plays the stock list (SINE..CRASH; P2 appends the
 *             new stock waves); OSC_TYPE_PWM plays a computed pulse-width
 *             family (OscillatorPwm.c); OSC_TYPE_SMP / OSC_TYPE_SLP play an
 *             installed sample one-shot / looped (OscillatorSample.c).
 *             OSC_TYPE_WTB (4) is appended in P2. OSC_STOCK_WAVE_COUNT is the
 *             number of `wav` entries.
 * Why:        plan §4.1.1: each type exposes its continuous control on the
 *             existing `wav` row, so sweeps stay inside the selection.
 * Inputs:     the oscN_type descriptor rows (IM_SPECIAL_OSC_TYPE).
 * Outputs:    OscInfo.oscType; render and frequency dispatch.
 * Accessors:  Oscillator.c, OscillatorPwm.c, OscillatorSample.c,
 *             OscSection.c, InstrumentManager.c, InstrumentCellFormat.c.
 * Affiliates: MenuText.h oscTypeNames[] (same order, same count).
 */
#define OSC_TYPE_WAV          0u
#define OSC_TYPE_PWM          1u
#define OSC_TYPE_SMP          2u
#define OSC_TYPE_SLP          3u
#define OSC_TYPE_COUNT        4u
#define OSC_STOCK_WAVE_COUNT  (CRASH + 1u)
```

### P1-3.2 `Core/DSPAudio/Oscillator.h` — `OscInfo` fields

- **Where:** lines 61–92. Insert after line 69 `uint8_t tableOffset;`, and
  modify lines 82–86.
- **Action:**
  - Add `oscType`, `source`, `freqCacheType`.
  - Add the slew fields: `phaseIncTarget` (uint32), `phaseIncStep` (int32),
    `slewSamplesLeft` (uint16).
  - Add the sample bounds: `sampleStart`, `sampleEnd` (uint32, frame
    indices).
  - Add the pwm block: `OscPwmState pwm` (from `OscillatorPwm.h`, 16 B).
  - **Remove** `sampleCacheLooped` (line 83); loop is now the type.

```c
	/*
	 * Type, source and slew state (S084 P1 P1-3.2).
	 *
	 * What:       oscType = OSC_TYPE_*; source = pwm family or sample index
	 *             (unused for `wav`); freqCacheType extends the frequency
	 *             cache key; phaseIncTarget/phaseIncStep/slewSamplesLeft
	 *             implement the per-sample `fre` pitch slew (renderers add
	 *             phaseIncStep to phaseInc every sample, always);
	 *             sampleStart/sampleEnd bound smp/slp playback (frames);
	 *             pwm holds the per-block width parameters.
	 * Why:        plan §4.1.1 (types), §4.1.5 (start/loop), §4.1.6 (slew).
	 * Inputs:     IM_SPECIAL_OSC_TYPE / _OSC_SOURCE / _OSC_WAVE writers;
	 *             osc_setFreq() (target, step); oscSection_beginTrigger()
	 *             (slew arm).
	 * Outputs:    read by every block renderer and osc_setFreq().
	 * Accessors:  Oscillator.c, OscillatorPwm.c, OscillatorSample.c,
	 *             OscSection.c, InstrumentManager.c.
	 * Affiliates: inside the engine structs (runtime-slot union); the
	 *             _Static_asserts in InstrumentManager.c bound the growth.
	 */
	uint8_t		oscType;
	uint8_t		source;
	uint8_t		freqCacheType;
	uint16_t	slewSamplesLeft;
	uint32_t	phaseIncTarget;
	int32_t		phaseIncStep;
	uint32_t	sampleStart;
	uint32_t	sampleEnd;
	OscPwmState	pwm;
```

- **Header:** `#include "OscillatorPwm.h"` above the struct. That header
  must not include `Oscillator.h`; it declares only `OscPwmState` and
  forward-declares `struct OscStruct`.

### P1-3.3 `Core/DSPAudio/Oscillator.h` — new declarations

- **Where:** after line 151's last prototype.
- **Action:** Add `osc_armSlew()`.

```c
/*
 * Arm the `fre` pitch slew for one oscillator (S084 P1 P1-3.3).
 *
 * What:       records the current phaseInc as the slew origin and
 *             OSC_SLEW_SAMPLES as its length; the next osc_setFreq() computes
 *             the per-sample step toward the new target.
 * Why:        plan §4.1.6: `fre` never resets phase; every oscillator glides
 *             from its previous pitch to the new one over about 3 ms.
 * Inputs:     osc.  Outputs: osc->slewSamplesLeft.
 * Accessors:  oscSection_beginTrigger(), oscSection_claimPending() (fallback).
 * Affiliates: osc_setFreq(), the renderers' per-sample phaseIncStep.
 */
void osc_armSlew(OscInfo* osc);
```

### P1-3.4 `Core/DSPAudio/Oscillator.c` — remove the sample code (moves to WP4)

- **Where:**
  - lines 138–158 `osc_updateSampleCache()`;
  - lines 758–811 `calcUserSampleOscFmBlock()`;
  - lines 813–865 `calcUserSampleOscBlock()`;
  - lines 938–941 `osc_calcUserSampleFreqValue()`.

  `freq2PhaseIncr32767()` (lines 118–121) **stays**: the `CRASH` path's
  `osc_calcSampleFreqValue()` still uses it.
- **Action:** Remove (re-homed in `OscillatorSample.c`, P1-4.x). The `CRASH`
  path (`calcSampleOscBlock`/`calcSampleOscFmBlock`, the `>>17` crash index)
  stays unchanged in `Oscillator.c`. Delete the declarations at
  `Oscillator.h` lines ~142–147 (`calcUserSampleOscFmBlock`,
  `calcUserSampleOscBlock`).

### P1-3.5 `Core/DSPAudio/Oscillator.c` — waveform-interpolation gating

- **Where:** lines 160–180 `osc_waveInterpActive()`.
- **Action:** Modify.
  - Return 0 unless `osc->oscType == OSC_TYPE_WAV`.
  - `maxWave` becomes `OSC_STOCK_WAVE_COUNT - 1u` (line 162; samples are no
    longer in the list).
  - Comment addition:

```c
	/* S084 P1 P1-3.5: interpolation applies to the `wav` stock list only;
	** pwm/smp/slp controls are continuous by design and never use it. The
	** budget is now granted by OscRenderPool (modulationNode.c), after any
	** `itp` crossfades have claimed their units. */
```

### P1-3.6 `Core/DSPAudio/Oscillator.c` — block dispatch by type

- **Where:**
  - `calcNextOscSampleBlock()` lines 577–616;
  - `calcNextOscSampleFmBlock()` lines 442–481.
- **Action:** Modify.
  - Before today's `osc_waveInterpActive` test, dispatch:
    - `OSC_TYPE_PWM` → `oscPwm_render(osc, buf, size, gain)` /
      `oscPwm_renderFm(...)`;
    - `OSC_TYPE_SMP`/`OSC_TYPE_SLP` → `oscSample_render()` /
      `oscSample_renderFm()`.
  - The remaining `switch(osc->waveform)` handles `wav`. Its `default:`
    sample branches (lines 476–479 and 610–614) become `default: memset(buf,
    0, size*2); break;` (an out-of-list `wav` value renders silence; the
    writer clamps it anyway).

```c
/*
 * Render one oscillator block by type (S084 P1 P1-3.6).
 *
 * What:       pwm and sample types go to their own renderers; `wav` keeps the
 *             existing waveform switch and waveform interpolation.
 * Why:        plan §4.1.1 type dispatch; keeps every type's hot loop
 *             specialised (no per-sample type test).
 * Inputs:     osc (type, waveform, phase state), buf, size, gain.
 * Outputs:    buf (size samples, scaled by gain), osc phase advanced.
 * Accessors:  the engines through OscSection (P1-7), the interpolation path.
 * Affiliates: oscPwm_render*(), oscSample_render*(), osc_setFreq().
 */
```

### P1-3.7 `Core/DSPAudio/Oscillator.c` — per-sample slew in every renderer

- **Where:** every `INITCM` block loop that advances `osc->phase` by
  `osc->phaseInc`:
  - `calcSineBlock` line 316;
  - `calcFmSineBlock` 364;
  - `calcFmBlock` 411;
  - `calcNoiseBlock` 530;
  - `calcWavetableOscBlock` 676;
  - `calcSampleOscFmBlock` 727;
  - `calcSampleOscBlock` 885;
  - `calcPeriodicInterp*Block` (230, 278), which advance `startPhase` by
    `phaseInc*size`.
- **Action:** Modify.
  - Load `inc = osc->phaseInc` and `step = osc->phaseIncStep` into locals
    before each loop.
  - Per sample: `phase += inc; inc += (uint32_t)step;`.
  - Write `inc` back after the loop.
  - The interpolation helpers advance by summing the same ramp:
    `size*inc + step*size*(size-1)/2`.
- Comment block (once above `calcSineBlock()`, the first renderer), with a
  one-line pointer `/* S084 P1 P1-3.7: per-sample slew, see calcSineBlock(). */`
  in each other loop:

```c
	/*
	 * Per-sample pitch slew in every block renderer (S084 P1 P1-3.7).
	 *
	 * What:       the loop advances phase by a local increment and then adds
	 *             osc->phaseIncStep to that increment every sample; the final
	 *             increment is written back to osc->phaseInc.
	 * Why:        run mode `fre` glides every oscillator from its previous
	 *             pitch to the new one over OSC_SLEW_SAMPLES without zipper
	 *             steps (plan §4.1.6). The add always executes, with step 0
	 *             when no slew runs, so the cost never depends on the mode
	 *             (constant CPU).
	 * Inputs:     osc->phaseInc, osc->phaseIncStep (set by osc_setFreq()).
	 * Outputs:    osc->phase, osc->phaseInc.
	 * Accessors:  the block renderers listed above (ITCM).
	 * Affiliates: osc_setFreq() (computes the step), osc_armSlew(),
	 *             OscillatorPwm.c / OscillatorSample.c (same rule).
	 */
```

### P1-3.8 `Core/DSPAudio/Oscillator.c` — `osc_setFreq()` per type, cache key, slew step

- **Where:** lines 943–991.
- **Action:** Modify.
  1. **Cache key:** add `osc->freqCacheType == osc->oscType` to the test on
     lines 949–953 and store `freqCacheType` at line 988.
  2. **Dispatch** at line 955: switch on `osc->oscType` first:
     - `PWM` → `oscPwm_prepare(osc, currentFreq)`, which sets the
       phase-increment *target*, the table level and the width parameters;
     - `SMP`/`SLP` → `osc->phaseIncTarget = oscSample_phaseIncForFreq(currentFreq)`;
     - `WAV` → today's waveform switch, writing `phaseIncTarget` instead of
       `phaseInc`. The `osc_calc*FreqValue` helpers (lines 915–936) assign
       `phaseIncTarget`.
  3. **Slew** (runs after the cache check, every call, so it is never skipped
     by the cache):
     - if `slewSamplesLeft == 0`: `phaseInc = phaseIncTarget`, `phaseIncStep = 0`;
     - else: `phaseIncStep = ((int32_t)(phaseIncTarget − phaseInc)) / (int32_t)slewSamplesLeft`
       (one division per block per oscillator), and
       `slewSamplesLeft −= min(slewSamplesLeft, OUTPUT_DMA_SIZE)`.
     - When it reaches 0, the next call snaps to the target, so rounding
       never leaves a residue.
- **Block** (replaces the one at lines 945–948):

```c
/*
 * Update an oscillator's frequency state for the next block
 * (S084 P1 P1-3.8).
 *
 * What:       computes the phase-increment target for the oscillator's type
 *             (wav tables, pwm with warp-compensated table level, Q20.12
 *             samples), caches it by (frequency, waveform, type), and every
 *             block advances the `fre` slew: a per-sample step that reaches
 *             the target after OSC_SLEW_SAMPLES, or an immediate snap when no
 *             slew is armed.
 * Why:        plan §4.1.1 types, §4.1.6 `fre` (every oscillator glides);
 *             the cache must not skip the slew bookkeeping.
 * Inputs:     osc->freq, pitchMod, modNodeValue, oscType, waveform, source,
 *             slew state.
 * Outputs:    osc->phaseIncTarget, phaseInc, phaseIncStep, tableOffset,
 *             pwm parameters.
 * Accessors:  each engine's async update, once per block per oscillator.
 * Affiliates: osc_armSlew(), oscPwm_prepare(), oscSample_phaseIncForFreq().
 *             ITCM (INITCM) as today.
 */
```

- **New function** after it: `osc_armSlew()` (block P1-3.3) sets
  `slewSamplesLeft = OSC_SLEW_SAMPLES` and keeps the current `phaseInc` as
  the origin.

### P1-3.9 `Core/DSPAudio/modulationNode.c` — global maximum waveform

- **Where:** lines 76–84, `modNode_getMaxWaveformIndex()`.
- **Action:** Modify. Return `OSC_STOCK_WAVE_COUNT - 1u` (samples no longer
  extend the list).
- **Block:**

```c
/* S084 P1 P1-3.9: the `wav` list is the stock waves only; per-slot ranges
** for pwm/smp/slp come from InstrumentManager's slot-aware domain (P1-10.4).
** Used for direct ModulationNode (velocity) waveform targets. */
```

---

## 5. WP4 — `Core/DSPAudio/OscillatorSample.c/.h` (new)

### P1-4.1 `OscillatorSample.h` (new file)

The file header carries the project licence header (Brendan Clarke,
LXR02 Open-Source), then:

```c
/*
 * OscillatorSample — installed-sample playback for the smp/slp oscillator
 * types (S084 P1 P1-4.1).
 *
 * What:       Q20.12 phase (index = phase >> OSC_SAMPLE_PHASE_SHIFT) so every
 *             installable sample (<= 654,160 frames < 2^20) plays fully;
 *             one-shot (smp) holds at the end; looped (slp) wraps
 *             [sampleStart, sampleEnd) by compare-and-subtract; the start
 *             position is the oscillator's `wav` value 0..127 mapped onto the
 *             sample length; FM phase modulation as before at equal depth.
 * Why:        plan §4.1.5: the old `phase >> 17` index addressed only 32,768
 *             frames (0.74 s) and wrapped longer samples; loop becomes a
 *             per-oscillator choice; start position is sweepable.
 * Inputs:     OscInfo (oscType, source, waveform, phase, phaseInc,
 *             phaseIncStep, fmMod); the v1 sample catalogue (SampleMemory).
 * Outputs:    rendered blocks; OscInfo sample cache and bounds.
 * Accessors:  Oscillator.c dispatch, osc_setFreq(), OscSection.c (restart,
 *             sync reset), InstrumentManager.c writers.
 * Affiliates: SampleMemory.c (catalogue v1 in P1, v2 in P2); ITCM renderers.
 */
#define OSC_SAMPLE_PHASE_SHIFT 12u

void     oscSample_render(OscInfo* osc, int16_t* buf, uint8_t size, float gain);
void     oscSample_renderFm(OscInfo* osc, int16_t* modBuffer, int16_t* buf,
                            uint8_t size, float gain);
uint32_t oscSample_phaseIncForFreq(float f);
void     oscSample_bind(OscInfo* osc);
void     oscSample_restart(OscInfo* osc);
```

Each prototype gets its own block:

- **`oscSample_render` / `_renderFm`:** what, why, inputs and outputs as
  above.
  - **Constant cost:** two reads + interpolate per sample, the loop wrap
    always evaluated.
  - **FM:** the modulation offset is `(mod·fmMod) << 9` in Q20.12, equal
    depth to the old `<< 14` at `>> 17`.
- **`oscSample_phaseIncForFreq(f)`:**
  - returns `f · 2^22 / REAL_FS`: the same playback ratio as the old
    `freq2PhaseIncr32767()` (1,024·f/FS frames per sample), now with 12
    fraction bits;
  - accessor: `osc_setFreq()`.
- **`oscSample_bind(osc)`:**
  - refreshes the cached pointer and size for `source` when the catalogue
    generation, the source or the type changed;
  - computes `sampleStart = (waveform · size) >> 7` and `sampleEnd = size`;
  - clamps `source` to the installed count (0 samples → silent binding with
    size 0);
  - accessors: the IM writers (P1-10.2) and the renderers' first-block
    check.
  - *Replaces* `osc_updateSampleCache()`.
- **`oscSample_restart(osc)`:** `phase = sampleStart << 12`. Accessors: the
  engines' `rst`/`itp` reset (P1-8) and the `snc` slave reset (P1-7).

### P1-4.2 `OscillatorSample.c` (new file)

- **Includes:** `Oscillator.h`, `OscillatorSample.h`, `SampleMemory.h`,
  `config.h`.
- **Renderers** (`INITCM`):
  - `oscSample_render` from the removed `calcUserSampleOscBlock()`
    (Oscillator.c 813–865). Changes:
    - `>> 17` → `>> 12`; fraction `(phase & 0xFFF) * (1/4096)`;
    - `looped` from `oscType == OSC_TYPE_SLP` instead of the v1 flag;
    - the `% sampleSize` (lines 829, 852–856) → compare-and-subtract on
      `[sampleStart, sampleEnd)`;
    - the one-shot hold keeps `phase` at `(sampleEnd − 1) << 12`;
    - the per-sample slew add (P1-3.7).
  - `oscSample_renderFm` from the removed `calcUserSampleOscFmBlock()`, with
    the same changes and the `<< 9` modulation shift.
- **Interpolation guard:** the last frame interpolates toward `sampleStart`
  when looped, or toward itself when one-shot.
- **Comment blocks** as in P1-4.1, plus one line per renderer:

  ```
  Implementation note: re-homed from Oscillator.c calcUserSample*Block
  (S084 P1); Q20.12 and type-selected looping.
  ```

---

## 6. WP5 — `Core/DSPAudio/OscillatorPwm.c/.h` (new)

### P1-5.1 `OscillatorPwm.h` (new file)

```c
/*
 * OscillatorPwm — computed pulse-width families for the `pwm` oscillator
 * type (S084 P1 P1-5.1).
 *
 * What:       family = OscInfo.source: OSC_PWM_PRC (default square: difference
 *             of two band-limited falling saws w apart, DC-free, peak-scaled),
 *             OSC_PWM_PSW (default saw: sum of two saws d apart, scaled
 *             1/(1-d)), OSC_PWM_PTR (triangle through a two-slope phase
 *             warp), OSC_PWM_PQ3 / OSC_PWM_PW3 (303 square / saw through the
 *             warp; in P1 they warp recTable / sawTable, P2 swaps in the sq3 /
 *             sw3 tables), OSC_PWM_SWP (one control: sine -> tri -> saw ->
 *             square -> pulse, two aligned reads + crossfade, then the prc
 *             pulse narrowing 50 % -> 4 %). Control c = waveform/127.
 * Why:        plan §4.1.3: no width tables; prc/psw/swp exactly band-limited;
 *             warped families pick the table level for the warp's worst
 *             compression so narrow widths never alias.
 * Inputs:     OscInfo (source, waveform, phase, phaseInc, phaseIncStep, fmMod,
 *             tableOffset, pwm).
 * Outputs:    rendered blocks; OscPwmState per block.
 * Accessors:  Oscillator.c dispatch and osc_setFreq().
 * Affiliates: wavetable.c tables (sawTable, triTable, recTable, sine_table),
 *             tools/plots/plot_pwm.py (reference shapes), ITCM renderers.
 */
#define OSC_PWM_PRC 0u
#define OSC_PWM_PSW 1u
#define OSC_PWM_PTR 2u
#define OSC_PWM_PQ3 3u
#define OSC_PWM_PW3 4u
#define OSC_PWM_SWP 5u
#define OSC_PWM_FAMILY_COUNT 6u

/*
 * Per-block pwm parameters (S084 P1 P1-5.1).
 *
 * What:       shift = phase offset of the second read (prc/psw, uint32 of a
 *             cycle); scale = output gain (peak 1); warpA/warpB = the two
 *             warp slopes (Q32 multipliers, computed once per block, no
 *             per-sample division); split = warp split in phase units; mix =
 *             swp crossfade fraction; seg = swp segment 0..3.
 * Why:        width-dependent constants computed once per block keep the
 *             per-sample loop free of divisions (constant cost).
 * Inputs:     oscPwm_prepare().  Outputs: read by the pwm renderers.
 * Accessors:  OscillatorPwm.c; lives inside OscInfo.
 */
typedef struct {
    uint32_t shift;
    uint32_t split;
    float    scale;
    float    warpA;
    float    warpB;
    float    mix;
    uint8_t  seg;
} OscPwmState;

struct OscStruct;
void oscPwm_prepare(struct OscStruct* osc, float freqHz);
void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size, float gain);
void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer, int16_t* buf,
                     uint8_t size, float gain);
```

Each prototype gets a block:

- **`oscPwm_prepare`:** computes `phaseIncTarget` (1,024-point format) and
  the `tableOffset` for `f × compression`, where compression is
  `max(s0/w, (1−s0)/(1−w))` for warped families and 1 otherwise. It also
  computes the `OscPwmState` fields from `c`. Called from `osc_setFreq()`.
- **`oscPwm_render` / `_renderFm`:** about 30 instructions per sample (FM
  ≈ 45). The FM read offsets both table reads by `(mod·fmMod) << 19`, as
  `calcFmBlock`.

### P1-5.2 `OscillatorPwm.c` (new file)

**Mapping of the control `c` (0..1):**

| Family | Mapping |
|---|---|
| `prc` | `w = 0.5 − 0.46c`; `scale = 1/(2·max(w, 1−w))` |
| `psw` | `d = 0.5c`; `scale = 0.5/(1−d)` |
| `ptr` | `s0 = 0.5`, `w = 0.5 + (0.03 − 0.5)c` |
| `pq3` (P1) | `s0 = 0.5`, `w = 0.5 + (0.12 − 0.5)c` on `recTable` |
| `pw3` (P1) | `s0 = 0.5`, `w = 0.5 + (0.04 − 0.5)c` on `sawTable` |
| `swp` | `p = 4c`, segment and fraction; the segment-3 pulse width is `0.5 − 0.46·frac` |

**Phase offsets:** align the peaks in `swp`, as `plot_pwm.py`:

- sine +0;
- triangle +0.25 cycle;
- saw −0.25;
- square −0.25.

**Constant-cost reads:** every family reads exactly two table points (with
interpolation) per sample:

- prc/psw: two saws;
- the warps: one table plus the warp select;
- swp: two shapes.

**Tests:** add a host comparison against `tools/plots/plot_pwm.py` shapes
(P1-12.5).

**Comment block per function:** the What/Why/Inputs/Outputs above, plus the
implementation note "warp slopes from oscPwm_prepare(); no per-sample
division".

---

## 7. WP6 — `Core/DSPAudio/OscRenderPool.c/.h` (new)

### P1-6.1 `config.h` — pool size

- **Where:** lines 876–881 (the `OSC_WAVE_INTERP_MAX_ACTIVE` block).
- **Action:** Replace with the following.

```c
/*
 * Shared oscillator render pool (S084 P1 P1-6.1).
 *
 * What:       the number of extra oscillator renders allowed per 32-frame
 *             block. Run-mode `itp` crossfades claim first (ongoing, then new,
 *             lowest voice first; one unit per oscillator of the voice:
 *             Drum/Snare 2, Cymbal/HiHat 3), then waveform interpolation
 *             takes what remains (one unit per oscillator).
 * Why:        plan §4.1.6 / §6 r5 Q-N1: a bounded, always-budgeted pool keeps
 *             the worst case fixed (about 4 x 50 instructions per sample).
 *             Replaces OSC_WAVE_INTERP_MAX_ACTIVE (2).
 * Accessors:  OscRenderPool.c.
 */
#define OSC_RENDER_POOL_UNITS 4u
```

### P1-6.2 `OscRenderPool.h` (new file)

```c
/*
 * OscRenderPool — per-block budget for extra oscillator renders
 * (S084 P1 P1-6.2).
 *
 * What:       oscRenderPool_beginBlock() resets the budget at the top of each
 *             mixer block; oscRenderPool_claim(n) grants n units if they fit;
 *             the two scratch blocks hold an `itp` voice's old-section output
 *             and its old modulator block while it renders.
 * Why:        `itp` must outrank waveform interpolation and stay within a fixed
 *             worst case (plan §4.1.6).
 * Inputs:     claims from instrumentManager_claimOscSectionUnits() (itp) and
 *             modNode_updateValue() (waveform interpolation).
 * Outputs:    grants; scratch pointers valid for one engine render call.
 * Accessors:  mixer.c, InstrumentManager.c, modulationNode.c, OscSection.c.
 * Affiliates: OSC_RENDER_POOL_UNITS (config.h). 129 B SRAM1 (§0.4).
 */
void     oscRenderPool_beginBlock(void);
uint8_t  oscRenderPool_claim(uint8_t units);
int16_t *oscRenderPool_scratchSection(void);
int16_t *oscRenderPool_scratchMod(void);
```

### P1-6.3 `OscRenderPool.c` (new file)

- **Statics:**
  - `static uint8_t oscRenderPool_unitsUsed;`
  - `static int16_t oscRenderPool_scratchSectionBuf[OUTPUT_DMA_SIZE], oscRenderPool_scratchModBuf[OUTPUT_DMA_SIZE];`
- **Functions:** trivial bodies, each with the P1-6.2 block.
- **Note:** the scratch is shared sequentially. Engines render one slot at a
  time, and an `itp` render finishes before the next slot renders.

### P1-6.4 `Core/DSPAudio/modulationNode.c` — waveform interpolation claims from the pool

- **Where:**
  - line 68 (`static INCCMZ uint8_t modNode_waveInterpActiveCount = 0u;`);
  - line 438 (`modNode_waveInterpActiveCount = 0u;` in `modNode_resetTargets()`);
  - line 773 (`if (osc && modNode_waveInterpActiveCount < OSC_WAVE_INTERP_MAX_ACTIVE) {`);
  - line 795 (`modNode_waveInterpActiveCount++;`).
- **Action:**
  - Remove lines 68, 438 and 795.
  - Line 773 → `if (osc && osc->oscType == OSC_TYPE_WAV && oscRenderPool_claim(1u)) {`.
  - `#include "OscRenderPool.h"`.
  - Update the comment at lines 59–65 to:

```c
/* Oscillator waveform interpolation for one 32-frame DSP block
** (S084 P1 P1-6.4: budget moved to OscRenderPool).
**
** modNode_resetTargets() starts a new generation at the top of each mixer
** block. Eligible `wav`-type oscillator waveform targets receive a fractional
** base/next pair while OscRenderPool grants units; run-mode `itp` crossfades
** claimed theirs first, at the top of the block. Later targets snap to their
** integer waveform for that block. */
```

### P1-6.5 `Core/DSPAudio/mixer.c` — block-start pool reset and `itp` claims

- **Where:** line 866 `modNode_resetTargets();` (first statement of
  `mixer_calcNextSampleBlock()` at 863).
- **Action:** Add before it:

  ```c
  oscRenderPool_beginBlock();
  instrumentManager_claimOscSectionUnits();
  ```

  and `#include "OscRenderPool.h"`.

```c
	/*
	 * S084 P1 P1-6.5: reset the oscillator render pool and let run-mode `itp`
	 * crossfades claim first (ongoing, then pending, lowest slot first),
	 * before any waveform-interpolation claim made by the velocity re-apply
	 * below. Affiliates: OscRenderPool.c, instrumentManager_claimOscSectionUnits().
	 */
```

---

## 8. WP7 — `Core/DSPAudio/OscSection.c/.h` (new)

### P1-7.1 `OscSection.h` (new file)

```c
/*
 * OscSection — a voice's oscillator section: combination modes and run
 * modes (S084 P1 P1-7.1).
 *
 * What:       combination of a carrier and modulator(s): OSC_MOD_FM (phase
 *             modulation, today), OSC_MOD_MIX (today), OSC_MOD_SNC (hard sync:
 *             the modulator is the master, the main oscillator the slave,
 *             reset at each master wrap with the overshoot scaled), OSC_MOD_RNG
 *             (ring: carrier x modulator). Run modes: OSC_RUN_RST (today's
 *             trigger resets), OSC_RUN_FRE (no resets; every oscillator slews
 *             its pitch over OSC_SLEW_SAMPLES; the filter keeps its state and
 *             its cutoff/resonance slew), OSC_RUN_ITP (the whole section keeps
 *             rendering its pre-trigger state beside the reset one and the two
 *             crossfade over OSC_ITP_SAMPLES, using OscRenderPool units; with
 *             no free units that trigger behaves as `fre`; the filter keeps its
 *             state and slews).
 * Why:        plan §4.1.6 (run modes, per voice, all oscillators), §4.2
 *             (`snc`, `rng` on Drum).
 * Inputs:     engine oscillators, mode bytes, OscRenderPool grants.
 * Outputs:    the combined section block; slew/itp state.
 * Accessors:  DrumVoice.c, Snare.c, CymbalVoice.c, HiHat.c,
 *             InstrumentManager.c.
 * Affiliates: Oscillator.c (osc_armSlew, block renderers),
 *             OscillatorSample.c (sync/rst restarts), ResonantFilter.c
 *             (SVF_recalcFreqFrom()).
 */
#define OSC_RUN_RST 0u
#define OSC_RUN_FRE 1u
#define OSC_RUN_ITP 2u
#define OSC_RUN_COUNT 3u
#define OSC_MOD_FM  0u
#define OSC_MOD_MIX 1u
#define OSC_MOD_SNC 2u
#define OSC_MOD_RNG 3u
#define OSC_MOD_COUNT 4u
#define OSC_SECTION_MAX_OSCS 3u
#define OSC_SLEW_SAMPLES 132u
#define OSC_ITP_SAMPLES  132u
#define OSC_ITP_IDLE    0u
#define OSC_ITP_PENDING 1u
#define OSC_ITP_ACTIVE  2u

/*
 * Per-voice section state (S084 P1 P1-7.1).
 *
 * What:       runMode (descriptor row `run`), itpState/itpPos/itpUnits, the
 *             snapshot of the voice's oscillators taken at an `itp` trigger,
 *             and the filter slew (fSmooth, qSmooth, qTarget).
 * Why:        the crossfade must keep rendering the old oscillators; the
 *             filter slew needs its own smoothed values (plan §4.1.6, r4 Q4).
 * Accessors:  OscSection.c, the engines, InstrumentManager.c (writer, filter
 *             recalc, claims). Lives inside each engine struct.
 */
typedef struct {
    uint8_t runMode;
    uint8_t itpState;
    uint8_t itpUnits;
    uint8_t oscCount;
    uint16_t itpPos;
    OscInfo itpOld[OSC_SECTION_MAX_OSCS];
    float fSmooth;
    float qSmooth;
    float qTarget;
} OscSectionState;

void    oscSection_init(OscSectionState* s, uint8_t oscCount);
uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* const oscs[]);
void    oscSection_claimOngoing(OscSectionState* s);
void    oscSection_claimPending(OscSectionState* s, OscInfo* const oscs[]);
void    oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                              float amount, int16_t* buf, int16_t* modBuf,
                              uint8_t size);
void    oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                             float g1, float g2, float carGain,
                             int16_t* buf, int16_t* modBuf, int16_t* modBuf2,
                             uint8_t size);
void    oscSection_blendItp(OscSectionState* s, int16_t* buf,
                            const int16_t* oldBuf, uint8_t size);
void    oscSection_finishBlock(OscSectionState* s, uint8_t size);
void    oscSection_noteResoTarget(OscSectionState* s, float q);
void    oscSection_filterRecalc(OscSectionState* s, ResonantFilter* f);
```

**Prototype blocks** (each What, Why, Inputs, Outputs, Accessors,
Affiliates):

- **`oscSection_init`:** `runMode = RST`, idle, `oscCount`, filter slew
  copies `fSmooth`/`qSmooth` from the filter at first recalc. Accessor:
  engine `*_initVoice()`.
- **`oscSection_beginTrigger`:**
  - `RST` → returns 1 (the engine performs today's reset);
  - `FRE` → `osc_armSlew()` on every oscillator, returns 0 (no reset);
  - `ITP` → copies every oscillator into `itpOld[]` and zeroes each copy's
    `phaseIncStep` and `slewSamplesLeft` (the old section holds its pitch
    and does not keep ramping, because `osc_setFreq()` never runs on the
    copies). Sets `PENDING`, returns 1 (the engine resets the live
    oscillators).
  - Accessor: engine `*_triggerVoice()`.
- **`oscSection_claimOngoing` / `_claimPending`:** called per slot in voice
  order by `instrumentManager_claimOscSectionUnits()`.
  - `ACTIVE` re-claims its `oscCount` units. A grant always succeeds, because
    ongoing claims come first and every active total was granted before.
  - `PENDING` → `oscRenderPool_claim(oscCount)`: success → `ACTIVE`,
    `itpPos = 0`; failure → fallback to `fre`: each live oscillator's phase
    is restored from `itpOld`, `osc_armSlew()` runs, and the state goes
    `IDLE`.
- **`oscSection_renderDrum`:** the Drum combination.
  - `FM`/`MIX` are byte-identical to today's lines 314–326. The modulator
    renders with gain `amount`, and for `MIX` the carrier with
    `1 − amount`.
  - `SNC`: the master wrap positions are found per block by walking the
    master's phase with its increment and step (≈ 3 instructions per
    sample). The slave renders in segments between wraps through
    `calcNextOscSampleBlock()`. At each wrap the slave phase =
    `overshoot × (slaveInc / masterInc)`, using one division per block (for
    `smp`/`slp` slaves: `sampleStart << 12` plus the scaled overshoot). The
    master block, at gain `amount`, is added with saturation.
  - `RNG`: `buf = car·(1−amount) + (car·mod >> 15)·amount`.
  - Accessor: `Drum_calcVoiceSyncBlock()`.
- **`oscSection_renderFm2`:** Cymbal/HiHat. Byte-identical to today's
  `CymbalVoice.c` 241–247 / `HiHat.c` 258–264, with `carGain` 1.0 / 0.5.
- **`oscSection_blendItp`:**
  - `buf[i] = old + t·(new − old)`, with `t = (itpPos + i)/OSC_ITP_SAMPLES`
    clamped to 1;
  - it runs only while `ACTIVE`; the cost is inside the granted pool budget.
- **`oscSection_finishBlock`:** advances `itpPos` by `size`; at
  `OSC_ITP_SAMPLES` the state goes `IDLE` (the units free next block).
- **`oscSection_noteResoTarget`:** stores the resonance target written by
  `IM_SPECIAL_FILTER_RESO`.
- **`oscSection_filterRecalc`:** replaces `SVF_recalcFreq()` for voices.
  - `RST`: calls `SVF_recalcFreq(f)` exactly as today and copies
    `fSmooth = f->f`, `qSmooth = qTarget = f->q`. Sound class S0.
  - `FRE`/`ITP`:
    - `fSmooth += (f->f − fSmooth)·0.28` and `qSmooth += (qTarget − qSmooth)·0.28`
      (≈ 2 ms, three blocks);
    - `f->q = qSmooth`;
    - `SVF_recalcFreqFrom(f, fSmooth)`.
  - Accessor: `instrumentManager_recalcSlotFilter()`.

### P1-7.2 `OscSection.c` (new file)

- **Contents:** implements P1-7.1.
- **Code placement:** the sync segment walker and `oscSection_renderDrum`
  are `INITCM` (hot). The trigger, claims and filter recalc are flash.
- **The `FM`/`MIX` paths must call exactly what the engines call today**,
  with the same gains and order (golden S0 test, P1-12.5).
- **Includes:** `Oscillator.h`, `OscillatorSample.h`, `OscRenderPool.h`,
  `ResonantFilter.h`, `BufferTools.h`.

### P1-7.3 `Core/DSPAudio/ResonantFilter.c/.h` — `SVF_recalcFreqFrom()`

- **Where:** after `SVF_recalcFreq()` (ResonantFilter.c:100–106); declaration
  after `ResonantFilter.h:118` (`void SVF_recalcFreq(...)`).
- **Action:** Add.

```c
/*
 * Recompute the integrator gain from a given cutoff (S084 P1 P1-7.3).
 *
 * What:       g = fastTan(pi * fUsed) without changing filter->f.
 * Why:        run modes `fre`/`itp` slew the cutoff the loop uses while the
 *             writers keep setting the target in filter->f; SVF_recalcFreq()
 *             stays untouched for `rst` and for the Effect filter (twin rule).
 * Inputs:     filter, fUsed (0..0.45).  Outputs: filter->g.
 * Accessors:  oscSection_filterRecalc().
 * Affiliates: SVF_recalcFreq(), fastTan().
 */
void SVF_recalcFreqFrom(ResonantFilter* filter, float fUsed);
```

---

## 9. WP8 — Engines

All four engines get the same structure. Line numbers are per file.

### P1-8.1 Engine structs — add `OscSectionState section`

| File | Where (before the struct close) | Notes |
|---|---|---|
| `Drum/DrumVoice.h` | line 113 `Dither dither;` → add after it | `oscCount` 2; `mixOscs` (line 104) now holds `OSC_MOD_*` (0..3) |
| `Snare/Snare.h` | line 98 `uint8_t volumeMod;` → add after it | `oscCount` 2 (tonal + noise) |
| `Cymbal/CymbalVoice.h` | line 90 `SnapEg snapEg;` → add after it | `oscCount` 3 |
| `HiHat/HiHat.h` | line 96 `uint8_t isOpen;` → add after it | `oscCount` 3 |

Add `#include "OscSection.h"` in each header. Block (one per struct):

```c
	/*
	 * Oscillator section state (S084 P1 P1-8.1).
	 *
	 * What:       run mode, `itp` snapshot/crossfade and filter slew for this
	 *             voice's oscillators (OscSection.h).
	 * Why:        plan §4.1.6: run modes apply per voice to every oscillator.
	 * Inputs:     the `run` descriptor row (IM_SPECIAL_RUN_MODE); triggers.
	 * Outputs:    used by trigger, render and filter recalc.
	 * Accessors:  this engine's .c, InstrumentManager.c.
	 * Affiliates: OscSection.c, OscRenderPool.c. Inside the runtime-slot union.
	 */
	OscSectionState section;
```

### P1-8.2 Engine init

| File | Where | Action |
|---|---|---|
| `DrumVoice.c` | `Drum_initVoice()` 89–145; line 125 `voice->osc.waveform = TRI+seed_index;` | Modify line 125 to `TRI + (seed_index % 3u)`. Today slots 4/5 init to `CRASH` / sample 0, outside the stock list. Add `oscSection_init(&voice->section, 2u);` before line 145. |
| `Snare.c` | 64–110; before 110 | add `oscSection_init(&voice->section, 2u);` |
| `CymbalVoice.c` | 65–119; before 119 | add `oscSection_init(&voice->section, 3u);` |
| `HiHat.c` | 77–131; before 131 | add `oscSection_init(&voice->section, 3u);` |

```c
	/*
	 * Oscillator section init (S084 P1 P1-8.2).
	 *
	 * What:       run mode rst, no crossfade, oscCount set for this engine
	 *             (Drum 2, Snare 2, Cymbal 3, HiHat 3); the filter slew
	 *             starts from the filter's values at the first recalc.
	 * Why:        a fresh or reset slot must start in today's behaviour until
	 *             its `run` row is applied (plan §4.1.6).
	 * Inputs:     the engine instance.  Outputs: voice->section.
	 * Accessors:  *_initVoice() (via instrumentManager_resetRuntimeSlot*()).
	 * Affiliates: OscSection.c, IM_SPECIAL_RUN_MODE writer.
	 */
```

Drum line 125 gets its own note:
`/* S084 P1 P1-8.2: test seed limited to Tri/Saw/Rec; values above Cym are no longer legal `wav` values. */`

### P1-8.3 Engine trigger — run mode gates the resets

**`DrumVoice.c` `Drum_triggerVoice()` 147–218:**

- Lines 177–201 (the `//only reset phase…` block): wrap in
  `if (oscSection_beginTrigger(&voice->section, oscs)) { … }`, where
  `oscs = { &voice->osc, &voice->modOsc }`.
- Inside, for `OSC_TYPE_SMP`/`SLP` replace `voice->osc.phase = 0;` (line 200)
  with `oscSample_restart(&voice->osc);`.
- For `OSC_TYPE_PWM` use the wavetable branch `(0xff<<20)*offset`.
- `modOsc` reset stays as today (none for Drum).
- Line 217 `SVF_reset(&voice->filter);` → only when
  `voice->section.runMode == OSC_RUN_RST`.

```c
	/*
	 * S084 P1 P1-8.3: the run mode decides the trigger behaviour. rst = today
	 * (phase resets, filter cleared); fre = no reset (oscillators slew, filter
	 * keeps ringing); itp = snapshot then reset, crossfaded by the section
	 * (filter keeps ringing). Plan §4.1.6, §6 r4 Q3/Q4.
	 */
```

**`Snare.c` `Snare_triggerVoice()` 112–162:**

- Lines 140–150 (phase reset): wrap in
  `if (oscSection_beginTrigger(&voice->section, oscs))`, with
  `oscs = { &voice->osc, &voice->noiseOsc }`.
- Use `oscSample_restart` for sample types.
- Snare never clears its filter (unchanged).

**`CymbalVoice.c` `Cymbal_triggerVoice()` 121–173:**

- Lines 148–161 (the carrier reset plus `modOsc.phase = 0; modOsc2.phase = 0;`):
  wrap the same way, with `oscs = { &osc, &modOsc, &modOsc2 }`.

**`HiHat.c` `HiHat_triggerVoice()` 133–186:**

- Lines 162–172: wrap the same way, with `oscs = { &osc, &modOsc, &modOsc2 }`.
- The modulators are not reset (unchanged).

Every engine also applies sample restarts for modulators of type
`smp`/`slp` inside the gate, wherever today's code resets that oscillator.

### P1-8.4 Engine render — section and `itp`

**`DrumVoice.c` `Drum_calcVoiceSyncBlock()` 285–360:**

- Replace lines 313–326 (the `calcNextOscSampleBlock(&voice->modOsc …)` and
  the `if(voice->mixOscs) … else …` block) with:

  ```c
  oscSection_renderDrum(&voice->osc, &voice->modOsc, voice->mixOscs,
                        voice->fmModAmount, buf, modBuf, size);
  ```

- When `section.itpState == OSC_ITP_ACTIVE`, also render
  `oscSection_renderDrum(&s->itpOld[0], &s->itpOld[1], …)` into
  `oscRenderPool_scratchSection()`, using `oscRenderPool_scratchMod()` as
  its modulator block. Then call `oscSection_blendItp()` and
  `oscSection_finishBlock()`.
- The transient (line 329) keeps using `modBuf` after the section, as today.

**`Snare.c` `Snare_calcSyncBlockVoice()` 205–260:**

- Line 229 `calcNoiseBlock(&voice->noiseOsc,buf,size,0.9f);`: when the
  crossfade is active, render `itpOld[1]` noise into the scratch and blend
  before the filter (line 230).
- Line 237 `calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));`:
  same with `itpOld[0]` into the scratch, blended into `transBuf`.
- `oscSection_finishBlock()` at the end.

**`CymbalVoice.c` `Cymbal_calcSyncBlockVoice()` 213–270:**

- Replace lines 241–247 with `oscSection_renderFm2(&osc, &modOsc, &modOsc2,
  fmModAmount1, fmModAmount2, 1.0f, buf, mod, mod2, size)`.
- Add the `itp` render/blend.

**`HiHat.c` `HiHat_calcSyncBlockVoice()` 232–290:**

- Replace lines 258–264 the same way, with `carGain = 0.5f`.
- Add the `itp` render/blend.

```c
	/*
	 * S084 P1 P1-8.4: oscillator section (combination + run-mode crossfade).
	 * fm/mix render exactly as before (S0, golden test P1-12.5). While an itp
	 * crossfade is active the pre-trigger section also renders into the pool
	 * scratch and the two are blended over OSC_ITP_SAMPLES.
	 */
```

### P1-8.5 Engine async — no change

- `osc_setFreq()` already runs every block for every oscillator
  (`DrumVoice.c` 279–280, `Snare.c` 201–202, `CymbalVoice.c` 208–210,
  `HiHat.c` 227–229). The slew lives inside it (P1-3.8).
- The Snare noise oscillator is typed `wav` (`NOISE`). Its slew is the same
  code.

---

## 10. WP9 — Descriptor rows (four instruments)

### P1-9.1 Row macro for selectors (each `*Parameters.c`)

- **Where:** after each file's `ROW_MENU_SPECIAL` macro:
  - Drum ~line 79;
  - Snare, Cymbal, HiHat: the same position in their file.
- **Action:** Add.

```c
/*
 * Selector row: automatable, not Morphable, not an LFO/velocity target
 * (S084 P1 P1-9.1).
 *
 * What:       a byte selector with a special writer and no modulation domain.
 * Why:        plan §6 D16/Q30: typ/src/run are automatable but never Morphed
 *             or LFO-modulated (switching type mid-Morph would jump).
 * Accessors:  this file's descriptor table.
 * Affiliates: check_special_tags.py (macro name ends in _SPECIAL, so the tag
 *             is checked).
 */
#define FLAGS_SELECT INSTRUMENT_PARAM_FLAG_AUTOMATABLE
#define ROW_SELECT_SPECIAL(key_, cat_, long_, short_, member_, special_) \
    { key_, short_, long_, cat_, DTYPE_0B127, FLAGS_SELECT, MOD_NONE, \
      BIND_SPECIAL(member_, TYPE_UINT8, special_) }
```

- Add `#undef FLAGS_SELECT` / `#undef ROW_SELECT_SPECIAL` with the file's
  other `#undef`s (Drum lines ~281–292).

### P1-9.2 Waveform rows gain the `IM_SPECIAL_OSC_WAVE` tag

| File | Line | From | To |
|---|---|---|---|
| `DrumParameters.c` | 186 | `ROW_MENU("osc1_wave", …, osc.waveform, TYPE_UINT8)` | `ROW_MENU_SPECIAL(…, osc.waveform, TYPE_UINT8, IM_SPECIAL_OSC_WAVE \| IM_SPECIAL_OSC1)` |
| `DrumParameters.c` | 189 | `osc2_wave` … `modOsc.waveform` | same, `\| IM_SPECIAL_OSC2` |
| `SnareParameters.c` | 184 | `osc1_wave` | `\| IM_SPECIAL_OSC1` |
| `CymbalParameters.c` | 185, 188, 191 | `osc1_wave`, `osc2_wave`, `osc3_wave` | `\| OSC1`, `\| OSC2`, `\| OSC3` |
| `HiHatParameters.c` | 188, 191, 194 | same | same |

The `MOD_WAVE` domain stays (dynamic maximum, now per slot, P1-10.4).

### P1-9.3 New rows (appended at the end of each table; indices are stored, so append only)

**Drum** (`DrumParameters.c`):

- enum after line 171 `DRUM_PARAM_TRANSIENT_FREQ,`;
- rows before line 238 `};`.

```c
    DRUM_PARAM_OSC1_TYPE,
    DRUM_PARAM_OSC1_SOURCE,
    DRUM_PARAM_OSC2_TYPE,
    DRUM_PARAM_OSC2_SOURCE,
    DRUM_PARAM_RUN_MODE,
```

```c
    /*
     * Oscillator type/source and voice run mode (S084 P1 P1-9.3).
     *
     * What:       typ = OSC_TYPE_* per oscillator; src = pwm family or sample
     *             index; run = OSC_RUN_* for the whole voice.
     * Why:        plan §4.1.1, §4.1.6. Appended so existing indices, automation,
     *             LFO tokens, MIDI Learn and NRPN IDs are unchanged.
     * Inputs:     menu, automation, MIDI (menu edits), Kit/Instrument files
     *             (keys osc1_type, osc1_source, osc2_type, osc2_source,
     *             osc_run_mode; a missing key loads 0 = wav / 0 / rst).
     * Outputs:    the IM_SPECIAL_OSC_TYPE / _OSC_SOURCE / _RUN_MODE writers.
     * Accessors:  InstrumentManager.c, InstrumentCellFormat.c, menu.c.
     * Affiliates: drum_menu_pages[] page 0 and the FM page.
     */
    ROW_SELECT_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", osc.oscType, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
    ROW_SELECT_SPECIAL("osc1_source", "Oscilltr", "Source", "src", osc.source, IM_SPECIAL_OSC_SOURCE | IM_SPECIAL_OSC1),
    ROW_SELECT_SPECIAL("osc2_type", "FM", "Type", "typ", modOsc.oscType, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC2),
    ROW_SELECT_SPECIAL("osc2_source", "FM", "Source", "src", modOsc.source, IM_SPECIAL_OSC_SOURCE | IM_SPECIAL_OSC2),
    ROW_SELECT_SPECIAL("osc_run_mode", "Oscilltr", "RunMode", "run", section.runMode, IM_SPECIAL_RUN_MODE),
```

- `DrumParameters.h` line 14: `39u` → `44u`.

**Snare** (`SnareParameters.c`):

- enum after line 169: `SNARE_PARAM_OSC1_TYPE, SNARE_PARAM_OSC1_SOURCE, SNARE_PARAM_RUN_MODE`;
- rows before line 235 (`osc1_type`, `osc1_source`, `osc_run_mode`, category
  `"Oscilltr"`).
- `SnareParameters.h:14`: `38u` → `41u`.

**Cymbal** (`CymbalParameters.c`):

- enum after line 170: `OSC1_TYPE, OSC1_SOURCE, OSC2_TYPE, OSC2_SOURCE, OSC3_TYPE, OSC3_SOURCE, RUN_MODE`;
- rows before line 237. Categories:
  - `"Oscilltr"` for osc1 and run;
  - `"Mod Osc"` for osc2/osc3.
- `CymbalParameters.h:14`: `39u` → `46u`.

**HiHat** (`HiHatParameters.c`):

- the same seven, after line 173 / before line 240.
- `HiHatParameters.h:14`: `39u` → `46u`.

The `_Static_assert` at each file (`DRUM_PARAM_DESCRIPTOR_COUNT ==
DRUM_PARAM_COUNT`, etc.) enforces the counts.

### P1-9.4 Menu cells

**Drum:**

- **Page 0** (line 255 `{{ DRUM_PARAM_OSC1_PITCH_COARSE, DRUM_PARAM_OSC1_PITCH_FINE, DRUM_PARAM_OSC1_WAVE, INSTRUMENT_MENU_EMPTY, …`):
  cells 3–5 become `DRUM_PARAM_OSC1_TYPE, DRUM_PARAM_OSC1_SOURCE, DRUM_PARAM_RUN_MODE`.
  The first screen reads coa fin wav typ; the second screen src run.
- **FM page** (line 258 `{{ DRUM_PARAM_OSC2_MOD_AMOUNT, DRUM_PARAM_OSC2_PITCH_COARSE, DRUM_PARAM_OSC2_WAVE, DRUM_PARAM_OSC2_MOD_TYPE, …`):
  cells 4–5 become `DRUM_PARAM_OSC2_TYPE, DRUM_PARAM_OSC2_SOURCE`.

**Snare:** page 0 (line 250): the first free cells after the existing entries
take `SNARE_PARAM_OSC1_TYPE, SNARE_PARAM_OSC1_SOURCE, SNARE_PARAM_RUN_MODE`.

**Cymbal:**

- page 0 (line 251), cells 3–5: `OSC1_TYPE, OSC1_SOURCE, RUN_MODE`;
- Mod page (line 254), the first free cells:
  `OSC2_TYPE, OSC2_SOURCE, OSC3_TYPE, OSC3_SOURCE`.

**HiHat:** page 0 (line 256) and the Mod page (line 259), as Cymbal.

Comment block above each `*_menu_pages[]` table:

```c
/*
 * Oscillator page cells (S084 P1 P1-9.4).
 *
 * What:       typ, src and run join the oscillator page(s) as a second
 *             four-cell screen behind the same SELECT button; the modulator
 *             oscillators' typ/src sit on their own page.
 * Why:        plan §4.1.1: the new rows must be reachable from the voice
 *             pages without moving existing cells.
 * Inputs:     descriptor indices from this file's enum.
 * Outputs:    Menu cell layout.
 * Accessors:  menu.c VOICE pages (instrument_menu_page_t walk).
 * Affiliates: P1-9.3 rows, InstrumentCellFormat.c (their text).
 */
```

---

## 11. WP10 — InstrumentManager and `InstrumentCellFormat`

### P1-10.1 `InstrumentManager.h` — special writer IDs

- **Where:** lines 124–151 (enum), after line 149 `IM_SPECIAL_LFO_OFFSET,`
  and before `IM_SPECIAL_WRITER_COUNT`.
- **Action:** Add four IDs (20 + 4 = 24 ≤ 32; the `_Static_assert` at line
  159 stays valid).

```c
    /*
     * S084 P1 P1-10.1: oscillator type/source/waveform and voice run mode.
     * OSC_TYPE/OSC_SOURCE/OSC_WAVE carry an IM_SPECIAL_OSC_* selector; the
     * writers clamp per type, rebind sample state and refresh the slot's
     * waveform modulation domains. RUN_MODE writes the voice's
     * OscSectionState.runMode.
     */
    IM_SPECIAL_OSC_TYPE,
    IM_SPECIAL_OSC_SOURCE,
    IM_SPECIAL_OSC_WAVE,
    IM_SPECIAL_RUN_MODE,
```

### P1-10.2 `InstrumentManager.c` — writers

- **Where:** `instrumentManager_writeSpecialRuntime()`, before line 3385
  `case IM_SPECIAL_NONE:`.
- **Action:** Add four cases. Each uses
  `instrumentManager_oscBySelector(slot, special & IM_SPECIAL_OSC_MASK)` or
  `instrumentManager_oscSection(slot)`.

| Writer | Body |
|---|---|
| `OSC_TYPE` | `t = min(v, OSC_TYPE_COUNT−1)`; `osc->oscType = t`; `osc->waveform = min(osc->waveform, instrumentManager_oscWaveMax(osc))`; `osc->source = min(osc->source, instrumentManager_oscSourceMax(osc))`; `osc->freqCacheValid = 0`; if `smp`/`slp`: `oscSample_bind(osc)`; `instrumentManager_refreshWaveDomains(slot)` |
| `OSC_SOURCE` | `osc->source = min(v, instrumentManager_oscSourceMax(osc))`; `freqCacheValid = 0`; `smp`/`slp` → `oscSample_bind(osc)` |
| `OSC_WAVE` | `osc->waveform = min(v, instrumentManager_oscWaveMax(osc))`; `smp`/`slp` → `oscSample_bind(osc)` (start) |
| `RUN_MODE` | `instrumentManager_oscSection(slot)->runMode = min(v, OSC_RUN_COUNT−1)` |

Here `oscWaveMax` is `OSC_STOCK_WAVE_COUNT−1` for `wav` and 127 otherwise;
`oscSourceMax` is `OSC_PWM_FAMILY_COUNT−1` for `pwm`,
`max(0, numSamples−1)` for `smp`/`slp`, and 0 for `wav`. Block (one,
above the four cases):

```c
    /*
     * Oscillator type/source/waveform and run-mode writers (S084 P1 P1-10.2).
     *
     * What:       clamp each value to the oscillator's current type, keep the
     *             other two fields legal after a type change, invalidate the
     *             frequency cache, rebind sample state, and refresh any LFO
     *             adapter whose waveform domain depends on the type.
     * Why:        plan §4.1.1 / §5.1: `wav` means a stock wave, a pwm width, a
     *             wavetable position (P2) or a sample start, by type; sweeps
     *             must stay inside the selection.
     * Inputs:     slot, descriptor (special tag + selector), byte value.
     * Outputs:    OscInfo fields / OscSectionState.runMode; 1 = consumed.
     * Accessors:  instrumentManager_writeRuntime() (menu, automation, Morph
     *             base for the wave row, LFO adapters for the wave row, file
     *             apply, MIDI menu edits).
     * Affiliates: oscSample_bind(), instrumentManager_refreshWaveDomains(),
     *             InstrumentCellFormat.c (same max rules).
     */
```

- **New static helpers** (after `instrumentManager_oscBySelector()`, line
  ~2040), each with a block:
  - `instrumentManager_oscWaveMax()`;
  - `instrumentManager_oscSourceMax()`;
  - `instrumentManager_oscSection()`, a switch on the slot type returning
    `&voice->section`. Also expose it in the header for `InstrumentCellFormat.c`.

### P1-10.3 `InstrumentManager.c` — filter reso target and filter recalc via the section

- **Where:**
  - line 3263 `SVF_setReso(filter, byteValue / 127.0f);` in the
    `IM_SPECIAL_FILTER_RESO` case;
  - lines 1660–1691 `instrumentManager_recalcSlotFilter()`.
- **Action:**
  - **Line 3263:** add `oscSection_noteResoTarget(instrumentManager_oscSection(slot), filter->q);`
    after `SVF_setReso`.
  - **Recalc:** each case calls `oscSection_filterRecalc(&voice->section,
    &voice->filter)` instead of `SVF_recalcFreq(&voice->filter)` (lines
    1674, 1678, 1682, 1686).

```c
/*
 * Per-block filter coefficient update through the oscillator section
 * (S084 P1 P1-10.3).
 *
 * What:       rst: exactly SVF_recalcFreq() (S0). fre/itp: the cutoff and
 *             resonance the loop uses slew over about 2 ms toward the values
 *             the writers set, so a retrigger never steps a ringing filter.
 * Why:        plan §4.1.6 (r4 Q4 answer: yes, a fast parameter slew when the
 *             filter state is kept).
 * Inputs:     slot.  Outputs: filter->g (and q in fre/itp).
 * Accessors:  mixer_calcNextSampleBlock() step 4.
 * Affiliates: oscSection_filterRecalc(), SVF_recalcFreqFrom().
 */
```

### P1-10.4 `InstrumentManager.c` — per-slot waveform domain and adapter refresh

- **Where:**
  - lines 698–729 (`instrumentManager_descriptorModDomain()`): unchanged;
  - lines 2378–2381 in `instrumentManager_resolveModulationTarget()`;
  - new functions after line 729.
- **Action:**
  - Add `instrumentManager_descriptorModDomainForSlot(slot, descriptor,
    domain_out)`. It calls the existing function, then, if the row's special
    writer is `IM_SPECIAL_OSC_WAVE`, sets
    `domain.max_value = instrumentManager_oscWaveMax(osc)` for that slot's
    selected oscillator.
  - Lines 2378–2381 use `…ForSlot(target_slot, …)`.
  - Add `instrumentManager_refreshWaveDomains(slot)`: for every active
    `lfo_descriptor_targets[s][p]` whose `slot` equals the slot and whose
    descriptor is an OSC_WAVE row, recompute `adapter->domain` and clamp
    `base_value`. If the slot's velocity destination is an OSC_WAVE row,
    re-run its install through `instrumentManager_installVelocityModulationTarget()`
    (line 3066) with the slot's current token, so the cached range updates.

```c
/*
 * Waveform modulation domain for a slot's oscillator (S084 P1 P1-10.4).
 *
 * What:       the descriptor domain, with an OSC_WAVE row's maximum taken
 *             from the oscillator's current type (wav: stock count - 1;
 *             pwm/smp/slp: 127).
 * Why:        plan §5.1: LFO/velocity sweeps of `wav` must stay inside the
 *             selected type's range; the global maximum no longer fits.
 * Inputs:     slot, descriptor.  Outputs: domain_out; 1 when modulatable.
 * Accessors:  instrumentManager_resolveModulationTarget();
 *             instrumentManager_refreshWaveDomains().
 * Affiliates: IM_SPECIAL_OSC_TYPE writer (calls the refresh).
 */
```

### P1-10.5 `InstrumentManager.c` — `itp` claims for all slots

- **Where:** after `instrumentManager_dispatchRuntimeLfos()` (line 1552).
- **Action:** Add `void instrumentManager_claimOscSectionUnits(void)`:
  - **Loop 1**, slots 0..5: `oscSection_claimOngoing(section)`;
  - **Loop 2**, slots 0..5: `oscSection_claimPending(section, oscs)`, with
    the oscillator pointer array per type.
  - Declare it in the header.

```c
/*
 * Grant oscillator render-pool units to run-mode crossfades
 * (S084 P1 P1-10.5).
 *
 * What:       ongoing `itp` crossfades re-claim first, then new ones, lowest
 *             slot first; a new crossfade that does not fit falls back to
 *             `fre` for that trigger.
 * Why:        plan §4.1.6 priority: itp before waveform interpolation, lowest
 *             voice first; ongoing crossfades are never cut.
 * Inputs:     each slot's OscSectionState and oscillators.
 * Outputs:    OscRenderPool grants; itp states.
 * Accessors:  mixer_calcNextSampleBlock() before modNode_resetTargets().
 * Affiliates: OscSection.c, OscRenderPool.c.
 */
```

### P1-10.6 Diagnostic classifier

- **Where:** `instrumentManager_classifySpecialKey()` lines 3404–3459
  (`DEV_MODE_DIAGNOSTIC`).
- **Action:** Add, inside the `if (osc != 0xFFu)` block (after line 3428):
  - `strstr(key, "_type")` → `IM_SPECIAL_OSC_TYPE | osc`;
  - `"_source"` → `OSC_SOURCE | osc`;
  - `"_wave"` → `OSC_WAVE | osc`.
- Before line 3459: `if (strcmp(key, "osc_run_mode") == 0) return IM_SPECIAL_RUN_MODE;`
```c
    /*
     * Legacy-rule classification of the S084 rows (S084 P1 P1-10.6).
     *
     * What:       maps oscN_type / oscN_source / oscN_wave to
     *             IM_SPECIAL_OSC_TYPE / _OSC_SOURCE / _OSC_WAVE with the
     *             oscillator selector, and osc_run_mode to IM_SPECIAL_RUN_MODE.
     * Why:        the diagnostic self-check (the `s` digit on the FxBf boot
     *             screen, DEV_MODES.md) compares every row's compile-time tag
     *             with this classifier; new rows must be covered so the proof
     *             stays complete.
     * Inputs:     instrument type, file key.  Outputs: expected tag.
     * Accessors:  the DEV_MODE_DIAGNOSTIC tag self-check.
     * Affiliates: tools/dsp_test/check_special_tags.py (host twin, P1-12.2).
     */
```

### P1-10.7 `Core/DSP/Instruments/InstrumentCellFormat.h/.c` (new)

```c
/*
 * InstrumentCellFormat — type-dependent meaning of instrument cells
 * (S084 P1 P1-10.7).
 *
 * What:       for cells whose meaning depends on runtime state (osc type,
 *             osc source, osc waveform, run mode) returns a semantic
 *             description: a format kind, an index and the current maximum.
 *             Menu renders names from MenuText.h tables (decision O3).
 * Why:        plan §5.2: `wav` shows a stock wave, a pwm width or a sample
 *             start depending on type; `src` shows a pwm family or a sample;
 *             the 4-bit dtype field is full, so these rows are recognised by
 *             their special writer tag instead of a new dtype (the
 *             MENU_CELL_STATIC special-case precedent in menu.c).
 * Inputs:     slot, descriptor, stored value.
 * Outputs:    instrument_cell_format_t.
 * Accessors:  menu.c (cell value text, clamp, automation view, single-
 *             parameter view).
 * Affiliates: InstrumentManager.c max rules (shared helpers), MenuText.h
 *             oscTypeNames[], runModeNames[], pwmFamilyNames[], waveformNames[],
 *             SampleMemory display names.
 */
typedef enum {
    CELL_FMT_NONE = 0,       /* not a formatted row: Menu uses its dtype */
    CELL_FMT_OSC_TYPE,       /* index into oscTypeNames */
    CELL_FMT_RUN_MODE,       /* index into runModeNames */
    CELL_FMT_PWM_FAMILY,     /* index into pwmFamilyNames */
    CELL_FMT_SAMPLE,         /* sample index (short sNN, full display name) */
    CELL_FMT_NO_SOURCE,      /* `wav` type source: shown as "-" */
    CELL_FMT_STOCK_WAVE,     /* index into waveformNames */
    CELL_FMT_PWM_WIDTH,      /* 0..127 control, full view "width NN%" */
    CELL_FMT_SAMPLE_START    /* 0..127 control, full view "start NN%" */
} instrument_cell_format_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t index;
    uint8_t max;
} instrument_cell_format_t;

uint8_t instrumentCellFormat_describe(uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out);
```

- **Implementation:** switches on `descriptor->runtime.special &
  IM_SPECIAL_WRITER_MASK` (OSC_TYPE, OSC_SOURCE, OSC_WAVE, RUN_MODE). It uses
  `instrumentManager_oscBySelector` through a new exported accessor
  `instrumentManager_oscForDescriptor(slot, descriptor)`, declared in
  `InstrumentManager.h` with a contract block.
- **Return value:** 0 for every other row, so Menu falls through to its
  existing dtype paths.

---

## 12. WP11 — Menu and `MenuText.h`

### P1-11.1 `MenuText.h` — new tables, mix/fm names

- **Where:**
  - after line 100 (`lfoWaveNames`);
  - line 43–44 (`menuText_mix`, `menuText_fm`);
  - line 128–130 (`waveformNames`).
- **Action:**
  - Add `oscTypeNames {4, "wav","pwm","smp","slp"}`,
    `runModeNames {3, "rst","fre","itp"}`,
    `pwmFamilyNames {6, "prc","psw","ptr","pq3","pw3","swp"}`,
    `mixFmNames {4, "fm ","mix","snc","rng"}`.
  - Keep `menuText_mix/fm` only if referenced elsewhere after P1-11.3 (grep).
  - `waveformNames` is unchanged (6 entries).

```c
/* S084 P1 P1-11.1: oscillator type, run mode, pwm family and mod-mode
** names. Order and count must match OSC_TYPE_*, OSC_RUN_*, OSC_PWM_*,
** OSC_MOD_* (Oscillator.h, OscSection.h, OscillatorPwm.h). Rendered by menu.c
** from InstrumentCellFormat descriptions (decision O3). */
```

### P1-11.2 `menu.c` — formatted instrument cells (five sites)

All five sites use `instrumentCellFormat_describe(menu_activeVoice,
cell->descriptor, value, &f)`, or the automation slot (see below), and return
early when it reports a formatted row:

| Site | Where | Insert after | Behaviour |
|---|---|---|---|
| `va_formatValue3()` | 3291 | line 3301 (end of the play-mode special case) | 3-char text from the kind: table name, `sNN`, `-`, or `%3u` for widths/starts |
| `menu_formatCellValue3()` | 6287 | line 6300 (after the Effect branch) | same |
| `menu_clampCellValue()` | 6435 | line 6447 (after the play-mode clamp) | clamp to `f.max` |
| `menu_repaintGeneric()` | 13903 | line 14017, before `} else switch (dtype) {` (14018) | line 2: full text: wave or table names; sample display name (`sampleMemory_getDisplayName`) at `[1][0]` and the short name at `[1][13]`; "width NN%"/"start NN%" |
| Automation formatters | `menu_formatAutomationValue3()` 12934, `menu_automationValueMax()` 12896 | top of each | add a `slot` parameter (renamed `…Slot()` variants) and route formatted rows the same way |

- **Callers of the automation formatters:** 13030, 13041, 13520, 13787,
  13894. Pass the slot decoded from the target ID each site already holds
  (`instrumentParam` slot = id / 64); keep the old signatures as thin
  wrappers using `menu_activeVoice` for any caller without an ID.
- **Common block** (once, above a new static helper
  `menu_formatInstrumentCell3()` used by the sites):

```c
/*
 * Type-dependent instrument cell text (S084 P1 P1-11.2).
 *
 * What:       renders osc type / source / waveform and run-mode cells from
 *             InstrumentCellFormat's description; returns 0 for every other
 *             cell so the dtype paths run unchanged.
 * Why:        plan §5.2; the dtype nibble is full, so these rows are
 *             recognised by their special writer tag (precedent: the
 *             MENU_CELL_STATIC play-mode/step-special cases).
 * Inputs:     cell, value, slot.  Outputs: 3-char text; 1 if handled.
 * Accessors:  va_formatValue3(), menu_formatCellValue3(), the automation
 *             formatters; menu_clampCellValue() and menu_repaintGeneric() use
 *             the same description for max and the full line.
 * Affiliates: InstrumentCellFormat.c, MenuText.h tables.
 */
```

### P1-11.3 `menu.c` — `DTYPE_MIX_FM` becomes four options (Drum `mod` row)

| Where | From | To |
|---|---|---|
| 3327–3329 (`va_formatValue3`) | `memcpy(out, (value == 1u) ? menuText_mix : menuText_fm, 3);` | `memcpy(out, mixFmNames[1u + (value < 4u ? value : 0u)], 3);` |
| 6397–6400 (`menu_formatCellValue3`) | two-way | same table |
| 6655–6658 (`menu_clampCellValue`) | `DTYPE_MIX_FM` shares the `> 1` clamp | split out: `if (*value > 3u) *value = 3u;` |
| 12917–12918 (`menu_automationValueMax`) | `return 1u;` (shared with ON_OFF) | split: `DTYPE_MIX_FM` → `return 3u;` |
| 12958–12960 (`menu_formatAutomationValue3`) | two-way | table |
| 14028–14031 (`menu_repaintGeneric`) | two-way | table |

Comment block once, above `va_formatValue3()`'s `DTYPE_MIX_FM` case, with a
pointer line at the other five sites:

```c
    /*
     * Drum oscillator combination mode, four values (S084 P1 P1-11.3).
     *
     * What:       DTYPE_MIX_FM now shows and clamps fm / mix / snc / rng
     *             (values 0..3, OSC_MOD_*); 0 and 1 keep their stored meaning.
     * Why:        plan §4.2 / r4 Q5: sync and ring join the Drum `mod` row;
     *             Cymbal and HiHat have no such row and stay FM-only.
     * Inputs:     the stored byte.  Outputs: 3-char name, clamp 3, max 3.
     * Accessors:  va_formatValue3(), menu_formatCellValue3(),
     *             menu_clampCellValue(), menu_automationValueMax(),
     *             menu_formatAutomationValue3(), menu_repaintGeneric().
     * Affiliates: MenuText.h mixFmNames[], oscSection_renderDrum().
     */
```

### P1-11.4 `menu.c` — `MENU_WAVEFORM` loses the sample tail

| Where | Change |
|---|---|
| 11456 `getMaxEntriesForMenu()` | `return (uint8_t)waveformNames[0][0];` (no `+ menu_numSamples`) |
| 11489–11495 `getMenuItemNameForValue()` | the sample branch is removed; an out-of-range value prints `menuText_dash` |
| 12893–12894 comment, 12949 `menu_formatAutomationValue3()` | drop the `MENU_WAVEFORM` exception (`menuId == MENU_WAVEFORM ||`) |
| 14050–14058 `menu_repaintGeneric()` | the sample-name block is removed. Sample names now come from `CELL_FMT_SAMPLE` (`src`) in P1-11.2. |

`menu_numSamples` (line 1657) stays; it is still used by the formatter's
sample count, via `sampleMemory_getNumSamples()`.

Comment block once, at `getMaxEntriesForMenu()`:

```c
    /*
     * Waveform list without samples (S084 P1 P1-11.4).
     *
     * What:       MENU_WAVEFORM covers only the stock waves; sample choice and
     *             names moved to the oscillator `src` cell (types smp/slp).
     * Why:        plan §4.1.1: one byte no longer enumerates stock waves and
     *             samples together; the `wav` cell's meaning depends on type
     *             and is rendered through InstrumentCellFormat.
     * Inputs:     menu id, value.  Outputs: count / name.
     * Accessors:  getMaxEntriesForMenu(), getMenuItemNameForValue(),
     *             menu_formatAutomationValue3(), menu_repaintGeneric().
     * Affiliates: P1-11.2 (formatted cells), OSC_STOCK_WAVE_COUNT.
     */
```

---

## 13. WP12 — Build, proofs, decoder, host tests

### P1-12.1 `Makefile`

- **`DSP_SRCS`** (lines 144–169): add `Core/DSPAudio/OscillatorPwm.c`,
  `OscillatorSample.c`, `OscSection.c`, `OscRenderPool.c`. They are covered
  by the `Core/DSPAudio/%.o` `-Ofast` rule (line ~197).
- **`SRCS`** (line 111 area): add
  `Core/DSP/Instruments/InstrumentCellFormat.c` (control code, `-O2`).
- No new include paths (both folders are already on `-I`).
- Makefile comment above the added `DSP_SRCS` lines:

```make
# S084 P1 P1-12.1: oscillator types, pwm families, sample playback, the
# oscillator section (run modes, sync/ring) and the shared render pool.
# Render code: -Ofast through the Core/DSPAudio/%.o rule. InstrumentCellFormat
# (menu text semantics) is control code and stays in SRCS (-O2).
```

### P1-12.2 `tools/dsp_test/check_special_tags.py`

- **Where:** `expected()` lines 24–59.
- **Action:**
  - Inside `if osc:` (after line 40), add `_type` → `{IM_SPECIAL_OSC_TYPE,
    osc}`, `_source` → `{IM_SPECIAL_OSC_SOURCE, osc}`, `_wave` →
    `{IM_SPECIAL_OSC_WAVE, osc}`.
  - Add `'osc_run_mode': 'RUN_MODE'` to `simple` (line 41 dict).
  - Comment:
    `# S084 P1: oscillator type/source/wave and run-mode rows (P1-10.1).`
- **Expected result:** 155 + 22 new rows (Drum 5, Snare 3, Cymbal 7, HiHat 7)
  + 9 re-tagged wave rows, 0 mismatches.

### P1-12.3 `tools/decode_devlogs.py`

P1-0.2.

### P1-12.4 `tools/link_budget.py`

No change. Record ITCM/flash/BSS deltas in the P1 log against §0.4.

### P1-12.5 `tools/dsp_test` host tests (new targets)

| Target | Proves |
|---|---|
| `osc_s0` | Drum `fm`/`mix`, Cymbal/HiHat FM through `OscSection` are byte-identical to the frozen pre-P1 engine section renders (S0); `rst` filter recalc identical to `SVF_recalcFreq` |
| `osc_sample` | Q20.12 against the frozen `>>17` path on samples ≤ 32,768 frames: S1 (high SDR); a 2 s sample plays to its end; `slp` wraps `[start,end)`; start position correct |
| `osc_pwm` | `prc`/`psw`/`swp` shapes against `tools/plots/plot_pwm.py` reference arrays (exported once into `tools/dsp_test/frozen/`) |
| `osc_sync` | segment sync equals a per-sample reference sync implementation (reset positions, overshoot) |
| `osc_run` | `fre` slew reaches the target in 132 samples with no residue; `itp` blend endpoints; pool priority (ongoing > pending, lowest slot first; fallback to `fre`) |
| `lfo_oneshot` | one cycle then hold; delay = `phaseOffset/phaseInc` blocks; noise in 0..1 |
| `swap_level` | the mixer peak/declick helper: peak before the declick; declick decays to < 1 LSB within 2 ms; no added DC |
| `armcheck-osc` | `fpseq.py` instruction counts for the new loops (record; reference for P2/P3) |

- Add them to `tools/dsp_test/Makefile`.
- Each test file starts with a contract block naming the P1 change it
  proves.

---

## 14. WP13 — Specification updates (same change set)

| Document | Update |
|---|---|
| `INSTRUMENTS_DSP_REFERENCE.md` | §6.1 oscillator types, Q20.12, pwm, sync/ring, run modes, pool; §7.1 one-shot LFOs and the noise range; §10: fix the phase-reset pitfall (all engines reset the main oscillator); §4.4 the four new special writers |
| `MODULE_INTERCHANGE_SPEC.md` | new modules and calls: `OscSection`, `OscRenderPool`, `OscillatorPwm`, `OscillatorSample`, `InstrumentCellFormat`; mixer → InstrumentManager level/declick calls; the presetManager swap APIs |
| `BANK_PRESET_ARCHITECTURE.md` | the deferred-swap rules (quiet = real pre-fader level, 2 s bound, declick, Instrument load/preview through the swap, modulation detached while waiting) |
| `FILESYSTEM_SPEC.md` | the new instrument keys (`oscN_type`, `oscN_source`, `osc_run_mode`; `wav` meaning by type; samples no longer in `wav`) |
| `STORAGE_SRAM_MANIFEST.md` | §0.4 rows with measured sizes; ITCM delta |
| `DEV_MODES.md` | trace stage `v` |
| `MIDI_SPEC.md` | new target IDs exist by append (NRPN); no behaviour change |
| `OSC_INTERP_AUDIT.md` | the pool replaces `OSC_WAVE_INTERP_MAX_ACTIVE`; `wav` type only |

---

## 15. Verification and items to confirm during implementation

### 15.1 Confirm while coding

1. **Missing instrument keys load 0.** Read the descriptor-keyed parser path
   (`storageTypes.c` around lines 1100–1180, `file_key`) and the Scene image
   staging. If a missing key keeps a non-zero staged value, add an explicit
   0 default for the five/three/seven new keys.
2. **Runtime-slot sizes:** `sizeof(DrumVoice)` etc. after P1-8.1 against
   `INSTRUMENT_RUNTIME_SLOT_BYTES` (`InstrumentManager.c` ~168–185).
3. **LFO adapters write integers** (`instrumentManager_applyLfoDescriptorTarget()`
   2903–2910). LFO sweeps of `wav` therefore step in 1/127 increments for
   pwm/smp (fine) and never use waveform interpolation. Only velocity
   (direct node) targets interpolate; unchanged.
4. **Automation callers** at menu.c 13030/13041/13520/13787/13894 each
   decode the slot from the target ID they hold. Confirm the variable names.

### 15.2 Host

All §13 targets pass; `make -C tools/dsp_test special_tags` passes with 0
mismatches.

### 15.3 Build

- `make clean && make all && make img`: no warnings in touched files.
- `arm-none-eabi-size`, `link_budget.py`: deltas within §0.4 (report exact).

### 15.4 Hardware (user)

- **H1 — the worst-case Scene:** `cpu` and underruns against the WP0
  baseline.
- **H2 — LFO:** each one-shot wave fires once per trigger; `ofs` delays it;
  `rn1` holds; `rnd` behaves under neg/pos/bi; one-shot with retrigger `off`
  fires on Scene activation.
- **H3 — the swap.** Trace `v` shows reasons and peaks.
  - A ringing driven voice is not cut at Scene change. It swaps when below
    −72 dBFS, at its next trigger, or after 2 s, with no click.
  - Instrument-Load preview while the voice rings: scrolling never lags, and
    the swap follows the same rules.
- **H4 — types:**
  - `wav` list `Sin…Cym`;
  - `pwm` families sweep smoothly with an LFO on `wav`;
  - `smp`/`slp` play long samples fully; start position sweeps;
  - `src` shows sample names.
- **H5 — run modes:**
  - `rst` = today;
  - `fre`: no phase reset, audible 3 ms glides, filter keeps ringing without
    ticks;
  - `itp`: a smooth crossfade; with more than four oscillators crossfading,
    the extra triggers behave as `fre`.
- **H6 — Drum `snc`/`rng`:** the sync sweep with the pitch envelope; ring
  sounds metallic; `fm`/`mix` unchanged.
- **H7 — kits saved before P1:** they load. Samples on oscillators now read
  as `wav` and are clamped to `Cym` (no migration, by decision).

### 15.5 Close-out

- **P1 log:** append to this document (§16), then
  `knowledge_files/log_archive/084_SESSION_HANDOFF_LOG.md`.

---

## 16. Implementation log

**Implementation date:** 2026-10-25. Branch `dev-ph6-midi`; no commit made
(commits belong to the user). Every WP below is implemented in the working
tree; hardware checks H1-H7 remain.

### Build and host results (measured)

| Item | S084 P0 baseline | S084 P1 | Delta |
|---|---:|---:|---:|
| `text` | 609,128 | 624,464 | +15,336 |
| `data` | 424 | 428 | +4 |
| `bss` | 430,200 | 430,408 | +208 |
| Flash payload / headroom | 609,552 / 144,112 | 624,892 / 128,772 | +15,340 |
| ITCM | 4,168 | 10,872 | +6,704 |
| DTCM statics | 4,472 | 4,468 | -4 |

- `make all` and `make img`: no warnings in touched files (only the
  pre-existing `filesystem.c` unused-function and `EuklidGenerator`
  sign-compare warnings remain).
- `tools/dsp_test`: `selftest` identical; `filter` S1 PASS; `pack`,
  `postchain`, `mixer` 0 differing samples; `octave` unchanged (16
  sub-ulp edge cases); `special_tags` 177 rows, 0 mismatches.
- New tests: `lfo_oneshot`, `osc_pwm`, `osc_sample`, `osc_run`,
  `swap_level` all pass (0 failures).

### Deviations, decisions and fixes found while coding

1. **Runtime-slot reserve is now a fixed 1,176 B** (`InstrumentManager.c`).
   The union size was `2 x sizeof(DrumVoice)`; with the new `OscInfo` fields
   and `OscSectionState` that would have added about 5.8 KB of SRAM. The plan
   approves the growth *inside the existing reserve*, so the reserve is now
   the constant 1,176 B and the `_Static_assert`s prove all four engines fit
   (`sizeof(DrumVoice)` = 1,072 B). P2/P3 engine fields must fit the
   remaining 104 B or the ledger must be reopened.
2. **ITCM grew by 6,704 B, not the estimated 2.5-3.5 KB.** The ITCM renderers
   are the seven slew-enabled `wav` loops, both sample loops, the pwm
   renderers and `OscSection`. 10,872 / 16,384 B are used; P2's wavetable
   reader must be budgeted against the remaining 5,512 B.
3. **PWM phase domain:** a wavetable cycle is 2^32 phase units (1,024 points
   x 2^22), not 2^22. The warp, `split`, `shift` and `sweep` offsets use
   `1 / 2^32`; `pwm_read1024()` still uses the 22-bit index and its 22-bit
   fraction. The host `osc_pwm` test caught this.
4. **Sync master cycle:** every table oscillator (wav/pwm/sine/noise) wraps
   its full 32-bit phase; `smp/slp` uses `[start,end)` in Q20.12.
5. **`itp` renders need three buffers for Cymbal/HiHat** (new section, old
   section, and two modulator blocks). The old section uses the pool scratch
   for its output and first modulator, and the finished new path's second
   modulator static for its second; engines render one slot at a time.
6. **The declick decay is about 9 ms, not 2 ms.** With the specified 1 ms
   exponential (`R = 0.97758`), a full-scale offset falls below 1 LSB after
   about 9 ms; the plan's 2 ms figure is unattainable with this constant.
   `swap_level` proves the actual behaviour (11 ms gate). A linear 2 ms ramp
   would meet 2 ms but needs 24 B more SRAM, so it was not adopted.
7. **`osc2_mod_type` is excluded from the `_type` classifier rule.** It is
   the fm/mix/snc/rng row and has no special tag; without the exclusion the
   DEV tag self-check and `check_special_tags.py` would report it.
8. **`tools/dsp_test/extract.py`** now accepts multi-dimensional objects
   (`sawTable[11][1024]`), needed by the pwm test.
9. **`oscSample_bind()` runs every block** (cheap generation compare) so a
   sample reinstall refreshes the cached pointer, matching the retired
   `osc_updateSampleCache()`.
10. **Instrument swap and an in-progress Scene worker:** a commit for the
    same active Scene keeps the worker's remaining pending mask; only a
    different Scene starts a fresh mask.

### WP-by-WP notes

- **WP0:** `AUTOSAVE_TRACE_STAGE_VOICE_SWAP = 'v'`; decoder branch prints
  reason/path/slot/peak in LSB and dBFS.
- **WP1:** waves 8..17, one-shot state/delay, `rnd` 0..1, `sq1` inverted,
  Scene-activation fire, 18 names.
- **WP2:** quiet state (8 LSB x 8 blocks), 2,000 ms bound, mixer declick,
  Instrument preview on the same path, trigger force, trace `v`.
- **WP3-WP7:** type/slew fields, `osc_armSlew()`, `osc_freqToTableIndex()`,
  sample/pwm/pool/section modules; `SVF_recalcFreqFrom()`.
- **WP8:** all four engines on `OscSection`; `fm`/`mix` and the FM pair keep
  the old byte order (S0 by construction).
- **WP9:** rows appended (Drum 44, Snare 41, Cymbal/HiHat 46); wave rows
  re-tagged; pages updated.
- **WP10:** four writers, per-slot wave domain, adapter/velocity refresh,
  filter reso target + section recalc, `itp` claims, classifier.
- **WP11:** `InstrumentCellFormat` hook at the five sites, four-value
  `DTYPE_MIX_FM`, waveform list without samples.
- **WP12:** build, tag proof, five host tests (`osc_s0` is covered by the
  unchanged-code S0 argument and the existing `filter`/`mixer` proofs; an
  explicit frozen-engine golden would need a new frozen snapshot).
- **WP13:** specifications updated (instruments, module map, Bank/Preset,
  filesystem, SRAM manifest, DEV_MODES, OSC_INTERP_AUDIT, MIDI_SPEC).

### Still to confirm on hardware (H1-H7, §15.4)

- H1 worst-case Scene CPU/underruns against the WP0 baseline;
- H2 one-shot LFOs (delay, hold, polarity, Scene fire);
- H3 swap timing/level/clicks and preview scrolling;
- H4 `wav`/`pwm`/`smp`/`slp` listening and start sweeps;
- H5 `rst`/`fre`/`itp`;
- H6 Drum `snc`/`rng`;
- H7 old kits load (sample indices clamp to `Cym`).

### Items from §15.1 confirmed

1. Missing instrument keys load 0: `instrumentManager_resetSlot()` zeroes
   the staged slot before parsing (`filesystem.c` phase 11).
2. Runtime-slot sizes: fixed 1,176 B; `DrumVoice` 1,072 B; all four
   `_Static_assert`s pass.
3. LFO adapters still write integers: unchanged in P1.
4. Automation callers decode the slot from the target ID; updated callers
   now pass it to the slot-aware formatters.

---

## 17. Assessment of the implementation (review, 2026-10-25)

**Reviewer scope:** the working-tree diff against this schedule. Code read:

- the new modules `OscSection`, `OscRenderPool`, `OscillatorPwm`,
  `OscillatorSample`, `InstrumentCellFormat`;
- the diffs of `Oscillator.c/h`, `mixer.c`, `lfo.c/h`, `InstrumentManager.c`,
  `presetManager.c`, the four engines and their parameter tables, `menu.c`
  (formatter and clamp sites) and `MenuText.h`;
- the host tests.

No code was changed by the review.

**Verdict:** the DSP side is in good shape and matches the schedule. The
oscillator **selector rows do not work outside step automation**, so P1 is
**not ready for hardware checks H4–H7** until finding F1 (and F2, which
appears as soon as F1 is fixed) is resolved. H1–H3 can run now.

### 17.1 Verification repeated

| Check | Result |
|---|---|
| `make all` / `make img` | clean; no new warnings in P1 files |
| Sizes | `text` 624,464 / `data` 428 / `bss` 430,408; ITCM 10,872; flash headroom 128,772 — **match §16** |
| `tools/dsp_test` (all targets, incl. the 5 new tests) | all pass, 0 failures — **matches §16** |
| `_Static_assert`s | the runtime-slot fit (all four engines ≤ 1,176 B) and the descriptor counts hold |

### 17.2 Findings (most severe first)

#### F1 — BLOCKER: `typ` / `src` / `run` are never stored or applied (menu, MIDI, Kit/Scene/Instrument load)

- **Where:**
  - `Core/DSP/Instruments/*/…Parameters.c:100`: `FLAGS_SELECT` = AUTOMATABLE
    only (not MORPHABLE), bound as `INSTRUMENT_BIND_INSTANCE_OFFSET` with a
    special writer (`ROW_SELECT_SPECIAL`, e.g. `DrumParameters.c:273-277`).
  - `Core/Bank/Scene/Preset/presetManager.c:1112-1115`:
    `preset_setSupplementalParameterInternal()` returns 0 for **any**
    INSTANCE_OFFSET row.
  - `presetManager.c:1054-1057`: `preset_setInstrumentParameter()` returns 0
    for non-MORPHABLE rows.
  - `presetManager.c:1774` (`preset_applyKitVoiceSupplementalInternal`)
    skips MORPHABLE rows and then hits the rejection above. The Morph image
    apply (`presetMorph_applyVoiceNowInternal`) skips non-MORPHABLE rows.
- **Effect:**
  - `menu.c:5335-5343` routes the edit to the supplemental setter, which
    rejects it: the knob moves, nothing is stored, and the runtime does not
    change.
  - MIDI CC/NRPN to these rows: same path, same result.
  - Kit, Scene and Instrument loads parse the keys into
    `parameter_images`, but no apply path writes them to the runtime: every
    voice stays `wav`/`rst` after load.
  - Only step automation (the direct `instrumentManager_writeRuntime()`
    path) changes the type, and the next trigger after the lock releases
    reverts nothing because the stored byte was never updated.
  - The `InstrumentCellFormat` display follows the **runtime** type, so the
    `wav`/`src` cells keep formatting as `wav`.
- **Why the host tests miss it:** no test drives the preset/menu store path
  for the new rows.
- **Fix (proposed):**
  1. In `preset_setSupplementalParameterInternal()`, allow
     INSTANCE_OFFSET rows that carry a special writer (`runtime.special &
     IM_SPECIAL_WRITER_MASK` ∈ {OSC_TYPE, OSC_SOURCE, RUN_MODE}). The
     existing rejection exists for LFO/velocity target tokens; keep it for
     every other INSTANCE_OFFSET row.
  2. Make the voice apply write these selector rows **before** the Morph
     image (see F2). Two equal choices:
     - a selector pre-pass in `preset_resetAndApplyKitVoiceImage()` ahead of
       `presetMorph_applyVoiceNow()`;
     - a call at the top of `presetMorph_applyVoiceNowInternal()`.
  3. Add a host or DEV check: store `typ = pwm`, `wav = 100`; reload the Kit;
     expect runtime `oscType == PWM`, `waveform == 100`.

#### F2 — HIGH: the wave byte is clamped against the old type during apply

- **Where:**
  - `InstrumentManager.c:3849+` (the `IM_SPECIAL_OSC_WAVE` writer): clamps
    to `instrumentManager_oscWaveMax(osc)` (5 while the type is `wav`).
  - `InstrumentManager.c:3827-3829` (the `IM_SPECIAL_OSC_TYPE` writer):
    clamps the runtime `waveform` again when the type changes.
- **Effect (once F1 is fixed):** the wave rows are applied with the Morph
  image, before the supplemental selector rows. A stored `pwm` width 100 or
  sample start 80 is applied while the runtime type is still `wav`, becomes
  5, and stays 5 after `typ` lands. Every load of a pwm or sample sound
  loses its width or start.
- **The same loss at the knob:** switching `pwm` → `wav` → `pwm` truncates
  the width to 5. The stored byte keeps 100, so the runtime and the store
  diverge until the next load.
- **Fix:**
  - Apply the selectors first (F1 step 2).
  - In the type writer, re-derive the runtime wave from the **stored**
    wave byte (the Scene endpoint or interpolated value for that row),
    clamped to the new maximum, instead of clamping the current runtime
    value.
  - Do the same for `source`.

#### F3 — MEDIUM: `pwm_read1024()` effectively does not interpolate

- **Where:** `Core/DSPAudio/OscillatorPwm.c:41`, `frac = (phase &
  0x003FFFFF) * PWM_INV_CYCLE` with `PWM_INV_CYCLE` = 1/2^32.
- **Effect:**
  - The fraction spans only 0…1/1,024, so every pwm read is close to the
    lower table point: a stepped read.
  - Audible as extra aliasing and zipper on `prc`/`psw`/`ptr`/`pq3`/`pw3`
    at low pitches, wherever the per-sample increment is below one table
    point (≈ < 43 Hz on the top level, higher on decimated levels).
- **The log contradicts the code:** §16 deviation 3 says this function
  "still uses … its 22-bit fraction". It does not.
- **Why the test passes:** `test_osc_pwm.c:119` builds its reference with
  `pwm_read1024()` itself (self-referential). The schedule (§13, `osc_pwm`)
  asked for reference arrays exported from `tools/plots/plot_pwm.py`.
- **Fix:**
  - Use a separate constant `1/2^22` (`2.384185791015625e-07f`) for
    `pwm_read1024()`; keep `PWM_INV_CYCLE` for the warp/split/shift paths
    at lines 146-147.
  - Re-base the test on an independent double-precision table read, or on
    the exported `plot_pwm.py` arrays.

#### F4 — LOW/MEDIUM: velocity and LFO on resonance are inert in `fre`/`itp`

- **Where:** `OscSection.c:368-388`. `oscSection_filterRecalc()` sets
  `f->q = qSmooth`, which slews toward `qTarget`. `qTarget` is written only
  by `oscSection_noteResoTarget()` (the reso row writer, ~line 353) and by
  the `rst` branch.
- **Effect:** in `fre`/`itp`, modulation that writes `filter->q` (velocity
  node, LFO adapter) is overwritten on the next recalc, so modulated
  resonance does nothing. In `rst` it works as before.
- **Fix:** take the target from `f->q` as written by the modulation, before
  overwriting it. Keep the smoothed value in a separate field the SVF reads
  (e.g. `SVF_recalcFreqFrom(f, fSmooth, qSmooth)`) instead of writing it
  back to `f->q`.

#### F5 — LOW: `snc` with a sample master and `start > 0` drifts

- **Where:** `OscSection.c:198-234`. The shadow master phase restarts at
  `overshoot` (relative to 0), while a `smp`/`slp` master restarts at
  `start`.
- **Effect:** the shadow wraps `start` units late on every cycle after the
  first. The slave resets drift against the audible master.
- **Fix:** for sample masters, set the shadow `phase = start + overshoot`,
  with the overshoot taken modulo `(end − start)`.

#### F6 — LOW: an `itp` retrigger inside a running crossfade drops the old section

- **Where:** `OscSection.c:80`. `beginTrigger()` overwrites `itpOld[]` with
  the current live oscillators and restarts at position 0. The partly faded
  previous section is discarded.
- **Effect:** a step equal to the remaining old-section weight. This is
  inaudible below the ≈ 3 ms crossfade length at normal levels, but
  measurable on fast rolls.
- **Options:**
  - accept and document;
  - or snapshot the **blended** output state: keep the old section if it
    still has more than ½ weight.

#### F7 — LOW: a one-shot LFO selected in the menu with retrigger `off`/`scn` sits idle

- **Where:** `lfo.c`. The state stays `LFO_ONESHOT_IDLE` until
  `lfo_retriggerPhase()`, which runs only on a voice trigger or at Scene
  activation (`InstrumentManager.c:3225-3233`).
- **Effect:** after the wave row is changed to `si1`…`xt1`, the LFO holds the
  value at its current phase until the next Scene activation. This is a
  static offset on the target, which is confusing while editing.
- **Fix:** have the LFO wave writer call `lfo_retriggerPhase()` when the new
  wave is a one-shot and retrigger is `off`/`scn`.

#### F8 — NOTE: the `snc` mix differs between the plan and the schedule

- Plan §4.2: "`amt` mixes slave and master."
- Schedule §8 (WP7): "the master block, at gain `amount`, is added with
  saturation".
- The code (`OscSection.c:244-246`) follows the schedule. At high `amt` the
  sum of the slave (full scale) and the master can clip (+6 dB peak).
- **Decide by ear in H6:** keep it, or use `slave·(1−amt) + master·amt` as
  in `mix`.
- The division runs once per master wrap (not once per block as written).
  It is bounded and acceptable.

#### F9 — NOTE: `prc` polarity

The `prc` pulse is the inverse of the `Rec` stock wave (the high part
leads differently). It is harmless for sound. Flip the subtraction order if
`pwm` width 64 should match `Rec` exactly when A/B compared.

### 17.3 The implementer's §16 claims

| §16 item | Assessment |
|---|---|
| Sizes and test results | **Confirmed** (17.1) |
| Dev. 1: reserve fixed at 1,176 B, 104 B left | **Accepted.** It stays within the approved reserve and is the correct reading of the plan. P2 adds no engine state, but P2-E's shaper state (≤ 28 B) and any P3 per-engine field must be ledgered against the 104 B |
| Dev. 2: ITCM +6,704 B (5,512 B left) | **Accepted**, but over the estimate by ~2×. P2's `OscillatorWavetable` (+1–1.5 KB) fits. Re-measure before P2-E puts a drive-type loop in ITCM |
| Dev. 3: PWM phase domain | **Partly wrong:** the warp paths are right, but `pwm_read1024()` is not (F3) |
| Dev. 4: sync master cycle | **Confirmed**, except the sample-master start (F5) |
| Dev. 5: three buffers for Cymbal/HiHat `itp` | **Confirmed safe:** the live `mod2` static is reused only after the live render is finished, and nothing reads it later in the block |
| Dev. 6: declick ≈ 9 ms, not 2 ms | **Accepted.** The decay to inaudibility is well under 2 ms (−40 dB at ≈ 4.5 ms; most of the 9 ms is below −60 dB). Update plan §3.9 to say "1 ms exponential, ≈ 9 ms to 1 LSB" |
| Dev. 7: `osc2_mod_type` classifier exclusion | **Confirmed correct** |
| Dev. 9: `oscSample_bind()` every block | **Accepted** (a generation compare; constant cost) |
| Dev. 10: swap with an in-progress Scene worker | **Confirmed** |
| WP12: no frozen `osc_s0` golden | **Accepted for now.** `fm`/`mix` keep the old call order, and `filter`/`mixer` S0 hold. A frozen Drum-engine golden is still recommended before P2 touches `Oscillator.c` again |
| WP9: Drum 44 / Snare 41 / Cymbal 46 / HiHat 46 | **Confirmed** by the `_Static_assert` counts |

### 17.4 Reviewed and found correct

- **Engines:**
  - trigger gating via `oscSection_beginTrigger()`: `rst` resets as before,
    `fre` arms slews, `itp` snapshots;
  - Drum's EG/offset reset is gated the same way;
  - `SVF_reset` only in `rst`;
  - sample restart and pwm phase offset on reset;
  - `itp` render and blend in all four engines;
  - `finishBlock` at the end of each render.
- **Render pool:** claims ongoing crossfades first, then pending ones (lowest
  voice first), and falls back to `fre` with the phases restored.
- **LFO:**
  - waves 8–17 and `xtr`/`xt1`;
  - one-shot delay → run → hold at the end value;
  - `sq1` hold at 0;
  - `rn1` sampled at the fire;
  - the `rnd` fix `& 0x7FFF`;
  - the voice-trigger guard;
  - the Scene-activation fire before the handoff check.
- **Formatting:**
  - `InstrumentCellFormat` and the menu 3-char, full-line and clamp sites;
  - the name tables match their count macros (`oscTypeNames` 4,
    `runModeNames` 3, `pwmFamilyNames` 6, `waveformNames` 6,
    `lfoWaveNames` 18).
- **`mixer.c`:** the slot declick and level helpers.

### 17.5 Before hardware checks

1. Fix F1 and F2. Re-run all host tests and add the store/reload check from
   F1 step 3.
2. Fix F3 and the `osc_pwm` reference.
3. Optionally fix F4, F5 and F7 in the same pass (each is a few lines).
4. Then run H1–H7 as listed in §16. Add these hardware checks:
   - **H4a:** menu-edit `typ`/`src`/`run`, save the Kit, reboot, reload:
     the type, width/start and run mode survive.
   - **H4b:** a MIDI CC to `typ` changes the sound and the display.
   - **H5a:** velocity → resonance audible in `fre` and `itp` (after F4).
   - **H2a:** select a one-shot LFO wave with retrigger `off`: it fires
     (after F7).

---

## 18. Review fixes applied (2026-10-25)

**Scope:** the §17 findings, applied in the working tree.

- **Fixed:** F1, F2, F3, F4, F5, F7.
- **Accepted and documented in code:** F6.
- **Unchanged:** F8 and F9. They are listening decisions for H6/H4, not
  defects.
- No commit was made.

### 18.1 Changes by finding

#### F1 — selector rows are now stored and applied

The selector rows are `typ`, `src` and `run`.

- **`InstrumentManager.c/.h`:** new
  `instrumentManager_isOscSelectorDescriptor()`. It returns 1 for
  INSTANCE_OFFSET rows with MORPHABLE clear and a special writer of
  OSC_TYPE, OSC_SOURCE or RUN_MODE. The rows are identified by writer tag,
  never by key text.
- **`presetManager.c` `preset_setSupplementalParameterInternal()`:** selector
  rows are accepted before the general INSTANCE_OFFSET rejection.
  - The Normal cell is stored through `preset_storeInstrumentEndpoint()`, so
    dirty marking and Autosave work.
  - The Morph mirror and `morph_interpolation[]` are set to the same value,
    as `preset_storeSupplementalCell()` already does for target selectors.
  - The runtime is written for the active Scene, or with force.
  - The mirror matters for step automation:
    `seq_restoreAutomatedParameters()` writes `morph_interpolation[]` back
    when a lock releases. Without the mirror, an automated `typ` would have
    snapped to a stale value on the next trigger.
  - This one change fixes menu edits and MIDI CC/NRPN edits, since MIDI
    reaches the same commit through `menu_commitTargetEdit()`.
- **`presetManager.c`:** new `preset_applyOscSelectorRows()`. It copies each
  stored selector into its two mirrors and writes it to the runtime, in
  descriptor order. It is called:
  - in `preset_resetAndApplyKitVoiceImage()`, right after
    `instrumentManager_resetRuntimeSlotFromScene()` and before routing and
    the Morph image. That covers boot, Scene switch, the per-track played
    Scene, the staged Instrument commit and the deferred Instrument swap
    commit;
  - in the immediate Instrument apply, right after
    `instrumentManager_resetRuntimeSlot()`. That path rebuilds the Morph
    image through the background worker and never passes through
    `preset_resetAndApplyKitVoiceImage()`.
- **`preset_applyKitVoiceSupplementalInternal()`:** skips selector rows. The
  pre-pass owns them, and this loop also runs later (the rebind cursor, the
  forced per-track apply), where a rewrite would override a held automation
  value and repeat the type-change side effects.
- **Unchanged and already correct:**
  - Kit/Instrument save and load write and read `[params]` for
    non-morphable rows;
  - Autosave journals the Normal cell for any descriptor index;
  - Morph-endpoint-only loads (`preset_startKitMorphApply()`,
    `preset_startInstrumentMorphApply()`) never touch single-ended rows.

#### F2 — wave and source survive the apply order and type round trips

- **`Oscillator.h` `OscInfo`:** two new bytes, `waveRequest` and
  `sourceRequest`. They hold the last byte written to the `wav`/`src` rows,
  before the clamp. They sit in the existing padding between
  `waveInterpNext` (offset 80) and `waveInterpFrac` (offset 84):
  `sizeof(OscInfo)` stays 120 B and every engine struct keeps its size
  (measured, §18.2). **No new RAM.**
- **`InstrumentManager.c` writers:**
  - The OSC_WAVE and OSC_SOURCE writers record the request byte, then clamp
    only the live field.
  - The OSC_TYPE writer re-derives `waveform` and `source` from the request
    bytes against the new type's maximum. Before, it clamped the current
    live value, which was lossy.
  - A pwm width of 100 now survives `pwm` → `wav` → `pwm`.
- **`OscillatorSample.c` `oscSample_bind()`:** the index is
  `min(sourceRequest, count − 1)`. A reinstall that briefly has fewer samples
  no longer moves the selection for good. While the request is within the
  count, the behaviour is identical to before.
- **Ordering:** applying the selectors before the Morph image (F1) also makes
  the first rendered block after a load correct. The request bytes alone
  would only repair the value at the next type write.
- **Not tracked:** LFO modulation of the wave row writes `osc->waveform`
  directly every block (`modulationNode.c`) and re-applies on the next block.
  This is documented at the writer.

#### F3 — `pwm_read1024()` interpolates again

- **`OscillatorPwm.c`:** the fraction is now the low 22 bits × 1/2^22
  (`2.384185791015625e-07f`), the same constant as the stock wavetable reads
  in `Oscillator.c`. `PWM_INV_CYCLE` (1/2^32) stays for the warp, split,
  shift and sweep positions, where it is correct.
- The constant is written inline, so the host extract of the function
  carries the firmware value; the test has no copy that could drift.
- **`tools/dsp_test/test_osc_pwm.c`:**
  - new `refRead1024()`: an independent double-precision reference written
    from the table format. The correlation test now uses it instead of the
    firmware function.
  - new checks:
    - every 2^14 phase step across the saw level matches the reference
      within 1 LSB;
    - 28 midpoints between table points are interpolated.

#### F4 — velocity and LFO on resonance work in `fre`/`itp`

- **`OscSection.c` `oscSection_filterRecalc()`:**
  - In `fre`/`itp`, if `filter->q` differs from the value written back on the
    previous block (`qSmooth`), the new value becomes `qTarget` before the
    slew step.
  - This catches the velocity node and LFO adapters, which write
    `filter->q` through their pointer (TYPE_FLT) and not through the reso
    writer.
  - The exact compare is valid because the write-back is a plain copy.
  - Cost: one compare per slot per block. The ZDF loop is untouched, and
    `rst` is unchanged (S0).
  - `oscSection_noteResoTarget()` stays; its value is picked up by the same
    compare.
- **`tools/dsp_test/test_osc_run.c` + `Makefile`:** the `osc_run` target now
  extracts `oscSection_filterRecalc()` too, with SVF stubs. New checks:
  - a modulated q is followed and holds;
  - `rst` never touches q.

#### F5 — `snc` with a sample master and `start > 0`

- **`OscSection.c` `OSC_MOD_SNC`:** for `smp`/`slp` masters, the shadow phase
  starts at `mod->phase − (sampleStart << 12)`, clamped at 0. It now runs
  over the same `[0, cycle)` span that `oscSection_masterCycle()` returns.
  The overshoot was already relative, so only the start value needed the
  rebase. Table masters are unchanged (base 0).
- **No host test.** `oscSection_renderDrum()` calls the full oscillator
  renderers, and the existing harness does not extract them. Covered by H6.

#### F6 — accepted, documented at the code

`OscSection.c` `oscSection_beginTrigger()`, ITP case: the comment block
records the effect (a step on retriggers inside ~3 ms) and why it is kept.

- A clean result needs a third section: another `OscInfo` set per voice
  plus a third render.
- The 104 B runtime-slot margin and the constant-CPU budget do not allow
  that.
- Revisit only if H5 hears it.

#### F7 — a one-shot LFO no longer freezes when selected

- **`lfo.c` `lfo_calc()`, IDLE state:**
  - With retrigger `off` or `scn`, the LFO fires through
    `lfo_retriggerPhase()` on its first block.
  - With `v1`–`v6`, it waits at phase 0, its start value, instead of the old
    mid-cycle phase.
  - IDLE is exactly "just became a one-shot". It is the state after
    `lfo_init()`, and `lfo_retriggerPhase()` sets it for looping waves.
  - Scene activation still fires, as before.
  - The `lfo_init()` comment is updated to match.
- **`tools/dsp_test/test_lfo_oneshot.c`:**
  - `LFO_RETRIGGER_SCENE` is now defined in the test.
  - New checks:
    - `off`/`scn` fire when selected;
    - the fire starts at phase 0;
    - a voice retrigger waits idle at the start value, then fires on the
      trigger.

### 18.2 Measured

| Item | §16 (before fixes) | After fixes | Delta |
|---|---:|---:|---:|
| `text` | 624,464 | 625,144 | +680 |
| `data` | 428 | 428 | 0 |
| `bss` | 430,408 | 430,408 | 0 |
| Flash payload / headroom | 624,892 / 128,772 | 625,572 / 128,092 | +680 |
| ITCM | 10,872 | 10,856 | −16 |
| DTCM statics | 4,468 | 4,468 | 0 |
| `sizeof(OscInfo)` | 120 | 120 | 0 |
| `sizeof` Drum / Snare / Cymbal / HiHat voice | 1,072 / 904 / 1,008 / 1,016 | same | 0 |
| `sizeof(OscSectionState)` | 380 | 380 | 0 |

- **Struct sizes:** measured with a compile probe against the firmware
  headers. `waveInterpFrac` stays at offset 84.
- **RAM:** no new RAM in any region.
- **`make all` / `make img`:** build clean in the touched files. The only
  other compiler warning is a `-Waddress-of-packed-member` in
  `PatternData.c:201`, which no P1 change touches. The image was written
  (625,572 B).
  - Note: `build/LXRV2_lxr02.img` showed as deleted in the working tree at
    the start of the session; `make img` has recreated it.

### 18.3 Host tests

Full suite (`tools/dsp_test`, every target, after all fixes):

| Target | Result |
|---|---|
| `selftest` | int16 and float: 0 differing |
| `filter` | S1 acceptance PASS (0 failures); unchanged from §16 |
| `special_tags` | OK, 177 rows, 0 mismatches |
| `pack` | 0 differing bytes |
| `postchain` | 0 differing samples |
| `octave` | unchanged (16 sub-ulp edge cases, max 0.0004 cents) |
| `mixer` | 0 differing samples |
| `lfo_oneshot` | OK, 0 failures (incl. the new F7 checks) |
| `osc_pwm` | OK, 0 failures (incl. the new F3 checks) |
| `osc_sample` | OK, 0 failures (incl. the new F2 checks) |
| `osc_run` | OK, 0 failures (incl. the new F4 checks) |
| `swap_level` | OK, 0 failures |

**The new checks fail on the old code.** Each fix was reverted in place, one
at a time (from a scratch copy, then restored), and the matching target run:

| Fix reverted | Target | Result on old code |
|---|---|---|
| F3 (1/2^32 fraction) | `osc_pwm` | FAILED, 29 failures (max error 32,608 LSB at the table wrap; all 28 midpoints) |
| F4 (no q pick-up) | `osc_run` | FAILED, 2 failures |
| F7 (IDLE does nothing) | `lfo_oneshot` | FAILED, 4 failures |
| F2 (bind from `source`) | `osc_sample` | FAILED, 1 failure |

All targets pass again after the restore.

F1 lives in the Preset/Menu layer, which the host harness does not compile.
It is covered by H4a/H4b below.

### 18.4 Remaining before P1 closes

1. **Hardware H1–H7** (§16), plus the §17.5 additions:
   - **H4a:** menu-edit `typ`/`src`/`run`, save the Kit, reboot, reload: the
     type, width/start and run mode survive.
   - **H4b:** a MIDI CC to `typ` changes the sound and the display.
   - **H5a:** velocity → resonance is audible in `fre` and `itp`.
   - **H2a:** select a one-shot LFO wave with retrigger `off`: it fires at
     once.
   - **Also check:** step-automate `typ`, then let the lock release: the
     stored type returns on the next trigger.
2. **H6 decisions:**
   - the F8 `snc` mix law;
   - one observation from the F5 work: with a one-shot (`smp`) master that
     has reached its end and is holding, the shadow keeps wrapping at the
     sample-length period, so the slave keeps resetting against a silent
     master. Decide by ear whether a holding `smp` master should stop
     resetting the slave.
3. **F9:** `prc` polarity, optional.
