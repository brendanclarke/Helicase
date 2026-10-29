# Session 088 — Handoff Log

**Date:** 2026-10-29
**Branch:** `dev-ph7-voice_expansion`
**Base commit:** `2ab359eb` (S087 closeout)
**Tip at close:** `80029d31` (osc run modes implemented, needs test) +
uncommitted P1 changes (fader smoothing, float-through distortion)
**Build:** `text 676,264`, `data 420`, `bss 412,884`
**Flash headroom:** 77,400 B
**ITCM:** 12,656 / 16,384 B

---

## End of session block

```
DATE: 2026-10-29
SESSION GOAL: Implement oscillator run modes (rst/fre/itp) and fix two
  audio quality issues (fader zipper noise, retrigger HF puff)
COMPLETED:
  - P0: full oscillator run modes (rst/fre/itp) across all four engines
  - P1: fader zipper fix (block-rate one-pole smoothing in mixer)
  - P1: retrigger HF puff fix (float-through distortion in all post-chains)
VERIFIED ON HARDWARE: yes (user hardware listening)
  - P0: not yet hardware-tested (host tests pass, hardware matrix H1-H17 pending)
  - P1 fader smoothing: hardware-confirmed, user approved ("pretty much the
    same as the device noise floor now")
  - P1 HF puff: user closed as fixed ("can't detect it now")

CHANGES THIS SESSION:
  - Core/DSP/Instruments/Drum/DrumVoice.c: full run-mode trigger, render,
    FM depth ramp, itp crossfade, instant-attack ramp bypass fix
  - Core/DSP/Instruments/Drum/DrumVoice.h: runMode, ampRampArm,
    lastVeloGain, lastFmDepth, OscCarrierSnapshot snapshot
  - Core/DSP/Instruments/Drum/DrumParameters.c: osc1_run descriptor row
  - Core/DSP/Instruments/Drum/DrumParameters.h: descriptor count +1
  - Core/DSP/Instruments/Snare/Snare.c: same run-mode pattern as Drum
  - Core/DSP/Instruments/Snare/Snare.h: runMode, ampRampArm, lastAmpGain, snapshot
  - Core/DSP/Instruments/Snare/SnareParameters.c: osc1_run descriptor row
  - Core/DSP/Instruments/Snare/SnareParameters.h: descriptor count +1
  - Core/DSP/Instruments/Cymbal/CymbalVoice.c: same run-mode pattern + mod osc guards
  - Core/DSP/Instruments/Cymbal/CymbalVoice.h: runMode, ampRampArm, lastAmpGain, snapshot
  - Core/DSP/Instruments/Cymbal/CymbalParameters.c: osc1_run descriptor row
  - Core/DSP/Instruments/Cymbal/CymbalParameters.h: descriptor count +1
  - Core/DSP/Instruments/HiHat/HiHat.c: same run-mode pattern + mod osc guards
  - Core/DSP/Instruments/HiHat/HiHat.h: runMode, ampRampArm, lastAmpGain, snapshot
  - Core/DSP/Instruments/HiHat/HiHatParameters.c: osc1_run descriptor row
  - Core/DSP/Instruments/HiHat/HiHatParameters.h: descriptor count +1
  - Core/DSP/Instruments/InstrumentManager.c: IM_SPECIAL_OSC_RUN writer,
    classifier, filter slew dispatcher, itpFadeSlotAvailable,
    activeItpFadeCount, slotHasActiveItpFade
  - Core/DSP/Instruments/InstrumentManager.h: IM_SPECIAL_OSC_RUN enum,
    itpFadeSlotAvailable/activeItpFadeCount/slotHasActiveItpFade decls
  - Core/DSPAudio/Oscillator.h: OSC_RUN_RST/FRE/ITP/COUNT,
    OSC_RUN_ITP_SAMPLES, OSC_RUN_ITP_FADE_BLOCKS, OscCarrierSnapshot,
    oscSection_renderOldCarrier() decl
  - Core/DSPAudio/Oscillator.c: osc_itpScratch, oscSection_renderOldCarrier()
  - Core/DSPAudio/SlopeEg2.h: slopeEg2_retriggerFrom() decl,
    slopeEg2_isAudibleGain() inline
  - Core/DSPAudio/SlopeEg2.c: slopeEg2_retriggerFrom() body
  - Core/DSPAudio/ResonantFilter.h: SVF_recalcFreqSlew() decl,
    SVF_setFrequencyTarget() decl
  - Core/DSPAudio/ResonantFilter.c: SVF_recalcFreqSlew() body,
    SVF_setFrequencyTarget() body, OSC_RUN_FILTER_SLEW 0.28
  - Core/DSPAudio/voicePostChain.h: velocity ramp (Drum), amp ramps
    (Snare, Cymbal/HiHat), float-through distortion (all three)
  - Core/DSPAudio/distortion.h: distortion_curveSampleF() float-input variant
  - Core/DSPAudio/mixer.c: itp slot accounting, block-rate fader smoothing
    (mixer_fader_smooth[6], MIXER_FADER_SMOOTH_ALPHA 0.15)
  - Core/DSPAudio/modulationNode.c: modNode_setItpReserved(), reserved sum
    in modNode_resetTargets()
  - Core/DSPAudio/modulationNode.h: modNode_setItpReserved() decl
  - Core/Menu/menu.c: six DTYPE_ON_OFF sites (high-nibble tag split from
    DTYPE_0b1)
  - Core/Menu/MenuText.h: ONOFF_TAG_OSC_RUN, oscRunNames
  - tools/dsp_test/modules/run_modes.c: laws A1-A4, B1-B4
  - tools/dsp_test/modules/oscillator.c: itp group I1-I4
  - tools/dsp_test/modules/modulation.c: law L6
  - tools/dsp_test/modules/instrument_manager.c: laws 6-7, run row
  - tools/dsp_test/Makefile: run_modes target, break builds
  - tools/dsp_test/check_special_tags.py: osc1_run rule
  - knowledge_files/specification_reference/* (multiple spec updates)

KNOWN ISSUES INTRODUCED: none
KNOWN ISSUES RESOLVED:
  - Fader zipper artifact (fixed: block-rate one-pole smoothing)
  - Retrigger HF puff (closed/fixed: float-through distortion)

NEXT SESSION RECOMMENDED GOAL: hardware testing of run modes (H1-H17 matrix
  from S088_P0_OSC_RUN_MODES.md §10)
BLOCKERS: hardware acceptance of all three run modes before further DSP work

CRITICAL REMINDERS FOR NEXT SESSION:
  - ampRampArm arms ONE block only — never pass unequal ramp endpoints when clear
  - rst must remain bit-identical to pre-S088; host law B1 proves this
  - SVF_recalcFreqSlew() slews only g (cutoff coefficient); if resonance steps
    are audible in fre/itp, extend it to step q
  - The Drum instant-attack shortcut is guarded by !ampRampArm; do not remove
  - itp crossfade shared scratch is osc_itpScratch[OUTPUT_DMA_SIZE] in
    Oscillator.c (64 B SRAM1); all four engines use it through the helper
  - The two interpolation slots are shared between itp fades and wtb blending;
    ongoing itp fades have priority
  - mixer_fader_smooth[] runs at block rate (1,378 Hz); alpha=0.15 gives
    tau~4.5ms, fc~35 Hz
```

