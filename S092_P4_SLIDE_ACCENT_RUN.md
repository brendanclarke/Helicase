# S092 P4 — TB-303 End-to-End Slide and Accent Run (SPICE)

- **Session:** 092, phase 4 (end-to-end run). Plan:
  `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` (A7–A11, §5.3–§5.4, §7.1); the
  circuit models: `S092_P1_VCB_MODEL_VCO.md`, `S092_P2_VCB_MODEL_VCF.md`,
  `S092_P3_VCB_MODEL_VCA.md`, `S092_P4_CONTROL_NET.md`.
- **What this is:** one ngspice netlist joining all four models, driven by
  an 8-step pattern at 120 BPM with normal notes, accents, a plain slide, a
  slide into an accented note, a rest and an accent after the rest. Run
  twice: Resonance 0.85 ("acid") and 0.2 ("lowres").
- **Status (2026-11-07):** done. Pitch, slide, gate, accent and rest
  behaviour all match the plan's rules and the individual models. Three
  behaviours the plan did not yet state are in §4 (items 4–6, marked
  "new"); the plan now carries them (P0 A7).
- **Decisions used:** gate 3.5 of 6 clocks (D11); VCA trigger 0.70 ms,
  which gives the decided 2.5–3 ms attack (P3 §3.2); Roland VEG constants
  (D10); release ramp D9 applies to the DSP, not to this circuit run.
- **Used by:** `S092_P6_DSP_EQUATIONS.md`.
  - V4 drives the DSP audio path with this run's saw and currents.
  - V5 plays the same pattern through the whole DSP model and compares it
    with the per-step tables in §3.
  - The traces are saved for that (§1).

---

## 1. Files and how to run

| Item | Value |
|---|---|
| Script | `tools/spice/tb303/e2e.py` (ngspice 47; numpy, matplotlib). `python3 tools/spice/tb303/e2e.py [acid] [lowres]`, about 20 s per run |
| Netlists | `tools/spice/tb303/out/e2e_acid.cir`, `e2e_lowres.cir` |
| Results | `tools/spice/tb303/e2e_results.json` (per-step measurements, knob settings, gate intervals) |
| Traces | `<tempdir>/lxr_spice_tb303/e2e_{acid,lowres}_traces.npz`: float32 `cv`, `saw`, `meg` (N53), `c13`, `itail`, `iabc`, `tp6`, `out`, `gate`, `acc`, `slide`, `trig`, with `t` in seconds from step 1. Used by S092_P6 V4–V6. Kept outside the repo; rerun `e2e.py` to regenerate |
| Plots | `tools/plots/303_e2e_run_acid.png`, `303_e2e_run_lowres.png` (pitch CV, sequencer lanes, MEG and C13, cutoff, VCA current, output) |
| Audio | `tools/spice/tb303/e2e_acid.wav`, `e2e_lowres.wav`: the output (after C38, Volume full), 44.1 kHz mono, 1.3 s. **Each file is normalised to −2 dBFS peak on its own**, so compare levels within a file, not between files |
| Simulation | Output step 1/88,200 s (linearised), internal step ≤ 4 µs, Gear integration. Starts from a DC operating point with only the VCO capacitor held at its reset level, then a 1 s settling pre-roll before step 1 |

### 1.1 Reading the plots

Six panels on one time axis (ms from step 1; grey lines mark the steps):

1. **Pitch CV** converted to Hz (65.406 Hz × 2^(CV − 2 V)). Labels mark
   each note: A = accent, S = slide.
2. **Sequencer lanes**, from the bottom: gate, trigger, accent, slide
   switch.
3. **MEG** (N53, the filter envelope after the Darlington) and **C13** (the
   accent sweep).
4. **Cutoff:** Q11's current through the P2 law, log scale.
5. **VCA control current** (IC15 pin 1, VEG plus accent branch).
6. **Output** after C38.

---

## 2. The netlist

Each block keeps its own node prefix (`v_`, `f_`, `c_`, `a_`) so no node
name is shared by accident (the P3 lesson).

