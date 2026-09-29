# Instruments DSP Reference

How the six instrument voices are rendered, how their parameters reach the
DSP, how modulation (LFO, velocity, Morph, step automation) works, what each
part costs, and how to change or extend it.

- **Current as of:** Session 073 close (2026-09-29).
- **Related documents:**
  - `EFFECTS_MIXER_DSP_REFERENCE.md`: what happens to a voice block after it
    leaves the instrument (decimation, mixer, FX bus, output);
  - `BANK_PRESET_ARCHITECTURE.md`: where parameter values are stored (Scene
    images, Morph, dirty marking);
  - `FILESYSTEM_SPEC.md`: instrument files and descriptor keys on the card;
  - `CPU_USE_DSP_AUDIT.md`: the cost audit and optimisation history;
  - `OSC_INTERP_AUDIT.md`: oscillator waveform interpolation;
  - `tools/dsp_test/DSP_TEST.md`: the host test bench for DSP changes.

---

## 1. Timing model

| Quantity | Value | Where |
|---|---|---|
| Sample rate | 44,108 Hz (PLLI2S N=271, R=2, I2S prescaler 6) | `AudioCodecManager.c` |
| DSP block | 32 frames (`OUTPUT_DMA_SIZE`), 0.73 ms | `config.h` |
| Control rate | once per block: 1,378 Hz | envelopes, LFOs, filter coefficients |
| DMA half | 96 frames (`AUDIO_DMA_FRAMES`), 2.18 ms = three DSP blocks | `config.h` |
| CPU | 216 MHz, Cortex-M7 with single- and double-precision FPU, I- and D-cache on | `clocks.c` |
| Budget | 4,897 cycles per output frame; about 156,700 per 32-frame block | |

- **Rendering runs in the main loop**, never in an ISR.
  `audio_check_and_render()` (`main.c`) fills each free 96-frame DMA slot
  with three calls to `mixer_calcNextSampleBlock()`. Before each call it
  drains pending triggers (`voiceControl_processPending()`) and pending step
  automation (`seq_drainPendingAutomation()`), in that order. Each call runs
  with `BASEPRI` masking low-priority interrupts only (timing-critical
  sources such as TIM3, TIM1, audio DMA and USART3 stay live). If rendering
  falls behind, the DMA ISR replays the last slot and counts an underrun
  (`audioCodec_underrunCount`); the system never locks up. Rendering inside
  the DMA ISR was tried and failed (Session 10).
- **Do not change `OUTPUT_DMA_SIZE`.** Every envelope and LFO step is per
  block, so a different block size changes every time constant, and it breaks
  the LXR-master cadence.

### 1.1 Constant-CPU rule

**No CPU saving may come from skipping or bypassing DSP work because an
element is inactive, silent or set to zero.** Every voice renders every block
in full, even when silent: a finished amp envelope multiplies a fully
rendered block by zero. This keeps the cost flat, so headroom is always the
worst case with everything active. Any change that would switch processing
off for a feature that can be active must be raised with the user
specifically. The source of the rule is `MEMORY.md` (DSP CPU Policy).

A few older paths do vary with settings; they are algorithm choices, not
skips, and were kept by user decision:

- the oscillator waveform (sine, wavetable, noise, sample, FM) and the filter
  type (the naive 2-pole type is cheaper than the ZDF types);
- transient modes 0 (snap envelope) and 1 (offset) render no sample, so
  `transient_calcBlock()` writes zeros instead of reading the table;
- `osc_setFreq()` skips its phase-increment recalculation when the effective
  frequency and waveform are unchanged (a control-rate cache).

Budget against the expensive choice of each.

---

## 2. Per-block sequence

`mixer_calcNextSampleBlock()` (`mixer.c`) does, in order:

1. `modNode_resetTargets()`: restore every value a modulation node overwrote
   last block (velocity nodes and the live LFO nodes), and advance the
   waveform-interpolation generation.
2. `modNode_reassignVeloMod()`: re-apply each velocity node's last value.
3. `instrumentManager_dispatchRuntimeLfos()`: one LFO step per slot (§7.1).
4. `instrumentManager_recalcSlotFilter(slot)` × 6: `SVF_recalcFreq()`, which
   recomputes the filter gain `g = tan(π·f)` with `fastTan()`.
5. `instrumentManager_calcSlotAsync(slot)` × 6: each engine's control-rate
   update (envelopes, pitch modulation, oscillator frequencies).
6. `effects_service()` (the Effect, see `EFFECTS_MIXER_DSP_REFERENCE.md`).
7. For each slot 0..5: `instrumentManager_calcSlotSyncBlock(slot, buf, 32)`
   renders one mono, **pre-volume** int16 block; the mixer then decimates,
   applies volume, pan and routing, and taps the FX send.

**Triggers** arrive from the sequencer (TIM3), MIDI and the front panel.
They are queued and applied in the foreground by
`voiceControl_processPending()` → `instrumentManager_triggerTrack(track,
note, velocity)`. Tracks 0..5 trigger slots 0..5. Track 6 (visible track 7)
triggers slot 6 as its alternate: a Choke instrument (HiHat) gets the open
variant; any other instrument borrows a hidden Scene decay value for that hit
(step automation, then LFO, then the retained Kit setting, in that priority).

---

## 3. Runtime ownership

### 3.1 Tagged runtime slots