---

## 1. Session 088 P0 — Oscillator Run Modes

### 1.1 Summary

Three oscillator run modes (`rst`/`fre`/`itp`) implemented across all four
engine types (Drum, Snare, Cymbal, HiHat). The feature adds a parameter
(`osc1_run`, display name `run`) to each instrument's osc page, selectable
via the existing `DTYPE_ON_OFF` widget with a high-nibble tag mechanism
(`ONOFF_TAG_OSC_RUN`).

Implementation followed a four-layer plan in `S088_P0_IMPLEMENTATION.md`:
1. **Layer 1** — parameter, menu, storage (DTYPE_ON_OFF tag, descriptor rows,
   InstrumentManager writer, classifier)
2. **Layer 2** — `fre` mode (gain-matched retrigger, post-chain ramps, FM
   depth ramp, filter coefficient slew, phase continuity)
3. **Layer 3** — `itp` mode (OscCarrierSnapshot, 132-sample crossfade,
   slot accounting, claim/refuse logic)
4. **Layer 4** — tests, specifications

### 1.2 The three modes

| Mode | Constant | Carrier phase | Amp EG | Filter | Modulators |
|------|----------|--------------|--------|--------|------------|
| `rst` (0) | `OSC_RUN_RST` | Reset | `slopeEg2_trigger()` | Clear + recalc | Reset |
| `fre` (1) | `OSC_RUN_FRE` | Continues | `slopeEg2_retriggerFrom()` | Keeps ringing, `SVF_recalcFreqSlew()` | Restart, FM depth ramp |
| `itp` (2) | `OSC_RUN_ITP` | Reset + crossfade old | `slopeEg2_retriggerFrom()` | Keeps ringing, `SVF_recalcFreqSlew()` | Restart, FM depth ramp |

