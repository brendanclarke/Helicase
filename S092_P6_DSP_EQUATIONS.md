# S092 P6 — Alkaline DSP Equations

- **Session:** 092, phase 6 (DSP equations).
  - Plan: `S092_P0_ALKALINE_INSTRUMENT_PLAN.md`.
  - Circuit models: `S092_P1_VCB_MODEL_VCO.md`, `S092_P2_VCB_MODEL_VCF.md`,
    `S092_P3_VCB_MODEL_VCA.md`, `S092_P4_CONTROL_NET.md`.
  - End-to-end SPICE run: `S092_P4_SLIDE_ACCENT_RUN.md`.
- **What this is:** the DSP model of the TB-303 voice, derived from the SPICE
  models. It covers:
  - the equations, with every constant's source;
  - the solvers, from the golden reference to the real-time form;
  - the control-rate state equations and the constants fitted from SPICE;
  - validation against SPICE (V1–V6);
  - the evidence for the filter choice (P0 D2) and a CPU estimate.
- **Status (2026-11-07):**
  - The equations are done and match SPICE (§0).
  - The real-time form is defined and validated: `B_rt` / `A_rt`, §5.3.
  - D2 waits for the user to listen to the renders (§7).
  - The C implementation and its measured instruction count are P0 phase P2.
- **Decisions used:**
  - R110 = 22 kΩ (P2);
  - D8: one square/saw table per octave level, from P1;
  - D9: one-block release ramp, 0.73 ms;
  - D10: Roland VEG constants;
  - D11: gate 3.5/6 of a step;
  - the VCA trigger of 0.70 ms (P3 §3.2).
- **Licensing:** as in P1–P4, x0xb0x ([XB], CC0) is used for part values and
  topology only. No code is copied.

---

## 0. Results at a glance

All comparisons are against the S092 SPICE models (ngspice 47).

| Check | What | Result |
|---|---|---|
| V1 | Small-signal filter response against SPICE AC, Res 0/0.5/0.8/1 × ladder current 1–300 µA (24 curves) | Off-peak RMS error 0.14–0.60 dB (worst point 1.05 dB). Main and low (7–30 Hz) resonance frequencies within one grid step |
| V2 | [SN] p7 calibration ring (Res full, saw at C2) | Period 1.995 ms (SPICE 2.034, −1.9 %); decay per cycle 0.641 (SPICE 0.629); cycle 0.394 V p-p (SPICE 0.389). Same in every solver; the real-time forms give 1.995 ms / 0.645 |
| V3 | Control path | Expo law within 33 cents (RMS 20). MEG decay times within 1.4 %. Accent sweep peaks +2 to +7 %. VEG peak 45.6 µA (SPICE 46.0); −6/−20/−40 dB points within 3 %; attack 2.18 ms |
| V4 | Audio path driven by the SPICE run's own saw and currents | Output level −0.003 to −0.08 dB. Each step within 0.10 dB. Log-spectral distance (LSD) 0.14–0.29 dB |
| V5 | The whole DSP model on the P4 pattern | Pitch, slide, gate, legato and accent behaviour as SPICE. Cutoff peaks +20 to +60 cents; step-end cutoff −28 to +7 cents. VCA peaks +3 to +4 % on accents, −1 to −5 % otherwise (−9 % where SPICE has a sub-ms gate-on spike). Output RMS per step within 0.45 dB |
| V6 | Filter drive ×1 to ×8 (P0 E2), each solver against the golden reference | `B_rt` LSD 0.07–0.22 dB, and it saturates its stages like the reference. `A_rt` LSD 0.16–0.78 dB; its stages stay linear |
| Prewarp | The TPT ladder's cutoff against the continuous model, Res 0–1, up to 13 kHz | Without prewarp up to −391 cents flat; with it within +40 cents (§5.2) |
| CPU | Hand count of the real-time kernels (§5.4) | `B_rt` ≈ 205 instructions/sample + 3 VDIV; `A_rt` ≈ 120 + 2 VDIV. Add the oscillator (24). The ceiling is ≈ 247 + 4 VDIV (257 without SRR) |

**Main findings:**

1. **The network around the ladder matters.** C14, C15, C16, C17, C21,
   C22, the Q18 bias and C23 are all needed to get the 7–30 Hz lower
   resonance, the low-frequency loop gain and the VCA input right. A
   single feedback high-pass is not enough (§3.4).
2. **The network can run at control rate** apart from two fast modes and
   two one-pole asymptotes. The slow part is solved once per block. Per
   sample this costs about 25 operations instead of about 100 (§5.3).
3. **The VR7 wiper is shared.** It drives both the filter accent sweep
   and the VCA accent branch (R119/C36). Solving the two as one network was
   needed to match SPICE: without it the first accent's ladder current was
   27 % high (§4.3).
4. **A and B are the same at the 303's own drive level.** They differ
   only when the filter is driven harder (P0 E2). B costs about 85 more
   instructions/sample (§7).
5. **The TPT ladder needs prewarping.** Without it, high cutoffs run
   flat: −120 cents at 6.7 kHz and −391 cents at 13 kHz (Res full). One tan
   per block fixes this (§5.2).

---

## 1. Files and how to run

| Item | Value |
|---|---|
| Model | `tools/alkaline/alk_model.py`: equations, constants and solvers (Python, numpy) |
| Fit | `tools/alkaline/fit_spice.py`: runs the P1/P3/P4 netlists and fits the MEG, VEG and square-table constants. Writes `tools/alkaline/alk_constants.json`. Needs ngspice |
| Validation | `tools/alkaline/validate.py [v1 … v6]`: writes `tools/alkaline/validate_results.json`. All six take about 1.5 min. V1 runs ngspice. V4–V6 need the end-to-end traces: run `tools/spice/tb303/e2e.py` first (it writes `e2e_{acid,lowres}_traces.npz` to `<tempdir>/lxr_spice_tb303`) |
| Plots | `tools/plots/alk_dsp_pattern_acid.png`, `alk_dsp_pattern_lowres.png` (V5: ladder current, VCA current, output, two zooms; DSP over SPICE) |
| Renders, V4 | `alk_v4_{acid,lowres}_{spice,ref,B_rt}.wav`: the SPICE run's own saw and currents through each model |
| Renders, V5 | `alk_v5_{acid,lowres}_{A_rt,B_rt}.wav`: the whole DSP model playing the P4 pattern |
| Renders, V6 | `alk_d2_{ref,A_rt,B_rt}_drv{1,4}.wav`: the D2 listening set (§7) |

All WAV files are 44.1 kHz mono and are normalised per file to −2 dBFS peak.
Compare levels within a file, not between files.

