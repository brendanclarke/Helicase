# Instruments DSP Reference

How the six instrument voices are rendered, how their parameters reach the
DSP, how modulation (LFO, velocity, Morph, step automation) works, what each
part costs, and how to change or extend it.

- **Current as of:** Session 087 close (2026-10-29). S087 added the
  oscillator type system (`osc1_type`: `wav`/`smp`/`slp`/`wtb`, §6.1),
  sample flash catalogue v2 and installer, Q20.12 sample and wavetable
  readers with `pos` on samples (§6.1.1), the step-automation order fix
  (§2, §7.5), the modulator `wav` list expansion (§6.1.3), one-shot
  sample mod oscillators, the transient click sample list (§6.4),
  display/naming corrections, and v1 sample loop removal.
  Earlier: S085 added the Drum mod modes `snc`/`rng` (§5.1.1). S086
  added the stock table waves `Re3`/`Sw3` (§6.1.2) and the oscillator
  position row `pos` (§6.1.1), and moved the user samples to `wav` 8+.
  S076 added LFO retrigger value 7 (`scn`) and fixed the LFO phase
  offset scaling (§7.1). S075 fixed runtime-write priority (§4.3, §7.5).
  S074 added the sidechain tap (§2). S083 Q1 made a MIDI write a menu
  edit.
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
  runs `voiceControl_processPending()`, which internally handles triggers
  and step automation in the correct order (S087 D6, see below). Each call runs
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

**Trigger / automation order (S087 D6).** Before S087, `main.c` ran
`voiceControl_processPending()` (triggers) and then
`seq_drainPendingAutomation()` as two separate calls. A step's own automation
landed one block after its trigger — inaudible for phase-based parameters,
but wrong for per-step sample starts (slicing). S087 merged both into
`voiceControl_processPending()` with this order per render pass:

1. Pop every pending trigger + snapshot the automation count, under one
   PRIMASK section
2. Prepare each popped trigger (deferred Scene apply, restore when reset)
3. `seq_drainPendingAutomationTo(mark)` — drain automation up to the mark
4. Fire each popped trigger in pop order

Entries queued after the mark wait one block with their own triggers. The
`seq_drainPendingAutomation()` call was removed from `main.c`.

**Sidechain tap (S074).** `voiceControl_fireTrigger()` (split from the
former `voiceControl_triggerNow()` in S087), the trigger funnel for the
sequencer, rolls, MIDI and front-panel previews, calls
`busComp_sidechainTrigger(track, velocity)`. The master bus compressor
records the largest `(velocity/127)³` weight for the track selected by the
active Scene's `csc` and applies it in the next block
(`EFFECTS_MIXER_DSP_REFERENCE.md` §5A). Track 6 (visible track 7) counts as
voice 6, a working assumption not yet confirmed on hardware (BC11). The
tap is foreground-only, like the rest of the funnel.

---

## 3. Runtime ownership

### 3.1 Tagged runtime slots

- `InstrumentManager.c` owns `runtime_slots[6]`, a union of the four engine
  structures, 1,192 B per slot (twice the largest engine, `DrumVoice`,
  596 B since S085), 7,152 B in SRAM1. The reserve follows
  `sizeof(DrumVoice)` automatically, so every byte added to `DrumVoice`
  costs 12 B of SRAM (2 × 6 slots).
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
modMode fm  (0): modOsc --(gain fmModAmount)--> modBuf; osc phase-modulated by modBuf      --> buf
modMode mix (1): modOsc --(gain fmModAmount)--> modBuf; osc (gain 1−fmModAmount) + modBuf  --> buf
modMode rng (3): d = fmModAmount·osc.fmMod (= amt²·env), Q15; modOsc --(gain 1)--> modBuf;
                 osc ring copy: osc·G>>15, G = (32768−d_i) + (d_i·modBuf>>15), d_i ramped
                 per sample from the last block's d (snapped after a trigger)    --> buf
modMode snc (2): modOsc not rendered; syncClock at the FM-page frequency --> reset list;
                 osc sync copy, gain 1, depth d = amt²·env                                    --> buf
transient (sample 2.., or zeros for snap/offset modes)           --> + buf, saturating
ZDF filter (filterType)                                          --> buf
fused post-chain: amp EG ramp → velocity gain → distortion       --> buf (pre-volume)
```

- **Control rate** (`Drum_calcVoiceAsync`): pitch envelope
  `pitchMod = 1 + DecayEg·egPitchModAmount`; in transient mode 0 the snap
  envelope adds `snap·transientVolume` to the pitch; the mod envelope
  `osc.fmMod = fmModAmount·egPitch` is set in every mode (used by `fm` as
  its FM depth and by `snc`/`rng` through `Drum_modEnvDepth()`, §5.1.1);
  the amp envelope advances
  (`slopeEg2_calc`), keeping `lastGain` and the new value for the per-sample
  ramp; `osc_setFreq()` for both oscillators.
- **Trigger:** retriggers LFOs whose retrigger track matches; applies velocity
  modulation; resets the oscillator phase (with the transient offset mode),
  then adds the `pos` offset (`osc_positionTrigger()`, §6.1.1); triggers
  pitch and amp envelopes, transient and snap; sets `ringSnap` (§5.1.1);
  `SVF_reset()` clears filter state to avoid a wrong transient.
- **`pos` while playing (S086):** `Drum_calcVoiceSyncBlock()` calls
  `osc_positionBendApply(&osc, modMode != DRUM_MOD_SNC)` before the mod-mode
  switch and `osc_positionBendRestore(&osc)` after it, so the carrier glides
  to a changed `pos` within the block (§6.1.1). In `snc` the change is
  consumed without bending.
  Only the carrier (`osc`) phase is reset; **`modOsc` is never reset**, in
  any mode (user rule, S085 Q6b: change only after a hardware trial).
- **Velocity:** `velo = vel/127`; with `volumeMod` on it scales the output
  (the multiply always runs; with `volumeMod` off it multiplies by 1.0).
#### 5.1.1 The oscillator section: four mod modes (S085)

The FM page's `mod` row (`osc2_mod_type`, bound to `DrumVoice.modMode`,
`DTYPE_MIX_FM`) selects how the FM-page oscillator (`modOsc`, "osc2") acts
on the main oscillator (`osc`, the carrier, "osc1").

| Stored | Name | Output | `amt` (`osc2_mod_amount`, 0..1) |
|---|---|---|---|
| 0 | `fm` | carrier phase-modulated by the modulator | FM index ∝ amt² × env |
| 1 | `mix` | carrier × (1 − amt) + modulator × amt (saturating add) | the mix share (no envelope) |
| 2 | `snc` | the carrier hard-synced to a clock at the FM-page frequency; only the carrier is heard | sync depth d = amt² × env |
| 3 | `rng` | carrier × (1 − d) + carrier × modulator × d | ring amount d = amt² × env |

- **Other bytes** play and display as `fm` (`switch` default;
  `menu_getMixFmName()`). Values 0/1 kept their pre-S085 meaning, so old
  kits are unchanged; an older firmware plays 2/3 as `mix`.
- **The mod envelope** (`env`) is the PitchMod decay envelope `egPitchVal`
  (`DecayEg`, 1 at each trigger, decaying to 0 at the PitchMod `dec` rate).
  `Drum_calcVoiceAsync()` sets `osc.fmMod = amt · env` every block.
  - `fm` reads `osc.fmMod` as its depth on a modulator block already scaled
    by amt, so its index is amt² × env.
  - `snc` and `rng` use `Drum_modEnvDepth(voice)` = `fmModAmount ·
    osc.fmMod`, the same amt² × env (user rule: one response for all three).
  - The PitchMod amount (`egPitchModAmount`) scales only the pitch sweep.
  - `DecayEg_calc()` can return a slightly negative value for one block at
    the end of the decay; the depth mappings clamp it.
- **Cost rule:** no mode may cost more than the dearest `fm` setting (S085
  user rule "no increase in CPU cycles overall"; figures §8). `fm` and `mix`
  call exactly the loops they called before S085.

**Render, per block (`Drum_calcVoiceSyncBlock()`):**

```
fm  (0, default): modOsc plain (gain amt) -> modBuf; carrier FM from modBuf
mix (1):          modOsc plain (gain amt) -> modBuf; carrier plain (gain 1-amt); QADD16
snc (2):          clockInc  = osc_syncClockInc(&modOsc)
                  syncClock = osc_buildSyncList(syncClock, clockInc, syncOver[32])   (stack)
                  carrier   = calcNextOscSampleSyncBlock(&osc, syncOver, clockInc,
                                                         osc_syncDepthQ30(d))