`rst` is bit-identical to the pre-S088 tree (host law B1/B4 and the full
`run_modes` test suite prove this).

### 1.3 Gain-matched retrigger

`slopeEg2_retriggerFrom()` (`SlopeEg2.c`) inverts the attack slope curve:

```
v = g / (1 + k*(1-g))
```

where `g` is the current gain and `k` is `invSlope` (the attack shape
parameter). The function sets the raw envelope value to `v` so that
`slopeEg2_calc()` returns `g` on its first call — the attack starts from
the current gain, not from zero. Repeat mode falls back to
`slopeEg2_trigger()`.

The audible threshold is `g >= 1/32768` (`slopeEg2_isAudibleGain()`). Below
it, the retrigger is a fresh start from silence, identical to `rst`.

### 1.4 Post-chain gain ramps

The armed retrigger block (`ampRampArm = 1`, one block only) passes the
previous block's gain as `lastGain` / `lastVeloGain` to the post-chain. The
per-sample ramp interpolates from old to new across 32 samples (0.73 ms).

- **Drum:** velocity ramp (`lastVeloGain`) and amp ramp (through the
  combined `gain` parameter). Cost: 32 → 35 instructions/sample.
- **Snare/Cymbal/HiHat:** amp ramp (`lastAmpGain`). Cost: 30 → 35 (Snare),
  25 → 30 (Cymbal/HiHat).

When not armed, `last == current`, and the ramp is the identity (zero cost
over the pre-S088 path; `rst` is bit-identical).

### 1.5 FM depth ramp (Drum only)

The Drum `fm` branch multiplies each modulator sample by a linear ratio
ramping from `lastFmDepth / fmMod` to 1.0 over the block. This smooths
the FM depth step on a fre/itp retrigger. Always runs (constant CPU, D1);
when not armed the ratio starts at 1.0 (identity). Measured +10
instructions/sample; the Drum `fm` ceiling is ≈ 102 (budget 107, G1).

### 1.6 Filter coefficient slew

In `fre`/`itp`, the per-block filter recalculation uses
`SVF_recalcFreqSlew()` instead of `SVF_recalcFreq()`. This steps the `g`
coefficient toward the target by a factor of `OSC_RUN_FILTER_SLEW` (0.28)
per block, preventing the hard coefficient jump that would ring the filter.
Only `g` (cutoff) is slewed; resonance `q` changes instantly. `rst` keeps
the original `SVF_recalcFreq()`.

