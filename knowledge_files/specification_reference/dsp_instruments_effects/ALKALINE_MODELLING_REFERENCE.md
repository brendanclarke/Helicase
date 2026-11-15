# Alkaline Modelling Reference

How the Alkaline instrument (TB-303 emulation) was designed, modelled and
validated before any firmware code was written. This document covers the
circuit analysis, SPICE simulation, DSP equation derivation, solver design
and validation. For the firmware implementation, see
`ALKALINE_MODULE_REFERENCE.md`; for the instrument's place in the DSP
pipeline, see `INSTRUMENTS_DSP_REFERENCE.md` §5.5.

- **Sessions:** S092 (this work), S093 (implementation).
- **Sources:**
  - [SN] Roland TB-303 Service Notes, first edition, 19 Feb 1982 (all 8 pages).
  - [OL] S. Olney, *CT Modular Book*, ch. 13 "TB-303".
  - [XB] `cozycactus/x0xb0x-vcf-bench` (CC0): ngspice-fitted x0xb0x model.
    Used for part values and topology only; no code copied.
  - Stinchcombe: T. Stinchcombe, TB-303 filter analysis.
  - Zavalishin: V. Zavalishin, *The Art of VA Filter Design* (diode-ladder ZDF).

---

## 1. Circuit overview

The TB-303 voice has four analog blocks, each modelled at transistor level
in ngspice 47 during S092 P1–P4:

| Block | Script | Key components |
|---|---|---|
| VCO and Q8 square shaper | `tools/spice/tb303/vco_q8.py` | C33, Q26, Q28, Q8 (2SA733P) |
| VCF diode ladder | `tools/spice/tb303/vcf.py` | Q12–Q23, C18/C19/C24/C26, Q21, R110 |
| VCA (VEG + OTA) | `tools/spice/tb303/vca.py` | Q31/Q32, C42, R123, IC15 (BA662A) |
| Control network | `tools/spice/tb303/control.py` | Q9/Q10/Q11, TM3, C62, VR7 |
| End-to-end run | `tools/spice/tb303/e2e.py` | All blocks, 8-step pattern |

---

## 2. VCO (P1)

### 2.1 Saw core

- Q26 (2SC1583 matched pair) exponential converter charges C33 (0.01 µF).
- Q24/Q25/Q27 reset C33 from ~5.5 V to ~11.3 V at the top.
- Q28 (2SK30 JFET) source follower buffers the ramp.
- The saw falls (the [SN] p5 scope sketch shows rise to 12 V, ramp down to
  5.5 V; the reset is on the rising edge).
- Modelled bow: 0.47–0.66 % (Q26 Early effect). The core ramp is straight
  enough that `sawTable` is a close match.

### 2.2 Q8 square shaper

The square is a pulse whose width and level change with pitch.

| Component | Value | Role |
|---|---|---|
| R35 | 100 kΩ | Saw → Q8 base (slow path) |
| R34 + C10 | 10 kΩ + 0.01 µF (τ 100 µs) | Fast-edge path in parallel |
| R45 + C11 | 22 kΩ + 1 µF (τ 22 ms) | Emitter to +12 V; sets pulse width vs pitch |
| R36 | 10 kΩ | Collector to ground |

**Mechanism:** C11 recharges through R45 between pulses. A longer period
(lower pitch) gives a longer pulse. Q8's gain hardly matters.

| Note | On-time | Level re. saw at Q12 |
|---|---:|---:|
| C1 (33.9 Hz) | 51.1 % | −2.5 dB |
| C2 (67.7 Hz) | 37.7 % | +0.3 dB |
| C3 (135 Hz) | 28.2 % | +2.7 dB |
| C5 (540 Hz) | 23.2 % | +3.2 dB |

Decision D8: the duty and level per octave are baked into a per-octave-level
square table (`alkSqrTable`), at zero runtime cost.

### 2.3 VCF input coupling

