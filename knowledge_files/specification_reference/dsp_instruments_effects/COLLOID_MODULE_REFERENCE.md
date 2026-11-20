# Colloid Module Reference

How the Colloid instrument (6th type, polyphonic pad/drone synthesiser)
works as firmware on the LXR-02. This document covers the module layout,
signal path, control path, parameter table, build integration and cost.
For the DSP design rationale and equations, see
`COLLOID_MODELLING_REFERENCE.md`; for the instrument's place in the DSP
pipeline alongside the other five types, see `INSTRUMENTS_DSP_REFERENCE.md`.

- **Sessions:** S095 P2 (core implementation), S095 P3 (follow-ups),
  S096 P2 (instrument fixes), S096 P3 (gate, pitch envelope, noise,
  wavetable scan, MIDI note modes, Basic-share acceptance), S099 P1/P2
  (chopped PWM R-S wave, Advanced Colloid `clx` with BBD chorus, PWM Sin
  removed, amp-attack minimum removed, effect page rework).
- **Type:** `INSTRUMENT_TYPE_COLLOID` (Basic) and `INSTRUMENT_TYPE_CLX`
  (Advanced Colloid). The Basic Colloid may take any of the six slots
  without counting towards the Advanced limit. The Advanced Colloid (`clx`)
  runs the same engine plus a BBD chorus pass.
- **S099 changes:** Chopped PWM wave R-S added (`COL_PWM_RS` 3,
  `col_chopDcInit()` + `col_chopPairAdd()` in ITCM `.itcm.s099p1`);
  REx tried and removed by ear; PWM Sin removed from all engines
  (`COL_PWM_WAVE_COUNT` 5→4: Tri/Saw/Rec/R-S); R-S width narrowed
  to 0.39% floor (`oscPwmDRec`); amp-attack minimum removed (all
  envelopes run plain ADSR from gate); Advanced Colloid type `clx`
  (BBD chorus kernel, half-rate wet `BBD_COL_HALF_RATE`, 63 rows);
  effect page fx/dpt/rte/mix on page 4; `csr` derived from route.
  Voice struct 1,224 B (was 1,236 B after S096, was 1,112 B after S095 P3).
- **S096 changes:** C1 real note gate, C2a pitch envelope (third ADSR),
  C2b noise source (`nos`), C2c wavetable scan (`wts`), oscillator count
  10 for wav/wtb and 6 for pwm/wts (was 12/6), UBP and off filter types,
  filter envelope range widened, PWM gliding fix, Basic-share B1-B6
  optimisation, voice struct 1,236 B (was 1,112 B), 59 parameter rows
  (was 50).

---

## 1. Files

| File | Role |
|---|---|
| `Core/DSP/Instruments/Colloid/ColloidVoice.h/.c` | Voice struct (1,224 B after S099; was 1,236 B after S096, 1,112 B after S095 P3), init, trigger, gate machine (C1), pitch envelope (C2a), noise source (C2b), async (control rate), sync-block (audio rate), chop block (`col_chopDcInit`, `col_chopPairAdd` in ITCM, `col_chopBlock`), parameter apply. S099: amp-attack minimum removed; all envelopes run plain ADSR from gate |
| `Core/DSP/Instruments/Colloid/ColloidOsc.h/.c` | Integer pair-loop oscillator engine: `col_read()`, `col_pulse()`, four loop functions in ITCM; noise loop (C2b, no ITCM); wavetable scan loops (C2c). S099: PWM Sin form WS loops removed |
| `Core/DSP/Instruments/Colloid/ColloidParameters.h/.c` | Parameter descriptor table (59 rows Basic / 63 rows Advanced clx after S099; was 59 after S096, 50 after S095 P3), 8 pages (Basic) / 8+ pages (clx), labels, format helpers, cell-inactive predicates |
| `Core/DSP/Instruments/Colloid/ColloidTables.h/.c` | Position tables (PWM width indexed by pos), interval tables, format lookup tables |
| `Core/DSPAudio/AlkalinePostTables.c/.h` | Shared post-FX tables (tanh drive, tone); also used by Alkaline |
| `SD_CARD/Instrument/Colloid/init.col` | Default instrument file |

