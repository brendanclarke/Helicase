# Instruments DSP Reference

How the six instrument voices are rendered, how their parameters reach the
DSP, how modulation (LFO, velocity, Morph, step automation) works, what each
part costs, and how to change or extend it.

- **Current as of:** Session 085 close (2026-10-27). S085 rebuilt the
  oscillator: five types (`wav pwm smp slp wtb`), run modes
  (`rst fre itp`), Drum `mod` modes `fm mix snc rng`, the render pool, the
  stock waves and sample-flash playback. §12 is the full reference; §5,
  §6.1, §7.4, §8 and §9.3 point to it.
- **Earlier:** Session 076 close (2026-10-06). S074 changed nothing
  in the voice engines; it added a sidechain tap in the trigger funnel
  (§2) and a mirrored fader gain used by the mixer's `xfd` mode. S075
  changed no engine either: it retired the global `srt` LFO/automation
  target and fixed **who may write a voice parameter's runtime value
  when** (§4.3, §7.5). S083 Q1 made a MIDI write a menu edit (same priority
  and actions). S076 added
  LFO retrigger value 7 (`scn`, Scene-change phase handoff) and fixed
  the LFO phase offset scaling (§7.1).
- **Related documents:**
  - `EFFECTS_MIXER_DSP_REFERENCE.md`: what happens to a voice block after it
    leaves the instrument (decimation, mixer, FX bus, output);
  - `BANK_PRESET_ARCHITECTURE.md`: where parameter values are stored (Scene
    images, Morph, dirty marking);
  - `FILESYSTEM_SPEC.md`: instrument files and descriptor keys on the card;
  - `CPU_USE_DSP_AUDIT.md`: the cost audit and optimisation history;
  - `OSC_INTERP_AUDIT.md`: oscillator waveform interpolation;
  - `FILESYSTEM_SPEC.md` "Sample flash catalogue v2": how samples, wavetables
    and the stock waves reach sample flash;
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

**Sidechain tap (S074).** `voiceControl_triggerNow()` (`MidiVoiceControl.c`),
the single trigger funnel for the sequencer, rolls, MIDI and front-panel
previews, also calls `busComp_sidechainTrigger(track, velocity)`. The master
bus compressor records the largest `(velocity/127)³` weight for the track
selected by the active Scene's `csc` and applies it in the next block
(`EFFECTS_MIXER_DSP_REFERENCE.md` §5A). Track 6 (visible track 7) counts as
voice 6, a working assumption not yet confirmed on hardware (BC11). The
tap is foreground-only, like the rest of the funnel.

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
apply, supplemental edits), the Morph worker and the one-parameter menu
apply (both through the guarded `presetMorph_writeRuntimeBase()`), step
  automation drain and restore, and Scene activation. External MIDI does not
  write the runtime directly either: since S083 Q1 it commits through the
  shared menu path (`menu_commitTargetEdit()`), whose menu-edit write reaches
  the runtime through the same guarded `presetMorph_*` route (§7.5).

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
interpolated value that the runtime should hold). Menu edits, loads, MIDI and
the Morph worker write the images; the runtime then receives
`morph_interpolation[]`, never a raw endpoint, unless step automation holds
that parameter (§7.5).
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
oscillator section (§12.7), by the mod row (mixOscs = OSC_MOD_*):
  fm : modOsc (gain amt) -> modBuf; osc phase-modulated by modBuf --> buf
  mix: modOsc (gain amt) + osc (gain 1−amt), saturating             --> buf
  snc: osc hard-synced to modOsc; slave·(1−amt) + master·amt         --> buf
  rng: osc·(1−amt) + ((osc·modOsc)>>15)·amt, saturating             --> buf
  itp active: the old carrier renders the same way into the pool
  scratch and crossfades into buf (oscSection_blendItp)