The cutoff writer also changed: `SVF_setFrequencyTarget()` sets `f` without
jumping `g`, so control-rate writes don't defeat the slew.

### 1.7 `itp` crossfade

When an `itp` retrigger has an audible amp gain and a crossfade slot is
available:

1. **Snapshot:** the trigger fills `OscCarrierSnapshot` (28 B) from the
   current carrier state (phase, phaseInc, waveform, tableOffset, oscType,
   sample cache fields). The snapshot is taken BEFORE the phase reset.
2. **Crossfade:** `oscSection_renderOldCarrier()` renders the old carrier
   into `osc_itpScratch[32]` (shared 64 B SRAM1), blends with the live
   carrier output using a linear `t` ramp from 0 to 1 over 132 samples
   (≈ 3 ms, 5 blocks). The old carrier is rendered PLAIN: no FM, sync or
   ring on the old copy (S085 P1 Q14).
3. **Slot accounting:** `instrumentManager_activeItpFadeCount()` counts
   active fades before `modNode_resetTargets()` each block.
   `modNode_setItpReserved()` reserves those slots. Wavetable blending
   steps (3 ms) when both slots are held by itp fades.
4. **Refused claim:** if both interpolation slots are held, `itp` falls
   back to `fre` (no crossfade, no phase reset, gain-matched retrigger
   only). `instrumentManager_itpFadeSlotAvailable(exclude_slot)` excludes
   the requesting voice's own slot from the count.
5. **Decayed itp:** when the amp is fully decayed, `itp` behaves exactly
   as `rst` (D6/A3 — no snapshot, no crossfade, phase reset, filter clear).

### 1.8 DTYPE_ON_OFF high-nibble tag (D12)

All 16 `DTYPE_MENU` IDs are in use (S078). The `osc1_run` parameter reuses
`DTYPE_ON_OFF` with a high-nibble tag (`ONOFF_TAG_OSC_RUN = 1`). The six
`menu.c` sites that handle `DTYPE_ON_OFF` check `(dtype >> 4) != 0` before
branching to the tagged path. Future tagged ON_OFF parameters use tag
values 2, 3, etc.

### 1.9 `behavesAsRst` unification

A single boolean `behavesAsRst` gates filter clear, ring snap, and
modulator reset in each engine trigger. It covers both literal `rst` and
the decayed-`itp` case (D6/A3), avoiding four separate `runMode ==`
comparisons.

### 1.10 Drum instant-attack ramp bypass fix

Found during hardware listening (H7-equivalent). At amp attack 0 (knob 0,
internal `attack == 1.0`), all three modes clicked identically because the
Drum render's instant-attack shortcut set `lastGain = ampFilterInput = 1.0`,
discarding the previous block's gain. The fix adds `!voice->ampRampArm` to
the shortcut condition so it is bypassed on the one armed block, letting the
post-chain ramp smoothly from the old gain. Snare/Cymbal/HiHat do not have
this shortcut and were unaffected.

### 1.11 Build metrics

| Metric | Pre-S088 | Post-P0 | Delta |
|--------|----------|---------|-------|
| text | 670,628 | 676,344 | +5,716 |
| bss | 412,392 | 412,892 | +500 |
| DrumVoice | 596 B | 632 B | +36 |
| SnareVoice | ~480 B | 452 B | (measured) |
| CymbalVoice | ~520 B | 508 B | (measured) |
| HiHatVoice | ~530 B | 516 B | (measured) |
| runtime_slots | 7,152 B | 7,584 B | +432 |
| osc_itpScratch | 0 | 64 B | +64 |
| ITCM | 12,640 | 12,656 | +16 |
| DTCM | 4,344 | 4,345 | +1 (modNode_itpReserved) |

### 1.12 Host test results

