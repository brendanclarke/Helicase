# Session 092 — Alkaline (TB-303) Pre-Implementation: Circuit Modelling, DSP Equations, and Instrument Plan

```
DATE: 2026-11-07/08
SESSION GOAL: Define the Alkaline instrument (TB-303 emulation) through circuit
  modelling, DSP equation derivation, and a complete implementation plan — before
  any firmware code is written.
COMPLETED: Seven documents delivered (P0 plan, P1–P4 circuit models, P4 end-to-end
  run, P6 DSP equations). All circuit blocks modelled (VCO, VCF, VCA, control
  network) and validated against the [SN] p7 calibration. DSP equations derived,
  two real-time solver options (A_rt and B_rt) built and validated against SPICE.
  Eleven user decisions recorded (D1–D11, D8). Renders produced for the D2
  listening test. No firmware code changed.
VERIFIED ON HARDWARE: No (modelling and plan session; no firmware changes to test)

CHANGES THIS SESSION:
- S092_P0_ALKALINE_INSTRUMENT_PLAN.md: created (instrument plan)
- S092_P1_VCB_MODEL_VCO.md: created (VCO SPICE model)
- S092_P2_VCB_MODEL_VCF.md: created (VCF SPICE model)
- S092_P3_VCB_MODEL_VCA.md: created (VCA SPICE model)
- S092_P4_CONTROL_NET.md: created (control network SPICE model)
- S092_P4_SLIDE_ACCENT_RUN.md: created (end-to-end SPICE run)
- S092_P6_DSP_EQUATIONS.md: created (DSP equations and solvers)
- tools/spice/tb303/vco_q8.py: created (VCO/Q8 SPICE script)
- tools/spice/tb303/vcf.py: created (VCF SPICE script)
- tools/spice/tb303/vca.py: created (VCA SPICE script)
- tools/spice/tb303/control.py: created (control network SPICE script)
- tools/spice/tb303/e2e.py: created (end-to-end run SPICE script)
- tools/alkaline/alk_model.py: created (DSP model: equations, constants, solvers)
- tools/alkaline/fit_spice.py: created (fits MEG/VEG/square-table constants)
- tools/alkaline/validate.py: created (V1–V6 validation suite)
- tools/alkaline/alk_constants.json: created (fitted constants)
- tools/alkaline/validate_results.json: created (validation results)
- tools/plots/303_vco_q8_spice.png: created
- tools/plots/303_vcf_spice.png: created
- tools/plots/303_vca_spice.png: created
- tools/plots/303_control_spice.png: created
- tools/plots/303_e2e_run_acid.png: created
- tools/plots/303_e2e_run_lowres.png: created
- tools/plots/alk_dsp_pattern_acid.png: created
- tools/plots/alk_dsp_pattern_lowres.png: created
- tools/alkaline/alk_d2_*.wav: created (D2 listening renders)
- tools/alkaline/alk_v4_*.wav, alk_v5_*.wav: created (validation renders)

KNOWN ISSUES INTRODUCED: None (no firmware changes)
KNOWN ISSUES RESOLVED: None (modelling session)

NEXT SESSION RECOMMENDED GOAL: S093 — Alkaline firmware implementation, starting
  with the Drum ceiling freeze, the square table generator, and the DiodeLadder
  C port of the chosen solver (B_rt or A_rt, after the user listens to D2).
BLOCKERS: D2 (filter choice A vs B) awaits the user's listening test on the
  renders; D3 (flash for the square table) needs a decision on whether to replace
  Re3 or accept the 16 KiB warning.

CRITICAL REMINDERS FOR NEXT SESSION:
- The CPU ceiling must be frozen (elf_loopdump.py) before any Alkaline code
- No per-sample DSP cost may increase (binding mandate)
- The cost comparison includes per-block work (S093 binding)
- Mark Alkaline_calcVoiceSyncBlock() noinline (LTO inlining risk, S086 F1)
- Re3/Sw3 are output captures, not raw VCO — do not use them for Alkaline
- R110 = 22 kΩ, not the printed 2.2 kΩ
- The VR7 wiper is shared between the filter accent sweep and the VCA accent
  branch — they must be solved together (separate solves put the first accent
  27 % high)
```

