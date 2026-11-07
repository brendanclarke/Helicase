# S092 P4 — TB-303 Circuit Model: Control Network (SPICE)

- **Session:** 092, phase 4 of the circuit model. Plan:
  `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` (A3, A5, A6); previous phases:
  `S092_P1_VCB_MODEL_VCO.md`, `S092_P2_VCB_MODEL_VCF.md`,
  `S092_P3_VCB_MODEL_VCA.md`.
- **Status (2026-11-07):** modelled at transistor level, from the knobs and
  the filter envelope (MEG) to the ladder current:
  - the Cutoff and Env Mod knobs;
  - TM3 and R73;
  - the exponential converter (Q9/Q10/Q11);
  - the MEG with its accent decay switch;
  - the accent sweep (VR7, D24, R46, C13 and the Resonance pot's second
    gang).

  It reproduces the [SN] decay range and the [SN] p7 calibration, and
  agrees with [XB]'s operating point, without a calibration factor.
- **Sources:** Roland TB-303 Service Notes p5 (values), p7 (TM3
  calibration), p8 (Env Mod text) ([SN]); the user's trace of the
  accent/Resonance pot wiring; **[XB]** `cozycactus/x0xb0x-vcf-bench`
  (CC0, an ngspice-fitted x0xb0x model). [XB] supplied the return of R47/R61
  (§2.1) and the strike figures; it is used for values and topology only,
  and no code was used.
- **Tags as before:** confirmed, inferred, modelled.
- **Decisions recorded alongside this report (user, 2026-11-07):**
  - the volume envelope uses the Roland constants (P0 D10);
  - the attack is 2.5–3 ms;
  - the normal gate is 3.5 of 6 clocks (D11);
  - the release at gate-off is a one-block ramp of 0.73 ms (D9).

---

## 1. Tooling

| Item | Value |
|---|---|
| Script | `tools/spice/tb303/control.py` (ngspice 47; numpy, matplotlib) |
| Outputs | `tools/spice/tb303/out/ctl_*.cir`, `tools/spice/tb303/control_results.json`, `tools/plots/303_control_spice.png` |
| Run | `python3 tools/spice/tb303/control.py`, about 3 minutes. Raw data goes to a temp folder and is deleted once read |
| Frequency axis | The ladder current is converted to the Res-full resonance frequency with the P2 law (`vcf_results.json`, log-log interpolation, extrapolated beyond 1–300 µA). Frequencies above 20 kHz in §3.5 are extrapolated and only show the current's size |

---

## 2. Circuit as modelled

| Block | As read ([SN] p5; [XB] where stated) | Model |
|---|---|---|
| Cutoff knob | VR3 50 kΩ (A taper, parts list "CUT OFF FREQ … 50kA") from **+12 V** to R47 10 kΩ → **N561**; wiper → TM3 (470 kΩ trimmer) → R73 100 kΩ → Q10 emitter / Q11 base | A taper: 20 % of the track at half rotation ((16^r − 1)/15; [XB] uses the same 0.2) |
| Env Mod knob | VR5 50 kΩ(A) from R61 10 kΩ → **N561** to the MEG output N53; wiper → R63 220 kΩ → summing node N134 (VR5 pin 3 = MEG: [SN] p8) | as read |
| Expo | Q10 (PNP) base = N134, emitter = Q11 base, collector → N561; Q11 (NPN) collector → R69 2.2 kΩ → Q12 emitters (ladder tail), emitter → N561; R71 2.2 kΩ from N134 to N561; Q9 (PNP, 2SA733P) emitter = N561, base on R65 10 kΩ (from 5.333 V) / R64 10 kΩ, collector to ground | as read. Law: I = I(R73) × exp((V134 − V561)/Vt), the law [XB] states |
| MEG | C62 1 µF struck through D37 + R152 100 Ω; discharged through R138 68 kΩ → VR6 (Decay, 1 MΩ(A) rheostat) → diode stack D28 → D27 → D26 to ground with R117 22 kΩ from 5.333 V into D28/D27; IC12 (4066, pins 3–4) bypasses VR6 while ACCENT is high; Darlington Q41/Q40 → N53, Q39 2SK30(Y) as its current-sink load | **Strike source behavioural:** 11.85 V for 1.3 ms ([XB]: strike length 1.2–1.4 ms, source 11.85 V). The Q36/Q38 strike circuit is not modelled |
| Accent sweep | N53 → IC12 (pins 1–2, ACCENT) → VR7 Accent 50 kΩ(B) top; wiper → D24 → R46 47 kΩ → VR4b (Resonance, second gang, 50 kΩ B) pin 1; pin 3 = C13 1 µF to ground; wiper → R72 100 kΩ → N134. The same VR7 wiper feeds the VCA through R119 (P3) | as read. VR4b's wiper follows the Resonance knob |
| ACCENT line | IC13 latch, set per step | **Assumed high for the whole accented step** (125 ms at 120 BPM) |
| Ladder tail | Q11's collector through R69 into the Q12 emitters | R69 to a 4.7 V source; the source's current is the ladder current |
| Devices | — | Generic NPN/PNP (Q11 current gain 240, Q9 300), 1N4148 diodes (IS 2.52 nA, N 1.752, as [XB]), 4066 R_on 250 Ω |

