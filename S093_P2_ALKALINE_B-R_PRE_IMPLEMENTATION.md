# S093 P2 — Alkaline B-R: Pre-Implementation Brief

- **Session:** 093, phase 2. This document is written for a **fresh session**
  that will write the code. It holds every detail needed, with
  back-references.
- **Goal of the implementation:** an on-device prototype of the Alkaline
  (TB-303) instrument with the **B2R** filter (§3.5, §3B), as a **Basic** type
  fitted under the Drum CPU ceiling, so the user can listen to it.
- **Decision of record (user, S093, final): B2R as Basic.**
  - B2R is B-R with only rungs 0–1 nonlinear: loop 209 + 3 VDIV. With the
    Tier 1 + Tier 2 control path its total is ≈ 264–267 per sample, against
    Drum ≈ 275+.
  - It supersedes "B-R under Drum" below, which does not fit (§3A.4).
  - B-R and A-R stay in `DiodeLadder.c` as measurable alternatives (B-R as
    Advanced; A-R as the cheaper Basic fallback).
- **Decision (user, S093):**
  - Implement B-R fitted under Drum, for a listening trial.
  - **Measured correction (S093):** the per-block control path was never
    counted against Drum. Measured, it is about 3,010 instructions per block
    against Drum's 413, which puts B-R at about 348 per sample against
    Drum's 270 (§8).
  - **Decision (user, S093): B-R with control-path reductions Tier 1 and
    Tier 2 (§3A).**
  - **Corrected projection (S093, after review):** an earlier version of
    this brief added the cuts up wrong ("500–600 per block, level with
    Drum").
    - Re-summed, with the measured render overheads and the drive included,
      B-R + Tier 1 + Tier 2 is **≈ 285–288 per sample against Drum's ≈ 275**
      (same accounting on both sides): **about 10–13 over Drum**.
    - It fits under the Cymbal/HiHat envelope (≈ 291) by about 3–6.
    - A-R with the same control path is ≈ 260–264, under Drum by about
      11–15.
    - **B-R under Drum is not reachable with the cuts known so far.** That
      needs a user decision before integration (§9, §12 R-1).
  - **Drive weighed (S093, §3B):**
    - A-R's "weaker drive" is measured. Its level, compression, bass and
      resonance follow the circuit under drive. **It is too bright when
      driven:** 3–8 kHz is +1.5 dB at ×4 and +5.3 dB at ×8 (acid), because
      its linear rungs pass the input pair's harmonics without the ladder's
      own rounding.
    - A new "B as Basic" option, **B2R**, makes only the two bottom rungs
      nonlinear. Its loop is 209 (−21 against B-R) and its total ≈ 264–267,
      **under Drum**. It sits between A-R and B-R at drive: +1.1/+1.6 dB at
      3–8 kHz (acid), but it loses mids and resonance at low Res with high
      drive.
    - The other "accuracy-cost" cuts (T2-4, rung secants every second
      sample) together give only −7.5, which is not enough.
  - Afterwards the user chooses: reduce further, or make Alkaline Advanced
    (replacing Cymbal/HiHat in a Scene) and add post-distortion/FX types in
    the freed margin.
  - Recorded in `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` §9 D2 and
    `S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md` §8.
- **Project rules that bind this work** (`MEMORY.md`):
  - Commits belong to the user: do not commit or suggest commits.
  - Any new or enlarged RAM needs the user's sign-off (§9).
  - No backward compatibility, ever.
  - The CPU ceiling is binding: the new type must be ≤ Drum's dearest
    setting, measured linked and on the device.
  - Constant per-sample cost: no skips on silence or settings.
  - New ITCM code only in a new `.itcm.sNNN` subsection (none is planned
    here).
  - Do not edit `ResonantFilter.c`'s ZDF twins or existing oscillator loops.
  - Do not use the `.claude` auto-memory; project memory is in the
    repository.

---

## 0. Document map (read in this order)

| Doc | What to take from it |
|---|---|
| `MEMORY.md` | Binding rules (RAM, CPU, DSP S073 invariants, ITCM order, voice chain) |
| `S092_P0_ALKALINE_INSTRUMENT_PLAN.md` | §1.3 CPU gate rules; §5 instrument definition (registry, rows, trigger semantics, gate timer, platform interactions); §9 decisions D1–D17 |
| `S092_P6_DSP_EQUATIONS.md` | The model: §3 audio path, §4 control path, §5.2 prewarp, §5.3 the real-time network (SplitNet), §5.4 the kernel pseudo-code |
| `S093_P0_ALKALINE_C_FUNCTIONS_CODE.md` | The existing C (§1–§4), the bench (§5), cost method (§6), integration plan (§7.1–§7.3), **the Re3/Sw3 removal plan (§10)** |
| `S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md` | The reductions, network form N1 (§5), the two drive fixes (§2.1), the variant table (§4); **B-R = `B_n1rq_ota32`** |
| `knowledge_files/specification_reference/dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | §3 runtime slots and dispatch, §4 descriptors and writers, §9.4 "Add an instrument type", §10 pitfalls |
| `tools/dsp_test/DSP_TEST.md` | Bench conventions (extract by name, break builds, `fpseq.py`) |

---

## 1. What exists today (S093 P0/P1), and its status

| File | Status | Change in P2 |
|---|---|---|
| `Core/DSPAudio/DiodeLadder.h/.c` | S093 P0 options A and B (S092 P6 SplitNet, [5/4] secants, 3 V out). Not in the build | **Replace the kernels with B-R and A-R** (§3) |
| `Core/DSP/Instruments/Alkaline/AlkalineControl.h/.c` | Control path (expo, MEG, accent network, VEG, prewarp). Validated (S093 P0) | **Rewritten for Tier 1/Tier 2** (§3A): fast maths, one-pass MEG/expo, the written-out accent solve, `alk_tanf` |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.h/.c` | The engine (init, trigger, async, render) | Trigger signature, drive, quiet query, B-R call (§5) |
| `Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c` | Generated (`gen_alk_tables.py`): SplitNet Res table, prewarp, VEG, fitted constants | **Regenerate as N1** (§4) |
| `Core/DSP/Instruments/Alkaline/AlkalineSquareTable.c` | Generated: the D8 303 square `[11][1024]` (22,528 B) | None (Alkaline keeps its own square; D3) |
| `tools/alkaline/` | Python model (`alk_model.py`), generator, validation (`validate.py`), C cross-check (`check_c.py`) | `gen_alk_tables.py` → N1 (and, if T2-2 is taken, the 64-sample table); `alk_model.py` gets the Tier 1/Tier 2 equations so `validate.py` V3/V5 test them; `check_c.py` gains drive cases |
| `tools/alkaline/variants/` | The S093 P1 experiment kernel `dlv_kernel.c` (switches; MODEL 3 = B2), `run_variants.py`, `gen_n1.py`, `drive_character.py` (§3B) | Reference for the port; keep |
| `tools/dsp_test/modules/alkaline.c` + Makefile target `alkaline` | Laws AL-B/R/S/N/AB + break | Update (§7) |

Nothing Alkaline is in the firmware build yet (`Makefile`,
`InstrumentManager`, menus).

---

## 2. Work order

| Phase | Content | Gate |
|---|---|---|
| **P2-0** (recommended first) | Remove Re3/Sw3 (S093 P0 §10, decided by the user). Frees 45,056 B of flash, which Alkaline needs (§8) | S093 P0 §10.4 W1–W7 |
| **P2-1** | DSP port: N1 tables, the **B2R** kernel (`DiodeLadder_renderB2`, §3.5) plus B-R and A-R as alternatives, the 7 V output scale, drive | Bench `alkaline` green; `check_c.py`; ARM count of `DiodeLadder_renderB2` ≈ 209, 3 VDIV |
| **P2-1A** | Control-path reductions Tier 1 and Tier 2 (§3A) | Per-block count ≈ 1,000 or less, all included (§3A.4; measured as in §8); S092 P6 V3/V5 control results held within the §3A tolerances |
| **P2-2** | Firmware integration: type, registry, union, dispatch, parameters, menu, sequencer query, Makefile | `make all`, `special_tags`, the instrument-manager bench |
| **P2-3** | Gates: linked loop counts, mixer check, flash, device CPU, listening | §10 |

Confirm §9's decisions at the start of the session before P2-2.

---

## 3. The B-R kernel (`DiodeLadder_renderB`)

### 3.1 Definition

B-R is S093 P1's variant `B_n1rq_ota32`. The exact reference is
`tools/alkaline/variants/dlv_kernel.c` compiled with:

```
-DMODEL=1 -DPADE_RUNG=32 -DRUNG_CLAMP=0 -DPADE_IO=54 -DXIN_TAIL=1 -DGAU1=0
-DNET1=1 -DQ21_LIN=1 -DOTA_P32=1 -DSPLIT=0 -DUNROLL=1 -DRUNG_HALF=0 -DRRAMP=1
```

**The port is that file with the `#if` branches resolved**, written in the
style of today's `DiodeLadder.c`:
- state loaded into locals and written back once;
- no switch in the loop;
- one `__SSAT` per sample.

Its measured cost is **230 instructions per sample, 3 VDIV**: standalone
`-Ofast`, `run_variants.py`'s loop count, which reproduces S093 P0's
figures.

| Property | Value |
|---|---|
| Ladder rungs | All four nonlinear, secants by the [3/2] Padé `t = (27 + z²)/(27 + 9z²)`, **not clamped** |
| Input pair (Q12) | [5/4] Padé `t = (945 + 105z² + z⁴)/(945 + 420z² + 15z⁴)`, argument clamped to ±3.5, **with the tail**: the denominator × max(\|x_in\|, 3.5)/3.5 (S093 P1 §2.1) |
| Q21 output stage | **Linear**: tp_pre = −A_OUT · v_hp/(2 Vt). No secant; `at = KA`, `bt = KB · zo` |
| OTA | [3/2] Padé, argument clamped to ±3; one sample late (it shares the batch) |
| Network | **Form N1** (§4): no per-sample fast modes. Per sample: D terms and one asymptote one-pole per output (u at 50 Hz, v at 120 Hz). Per block: all eight states |
| s-asymptote (`gau1`) | Dropped per sample (also from the block copy z8, so the block system carries it) |
| Divisions | VDIV 1: the four continuants' shared reciprocal. VDIV 2: the loop closure. VDIV 3: the batched secants (rungs, input pair, OTA) |
| Output | int16 at **7 V full scale** (`DL_OUT_SCALE = 32767/7`) |
| Accuracy | LSD60 against the golden model, acid ×1/×4/×8: 0.19/0.69/0.64 dB; lowres ×1/×4: 0.17/0.57. Ring 2.018 ms / 0.372 (golden 2.018 / 0.364) |

### 3.2 Per-sample pseudo-code (B-R)

Per block on entry:
- ramps: `di = (iEnd − i)/32`, `dia = (iAbcEnd − ia)/32`,
  `dru = (rU − rUPrev)/32`, `drv = (rV − rVPrev)/32`;
- start values `ru = rUPrev`, `rv = rVPrev`;
- `sScale` already includes the drive.

```
i += di;  ia += dia;  ru += dru;  rv += drv
s  = buf[j] * sScale
w0 = K0*i;  w1 = K1*i
d01 = x0-x1;  d12 = x1-x2;  d23 = x2-x3
a0 = t0*d01;  a1 = t1*d12;  a2 = t2*d23;  a3 = t3*x3
r0 = x0 - w0*(a0 + tin*xinL)
r1 = x1 + w1*(a0 - a1);   r2 = x2 + w1*(a1 - a2)
r3 = x3 + w1*(a2 - a3) + KL*(KG*zo - vhpL)
e0 = w0*t0;  e1 = w1*t0;  e2 = w1*t1;  e3 = w1*t2
th1 = 1+e0
th2 = (1+e1+e2)*th1 - e1*e0
th3 = (1+e2+e3)*th2 - e2*e2*th1
th4 = (1+e3+w1*t3+D3X)*th3 - e3*e3*th2
p12 = th1*th2;  p123 = p12*th3
inv = 1/(p123*th4)                                        // VDIV 1
i123 = inv*th4;  i12 = i123*th3
ip0 = i12*th2;  ip1 = i12*th1*th1;  ip2 = i123*p12*th2;  ip3 = inv*p123*th3
m1 = e1*ip0;  m2 = e2*ip1;  m3 = e3*ip2
y1 = r1 + m1*r0;  y2 = r2 + m2*y1;  y3 = r3 + m3*y2
q0 = -w0*tin;  q1 = m1*q0;  q2 = m2*q1;  q3 = m3*q2
pr3 = y3*ip3;  pq3 = q3*ip3
quk  = ALU*qu + BEU*gau0*tpL
base = ru + quk + ku1*s
xin  = (base + ku0*(KA*pr3 + KB*zo)) / (2Vt - ku0*KA*pq3)    // VDIV 2
n3 = pr3 + pq3*xin
n2 = (y2 + q2*xin + e3*n3)*ip2
n1 = (y1 + q1*xin + e2*n2)*ip1
n0 = (r0 + q0*xin + e0*n1)*ip0
vhp = (2Vt*n3 - zo)*KG;   zo += TWO_G*vhp
// secants
zE = clamp(xin, ±3.5);  sE = zE²;  nE = 945 + sE*(105 + sE)
dE = (945 + sE*(420 + 15*sE)) * (max(|xin|, 3.5) / 3.5)
zG = clamp(vcaL/2Vt, ±3);  sG = zG²;  nG = 27 + sG;  dG = 27 + 9*sG
zA = n0-n1; zB = n1-n2; zC = n2-n3; zD = n3        // unclamped
nX = 27 + zX²;  dX = 27 + 9*zX²                    // X = A..D
pAB = dA*dB;  pABC = pAB*dC;  pABCD = pABC*dD;  prod = dE*dG
r = 1/(pABCD*prod)                                  // VDIV 3
iP = r*pABCD;   r *= prod                           // iP = 1/(dE dG); r = 1/pABCD
t3 = nD*r*pABC;  r *= dD
t2 = nC*r*pAB;   r *= dC
t1 = nB*r*dA
t0 = nA*r*dB
tin  = nE*iP*dG
tota = nG*iP*dE
// output stage, network, OTA
tp  = -A_OUT * vhp/2Vt                              // linear Q21
qu  = quk + BEU*gau0*tp
qv  = ALV*qv + BEV*gav0*(tpL + tp)
vca = rv + qv + dv0*tp
out = R_OTA * iaL * zG * tota                       // the OTA, one sample late
buf[j] = SSAT16(out * (32767/7))
accTp += tp;  accS += s
x0..x3 = n0..n3;  tpL = tp;  xinL = xin;  vhpL = vhp;  vcaL = vca;  iaL = ia
```

Per block on exit:
- write the state back;
- `slowUpdate(accTp/32, accS/32)` (§4.3).

**Initial state** (`DiodeLadder_init`): everything 0, except t0–t3,
tin and tota, which are 1.

### 3.3 Constants (float literals in `DiodeLadder.c`)

Derivations are in S093 P0 §3.2 and S092 P6 §3.

| Name | Value | Note |
|---|---|---|
| `K0`, `K1` | 6090.59326, 3322.14185 | (h/2)/(4 Vt C18), (h/2)/(4 Vt C19) |
| `KG`, `TWO_G` | 0.995162427, 0.00972212479 | C27/C25 TPT one-pole |
| `KL`, `D3X` | 0.142460123, 0.00732955895 | Term L |
| `KA`, `KB`, `A_OUT` | −5.33020973, 103.098831, 5.35612011 | Q21 |
| `R_OTA` | 52525.9531 | OTA load |
| `2Vt`, `1/2Vt` | 0.0517, 19.342359 | Vt = 25.85 mV |
| `ALU`, `BEU`, `DECAY_U` | 0.992902756, 1.12955859e-05, 0.796187937 | N1 loop asymptote, 50 Hz (`variants/dlv_n1.h`) |
| `ALV`, `BEV`, `DECAY_V` | 0.983050883, 1.12397456e-05, 0.578678608 | N1 VCA asymptote, 120 Hz |
| `DL_OUT_SCALE` | 32767/7 | Output full scale 7 V (S093 P1 D17) |

The generator must assert `DECAY_U`/`DECAY_V` against the network it
writes, as it does today for the 5 Hz decay (compare as float32).

### 3.4 A-R (`DiodeLadder_renderA`, the fallback, kept measurable)

Reference flags: `-DMODEL=0 -DXIN_TAIL=1 -DGAU1=0 -DNET1=1 -DQ21_LIN=1`
(OTA [5/4]). Cost 206 + 2 VDIV.
- The rungs are linear, so the next sample's continuants share the secant
  batch, and the first sample's pivots are computed once per block (see
  `dlv_kernel.c` `pivots()`).
