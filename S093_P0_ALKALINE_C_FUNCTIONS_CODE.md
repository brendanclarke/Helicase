# S093 P0 — Alkaline C Functions and Code (prototype drafting)

- **Session:** 093, phase 0. This is S092 P0's phase P2 ("DSP core"), the
  first step towards **S093's goal: a prototype Alkaline instrument
  running on the device**.
- **Inputs:**
  - the plan, `S092_P0_ALKALINE_INSTRUMENT_PLAN.md`;
  - the validated DSP model, `S092_P6_DSP_EQUATIONS.md`, with its Python
    reference in `tools/alkaline/`;
  - the circuit models S092 P1–P4.
- **What this is:** the C engine drafted from the S092 model, tested on the
  host against SPICE and against the Python model, and measured for M7 cost.
  It also sets out the remaining steps to an on-device prototype and the
  decisions they need.
- **Status (2026-11-07):**
  - **Done:** the DSP core, the control path and the voice engine in C;
    the generated tables; a bench module with laws and a break build; a
    cross-check against SPICE and the Python model. Everything compiles
    warning-free for the M7 with the firmware flags.
  - **Not done:** nothing is wired into the firmware build yet
    (InstrumentManager, parameters, menu, Makefile). That needs the
    decisions in §8.
  - **Main finding:** the engine is correct. Option A fits the Advanced
    (Cymbal/HiHat) cost envelope today and is 12 instructions over the
    Basic (Drum) ceiling; option B fits neither (§6.5).
  - **Re3/Sw3:** to be removed from the firmware entirely (user, S093),
    with their specification kept in the project for a later return; plan in
    §10. Alkaline keeps its own 303 square table.
- **Decisions used:**
  - D1: velocity ≥ 100 is an accent (your original request);
  - D8: the square table per octave level from SPICE;
  - D9: one-block release;
  - D10: Roland VEG;
  - D11: gate 3.5/6;
  - D4 (recommended, not yet confirmed): no SRR.
- **Project rules kept:**
  - no new RAM outside the existing slot union (§6.4);
  - `ResonantFilter.c` and `Oscillator.c` untouched (the oscillator's
    existing ITCM table loop is called as a leaf);
  - constant per-sample cost;
  - no commits.

---

## 0. Summary