### 2.1 Correction during modelling: where R47 and R61 return

- **First reading (wrong):** R47 and R61 joined each other, making one
  chain from +12 V through both knobs to the MEG.
- **What that produced:** the MEG lifted the Cutoff pot itself. Env Mod at
  minimum still swept 3.4 octaves, and TM3 could not reach the p7
  calibration even at its 470 kΩ end stop.
- **Fix:** [XB] (built from the x0xb0x KiCad netlist) returns both R47 and
  R61 to **N561**, Q9's reference node. With that, TM3 calibrates
  to 153 kΩ ([XB]'s trim is 159 kΩ) and every rest voltage agrees (§3.1).

---

## 3. Results (modelled)

### 3.1 Calibration and rest point

TM3 is the [SN] p7 "FREQ" trimmer. With Cutoff at centre, Env Mod
minimum and the MEG at rest, it is set for the 2.0 ms ring (Q11 =
7.375 µA, P2 §3.2):

| Quantity | This model | [XB] |
|---|---:|---:|
| TM3 | **153 kΩ** | 159 kΩ |
| Cutoff wiper (VR3), centre | 6.06 V | ≈ 6.05 V (from its R73 current) |
| N561 (Q9 node) | 3.263 V | 3.26 V |
| N53 (MEG at rest) | 0.021 V | 0.017 V |
| Ladder current, Cutoff centre / Env Mod 0 | 7.375 µA (calibrated) | — |

### 3.2 Knobs at rest: resonance frequency (Res full), MEG at rest

| Env Mod ↓ / Cutoff → | 0 | 0.2 | 0.5 | 0.8 | 1.0 |
|---:|---:|---:|---:|---:|---:|
| 0.00 | 217 Hz | 289 | **500** | 992 | 1,738 |
| 0.25 | 206 | 273 | 472 | 932 | 1,633 |
| 0.50 | 186 | 243 | 420 | 824 | 1,443 |
| 0.75 | 151 | 195 | 333 | 644 | 1,125 |
| 1.00 | 93 | 120 | 196 | 374 | 633 |

(Full 11-point rows: `control_results.json`, `dc_map`.)

- **Cutoff range:** about **3 octaves** at Env Mod minimum (217 Hz →
  1.74 kHz). With the A taper it is nearly exponential in rotation.
- **Env Mod lowers the resting cutoff** by up to 1.4 octaves (500 →
  196 Hz at Cutoff centre). This is [SN] p8's "gimmick", now reproduced.

### 3.3 MEG decay (Decay knob)

| Decay knob | To 10 % | [SN] p5 | MEG peak (N53) |
|---|---:|---:|---:|
| 0 | **173 ms** | 200 ms (minimum) | 10.12 V |
| 0.5 | 660 ms | — | 10.16 V |
| 1.0 | **2.54 s** | 2.5 s (maximum) | 10.17 V |
| 1.0, accented step | 815 ms (fast while ACCENT is high, then the knob's rate) | — | 10.12 V |

- The peak is the same for every setting (C62 is struck to about 11.33–
  11.38 V each time; [XB]: 11.33–11.48 V). **The envelope does not stack**
  on fast notes; only the accent sweep accumulates (§3.5).
- **Accent shortens the decay only while ACCENT is high.** With the line
  held for one step, an accented note at Decay maximum falls fast for
  125 ms (to about 18 %), then slowly. If ACCENT is held differently
  (gate length, or until the next step), this changes (§5, K3).

### 3.4 One note (Cutoff centre, Decay 0.5)

| Env Mod | Rest | Peak | At 125 ms (one 16th) | Sweep |
|---:|---:|---:|---:|---:|
| 0 | 500 Hz | 871 Hz | 715 Hz | +0.8 octave |
| 0.5 | 420 Hz | 1,273 Hz | 856 Hz | +1.6 octaves |
| 1.0 | 196 Hz | **6,736 Hz** | 1,902 Hz | **+5.1 octaves** |

- **Env Mod at minimum still leaves a small envelope** (+0.8 octave).
  VR5's pin-1 end sits on R61 → N561, so the MEG still moves its wiper
  slightly.
- At maximum the sweep reaches about 5 octaves while the resting point
  drops. Roland's stated aim on p8: a deeper sweep that stays in range.

### 3.5 Accent sweep: four accented 16ths, then a normal one

Accent full, Env Mod 0.5, Cutoff centre, Decay 0.5, 120 BPM; ACCENT held
for each accented step.

| Resonance | Peak per step (Hz): accents 1 → 4, then the normal note | Ladder current peaks (µA) | Shape |
|---:|---|---|---|
| 0 | 9,963 → 12,180 → 12,968 → 13,231; 2,154 | 154 → 204; 33 | A sharp spike at each hit, climbing slightly |
| 0.5 | 3,839 → 5,144 → 5,532 → 5,635; 2,104 | 60 → 88; 32 | A softer spike, climbing |
| 1.0 | 1,968 → 2,555 → 2,679 → 2,701; 2,078 | 30 → 42; 32 | **A smooth swell peaking about 30 ms after the hit, climbing over consecutive accents** |

- This is the accent "wow", from the circuit alone:
  - **At low Resonance**, VR4b's wiper sits at R46's end, so the accent
    reaches the cutoff as a sharp spike. C13 still charges through the
    whole VR4b track (50 kΩ), so the spikes climb a little.
  - **At high Resonance**, the wiper sits at C13. The accent arrives
    through R46 + VR4b + C13 (τ ≈ 0.1 s) as a delayed, smoothed swell, and
    C13 discharges only through R72 (D24 blocks the way back). So
    consecutive accents stack: +2.3 dB of current on the second hit,
    then +0.4 and +0.1 dB, levelling after about three.
- C13 reaches 4.7–4.9 V in this run.

---

## 4. Consequences for the Alkaline control path (P0 A3, A5, A6)

| Item | Design |
|---|---|
| Cutoff CV | Ladder current I = I(R73) × exp(ΔV/Vt). I(R73) follows the Cutoff wiper (A taper, VR3 from 12 V to N561 via R47). ΔV sums Env Mod (VR5 wiper → R63) and accent (VR4b wiper → R72) into R71. Evaluate at control rate in the log domain: one exp2 per block, as P0 A3 planned. A knob → Hz table from §3.2 is the simplest form |
| Calibration | Cutoff centre / Env Mod minimum = 500 Hz resonance (TM3, [SN] p7) |
| Env Mod | Lowers the resting cutoff by up to 1.4 octaves and scales the sweep from +0.8 to +5.1 octaves (§3.4). Not a plain depth control |
| MEG | Struck to the same peak on every note; decay to 10 % from 173 ms to 2.54 s (A taper on the Decay knob); accent bypasses the Decay knob while ACCENT is high |
| Accent sweep | A one-pole state for C13 (charge through R46 + VR4b's upper part, discharge through R72 only) plus the VR4b tap between R46's end and C13. Control rate. Accumulation and the Resonance-dependent shape follow from these two terms |
| CPU | All of this is control rate (1,378 Hz). No per-sample cost |

---

## 5. Open items

| # | Item | Status |
|---|---|---|
| K1 | Cutoff and Env Mod pot tapers (A) | Parts list ([SN] p8: "50kA" for CUT OFF FREQ and ENV MOD). The curve shape (20 % at half) is assumed, as in [XB] |
| K2 | The MEG strike circuit (Q36/Q38, C54/R148/R151, C55) | Behavioural here (11.85 V, 1.3 ms, from [XB]). Peak and timing match [XB]. A transistor-level model can follow if the attack shape matters |
| K3 | How long ACCENT stays high (IC13 latch) | Assumed one full step. It sets how long the Decay bypass lasts (§3.3) and the accent sweep's charge time. CPU behaviour, not in [SN] |
| K4 | P2 frequency law above 300 µA | Extrapolated. Only the Res-0 accent peaks (> 9 kHz) use it |
| K5 | Device spread (PNP Q10 against NPN Q11) | Matched generic devices calibrate within TM3's range (153 kΩ). On a real unit TM3 absorbs the spread |

---

## 6. Next

1. **End-to-end run: done**, `S092_P4_SLIDE_ACCENT_RUN.md`. VCO (P1) → VCF
   (P2) → VCA (P3) with this control network, for an accent and slide
   pattern. The reference waveforms for
   the Alkaline host tests (P0 §7.1 AL2–AL5).
2. **Reduce to DSP constants:** the knob → current tables (§3.2), the MEG
   and accent-sweep state equations (§4), the VEG table (P3), and the
   P1/P2 waveform and filter references, into the P0 §4 host model.
