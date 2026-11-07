# S092 P2 — TB-303 Circuit Model: VCF (SPICE)

- **Session:** 092, phase 2 of the circuit model. Plan:
  `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` §4; previous phase:
  `S092_P1_VCB_MODEL_VCO.md` (VCO, Q8, VCF input).
- **Status (2026-11-07, revised):** a transistor-level model of the VCF
  signal path and resonance loop reproduces the [SN] p7 calibration with
  **no calibrated parts**. It needs one change from the printed schematic:
  **R110 = 22 kΩ** instead of the printed 2.2 kΩ (§2.1). All other values
  were confirmed against the schematic by the user.
  - Not modelled yet: the cutoff/Env Mod CV network (Q9/Q10/Q11; the Q11
    current is an ideal source here) and the MEG/accent sweep (§7).
- **Sources:**
  - Roland TB-303 Service Notes p5 (values), p7 (calibration), p8 (Env Mod
    text) ([SN]).
  - The user's check of the output stage, the resonance loop and R62.
  - T. Stinchcombe's TB-303 filter pages (the lower resonant peak).
  - **[XB]** the `cozycactus/x0xb0x-vcf-bench` model (CC0): a model fitted
    to an ngspice run of an x0xb0x (TB-303 clone) KiCad schematic. Its part
    values were used for comparison only; no code was used.
- **Tags as before:** confirmed, inferred, modelled.

---

## 1. Tooling

| Item | Value |
|---|---|
| Script | `tools/spice/tb303/vcf.py` (ngspice 47; numpy, matplotlib) |
| Outputs | `tools/spice/tb303/out/vcf_*.cir` (netlists), `tools/spice/tb303/vcf_results.json`, `tools/plots/303_vcf_spice.png` |
| Run | `python3 tools/spice/tb303/vcf.py`, about 6 minutes. Raw data goes to a temp folder and is deleted once read |
| Options | `vcf_core(..., r110="22k" or "2.2k", bf21=300 or 400, gfb=None)`. `gfb` is the superseded gain-block variant (§6) |
| AC data | `wrdata` writes (x, re, im) per complex vector and (x, y) for `vdb()`; the script's column indices assume this |

---

## 2. Circuit as modelled

| Block | Values | Status |
|---|---|---|
| Input | S1 → R62 220 kΩ → C17 1 µF → Q12 left base; R70 2.2 kΩ to the bias node (R68 2.2 kΩ from the 5.333 V reference, C16 10 µF) | confirmed (user; R62 also [XB]) |
| Q12 (2SC1583 pair) | Ladder driver; tail = Q11's collector through R69 2.2 kΩ | Ideal tail current I, swept 1–300 µA |
| Ladder | Diode-connected transistor pairs Q13/Q15, Q14/Q17, Q16/Q23; C18 0.018 µF bottom rung, C19/C24/C26 0.033 µF | confirmed (scan; [XB] has the same) |
| Top | Q22 (2SC2291 pair) collectors to the filter rail: +12 V through R95 100 Ω, C28 47 µF. Bases through R115 10 kΩ to that rail, C30 decoupling (value unread; 10 µF used) | confirmed (user) |
| Output stage | C27/C25 0.1 µF from the ladder top (crossed: C27 from the right top node, C25 from the left) to Q21's bases (2SC1583); R113/R114 100 kΩ to a bias node (R116 10 kΩ from the reference, C29 1 µF); Q20 tail: R112 10 kΩ / R109 10 kΩ base divider, R108 2.2 kΩ emitter; R111 2.2 kΩ; **R110 22 kΩ** (§2.1); Q19 follower (R96 10 kΩ) → C14 1 µF → TP6 | confirmed (user) except R110 |
| Resonance loop | TP6 → VR4 50 kΩ(B) to ground; wiper → C15 1 µF → R67 2.2 kΩ → Q18 base (R99 100 kΩ to C22 1 µF ‖ R66 100 kΩ from the reference) → Q18 follower (R94 10 kΩ) → C23 1 µF → R97 10 kΩ → Q12 right base; R98 2.2 kΩ from that base to the R68/C16 bias node | confirmed (user, with the less-obscured image) |
| Load | VCA inputs: R121/C21 from TP6, R122/C22 from VR4's wiper (P3) | 220 kΩ to ground (approximation) |
| Supplies | Up-arrow = +12 V; ⊕ = 5.333 V (IC16, [SN] p3) | confirmed |
| Devices | Generic NPN for all; Q21 current gain 300 (400 as the [XB] value, swept) | generic |

### 2.1 R110: 22 kΩ, not 2.2 kΩ

- **The print reads 2.2 kΩ** (confirmed by the user). **[XB]'s x0xb0x KiCad
  values have R110 = 22 kΩ.** Every other part [XB] lists matches the
  Roland reading:
  - R62, R70, R68, C16, C17, C18, the 0.033 µF rungs, C25, R114;
  - C14, C15, VR4, R67, R99, R66, C22, R94, C23, R97, R98, R96;
  - R121, R122, R124, the 0.01 µF VCA input caps;
  - R46, C13, R119, C36, R120, R138, VR6, C62.