rng (3):          target = osc_ringDepthQ15(d); start = ringSnap ? target : ringDepthQ15
                  step   = (target - start) / size;  ringSnap = 0
                  modOsc plain (gain 1) -> modBuf
                  carrier = calcNextOscSampleRingBlock(&osc, modBuf, buf, size, start, step)
                  ringDepthQ15 = start + step * size        (the value reached)
```

`transient_calcBlock()` rewrites all of `modBuf` afterwards, so its content
in `snc` does not matter.

**`snc` in detail:**
- **The master is a clock, not audio.** `modOsc` is not rendered and its
  phase is not touched.
  - The clock is `DrumVoice.syncClock`, its own `uint32_t`: zeroed at init,
    free-running, not reset on a trigger.
  - **Why not `modOsc.phase`:** a one-shot user sample on the FM page holds
    its phase at the sample end. Advancing that phase in `snc` would make a
    later `fm`/`mix` render read up to 64 KB past the sample, beyond flash
    near the top of sample flash.
- **The clock increment** (`osc_syncClockInc()`):
  - `modOsc.phaseInc` for Sin/Tri/Saw/Rec/Noi (2^32 per cycle);
  - for crash and user samples (a `<<17` point index, 2^27 per nominal
    period), `min(inc, 0x07FFFFFF) << 5`. They act as noise: one reset per
    nominal period at `frq`, not their audio.
  - So **the FM-page `wav` has no audible effect in `snc`**; only `frq`
    matters.
- **Reset list:** once per block, `osc_buildSyncList()` adds the increment
  per sample and records `over[i]`:
  - the overshoot past the 32-bit wrap when the add carried;
  - otherwise `OSC_SYNC_NONE` (the overshoot is always < the increment, so
    it never equals the marker).
- **Slaves:**
  - Sin/Tri/Saw/Rec/Crash use **sync copies** of their plain loops.
  - Noise and user samples render **unsynced** (the precedent: the FM
    carrier ignores FM on noise).
  - A waveform-interpolated slave renders both partners from the same list.
- **The reset law** (`osc_syncPull()`), after sample *i* is rendered and the
  phase advanced:
  - `target = over[i] × (slaveInc / clockInc)`: where the slave would be had
    it restarted at the wrap instant (overshoot correction);
  - `phase += depth × (int32)(target − phase)`, the short way round;
  - the reset therefore lands on sample *i* + 1;
  - depth 0 = free-running (bit-identical to the plain loop); depth 1 =
    exact hard sync.
- **Not ramped:** the depth acts only at reset instants, so a per-block
  depth step puts no step into the audio.

**`rng` in detail:**
- **The law:** G = (32768 − d) + ((d·m) >> 15); out = SSAT16((car·G) >>
  15).
  - d is the Q15 depth (0..32768); m is the modulator at gain 1.
  - d = 0 is the carrier exactly; d = 32768 is the pure ring product.
- **It is folded into per-waveform carrier copies**, replacing the plain
  loop's gain store. **Every waveform is supported.**
  - **Why not a separate pass:** measured at 7.5–9 instructions per sample,
    a pass made `rng` cost as much as `fm` or more with user samples.
- **Depth ramp:**
  - **The problem:** d follows the envelope and changes every block; stepping
    an amplitude every 32 samples zippered ("crunchy").
  - **The fix:** the ring copies move d linearly per sample, d_i = start +
    (i + 1)·step, from the value reached at the end of the previous block
    (`DrumVoice.ringDepthQ15`).
  - **Snap:** `Drum_triggerVoice()` sets `DrumVoice.ringSnap`; the first
    `rng` block after a trigger starts at its own d with no ramp, so a hit
    starts at full depth like an `fm` hit (user choice).
  - **Future (user):** the free-run run mode (S087) is to try "ramp always"
    instead.

**State added to `DrumVoice` (S085):**

| Field | Bytes |
|---|---:|
| `modMode` (was the boolean `mixOscs`) | — |
| `syncClock` | +4 |
| `ringSnap` | 0 (in the padding) |
| `ringDepthQ15` | +4 |

`sizeof(DrumVoice)` 588 → 596.

**Accepted limits:**
- In `snc`, the first reset after a hit falls at a different place each hit
  (the clock runs free).
- An LFO on the FM-page `wav` in `snc` still claims an interpolation unit
  for the unrendered `modOsc`.
- A one-shot user sample on the FM page (any mode) plays only once after
  selection or power-up, because `modOsc` is never reset.

**Record:** `085_SESSION_HANDOFF_LOG.md` (design, measurements, tests,
decisions).

### 5.2 Snare (`Snare.c`, Basic)

```
noise osc (gain 0.9)                                  --> buf
ZDF filter                                            --> buf   (the filter shapes the noise only)
transient                                             --> transBuf; buf + transBuf, saturating
tonal osc (gain 1−mix)                                --> transBuf
fused post-chain: buf·mix + transBuf (saturating) → amp gain → distortion
```

`ampGain = volumeMod ? velo·EG : EG`, chosen once per block.

**`pos` (S086):** the trigger adds `osc_positionTrigger()` to the tonal
oscillator's start phase; `Snare_calcSyncBlockVoice()` wraps the tonal
oscillator render in `osc_positionBendApply(&osc, 1)` /
`osc_positionBendRestore()`. The noise oscillator is not affected.

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

**`pos` in Cymbal and HiHat (S086):** the trigger adds
`osc_positionTrigger()` to the carrier's start phase, and the carrier's FM
render is wrapped in `osc_positionBendApply(&osc, 1)` /
`osc_positionBendRestore()`. The modulators have no `pos` row yet.
**Modulator resets:** Cymbal resets `modOsc`/`modOsc2` to phase 0 on every
trigger; **HiHat does not** (and the Drum FM oscillator never resets).

### 5.5 Where each engine's hot code runs

The oscillator block functions are in ITCM (`INITCM`), zero wait states
(since S085 P1 also the sync/ring copies, their dispatchers and the
reset-list builder; the two interpolation variants are inlined into their
ITCM dispatchers, like `calcPeriodicInterpBlock()`). The
filter, distortion and post-chains are flash code, inlined by LTO into
`mixer_calcNextSampleBlock()`. ITCM for the filter and distortion was tried
in Session 023 and measured worse.

**The engine render functions themselves** (`Drum_calcVoiceSyncBlock()` and
the others) are also inlined by LTO into `mixer_calcNextSampleBlock()`,
together with some oscillator dispatchers (an FM dispatcher, noise loops).
Code added around a voice render therefore changes register allocation in
that one large function. In S086 a local variable kept live across the
render added reloads to two inlined per-sample loops (§10, "State live
across a render"). `make -C tools/dsp_test elf86_mixer_check` compares the
function's per-sample loops with a P2 baseline and fails if one grew.

---

## 6. Building blocks (`Core/DSPAudio/`)

### 6.1 Oscillator (`Oscillator.c/.h`)

- **`OscInfo`** holds a 32-bit phase accumulator, `phaseInc`, `freq`,
  `pitchMod`, `fmMod`, `modNodeValue`, the waveform, the wavetable octave
  (`tableOffset`), a frequency cache, and a sample cache.
- **`OscInfo` padding (S087):** `position` (offset 18), `positionApplied`
  (19), `positionBendStep` (33, after `waveInterpNext`), `oscType` (34),
  `interpStamp` (35). `sizeof(OscInfo)` is 72 on ARM, asserted together
  with the field offsets after the struct. Free padding left: 1 B at 75.
- **Waveforms (stock, `wav` 0–7):**

  | `wav` | Constant | Display | Reader |
  |---:|---|---|---|
  | 0 | `SINE` | `Sin` | `calcSineBlock` |
  | 1–3 | `TRI SAW REC` | `Tri Saw Rec` | `calcWavetableOscBlock(table)` |
  | 4 | `NOISE` | `Noi` | `calcNoiseBlock` |
  | 5 | `CRASH` | `Cym` | `calcSampleOscBlock` (the built-in crash sample) |
  | 6–7 | `SQ3 SW3` (`OSC_STOCK_FIRST`..`OSC_STOCK_LAST`) | `Re3 Sw3` | the table loops, through `osc_stockTables[]` (S086) |

  `waveformNames` (`MenuText.h`) must have exactly `OSC_SAMPLE_START`
  entries (a `_Static_assert` in `menu.c`). Inserting a waveform renumbers
  the catalogue entries (S086 did, 6+ → 8+, with no kit migration).

- **Oscillator type system (S087, `osc1_type`):** the `typ` row on the main
  oscillator selects which reader serves `wav` values ≥ `OSC_SAMPLE_START`:

  | `typ` | Constant | Reader family |
  |---|---|---|
  | `wav` (0) | `OSC_TYPE_WAV` | stock waves only; catalogue entries unused |
  | `smp` (1) | `OSC_TYPE_SMP` | one-shot sample reader (`osc_smp*Block`, §6.1.4) |
  | `slp` (2) | `OSC_TYPE_SLP` | looped sample reader (`osc_slp*Block`, §6.1.4) |
  | `wtb` (3) | `OSC_TYPE_WTB` | wavetable reader with frame blending (§6.1.5) |

  `typ` is a **selector row**: `INSTRUMENT_PARAM_FLAG_SELECTOR` (0x08),
  `DTYPE_MENU` with id `MENU_OSC_TYPE`. Not morphable, stored single-ended,
  applied before the Morph image on every slot reset. Step-automatable with
  no ramp and one value per step (held until the voice's next trigger).
  `OscInfo.oscType` at byte 34.

  Under `typ` = `wav`, `wav` values ≥ `OSC_SAMPLE_START` are ignored (the
  oscillator stays on its last stock wave). Under `smp`/`slp`/`wtb`, `wav`
  8+ selects catalogue entries: `s01..sNN` (samples), `l01..lNN` (looped
  samples), `w01..wNN` (wavetables). The type determines which entries are
  meaningful; out-of-range entries are clamped.

  `osc_setType()` clears the interpolation slot stamp when the type
  changes, and rebinds the oscillator to the selected catalogue entry.

- **Oscillator run modes (S088, `osc1_run`):** a selector row (`run`,
  `DTYPE_ON_OFF` with the high-nibble tag `ONOFF_TAG_OSC_RUN`, flags
  `AUTOMATABLE | SELECTOR`) chooses what a retrigger does to the carrier.
  The value lives in `runMode` on the engine struct (DrumVoice, SnareVoice,
  CymbalVoice, HiHatVoice) - not in `OscInfo`; the special writer
  `IM_SPECIAL_OSC_RUN` clamps 0..2 and stores it.

  | `run` | Constant | Retrigger behaviour |
  |---|---|---|
  | `rst` (0) | `OSC_RUN_RST` | today's behaviour, bit-identical |
  | `fre` (1) | `OSC_RUN_FRE` | carrier phase is never reset; modulators slew; amp EG gain-matched; filter keeps ringing with a slewed coefficient |
  | `itp` (2) | `OSC_RUN_ITP` | carrier phase resets; the old carrier plays on for 132 samples (`OSC_RUN_ITP_SAMPLES`) in a linear crossfade; modulators slew; amp EG gain-matched |

  Shared rules for `fre`/`itp` when the amp gain is audible (`>= 1/32768`,
  `slopeEg2_isAudibleGain()`): `slopeEg2_retriggerFrom()` inverts the attack
  curve so the attack enters at the current gain, and the engine arms one
  block of post-chain ramps (`ampRampArm`): Drum velocity, Snare/Cymbal/HiHat
  compound amp gain, and the Drum FM depth. `rst` passes equal endpoints, so
  every ramp is the identity and the output is unchanged. A fully decayed
  `itp` retrigger behaves exactly as `rst` (D6). A refused `itp` claim (both
  interpolation slots held) falls back to `fre` (no crossfade, no phase
  reset). Filter coefficient slew is `SVF_recalcFreqSlew()`
  (`OSC_RUN_FILTER_SLEW` 0.28 per block); `rst` keeps `SVF_recalcFreq()`.

  The `itp` old-carrier copy is `OscCarrierSnapshot` (28 B on ARM) and is
  rendered PLAIN (`oscSection_renderOldCarrier()`): no FM, sync or ring on
  the old copy (S085 P1 Q14). It uses the same two interpolation slots as
  wavetable frame blending. Per-block claim priority: ongoing `itp` fades
  (counted by the mixer, reserved in `modNode_resetTargets()`), then wavetable
  blending, then new `itp` fades. A `wtb` snapshot plays its frozen pos frame
  through the plain copy and never claims.

- **Modulator `wav` list (S087, §6.1.3):** modulators get no `typ` or `pos`
  rows. Their `wav` list is: stock 0–7, then `s01..sNN`, `l01..lNN`,
  `w01..wNN`, up to 127 entries. The type is inferred from the entry
  prefix.

- **v1 sample loops removed (S087):** `calcUserSampleOscBlock`,
  `calcUserSampleOscFmBlock`, `calcUserSampleOscRingBlock`,
  `calcUserSampleOscSyncBlock` and `osc_syncWaveform()` are deleted.
  `osc_interp_a/b` DTCM scratch (128 B) released. `stub_silence_buffer`
  (512 B flash) removed. v1 `SampleInfo` shims in `SampleMemory.c/.h`
  removed.
- **Effective frequency:** `freq · pitchMod · modNodeValue`, where `freq`
  comes from the base note plus coarse/fine tuning
  (`MidiNoteFrequencies[note] · detune`).
- **Phase formats:** sine uses a 4,096-entry table (`<<20`), wavetables 1,024
  entries (`<<22`). For sine and the tables one cycle is the full 2³² phase.
  The crash sample is 32,768 points, so it too spans exactly 2³² and loops
  at its end; `crashSample[]` has a 32,769th guard entry (= entry 0) for the
  interpolators' `itg + 1` read (S086). The S087 sample/wavetable readers
  use Q20.12 phase (`OSC_SAMPLE_PHASE_SHIFT` 12), supporting up to 2²⁰
  points per sample (§6.1.4).
- **Wavetables:** `sawTable`, `triTable`, `recTable` (`wavetable.c`) and the
  stock waves `sq3Table`, `sw3Table` (`wavetableStock.c`, generated) are 11
  band-limited octaves × 1,024 samples (22,528 B each, program flash), every
  level peaking at 32,767. `osc_isTableWave()` names this set. `osc_setFreq()` picks the
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
  `calcSampleOscFmBlock`). The v1 `calcUserSampleOscFmBlock` is removed
  (S087); sample/wavetable FM uses the new type-aware readers (§6.1.4,
  §6.1.5).
- **Sync and ring support (S085; used by the Drum `snc`/`rng` modes,
  §5.1.1).** A separate family of functions; the plain and FM loops above
  were not edited.

  | Function | Role |
  |---|---|
  | `osc_syncClockInc(master)` | the clock increment (2^32 per nominal period; `<< 5` for crash) |
  | `osc_buildSyncList(clock, inc, over, size)` | the per-block reset list (`OSC_SYNC_NONE` = no reset); returns the end clock; 7 instructions per sample |
  | `calcNextOscSampleSyncBlock(osc, over, clockInc, depthQ30, buf, size)` | the slave render: ratio (one VDIV), interpolation entry, then `osc_renderSyncWave()` |
  | `calcSineSyncBlock`, `calcWavetableOscSyncBlock` (table as argument), `calcSampleOscSyncBlock` | the sync copies: the plain read verbatim, the store at gain 1, the phase through `osc_syncPull()` |
  | `osc_renderSyncWave()` / `osc_renderPlainWave()` | the per-waveform dispatch without the interpolation check; the plain one calls the leaf loops at gain 1 (unsynced slaves, ring interpolation partners) |
  | `calcPeriodicInterpSyncBlock` / `calcPeriodicInterpRingBlock` | interpolated variants (partners from one list / the ring applied in the blend) |
  | `calcNextOscSampleRingBlock(osc, modBuf, buf, size, depthQ15, depthStepQ15)` | the carrier render with the ring law and a per-sample depth ramp; mirrors `calcNextOscSampleBlock()` including its user-sample range check |
  | `calcSineRingBlock`, `calcWavetableOscRingBlock`, `calcNoiseRingBlock`, `calcSampleOscRingBlock` | the ring copies (v1 `calcUserSampleOscRingBlock` removed S087) |
  | `osc_syncDepthQ30(amount)`, `osc_ringDepthQ15(amount)` (header, static inline) | the amount → Q30 sync depth / Q15 ring depth (clamped; NaN → 0) |

  **Placement and build rules:**
  - Every function above lives in `.itcm.s085` (`OSC_S085_SECTION`). The
    linker places `*(.itcm.*)` after `*(.itcm)` in the one copied ITCM
    section, so the new code never moves an existing loop. Appending inside
    `.itcm` did: it turned a 16-bit tail branch into `b.w`.
  - The loops and the builder are `noinline, noclone`
    (`OSC_S085_LOOP`), and so are the two dispatchers (`OSC_S085_DISPATCH`).
    The interpolated variants are inlined into the dispatchers, so the
    blends stay in ITCM even though LTO merges the Drum render into the
    flash-resident `mixer_calcNextSampleBlock()`.
  - They do **not** call `calcNextOscSampleBlock()` with constant
    arguments. A new constant-gain call site made LTO re-specialise that
    function's two existing clones; `osc_renderPlainWave()` calls the leaves
    instead.
  - The copies require `INTERPOLATE_OSC == 1` (`#error` otherwise).