| Block | Source | What changed for this run |
|---|---|---|
| Sequencer | Behavioural PWL sources: DAC CV (1 V/oct, C2 = 2.0 V, [SN] p5/p7), GATE (0/12 V), TRIGGER (0/5.333 V, 0.70 ms), MEG strike (11.85 V, 1.3 ms), ACCENT (0/5 V), SLIDE (0/5 V) | — |
| Pitch CV and slide | P0 A9: DAC output resistance 100 kΩ (R-2R), IC11b follower, C35 0.22 µF switched by two 4066 sections: on the follower output when slide is off, on the DAC node when it is on | The slide switch engages 0.1 ms before the DAC steps, so C35 meets the DAC node at the old CV (§5, E3) |
| VCO | P1: C33, reset to 11.3 V at 5.5 V, Q28 follower, Q8 shaper | **Behavioural expo:** the ramp current is I_C2 × 2^(CV − 2.0) instead of the Q26 transistor pair. I_C2 was trimmed once (×1.073) so C2 measures 65.4 Hz in this netlist |
| VCF | P2, R110 = 22 kΩ, saw selected (S1 → R62) | The ladder tail is now Q11's real collector current through R69 (no ideal source) |
| Control network | P4 (TM3 152.7 kΩ) | VR4b shares the Resonance setting with VR4a in the VCF |
| VCA | P3: VEG (Q31/Q32, C42), accent branch from VR7's wiper, inputs A (TP6) and B (VR4a's wiper), OTA as I_abc · tanh(v / 2Vt), output load, C38 into Volume 50 kΩ | Inputs are the real filter nodes, so pot loading is included |

**Knob settings** (both runs): saw, Cutoff 0.35, Env Mod 0.6, Decay 0.45,
Accent 0.8; Resonance 0.85 ("acid") or 0.2 ("lowres").

### 2.1 Pattern (120 BPM, 16ths, 125 ms per step)

| Step | Note | Accent | Slide | What it tests |
|---:|---|:--:|:--:|---|
| 1 | C2 | | | Plain note after silence |
| 2 | C2 | A | | Accent |
| 3 | D#2 | | S | Slide from a normal note |
| 4 | G2 | | | Slid into (legato): no trigger, no strike |
| 5 | C3 | A | S | Accent with slide |
| 6 | A#2 | A | | Accented slid-to note (legato) |
| 7 | — | | | Rest |
| 8 | C2 | A | | Accent after a rest |

**Gate intervals produced** (ms from step 1): 0–72.9, 125–197.9,
**250–447.9** (step 3's gate held through step 4's gate), **500–697.9**
(steps 5→6), 875–947.9. The normal gate is 3.5/6 of a step (72.9 ms). A
slide holds the gate for the whole step plus the next step's gate
(6 + 3.5 clocks).

---

## 3. Results, step by step

Columns:
- **Pitch:** measured over the last 50 ms of the step (error against
  equal temperament, A = 440 Hz).
- **Cutoff:** the resonance frequency from Q11's current through the P2
  law at Res full (at Res 0.2 it is the same current, but resonance there
  is weak; §5 E2). "Start → end" is the value just before the step and at
  its end.
- **VCA:** IC15 pin-1 current.
- **C13:** the accent sweep capacitor at step end (rest level 3.25 V).
- **Out:** output RMS for the step, relative to step 1. Step 1's RMS is
  0.071 V in "acid" and 0.103 V in "lowres" (after C38, Volume full into
  50 kΩ).

### 3.1 "acid" (Resonance 0.85)

| Step | Note | Flags | Pitch, Hz (error) | Cutoff start → end, Hz | Cutoff peak | VCA peak | C13 end, V | Out, dB |
|---:|---|---|---|---|---|---|---:|---:|
| 1 | C2 | | 65.3 (−3.8 c) | 293 → 646 | 1,079 Hz @ 1.3 ms | 46 µA (rises to its plateau by ~24 ms) | 3.29 | 0.0 |
| 2 | C2 | A | 65.3 (−3.8 c) | 646 → 445 | 1,363 Hz @ 1.5 ms | **124 µA @ 5.1 ms** | 3.51 | **+6.8** |
| 3 | D#2 | S | 78.0 (+5.0 c) | 445 → 686 | 1,300 Hz @ 1.3 ms | 50 µA @ 0 ms | 3.37 | +2.8 |
| 4 | G2 | legato | 97.8 (−3.8 c) | 686 → 483 | none (decay continues) | 44 µA, decay continues | 3.31 | −0.1 |
| 5 | C3 | A S | 130.5 (−4.5 c) | 483 → 446 | 1,385 Hz @ 1.5 ms | **124 µA @ 5.4 ms** | 3.51 | **+9.5** |
| 6 | A#2 | A, legato | 116.6 (+0.5 c) | 446 → 331 | none (decay continues) | 52 µA at the start, following the accented MEG down | 3.34 | +0.8 |
| 7 | rest | | — | → 312 | — | 0 | 3.28 | −48 |
| 8 | C2 | A | 65.4 (−1.1 c) | 312 → 444 | 1,374 Hz @ 1.9 ms | **123 µA @ 5.5 ms** | 3.51 | **+6.9** |