- S1 → R62 220 kΩ → C17 1 µF → Q12 left base.
- R70 2.2 kΩ to bias node (R68 2.2 kΩ from 5.333 V, C16 10 µF).
- Input divider: R62/R70 ≈ 101:1.
- High-pass corner: 0.72 Hz (inaudible; withdraws an earlier 40 Hz hypothesis).
- Saw at Q12: ±1.0 Vt (mild 8 % peak compression in the Q12 pair).

### 2.4 Re3/Sw3 assessment

The existing `Re3`/`Sw3` stock waveforms are digitised captures of a 303's
*filtered output*, not its raw VCO. Their spectrum falls ~30 dB by harmonic
10 (a raw saw falls 20 dB), and `sq3` is not the Q8 pulse shape. Using them
for Alkaline's VCO would put one filter snapshot in front of the emulated
filter. **Not used.**

### 2.5 Open questions

| # | Question |
|---|---|
| V1 | ~20-point duty offset against [OL]'s 45 %/71 % figures |
| V2 | Reset level: 11.3 V (inferred) or 12 V (top compression) |

---

## 3. VCF — diode ladder (P2)

### 3.1 R110 = 22 kΩ (the critical finding)

The schematic print reads 2.2 kΩ. The x0xb0x KiCad values have 22 kΩ. Every
other part matches between the two.

| R110 | Q21 stage gain | Loop gain at Res full, 30 µA | Threshold | Resonance |
|---|---:|---:|---:|---|
| 2.2 kΩ (print) | 10.7 | 1.93 | 19.1 | +1.6 dB — **cannot resonate** |
| **22 kΩ** ([XB]) | **103.6** | **18.7** | 19.1 | **+26.3 dB** — matches the 303 |

Only R110 = 22 kΩ reproduces the [SN] p7 calibration (ring period 2.03 ms,
decay per cycle 0.63). The 303 rings at 98 % of self-oscillation.

### 3.2 Ladder structure

Four stages with spread pole frequencies:

| Stage | Capacitor | Value |
|---|---|---|
| Bottom (rung 0) | C18 | 0.018 µF |
| Rung 1 | C19 | 0.033 µF |
| Rung 2 | C24 | 0.033 µF |
| Rung 3 | C26 | 0.033 µF |

Transistor pairs: Q13/Q15, Q14/Q17, Q16/Q23 (diode-connected). The ladder
current (I) from Q11's collector divides equally: each stage carries I/2,
steered by the tanh of the voltage across the pair.

Top: Q22 pair (2SC2291) on the filter rail (+12 V through R95 100 Ω, C28
47 µF). An AC ground (x4 = 0).

### 3.3 Output stage (Q21)

- C27/C25 0.1 µF (crossed) couple the ladder top to Q21 (2SC1583
  differential pair).
- Q20 tail (R112/R109 base divider, R108 2.2 kΩ emitter), R111 2.2 kΩ,
  **R110 22 kΩ**, Q19 follower (R96 10 kΩ) → C14 1 µF → TP6.
- Voltage gain: 103.6 (with R110 = 22 kΩ and its loading).
- R_IN21 = R114 (100 kΩ) ∥ β·Vt/(I21/2) = 23.3 kΩ (β = 300, I21 = 0.51 mA).
- Output coupling: τ_out = 0.1 µF × R_IN21 = 2.33 ms (68 Hz high-pass).

### 3.4 Resonance loop

TP6 → VR4 50 kΩ(B) → ground; wiper → C15 1 µF → R67 2.2 kΩ → Q18 base
(biased by R99 100 kΩ to C22F 1 µF ∥ R66 100 kΩ from 5.333 V) → Q18
follower (gain 0.99, R94 10 kΩ) → C23 1 µF → R97 10 kΩ → Q12 right base;
R98 2.2 kΩ from Q12 right base to the C16/R68 bias node.

### 3.5 Key results

