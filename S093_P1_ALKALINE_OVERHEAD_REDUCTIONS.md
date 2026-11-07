# S093 P1 — Alkaline Overhead Reductions and Filter Drive

- **Session:** 093, phase 1.
  - Follows `S093_P0_ALKALINE_C_FUNCTIONS_CODE.md`, which stays as it is.
  - Model: `S092_P6_DSP_EQUATIONS.md`.
- **Goal:** model reductions to the C ladder kernel that save operations with
  minimal effect on the sound, and give Alkaline filter drive. That means
  either emulating drive in option A, or bringing option B's cost within
  the Cymbal/HiHat envelope.
- **Status (2026-11-07):**
  - Modelled and measured; nothing in the firmware tree changed.
  - The reductions are switches of an experimental kernel in
    `tools/alkaline/variants/`.
  - Each is costed on the M7 and heard against the golden model at drive
    ×1, ×4 and ×8.
  - Porting the chosen form into `DiodeLadder.c` is the next step (§7).
- **Result:**
  - A reduced option B ("B-R") costs **237 instructions per sample + 3
    VDIV** (261 with the oscillator). That is **inside the Cymbal/HiHat
    envelope (≈ 270 + 4 VDIV)**, with full drive behaviour, and 4 over the
    Basic ceiling.
  - A cheaper OTA tanh brings it to 230 (254), just under Basic, at a small
    cost in accuracy.
  - Option A gains drive only partly by emulation. The cheapest A ("A-R")
    is 206 (230), comfortably Basic.

---

## 0. Summary

| Form | Loop instr/sample | + oscillator | VDIV | Fits | Error against the golden model, LSD dB: acid ×1 / ×4 / ×8, lowres ×1 / ×4 | Calibration ring (golden 2.018 ms / 0.364) |
|---|---:|---:|---:|---|---|---|
| S093 P0 option B, with the clamp fix (§2.1) | 325 | 349 | 3 | Neither | 0.12 / 0.35 / 0.57, 0.13 / 0.48 | 1.995 / 0.368 (the period is one sample shorter) |
| **B-R** (`B_n1rq`) | **237** | **261** | 3 | **Cymbal/HiHat (≈ 270)** | 0.16 / 0.58 / 0.59, 0.16 / 0.49 | 2.018 / 0.372 |
| B-R with the [3/2] OTA (`B_n1rq_ota32`) | 230 | 254 | 3 | Basic, by 3 (Drum ≈ 257) | 0.19 / 0.69 / 0.64, 0.17 / 0.57 | 2.018 / 0.372 |
| S093 P0 option A, with the clamp fix (`A_xtail`) | 258 | 282 | 2 | Neither (Cymbal/HiHat by instructions +12, with 2 fewer VDIV) | 0.33 / 1.16 / 1.78, 0.07 / 0.68 | — |
| **A-R** (`A_n1q`) | **206** | **230** | 2 | **Basic, 27 spare** | 0.30 / 1.26 / 1.98, 0.10 / 0.88 | 2.018 / 0.370 |
| A1-R, A with rung 0 nonlinear (`A1_n1q`) | 228 | 252 | 2 | Basic, by 5 | 0.14 / 0.94 / 1.56, 0.19 / 0.92 | 2.018 / 0.372 |

**Findings:**

1. **B goes from 325 to 237 in three steps** (each measured in that order):
   - **network form N1 (−26):** the network's two fast modes (C21/C22,
     72–157 Hz) run at block rate, with the per-sample asymptote poles
     moved to 50 Hz (loop) and 120 Hz (VCA input);
   - **the three small changes (−38 together on top of N1):** fewer live
     values make them pay more than they did alone;
   - **the linear Q21 (−24).**

   N1 itself:
   - It moves the output by 0.04–0.07 dB against the current network.
   - It brings the ring decay to 0.631 against SPICE's 0.629 (§5).
2. **The three small changes are nearly free in sound:** cheaper rung
   secants, no rung clamp, and the s-asymptote moved to block rate.
