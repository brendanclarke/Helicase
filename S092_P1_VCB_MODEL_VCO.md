# S092 P1 — TB-303 Circuit Model (SPICE)

- **Session:** 092, phase 1 (circuit modelling). Plan:
  `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` (§2.1, §3.3, §6).
- **Status (2026-11-07):** **done:** VCO saw core, Q8 square shaper and the
  VCF input network. The VCF and VCA continue in S092 P2 and P3 (§8).
- **Source of every component value:** Roland TB-303 Service Notes, first
  edition, 19 Feb 1982, p5 analog schematic ([SN]; URL in the P0 plan §3.1).
  Where a value comes from somewhere else, the table says so.
- **Tags as in P0:** **confirmed** = read in a source; **inferred** = my
  reasoning; **modelled** = a SPICE result that depends on the stated
  assumptions.

---

## 1. Tooling

| Item | Value |
|---|---|
| Simulator | ngspice 47 (Homebrew, `brew install ngspice`), batch mode |
| Script | `tools/spice/tb303/vco_q8.py`: writes one netlist per run, runs ngspice, analyses the result, plots |
| Outputs | `tools/spice/tb303/out/*.cir` (netlists); `tools/spice/tb303/vco_q8_results.json`; `tools/plots/303_vco_q8_spice.png`. The raw `wrdata` files (about 60 MB per run) go to a temp folder and are deleted once read |
| Run | `python3 tools/spice/tb303/vco_q8.py` (numpy, matplotlib); about 2 minutes, 46 transient runs |
| Analysis | The last full cycle after the C11 network has settled (≥ 0.45 s or 30 periods). Cycles are cut at the reset edges, so phase 0 is the reset in every plot |
| ngspice notes | `wrdata` cannot take a path containing spaces, so the script runs ngspice inside `out/`. `ne` is an operator in ngspice expressions, so it cannot be a node name |

---

## 2. Circuit as modelled

### 2.1 Saw core

