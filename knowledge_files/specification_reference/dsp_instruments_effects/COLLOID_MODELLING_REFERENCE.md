# Colloid Modelling Reference

Design rationale and DSP equations for the Colloid instrument (the 6th
instrument type in the LXR-02 firmware). This document covers the signal
path architecture, oscillator pair-loop design, filter topology, envelope
design, and stereo positioning model. For the firmware implementation, see
`COLLOID_MODULE_REFERENCE.md`; for the instrument's place in the DSP
pipeline, see `INSTRUMENTS_DSP_REFERENCE.md`.

- **Sessions:** S095 P2 (core implementation), S095 P3 (follow-ups),
  S096 P2 (instrument fixes), S096 P3 (gate, pitch envelope, noise,
  wavetable scan, Basic-share acceptance).
- **Design origin:** `S095_P2_DRAFT_COLLOID.md` (the plan, deleted after
  logging) and its decisions C-D1..C-D21.
- **S096 extensions:** C1 real note gate, C2a pitch envelope (third ADSR,
  ±24 semitones), C2b noise source (hardware RNG, one-pole HP + TPT SVF),
  C2c wavetable scan (`wts`, COL_SCAN_COUNT 6, interpolated frames),
  oscillator count reduced to 10/6 for Basic-share acceptance (B1-B6),
  UBP and off filter types, filter envelope range widened.

---

## 1. Concept and Constraints

### 1.1 Design goal

A polyphonic pad/drone instrument for the LXR-02 drum machine: a stack of
free-running oscillators with rich stereo field, simple subtractive
filtering, and an ADSR envelope with a sustain switch. The instrument must
fit within the **Basic** CPU class (19,620 cycles per 32-sample block at
216 MHz) and use no additional SRAM beyond the existing 1,176 B tagged
runtime voice slot.

### 1.2 Constraints

| Resource | Budget |
|----------|--------|
| CPU | Basic class: 19,620 cycles/block |
| Voice struct | ≤ 1,176 B (tagged runtime slot) |
| Flash | ≤ +8,192 B net (approved P2 budget line) |
| ITCM | ≤ +1,024 B (approved P2 budget) |
| SRAM | 0 B new |

### 1.3 Key design decisions

| ID | Decision | Why |
|----|----------|-----|
| C-D1 | Basic class | Measured at 17,433 worst case (S095); all modes within 19,620 after S096 B1-B6 |
| C-D2 | 10 plain / 6 PWM (S096; was 12/6) | B1-B6 optimisation reduced wav/wtb count to fit Basic share |
| C-D8 | Sustain switch, not gate length | Automatable, drum-machine ergonomics |
| C-D10 | 1,024-point sine (all engines) | −6,144 B flash, perceptually transparent |
| C-D13 | Linear mid/side pan law | No sqrt per sample |
| C-D19 | Sustain holds only while transport runs | Transport stop = hands off keys |

---

## 2. Signal Path

```
Oscillator bank (N oscillators)
  ├─ Each osc: table read → (optional PWM second read) → stereo position
  ├─ Accumulate into mid bus (M) and side bus (S)
  └─ Pair loop: two oscillators per iteration, integer DSP instructions

Mid/Side → Stereo conversion
  L = M + 2·S
  R = M − 2·S

Filter (TPT SVF, division-free)
  ├─ Series mode: mono input → SVF → SVF (cascaded)
  └─ Parallel/stereo mode: L → SVF₁, R → SVF₂

Post-filter drive (OTA tanh) + one-pole tone

ADSR envelope → gain

Output: stereo int16 pair
```

### 2.1 Configuration modes

| Filter mode (`str`) | Routing |
|---------------------|---------|
| `str` = 0 (mono series) | M+S → mono → filter₁ → filter₂ → drive/tone → envelope → stereo out |
| `str` = 1 (stereo parallel) | L → filter₁, R → filter₂ → drive/tone → envelope → stereo out |

In series mode the stereo field collapses to mono before filtering and is
reconstructed from the mid bus after the envelope. In parallel mode each
filter processes one stereo channel independently.

---

## 3. Oscillator Bank

### 3.1 Architecture

N free-running oscillators (N = 12 for plain waves, N = 6 for PWM). Each
oscillator has:
- A 32-bit phase accumulator (integer, free-running — never reset on
  retrigger)
- A per-oscillator pitch (base + interval + detune + pitch slew)
- A per-oscillator stereo position (base + spread + motion)
- A waveform read from a 1,024-point table

Oscillators are grouped in pairs (o, o+1) and rendered by **pair loops**
that process both oscillators in a single function call, accumulating into
the mid and side buses.

### 3.2 Pair-loop DSP

The pair loop is the innermost hot path, running in ITCM for single-cycle
fetch. For each sample in the 32-sample block:

```
For each pair (o, o+1):
  sample_o   = col_read(table, phase[o])
  sample_o1  = col_read(table, phase[o+1])
  
  // Stereo position via ARM DSP halfword multiply:
  // pos_o is a Q15 mid/side weight pair packed into a 32-bit word
  // SMULWB extracts the mid component, SMLAWT extracts the side
  busM += SMLAWB(sample_o, pos[o])    // mid accumulate
  busS += SMLAWT(sample_o, pos[o])    // side accumulate
  busM += SMLAWB(sample_o1, pos[o+1])
  busS += SMLAWT(sample_o1, pos[o+1])
  
  phase[o]  += inc[o]
  phase[o+1] += inc[o+1]
```

The first pair uses `Store` variants (initialise buses), subsequent pairs
use `Add` variants (accumulate). This avoids a branch or zero-init cost.

### 3.3 Table read

`col_read(table, phase)` performs linear interpolation on a 1,024-point
table:

```
index = phase >> 22           // top 10 bits = table index
frac  = (phase >> 6) & 0xFFFF // next 16 bits = interpolation fraction
a     = table[index]
b     = table[(index + 1) & 1023]
result = a + ((b - a) * frac >> 16)
```

The result is a Q15 (signed 16-bit) sample. The shift amounts (22 for
index, 6 for fraction) are consequences of the 1,024-point table size:
32 − 10 = 22 for the index, leaving 22 fractional bits of which the top
16 are used for interpolation.

### 3.4 PWM (pulse-width modulation)

`col_pulse(table, phase, width)` performs a two-read operation:

```
a = col_read(table, phase)
b = col_read(table, phase + width)
result = a - b     // or a + b for Saw variant (sign parameter)
```

This produces a pulse wave whose width is controlled by `pos` (after the
P4 position law fix: 128 distinct widths from position-indexed tables,
`pos` 0 = 50 %, `pos` 127 = narrowest).

PWM waveforms (after P3 F2):
- **Rec** (rectangle): two-read subtract
- **Saw**: two-read add with sign flip
- **Sin**: phase-warped sine via warp pair loops
- **Tri**: phase-warped triangle via warp pair loops

Rec/Saw share one loop with a sign parameter. Sin/Tri share warp pair
loops in ITCM (`.itcm.s095p3`).

### 3.5 Pitch model

Each oscillator's pitch is computed per block:

```
pitch_target[o] = base_pitch
                + interval[o mod 4]     // in1/in2/in3 semitones
                + detune_spread[o]      // ±250 cents max (COL_DETUNE_MAX_LN)

pitchCur[o] += slew_rate × (pitch_target[o] − pitchCur[o])  // log2 Hz domain

phase_inc[o] = alk_exp2f(pitchCur[o])   // convert log2 Hz to phase increment
```

Pitch slew (P3 F3) operates per oscillator in the log2 Hz domain, using
`alk_exp2f` (the fast 2^x from the Alkaline engine). This replaced the
draft's `por`/`pmd`/`lgt` portamento system which mapped poorly to
free-running oscillators.

**Intervals** (P3 F4): `in1`/`in2`/`in3` are PM63 semitones (−63..+63
centred at 63 = unison). Oscillators are assigned by `o mod 4`:
- Group 0: unison
- Group 1: `in1` offset
- Group 2: `in2` offset
- Group 3: `in3` offset

### 3.6 Stereo position model

Each oscillator's position is a mid/side weight pair:

```
pos_base[o] = centre + spread_offset[o] + motion_lfo[o]
```

Where:
- `spread_offset[o]` fans oscillators across the stereo field (×5 range
  after P3 F6), with a soft-edge law to prevent hard panning
- `motion_lfo[o]` is a per-oscillator slow LFO modulation of position
  (depth 0.6 √mot, rate 0.05→2 Hz, own rate ratios, parabolic sine;
  decision C-D15)
- The position is packed into a 32-bit word with mid weight in the bottom
  halfword and side weight in the top halfword, for direct use by
  `SMLAWB`/`SMLAWT`

**Mid/side to stereo:** `L = M + 2S`, `R = M − 2S`. The factor of 2 on
S compensates for the half-power in the mid/side decomposition, giving
unity gain at centre.

### 3.7 Wave position spread (psp)

`psp` controls how the oscillator bank's wave table positions are spread
(P3 F7). Applied with a `psp^1.393` power curve for more resolution in
the musically useful lower range. Range ×5 after P3.

---

## 4. Filter (Division-Free TPT SVF)

### 4.1 Topology

The trapezoidal-integrated state-variable filter (TPT SVF), implemented
without any division per sample. The standard TPT SVF requires a division
by `(1 + g + g·k)` to solve the implicit equation; Colloid avoids this by
precomputing the reciprocal per block during `col_svfControl()`.

### 4.2 Per-sample equations

```
State: s1, s2 (two integrator states)
Input: x
Parameters: g (cutoff coefficient), k (damping = 1/Q)

v1 = g · (x − s2 − k·s1 − s1)   // division-free: g already has the
v1 = v1 / (1 + g + g·k)           //   reciprocal baked in per block
hp = x − v1 − k·s1 − s2
bp = v1 + s1
lp = g·bp + s2

s1 = bp + v1
s2 = lp + g·bp
```