3. **The linear Q21 output stage** costs little in sound (its argument stays
   below 0.32 even at ×8), but it is measurable at ×4 (0.43 → 0.58).
4. **Two corrections are needed for any drive:**
   - the input-pair secant beyond the Padé clamp (+7 instructions;
     without it ×8 drive is 3.5–4.8 dB off);
   - an output full scale of 7 V, because the driven OTA reaches about
     6.7 V.
5. **Drive in option A can only be partly emulated.**
   - Making only the bottom rung nonlinear (A1, where most of the rung
     saturation is) recovers about a third of the gap to B. It costs 22.
   - B-R costs just 9 more than A1-R and is much closer to the circuit.
6. **Rejected as not paying:** splitting the VCA side into a second pass
   (+19), unrolling by two (+8 to +16), updating the rung secants every
   second sample (accuracy loss), holding the slow outputs per block
   (zipper), and the [3/2] Padé on the input pair (accuracy loss).

**Decision (user, S093):** B-R fitted under Drum (with the [3/2] OTA,
254), as a Basic type, for a listening trial (§8).

**Earlier recommendation:** B-R as the Alkaline filter, as an Advanced type now.
Its drive is the circuit's own, and it is 9 instructions inside the
Cymbal/HiHat envelope with one division fewer. Keep A-R as the documented
Basic fallback.

---

## 1. Files and how to run

| File | What |
|---|---|
| `tools/alkaline/variants/dlv_kernel.c` | The experimental kernel. One per-sample step with compile-time switches (§4 table); built for ARM (cost) and the host (sound) |
| `tools/alkaline/variants/dlv.h`, `dlv_common.c` | Its state and per-block helpers (as `DiodeLadder.c`) |
| `tools/alkaline/variants/dlv_host.c` | Host driver: SPICE trace in, OTA output out; the currents applied as block-end ramps (the firmware's control rate) |
| `tools/alkaline/variants/gen_n1.py` → `AlkalineTablesN1.c`, `dlv_n1.h` | The N1 network table (§5) and its asymptote constants |
| `tools/alkaline/variants/run_variants.py` | Builds every variant, counts its per-sample loops, runs the sound cases, writes `variants_results.json` |
| `tools/alkaline/alk_model.py` | `SplitNet` gained per-output asymptote poles (`A_ASYM_U`, `A_ASYM_V`). The defaults are unchanged and `gen_alk_tables.py --check` still matches |

```
make -C tools/dsp_test alkaline                    # the generated host fragments
python3 tools/alkaline/variants/gen_n1.py
python3 tools/alkaline/variants/run_variants.py    # all variants (or names), ~10 s after the first run
```

The first run computes the golden references (RK4 ×8) and caches them in
`<tempdir>/alk_variants`.

---

## 2. Method

**Cost:**
- `dlv_kernel.c` is compiled with the firmware's M7 flags (`-Ofast`, the
  firmware include path).
- The per-sample loop is the largest region closed by a conditional
  backward branch in `dlv_render` (plus `vcaPass` for split variants),
  divided by the unroll factor.
- This reproduces S093 P0's figures (B0 318, A0 246 against 323 and 245).

**Sound:**
- **Inputs:** the SPICE end-to-end run's saw × drive, its ladder current
  and its VCA current, applied as the firmware will (block-end values,
  ramped per sample).
- **Reference:** the golden RK4 ×8 model (`LadderRef`) on exactly the same
  inputs.
- **Window:** −0.05 to 0.6 s, covering steps 1–5 (accents, a slide, a
  legato note).
- **Measure:** log-spectral distance over 30 Hz–8 kHz, in the bins within
  60 dB of the reference's peak (LSD60), plus the level difference.
- **Cases:** acid (Res 0.85) at ×1, ×4, ×8; lowres (Res 0.2) at ×1, ×4.

**Ring:** the [SN] p7 calibration (Res full, saw at C2, 7.375 µA, I_abc
50 µA), on the OTA output: the period and the second ring peak over the
first. This is the bench law AL-R's metric.