- `InstrumentManager.c` owns `runtime_slots[6]`, a union of the four engine
  structures, 1,176 B per slot (twice the largest engine), 7,056 B in SRAM1.
  `_Static_assert`s keep every engine inside the reserve.

  | Engine | Struct | Bytes |
  |---|---|---:|
  | Drum | `DrumVoice` | 588 |
  | Snare | `SnareVoice` | 420 |
  | Cymbal | `CymbalVoice` | 476 |
  | HiHat | `HiHatVoice` | 484 |

  Building blocks: `OscInfo` 72 B, `ResonantFilter` 36 B, `Lfo` 124 B,
  `ModulationNode` 44 B, `SlopeEg2` 28 B, `DecayEg` 12 B,
  `TransientGenerator` 24 B, `Distortion` 8 B.
- `runtime_slot_type[6]` is a **runtime type shadow**. The active Scene can
  change at once while a slot is still ringing with the old instrument; the
  shadow keeps rendering the type whose parameters are actually loaded.
  `instrumentManager_resetRuntimeSlot()` is its only writer: it zeroes the
  slot, copies the incoming type and calls the engine's `*_initVoice()`.
- A Scene switch waits for a slot to go quiet
  (`instrumentManager_ampEnvelopeQuiet()`: envelope stopped or value
  ≤ 0.0001) or for the new Scene to trigger it, then clears the modulation
  graph, resets the slot, applies the descriptor images and rebinds every LFO
  and velocity target (`BANK_PRESET_ARCHITECTURE.md`).
- **Borrowing rule:** `instrumentManager_runtimeInstance(slot)` and the
  private per-type accessors return a pointer for immediate use only. Never
  keep one across a Scene or Instrument change.
- **Type limits:** each type has flags: Drum and Snare are Basic; Cymbal is
  Advanced; HiHat is Advanced and Choke. A Scene may hold at most two
  Advanced instruments (`instrumentManager_typeSelectableForSceneSlot()`).

### 3.2 Dispatch functions

Every per-slot operation goes through a `switch` on the runtime type in
`InstrumentManager.c`. Adding an engine means adding a case to each:

| Function | Calls |
|---|---|
| `instrumentManager_runtimeInit()` / `instrumentManager_resetRuntimeSlot()` | `*_initVoice()` |
| `instrumentManager_triggerTrack()` | `*_triggerVoice()` |
| `instrumentManager_calcSlotAsync()` | control-rate `*_calc*Async*()` |
| `instrumentManager_calcSlotSyncBlock()` | render `*_calc*SyncBlock*()` |
| `instrumentManager_recalcSlotFilter()` | `SVF_recalcFreq(&voice->filter)` |
| `instrumentManager_runtimePan()` / `instrumentManager_runtimeVolume()` | `voice->pan` / `voice->vol` |
| `instrumentManager_runtimeLfo()`, `_filter()`, `_ampEg()`, `_pitchEg()`, `_distortion()`, `_transient()`, `_oscBySelector()` | accessors used by the special writers and the LFO code |

---

## 4. Parameters: from stored byte to DSP state

### 4.1 Descriptors

Each instrument type has a flash table of `ParamDescriptor` rows
(`Core/DSP/Instruments/<Type>/<Type>Parameters.c`). A row is one retained
parameter cell (Drum 39 rows, Snare 38, Cymbal 39, HiHat 39):

| Field | Meaning |
|---|---|
| `file_key` | Card key, e.g. `filter_freq` |
| `short_name`, `long_name`, `category` | Menu text (3 characters, 8 characters, category label) |
| `dtype` | Display/edit type (`DTYPE_0B127`, `DTYPE_PM63`, `DTYPE_MENU` with a menu table, target selectors, ...) |
| `flags` | `MORPHABLE`, `MODULATABLE`, `AUTOMATABLE` |
| `mod_domain` | `{min, max, flags}` in descriptor units: the legal range for LFO and velocity overlays (`INSTRUMENT_MOD_DOMAIN_NONE` = not a continuous target) |
| `runtime` | `instrument_runtime_binding_t`: binding kind, byte offset into the engine struct, scalar type, and the special-writer tag |

- **Values are bytes** (`instrument_param_value_t`), 0..127 or 0..255 in the
  row's own domain. Target selector rows store compact tokens (0xFF = off).
- **Menu pages** are also in the parameter file (`*_menu_pages[]`, 16 cells
  per sub-page, shown as four-cell screens).
- **Layout guards:** `sizeof(instrument_runtime_binding_t) == 6` and
  `sizeof(ParamDescriptor) == 28` are `_Static_assert`s, so adding a field
  cannot silently grow every table.
- **Row macros** (in each parameter file): `ROW` (normal image parameter,
  offset binding), `ROW_SPECIAL` (with a writer tag), `ROW_MENU` and
  `ROW_MENU_SPECIAL` (named menu dtype), `ROW_NOBIND` (supplemental selector
  cell, no image flags), `ROW_NOBIND_IMAGE` (image parameter with a
  supplemental binding), `ROW_SLOT_DECIMATION`.

### 4.2 Binding kinds

