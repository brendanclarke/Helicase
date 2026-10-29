# S088 P1 — Slider Zipper Fix

**Session:** 088, phase P1
**Branch:** `dev-ph7-voice_expansion`
**Date:** 2026-10-29

---

## Symptom

A "zipper" artefact is audible when moving the hardware slider faders
(RV5–RV10) while a voice is playing. The gain steps are heard as
staircase noise superimposed on the audio output.

## Cause

`slider_vol[]` is refreshed in `adc_checkPots()` (called once per main
loop iteration) by a non-interpolated LUT lookup:

```c
slider_vol[i] = slider_lut[(cur & SLIDER_RAW_MASK) >> SLIDER_LUT_SHIFT];
```

The ADC is 12-bit (0–4095). The LUT was 1,024 entries (10-bit index,
shift 2), so each entry covered **4 raw ADC codes**. Because the LUT is
not interpolated, the gain output quantises to 1,024 steps. With the
`SLIDER_LOG_TAPER_DB` dB curve, the gain steps in the middle of the
taper can be large enough to hear — especially through the mixer's
per-sample ramp which faithfully reproduces every step as a short ramp
segment.

The mixer does apply a per-sample gain ramp between blocks
(`mixer_voice_last_gain[slot]` → current combined gain), which smooths
*within-block* transitions. The zipper comes from the *between-block*
gain steps at the quantised LUT resolution — each block gets a new
endpoint that is one or more LUT nodes apart.

## Path

```
ADC DMA (12-bit) → slider_refreshVolumes() → slider_lut[] (non-interpolated)
→ slider_vol[] (float cache) → mixer_faderGains() → mixer_addVoiceInt16ToOutput()
  (per-sample ramp from mixer_voice_last_gain to voiceGain)
```

All files: `Core/Hardware/frontPanel/IO/adcPots.c`,
`Core/Hardware/frontPanel/IO/adcPots.h`, `Core/DSPAudio/mixer.c` (mixer
is unchanged, listed for the ramp path).

## Change applied (test build)

Doubled the slider LUT from 1,024 to **2,048** entries:

| | Before | After |
|---|--------|-------|
| `SLIDER_LUT_ENTRIES` | 1,024 | 2,048 |
| `SLIDER_LUT_SHIFT` | 2 (implicit) | 1 (named constant) |
| Raw codes per node | 4 | 2 |
| LUT size | 4,096 B | 8,192 B |
| SRAM1 `.bss` delta | — | **+4,096 B** |

The shift is now a named constant `SLIDER_LUT_SHIFT` used in both the
build loop and the refresh loop, so the two cannot diverge.

### Files touched

| File | Change |
|------|--------|
| `Core/Hardware/frontPanel/IO/adcPots.c` | `SLIDER_LUT_ENTRIES 2048`, `SLIDER_LUT_SHIFT 1`, LUT build uses `<< SLIDER_LUT_SHIFT`, refresh uses `>> SLIDER_LUT_SHIFT`, comment blocks updated |
| `Core/Hardware/frontPanel/IO/adcPots.h` | Comment block updated (2,048-node, two-code) |

### Build

```
text 676,352   data 420   bss 416,988   (bss +4,096 vs pre-change)
```

Flash headroom 77,312 B. No new warnings.

## Hardware test matrix

- **Z1:** slow fader sweep on a sustaining voice — compare zipper
  audibility before/after (the A/B is reverting `SLIDER_LUT_ENTRIES` to
  1024 and rebuilding).
- **Z2:** fader at rest — confirm the output is bit-identical to the
  1,024-entry build (the mixer ramp endpoints are equal, so the block
  gain is the same float; only the LUT node resolution changed).
- **Z3:** fader extremes — 0 (silence) and max (full gain) must still
  reach exact endpoints (`slider_raw_to_float` returns 0.0/1.0 at the
  deadzones, unchanged).
- **Z4:** rapid fader sweeps on all six sliders simultaneously — confirm
  no audible regression in latency or smoothness.

## If the doubled LUT is not enough

Options, in order of cost:

1. **Double again to 4,096** (the full 12-bit range, shift 0): +8,192 B
   more SRAM1 (16,384 B total LUT), eliminates all quantisation — every
   raw ADC code gets its own float node. Maximum resolution from this ADC.
2. **Linear interpolation between adjacent nodes** in
   `slider_refreshVolumes()`: one multiply-add per slider per call,
   keeps the LUT at 2,048 or even 1,024, gives sub-node resolution. Cost
   is CPU (six multiply-adds per main-loop iteration, negligible).
3. **Exponential moving average on `slider_vol[]`**: a one-pole low-pass
   with a configurable time constant, called every `adc_checkPots()`.
   Smooths both quantisation and ADC noise. Adds latency to fader moves.