### 2.1 Two corrections found on the way

| Issue | Effect | Fix | Cost |
|---|---|---|---|
| The [5/4] Padé clamps its argument at ±3.5. Under drive the input pair's argument reaches 3.6 (×4) and 6.9 (×8), so its secant stays at t(3.5) instead of falling as 1/\|z\| | ×8 drive 3.45 dB (B) and 4.79 dB (A) off the reference | `XIN_TAIL`: divide the secant's denominator by max(\|z\|, 3.5)/3.5, which gives tanh(3.5)/\|z\| beyond the clamp. It shares the batched reciprocal | +7 |
| S093 P0 set the int16 output's full scale to 3 V. A driven OTA reaches R_OTA · I_abc ≈ 6.7 V | Clipping: 5–7 dB of error at ×4–×8 | Full scale 7 V (`VOUT_SCALE`). 16 bits still give 0.2 mV steps | 0 |

Both are in every variant below except B0 and A0.

---

## 3. Drive

### 3.1 What saturates

The largest nonlinearity argument per stage (golden model, acid run;
normalised units, where tanh bends noticeably above about 0.5):

| Drive | Rung 0 (bottom) | Rung 1 | Rung 2 | Rung 3 (top) | Input pair (Q12) | Output stage (Q21) |
|---|---:|---:|---:|---:|---:|---:|
| ×1 (the 303) | 0.44 | 0.18 | 0.14 | 0.13 | 0.93 | 0.07 |
| ×4 | 0.94 | 0.66 | 0.60 | 0.56 | 3.60 | 0.20 |
| ×8 | 1.48 | 1.14 | 1.01 | 0.89 | 6.86 | 0.32 |

- **The input pair** is the dominant drive nonlinearity, and option A
  already has it.
- **The rungs** come next, the bottom one most. This is what A lacks: it
  shows as A's 1.2–2.0 dB error at ×4–×8, against B's 0.4–0.6.
- **Q21** stays nearly linear even at ×8, which is why a linear Q21 costs
  little (§4).

### 3.2 The drive control

- Drive is E2's input gain. In the firmware it scales `sScale` (volts per
  table LSB) once per block, so it costs **nothing per sample** in either
  option.
- The cost of drive is only in what the ladder model can represent, and
  that is the A/B question.

### 3.3 Emulating drive in A

- **A1:** rung 0 nonlinear, rungs 1–3 linear. Rung 0's secant is used one
  sample later, so the next sample's pivots can still share the batch and
  A keeps 2 VDIV.
- **Result:** the error at ×4/×8 falls from 1.12/1.77 to 0.78/1.37 dB (A_n1
  → A1_n1), a third of the way to B, for +22–30 instructions.
- A static waveshaper elsewhere would only add a second input-pair-like
  stage, which A already has. Its missing behaviour is the rungs'
  compression *inside* the loop, which a static shaper cannot reproduce.
- **Conclusion:** emulation gives part of the character at a cost close to
  reduced B. The better route to drive is to make B cheaper.

---

## 4. Reductions: every variant

LSD60 against the golden model (dB). The first two rows are S093 P0's
kernels, without the §2.1 fixes.