| Test | Result |
|------|--------|
| `run_modes` (A1-A4, B1-B4) | 0 failures; gain-match break caught |
| `oscillator` itp group (I1-I4) | 0 failures |
| `modulation` (incl. L6) | 0 failures |
| `instrument_manager` (incl. laws 6-7) | 0 failures; on/off-tag break caught |
| `check_special_tags.py` | OK (167 rows, 0 mismatches) |
| All other existing tests | 0 failures |

### 1.13 Post-chain measured costs (ARM, `elf_hotloops.py`)

| Loop | Pre-S088 | S088 | Budget |
|------|----------|------|--------|
| Drum post-chain | 32 | 35 | 35 (G4) |
| Snare post-chain | 30 | 35 | 35 (G4) |
| Cymbal/HiHat post-chain | 25 | 30 | 30 (G4) |
| Drum FM depth ramp | — | 10 | +15 planned, ~107 total (G1) |
| itp crossfade blend | — | 15 | 23 budgeted (G5) |

### 1.14 Hardware testing pending

The full hardware matrix H1–H17 from `S088_P0_OSC_RUN_MODES.md` §10:
- H1: `rst` A/B (bit-identical to pre-S088 on real hardware)
- H7/H8: `itp` audible / decayed retrigger
- H11: carrier-only `itp` on Drum `fm`
- H15: step automation of `run`
- H16: worst-case CPU with all six voices in `itp`, both fade slots held
- G1: Drum `fm` ceiling with depth ramp (hardware stress confirms)
- G2: resonance step in `fre`/`itp` (if audible, extend
  `SVF_recalcFreqSlew()` to step `q`)

### 1.15 Implementation deviations recorded

**Layer 1:**
1. `runMode` lives on the engine struct, not in `OscInfo`.
2. `IM_SPECIAL_OSC_RUN` is ordinal 21 (22nd tag, 0-based).
3. The special-tag classifier learned `osc1_run`.
4. `ROW_SELECT_ONOFF_SPECIAL` rows are one physical line for
   `check_special_tags.py`.
5. The descriptor offset is into the engine struct, not `OscInfo`.

**Layer 2:**
1. `ampRampArm` gates every ramp endpoint (not unconditional).
2. `behavesAsRst` instead of `runMode == OSC_RUN_RST`.
3. Drum FM ramp runs in `itp` too (no flat under crossfade).
4. `inv_size` guards `size == 1` in all three post-chains.
5. Centralized filter slew in `instrumentManager_recalcSlotFilter()`.
6. Inversion covers the full slope knob range (no clamp needed).

**Layer 3:**
1. `OscCarrierSnapshot` is 28 B ARM / 32 B host (asserted <= 32).
2. One shared 64 B scratch, not one per engine.
3. The helper owns progress, counter and release.
4. `modNode_setItpReserved()` writes a separate `modNode_itpReserved`.
5. `itpFadeSlotAvailable(exclude_slot)` excludes the requesting voice.
6. Phase guard adds the refused-fade exception.
7. Snapshot taken before `osc_setBaseNote()` and phase reset.

---

## 2. Session 088 P1 — Fader Zipper Fix

### 2.1 Symptom

A "zipper" staircase noise when moving hardware slider faders (RV5–RV10)
while a voice is playing. Audible through the mixer's per-sample gain ramp
which faithfully reproduced each gain step.

### 2.2 Root cause

`slider_vol[]` updates once per main-loop iteration (≈ 460 Hz at measured
main-loop rate). Each update is a non-interpolated LUT lookup from the ADC
DMA buffer. The gain steps between consecutive reads are large enough to
hear at mid-taper positions.

### 2.3 Failed approaches

Three approaches were tried and rejected before the solution:

1. **LUT doubling** (1,024 → 2,048 entries): the artifact was not a LUT
   quantization issue. Halving the node spacing did not reduce the zipper
   because the steps came from the update rate, not the resolution.
2. **EMA on `slider_vol[]`** in `adc_checkPots()`: same update rate as the
   main loop. The filter ran at the same cadence as the steps it was trying
   to smooth.