---

## 1. Session overview

Session 092 was a pure design and modelling session for the Alkaline instrument
(TB-303 emulation). No firmware code was changed. The session produced:

1. A complete instrument plan (P0) with budget analysis, parameter layout,
   trigger semantics, gate timer design, and a six-phase implementation plan.
2. Four transistor-level SPICE circuit models of the TB-303 analog signal path,
   each built from the Roland TB-303 Service Notes (first edition, 19 Feb 1982)
   and validated.
3. An end-to-end SPICE simulation of an 8-step pattern with accents, slides
   and rests.
4. A DSP model (P6) translating the SPICE circuits into real-time equations
   with two solver options, validated against SPICE to within 0.1 dB per step.

---

## 2. References used

| Tag | Source |
|---|---|
| [SN] | Roland TB-303 Service Notes, first edition, 19 Feb 1982 (all 8 pages) |
| [OL] | S. Olney, *CT Modular Book*, ch. 13 "TB-303" |
| [XB] | `cozycactus/x0xb0x-vcf-bench` (CC0): ngspice-fitted x0xb0x model |
| Stinchcombe | T. Stinchcombe, TB-303 filter analysis |
| Zavalishin | V. Zavalishin, *The Art of VA Filter Design* (diode-ladder ZDF) |

---

## 3. P0 — Instrument plan

### 3.1 CPU budget

| Item | Value |
|---|---|
| Drum worst case (the ceiling) | ≈ 247 instr/sample, 4 VDIV + 10 SRR = 257 charged |
| Alkaline A_rt estimate | ≈ 145 total (120 ladder + 24 osc), 2 VDIV |
| Alkaline B_rt estimate | ≈ 230 total (205 ladder + 24 osc), 3 VDIV |
| Margin | 50–95 (A) or 25 (B) instructions and 1–2 VDIV |

### 3.2 Re3/Sw3 assessment

The existing `Re3`/`Sw3` stock waveforms were found to be unsuitable for
Alkaline's VCO. They are digitised from a capture of a 303's *filtered output*:

- The `sw3` spectrum falls about 30 dB by harmonic 10 (a raw saw falls 20 dB)
- The `sq3` second harmonic is only 4 dB below the fundamental
- Only about 40 harmonics were measured; the rest are a fitted extension
- They are peak-normalised

Using them would put one filter snapshot in front of the emulated filter.

### 3.3 VCO design (from P1 modelling)

- **Saw:** `sawTable` confirmed as a match (core ramp straight to 0.5 %).
- **Square:** a pitch-dependent pulse requiring a new per-octave table. D8
  encodes duty (51 % at C1 → 23 % at C5), rising top, and level (−2.5 to
  +3.2 dB against the saw) per octave level. Runtime cost: 0.

### 3.4 Parameter rows (28 rows, 8 sub-pages)

OSC: `wav` (Saw/Sqr), `coa` (default 36 = C2), `tun` (±700 cents), `sld`.
VCF: `cut`, `res`, `env`, `dec`. Voice: `acc`, `vol`, `pan`. LFO and
velocity rows as Drum.

### 3.5 Trigger semantics

1. Accent: velocity ≥ 101 (D1; default step velocity 100 is not accented).
2. Legato: a note after a slide-flagged note whose gate is still open. No MEG
   or VEG retrigger; pitch glides (22 ms).
3. Slide latch: the `sld` value at this trigger decides what happens at the
   next trigger (because automation order is restore → drain → fire).
4. Gate: 3.5/6 of a step (D11); slide holds for 6 + 3.5 clocks.
5. No oscillator phase reset, no filter clear (A1, A2).

### 3.6 Decisions