| Kind | Write target |
|---|---|
| `INSTANCE_OFFSET` | A member of the engine struct at `offset`, written as `parameter_type` (`TYPE_UINT8`, `TYPE_FLT`, `TYPE_UINT32`, `TYPE_SPECIAL_F`, ...) |
| `SLOT_DECIMATION` | `mixer_decimation_rate[slot] = valueShaperI2F(v, −0.7)` |
| `VELOCITY_AMOUNT` | `velocityModulators[slot].amount = v/127` |
| `VELOCITY_TARGET` | Installs the velocity destination from a token |
| `LFO_TARGET_VOICE(_2)`, `LFO_TARGET_PARAM(_2)` | Reinstall LFO pair 1 or 2 from the voice-namespace and parameter-token sibling cells |

### 4.3 The write path

`instrumentManager_writeRuntime(slot, descriptor, value)` is the one way a
retained value reaches the DSP. Its callers: Preset (Kit/Scene/Instrument
apply), Menu edits, the Morph worker, step automation drain and restore, and
Scene activation.

1. `instrumentManager_writeSpecialRuntime()` switches on the row's special
   tag (§4.4). If the row has a special writer, it converts the byte and calls
   the DSP setter, and the write is done.
2. Otherwise the binding kind decides (§4.2).
3. After an ordinary write, `instrumentManager_noteRuntimeValueChanged()`
   refreshes modulation baselines: direct `ModulationNode` targets recapture
   their original value, and LFO adapters on that parameter take the new
   value as their base. LFO overlay writes use the same path with
   notification off, so an overlay never becomes the next base.

**Foreground only.** `instrumentManager_writeRuntime()` is not ISR-safe.
TIM3 queues automation values; the foreground drains them.

### 4.4 Special writers (S073)

Rows whose value needs conversion call a fixed setter. Since Session 073 the
row carries the choice as a tag in the former padding byte of its binding
(bits 0–4 the writer, bits 5–6 the oscillator: `IM_SPECIAL_OSC1`, `OSC2`,
`OSC3`, `OSC_NOISE`). Before, the writer searched the key string on every
write (up to 20 `strcmp`, 2 `strstr` and 7 `strncmp`).

| Tag | Conversion and setter |
|---|---|
| `NOISE_FREQ` | `osc->freq = v/127 × 22,000 Hz` |
| `PITCH_COARSE` | high byte of `osc->midiFreq` = v; `osc_recalcFreq()` |
| `PITCH_FINE` | low byte of `osc->midiFreq` = v; `osc_recalcFreq()` |
| `FILTER_FREQ` | `SVF_directSetFilterValue(valueShaperF2F(v/127, −0.9))` |
| `FILTER_RESO` | `SVF_setReso(v/127)`: `q = 1 − r`, floored at 0.02 |
| `FILTER_DRIVE` | `SVF_setDrive(v)`: `drive = 0.4 + 6·(v/127)²` |
| `FILTER_TYPE` | stores `v + 1` (filter types 1..7) |
| `AMP_ATTACK` | `slopeEg2_setAttack(v, sync)` (Drum uses `AMP_EG_SYNC`) |
| `AMP_DECAY` | `slopeEg2_setDecay(v, sync)`; on a HiHat it sets the closed decay cache `decayClosed` |
| `HAT_DECAY_CHOKE` | HiHat open decay cache `decayOpen` |
| `AMP_SLOPE` | `slopeEg2_setSlope(v)` |
| `PITCH_EG_DECAY`, `PITCH_EG_SLOPE` | `DecayEg_setDecay(v)`, `DecayEg_setSlope(v)` |
| `PITCH_EG_AMOUNT` | Drum/Snare `egPitchModAmount` via `instrumentManager_pitchModAmount(v)` |
| `TRANSIENT_WAVE` | `transient_setWaveform(v)` |
| `TRANSIENT_FREQ` | `transient->pitch = 1 + (v/33.9 − 0.75)` |
| `INSTRUMENT_DRIVE` | `setDistortionShape(v)` (§6.6) |
| `LFO_RATE` | `lfo_setFreq(v)` (§7.1) |

- `valueShaperF2F(x, s)` and `valueShaperI2F(v, s)` (`valueShaper.h`) apply
  the curve `(1+k)x/(1+k|x|)` with `k = 2s/(1.0001 − s)`.
- **Proof that the tags are right:** `make -C tools/dsp_test special_tags`
  classifies every row with the old string rules and compares (155 rows,
  0 mismatches at S073). The diagnostic build shows the same comparison as
  the `s` digit on the `FxBf` boot screen (`DEV_MODES.md`).

### 4.5 Where values come from

Retained values live in the Scene's Kit images: `instrument_parameters[]`
(normal), the Morph endpoint image, and `morph_interpolation[]` (the current
interpolated value that the runtime should hold). Menu edits, loads and the
Morph worker write the images and then call `instrumentManager_writeRuntime()`.
See `BANK_PRESET_ARCHITECTURE.md` for the images, the Morph worker and dirty
marking.

---

## 5. Voice engines

All four render into an int16 buffer in place, in 32-sample blocks. **None
applies channel volume**: the mixer applies `vol` after decimation, so the FX
send taps a pre-volume signal that includes distortion (Session 072).
Scratch buffers are `static int16_t[OUTPUT_DMA_SIZE]`; **variable-length
arrays are forbidden** in DSP code (they caused silent stack corruption).

### 5.1 Drum (`DrumVoice.c`, Basic)