| Variant | What | Instr | VDIV | ld/st | acid ×1 | ×4 | ×8 | lowres ×1 | ×4 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| B0 | S093 P0 option B | 318 | 3 | 76 | 0.12 | 0.37 | 3.45 | 0.13 | 0.48 |
| A0 | S093 P0 option A | 246 | 2 | 65 | 0.33 | 1.18 | 4.79 | 0.07 | 0.68 |
| B_xtail | + input-pair tail (§2.1) | 325 | 3 | 77 | 0.12 | 0.35 | 0.57 | 0.13 | 0.48 |
| B_gau1 | the s-asymptote to block rate | 310 | 3 | 75 | 0.12 | 0.37 | 3.45 | 0.13 | 0.48 |
| B_p32rung | [3/2] Padé on the rungs | 300 | 3 | 69 | 0.10 | 0.44 | 3.61 | 0.11 | 0.40 |
| B_noclamp | no rung clamp | 314 | 3 | 77 | 0.12 | 0.37 | 3.45 | 0.13 | 0.48 |
| B_p32io | [3/2] on the input pair and Q21 | 315 | 3 | 74 | 0.13 | **1.08** | 4.05 | 0.06 | 0.45 |
| B_split | VCA side in a second pass | **337** | 4 | 52 | 0.13 | 0.37 | 3.45 | 0.13 | 0.48 |
| B_unroll | two samples per iteration | **334** | 3 | 81 | 0.12 | 0.37 | 3.45 | 0.13 | 0.48 |
| B_half | rung secants every second sample | 308 | 3 | 87 | 0.12 | 0.40 | 3.28 | 0.15 | **0.96** |
| B_r1 | xtail + gau1 + p32rung + noclamp | 286 | 3 | 68 | 0.10 | 0.42 | 0.56 | 0.11 | 0.40 |
| B_r2 / B_r3 | B_r1 + split / + unroll | 305 / 299 | 4 | 46 / 52 | 0.12 | 0.42 | 0.56 | 0.12 | 0.40 |
| B_n1 | xtail + network form N1 (§5) | 299 | 3 | 63 | 0.15 | 0.39 | 0.59 | 0.15 | 0.49 |
| B_q21 | xtail + linear Q21 | 307 | 3 | 76 | 0.14 | 0.52 | 0.54 | 0.14 | 0.52 |
| B_n1r | B_r1 + N1 | 261 | 3 | 54 | 0.14 | 0.43 | 0.55 | 0.14 | 0.41 |
| **B_n1rq (B-R)** | **B_n1r + linear Q21** | **237** | **3** | 52 | 0.16 | 0.58 | 0.59 | 0.16 | 0.49 |
| B_n1rq_ota32 | B-R + [3/2] OTA | 230 | 3 | 47 | 0.19 | 0.69 | 0.64 | 0.17 | 0.57 |
| B_n1rq_hold | B-R, slow outputs held per block | 228 | 3 | 46 | 0.49 | 1.13 | 1.10 | **1.08** | **2.32** |
| B_n1rq_unroll | B-R unrolled | 244.5 | 3 | 57 | 0.16 | 0.58 | 0.59 | 0.16 | 0.49 |
| B_n1rq_half | B-R, rung secants halved | 233.5 | 3 | 62 | 0.17 | 0.58 | 0.68 | 0.18 | 0.81 |
| A_xtail | A + xtail | 258 | 2 | 69 | 0.33 | 1.16 | 1.78 | 0.07 | 0.68 |
| A_r | A + xtail + gau1 + split | 259 | 3 | 39 | 0.33 | 1.16 | 1.78 | 0.07 | 0.68 |
| A_n1 | A + xtail + gau1 + N1 | 224 | 2 | 55 | 0.32 | 1.12 | 1.77 | 0.10 | 0.68 |
| **A_n1q (A-R)** | **A_n1 + linear Q21** | **206** | **2** | 56 | 0.30 | 1.26 | 1.98 | 0.10 | 0.88 |
| A1 | A + rung 0 nonlinear + xtail | 284 | 2 | 75 | 0.09 | 0.75 | 1.29 | 0.14 | 0.77 |
| A1_n1 | A1 + gau1 + p32rung + N1 | 254 | 2 | 62 | 0.12 | 0.78 | 1.37 | 0.16 | 0.76 |
| A1_n1q | A1_n1 + linear Q21 | 228 | 2 | 60 | 0.14 | 0.94 | 1.56 | 0.19 | 0.92 |

### 4.1 Accepted reductions (in B-R)