### 3.2 "lowres" (Resonance 0.2)

| Step | Note | Flags | Pitch, Hz (error) | Cutoff start → end, Hz | Cutoff peak | VCA peak | C13 end, V | Out, dB |
|---:|---|---|---|---|---|---|---:|---:|
| 1 | C2 | | 65.3 (−3.8 c) | 293 → 645 | 1,092 Hz @ 1.3 ms | 46 µA | 3.28 | 0.0 |
| 2 | C2 | A | 65.4 (−0.6 c) | 645 → 435 | **2,630 Hz @ 1.3 ms** | 124 µA @ 5.1 ms | 3.54 | +7.5 |
| 3 | D#2 | S | 78.0 (+5.0 c) | 435 → 690 | 1,285 Hz @ 1.3 ms | 50 µA @ 0 ms | 3.40 | +3.0 |
| 4 | G2 | legato | 97.6 (−6.2 c) | 690 → 486 | none | 44 µA | 3.33 | −1.0 |
| 5 | C3 | A S | 130.5 (−4.5 c) | 486 → 439 | **2,679 Hz @ 1.3 ms** | 123 µA @ 5.4 ms | 3.56 | +9.2 |
| 6 | A#2 | A, legato | 116.5 (−1.3 c) | 439 → 334 | none | 52 µA | 3.38 | −2.2 |
| 7 | rest | | — | → 315 | — | 0 | 3.31 | −57 |
| 8 | C2 | A | 65.3 (−4.1 c) | 315 → 437 | **2,647 Hz @ 1.3 ms** | 123 µA @ 5.5 ms | 3.55 | +7.7 |

### 3.3 Slide

| Glide | Interval | 50 % | 90 % | Within 10 cents |
|---|---:|---:|---:|---:|
| D#2 → G2 (step 3 → 4) | +4 semitones | 15.2 ms | 50.7 ms | 81.4 ms |
| C3 → A#2 (step 5 → 6) | −2 semitones | 15.2 ms | 50.7 ms | 65.9 ms |

- An exponential approach with **τ = 22 ms**: 15.2 ms = τ·ln 2 and
  50.7 ms = τ·ln 10. This is the P0 A9 circuit value (100 kΩ × 0.22 µF).
- The CV is linear in volts, so the glide is exponential in log-pitch and
  takes the same 50/90 % times for any interval. Only the
  "within 10 cents" time grows with the interval.
- At 120 BPM both glides settle well inside their step.

### 3.4 Pitch

All notes land within **±6 cents** of equal temperament, over two octaves
(C2–C3). The VCO current was trimmed once, at C2. The ± spread comes from
counting 3–6 cycles in a 50 ms window.

---

## 4. What the run shows

1. **Slide is legato in every lane.** The slid-to notes (steps 4 and 6)
   get no trigger and no MEG strike. The filter envelope and the VCA keep
   decaying from the previous note (step 4's MEG is still at 6.1 V from
   step 3; the VCA continues at 44 µA). The gate stays high across the pair
   (P0 A9, A10 confirmed).
2. **An accented slid-to note gets the VCA accent but no new filter
   peak** (step 6). The cutoff keeps falling (446 → 331 Hz). The VCA's
   accent current follows the still-decaying, accent-bypassed MEG, and the
   Decay bypass stays on through the step. This confirms P0 A11.
3. **Accent depends on Resonance.**

   | Resonance | Accent cutoff peak | Normal note peak | VCA, accent / normal | Output RMS, accented steps |
   |---|---:|---:|---:|---:|
   | 0.85 | 1.36–1.39 kHz, smoothed | 1.08–1.30 kHz | 124 / 46 µA (+8.6 dB) | +6.8 to +9.5 dB |
   | 0.2 | **2.63–2.68 kHz, a sharp spike** | 1.09–1.29 kHz | 124 / 46 µA | +7.5 to +9.2 dB |

   - At high resonance the accent's cutoff boost goes into C13 (the swell
     of P4 §3.5). At low resonance it arrives as a spike.
   - The VCA accent is the same in both runs: it depends on the Accent
     knob, not on Resonance.
   - The VCA current peaks about 5 ms after an accented hit (C36 smoothing
     plus the strike).