- **Waveform interpolation:** when a waveform parameter is modulated, up to
  `OSC_WAVE_INTERP_MAX_ACTIVE` (2) oscillators per block blend the two
  neighbouring waveforms (`calcPeriodicInterpBlock`: render both into DTCM
  scratch buffers, then blend). Each costs about two oscillator renders.
  Details: `OSC_INTERP_AUDIT.md`.
- **ITCM:** the block renderers and (since S073) `osc_setFreq()`.

#### 6.1.1 `pos`: the oscillator position (S086; extended S087)

The `osc1_position` row (`Oscilltr` / `Position`, short `pos`, 0..127) is
the fourth cell of the first oscillator screen on every instrument. Its
meaning depends on the oscillator type:

| Type | `pos` meaning |
|---|---|
| `wav` (stock waves) | continuous phase offset of `pos`/128 of a cycle |
| `smp` | sample start at `pos`/128 of the sample; linear bend, 4× speed cap |
| `slp` | sample start at `pos`/128 of the whole sample; linear bend, wrapped into loop region |
| `wtb` | frame position, read every block |

**Phase waves (`wav`).** `osc_positionIsPhase(w)` is 1 for `SINE`, `CRASH`
and the table waves. For all of them one cycle (for `CRASH`, the whole
32,768-point sample) is the full 2³² phase, so `pos << 25` is `pos`/128 of
it at every octave. Noise ignores `pos`.