| Reduction | Saves | Effect | Why it is safe |
|---|---:|---|---|
| **Network form N1** (§5) | ≈ 40 (with the others) | Ring decay 0.631 (SPICE 0.629); output 0.04–0.07 dB from the current network | The fast modes are slow enough for the block-rate solve once the per-sample asymptote poles sit near them |
| **[3/2] Padé on the rungs** (27, 9 are VFP immediates) | 18 | +0.0–0.1 dB | Rung arguments stay below 1.5 even at ×8, where [3/2] is within 2 % |
| **No rung clamp** | 4 | None | [3/2]'s secant stays in (1/9, 1], so it is stable for any argument |
| **s-asymptote to block rate** (`GAU1`) | 8 | None | The C17 input path's 1/s tail is below 0.5 % of the direct path above 100 Hz; the block-rate system now carries it with one block of latency |
| **Linear Q21** | 24 | +0.15 dB at ×4, none elsewhere | Its argument is at most 0.32 (tanh 3 % from linear); it also removes a secant from the batch and makes `at` constant |
| Input-pair tail (`XIN_TAIL`) | −7 (costs) | Corrects drive | §2.1 |

### 4.2 Rejected

| Reduction | Why |
|---|---|
| VCA side in a second pass (S093 P0 §6.3 R2) | Halves the loads and stores, but the second pass's own loop (and its division) costs more than it saves: +19 |
| Unroll by two (R5) | The compiler does not remove the loop-carried copies; register pressure rises: +8 to +16 |
| Rung secants every second sample | −10, but lowres ×4 error doubles (0.48 → 0.96) |
| Slow outputs held per block | −9, but a block-rate zipper: 1–2.3 dB |
| [3/2] on the input pair | The input pair carries the drive; ×4 error triples |
| [3/2] OTA | −7 for +0.05–0.11 dB. Kept as the one step that would take B-R into Basic (§6) |

---

## 5. Network form N1

**S093 P0's form (SplitNet):**
- per sample: two fast eigenmodes (the C21/C22 VCA-input high-passes, 72 Hz
  and 141–157 Hz), each feeding both outputs, plus one asymptote one-pole
  per output at 5 Hz;
- per block: the six slow modes.

Together the fast modes and their 13 coefficients cost about 57
instructions per sample (S093 P0 §6.1.1).

**N1:**
- all eight network states, the fast modes included, at block rate;
- per sample, only the instantaneous gains and **one asymptote one-pole per
  output, at a_u (loop) and a_v (VCA input)**;
- the block remainder is the network minus those two one-poles, as before.

The fast modes are well below the block rate's Nyquist (689 Hz). The
per-sample one-poles carry the network's high-frequency tail, which sets
the loop's damping and the VCA input's response above about 200 Hz.

**Pole choice** (Python model; option B; against the current network and
SPICE on the traces; ring on tp_pre):

| a_u (loop) | a_v (VCA) | Ring decay (golden 0.641, SPICE 0.629) | acid against current / SPICE (LSD dB) | lowres against current / SPICE |
|---:|---:|---:|---|---|
| 5 Hz | 5 Hz | 0.651 | 1.02 / — | 1.01 / — |
| 70 Hz | 70 Hz | 0.621 | 0.22 / — | 0.10 / — |
| 30 Hz | 100 Hz | 0.641 | 0.17 / 0.28 | 0.03 / 0.17 |
| 30 Hz | 150 Hz | 0.641 | 0.14 / 0.13 | 0.13 / 0.19 |
| **50 Hz** | **120 Hz** | **0.631** | **0.07 / 0.18** | **0.04 / 0.16** |
| 20 Hz | 120 Hz | 0.645 | 0.08 / 0.20 | 0.04 / 0.16 |

The current network is 0.16 / 0.16 from SPICE, so N1 at 50/120 Hz is as
close to SPICE as before, and its ring is closer to SPICE's.

**Tables:**
- `gen_n1.py` writes the N1 Res table in the same `DiodeLadderNet` layout,
  with the fast-mode fields zero.
- The asymptote constants differ per output: decay per block 0.796 (u) and
  0.579 (v).