4. **Accented notes stay audible after the gate falls (new).**
   - At step 2's gate-off (72.9 ms into the step) the VCA current drops
     from about 72 µA to about 27 µA, not to zero. It then falls with the
     MEG to about 8 µA, and stops only when ACCENT drops at the step's end.
   - The cause: the accent branch (VR7 → R119 → C36 → R120 → D27 into pin 1)
     does not pass through the gate.
   - So an accented note in a 303 has a short tail past its gate. For
     normal notes the VCA closes at gate-off.
5. **The VCA attack only appears after silence (new).**
   - Step 1, after a long silence, rises over a few ms (the P3 attack).
   - Steps 3 and 4, 125 ms after the previous note, open at once at
     gate-on (50 µA within 12 µs). C42 recharges with τ 1.5 s, so after
     125 ms it is still near the bottom; Q31 conducts as soon as its
     emitter supply (the gate) appears, and the trigger has nothing to
     discharge.
   - In a 303 sequence at normal tempos, most notes start at once. The
     2.5–3 ms attack (decided with P0 D10) applies to notes after a gap of
     roughly a second or more.
6. **The VEG is a state, not an envelope that restarts.** Retriggering
   restarts the decay from wherever C42 is. Step 3 starts at 50 µA, which
   is the decay curve's value after only 125 ms of recovery.
7. **The saw comes out as spikes.** The VCA input's high-pass (66 Hz at
   Res 0 to 127 Hz at Res full, P3 §3.5) acts on notes around 65–130 Hz,
   leaving mainly the saw's reset edges. Each spike rings at the
   resonance frequency (visible in the acid plot). This is consistent with
   the "spike" shapes in the `Sw3`/`Re3` captures (P0 §2).
8. **Rest:** the output is −48 dB (acid) / −57 dB (lowres) relative to
   step 1. The filter keeps running; the VCA is closed.

---

## 5. Assumptions and limits

| # | Item | Effect |
|---|---|---|
| E1 | The VCO uses a behavioural exponential (ramp current ∝ 2^CV), trimmed once at C2 | Pitch tracking is ideal by construction. The core shape (P1) is unchanged |
| E2 | The cutoff axis uses the P2 Res-full law for every run | At Res 0.2 the same ladder current means a weaker, slightly lower resonance (P2 §3.3). The currents are exact either way |
| E3 | The slide switch engages 0.1 ms before the DAC steps; ACCENT is held for the whole accented step; the trigger is 0.70 ms; the MEG strike is 11.85 V for 1.3 ms | CPU timing is not in [SN]. The first run, with the switch engaging 1 µs after the DAC step, showed a 1 µs CV spike |
| E4 | Generic devices; ideal follower (IC11b) and output buffer (IC15 pins 7–8); Volume at full into 50 kΩ | As in P1–P4 |
| E5 | The WAV files are normalised per file | Compare levels within a file only |
| E6 | This run has the R119/C36 accent branch on VR7's wiper; the P4 control netlist (`S092_P4_CONTROL_NET.md` §3.5) does not | While C36 charges, R119 loads the wiper and lowers the accent sweep's start. The DSP must solve both branches together (S092_P6 §4.3) |

---

## 6. Consequences for Alkaline (P0)

| Item | Design rule from this run |
|---|---|
| Slide (A9) | One-pole lag on log-pitch, τ = 22 ms: α = 1 − e^(−0.7256/22) ≈ 0.0324 per block. The legato note gets no MEG strike and no VEG retrigger |
| Accent on a legato note (A11) | VCA accent current follows the running MEG; the Decay bypass continues; no new cutoff peak |
| VCA (A7) | Model the VEG as the C42 state (recharge τ 1.5 s, discharged by the trigger) and Q31's current law, not as attack/decay segments. The 2.5–3 ms attack then appears only after silence, as in the circuit |
| VCA accent path | Not gated: it adds current while ACCENT is high, following the accented MEG, even after gate-off. It ends when ACCENT drops at the step's end |
| Gate (D11) | 3.5/6 of a step; slide holds it for 6 + 3.5 clocks (confirmed in §2.1) |
| Test references | The per-step tables in §3 are the reference for host laws AL4 (accent, VEG), AL5 (slide latch, glide τ) and AL7 (gate lengths). `e2e_results.json` holds them in machine-readable form |