| # | Decision | Outcome |
|---|---|---|
| D1 | Accent threshold | Velocity 101+ (un-edited steps at 100 are not accented) |
| D2 | Filter option A vs B | Deferred to S093 (decided B2R: B-R with rungs 0-1 nonlinear) |
| D3 | Flash for square table | 22,528 B of 40,980 B headroom |
| D4 | SRR on Alkaline | Left out (saves 10/sample; the 303 has no SRR) |
| D5 | Reference material | Schematic in hand; raw-VCO captures still wanted |
| D6 | Tuning range | ±700 cents (new writer `IM_SPECIAL_ALK_TUNE`, tag 24/32) |
| D7 | Extras for P6 | E1 (post-VCA overdrive) + E2 (filter drive) + E3 (Devil Fish ranges) + E4 (MIDI note-off) |
| D8 | Square pitch dependence | Per-octave level in the table (option a, 0 runtime cost) |
| D9 | VCA release | One-block ramp (0.73 ms); the circuit cuts in < 1 µs |
| D10 | VEG constants | Roland/P3 constants (not [XB]'s x0xb0x values) |
| D11 | Gate length | 3.5 of 6 clocks ([OL]) |

---

## 4. P1 — VCO circuit model (SPICE)

**Script:** `tools/spice/tb303/vco_q8.py` (ngspice 47, 46 transient runs).

### 4.1 Circuit

- **Saw core:** Q26 (2SC1583 matched pair, expo converter) charges C33
  (0.01 µF); Q24/Q25/Q27 reset C33 to ~11.3 V at 5.5 V; Q28 (2SK30 JFET)
  source follower buffers the ramp.
- **Q8 square shaper:** 2SA733(P) PNP. Base: R35 100 kΩ from Q28 source,
  with R34 10 kΩ + C10 0.01 µF fast-edge path (τ 100 µs). Emitter: R45
  22 kΩ to +12 V, C11 1 µF to ground (τ 22 ms). Collector: R36 10 kΩ to
  ground.
- **VCF input load:** S1 → R62 220 kΩ → C17 1 µF → Q12 left base; R70
  2.2 kΩ to bias node.

### 4.2 Key results

| Note | Saw bow | Square on-time | Square level re. saw at Q12 |
|---|---:|---:|---:|
| C1 (33.9 Hz) | 0.47 % | 51.1 % | −2.5 dB |
| C2 (67.7 Hz) | 0.48 % | 37.7 % | +0.3 dB |
| C3 (135 Hz) | 0.51 % | 28.2 % | +2.7 dB |
| C5 (540 Hz) | 0.66 % | 23.2 % | +3.2 dB |

- The saw is straight to 0.5 % (Early effect of Q26); `sawTable` is a
  close match.
- The square duty falls with pitch because C11 recharges through R45 (τ
  22 ms) between pulses. Q8's gain hardly matters.
- VCF input drive: R62/R70 divides by 101; saw at ±1.0 Vt (mild 8 %
  compression in the Q12 pair).
- VCF input high-pass: 0.72 Hz (inaudible; withdraws the earlier 40 Hz
  hypothesis).

### 4.3 Open questions

| # | Question |
|---|---|
| V1 | ~20-point duty offset against [OL]'s 45 %/71 % |
| V2 | Reset level: 11.3 V (inferred) or 12 V (top compression) |

---

## 5. P2 — VCF circuit model (SPICE)

**Script:** `tools/spice/tb303/vcf.py` (ngspice 47, about 6 minutes).

### 5.1 R110 = 22 kΩ, not 2.2 kΩ

The most significant finding of the session. The schematic print reads 2.2 kΩ;
the x0xb0x KiCad values have 22 kΩ. Every other part matches. With 2.2 kΩ,
loop gain is 1.93 (threshold 19.1) — it cannot resonate. With 22 kΩ, loop gain
is 18.7, 98 % of the threshold. The 303 rings but stays stable.

### 5.2 Ladder structure

- Four stages: C18 0.018 µF (bottom), C19/C24/C26 0.033 µF.
- Transistor pairs Q13/Q15, Q14/Q17, Q16/Q23 (diode-connected).
- Resonance feedback through VR4 50 kΩ(B) → C15 1 µF → R67 2.2 kΩ → Q18
  follower → C23 1 µF → R97 10 kΩ → Q12 right base.
- Output stage: C27/C25 0.1 µF (crossed) → Q21 (2SC1583) differential pair →
  Q20 tail → Q19 follower → C14 1 µF → TP6.

### 5.3 Key results

| Q11 current | Ladder −3 dB | Resonance freq (Res full) | Resonance boost |
|---:|---:|---:|---:|
| 1 µA | 15 Hz | 86 Hz | +2.1 dB |
| 10 µA | 52 Hz | 663 Hz | +16.2 dB |
| 30 µA | 166 Hz | 1,948 Hz | +26.3 dB |
| 100 µA | 591 Hz | 6,421 Hz | +29.3 dB |
| 300 µA | 1,874 Hz | 19,601 Hz | +27.5 dB |

- Resonance frequency ≈ 12× the ladder's −3 dB point.
- Resonance weakens at low cutoff (+2 dB at 86 Hz vs +29 dB at 6.4 kHz).
- Passband drops with resonance (negative feedback, ~25 dB at Res full).
- High-pass inside the loop (C27/C25 into Q21, 60–120 Hz).
- Lower resonance at 7–9 Hz (Stinchcombe's "very real" peak), stable.

### 5.4 [SN] p7 calibration (Cutoff centre, Res full, saw)

Ring period **2.03 ms** (target 2.0 ± 0.5 ms), decay per cycle **0.63**.

---

## 6. P3 — VCA circuit model (SPICE)

**Script:** `tools/spice/tb303/vca.py` (ngspice 47).

### 6.1 VEG (volume envelope generator)

- **Circuit:** trigger (D36 → R134 22 kΩ → Q32 base, C41 0.1 µF) empties C42
  (1 µF) through R132 100 Ω. C42 recharges through R123 (1.5 MΩ) from +12 V.
  Q31 converts C42 voltage to the OTA's control current.
- **Gate level:** 12 V (from [SN] p1 Gate Out spec; D34 is on the gate line).
- **Decay:** −6 dB at 1.01 s, −20 dB at 2.68 s, −40 dB at 3.85 s, silent at
  4.51 s. Close fit: g(t) = 1.052·e^(−t/1.5 s) − 0.052.
- **Attack:** 2.5–3 ms (trigger width 0.65–0.75 ms reproduces this).
- **Release:** under 1 µs (no timing capacitor in the gate path).
- **Accent:** +5.9 dB (VR7 half), +11.4 dB (VR7 full), gone after ~200 ms.
- **Roland vs [XB]:** [XB]'s τ 2.29 s and 62.65 µA peak cannot come from the
  Roland values (τ = R123·C42 = 1.5 s). These are x0xb0x constants, not
  Roland ones. **Decided: Roland constants (D10).**

### 6.2 VCA signal path

| Path | High-pass corner | Level |
|---|---:|---:|
| A alone (TP6 → C21 → R121 220 kΩ) | 66 Hz | −40.3 dB |
| A + B, Res 0 | 66 Hz | −40.3 dB |
| A + B, Res full | 127 Hz | −30.2 dB |

The Resonance pot changes the VCA input: +10 dB and the high-pass moves from
66 Hz to 127 Hz. The OTA is linear at the 303's level (h3 at −72 dB).

---

## 7. P4 — Control network and end-to-end run (SPICE)

**Scripts:** `tools/spice/tb303/control.py`, `tools/spice/tb303/e2e.py`.

### 7.1 Expo converter

Q9 holds the reference node (N561, 3.263 V). Cutoff (VR3, A taper from +12 V
through R47 10 kΩ to N561) and Env Mod (VR5, A taper from R61 10 kΩ → N561 to
the MEG output N53) drive Q10/Q11 through R63 220 kΩ and R73 100 kΩ + TM3
(trimmer). TM3 calibrates to 153 kΩ ([XB]: 159 kΩ).

- Cutoff range: ~3 octaves at Env Mod minimum (217 Hz → 1.74 kHz).
- Env Mod lowers the resting cutoff by up to 1.4 octaves and scales the
  sweep from +0.8 to +5.1 octaves.

### 7.2 MEG

- Struck to the same peak on every note (no stacking); decay to 10 %: 173 ms
  (Decay min) to 2.54 s (max).
- Accent bypasses the Decay pot (IC12 4066) while ACCENT is high.

### 7.3 Accent sweep

- VR7 wiper → D24 → R46 47 kΩ → VR4b (Resonance second gang) → C13 1 µF →
  R72 100 kΩ to the expo node.
- At low Resonance: a sharp cutoff spike per accent.
- At high Resonance: a smooth swell peaking ~30 ms after the hit, climbing
  over consecutive accents (+2.3 dB on the second, levelling after three).

### 7.4 End-to-end run (8-step pattern at 120 BPM)

Pattern: C2, C2 accent, D#2 slide → G2, C3 accent slide → A#2 accent, rest,
C2 accent. Run at Resonance 0.85 ("acid") and 0.2 ("lowres").

Key findings:
1. Slide is legato in every lane (no trigger, no MEG strike).
2. An accented slid-to note gets the VCA accent but no new filter peak.
3. The VCA attack appears only after silence; on running 16ths notes open
   at once.
4. The VEG is a state, not a restart.
5. Accented notes stay audible after gate-off (the accent branch is ungated).
6. Slide τ = 22 ms; 50 % in 15.2 ms, 90 % in 50.7 ms.

---

## 8. P6 — DSP equations

**Files:** `tools/alkaline/alk_model.py`, `validate.py`, `fit_spice.py`.

### 8.1 Audio path

- **Diode ladder:** four stages with ω_k = I/(4 Vt C_k). Bottom stage
  C18 = 0.018 µF, upper three 0.033 µF. Six nonlinearities: four rung tanhs,
  the Q12 input pair, and Q21 inside the loop.
- **Q21 output stage:** voltage gain 103.6 (R110 = 22 kΩ), output coupling
  τ_out = 2.33 ms (68 Hz).
- **FeedbackNet:** eight-capacitor linear network (C14, VR4, C15, Q18 bias,
  C23, R97/R98, C17/R62, C21/R121, C22/R122). Two inputs (tp_pre, s), three
  outputs (u, v_vca, TP6). All poles real, below 160 Hz. Above ~200 Hz the
  network is its D matrix (instantaneous gain). A single feedback high-pass
  could not reproduce the low-frequency loop correctly.

### 8.2 Solver options

| Option | Model | Instr/sample | VDIV |
|---|---|---:|---:|
| A_rt | Linear rungs + nonlinear input pair and Q21 | ~120 | 2 |
| B_rt | All six nonlinearities (secant linearisation) | ~205 | 3 |

Both close the loop through the whole network in its real-time form (SplitNet):
two fast eigenmodes and two one-pole asymptotes per sample, the slow remainder
per block. Prewarp: one `fastTan` per block on the ladder current.

### 8.3 Control path (all control rate)

- **Expo converter:** I = I(R73) × exp(ΔV/Vt), three fixed-point passes.
- **MEG:** C62 state with diode-stack floor (D28/D27/D26); the stack floor
  gives the long tail of the accented decay.
- **Accent network:** 5 × 5 nodal solve of both VR7 branches (C13 sweep +
  C36 VCA accent) together, because R119 loads the wiper.
- **VEG:** C42 state with a fitted 64-point current table; attack delay +
  exponential discharge.
- **Prewarp:** TPT correction P = tan(θ)/θ; without it, −391 cents at 13 kHz.

### 8.4 Validation summary

| Check | Result |
|---|---|
| V1 (small signal vs SPICE AC) | Off-peak RMS 0.14–0.60 dB over 24 curves |
| V2 ([SN] p7 calibration ring) | Period 1.995 ms, decay/cycle 0.641 |
| V3 (control path) | Expo within 33 cents; MEG decay within 1.4 %; VEG within 3 % |
| V4 (audio on SPICE signals) | Output LSD 0.14–0.29 dB |
| V5 (whole DSP on the pattern) | Each step within 0.45 dB |
| V6 (drive ×1–×8) | B_rt LSD 0.07–0.22 dB; A_rt 0.16–0.78 dB |

### 8.5 SplitNet (the real-time network)

The eight-state network around the ladder costs ~100 multiply-adds per sample
as a dense update. The real-time form splits it by speed:

| Part | Rate | What |
|---|---|---|
| D w | per sample | Instantaneous gains (resonance, input ÷101) |
| y_i (i=1,2) | per sample | Two fast eigenmodes (72 Hz and 141–157 Hz) |
| q_u, q_v | per sample | Slow-group asymptote, one-pole each at 5 Hz |
| r (slow remainder) | per block | 8 states in the slow subspace, via Φ = e^{A_a T} |

This reduces the per-sample cost from ~100 to ~25 operations. Linear
interpolation from a Res-table of 9–17 points; each point ~114 floats
(4.1–7.8 KB flash).

---

## 9. Tooling produced

| Tool | Location | Purpose |
|---|---|---|
| `vco_q8.py` | `tools/spice/tb303/` | VCO and Q8 SPICE model, 46 runs |
| `vcf.py` | `tools/spice/tb303/` | VCF ladder SPICE model, AC and transient |
| `vca.py` | `tools/spice/tb303/` | VCA envelope and accent SPICE model |
| `control.py` | `tools/spice/tb303/` | Expo/MEG/accent sweep SPICE model |
| `e2e.py` | `tools/spice/tb303/` | End-to-end VCO→VCF→VCA pattern run |
| `alk_model.py` | `tools/alkaline/` | DSP equations, constants, solvers (Python) |
| `fit_spice.py` | `tools/alkaline/` | Fits MEG/VEG/square-table constants from SPICE |
| `validate.py` | `tools/alkaline/` | V1–V6 validation suite |
| `alk_constants.json` | `tools/alkaline/` | Fitted constants for the DSP model |

---

## 10. Failed approaches and pitfalls

1. **A single feedback high-pass** could not reproduce the low-frequency loop
   behaviour. The shared parts (NB/C16 under both Q12 bases, Q18 bias, VR4
   wiper loaded by C15 and C22) do not factor into separate stages.
2. **Balanced truncation of the network** saved little per sample (7 dense
   states vs 8) and the balanced basis was not smooth in Res, preventing table
   interpolation.
3. **Separate accent solves** (C13 sweep and C36 VCA branch solved
   independently) put the first accent's ladder current 27 % high at Res 0.2.
   The branches share VR7's wiper, and R119 loads it while C36 charges.
4. **The R110 print value (2.2 kΩ)** gives a loop gain of 1.93, far below the
   threshold of 19.1. The 303 cannot resonate with this value.
5. **[XB]'s VEG constants (τ 2.29 s, peak 62.65 µA)** cannot come from the
   Roland component values (R123·C42 = 1.5 s). They are x0xb0x constants.

---

## 11. Component values confirmed by the user

All [SN] p5 values were confirmed by the user against the schematic except
where noted. The scan is smudged at R62 (taken as 220 kΩ from x0xb0x
documentation). R110 = 22 kΩ is the x0xb0x value, not the print.

Notable confirmations:
- Output stage (Q21 pair, R113/R114, R110, R111, R108, R109, R112, R96, C14)
- Resonance loop (VR4, C15, R67, Q18 bias, R94, C23, R97, R98)
- R62 = 220 kΩ (from x0xb0x; the scan is smudged)
- R119 source = VR7 wiper (user's trace)
- VCA inputs A = TP6, B = VR4 wiper (user's trace)

---

## 12. Documents produced (to be deleted after logging)

| Document | Content |
|---|---|
| `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` | Instrument plan, budget, parameters, phases, tests, decisions |
| `S092_P1_VCB_MODEL_VCO.md` | VCO SPICE model (saw core, Q8, VCF input) |
| `S092_P2_VCB_MODEL_VCF.md` | VCF SPICE model (ladder, loop, R110 finding) |
| `S092_P3_VCB_MODEL_VCA.md` | VCA SPICE model (VEG, accent, OTA, signal path) |
| `S092_P4_CONTROL_NET.md` | Control network (expo, MEG, accent sweep) |
| `S092_P4_SLIDE_ACCENT_RUN.md` | End-to-end SPICE run (8-step pattern) |
| `S092_P6_DSP_EQUATIONS.md` | DSP equations, solvers A/B, validation V1–V6 |

All details from these documents are captured in this log and in the permanent
specification documents:
- `ALKALINE_MODELLING_REFERENCE.md` (circuit models, SPICE, DSP equations)
- `ALKALINE_MODULE_REFERENCE.md` (firmware module design)
- `INSTRUMENTS_DSP_REFERENCE.md` §5.5 (the Alkaline section)