transient (sample 2.., or zeros for snap/offset modes)           --> + buf, saturating
ZDF filter (filterType)                                          --> buf
fused post-chain: amp EG ramp → velocity ramp → distortion       --> buf (pre-volume)
```

- **Control rate** (`Drum_calcVoiceAsync`): pitch envelope
  `pitchMod = 1 + DecayEg·egPitchModAmount`; in transient mode 0 the snap
  envelope adds `snap·transientVolume` to the pitch; FM amount follows the
  pitch envelope (`fmMod = fmModAmount·egPitch`); the amp envelope advances
  (`slopeEg2_calc`), keeping `lastGain` and the new value for the per-sample
  ramp; `osc_setFreq()` for both oscillators.
- **Trigger:** retriggers LFOs whose retrigger track matches; applies velocity
  modulation; asks `oscSection_beginTrigger()` what the run mode resets
  (§12.6): in `rst` the carrier phase resets (only when the amp envelope is
  closed, as before; with the transient offset mode) and `SVF_reset()`
  clears the filter; in `fre` nothing resets and every oscillator slews; in
  `itp` the carrier is snapshotted and restarted. Then it triggers the pitch
  and amp envelopes, transient and snap.
- **Amp envelope on a trigger (S085 P2, §12.6.1):** `rst` calls
  `slopeEg2_trigger()` as before. `fre`/`itp` call
  `slopeEg2_retriggerFrom(ampFilterInput)` (the attack starts at the gain
  the last block ended on) and set `ampRampArm` when that gain is audible.
  An armed block always ramps the envelope gain from `lastGain`, even at
  attack 0 (the async's no-ramp rule for attack 0 is skipped), and ramps
  the velocity gain from `lastVeloGain`.
- **Velocity:** `velo = vel/127`; with `volumeMod` on it scales the output
  (the multiply always runs; with `volumeMod` off it multiplies by 1.0). The
  post-chain ramps it from `lastVeloGain` on an armed block; every other
  block passes equal endpoints (flat).

### 5.2 Snare (`Snare.c`, Basic)

```
noise osc (gain 0.9)                                  --> buf
ZDF filter                                            --> buf   (the filter shapes the noise only)
transient                                             --> transBuf; buf + transBuf, saturating
tonal osc (gain 1−mix)                                --> transBuf  (itp: the old copy blends in here)
fused post-chain: buf·mix + transBuf (saturating) → amp ramp → distortion
```

The tonal oscillator is the carrier for the run modes; the noise oscillator
slews in `fre`/`itp` and is never snapshotted.

`ampGain = volumeMod ? velo·EG : EG`, chosen once per block. The chain
ramps from `lastAmpGain` to `ampGain` across the block; the endpoints are
equal (flat, the S073 loop exactly) except on the one block armed by a
`fre`/`itp` retrigger with level left (`ampRampArm`, §12.6.1). The trigger
calls `slopeEg2_trigger()` in `rst` and
`slopeEg2_retriggerFrom(egValueOscVol)` in `fre`/`itp`.

### 5.3 Cymbal (`CymbalVoice.c`, Advanced)

```
oscSection_renderFm2 (§12.7):
modOsc (gain fmModAmount1) + modOsc2 (gain fmModAmount2), saturating --> mod
osc phase-modulated by mod (FM, gain 1.0)                           --> buf
(itp: the old carrier, FM'd by the same mod block, crossfades in)
ZDF filter                                                          --> buf
transient                                                           --> mod
fused post-chain: buf + mod (saturating) → amp ramp → distortion
```

Amp gain, its ramp and the trigger as Snare (§5.2, §12.6.1).

### 5.4 HiHat (`HiHat.c`, Advanced + Choke)

As Cymbal, with the FM carrier at gain 0.5 (`oscSection_renderFm2(…,
carGain 0.5)`). In `rst` Cymbal resets both modulator phases on a trigger;
HiHat does not (both as before S085). Two amp-decay caches: `decayClosed`
(`amp_envelope_decay`) and `decayOpen` (the `_choke` row). The trigger's
alternate flag (visible track 7) selects the open decay. The amp trigger
and ramp are as Snare (§5.2, §12.6.1); the decay is selected before the
envelope is retriggered, so a matched attack decays at the new note's
rate.

### 5.5 Where each engine's hot code runs

The oscillator block functions, the type renderers, the hard-sync loops and
`osc_setFreq()` are in ITCM (`INITCM`), zero wait states (12,432 of 16,384 B
used after S085, §12.12). The
filter, distortion and post-chains are flash code, inlined by LTO into
`mixer_calcNextSampleBlock()`. ITCM for the filter and distortion was tried
in Session 023 and measured worse.

---

## 6. Building blocks (`Core/DSPAudio/`)

### 6.1 Oscillator (`Oscillator.c/.h`)

Summary; the full reference is §12.

- **`OscInfo`** holds a 32-bit phase accumulator, `phaseInc` (and the slew
  fields `phaseIncTarget`, `phaseIncStep`, `slewSamplesLeft`), `freq`,
  `pitchMod`, `fmMod`, `modNodeValue`, the type (`oscType`), the selection
  (`waveform`: the `wav` row), the continuous control (`source`: the `mod`
  row), the request bytes,
  the wavetable octave (`tableOffset`), a frequency cache, sample bounds and
  a per-type union.
- **Types:** `wav` (the 11-entry wave list `Sin Tri Saw Rec Noi Cym Re3 Sw3
  ReA TrA SwA`), `pwm`, `smp`, `slp`, `wtb`. Samples are no longer part of
  the wave list (§12.2).
- **Effective frequency:** `freq · pitchMod · modNodeValue`, where `freq`
  comes from the base note plus coarse/fine tuning
  (`MidiNoteFrequencies[note] · detune`).
- **Phase formats:** sine 4,096 entries (`<<20`); tables 1,024 (`<<22`);
  the crash sample `<<17`; pwm and `wtb` a full 2^32 cycle; installed
  samples Q20.12 frames (any length up to 1,048,575 frames).
- **Wavetables:** `sawTable`, `triTable`, `recTable` are 11 band-limited
  octaves × 1,024 samples (22,528 B each, flash). `osc_setFreq()` picks the
  octave with `freqToTableIndex()`: the number of the ten edges
  `osc_octaveEdgeHz[k] = 440·2^(k − 5.75)` Hz (16.35 Hz … 8,372 Hz) at or
  below the frequency. All ten compares always run. (Before S073 this was
  `(69 + 12·log2f(f/440))/12`; the host check found 16 inputs that differ,
  each within 2 ulps of an edge.)
- **Frequency cache:** `osc_setFreq()` recomputes the target and the octave
  only when the effective frequency, waveform or type key changed; the slew
  bookkeeping runs every call. A pitch envelope changes the frequency every
  block, so budget for the recompute.
- **Noise:** `calcNoiseBlock()` reads the hardware RNG (`GetRngValue()`,
  returns `int16_t`) on each phase wrap. The RNG makes a new value about
  every 0.83 µs, so at high noise rates some samples repeat. This is the
  accepted noise character: a software PRNG was rejected in S073 because it
  would change it.
- **FM:** `calcNextOscSampleFmBlock()` phase-modulates the carrier by a
  modulator block (`calcFmBlock` for tables, `calcFmSineBlock`,
  `calcSampleOscFmBlock`, `calcStockWaveFmBlock`, and the pwm/sample/`wtb`
  FM renderers).
- **Waveform interpolation:** for `wav`-type oscillators only, an
  oscillator granted a render-pool unit blends the two neighbouring waves
  (§12.8, `OSC_INTERP_AUDIT.md`).
- **ITCM:** the block renderers, the sync renderers and `osc_setFreq()`.

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
- **Retrigger:** values 0–7: off (0), v1–v6 (1–6), scn (7,
  `LFO_RETRIGGER_SCENE`). Values 1–6 reset the phase to the offset on a
  trigger from the corresponding track. Value 7 (`scn`) preserves the LFO
  phase across a Scene change: `captureLfoPhases()` snapshots the phase
  before the old Scene's instruments are torn down, and
  `restoreLfoPhaseIfNeeded(voice)` writes it back after the new voice is
  rebuilt, provided the new LFO retrigger is still `scn`. The handoff struct
  is 28 B BSS in InstrumentManager.
- **Phase offset scaling (Session 076 fix):** the `lfo_offset` parameter
  (0–127) is now tagged `ROW_SPECIAL` and handled by `IM_SPECIAL_LFO_OFFSET`
  (value 19). The scaling is
  `phaseOffset = (uint64_t)byteValue * 0xFFFFFFFFu / 127u`, mapping 0 to
  0x00000000 and 127 to 0xFFFFFFFF. Before this fix the raw byte was written
  directly into the uint32_t `phaseOffset`, which made the offset effectively
  zero.
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
| Scene target (`instrumentManager_updateLfoSceneDestination()`) | Voice Morph: a base-independent direction + depth entry that the Morph engine resolves around the current base (S071). Effect Morph (`fxm`): the same encoding, resolved by EffectsManager. The slot-6 track-7 decay: shaped around the retained value and applied as runtime-only overrides, so the saved value never moves. The retired `srt` target (390, S075) does nothing. |
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
- **Waveform modulation** of a `wav`-type oscillator can blend neighbouring
  waves when the render pool grants it a unit (§12.8; two units per block,
  shared with `itp` crossfades); the rest step. pwm/smp/slp/wtb controls
  are continuous by design and never use the pool.

### 7.5 Step automation and Morph

- **Step automation:** TIM3 queues values; the foreground drain
  (`seq_drainPendingAutomation()`, after trigger processing in the render
  loop) writes voice targets with `instrumentManager_writeRuntime()` and
  marks them dirty. The voice's next **resetting** trigger
  (`seq_restoreAutomatedParameters()` in the trigger funnel), transport
  restart and Pattern changes restore dirty parameters from
  `morph_interpolation[]` (not from the normal image).
  Values are stored 7-bit with an identity mapping. Since S081 only a step's
  first trigger resets (a delayed hit, a roll's first hit or renewal, a live
  press), carried by the per-voice pending-reset mask; roll retriggers 2+ do
  not reset, and a deferred step's automation is queued with its delayed
  trigger (`PATTERN_DYNAMIC_STACK.md` §6.1–§6.2).
- **Automation ramps (S081 P2):** on a track with `asm` on, numeric
  descriptor parameters ramp linearly between consecutively played steps
  that both hold them; `seq_rampService()` writes the samples through the
  same runtime writer and held bit as the drain (at most 64 ramps, every
  ~2.2 ms, only on a value change). This is foreground control-rate work
  whose 64-ramp worst case still has to be measured under the constant-CPU
  rule (`PATTERN_DYNAMIC_STACK.md` §6.2b).
- **Morph:** the Morph worker (`presetMorph_tick()`, foreground, one
  parameter per pass) interpolates normal → Morph images and writes
  `morph_interpolation[]` through `instrumentManager_writeRuntime()`. An LFO
  aimed at a voice Morph re-queues a pass of every Morphable parameter every
  block, which is foreground cost.
- **Priority (S083 Q1, user): a MIDI write is a menu edit.**
  A voice-parameter automation value holds until the voice's next trigger:
  `seq_automationHoldsParameter(slot, local)` reports it, and every
  Morph-base runtime write (`presetMorph_tick()`,
  `presetMorph_applyVoiceNow()`, `presetMorph_applyParameterNow()`) goes
  through `presetMorph_writeRuntimeBase()`, which skips a held parameter
  while still updating `morph_interpolation[]`. A menu edit re-interpolates
  only its own parameter at the resolved Morph amount (it no longer queues
  the whole voice); since S081 R12 the edit also clears the held bit and sets
  a menu override (`seq_menuOverrideSet()`), so the new value sounds at once
  and that parameter's automation is suppressed until the voice's next
  trigger. External MIDI CC/NRPN goes through the same menu commit, so it has
  the same effect. Before S075 F3 a menu
  edit, a Morph change, an LFO on Morph, `Nvm` automation or a Kit apply
  reset automated parameters mid-note.
- **LFO:** step automation and Morph set the base; the LFO modulates around
  it. Details and the Scene-target rules: `BANK_PRESET_ARCHITECTURE.md` and
  `PATTERN_DYNAMIC_STACK.md` §6.2a.

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

The S085 oscillator figures (every type, FM, hard-sync slave, section
loops) and the derived per-voice worst cases are in §12.11; they supersede
the oscillator rows below.

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
| User sample (non-looped to looped paths) | about 28–49 (retired in S085; see §12.11 for `smp`/`slp`) | 0 |
| Transient sample | 22 | 0 |
| Saturating buffer add | about 4–5 (two samples per step) | 0 |
| ZDF filter (per type) | 85–104 (was 73–89) | 2 (was 5) |
| Naive 2-pole filter | about 40–60 | 1 |
| Drum post-chain | 35 since S085 P2 (velocity ramp; S073: 32, was 38 in three passes) | 1 |
| Snare post-chain | 35 since S085 P2 (amp ramp; S073: 30, was 35) | 1 |
| Cymbal/HiHat post-chain | 30 since S085 P2 (amp ramp; S073: 25, was 28) | 1 |

### 8.2 Per-voice estimate (worst case, per sample)

| Engine | Composition | About |
|---|---|---:|
| Drum (FM mode) | mod osc 24 + FM carrier 35 + transient 22 + add 5 + filter 95 + post 35 | 218 instructions, 3 divisions |
| Snare | noise 13 + filter 95 + transient 22 + add 5 + osc 24 + post 35 | 195 instructions, 3 divisions |
| Cymbal | 2 mod osc 48 + add 5 + FM carrier 35 + filter 95 + transient 22 + post 30 | 235 instructions, 3 divisions |
| HiHat | as Cymbal | 235 instructions, 3 divisions |

The post figures are the S085 P2 ramped chains (+3 Drum, +5 the others).

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
- Oscillator: see §12.13 (add a `wav` wave, add a type, add a pwm
  family). Append wave numbers after `SWA` (10); stored `wav` values are
  kept as numbers, so inserting renumbers saved kits.
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
- **Retrigger phase:** depends on the run mode (§12.6). In `rst`, Drum resets
  its filter and carrier phase (the phase only when the amp envelope is
  closed), Cymbal resets its carrier and both modulators, Snare and HiHat
  reset the carrier phase; in `fre` nothing resets; in `itp` the carrier is
  snapshotted and restarted and crossfades.
- **Request bytes vs live values (§12.10).** `waveform`/`source` are the
  clamped live values; `waveRequest`/`sourceRequest` hold what the user set.
  A writer that skips the request (any retained write path) loses values on
  a type round trip; an LFO overlay that writes it pollutes them.
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
- Session 074: no engine change; the bus-compressor sidechain tap in the
  trigger funnel (`074_SESSION_HANDOFF_LOG.md` §8).
- Session 075: no engine change; global `srt` retired (target 390 a
  placeholder); runtime-write priority fixed (§4.3, §7.5;
  `075_SESSION_HANDOFF_LOG.md` §11).
- Session 085: oscillator types, stock waves, per-sample slew, run modes,
  Drum `snc`/`rng`, the render pool, sample-flash playback, constant-cost
  hard sync (§12; `085_SESSION_HANDOFF_LOG.md`).

---

## 12. The oscillator section (Session 085)

**What this covers:** what a voice's oscillators can be, how each type
renders, what happens on a trigger in each run mode, how the Drum `mod`
modes combine two oscillators, how extra renders are budgeted, what
everything costs, how it is tested, and how to extend it.

**Where the code is:**

| File | What it owns |
|---|---|
| `Core/DSPAudio/Oscillator.h/.c` | `OscInfo`, the wave list, the type dispatch, the slew, the `wav` renderers, `osc_setFreq()`, the sync dispatch |
| `OscillatorPwm.h/.c` | the pwm families |
| `OscillatorSample.h/.c` | `smp`/`slp` |
| `OscillatorWavetable.h/.c` | `wtb` |
| `StockWaves.h/.c` | stock-wave table access and the fallback |
| `OscRenderPool.h/.c` | the 2-unit budget and the scratch block |
| `OscSection.h/.c` | run modes, `itp`, mod modes, `snc`, filter slew |
| `OscSyncLane.h` | the hard-sync reset contract |
| `Core/DSP/Instruments/InstrumentManager.c` | writers, maxima, domains, claims |
| `InstrumentCellFormat.c` | menu display |
| `Core/SampleRom/` | the stock-wave generator and the catalogue |

### 12.1 Rows a user sees

| Row (key) | Values | Meaning |
|---|---|---|
| `typ` (`oscN_type`) | 0 `wav`, 1 `pwm`, 2 `smp`, 3 `slp`, 4 `wtb` | the oscillator type |
| `wav` (`oscN_wave`) | depends on `typ` | the selection. `wav`: wave 0–10; `pwm`: family 0–5 (`Rec Saw Tri Re3 Sw3 Swp`); `smp`/`slp`: sample 0..count−1; `wtb`: wavetable 0..count−1. The single-parameter view shows a sample's or table's 8-character name on the left. |
| `mod` (`oscN_source`) | 0–127 | the type's continuous control. `pwm`: width; `smp`/`slp`: start; `wtb`: table position; `wav` ignores it. Always a plain number; the single-parameter view shows it once, on the left. Morphable, and an LFO/velocity target. |
| `run` (`osc_run_mode`) | 0 `rst`, 1 `fre`, 2 `itp` | one per voice |
| Drum `mod` (`osc2_mod_type`) | 0 `fm`, 1 `mix`, 2 `snc`, 3 `rng` | how the two Drum oscillators combine |

**Which oscillator is which:**
- `osc1` is the carrier (the oscillator you hear).
- Drum's `osc2` is the modulator / sync master.
- Cymbal and HiHat have two modulators, `osc2` and `osc3`.
- Snare has `osc1` and an internal noise oscillator with no type row.
- The Cymbal and HiHat modulators (`osc2`, `osc3`) cannot be `pwm`
  (`instrumentManager_oscTypeAllowed()`; a `pwm` FM oscillator there crashed
  on hardware). The writer stores a `pwm` request as `wav`, the menu steps
  over it (`wav` ↔ `smp`), and a stored `pwm` displays as `wav`. Drum's
  `osc2` and every carrier keep all five types.

**History (S085 P2 §14):** until the row swap the selection (pwm family,
sample, table) was on a selector row `src` and the continuous control on
`wav`. The keys did not change, so a kit saved before the swap with a
non-`wav` type loads with the two values exchanged (no migration, Q8).

**Stored values:**
- Stored 0 is the pre-S085 behaviour.
- The rows are appended (Drum 44, Snare 41, Cymbal/HiHat 46 descriptors),
  so old files and IDs keep their meaning; a missing key loads 0.
- Old kits' `wav` 6–10 now play the stock waves, and 11 or more clamp to
  `swa`. There is no migration.

### 12.2 The data model (`OscInfo`, `Oscillator.h`)

| Field | Role |
|---|---|
| `phase`, `phaseInc` | the accumulator and the current per-sample increment |
| `phaseIncTarget`, `phaseIncStep`, `slewSamplesLeft` | the pitch slew (§12.4) |
| `oscType` | `OSC_TYPE_*` |
| `waveform` | the live, clamped `wav` value (wave id, pwm family, sample or table index) |
| `source` | the live `mod` value 0–127 (width, start or position) |
| `waveRequest`, `sourceRequest` | the last retained bytes written to `wav`/`mod`, before clamping (§12.10); the sample/table binds re-expand `waveform` from `waveRequest` when the catalogue generation changes |
| `tableOffset` | band-limited level 0–10 for table reads |
| `freqCacheValue/Waveform/Source/Type/Valid` | the `osc_setFreq()` cache key (the type key's bit 7 = `sampleMemory_stockValid()` for pwm; `Source` because the pwm width is `source`, which velocity and the LFO write directly) |
| `sampleStart`, `sampleEnd`, `sampleCache*`, `sampleGainCode` | the bound sample (frames, data pointer, generation, gain) |
| union `pwm` (`OscPwmState`) / `wtb` (`OscWtbState`) | per-type block state; each type re-initialises its member on entry, so a type change never reads the other member's bytes |
| `waveInterpNext/Frac/Generation` | the waveform-interpolation grant (§12.8) |

`sizeof(OscInfo)` is 124 B.

**Engine structs:** each engine also holds one `OscSectionState` (140 B,
§12.6). Measured struct sizes: Drum 832 B, HiHat 780, Cymbal 772,
Snare 664, all inside the fixed 1,176 B runtime slot.

### 12.3 Dispatch and the per-type renderers

**Entry points:**
- **`calcNextOscSampleBlock(osc, buf, size, gain)`** (plain) and
  **`calcNextOscSampleFmBlock(osc, mod, buf, size, gain)`** (phase
  modulated by a block) switch on `oscType` **once per block**.
- Every type keeps its own specialised hot loop; no loop tests the type
  per sample.
- Hard sync uses `osc_renderSyncBlock()` (§12.9).

| Type | Renderer | Read | FM depth |
|---|---|---|---|
| `wav` Sin | `calcSineBlock` | 4,096-point table, linear interpolation | `calcFmSineBlock` |
| `wav` Tri/Saw/Rec | `calcWavetableOscBlock(table)` | 1,024 points × level, linear | `calcFmBlock` (mod × fmMod << 19) |
| `wav` Noi | `calcNoiseBlock` | a new `GetRngValue()` on each phase wrap | (plain in FM too) |
| `wav` Cym | `calcSampleOscBlock` | 8-bit crash sample, `phase >> 17` | `calcSampleOscFmBlock` |
| `wav` Re3…SwA | `calcStockWaveBlock` | stock level (§12.3.1), linear | `calcStockWaveFmBlock` (<< 19) |
| `pwm` | `oscPwm_render` | §12.3.2 | `oscPwm_renderFm` (<< 19) |
| `smp`/`slp` | `oscSample_render` | §12.3.3 | `oscSample_renderFm` (<< 9) |
| `wtb` | `oscWavetable_render` | §12.3.4 | `oscWavetable_renderFm` (<< 19) |

**Edge cases:**
- A `wav` value outside the list renders silence.
- `wav`-type oscillators can blend two neighbouring waves when the pool
  grants a unit (§12.8).

#### 12.3.1 Stock waves `Re3 Sw3 ReA TrA SwA`

The menu names (S085 P2 §14) map to the code constants `SQ3 SW3 SQA TRA
SWA` (wav values 6–10).

- **Character:** five analog-style waves (303 square and saw, a squarer
  square, a "dirty" triangle, a saw).
- **Where they come from:** generated **at install** into the first
  61,120 B of sample flash by `StockWaveGen.c`, from additive recipes in
  `StockWaveRecipes.c`.
  - The recipes are made by `tools/gen_stock_recipes.py` from
    `tools/plots/stock_wave_harmonics.csv`.
  - The device and the script sum in the same float32 order; `stock_gen`
    checks every point within ±1 LSB.
- **Level geometry:** 11 band-limited levels.
  - Levels 0–4 have 1,024 points; levels 5–10 have 512, 256, 128, 64, 16
    and 16.
  - Harmonics per level: 511, 511, 337, 168, 84, 42, 21, 10, 5, 2, 1.
  - 6,112 points per wave.
- **Lookup:** `stockWave_level(wave, level)` returns `{tab, shift, mask}`.
  The renderer reads `index = phase >> shift`.
- **Fallback:** before the first install (`sampleMemory_stockValid()` = 0)
  the same call returns program-flash tables: `Sw3`/`SwA` → Saw,
  `TrA` → Tri, `Re3`/`ReA` → Rec. The boot shows "No samples" /
  "load samples" in that state.

#### 12.3.2 pwm families (`OscillatorPwm.c`)

- **What is stored:** nothing. Each family is computed from the saw/tri
  tables or the stock block.
- **Per block:** `oscPwm_prepare()` runs on a frequency-cache miss. It
  turns c = `mod`/127 (`source`) into the width state (`OscPwmState`: `shift`,
  `split`, `scale`, `warpA`, `warpB`, `mix`, `seg`), with up to three
  divisions. It picks the table level at frequency × the warp compression,
  so a narrowed segment stays band-limited.

The family is the `wav` row (`waveform`). Menu names (S085 P2 §14; the
code constants keep the old spelling): `Rec` = `OSC_PWM_PRC`, `Saw` =
`PSW`, `Tri` = `PTR`, `Re3` = `PQ3`, `Sw3` = `PW3`, `Swp` = `SWP`.

| Family | Output | Width w as c goes 0 → 1 |
|---|---|---|
| `prc` | (saw(p) − saw(p + w)) · 1/(2·max(w, 1−w)): a pulse; at c = 0 it equals `Rec` (correlation > 0.99) | 0.5 → 0.04 |
| `psw` | (saw(p) + saw(p + w)) · 0.5/(1 − w) | 0 → 0.5 |
| `ptr` | triangle read through a two-slope phase warp meeting at w | 0.5 → 0.03 |
| `pq3` | `sq3` through the warp; natural split s0 = 0.78 (0.5 before an install) | s0 → 0.12 |
| `pw3` | `sw3` through the warp; s0 = 0.42 (0.5 before) | s0 → 0.04 |
| `swp` | four segments by ⌊4c⌋: sine→tri, tri→saw, saw→square (two reads + blend), then a narrowing `prc` | blend = frac(4c) |

- **Warp:** maps [0, w) onto [0, s0) with slope s0/w, and [w, 1) onto
  [s0, 1); w is clamped to [0.02, 0.98].
- **Interpolation:** reads interpolate with a 22-bit fraction (×
  2.384185791015625e−07). Whole-cycle positions use 2^−32.
- **Measured width:** `prc`'s positive fraction rises from 50 % to 96 % as
  c goes 0 → 1 (host test).
- **Reference model:** `tools/plots/plot_pwm.py` (`pwm_families.png`).

#### 12.3.3 `smp` and `slp` (`OscillatorSample.c`)

- **Phase:** Q20.12 frames, `OSC_SAMPLE_PHASE_SHIFT` 12, so up to
  1,048,575 frames play fully. The increment is `f · 2^22 / FS` (1,024 · f
  / FS frames per sample).
- **`oscSample_bind()`** runs every block (a generation compare when
  nothing changed).
  - The sample index is `min(waveform, count − 1)` (the `wav` row). When
    the catalogue generation changed, `waveform` is first re-expanded from
    `waveRequest`, so a smaller reinstall does not overwrite the stored
    choice.
  - `sampleStart = mod · frames / 128` (`source`, linear);
    `sampleEnd = frames`.
  - The gain code comes from the catalogue.
- **Per sample:** clamp the read index to start; interpolate to the next
  frame.
  - **`slp`:** wraps reads and phase into `[start, end)`; the frame after
    the last is `start` (the loop seam).
  - **`smp`:** holds at `end − 1` and outputs **0** there and afterwards (a
    select, so the cost does not change).
- **Gain:** `sampleMemory_gainFromCode(code)` = 2^(code/24), applied once
  per block.
- **Trigger:** a resetting trigger calls `oscSample_restart()`
  (phase = start).
- **End test:** `oscSample_isFinished()` (an `smp` read index has reached
  `end − 1`) is the `snc` "master ended" test.

#### 12.3.4 `wtb` (`OscillatorWavetable.c`)

- **Format:** a wavetable is `frame_count` (≤ 128) frames of `frame_len`
  points, stored 8- or 16-bit.
- **Rows:** `wav` selects the table (`min(waveform, count − 1)`, re-expanded
  from `waveRequest` on a catalogue change, as for samples); `mod`
  (`source`) is the frame position, target = `mod · (n − 1) / 127`.
- **Looping:** each frame is one cycle; the phase wraps at 2^32 and the
  point after the last reads point 0 (`nx`), so a note plays the cycle
  continuously. `wtb` has no end test.
- **The frame pair** (design decision D1):
  - Once per block the renderer picks two neighbouring frames `fa`, `fb`
    between the current position and the target.
  - It moves the position at most to the next frame boundary and ramps the
    blend `ff` across the block (one float divide per block).
- **Per sample:**
  - `m = phase · frame_len` (64-bit); `idx = m >> 32`;
    `nx = idx + 1`, wrapping to 0 at `frame_len`.
  - Two points from each frame, two blends. 8-bit points are `int8 << 8`.
- **Cost of the design:** a fast position sweep glides at one frame per
  block (≈ 1,378 frames/s) instead of jumping. It keeps the worst case at
  43 instructions per sample (FM 48) instead of 64/70.
- **Hygiene (F11):** entering `wtb` clears `wtb.base` and `primed`, so stale
  pwm bytes in the union are never trusted.

### 12.4 Per-sample pitch slew

**In the renderers:** every block renderer advances a local increment and
adds `phaseIncStep` to it on every sample, then writes it back. The add
always runs; in `rst` the step is 0, so the output equals the pre-S085
loop (`osc_s0`).

**Per block, `osc_setFreq()`:**
1. Recompute the target for the type, cached on (frequency, waveform,
   source, type key).
2. With no slew armed: `phaseInc = target`, step 0.
3. With a slew armed (`osc_armSlew()` sets `slewSamplesLeft` = 132):
   step = (target − phaseInc) / **max(left, 32)**, and `left` falls by 32.
   - The divisor never goes below the block length: the renderer applies
     the step on all 32 samples. (Dividing by the 4 samples left in the
     last block overshot the target by 20.5 % of the interval; assessment
     A1.)
   - The glide is linear for 128 samples, then the last 3 % arrives over a
     fifth block. It never passes the target.

### 12.5 Frequency per type (`osc_setFreq()`)

| Type | Target increment | Level |
|---|---|---|
| `wav` Sin, Noi | `freq2PhaseIncr()` (4,096-point) | — |
| `wav` tables and stock | `freq2PhaseIncr1024()` | `freqToTableIndex(f)` |
| `wav` Cym | `freq2PhaseIncr32767()` | — |
| `pwm` | from `oscPwm_prepare()` (2^32 per cycle) | at f × compression |
| `smp`/`slp` | `oscSample_phaseIncForFreq()` | — |
| `wtb` | `freq2PhaseIncr1024()` (2^32 per cycle) | — |

### 12.6 Run modes and the trigger (`OscSection.c`)

**`OscSectionState`:**

| Field | Meaning |
|---|---|
| `runMode` | `rst` / `fre` / `itp` |
| `itpState` | IDLE / PENDING / ACTIVE |
| `itpPos` | the crossfade position |
| `itpOld` | one `OscInfo` copy of the carrier |
| `fSmooth`, `qSmooth`, `qTarget` | filter slew state |

**`oscSection_beginTrigger(section, carrier, mods[], n)`** returns what the
engine resets:

| Mode | Returns | Oscillators | Filter |
|---|---|---|---|
| `rst` | `OSC_TRIGGER_RESET_ALL` | the engine's usual resets (see §5 and §10) | Drum clears it (`SVF_reset`) |
| `fre` | `OSC_TRIGGER_RESET_NONE` | none; every oscillator's slew is armed | keeps ringing |
| `itp` | `OSC_TRIGGER_RESET_CARRIER` | carrier snapshotted into `itpOld` and restarted; the modulators slew | keeps ringing |

**Phase offsets on reset:**
- `smp`/`slp` → `oscSample_restart()`;
- pwm, `wtb` and table waves → `(0xff << 20) × offset`;
- sine → its quarter offset;
- else 0.

`offset` is 1, or `1 − transient volume` in the transient offset mode.

**Why `itp` crossfades only the carrier:** a click is an amplitude step,
and the section has three sources:
1. The carrier reset. This is what `itp` hides.
2. A modulator reset in `mix`. A direct step.
3. A modulator reset in FM. A frequency kink, not a step.

`itp` never resets modulators. It renders the old carrier copy against the
**same live modulator block** as the new carrier, so the fading copy
carries exactly the FM tail of the un-retriggered sound. The crossfade is
linear over `OSC_ITP_SAMPLES` (132). One pool unit per voice per block.

**Lifecycle:**
1. A trigger sets PENDING (or keeps ACTIVE).
2. At the next block start the mixer claims a unit (§12.8). Granted:
   ACTIVE. Refused: the carrier's phase is restored from the snapshot and
   its slew armed, so the trigger behaves as `fre`.
3. The engine renders the old copy into `oscRenderPool_scratch()`, then
   calls `oscSection_blendItp()` and `oscSection_finishBlock()`. After 132
   samples the state returns to IDLE.

**Accepted:**
- A retrigger during an ACTIVE fade re-snapshots the live carrier, so the
  output steps by (1 − t)·(old − new). It needs triggers less than 3 ms
  apart.
- In `itp`, Cymbal's FM attack differs per hit, because its modulators are
  not reset.

**Filter slew** (`oscSection_filterRecalc()`, replacing `SVF_recalcFreq()`
in `instrumentManager_recalcSlotFilter()`):
- **`rst`:** exactly `SVF_recalcFreq()`.
- **`fre`/`itp`:** cutoff and resonance move toward their targets by 0.28
  per block (≈ 2 ms) through `SVF_recalcFreqFrom(filter, f)`. A resonance
  written by modulation since the last block becomes the new target
  (`oscSection_noteResoTarget()` from the resonance writer).

#### 12.6.1 Amp envelope on a `fre`/`itp` retrigger (S085 P2)

The run modes keep the oscillators and the filter running through a
retrigger. Before P2 every engine still restarted its amp envelope with
`slopeEg2_trigger()`, which keeps the raw value v but switches from the
decay curve `f(v, slope)` to the attack curve `f(v, invSlope)`, with
`f(v,k) = (1+k)v/(1+k|v|)`. The gain jumps when the envelope enters attack.
At v = 0.5 (halfway down the decay):

| Slope | Gain before | Gain after | Step |
|---:|---:|---:|---:|
| 0 | ≈ 0.00 | ≈ 1.00 | +1.0 |
| 32 | 0.25 | 0.75 | +0.50 |
| 64 | 0.50 | 0.50 | ≈ 0 |
| 96 | 0.76 | 0.24 | −0.51 |
| 127 | ≈ 1.00 | ≈ 0.00 | −1.0 |

Drum ramped the step over one block, except at attack 0, where it was not
ramped at all. Snare, Cymbal and HiHat apply one constant gain per block,
so the step landed on one sample. A new velocity (`volumeMod` on) stepped
the level too.

**The rule (user, 2026-10-27):**
- **`rst` is unchanged:** `slopeEg2_trigger()`, and every ramp runs flat,
  so the output is bit-identical (`postchain`).
- **`fre`/`itp`:** the attack starts at the gain the voice is producing.
  When that gain is audible, the next block ramps from the previous block's
  gain (and velocity) to the new one.

**Gain match** (`slopeEg2_retriggerFrom(eg, g)`, `SlopeEg2.c`): sets
`v = g / (1 + k(1 − g))`, k = `invSlope`, so `f(v, invSlope) = g`.
- The denominator is ≥ 1 + `invSlope` ≥ 0.00025, because
  `slopeEg2_setSlope()` caps the amount at ±0.9995.
- g and v are clamped to [0, 1].
- **Repeat mode:** `repeat > 0` keeps `slopeEg2_trigger()` (value 1,
  `EG_REPEAT`); the armed ramp covers the jump.
- **Fully decayed:** g ≈ 0 gives v ≈ 0, a fresh attack.
- **Precision:** within 1e-5, except near slope 127, where the attack curve
  is so steep near v = 1 that the nearest float v misses g by up to ~1e-4
  (−80 dB).

**Audible test** (`slopeEg2_isAudibleGain()`): `gain ≥ 1/32768`
(`SLOPEEG2_AUDIBLE_GAIN`, one output step), on the gain the engine applied
to its last block: Drum `ampFilterInput`, the others `egValueOscVol`.
`instrumentManager_ampEnvelopeQuiet()` is not used: it tests the raw value,
and at slope 127 a raw 0.0001 is still a gain of about 0.29.

**Arm lifecycle** (`ampRampArm`, one byte per engine):
1. The `fre`/`itp` trigger sets it when the gain is audible.
2. Drum's async (`Drum_calcVoiceAsync()`) reads it: an armed block always
   ramps the envelope gain, even at attack 0.
3. The render passes the stored "last" value as the ramp start
   (`lastVeloGain` for Drum, `lastAmpGain` for the others), records the
   gain just applied, and clears the arm.

Every other block passes equal endpoints. The ramp arithmetic runs every
sample of every block in every mode (constant cost). Only its endpoints
differ.

**Still restarts on a `fre`/`itp` retrigger, by design:**
- the transient sample (the intended attack sound);
- the pitch envelope (Drum, Snare) and the snap envelope. These change
  frequency, not amplitude: in `fre` the oscillator slew glides the jump
  over 160 samples; in `itp` the modulators glide and the carrier's jump
  is inside the crossfade.

**State:** Drum `float lastVeloGain; uint8_t ampRampArm;`; Snare, Cymbal,
HiHat `float lastAmpGain; uint8_t ampRampArm;`. Inside the 1,176 B runtime
slot (`STORAGE_SRAM_MANIFEST.md` "S085").

**Tests:** `ampretrig` (gain match for every slope, repeat fallback, the
audible threshold, ramp wiring, no step in a Snare-style sequence);
`postchain` (flat ramps = the S073 loops); breaks B27–B32.

**Drum FM depth on a `fre` retrigger (S085 P2 F-A):**
- **The problem:** Drum's FM depth is `fmMod = fmModAmount × pitch
  envelope` (`Drum_calcVoiceAsync()`), and `DecayEg_trigger()` restarts the
  pitch envelope at 1.0. The depth therefore jumps at every retrigger. The
  Drum's FM is phase modulation (the carrier reads at
  `phase + (mod × fmMod) << 17/19`), so the jump moves the read phase by
  `mod × Δ fmMod`: a click whose size depends on the modulator's value at
  the trigger (sporadic). The pitch slew does not cover it, because it
  glides increments, not depth.
- **The fix:** on the armed block (`ampRampArm`), `Drum_calcVoiceSyncBlock()`
  passes `fmRampFrom = lastFmMod / fmMod` (clamped to 0..1; 1 on every
  other block) to `oscSection_renderDrumRamped()`. In fm mode
  `oscSection_rampModDepth()` scales the modulator block from `fmRampFrom`
  to 1 across the block before the carrier reads it, so the effective depth
  glides from the old value to the new over one block.
- **Flat cases:**
  - With an `itp` crossfade active the ramp is forced flat: the old carrier
    copy keeps its own old `fmMod` and must see the unscaled modulator, and
    the crossfade already hides the new carrier's jump.
  - `mix`, `snc` and `rng` do not use `fmMod`.
  - A ramp from 1 multiplies by exactly 1.0 (bit-identical; `osc_s0` runs
    this path).
- **Limit:** the clamp keeps the scaled modulator from exceeding its
  rendered value. A retrigger whose new depth is *below* the old one (a
  lowered FM amount) keeps that part of the step.
- **Cost:** 15 instructions per sample in Drum fm mode, every block
  (`p_fm_ramp`). Drum fm becomes 135 per sample, still below Drum `snc`
  (162) and Cymbal/HiHat (178).
- **State:** `float lastFmMod`. Drum's unused `egValueOscVol` field was
  removed to pay for it, so the struct is 840 B (+8 for all of P2).
- **Tests:** `osc_run` "fm depth ramp": exact wiring, flat = unramped,
  forced flat under `itp`, and a phase sweep where the worst boundary step
  falls from 65,311 to 294 (never above the block's own largest step +
  3 LSB). Breaks B33–B35.
- **On hardware** (pre-existing, unchanged): the FM conversion
  `(uint32_t)(mod × fmMod)` compiles to `VCVT.U32`, which saturates
  negative products to 0. The carrier is therefore phase-modulated by the
  positive half of the modulator only. The host build wraps instead.

### 12.7 Combining oscillators

**`oscSection_renderDrum(car, mod, mode, amt, buf, modBuf, n, oldCar, oldBuf)`:**

| Mode | Computation |
|---|---|
| `fm` | `mod` at gain amt → `modBuf`; `car` FM by `modBuf` → `buf`. Bit-identical to pre-S085. |
| `mix` | `mod` at amt; `car` at 1 − amt; saturating sum. Bit-identical to pre-S085. |
| `snc` | §12.9 |
| `rng` | `mod` and `car` at gain 1; `out = car·(1 − amt) + ((car·mod) >> 15)·amt`, saturated |

In every mode an `itp` copy (`oldCar`) is rendered with the same modulator
block.

**`oscSection_renderFm2(car, m1, m2, g1, g2, carGain, …)`** (Cymbal, HiHat):
- the two modulators at their gains, a saturating sum;
- the carrier FM at `carGain` (Cymbal 1.0, HiHat 0.5);
- the `itp` copy with the same sum.

`osc_s0` proves `fm`, `mix` and FM2 bit-identical to the frozen pre-S085
engine lines.

### 12.8 The render pool (`OscRenderPool.c`)

**The budget:**
- **Units:** `OSC_RENDER_POOL_UNITS` = 2 per 32-sample block, system-wide.
  One unit = one extra oscillator render (plus its blend): an `itp` old
  carrier, or the second wave of a `wav` interpolation pair.
- **Budgeted full:** the worst case assumes both units always taken by the
  dearest render.

**Claims, in mixer order** (`EFFECTS_MIXER_DSP_REFERENCE.md`):
1. `oscRenderPool_beginBlock()`.
2. Ongoing `itp` crossfades, lowest slot first. They are never cut unless
   the pool is full.
3. Waveform interpolation claims, while velocity and LFO modulation are
   applied (`modulationNode.c`; `wav` type only).
4. New `itp` crossfades, lowest slot first; refused ones fall back to `fre`.

**Scratch:** `oscRenderPool_scratch()` is one 32-sample block. Voices
render one at a time, so one block serves both units.

### 12.9 Hard sync (`snc`) at constant cost

**Roles:** the modulator is the master and the carrier the slave. At each
master wrap the slave restarts at the master's overshoot scaled by
`ratio` = slave increment / master increment (once per block).

**Steps:**
1. **Lane.** `oscSection_buildSyncLane()` walks a copy of the master phase
   before the master renders and records, for each sample *i*, `wrap[i]`
   (the master wrapped between *i* and *i* + 1) and `over[i]` (the
   overshoot).
   - A periodic master wraps at 2^32.
   - A sample master's cycle is `(end − start) · 4096`, measured from its
     start. The modulo runs every sample.
   - An `smp` master that has ended records no wraps, so the slave runs
     free.
2. **Master.** The master renders once, at unit gain, into `modBuf`.
3. **Slave.** `osc_renderSyncBlock(slave, buf, n, 1.0, &lane)` runs the
   slave's normal loop. After producing sample *i* it replaces the phase
   with `base + over[i]·ratio` when `wrap[i]` is set (`oscSync_phase()`):
   - the reset lands on sample *i* + 1, so the slave renders through the
     wrap sample (D13);
   - `base` is `start << 12` for an `smp`/`slp` slave, else 0;
   - a synced one-shot slave replays from its start on each wrap.
4. **Copy.** The `itp` copy runs the same lane with its own ratio.
5. **Mix:** `slave·(1 − amt) + master·amt`, saturated.

**Why this shape:** the cost per sample is the same for 0 wraps or 32. The
first S085 form called the renderer once per wrap. A sample master at a
high note wraps on **every** sample, so the per-call setup (dispatch,
bind, frame pair) landed on every sample (assessment A2).

**Code layout (ITCM matters):**
- **wav loops** are always-inline cores that take a lane pointer
  (`calcSineCore`, `calcWavetableOscCore`, `calcNoiseCore`,
  `calcSampleOscCore`, `calcStockWaveCore`).
  - The plain functions pass a literal NULL, so their loops are unchanged.
  - `osc_renderSyncBlock()` holds the sync copies.
- **pwm, smp/slp and wtb** run one shared loop each for plain and sync
  (`oscPwm_blockLane()`, `oscSample_blockLane()`,
  `oscWavetable_blockLane()`).
  - The loop is marked `OSC_SHARED_LOOP` (`noinline`, `noclone`, no loop
    unswitching) and tests the lane every sample (+2 instructions on the
    plain render).
  - Separate copies of everything cost 5.6 KB of ITCM.
  - **Do not remove these attributes:** GCC otherwise clones the loop per
    lane value.

### 12.10 Values, requests and ranges (`InstrumentManager.c`)

**Special writers:**
- **`IM_SPECIAL_OSC_TYPE` (20):** sets the type.
  - A type the oscillator may not take (`pwm` on a Cymbal/HiHat modulator,
    `instrumentManager_oscTypeAllowed()`) is written as `wav`.
  - It re-derives `waveform` from `waveRequest` and `source` from
    `sourceRequest`, clamped to the new type's maxima.
  - It binds `smp`/`slp`/`wtb` and refreshes the slot's `wav` modulation
    domains.
  - Entering `wtb` clears its union state first.
- **`IM_SPECIAL_OSC_SOURCE` (21), `IM_SPECIAL_OSC_WAVE` (22):** record the
  request byte **only for retained writes**, then clamp the live value.
  - Retained writes are menu, MIDI, file load, Morph, step automation and
    LFO restore of a base. LFO overlays are not.
  - Because of this, a sample choice of 100 survives smp → wav → smp, and an
    LFO can never become the stored value.
  - `OSC_WAVE` (`wav`) rebinds `smp`/`slp`/`wtb` (the selection moved);
    `OSC_SOURCE` (`mod`) rebinds `smp`/`slp` (the start moved).
- **`IM_SPECIAL_RUN_MODE` (23):** sets `runMode`.

**Maxima:**
- `wav`: 10 for the `wav` type, 5 for pwm, count − 1 for samples and
  tables (0 when none is installed).
- `mod`: 127 for every type.

**Per-slot domains:** the clamp, the velocity range, the LFO adapter
domain and base, and the menu's automation maximum all use the per-slot
maximum (`instrumentManager_descriptorModDomainForSlot()`). A type change
re-derives each LFO adapter's base from `waveRequest` (assessment A9).

**Storage** (`BANK_PRESET_ARCHITECTURE.md`): `typ` and `run` are
automatable but not morphable. `wav` and `mod` are ordinary image rows
(Morphable, LFO/velocity targets); Morphing `wav` under `smp`/`wtb` steps
through the installed samples or tables between the two endpoints. They are stored single-ended and applied
before the Morph image on every slot reset (`preset_applySelectorRows()`).

### 12.11 Cost (worst-case instructions per sample)

- **Measured** by `make -C tools/dsp_test armcheck-osc` (`osccost.py`: the
  longest path through each loop; `armcheck/probe_*.c` fix the block
  constants).
- **Counts:** firmware pwm/smp/wtb plain = the loop + 2 (the lane test).
  Calls count 1.
- **Not included:** sample-flash wait states, which apply to the stock,
  `smp` and `wtb` reads.

| Loop | pre-S085 | plain | FM | `snc` slave |
|---|---:|---:|---:|---:|
| Tri/Saw/Rec | 24 | 25 | 36 | 35 |
| Sin | 23 | 24 | 31 | 34 |
| Noi | 16 + call | 16 + call | — | 20 + call |
| Cym | 30 | 31 | 39 | 37 |
| stock | — | 24 | 34 | 35 |
| pwm `prc`/`psw` | — | 51 | 56 | 63 |
| pwm `ptr`/`pq3`/`pw3` | — | 42 | 47 | 53 |
| pwm `swp` seg 0/1/2/3 | — | 48/50/58/52 | 53/55/62/57 | 60/63/70/65 |
| `smp` | — | 40 | 45 | 53 |
| `slp` | — | 47 | 53 | 61 |
| `wtb` 8/16-bit | — | 40/43 | 44/48 | 51/54 |

**Section loops:**

| Loop | Worst |
|---|---:|
| `snc` lane, full-wrap master | 11 |
| `snc` lane, sample master | 18 (includes a UDIV) |
| `snc` mix pass / with the `itp` copy | 16 / 28 |
| `rng` law | 19 |
| `itp` blend | 23 |

**Per-voice worst cases** (oscillator section only):

| Engine / mode | Composition | Worst |
|---|---|---:|
| Drum `fm` | 58 + 62 + FM depth ramp 15 (S085 P2 F-A) | 135 |
| Drum `mix` | 58 + 58 + add | 116 + add |
| Drum `snc` | lane 18 + master 58 + slave 70 + mix 16 | **162** |
| Drum `rng` | 58 + 58 + 19 | 135 |
| Snare | 58 + 16 | 74 |
| Cymbal / HiHat | 2 × 58 + 62 + add | **178** + add |
| Pre-S085 Cymbal (comparison) | | 98 |
| Pool (system-wide) | 2 × (≤ 82 + 23) | ≤ 210 |

**Post-chains (S085 P2):** the amp/velocity ramps (§12.6.1) bring the
post-chain loops to Drum 35 (+3), Snare 35 (+5), Cymbal/HiHat 30 (+5),
outside the oscillator section above. That is at most +30 per sample over
six voice slots, about 0.6 % of the sample period, in every mode
(`armcheck-postchain`, `ramp_*`).

### 12.12 Tests

| Target | What it proves |
|---|---|
| `osc_run` (modes `run`, `pwm`, `sample`, `wtb`) | slew trajectory; `itp` click bound in fm and mix (rst and a modulator reset must exceed it); pool order; `snc` (no-wrap identity per family, reset index, `itp` copy ratio, mix law, sample master with start > 0, finished master); `rng`; sample playback, seam, gain, re-expansion; `wtb` continuity, nx wrap, 8-bit; pwm polarity, F3 midpoints, width |
| `osc_s0` | `rst` bit-identity against the frozen pre-S085 loops (incl. noise), an `osc_setFreq()` pitch sweep, and the frozen engine source lines for Drum fm/mix and Cymbal/HiHat FM2 |
| `stock_gen` | every stock point within ±1 LSB of the reference |
| `armcheck-osc`, `armcheck-osc-base` | the cost table |
| `ampretrig` (S085 P2) | the gain-matched retrigger, the audible threshold, the post-chain ramps, no step on a `fre` retrigger (§12.6.1) |
| `breakproofs` | 32 deliberate defects (B27–B32 from S085 P2), each must fail its test |

### 12.13 How to extend

**Add a `wav` wave:**
1. Append a constant after `SWA` and raise `OSC_STOCK_WAVE_COUNT`.
2. Add the `case` in `calcNextOscSampleBlock()`,
   `calcNextOscSampleFmBlock()`, the scalar dispatchers and
   `osc_renderSyncBlock()`, plus the frequency case in `osc_setFreq()` if
   it is not a 1,024-point table.
3. Add the name to `waveformNames` (the `_Static_assert` in `menu.c` checks
   the count) and `osc_isTableWave()` if it takes the table phase offset.
4. If it is generated at install, extend the stock block (the sample-flash
   map moves: `SampleMemory.h`, `STOCK_WAVE_*`, the generator, the
   reference).

**Add a type:**
1. Append `OSC_TYPE_*` and raise `OSC_TYPE_COUNT` and `oscTypeNames`.
2. Write a plain and an FM renderer with the per-sample slew. Add a sync
   form (a lane argument; use the shared-loop pattern if ITCM is tight)
   and its `osc_renderSyncBlock()` case.
3. Add a union member if the type keeps block state (`_Static_assert` the
   size), re-initialised on entry.
4. Add the `osc_setFreq()` target, the maxima in
   `instrumentManager_oscWaveMaxForType()` / `_oscSourceMaxForType()`, the
   type writer's bind call, `InstrumentCellFormat` kinds, and the menu
   names.
5. Measure with a probe (`armcheck/probe_*.c`), add host tests, and add a
   break proof for each test.

**Add a pwm family:**
1. Append `OSC_PWM_*` and raise `OSC_PWM_FAMILY_COUNT` and
   `pwmFamilyNames`.
2. Add the width rule in `oscPwm_prepare()` and the `case` in
   `oscPwm_block()`.
3. Add probe wrappers (`PWM_PLAIN(name, family, seg)`) and update the cost
   table. The dearest family sets the per-voice worst case.

**Change the pool size:**
1. Change `OSC_RENDER_POOL_UNITS`.
2. Re-derive the worst case (units × the dearest unit). A unit is a full
   extra carrier render, not a fraction.