**Sample waves (S087, `smp`/`slp`).** Q20.12 phase. Start =
`((pos × len) >> 7) << 12`. **Linear** difference (not circular): 127 → 0
is a jump back, not +1 step. One-shot edges: clamped at 0; past end = ended;
finished one-shot stays finished until the next trigger. Time-based 4× speed
cap; larger change glides over several blocks via `osc_slpBlockInc()`.
`slp` wraps into the fixed loop region. `smp` clamps at ends.

**Wavetable waves (S087, `wtb`).** `pos` = frame position, proportional to
the wavetable's frame count. Read every block by the blend planner.

**Type/source change re-anchor (D7):** `positionApplied` = `position`,
no bend that block. Sync reset target = `pos` start point (D9).

**At a trigger** (`osc_positionTrigger(osc)`, called by all four
`*_triggerVoice()` after the start-phase block):

```
osc.phase += (position & 127) << 25;      /* 0 for non-phase waves */
osc.positionApplied = position & 127;
```

It is added after the transient offset-mode scaling of the start phase, so
offset mode keeps its meaning. `pos` 0 adds 0: the old start phase, bit for
bit. Velocity modulation runs earlier in the trigger, so a velocity target
on `pos` is exact for the hit.

**While playing: the per-block bend.** Modulation reaches `pos` once per
block (the LFO dispatch at the start of `mixer_calcNextSampleBlock()`;
knob, automation, velocity and Morph between blocks). Adding a change to the
phase at a block edge would click. Instead each render is wrapped:

```
osc_positionBendApply(&osc, bend);   /* before the osc1 render */
render osc1 (any mode);
osc_positionBendRestore(&osc);       /* immediately after */
```

- `osc_positionBendApply()` computes the **circular** difference
  d = ((position − positionApplied + 64) & 127) − 64, in −64..+63 (127 → 0
  is +1 step), limits it to ±`OSC_POS_BEND_CAP`, advances
  `positionApplied` by d, stores d in `positionBendStep` and adds
  d × `OSC_POS_BEND_PER_STEP` to `phaseInc`. With `bend` = 0 or a non-phase
  wave it stores 0 and leaves `phaseInc` alone, but still consumes the
  change.
- `OSC_POS_BEND_PER_STEP` = (1 << 25) / `OUTPUT_DMA_SIZE` = 1 << 20. Held
  for the 32-sample block it moves the phase by exactly d << 25, so the
  oscillator lands on the new offset with no drift. Both constants are
  asserted in `Oscillator.h`.
- `osc_positionBendRestore()` subtracts the stored step and clears it.
  **It must follow the same render:** `osc_setFreq()` caches `phaseInc`
  (it returns early when the frequency is unchanged), so a bend left in it
  would become a lasting pitch error.
- The step lives in `OscInfo`, not in a local variable, so nothing stays
  live across the render that LTO inlines into the mixer (§5.5, §10).

**What it sounds like.** A held `pos` is a phase shift. A moving `pos` is a
frequency change for that block: an LFO on `pos` is phase modulation (a
4 Hz LFO with ¼-cycle depth ≈ ±6 Hz vibrato). There is no added latency
against pitch modulation: the change starts on the first sample of the
block in which it arrives and completes at the block end. A large step
(e.g. 64 at once) is squeezed into 0.7 ms, a short pitch blip that also
aliases (the octave table follows the frequency, not the bend);
`OSC_POS_BEND_CAP` (64 = off) would spread it over several blocks. On
`Cym` a change scrubs through the sample (one step = 256 points per block,
about 8× speed).

**Per engine and mode.**

| Oscillator / mode | Trigger | Bend |
|---|---|---|
| Drum/Snare/Cymbal/HiHat `osc1` | yes | yes |
| Drum `fm`/`mix`/`rng` carrier | yes | yes (the carrier moves against the unbent modulator) |
| Drum `snc` slave | yes | **no**: each master reset sets the phase to a target without `pos`, and `osc_syncRatioQ16()` reads the slave `phaseInc` |
| Waveform interpolation | the base waveform decides | both partners render with the bent `phaseInc`; `calcPeriodicInterp*` ends the block at start + `phaseInc` × size |
| Modulators (Drum FM, Cymbal/HiHat) | no row yet | — |

**Modulation sources.** Per block `main.c` runs
`voiceControl_processPending()` (which internally: prepare triggers → drain
automation to mark → fire triggers, S087 D6), then the mixer (LFOs, async,
renders). So: a step's automation lands before its trigger fires; velocity
is exact at the hit; an LFO glides continuously (its depth is **clamped** at
0/127, not wrapped); with LFO retrigger the hit starts at the previous LFO
value and glides to the restarted one within 0.7 ms; a step-automated `pos`
starts at the Morph base (the trigger restores it) and glides to the step
value within the first block.

**Slot reset.** A runtime slot reset `memset`s the voice, so
`positionApplied` starts at 0 and the first block may glide from 0 to the
loaded `pos`; the voice is silent until triggered, and the trigger
re-anchors.