---

## 2. The voice, end to end

```
 per block (32 samples, Δt = 0.7255 ms)            per sample (fs = 44,108 Hz)
 ─────────────────────────────────────            ───────────────────────────
 step events (§4.6) ─► pitch CV, slide lag ─────► oscillator table (§3.6) ─► s
 knobs + MEG (§4.2) + accent network (§4.3)
        └─► expo law (§4.1) ─► ladder current I ──► ramp ─┐
                                                          ▼
 s ─► [network: C17/R62 → Q12 left base] ─► u ─► diode ladder x0..x3 (§3.2)
                    ▲                                     │ v3
                    │ u (resonance loop)                  ▼
 [network: C14, VR4, C15, Q18, C23, R97/R98] ◄── tp_pre ◄─ Q21 stage (§3.3)
 [network: C21/R121, C22/R122 → IC15 pin 3] ─► v_vca
 VEG (§4.4) + accent branch (§4.3) ─► I_abc ──► ramp ─► OTA (§3.5) ─► out
```

---

## 3. Audio-path equations

### 3.1 Units

- Ladder states are capacitor voltages normalised by 2 Vt:
  `x = v / (2 Vt)`, with Vt = 25.85 mV (27 °C, as in every SPICE run).
- Network voltages are in volts. Currents are in amperes.
- The oscillator signal `s` is the voltage at S1, the VCF input node, as
  in P1.

### 3.2 The diode ladder

Four capacitors, bottom to top:
- C18 = 0.018 µF;
- C19, C24 and C26 = 0.033 µF ([SN] p5, P0 A4).

Each rung is a transistor pair. Each stage carries half of the ladder
current I, steered by the tanh of the voltage across the pair:

```
dx0/dt = ω0 · [ −tanh(x_in)     − tanh(x0 − x1) ]
dx1/dt = ω1 · [  tanh(x0 − x1)  − tanh(x1 − x2) ]
dx2/dt = ω2 · [  tanh(x1 − x2)  − tanh(x2 − x3) ]
dx3/dt = ω3 · [  tanh(x2 − x3)  − tanh(x3)      ]  − W_L · v_hp / (2 Vt)

ω_k = I / (4 Vt C_k)          (C dv/dt = (I/2)·Δtanh, then v = 2 Vt x)
x_in = u / (2 Vt),  u = v(Q12 left base) − v(Q12 right base)
```