Build: voice and oscillator files in `DSP_SRCS` with explicit `-Ofast`
rule; parameters in normal sources (`-O2`); tables in normal sources.

---

## 2. Registry

| Field | Value |
|---|---|
| Enum | `INSTRUMENT_TYPE_COLLOID` (before `INSTRUMENT_TYPE_TKT`; was before `INSTRUMENT_TYPE_UNKNOWN` prior to S096) |
| Token | `col` (3 characters, as required by the storage format) |
| Extension | `.col` |
| Directory | `/Instrument/Colloid` (FAT 8.3-safe) |
| Display label | `Colloid ` (fits 8 LCD cells) |
| Flags | `INSTRUMENT_FLAG_BASIC` |
| Struct | `ColloidVoice` in the `InstrumentRuntimeSlot` union (1,224 B after S099; was 1,236 B after S096, 1,112 B after S095 P3) |

The registry row is designated (`[INSTRUMENT_TYPE_COLLOID] = { ... }` in
`instrument_registry[]`). The type was appended (never inserted), so the
`/.hcnames` header check accepts older headers.

---

## 3. Voice Struct (`ColloidVoice`, 1,224 B after S099; was 1,236 B after S096, 1,112 B after S095 P3)

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
| `eg[3]` | `ColEg` | Three envelope generators: AEG, modulation EG, pitch EG (C2a, S096) |
| `lfo` | `Lfo` | LFO state |
| `oscInfo` | `OscInfo` | Oscillator configuration (count, type, wave, pwm mode); count 10 for wav/wtb, 6 for pwm/wts (S096) |
| `gateState` | `uint8_t` | Gate machine state: IDLE/ATTACK/HOLD/RELEASE (C1, S096) |
| `noiseState` | (struct) | Noise source state: RNG, HP, SVF, tone/width/pan (C2b, S096) |
| `scanIndex` | `float` | Wavetable scan position for `wts` type (C2c, S096) |
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
   - PWM Tri: warp pair loops in `.itcm.s095p3` (Sin removed S099).
   - PWM R-S (chopped ramp, S099): `col_chopPairAdd` in `.itcm.s099p1`
     (306 B). Preceded by `col_chopDcInit()` (flash) which computes the
     DC correction per block from start/end widths. Two-pass
     `col_chopBlock()`: pass 1 computes six-oscillator mean, pass 2
     normalises each pair's gate with packed Q14 gain and applies per-osc
     level through the existing bus multiplies.

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

## 6. Trigger, Gate and Release

**Trigger** (`Colloid_trigger`):
- Resets envelopes to attack state (AEG, mod EG, pitch EG)
- Does NOT reset oscillator phases (free-running, decision C-D2 context)
- Latches wavetable frames for wavetable oscillator types (C-D4)
- Latches scan start frame for `wts` scan type (C2c, S096)
- If `sus` switch is on and transport is running, envelope will hold at
  sustain level

**Gate (C1, S096):**
- Real note gate based on step length, length special, sustain switch, and
  Scene/track change. Stage machine: IDLE → ATTACK → HOLD → RELEASE.
- Gate length is the step's trigger length (or the length special value).
- `sus` on + transport running: gate holds indefinitely (the S095 behaviour).
- Scene change or track reassignment forces release.
- Constant CPU: the gate machine runs every block regardless of state.

**Release** (implicit):
- Envelope enters release stage when gate ends
- Voice goes idle when envelope reaches zero

**Transport stop**:
- All Colloid voices with `sus` on enter release immediately
- This is checked every block: `susOn = sus && seq_isRunning()`

---

