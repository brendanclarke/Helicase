# Colloid Module Reference

How the Colloid instrument (6th type, polyphonic pad/drone synthesiser)
works as firmware on the LXR-02. This document covers the module layout,
signal path, control path, parameter table, build integration and cost.
For the DSP design rationale and equations, see
`COLLOID_MODELLING_REFERENCE.md`; for the instrument's place in the DSP
pipeline alongside the other five types, see `INSTRUMENTS_DSP_REFERENCE.md`.

- **Sessions:** S095 P2 (core implementation), S095 P3 (follow-ups).
- **Type:** `INSTRUMENT_TYPE_COLLOID`, Basic class (may take any of the
  six slots without counting towards the Advanced limit).

---

## 1. Files

| File | Role |
|---|---|
| `Core/DSP/Instruments/Colloid/ColloidVoice.h/.c` | Voice struct (1,112 B), init, trigger, async (control rate), sync-block (audio rate), parameter apply |
| `Core/DSP/Instruments/Colloid/ColloidOsc.h/.c` | Integer pair-loop oscillator engine: `col_read()`, `col_pulse()`, four loop functions in ITCM |
| `Core/DSP/Instruments/Colloid/ColloidParameters.h/.c` | Parameter descriptor table (50 rows, 8 pages), labels, format helpers, cell-inactive predicates |
| `Core/DSP/Instruments/Colloid/ColloidTables.h/.c` | Position tables (PWM width indexed by pos), interval tables, format lookup tables |
| `Core/DSPAudio/AlkalinePostTables.c/.h` | Shared post-FX tables (tanh drive, tone); also used by Alkaline |
| `SD_CARD/Instrument/Colloid/init.col` | Default instrument file |

Build: voice and oscillator files in `DSP_SRCS` with explicit `-Ofast`
rule; parameters in normal sources (`-O2`); tables in normal sources.

---

## 2. Registry

| Field | Value |
|---|---|
| Enum | `INSTRUMENT_TYPE_COLLOID` (before `INSTRUMENT_TYPE_UNKNOWN`) |
| Token | `col` (3 characters, as required by the storage format) |
| Extension | `.col` |
| Directory | `/Instrument/Colloid` (FAT 8.3-safe) |
| Display label | `Colloid ` (fits 8 LCD cells) |
| Flags | `INSTRUMENT_FLAG_BASIC` |
| Struct | `ColloidVoice` in the `InstrumentRuntimeSlot` union (1,112 B, fits the reserve) |

The registry row is designated (`[INSTRUMENT_TYPE_COLLOID] = { ... }` in
`instrument_registry[]`). The type was appended (never inserted), so the
`/.hcnames` header check accepts older headers.

---

## 3. Voice Struct (`ColloidVoice`, 1,112 B after P3)

Key fields:

| Field | Type | Purpose |
|---|---|---|
| `phase[12]` | `uint32_t[12]` | Per-oscillator phase accumulators (free-running) |
| `inc[12]` | `uint32_t[12]` | Per-oscillator phase increments |
| `pitchCur[12]` | `float[12]` | Per-oscillator current pitch in log2 Hz (for slew) |
| `posPair[12]` | `uint32_t[12]` | Packed mid/side position weights (Q15 bottom, Q15 top) |
| `pwD[12]` | `uint32_t[12]` | PWM pulse-width delta per oscillator |
| `busM[32]` | `int32_t[32]` | Mid bus accumulator (one per sample in block) |
| `busS[32]` | `int32_t[32]` | Side bus accumulator |
| `right[32]` | `int16_t[32]` | Right-channel output block |
| `svf[2]` | `ColSvf` | Two TPT SVF filter states |
| `eg[2]` | `ColEg` | Two envelope generators (AEG + modulation EG) |
| `lfo` | `Lfo` | LFO state |
| `oscInfo` | `OscInfo` | Oscillator configuration (count, type, wave, pwm mode) |
| `parameters` | (various) | Cached parameter values from Scene image |
| `state` | `uint8_t` | Voice state (idle/attack/decay/sustain/release) |

---

## 4. Signal Path (Audio Rate)

The sync-block function (`Colloid_calcVoiceAsync` / `Colloid_calcSyncBlock`)
runs once per 32-sample block:

### 4.1 Oscillator render