```
modOsc  --(gain fmModAmount)--> modBuf
if mixOscs:  osc (gain 1−fmModAmount) + modBuf, saturating      --> buf
else:        osc phase-modulated by modBuf (FM)                  --> buf
transient (sample 2.., or zeros for snap/offset modes)           --> + buf, saturating
ZDF filter (filterType)                                          --> buf
fused post-chain: amp EG ramp → velocity gain → distortion       --> buf (pre-volume)
```

- **Control rate** (`Drum_calcVoiceAsync`): pitch envelope
  `pitchMod = 1 + DecayEg·egPitchModAmount`; in transient mode 0 the snap
  envelope adds `snap·transientVolume` to the pitch; FM amount follows the
  pitch envelope (`fmMod = fmModAmount·egPitch`); the amp envelope advances
  (`slopeEg2_calc`), keeping `lastGain` and the new value for the per-sample
  ramp; `osc_setFreq()` for both oscillators.
- **Trigger:** retriggers LFOs whose retrigger track matches; applies velocity
  modulation; resets the oscillator phase (with the transient offset mode);
  triggers pitch and amp envelopes, transient and snap; `SVF_reset()` clears
  filter state to avoid a wrong transient.
- **Velocity:** `velo = vel/127`; with `volumeMod` on it scales the output
  (the multiply always runs; with `volumeMod` off it multiplies by 1.0).

### 5.2 Snare (`Snare.c`, Basic)

```
noise osc (gain 0.9)                                  --> buf
ZDF filter                                            --> buf   (the filter shapes the noise only)
transient                                             --> transBuf; buf + transBuf, saturating
tonal osc (gain 1−mix)                                --> transBuf
fused post-chain: buf·mix + transBuf (saturating) → amp gain → distortion
```

`ampGain = volumeMod ? velo·EG : EG`, chosen once per block.

### 5.3 Cymbal (`CymbalVoice.c`, Advanced)

```
modOsc (gain fmModAmount1) + modOsc2 (gain fmModAmount2), saturating --> mod
osc phase-modulated by mod (FM, gain 1.0)                           --> buf
ZDF filter                                                          --> buf
transient                                                           --> mod
fused post-chain: buf + mod (saturating) → amp gain → distortion
```

### 5.4 HiHat (`HiHat.c`, Advanced + Choke)

As Cymbal, with the FM carrier at gain 0.5. Two amp-decay caches: `decayClosed`
(`amp_envelope_decay`) and `decayOpen` (the `_choke` row). The trigger's
alternate flag (visible track 7) selects the open decay.

### 5.5 Where each engine's hot code runs

The oscillator block functions are in ITCM (`INITCM`), zero wait states. The
filter, distortion and post-chains are flash code, inlined by LTO into
`mixer_calcNextSampleBlock()`. ITCM for the filter and distortion was tried
in Session 023 and measured worse.

---

## 6. Building blocks (`Core/DSPAudio/`)

### 6.1 Oscillator (`Oscillator.c/.h`)

- **`OscInfo`** holds a 32-bit phase accumulator, `phaseInc`, `freq`,
  `pitchMod`, `fmMod`, `modNodeValue`, the waveform, the wavetable octave
  (`tableOffset`), a frequency cache, and a sample cache.
- **Waveforms:** `SINE` 0, `TRI` 1, `SAW` 2, `REC` 3, `NOISE` 4, `CRASH` 5
  (the built-in crash sample), then user samples from 6
  (`OSC_SAMPLE_START`).
- **Effective frequency:** `freq · pitchMod · modNodeValue`, where `freq`
  comes from the base note plus coarse/fine tuning
  (`MidiNoteFrequencies[note] · detune`).
- **Phase formats:** sine uses a 4,096-entry table (`<<20`), wavetables 1,024
  entries (`<<22`), samples a `<<17` index. Long samples are not fully
  32-bit clean yet (the `phase >> 17` index path).
- **Wavetables:** `sawTable`, `triTable`, `recTable` are 11 band-limited
  octaves × 1,024 samples (22,528 B each, flash). `osc_setFreq()` picks the
  octave with `freqToTableIndex()`: the number of the ten edges
  `osc_octaveEdgeHz[k] = 440·2^(k − 5.75)` Hz (16.35 Hz … 8,372 Hz) at or
  below the frequency. All ten compares always run. (Before S073 this was
  `(69 + 12·log2f(f/440))/12`; the host check found 16 inputs that differ,
  each within 2 ulps of an edge.)
- **Frequency cache:** `osc_setFreq()` recomputes `phaseInc` and the octave
  only when the effective frequency or waveform changed. A pitch envelope
  changes the frequency every block, so budget for the recompute.
- **Noise:** `calcNoiseBlock()` reads the hardware RNG (`GetRngValue()`,
  returns `int16_t`) on each phase wrap. The RNG makes a new value about
  every 0.83 µs, so at high noise rates some samples repeat. This is the
  accepted noise character: a software PRNG was rejected in S073 because it
  would change it.
- **FM:** `calcNextOscSampleFmBlock()` phase-modulates the carrier by a
  modulator block (`calcFmBlock` for wavetables, `calcFmSineBlock`,
  `calcSampleOscFmBlock`, `calcUserSampleOscFmBlock`).