3. **Mixer int16 truncation removal** (the `* 256.0f` direct float-to-24-bit
   conversion): this was a real precision improvement and was kept, but it did
   not fix the fader artifact. The zipper came from the gain staircase, not
   from the int16→int32 bit depth.

### 2.4 Solution: block-rate one-pole smoothing

Added a 6-element smoothing state in `mixer.c`:

```c
static INDTCMZ float mixer_fader_smooth[6];
#define MIXER_FADER_SMOOTH_ALPHA 0.15f
```

Applied in `mixer_faderGains()` at the block rate (1,378 Hz):

```c
const float fader_raw = slider_vol[slot];
if (fader_raw <= 0.0f || fader_raw >= 1.0f)
    mixer_fader_smooth[slot] = fader_raw;
else
    mixer_fader_smooth[slot] += MIXER_FADER_SMOOTH_ALPHA
            * (fader_raw - mixer_fader_smooth[slot]);
const float fader = mixer_fader_smooth[slot];
```

**Parameters:**
- Alpha: 0.15
- Time constant: τ ≈ 4.5 ms
- Cutoff: fc ≈ 35 Hz
- Update rate: 1,378 Hz (block rate, 3× faster than main-loop rate)
- Endpoint bypass: at 0.0 and 1.0 the filter snaps to the exact endpoint
  (true silence at bottom, full gain at top)

**Why block rate:** the mixer already runs once per 32-sample block. Placing
the filter here rather than in the main-loop `adc_checkPots()` gives a 3×
higher update rate, distributing each ADC step across multiple blocks.

### 2.5 Hardware result

User: "It's not entirely gone but it's pretty much the same as the device
noise floor now, I don't think we can do much better than that."

### 2.6 Signal path

```
ADC DMA (12-bit, 4 MHz clock, 480-cycle sample)
  → adc_checkPots() (main loop, ~460 Hz)
    → slider_lut[] (1,024-entry float LUT, non-interpolated)
      → slider_vol[] (float cache, 6 channels)
        → mixer_faderGains() (block rate, 1,378 Hz)
          → mixer_fader_smooth[] (one-pole EMA, α=0.15)
            → per-voice fader gain
              → mixer_addVoiceInt16ToOutput() (per-sample ramp)
```

---

## 3. Session 088 P1 — Retrigger HF Puff Fix

### 3.1 Symptom

A tiny high-frequency "puff" audible on voice retrigger, particularly on a
low-pitch bass drum with `fre` mode, no modulation, no filter, no drive,
and a minimal click/transient setting.

### 3.2 Analysis

The voice post-chain applied amp gain and velocity gain as separate int16
truncation steps before distortion. At low amplitudes during attack onsets,
the two successive truncations quantized small signals to ±1 LSB, producing
a brief burst of HF energy.

### 3.3 Fix: float-through distortion

Two changes, applied together:

1. **Merged gain multiplies:** the Drum post-chain now computes
   `(float)buf[i] * ag * vg` as a single float expression before distortion.
2. **Float-input distortion variant:** `distortion_curveSampleF()` accepts a
   float in ±32767 range directly, avoiding the int16→float→int16 round-trip:

```c
static inline int16_t distortion_curveSampleF(const Distortion *dist,
                                        const float in)
{
    float x = in * (1.f/32767.f);
    x = (1+dist->shape)*x/(1+dist->shape*fabsf(x));
    return (int16_t)(int32_t)(x*32767);
}
```

All three post-chains updated:
- `voicePost_drum()`: merged amp × velocity → `distortion_curveSampleF()`
- `voicePost_mixAddGainDist()` (Snare): float amp gain → `distortion_curveSampleF()`
- `voicePost_addGainDist()` (Cymbal/HiHat): float amp gain → `distortion_curveSampleF()`

### 3.4 Resolution

