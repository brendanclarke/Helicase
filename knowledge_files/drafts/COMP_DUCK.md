# Lightweight Mastering Compressor Implementation Plan

## Overview
You requested a lightweight stereo mastering compressor on DAC1 (Stereo Out 1) with parameters: depth, speed, mix, saturation, and trigger-based sidechain. 

Because DSP overhead is a major concern on this MCU, we can avoid heavy logarithm/pow() calculations typical of standard compressors and instead implement a **feed-forward peak compressor** combined with a **trigger-based ducker** and a **rational polynomial saturator**.

## Proposed DSP Architecture

We can insert this processing at the very end of `mixer_calcNextSampleBlock()` in `mixer.c`, operating exclusively on the `output2` buffer (which corresponds to DAC1 / Stereo Out 1).

### 1. Level Detection (Audio + Trigger Sidechain)
Instead of a heavy RMS window, we use a simple one-pole peak follower on the audio, summed with a sidechain envelope:

- **Audio Envelope (`audio_env`)**: 
  Calculated per-sample using the maximum absolute value of L and R. 
  Attack is fixed (e.g., 2ms) to catch transients. **Speed** parameter controls the release time.
- **Sidechain Envelope (`sc_env`)**: 
  Instead of analyzing the audio of the kick drum, we simply trigger a decay envelope whenever the designated sidechain track fires. The amplitude of the trigger is scaled by the sequencer velocity. This envelope decays based on the same **Speed** parameter.

`total_control_voltage = audio_env + (sc_env * sidechain_depth)`

### 2. Gain Calculation (Depth)
To avoid expensive dB conversions, we use a linear gain reduction formula:
`target_gain = 1.0f - (total_control_voltage * depth_parameter)`
*(We clamp `target_gain` to a minimum of ~0.1f to prevent silence/phase inversion).*

### 3. Parallel Mix
We process the signal and mix it with the dry signal:
`wet_L = L * target_gain`
`mixed_L = L * (1.0 - mix_parameter) + wet_L * mix_parameter`

### 4. Saturation
We can reuse the existing highly-optimized `distortion_calcSampleFloat` algorithm (`x = (1+shape)*x / (1+shape*abs(x))`). We apply a very gentle `shape` value derived from the **Saturation** parameter to glue the parallel mix together and catch any stray peaks.

## Parameters Map
1. **Depth**: Scales the intensity of the audio envelope. (0 = no compression, 1 = heavy squashing).
2. **Speed**: Controls the release coefficient of both the audio compressor and the sidechain ducker. (Fast = pumping, Slow = smooth leveling).
3. **Mix**: Crossfades between the uncompressed dry signal and the compressed wet signal (0.0 to 1.0).
4. **Saturation**: Controls the `shape` parameter of the polynomial soft-clipper applied post-mix.
5. **Sidechain Track**: Selects which sequencer track (1-6) triggers the ducking envelope, or 0 for OFF.
6. **Sidechain Depth**: Controls how much the trigger envelope ducks the master volume.

## User Review Required

> [!IMPORTANT]
> **Sidechain Trigger vs Audio**: My proposed sidechain is triggered strictly by MIDI/Sequencer note-on events rather than analyzing the audio of the kick drum. This is *much* cheaper on the CPU and provides perfectly tight ducking. Is this what you meant by "SC depth by trigger/velocity"?

> [!WARNING]
> **Parameter Storage**: Where should these 6 parameters live in the UI? Should I add them to the global `mixer` menu page, or create a dedicated `MSTR` (Master) effects page?

> [!TIP]
> **Performance**: This entire block (envelope, 2 multiplies for gain, mix, and 2 saturator calls) will take fewer than ~25 CPU cycles per sample. Across a 32-sample DMA block, the overhead is practically invisible.

Let me know if you approve this DSP architecture and where you'd like the UI parameters to be placed!