- **Waveform interpolation:** when a waveform parameter is modulated, up to
  `OSC_WAVE_INTERP_MAX_ACTIVE` (2) oscillators per block blend the two
  neighbouring waveforms (`calcPeriodicInterpBlock`: render both into DTCM
  scratch buffers, then blend). Each costs about two oscillator renders.
  Details: `OSC_INTERP_AUDIT.md`.
- **ITCM:** the block renderers and (since S073) `osc_setFreq()`.

### 6.2 Resonant filter (`ResonantFilter.c/.h`)

- **Types** (`filterType`, stored as UI value + 1): `LP` 1, `HP` 2, `BP` 3,
  `UNITY_BP` 4, `NOTCH` 5, `PEAK` 6 (the nonlinear ZDF state-variable
  filter), `NAIVE_2_POLE` 7 (a cheaper 2-pole low-pass kept for kick
  transients). Other values pass the block through.
- **State:** `f` (0..0.45 of the sample rate), `g = fastTan(π·f)`, `q`, `s1`,
  `s2`, `zi` (input half-sample delay for the nonlinearities), `a`, `b`
  (naive type), `drive`.
- **Resonance:** `R = q`, except `R = 1` when `f ≥ 0.4499` (stability fix
  for high f and resonance).
- **Batched ZDF solver (S073 Step 1).** The nonlinear trapezoidal SVF needs
  reciprocals for the input soft clip, the two `tanh(x)/x` Padé terms, the
  integrator gain and the output. They are grouped into two divisions per
  sample:
  - normalised Padé pieces `N = (a/945 + 105/945)·a + 1`,
    `D = (15a/945 + 420/945)·a + 1` with `a = v²` (both ≥ 1, so the products
    stay far from overflow): `svf_padeNum()`, `svf_padeDen()`;
  - Stage A: one division gives the soft-clipped input `x` and `t1`;
  - Stage B: one division gives `t0`, `g0` and the output `y1`;
  - LP adds `fastTanh()` on the output (`4.15x/(4.29 + x²)`, clamped at
    ±1.95).
  - An `#error` rejects configurations the algebra does not cover
    (`ENABLE_NONLINEAR_INTEGRATORS 0` or `USE_SHAPER_NONLINEARITY 1`).
  - Sound class S1: only float rounding changed (int16 SDR 91.7 dB against
    the old code; every larger difference is in the self-oscillating
    cutoff 0.8 / resonance 0.98 family, where a compiler-flag change also
    diverges).
- **Two twins:** `SVF_calcBlockZDF()` (int16 in and out, `__SSAT` to 16
  bits, output gain `FILTER_GAIN = 0x70ff`) for voices, and
  `SVF_calcBlockZDFFloat()` (normalised float, no saturation) for Effects.
  **Keep their arithmetic identical.**
- **Filter state in locals:** `s1/s2/zi` are loaded into registers and
  written back once per block.

### 6.3 Envelopes

| Envelope | File | Behaviour |
|---|---|---|
| Amp (`SlopeEg2`) | `SlopeEg2.c` | States stopped / attack / decay / repeat. Linear value steps once per block, shaped by `(1+k)x/(1+k|x|)` (slope curve; attack uses the inverse slope). Times: `1 − (1+K)v/(1+K|v|)` with `K` from 0.99 (attack) or 0.999 (decay); sync mode divides by 16. Repeat mode loops the attack phase as a second decay. |
| Pitch (`DecayEg`) | `Decay.c` | One-shot decay from 1 with the same slope curve. |
| Snap (`SnapEg`) | `snapEg.c` | `24·value²` pitch kick, decaying by `0.2·transient pitch` per block; used when transient mode 0 is selected. |

Envelopes run at control rate. The amp gain is ramped per sample from the
previous block's value to the new one (Drum: `lastGain` → `ampFilterInput`)
to avoid zipper noise.

### 6.4 Transient generator (`transientGenerator.c`)

Plays one of the `transientData` samples (26,460 B in flash) from its start
on trigger, at pitch `transient->pitch`, scaled by `volume`. Modes 0 (snap
envelope) and 1 (phase offset at trigger) produce no sample.

### 6.5 Buffer tools and post-chains

- `BufferTools.h`: `bufferTool_satAdd16()` (sum clamped with `__SSAT` to
  16 bits), `bufferTool_satAdd32()` (64-bit sum clamped to int32), `bufferTool_floatToInt16Store()` (the
  float → int16 conversion used by every stage: `(int16_t)(int32_t)x`),
  `bufferTool_interpolatedGain()` (the per-sample ramp).
- `bufferTool_addBuffersSaturating()` processes two samples per step with
  packed saturating adds.
- **`voicePostChain.h` (S073 Step 4)** fuses each engine's final stages into
  one loop. It keeps every int16 truncation and saturation point the old
  separate passes had, so the output is bit-identical:
  - `voicePost_drum()`: amp ramp → velocity gain → distortion;
  - `voicePost_mixAddGainDist()` (Snare): mix gain → saturating add → amp
    gain → distortion;
  - `voicePost_addGainDist()` (Cymbal, HiHat): saturating add → amp gain →
    distortion.

### 6.6 Distortion (`distortion.c/.h`)

- Curve: `y = (1+k)·x/(1+k·|x|)` on `x = s/32767`, back to int16;
  `k = 2·(d/128)/(1 − d/128)` for the byte `d` (0 at `d = 0`, 254 at 127).
- One division per sample, always run. There is deliberately **no bypass at
  drive 0**: it would make cost depend on a control, and the round trip at
  `k = 0` can move one LSB, so a bypass would change the sound.