User initially reported "it's still there, that didn't help much" after the
fix. Further investigation identified possible remaining HF sources (SnapEg,
LFO, velocity mod, int16 buffer floor) but could not isolate a definitive
cause. User ultimately said "can't detect it now" and closed it as fixed.

### 3.5 Build impact

text 676,264 (−80 from the P0 close at 676,344). The merged float multiply
removed the intermediate int16 store, saving a few instructions.

---

## 4. Mixer `* 256.0f` change (P1, kept)

During the fader zipper investigation, the mixer's 12 dry-gain multiplication
sites were changed from `(int32_t)(sample * gain) << 8` to
`(int32_t)(sample * gain * 256.0f)`. This removes the int16 truncation
before the 24-bit shift, preserving sub-LSB detail in the float multiply.
This did not fix the fader zipper (that was a rate issue, not a precision
issue) but was a real precision improvement and was kept.

---

## 5. Decisions and Ambiguities Resolved

### From S088_P0_OSC_RUN_MODES.md

| # | Decision |
|---|----------|
| D1 | FM depth ramp: per-sample linear, constant cost, identity when not armed |
| D2 | The `run` row is `DTYPE_ON_OFF` with high-nibble tag (D12) |
| D3 | One shared helper (`oscSection_renderOldCarrier()`) for all engines |
| D4 | Full-block crossfade blend, no early termination |
| D5 | itp crossfade length = 132 samples (≈ 3 ms) |
| D6 | Decayed itp = exactly rst; refused itp claim = fre |
| D7 | Ring depth snap in rst/decayed-itp; ramp otherwise |
| D8 | `osc1_run` flags = AUTOMATABLE + SELECTOR (not morphable) |
| D9 | `runMode` on the engine struct, not OscInfo |
| D10 | Crossfade = 132 samples, 5 blocks (rounded up) |
| D11 | `ampRampArm` one-block flag, never ramps when clear |
| D12 | DTYPE_ON_OFF high-nibble tag mechanism |
| A3 | Fully decayed itp → rst (no snapshot, no crossfade) |
| A4 | Audible threshold = 1/32768 |
| A8 | Ring snap = behavesAsRst |
| A10 | Repeat mode falls back to slopeEg2_trigger() |

### From the implementation

- `behavesAsRst` boolean unifies rst and decayed-itp across all guards.
- Filter slew centralized in `instrumentManager_recalcSlotFilter()` rather
  than four engine async functions.
- `itpFadeSlotAvailable(exclude_slot)` allows a re-snapshot to reuse
  its own slot without double-counting.
- The Drum instant-attack shortcut is guarded by `!ampRampArm`.

---

## 6. Files and Code Sections (full reference)

### 6.1 distortion.h

`distortion_curveSampleF()` added after `distortion_curveSample16()`:
float-input variant accepting a pre-gained float in ±32767 range, same
curve `(1+k)x/(1+k|x|)`, returns int16.

### 6.2 voicePostChain.h

All three post-chains updated:
- `voicePost_drum()`: takes `lastGain`, `lastVeloGain` for per-sample ramps;
  merged `ag * vg` float multiply → `distortion_curveSampleF()`.
- `voicePost_mixAddGainDist()`: takes `lastAmpGain` for per-sample ramp;
  float through → `distortion_curveSampleF()`.
- `voicePost_addGainDist()`: takes `lastAmpGain` for per-sample ramp;
  float through → `distortion_curveSampleF()`.

All three have `inv_size = (size > 1u) ? 1.f/(size-1.f) : 0.f` guard.

### 6.3 mixer.c

Two S088 changes:
1. `* 256.0f` direct float-to-24-bit in all 12 dry gain sites.
2. Block-rate fader smoothing: `mixer_fader_smooth[6]` (INDTCMZ),
   `MIXER_FADER_SMOOTH_ALPHA 0.15f`, applied in `mixer_faderGains()`.