- The bottom stage is driven by the Q12 input pair, `−tanh(x_in)`.
- The top of the ladder (Q22's side) is an AC ground. Q22 sits on V+
  through 100 Ω with a capacitor to ground, so x4 = 0 (user's trace).
- C19, C24 and C26 are equal, so ω1 = ω2 = ω3. At the calibration current
  of 7.375 µA, ω0 = 3,962 rad/s and ω1–3 = 2,161 rad/s.
- **Term L** (the last term of dx3/dt) is the current that the output
  coupling draws from C26:
  - W_L = 1/(2 · R_IN21 · C26) = 650 rad/s, with R_IN21 from §3.3.
  - Without it, at low cutoffs the response above the cutoff is 1–1.5 dB
    high against the P2 AC runs (RMS error 1.3–1.4 dB, against 0.16–0.23 dB
    with it). At high cutoffs it makes no difference.

### 3.3 The Q21 output stage

The ladder top feeds Q21's differential pair through the crossed coupling
capacitors C27/C25 (0.1 µF each; user's trace). Q20/Q19 then buffer it to
Q19's emitter, before C14. P2 §2.1 measured this stage with R110 = 22 kΩ.

```
v_hp = 2 Vt · x3 − z_out                 (the coupling high-pass)
dz_out/dt = v_hp / τ_out
tp_pre = −A_OUT · tanh(v_hp / (2 Vt))     (Q19 emitter, volts)
```

| Constant | Value | Source |
|---|---|---|
| A_OUT | 103.6 × 2 Vt = 5.356 V | P2 §2.1: Q21 stage small-signal gain 103.6 at 1 kHz with its loading, R110 22 kΩ |
| R_IN21 | R114 (100 kΩ) ∥ β·Vt/(I21/2) = 23.3 kΩ | β = 300, I21 = 0.51 mA (P2 operating point) |
| τ_out | 0.1 µF × R_IN21 = 2.33 ms (68 Hz) | as above |

TP6 = tp_pre − v(C14). The real-time forms do not carry TP6; they need
only tp_pre and the VCA input.

### 3.4 The network around the ladder (`FeedbackNet`)

This network is linear. It has two inputs, `w = (tp_pre, s)`, and three
outputs:
- `u` (into Q12);
- `v_vca` (IC15 pin 3);
- TP6 (for diagnostics).

Values are from [SN] p5 and the user's traces (P2 §2, P3 §2):

| Element | Nodes | Value | Role |
|---|---|---|---|
| C14 | Q19 emitter E → TP6 | 1 µF | Filter output coupling |
| VR4a | TP6 → W → ground | 50 kΩ, wiper at k = Resonance | Resonance amount |
| C15, R67 | W → B1 → Q18 base (B18) | 1 µF, 2.2 kΩ | Into the Q18 follower |
| R99, R66, C22F | B18 → NB18; NB18 → ground (both) | 100 kΩ, 100 kΩ, 1 µF | Q18 bias. P2 calls this capacitor C22; it is renamed here to keep it apart from the 0.01 µF VCA-input C22 |
| Q18 | Q = 0.99 · v(B18); input resistance β·(R94 ∥ (R97 + R98)) = 300 × 5.5 kΩ | | Follower (VCVS) |
| C23, R97 | Q → F1 → Q12 right base (INR) | 1 µF, 10 kΩ | Feedback into the input pair |
| R98 | INR → NB | 2.2 kΩ | |
| R68 ∥ C16 | NB → ground | 2.2 kΩ ∥ 10 µF | Shared bias node of both Q12 bases |
| C17, R62, R70 | S → G1 → INL; INL → NB | 1 µF, 220 kΩ, 2.2 kΩ | VCO input (÷101) |
| C21, R121 | TP6 → A1 → P3 | 0.01 µF, 220 kΩ | VCA input A |
| C22, R122 | W → A2 → P3 | 0.01 µF, 100 kΩ | VCA input B |
| R124 | P3 → ground | 2.2 kΩ | VCA input node |

- The supplies and the 5.333 V reference are AC grounds. The VCA bias node
  is bypassed by C37 (10 µF), so it is too.
- The eight capacitor voltages are the states.
- `FeedbackNet(k)` builds the continuous state-space model by modified
  nodal analysis, one unit input at a time:

  ```
  dz/dt = A(k) z + B(k) w,   y = C(k) z + D(k) w,   y = (u, v_vca, TP6)
  ```

**What it does:**

| Res k | Poles (Hz) | D: u ← tp_pre | D: u ← s | D: v_vca ← tp_pre |
|---:|---|---:|---:|---:|
| 0 | 0.67, 0.71, 3.15, 4.13, 6.15, 15.5, 72.4, 155.8 | 0 | 0.0099 | 0.0097 |
| 0.5 | 0.62, 0.71, 2.70, 4.63, 6.15, 15.5, 72.4, 140.9 | −0.070 | 0.0099 | 0.018 |
| 1 | 0.56, 0.71, 2.32, 6.15, 6.65, 15.5, 72.4, 157.4 | −0.174 | 0.0099 | 0.031 |

- All poles are real.
- **Above about 200 Hz the network is nearly its D matrix.**
  - The resonance feedback is an instantaneous gain, from 0 at Res 0 to
    −0.174 at Res full.
  - The input is divided by 101.
  - The poles shape only the low-frequency loop (the 7–30 Hz lower
    resonance, Stinchcombe's "very real" bump), the VCO coupling and the
    VCA input high-passes.
- **Why the whole network:**
  - Earlier versions of this model used two or three separate high-passes
    with an effective pot gain.
  - Those made the low-frequency loop too strong and misplaced the lower
    resonance against the P2 AC runs.
  - Shared parts (NB and C16 under both Q12 bases; the Q18 bias; the
    VR4 wiper loaded by both C15 and C22) do not factor into separate
    stages.

### 3.5 The OTA (IC15, BA662A)

```
out = R_OTA · I_abc · tanh(v_vca / (2 Vt))
R_OTA = (47 kΩ + 22 kΩ) ∥ 220 kΩ = 52.5 kΩ
```

- I_abc is the VEG current plus the accent current (§4.4, §4.3).
- `out` is the voltage at pin 6, as in the P3/P4 output trace.

### 3.6 The oscillator source

D8: one cycle per octave level, taken from the P1 SPICE netlist (Q28 saw
source and Q8 square collector). `fit_spice.py` extracts it at each level's
centre frequency, 16.35 Hz × 2^level × √2.

- Each level is band-limited to the harmonics below Nyquist at the top
  of its octave.
- It is played back with linear interpolation.
- Levels 7–10 reuse level 6.

| Level | Centre (Hz) | Square low–high (V) | Square on-time | Saw (V) |
|---:|---:|---|---:|---|
| 0 | 23.1 | 0.07–3.61 | 57.7 % | 11.16 → 5.81 (falling) |
| 1 | 46.2 | 0.08–4.75 | 44.2 % | same |
| 2 | 92.5 | 0.07–6.72 | 31.8 % | same |
| 3 | 185 | 0.07–7.64 | 26.7 % | same |
| 4 | 370 | 0.07–7.70 | 24.4 % | same |
| 5 | 740 | 0.06–7.85 | 22.2 % | same |
| 6 | 1,480 | 0.06–8.00 | 20.5 % | same |

The square's level and duty change with pitch, as in P1 §3.

- **DC:** the tables keep their DC. The network's C17 removes it, as in
  the circuit.
- **Start:** `settle()` starts the network at the DC operating point for
  the table's mean, as SPICE starts from `.op`.

### 3.7 What is not modelled

- Temperature and device spread. Every Vt and β is the SPICE model's.
- Early effect, base currents in the ladder, and the ladder transistors'
  capacitances. The P2 netlist has the base currents and β; the
  small-signal agreement in V1 suggests they are small.
- Q21's second-order behaviour beyond its tanh. Its argument stays below
  0.14 (V4), so it is linear in practice.
- The output stage after pin 6 (C38, Volume) and any later coupling.

---

## 4. Control-rate equations

All run once per block, Δt = 32/fs = 0.7255 ms. The ladder current and
I_abc ramp linearly per sample from the previous block's value to this
block's.

### 4.1 Expo converter (Q9/Q10/Q11), `expo_current()`

The cutoff pot VR3 and the Env Mod pot VR5 each become a Thevenin source.
Both pot networks return to N561 (Q9's node, 3.263 V at rest; P4 §2.1).

```
r3 = 50k · taper(cut)      V_th3 = V561 + (12 − V561)(R47 + r3)/(R47 + 50k)
                           R_th3 = (50k − r3)(R47 + r3)/(R47 + 50k)
r5 = 50k · taper(env)      V_th5 = V561 + (N53 − V561)(R61 + r5)/(R61 + 50k)
                           R_th5 = (R61 + r5)(50k − r5)/(R61 + 50k)
taper(r) = (16^r − 1)/15                         (audio taper, 20 % at half)

ΔV = [(V_th5 − V561)/(R63 + R_th5) + (V_W4 − V561)/R72]
     / [1/R71 + 1/(R63 + R_th5) + 1/R72]          (Q10 base, re V561)
e  = exp(ΔV / Vt)
Vbe11 = 0.5279 + Vt · ln(I / 7.375 µA)
I73 = (V_th3 − V561 − Vbe11) / (R_th3 + TM3 + R73)
I  = I73 · e / (1 + e/β11)        (three fixed-point passes on Vbe11)
```

- V_W4 is VR4b's wiper voltage from the accent network (§4.3).
- `I / (1 + e/β)` accounts for Q11's base current, which R73 supplies.
  Without it the accent-run peaks at Res 0 are 16–19 % high against SPICE
  (7 % with it).

| Constant | Value | Source |
|---|---|---|
| R47, R61 | 10 kΩ, returning to N561 | [SN] p5; return point from [XB] (P4 §2.1) |
| R63, R71, R72, R73 | 220 kΩ, 2.2 kΩ, 100 kΩ, 100 kΩ | [SN] p5 |
| TM3 | 152.74 kΩ | P4 §3.1: calibrated so that Res full, cutoff/env centred and MEG at rest give the [SN] p7 ring |
| V561, Vbe11 at 7.375 µA | 3.263 V, 0.5279 V | P4 rest point |
| β11 | 240 | 2SC536 in the P4 netlist |

**Accuracy (V3a):** within 33 cents of the P4 DC map (RMS 20 cents) over
cutoff × env, MEG at rest. The error is mostly a function of Env Mod:
about −25 cents at Env 0, 0 at Env 1.

### 4.2 MEG (filter envelope), `Meg`

State: v62, the voltage on C62. N53 is the Darlington output that feeds
VR5 and VR7.

```
on a (non-legato) trigger: v62 = v_peak for t_strike (the CPU's strike pulse)
otherwise:   R = 68k + (R_4066 if ACCENT else 1 MΩ · taper(decay))   (R138 + VR6)
             v_stack = 2·n·Vt·ln((I_R117 + i)/Is + 1) + n·Vt·ln(i/Is + 1),
             i = (v62 − v_stack)/R          (4 fixed-point passes)
             v62 ← v_stack + (v62 − v_stack) · exp(−Δt / (R · 1 µF))
N53 = v53_min + soft · softplus((v62 − drop)/soft)
```

- The discharge runs into the D28 / D27 + D26 stack, which R117 biases.
  Its voltage follows its current.
  - This stack floor gives the long tail of the accented (4066-bypassed)
    decay.
  - A fixed floor (`v_inf` in `alk_constants.json`, kept for the record)
    could not fit the accented and normal decays together.
- The diodes use the 1N4148 model: Is = 2.52 nA, n = 1.752. I_R117 =
  (5.333 − 1.02)/22 kΩ = 0.196 mA.

| Fitted (`alk_constants.json` → `meg`) | Value | Fit |
|---|---|---|
| v_peak | 11.376 V | P4 MEG runs, maximum |
| v_rest | 0.706 V | before the strike |
| t_strike | 1.3 ms | the CPU strike (P4 E3) |
| drop, soft, v53_min | 1.21 V, 0.12 V, 0.021 V | Darlington transfer, least squares over three decays, RMS 14 mV |

### 4.3 The accent network, `AccentNet`

VR7's wiper (W7) drives two branches:
- **sweep:** D24 → R46 47 kΩ → VR4b (pin 1 P, wiper W, pin 3 on C13 1 µF)
  → R72 into the expo node;
- **VCA accent:** R119 47 kΩ → C36 0.033 µF → R120 22 kΩ → D27 → IC15
  pin 1.

VR7's top is N53 through the 4066 while ACCENT is high. Otherwise it is
open, and the wiper falls to ground through VR7's lower part.

The network is solved once per block by backward Euler on C13 and C36.
There are five unknowns (W7, P, W, V13, V36). D24 and D27 are ideal
switches with fixed drops (D24 0.48 V; D27 plus the pin-1 mirror 1.099 V).
The four diode states are tried in turn, and the first consistent one is
kept.

```
G · [W7, P, W, V13, V36]ᵀ = rhs        (5 × 5, see alk_model.AccentNet)
VR4b wiper:      V_W4 = W         → expo law (§4.1)
accent current:  I_acc = max(0, (V36 − 1.099)/R120)   → added to I_abc, not gated
```

**Finding:** the two branches must share one solve.

- The P4 control netlist has no R119.
  - Against it, separate solves matched (V3c).
  - Against the end-to-end run, which has R119, the first accented
    note's ladder current came out 27 % high at Res 0.2, and the accent
    VCA current 6 % high.
- While C36 charges, R119 loads the wiper. That lowers the sweep's start.
- With the joint solve, the accent cutoff peaks are within 3.5 % and the
  accent VCA peaks within 4.2 % (V5).
- `AccentNet(vca_branch=False)` drops R119 to reproduce the P4 control
  netlist (V3c).

The resting C13 voltage is 3.2487 V (the P4 operating point).

### 4.4 VEG (volume envelope), `Veg`

State: v42, the voltage on C42. R123 recharges it from +12 V; the trigger
empties it (P3 §3.2–3.3, D10).

```
on a (non-legato) trigger: wait att_delay, then v42 ← v42 · exp(−Δt/τ_att) for 8 τ_att
always:                    v42 ← v_rec + (v42 − v_rec) · exp(−Δt/τ_rec)
I_veg = table(v42) while the gate is high, else 0
```

| Fitted (`veg`) | Value | Fit |
|---|---|---|
| att_delay, τ_att | 0.65 ms, 1.2 ms | P3 attack with the 0.70 ms trigger (10–90 % = 2.18 ms) |
| v_rec, τ_rec | 12.2 V, 1.72 s | P3 long decay. R123·C42 is 1.5 s; the fit absorbs Q31's emitter loading |
| table | 64 points, v42 0.08–12 V → 45.6–0 µA | P3 long-decay run |

This is a state, not an envelope that restarts. A note 125 ms after the
previous one starts from wherever C42 has recovered to. So the
2.5–3 ms attack appears only after silence (P4 run §4 item 5).

### 4.5 VCA current

```
I_abc = I_veg (gated) + I_acc (not gated)
```

- I_abc ramps per sample across each block.
- Gate-off therefore takes one block, 0.73 ms. This is the D9 release.
- An accented note keeps sounding through I_acc after its gate falls,
  until ACCENT drops at the step's end (P4 run §4 item 4).

### 4.6 Pitch, slide and sequencing

| Rule | Implementation |
|---|---|
| Event timing | A step event lands in the first block that starts at or after the step boundary. Latency is under one block (0.73 ms). Firing in the block that *contains* the boundary started each ramp a few samples early; V5 showed it |
| Gate | 3.5/6 of a step (D11). A slide-flagged note holds the gate through the next step's gate (6 + 3.5 clocks) |
| Legato | A note after a slide-flagged note is legato: no MEG strike and no VEG trigger. The CV glides. Slide is taken *from* the flagged note |
| Glide | `cv ← target + (cv − target) · e^(−Δt/τ)`, τ = 100 kΩ × 0.22 µF = 22 ms, so α = 0.0324 per block. Linear in volts, so exponential in log-pitch (P0 A9). The oscillator frequency is set once per block |
| Non-legato note | CV jumps; MEG strike; VEG trigger |
| Accent | ACCENT is high for the whole accented step (P4 E3). It switches the MEG's discharge to the 4066 and connects VR7 to N53 |
| Rest | No events; ACCENT low; the gate stays low |

---

## 5. Solvers

### 5.1 Golden reference, `LadderRef`

- The continuous equations of §3: 4 ladder states, z_out and the 8
  network states.
- Classical RK4 at 8× oversampling. The input is interpolated linearly
  within each sample.
- It is the reference for V2 and V6, and the check that the TPT solvers
  are not hiding a discretisation error.

### 5.2 TPT per sample, `LadderTPT` (options A and B)

The ladder uses the trapezoidal rule (TPT, zero-delay feedback) at 1×.

- **Linearisation:** every tanh is replaced by its secant gain at the
  previous sample, `t(z) = tanh(z)/z` (the "mystran" method).
  - Option **B** applies this to all six nonlinearities: four rungs,
    the input pair and Q21.
  - Option **A** makes the four rungs linear (t = 1) and keeps the input
    pair and Q21 nonlinear.
- **The step:** with h/2 · ω_k = w_k, the step is a 4 × 4 tridiagonal
  system in x⁺ with one extra unknown, x_in⁺:

  ```
  M(w, t) · x⁺ = r(x, previous sample) + q · x_in⁺
  ```

- Elimination gives x3⁺ as an affine function of x_in⁺:
  `x3⁺ = pr3 + pq3 · x_in⁺`.
- **Closing the loop:**
  - Q21 gives `tp_pre⁺ = at · x3⁺ + bt`, with `at = −A_OUT · t_out / (1 + g_out)`
    and `bt = A_OUT · t_out · z_out / (2 Vt (1 + g_out))`.
  - The network gives `u⁺ = base + K_u0 · tp_pre⁺`, where K_u0 is the
    network's instantaneous gain from tp_pre.
  - One division closes the loop:

  ```
  x_in⁺ = (base + K_u0 · (at · pr3 + bt)) / (2 Vt − K_u0 · at · pq3)
  ```

- Back-substitution then gives x0⁺–x3⁺. After that come the output
  coupling, tp_pre⁺, the network update and the new secant gains.
- With the network per sample (options A and B), the network is the
  bilinear discretisation of §3.4: `z⁺ = Ad z + Bd (w + w⁺)`.

**Prewarp:**
- The trapezoidal rule maps analog frequency f_a to digital f by
  f_a = (fs/π)·tan(πf/fs), so a TPT ladder runs flat at high cutoffs.
- The ladder rate is therefore scaled by `P = tan θ / θ`, with
  `θ = c(k) · ω1 / (2 fs)` (clamped at 1.3). The scaling is applied to the
  ladder current and does not touch the network, whose poles are below
  160 Hz.
- c(k) is the response's −3 dB point (just above the peak when there is
  one) divided by ω1/2π, measured on the linearised model:

  | k | 0 | 0.1 | 0.2 | 0.3 | 0.4 | 0.5 | 0.6 | 0.7 | 0.8 | 0.9 | 1 |
  |---|---|---|---|---|---|---|---|---|---|---|---|
  | c | 0.256 | 0.597 | 0.911 | 1.055 | 1.153 | 1.228 | 1.287 | 1.342 | 1.387 | 1.430 | 1.470 |

- In the linear case the TPT response is exactly
  `H_c(f_a; P·I)`, so the effect can be computed in closed form
  (−3 dB point against the continuous model, cents; in-band RMS error):

  | Res | Current | −3 dB point | No prewarp | Prewarp | In-band error, no prewarp → prewarp |
  |---:|---:|---:|---:|---:|---|
  | 0.5 | 100 µA | 5,555 Hz | −80 c | +8 c | 1.17 → 0.73 dB |
  | 0.5 | 200 µA | 11,088 Hz | −287 c | +24 c | 2.71 → 1.16 dB |
  | 0.85 | 100 µA | 6,349 Hz | −104 c | +8 c | 1.95 → 1.13 dB |
  | 0.85 | 200 µA | 12,615 Hz | −351 c | +40 c | 4.03 → 1.44 dB |
  | 1 | 100 µA | 6,679 Hz | −120 c | +8 c | 2.59 → 0.63 dB |
  | 1 | 200 µA | 13,394 Hz | −391 c | +24 c | 5.26 → 1.74 dB |
  | any | ≤ 30 µA | ≤ 2 kHz | ≤ −16 c | 0 c | ≤ 0.3 dB, both |

  (The grid step is 8 cents. "In-band" means up to twice the −3 dB point
  or 16 kHz, within 40 dB of the maximum.)
- The remaining error at high cutoffs is the response's shape near
  Nyquist, which a 1× solver cannot follow (P0 E7).
- Firmware: one `fastTan` per block on the block's end current (P0 A3
  already budgets it). The solvers in `alk_model.py` apply it per sample
  (`prewarp()`); the difference is within the current ramp.

**A against B:**
- At the 303's own drive level the rung arguments stay below 0.45 (V2,
  V4), so A and B agree to about 0.05 dB.
- At ×8 drive the reference's rungs reach 1.47. B follows (1.48); A stays
  near 0.95 and loses the stage saturation (V6).

### 5.3 The real-time network, `SplitNet` (`block_net=True`)

**Problem:** the eight-state network costs about 100 multiply-adds per
sample as a dense update (64 for `Ad z`, plus the inputs and three outputs).
That alone would use the margin under the Drum ceiling.

**Tried and rejected: balanced truncation.** Reducing the network on its
outputs (u, v_vca) gave these closed-loop errors, worst case over Res
0–1 × I 1–300 µA:

| Order | Ladder output, 20 Hz+ | Ladder output, all | VCA input, 20 Hz+ |
|---|---:|---:|---:|
| loop 4 + VCA 2 | 0.04 dB | 0.17 dB | 0.82 dB |
| loop 4 + VCA 3 | 0.04 dB | 0.17 dB | 0.04 dB |
| loop 3 + VCA 2 | 0.42 dB | 2.9 dB | 1.1 dB |

- It works for accuracy, but seven dense states save little per sample.
- The balanced basis is not smooth in k, so a Res table cannot be
  interpolated.

**Used: a split by speed, with the slow part at control rate.**

```
u     = D_u · w + Σ c_u,i · y_i + q_u + r_u(block)
v_vca = D_v · w + Σ c_v,i · y_i + q_v + r_v(block)
```

| Part | Rate | What |
|---|---|---|
| D w | per sample | Instantaneous gains (the resonance amount, the input ÷101) |
| y_i, i = 1, 2 | per sample | The two fast eigenmodes (poles above 40 Hz: C21/C22, 72 Hz and 141–157 Hz) as one-poles `dy/dt = −p_i y + tp_pre`. Each mode is scaled to unit input gain and sorted by pole, so p, c_u and c_v are smooth in k. The oscillator does not reach them |
| q_u, q_v | per sample | The slow group's high-frequency asymptote, one-pole each at a = 2π · 5 Hz with gains C_o · B_slow. It keeps the slow group's 1/s tail at audio rate; that tail sets the resonance damping |
| r (slow remainder) | per block | The eight network states restricted to the slow subspace, minus the two asymptote poles (10 states). Zero-order hold on the block means of w; `Φ = e^{A_a T}` and `Γ = A_a⁻¹(Φ − I)B_a`. The output ramps per sample from the previous block's value to this one's (one block of latency) |

**Why the asymptote is needed:**
- A first version ran all eight network states at block rate and kept
  only D per sample. It rang too long: decay per cycle 0.689 against
  0.641.
- Near the 500 Hz ring, the slow states still contribute a small
  quadrature term, about C·B/ω, which a block-rate solve cannot carry.
- Fitting a single pole to the asymptote (a = −C A B / C B) fails:
  the sign of C A B changes across Res, so the pole would be unstable at
  mid Res.
- A fixed slow pole carries the 1/s term exactly. The 1/s² mismatch is
  ≤ 3 % of an already small term (slow poles ≤ 15.5 Hz against a ring at
  hundreds of hertz).

**Result:**
- Ring 1.995 ms / 0.645, against 1.995 / 0.641 for the per-sample network
  (V2).
- On the SPICE traces, within 0.03 dB of the per-sample network on every
  step (V4).
- Same drive behaviour as B (V6).

**Res table:**
- Every coefficient depends on k only. The block system stays in the
  physical capacitor basis and the fast modes are normalised, so all
  coefficients vary smoothly with k.
- Linear interpolation from a k-grid, on the acid run (k = 0.85,
  off-grid):

  | Grid points | LSD vs SPICE |
  |---:|---:|
  | exact | 0.138 dB |
  | 33 | 0.139 dB |
  | 17 | 0.144 dB |
  | 9 | 0.154 dB |
  | 5 | 0.297 dB |

- **Per grid point** about 114 floats:
  - Φ's 8 × 8 block (the asymptote rows are constant);
  - Γ, 10 × 2;
  - the output rows;
  - p, c_u, c_v, the asymptote gains and D.
- **Flash:** 17 points = 7.8 KB; 9 points = 4.1 KB (headroom 40,980 B).

### 5.4 Per-sample kernel and count

`B_rt`, one sample. The coefficients are fixed for the block, and i, I_abc
and the slow outputs r_u, r_v ramp:

```
i += di;  w0 = c0·i;  w1 = c1·i      // i prewarped at the ramp ends (§5.2); C19 = C24 = C26
d01 = x0−x1; d12 = x1−x2; d23 = x2−x3
a0 = t0·d01; a1 = t1·d12; a2 = t2·d23; a3 = t3·x3
r0 = x0 − w0·(a0 + tin·xin_p);  r1 = x1 + w1·(a0 − a1)
r2 = x2 + w1·(a1 − a2);         r3 = x3 + w1·(a2 − a3) + kL·(kz·zo − vhp_p)
e0 = w0·t0; e1 = w1·t0; e2 = w1·t1; e3 = w1·t2; e4 = w1·t3
θ1 = 1+e0;  θ2 = (1+e1+e2)·θ1 − e1·e0;  θ3 = (1+e2+e3)·θ2 − e2²·θ1
θ4 = (1+e3+e4+kd)·θ3 − e3²·θ2
one reciprocal of θ1θ2θ3θ4 → 1/θk → 1/pk = θk/θk+1       // VDIV 1
y0 = r0; y1 = r1 + e1·y0/p0; y2 = r2 + e2·y1/p1; y3 = r3 + e3·y2/p2
q_k = −w0·tin·Π(m)                                       // the x_in column
base = r_u + Σ c_u,i·(α_i y_i + β_i tp_l) + (α_a q_u + β_a(g_u0 tp_l + g_u1 s_l)) + K_u1·s
at = kA·tout;  bt = kB·tout·zo
xin = (base + K_u0·(at·y3/p3 + bt)) / (2Vt − K_u0·at·q3/p3)   // VDIV 2
x3 = (y3 + q3·xin)/p3;  x2 = (y2 + q2·xin + e3·x3)/p2;  x1 = …;  x0 = …
vhp = (2Vt·x3 − zo)·kg;  zo += 2g·vhp
7 Padé secants (27 + z²)/(27 + 9z²), one shared reciprocal:   // VDIV 3
   z = d01', d12', d23', x3', xin, vhp/2Vt, and the OTA argument (one sample late)
tp = −A_OUT·(vhp/2Vt)·tout
y_i += …;  q_u += …;  q_v += …;  v_vca = r_v + Σ c_v,i·y_i + q_v + D_v0·tp
out = kR · I_abc · v_vca_prev · t_ota;   accumulate tp and s for the block mean
```

| Part | B_rt | A_rt | Notes |
|---|---:|---:|---|
| Current ramp, w | 3 | 0–3 | |
| Right-hand side r | 19 | 15 | A: t = 1, no rung products |
| Tridiagonal: e, d, continuants, 1/p | 32 + VDIV | 11 (ramped) | A's matrix depends only on I. Its 1/p, m and q can be computed at the block ends and ramped |
| Forward pass, x_in column | 12 | 5 | |
| Loop closure | 19 + VDIV | 19 + VDIV | Includes the network's per-sample base |
| Back-substitution, output coupling | 14 | 14 | |
| Secants (Padé, batched) | 49 + VDIV | 19 + VDIV | B: 7 values; A: x_in, Q21, OTA |
| Network per sample (fast modes, asymptotes, v_vca) | 11 | 11 | |
| OTA, ramps, accumulators | 9 | 9 | |
| **FP operations** | **≈ 180** | **≈ 100** | |
| Loads/stores (about 45 live values, 32 FP registers) | ≈ 25 | ≈ 20 | |
| **Total** | **≈ 205, 3 VDIV** | **≈ 120, 2 VDIV** | Plus the oscillator table loop, 24 |

This is a hand count of the kernel above. P0 §4 requires a measured
count (`fpseq.py` on standalone ARM objects) before D2 is final.

Ways to save on B:
- An even polynomial for tanh(z)/z, clamped at |z| ≤ 4, removes VDIV 3 for
  about the same operation count.
- Holding the rung secants for 2 samples has not been tested.

### 5.5 Control-rate work and memory

**Per block (estimates):**

| Work | Operations per block |
|---|---:|
| Res-table interpolation | ≈ 230 |
| Slow network update (Φ z, Γ w̄, outputs) | ≈ 110 |
| Accent network (5 × 5) | ≈ 60–300 (one solve usually; up to four diode states) |
| MEG with its diode-stack logs | ≈ 100 |
| Expo law (one log, one exp, three passes) | ≈ 50 |
| Prewarp (one tan) | ≈ 15 |
| VEG table | ≈ 10 |

That is about 600–800 per block, or 20–25 per sample if spread. P0 §1.1
counts control-rate work outside the voice render; the device gate (cpu
widget) covers it.

To keep it constant:
- run every part every block;
- start the MEG and accent solves from the previous block's diode state;
- use the project's fast log2/exp2.

**RAM per voice:**
- states: about 45 floats (x, z_out, secants, fast modes, asymptotes,
  ten slow states, MEG, C13, C36, C42, CV and flags);
- the coefficient set for the current Res (about 114 floats) and the
  ladder constants (about 20);
- about 180 floats, 0.7 KB, in total.

That fits the existing 1,264 B runtime slot union. No new allocation;
P2 confirms.

---

## 6. Validation

### 6.1 V1: small signal against SPICE AC (`v1`)

- Linearised DSP equations (every tanh → 1, term L and the full
  FeedbackNet) against fresh ngspice AC runs of the P2 netlist.
- Output TP6 per unit S1.
- Error measured within 60 dB of each curve's maximum.

| Res | Off-peak RMS (dB) | Main peak Hz, DSP / SPICE | Lower resonance Hz, DSP / SPICE |
|---:|---|---|---|
| 0 | 0.15–0.36 | 154/154 … 372/358 | 13–29 / 13–29 |
| 0.5 | 0.14–0.60 | 434/469 … 13,340/13,863 | 8.2–11.7 / 8.2–11.7 |
| 0.8 | 0.15–0.46 | 568/591 … 17,464/17,464 | 7.3–9.6 / 7.6–9.6 |
| 1 | 0.16–0.23 (worst point 1.05) | 663/663 … 19,601/19,601 | 7.1–8.6 / 7.1–8.9 |

At Res 1 and high current the peak heights differ by up to 1.9 dB. The
peaks there are narrower than the AC grid, so the heights are limited by
where the grid falls.

### 6.2 V2: the [SN] p7 calibration ring (`v2`)

Res full, saw at C2, 7.375 µA, measured on tp_pre:

| | Period (ms) | Decay per cycle | Cycle p-p (V) | Max rung arg | Max Q21 arg |
|---|---:|---:|---:|---:|---:|
| SPICE (P2) | 2.034 | 0.629 | 0.389 | | |
| ref | 1.995 | 0.641 | 0.395 | 0.45 | 0.05 |
| A, B | 1.995 | 0.641 | 0.393–0.394 | 0.43–0.45 | 0.05 |
| A_rt, B_rt | 1.995 | 0.645 | 0.392–0.393 | 0.43–0.45 | 0.05 |

### 6.3 V3: control path (`v3`)

| Part | DSP | SPICE |
|---|---|---|
| Expo law, cutoff × env grid (P4 DC map) | max 33 cents, RMS 20 | — |
| MEG time to 10 %, Decay 0 / 0.5 / 1 / 1 + accent | 174 / 663 / 2,569 / 826 ms | 173 / 660 / 2,536 / 815 ms |
| Accent run, four accents then a normal note, peak µA, Res 0 | 165, 200, 213, 217, 34 | 154, 188, 200, 204, 33 |
| … Res 0.5 | 63, 84, 90, 92, 33 | 60, 80, 86, 88, 32 |
| … Res 1 | 31.0, 40.3, 42.2, 42.6, 32.6 | 30.3, 39.5, 41.4, 41.7, 32.0 |
| VEG peak; −6 / −20 / −40 dB | 45.6 µA; 0.98 / 2.73 / 3.91 s | 46.0 µA; 1.01 / 2.68 / 3.85 s |
| VEG attack, 10–90 % | 2.18 ms | 2.11–2.61 ms |

### 6.4 V4: audio path on the SPICE run's own signals (`v4`)

- Inputs: the SPICE run's saw at S1, ladder current and VCA current.
- Each solver starts at the DC operating point.

| Run | Solver | Output level | Output LSD | Worst step |
|---|---|---:|---:|---:|
| acid (Res 0.85) | ref | −0.01 dB | 0.15 dB | 0.03 dB |
| | A / B | −0.06 / −0.03 | 0.28 / 0.14 | 0.08 / 0.05 |
| | A_rt / B_rt | −0.08 / −0.05 | 0.29 / 0.16 | 0.10 / 0.07 |
| lowres (Res 0.2) | ref | −0.00 | 0.16 | 0.04 |
| | A / B | −0.02 / −0.00 | 0.17 / 0.16 | 0.03 / 0.04 |
| | A_rt / B_rt | −0.03 / −0.02 | 0.17 / 0.16 | 0.05 / 0.06 |

The TPT solvers are prewarped (§5.2). At these cutoffs (up to 2.7 kHz)
prewarping changes the LSD by at most 0.03 dB.

**TP6 (diagnostic):** LSD 0.35–0.44 dB for ref and 0.95–1.21 dB for A/B.
The A/B difference is all above 4 kHz, where TP6 is 90–127 dB below its
peak: the bilinear warping of a low-cutoff ladder near Nyquist. Below
4 kHz the per-band differences are ≤ 0.25 dB for every solver.

### 6.5 V5: the whole DSP model on the P4 pattern (`v5`)

Pattern: C2, C2 accent, D#2 slide → G2, C3 accent slide → A#2 accent,
rest, C2 accent. 120 BPM, knobs as in the P4 run. The SPICE values come
from `e2e_results.json`.

`B_rt`, acid (Res 0.85):

| Step | Note | Cutoff peak Hz, DSP / SPICE | End Hz | VCA peak µA | Output RMS re step 1 (dB) | Pitch end Hz |
|---:|---|---|---|---|---|---|
| 1 | C2 | 1,111 / 1,079 (+50 c) | 648 / 646 | 45.6 / 46.1 | 0 / 0 | 65.41 / 65.41 |
| 2 | C2 A | 1,402 / 1,363 (+48 c) | 438 / 445 | 127.9 / 124.1 | +6.81 / +6.76 | 65.41 / 65.41 |
| 3 | D#2 S | 1,317 / 1,300 | 685 / 686 | 45.6 / 50.3 | +2.81 / +2.76 | 77.78 / 77.78 |
| 4 | G2 (legato) | 684 / 683 | 481 / 483 | 43.4 / 43.8 | +0.02 / −0.12 | 97.92 / 98.00 |
| 5 | C3 A S | 1,423 / 1,385 | 440 / 446 | 128.3 / 123.6 | +9.53 / +9.53 | 130.81 / 130.81 |
| 6 | A#2 A (legato) | 439 / 444 | 325 / 331 | 49.2 / 51.6 | +0.35 / +0.75 | 116.59 / 116.54 |
| 7 | rest | 325 / 332 | 308 / 312 | 0 / 0.006 | silent / −48 | — |
| 8 | C2 A | 1,409 / 1,374 | 438 / 444 | 128.6 / 123.4 | +6.88 / +6.95 | 65.41 / 65.41 |

- **lowres (Res 0.2):** the same pattern. Accent spikes 2,705–2,754 Hz
  against 2,630–2,679 Hz. Output per step within 0.45 dB.
- **A_rt:** within 0.03 dB of B_rt on every step, in both runs.

**Behaviour checks:**
- Legato steps 4 and 6 get no strike and no VEG trigger.
- The glide reaches the target within 1.4 cents by the step's end.
- The accented legato step 6 gets the VCA accent but no cutoff peak.
- The accent tail continues after gate-off.

**Residues:**
1. **Cutoff:** peaks +20 to +60 cents and ends −28 to +7 cents
   (1–3.5 %). This is the same pattern as the V3a expo-law error. It is
   below a real 303's TM3 trim spread.
2. **Accent VCA peak:** +3 to +4 % (128 against 123–124 µA).
3. **Steps 3 and 6** (the notes after an accent):
   - SPICE shows a sub-millisecond 47–50 µA spike at gate-on, before
     C42's state takes over.
   - The DSP's one-block event timing does not reproduce it.
   - From 3 ms on, the traces agree within 1 µA.
4. **Step 6's accent current:** 5 % low (49.2 against 51.6 µA), so its
   output is 0.4 dB low.
5. **Rest:** exactly silent in the DSP. SPICE leaks −48 dB through the OTA.

Plots: `tools/plots/alk_dsp_pattern_{acid,lowres}.png`.

### 6.6 V6: solver choice under drive (`v6`)

- Inputs: the acid run's saw × drv, with the SPICE currents.
- Output (after the OTA) compared with the golden reference over the
  first second.

| drv | ref max rung | A | B | A_rt | B_rt |
|---:|---:|---|---|---|---|
| 1 | 0.45 | LSD 0.15, rung 0.43 | 0.06, 0.45 | 0.16, 0.43 | 0.07, 0.45 |
| 2 | 0.68 | 0.31, 0.60 | 0.09, 0.66 | 0.33, 0.60 | 0.10, 0.66 |
| 4 | 0.92 | 0.39, 0.75 | 0.15, 0.92 | 0.40, 0.75 | 0.16, 0.94 |
| 8 | 1.47 | 0.78, 0.95 | 0.22, 1.48 | 0.78, 0.95 | 0.22, 1.48 |

Levels are within 0.33 dB in every case.

---

## 7. The filter choice (P0 D2)

| | A_rt | B_rt |
|---|---|---|
| Fidelity at the 303's own drive | Within 0.10 dB per step of SPICE; LSD 0.17–0.29 dB | Within 0.07 dB; LSD 0.16 dB |
| Fidelity when driven (E2) | Linear rungs: no stage saturation; LSD 0.78 dB at ×8 | Follows the reference; LSD 0.22 dB at ×8 |
| Estimated cost | ≈ 120 + 24 osc = 145, 2 VDIV | ≈ 205 + 24 osc = 230, 3 VDIV |
| Room for P0 extras under 247 (257 without SRR) | E1 + E2 + E3 + E4 fit (≈ 170) | E2–E4 fit (0 per sample); E1 (+22–25, +1 VDIV) only if SRR is left out, at about 255 of 257 |

**Renders:**
- `alk_d2_ref_drv{1,4}.wav`, `alk_d2_A_rt_drv{1,4}.wav` and
  `alk_d2_B_rt_drv{1,4}.wav`: the same notes at stock drive and ×4.
- `alk_v5_{acid,lowres}_{A_rt,B_rt}.wav`: the pattern.

**Recommendation:**
- Choose **B_rt** if the drive extra (E2) is wanted as a "drive into the
  filter" control and the measured count confirms ≈ 205.
- Choose **A_rt** if E1 (the post-VCA overdrive) matters more, or if the
  measured B count exceeds the ceiling with E1.
- At stock drive the two cannot be told apart by these measures. Listen
  at ×4.

---

## 8. Open items

| # | Item | Effect | Next |
|---|---|---|---|
| O1 | Expo-law residue: −25 cents at Env 0 to 0 at Env 1; +50 cents at MEG peaks | 1–3.5 % cutoff | Check VR5's taper and the R61/R63 Thevenin against the P4 netlist node by node, or fit one gain on the Env term |
| O2 | Accent current on a legato accented note 5 % low (step 6) | 0.4 dB on that step | Compare the C36 state across the legato pair with SPICE |
| O3 | The gate-on spike after an accented note (sub-millisecond) | Not reproduced | Probably inaudible. Compare the renders |
| O4 | The Padé secants and fast log/exp in place of exact functions | Unmeasured | AL3 grid test in `tools/dsp_test` (P2) |
| O5 | Measured instruction count of A_rt and B_rt | D2 | `fpseq.py` on ARM objects (P0 §4) |
| O6 | Control-rate cost (≈ 600–800 operations per block) | Device CPU gate | Measure in P2. Warm-start the diode solves |
| O7 | Square-table band-limiting uses the level's harmonic count at the top of the octave | Same aliasing behaviour as the existing stock tables | None beyond what the stock tables do |
| O8 | TP6 is not carried in the real-time form | None, unless a TP6 output is wanted | — |
| O9 | Above about 6 kHz cutoff the 1× ladder's response shape near Nyquist differs: 0.6–1.9 dB in band after prewarp | High cutoff settings only | Listen. 2× oversampling (P0 E7) is the only cure, and it does not fit |

---

## 9. Consequences for the plan (P0)

| P0 item | Change |
|---|---|
| §1.2 core estimate | The ladder line becomes `A_rt` ≈ 120 or `B_rt` ≈ 205, including the network and the OTA. The separate "input coupling HPF" and "VCA input HPF" lines are absorbed (C17, C21 and C22 are in the network) |
| §4 options | A = linear rungs with a nonlinear input pair and Q21 (not "one tanh on the feedback sum"). B = all six nonlinearities. Both close the loop through the whole network in its real-time form (§5.3), not one feedback HPF |
| A3 cutoff CV | The expo law of §4.1 gives the ladder current directly (ω_k = I/(4 Vt C_k)). The per-block `fastTan` becomes the TPT prewarp (§5.2), with c(k) from the table there |
| A3/A5/A6 accent | The sweep and the VCA accent share VR7's wiper. Solve them together (§4.3) |
| A7 VCA | VEG as the C42 state with a fitted table; I_acc not gated; release is the one-block ramp (D9) |
| A9 slide | Per-block lag, α = 0.0324; legato as §4.6 |
| Phase P1 | Host models for A and B done, with the real-time forms; D2 inputs ready (§7) |
| Phase P2 | Port `B_rt`/`A_rt` to C (`DiodeLadder_calcBlock()`), the Res table (9–17 points), the control path; AL1–AL9 against this model and `validate_results.json` |