In practice the division is replaced by multiplying with
`1.0f / (1.0f + g + g * k)` precomputed at block rate. The filter output
is selected by `typ`:
- 0: LP (low-pass)
- 1: BP (band-pass)
- 2: HP (high-pass)
- 3: Notch (LP + HP)

### 4.3 Drive

Cubic waveshaping on the filter input, division-free:

```
drive(x) = x · (1.5 − 0.5 · x²)   for |x| ≤ 1
         = ±1                        for |x| > 1
```

This is cheaper than tanh and provides soft saturation. The drive amount
scales the input before the waveshaper.

### 4.4 Filter configuration (after P3 F9)

One filter with offset parameters:
- `frq` — base cutoff frequency
- `res` — base resonance
- `typ` — filter type (LP/BP/HP/Notch)
- `rte` — cutoff keyboard tracking rate
- `fro` — frequency offset (applied to second filter in stereo mode)
- `reo` — resonance offset
- `eno` — envelope-to-cutoff offset
- `dro` — drive offset

In stereo parallel mode (`str` = 1), filter₁ uses the base parameters and
filter₂ uses base + offsets, creating stereo movement. In mono series
mode, the offsets create a two-stage filter with different character per
stage.

### 4.5 Cost

72 filter evaluations per sample in parallel mode (two filters × 12
oscillators feeding each × 3 operations per evaluation). This is the
dominant cost in the Colloid signal path after the oscillator pair loops.

---

## 5. Envelope (ADSR with Sustain Switch)

### 5.1 Shape

Standard four-stage ADSR:
- **Attack** (`atk`): linear ramp from 0 to 1
- **Decay** (`dec`): exponential fall from 1 to sustain level
- **Sustain** (`slv`): held level (0..127 → 0..1)
- **Release** (`rel`): exponential fall from current level to 0

### 5.2 Sustain switch

The `sus` parameter (on/off, automatable) controls whether the envelope
holds at the sustain level:
- `sus` ON **and** transport running: envelope holds at sustain level
  indefinitely
- `sus` OFF or transport stopped: envelope enters release immediately
  after decay completes

```
susOn = sus && seq_isRunning()
```

This means transport stop releases all Colloid voices (decision C-D19).

### 5.3 Modulation

- `dst` — velocity/modulation destination target
- `amt` — modulation amount
- `slw` — slew rate for the modulation target (smoothing)

Velocity modulation routes through the `dst` targets only (decision C-D18),
not through the envelope level directly.

---

## 6. Post-Filter Processing (P3 F10)

### 6.1 OTA tanh drive

After the SVF filter output, an OTA-style (operational transconductance
amplifier) tanh saturator:

```
y = tanh(gain · x)
```

Implemented using Alkaline's existing drive tables from
`AlkalinePostTables.c/.h` (the A3 tables). The `gain` is controlled by
the drive parameter.

### 6.2 One-pole tone

A simple one-pole low-pass after the drive:

```
z += c · (y − z)
```

Where `c` is derived from the tone parameter. Also from Alkaline's
existing tone implementation.

---

## 7. Level Management

### 7.1 Normalisation

The oscillator bank output is normalised by `1/√N` before entering the
drive (decision C-D12, by ear):
- `COL_NORM_WAV` — normalisation factor for plain waves (12 oscillators)
- `COL_NORM_PWM` — normalisation factor for PWM waves (6 oscillators)

### 7.2 Headroom and makeup

After the P3 device test revealed clipping:
- `COL_HEADROOM` multiplied by 0.35 (reduce level into the filter/drive)
- `COL_MAKEUP` multiplied by 1.43 (compensate at the output)

These values were set by ear on the device.

---

## 8. Measurement Results

### 8.1 CPU cost

After S096 B1-B6 optimisation, oscillator counts are 10 for wav/wtb and
6 for pwm/wts/nos. All modes fit the Basic share (19,620 cycles/block).

| Configuration | Cycles/block | Within budget? |
|---------------|-------------|----------------|
| wav 10, stereo filter | ≤ 19,620 | Yes |
| pwm 6, Saw/Rec | ≤ 19,620 | Yes |
| pwm 6, Sin/Tri | ≤ 19,620 | Yes |
| wts 6, wavetable scan | ≤ 19,620 | Yes |
| nos 6, noise | ≤ 19,620 | Yes |

(S095 P4 measurements before B1-B6: plain 12 stereo 12,227 cycles, pwm 6
Saw/Rec 10,371, pwm 6 Sin/Tri 10,435, worst case wtb 12 stereo 17,433.)

### 8.2 Memory

| Resource | Size |
|----------|------|
| Voice struct (after S096) | 1,236 B (was 1,112 B after S095 P3) |
| Flash contribution (cumulative) | Part of 743,016 B total |
| ITCM (pair loops + noise) | Part of 15,976 B total |