- `distortion_curveSample16()` is shared by `calcDistBlock()` and the fused
  post-chains so both evaluate the same expression.

---

## 7. Modulation

### 7.1 LFOs (`lfo.c/.h`)

- One LFO per voice (in its engine struct) with **two destinations**
  (`modTarget`, `modTarget2`) sharing phase, waveform, rate, sync, offset,
  retrigger and polarity; each destination has its own amount.
- **Waveforms:** sine (from `sine_table`), triangle, saw up, saw down, rect,
  noise (a new hardware-RNG value on each wrap), exponential up, exponential
  down. Output is 0..1, except noise (§10).
- **Rate:** `lfo_setFreq(v)`: `f = ((v+1)/128)³ · 200 Hz`; the phase
  increment is `f / (44,108/32) · 2³²`, so the LFO steps once per block.
  Sync modes 1..11 (4/1 … 1/32) derive the rate from `seq_getBpm()`.
- **Retrigger:** a trigger on the selected track resets the phase to the
  offset.
- **Polarity:** negative (original LXR: `base · (1 − amount + amount·lfo)`,
  moves down from the base), positive (towards the maximum), bipolar.
- **Dispatch** (`lfo_dispatchNextValue`, once per block per slot): the value
  goes to any remaining direct `ModulationNode` target, then to
  `instrumentManager_updateLfoAdapters()` for both pairs.

### 7.2 LFO destinations

`lfo_target_voice` selects the namespace: 1..6 voices, 7 `scn` (Scene
targets), 8 `fx` (the active Scene's Effect). `lfo_target_param` holds a
local token in that namespace. Install:
`instrumentManager_installLfoModulationTarget()`. Kinds:

| Destination | Mechanism |
|---|---|
| Instrument descriptor parameter (any slot) | **Descriptor adapter** (`lfo_descriptor_targets[6][2]`): each block the LFO value is shaped in descriptor units (`modNode_shapeParameterU16()`, clamped to the row's `mod_domain`) around the adapter's base value, and written through the normal writer with notification off. So an LFO on filter cutoff goes through the same `valueShaperF2F()` and `SVF_directSetFilterValue()` as a knob. |
| Slot decimation | Supplemental write to `mixer_decimation_rate[slot]` |
| Scene target (`instrumentManager_updateLfoSceneDestination()`) | Voice Morph: a base-independent direction + depth entry that the Morph engine resolves around the current base (S071). Effect Morph (`fxm`): the same encoding, resolved by EffectsManager. Scene decimation (`srt`) and the slot-6 track-7 decay: shaped around the retained value and applied as runtime-only overrides, so the saved value never moves. |
| Effect parameter or `fxm` | Direction + depth entry in EffectsManager (`effects_setLfoContribution()`), resolved around the Effect's current held value |

- **Base and restore:** the adapter's base is the parameter's current
  retained value, refreshed by every ordinary write (menu, load, Morph, step
  automation), so an LFO modulates around the automated value. Clearing or
  replacing a target restores the base through the same writer.
- **Validation:** the adapter re-checks each block that the target slot still
  holds the same descriptor; a changed Instrument makes it a no-op until the
  rebind.

### 7.3 Velocity modulation

- `velocityModulators[6]` (DTCM, `ModulationNode`), one per source voice,
  with `amount` from the `velo_mod_amount` row.
- The destination is the source voice's own descriptor (a direct
  `ModulationNode` pointer with a cached min/max range), slot decimation, or
  the voice's own Scene Morph (token `0x40`). Velocity cannot target Effects.
- On trigger: `modNode_updateValue(&velocityModulators[slot], vel/127)` for
  direct targets, then `instrumentManager_applyVelocityModulationTarget()`
  for supplemental ones.

### 7.4 Block-local overlays and restore

- Direct `ModulationNode` targets write the runtime field and remember the
  original value. `modNode_resetTargets()` restores them at the start of the
  next block, so an overlay lasts exactly one block. Descriptor adapters
  instead write through the owner's setter each block, with the base kept in
  the adapter.
- **Waveform modulation** of an oscillator can blend neighbouring waveforms
  (§6.1) for at most two oscillators per block; the rest step.

### 7.5 Step automation and Morph

- **Step automation:** TIM3 queues values; the foreground drain
  (`seq_drainPendingAutomation()`, after trigger processing in the render
  loop) writes voice targets with `instrumentManager_writeRuntime()` and
  marks them dirty. Transport restart and Pattern changes restore dirty
  parameters from `morph_interpolation[]` (not from the normal image).
  Values are stored 7-bit with an identity mapping.
- **Morph:** the Morph worker (`presetMorph_tick()`, foreground, one
  parameter per pass) interpolates normal → Morph images and writes
  `morph_interpolation[]` through `instrumentManager_writeRuntime()`. An LFO
  aimed at a voice Morph re-queues a pass of every Morphable parameter every
  block, which is foreground cost.
- **Priority:** step automation and Morph set the base; the LFO modulates
  around it. Details and the Scene-target rules:
  `BANK_PRESET_ARCHITECTURE.md` and `PATTERN_DYNAMIC_STACK.md`.

### 7.6 Target IDs

| IDs | Owner |
|---|---|
| 0..383 | Voice parameters: `slot · 64 + local` (`instrumentParam_make()`) |
| 384..447 | Scene targets (`SceneModTargets.c`; 404 = `fxm`) |
| 448..510 | Effect parameters (local 0..62) |
| 511 | Pattern automation off sentinel |

Retained cells store byte tokens; wide IDs exist only during install,
display and runtime resolution.

---

## 8. Cost

### 8.1 Measured instruction counts (S073)

Per-sample loop instruction totals from `fpseq.py --report --all` on
standalone `-Ofast` ARM objects. Instructions, not cycles: the Cortex-M7
dual-issues simple instructions, and `VDIV.F32` costs about 14 cycles.

| Stage | Instructions per sample | Divisions |
|---|---:|---:|
| Sine / wavetable oscillator | 23 / 24 | 0 |
| FM wavetable / FM sine carrier | 35 / 30 | 0 |
| Noise | 12–13 | 0 |
| Crash sample / FM crash | 30 / 38 | 0 |
| User sample (non-looped to looped paths) | about 28–49 | 0 |
| Transient sample | 22 | 0 |
| Saturating buffer add | about 4–5 (two samples per step) | 0 |
| ZDF filter (per type) | 85–104 (was 73–89) | 2 (was 5) |
| Naive 2-pole filter | about 40–60 | 1 |
| Drum post-chain | 32 (was 38 in three passes) | 1 |
| Snare post-chain | 30 (was 35) | 1 |
| Cymbal/HiHat post-chain | 25 (was 28) | 1 |

### 8.2 Per-voice estimate (worst case, per sample)

| Engine | Composition | About |
|---|---|---:|
| Drum (FM mode) | mod osc 24 + FM carrier 35 + transient 22 + add 5 + filter 95 + post 32 | 215 instructions, 3 divisions |
| Snare | noise 13 + filter 95 + transient 22 + add 5 + osc 24 + post 30 | 190 instructions, 3 divisions |
| Cymbal | 2 mod osc 48 + add 5 + FM carrier 35 + filter 95 + transient 22 + post 25 | 230 instructions, 3 divisions |
| HiHat | as Cymbal | 230 instructions, 3 divisions |

Add waveform interpolation (about one extra oscillator render each, at most
two per block) and the mixer per slot (decimator 12–18, dry path 28–46, plus
the send; `EFFECTS_MIXER_DSP_REFERENCE.md`).

### 8.3 Share of the budget (audit estimates, ±50 %)

From `CPU_USE_DSP_AUDIT.md` (S073 audit, before the refactor): six voice ZDF
filters 12–16 % of the CPU, oscillators 4–6 %, distortion about 3 %,
transient/EG/velocity/mix passes 3–4 %, control rate (LFO writes, restores,
envelopes) 2.5–6 %. The S073 refactor cut the filter divisions from 5 to 2,
removed the string search from every descriptor write, and fused the
post-chains. On hardware the user measured about 10 % less CPU on the
worst-case Scene (with the stereo filter Effect).

### 8.4 Control-rate costs

Per block, per slot: one LFO step and up to two adapter writes (each a tag
switch plus the setter; filter cutoff costs one `fastTan()` division);
`SVF_recalcFreq()` (one division); the engine's async update (envelope
divisions); `osc_setFreq()` when the frequency changed. Amortised over 32
samples these are a few percent at most. The Morph worker and LFOs on voice
Morph add foreground (non-render) cost.