| Item | Result |
|---|---|
| C against the Python model, ladder alone, identical inputs | Level **0.001–0.01 dB**, LSD **0.02–0.05 dB** (bins within 60 dB of the peak) |
| Whole C voice playing the P4 pattern, against the Python model | Level −0.05 to −0.11 dB, LSD 0.27–0.50 dB (the stock `sawTable` against SPICE's own cycles, §5.3) |
| Whole C voice against the SPICE per-step table | The same figures as S092 P6 V5: cutoff peaks +20 to +60 cents, accent VCA peaks within 4 %, output per step within 0.31 dB (acid) |
| Calibration ring ([SN] p7), on the OTA output | 2.018 ms; second peak / first 0.369 (B), 0.367 (A); golden RK4 2.018 ms / 0.364 |
| Option A against B at the 303's drive | −0.03 dB |
| Bench laws AL-B, AL-R, AL-S, AL-N, AL-AB; the break build | All pass; the break (legato ignored) is caught |
| **Measured cost per sample** (standalone `-Ofast` object, `fpseq.py`) | **B 323 instructions + 3 VDIV; A 245 + 2 VDIV.** With the oscillator (24): **B ≈ 347, A ≈ 269**. Drum (Basic) ≈ 257 and Cymbal/HiHat (Advanced) ≈ 270 per slot, both with 4 VDIV (§6.5) |
| Flash (all new objects) | 35,784 B, of which Alkaline's 303 square table is 22,528 B. Headroom is 40,980 B; removing Re3/Sw3 (§10) gives back 45,056 B |
| RAM | `AlkalineVoice` 508 B (`DiodeLadder` 220 B), inside the 1,264 B runtime slot. No new allocation |

**Key findings:**

1. **The C reproduces the model.** Every difference against SPICE is the
   model's own residue (S092 P6 §6.5), not the port.
2. **Cost is register pressure, not arithmetic.**
   - B's loop does about 198 floating-point operations, close to the S092
     P6 hand count (180).
   - It also spends 57 stack reloads and spills, about 37 register moves
     and 20 constant loads per sample, because about 68 floats are live
     against 32 registers.
   - A is 245, of which about 150 are arithmetic.
   - §6.3 lists the reductions, aimed first at A.
3. **Two behaviours were fixed while porting:**
   - **Gate length** must be counted in samples, not whole blocks: one
     block of rounding cut a saw spike and moved step 1 by 0.7 dB.
   - **SPICE's VCA-current trace** carries audio-rate feedthrough that a
     control-rate current cannot (§5.3).
4. **Flash is solved by removing Re3 and Sw3 (§10).** That frees
   45,056 B; with Alkaline in (35,784 B) and its parameter and menu code,
   about 47 KB of headroom remains.
5. **Option A's largest costs** are the ladder solve itself (141 of 245)
   and the network's per-sample part (57, half of it spills); §6.1.1.

---

## 1. Files and how to run

| File | What |
|---|---|
| `Core/DSPAudio/DiodeLadder.h/.c` | The real-time ladder, network and OTA: `DiodeLadder_renderA()`, `DiodeLadder_renderB()`, `DiodeLadder_setNet()`, `DiodeLadder_init()`; the `DiodeLadderNet` layout |
| `Core/DSP/Instruments/Alkaline/AlkalineControl.h/.c` | Control rate: `alk_expoCurrent()`, `alkMeg_*`, `alkAcc_step()`, `alkVeg_*`, `alk_prewarp()` |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.h/.c` | The engine: `Alkaline_initVoice()`, `_triggerVoice()`, `_calcVoiceAsync()`, `_calcVoiceSyncBlock()` |
| `Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c`, `AlkalineSquareTable.c` | **Generated, do not edit.** The Res table, prewarp table, VEG table and fitted constants, plus the D8 square |
| `tools/alkaline/gen_alk_tables.py` | Generator. `--check` proves the committed files are its output |
| `tools/dsp_test/modules/alkaline.c` | Bench module: laws AL-B/R/S/N/AB, the `--traces` and `--pattern` modes |
| `tools/dsp_test/Makefile` | Targets `alkaline` (with one break build) and `alkaline_tables`; both added to `all` |
| `tools/dsp_test/extract.py` | `--object` now also matches multi-dimensional arrays (`sawTable[11][1024]`); every existing target gives the same result as with the original (checked) |
| `tools/alkaline/check_c.py` | C1 and C2 cross-checks (§5). Writes `check_c_results.json` and `alk_c_*.wav` |

```
python3 tools/alkaline/gen_alk_tables.py          # regenerate (needs numpy)
make -C tools/dsp_test alkaline alkaline_tables    # laws + break + table check
python3 tools/alkaline/check_c.py                  # needs the e2e traces (tools/spice/tb303/e2e.py)
```

**ARM cost** (until the files are in the firmware build), from the
repository root:

```
mkdir -p build/s093
arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast -std=gnu11 \
  -I. -ICore -ICore/DSPAudio -ICore/Hardware -ICore/MIDI -ICore/compat \
  -c Core/DSPAudio/DiodeLadder.c -o build/s093/DiodeLadder.o
python3 tools/dsp_test/fpseq.py --obj build/s093/DiodeLadder.o \
  --ref DiodeLadder_renderB --new DiodeLadder_renderA --report --all
```

---

## 2. Architecture

### 2.1 One block, in the platform's order

```
voiceControl_processPending()  ─► Alkaline_triggerVoice(v, velocity, note, stepSamples)
                                     accent, legato, MEG strike / VEG trigger, gate + accent counters
instrumentManager_calcSlotAsync ─► Alkaline_calcVoiceAsync(v)
                                     lanes (gate, ACCENT) ─► pitch: osc_setBaseNote, slide lag, Tuning,
                                     osc_setFreq ─► MEG ─► accent network ─► expo law ─► VEG
                                     ─► iLadEnd (prewarped), iAbcEnd ─► DiodeLadder_setNet(Res)
instrumentManager_calcSlotSyncBlock ─► Alkaline_calcVoiceSyncBlock(v, buf, 32)
                                     calcWavetableOscBlock(osc, sawTable | alkSqrTable, buf)   [ITCM, existing]
                                     DiodeLadder_renderB(ladder, buf, volts/LSB, iLadEnd, iAbcEnd)
                                        per sample: ladder + network fast part + OTA ─► buf (int16)
                                        per block:  slow network update
```

- The output is pre-volume. The mixer applies `vol` and `pan`, as for every
  engine (S072).
- No state lives outside the voice struct. Every per-sample loop is
  branch-free and identical for every setting.

### 2.2 Data

| Struct | Bytes (ARM) | Contents |
|---|---:|---|
| `DiodeLadder` | 220 | Ladder x0–x3; seven secant gains; the C27/C25 state; fast modes and asymptotes; ten slow states and their outputs; the previous sample's values; the current ramps; the per-block coefficient set and the two Res-table pointers |
| `AlkalineVoice` | 508 | `OscInfo` 72, `DiodeLadder` 220, MEG, accent network, VEG, `Lfo` 124, the knob floats, the sequencing state, the per-block results |
| `DiodeLadderNet` (flash) | 464 per point | The slow block system (Φ 8 × 8, Γ 10 × 2, outputs 2 × 8), fast modes, asymptote and instantaneous gains |

### 2.3 Interface for the integration

| Function | Called from | Notes |
|---|---|---|
| `Alkaline_initVoice(v)` | `instrumentManager_resetRuntimeSlot()` (after the slot `memset`) | Sets coarse 36 (C2) with the fine byte at its centre (63), knobs at 0.5, Tuning centred |
| `Alkaline_triggerVoice(v, velocity, note, stepSamples)` | `instrumentManager_triggerTrack()` | `stepSamples`: the track's step duration in samples, from a new read-only sequencer query (§7.2). The platform part of a trigger (LFO retrigger, velocity nodes) stays in InstrumentManager, as for the other engines |
| `Alkaline_calcVoiceAsync(v)` | `instrumentManager_calcSlotAsync()` | |
| `Alkaline_calcVoiceSyncBlock(v, buf, 32)` | `instrumentManager_calcSlotSyncBlock()` | Mark `noinline` at integration (S092 P0 §1.3, the LTO risk) |

---

## 3. The DSP core (`DiodeLadder.c`)

### 3.1 What it computes

The kernel is the S092 P6 §5.4 pseudo-code, line for line against
`alk_model.py` (`LadderTPT._ladder_step`, `_process_block`, `SplitNet`):

- the TPT diode ladder with term L, the Q21 coupling and its tanh (§3.2–3.3
  there);
- the loop closed by one division through the network's instantaneous gain;
- the network's per-sample part:
  - D·w;
  - two fast modes (C21/C22, unit input gain);
  - two asymptote one-poles;
  - the block-rate slow output, ramped per sample;
- the slow network once per block, in the physical capacitor basis,
  Res-interpolated as a blend of the two neighbouring points' products (exact,
  because the system is linear in Φ and Γ);
- the OTA: `out = R_OTA · I_abc · tanh(v_vca / 2Vt)`, then int16 at full
  scale 3 V, saturated once.

### 3.2 Constants

The derived constants are float literals in `DiodeLadder.c`. Their
derivations are in the file's comment block and come from S092 P6 §3:

| Name | Value | Meaning |
|---|---|---|
| `DL_K0`, `DL_K1` | 6,090.59, 3,322.14 | w_k = (h/2) · I / (4 Vt C_k) per ampere |
| `DL_KG`, `DL_TWO_G` | 0.995162, 0.009722 | The C27/C25 TPT one-pole |
| `DL_KL`, `DL_D3X` | 0.142460, 0.007330 | Term L in the right-hand side and the diagonal |
| `DL_KA`, `DL_KB`, `DL_A_OUT` | −5.3302, 103.099, 5.3561 | Q21: tp_pre = at · x3 + bt |
| `DL_R_OTA` | 52,526 Ω | OTA load |
| `DL_ASYM_AL`, `_BE`, `_DECAY` | 0.999288, 1.13318e-5, 0.977466 | Asymptote poles at 2π · 5 Hz. The generator asserts `_DECAY` against the network it writes |

### 3.3 Approximations against the Python model

| Python | C | Effect (measured, §5) |
|---|---|---|
| Exact `tanh` | [5/4] Padé `x(945 + 105x² + x⁴)/(945 + 420x² + 15x⁴)`, argument clamped to ±3.5 (error < 2e-5 below 2, ≤ 1e-3 at the clamp) | Within the 0.02–0.05 dB C1 agreement |
| One division per secant | All secant reciprocals batched: one VDIV per sample (B: 7 values, A: 3) | None (exact algebra) |
| Thomas algorithm (4 divisions) | Continuants θ1–θ4 with one shared reciprocal (B); for A, the next sample's θ batched with the secants | None (exact algebra) |
| OTA in the same sample | One sample late (its tanh shares the batch) | A 23 µs delay of the output |
| Prewarp per sample | Per block at the block-end current, then ramped | Within the ramp |
| Network coefficients exact in k | 9-point Res table, linear interpolation | +0.016 dB LSD (S092 P6 §5.3) |
| Ladder and VCA currents per sample | Linear ramps per block (the firmware's control rate) | See §5.3 |

### 3.4 Options A and B

They are two separate functions, so each can be measured and the other
deleted (P0 §4: no switch in a loop).

- **B:** all six nonlinearities, 3 VDIV.
- **A:** linear rungs, 2 VDIV. With t = 1 the tridiagonal depends only on
  the ladder current, so the next sample's pivot reciprocals join the
  secant batch.

---

## 4. Control path and sequencing (`AlkalineControl.c`, `AlkalineVoice.c`)

### 4.1 The control path

These are ports of S092 P6 §4, with the same constants, the same iteration
counts and the same diode-state order:

- **expo law:** three fixed-point passes on Q11's Vbe;
- **MEG:** the strike; the discharge into the diode stack with four
  fixed-point passes; the Darlington softplus;
- **accent network:** a 5 × 5 nodal solve. **All four diode states are
  solved every block** for constant cost; the first consistent one is kept;
- **VEG:** a C42 state with a 64-point current table;
- **prewarp:** `tan θ / θ` with an 11-point c(k) table.

They use libm (`expf`, `logf`, `log1pf`, `exp2f`, `tanf`) at control rate,
about 20 calls per block. §6.2 covers their cost.

### 4.2 Sequencing rules in C

| Rule | Implementation | Source |
|---|---|---|
| Accent | `velocity >= 100` | D1 (your request) |
| Legato | The previous note was slide-flagged **and** its gate is still open | P0 §5.3 |
| Slide latch | `slideLatch = slide` at each trigger; it decides the **next** note | P0 A10 |
| Gate | **In samples:** `7/12 × stepSamples` (rounded); a slide-flagged note holds `stepSamples + gate`. The counter drops by 32 per block, so the gate is open for every block that starts before its end, as in S092 P6 V5 | D11. Changed from P0 §5.4's whole blocks; see §5.3 |
| ACCENT lane | High for one step (`stepSamples`), counted the same way | S092 P4 E3 |
| Pitch | `osc_setBaseNote()` (coarse + note − 63, fine) each block, then the slide lag in log2 Hz with keep = e^(−0.7255/22) = 0.967561 while gliding, plus Tuning ±700 cents (`(v − 63)/63 × 7/12` octave) | P0 A9, A13, D6 |
| Coarse default | 36 (C2 at note 63), fine byte 63 (centre) | P0 §5.2 |
| No phase reset, no filter clear on a trigger | — | P0 A1, A2 |

Tuning is a plain float binding converted at control rate. No new special
writer is needed (P0 §5.2 had proposed `IM_SPECIAL_ALK_TUNE`).

---

## 5. Validation

### 5.1 Bench laws (`make -C tools/dsp_test alkaline`)

The real `DiodeLadder.c`, `AlkalineControl.c`, `AlkalineVoice.c` and
generated tables are built over the whole real `Oscillator.c` and the real
`sawTable` (`extract.py`, by name).

| Law | Checks | Result |
|---|---|---|
| AL-B | No NaN and bounded state across 32 knob patterns × 2 waves × 2 options. Minimum-to-maximum steps every 50 blocks; triggers at random notes and velocities; slide toggling | 0 failures |
| AL-R | [SN] p7 calibration on the OTA output: period 2.0 ms ± 3 %; second ring peak / first = 0.364 ± 0.03 (golden RK4, same metric) | B 2.018 ms / 0.369; A 2.018 / 0.367 |
| AL-S | Accent at 100, not 99. Gate 3,217 samples at a 5,514-sample step; slide holds 8,731. A note after a slide glides with no MEG strike and no VEG trigger; the glide's one-block step is exact. The slid-to note's own flag decides the next note. Slide into a rest: the gate closes and the next note is not legato | 0 failures |
| AL-N | `setNet()` at a grid point reproduces the row; K_u0 is continuous across points | 0 failures |
| AL-AB | A against B over a 1,400-block pattern at the 303's drive | −0.029 dB |
| Break | The legato rule removed (every note retriggers) | Caught by AL-S |

### 5.2 C1: the ladder on SPICE's own signals (`check_c.py`)

The inputs are the SPICE end-to-end run's saw, ladder current and VCA
current. The output is compared with SPICE and with Python B_rt (given the
same block-ramped currents):

| Run | C B against Python B_rt | C B against SPICE, level / worst step |
|---|---|---|
| acid (Res 0.85) | +0.001 dB; LSD 0.163 dB (0.046 within 60 dB of the peak) | −0.06 dB / 0.16 dB |
| lowres (Res 0.2) | +0.010 dB; LSD 0.070 dB (0.019) | −0.02 dB / 0.13 dB |

### 5.3 What the cross-checks taught

1. **Gate rounding.**
   - A 16th at 120 BPM is 172.29 blocks. A gate counted in whole blocks
     (100) closed 0.4 ms earlier than the model's.
   - At C2 that cut a saw-reset spike, and step 1 played 0.73 dB low.
   - Counting in samples (§4.2) fixed it: step 2 against step 1 now reads
     +6.84 dB, against SPICE +6.76 and Python +6.81.
2. **SPICE's I_abc is not a control signal.**
   - The measured VCA current (`i(VABC)`) carries audio-rate pin-1
     feedthrough.
   - Multiplying by it adds energy at 2–8 kHz, 50–60 dB below the peak.
     The Python V4 harness used it per sample.
   - A control-rate current cannot carry it. With block-ramped currents
     the Python model loses the same 5–19 dB in that band as the C.
   - So the C1 distance to SPICE above 2 kHz (LSD 4.7–12 dB within 60 dB)
     is this feedthrough, not the port. Levels per step are within
     0.16 dB.
3. **The oscillator.**
   - The C plays the stock `sawTable`. The Python model played SPICE's own
     cycles.
   - Their harmonics agree within 0.12 dB up to the band limit. Each
     table level rolls off earlier near its top (−0.7 dB at harmonic 64
     of level 2).
   - That, plus event timing, is the C2 residue: 0.27–0.50 dB LSD against
     Python, with levels −0.05 to −0.11 dB.

### 5.4 C2: the whole voice on the P4 pattern

The control results are identical to S092 P6 V5, against the SPICE table:

| Step (acid, B) | Note | Cutoff peak / end against SPICE | VCA peak, C / SPICE (µA) | Output re step 1, C / SPICE (dB) |
|---:|---|---|---|---|
| 1 | C2 | +50 c / +5 c | 45.6 / 46.1 | 0 / 0 |
| 2 | C2 A | +48 c / −25 c | 127.9 / 124.1 | +6.84 / +6.76 |
| 3 | D#2 S | +22 c / 0 c | 45.6 / 50.3 (SPICE's gate-on spike) | +2.86 / +2.76 |
| 4 | G2 legato | −3 c / −10 c | 43.3 / 43.8 | +0.03 / −0.12 |
| 5 | C3 A S | +48 c / −25 c | 128.3 / 123.6 | +9.59 / +9.53 |
| 6 | A#2 A legato | −28 c / −28 c | 48.8 / 51.6 | +0.44 / +0.75 |
| 7 | rest | — | 0 / 0.006 | silent / −48 |
| 8 | C2 A | +44 c / −26 c | 128.6 / 123.4 | +7.02 / +6.95 |

Every output step is within 0.31 dB of SPICE. The lowres run and option A
are in `check_c_results.json`; A is within 0.02 dB of B on every step.

Renders: `tools/alkaline/alk_c_{acid,lowres}_{B,A}.wav` (the C voice
playing the pattern) and `alk_c_traces_*.wav`.

---

## 6. Cost and budget

### 6.1 Per sample, measured

Standalone `-Ofast` M7 object, `fpseq.py --report`, the per-sample loop
only:

| | B | A |
|---|---:|---:|
| Arithmetic (add, sub, mul, FMA, min/max, conversions) | ≈ 198 | ≈ 149 |
| VDIV | 3 | 2 |
| Stack reloads / spills | 41 / 16 | 34 / 10 |
| Register moves (`vmov`), immediates | 37, 4 | 22, 5 |
| Constant loads (literal pool) | 20 | 20 |
| **Total instructions** | **323** | **245** |
| + oscillator (existing table loop) | 24 | 24 |
| **Voice per sample** | **≈ 347** | **≈ 269** |
| Drum ceiling (S089) | 247 + 4 VDIV (257 if Alkaline drops SRR) | |

**Neither option fits yet.**
- A is about 12–22 over the ceiling.
- B is about 90–100 over.

On the M7 a load, store or `vmov` usually costs one cycle or less (it can
dual-issue), so the cycle gap is smaller than the instruction gap. The
binding gate is still the linked count and the device's `cpu` widget
(P0 §1.3).

#### 6.1.1 Option A by component (measured)

Each figure is the loop count with that component taken out of a scratch
copy of `DiodeLadder_renderA()` (stacked in this order, so they add up;
standalone `-Ofast`, `fpseq.py`). The repository code is unchanged.

| Component | Instructions/sample | Of which loads/stores | Notes |
|---|---:|---:|---|
| Ladder solve: current ramps, right-hand side (with term L), the tridiagonal (next sample's continuants and pivots, batched with the secants), forward and back substitution, the loop division, Q21's output, the C27/C25 state, int16 in/out | **141** | 23 | Holds both VDIVs. Term L costs 0 net |
| Network, per-sample part: two fast modes, two asymptote one-poles, the u base and the v_vca sum | **57** | 28 | The largest add-on; half of it is spills (13 coefficients + 4 states) |
| OTA: its Padé tanh, its share of the batch, × I_abc | **24** | 6 | |
| Input-pair and Q21 secants (their nonlinearity) | **17** | 3 | Q21's argument stays below 0.14 at the 303's drive |
| Slow-network output ramps (u, v) | **6** | 2 | |
| **Total** | **245** | **64** | + oscillator 24 (the existing table loop) |

### 6.2 Control rate

Per block and voice:
- about 20 libm calls (MEG 9, expo 4, tanf, exp2f × 4, log2f, VEG 2);
- the accent network's 4 × 5 × 5 solves;
- the 2 × 104 multiply-adds of the slow network.

That is roughly 4–6 k cycles per block, 3–4 % of a block's 156,700-cycle
budget per voice. This needs measuring on the device.

The cheaper forms are listed in S092 P6 §5.5: warm-started diode passes,
the project's fast log2/exp2, and one accent solve from the previous diode
state.

### 6.3 Reductions to reach the ceiling (next DSP step, S093 P1)

All of these target register pressure first. The estimates are for A; B
gains similar amounts.

| # | Change | Estimated saving | Fidelity risk |
|---|---|---:|---|
| R1 | Drop the s-input asymptote term (`gau1`): its 1/s tail from C17 is below 0.5 % of the input path above 100 Hz. Frees `sL`, `gau1` and two FMAs | 6–8 | Tiny; re-run C1/C2 |
| R2 | Move the whole VCA side to a short second pass: the main loop writes tp_pre to `buf` as int16 (0.16 mV per LSB); the second pass rebuilds v_vca (its own two fast one-poles, q_v, the slow v ramp) and applies the OTA. The main loop sheds the OTA (24) and the v-side network terms, and much of their spilling; the second pass costs about 25 with few live values and adds 1 VDIV (3 in all, still below Drum's 4) or uses a division-free tanh | 20–30 net | None in principle |
| R3 | Accumulate `s` for the slow network in the oscillator-side pre-pass, not in the loop | 2–4 | None |
| R4 | Constants that VFP can encode as immediates; the [3/2] Padé (27, 9 encodable) for the input pair and Q21, whose arguments stay below 0.5 at the 303's drive | 8–12 | Small; measure |
| R5 | Two-sample unroll, so the compiler drops the loop-carried `vmov` copies | 10–20 | None; +1.5 KB flash |
| R6 | Hand-scheduled kernel (inline assembly), as a last resort | 30–60 | None; maintenance cost |

R1–R5 together plausibly bring A to about 200–220 per sample plus the
oscillator, under the ceiling. B needs about 90 off; R1–R6 may not be
enough. That makes D2 effectively **A**, unless the drive extra (E2) is
worth an Advanced-class Alkaline.

### 6.4 Flash and RAM

| Object | Flash (B) |
|---|---:|
| `DiodeLadder.o` (both kernels; deleting one saves about 1.7 KB) | 4,756 |
| `AlkalineControl.o` | 3,200 |
| `AlkalineVoice.o` | 824 |
| `AlkalineTables.o` (Res table 4,176 + VEG + prewarp) | 4,476 |
| `AlkalineSquareTable.o` (D8; Alkaline's own 303 square) | 22,528 |
| **Total so far** | **35,784** |
| Still to come: parameters, menu text, registry, dispatch cases (estimate) | 2,000–3,000 |
| Headroom today | 40,980 |

On its own that would leave about 2–3 KB, well under the 16 KiB
`link_budget` warning. **Removing Re3 and Sw3 (§10) frees 45,056 B**, so
about 47 KB of headroom remains after Alkaline and its parameter and menu
code.

**RAM:**
- `AlkalineVoice` (508 B) lives in the existing runtime slot union
  (1,264 B per slot). The union does not grow, and `_Static_assert`s will
  enforce it.
- The only new RAM in the whole plan is P0 §1.4's
  `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]`, which grows by one
  `uint16_t`: **+2 B in `.bss` (SRAM1, owner `menu.c`)**. That needs your
  sign-off.

### 6.5 Against Cymbal and HiHat (the Advanced types)

There is no frozen per-voice figure for Cymbal/HiHat. Built the way S092 P0
§1.1 built Drum's 247, from the per-loop counts in
`INSTRUMENTS_DSP_REFERENCE.md` §8.1–§8.2 and the S089 log (to be re-measured
on the linked image with `elf_loopdump.py`, as for Drum):

| Stage, dearest setting | Drum | Cymbal / HiHat |
|---|---:|---:|
| Oscillator section | 81 (`fm`, `slp` ramp twin + modulator) | 104 (samples on all three: 27 + 27 + 50, S087) |
| Modulator sum (saturating) | — | ≈ 5 |
| Transient click (+ mix-in) | 27 | 22 (the add is in the post-chain) |
| ZDF filter | ≤ 104, 3 VDIV | ≤ 104, 3 VDIV |
| Post-chain | 35, 1 VDIV | 25 (with the add), 1 VDIV |
| **Voice render** | **≈ 247, 4 VDIV** | **≈ 260, 4 VDIV** |
| + SRR in the mixer | 10 | 10 |
| **Charged to the slot** | **≈ 257** | **≈ 270** |

Alkaline against both (no SRR; the oscillator included). The last column
weights a VDIV at 14 cycles and everything else at 1, a crude cycle
proxy (dual issue makes the real figures lower for all of them):

| Voice | Instructions | VDIV | Cycle proxy |
|---|---:|---:|---:|
| Cymbal / HiHat dearest | ≈ 270 | 4 | ≈ 322 |
| Drum dearest (the Basic ceiling) | ≈ 257 | 4 | ≈ 309 |
| **Alkaline A** | **≈ 269** | **2** | **≈ 295** |
| **Alkaline B** | **≈ 347** | **3** | **≈ 386** |

- **Option A as an Advanced type fits today.** It is level with the
  dearest Cymbal/HiHat in instructions (−1) and has two fewer divisions, so
  it can take an Advanced place in the worst-case Scene without raising it.
  On the cycle proxy it is even below the dearest Drum. As a Basic type it
  is 12 instructions over Drum's 257, with 2 fewer divisions.
- **Option B does not fit either class.** It is about 77 instructions over
  Cymbal/HiHat (one division fewer), and about 90 over Drum.
- The reductions of §6.3 aim at Basic (≤ 257); the Advanced fit needs none
  of them.
- These are standalone and reference figures. The binding gate is still
  the linked mixer check and the device `cpu` widget (P0 §1.3).

### 6.6 What removing Re3/Sw3 changes in cost (§10)

Nothing per sample. Their renders were the shared table loops, which stay
for Tri/Saw/Rec. The dispatch `case SQ3: case SW3:` labels go; that can
change how LTO lays out the dispatchers, so the removal is gated on
`elf_osc_check` (0 changed loops) and `elf86_mixer_check`.

---

## 7. Path to the on-device prototype

### 7.1 Firmware integration (S093 P2)

| Step | Files | Notes |
|---|---|---|
| 1. Type | `InstrumentManager.h` | `INSTRUMENT_TYPE_ALK` before `INSTRUMENT_TYPE_UNKNOWN`. UNKNOWN's numeric value changes: audit stored bytes that might hold UNKNOWN (P0 §5.5; no migration, by policy) |
| 2. Registry | `InstrumentManager.c` `instrument_registry[]` | `{ INSTRUMENT_TYPE_ALK, "alk", alkaline_instrument_display_label, ".alk", "Alkaline", ALKALINE_INSTRUMENT_TYPE_FLAGS, alkaline_param_descriptors, …, alkaline_menu_pages, … }` |
| 3. Runtime union | `InstrumentManager.c` | `AlkalineVoice alkaline;` plus its `_Static_assert` against the slot reserve |
| 4. Dispatch | `InstrumentManager.c`: 19 `case INSTRUMENT_TYPE_HAT` sites | init, trigger, async, sync block, pan, volume, LFO accessor. The filter, amp-EG, pitch-EG, transient and distortion accessors return NULL (Alkaline has none). `instrumentManager_recalcSlotFilter()`: no-op. `instrumentManager_ampEnvelopeQuiet()`: VEG current ≈ 0 and the gate closed |
| 5. Other type users | `SceneData.c`, `filesystem.c` (default Scene stage), `storageTypes.h`, `menu.c` `menu_instrumentLoadIndex` | +2 B RAM (sign-off) |
| 6. Parameters | `Core/DSP/Instruments/Alkaline/AlkalineParameters.c/.h` | Rows as P0 §5.2, all plain offset bindings except `coa` (`IM_SPECIAL_PITCH_COARSE \| OSC1`, existing). `tun` is a TYPE_FLT offset, so no new writer. Platform rows (vol, pan, LFO pair, velocity) as Drum. One row per line; descriptor-count assert; `check_special_tags.py` |
| 7. Menu | Pages `303` (wav coa tun sld), `VCF` (cut res env dec), `Voice` (acc vol pan), LFO, Velocity | New menu text `Saw`/`Sqr` |
| 8. Makefile | `DSP_SRCS` + explicit `-Ofast` rules for `AlkalineVoice.c`, `AlkalineControl.c`, `DiodeLadder.c` (the `Core/DSPAudio` pattern rule covers the last); the tables and parameters in the normal sources; `-ICore/DSP/Instruments/Alkaline` | |
| 9. Type class | `ALKALINE_INSTRUMENT_TYPE_FLAGS` | **Advanced** for the first device prototype (it is over the ceiling, §6.1). Basic only after S093 P1 passes the gate (P0 §1.3) |
| 10. Track 7 alternate | Hidden decay → MEG decay (P0 §5.5) | |
| 11. Docs | `MIDI_NRPN_TABLE.md`, `INSTRUMENTS_DSP_REFERENCE.md` §3.1, §3.2, §5, §8 | |

### 7.2 The step length (the gate's only new input)

- Add a read-only `seq_trackStepDurationSamples(track)`. It is built on
  `seq_getBpm()` and `seq_effectiveTrack*[]`, and never writes the Pattern
  region.
- `instrumentManager_triggerTrack()` passes it to `Alkaline_triggerVoice()`.
  Live presses and MIDI with the transport stopped use the current BPM;
  external clock uses its estimate (P0 §5.4).
- For the very first device build, a 16th at the current BPM is enough.

### 7.3 Prototype gates

1. **Host:** `make -C tools/dsp_test all` stays green (`alkaline`,
   `alkaline_tables`, `special_tags`).
2. **Build:** `make all`, `link_budget.py`, flash ≥ the floor agreed in D3.
3. **Mixer:** `elf86_mixer_check` / `elf_hotloops.py` with 0 loops grown.
   This is the LTO check for the new render, which is `noinline`.
4. **Device:** an Alkaline track plays the P4 pattern. Listen against
   `alk_c_acid_B.wav`. Check the `cpu` widget with Alkaline in a
   worst-case Scene (as Advanced first), and 0 underruns.

---

## 8. Decisions needed

| # | Decision | Recommendation |
|---|---|---|
| **D2** | Filter option, now with measured costs: A ≈ 269 + 2 VDIV, B ≈ 347 + 3 VDIV (Drum ≈ 257, Cymbal/HiHat ≈ 270, both 4 VDIV; §6.5) | **A.** It fits as an Advanced type today and is within reach of Basic after §6.3; identical to B at the 303's own drive (−0.03 dB). B fits neither class |
| **D3** | Flash for the square | **Decided (user, S093): remove Re3 and Sw3 from the firmware (§10); Alkaline keeps its own square table.** The waveform specification stays in the project |
| **D14** | Waveform numbers after the removal | **Renumber** (`OSC_SAMPLE_START` 8 → 6), as the removal implies. Kits that used Re3/Sw3 or modulator sample entries change (§10.2); no migration, by policy. The alternative, keeping 6/7 as unused numbers, avoids that but leaves holes in every wave list |
| **R1** | RAM: `menu_instrumentLoadIndex` +2 B (`.bss`, SRAM1, `menu.c`) | Approve. It is the only new RAM |
| **P1** | First device prototype as an **Advanced** type, while S093 P1 brings the cost under the Basic ceiling | Yes. Option A already fits the Advanced envelope (§6.5); Basic is the target |
| **D4** | SRR on Alkaline | No SRR (confirms P0's recommendation; saves 10) |
| **G1** | Gate counted in samples from the step length (changes P0 §5.4's whole-block gate) | Yes (§5.3 item 1) |

---

## 9. Open items

| # | Item | Next |
|---|---|---|
| O1 | Cost over the ceiling (§6) | S093 P1: R1–R5 on A, re-measure, re-run C1/C2 and the laws |
| O2 | Control-rate libm cost unmeasured | Device measurement; fast log2/exp2 and warm starts |
| O3 | The S092 P6 model residues carry over unchanged (cutoff +20 to +60 cents at peaks, step-6 accent −5 %) | S092 P6 §8 O1–O3 |
| O4 | `sawTable` levels roll off earlier than SPICE's cycles at each level's top | Listen. An Alkaline saw table would cost 22.5 KB |
| O5 | The host build warns on the `sawTable`/`alkSqrTable` const mismatch (host tables are writable); the firmware does not | Cosmetic |
| O6 | The LFO is a same-size stand-in in the host test | The platform LFO is integration (§7.1 step 4) |
| O7 | **Pre-existing bench failures** (the same with the original `extract.py`, so not caused by S093): `run_modes` does not compile (`distortion_curveSampleF` undeclared in the generated `rm_postchain.c`), and `make all` stops there, before `special_tags` and the targets after it; `stock_tables` fails on a path (`tools/Core/DSPAudio/wavetable.c`, the P0 P1 item "fix the gen_stock_tables.py paths"); `voice_control`'s break build prints its expected failures but is not asserted (make exits 0) | Fix in S093 so `make all` is a full gate again. Until then run `special_tags voicefold fold_tables crumpbit crumpbit_tables alkaline alkaline_tables` individually (all pass) |

---

## 10. Removing the stock Re3 and Sw3

**Decision (user, S093):** Re3 and Sw3 are removed. They are not available
in any engine. Their specification stays in the project so they can be
added back later.

Why removal is no loss for Alkaline: today's Re3/Sw3 were built from a
recording of a 303's **filtered** output, with the same harmonic shape at
every octave, so they never were the 303's oscillator waves. Alkaline plays
the real ones: its own 303 square table (`alkSqrTable`, D8) and the stock
`Saw`, which the 303's saw matches within 0.1 dB up to harmonic 32.

### 10.1 What is removed

| Item | Where |
|---|---|
| `sq3Table`, `sw3Table` (2 × 22,528 B) | `Core/DSPAudio/wavetableStock.c` (the file goes), `wavetable.h` externs |
| `SQ3`, `SW3`, `OSC_STOCK_FIRST`, the stock-table list `osc_stockTables[]` and its assert | `Oscillator.h`, `Oscillator.c` |
| The six `case SQ3: case SW3:` labels in the render and frequency dispatchers (`Oscillator.c` lines 308, 442, 816, 1026, 1224, 1348 today) | `Oscillator.c` |
| `OSC_STOCK_LAST` as "the last stock wave": becomes the last built-in wave (`CRASH`) for the wav-row clamp and the menu range | `Oscillator.h/.c`, `InstrumentManager.c` (wave-row maximum) |
| `OSC_SAMPLE_START` 8 → 6 | `Oscillator.h`; users in `Oscillator.c`, `InstrumentManager.c`, `menu.c`, `MenuText.h` |
| `"Re3"`, `"Sw3"` in `waveformNames` (count 8 → 6; the `menu.c` assert follows) | `MenuText.h` |
| Bench: the S086 stock group and the `pos` P1 stock cases; the writable host tables | `tools/dsp_test/modules/oscillator.c`, `osc_host.h` |
| Bench target `stock_tables` (no longer a firmware file to compare) | `tools/dsp_test/Makefile` (re-pointed, §10.3) |

### 10.2 Consequences for stored data (no migration)

- **osc1 `wav` on Re3/Sw3 (6/7):** clamped to the last built-in wave (Cym)
  on load.
- **A modulator `wav` on Re3/Sw3 (6/7):** now selects the first two sample
  entries (`s01`, `s02`), or the last entry if there are fewer.
- **A modulator on `sNN`/`lNN`/`wNN`:** every entry moves two places
  (value v now selects what v + 2 selected).
- **Step automation and Morph** that hold those values follow the same
  shift.
- Re-save affected Kits after the change. Per the standing rule there is no
  converter (D14).

### 10.3 What is kept (the specification)

| Kept | Purpose |
|---|---|
| `tools/plots/stock_wave_harmonics.csv` | The S086 harmonic recipes (Re3, Sw3; also the prepared ReA/TrA/SwA) |
| `tools/plots/plot_stock_waves.py`, `tools/plots/reference/303_square_saw.png` | How the recipes were digitised, and the source capture |
| `tools/plots/gen_stock_tables.py` | The generator. Fix its paths (O7). Its default `--out` moves to `tools/plots/archive/wavetableStock.c`, outside the build, so `--check` and the `stock_tables` bench target keep proving the recipe reproduces |
| `tools/plots/plot_stock_tables.py`, `plot_303_raw_vco.py` | Review plots; they read the archived file |
| The 303's own cycles: `tools/alkaline/alk_constants.json` (`square_levels`), `fit_spice.py`, S092 P1 | For a later 303-authentic Re3/Sw3: per-level recipes from these, normalised per level |
| A specification section | `INSTRUMENTS_DSP_REFERENCE.md` §6.1.2 becomes "Stock table waves: removed in S093", stating the formats, the recipe files, the generator and the restore steps |

**To add them back later:** follow `INSTRUMENTS_DSP_REFERENCE.md` §9.3
("Another stock table wave"): regenerate into `Core/DSPAudio/`, restore the
externs, the constants, `osc_stockTables[]` and the names, and move
`OSC_SAMPLE_START` up again. That renumbers the user samples back, with
the same stored-data effect as §10.2.

### 10.4 Work list

| # | Change | Gate |
|---|---|---|
| W1 | Fix `gen_stock_tables.py` paths; archive its output under `tools/plots/archive/`; re-point the `stock_tables` target | `make -C tools/dsp_test stock_tables` |
| W2 | Remove the tables, constants, list, dispatch labels and names (§10.1); `OSC_SAMPLE_START` 6 | `make all`; `menu.c` assert |
| W3 | Bench: drop the stock group, adjust `pos` P1 and the host tables | `make -C tools/dsp_test oscillator` (incl. its break builds) |
| W4 | Prove the remaining oscillator loops unchanged | `elf_osc_check`: 0 missing, 0 changed; `elf86_mixer_check`: 0 grew |
| W5 | Documentation: DSP reference §6.1, §6.1.2, §6.1.3 (modulator list now stock 0–5), §8; `MenuText.h` comment; plots README; `MIDI_NRPN_TABLE.md` wave ranges | |
| W6 | Flash check | `link_budget.py`: about +45 KB |
| W7 | Device: a Kit using `sNN` modulators re-selected after the shift; menus show six built-in waves | device |

This can go before or after the Alkaline integration (§7). Doing it first
gives the flash room for the prototype without any Alkaline-specific
decision.