- Not called by `AlkalineVoice`; it is kept so the class decision can be
  revisited.
- LTO's `--gc-sections` drops it from the image while unreferenced.

### 3.5 B2R (`DiodeLadder_renderB2`): **the chosen kernel (user, S093)**

`AlkalineVoice` calls this one. §3.1–§3.3 (B-R) remain the base
description: B2R is that kernel with rungs 2 and 3 linear, as listed
below.

- **Reference flags:**
  `-DMODEL=3 -DPADE_RUNG=32 -DRUNG_CLAMP=0 -DXIN_TAIL=1 -DGAU1=0 -DNET1=1 -DQ21_LIN=1 -DOTA_P32=1`.
- **The B-R kernel with rungs 2 and 3 linear** (t2 = t3 = 1):
  - the continuants and their reciprocal stay per sample (VDIV 1);
  - the secant batch holds rungs 0–1, the input pair and the OTA (VDIV 3).
- **Measured** (standalone `-Ofast`): **209 instructions per sample, 3
  VDIV**.
- **Accuracy (LSD60 against the golden model):**
  - acid ×1/×4/×8: 0.20/0.88/0.90 dB;
  - lowres ×1/×4: 0.22/1.15.
- In the §3.2 pseudo-code: drop zC, zD, nC, dC, nD, dD from the batch
  (`pAB = dA·dB`, `r = 1/(pAB·prod)`); `t2`, `t3` are the constant 1.