| Q11 current | Ladder −3 dB | Resonance freq (Res full) | Resonance boost |
|---:|---:|---:|---:|
| 1 µA | 15 Hz | 86 Hz | +2.1 dB |
| 10 µA | 52 Hz | 663 Hz | +16.2 dB |
| 30 µA | 166 Hz | 1,948 Hz | +26.3 dB |
| 100 µA | 591 Hz | 6,421 Hz | +29.3 dB |
| 300 µA | 1,874 Hz | 19,601 Hz | +27.5 dB |

Characteristics:
- Resonance frequency ≈ 12× the ladder's −3 dB point.
- Resonance weakens at low cutoff (+2 dB at 86 Hz vs +29 dB at 6.4 kHz).
- Passband drops with resonance (negative feedback, ~25 dB at Res full).
- High-pass inside the loop: C27/C25 into Q21 (60–120 Hz corner).
- Lower resonance at 7–9 Hz (Stinchcombe's "very real" peak), stable.
- [SN] p7 calibration (Cutoff centre, Res full, saw): ring period 2.03 ms
  (target 2.0 ± 0.5 ms), decay per cycle 0.63.

### 3.6 VCA inputs

Traced by the user to IC15 (BA662A):
- **Input A:** TP6 → C21 0.01 µF → R121 220 kΩ → pin 3.
- **Input B:** VR4 wiper → C22 0.01 µF → R122 100 kΩ → pin 3.
- R124 2.2 kΩ to the bias node (R126 2.2 kΩ from 5.333 V, C37 10 µF).

| Path | HP corner | Level |
|---|---:|---:|
| A alone (TP6 → C21 → R121) | 66 Hz | −40.3 dB |
| A + B, Res 0 | 66 Hz | −40.3 dB |
| A + B, Res full | 127 Hz | −30.2 dB |

Resonance changes the VCA input: +10 dB and HP moves from 66 Hz to 127 Hz.

---

## 4. VCA and VEG (P3)

### 4.1 VEG circuit

| Component | Value | Role |
|---|---|---|
| D36 → R134 | 22 kΩ | Trigger path to Q32 base |
| C41 | 0.1 µF (to ground) | Trigger network time constant |
| Q32 | NPN | Trigger switch (empties C42) |
| R132 | 100 Ω | Discharge resistance (fast attack) |
| C42 | 1 µF (to ground) | Envelope state capacitor |
| R123 | 1.5 MΩ (to +12 V) | Recovery (τ = 1.5 s) |
| Q31 | PNP | Converts C42 voltage to OTA control current |
| D34, D35, R137 | On the gate line | Gate-level determines max decay current |

### 4.2 VEG results

**Gate level = 12 V** (from [SN] p1 Gate Out spec; D34 is on the external
gate line).

| Metric | Value |
|---|---|
| Peak current | 46.0 µA |
| Attack (10→90 %) | 2.5–3 ms (trigger width 0.65–0.75 ms) |
| −6 dB | 1.01 s |
| −20 dB | 2.68 s |
| −40 dB | 3.85 s |
| Silent | 4.51 s |
| Release | < 1 µs (no timing capacitor in the gate path) |
| Close fit | g(t) = 1.052·e^(−t/1.5 s) − 0.052 |

**VEG is a state, not a restart.** C42 recharges with τ = R123·C42 = 1.5 s.
A note 125 ms after the previous one starts from wherever C42 has recovered
to. The 2.5–3 ms attack appears only after silence.

### 4.3 Roland vs [XB] constants (D10)

[XB]'s τ = 2.29 s and 62.65 µA peak cannot come from the Roland values
(R123·C42 = 1.5 s; 12 V gate gives 46 µA). These are x0xb0x constants, not
Roland. **Decision: Roland constants used.**

### 4.4 Accent

- Accent +5.9 dB (VR7 half), +11.4 dB (VR7 full), gone after ~200 ms.
- The accent branch (R119 → C36 → R120 → D27 → IC15 pin 1) is not gated:
  accented notes stay audible after gate-off.

### 4.5 OTA (BA662A)

`out = R_OTA · I_abc · tanh(v_vca / (2 Vt))`, with R_OTA = (47 kΩ + 22 kΩ)
∥ 220 kΩ = 52.5 kΩ. Linear at the 303's level (h3 at −72 dB).

---

## 5. Control network (P4)

### 5.1 Expo converter (Q9/Q10/Q11)

Q9 holds the reference node (N561, 3.263 V). The Cutoff pot VR3 (A taper,
+12 V through R47 10 kΩ to N561) and Env Mod pot VR5 (A taper, R61 10 kΩ →
N561 to N53) drive Q10/Q11 through R63 220 kΩ and R73 100 kΩ + TM3
(trimmer).

**Correction:** R47 and R61 both return to N561, not to each other (from
[XB]; confirmed by modelling — the alternative cannot calibrate).

TM3 calibrates to **153 kΩ** ([XB]: 159 kΩ). The expo law:

```
I = I(R73) × exp(ΔV / Vt) / (1 + exp(ΔV/Vt) / β11)
```

Three fixed-point passes on Vbe11 for convergence.

- Cutoff range: ~3 octaves at Env Mod minimum (217 Hz → 1.74 kHz).
- Env Mod lowers the resting cutoff by up to 1.4 octaves and scales the
  sweep from +0.8 to +5.1 octaves.

### 5.2 MEG (filter envelope)

- C62 struck to the same peak on every note (no stacking).
- Discharge through R138 68 kΩ → VR6 Decay (1 MΩ, A taper, rheostat) →
  diode stack D28/D27/D26 to ground with R117 22 kΩ from 5.333 V biasing
  the stack.
- Accent bypasses VR6 (IC12 4066) while ACCENT is high.
- Decay to 10 %: 173 ms (Decay min) to 2.54 s (max).
- The diode stack floor gives the long tail of the accented decay — a fixed
  floor cannot fit both accented and normal decays together.

### 5.3 Accent sweep

VR7 wiper (50 kΩ B) → D24 → R46 47 kΩ → VR4b (Resonance second gang) →
C13 1 µF → R72 100 kΩ to the expo node.

- At low Resonance: a sharp cutoff spike per accent.
- At high Resonance: a smooth swell peaking ~30 ms after the hit, climbing
  over consecutive accents (+2.3 dB on the second, levelling after three).

**The VR7 wiper is shared** between the filter accent sweep (C13 branch)
and the VCA accent branch (R119 → C36). Separate solves put the first
accent's ladder current 27 % high at Res 0.2. The branches must be solved
together as a 5 × 5 nodal system.

---

## 6. End-to-end SPICE run (P4)

### 6.1 Pattern

8 steps at 120 BPM (125 ms/step): C2, C2 accent, D#2 slide → G2, C3
accent slide → A#2 accent, rest, C2 accent. Run at Resonance 0.85 ("acid")
and 0.2 ("lowres"). Knobs: saw, Cutoff 0.35, Env Mod 0.6, Decay 0.45,
Accent 0.8.

### 6.2 Key findings

1. **Slide is legato in every lane:** no trigger, no MEG strike. The pitch
   glides (τ = 22 ms from the 100 kΩ × 0.22 µF slide network).
2. **An accented slid-to note** gets the VCA accent but no new filter peak.
3. **The VCA attack** appears only after silence; on running 16ths C42 has
   not recharged, so notes open at once.
4. **VEG is a state**, not a restart. A triggered note starts from wherever
   C42 has recovered to.
5. **Accented notes stay audible** after gate-off: the accent branch (R119 →
   C36) is not gated by GATE, so the accent current persists through I_acc
   until ACCENT drops at the step's end.
6. **Slide τ = 22 ms:** 50 % in 15.2 ms, 90 % in 50.7 ms. Linear in volts,
   exponential in log-pitch.

### 6.3 Traces and audio

- Traces saved as `.npz` files for DSP validation (V4–V6).
- Audio: `tools/spice/tb303/e2e_acid.wav`, `e2e_lowres.wav` (44.1 kHz mono,
  normalised per file).
- Plots: `tools/plots/303_e2e_run_acid.png`, `303_e2e_run_lowres.png`.

---

## 7. DSP equations (P6)

### 7.1 Audio path

**Diode ladder** — four capacitor stages, bottom to top:

```
dx0/dt = ω0 · [ −tanh(x_in)     − tanh(x0 − x1) ]
dx1/dt = ω1 · [  tanh(x0 − x1)  − tanh(x1 − x2) ]
dx2/dt = ω2 · [  tanh(x1 − x2)  − tanh(x2 − x3) ]
dx3/dt = ω3 · [  tanh(x2 − x3)  − tanh(x3)      ]  − W_L · v_hp / (2 Vt)

ω_k = I / (4 Vt C_k)
x_in = u / (2 Vt)
```

States are capacitor voltages normalised by 2 Vt (x = v / (2 Vt), Vt =
25.85 mV). The bottom stage (C18 = 0.018 µF) has a faster rate than the
upper three (C19 = C24 = C26 = 0.033 µF), so ω0 ≈ 1.83 × ω1.

Term L (last term of dx3/dt): the current that the output coupling C27/C25
draws from C26. W_L = 1/(2 · R_IN21 · C26) = 650 rad/s. Without it, the
response above cutoff at low currents is 1–1.5 dB high.

**Q21 output stage:**

```
v_hp = 2 Vt · x3 − z_out
dz_out/dt = v_hp / τ_out
tp_pre = −A_OUT · tanh(v_hp / (2 Vt))
```

A_OUT = 103.6 × 2 Vt = 5.356 V. τ_out = 2.33 ms (68 Hz).

**FeedbackNet** — the eight-capacitor linear network around the ladder:

| Element | Nodes | Value | Role |
|---|---|---|---|
| C14 | Q19 emitter → TP6 | 1 µF | Filter output coupling |
| VR4a | TP6 → W → ground | 50 kΩ, wiper at k = Res | Resonance |
| C15, R67 | W → Q18 base | 1 µF, 2.2 kΩ | Into Q18 follower |
| Q18 bias | R99, R66, C22F | 100 kΩ, 100 kΩ, 1 µF | |
| Q18 | Follower | Gain 0.99 | VCVS |
| C23, R97 | Q18 → Q12 right base | 1 µF, 10 kΩ | Feedback path |
| R98 | Q12 right base → NB | 2.2 kΩ | |
| R68 ∥ C16 | NB → ground | 2.2 kΩ ∥ 10 µF | Shared bias |
| C17, R62, R70 | s → Q12 left base | 1 µF, 220 kΩ, 2.2 kΩ | VCO input (÷101) |
| C21, R121 | TP6 → IC15 pin 3 | 0.01 µF, 220 kΩ | VCA input A |
| C22, R122 | W → IC15 pin 3 | 0.01 µF, 100 kΩ | VCA input B |
| R124 | pin 3 → ground | 2.2 kΩ | VCA input node |

The network has two inputs (tp_pre, s), three outputs (u, v_vca, TP6), and
eight capacitor-voltage states. All poles are real, below 160 Hz. Above
~200 Hz the network is its D matrix (instantaneous gain): resonance
feedback 0 (Res 0) to −0.174 (Res full), input divided by 101.

A single feedback high-pass could not reproduce the low-frequency loop
correctly: shared parts (NB/C16, Q18 bias, VR4 wiper loaded by C15 and
C22) do not factor into separate stages.

**OTA:**

```
out = R_OTA · I_abc · tanh(v_vca / (2 Vt))
R_OTA = 52.5 kΩ
```

### 7.2 Control path (all per-block, control rate)

| Block | Equation | Key constants |
|---|---|---|
| Expo converter | `I = I(R73) × exp(ΔV/Vt)`, three fixed-point passes | TM3 = 153 kΩ, β11 = 240, V561 = 3.263 V |
| MEG | C62 state with diode-stack floor (D28/D27/D26) | v_peak = 11.376 V, t_strike = 1.3 ms, 1N4148 Is = 2.52 nA |
| Accent network | 5 × 5 nodal solve of W7, P, W, V13, V36 | D24 drop 0.48 V, D27 mirror drop 1.099 V |
| VEG | C42 state, 64-point current table | att_delay = 0.65 ms, τ_att = 1.2 ms, τ_rec = 1.72 s |
| Prewarp | P = tan(θ)/θ, θ = c(k)·ω1/(2 fs) | c(k) from 0.256 (Res 0) to 1.470 (Res 1) |

### 7.3 What is not modelled

- Temperature and device spread.
- Early effect and base currents in the ladder (agreement in V1 suggests
  they are small).
- Q21's second-order behaviour beyond tanh (argument stays below 0.14).
- The output stage after OTA pin 6 (C38, Volume) and any later coupling.

---

## 8. Solvers

### 8.1 Golden reference (`LadderRef`)

Classical RK4 at 8× oversampling on the continuous equations (4 ladder
states + z_out + 8 network states). The reference for V2 and V6.

### 8.2 TPT per-sample solvers (options A and B)

Trapezoidal rule (zero-delay feedback) at 1× sample rate.

**Linearisation:** every tanh is replaced by its secant gain at the previous
sample, `t(z) = tanh(z)/z` (the "mystran" method).

- **Option B:** all six nonlinearities (four rungs, input pair, Q21).
- **Option A:** four rungs linear (t = 1), input pair and Q21 nonlinear.

The step is a 4 × 4 tridiagonal system in x⁺ with one extra unknown
(x_in⁺). Elimination gives x3⁺ as an affine function of x_in⁺; Q21 and
the network close the loop with one division.

**Prewarp:** the TPT ladder's cutoff is scaled by `P = tan(θ)/θ`. Without
it, high cutoffs run flat: −120 cents at 6.7 kHz, −391 cents at 13 kHz
(Res full). One `fastTan` per block on the ladder current fixes this.

**A vs B:** at the 303's own drive level the rung arguments stay below 0.45,
so A and B agree to ~0.05 dB. At ×8 drive, B follows the reference (max
rung 1.48, LSD 0.22 dB); A stays near 0.95 and loses stage saturation
(LSD 0.78 dB). **B2R was chosen (S093 decision):** B-style with rungs 0-1
nonlinear, rungs 2-3 linear, 3 VDIV per sample.

### 8.3 SplitNet (real-time network)

The eight-state network costs ~100 multiply-adds per sample as a dense
update. The real-time form splits it by speed:

| Part | Rate | What |
|---|---|---|
| D w | per sample | Instantaneous gains (resonance, input ÷101) |
| y_i (i = 1, 2) | per sample | Two fast eigenmodes (72 Hz and 141–157 Hz), one-pole each |
| q_u, q_v | per sample | Slow-group asymptote, one-pole each at 5 Hz |
| r (slow remainder) | per block | 8 states in slow subspace via Φ = e^{A_a T}. Output ramps per sample |

Reduces per-sample cost from ~100 to ~25 operations. The fast modes are
normalised to unit input gain and sorted by pole, keeping all coefficients
smooth in Res for table interpolation.

**Why the asymptote is needed:** without it, the ring decays too slowly
(0.689 per cycle vs 0.641). Near the 500 Hz ring, the slow states still
contribute a small quadrature term. A fixed slow pole carries the 1/s term
exactly.

**Res table:** all coefficients depend on k only. Linear interpolation from
a k-grid. 9 points = 4.1 KB, 17 points = 7.8 KB. Per grid point: ~114
floats (Φ's 8 × 8 block, Γ, output rows, mode coefficients, D).

### 8.4 Per-sample cost

| Part | B_rt | A_rt |
|---|---:|---:|
| Current ramp, w | 3 | 0–3 |
| Right-hand side r | 19 | 15 |
| Tridiagonal solve | 32 + VDIV | 11 (ramped) |
| Forward pass, x_in column | 12 | 5 |
| Loop closure | 19 + VDIV | 19 + VDIV |
| Back-substitution, output coupling | 14 | 14 |
| Secants (Padé, batched) | 49 + VDIV | 19 + VDIV |
| Network per sample | 11 | 11 |
| OTA, ramps, accumulators | 9 | 9 |
| **Total** | **≈ 205, 3 VDIV** | **≈ 120, 2 VDIV** |

Plus the oscillator table loop: 24 instructions. Core: ≈ 230 (B) or
≈ 145 (A) per sample. Ceiling: ≈ 257 (Drum worst case).

---

## 9. Validation summary

All comparisons against the S092 SPICE models (ngspice 47).

| Check | What | Result |
|---|---|---|
| V1 | Small-signal filter vs SPICE AC (24 curves) | Off-peak RMS 0.14–0.60 dB |
| V2 | [SN] p7 calibration ring | Period 1.995 ms, decay/cycle 0.641 |
| V3 | Control path | Expo within 33 cents; MEG within 1.4 %; VEG within 3 % |
| V4 | Audio path on SPICE signals | Output LSD 0.14–0.29 dB |
| V5 | Whole DSP model on the 8-step pattern | Each step within 0.45 dB |
| V6 | Drive ×1–×8 | B_rt LSD 0.07–0.22 dB; A_rt 0.16–0.78 dB |
| Prewarp | TPT cutoff vs continuous model | Without: −391 cents at 13 kHz; with: +40 cents |

Validation tools: `tools/alkaline/validate.py`, `tools/alkaline/alk_model.py`.
Fitted constants: `tools/alkaline/alk_constants.json`.

---

## 10. Failed approaches

1. **Single feedback high-pass:** could not reproduce the low-frequency loop
   (the 7–30 Hz lower resonance, the VCA HP tracking). The shared network
   nodes do not factor.
2. **Balanced truncation of the network:** 7 dense states saved little per
   sample, and the balanced basis was not smooth in Res, preventing table
   interpolation.
3. **Separate accent solves:** the C13 sweep and C36 VCA branch share VR7's
   wiper through R119. Independent solves put the first accent 27 % high.
4. **R110 = 2.2 kΩ (the print value):** loop gain 1.93 vs threshold 19.1.
   Cannot resonate.
5. **[XB]'s VEG constants (τ 2.29 s, peak 62.65 µA):** do not match the
   Roland component values. They are x0xb0x measurements.

---

## 11. User decisions recorded in S092

| # | Decision | Outcome |
|---|---|---|
| D1 | Accent threshold | Velocity 101+ (un-edited steps at 100 are not accented) |
| D2 | Filter option A vs B | Deferred to S093 → decided B2R |
| D3 | Flash for square table | 22,528 B of 40,980 B headroom |
| D4 | SRR on Alkaline | Left out (saves 10/sample) |
| D5 | Reference material | Schematic in hand; raw-VCO captures still wanted |
| D6 | Tuning range | ±700 cents (`IM_SPECIAL_ALK_TUNE`, tag 24/32) |
| D7 | Extras for P6 | E1 (post-VCA overdrive) + E2 (filter drive) + E3 (Devil Fish ranges) + E4 (MIDI note-off) |
| D8 | Square pitch dependence | Per-octave level in the table (0 runtime cost) |
| D9 | VCA release | One-block ramp (0.73 ms) |
| D10 | VEG constants | Roland/P3 constants, not [XB] |
| D11 | Gate length | 3.5 of 6 clocks |

---

## 12. Component values reference

All [SN] p5 values confirmed by the user against the schematic except where
noted. The scan is smudged at R62 (taken as 220 kΩ from [XB]). R110 = 22 kΩ
is the [XB] value, not the print.

Notable confirmations:
- Output stage: Q21 pair, R113/R114, R110, R111, R108, R109, R112, R96, C14.
- Resonance loop: VR4, C15, R67, Q18 bias (R99, R66, C22F), R94, C23, R97,
  R98.
- R62 = 220 kΩ (from [XB]; the scan is smudged).
- R119 source = VR7 wiper (user trace).
- VCA inputs A = TP6, B = VR4 wiper (user trace).