- **Why 22 kΩ is taken (inferred, on circuit behaviour):** it multiplies the
  Q21 stage gain by ten (×10.7 → ×103.6):

  | R110 | Loop gain at Res full (30 µA) | Ladder's threshold | Resonance at Res full (30 µA) | Can it give the p7 ring? |
  |---|---:|---:|---:|---|
  | 2.2 kΩ (print) | 1.93 | 19.1 | +1.6 dB, at 7× the right frequency | No |
  | **22 kΩ** ([XB]) | **18.7** | 19.1 | **+26.3 dB** at 1,948 Hz | **Yes**: 2.03 ms, 0.63 per cycle (§3.2) |

- **Either the print or the clone is wrong.** A 2.2 kΩ R110 cannot make the
  303 resonate the way it does. Only R110 matters here: R111 carries no
  signal to the output.
- R110's DC drop (0.25 mA × 22 kΩ ≈ 5.6 V) puts Q21's collector near 6.3 V,
  which is a normal operating point.

### 2.2 Withdrawn from the first version of this document

- **The "TP6 DC level" clue:** TP6 is after C14 (user), so the sketch's
  5.2–5.8 V marks cannot be a DC level.
- **The calibrated gain block (G = 8.5):** it is superseded by R110 =
  22 kΩ. It had reproduced p7 by putting the missing gain in the wrong
  place (the feedback path). The normalised shapes it gave match the
  corrected model closely; the levels did not.
- **The "R108 = 220 Ω" test:** R108 is 2.2 kΩ (user).

---

## 3. Results (modelled, R110 = 22 kΩ)

### 3.1 The ladder and the loop

- **The ladder is a diode ladder with spread poles.** DC gain 0.998. Slope
  about −4.5 dB/oct from its −3 dB point (fc) to 4·fc, and −13 to
  −18.5 dB/oct from 8·fc to 32·fc. This is the "about 18 dB/oct over much
  of the audible range" behaviour ([OL], citing Stinchcombe).
- **The phase turn needed for resonance is far above fc.** At 30 µA, fc is
  166 Hz, but the ladder's phase turns through 180° only at 2,024 Hz, where
  its gain is −25.6 dB. That is why the loop needs a gain of 19.
- **The loop gain is 18.7 at Res full, 98 % of the threshold.** The 303
  rings strongly but stays stable. That fits a filter that "nearly"
  self-oscillates, and fits Q21's current gain and R97 being the parts that
  move its margin in [XB].
- **The output stage puts a high-pass inside the loop.** C27/C25 feed
  Q21's input resistance (about 34 kΩ differential, in parallel with
  R113/R114). The corner is about 40–60 Hz at Res minimum and 90–120 Hz
  with feedback. Because the loop takes its signal after this high-pass,
  resonance weakens at low cutoff and the bass returns below it.
- **The lower resonance near 8 Hz:** at Res full the output has a
  lightly damped peak at **7–9 Hz**, up to +3 dB over the passband. This
  matches Stinchcombe, who found the 303's lower resonant peak "very real":
  his TBX-303 clone oscillated at about 8 Hz with a little extra
  capacitive load. In the model it is stable: with no input, the output
  decays to under 20 nV after 0.8 s at every cutoff tried.

### 3.2 [SN] p7 calibration

Settings: Cutoff centre, Res full, Env Mod/Decay/Accent minimum, saw.

| Q21 current gain | Q11 current | Ring period | Decay per cycle | Ringing, p-p |
|---:|---:|---:|---:|---:|
| 300 | 7.4 µA | **2.03 ms** | **0.63** | 0.39 V |
| 400 | 7.4 µA | 2.06 ms | 0.66 | 0.41 V |

- [SN] p7: 2.0 ms ± 0.5 ms, decaying within each C2 cycle. The p5 sketch
  shows the same form: ringing that dies away within the cycle and repeats
  at each reset. Its "5.8 V / 5.2 V" marks span 0.6 V; the model's ringing
  is 0.39 V p-p.
- **Calibration anchor for the DSP:** Cutoff centre with Env Mod minimum ↔
  Q11 ≈ 7.4 µA ↔ resonance at 500 Hz (±25 %).

### 3.3 Cutoff and resonance

| Q11 current | Ladder −3 dB | Resonance frequency, Res 0.5 / 0.8 / full | Resonance boost, Res 0.5 / 0.8 / full |
|---:|---:|---|---|
| 1 µA | 15 Hz | 83 / 86 / 86 Hz | +1.0 / +1.6 / +2.1 dB |
| 3 µA | 22 Hz | 194 / 209 / 217 Hz | +2.9 / +4.9 / +7.0 dB |
| 10 µA | 52 Hz | 547 / 614 / 663 Hz | +5.5 / +10.2 / +16.2 dB |
| 30 µA | 166 Hz | 1,546 / 1,803 / 1,948 Hz | +7.1 / +13.9 / +26.3 dB |
| 100 µA | 591 Hz | 5,097 / 5,945 / 6,421 Hz | +7.8 / +15.7 / +29.3 dB |
| 300 µA | 1,874 Hz | 14,972 / 17,464 / 19,601 Hz | +7.9 / +16.3 / +27.5 dB |