1. **Control update** (`col_oscControl`): compute per-oscillator pitch
   targets from base pitch + intervals (in1/in2/in3, PM63 semitones,
   `o mod 4` grouping) + detune spread (±250 cents `COL_DETUNE_MAX_LN`),
   apply pitch slew in log2 Hz domain, convert to phase increment via
   `alk_exp2f`, compute stereo position from spread + motion LFO, pack
   into `posPair[]`.

2. **Pair loops** (ITCM `.itcm.s095p2` and `.itcm.s095p3`):
   - Plain waves: `col_oscPairStore` (first pair), then
     `col_oscPairAdd` × (N/2 − 1) subsequent pairs. Each pair reads two
     table values, multiplies by packed position via `SMULWB`/`SMLAWB`/
     `SMLAWT`, accumulates into `busM[]` and `busS[]`.
   - PWM Rec/Saw: `col_pwmPairStore` / `col_pwmPairAdd` with sign
     parameter.
   - PWM Sin/Tri: warp pair loops in `.itcm.s095p3`.

3. **Normalisation**: `busM` and `busS` scaled by `COL_NORM_WAV` (plain)
   or `COL_NORM_PWM` (PWM), which is `1/√N` by ear (decision C-D12).

### 4.2 Stereo conversion

```
L = M + 2·S
R = M − 2·S
```

Applied per sample. In mono series filter mode, only M is used (S is
discarded before filtering and reconstructed after).

### 4.3 Filter

Per sample, the TPT SVF evaluates with precomputed reciprocal
(`1 / (1 + g + g·k)` from `col_svfControl`). Filter type selected by
`typ` parameter (LP/BP/HP/Notch).

In stereo parallel mode: filter₁ processes L, filter₂ processes R.
In mono series: filter₁ then filter₂ in cascade on the mono signal.

Filter offset parameters (P3 F9) allow filter₂ to differ from filter₁
in frequency (`fro`), resonance (`reo`), envelope depth (`eno`), and
drive amount (`dro`).

### 4.4 Post-filter

1. **OTA tanh drive**: lookup from `AlkalinePostTables` (A3 drive tables).
2. **One-pole tone**: `z += c · (y − z)`, coefficient from tone parameter.
3. **Headroom/makeup**: `COL_HEADROOM` (×0.35) applied before filtering;
   `COL_MAKEUP` (×1.43) applied after envelope.

### 4.5 Envelope

ADSR with sustain switch. `susOn = sus && seq_isRunning()`. The envelope
gain is applied to the stereo output pair. Transport stop forces release.

### 4.6 Output

The final stereo int16 pair is written to the voice output buffer for
the mixer. The mixer applies per-slot volume, pan, fader, and send as
usual.

---

## 5. Control Path (Block Rate)

`Colloid_calcVoiceAsync()` runs at block rate (every 32 samples) before
the sync block:

1. Read and cache parameters from the Scene image
2. Compute filter coefficients (`col_svfControl`)
3. Compute envelope rates
4. Update LFO
5. Apply modulation targets
6. Compute per-oscillator pitch targets and positions

---

## 6. Trigger and Release

**Trigger** (`Colloid_trigger`):
- Resets envelopes to attack state
- Does NOT reset oscillator phases (free-running, decision C-D2 context)
- Latches wavetable frames for wavetable oscillator types (C-D4)
- If `sus` switch is on and transport is running, envelope will hold at
  sustain level

**Release** (implicit):
- Envelope enters release stage
- Voice goes idle when envelope reaches zero

**Transport stop**:
- All Colloid voices with `sus` on enter release immediately
- This is checked every block: `susOn = sus && seq_isRunning()`

---

## 7. Parameter Table (50 Rows, 8 Pages)

### Page layout

| Page | Rows | Content |
|------|------|---------|
| OSC | ~8 | `typ` `wav` `pos` `psp` `in1` `in2` `in3` `det` `mot` `cnt` |
| AEG | 4 | `atk` `dec` `slv` `rel` |
| MOD | 4 | `dst` `amt` `sus` `slw` |
| FM | ~3 | FM depth, FM source, cross-modulation |
| Click | ~2 | Transient click selection, click level |
| FILTER | ~8 | `frq` `res` `typ` `rte` `fro` `reo` `eno` `dro` `str` |
| LFO | ~6 | Standard LFO rows |
| MIX | ~5 | `vol` `pan` `ton` `drv` `snd` |

### Key parameter details