## 3A. Control-path reductions, Tier 1 and Tier 2 (decided)

The per-block work is measured at about 3,010 instructions, plus the render's
own entry and exit. Each cut below gives its measured cost now, the
estimated cost after, the method, and the check it must pass.
- **Measurement:** the static longest path on an M7 link with newlib,
  loops multiplied by their iteration counts, `bl` callees added. §8 lists
  the reproduce steps.
- **Reference for the checks:** `tools/alkaline/validate.py` V3 (control
  path against SPICE) and V5 (the pattern against SPICE). Port each cut
  into `alk_model.py` too, so V3/V5 run on the reduced equations.

### 3A.1 Fast maths (needed by several cuts)

The project has **no** fast log/exp. `fastTan` in `ResonantFilter.c` is not
exported, and S093 P0 §6.2's "the project's fast log2/exp2" was wrong.

Add `static inline` functions in `AlkalineControl.c`:
- `alk_log2f(x)`: exponent from the bits, plus a degree-5 polynomial on the
  mantissa;
- `alk_exp2f(x)`: integer part into the exponent bits, plus a degree-5
  polynomial on the fraction;
- `alk_tanf(x)` for |x| ≤ 1.3: `x(945 − 105x² + x⁴)/(945 − 420x² + 15x⁴)`.

Pitch needs a relative error of 6e-5 or better (0.1 cent); degree 5 gives
about 1e-7. Each function is about 12–18 instructions.

### 3A.2 Tier 1 (no intended change to the sound)

