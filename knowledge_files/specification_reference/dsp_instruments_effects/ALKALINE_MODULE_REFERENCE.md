# Alkaline Module Reference

How the Alkaline instrument (TB-303 emulation) works as firmware on the
LXR-02. This document covers the module layout, signal path, control path,
sequencing rules, parameter table, build integration and cost. For the
circuit modelling and DSP equation derivation that preceded implementation,
see `ALKALINE_MODELLING_REFERENCE.md`; for the instrument's place in the
DSP pipeline alongside the other five types, see
`INSTRUMENTS_DSP_REFERENCE.md` §5.5.

- **Sessions:** S092 (design), S093 P3–P6 (implementation).
- **Type:** `INSTRUMENT_TYPE_ALK`, Basic class (may take any of the six
  slots without counting towards the Advanced limit).

---

## 1. Files

| File | Role |
|---|---|
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.c/.h` | Voice struct, init, trigger, async (control rate), sync-block (audio rate) |
| `Core/DSP/Instruments/Alkaline/AlkalineParameters.c/.h` | Parameter descriptor table (28 rows, 8 sub-pages), label, flags |
| `Core/DSP/Instruments/Alkaline/AlkalineControl.c/.h` | Control-rate path: expo converter, MEG, accent network, VEG, prewarp |
| `Core/DSPAudio/DiodeLadder.c/.h` | The diode-ladder filter: `DiodeLadder_renderB2Osc()` (fused oscillator + B2R ladder), `DiodeLadder_setNet()`, `dl_slowUpdate()` |
| `Core/DSPAudio/AlkalineSquareTable.c` | Generated per-octave Q8 square table (`alkSqrTable[]`) |
| `SD_CARD/Instrument/Alkaline/init.alk` | Default instrument file |

Build: voice, control and ladder in `DSP_SRCS` with explicit `-Ofast` rule;
parameters in normal sources (`-O2`); square table in `DSP_SRCS`.

---

## 2. Registry

| Field | Value |
|---|---|
| Enum | `INSTRUMENT_TYPE_ALK` (before `INSTRUMENT_TYPE_UNKNOWN`) |
| Token | `alk` (3 characters, as required by the storage format) |
| Extension | `.alk` |
| Directory | `/Instrument/Alkaline` (8 characters, FAT 8.3-safe) |
| Display label | `Alkaline` (fits 8 LCD cells) |
| Flags | `INSTRUMENT_FLAG_BASIC` |
| Struct | `AlkalineVoice` in the `InstrumentRuntimeSlot` union (fits the 1,264 B reserve) |

The registry row is designated (`[INSTRUMENT_TYPE_ALK] = { ... }` in
`instrument_registry[]`). `check_special_tags.py registry_check()` pins the
enum order and token width. The type was appended (never inserted), so the
`/.hcnames` header check accepts older headers.

---

## 3. Signal path

```
osc table wave (sawTable | alkSqrTable[octave]) --x driveGain--> B2R ladder
  --OTA (iAbc x makeup)--> prewarped ladder current --> int16 buf --mixer-->