## 7. Parameter Table (59 Rows after S096, 8 Pages)

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
| `typ` | MENU | wav/smp/slp/wtb/pwm/wts/nos | Oscillator type selector (flag SELECTOR, not morphable); `wts` = wavetable scan (C2c, S096), `nos` = noise (C2b, S096) |
| `wav` | MENU | depends on typ | Waveform: Saw/Rec/Tri/Sin/wavetable for wav; sample list for smp/slp; wavetable list for wtb; Tri/Saw/Rec/R-S for pwm (Sin removed S099, REx tried and removed S099); wavetable list for wts; fixed for nos |
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
| `pta` | 0B127 | 0..127 | Pitch EG attack (C2a, S096) |
| `ptd` | 0B127 | 0..127 | Pitch EG decay (C2a, S096) |
| `pam` | PM63 | −63..+63 | Pitch EG amount ±24 semitones, 0.38 st/step (C2a, S096) |
| `ntn` | 0B127 | 0..127 | Noise tone (C2b, S096) |
| `nwd` | 0B127 | 0..127 | Noise width (C2b, S096) |
| `npn` | PM63 | −63..+63 | Noise pan (C2b, S096) |
| `scn` | 0B127 | 0..127 | Wavetable scan speed (C2c, S096) |
| `cnt` | MENU | — | Oscillator count selector (10/6; S096 B1-B6) |

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
| `.itcm.s095p3` | PWM Tri warp pair loops (store and add variants; Sin loops removed S099) | ~350 B (was ~350 B; Sin loops freed 856 B total ITCM) |
| `.itcm.s096p3` | Colloid noise loop (C2b) | — |
| `.itcm.s099p1` | `col_chopPairAdd` (R-S chop loop, S099) | 306 B |

Total ITCM after S099: 15,432 / 16,384 B (94.2 %; 952 B free). Was 15,976 after S096,
16,288 after S099 P1 P-C (before PWM Sin removal), 15,232 after S095.

---

## 9. Cost

### 9.1 Device measurements

After B1-B6 optimisation (S096), all Basic Colloid modes fit the Basic share
(19,620 cycles/block). Oscillator count: 10 for wav/wtb, 6 for pwm/wts/nos.

| Configuration | Cycles/block | Within budget? |
|---------------|-------------|----------------|
| wav 10, stereo filter | ≤ 19,620 | Yes (Basic = 19,620) |
| pwm 6, Saw/Rec | ≤ 19,620 | Yes |
| pwm 6, Tri (Sin removed S099) | ≤ 19,620 | Yes |
| pwm 6, R-S (chop, S099) | ≤ 19,620 | Yes (host: chop6-par 387.8, below pwm6-par) |
| wts 6, wavetable scan | ≤ 19,620 | Yes |
| nos 6, noise | ≤ 19,620 | Yes |
| **Basic worst case** | **≤ 19,620** | Yes |

**Advanced Colloid (`clx`, S099):** the Basic engine plus a BBD chorus pass.
Device measurements from `SD_CARD_ADV/asavetrc.bin` (slot 3 as clx):

| Measure | Value | Cap |
|---------|-------|-----|
| Median | 21,034 | 21,803 (Advanced) |
| p95 | 22,397 | 21,803 |
| Max | 23,076 | — |

The Advanced step costs +2,026 cycles at the median (instruction model: 2,025
at CPI 1.4). The p95 exceedance comes from the Colloid engine itself (its
Basic p95 20,267 was already above the Basic share). Host: `clx-pwm6-par`
449.8 instructions/sample (chorus 47.0, with half-rate wet `BBD_COL_HALF_RATE`).

Cost per instruction: 1.37–1.41 cycles/instr.

### 9.3 Note length (S099)

The Colloid note is as long as the step (or the length special's window). All
three envelopes (amp, filter, pitch) run a plain ADSR from the gate. The S096
P3 amp-attack minimum is removed: a step shorter than the amp attack now
releases mid-attack from the level it reached. `col_egStep()` takes no
`isAmp` argument. `heldFP` is removed. Every Colloid mode's per-block cost
fell by 56 instructions as a result.

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

The oscillator count is per-type: 10 for wav/wtb, 6 for pwm/wts/nos
(S096 B1-B6). To change it:
1. Update the per-type count constants and the corresponding
   `COL_NORM_WAV`/`COL_NORM_PWM` normalisation factors.
2. Verify that the voice struct still fits within the tagged runtime slot
   (1,264 B reserve).
3. Device-measure the new cost against the Basic budget (19,620 cycles).
4. The pair-loop count in the sync block is count / 2.

### Adding a filter mode

1. Add the mode to the `typ` parameter menu list.
2. Add the output selection in the per-sample filter evaluation.
3. Update `col_svfControl()` if the new mode needs different coefficient
   preparation.
4. Verify cost on device.