| # | Cut | Now | After (est.) | Method | Check |
|---|---|---:|---:|---|---|
| T1-1 | Accent network | 812 | ≈ 100 | Write the 5 × 5 solve out for its fixed shape. Eliminate V13 (from W), V36 (from W7) and P (from W7 and W); that leaves a 2 × 2 in (W7, W), solved with one division. Diode states (D24, D27): start from last block's; solve; if inconsistent, flip and solve once more. **Always do both solves and select** (constant cost) | V3c accent-run peaks within ±1 % of today's C; C2 accent peaks unchanged |
| T1-2 | MEG | 713 | ≈ 110 | Keep the stack current `i` from the last block (new state field); one fixed-point pass instead of four; `alk_log2f`/`alk_exp2f`; softplus as `max(x, 0) + ln(1 + exp(−\|x\|))` with the fast functions | V3b MEG time to 10 % within ±1.5 % of SPICE (today 0.4–1.4 %) |
| T1-4 | Expo converter | 313 | ≈ 80 | Keep last block's ladder current; one Vbe pass from it; fast exp/log | V3a within 33 cents of the P4 DC map; C2 cutoff per step ±5 cents of today |
| T1-5 | Prewarp | 132 | ≈ 15 | `alk_tanf` | Cutoff placement (S092 P6 §5.2 table) within ±10 cents |
| T1-6 | Pitch | 254 | ≈ 180 | `alk_log2f`/`alk_exp2f` instead of `log2f`/`exp2f` | Pitch error ≤ 0.1 cent at every note; AL-S glide step |
| T1-7 | Oscillator setup (new, S093 review) | (in T1-6's 180) | −≈ 110 | Bypass `osc_setFreq()` (139 worst path; its frequency cache misses every block during a glide). Set `osc.phaseInc` (the `freq2PhaseIncr1024` formula: f × 1024/fs × 2²²) and `osc.tableOffset` (the octave-edge count) directly in `AlkalineVoice`. `Oscillator.c` is not edited | The same phase increment and octave level as `osc_setFreq()` for every note (bench) |
| T1-8 | Drive gain (new, S093 review) | 44 (`exp2f`) | ≈ 15 | `alk_exp2f(3·drive)` | — |

**Not Tier 1 (moved, S093 review):** the 17-point nearest Res table (old
T1-3).
- Every entry of the table depends on the Resonance pot, so a nearest-point
  lookup makes the resonance move in 17 steps, about every 8 knob values.
- That is audible when turning, automating or LFO-modulating `res`, and the
  old check (a sweep with no step above −80 dB) did not test it.
- **The blend stays.** The option is listed as T2-4 with a listening check.

### 3A.3 Tier 2 (small model simplifications; must be checked against SPICE)

| # | Cut | Saves (est.) | Method | Check / risk |
|---|---|---:|---|---|
| T2-1 | MEG diode-stack floor | ≈ 70 | No fixed-point pass: the stack voltage as a short table or fitted curve of the C62 voltage and the discharge resistance | **Risk:** S092 P6 found a constant floor could not fit the accented and normal decays together. V3b within ±3 %, or revert to T1-2 |
| T2-2 | Slow network at half rate, **split evenly over two blocks** | ≈ 250 | One update per 64 samples. Rows 0–3 of z_new in even blocks and rows 4–7 in odd blocks, both from a saved z_old (+≈ 25 for the copy and bookkeeping), so **every block pays the same** (constant-CPU rule). Table regenerated for T = 64/fs; output ramp over 64 samples; two blocks of latency | Ring decay within ±0.01 of 0.372; C1 LSD60 ≤ +0.1 dB; a Res sweep shows no step |
| T2-3 | Accent-network diode states held through an accent | ≈ 30 | Decide D24/D27 at the step's first block and hold them (one solve per block) | V3c and V5 accent peaks within ±2 %; **risk** at Res 0 |
| T2-4 | Res table nearest point instead of the blend (optional, moved from Tier 1) | ≈ 120 on top of T2-2 | 33 points (flash 14,256 B) to keep steps small | **Listening check** of `res` turned, automated and LFO-modulated. Steps of about 4 knob values; reject if audible |

### 3A.4 Projected total (corrected, S093 review)

Per block after Tier 1 + Tier 2 (without T2-4), all included:

| Item | Instructions |
|---|---:|
| `Alkaline_calcVoiceAsync` body | 115 |
| MEG (T1-2 + T2-1) | 40 |
| Accent network (T1-1 + T2-3) | 70 |
| Expo converter (T1-4) | 80 |
| VEG | 45 |
| Prewarp (T1-5) | 15 |
| `DiodeLadder_setNet` | 82 |
| Slow network (blend kept; T2-2) | ≈ 300 |
| Pitch (T1-6; with T1-7 ≈ 70) | 180 (70) |
| Drive gain (T1-8) | 15 |
| Render entry/exit: `dlv_render`'s non-loop part 113 + `Alkaline_calcVoiceSyncBlock` 19 + the oscillator block entry ≈ 15 (measured on the B-R kernel object) | ≈ 147 |
| **Per block** | **≈ 1,090 (≈ 980 with T1-7)** |
| ÷ 32 | ≈ 34 (≈ 31) |

| Per sample, same accounting on both sides | Drum | Cymbal/HiHat | Alkaline B-R | Alkaline B2R | Alkaline A-R |
|---|---:|---:|---:|---:|---:|
| Audio loop (+ oscillator) | 247 | ≈ 260 (DSP ref §8.2, not re-measured) | 254 | 233 | 230 |
| Per-block work ÷ 32 | 13 (413) | 16 (≈ 525: 26 + 3 × 139 + 28 + snap/filter/dispatch ≈ 54) | 31–34 | 31–34 | 31–34 |
| Render entry/exit ÷ 32 | ≥ 5 (≥ 163; its callees' entries not counted) | ≥ 4 (≥ 134) | (in the 31–34) | (in the 31–34) | (in the 31–34) |
| SRR | 10 | 10 | 0 | 0 | 0 |
| **Total** | **≈ 275+** | **≈ 290+** | **≈ 285–288** | **≈ 264–267** | **≈ 261–264** |

**Result:**
- **B-R + Tier 1 + Tier 2 is about 10–13 over Drum.** Drum's callee
  entries, still uncounted, would narrow that by a few.
- **It fits under Cymbal/HiHat**, by about 3–6.
- **A-R fits under Drum** with about 11–14 to spare.
- T2-4 would take about 4 more off B-R (≈ 281–284), still over Drum.
- **B2R fits under Drum** with about 8–11 to spare (§3B).
- No further full-B-R cut is known without an accuracy cost; the rung
  secants every second sample give −3.5 (S093 P1 §4).

## 3B. Drive: what A-R's "weaker drive" is, weighed against B as Basic

**Measurement:**
- Inputs: the SPICE end-to-end runs (acid, Res 0.85; lowres, Res 0.2) at
  ×1/×4/×8, with the currents block-ramped as the firmware applies them.
- Each form is compared with the golden RK4 model on the same inputs. Bands
  are the energy ratio to the golden model per band.
- **Resonance share:** energy within ±⅓ octave of the resonance frequency
  over the total, per step.
- Tool: `tools/alkaline/variants/drive_character.py`, writing
  `drive_character.json`.

### 3B.1 What saturates when driven

The golden model's largest normalised arguments per stage (S093 P1 §3.1):

| Drive | Input pair (Q12) | Rung 0 | Rung 1 | Rung 2 | Rung 3 |
|---|---:|---:|---:|---:|---:|
| ×1 | 0.93 | 0.44 | 0.18 | 0.14 | 0.13 |
| ×4 | 3.60 | 0.94 | 0.66 | 0.60 | 0.56 |
| ×8 | 6.86 | 1.48 | 1.14 | 1.01 | 0.89 |

- **The input pair** carries the drive in every form (A, B and B2 all
  model it). It sets the level, the compression and the added harmonics.
- **The rungs** then round those harmonics off inside the ladder.
  - A-R has no rung saturation.
  - B2R has rungs 0–1, which saturate most.
  - B-R has all four.

### 3B.2 Measured against the golden model

| Run | Drive | Form | Level | 30–200 Hz | 200–800 | 800 Hz–3 kHz | **3–8 kHz** | Resonance share | LSD60 |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| acid | ×1 | A-R / B-R / B2R | −0.14 / −0.11 / −0.11 | −0.33 / −0.33 / −0.33 | ±0.03 | ±0.07 | +0.11 / −0.13 / −0.14 | +0.10 / +0.12 / +0.12 | 0.31 / 0.19 / 0.20 |
| acid | ×4 | A-R | −0.23 | −0.34 | −0.11 | −0.24 | **+1.52** | +0.07 | 1.26 |
| | | B-R | −0.14 | −0.30 | +0.05 | −0.01 | +0.41 | +0.14 | 0.69 |
| | | B2R | −0.18 | −0.32 | −0.01 | −0.15 | **+1.09** | +0.07 | 0.88 |
| acid | ×8 | A-R | −0.17 | −0.26 | −0.05 | −0.26 | **+5.29** | −0.15 | 1.98 |
| | | B-R | −0.07 | −0.23 | +0.23 | +0.22 | +0.16 | +0.28 | 0.64 |
| | | B2R | −0.21 | −0.29 | −0.09 | −0.25 | **+1.63** | −0.09 | 0.90 |
| lowres | ×1 | all three | +0.06 to +0.08 | +0.11 | ±0.05 | ±0.07 | −0.03 / −0.24 / −0.22 | −0.15 to −0.21 | 0.10 / 0.17 / 0.22 |
| lowres | ×4 | A-R | +0.17 | +0.20 | −0.05 | −0.46 | **+3.11** | −0.42 | 0.88 |
| | | B-R | +0.24 | +0.26 | +0.25 | −0.04 | −0.12 | −0.44 | 0.57 |
| | | B2R | +0.11 | +0.17 | −0.13 | **−0.83** | +0.96 | **−0.90** | 1.14 |
| lowres | ×8 | A-R | +0.40 | +0.42 | −0.11 | −1.26 | **+4.30** | −0.88 | 1.46 |
| | | B-R | +0.48 | +0.49 | +0.66 | −0.38 | −1.39 | −0.76 | 1.00 |
| | | B2R | +0.24 | +0.29 | −0.30 | **−2.11** | −0.36 | **−1.50** | 2.28 |

### 3B.3 What "weaker drive" means

- **At the 303's own drive (×1), all three are the same:** every band
  within ±0.33 dB, resonance within ±0.2 dB.
- **Level, compression, bass and resonance under drive follow the circuit
  in all three.**
  - Output level rises 14.5 dB from ×1 to ×8 for an 18 dB input increase.
    Every form tracks that within 0.5 dB.
  - The input pair does this, and all forms have it.
- **A-R is too bright when driven.** Its linear rungs pass the input pair's
  harmonics through without the ladder's own rounding:
  - 3–8 kHz: +1.5 dB at ×4, **+5.3 dB at ×8** (acid); +3.1 / +4.3 dB
    (lowres);
  - a slight dip in 800 Hz–3 kHz at low Res and high drive (−1.3 dB at
    ×8).

  It sounds fizzier and harsher than the circuit at high drive, not
  weaker or quieter.
- **B-R follows the circuit:** every band within ±0.7 dB except the top
  band at lowres ×8 (−1.4 dB).
- **B2R is in between.**
  - At high Res, about a third of A-R's top-band excess: +1.1 / +1.6 dB.
  - At low Res and high drive it errs the other way: the 800 Hz–3 kHz band
    is −0.8 / −2.1 dB, and the resonance share −0.9 / −1.5 dB. That is
    slightly duller and less resonant than the circuit, because rungs 2–3
    stay linear while the lower two compress.

### 3B.4 The options under Drum

| Form | Total per sample (§3A.4) | Margin under Drum (≈ 275+) | Error at drive | Character |
|---|---:|---:|---|---|
| **A-R** | ≈ 261–264 | ≈ 11–14 | Largest (LSD60 up to 1.98) | Too bright/fizzy at ×4–×8 (up to +5 dB at 3–8 kHz) |
| **B2R** (new) | ≈ 264–267 | ≈ 8–11 | Middle (up to 0.90 at high Res; 1.14–2.28 at low Res, high drive) | Close at high Res (+1–1.6 dB top); duller mids and weaker resonance at low Res, high drive |
| B-R + T2-4 + rung secants every second sample | ≈ 277–281 | Over by 2–6 | Small (B-R + the half-rate secant cost, lowres ×4 0.57 → 0.91) | As B-R, plus Res stepping if T2-4 is audible |
| B-R (Advanced) | ≈ 285–288 | (Cymbal/HiHat ≈ 290+: 3–6 under) | Smallest (0.64 at ×8) | Follows the circuit |

**Recommendation:**
- **B2R as Basic.** It fits under Drum, its error at the 303's own drive is
  that of B-R (0.20 against 0.19), and at high resonance it keeps most of
  B-R's drive character. A-R's top band would be 3–4 dB too bright there.
- **Its weakness is low resonance at high drive.** Listen there first.
- A-R stays as the cheaper fallback; B-R as Advanced stays the
  most-accurate option.

**Listening renders** (`tools/alkaline/alk_drive_<run>_x<d>_<form>.wav`):
- runs `acid` and `lowres`; drive `x1`, `x4` and `x8`;
- forms `golden`, `A-R`, `B-R` and `B2R`;
- the first 0.6 s of each run; each file normalised to −2 dBFS.

**Before relying on the margins:** R-2 and R-6 (Drum's audio loop and
render callees not re-measured) apply to all three Basic options equally.

---

## 4. Tables: network form N1 (`gen_alk_tables.py`)

### 4.1 Generator change

- Build every Res point with `SplitNet` configured as N1:
  `F_SPLIT = 1e9` (no fast modes), `A_ASYM_U = 2π·50`, `A_ASYM_V = 2π·120`.
  `alk_model.SplitNet` already supports per-output poles (S093 P1).
- Today's `gen_n1.py` shows the configuration and the assertions; fold it
  into `gen_alk_tables.py` and delete the variant copy, or keep the variant
  for the experiments.
- `--check` must still prove the committed files.

### 4.2 Layout (drop the fast-mode fields)

```c
typedef struct {
	float phi[8][8];   /* slow block system, physical basis */
	float gam[10][2];  /* block input gains (tp_pre mean, s mean); rows 8, 9 = asymptote copies */
	float cs[2][8];    /* slow outputs u, v */
	float ga[2][2];    /* asymptote gains [u|v][tp|s] */
	float d[2][2];     /* instantaneous gains [u|v][tp|s] */
} DiodeLadderNet;      /* 108 floats, 432 B; 9 points = 3,888 B */
```

Per-block `DiodeLadder_setNet()`, with f the interpolation fraction and
g = 1 − f:

```
gau0 = lerp(ga[0][0]);  gav0 = lerp(ga[1][0]);  dv0 = lerp(d[1][0])
ku0  = lerp(d[0][0]) + BEU*gau0
ku1  = lerp(d[0][1])                    /* gau1 dropped */
```

### 4.3 The slow update (per block)

```
for r in 0..7:  zn[r] = g*(phiLo[r]·z + gamLo[r]·(m0, m1)) + f*(phiHi[r]·z + gamHi[r]·(m0, m1))
z8 = DECAY_U*z8 + g*gamLo[8][0]*m0 + f*gamHi[8][0]*m0        /* no s input (gau1 dropped) */
z9 = DECAY_V*z9 + g*(gamLo[9]·(m0, m1)) + f*(gamHi[9]·(m0, m1))
rUPrev = rU;  rVPrev = rV
rU = lerp(cs[0])·zn − z8;   rV = lerp(cs[1])·zn − z9;   z[0..7] = zn
```

---

## 5. `AlkalineVoice` changes

| Item | Change |
|---|---|
| Trigger | `Alkaline_triggerVoice(voice, source_slot, velocity, note, stepSamples)`. Add the platform part, as `Drum_triggerVoice()` does (`DrumVoice.c` ≈ l. 241–254): `lfo_retrigger(source_slot)`, `modNode_updateValue(&velocityModulators[source_slot], velocity/127.f)`, `instrumentManager_applyVelocityModulationTarget(source_slot, velocity/127.f)`. The rest stays as today (S093 P0 §4.2): accent ≥ 101, legato, slide latch, gate in samples |
| Drive | New member `float drive` (0..1, TYPE_FLT). In `calcVoiceAsync`: `voice->driveGain = alk_exp2f(3.0f * voice->drive)` (×1..×8, D16; T1-8: not the library `exp2f`). In the render: `sScale × driveGain` |
| Render | `DiodeLadder_renderB2(&ladder, buf, sScale * driveGain, iLadEnd, iAbcEnd)` (B2R, §3.5). `renderB` (B-R) and `renderA` (A-R) are not called |
| Quiet query | `uint8_t Alkaline_isQuiet(const AlkalineVoice *v)`: `gateLeft == 0 && !accentOn && iAbcEnd < 1e-7f` (for `instrumentManager_ampEnvelopeQuiet()`, which otherwise returns 1 for a type without a `SlopeEg2`) |
| `noinline` | **Both** `Alkaline_calcVoiceSyncBlock()` and `Alkaline_calcVoiceAsync()` get `__attribute__((noinline))`. Both are called from inside `mixer_calcNextSampleBlock()` (S092 P0 §1.3: the LTO risk); the async is now the heavier of the two |
| Struct | `DiodeLadder` loses y0/y1, tout and the fast-mode coefficients, and gains nothing (it was 220 B). `AlkalineVoice` stays about 500 B, inside the 1,264 B slot. Add `_Static_assert(sizeof(AlkalineVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES)` at integration |

---

## 6. Parameters and menu (`AlkalineParameters.c/.h`)

Pattern: `DrumParameters.c`, with its row macros: `ROW`, `ROW_SPECIAL`,
`ROW_MENU`, `ROW_SELECT_ONOFF_SPECIAL`, `ROW_NOBIND`, `ROW_NOBIND_IMAGE`.
- `BIND` uses `offsetof(AlkalineVoice, …)`.
- Each row on one line (for `check_special_tags.py`).
- `ALKALINE_PARAM_DESCRIPTOR_COUNT` must equal the enum count
  (`_Static_assert`).
- `ALKALINE_INSTRUMENT_TYPE_FLAGS INSTRUMENT_FLAG_BASIC`; display label
  `"Alkaline"`.

| # | Key | Short / long, category | dtype, mod | Binding |
|---:|---|---|---|---|
| 0 | `alk_wave` | `wav` / "Waveform", "303" | ON/OFF tag `ONOFF_TAG_ALK_WAVE` (new, value 3; names `{2}, "Saw", "Sqr"`); selector, MOD_NONE | `ROW_SELECT_ONOFF_SPECIAL(..., wave, TYPE_UINT8, IM_SPECIAL_NONE)` |
| 1 | `osc1_pitch_coarse` | `coa` / "Coarse", "303" | `DTYPE_0B127`, MOD_0_127 | `ROW_SPECIAL(..., osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE \| IM_SPECIAL_OSC1)` (default 36 = C2) |
| 2 | `alk_tuning` | `tun` / "Tuning", "303" | `DTYPE_PM63`, MOD_PM63 | `ROW(..., tuning, TYPE_FLT)`; ±700 cents at control rate (no new writer) |
| 3 | `alk_slide` | `sld` / "Slide", "303" | `DTYPE_ON_OFF`, MOD_NONE | `ROW(..., slide, TYPE_UINT8)` |
| 4 | `alk_cutoff` | `cut` / "Cutoff", "VCF" | `DTYPE_0B127`, MOD_0_127 | `ROW(..., cutoff, TYPE_FLT)` |
| 5 | `alk_reso` | `res` / "Resnance", "VCF" | as 4 | `ROW(..., reso, TYPE_FLT)` |
| 6 | `alk_envmod` | `env` / "Env Mod", "VCF" | as 4 | `ROW(..., envMod, TYPE_FLT)` |
| 7 | `alk_decay` | `dec` / "Decay", "VCF" | as 4 | `ROW(..., decay, TYPE_FLT)` |
| 8 | `alk_accent` | `acc` / "Accent", "Voice" | as 4 | `ROW(..., accent, TYPE_FLT)` |
| 9 | `alk_drive` | `drv` / "Drive", "VCF" | as 4 | `ROW(..., drive, TYPE_FLT)` (×1..×8) |
| 10 | `instrument_vol` | `vol` | as Drum | `ROW(..., vol, TYPE_FLT)` |
| 11 | `instrument_pan` | `pan` | as Drum | `ROW(..., pan, TYPE_UINT8)` |
| 12–19 | `lfo_rate`, `lfo_amount`, `lfo_amount_2`, `lfo_wave`, `lfo_retrigger_voice`, `lfo_polarity`, `lfo_sync`, `lfo_offset` | as Drum | as Drum | as Drum, on `lfo.*` |
| 20–21 | `velo_mod_amount`, `velo_mod_dest` | as Drum | as Drum | `ROW_NOBIND_IMAGE` / `ROW_NOBIND` as Drum (no `velo_vol_on_off`: the 303's velocity is the accent) |
| 22–25 | `lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`, `lfo_target_param_2` | as Drum | as Drum | `ROW_NOBIND` as Drum |

- **Not offered** (S092 P0 §5.5): `srt` (D4, no SRR), `run`, `chn`, `pos`,
  `typ`, fine pitch.
- **Menu pages** (16 cells per page, four-cell screens, padded with
  `INSTRUMENT_MENU_EMPTY`):
  - `303`: coa tun wav sld;
  - `VCF`: cut res env dec, then drv;
  - `Voice`: acc vol pan;
  - LFO page as Drum;
  - velocity: dst amt.
- **New menu text:** `alkWaveNames` in `MenuText.h`. Add a branch in
  `menu_onoffTagNames()` (`menu.c` ≈ l. 12340, which today maps every tag
  other than VOICE_CHAIN to `oscRunNames`). Check the other `DTYPE_ON_OFF`
  tag sites in `menu.c`: S088 lists six.
- **`check_special_tags.py`:** add
  `'ALK': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c'` to `TABLES`;
  the coarse row must classify as `PITCH_COARSE | OSC1`.

---

## 7. Integration (InstrumentManager and the rest)

Line numbers are from the S093 tree and will shift.

### 7.1 `InstrumentManager.h/.c`

| Site | Change |
|---|---|
| `InstrumentManager.h` ≈ l. 35 | `INSTRUMENT_TYPE_ALK` before `INSTRUMENT_TYPE_UNKNOWN`. UNKNOWN moves from 4 to 5: check stored bytes that might hold it (S092 P0 §5.5) |
| `instrument_registry[]` ≈ l. 35 | `{ INSTRUMENT_TYPE_ALK, "alk", alkaline_instrument_display_label, ".alk", "Alkaline", ALKALINE_INSTRUMENT_TYPE_FLAGS, alkaline_param_descriptors, ALKALINE_PARAM_DESCRIPTOR_COUNT, alkaline_menu_pages, ALKALINE_MENU_PAGE_COUNT }` |
| Runtime union ≈ l. 171 | `AlkalineVoice alkaline;` + `_Static_assert` + `instrumentManager_alkalineRuntime(slot)` |
| `instrumentManager_runtimeInstance` (1408) | Return `instrumentManager_alkalineRuntime(slot)` |
| `instrumentManager_runtimeLfo` (1425) | `&voice->lfo` |
| `instrumentManager_runtimeInit` (1493), `resetRuntimeSlotFromScene` (1624) | `Alkaline_initVoice()` |
| `instrumentManager_ampEnvelopeQuiet` (1502) | Branch for ALK: `Alkaline_isQuiet()` |
| `instrumentManager_runtimePan` (1739), `runtimeVolume` (1768) | `voice->pan`, `voice->vol` |
| `instrumentManager_slotRunMode` (1801), `slotVoiceChain` (1829), `slotHasActiveItpFade` (1919) | Default (0) |
| `instrumentManager_recalcSlotFilter` (1881) | No-op (no ZDF filter) |
| `instrumentManager_calcSlotAsync` (1990) | `Alkaline_calcVoiceAsync()` |
| `instrumentManager_calcSlotSyncBlock` (2024) | `Alkaline_calcVoiceSyncBlock(..., buf, size)` |
| `instrumentManager_slotUsesSrr` (2047) | Not listed, so it returns 0: no SRR (D4) |
| `instrumentManager_triggerTrack` (2149) | `Alkaline_triggerVoice(voice, slot, velocity, note, seq_trackStepSamples(trigger_track))` |
| `instrumentManager_oscBySelector` (2311) | `IM_SPECIAL_OSC1` → `&voice->osc` (the coarse writer) |
| `_filter` (2335), `_ampEg` (2355), `_distortion` (2389), `_transient` (2409) | Return 0 (no such stage) |
| `instrumentManager_applySlot6AlternateDecay` | No change: it finds no `amp_envelope_decay` row, so track 7's alternate has no effect on Alkaline in this prototype (S092 P0 §5.5 mapping to MEG decay deferred) |

### 7.2 Other files

| File | Change |
|---|---|
| `Core/Sequencer/sequencer.c/.h` | New read-only `uint32_t seq_trackStepSamples(uint8_t track)` = `stepScale_ticksQ8(seq_effectiveTrackScale[track]) × 44108 × 60 / (256 × 96 × seq_getBpm())` (96 PPQ, Q8.8 ticks; a 16th = 24 ticks). Clamp the BPM ≥ 1; track ≥ `NUM_TRACKS` → use the default scale. Never writes Pattern data (MEMORY.md S078). Check that `seq_tempo` follows external clock (S092 P0 §5.4) |
| `Core/Menu/MenuText.h`, `menu.c` | `ONOFF_TAG_ALK_WAVE`, `alkWaveNames`, the tag-name branch (§6) |
| `Core/Menu/menu.c` l. 1728 | `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` grows by 1 entry: **+2 B `.bss` (needs sign-off, §9)** |
| `SceneData.c` 1128, `filesystem.c` 18242 | The default Scene stays Drum/Snare/Cymbal/HiHat (no change) |
| `storageTypes.h` 141 | `#define STORAGE_INSTRUMENT_ALK INSTRUMENT_TYPE_ALK` |
| `Makefile` | `-ICore/DSP/Instruments/Alkaline`. `DSP_SRCS` gets `Core/DSPAudio/DiodeLadder.c`, `Core/DSP/Instruments/Alkaline/AlkalineVoice.c`, `AlkalineControl.c`, `AlkalineTables.c`, `AlkalineSquareTable.c`, with explicit `-Ofast` rules for the three non-`DSPAudio` `.c` files (as `DrumVoice.o`). `AlkalineParameters.c` goes in `SRCS` |
| `MIDI_NRPN_TABLE.md`, `INSTRUMENTS_DSP_REFERENCE.md` §3.1/§3.2/§5/§8, `FILESYSTEM_SPEC.md` (keys) | Documentation |

---

## 8. Budgets

| Resource | Figure |
|---|---|
| **CPU per sample** | B-R loop 230 + oscillator 24 = 254, 3 VDIV (standalone loop counts) |
| **CPU per block (measured S093, static longest path on M7 code)** | **Drum: ≈ 413 instructions** (`Drum_calcVoiceAsync` 63 + `slopeEg2_calc` 28 + 2 × `osc_setFreq` 139 + dispatch 14 + filter tangent ≈ 30; firmware `build/lxr02.elf`). **Alkaline: ≈ 3,010** (async 115; MEG 713 incl. 8 `logf`, 2 `expf`, `log1pf`, `exp2f`; accent network 812 = 144 + 4 × 167; expo 313; VEG 45; prewarp ≈ 132 incl. `tanf`; setNet 82; slow network 547 = 155 + 8 × 49; `osc_setBaseNote`/`osc_setFreq`/`log2f`/`exp2f` ≈ 254), plus ≈ 25 VDIV |
| **Render entry/exit per block (measured S093, standalone objects)** | Drum `Drum_calcVoiceSyncBlock` 163 (its callees' entries not counted); Cymbal `Cymbal_calcSyncBlockVoice` 134; Alkaline `dlv_render` (B-R kernel) 113 + `Alkaline_calcVoiceSyncBlock` 19. None of these is in any audio-loop figure |
| **Cymbal/HiHat per block** | `Cymbal_calcAsyncVoice` 26 + 3 × `osc_setFreq` 139 + `slopeEg2_calc` 28 + snap, filter tangent and dispatch ≈ 54 ≈ **525** |
| **Reproduce the per-block count** | Link `DiodeLadder.c`, `AlkalineControl.c`, `AlkalineVoice.c` and the generated tables with stubs for the oscillator calls, `-Ofast` with the firmware flags and `-specs=nano.specs -specs=nosys.specs -lm`. Then `tools/dsp_test/elf_blockpath.py` and `elf_looppath.py` on each function, multiplying loops by their iteration counts and adding each `bl` callee's own path. Drum: the same tools on `build/lxr02.elf` (`Drum_calcVoiceAsync`, `slopeEg2_calc`, `osc_setFreq`, `instrumentManager_calcSlotAsync`) |
| **Total per sample** | See §3A.4: Drum ≈ 275+, Cymbal/HiHat ≈ 290+, B-R + Tier 1 + Tier 2 ≈ 285–288, A-R ≈ 261–264. As built (no cuts): B-R ≈ 348 |
| Flash | `DiodeLadder.o` about 4.5 KB (both kernels); `AlkalineControl.o` 3,200; `AlkalineVoice.o` about 0.9 KB; N1 tables 3,888 + VEG and prewarp about 300; square table 22,528; parameters and menu about 2–3 KB. **About 37–38 KB against 40,980 B headroom**: without P2-0 only about 3 KB would remain, well below the 16 KiB `link_budget` warning. **With P2-0 (Re3/Sw3 removed), about 48 KB remain** |
| RAM | `AlkalineVoice` about 500 B inside the existing slot union (no growth). `menu_instrumentLoadIndex` +2 B (sign-off) |
| ITCM | 0 (the oscillator reuses the existing ITCM table loop; the ladder runs from flash) |

---

## 9. Decisions to confirm at the start of the session

| # | Item | Proposed |
|---|---|---|
| **R-1** | **Filter and class** | **Decided (user, S093): B2R as Basic** (§3.5, §3B): ≈ 264–267 against Drum ≈ 275+. Alternatives kept measurable: A-R (Basic, ≈ 261–264), B-R (Advanced, ≈ 285–288) |
| R1 | `menu_instrumentLoadIndex` +2 B, `.bss`, SRAM1, owner `menu.c` | Approve (the only new RAM) |
| D14 | Re3/Sw3 removal renumbers user samples (S093 P0 §10.2) | Accept (no migration) |
| D16 | Drive mapping ×1..×8 as `8^(v/127)`, default 0 (×1) | Accept |
| P2-0 order | Re3/Sw3 removal before the Alkaline integration | Yes (flash) |
| D1 | Accent at velocity **≥ 101** | **Decided (user, S093)**; done in `AlkalineVoice.h` and the AL-S law |

---

## 10. Verification and gates

1. **Bench** (`make -C tools/dsp_test alkaline alkaline_tables special_tags oscillator`):
   - **AL-R:** ring 2.0 ms ± 3 %, decay 0.364 ± 0.03 (B-R gives 0.372).
   - **AL-AB:** B2R against B-R and against A-R within 0.3 dB at ×1
     (measured at ×1: their levels within 0.03 dB of each other, §3B.2).
   - **New AL-D (drive):** at ×8, bounded, no NaN, output within full
     scale, the input-pair tail active.
   - **AL-S:** unchanged.
   - **Break build** kept.
   - `make all` stops at `run_modes` today (S093 P0 O7): run the targets
     individually, or fix O7 first.
2. **`tools/alkaline/check_c.py`:**
   - C1 against SPICE and the Python B_rt: B-R is N1, so compare with
     `run_variants.py`'s `B_n1rq_ota32` instead (LSD60 against golden
     ≈ 0.19 at ×1).
   - C2 pattern within S093 P0 §5.4's figures ± 0.3 dB.
   - Add drive cases ×4/×8.
3. **ARM:**
   - `fpseq.py` (or `run_variants.py`'s loop counter) on `DiodeLadder.o`:
     **`DiodeLadder_renderB2` ≈ 209, 3 VDIV** (B-R ≈ 230, A-R ≈ 206 for
     reference).
   - After `make all`: `elf_looppath.py --elf build/lxr02.elf --sym DiodeLadder_renderB2`
     (the linked count).
   - The per-block count after Tier 1 + Tier 2 (§3A.4, §8 method), and the
     total against Drum.
4. **Mixer:** `make -C tools/dsp_test elf86_mixer_check` and
   `elf_hotloops.py`: 0 loops grew in the other engines (Alkaline's render
   is `noinline`).
5. **Flash:** `make all` runs `link_budget.py`; record the headroom.
6. **Device:**
   - An Alkaline track plays the S092 P4 pattern. Compare with
     `tools/alkaline/alk_c_acid_B.wav` (the S093 P0 render; B-R differs by
     about 0.1–0.2 dB).
   - Worst-case Scene with **Alkaline in every Basic slot** (four), at its
     dearest settings, the two Advanced slots at theirs. The `cpu` widget ≤
     baseline and 0 underruns (S092 P0 §1.3). At a small margin, an excess
     too small to see once shows four times over. If Alkaline goes
     Advanced, the same test with Alkaline in both Advanced slots.
   - Listen at drive ×1/×4/×8, **low resonance at high drive first** (B2R's
     weak spot, §3B.3), against `tools/alkaline/alk_drive_*` renders.

---

## 11. After the listening trial (the user's options)

All figures are totals per sample (audio loop + per-block ÷ 32 + render
entry/exit + SRR), the same accounting on both sides (§3A.4).

| Path | What it means |
|---|---|
| Stay Basic | **Chosen: B2R** (≈ 264–267). Further options if needed: A-R (≈ 261–264; brighter when driven). Full B-R (≈ 285–288) does not fit under Drum |
| Advanced (replace Cymbal/HiHat in a Scene) | B-R with Tier 1 + Tier 2 fits under Cymbal/HiHat (≈ 290+) by about 3–6. B-R without the [3/2] OTA (+7) would be ≈ 292–295, about level. Post-distortion/FX types (E1: ≈ +22–25, +1 VDIV) do not fit inside that margin without further cuts. Cymbal/HiHat's audio-loop figure (≈ 260) is from DSP ref §8.2 and is not re-measured; measure it before relying on this |

---

## 12. Remaining work and risks: the complete list

All known items, including those that are not code. Nothing else is known
to be outstanding.

### 12.1 Risks to the "fits under Drum" goal

| # | Item | Why it matters | What is done about it |
|---|---|---|---|
| R-1 | **Margin.** B2R is ≈ 264–267 against Drum ≈ 275+ (8–11 to spare, §3A.4); full B-R did not fit, so the user chose B2R (S093) | The margin rests on R-2/R-6 (Drum not fully re-measured) and on Tier 1/Tier 2 reaching their estimates (R-5) | Measure first (R-2, R-6); if B2R comes out over: A-R as Basic (−3), or Alkaline as Advanced with B2R or B-R |
| R-2 | **Drum's 247 per-sample figure was never re-measured** on the current image. S092 P0 §1.1 required a freeze before any Alkaline code; it was not done | The comparison rests on S089-era component figures | First task of P2: measure Drum's dearest per-sample loops on `build/lxr02.elf` (`elf_hotloops.py`/`elf_looppath.py`) and freeze them |
| R-3 | **All Alkaline costs are standalone or static estimates** (loop counts, longest static paths); LTO can add or remove a few | The decision depends on single digits | The linked count and the device `cpu` widget are the gate (§10) |
| R-6 | **Render entry/exit and callee entries:** Drum's render callees' per-block entries (the ZDF filter, the oscillator blocks, the post-chain) are not yet counted, so Drum's ≈ 275 is a lower bound | B-R's excess may be a few less than 10–13 | Count them with `elf_blockpath.py` on the standalone objects (`ResonantFilter.o`, `Oscillator.o`, the post-chain) as for Alkaline |
| R-4 | **Control-path per-block cost was left out of every comparison until now** (S092 P0 §1.1 called it "shared"; S093 P0/P1 did not add it) | It was the 94-per-sample surprise | Now measured and in §8 and §3A. Every later comparison must include it |
| R-5 | **Tier 2 cuts may fail their SPICE or listening checks** (T2-1, T2-4 above all) | Each failure adds back its saving (T2-1 ≈ +2, T2-2 ≈ +8, T2-4 ≈ +4 per sample) | Revert that cut; see R-1 |

### 12.2 Behaviour and sound items not yet decided or tested

| # | Item | Detail |
|---|---|---|
| B-1 | Accent threshold against the default step velocity (100) | **Resolved (user, S093): accent at velocity ≥ 101**, so un-edited steps are not accented. Done in `AlkalineVoice.h` and the AL-S law |
| B-2 | **Output level against the other engines: a trade-off** | Alkaline's output is pin-6 volts at a 7 V full scale (D17), chosen so drive ×8 (OTA up to ≈ 6.7 V) does not clip. At ×1 the 303 peaks at about 1.7 V, about −12 dB below full scale, so it sounds quieter than a Drum at the same `vol`. A fixed make-up gain costs nothing (it folds into the scale), but every dB of it is a dB of headroom lost at high drive: +6 dB clips drive above about ×4. **Options:** (a) no make-up, quieter at ×1; (b) make-up that falls as drive rises (`gain = f(drive)`, once per block, costs nothing per sample), level at ×1 and full headroom at ×8; (c) a fixed make-up with the drive range reduced. **Recommended: (b).** User choice |
| B-3 | Square wave under drive not run in the variant tests | S093 P1 P1-O3 |
| B-4 | High cutoffs above about 6 kHz: the 1× ladder's shape near Nyquist (0.6–1.9 dB in band) | S092 P6 O9; listening |
| B-5 | Track 7 alternate (slot 6) has no effect on Alkaline | S092 P0 §5.5 mapping to MEG decay deferred |
| B-6 | MIDI note-off does not end the gate; MIDI and live notes use the current BPM's step length | P0 E4 extra, not in the prototype |
| B-7 | Rolls: each roll hit is a trigger with a full 7/12-step gate, which overlaps the next roll hit | Untested; listen |
| B-8 | Step length ignores shuffle and per-step timing offsets | **External clock: closed.** `seq_tempo` follows the incoming clock (`sequencer.c` ≈ l. 6611, `sync_calcBpm()`) |

### 12.3 Integration work beyond §7 (also needed)

| # | Item |
|---|---|
| I-1 | **Storage:** the `.alk` type end to end. `/Instrument/Alkaline` directory creation at boot (the boot-index stage), `.hcindex` per type, HCNAMES type fields, Load/Save type stepping (open item M1 in MEMORY.md touches it), the `kit` snapshot (`pm_instrument_snapshot`, 129 B: check it holds 26 Alkaline rows), AutoSave restore. S092 P0 §5.5 lists the tests |
| I-2 | **Closed:** instrument types are stored as text tokens (`storage_instrumentTypeToText()`; AutoSave, `.hcnames`, `midilrn.cfg`), so UNKNOWN's numeric change is not stored data |
| I-3 | **Scene type selection:** `instrumentManager_typeSelectableForSceneSlot()` and the Basic/Advanced count, checked with the new type |
| I-4 | **MIDI NRPN mapping** for the new rows (`MIDI_NRPN_TABLE.md`, and whatever code assigns NRPN numbers from descriptors) |
| I-5 | **Mixer baseline:** `elf86_mixer_check` compares against a P2 baseline. A new engine changes `mixer_calcNextSampleBlock()`, so the check reports which loops moved; refresh with `elf86_mixer_snapshot` only after confirming no other engine's loop grew |
| I-6 | **Bench pre-existing failures** (S093 P0 O7): `run_modes` does not compile and `stock_tables` has a path bug, so `make -C tools/dsp_test all` stops early. Fix before relying on it as a gate |
| I-7 | **Re3/Sw3 removal** (P2-0, S093 P0 §10): renumbering, bench and docs; needed for flash |
| I-8 | **Documentation:** `INSTRUMENTS_DSP_REFERENCE.md` (§3, §5, §8), `FILESYSTEM_SPEC.md` (the keys), `MIDI_NRPN_TABLE.md`, `OPEN_TESTS.md` (device tests), the session log |

### 12.4 Sign-offs needed (unchanged from §9)

R-1 is decided (B2R as Basic). Still open: R1 (+2 B RAM), D14 (renumbering),
D16 (the drive mapping) and **B-2** (the output make-up trade-off). B-1 is
resolved (accent at ≥ 101).