| Part | [SN] p5 | Model |
|---|---|---|
| Q26 (2SC1583 matched pair), exponential converter | IC11a (AN6562) drives the pair; TM4 tune; TM5 width; R100 560 Ω posistor | Current mirror: Q26A diode-connected, fed by an ideal reference current (the converter's job); Q26B's collector drives C33. Only the output transistor's behaviour (Early effect) affects the shape |
| C33 | 0.01 µF to ground | 0.01 µF |
| Reset (Q24, Q25, Q27, D25, R101 10 kΩ to a reference) | A comparator that recharges C33 from the 12 V rail | **Behavioural** switch, R_on 100 Ω: closes at 5.5 V, opens 50 mV below the reset level. Reset level default **11.3 V** (12 V less D25 and Q27's saturation, inferred); **12 V** run as the bound |
| Q28 (2SK30A-O, IDSS 0.6–1.4 mA) source follower | Drain to +12 V, R105 10 kΩ to ground | Generic N-JFET, VTO −1 V, IDSS 1 mA (swept 0.6–3 mA) |

**Direction (corrects P0 §2.1):** the scope sketch beside Q28 on [SN] p5
shows a vertical rise to 12 V followed by a ramp **down** to 5.5 V. That
fits the circuit: an NPN collector draining a grounded capacitor. The
rising ramps on the p7 adjustment page are a drawing for setting the
width.

### 2.2 Q8 square shaper

| Part | [SN] p5 | Model |
|---|---|---|
| Q8 (2SA733, high-gain PNP; inferred from the parts list and x0x notes) | — | Generic PNP, BF 300 (swept 150–600), VAF 80 |
| Base | R35 100 kΩ from the Q28 source; R34 10 kΩ in series with C10 0.01 µF in parallel with it | as read |
| Emitter | R45 22 kΩ to the supply (assumed +12 V), C11 1 µF to ground | as read |
| Collector (square output, to S1) | R36 10 kΩ to ground | as read |

### 2.3 Load: S1 into the VCF input

| Part | Source | Model |
|---|---|---|
| R62 | **220 kΩ**: x0xb0x VCF documentation (search-result summary; the page now returns 404). The p5 scan shows a three-character value that is too smudged to read and fits "220K" | 220 kΩ |
| C17 | 1 µF ([SN] p5) | 1 µF |
| R70 to the Q12 bias node, C16 10 µF to ground | 2.2 kΩ, 10 µF ([SN] p5) | R70 to AC ground. Q12's base current is neglected |

S1 connects only the selected wave to R62. Each pitch is run once with
the saw loaded and once with the square loaded.

---

## 3. Results

### 3.1 Per pitch (defaults: reset 11.3 V, IDSS 1 mA, Q26 VAF 100, Q8 BF 300)

The pitch is the simulated frequency. The reference current is set so
that the run lands within about 3.5 % of the note.

| Note | f (Hz) | Saw at Q28 source (V) | Saw bow | Saw slope change | Reset | Square at Q8 collector (V) | Square on-time | Q12 drive: saw / square (mV p-p) | Square re. saw at Q12 |
|---|---:|---|---:|---:|---:|---|---:|---|---:|
| C1 | 33.9 | 5.81 – 11.15 | 0.47 % | −5.4 % | 2.2 µs | 0.08 – 4.08 | 51.1 % | 53.1 / 39.6 | −2.5 dB |
| C2 | 67.7 | 5.81 – 11.16 | 0.48 % | −5.4 % | 2.3 µs | 0.07 – 5.68 | 37.7 % | 53.7 / 55.3 | +0.3 dB |
| A2 | 113.8 | 5.81 – 11.16 | 0.49 % | −5.6 % | 2.2 µs | 0.07 – 7.09 | 29.7 % | 53.4 / 69.6 | +2.3 dB |
| C3 | 135.3 | 5.81 – 11.16 | 0.51 % | −5.7 % | 2.1 µs | 0.07 – 7.41 | 28.2 % | 53.3 / 72.8 | +2.7 dB |
| C4 | 270.1 | 5.82 – 11.16 | 0.56 % | −5.9 % | 2.4 µs | 0.07 – 7.66 | 25.5 % | 53.1 / 75.3 | +3.0 dB |
| C5 | 539.5 | 5.83 – 11.16 | 0.66 % | −6.4 % | 2.1 µs | 0.07 – 7.77 | 23.2 % | 52.9 / 76.3 | +3.2 dB |

How each column is measured:
- **Bow:** the largest deviation from a straight-line fit of the ramp (5–95 %
  of the cycle), as a percentage of p-p.
- **Slope change:** end-of-ramp slope against start-of-ramp slope. Negative
  means the ramp slows as it falls.
- **On-time:** the fraction of the cycle above mid-level. The threshold at
  the AC-coupled zero crossing gives the same figure within 0.7 points.
- **Reset** is set by the behavioural R_on, so it is an assumption, not a
  result. Anything under one sample (22.7 µs) is inaudible at 44.1 kHz.

### 3.2 Sensitivity

| Parameter | Range | Effect |
|---|---|---|
| Q26 VAF (Early voltage) | 30 / 100 / 300 V | Saw bow 1.1 / 0.47 / 0.24 %; slope change −12.5 / −5.4 / −2.7 % (C2). The Early effect is the only source of curvature in the core |
| Q8 BF | 150 / 300 / 600 | On-time C1 48.2 / 47.9 / 47.6 %, C2 36.0 / 34.8 / 34.0 %, C5 22.7 / 22.8 / 22.8 %; collector peak changes by 0.1–0.3 V. **The duty is set by the network, not by the transistor gain** |
| Q28 IDSS, reset 11.3 V | 0.6 / 1.0 / 1.4 / 3.0 mA | Saw bow 0.42–0.50 %, slope change −4.8 to −5.7 %; the source level shifts by 0.5 V |
| Q28 IDSS, reset 12 V | 0.6 / 1.0 / 1.4 / 3.0 mA | Bow 0.50 / 1.87 / 2.29 / 3.25 %; slope change +0.7 / +29 / +37 / +63 %. With C33 reset to the drain rail, Q28's drain-source voltage collapses at the top of the ramp, and the follower compresses the top |

Which reset level the real circuit reaches decides whether the saw's top is
compressed. The D25/Q27 path makes 11.3 V the more likely value
(inferred), but only a capture of the Q28 source can settle it.

---

## 4. Findings

### 4.1 Saw (modelled)

- The core ramp is straight to about 0.5 %, with a slight bow and a
  5–6 % slow-down as it falls (Early effect of Q26). With a reset to 12 V,
  the top of the ramp is compressed instead (§3.2).
- **For Alkaline:** `sawTable` (an ideal saw, used inverted or not) is a
  close match. The bow is −46 dB of p-p and lies in the lowest harmonics,
  so a curved table is not justified until a capture shows the 12 V-reset
  case.

### 4.2 Square (modelled)

- **Shape:** a pulse, not a square. The collector sits near 0 V for most of
  the cycle. Q8 switches on late in the falling ramp and off at the reset.
  The "on" level is not flat: it rises through the pulse because the
  current follows the falling base and C11's discharge. The rise into the
  pulse is soft at low pitch and sharp at high pitch.
- **On-time falls with pitch:** 51 % at C1, 38 % at C2, 23 % at C5.
  - **Mechanism:** C11 recharges through R45 (τ 22 ms) between pulses. Each
    pulse removes the charge that came in, so a longer period gives a
    longer pulse.
  - Q8's gain hardly changes the result.
- **Level rises with pitch:** the pulse top runs from 4.1 V at C1 to 7.8 V
  at C5. At the filter input the square is 2.5 dB below the saw at C1 and
  3.2 dB above it at C5. The [SN] sketch (top near 8 V on the A key)
  agrees in scale.
- **Against [OL]** (45 % high, 71 % at the lowest pitches):
  - The **direction matches**: duty falls as pitch rises (the model's ratio
    is about 2.2 : 1 across C1–C5; [OL]'s is about 1.6 : 1).
  - The **absolute values do not**: the model is about 20 points lower
    throughout.
  - The polarity question does not explain it. If [OL] measured the low
    part, its trend would run the opposite way to the model.
  - The candidates are below (§5).

### 4.3 Filter input (modelled)

- **Drive:** R62/R70 divides by about 101. The saw reaches the Q12 base at
  about 53 mV p-p, which is ±26 mV, or **±1.0 thermal voltage** (Vt =
  25.9 mV at 27 °C). The square reaches 40–76 mV p-p (±0.8–1.5 Vt).
  - In the differential pair's tanh(v / 2Vt), ±1 Vt compresses the peaks
    by about 8 %, so the input pair runs into mild nonlinearity, not hard
    clipping. Any stronger "drive" character must come from the ladder
    and its resonance loop, which is the next model.
  - The Alkaline ladder's input scale is calibrated so that the saw peak is
    1.0 Vt.
- **High-pass:** C17 with R62 + R70 gives a corner of **0.72 Hz**. The VCF
  input does **not** high-pass the waves audibly.
  - This withdraws the 40 Hz coupling hypothesis in
    `tools/plots/303_raw_vco_vs_stock.png` row 4 (P0 §2, point 2).
  - The high-pass relationship [OL] reports must come from the square
    shaper itself (its rising top and pitch-dependent pulse, §4.2) and from
    stages after the input: the resonance loop, the Q21 output coupling and
    the VCA. Those are in the VCF model (§8).

---

## 5. Open questions from this model

| # | Question | What would settle it |
|---|---|---|
| V1 | The ~20-point duty offset against [OL] | (a) A capture of Q8's collector on a real 303 (D5). (b) [OL]'s measurement method: which signal, which threshold, filter settings. (c) The model's assumptions: R45's supply (assumed +12 V), the comparator's bottom threshold (5.5 V from the sketch), the reset level. Each can be swept in `vco_q8.py` |
| V2 | Reset level, 11.3 V or 12 V (top-of-ramp compression) | A capture of the Q28 source; or modelling Q24/Q25/Q27/D25 as drawn (the topology needs a cleaner read than the scan gives) |
| V3 | Q12 base loading and the bias at R70 | The ladder model (§8) includes the Q12 pair and its tail current |
| V4 | R62 = 220 kΩ | Any legible copy of p5, or the x0xb0x BOM |

---

## 6. Consequences for the Alkaline plan

| P0 item | Change |
|---|---|
| §2.1 saw | Falls at the Q28 source (from 12 V to 5.5 V on the sketch; about 11.2 V to 5.8 V modelled). `sawTable` is confirmed as a close match by the model (§4.1) |
| §2.1 square, D8 | **D8 (a) holds and extends:** each octave level of the square table carries its own pulse width, rising top **and level**. The level change (−2.5 to +3.2 dB against the saw, C1–C5) can be baked into each level's amplitude instead of peak-normalising every level. Runtime cost stays 0. The table generator samples this SPICE model, one level per octave, once V1 is resolved |
| §0 "Drive into the filter" | Mild at the input pair (±1 Vt). Calibrate the ladder input to that, and let the ladder model provide the rest |
| §3.2 A12 | The VCF input corner is 0.72 Hz (negligible); R62 = 220 kΩ (cross-reference) |
| P0 §2 point 2 | The "40 Hz coupling" explanation of `Re3`'s shape is withdrawn (§4.3). `Re3` is still an output capture; the stages that shaped it are downstream |

---

## 7. Assumptions in one place

1. **Device models are generic, not vendor models:** 2SC1583, 2SA733 and
   2SK30A are approximated by class-typical parameters. The parameters that
   matter are swept (§3.2).
2. **The reset comparator is behavioural.** Its thresholds come from the
   [SN] sketch (bottom 5.5 V) and from inference (top 11.3 V; 12 V as the
   bound).
3. **The expo converter is an ideal reference current into a matched
   pair.** IC11a's loop, TM4/TM5, temperature and tracking are not
   modelled. They set pitch, not shape.
4. **R45 returns to +12 V** (the drawing shows a supply arrow; the rail is
   inferred from the VCO's other supply arrows).
5. **Q12's input is treated as an open circuit**, and R70 returns to AC
   ground.
6. **Temperature is 27 °C.**

---

## 8. Next (P1 continues)

Steps 1 and 3 below are done: `S092_P2_VCB_MODEL_VCF.md` (VCF) and
`S092_P3_VCB_MODEL_VCA.md` (VCA). Step 2 (MEG and accent sweep) and the
expo/CV network are listed under "Next" in P2.


1. **VCF ladder:**
   - Q12 driver pair, the three ladder pairs with C18 0.018 µF and
     C19/C24/C26 0.033 µF, the Q22 (2SC2291) top pair, and the Q21
     differential output → Q20/Q19.
   - The resonance loop: VR4 → C15 1 µF → R67 → Q18 → R94/C23/R97/R98 to
     Q12's other base.
   - The exponential cutoff pair Q10/Q11 with Q9.
   - Read the remaining bias values from p5.
   - **Validate against [SN] p7:** Cutoff centre and Res full must ring with
     a 2 ms ± 0.5 ms period that decays within a C2 cycle.
   - **Outputs:** small-signal response against cutoff and resonance (for
     the P0 option A/B host model), low-cutoff resonance loss, and large-
     signal behaviour at the drive levels found in §4.3.
2. **MEG and accent sweep:** C62/R138/VR6 with the IC12 accent switch;
   C13/R46/D24 with VR4's second gang. **Outputs:** cutoff CV against time
   for accent runs at Resonance minimum and maximum.
3. **VCA envelope:** the Q31/Q32 network with C42/R123 and the gate/accent
   diode network into IC15. **Outputs:** attack, decay and release times.
4. **V1/V2 sweeps** in `vco_q8.py` (R45 supply, bottom threshold, reset
   level).
5. **Table generator:** once V1 is settled, sample the Q8 output per octave
   level into the `[11][1024]` square table (P0 D8).