---

## 9. How to modify

General rules for every change:

- **Constant CPU** (§1.1). Budget the worst case; no new skip on a control
  value or silence.
- **RAM:** any new or larger static, union member, `INDTCM`/`INDTCMZ`
  object, DMA buffer or ITCM code needs its byte count, region, lifetime and
  owner, and the user's acknowledgement (`STORAGE_SRAM_MANIFEST.md` §10).
  An `INDTCM`/`INDTCMZ` static shrinks the FX arena.
- **Foreground only** for anything that touches InstrumentManager or the
  modulation graph. ISRs only queue.
- **No VLAs**; use `static` block buffers sized `OUTPUT_DMA_SIZE`.
- **Fast-math:** DSP files compile with `-Ofast` (reordering, no NaN/Inf
  handling). Do not rely on NaN or infinity behaviour. No code sets the FPU's
  flush-to-zero mode.
- **Doubles** execute in hardware but more slowly; use `f`-suffixed
  literals in hot code.
- **Verify:** `make clean && make all`, `arm-none-eabi-size`,
  `python3 tools/link_budget.py arm-none-eabi-nm build/lxr02.elf`, the
  relevant `tools/dsp_test` targets, and on hardware the worst-case Scene
  (underruns, the `cpu` widget) and a listening check.

### 9.1 Add a parameter to an existing instrument

1. Add the engine struct member if the DSP needs new state (check the struct
   still fits 1,176 B; the `_Static_assert` will tell you).
2. Add the row in `<Type>Parameters.c` at the end of the table (appending
   keeps existing descriptor indices, which are stored in Scene images,
   automation and LFO tokens), and the matching enum entry. Update
   `<TYPE>_PARAM_DESCRIPTOR_COUNT` (a `_Static_assert` checks it).
3. Choose flags and a `mod_domain`. Only rows with a continuous domain can be
   LFO or velocity targets.
