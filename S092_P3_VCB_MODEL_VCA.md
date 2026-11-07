# S092 P3 — TB-303 Circuit Model: VCA (SPICE)

- **Session:** 092, phase 3 of the circuit model. Plan:
  `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` (A7, A12, §5.4); previous phases:
  `S092_P1_VCB_MODEL_VCO.md` (VCO), `S092_P2_VCB_MODEL_VCF.md` (VCF).
- **Status (2026-11-07, revised):** modelled:
  - the volume envelope generator (VEG: Q31/Q32, C42) at transistor level;
  - IC15's two inputs, traced by the user: A = the filter output, B = the
    Resonance pot's wiper;
  - the accent branch into IC15 pin 1 (R119 from the Accent pot's wiper,
    traced by the user);
  - the OTA's linearity.

  **Decided (user, 2026-11-07):** the Roland constants from this model
  are used (12 V gate; [XB]'s were considered, §4). The attack is 2.5–3 ms
  (§3.2), and the gate is 3.5 of 6 clocks. The release is a one-block
  (0.73 ms) ramp (P0 D9).
- **Sources:**
  - Roland TB-303 Service Notes p1 (Gate Out spec), p3 (block diagram: VCA
    ENV GEN Q31/Q32 with GATE, TRIGGER, ACCENT; 12 V and 5.333 V from
    IC16), p5 (values), p8 (IC15 = BA662A "vari-conductance amp") ([SN]).
  - The user's traces (R119, R121/C21, R122/C22).
  - **[XB]**, the `cozycactus/x0xb0x-vcf-bench` model (CC0), fitted to an
    ngspice run of an x0xb0x KiCad schematic. Its numbers are used for
    comparison only.
- **Tags as before:** confirmed, inferred, modelled.

---

## 1. Tooling

| Item | Value |
|---|---|
| Script | `tools/spice/tb303/vca.py` (ngspice 47; numpy, matplotlib) |
| Outputs | `tools/spice/tb303/out/vca_*.cir`, `tools/spice/tb303/vca_results.json`, `tools/plots/303_vca_spice.png` |
| Run | `python3 tools/spice/tb303/vca.py`, about a minute. Raw data goes to a temp folder and is deleted once read |
| Lessons | **Pin 1:** IC15 pin 1 is a current-mirror input (a diode-connected transistor to V−) and can only sink current. An ideal voltage source there produced a false 1.5 mA "peak" (first run, discarded). **Node names:** they must be unique across sub-circuits. The accent branch first reused the trigger network's node `n36`, which wired the Accent pot into Q32's trigger and gave a false accent result (caught and fixed) |

---

## 2. Circuit as modelled ([SN] p5, user's traces)

| Block | As read / traced | Model |
|---|---|---|
| Trigger → Q32 | TRIGGER → D36 → R134 22 kΩ → Q32 base, C41 0.1 µF to ground; Q32 emitter R130 100 Ω; collector → R132 100 Ω → C42 | as read; trigger 0 → 5.333 V, width swept 0.05–5 ms |
| Envelope capacitor | C42 1 µF to ground; R123 1.5 MΩ to the up-arrow supply | as read; the up-arrow is **+12 V** (IC16, [SN] p3; also Stinchcombe) |
| Q31 (PNP) | Base on C42; emitter → R131 220 kΩ → R133 2.2 kΩ → D35 / R137 1 kΩ / D34 → the gate line | as read; gate high = **12 V** (inferred, §3.1), 5.333 V as the alternative |
| IC15 pin 1 | Amplifier bias current input | Diode-connected NPN to ground. Q31's collector and the accent branch both feed it, and their sum is the VCA gain |
| Accent branch | Accent pot VR7 (50 kΩ B) wiper → R119 47 kΩ → C36 0.033 µF → R120 22 kΩ → D27 → pin 1 (traced by the user) | VR7's top driven by the accented MEG: 10.2 V peak ([XB]: MEG maximum 10.13–10.28 V), decaying 200 ms to 10 % (P0 A5/A6) |
| IC15 inputs | Pin 3: **A** = TP6 (filter output after C14, top of VR4) → C21 0.01 µF → R121 220 kΩ; **B** = VR4's wiper (also feeding C15) → C22 0.01 µF → R122 100 kΩ; R124 2.2 kΩ to the bias node (R126 2.2 kΩ from 5.333 V, C37 10 µF). Pin 2: R125 2.2 kΩ to the same node | as traced. B = k_res × TP6 (pot loading neglected) |
| IC15 output | Pin 6 → buffer (7 → 8) → C38 1 µF → VR8 Volume 50 kΩ(A). Load (R127 47 kΩ + R129 22 kΩ) ‖ R128 220 kΩ to the C40 node (10 µF; R162 2.2 kΩ from 5.333 V) | as read; buffer ideal; Volume at full |
| OTA | BA662A | I_out = I_abc · tanh(v_in / 2Vt) |

---

## 3. Results (modelled)

### 3.1 VEG decay, gate held

| Gate level | Peak current | −6 dB | −20 dB | −40 dB | Silent |
|---|---:|---:|---:|---:|---:|
| **12 V** | 46.0 µA | 1.01 s | 2.68 s | 3.85 s | 4.51 s |
| 5.333 V | 17.0 µA | 0.35 s | 0.67 s | 0.80 s | 0.87 s |

- **Shape:** the current is (gate − 2 diode drops − Q31's Vbe − V_C42) /
  222 kΩ. V_C42 rises with τ = R123·C42 = 1.5 s, so the current is an
  exponential minus an offset and reaches zero at a finite time.
  - Close fit: g(t) = 1.052·e^(−t / 1.5 s) − 0.052. The DSP uses a table
    fitted to the SPICE curve.
- **The gate level is not just on/off: it sets the decay length** (4.5 s
  against 0.9 s to silence). That is why it matters for the emulation even
  though Alkaline toggles the gate itself.
  - The voltage Q31 sees fixes the decay table. Once chosen, it is a
    constant.
- **Which level?**
  - The +5 V figure (web search) fits the CPU's logic (5.333 V, IC16) or
    other gear.
  - [SN] p1 gives **Gate Out: ON +12 V**, and D34 hangs on the external
    gate line (user). So Q31's gate path most likely sees 12 V (inferred).
  - The 12 V result also fits the "3–4 s" VEG decay usually quoted for the
    303.
- **Short notes:** a 58 % 16th at 120 BPM (72.5 ms) is still at −0.2 dB
  when the gate drops. The decay is heard only on long gates: slides,
  ties, slow tempos.

### 3.2 Attack depends on the trigger width (gate 12 V)

| Trigger | Attack 10→90 % | Peak |
|---:|---:|---:|
| 0.05 ms, 0.2 ms | none (Q32 never fires) | 0 |
| 0.5 ms | 10.5 ms (Q32 half on) | 46 µA |
| 1 ms | 1.0 ms | 46 µA |
| 2 ms, 5 ms | 0.81 ms | 46 µA |

C41 charges through R134 (τ 2.2 ms), so the trigger must last about
0.5 ms or more. **The CPU's trigger width is not in [SN].**

**Decided attack: 2.5–3 ms (user).** A finer sweep shows which trigger
width gives it:

| Trigger | Attack 10→90 % | Trigger to 90 % |
|---:|---:|---:|
| 0.55 ms | 5.15 ms | 6.20 ms |
| 0.60 ms | 3.45 ms | 4.42 ms |
| **0.65 ms** | **2.61 ms** | 3.52 ms |
| **0.70 ms** | 2.11 ms | **2.99 ms** |
| 0.75 ms | 1.77 ms | 2.63 ms |
| 0.80 ms | 1.53 ms | 2.38 ms |
| 0.90 ms | 1.21 ms | 2.06 ms |

- A trigger of about **0.65–0.75 ms** reproduces 2.5–3 ms: 0.65 ms on the
  10→90 % definition, 0.70–0.75 ms on trigger-to-near-full.
- The attack is steep in this range because Q32 is only just turning on,
  so real units would differ with component tolerance.
- Alkaline uses the attack time directly, as a constant.

### 3.3 Release at gate-off

- **Under 1 µs (90→10 %).** No timing capacitor sits in the gate path as
  read, so Q31's emitter supply and the VCA current collapse at once.
- [XB] disagrees (half the level gone 7.7 ms after the gate falls; §4).

### 3.4 Accent in the VCA (gate 12 V, 1 ms trigger)

| Accent pot VR7 | Peak pin-1 current | Re. no accent | At 200 ms |
|---|---:|---:|---:|
| no accent | 46.0 µA | 0 dB | 41.8 µA |
| half | 91.3 µA | **+5.9 dB** | 42.0 µA |
| full | 170.9 µA | **+11.4 dB** | 44.4 µA |

- The accent adds VCA current that follows the accented filter envelope.
  C36 smooths its onset (τ about 1.5 ms). It is gone once the envelope
  falls below D27's and pin 1's two diode drops, about 1.2 V (after about
  200 ms here). An accented note is louder at its start, and the boost
  decays with the filter envelope.
- The same VR7 wiper feeds the filter's accent sweep (D24/R46/C13, P0 A6).
  One knob sets both.

### 3.5 Signal path into the VCA

| Path | High-pass corner (−3 dB) | Level into the OTA |
|---|---:|---:|
| A alone (TP6 → C21 → R121) | 66 Hz | −40.3 dB |
| B alone (VR4 wiper → C22 → R122) | 154 Hz | −33.4 dB |
| **A + B, Res 0** | **66 Hz** | −40.3 dB |
| A + B, Res 0.25 | 94 Hz | −36.5 dB |
| A + B, Res 0.5 | 109 Hz | −33.8 dB |
| **A + B, Res full** | **127 Hz** | **−30.2 dB** |
| Output (C38 into Volume) | 2.9 Hz | — |

- **The Resonance knob also changes the VCA's input.** Turning it up
  raises the VCA input by 10 dB and moves the high-pass from 66 Hz to
  127 Hz.
  - The +10 dB partly offsets the filter's in-band drop at full resonance
    (about −25 dB, P2 §3.3).
  - The moving corner thins the low notes further at high resonance: at C1
    (33 Hz) the high-pass costs 6.9 dB at Res 0 and 10.7 dB at Res full.
- **The OTA is linear at the 303's level:** 2.9 mV peak at pin 3 for 0.6 V
  p-p at TP6 gives a third harmonic at −72 dB. The VCA adds no distortion.

---

## 4. Against [XB]

| Quantity | This model | [XB] (x0xb0x KiCad, whole-chain ngspice fit) |
|---|---|---|
| VEG peak current | 46.0 µA (12 V gate) | 62.65 µA |
| VEG decay | τ 1.5 s (R123·C42) with an offset; silent at 4.5 s | τ 2.29 s |
| Gate → VCA open | ~1 ms after the trigger | a step 5.63 ms after the first gate (0.3–6 ms by history) |
| Release | under 1 µs | half the level 7.7 ms after the gate falls |
| Filter-side values (R119, C36, R120, R46, C13, R138, VR6, C62) | as read | the same |

- **The difference sits in the gate and trigger front end.** The x0xb0x
  drives its gate and trigger from its own microcontroller, so its front
  end may differ from the Roland's. Its delays and release could come from
  that.
- This model drives the Roland's network with ideal gate and trigger edges.
- Neither has been checked against a real 303 (P0 D5).
- **Size of the difference** (decay re. peak, this model / [XB]):

  | After | This model | [XB] |
  |---:|---:|---:|
  | 125 ms | −0.8 dB | −0.5 dB |
  | 500 ms | −3.1 dB | −1.9 dB |
  | 1 s | −6.2 dB | −3.8 dB |
  | 2 s | −13.0 dB | −7.6 dB |

  Under 1 dB for notes shorter than about 0.5 s.
- **[XB]'s τ 2.29 s cannot come from the Roland values.** τ = R123·C42 =
  1.5 s whatever the gate level, and the gate's offset makes the decay
  faster, never slower. [XB]'s 62.65 µA peak would need about 15.7 V on
  this network. These are x0xb0x constants. With [XB]'s larger peak, the
  same accent current gives +9.5 dB instead of +11.4 dB.
- **Decided (user, 2026-11-07): the Roland constants (this model):** the
  faster decay and the stronger accent.

---

## 5. Consequences for the Alkaline VCA (P0 §1.2, §5.3–§5.4)

| Item | Design |
|---|---|
| Gain law | Linear in the control current; no VCA nonlinearity |
| Attack | **2.5–3 ms** (decided; §3.2): a per-sample ramp over 4 blocks |
| Decay | **Table from the SPICE curve (12 V gate):** −6 dB at 1.0 s, −20 dB at 2.7 s, silent at 4.5 s. Control rate, ramped per sample (D10, decided) |
| Release | **A one-block ramp (0.73 ms)** (P0 D9, decided); the circuit itself cuts in under 1 µs |
| Accent | Extra gain following the accented filter envelope: +11.4 dB at full Accent, +5.9 dB at half, gone after about 200 ms. Same knob as the filter's accent sweep |
| Input network | A one-pole high-pass on the filter output whose corner and gain follow Resonance: 66 Hz / −40.3 dB at Res 0 to 127 Hz / −30.2 dB at Res full (the A + B sum). Inside the P0 §1.2 "VCA + output HPF" budget: one one-pole filter, with its coefficient and gain per block |
| Gate length | **3.5 of 6 clocks** (decided, P0 D11); slide: held for 6 clocks plus the next step's gate |

---

## 6. Open items

| # | Item | Status |
|---|---|---|
| V1 | R119 source | **Resolved** (user): Accent pot VR7's wiper |
| V2 | VCA inputs A and B | **Resolved** (user): A = TP6, B = VR4's wiper |
| V3 | Gate level at D34 | **12 V.** D34 is on the gate line that also feeds the Gate Out jack (+12 V, [SN] p1). A stock 303 has no gate input; 5 V gate inputs belong to modified units and clones, and the CPU's own logic is 5.333 V. Built into the chosen constants |
| V4 | R123's supply | **+12 V** (IC16 rail; Stinchcombe agrees. Stinchcombe's page has other values wrong (user), so it supports this but does not prove it) |
| V5 | CPU trigger width | **Resolved by assumption:** attack 2.5–3 ms (user) ↔ trigger about 0.65–0.75 ms (§3.2) |
| V6 | VEG constants (this model against [XB]) | **Decided:** this model's (Roland) constants (§4). Release: a one-block ramp (P0 D9) |
| V7 | Gate length | **Decided: 3.5 of 6 clocks** ([OL]; a web summary gives 3) |

---

## 7. Next

1. One end-to-end run (VCO → VCF → VCA) of an accent and slide pattern, once
   the CV/MEG network (P2 §7) is modelled. This is the reference for the
   Alkaline host tests (P0 §7.1 AL4, AL5).