| Row | Dtype | Range | Notes |
|-----|-------|-------|-------|
| `typ` | MENU | wav/smp/slp/wtb/pwm | Oscillator type selector (flag SELECTOR, not morphable) |
| `wav` | MENU | depends on typ | Waveform: Saw/Rec/Tri/Sin/wavetable for wav; sample list for smp/slp; wavetable list for wtb; Rec/Saw/Sin/Tri for pwm |
| `pos` | 0B127 | 0..127 | Wave position / PWM pulse width (128 distinct widths after S094 P4 fix) |
| `psp` | 0B127 | 0..127 | Wave position spread across oscillators (psp^1.393 curve, ×5 range) |
| `in1`/`in2`/`in3` | PM63 | −63..+63 | Interval semitones, applied by o mod 4 grouping |
| `det` | 0B127 | 0..127 | Detune amount (±250 cents max, spread across oscillators) |
| `mot` | 0B127 | 0..127 | Stereo motion LFO depth (×5 range) |
| `sus` | ON_OFF | 0/1 | Sustain switch (holds only while transport runs) |
| `str` | ON_OFF | 0/1 | Stereo parallel filter mode (1 = parallel, 0 = mono series) |
| `frq` | 0B127 | 0..127 | Filter cutoff frequency |
| `fro` | PM63 | −63..+63 | Filter₂ frequency offset from filter₁ |
| `drv` | 0B127 | 0..127 | Post-filter OTA tanh drive amount |
| `ton` | 0B127 | 0..127 | Post-filter one-pole tone |

### Cell-inactive predicates

`colloidParameters_cellInactive()` returns true for:
- `spr` and `mot` when filter mode is mono series (not stereo parallel):
  displayed as `---`
- Offset rows (`fro`/`reo`/`eno`/`dro`) when filter mode is mono series
  and the offset would have no audible effect

---

## 8. ITCM Layout

| Section | Content | Size (approx.) |
|---------|---------|----------------|
| `.itcm.s095p2` | `col_oscPairStore`, `col_oscPairAdd`, `col_pwmPairStore`, `col_pwmPairAdd` | ~600 B |
| `.itcm.s095p3` | PWM Sin/Tri warp pair loops (store and add variants) | ~350 B |

Total ITCM after S095: 15,232 / 16,384 B (93.0 %).

---

## 9. Cost

### 9.1 Device measurements (P4 §7)

| Configuration | Cycles/block | Within budget? |
|---------------|-------------|----------------|
| plain 12, stereo filter | ~12,227 | Yes (Basic = 19,620) |
| pwm 6, Saw/Rec | ~10,371 | Yes |
| pwm 6, Sin/Tri | ~10,435 | Yes |
| **Worst case** (wtb 12, stereo) | **17,433** | Yes |

Cost per instruction: 1.37–1.41 cycles/instr.

### 9.2 Cost breakdown (approximate)

| Stage | Cost per sample (est.) |
|-------|----------------------|
| Oscillator pair loops (12 osc) | ~180 |
| TPT SVF filters (×2 stereo) | ~72 |
| Post-filter drive + tone | ~20 |
| Envelope | ~8 |
| Stereo conversion + output | ~10 |
| Control update (amortised) | ~15 |

---

## 10. Extension Guide

### Adding a waveform

1. Add the wave table to `ColloidTables.c` (or reuse an existing table).
2. Add the enum value to the waveform menu list in `ColloidParameters.c`.
3. If PWM: add the corresponding warp loop to `ColloidOsc.c` and place it
   in the appropriate ITCM section. Update the loop dispatcher.
4. Update `colloidParameters_cellInactive()` if the new wave affects
   parameter visibility.
5. Run `make -C tools/dsp_test all` to verify host tests still pass.

### Changing the oscillator count

The oscillator count is `COL_OSC_COUNT` (12 for plain, 6 for PWM). To
change it:
1. Update `COL_OSC_COUNT` and the corresponding `COL_NORM_WAV`/
   `COL_NORM_PWM` normalisation factors.
2. Verify that the voice struct still fits within the tagged runtime slot
   (1,176 B reserve).
3. Device-measure the new cost against the Basic budget (19,620 cycles).
4. The pair-loop count in the sync block is `COL_OSC_COUNT / 2`.

### Adding a filter mode

1. Add the mode to the `typ` parameter menu list.
2. Add the output selection in the per-sample filter evaluation.
3. Update `col_svfControl()` if the new mode needs different coefficient
   preparation.
4. Verify cost on device.