3. itp slot accounting: `instrumentManager_activeItpFadeCount()` →
   `modNode_setItpReserved()` before `modNode_resetTargets()`.

### 6.4 DrumVoice.c

- `Drum_triggerVoice()` (≈line 221): run-mode decision tree with
  `behavesAsRst`, `ampAudible`, `ampRampArm`, gain-matched retrigger via
  `slopeEg2_retriggerFrom()`, filter reset guard (`!= FRE`), itp snapshot
  before phase reset, ring snap vs ramp.
- `Drum_calcVoiceAsync()` (≈line 442): filter slew via centralized
  dispatcher.
- `Drum_calcVoiceSyncBlock()` (≈line 571): FM depth ramp, itp crossfade
  blend, velocity bookkeeping, instant-attack bypass guard.

### 6.5 SlopeEg2

- `slopeEg2_retriggerFrom()`: inverts attack curve `v = g / (1 + k*(1-g))`,
  clears repeat counter, falls back in repeat mode.
- `slopeEg2_isAudibleGain()`: inline, threshold 1/32768.

### 6.6 ResonantFilter

- `SVF_recalcFreqSlew()`: steps `g` by 0.28 per block toward target.
- `SVF_setFrequencyTarget()`: sets `f` only, does not jump `g`.

### 6.7 OscCarrierSnapshot (Oscillator.h)

28 B struct (ARM), asserted <= 32: phase, phaseInc, waveform, tableOffset,
oscType, active, counter, sample cache fields (sampleCacheIndex,
sampleCacheLooped, sampleCacheGeneration, sampleCacheSize, sampleCacheData).

### 6.8 oscSection_renderOldCarrier (Oscillator.c)

Builds an `OscInfo` view from the snapshot, dispatches to the correct plain
block function, crossfades with accumulated `t` (no per-sample divide),
decrements counter to inactive. Uses `osc_itpScratch[32]` as scratch.
Never applies FM, sync or ring to the old carrier.

---

## 7. Specification documents updated in S088

The following were updated during the P0 implementation (already done):

| Document | What changed |
|----------|-------------|
| `INSTRUMENTS_DSP_REFERENCE.md` | §6.1 run modes block, §6.5 post-chain ramp descriptions |
| `CPU_USE_DSP_AUDIT.md` | Items 50–54 (S088 P0 additions) |
| `STORAGE_SRAM_MANIFEST.md` | S088 P0 header, measured sizes, runtime_slots, scratch, ITCM |
| `FILESYSTEM_SPEC.md` | `osc1_run` key (0 rst, 1 fre, 2 itp, SELECTOR) |
| `MIDI_SPEC.md` | §5.5 `osc1_run` appended after `osc1_type` |
| `MEMORY.md` | §Binding Rules: oscillator run modes (S088) |

---

## 8. Pre-existing issues observed (not changed)

- `make all` ends with `python3 tools/link_budget.py` error (script at
  `tools/diagnostic_tools/link_budget.py`). The ELF builds before that step.
- `make -C tools/dsp_test all` ends at `stock_tables` (path resolution in
  `tools/plots/gen_stock_tables.py`). Individual module targets pass.
- The 7 pre-existing `filesystem.c` unused-function warnings.
- `PatternData.c:201` packed-member and `EuklidGenerator.c:54` sign-compare
  warnings.

---

## 9. Attack rise time reference

| Knob | `data2` | `attack` increment | Blocks 0→1 | Rise time |
|------|---------|-------------------|------------|-----------|
| 0 | 0 | 1.000 | 1 | 0.73 ms |
| 1 | 1 | 0.388 | 3 | 2.2 ms |
| 2 | 2 | 0.239 | 5 | 3.6 ms |
| 5 | 5 | 0.106 | 10 | 7.3 ms |
| 10 | 10 | 0.054 | 19 | 13.8 ms |

Block rate: 44,108 / 32 = 1,378 Hz (≈ 0.726 ms per block).