- The firmware version replaces `alkResTable` and the asymptote constants;
  flash is unchanged (the same 464 B per point).

---

## 6. Where the candidates land

Per-sample instructions with the oscillator (24); VDIV in brackets. The
envelopes are S093 P0 §6.5's (Drum includes SRR; Alkaline has none).

| | Instructions (VDIV) | Against Drum (Basic) ≈ 257 (4) | Against Cymbal/HiHat ≈ 270 (4) |
|---|---:|---:|---:|
| B (S093 P0, with the drive fixes) | 349 (3) | +92 | +79 |
| **B-R** | **261 (3)** | +4 | **−9** |
| B-R + [3/2] OTA | 254 (3) | −3 | −16 |
| A1-R | 252 (2) | −5 | −18 |
| **A-R** | **230 (2)** | **−27** | −40 |

These are standalone loop counts. The binding gates are still the linked
mixer check and the device's `cpu` widget (P0 §1.3). Control-rate work is
unchanged from S093 P0 §6.2.

---

## 7. Next steps

1. **Port B-R into `Core/DSPAudio/DiodeLadder.c`**, replacing
   `DiodeLadder_renderB()`'s body:
   - N1;
   - [3/2] rung secants without clamp;
   - the s-asymptote at block rate;
   - linear Q21;
   - the input-pair tail.

   Keep A-R as `DiodeLadder_renderA()` so the Basic fallback stays
   measurable. Move `gen_n1.py`'s form into `gen_alk_tables.py` (the
   firmware table becomes N1) and set `DL_OUT_SCALE` to 7 V.
2. **Re-run the S093 P0 checks** on the ported kernel: the bench laws
   (AL-R's tolerance holds: ring 0.372), C1/C2 against SPICE, and the ARM
   count of the firmware object.
3. **Add the drive parameter** (E2): a `drv` row scaling `sScale` per
   block. Decide its range (×1 to ×8 is what was modelled) and default ×1.
4. **Integration** as S093 P0 §7, as an Advanced type with B-R. Then the
   linked mixer check and the device `cpu` widget against the worst-case
   Scene with two Advanced slots.

---

## 8. Decisions

| # | Decision | Status |
|---|---|---|
| **D2′** | The Alkaline filter | **Decided (user, S093): B-R, fitted under Drum.** The form is `B_n1rq_ota32`: B-R with the [3/2] OTA tanh, 230 per sample + 24 oscillator = 254, 3 VDIV, against Drum's ≈ 257. A-R stays as the documented fallback |
| **D2″** | Control path (added after measurement) | **Decided (user, S093): Tier 1 + Tier 2 control-path reductions** (`S093_P2` §3A). The figures in §0 and §6 above are audio-loop only; the per-block control path adds about 94 per sample as built and 16–19 after the reductions (`S093_P2` §8, §3A.4) |
| **D15** | Instrument class | **Decided (user, S093): Basic, for a listening trial.** Afterwards, either reduce further, or go Advanced (replacing Cymbal/HiHat in a Scene) and add post-distortion/FX types in the margin (B-R without the [3/2] OTA is 261, 9 inside Cymbal/HiHat) |
| **D16** | Drive range | ×1 to ×8 (modelled); the mapping is in `S093_P2` §6 |
| **D17** | Output full scale | 7 V |

The implementation brief is `S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md`.

---

## 9. Open items

| # | Item | Next |
|---|---|---|
| P1-O1 | All costs are standalone `-Ofast` loop counts | Linked image after the port (`elf_looppath.py`, `elf86_mixer_check`) |
| P1-O2 | The sound cases cover the first 0.6 s of two runs (Res 0.85 and 0.2) | After the port, the full C2 pattern check and the [SN] p7 law; a high-cutoff case |
| P1-O3 | The square wave under drive has not been run (the traces are the saw) | Add a square trace case to `run_variants.py` |
| P1-O4 | The drive's effect is measured as spectral distance only | Listening renders of B-R against A-R at ×4 and ×8 |