(Source: `vcf_results.json`, `model.ac`.)

- **Resonance frequency is nearly proportional to the current** (∝ I^0.88)
  and sits about **12× above the ladder's −3 dB point**. In the DSP,
  "cutoff" must mean the resonance frequency.
- **Resonance weakens at low cutoff:** +29 dB at 6.4 kHz, +16 dB at
  660 Hz, +7 dB at 217 Hz, +2 dB at 86 Hz (Res full).
- **The passband drops with resonance:** at Res full the in-band level is
  about 25 dB below Res minimum (negative feedback, 1 + loop gain), with
  the bass returning below the in-loop high-pass.

### 3.4 Drive (C2 saw, 4 × I_cal ≈ 30 µA)

| Res | Input level ×0.25 / ×1 / ×2 | Output, V p-p | Per unit input | h2 / h3 re. h1 at ×0.25 → ×2 |
|---|---|---|---|---|
| minimum | 0.25 / 1 / 2 | 0.74 / 2.72 / 4.49 | 2.97 / 2.72 / 2.25 (−0.8 dB at ×1, −2.4 dB at ×2) | −5.3 → −8.2 / −9.7 → −13.4 dB |
| full | 0.25 / 1 / 2 | 0.13 / 0.49 / 0.90 | 0.50 / 0.49 / 0.45 | −6.5 → −6.4 / −10.1 → −9.9 dB |

- **Two nonlinearities matter at the stock level:** the Q12 input pair (the
  saw at about ±1 Vt, P1) and now the Q21 output pair, whose output swing
  is several volts.
- At Res minimum, more drive gives a duller, quieter tone, not a buzzier
  one. At Res full the negative feedback keeps it nearly linear at these
  levels.

---

## 4. External cross-checks

| Check | Result |
|---|---|
| [SN] p7 calibration | Reproduced (§3.2) |
| Stinchcombe: the lower resonant peak (~8 Hz) is real | Reproduced: a 7–9 Hz peak at Res full, stable but lightly damped (§3.1) |
| [XB] part values | Same as the Roland reading except R110 (§2.1). [XB] calls the R122 capacitor C20; the Roland scan labels it C22 |
| [XB] Q21 tail current 0.467 mA (fitted to its loop margins) | This model: 0.51 mA from the R112/R109/R108 bias as read |
| [XB] says its own model was fitted to a simulation, not to a real 303 | The same applies here: no real unit has been measured (P0 D5) |

---

## 5. Consequences for the Alkaline filter (P0 §4)

- **Topology:** a 4-stage diode ladder (coupled stages) with the bottom
  capacitor at 0.018/0.033 of the others.
- **"Cutoff" = the resonance frequency,** about 12× the ladder's −3 dB point.
- **The high-pass is inside the resonance loop** (C27/C25 into Q21, about
  60–120 Hz, set by Q21's bias and current gain). This one placement
  produces the low-cutoff resonance loss, the bass return and the ~8 Hz
  lower peak.
- **Loop gain at Res full is 98 % of the threshold** at mid cutoffs. There
  is no stock self-oscillation; that stays an extra (P0 E3).
- **Nonlinearities:** tanh at the Q12 input pair (saw at ±1 Vt) and tanh at
  the Q21 output pair inside the loop. P0 option B should place one
  nonlinearity at each; §3.4 is the test reference.
- **Calibration anchor:** Cutoff centre / Env Mod minimum ↔ 500 Hz
  resonance (P0 A4b, host law AL2b).

---

## 6. Superseded

- The `gfb` gain-block variant (G = 8.5) is kept in `vcf.py` for comparison
  but is not the model. R110 = 22 kΩ does its job in the right place.

---

## 7. Next

Steps 1 and 2 are done in `S092_P4_CONTROL_NET.md`. Its correction: R47
and R61 return to Q9's node N561, not to each other.

1. **Expo and CV network:** Q9/Q10/Q11 with R63 220 kΩ, R72 100 kΩ, R73
   100 kΩ, TM3 470 kΩ (trimmer), R71 2.2 kΩ, R64/R65 10 kΩ, VR3 (via R47
   10 kΩ) and VR5 (with R61 10 kΩ). Map Cutoff and Env Mod knob positions to
   Q11 current, including the Env Mod pot's base-lowering effect ([SN] p8).
   [XB] states the law as I = K·(I(R73) − Q11's base current)·
   exp((V134 − V561)/Vt), with Q9 holding the reference node.
2. **MEG and the accent sweep:** C62/R138/VR6 with the IC12 switch;
   VR7 → D24/R46 → C13 with VR4's second gang → R72 into the CV node.
3. **One end-to-end run** (VCO → VCF → VCA) of an accent and slide pattern,
   as the reference for the host tests.