```

### 3.1 Oscillator

Stays a table wave (S093 P3):
- `SAW` reads `sawTable` (the stock falling ramp; matches the 303's VCO core
  to ~0.5 % bow).
- `ALK_WAVE_SQUARE` reads `alkSqrTable[octave]` — the D8 per-octave Q8
  square table generated from the P1 SPICE model of Q8's collector. Each of
  the 11 table levels carries its octave's duty (51 % at C1, 23 % at C5) and
  level (−2.5 to +3.2 dB relative to saw). Runtime cost: 0 (the table lookup
  is the same as any stock wave).
- Pitch is computed at control rate from `midiFreq` (coarse) + note + fine
  byte, plus Tuning (±700 cents centred at byte 63) and slide lag (22 ms
  one-pole in log2 Hz). `osc_setTableFreq()` sets `phaseInc`/`tableOffset`
  directly, without `osc_setFreq()`'s cache.
- The `coa` row's LFO/velocity target is `osc.modNodeValue`, so modulation
  moves the pitch.

### 3.2 Position (`pos`, S093 P6)

`osc1_position` (row 27): 0 leaves the oscillator free-running (the 303's
VCO behaviour, the pre-P6 sound bit for bit). 1–127 sets the phase to
pos/128 of a cycle at every non-legato trigger, so a note always starts at
the same waveform point. A move bends `phaseInc` for one block (13
instructions, +0.41 per sample), landing on the new offset with no drift.
Legato notes keep the running phase.

### 3.3 Diode ladder (B2R)

`DiodeLadder_renderB2Osc()` fuses the oscillator table read into the B2R
per-sample loop (bit-identical to `calcWavetableOscBlock()` then
`DiodeLadder_renderB2()`; law AL-X).

B2R is the chosen kernel (S093 D2): rungs 0-1 nonlinear (secant/"mystran"
linearisation, `tanh(z)/z`), rungs 2-3 linear (t = 1), 3 VDIV per sample.
The linked loop is 214 instructions including the oscillator (standalone
213; G3 measurement).

The ladder equations (from `ALKALINE_MODELLING_REFERENCE.md` §7.1):
- Four capacitor stages: C18 0.018 µF (bottom, faster ω), C19/C24/C26
  0.033 µF.
- Each rung: I/2 steered by tanh of the voltage across the pair.
- Term L: the output coupling draws current from C26.
- Q21 output stage: gain 103.6, coupling τ_out = 2.33 ms (68 Hz HP).
- Prewarp: `tan(θ)/θ` applied to the ladder current, one `fastTan` per
  block. Without it, −391 cents at 13 kHz.

### 3.4 Network

The eight-capacitor linear network around the ladder runs as a SplitNet
(S092 P6 §5.3; S093 P6):

- **6 block-system states** on one balanced basis common to every Res point
  (r6). `DiodeLadder_setNet()` interpolates the 13 table points per block
  (0.125 spacing in k_eff over 0..1.5).
- `dl_slowUpdate()` solves `DL_NET_ROWS` = 3 rows per phase over two
  phases (N1 at 64 samples, T2-2), updates the u asymptote (50 Hz) at the
  latch and the v asymptote (120 Hz) at the commit, and ramps the new
  outputs over the next window.
- The audio loop reads the ramped u/v steps. Per-sample cost: ~25 operations
  (vs ~100 for a dense 8-state update).

### 3.5 OTA

The BA662A output is one sample late (the [3/2] Padé). Int16 full scale
is 7 V at OTA pin 6 (`DL_OUT_SCALE`), so a driven signal keeps its
headroom. Output make-up: `2^(ALK_MAKEUP_OCT (1 - drive))` (B-2), default
`ALK_MAKEUP_OCT 1.0` (+6 dB at drive x1, 0 dB at x16), set by ear on the
device.

### 3.6 Drive

`driveGain = 16^drive = 2^(4 drive)`, drive = byte/127 (x1 at 0, x4 at mid,
x16 at max; S093 P6 doubled the former x8). Applied to the oscillator signal
before the ladder. Cost: 0 per sample.

### 3.7 Resonance and self-oscillation (S093 P6)

The Res knob runs `k_eff = ALK_RES_KMAX × knob` with `ALK_RES_KMAX` 1.5.
The 303's maximum sits at byte ≈ 85; above it is new territory.

For k > 1 the model adds an ideal transformer at the VR4a wiper (`V(W) =
k V(TP6)`, TP6 supplying k × the W-side current). At k = 1 it is the stock
wire to 4e-8. The accent sweep's VR4b gang stays physical: `min(k_eff, 1)`.

At the calibration point k_eff 1.5: rings 1.791 ms at 0.823 per cycle; above
the threshold with a high cutoff, it sustains a bounded tone (AL-SO). The
prewarp table has 16 points (k_eff 0.1 steps to 1.5, held at c(1) = 1.470
above 1). Cost: 0 per sample (measured).

---

## 4. Control path (`AlkalineControl.c`)

All run once per block (32 samples, 0.7255 ms). The ladder current and
I_abc ramp linearly per sample.

### 4.1 Expo converter

Two fixed-point passes warm-started from the last ladder current
(`ALK_EXPO_PASSES 2`; one pass was 20–30 cents off at strikes). The expo
law gives the ladder current directly from the Cutoff, Env Mod and accent
contributions.

Key constants: TM3 = 153 kΩ, β11 = 240, V561 = 3.263 V, Vbe11(7.375 µA) =
0.5279 V.

### 4.2 MEG (filter envelope)

C62/N53 state with one Newton step on the diode stack (1N4148, Is = 2.52 nA,
n = 1.752). The diode stack floor gives the long tail of the accented decay
— a fixed floor cannot fit both decay shapes.

S093 P6 decay range: `r = ALK_MEG_R_MIN + ALK_MEG_R_SPAN × taper(decay)`,
fitted to −20 % at decay 0 and +50 % at decay 1 against the 303's 68 kΩ +
1 MΩ. The accent branch keeps 68 kΩ + 250 Ω.

### 4.3 Accent network

VR7 shared by the C13 sweep and the C36 VCA branch. One 5 × 5 nodal solve
per block, written as a 2 × 2 in (W7, W), `ALK_ACC_SOLVES 1`. T2-3 lagged
accent. D24 drop 0.48 V, D27 mirror drop 1.099 V. Four diode states tried;
the first consistent one is kept.

The two branches must share one solve because R119 loads the wiper while
C36 charges (separate solves put the first accent's cutoff 27 % high).

### 4.4 VEG (volume envelope)

C42 attack/recovery with delay. S093 P6 `alk_attack`: the VEG attack is the
303's CPU trigger-pulse width, interpolated from the generated 9-point
`alkVegAttack[]` (delay, 8 tau and the per-block factor). Byte 0 is the
former 0.70 ms trigger (2.11 ms SPICE 10→90 %); 127 is 0.55 ms (5.15 ms).
The exponential shape stays.

The attack is heard after rests and long notes, as on the circuit. On
running 16ths, C42 has not recharged (τ = 1.5 s), so notes open at once.

Fast `alk_log2f()`/`alk_exp2f()` replace libm.

### 4.5 Prewarp

TPT correction P = tan(θ)/θ, applied once per block. The prewarp table has
16 points over k_eff 0..1.5. One `fastTan` per block.

### 4.6 Knob tapers

The five 303 knobs use `alk_taper()`: `(16^r − 1)/15` (audio taper, 20 % at
half rotation). `ALK_MAKEUP_OCT` is the only level trim.

---

## 5. Sequencing (the 303 rules)

### 5.1 Trigger semantics

At `Alkaline_triggerVoice(voice, slot, velocity, note)`:

1. **Accent:** velocity ≥ 101 (D1; default step velocity 100 is not
   accented). One-step ACCENT lane latched at trigger.
2. **Legato:** a note after a slide-flagged note whose gate is still open.
   No MEG strike, no VEG trigger; the pitch glides (22 ms one-pole). The
   accent flag updates the VCA accent gain and the accent sweep input.
3. **Slide latch:** `voice->slideLatch = voice->slide`, where `slide` is the
   `sld` value at *this* trigger. It decides what happens at the *next*
   trigger (needed because the automation order is restore → drain → fire).
4. **Non-legato note:** pitch jumps, MEG strike, VEG trigger.
5. No oscillator phase reset and no filter clear (the 303 has neither).

### 5.2 Gate timer

The sequencer has no gate; Alkaline runs its own engine-side gate timer.

- Normal gate: 7/12 of a step (3.5 of 6 clocks, D11), rounded to the
  nearest sample and counted down 32 per block.
- Slide gate: held through the next step (step + 7/12 = 9.5/6 ≈ 1.58 steps).
- `stepSamples` from `seq_trackStepSamples(trigger_track)` (96 PPQ Q8.8 ×
  44,108 Hz, 64-bit product).
- A MIDI note-off does not end the gate (B-6).
- Slide into a rest closes the gate first.

### 5.3 Slide

Per-block exponential approach in log-pitch: `cv ← target + (cv − target) ×
e^(−Δt/τ)`, τ = 22 ms (from the 303's 100 kΩ × 0.22 µF network). Linear
in volts, exponential in log-pitch. α = 1 − e^(−0.7256 ms / 22 ms) = 0.0324
per block. 0 per-sample cost.

### 5.4 Platform calls

The trigger makes the same three calls as `Drum_triggerVoice()` for its slot:
`lfo_retrigger(source_slot)`,
`modNode_updateValue(&velocityModulators[source_slot], vel)`, and
`instrumentManager_applyVelocityModulationTarget()`. Velocity is the accent,
so there is no `velo_vol_on_off` row.

---

## 6. Parameter table

28 rows, 8 sub-pages (S093 P6):

| Sub-page | Rows | Notes |
|---|---|---|
| OSC | `coa`, `tun`, `wav`, `pos` | `wav`: Saw/Sqr selector. `tun`: ±700 cents (`IM_SPECIAL_ALK_TUNE`, tag 24/32). `pos`: S093 P6 addition |
| AEG | `atk`, `dec` | `atk` = `alk_attack` (row 26): VEG trigger-pulse width. `dec` = MEG decay |
| mod | `dst`, `amt`, `sld` | `dst`/`amt`: filter drive and amount. `sld`: slide on/off (latched at trigger) |
| click | `acc` | Accent level |
| FILTER | `cut`, `res`, `env`, `drv` | One screen. `drv`: drive (x1–x16) |
| LFO | (as Drum) | |
| MIX | `vol`, `pan` | |
| (FM page empty) | | |

`alk_attack` (26) and `osc1_position` (27) are appended, so every stored
index, automation ID and NRPN of rows 0–25 is unchanged.

### 6.1 Morphable rows

22 of 28 rows are MORPHABLE. `alk_wave` is a SELECTOR (step automation
steps between Saw and Sqr; Morph never blends them). `velo_mod_dest` and
the four LFO targets are not morphable.

### 6.2 Storage

`init.alk` writes every key in table order; `[morph]` holds the MORPHABLE
rows. LFO voices are `self`, target selectors `255`.
`Alkaline_initVoice()` loads the same bytes.

---

## 7. Cost

### 7.1 Measured (S093 P6, G3–G5)

| Component | Cost |
|---|---|
| Fused loop (oscillator + B2R ladder) | 214 instr/sample, 3 VDIV |
| Per-block (async + sync-block) | 1,230 per block = 38.4/sample, 9 VDIV |
| **Whole voice** | **252.5 per sample** |
| **Drum ceiling (frozen, P4 re-measured)** | **261.3 per sample** |

The per-block part: async 884 + sync-block entry/exit 332 + dispatch 14.
The `pos` bend adds +13 per block. `Alkaline_isQuiet()` is used for Scene
swap only, not for CPU saving (constant-CPU rule).

Basic class: may take any slot. `slotUsesSrr()` returns 0 (no SRR, D4).

### 7.2 Budget rules

- Alkaline's dearest setting, measured linked, must be ≤ the frozen Drum
  dearest setting (`elf_hotloops.py` reports 0 loops grew).
- Every block renders in full (constant-CPU rule). No skip for closed VCA,
  zero resonance or silence.
- All per-sample loops are branch-free.
- `Alkaline_calcVoiceSyncBlock()` is marked `noinline` (LTO inlining risk,
  S086 F1). Gate on `elf86_mixer_check` / `elf_hotloops.py` with 0 grew.

---

## 8. LTO and inlining

The engine render `Alkaline_calcVoiceSyncBlock()` is **not** inlined into
`mixer_calcNextSampleBlock()` (marked `noinline`). This prevents a fifth
engine render from changing register allocation for the other four inlined
loops (S086 F1). The one call per block costs nothing measurable.

If a future change removes the `noinline`, gate it on `elf86_mixer_check`:
the check compares per-sample loops with the P2 baseline and fails if one
grew.

---

## 9. Extending Alkaline

### 9.1 Adding a parameter

1. Add the row to `AlkalineParameters.c` (key, short name, dtype, flags,
   binding). Keep the row index stable: append, never insert.
2. If the row needs a new special writer: add `IM_SPECIAL_<NAME>` before
   `IM_SPECIAL_WRITER_COUNT` (at most 31 tags fit bits 0–4), add the case
   in `instrumentManager_writeSpecialRuntime()`, extend the diagnostic
   classifier and `check_special_tags.py`.
3. Add the key to `init.alk` and the `[morph]` section if MORPHABLE.
4. Update `MIDI_NRPN_TABLE.md`.

### 9.2 Changing the filter

The ladder is in `DiodeLadder.c/.h`. **Do not edit `SVF_calcBlockZDF()` or
its float twin** — the diode ladder is a separate module. The S073 twin
rule applies only if a float Effect version is ever added.

State is loaded into locals and written back once per block. No `switch` in
the loop. Saturate to int16 once at the end.

To change the number of nonlinear rungs (e.g. all four nonlinear for a
future "B4R" variant): the tridiagonal structure and elimination order stay
the same; the secant count and VDIV change. Measure with `fpseq.py` on
standalone ARM objects and confirm against the ceiling.

### 9.3 Network table

The Res table (13 points at 0.125 spacing in k_eff over 0..1.5) has ~114
floats per point (Φ's 8 × 8 block, Γ, output rows, mode coefficients, D).
Linear interpolation; all coefficients smooth in Res because the fast modes
are normalised and sorted by pole.

To add points: expand the table in `DiodeLadder.c` and update
`DiodeLadder_setNet()`. Flash cost: ~456 B per point.

### 9.4 The extras (from S092 P0 §8)

| # | Extra | Status | Notes |
|---|---|---|---|
| E1 | Post-VCA overdrive (`tanh` stage) | Not implemented | +22–25 instr/sample, +1 VDIV. Only fits if SRR is out |
| E2 | Filter drive (user `drv` row) | **Implemented** (S093 P6) | x1–x16, applied before the ladder. 0 per sample |
| E3 | Devil Fish ranges (extended Res, Decay, Accent, Env Mod) | **Partially** (Res beyond 303 max, S093 P6) | |
| E4 | MIDI note-off | **Implemented** (B-6) | Does not end the gate |

### 9.5 Key pitfalls

1. **R110 = 22 kΩ**, not 2.2 kΩ. The Q21 stage gain (103.6) and the loop
   gain (18.7 of 19.1 threshold) depend on this value.
2. **VR7 wiper is shared** between the filter accent sweep and VCA accent
   branch. Separate solves put the first accent 27 % high.
3. **VEG is a state** (C42), not an envelope that restarts. On running
   16ths, notes open at once.
4. **Accent is not gated.** Accented notes stay audible after gate-off until
   ACCENT drops at the step's end.
5. **Slide latch order:** `sld` at this trigger decides what happens at the
   next trigger (automation order: restore → drain → fire).
6. **Re3/Sw3 are filtered output captures**, not raw VCO. Do not use them.
7. **The prewarp is essential.** Without `tan(θ)/θ`, the cutoff runs −391
   cents flat at 13 kHz.