**Tests.** `make -C tools/dsp_test osc_pos` (P1–P8, an uncapped and a cap-8
build; it compiles the header's own constants block) and
`elf86_mixer_check`.

#### 6.1.2 Stock table waves (S086)

`Re3` (`SQ3`, a TB-303 square) and `Sw3` (`SW3`, a 303 saw) are fixed
`const int16_t [11][1024]` tables like `sawTable`, read by the same loops
(plain, FM, sync and ring) at the same cost.

- **Dispatch:** `osc_stockTables[]` (`Oscillator.c`) maps `waveform −
  OSC_STOCK_FIRST` to the table, with a `_Static_assert` on its length.
  Each dispatcher (`calcNextOscSampleBlock`, `calcNextOscSampleFmBlock`,
  `osc_renderPlainWave`, `osc_renderSyncWave`, `calcNextOscSampleRingBlock`)
  has one case group for all stock waves with this **variable** pointer;
  `osc_setFreq()` gives them `osc_calcWavetableFreqValue()`. One
  variable-pointer site per dispatcher avoids constant-argument call sites
  (which make LTO re-specialise the existing clones, §10).
- **Sync:** `osc_syncWaveform()` includes them (an interpolated stock slave
  takes the synced phase rule). `osc_syncClockInc()` scales only `CRASH`
  and user samples by `<< 5`; the stock waves are 2³²-per-cycle clocks like
  the tables (testing `>= CRASH` would make a stock master reset 32× too
  often).
- **Trigger start phase:** the engines test `osc_isTableWave()`, so the
  stock waves restart at `0xff << 20` × offset like the tables.
- **Generation:** `tools/gen_stock_tables.py` reads
  `tools/plots/stock_wave_harmonics.csv` (the harmonic recipe made by
  `tools/plots/plot_stock_waves.py` from the user's reference captures) and
  writes `Core/DSPAudio/wavetableStock.c`. Each level is the recipe's
  harmonics through a per-level gain curve **measured from `sawTable`**
  (g = |SAW(k)|·k/|SAW(1)|, a Hann-like taper), synthesised at 1,024
  points, scaled to a 32,767 peak. Built-in checks: C1 rebuilds `sawTable`
  within 6 LSB (`recTable`/`triTable` do not follow this taper: they roll
  off more slowly and carry a −20…−60 dB tail above the level's Nyquist
  harmonic, so they are printed for information), C2 peak/DC, C3
  out-of-band, C4 `--check` (regenerates byte-identical;
  `make -C tools/dsp_test stock_tables`). Review plot:
  `tools/plots/plot_stock_tables.py`.
- **Level:** peak-normalised like every table, so `Re3` plays about 8 dB and
  `Sw3` about 2 dB quieter than `Rec`/`Saw` (crest factor).
- **Flash:** 22,528 B per wave. The fixed `phase >> 22` / `& 1023` indexing
  needs 1,024 points at every level; shorter upper levels would need a new
  reader (and change the sound by −35…−67 dB at the top octaves).
  `ReA`/`TrA`/`SwA` from the same recipe are deferred.

#### 6.1.3 Modulator `wav` list (S087)

Drum FM, Cymbal and HiHat modulators share an extended `wav` list:
- Stock waves 0–7 (as above)
- `s01..sNN` — one-shot samples from the catalogue
- `l01..lNN` — looped samples (same catalogue items, looped playback)
- `w01..wNN` — wavetable first frame

Up to 127 entries total. Modulators get no `typ` or `pos` rows (A22).
A value past the list end is clamped to the last entry that exists.

Sync master rules by entry: stock → phase clock (as before); `sNN` → as
noise (one reset per nominal period at `frq`); `lNN` → loop wrap; `wNN` →
frame wrap. `osc_modulatorTrigger()` (S087 P3) restarts a modulator's
one-shot sample on every hit when its `wav` selects an `s` entry; stock
waves and `l`/`w` entries return immediately. Called from Drum and HiHat
triggers (whose modulators never reset); Cymbal needed no change (it
already resets both modulators to phase 0 on every hit).

#### 6.1.4 `smp`/`slp` sample readers (S087)

Q20.12 phase (`OSC_SAMPLE_PHASE_SHIFT` 12). Up to 2²⁰ points per sample.
Eight loops in `.itcm.s087`, `noinline, noclone`:

| Loop | Function |
|---|---|
| smp plain | `osc_smpPlainBlock` |
| smp FM | `osc_smpFmBlock` |
| smp sync | `osc_smpSyncBlock` |
| smp ring | `osc_smpRingBlock` |
| slp plain | `osc_slpPlainBlock` |
| slp FM | `osc_slpFmBlock` |
| slp sync | `osc_slpSyncBlock` |
| slp ring | `osc_slpRingBlock` |

Install-time preparation (catalogue v2, `FILESYSTEM_SPEC.md`):
- Two zero frames after every sample: removes played-out select and
  next-point hold
- Fixed loop region: `smpl` loop 1 when present, else the whole sample;
  stored as `loop_start`/`loop_end` in the entry
- Gain code: per block, `2^(code/24)`, peak within 0.25 dB of full scale

`smp` (one-shot): plays once from the trigger start point; past end = ended;
finished one-shot stays finished until the next trigger.
`slp` (looped): loops within the fixed region; lead-in plays before the
loop, not frozen at loop start. FM on `slp` reads absolute (picks
loop/lead-in per block).

`pos` on samples: see §6.1.1 (updated for S087).

#### 6.1.5 `wtb` wavetable reader (S087)

- Any frame length (UMULL indexing, A16). One-frame base loop.
- Frame blending through two interpolation slots (`OscInfo.interpStamp`,
  byte 35). Sticky claims: a grant made this block is kept. Stamps 1..255,
  never 0 (stamp 0 = "no claim"). Without a slot: nearest frame (Q9
  fallback).
- `osc_wtbPlanBlock()` splits at frame crossings (at most two segments per
  block).
- `pos` on wavetables = frame position, read every block.
- Four blend copies: plain, FM, ring, sync (in `.itcm.s087`).
- Interpolation slot management: `modNode_interpStampOf()` gives 1..255
  (wraps from 255 to 1); `modNode_interpLastStamp` (1 B DTCM) +
  `modNode_interpReserved` (1 B). Claims are first come, first served.
  `osc_setType()` clears the stamp when the type changes.

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

**Sample click list (S087 P3).** The click/transient selector is extended:
Snp, Ofs, 12 built-in clicks, then one entry per installed sample (`s01`,
`s02`, …). One-shots only, no loop or wavetable entries. Compact cells show
`sNN`; the single-parameter view shows the sample's 8-character name on the
left. `frq` sets the speed (1.0 = file's own rate); `vol` applied on top of
install normalisation. Silent at the end until the next hit. After a
reinstall: stored value past installed samples → last sample; no samples →
last built-in click with one silent block first. CPU: 20 instructions per
sample for the new sample loop vs 22 for built-in clicks. Switching between
a built-in click and a sample mid-hit stays silent until the next trigger.

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
- **Waveform modulation** of an oscillator can blend neighbouring waveforms
  (§6.1) for at most two oscillators per block; the rest step.

### 7.5 Step automation and Morph

- **Step automation:** TIM3 queues values; the foreground drain
  (`seq_drainPendingAutomationTo(mark)`, inside
  `voiceControl_processPending()` between prepare and fire, S087 D6) writes
  voice targets with `instrumentManager_writeRuntime()` and marks them
  dirty. The voice's next **resetting** trigger
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

### 8.1 Measured instruction counts (S073; S085, S086 and S087 rows)

Per-sample loop instruction totals from `fpseq.py --report --all` on
standalone `-Ofast` ARM objects. Instructions, not cycles: the Cortex-M7
dual-issues simple instructions, and `VDIV.F32` costs about 14 cycles.

| Stage | Instructions per sample | Divisions |
|---|---:|---:|
| Sine / wavetable oscillator (incl. the stock waves `Re3`/`Sw3`, S086) | 23 / 24 | 0 |
| FM wavetable / FM sine carrier | 35 / 30 | 0 |
| Noise | 12–13 | 0 |
| Crash sample / FM crash | 30 / 38 | 0 |
| S087 `slp` plain / FM (inside loop) / ring / `smp` ring | 27 / 49–50 / 42 / 38 | 0 |
| S087 `wtb` blend inner (plain/FM/ring/sync) | 34–38 / 42–46 / 43–47 / 44–48 | 0 |
| Transient sample / S087 sample click | 22 / 20 | 0 |
| Saturating buffer add | about 4–5 (two samples per step) | 0 |
| Sync reset-list builder (S085) | 7 | 0 |
| `pos` (S086): trigger offset / per-block bend | a few per trigger / about 10 per block per voice (≈ 0.3 per sample), outside the loops | 0 |
| Sync slave Tri/Saw/Rec / sine / crash (no reset / reset path) | 34/39 / 33/38 / 36/41 | 0 (+1 per block) |
| Ring carrier, depth ramped (S085 follow-up) Tri/Saw/Rec / sine / crash / noise / user sample | 31 / 30 / 33 / 16–19 / 46–48 (linked 49–50) | 0 (+1 per block) |
| ZDF filter (per type) | 85–104 (was 73–89) | 2 (was 5) |
| Naive 2-pole filter | about 40–60 | 1 |
| Drum post-chain | 32 (was 38 in three passes) | 1 |
| Snare post-chain | 30 (was 35) | 1 |
| Cymbal/HiHat post-chain | 25 (was 28) | 1 |

### 8.2 Per-voice estimate (worst case, per sample)

| Engine | Composition | About |
|---|---|---:|
| Drum (FM mode) | mod osc 24 + FM carrier 35 + transient 22 + add 5 + filter 95 + post 32 | 215 instructions, 3 divisions |
| Drum (`rng`) | mod osc 24 + ring carrier 31 + transient 22 + add 5 + filter 95 + post 32 | 209 instructions, 3 divisions + 1 per block |
| Drum (`snc`) | builder 7 + sync slave 39 + transient 22 + add 5 + filter 95 + post 32 | 200 instructions, 3 divisions + 1 per block |
| Snare | noise 13 + filter 95 + transient 22 + add 5 + osc 24 + post 30 | 190 instructions, 3 divisions |
| Cymbal | 2 mod osc 48 + add 5 + FM carrier 35 + filter 95 + transient 22 + post 25 | 230 instructions, 3 divisions |
| HiHat | as Cymbal | 230 instructions, 3 divisions |

Add waveform interpolation (about one extra oscillator render each, at most
two per block) and the mixer per slot (decimator 12–18, dry path 28–46, plus
the send; `EFFECTS_MIXER_DSP_REFERENCE.md`).

**Drum oscillator section by mod mode (S085, instructions per sample).**
Standalone objects, and in brackets the linked image (LTO moves the
user-sample loops by 1–3):

| Mode | Table | Sine | Crash | User samples (the dearest) |
|---|---:|---:|---:|---:|
| `fm` | 59 | 53 | 68 | 90 (≈ 92) |
| `mix` | 53 | 51 | 65 | 87 |
| `rng` (ramped) | 55 | 53 | 63 | 87–89 (≈ 90–91) |
| `snc` | 46 | 45 | 48 | 48 (the user-sample slave is unsynced) |

`rng` with user samples has the narrowest margin against `fm` (1–2 in the
linked image): re-measure it after any change to the ring copies or the
sample loops.

**S086:** the stock waves use the table loops (plain 24 / gain-1 23, FM 35,
the existing sync and ring functions), so no voice's dearest setting
changed. `pos` adds nothing per sample; its per-block bend is outside the
loops.

**S087:** the v1 user-sample loops are removed and replaced by type-aware
readers (§6.1.4, §6.1.5). The Drum oscillator section worst cases stay
below their frozen ceilings (A1):
- Drum `fm` with `slp` on both oscillators: 27 + 50 = **77**, against
  frozen ceiling **≈ 92**
- Cymbal/HiHat with samples on all three: 27 + 27 + 50 = **104**, against
  frozen ceiling **≈ 131**
- Per-slot wavetable blend cost: +10..+13 over one frame, against ≈ +66 today

ITCM went from 8,512 (S086) to 15,032 (S087 D, before P1 v1 removal) to
12,640 (after P1). The transient sample click adds 20 instructions per
sample vs 22 for built-in clicks.

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
   still fits the slot reserve, 2 × `sizeof(DrumVoice)` = 1,192 B; the
   `_Static_assert` will tell you). A byte added to `DrumVoice` itself
   grows all six slots by 2 B each. Per-oscillator state can often go into
   `OscInfo` padding at no cost (S086 used offsets 18–19 and 33; S087
   used 34–35; 75 is free); assert the offsets as `Oscillator.h` does.
2. Add the row in `<Type>Parameters.c` at the end of the table (appending
   keeps existing descriptor indices, which are stored in Scene images,
   automation and LFO tokens), and the matching enum entry. Update
   `<TYPE>_PARAM_DESCRIPTOR_COUNT` (a `_Static_assert` checks it).
3. Choose flags and a `mod_domain`. Only rows with a continuous domain can be
   LFO or velocity targets.
4. If the value needs conversion, give the row a special tag (`ROW_SPECIAL`)
   or add a new writer (§9.2). If the key would match an old string rule,
   the tag must agree: run `make -C tools/dsp_test special_tags`.
5. Place it on a menu page (`*_menu_pages[]`; each page row has exactly 16
   cells, four screens of four). Keep the row on one line in the table:
   `check_special_tags.py` matches rows line by line.
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
  `calcNextOscSampleBlock()` / `calcNextOscSampleFmBlock()` (and the S085
  dispatchers `osc_renderPlainWave()`, `osc_renderSyncWave()`,
  `calcNextOscSampleRingBlock()`), the frequency `case` in `osc_setFreq()`,
  the sync rules (`osc_syncWaveform()`, `osc_syncClockInc()`), the
  `pos` rule (`osc_positionIsPhase()`), and the menu text. Waveform numbers
  from `OSC_SAMPLE_START` on are user samples; inserting a waveform shifts
  them, which changes stored values. (`calcNextOscSample()`/`calcFm()`, the
  per-sample variants, have no callers.)
- **Another stock table wave** (`ReA`/`TrA`/`SwA` are ready in the recipe):
  1. add a line to `WAVES` in `tools/gen_stock_tables.py` and run it (then
     `make -C tools/dsp_test stock_tables`);
  2. add the `extern` in `wavetable.h`;
  3. add the constant after `SW3`, move `OSC_STOCK_LAST` and
     `OSC_SAMPLE_START`;
  4. add the table to `osc_stockTables[]` (the assert checks the length);
  5. add the name to `waveformNames` (the assert checks the count);
  6. extend the host tests (`test_osc_stock.c`, `test_osc_pos.c` P1).
  It costs 22,528 B of program flash and renumbers the user samples.
- **Oscillator modes and types under the CPU rule (S085, user):** "no
  increase in CPU cycles overall".
  - **The gate:** the dearest setting of a voice in any mode ≤ that voice's
    dearest setting before the change. Freeze the ceiling figures before
    replacing any loop that sets them.
  - **The method:**
    1. Freeze the files to edit (`tools/dsp_test/snapshot85.sh` is the
       pattern).
    2. Dump the linked oscillator loops (`elf_osc_baseline`).
    3. Write the new code as **separate copies** per waveform and per use
       (plain, FM carrier, `snc` sync slave, `rng` ring carrier). Never add
       a parameter or test to an existing loop: the rollback's shared
       plain/sync loops added cost to every plain render.
    4. Put the copies in a new `.itcm.sNNN` subsection, `noinline,
       noclone`, with no new constant-argument calls into the existing
       dispatchers.
    5. Prove the old code unchanged (`oscmod_identity`, `elf_osc_check`: 0
       missing, 0 changed).
    6. Measure the new loops standalone and linked (`armcheck-oscmod`,
       `fpseq.py` on `build/lxr02.elf`).
    7. Write a host test with break proofs (`test_osc_mod.c`).
    8. Gate on the device with the six-slot worst-case Scene (`cpu` widget,
       underruns).
  - **For Drum:** a new waveform a carrier can select needs an FM loop, a
    ring copy, and a sync copy or an explicit "unsynced" rule. A new
    waveform `modOsc` can select needs a sync-master rule: a reset-list
    builder; the slave copies stay as they are.
  - **A per-block parameter that scales amplitude** needs a per-sample
    ramp, or it zippers (the `rng` depth).
  - **A per-block phase or position change** can be spread through
    `phaseInc` for one block instead of a per-sample ramp (the `pos` bend,
    §6.1.1): zero per-sample cost, exact landing. Keep its state in the
    oscillator, never in a local that lives across the render.
  - **Check the mixer too:** the voice renders are inlined into
    `mixer_calcNextSampleBlock()`; run `elf86_mixer_check` (and refresh its
    baseline with `elf86_mixer_snapshot` on a known-good build when the
    mixer itself is meant to change).
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
  engines keep their oscillator phase and filter state. Drum `modOsc` is
  never reset (user rule, S085).
- **`modMode` is not a boolean** (S085): it was `mixOscs`, tested with
  `if (mixOscs)`. Any new reader must `switch` on `DRUM_MOD_*` with `fm` as
  the default.
- **State another mode reads later** (S085 D2): when a mode stops rendering
  an oscillator, do not advance that oscillator's phase arbitrarily. A
  one-shot sample phase pushed past its end makes the next render read past
  the sample. Use separate state (`syncClock`).
- **ITCM placement moves code** (S085 D7): new ITCM functions go in a new
  `.itcm.sNNN` subsection, or existing loops move and their branches can
  change size.
- **LTO re-specialises dispatchers** (S085 D8): a new call site with
  constant arguments (for example gain `1.f`) into
  `calcNextOscSampleBlock()` can re-clone the existing specialisations and
  change `fm`/`mix` code. Call the leaf loops instead.
- **Per-block amplitude steps zipper** (S085): ramp per sample.
- **State live across a render** (S086 F1): the voice renders are inlined
  into `mixer_calcNextSampleBlock()`. A local variable kept from before a
  render to after it (the first `pos` version kept the bend that way) raised
  register pressure in that function, and two unrelated inlined per-sample
  loops gained reloads (an FM sine loop 29 → 31, a noise loop 15 → 16). Keep
  such state in the oscillator structure (`positionBendStep`).
- **A bend left in `phaseInc` sticks** (S086): `osc_setFreq()` recomputes
  `phaseInc` only when the frequency changes. Every
  `osc_positionBendApply()` needs its `osc_positionBendRestore()` after the
  same render, with no early return in between.
- **Host tests must compile the header's constants** (S086 F3): a test that
  restates a constant passes even when the header's value is wrong. Extract
  the header block (`osc_pos` cuts it with `sed`), and keep a
  `_Static_assert` in the header.
- **The bench extracts `Oscillator.h` by line range** (`osc_mod`: defines
  49–65, `OscInfo` 71–143). Edits above or inside the struct must update the
  ranges in `tools/dsp_test/Makefile`.
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
- Session 085: Drum mod modes `snc` (hard sync to an FM-page clock) and
  `rng` (ring, folded into the carrier store, per-sample depth ramp);
  `snc`/`rng` follow the mod envelope with the `fm` response (amt² × env);
  `mixOscs` renamed `modMode`; `DrumVoice.syncClock`, `ringSnap`,
  `ringDepthQ15`; the S085 sync/ring oscillator family in `.itcm.s085`;
  the no-CPU-increase gate and its tooling (`085_SESSION_HANDOFF_LOG.md`).
  An earlier S085 attempt (oscillator types, run modes, render pool) reached
  100 % CPU on hardware and was rolled back (§9.3; the 085 log §2).
- Session 086: stock table waves `Re3`/`Sw3` (`SQ3`/`SW3`, `wav` 6–7; user
  samples moved to 8+), generated by `tools/gen_stock_tables.py` and played
  by the table loops; `osc_syncClockInc()` fixed for them. The `pos` row
  (`osc1_position`): a trigger phase offset plus a per-block `phaseInc`
  bend, on the phase waves and the crash sample (whose array gained a guard
  entry). The F1 lesson (no state live across an inlined render) and the
  mixer hot-loop gate (`086_SESSION_HANDOFF_LOG.md`).
- Session 087: oscillator type system (`osc1_type`: `wav`/`smp`/`slp`/`wtb`,
  §6.1), sample flash catalogue v2 (128 samples + 128 wavetables, 8 KB at
  `0x081FE000`), Q20.12 sample and wavetable readers (8 loops in
  `.itcm.s087`, §6.1.4–§6.1.5), `pos` extended to samples/wavetables
  (§6.1.1), step-automation order fix D6 (§2, §7.5), modulator `wav` list
  expansion to s/l/w entries (§6.1.3), `osc_modulatorTrigger()` for
  one-shot sample mod oscillators, transient click sample list (§6.4),
  display/naming corrections (first 4 + last 4 for names over 8 chars),
  v1 user-sample loops and `osc_interp_a/b` removed, MEMORY.md refactored.
  Build at close: Flash 670,628 (headroom 83,036), ITCM 12,640
  (`087_SESSION_HANDOFF_LOG.md`).

---

## 12. Binding Mandates — Oscillator Work Under the CPU Rule

> **Any session that touches oscillator or voice DSP must read and follow
> these rules.** (User, S085)

- **"Absolutely no increase in CPU cycles overall."** The gate: the dearest
  setting of a voice in any mode must not exceed that voice's dearest
  setting before the change. Per-same-wave comparisons are reported only.
  - Freeze the ceiling figures before replacing a loop that sets them.
  - The Drum ceiling is `fm` with user samples, ≈ 90 per sample standalone
    / ≈ 92 linked (frozen at S085 close). S087 D replaced the v1
    user-sample loops; the new `slp` + FM carrier reads 77 against the 92
    ceiling (§8.2).
- **Proof:**
  - `fpseq.py` counts on the standalone object **and** the linked ELF;
  - `oscmod_identity` + `elf_osc_check` (the old loops unchanged); since
    S086 `osc86_identity`, `engine86_identity`, `elf86_p3_check` against a
    snapshot of the previous image, and **`elf86_mixer_check`** (the
    per-sample loops LTO inlines into `mixer_calcNextSampleBlock()`);
  - a host test with break proofs;
  - then the user's device check (six-slot worst-case Scene, `cpu` widget,
    underruns).
  - No DEV cycle probe (user, S085 Q2).
- **Code rules:**
  - never edit an existing oscillator loop to add a feature; add copies per
    waveform and use;
  - new ITCM code in its own `.itcm.sNNN` subsection, `noinline, noclone`;
  - no new constant-argument calls into `calcNextOscSampleBlock()` (LTO
    re-clones it; call the leaves);
  - per-sample ramps for per-block amplitude parameters;
  - **(S086 F1) no state live across a voice render:** the renders are
    inlined into `mixer_calcNextSampleBlock()`, so a local kept from before
    a render to after it raises register pressure there and slows unrelated
    inlined loops. Keep such state in `OscInfo` (the `positionBendStep`
    pattern; free padding: offset 75; 34–35 used by S087 `oscType`/`interpStamp`);
  - **every `osc_positionBendApply()` needs its `osc_positionBendRestore()`
    after the same render** (`osc_setFreq()`'s cache would keep a bend);
  - host tests compile the header's own constants (extract them; S086 F3);
  - a new waveform "the same way as the existing ones" means the same
    mechanism (fixed tables, same loops), not a new one (S086 P2).
- **Drum `modOsc` is never reset on a trigger** (any mode). Do not change it
  unless the user tests it and asks (S085 Q6b).
- **`snc`/`rng`/`fm` share the amt² × mod-envelope response.** The `rng`
  depth snaps after a trigger; **in S087 the free-run run mode is to try
  "ramp always"** (user).