4. If the value needs conversion, give the row a special tag (`ROW_SPECIAL`)
   or add a new writer (§9.2). If the key would match an old string rule,
   the tag must agree: run `make -C tools/dsp_test special_tags`.
5. Place it on a menu page (`*_menu_pages[]`).
6. Storage, Morph, automation and LFO pickers are registry-driven. Check the
   file round trip, AutoSave restore and old files without the key
   (`FILESYSTEM_SPEC.md`: missing keys take defaults).

### 9.2 Add a special writer

1. Add `IM_SPECIAL_<NAME>` before `IM_SPECIAL_WRITER_COUNT` (at most 31
   writers fit bits 0–4).
2. Add the `case` in `instrumentManager_writeSpecialRuntime()`; return 1 when
   it consumed the value, 0 when the slot's engine lacks the target.
3. Tag the rows. If the rows' keys are new, the diagnostic classifier and
   `check_special_tags.py` do not know them; extend both so the proof keeps
   covering every row.

### 9.3 Add a filter type, oscillator waveform or distortion mode

- These files serve every voice and the Effects. Prove the existing modes are
  unchanged (S0) with the bench (`DSP_TEST.md` §6.2), then test the new mode
  on its own.
- Filter: add the type to `filterTypeEnum`, the output `switch` in **both**
  ZDF twins, and the `MENU_FILTER` text; keep the batched solver untouched.
- Oscillator: add the waveform constant, the render `case` in
  `calcNextOscSampleBlock()` / `calcNextOscSampleFmBlock()` and
  `calcNextOscSample()`, the frequency `case` in `osc_setFreq()`, and the menu
  text. Waveform numbers above `CRASH` are user samples; inserting a waveform
  shifts them, which changes stored values. Prefer a design that does not
  renumber.
- Distortion: add a mode selector rather than changing
  `distortion_curveSample16()`; keep the fused post-chains using one shared
  expression per mode.

### 9.4 Add an instrument type

1. `Core/DSP/Instruments/<Type>/`: `<Type>Voice.c/.h` (struct and
   init/trigger/async/render functions taking an instance pointer) and
   `<Type>Parameters.c/.h` (descriptors, menu pages, display label, type
   flags).
2. Registry row in `InstrumentManager.c` (`instrument_registry[]`: type,
   three-letter token, display label, extension, storage directory, flags,
   descriptors, menu pages) and a new `INSTRUMENT_TYPE_*`.
3. Add the struct to the runtime union and a case in every dispatcher (§3.2),
   including the accessors used by the special writers.
4. Makefile: the voice `.c` into `DSP_SRCS` **and** an explicit `-Ofast`
   rule (the pattern rule covers only `Core/DSPAudio/`); the parameter file
   into the normal sources; the include path.
5. Decide Basic or Advanced from the measured worst-case cost; an Advanced
   type counts towards the limit of two per Scene.
6. Storage: the type token, extension and `/Instrument/<dir>` come from the
   registry; check boot index creation and HCNAMES type fields
   (`FILESYSTEM_SPEC.md`).
7. A type that needs audio memory uses FxBuffer voice units (4,416 B each, at
   most two per slot; `EFFECTS_MIXER_DSP_REFERENCE.md` §5). It must clear
   what it reads unless it adopts content from the handoff record.

### 9.5 Add a modulation destination kind

Follow the adapter pattern in `instrumentManager_installLfoModulationTarget()`
and `instrumentManager_updateLfoAdapters()`: resolve once at install, shape in
the destination's own units, write through the owner's setter, and restore
through the same path on clear. Do not write shaped values into raw DSP
fields from `ModulationNode`.

---

## 10. Pitfalls

- **Descriptor indices are stored.** Reordering or inserting rows changes the
  meaning of saved Scenes, automation and LFO tokens. Append.
- **The two ZDF twins must match.** A change to one without the other makes
  an Effect filter sound different from a voice filter.
- **Special tags must match the keys.** A wrong tag silently sends a value to
  the wrong setter. Run the tag check.
- **Pre-volume engines.** Do not apply `vol` in an engine; the mixer does.
- **The runtime type shadow**, not the Scene's stored type, decides which
  engine renders a slot during a Scene switch.
- **Pointers into `runtime_slots` die at a type change.**
- **Retrigger phase:** Drum resets its filter and phase on trigger; the other
  engines keep their oscillator phase and filter state.
- **LFO noise range (suspected issue, not verified):** `lfo_calc()` divides the
  signed `GetRngValue()` by 32767, so noise spans −1..1 while every other
  waveform is 0..1. The descriptor shaper clamps its source to 0..1, so about
  half the noise steps may sit at the clamp. Logged in `SCOPING_TARGETS.md`;
  changing it would change the sound, so check with the user first.

---

## 11. History

- Sessions 008–023: port and first optimisation (caches, MPU, LTO, `-Ofast`,
  ITCM oscillators, NVIC order, DTCM output buffers; `CPU_USE_DSP_AUDIT.md`
  items 1–15).
- Sessions 032–044: descriptor registry, tagged runtime slots, descriptor
  LFO/velocity targets, dynamic Instrument Load.
- Session 071: base-independent LFO voice-Morph; LFO target-voice handler.
- Session 072: engines render pre-volume; the LFO `fx` namespace.
- Session 073: batched ZDF divisions, special-writer tags, fused post-chains,
  octave edge table (`073_SESSION_HANDOFF_LOG.md` §6).
